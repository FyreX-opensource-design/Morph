#include "ui.h"

#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** CPU ARGB8888 buffer backed by a cairo image surface. */
struct comp_ui_buffer
{
	struct wlr_buffer base;
	cairo_surface_t *surface;
	void *data;
	size_t stride;
};

static void ui_buffer_destroy(struct wlr_buffer *wlr_buf)
{
	struct comp_ui_buffer *buf = wl_container_of(wlr_buf, buf, base);
	cairo_surface_destroy(buf->surface);
	free(buf);
}

static bool ui_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buf, uint32_t flags, void **data,
											uint32_t *format, size_t *stride)
{
	(void)flags;
	struct comp_ui_buffer *buf = wl_container_of(wlr_buf, buf, base);
	*data = buf->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = buf->stride;
	return true;
}

static void ui_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buf)
{
	(void)wlr_buf;
}

static const struct wlr_buffer_impl ui_buffer_impl = {
	.destroy = ui_buffer_destroy,
	.begin_data_ptr_access = ui_buffer_begin_data_ptr_access,
	.end_data_ptr_access = ui_buffer_end_data_ptr_access,
};

static struct comp_ui_buffer *ui_buffer_create(int width, int height)
{
	if (width < 1 || height < 1)
	{
		return NULL;
	}
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		return NULL;
	}
	struct comp_ui_buffer *buf = calloc(1, sizeof(*buf));
	if (!buf)
	{
		cairo_surface_destroy(surface);
		return NULL;
	}
	cairo_surface_flush(surface);
	buf->surface = surface;
	buf->data = cairo_image_surface_get_data(surface);
	buf->stride = (size_t)cairo_image_surface_get_stride(surface);
	wlr_buffer_init(&buf->base, &ui_buffer_impl, width, height);
	return buf;
}

/** Overlay is decorative: pointer events should reach the window behind it. */
static bool overlay_rejects_input(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	(void)sx;
	(void)sy;
	return false;
}

void comp_ui_overlay_init(struct comp_ui_overlay *ov, struct wlr_scene_tree *parent)
{
	if (!ov)
	{
		return;
	}
	memset(ov, 0, sizeof(*ov));
	if (!parent)
	{
		return;
	}
	ov->tree = wlr_scene_tree_create(parent);
	if (!ov->tree)
	{
		return;
	}
	ov->buffer = wlr_scene_buffer_create(ov->tree, NULL);
	if (!ov->buffer)
	{
		wlr_scene_node_destroy(&ov->tree->node);
		ov->tree = NULL;
		return;
	}
	ov->buffer->point_accepts_input = overlay_rejects_input;
	wlr_scene_node_set_enabled(&ov->tree->node, false);
}

void comp_ui_overlay_fini(struct comp_ui_overlay *ov)
{
	if (!ov)
	{
		return;
	}
	if (ov->tree)
	{
		wlr_scene_node_destroy(&ov->tree->node);
	}
	ov->tree = NULL;
	ov->buffer = NULL;
}

void comp_ui_overlay_hide(struct comp_ui_overlay *ov)
{
	if (!ov || !ov->tree)
	{
		return;
	}
	wlr_scene_node_set_enabled(&ov->tree->node, false);
	if (ov->buffer)
	{
		wlr_scene_buffer_set_buffer(ov->buffer, NULL);
	}
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (r > w / 2.0)
	{
		r = w / 2.0;
	}
	if (r > h / 2.0)
	{
		r = h / 2.0;
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2.0);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
	cairo_close_path(cr);
}

static const char *row_title(const struct comp_ui_switcher_row *row)
{
	if (row->title && row->title[0])
	{
		return row->title;
	}
	if (row->app_id && row->app_id[0])
	{
		return row->app_id;
	}
	return "(untitled)";
}

static const char *row_app_id(const struct comp_ui_switcher_row *row)
{
	if (row->app_id && row->app_id[0])
	{
		return row->app_id;
	}
	return NULL;
}

enum
{
	SWITCHER_MAX_VISIBLE = 8,
};

