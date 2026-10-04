#pragma once

#include <stdbool.h>
#include <stddef.h>

struct wlr_box;
struct wlr_scene_tree;

/**
 * Compositor-owned overlay: a scene buffer parented above client surfaces.
 *
 * Input is rejected so the overlay is visual-only (pointer events pass through
 * to the window underneath). The first consumer is the Alt-Tab switcher; later
 * menus and SSD chrome can reuse the same present/hide path.
 */
struct comp_ui_overlay
{
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buffer;
};

void comp_ui_overlay_init(struct comp_ui_overlay *ov, struct wlr_scene_tree *parent);
void comp_ui_overlay_fini(struct comp_ui_overlay *ov);
void comp_ui_overlay_hide(struct comp_ui_overlay *ov);

struct comp_ui_switcher_row
{
	const char *title;
	const char *app_id;
};

/**
 * Paint a window list centered in `workarea` and attach it to `ov`.
 *
 * `scale` is the output scale so text stays sharp. `selected` is highlighted.
 * Returns false if the overlay cannot be presented (no size, no memory).
 */
bool comp_ui_switcher_present(struct comp_ui_overlay *ov, const struct wlr_box *workarea, float scale,
							  const struct comp_ui_switcher_row *rows, size_t n, size_t selected);