bool comp_ui_switcher_present(struct comp_ui_overlay *ov, const struct wlr_box *workarea, float scale,
							  const struct comp_ui_switcher_row *rows, size_t n, size_t selected)
{
	if (!ov || !ov->buffer || !workarea || !rows || n == 0)
	{
		return false;
	}
	if (scale < 1.0f)
	{
		scale = 1.0f;
	}
	if (selected >= n)
	{
		selected = n - 1;
	}

	const double s = (double)scale;
	const int pad = (int)lround(12.0 * s);
	const int row_h = (int)lround(40.0 * s);
	const int radius = (int)lround(10.0 * s);
	const int max_w = (int)lround(fmin(520.0 * s, workarea->width * 0.72 * s));
	size_t visible = n < SWITCHER_MAX_VISIBLE ? n : SWITCHER_MAX_VISIBLE;
	size_t first = 0;
	if (n > visible)
	{
		const size_t mid = visible / 2;
		if (selected >= n - (visible - mid))
		{
			first = n - visible;
		}
		else if (selected > mid)
		{
			first = selected - mid;
		}
	}
	const int buf_w = max_w < 160 ? 160 : max_w;
	const int buf_h = pad * 2 + (int)visible * row_h;
	if (buf_w < 1 || buf_h < 1)
	{
		return false;
	}

	struct comp_ui_buffer *buf = ui_buffer_create(buf_w, buf_h);
	if (!buf)
	{
		wlr_log(WLR_ERROR, "switcher: failed to allocate %dx%d overlay buffer", buf_w, buf_h);
		return false;
	}
	cairo_t *cr = cairo_create(buf->surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	rounded_rect(cr, 0.5, 0.5, buf_w - 1.0, buf_h - 1.0, radius);
	cairo_set_source_rgba(cr, 0.09, 0.09, 0.10, 0.94);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
	cairo_set_line_width(cr, 1.0 * s);
	cairo_stroke(cr);

	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *font = pango_font_description_from_string("sans");
	pango_font_description_set_absolute_size(font, 13.0 * s * PANGO_SCALE);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
	pango_layout_set_single_paragraph_mode(layout, TRUE);

	const int text_x = pad;
	const int text_w = buf_w - pad * 2;
	pango_layout_set_width(layout, text_w * PANGO_SCALE);

	for (size_t i = 0; i < visible; i++)
	{
		const size_t idx = first + i;
		const int y = pad + (int)i * row_h;
		const bool sel = idx == selected;
		if (sel)
		{
			rounded_rect(cr, pad - 4.0 * s, y + 2.0 * s, buf_w - 2.0 * (pad - 4.0 * s),
						 row_h - 4.0 * s, 6.0 * s);
			cairo_set_source_rgba(cr, 0.933, 0.310, 0.090, 1.0);
			cairo_fill(cr);
		}

		char line[512];
		const char *title = row_title(&rows[idx]);
		const char *app = row_app_id(&rows[idx]);
		if (app && strcmp(app, title) != 0)
		{
			snprintf(line, sizeof(line), "%s  ·  %s", title, app);
		}
		else
		{
			snprintf(line, sizeof(line), "%s", title);
		}
		pango_layout_set_text(layout, line, -1);

		int tw = 0, th = 0;
		pango_layout_get_pixel_size(layout, &tw, &th);
		(void)tw;
		cairo_move_to(cr, text_x, y + (row_h - th) / 2.0);
		if (sel)
		{
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
		}
		else
		{
			cairo_set_source_rgba(cr, 0.92, 0.92, 0.93, 1.0);
		}
		pango_cairo_show_layout(cr, layout);
	}

	pango_font_description_free(font);
	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_flush(buf->surface);

	const int dest_w = (int)lround((double)buf_w / s);
	const int dest_h = (int)lround((double)buf_h / s);
	const int lx = workarea->x + (workarea->width - dest_w) / 2;
	const int ly = workarea->y + (workarea->height - dest_h) / 2;
	wlr_scene_node_set_position(&ov->tree->node, lx, ly);
	wlr_scene_buffer_set_buffer(ov->buffer, &buf->base);
	wlr_scene_buffer_set_dest_size(ov->buffer, dest_w, dest_h);
	wlr_scene_node_set_enabled(&ov->tree->node, true);
	wlr_buffer_drop(&buf->base);
	return true;
}
