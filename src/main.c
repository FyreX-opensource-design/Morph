#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <xdg-shell-protocol.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/region.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "config.h"
#include "crash_handler.h"
#include "ext_workspace.h"
#include "gamma_control.h"
#include "output_mgmt.h"
#include "output_power.h"
#include "server.h"
#include "virtual_input.h"

/**
 * Runtime state for one tablet tool cursor stream.
 *
 * Binds wlroots tool events to tablet-v2 objects and tracks tilt/emulation
 * state used while forwarding tool motion/tip/button input.
 */
struct comp_tablet_tool
{
	struct wlr_tablet_tool *wlr_tool;
	struct wlr_tablet_v2_tablet_tool *v2_tool;
	struct comp_tablet *tablet;
	double tilt_x, tilt_y;
	bool emulating_pointer_from_tip;
	struct wl_listener destroy;
	struct wl_listener set_cursor;
};

/** Per-client pointer constraint; freed on constraint destroy. */
struct comp_pointer_constraint
{
	struct comp_server *server;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
	struct wl_listener set_region;
};

/** Tracks one xdg_popup (menu/tooltip) for a toplevel; freed on popup destroy. */
struct comp_popup
{
	struct comp_toplevel *view;
	struct wlr_xdg_popup *wlr_popup;
	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener commit;
	struct wl_listener reposition;
};

/** Tracks one xdg_popup attached to a layer-surface subtree; freed on popup destroy. */
struct comp_layer_popup
{
	struct comp_layer *layer;
	struct wlr_xdg_popup *wlr_popup;
	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener commit;
	struct wl_listener reposition;
};

static void focus_toplevel(struct comp_server *server, struct comp_toplevel *toplevel);
static struct wlr_output *primary_wlr_output(struct comp_server *server);
static void process_cursor_motion(struct comp_server *server, uint32_t time_msec);
static void cursor_constrain(struct comp_server *server, struct wlr_pointer_constraint_v1 *constraint,
							 double sx, double sy);
static void apply_pointer_motion(struct comp_server *server, struct wlr_input_device *dev,
								 uint32_t time_msec, double dx, double dy, double dx_unaccel, double dy_unaccel);
static void pointer_constraint_handle_commit(struct wl_listener *listener, void *data);
static void cancel_active_grab(struct comp_server *server);
static void clear_keyboard_focus(struct comp_server *server);
static void begin_move(struct comp_server *server, struct comp_toplevel *view, bool swallow_left_release);
static bool toplevel_effectively_maximized(struct comp_toplevel *view);
static bool toplevel_can_direct_resize(struct comp_server *server, struct comp_toplevel *view);
static bool toplevel_get_hit_box(struct comp_toplevel *view, struct wlr_box *out);
static bool toplevel_get_resize_hit_box(struct comp_toplevel *view, struct wlr_box *out);
static bool toplevel_uses_client_side_resize(struct comp_toplevel *view);
static bool toplevel_needs_compositor_resize_fallback(struct comp_toplevel *view);
static void toplevel_set_resizing(struct comp_toplevel *view, bool resizing);
static void toplevel_constrain_resize(struct comp_toplevel *view, uint32_t edges,
								  int *x, int *y, int *width, int *height);
static void toplevel_update_resize_anchor(struct comp_toplevel *view);
static void toplevel_apply_resize_box(struct comp_toplevel *view, int x, int y, int width, int height);
static bool point_in_box(const struct wlr_box *box, double x, double y);
static uint32_t toplevel_resize_edges_at(struct comp_toplevel *view, double sx, double sy);
static uint32_t toplevel_resize_edges_at_cursor(struct comp_server *server, struct comp_toplevel *view);
static struct comp_toplevel *toplevel_near_cursor_resize_edge(struct comp_server *server);
static void begin_resize(struct comp_server *server, struct comp_toplevel *view, uint32_t edges);
static void ipc_fini(struct comp_server *server);
static bool ipc_init(struct comp_server *server);
static bool server_reload_config(struct comp_server *server);
static struct comp_toplevel **tile_sorted_views(struct comp_server *server, size_t *n_out);
static int tile_sorted_index(struct comp_toplevel **arr, size_t n, struct comp_toplevel *v);
static void toplevel_apply_decoration_mode(struct comp_toplevel *view);
static void toplevel_apply_requested_maximize(struct comp_toplevel *view);
static void toplevel_clamp_floating_box_to_workarea(struct comp_toplevel *view,
													int *scene_x, int *scene_y, int *width, int *height);
static struct comp_output *toplevel_preferred_output(struct comp_toplevel *view);
static struct comp_output *toplevel_tile_output(struct comp_toplevel *t);
static void foreign_toplevel_refresh(struct comp_toplevel *view);
static void foreign_toplevel_sync_all(struct comp_server *server);
void server_update_seat_capabilities(struct comp_server *server);
static void layer_surface_try_keyboard_focus_click(struct comp_server *server, double lx, double ly);
static void track_input_device(struct comp_server *server, struct wlr_input_device *dev);
static void input_device_apply_libinput_defaults(struct wlr_input_device *dev);
static struct comp_tablet *comp_tablet_from_wlr(struct comp_server *server, struct wlr_tablet *wt);
static struct comp_tablet_tool *tablet_tool_get_or_create(struct comp_server *srv, struct comp_tablet *tab,
														  struct wlr_tablet_tool *wtool);
static struct comp_output *comp_output_from_wlr(struct comp_server *server, struct wlr_output *wlr_out);

/** Remove a listener link only when currently attached to a signal. */
static void detach_listener_if_linked(struct wl_listener *listener)
{
	if (!listener)
	{
		return;
	}
	if (listener->link.prev && listener->link.next)
	{
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

/** Kept for teardown call-site stability; Xwayland is managed by satellite. */
static void server_destroy_xwayland(struct comp_server *server)
{
	(void)server;
}

/** Detach server-owned global listeners before wlroots object/display teardown. */
static void server_detach_global_listeners(struct comp_server *server)
{
	if (!server)
	{
		return;
	}

	/* Core compositor/server signals registered on backend, shell, and decoration managers. */
	detach_listener_if_linked(&server->backend_destroy);
	detach_listener_if_linked(&server->new_output);
	detach_listener_if_linked(&server->new_input);
	detach_listener_if_linked(&server->xdg_shell_new_toplevel);
	detach_listener_if_linked(&server->xdg_activation_request_activate);
	detach_listener_if_linked(&server->new_xdg_decoration);
	detach_listener_if_linked(&server->layer_shell_new_surface);

	/* Cursor input pipeline: relative/absolute motion, buttons, axes, touch, and tablet tool events. */
	detach_listener_if_linked(&server->cursor_motion);
	detach_listener_if_linked(&server->cursor_motion_absolute);
	detach_listener_if_linked(&server->cursor_button);
	detach_listener_if_linked(&server->cursor_axis);
	detach_listener_if_linked(&server->cursor_frame);
	detach_listener_if_linked(&server->cursor_touch_down);
	detach_listener_if_linked(&server->cursor_touch_up);
	detach_listener_if_linked(&server->cursor_touch_motion);
	detach_listener_if_linked(&server->cursor_touch_cancel);
	detach_listener_if_linked(&server->cursor_touch_frame);
	detach_listener_if_linked(&server->cursor_tablet_tool_axis);
	detach_listener_if_linked(&server->cursor_tablet_tool_proximity);
	detach_listener_if_linked(&server->cursor_tablet_tool_tip);
	detach_listener_if_linked(&server->cursor_tablet_tool_button);

	/* Seat/protocol control hooks: cursor requests, selection ownership, and pointer constraints. */
	detach_listener_if_linked(&server->seat_request_cursor);
	detach_listener_if_linked(&server->seat_request_set_selection);
	detach_listener_if_linked(&server->seat_request_set_primary_selection);
	detach_listener_if_linked(&server->seat_pointer_focus_change);
	detach_listener_if_linked(&server->new_pointer_constraint);
	detach_listener_if_linked(&server->pointer_constraint_commit);
	output_power_fini(server);
	virtual_input_fini(server);
	output_mgmt_fini(server);
}

/** Return the root wl_surface for an XDG or Xwayland toplevel, or NULL if unavailable. */
static struct wlr_surface *toplevel_wlr_surface(const struct comp_toplevel *v)
{
	return v->xdg_toplevel ? v->xdg_toplevel->base->surface : NULL;
}

/** True when the toplevel has a mapped root surface. */
static bool toplevel_surface_mapped(const struct comp_toplevel *v)
{
	struct wlr_surface *s = toplevel_wlr_surface(v);
	return s && s->mapped;
}

/** True when the toplevel is initialized enough for compositor-driven configure requests. */
static bool toplevel_surface_initialized(const struct comp_toplevel *v)
{
	return v->xdg_toplevel && v->xdg_toplevel->base->initialized;
}

/** Remember the newest compositor configure so client commits can be matched to it. */
static void toplevel_track_configure(struct comp_toplevel *view, uint32_t serial)
{
	if (view && serial != 0)
	{
		view->pending_configure_serial = serial;
	}
}

/** Request an XDG size and retain its configure serial for commit reconciliation. */
static void toplevel_set_size(struct comp_toplevel *view, int width, int height)
{
	if (!view || !view->xdg_toplevel)
	{
		return;
	}
	toplevel_track_configure(view,
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height));
}

/**
 * Send one configure carrying both maximize state and size.
 *
 * Clients such as VS Code can react badly if the maximize bit and target size arrive as
 * separate configures. Sending both together prevents a client from acknowledging an
 * intermediate half-maximized state with stale geometry.
 */
static void toplevel_set_maximized_size(struct comp_toplevel *view, bool maximized, int width, int height)
{
	if (!view || !view->xdg_toplevel)
	{
		return;
	}
	const struct wlr_xdg_toplevel_state current = view->xdg_toplevel->current;
	struct wlr_xdg_toplevel_configure configure = {
		.maximized = maximized,
		.fullscreen = current.fullscreen,
		.resizing = current.resizing,
		.activated = current.activated,
		.suspended = current.suspended,
		.tiled = current.tiled,
		.width = width,
		.height = height,
	};
	toplevel_track_configure(view,
		wlr_xdg_toplevel_configure(view->xdg_toplevel, &configure));
}

/** Extract normalized title/app fields for config/rule matching across backends. */
static void toplevel_title_app_for_config(const struct comp_toplevel *v, const char **title_out,
										  const char **app_out)
{
	*title_out = v->xdg_toplevel && v->xdg_toplevel->title ? v->xdg_toplevel->title : "";
	*app_out = v->xdg_toplevel && v->xdg_toplevel->app_id ? v->xdg_toplevel->app_id : "";
}

/** Apply activation state using the correct backend path (XDG or Xwayland). */
static void toplevel_set_activated(struct comp_toplevel *v, bool activated)
{
	if (v->xdg_toplevel)
	{
		toplevel_track_configure(v,
			wlr_xdg_toplevel_set_activated(v->xdg_toplevel, activated));
	}
}

/** Minimized views are hidden until explicit re-activation. */
static void toplevel_set_minimized(struct comp_toplevel *view, bool minimized)
{
	if (!view || view->minimized == minimized)
	{
		return;
	}
	view->minimized = minimized;
	if (minimized && view->server->focused_toplevel == view)
	{
		clear_keyboard_focus(view->server);
	}
	server_workspace_apply_visibility(view->server);
	if ((view->server->layout == COMP_LAYOUT_TILE || view->server->layout == COMP_LAYOUT_SCROLL) &&
		view->server->grab != COMP_GRAB_MOVE)
	{
		server_arrange_toplevels(view->server);
	}
	foreign_toplevel_refresh(view);
}

/** Tile/scroll: resize client; xdg ignores compositor x/y (scene positions the surface). */
static void toplevel_arrange_tile(struct comp_toplevel *v, int layout_x, int layout_y, int w, int h)
{
	(void)layout_x;
	(void)layout_y;
	if (v->xdg_toplevel)
	{
		toplevel_set_size(v, w, h);
	}
}

/** True after `wlr_backend_start` so shutdown hook runs only for a real session. */
static bool compositor_session_active;
/** Verbose XDG lifecycle logs (off by default). Enable with `MORPH_DEBUG_XDG=1`. */
static bool xdg_debug_logs_enabled;
/** Extra per-commit XDG trace; too noisy for normal live compositor debugging. */
static bool xdg_commit_debug_logs_enabled;
/** Focus-only pointer trace for diagnosing layer-shell hover and leave/enter churn. */
static bool pointer_focus_debug_logs_enabled;
/** Layer-shell hitbox trace used when panel hover regions fight toplevel hit-testing. */
static bool layer_hit_debug_logs_enabled;
/** Pace legacy X11 bridge resizes so GTK2 can process ConfigureNotify without a backlog. */
static uint32_t bridge_resize_interval_msec = 59;
/** Extra pointer-focus band for bottom/top panels whose hover visuals extend past their surface. */
static const int layer_pointer_guard_px = 40;
/** Optional append-only log target set by `--log-file`; NULL means stderr-only. */
static FILE *morph_log_file;
/** Active startup log threshold used by our callback for explicit filtering. */
static enum wlr_log_importance morph_active_log_level = WLR_INFO;

/**
 * Apply an optional resize frequency override for legacy X11 bridge clients.
 *
 * Lower values pace configure events more aggressively for slow toolkits; higher values
 * feel more immediate but can recreate the resize backlog that causes stale repaint loops.
 */
static void configure_bridge_resize_rate_from_env(void)
{
	const char *value = getenv("MORPH_BRIDGE_RESIZE_HZ");
	if (!value || !value[0])
	{
		return;
	}

	errno = 0;
	char *end = NULL;
	const long hz = strtol(value, &end, 10);
	if (errno == ERANGE || !end || *end || hz < 1 || hz > 240)
	{
		wlr_log(WLR_ERROR,
			"Ignoring invalid MORPH_BRIDGE_RESIZE_HZ='%s' (expected 1..240)", value);
		return;
	}

	/* Round to the nearest millisecond so integer hertz values map predictably. */
	bridge_resize_interval_msec = (uint32_t)((1000 + hz / 2) / hz);
	wlr_log(WLR_INFO, "Legacy bridge resize rate: %ld Hz (%u ms)",
		hz, bridge_resize_interval_msec);
}

/** Map wlroots importance to an ordered rank for deterministic threshold checks. */
static int morph_log_level_rank(enum wlr_log_importance importance)
{
	switch (importance)
	{
	case WLR_ERROR:
		return 1;
	case WLR_INFO:
		return 2;
	case WLR_DEBUG:
		return 3;
	case WLR_SILENT:
	default:
		return 0;
	}
}

/** Close the optional startup log file (registered via atexit). */
static void morph_log_close_file(void)
{
	if (morph_log_file)
	{
		fclose(morph_log_file);
		morph_log_file = NULL;
	}
}

/** Map wlroots log importance to a short stable label used in our log prefix. */
static const char *morph_log_level_name(enum wlr_log_importance importance)
{
	switch (importance)
	{
	case WLR_ERROR:
		return "ERROR";
	case WLR_INFO:
		return "INFO";
	case WLR_DEBUG:
		return "DEBUG";
	case WLR_SILENT:
	default:
		return "SILENT";
	}
}

/**
 * wlroots logger callback used for both stderr and optional file logging.
 *
 * The callback is invoked with a single `va_list`; we must copy it before each
 * sink write because consuming a `va_list` is destructive.
 */
static void morph_log_callback(enum wlr_log_importance importance, const char *fmt, va_list args)
{
	const int active = morph_log_level_rank(morph_active_log_level);
	const int msg = morph_log_level_rank(importance);
	if (active == 0 || msg > active)
	{
		return;
	}

	char ts[32] = "";
	time_t now = time(NULL);
	struct tm tm_now;
	if (localtime_r(&now, &tm_now))
	{
		strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
	}
	const char *lvl = morph_log_level_name(importance);

	/* First sink: stderr, always active for local debugging and systemd journals. */
	va_list a_stderr;
	va_copy(a_stderr, args);
	if (ts[0])
	{
		fprintf(stderr, "[%s] %s: ", ts, lvl);
	}
	else
	{
		fprintf(stderr, "%s: ", lvl);
	}
	vfprintf(stderr, fmt, a_stderr);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(a_stderr);

	if (!morph_log_file)
	{
		return;
	}
	/* Second sink: user-selected file path from `--log-file`. */
	va_list a_file;
	va_copy(a_file, args);
	if (ts[0])
	{
		fprintf(morph_log_file, "[%s] %s: ", ts, lvl);
	}
	else
	{
		fprintf(morph_log_file, "%s: ", lvl);
	}
	vfprintf(morph_log_file, fmt, a_file);
	fputc('\n', morph_log_file);
	fflush(morph_log_file);
	va_end(a_file);
}

/** Parse user-facing log level tokens into wlroots importance values. */
static bool morph_parse_log_level(const char *s, enum wlr_log_importance *out)
{
	if (!s || !out)
	{
		return false;
	}
	if (!strcasecmp(s, "silent") || !strcasecmp(s, "quiet") || !strcasecmp(s, "off"))
	{
		*out = WLR_SILENT;
		return true;
	}
	if (!strcasecmp(s, "error") || !strcasecmp(s, "err"))
	{
		*out = WLR_ERROR;
		return true;
	}
	if (!strcasecmp(s, "info"))
	{
		*out = WLR_INFO;
		return true;
	}
	if (!strcasecmp(s, "debug"))
	{
		*out = WLR_DEBUG;
		return true;
	}
	return false;
}

/** Optional verbose lifecycle trace for XDG state transitions. */
static void log_xdg_state(const char *tag, struct comp_toplevel *view)
{
	if (!xdg_debug_logs_enabled || !view || !view->xdg_toplevel || !view->xdg_toplevel->base)
	{
		return;
	}
	struct wlr_xdg_surface *xdg = view->xdg_toplevel->base;
	struct wlr_xdg_toplevel *toplevel = view->xdg_toplevel;
	struct wlr_xdg_toplevel *parent = toplevel->parent;
	const struct wlr_box *geo = &xdg->geometry;
	struct wlr_box extents = {0};
	if (xdg->surface)
	{
		wlr_surface_get_extents(xdg->surface, &extents);
	}
	const int surf_w = xdg->surface ? xdg->surface->current.width : 0;
	const int surf_h = xdg->surface ? xdg->surface->current.height : 0;
	wlr_log(WLR_INFO,
			"xdgdbg:%s app_id='%s' title='%s' mapped=%d initialized=%d initial_commit=%d layout=%d "
			"scene=%d,%d geo=%d,%d %dx%d surf=%dx%d ext=%d,%d %dx%d "
			"parent=%d parent_app_id='%s' parent_title='%s' "
			"constraints_cur=min:%dx%d,max:%dx%d constraints_pending=min:%dx%d,max:%dx%d",
			tag,
			toplevel->app_id ? toplevel->app_id : "",
			toplevel->title ? toplevel->title : "",
			xdg->surface->mapped, xdg->initialized, xdg->initial_commit, (int)view->server->layout,
			view->scene_tree ? view->scene_tree->node.x : 0,
			view->scene_tree ? view->scene_tree->node.y : 0,
			geo->x, geo->y, geo->width, geo->height, surf_w, surf_h,
			extents.x, extents.y, extents.width, extents.height,
			parent ? 1 : 0,
			parent && parent->app_id ? parent->app_id : "",
			parent && parent->title ? parent->title : "",
			toplevel->current.min_width, toplevel->current.min_height,
			toplevel->current.max_width, toplevel->current.max_height,
			toplevel->pending.min_width, toplevel->pending.min_height,
			toplevel->pending.max_width, toplevel->pending.max_height);
}

/** Optional verbose trace for compositor-owned interactive resize state. */
static void log_resize_state(const char *tag, struct comp_toplevel *view, int x, int y, int w, int h,
							 uint32_t edges)
{
	if (!xdg_debug_logs_enabled || !view || !view->xdg_toplevel || !view->xdg_toplevel->base)
	{
		return;
	}
	const struct wlr_box *geo = &view->xdg_toplevel->base->geometry;
	struct wlr_surface *surf = view->xdg_toplevel->base->surface;
	struct wlr_box extents = {0};
	if (surf)
	{
		wlr_surface_get_extents(surf, &extents);
	}
	wlr_log(WLR_INFO,
			"resizedbg:%s app_id='%s' title='%s' target=%d,%d %dx%d edges=0x%x scene=%d,%d "
			"geo=%d,%d %dx%d surf=%dx%d ext=%d,%d %dx%d parent=%d fallback=%d "
			"constraints_cur=min:%dx%d,max:%dx%d "
			"constraints_pending=min:%dx%d,max:%dx%d resizing_cur=%d resizing_pending=%d "
			"configure_cur=%u configure_pending=%u",
			tag,
			view->xdg_toplevel->app_id ? view->xdg_toplevel->app_id : "",
			view->xdg_toplevel->title ? view->xdg_toplevel->title : "",
			x, y, w, h, edges,
			view->scene_tree ? view->scene_tree->node.x : 0,
			view->scene_tree ? view->scene_tree->node.y : 0,
			geo->x, geo->y, geo->width, geo->height,
			surf ? surf->current.width : 0, surf ? surf->current.height : 0,
			extents.x, extents.y, extents.width, extents.height,
			view->xdg_toplevel->parent ? 1 : 0,
			toplevel_needs_compositor_resize_fallback(view) ? 1 : 0,
			view->xdg_toplevel->current.min_width, view->xdg_toplevel->current.min_height,
			view->xdg_toplevel->current.max_width, view->xdg_toplevel->current.max_height,
			view->xdg_toplevel->pending.min_width, view->xdg_toplevel->pending.min_height,
			view->xdg_toplevel->pending.max_width, view->xdg_toplevel->pending.max_height,
			view->xdg_toplevel->current.resizing ? 1 : 0,
			view->xdg_toplevel->pending.resizing ? 1 : 0,
			view->xdg_toplevel->base->current.configure_serial,
			view->pending_configure_serial);
}

/** Log one line when a new xdg_toplevel enters Morph, including capability gating context. */
static void log_new_toplevel_state(struct wlr_xdg_toplevel *xdg_toplevel, uint32_t shell_ver, bool wm_caps_enabled)
{
	if (!xdg_debug_logs_enabled || !xdg_toplevel || !xdg_toplevel->base)
	{
		return;
	}
	struct wlr_xdg_surface *xdg = xdg_toplevel->base;
	struct wlr_xdg_toplevel *parent = xdg_toplevel->parent;
	const struct wlr_box *geo = &xdg->geometry;
	const struct wlr_surface *surf = xdg->surface;
	wlr_log(WLR_INFO,
			"xdgdbg:new_toplevel app_id='%s' title='%s' shell_v=%u wm_caps=%d "
			"mapped=%d initialized=%d initial_commit=%d geo=%d,%d %dx%d surf=%dx%d "
			"parent=%d parent_app_id='%s' parent_title='%s' "
			"constraints_cur=min:%dx%d,max:%dx%d constraints_pending=min:%dx%d,max:%dx%d",
			xdg_toplevel->app_id ? xdg_toplevel->app_id : "",
			xdg_toplevel->title ? xdg_toplevel->title : "",
			shell_ver, wm_caps_enabled ? 1 : 0,
			surf ? surf->mapped : 0, xdg->initialized, xdg->initial_commit,
			geo->x, geo->y, geo->width, geo->height,
			surf ? surf->current.width : 0, surf ? surf->current.height : 0,
			parent ? 1 : 0,
			parent && parent->app_id ? parent->app_id : "",
			parent && parent->title ? parent->title : "",
			xdg_toplevel->current.min_width, xdg_toplevel->current.min_height,
			xdg_toplevel->current.max_width, xdg_toplevel->current.max_height,
			xdg_toplevel->pending.min_width, xdg_toplevel->pending.min_height,
			xdg_toplevel->pending.max_width, xdg_toplevel->pending.max_height);
}

/** Find compositor output wrapper by wlroots output pointer. */
static struct comp_output *comp_output_from_wlr(struct comp_server *server, struct wlr_output *wlr_out)
{
	if (!wlr_out)
	{
		return NULL;
	}
	struct comp_output *o;
	wl_list_for_each(o, &server->outputs, link)
	{
		if (o->wlr_output == wlr_out)
		{
			return o;
		}
	}
	return NULL;
}

/** Pick the most plausible output for stack/floating placement and maximized workarea sizing. */
static struct comp_output *toplevel_preferred_output(struct comp_toplevel *view)
{
	struct comp_server *server = view->server;
	const struct wlr_box *geo = &view->xdg_toplevel->base->geometry;
	const double cx = (double)view->scene_tree->node.x + (double)geo->width * 0.5;
	const double cy = (double)view->scene_tree->node.y + (double)geo->height * 0.5;
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, cx, cy);
	if (!out)
	{
		out = wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
	}
	if (!out)
	{
		out = primary_wlr_output(server);
	}
	return comp_output_from_wlr(server, out);
}

/**
 * Update one cached foreign-toplevel string and report whether clients need a protocol update.
 *
 * wlroots forwards every setter to clients, so avoiding unchanged strings prevents panels
 * from repainting their task list continuously while active/maximized state is stable.
 */
static bool foreign_toplevel_cache_string(char **cached, const char *value)
{
	const char *safe_value = value ? value : "";
	if (*cached && !strcmp(*cached, safe_value))
	{
		return false;
	}

	char *copy = strdup(safe_value);
	if (!copy)
	{
		wlr_log_errno(WLR_ERROR, "strdup foreign-toplevel metadata");
		return false;
	}
	free(*cached);
	*cached = copy;
	return true;
}

/**
 * Push current title/app_id/activation metadata to foreign-toplevel clients.
 *
 * This function is deliberately edge-triggered. Some panels redraw on every foreign
 * toplevel update even if the payload is identical; caching here keeps hover and active
 * visuals stable under clients that commit frequently.
 */
static void foreign_toplevel_refresh(struct comp_toplevel *view)
{
	if (!view || !view->foreign_toplevel)
	{
		return;
	}
	const char *title = "";
	const char *app_id = "";
	toplevel_title_app_for_config(view, &title, &app_id);
	if (foreign_toplevel_cache_string(&view->foreign_title, title))
	{
		wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, view->foreign_title);
	}
	if (foreign_toplevel_cache_string(&view->foreign_app_id, app_id))
	{
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, view->foreign_app_id);
	}
	const bool activated = view->server->focused_toplevel == view && toplevel_surface_mapped(view) &&
						   view->workspace == view->server->current_workspace;
	const bool maximized = view->xdg_toplevel && view->xdg_toplevel->current.maximized;
	const bool fullscreen = view->xdg_toplevel && view->xdg_toplevel->current.fullscreen;
	if (!view->foreign_state_valid || view->foreign_activated != activated)
	{
		wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel, activated);
		view->foreign_activated = activated;
	}
	if (!view->foreign_state_valid || view->foreign_maximized != maximized)
	{
		wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, maximized);
		view->foreign_maximized = maximized;
	}
	if (!view->foreign_state_valid || view->foreign_fullscreen != fullscreen)
	{
		wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, fullscreen);
		view->foreign_fullscreen = fullscreen;
	}
	if (!view->foreign_state_valid || view->foreign_minimized != view->minimized)
	{
		wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, view->minimized);
		view->foreign_minimized = view->minimized;
	}
	view->foreign_state_valid = true;
}

/** Refresh foreign-toplevel metadata for all known toplevels. */
static void foreign_toplevel_sync_all(struct comp_server *server)
{
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		foreign_toplevel_refresh(t);
	}
}

/** Seconds since CLOCK_MONOTONIC epoch (for layout animation delta time). */
static uint64_t timespec_to_ns(const struct timespec *ts)
{
	return (uint64_t)ts->tv_sec * 1000000000ull + (uint64_t)ts->tv_nsec;
}

/** True when layout animation is enabled in config and the server has config loaded. */
static bool layout_anim_effective(const struct comp_server *server)
{
	return server->config && server->config->layout_anim_enabled;
}

/** Returns true if any tiled view still needs another frame to reach its target. */
static bool layout_anim_tick(struct comp_server *server, const struct timespec *now)
{
	if (server->layout != COMP_LAYOUT_TILE && server->layout != COMP_LAYOUT_SCROLL)
	{
		return false;
	}
	if (!layout_anim_effective(server))
	{
		return false;
	}
	const double lambda = server->config->layout_anim_lambda;
	const double eps = server->config->layout_anim_epsilon;
	const uint64_t now_ns = timespec_to_ns(now);
	if (server->layout_anim_last_ns == 0)
	{
		server->layout_anim_last_ns = now_ns;
		return false;
	}
	double dt = (double)(now_ns - server->layout_anim_last_ns) / 1e9;
	server->layout_anim_last_ns = now_ns;
	if (dt <= 0.0)
	{
		return false;
	}
	if (dt > 0.1)
	{
		dt = 0.1;
	}
	const double k = 1.0 - exp(-lambda * dt);
	bool any = false;
	struct comp_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link)
	{
		if (!v->layout_anim_tracked)
		{
			continue;
		}
		if (v->workspace != server->current_workspace)
		{
			continue;
		}
		if (!toplevel_surface_mapped(v) || v->tile_float)
		{
			continue;
		}
		if (server->grab == COMP_GRAB_MOVE && v == server->grabbed_toplevel)
		{
			continue;
		}
		const double dx = (double)v->layout_tgt_x - v->layout_anim_x;
		const double dy = (double)v->layout_tgt_y - v->layout_anim_y;
		if (fabs(dx) < eps && fabs(dy) < eps)
		{
			if (v->scene_tree->node.x != v->layout_tgt_x || v->scene_tree->node.y != v->layout_tgt_y)
			{
				wlr_scene_node_set_position(&v->scene_tree->node, v->layout_tgt_x, v->layout_tgt_y);
				any = true;
			}
			v->layout_anim_x = (double)v->layout_tgt_x;
			v->layout_anim_y = (double)v->layout_tgt_y;
			continue;
		}
		v->layout_anim_x += dx * k;
		v->layout_anim_y += dy * k;
		const int ix = (int)lround(v->layout_anim_x);
		const int iy = (int)lround(v->layout_anim_y);
		wlr_scene_node_set_position(&v->scene_tree->node, ix, iy);
		any = true;
	}
	return any;
}

/** After targets change, ensure outputs repaint even if no client buffer update occurs yet. */
static void layout_anim_kick_outputs(struct comp_server *server)
{
	if (server->layout != COMP_LAYOUT_TILE && server->layout != COMP_LAYOUT_SCROLL)
	{
		return;
	}
	if (!layout_anim_effective(server))
	{
		return;
	}
	const double eps = server->config->layout_anim_epsilon;
	struct comp_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link)
	{
		if (!v->layout_anim_tracked || !toplevel_surface_mapped(v) || v->tile_float)
		{
			continue;
		}
		if (v->workspace != server->current_workspace)
		{
			continue;
		}
		if (server->grab == COMP_GRAB_MOVE && v == server->grabbed_toplevel)
		{
			continue;
		}
		if (fabs((double)v->layout_tgt_x - v->layout_anim_x) > eps ||
			fabs((double)v->layout_tgt_y - v->layout_anim_y) > eps)
		{
			struct comp_output *o;
			wl_list_for_each(o, &server->outputs, link)
			{
				wlr_output_schedule_frame(o->wlr_output);
			}
			return;
		}
	}
}

/* Hit surface under (lx, ly); sx/sy outputs are surface-local coords when non-NULL. */
static struct wlr_surface *surface_at(struct comp_server *server, double lx, double ly,
									  double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER)
	{
		return NULL;
	}
	struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buf);
	return ss ? ss->surface : NULL;
}

/** Hit-test and resolve the compositor toplevel at layout coordinates. */
static struct comp_toplevel *toplevel_at(struct comp_server *server, double lx, double ly, double *sx, double *sy)
{
	struct wlr_surface *surf = surface_at(server, lx, ly, sx, sy);
	if (!surf)
	{
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surf);
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (t->xdg_toplevel && t->xdg_toplevel->base->surface == root)
		{
			return t;
		}
	}
	return NULL;
}

/**
 * True when a visible panel-like layer surface owns the point plus an optional edge guard.
 *
 * The guard compensates for panels whose visual hover area, shadow, or input feedback
 * extends a few pixels beyond the committed layer-surface buffer.
 */
static bool blocking_layer_surface_at_guard(struct comp_server *server, double lx, double ly, int guard)
{
	if (!server)
	{
		return false;
	}
	struct comp_layer *layer;
	wl_list_for_each(layer, &server->layers, link)
	{
		struct wlr_layer_surface_v1 *ls = layer->layer_surface;
		if (!ls || !ls->surface || !ls->surface->mapped || !layer->scene_layer || !layer->scene_layer->tree)
		{
			continue;
		}
		const enum zwlr_layer_shell_v1_layer lyr = ls->current.layer;
		const bool visible_above_windows = lyr == ZWLR_LAYER_SHELL_V1_LAYER_TOP ||
										   lyr == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
		if (!visible_above_windows && ls->current.exclusive_zone <= 0)
		{
			continue;
		}
		struct wlr_box box = {
			.x = layer->scene_layer->tree->node.x,
			.y = layer->scene_layer->tree->node.y,
			.width = (int)ls->current.actual_width,
			.height = (int)ls->current.actual_height,
		};
		if (ls->current.exclusive_zone > 0 && guard > 0)
		{
			/* GTK layer-shell panels can draw hover backgrounds or shadows a few pixels
			 * past the committed surface bounds. Treat that edge band as panel-owned so
			 * pointer focus does not flap between the panel and the window below it. */
			const uint32_t anchor = ls->current.anchor;
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP))
			{
				box.y -= guard;
				box.height += guard;
			}
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
			{
				box.height += guard;
			}
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT))
			{
				box.x -= guard;
				box.width += guard;
			}
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
			{
				box.width += guard;
			}
		}
		if (box.width > 0 && box.height > 0 && point_in_box(&box, lx, ly))
		{
			return true;
		}
	}
	return false;
}

/** True when a visible panel-like layer surface owns the point, including Morph's panel guard. */
static bool blocking_layer_surface_at(struct comp_server *server, double lx, double ly)
{
	return blocking_layer_surface_at_guard(server, lx, ly, layer_pointer_guard_px);
}

/** Stable layer-shell layer name for debug logs. */
static const char *layer_debug_layer_name(enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer)
	{
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return "background";
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return "bottom";
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return "top";
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return "overlay";
	default:
		return "unknown";
	}
}

/** Expand a box in all directions for proximity-only debug filtering. */
static struct wlr_box layer_debug_expanded_box(struct wlr_box box, int margin)
{
	box.x -= margin;
	box.y -= margin;
	box.width += margin * 2;
	box.height += margin * 2;
	return box;
}

/** Log layer-shell geometry near a pointer-focus transition without changing hit-test behavior. */
static void layer_debug_log_hit_context(struct comp_server *server, const char *old_role,
										const char *new_role, double lx, double ly)
{
	if (!layer_hit_debug_logs_enabled || !server)
	{
		return;
	}
	if (strcmp(old_role, "layer") && strcmp(new_role, "layer"))
	{
		return;
	}

	struct comp_layer *layer;
	wl_list_for_each(layer, &server->layers, link)
	{
		struct wlr_layer_surface_v1 *ls = layer->layer_surface;
		if (!ls || !ls->surface || !ls->surface->mapped || !layer->scene_layer || !layer->scene_layer->tree)
		{
			continue;
		}
		const enum zwlr_layer_shell_v1_layer layer_type = ls->current.layer;
		const bool visible_above_windows = layer_type == ZWLR_LAYER_SHELL_V1_LAYER_TOP ||
										   layer_type == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
		if (!visible_above_windows && ls->current.exclusive_zone <= 0)
		{
			continue;
		}

		struct wlr_box surface_box = {
			.x = layer->scene_layer->tree->node.x,
			.y = layer->scene_layer->tree->node.y,
			.width = (int)ls->current.actual_width,
			.height = (int)ls->current.actual_height,
		};
		struct wlr_box guard_box = surface_box;
		if (ls->current.exclusive_zone > 0)
		{
			const uint32_t anchor = ls->current.anchor;
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP))
			{
				guard_box.y -= layer_pointer_guard_px;
				guard_box.height += layer_pointer_guard_px;
			}
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
			{
				guard_box.height += layer_pointer_guard_px;
			}
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT))
			{
				guard_box.x -= layer_pointer_guard_px;
				guard_box.width += layer_pointer_guard_px;
			}
			if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
				!(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
			{
				guard_box.width += layer_pointer_guard_px;
			}
		}

		struct wlr_box near_box = layer_debug_expanded_box(guard_box, 96);
		if (!point_in_box(&near_box, lx, ly))
		{
			continue;
		}

		struct comp_output *out = comp_output_from_wlr(server, ls->output);
		struct wlr_box workarea = {0};
		if (out)
		{
			workarea = out->layer_workarea;
		}
		wlr_log(WLR_INFO,
				"layerhit:focus old=%s new=%s cursor=%.1f,%.1f ns='%s' layer=%s anchor=0x%x exclusive=%d surface=%d,%d %dx%d guard=%d,%d %dx%d in_surface=%d in_guard=%d workarea=%d,%d %dx%d",
				old_role, new_role, lx, ly, ls->namespace ? ls->namespace : "",
				layer_debug_layer_name(layer_type), ls->current.anchor,
				ls->current.exclusive_zone, surface_box.x, surface_box.y,
				surface_box.width, surface_box.height, guard_box.x, guard_box.y,
				guard_box.width, guard_box.height, point_in_box(&surface_box, lx, ly),
				point_in_box(&guard_box, lx, ly), workarea.x, workarea.y,
				workarea.width, workarea.height);
	}
}

/** True when the current pointer focus belongs to a layer-shell surface. */
static bool pointer_focus_is_layer_surface(struct comp_server *server)
{
	if (!server || !server->seat || !server->seat->pointer_state.focused_surface)
	{
		return false;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(server->seat->pointer_state.focused_surface);
	return wlr_layer_surface_v1_try_from_wlr_surface(root) != NULL;
}

/** Per-output frame handler: drive scene commit and synchronized layout animation repaint. */
static void output_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_output *output = wl_container_of(listener, output, frame);
	struct comp_server *server = output->server;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct wlr_output *clock_out = primary_wlr_output(server);
	const bool run_layout_anim = clock_out && output->wlr_output == clock_out;
	const bool layout_anim = run_layout_anim && layout_anim_tick(server, &now);

	if (!wlr_scene_output_needs_frame(output->scene_output) && !layout_anim)
	{
		return;
	}

	if (!wlr_scene_output_commit(output->scene_output, NULL))
	{
		if (layout_anim)
		{
			struct comp_output *o;
			wl_list_for_each(o, &server->outputs, link)
			{
				wlr_output_schedule_frame(o->wlr_output);
			}
		}
		return;
	}
	wlr_scene_output_send_frame_done(output->scene_output, &now);

	if (layout_anim)
	{
		struct comp_output *o;
		wl_list_for_each(o, &server->outputs, link)
		{
			wlr_output_schedule_frame(o->wlr_output);
		}
	}
}

/** Choose primary output (cursor output first, then first registered output). */
static struct wlr_output *primary_wlr_output(struct comp_server *server)
{
	if (wl_list_empty(&server->outputs))
	{
		return NULL;
	}
	if (server->cursor)
	{
		struct wlr_output *at = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
															server->cursor->y);
		if (at)
		{
			return at;
		}
	}
	struct comp_output *co = wl_container_of(server->outputs.next, co, link);
	return co->wlr_output;
}

/** Recompute layer-shell layout and per-output usable workareas. */
static void layer_shell_arrange(struct comp_server *server)
{
	static const enum zwlr_layer_shell_v1_layer layer_order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	};
	struct comp_output *out;
	wl_list_for_each(out, &server->outputs, link)
	{
		struct wlr_box full;
		wlr_output_layout_get_box(server->output_layout, out->wlr_output, &full);
		struct wlr_box usable = full;
		for (size_t li = 0; li < sizeof(layer_order) / sizeof(layer_order[0]); li++)
		{
			struct comp_layer *layer;
			wl_list_for_each(layer, &server->layers, link)
			{
				if (layer->layer_surface->output != out->wlr_output)
				{
					continue;
				}
				/* Before the first ack, `current` may not match the client's layer yet. */
				const enum zwlr_layer_shell_v1_layer lyr = layer->layer_surface->configured
															   ? layer->layer_surface->current.layer
															   : layer->layer_surface->pending.layer;
				if (lyr != layer_order[li])
				{
					continue;
				}
				/* wlroots sets initialized on first surface commit; configure before that asserts. */
				if (!layer->layer_surface->initialized)
				{
					continue;
				}
				/* Must configure unmapped surfaces too: clients map only after the first configure. */
				wlr_scene_layer_surface_v1_configure(layer->scene_layer, &full, &usable);
			}
		}
		out->layer_workarea = usable;
	}
	struct comp_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link)
	{
		if (!toplevel_surface_mapped(view) || !toplevel_surface_initialized(view))
		{
			continue;
		}
		if (!view->xdg_toplevel->current.maximized && !view->xdg_toplevel->requested.maximized)
		{
			continue;
		}
		if (server->layout != COMP_LAYOUT_STACK && !view->tile_float)
		{
			continue;
		}
		/* Layer exclusive zones redefine the target workarea for maximized stack/floating windows. */
		toplevel_apply_requested_maximize(view);
	}
	server_workspace_apply_visibility(server);
	if ((server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL) &&
		server->grab != COMP_GRAB_MOVE)
	{
		server_arrange_toplevels(server);
	}
}

/** layer-shell map callback: trigger arrange. */
static void comp_layer_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer *layer = wl_container_of(listener, layer, map);
	layer_shell_arrange(layer->server);
}

/** layer-shell unmap callback: trigger arrange. */
static void comp_layer_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer *layer = wl_container_of(listener, layer, unmap);
	layer_shell_arrange(layer->server);
}

/** layer-shell commit callback: re-arrange on initial/pending commits. */
static void comp_layer_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer *layer = wl_container_of(listener, layer, commit);
	if (layer->layer_surface->initial_commit)
	{
		layer_shell_arrange(layer->server);
		return;
	}
	if (layer->layer_surface->pending.committed)
	{
		layer_shell_arrange(layer->server);
	}
}

/** Unconstrain popup geometry once the popup xdg surface is initialized. */
static bool popup_unconstrain(struct comp_popup *popup)
{
	struct comp_toplevel *view = popup->view;
	if (!view->xdg_toplevel || !view->scene_tree)
	{
		return false;
	}
	if (!popup->wlr_popup || !popup->wlr_popup->base || !popup->wlr_popup->base->initialized)
	{
		return false;
	}
	struct wlr_xdg_surface *toplevel = view->xdg_toplevel->base;
	struct comp_output *out = toplevel_tile_output(view);
	struct wlr_box usable = out->layer_workarea;
	const int geo_x = toplevel->geometry.x;
	const int geo_y = toplevel->geometry.y;
	const int view_x = view->scene_tree->node.x;
	const int view_y = view->scene_tree->node.y;
	struct wlr_box box = {
		.x = usable.x - view_x + geo_x,
		.y = usable.y - view_y + geo_y,
		.width = usable.width,
		.height = usable.height,
	};
	wlr_xdg_popup_unconstrain_from_box(popup->wlr_popup, &box);
	return true;
}

/** First popup commit callback: force one unconstrain pass, then detach listener. */
static void popup_handle_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_popup *popup = wl_container_of(listener, popup, commit);
	if (!popup_unconstrain(popup))
	{
		return;
	}
	wl_list_remove(&popup->commit.link);
	popup->commit.notify = NULL;
}

/** Popup reposition callback: recompute unconstrain box. */
static void popup_handle_reposition(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_popup *popup = wl_container_of(listener, popup, reposition);
	(void)popup_unconstrain(popup);
}

static void popup_create(struct comp_toplevel *view, struct wlr_xdg_popup *wlr_popup);
static void layer_popup_create(struct comp_layer *layer, struct wlr_xdg_popup *wlr_popup);

/** Unconstrain a layer popup once the popup xdg surface is initialized. */
static bool layer_popup_unconstrain(struct comp_layer_popup *popup)
{
	if (!popup || !popup->layer || !popup->wlr_popup || !popup->layer->layer_surface)
	{
		return false;
	}
	if (!popup->wlr_popup->base || !popup->wlr_popup->base->initialized)
	{
		return false;
	}
	struct comp_output *out = comp_output_from_wlr(popup->layer->server, popup->layer->layer_surface->output);
	struct wlr_box box;
	if (out)
	{
		box = out->layer_workarea;
	}
	else
	{
		wlr_output_layout_get_box(popup->layer->server->output_layout,
								  popup->layer->layer_surface->output, &box);
	}
	wlr_xdg_popup_unconstrain_from_box(popup->wlr_popup, &box);
	return true;
}

/** First layer popup commit callback: force one unconstrain pass, then detach listener. */
static void layer_popup_handle_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer_popup *popup = wl_container_of(listener, popup, commit);
	if (!layer_popup_unconstrain(popup))
	{
		return;
	}
	wl_list_remove(&popup->commit.link);
	popup->commit.notify = NULL;
}

/** Layer popup reposition callback: recompute unconstrain box. */
static void layer_popup_handle_reposition(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer_popup *popup = wl_container_of(listener, popup, reposition);
	(void)layer_popup_unconstrain(popup);
}

/** Nested layer popup callback: create child popup scene node. */
static void layer_popup_handle_new_popup(struct wl_listener *listener, void *data)
{
	struct comp_layer_popup *popup = wl_container_of(listener, popup, new_popup);
	layer_popup_create(popup->layer, data);
}

/** Layer popup destroy callback: detach listeners and free popup tracking state. */
static void layer_popup_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->new_popup.link);
	if (popup->commit.notify)
	{
		wl_list_remove(&popup->commit.link);
	}
	wl_list_remove(&popup->reposition.link);
	free(popup);
}

/** Nested popup callback: create child popup scene node. */
static void popup_handle_new_popup(struct wl_listener *listener, void *data)
{
	struct comp_popup *popup = wl_container_of(listener, popup, new_popup);
	popup_create(popup->view, data);
}

/** Popup destroy callback: detach listeners and free popup tracking state. */
static void popup_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->new_popup.link);
	if (popup->commit.notify)
	{
		wl_list_remove(&popup->commit.link);
	}
	wl_list_remove(&popup->reposition.link);
	free(popup);
}

/** Create scene integration and listeners for one xdg_popup subtree. */
static void popup_create(struct comp_toplevel *view, struct wlr_xdg_popup *wlr_popup)
{
	struct wlr_xdg_surface *parent_xdg = wlr_xdg_surface_try_from_wlr_surface(wlr_popup->parent);
	if (!parent_xdg)
	{
		return;
	}

	struct comp_popup *popup = calloc(1, sizeof(*popup));
	if (!popup)
	{
		return;
	}
	popup->view = view;
	popup->wlr_popup = wlr_popup;

	struct wlr_scene_tree *parent_tree;
	if (parent_xdg->role == WLR_XDG_SURFACE_ROLE_POPUP)
	{
		parent_tree = parent_xdg->surface->data;
		if (!parent_tree)
		{
			free(popup);
			return;
		}
	}
	else
	{
		parent_tree = view->scene_tree;
	}

	struct wlr_scene_tree *tree = wlr_scene_xdg_surface_create(parent_tree, wlr_popup->base);
	if (!tree)
	{
		free(popup);
		return;
	}
	wlr_popup->base->surface->data = tree;
	wlr_scene_node_raise_to_top(&tree->node);

	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_popup->events.destroy, &popup->destroy);
	popup->new_popup.notify = popup_handle_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);
	popup->commit.notify = popup_handle_commit;
	wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);
	popup->reposition.notify = popup_handle_reposition;
	wl_signal_add(&wlr_popup->events.reposition, &popup->reposition);
}

/** Create scene integration and listeners for one layer-surface xdg_popup subtree. */
static void layer_popup_create(struct comp_layer *layer, struct wlr_xdg_popup *wlr_popup)
{
	struct wlr_xdg_surface *parent_xdg = wlr_xdg_surface_try_from_wlr_surface(wlr_popup->parent);

	struct comp_layer_popup *popup = calloc(1, sizeof(*popup));
	if (!popup)
	{
		return;
	}
	popup->layer = layer;
	popup->wlr_popup = wlr_popup;

	struct wlr_scene_tree *parent_tree;
	if (parent_xdg && parent_xdg->role == WLR_XDG_SURFACE_ROLE_POPUP)
	{
		parent_tree = parent_xdg->surface->data;
		if (!parent_tree)
		{
			free(popup);
			return;
		}
	}
	else
	{
		/* Top-level layer popups typically have no xdg parent surface. */
		parent_tree = layer->scene_layer->tree;
	}

	struct wlr_scene_tree *tree = wlr_scene_xdg_surface_create(parent_tree, wlr_popup->base);
	if (!tree)
	{
		free(popup);
		return;
	}
	wlr_popup->base->surface->data = tree;
	wlr_scene_node_raise_to_top(&tree->node);

	popup->destroy.notify = layer_popup_handle_destroy;
	wl_signal_add(&wlr_popup->events.destroy, &popup->destroy);
	popup->new_popup.notify = layer_popup_handle_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);
	popup->commit.notify = layer_popup_handle_commit;
	wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);
	popup->reposition.notify = layer_popup_handle_reposition;
	wl_signal_add(&wlr_popup->events.reposition, &popup->reposition);
}

/** toplevel new_popup callback: attach popup to this toplevel. */
static void toplevel_handle_new_popup(struct wl_listener *listener, void *data)
{
	struct comp_toplevel *view = wl_container_of(listener, view, new_popup);
	popup_create(view, data);
}

/** Layer-surface popup callback: unconstrain and attach popup scene content. */
static void comp_layer_new_popup(struct wl_listener *listener, void *data)
{
	struct comp_layer *layer = wl_container_of(listener, layer, new_popup);
	layer_popup_create(layer, data);
}

/** layer-shell destroy callback: remove listeners/list entry and free wrapper. */
static void comp_layer_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_layer *layer = wl_container_of(listener, layer, destroy);
	wl_list_remove(&layer->map.link);
	wl_list_remove(&layer->unmap.link);
	wl_list_remove(&layer->commit.link);
	wl_list_remove(&layer->new_popup.link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->link);
	free(layer);
}

/** new layer-shell surface callback: create wrapper, scene node, and listeners. */
static void layer_shell_new_surface(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, layer_shell_new_surface);
	struct wlr_layer_surface_v1 *wlr_layer = data;

	if (wl_list_empty(&server->outputs))
	{
		wlr_layer_surface_v1_destroy(wlr_layer);
		return;
	}
	if (!wlr_layer->output)
	{
		wlr_layer->output = primary_wlr_output(server);
	}

	const enum zwlr_layer_shell_v1_layer lyr = wlr_layer->pending.layer;
	if ((size_t)lyr >= sizeof(server->layer_trees) / sizeof(server->layer_trees[0]))
	{
		wlr_layer_surface_v1_destroy(wlr_layer);
		return;
	}

	struct comp_layer *layer = calloc(1, sizeof(*layer));
	if (!layer)
	{
		wlr_layer_surface_v1_destroy(wlr_layer);
		return;
	}
	layer->server = server;
	layer->layer_surface = wlr_layer;
	layer->scene_layer = wlr_scene_layer_surface_v1_create(server->layer_trees[lyr], wlr_layer);
	if (!layer->scene_layer)
	{
		free(layer);
		wlr_layer_surface_v1_destroy(wlr_layer);
		return;
	}

	layer->destroy.notify = comp_layer_destroy;
	wl_signal_add(&wlr_layer->events.destroy, &layer->destroy);
	layer->map.notify = comp_layer_map;
	wl_signal_add(&wlr_layer->surface->events.map, &layer->map);
	layer->unmap.notify = comp_layer_unmap;
	wl_signal_add(&wlr_layer->surface->events.unmap, &layer->unmap);
	layer->commit.notify = comp_layer_commit;
	wl_signal_add(&wlr_layer->surface->events.commit, &layer->commit);
	layer->new_popup.notify = comp_layer_new_popup;
	wl_signal_add(&wlr_layer->events.new_popup, &layer->new_popup);

	wl_list_insert(&server->layers, &layer->link);
}

/** Output commit callback: update layer-shell arrangement after output state changes. */
static void output_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_output *output = wl_container_of(listener, output, commit);
	struct comp_server *srv = output->server;
	layer_shell_arrange(srv);
	output_mgmt_update_config(srv);
}

/** Output destroy callback: leave foreign outputs, detach scene/output layout, and free state. */
static void output_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_output *output = wl_container_of(listener, output, destroy);
	struct comp_server *server = output->server;
	struct comp_toplevel *t;
	wl_list_for_each(t, &output->server->toplevels, link)
	{
		if (t->foreign_toplevel)
		{
			wlr_foreign_toplevel_handle_v1_output_leave(t->foreign_toplevel, output->wlr_output);
		}
	}
	server_apply_input_device_maps(output->server);
	ext_workspace_on_output_remove(output->server, output->wlr_output);
	output_mgmt_update_config(output->server);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->commit.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	wlr_scene_output_destroy(output->scene_output);
	wlr_output_layout_remove(output->server->output_layout, output->wlr_output);
	/*
	 * Some nested backends can drop the last output without emitting backend
	 * destroy immediately (for example closing the host X11 window). In nested
	 * mode (`session == NULL`), terminate once no outputs remain so the process
	 * exits instead of lingering and keeping WAYLAND_DISPLAY active.
	 */
	/* Nested fallback: no outputs left means host window is gone -> exit main loop. */
	if (wl_list_empty(&server->outputs) && server->session == NULL && server->wl_display)
	{
		/*
		 * Shut down Xwayland first so it does not keep writing to a closing
		 * Wayland connection during the subsequent display-loop termination.
		 */
		server_destroy_xwayland(server);
		wlr_log(WLR_INFO, "Last output removed in nested backend, terminating display loop");
		server_request_terminate(server, NULL);
	}
	free(output);
}

/** new_output callback: initialize output mode, scene output, listeners, and protocol state. */
static void server_new_output(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct comp_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode)
	{
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
	struct wlr_output_layout_output *lout = wlr_output_layout_add_auto(server->output_layout, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, lout, output->scene_output);

	float scale = wlr_output->scale;
	if (!wlr_xcursor_manager_load(server->cursor_mgr, scale))
	{
		wlr_log(WLR_ERROR, "Cannot load XCursor theme for scale %f", scale);
	}

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->commit.notify = output_commit;
	wl_signal_add(&wlr_output->events.commit, &output->commit);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (t->foreign_toplevel && toplevel_surface_mapped(t))
		{
			wlr_foreign_toplevel_handle_v1_output_enter(t->foreign_toplevel, wlr_output);
		}
	}
	ext_workspace_on_output_new(server, wlr_output);
	layer_shell_arrange(server);
	output_mgmt_update_config(server);
	server_apply_input_device_maps(server);
}

static uint32_t tile_user_key_gen;

/** Re-evaluate tile rule results (float/order) for one toplevel from current config. */
static void toplevel_refresh_tile_props(struct comp_toplevel *view)
{
	if (!view->server->config)
	{
		view->tile_float = false;
		view->tile_order = 0;
		return;
	}
	const char *app_id = "";
	const char *title = "";
	toplevel_title_app_for_config(view, &title, &app_id);
	comp_config_tile_props_for_toplevel(view->server->config, app_id, title, &view->tile_float, &view->tile_order);
}

/** Re-evaluate tile rule results for all toplevels. */
static void server_refresh_all_tile_props(struct comp_server *server)
{
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		toplevel_refresh_tile_props(t);
	}
}

/** xdg-decoration destroy callback: clear listeners and detach decoration pointer. */
static void xdg_decoration_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, xdg_decoration_destroy);
	wl_list_remove(&view->xdg_decoration_request_mode.link);
	wl_list_remove(&view->xdg_decoration_destroy.link);
	view->xdg_decoration = NULL;
}

/** xdg-decoration request-mode callback: re-apply compositor policy. */
static void xdg_decoration_handle_request_mode(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, xdg_decoration_request_mode);
	toplevel_apply_decoration_mode(view);
}

/** Select and push xdg-decoration mode based on layout, float state, and rules. */
static void toplevel_apply_decoration_mode(struct comp_toplevel *view)
{
	if (!view->xdg_toplevel || !view->xdg_decoration)
	{
		return;
	}
	/* wlr_xdg_toplevel_decoration_v1_set_mode schedules configure; wlroots asserts if !initialized. */
	if (!view->xdg_toplevel->base->initialized)
	{
		return;
	}
	struct comp_server *server = view->server;
	enum wlr_xdg_toplevel_decoration_v1_mode mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	if (server->layout == COMP_LAYOUT_STACK)
	{
		mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	}
	else if (view->tile_float)
	{
		mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	}
	else if (comp_config_decoration_prefer_server_side_tile_scroll(server->config, view->xdg_toplevel->app_id,
																   view->xdg_toplevel->title))
	{
		mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	}
	else
	{
		mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	}
	/* Re-sending the same mode on every commit causes configure storms (X11 via satellite). */
	if (view->xdg_decoration->current.mode == mode &&
		view->xdg_decoration->scheduled_mode == mode)
	{
		return;
	}
	wlr_xdg_toplevel_decoration_v1_set_mode(view->xdg_decoration, mode);
}

/** Re-apply xdg-decoration policy to every toplevel. */
void server_sync_xdg_decorations(struct comp_server *server)
{
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		toplevel_apply_decoration_mode(t);
	}
}

/** Attach a new xdg-decoration object to its toplevel and apply current policy immediately. */
static void xdg_new_toplevel_decoration(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, new_xdg_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	struct comp_toplevel *view = NULL;
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (t->xdg_toplevel == deco->toplevel)
		{
			view = t;
			break;
		}
	}
	if (!view)
	{
		return;
	}
	if (view->xdg_decoration)
	{
		wl_list_remove(&view->xdg_decoration_destroy.link);
		wl_list_remove(&view->xdg_decoration_request_mode.link);
		view->xdg_decoration = NULL;
	}
	view->xdg_decoration = deco;
	view->xdg_decoration_destroy.notify = xdg_decoration_handle_destroy;
	wl_signal_add(&deco->events.destroy, &view->xdg_decoration_destroy);
	view->xdg_decoration_request_mode.notify = xdg_decoration_handle_request_mode;
	wl_signal_add(&deco->events.request_mode, &view->xdg_decoration_request_mode);
	toplevel_apply_decoration_mode(view);
}

/** Trace transient parent changes without assigning dialog or modality policy. */
static void toplevel_handle_set_parent(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, set_parent);
	log_xdg_state("set_parent", view);
}

/** Refresh tile/decor/foreign metadata when the visible title changes. */
static void toplevel_handle_set_title(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, set_title);
	toplevel_refresh_tile_props(view);
	if ((view->server->layout == COMP_LAYOUT_TILE || view->server->layout == COMP_LAYOUT_SCROLL) &&
		toplevel_surface_mapped(view))
	{
		server_arrange_toplevels(view->server);
	}
	server_sync_xdg_decorations(view->server);
	foreign_toplevel_refresh(view);
}

/** Refresh tile/decor/foreign metadata when app_id changes. */
static void toplevel_handle_set_app_id(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, set_app_id);
	toplevel_refresh_tile_props(view);
	if ((view->server->layout == COMP_LAYOUT_TILE || view->server->layout == COMP_LAYOUT_SCROLL) &&
		toplevel_surface_mapped(view))
	{
		server_arrange_toplevels(view->server);
	}
	server_sync_xdg_decorations(view->server);
	foreign_toplevel_refresh(view);
}

/** Handle foreign-toplevel activate requests (bars/task switchers) with seat/workspace validation. */
static void foreign_toplevel_handle_request_activate(struct wl_listener *listener, void *data)
{
	struct comp_toplevel *view = wl_container_of(listener, view, foreign_request_activate);
	struct wlr_foreign_toplevel_handle_v1_activated_event *ev = data;
	if (!view || !view->xdg_toplevel || !toplevel_surface_mapped(view))
	{
		return;
	}
	if (ev && ev->seat && ev->seat != view->server->seat)
	{
		return;
	}
	if (view->workspace != view->server->current_workspace)
	{
		server_workspace_go(view->server, view->workspace);
	}
	if (view->minimized)
	{
		toplevel_set_minimized(view, false);
	}
	focus_toplevel(view->server, view);
	foreign_toplevel_sync_all(view->server);
}

/**
 * Handle xdg-activation focus requests from launchers and notifications.
 *
 * wlroots validates the token and emits this request once the target surface is
 * known. Morph intentionally reuses the same workspace/minimize/focus rules as
 * foreign-toplevel activation so launcher-driven focus changes stay consistent
 * with user-visible task switching.
 */
static void xdg_activation_handle_request_activate(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, xdg_activation_request_activate);
	struct wlr_xdg_activation_v1_request_activate_event *ev = data;
	if (!server || !ev || !ev->surface)
	{
		return;
	}
	/* Reject tokens minted for a different seat so one seat cannot steal focus from another. */
	if (ev->token && ev->token->seat && ev->token->seat != server->seat)
	{
		return;
	}
	/* Activation may target a subsurface or popup; resolve to the root toplevel surface first. */
	struct wlr_surface *root = wlr_surface_get_root_surface(ev->surface);
	struct comp_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link)
	{
		if (!view->xdg_toplevel || view->xdg_toplevel->base->surface != root || !toplevel_surface_mapped(view))
		{
			continue;
		}
		if (view->workspace != server->current_workspace)
		{
			server_workspace_go(server, view->workspace);
		}
		if (view->minimized)
		{
			toplevel_set_minimized(view, false);
		}
		focus_toplevel(server, view);
		foreign_toplevel_sync_all(server);
		return;
	}
}

/** Handle foreign-toplevel close requests for Wayland and Xwayland clients. */
static void foreign_toplevel_handle_request_close(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, foreign_request_close);
	if (!view || !view->xdg_toplevel)
	{
		return;
	}
	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

/** Sort helper for tiled views: explicit rule order first, stable user key second. */
static int cmp_toplevel_tile_order(const void *va, const void *vb)
{
	const struct comp_toplevel *a = *(const struct comp_toplevel *const *)va;
	const struct comp_toplevel *b = *(const struct comp_toplevel *const *)vb;
	if (a->tile_order < b->tile_order)
	{
		return -1;
	}
	if (a->tile_order > b->tile_order)
	{
		return 1;
	}
	if (a->tile_user_key < b->tile_user_key)
	{
		return -1;
	}
	if (a->tile_user_key > b->tile_user_key)
	{
		return 1;
	}
	return 0;
}

/** Swap both tile tie-breakers so two windows exchange their position in tile order. */
static void tile_swap_sort_keys(struct comp_toplevel *a, struct comp_toplevel *b)
{
	uint32_t k = a->tile_user_key;
	a->tile_user_key = b->tile_user_key;
	b->tile_user_key = k;
	int o = a->tile_order;
	a->tile_order = b->tile_order;
	b->tile_order = o;
}

/** Caller frees; returns NULL when no tiled mapped non-float views. */
static struct comp_toplevel **tile_sorted_views(struct comp_server *server, size_t *n_out)
{
	*n_out = 0;
	size_t n_tile = 0;
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (!toplevel_surface_mapped(t) || t->tile_float || t->minimized)
		{
			continue;
		}
		if (t->workspace != server->current_workspace)
		{
			continue;
		}
		n_tile++;
	}
	if (n_tile == 0)
	{
		return NULL;
	}
	struct comp_toplevel **arr = calloc(n_tile, sizeof(*arr));
	if (!arr)
	{
		return NULL;
	}
	size_t i = 0;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (!toplevel_surface_mapped(t) || t->tile_float || t->minimized)
		{
			continue;
		}
		if (t->workspace != server->current_workspace)
		{
			continue;
		}
		arr[i++] = t;
	}
	qsort(arr, n_tile, sizeof(*arr), cmp_toplevel_tile_order);
	*n_out = n_tile;
	return arr;
}

/** Return index of `v` in sorted view array, or -1 when not present. */
static int tile_sorted_index(struct comp_toplevel **arr, size_t n, struct comp_toplevel *v)
{
	for (size_t i = 0; i < n; i++)
	{
		if (arr[i] == v)
		{
			return (int)i;
		}
	}
	return -1;
}

/** Which output's `layer_workarea` contains the center of this tiled view (layout coordinates). */
static struct comp_output *toplevel_tile_output(struct comp_toplevel *t)
{
	struct comp_server *server = t->server;
	if (!toplevel_surface_mapped(t))
	{
		return comp_output_from_wlr(server, primary_wlr_output(server));
	}
	const struct wlr_box *geo = &t->xdg_toplevel->base->geometry;
	const double cx = (double)t->scene_tree->node.x + (double)geo->width * 0.5;
	const double cy = (double)t->scene_tree->node.y + (double)geo->height * 0.5;
	struct comp_output *o;
	wl_list_for_each(o, &server->outputs, link)
	{
		if (wlr_box_contains_point(&o->layer_workarea, cx, cy))
		{
			return o;
		}
	}
	return comp_output_from_wlr(server, primary_wlr_output(server));
}

/**
 * Sublist of `full` in stable tile order, only views assigned to `out`.
 * Caller frees the returned pointer when non-NULL; does not free `full`.
 */
static struct comp_toplevel **tile_sorted_views_on_output(struct comp_server *server, struct comp_output *out,
														  struct comp_toplevel **full, size_t n_full, size_t *n_out)
{
	(void)server;
	size_t cnt = 0;
	for (size_t k = 0; k < n_full; k++)
	{
		if (toplevel_tile_output(full[k]) == out)
		{
			cnt++;
		}
	}
	if (cnt == 0)
	{
		*n_out = 0;
		return NULL;
	}
	struct comp_toplevel **sub = calloc(cnt, sizeof(*sub));
	if (!sub)
	{
		*n_out = 0;
		return NULL;
	}
	size_t j = 0;
	for (size_t k = 0; k < n_full; k++)
	{
		if (toplevel_tile_output(full[k]) == out)
		{
			sub[j++] = full[k];
		}
	}
	*n_out = cnt;
	return sub;
}

/** In scroll layout, align current output slot to keep the focused view visible. */
static void scroll_sync_to_focused(struct comp_server *server)
{
	if (server->layout != COMP_LAYOUT_SCROLL || !server->focused_toplevel)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	if (f->tile_float || !toplevel_surface_mapped(f))
	{
		return;
	}
	struct comp_output *out = toplevel_tile_output(f);
	if (!out)
	{
		return;
	}
	size_t n_full = 0;
	struct comp_toplevel **full = tile_sorted_views(server, &n_full);
	if (!full)
	{
		return;
	}
	size_t n = 0;
	struct comp_toplevel **sub = tile_sorted_views_on_output(server, out, full, n_full, &n);
	free(full);
	if (!sub || n == 0)
	{
		free(sub);
		return;
	}
	const int idx = tile_sorted_index(sub, n, f);
	if (idx >= 0)
	{
		out->workspace_scroll_slot[server->current_workspace] = idx;
	}
	free(sub);
}

/** Unmap callback: drop focus/grabs and re-run layout if needed. */
static void toplevel_unmap(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, unmap);
	if (view->server->focused_toplevel == view)
	{
		clear_keyboard_focus(view->server);
	}
	if (view->server->grabbed_toplevel == view)
	{
		cancel_active_grab(view->server);
	}
	if ((view->server->layout == COMP_LAYOUT_TILE || view->server->layout == COMP_LAYOUT_SCROLL) &&
		view->server->grab != COMP_GRAB_MOVE)
	{
		server_arrange_toplevels(view->server);
	}
	foreign_toplevel_refresh(view);
}

/** Destroy callback: remove listeners/resources and keep compositor state coherent. */
static void toplevel_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, destroy);
	if (view->xdg_decoration)
	{
		detach_listener_if_linked(&view->xdg_decoration_destroy);
		detach_listener_if_linked(&view->xdg_decoration_request_mode);
		view->xdg_decoration = NULL;
	}
	if (view->xdg_toplevel)
	{
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &view->xdg_toplevel->base->popups, link)
		{
			wlr_xdg_popup_destroy(popup);
		}
		detach_listener_if_linked(&view->map);
		detach_listener_if_linked(&view->unmap);
		detach_listener_if_linked(&view->commit);
		detach_listener_if_linked(&view->request_move);
		detach_listener_if_linked(&view->request_resize);
		detach_listener_if_linked(&view->request_maximize);
		detach_listener_if_linked(&view->request_fullscreen);
		detach_listener_if_linked(&view->request_minimize);
		detach_listener_if_linked(&view->set_parent);
		detach_listener_if_linked(&view->set_title);
		detach_listener_if_linked(&view->set_app_id);
		detach_listener_if_linked(&view->new_popup);
	}
	detach_listener_if_linked(&view->destroy);
	if (view->listed)
	{
		wl_list_remove(&view->link);
		view->listed = false;
	}
	if (view->foreign_toplevel)
	{
		detach_listener_if_linked(&view->foreign_request_activate);
		detach_listener_if_linked(&view->foreign_request_close);
		wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
		view->foreign_toplevel = NULL;
	}
	free(view->foreign_title);
	free(view->foreign_app_id);
	if (view->server->motion_focus_target == view)
	{
		view->server->motion_focus_target = NULL;
		view->server->motion_focus_clear = false;
	}
	if (view->server->focused_toplevel == view)
	{
		clear_keyboard_focus(view->server);
	}
	if (view->server->grabbed_toplevel == view)
	{
		cancel_active_grab(view->server);
	}
	struct comp_server *srv = view->server;
	free(view);
	if ((srv->layout == COMP_LAYOUT_TILE || srv->layout == COMP_LAYOUT_SCROLL) &&
		srv->grab != COMP_GRAB_MOVE)
	{
		server_arrange_toplevels(srv);
	}
}

/** toplevel commit callback: debug trace + one-time initial configure safeguards. */
static void toplevel_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, commit);
	if (!view->xdg_toplevel)
	{
		return;
	}
	struct wlr_xdg_surface *xdg = view->xdg_toplevel->base;
	if (xdg_commit_debug_logs_enabled)
	{
		log_xdg_state("commit", view);
	}
	const bool configure_acked = view->pending_configure_serial != 0 &&
		xdg->current.configure_serial == view->pending_configure_serial;
	if (configure_acked)
	{
		view->pending_configure_serial = 0;
	}
	/* wlroots 0.19 asserts if we schedule configure before initialized. */
	if (xdg->initial_commit && xdg->initialized)
	{
		const struct wlr_box *geo = &xdg->geometry;
		/* xwayland-satellite already negotiates X11 size; 0x0 reconfigure breaks splash/dialogs. */
		if (geo->width <= 0 || geo->height <= 0)
		{
			log_xdg_state("commit:set_size0x0", view);
			toplevel_set_size(view, 0, 0);
		}
	}
	toplevel_apply_decoration_mode(view);
	toplevel_update_resize_anchor(view);
	if (view->pending_configure_serial == 0 &&
		xdg->geometry.width > 0 && xdg->geometry.height > 0)
	{
		/* wlroots retains the last requested size and includes it in later state-only
		 * configure events. Once the client has answered our newest configure, keep
		 * that retained state aligned with the geometry it actually committed. Calling
		 * set_size here would schedule another configure and recreate the resize loop. */
		view->xdg_toplevel->scheduled.width = xdg->geometry.width;
		view->xdg_toplevel->scheduled.height = xdg->geometry.height;
	}
	if (view->server->grab == COMP_GRAB_RESIZE && view->server->grabbed_toplevel == view)
	{
		log_resize_state("commit-during-resize", view, view->server->grab_view_x, view->server->grab_view_y,
						 view->server->grab_view_width, view->server->grab_view_height,
						 view->server->resize_edges);
	}
}

/** toplevel map callback: place/focus according to current layout policy and rules. */
static void toplevel_map(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, map);
	view->minimized = false;
	view->has_restore = false;
	view->restore_width = 0;
	view->restore_height = 0;
	log_xdg_state("map", view);
	const struct wlr_box *geo = &view->xdg_toplevel->base->geometry;
	const int gw = geo->width;
	const int gh = geo->height;
	struct comp_output *o;
	wl_list_for_each(o, &view->server->outputs, link)
	{
		if (view->foreign_toplevel)
		{
			wlr_foreign_toplevel_handle_v1_output_enter(view->foreign_toplevel, o->wlr_output);
		}
	}
	foreign_toplevel_refresh(view);

	struct wlr_output *out = wlr_output_layout_output_at(
		view->server->output_layout, view->server->cursor->x, view->server->cursor->y);
	struct wlr_box obox = {0, 0, 800, 600};
	struct comp_output *co = comp_output_from_wlr(view->server, out);
	if (co)
	{
		obox = co->layer_workarea;
	}
	else if (out)
	{
		wlr_output_layout_get_box(view->server->output_layout, out, &obox);
	}
	toplevel_refresh_tile_props(view);
	if (view->xdg_toplevel->requested.maximized)
	{
		toplevel_apply_requested_maximize(view);
		focus_toplevel(view->server, view);
		server_workspace_apply_visibility(view->server);
		server_sync_xdg_decorations(view->server);
		return;
	}
	if (view->server->layout == COMP_LAYOUT_TILE || view->server->layout == COMP_LAYOUT_SCROLL)
	{
		if (view->tile_float)
		{
			const int x = obox.x + (obox.width - gw) / 2;
			const int y = obox.y + (obox.height - gh) / 2;
			wlr_scene_node_set_position(&view->scene_tree->node, x, y);
			wlr_scene_node_raise_to_top(&view->scene_tree->node);
			focus_toplevel(view->server, view);
			server_arrange_toplevels(view->server);
			server_workspace_apply_visibility(view->server);
			server_sync_xdg_decorations(view->server);
			return;
		}
		focus_toplevel(view->server, view);
		server_arrange_toplevels(view->server);
		server_workspace_apply_visibility(view->server);
		server_sync_xdg_decorations(view->server);
		return;
	}
	const int x = obox.x + (obox.width - gw) / 2;
	const int y = obox.y + (obox.height - gh) / 2;
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	focus_toplevel(view->server, view);
	server_workspace_apply_visibility(view->server);
	server_sync_xdg_decorations(view->server);
}

/** Begin interactive move grab for a view. */
static void toplevel_request_move(struct wl_listener *listener, void *data)
{
	struct comp_toplevel *view = wl_container_of(listener, view, request_move);
	struct wlr_xdg_toplevel_move_event *ev = data;
	if (!view->server->seat || view->server->seat->pointer_state.button_count != 1)
	{
		return;
	}
	if (!ev || !ev->seat || ev->seat->seat != view->server->seat ||
		!wlr_seat_validate_pointer_grab_serial(view->server->seat, NULL, ev->serial))
	{
		return;
	}
	if (toplevel_effectively_maximized(view))
	{
		return;
	}
	begin_move(view->server, view, false);
}

/** Begin interactive resize grab unless layout policy forbids direct resizing. */
static void toplevel_request_resize(struct wl_listener *listener, void *data)
{
	struct comp_toplevel *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *ev = data;
	struct comp_server *server = view->server;
	if (xdg_debug_logs_enabled)
	{
		wlr_log(WLR_INFO,
				"resizedbg:request app_id='%s' title='%s' edges=0x%x serial=%u seat_ok=%d button_count=%zu "
				"parent=%d constraints_cur=min:%dx%d,max:%dx%d constraints_pending=min:%dx%d,max:%dx%d",
				view->xdg_toplevel && view->xdg_toplevel->app_id ? view->xdg_toplevel->app_id : "",
				view->xdg_toplevel && view->xdg_toplevel->title ? view->xdg_toplevel->title : "",
				ev ? ev->edges : 0, ev ? ev->serial : 0,
				(ev && ev->seat && ev->seat->seat == server->seat) ? 1 : 0,
				server->seat ? server->seat->pointer_state.button_count : 0,
				view->xdg_toplevel && view->xdg_toplevel->parent ? 1 : 0,
				view->xdg_toplevel ? view->xdg_toplevel->current.min_width : 0,
				view->xdg_toplevel ? view->xdg_toplevel->current.min_height : 0,
				view->xdg_toplevel ? view->xdg_toplevel->current.max_width : 0,
				view->xdg_toplevel ? view->xdg_toplevel->current.max_height : 0,
				view->xdg_toplevel ? view->xdg_toplevel->pending.min_width : 0,
				view->xdg_toplevel ? view->xdg_toplevel->pending.min_height : 0,
				view->xdg_toplevel ? view->xdg_toplevel->pending.max_width : 0,
				view->xdg_toplevel ? view->xdg_toplevel->pending.max_height : 0);
	}

	if (!toplevel_can_direct_resize(server, view))
	{
		return;
	}
	if (!server->seat || server->seat->pointer_state.button_count != 1)
	{
		return;
	}
	if (!ev || !ev->seat || ev->seat->seat != server->seat ||
		!wlr_seat_validate_pointer_grab_serial(server->seat, NULL, ev->serial))
	{
		return;
	}
	if (!ev->edges)
	{
		return;
	}
	focus_toplevel(server, view);
	begin_resize(server, view, ev->edges);
}

/** Apply current maximize request when surface is initialized. */
static void toplevel_apply_requested_maximize(struct comp_toplevel *view)
{
	if (!view || !view->xdg_toplevel)
	{
		return;
	}
	if (!toplevel_surface_initialized(view))
	{
		/* Some clients request maximize before first commit; configuring here would assert in wlroots. */
		return;
	}
	if (view->server->grabbed_toplevel == view)
	{
		/* Title-bar double click often arrives as move initiation followed by maximize.
		 * Drop the stale move grab first so later pointer motion does not keep dragging the window. */
		cancel_active_grab(view->server);
	}
	const bool want_max = view->xdg_toplevel->requested.maximized;
	const bool was_max = view->xdg_toplevel->current.maximized;
	if (want_max)
	{
		if (!was_max || !view->has_restore)
		{
			/* Capture pre-maximize geometry once so normalize can restore it verbatim. */
			const struct wlr_box *geo = &view->xdg_toplevel->base->geometry;
			view->restore_x = view->scene_tree->node.x;
			view->restore_y = view->scene_tree->node.y;
			view->restore_width = geo->width;
			view->restore_height = geo->height;
			/* Some clients report 0x0 geometry transiently; fall back to mapped surface size. */
			if ((view->restore_width <= 0 || view->restore_height <= 0) &&
				view->xdg_toplevel->base->surface)
			{
				view->restore_width = view->xdg_toplevel->base->surface->current.width;
				view->restore_height = view->xdg_toplevel->base->surface->current.height;
			}
			view->has_restore = true;
		}
		struct wlr_box obox = {0, 0, 800, 600};
		struct comp_output *co = toplevel_preferred_output(view);
		if (co)
		{
			obox = co->layer_workarea;
		}
		toplevel_set_maximized_size(view, true, obox.width, obox.height);
		if (view->server->layout == COMP_LAYOUT_STACK || view->tile_float)
		{
			wlr_scene_node_set_position(&view->scene_tree->node, obox.x, obox.y);
		}
	}
	else
	{
		/* Restore pre-maximize geometry so normalize returns to previous size and position. */
		if (view->has_restore && view->restore_width > 0 && view->restore_height > 0)
		{
			int restore_x = view->restore_x;
			int restore_y = view->restore_y;
			int restore_width = view->restore_width;
			int restore_height = view->restore_height;
			if (view->server->layout == COMP_LAYOUT_STACK || view->tile_float)
			{
				toplevel_clamp_floating_box_to_workarea(view,
					&restore_x, &restore_y, &restore_width, &restore_height);
			}
			toplevel_set_maximized_size(view, false, restore_width, restore_height);
			if (view->server->layout == COMP_LAYOUT_STACK || view->tile_float)
			{
				wlr_scene_node_set_position(&view->scene_tree->node, restore_x, restore_y);
			}
			view->has_restore = false;
		}
		else
		{
			int restore_x = view->scene_tree->node.x;
			int restore_y = view->scene_tree->node.y;
			int restore_width = view->xdg_toplevel->base->geometry.width;
			int restore_height = view->xdg_toplevel->base->geometry.height;
			if ((restore_width <= 0 || restore_height <= 0) &&
				view->xdg_toplevel->base->surface)
			{
				restore_width = view->xdg_toplevel->base->surface->current.width;
				restore_height = view->xdg_toplevel->base->surface->current.height;
			}
			if (view->server->layout == COMP_LAYOUT_STACK || view->tile_float)
			{
				toplevel_clamp_floating_box_to_workarea(view,
					&restore_x, &restore_y, &restore_width, &restore_height);
				wlr_scene_node_set_position(&view->scene_tree->node, restore_x, restore_y);
			}
			toplevel_set_maximized_size(view, false, restore_width, restore_height);
		}
	}
	foreign_toplevel_refresh(view);
}

/** Ack maximize requests from clients and apply output workarea geometry. */
static void toplevel_request_maximize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, request_maximize);
	toplevel_apply_requested_maximize(view);
}

/** Ack fullscreen requests and align position/size to target output geometry. */
static void toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, request_fullscreen);
	if (!view || !view->xdg_toplevel)
	{
		return;
	}
	if (!toplevel_surface_initialized(view))
	{
		return;
	}
	if (view->server->grabbed_toplevel == view)
	{
		cancel_active_grab(view->server);
	}
	const bool want_fullscreen = view->xdg_toplevel->requested.fullscreen;
	toplevel_track_configure(view,
		wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, want_fullscreen));
	if (want_fullscreen)
	{
		struct wlr_output *out = view->xdg_toplevel->requested.fullscreen_output;
		if (!out)
		{
			out = wlr_output_layout_output_at(view->server->output_layout,
											  view->server->cursor->x, view->server->cursor->y);
		}
		struct wlr_box obox = {0, 0, 800, 600};
		if (out)
		{
			wlr_output_layout_get_box(view->server->output_layout, out, &obox);
		}
		toplevel_set_size(view, obox.width, obox.height);
		if (view->server->layout == COMP_LAYOUT_STACK || view->tile_float)
		{
			wlr_scene_node_set_position(&view->scene_tree->node, obox.x, obox.y);
		}
	}
	else
	{
		toplevel_set_size(view, 0, 0);
	}
	foreign_toplevel_refresh(view);
}

/** Minimize requests hide the scene node until an explicit activate/unminimize. */
static void toplevel_request_minimize(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, request_minimize);
	if (!view || !view->xdg_toplevel)
	{
		return;
	}
	toplevel_set_minimized(view, view->xdg_toplevel->requested.minimized);
}

static bool x11_display_socket_busy(int num)
{
	char path[64];
	if (snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", num) >= (int)sizeof(path))
	{
		return true;
	}
	return access(path, F_OK) == 0;
}

/** First free display >= 2 (:0/:1 are often taken by Xorg or a parent compositor). */
static int x11_display_pick_free(void)
{
	for (int n = 2; n < 100; n++)
	{
		if (!x11_display_socket_busy(n))
		{
			return n;
		}
	}
	return -1;
}

/** Disable with MORPH_X11=0. Override display with MORPH_X11_DISPLAY (e.g. :12). */
static void spawn_xwayland_satellite(const char *wayland_display)
{
	const char *disable = getenv("MORPH_X11");
	if (disable && disable[0] && strcmp(disable, "0") == 0)
	{
		/*
		 * Nested X11 backends need the caller's DISPLAY while wlroots starts, but
		 * child clients must not inherit that host display. Otherwise an X11 app
		 * launched from inside Morph appears in the parent desktop when the
		 * satellite bridge is intentionally disabled.
		 */
		unsetenv("DISPLAY");
		unsetenv("XAUTHORITY");
		wlr_log(WLR_INFO, "xwayland-satellite disabled; DISPLAY unset for child clients");
		return;
	}

	static char display_buf[16];
	const char *disp = getenv("MORPH_X11_DISPLAY");
	if (!disp || !disp[0])
	{
		const int n = x11_display_pick_free();
		if (n < 0)
		{
			wlr_log(WLR_ERROR, "No free X display for xwayland-satellite (tried :2..:99)");
			unsetenv("DISPLAY");
			unsetenv("XAUTHORITY");
			return;
		}
		snprintf(display_buf, sizeof(display_buf), ":%d", n);
		disp = display_buf;
	}

	pid_t pid = fork();
	if (pid < 0)
	{
		wlr_log_errno(WLR_ERROR, "fork xwayland-satellite");
		unsetenv("DISPLAY");
		unsetenv("XAUTHORITY");
		return;
	}
	if (pid == 0)
	{
		setenv("WAYLAND_DISPLAY", wayland_display, 1);
		unsetenv("XAUTHORITY");
		setenv("_JAVA_AWT_WM_NONREPARENTING", "1", 1);
		execlp("xwayland-satellite", "xwayland-satellite", disp, NULL);
		wlr_log_errno(WLR_ERROR, "exec xwayland-satellite");
		_exit(127);
	}

	unsetenv("XAUTHORITY");
	setenv("DISPLAY", disp, 1);
	setenv("_JAVA_AWT_WM_NONREPARENTING", "1", 1);
	wlr_log(WLR_INFO, "xwayland-satellite on DISPLAY=%s (pid %d)", disp, pid);
}

/** Create per-toplevel compositor view state for a new xdg_toplevel. */
static void xdg_shell_new_toplevel(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, xdg_shell_new_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct comp_toplevel *view = calloc(1, sizeof(*view));
	view->server = server;
	view->xdg_toplevel = xdg_toplevel;
	view->scene_tree = wlr_scene_xdg_surface_create(server->windows_tree, xdg_toplevel->base);
	assert(view->scene_tree);
	view->scene_tree->node.data = view;
	view->tile_user_key = ++tile_user_key_gen;
	view->tile_float = false;
	view->tile_order = 0;
	view->workspace = server->current_workspace;
	view->foreign_toplevel = server->foreign_toplevel_manager ? wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager) : NULL;
	if (view->foreign_toplevel)
	{
		view->foreign_request_activate.notify = foreign_toplevel_handle_request_activate;
		wl_signal_add(&view->foreign_toplevel->events.request_activate, &view->foreign_request_activate);
		view->foreign_request_close.notify = foreign_toplevel_handle_request_close;
		wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_request_close);
	}

	view->set_title.notify = toplevel_handle_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
	view->set_app_id.notify = toplevel_handle_set_app_id;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);

	view->map.notify = toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
	view->unmap.notify = toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
	view->commit.notify = toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);
	view->destroy.notify = toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);
	view->request_move.notify = toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &view->request_resize);
	view->request_maximize.notify = toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
	view->request_minimize.notify = toplevel_request_minimize;
	wl_signal_add(&xdg_toplevel->events.request_minimize, &view->request_minimize);
	view->set_parent.notify = toplevel_handle_set_parent;
	wl_signal_add(&xdg_toplevel->events.set_parent, &view->set_parent);
	view->new_popup.notify = toplevel_handle_new_popup;
	wl_signal_add(&xdg_toplevel->base->events.new_popup, &view->new_popup);
	/* wlroots asserts here on older xdg-shell versions; gate capabilities by negotiated version. */
	const uint32_t shell_ver = (xdg_toplevel->base && xdg_toplevel->base->client &&
								xdg_toplevel->base->client->shell)
								   ? xdg_toplevel->base->client->shell->version
								   : 0;
	bool wm_caps_enabled = false;
	if (shell_ver >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
	{
		wlr_xdg_toplevel_set_wm_capabilities(
			xdg_toplevel,
			WLR_XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE);
		wm_caps_enabled = true;
		if (xdg_debug_logs_enabled)
		{
			wlr_log(WLR_INFO, "xdgdbg:wm_caps enabled shell_v=%u", shell_ver);
		}
	}
	else if (xdg_debug_logs_enabled)
	{
		wlr_log(WLR_INFO, "xdgdbg:wm_caps skipped shell_v=%u (< %u)", shell_ver,
				XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION);
	}
	log_new_toplevel_state(xdg_toplevel, shell_ver, wm_caps_enabled);
	wl_list_insert(server->toplevels.prev, &view->link);
	view->listed = true;
	foreign_toplevel_refresh(view);
}

/** Activate one mapped view and synchronize seat keyboard focus plus foreign-toplevel state. */
static void focus_toplevel(struct comp_server *server, struct comp_toplevel *toplevel)
{
	if (!toplevel || !toplevel_surface_mapped(toplevel))
	{
		return;
	}
	if (toplevel->xdg_toplevel && !toplevel->xdg_toplevel->base->initialized)
	{
		log_xdg_state("focus:skip-not-initialized", toplevel);
		return;
	}
	if (toplevel->workspace != server->current_workspace)
	{
		return;
	}
	if (server->grab != COMP_GRAB_NONE && server->grabbed_toplevel != toplevel)
	{
		/* A compositor-side move or resize must never survive a real focus handoff,
		 * otherwise later pointer motion keeps driving the old client after a new map/focus. */
		cancel_active_grab(server);
	}
	struct wlr_surface *surf = toplevel_wlr_surface(toplevel);
	struct comp_toplevel *prev = server->focused_toplevel;
	if (prev == toplevel)
	{
		/* Same toplevel: still repair seat keyboard focus if pointer leave or a
		 * layer click left the seat keyboard on a different surface. */
		if (surf && server->seat && server->seat->keyboard_state.focused_surface != surf)
		{
			struct wlr_keyboard *kbd = wlr_seat_get_keyboard(server->seat);
			if (kbd)
			{
				wlr_seat_keyboard_notify_enter(server->seat, surf, kbd->keycodes, kbd->num_keycodes,
											   &kbd->modifiers);
			}
			else
			{
				wlr_seat_keyboard_notify_enter(server->seat, surf, NULL, 0, NULL);
			}
		}
		return;
	}
	if (prev && toplevel_surface_initialized(prev))
	{
		log_xdg_state("focus:deactivate-prev", prev);
		toplevel_set_activated(prev, false);
		foreign_toplevel_refresh(prev);
	}
	log_xdg_state("focus:activate-new", toplevel);
	toplevel_set_activated(toplevel, true);
	if (server->layout == COMP_LAYOUT_STACK ||
		((server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL) && toplevel->tile_float))
	{
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}
	server->focused_toplevel = toplevel;
	foreign_toplevel_refresh(toplevel);
	scroll_sync_to_focused(server);

	if (!surf)
	{
		return;
	}
	struct wlr_seat *seat = server->seat;
	struct wlr_keyboard *kbd = wlr_seat_get_keyboard(seat);
	if (kbd)
	{
		wlr_seat_keyboard_notify_enter(seat, surf, kbd->keycodes, kbd->num_keycodes, &kbd->modifiers);
	}
	else
	{
		wlr_seat_keyboard_notify_enter(seat, surf, NULL, 0, NULL);
	}
}

/** Drop compositor-managed move/resize state so the next pointer event re-evaluates ownership cleanly. */
static void cancel_active_grab(struct comp_server *server)
{
	if (!server || server->grab == COMP_GRAB_NONE)
	{
		return;
	}
	struct comp_toplevel *grabbed = server->grabbed_toplevel;
	const bool rearrange = server->grab == COMP_GRAB_MOVE &&
						   (server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL) &&
						   grabbed && !grabbed->tile_float;
	if (server->grab == COMP_GRAB_RESIZE && grabbed)
	{
		grabbed->resize_anchor_finishing = true;
		toplevel_set_resizing(grabbed, false);
	}
	server->grab = COMP_GRAB_NONE;
	server->grabbed_toplevel = NULL;
	server->resize_edges = 0;
	server->swallow_left_release = false;
	if (rearrange)
	{
		server_arrange_toplevels(server);
	}
}

/** Clear keyboard focus and compositor activation state together so empty-root clicks leave no stale focus. */
static void clear_keyboard_focus(struct comp_server *server)
{
	if (!server || !server->seat)
	{
		return;
	}
	struct comp_toplevel *prev = server->focused_toplevel;
	if (prev && toplevel_surface_initialized(prev))
	{
		toplevel_set_activated(prev, false);
		foreign_toplevel_refresh(prev);
	}
	server->focused_toplevel = NULL;
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

/** Active pointer-driven keyboard focus policy from config (defaults to click-to-focus). */
static enum comp_focus_policy server_focus_policy(struct comp_server *server)
{
	if (server && server->config)
	{
		return server->config->focus_policy;
	}
	return COMP_FOCUS_CLICK;
}

/** Cancel a pending deferred motion-focus handoff without applying it. */
static void motion_focus_idle_cancel(struct comp_server *server)
{
	if (!server)
	{
		return;
	}
	if (server->motion_focus_idle)
	{
		wl_event_source_remove(server->motion_focus_idle);
		server->motion_focus_idle = NULL;
	}
	server->motion_focus_target = NULL;
	server->motion_focus_clear = false;
}

/** Idle callback: apply the latest scheduled FocusFollowsMouse / SloppyFocus handoff. */
static void motion_focus_idle_apply(void *data)
{
	struct comp_server *server = data;
	server->motion_focus_idle = NULL;
	if (!server->seat || server->display_terminate_requested || server->grab != COMP_GRAB_NONE)
	{
		server->motion_focus_target = NULL;
		server->motion_focus_clear = false;
		return;
	}
	struct comp_toplevel *target = server->motion_focus_target;
	const bool clear = server->motion_focus_clear;
	server->motion_focus_target = NULL;
	server->motion_focus_clear = false;
	if (target)
	{
		/* Target may have been destroyed between schedule and idle. */
		struct comp_toplevel *t;
		bool alive = false;
		wl_list_for_each(t, &server->toplevels, link)
		{
			if (t == target)
			{
				alive = true;
				break;
			}
		}
		if (alive)
		{
			focus_toplevel(server, target);
		}
		return;
	}
	if (clear && server->focused_toplevel)
	{
		clear_keyboard_focus(server);
	}
}

/**
 * Schedule FocusFollowsMouse / SloppyFocus keyboard handoff for after the current
 * pointer event finishes. ClickToFocus is a no-op. Layer-shell panel hover is ignored.
 *
 * empty_root must be true for true empty layout space and for desktop-like
 * BACKGROUND/BOTTOM layer surfaces (wallpapers). TOP/OVERLAY panels are not empty root.
 */
static void apply_motion_focus_policy(struct comp_server *server, struct comp_toplevel *under_cursor,
									  bool empty_root)
{
	enum comp_focus_policy policy = server_focus_policy(server);
	if (policy == COMP_FOCUS_CLICK || !server || !server->wl_display ||
		server->grab != COMP_GRAB_NONE || server->display_terminate_requested)
	{
		return;
	}

	struct comp_toplevel *target = NULL;
	bool clear = false;
	if (under_cursor)
	{
		if (under_cursor == server->focused_toplevel)
		{
			/* Pointer is back over the focused window: drop a pending empty-root clear. */
			if (server->motion_focus_idle && server->motion_focus_clear && !server->motion_focus_target)
			{
				motion_focus_idle_cancel(server);
			}
			/* Reassert seat keyboard on idle if a leave/layer click left it elsewhere. */
			struct wlr_surface *surf = toplevel_wlr_surface(under_cursor);
			if (!surf || !server->seat || server->seat->keyboard_state.focused_surface == surf)
			{
				return;
			}
			target = under_cursor;
		}
		else
		{
			target = under_cursor;
		}
	}
	else if (empty_root)
	{
		if (policy == COMP_FOCUS_FOLLOWS_MOUSE)
		{
			/* Empty desktop only: FocusFollowsMouse clears; SloppyFocus keeps the last focus.
			 * Hovering layer-shell panels does not clear keyboard focus. */
			if (!server->focused_toplevel)
			{
				return;
			}
			clear = true;
		}
		else if (policy == COMP_FOCUS_SLOPPY)
		{
			/* Keep last focus. Cancel a pending clear only; keep a pending enter-focus. */
			if (server->motion_focus_idle && server->motion_focus_clear && !server->motion_focus_target)
			{
				motion_focus_idle_cancel(server);
			}
			if (server->motion_focus_idle && server->motion_focus_target)
			{
				return;
			}
			if (!server->focused_toplevel)
			{
				return;
			}
			/* Reassert keyboard focus on the last toplevel after pointer leave to desktop. */
			struct wlr_surface *surf = toplevel_wlr_surface(server->focused_toplevel);
			if (!surf || !server->seat || server->seat->keyboard_state.focused_surface == surf)
			{
				return;
			}
			target = server->focused_toplevel;
		}
		else
		{
			return;
		}
	}
	else
	{
		return;
	}

	/* Coalesce to the latest pointer target; replace any pending idle request. */
	if (server->motion_focus_idle)
	{
		wl_event_source_remove(server->motion_focus_idle);
		server->motion_focus_idle = NULL;
	}
	server->motion_focus_target = target;
	server->motion_focus_clear = clear;
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	server->motion_focus_idle = wl_event_loop_add_idle(loop, motion_focus_idle_apply, server);
	if (!server->motion_focus_idle)
	{
		server->motion_focus_target = NULL;
		server->motion_focus_clear = false;
		wlr_log(WLR_ERROR, "focus: failed to schedule motion focus idle");
	}
}

/**
 * True when a layer-shell surface should count as "desktop" for focus policy
 * (FocusFollowsMouse clears; SloppyFocus keeps). BACKGROUND/BOTTOM wallpapers
 * qualify; TOP/OVERLAY panels do not.
 */
static bool layer_surface_is_desktop_backdrop(struct wlr_layer_surface_v1 *ls)
{
	if (!ls || !ls->surface || !ls->surface->mapped)
	{
		return false;
	}
	const enum zwlr_layer_shell_v1_layer lyr = ls->current.layer;
	return lyr == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND || lyr == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
}

/**
 * Classify the pointer position for keyboard focus policy.
 * Sets *out_toplevel when an XDG toplevel is under the cursor.
 * Returns true when the point is empty desktop (no toplevel; no panel).
 */
static bool pointer_focus_target_at(struct comp_server *server, double lx, double ly,
									struct comp_toplevel **out_toplevel)
{
	if (out_toplevel)
	{
		*out_toplevel = NULL;
	}
	if (!server)
	{
		return true;
	}
	double sx, sy;
	struct wlr_surface *surface = surface_at(server, lx, ly, &sx, &sy);
	if (!surface)
	{
		return true;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	if (root && wlr_xdg_toplevel_try_from_wlr_surface(root))
	{
		struct comp_toplevel *under = toplevel_at(server, lx, ly, &sx, &sy);
		if (out_toplevel)
		{
			*out_toplevel = under;
		}
		return false;
	}
	struct wlr_layer_surface_v1 *ls = root ? wlr_layer_surface_v1_try_from_wlr_surface(root) : NULL;
	if (layer_surface_is_desktop_backdrop(ls))
	{
		return true;
	}
	/* Panels / overlays / other layer surfaces: not empty desktop. */
	return false;
}

/** Start move grab and capture cursor/view origin for delta-based motion. */
static void begin_move(struct comp_server *server, struct comp_toplevel *view, bool swallow_left_release)
{
	server->grab = COMP_GRAB_MOVE;
	server->grabbed_toplevel = view;
	server->swallow_left_release = swallow_left_release;
	server->grab_cursor_x = server->cursor->x;
	server->grab_cursor_y = server->cursor->y;
	server->grab_view_x = view->scene_tree->node.x;
	server->grab_view_y = view->scene_tree->node.y;
}

/** True while either the committed or newest scheduled client state is maximized. */
static bool toplevel_effectively_maximized(struct comp_toplevel *view)
{
	return view && view->xdg_toplevel &&
		   (view->xdg_toplevel->current.maximized ||
			view->xdg_toplevel->scheduled.maximized ||
			view->xdg_toplevel->requested.maximized);
}

/** True when direct interactive resize is allowed for this toplevel under current layout policy. */
static bool toplevel_can_direct_resize(struct comp_server *server, struct comp_toplevel *view)
{
	if (!server || !view || !view->xdg_toplevel || !toplevel_surface_mapped(view))
	{
		return false;
	}
	if ((server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL) && !view->tile_float)
	{
		return false;
	}
	if (toplevel_effectively_maximized(view))
	{
		return false;
	}
	return true;
}

/** Resolve the toplevel hit-test box in layout coordinates. */
static bool toplevel_get_hit_box(struct comp_toplevel *view, struct wlr_box *out)
{
	if (!view || !out || !view->xdg_toplevel || !view->scene_tree)
	{
		return false;
	}
	const struct wlr_box geo = view->xdg_toplevel->base->geometry;
	if (geo.width > 0 && geo.height > 0)
	{
		/* xdg window geometry excludes native CSD shadows and includes bridge-provided
		 * decorations outside the base surface. It is therefore the common coordinate
		 * space for both classes, independent of xdg-decoration negotiation. */
		out->x = view->scene_tree->node.x + geo.x;
		out->y = view->scene_tree->node.y + geo.y;
		out->width = geo.width;
		out->height = geo.height;
		return true;
	}

	/* Initial commits may not have window geometry yet. Use the base surface only until
	 * the client supplies authoritative geometry on a later commit. */
	struct wlr_surface *surface = view->xdg_toplevel->base->surface;
	if (!surface || surface->current.width <= 0 || surface->current.height <= 0)
	{
		return false;
	}
	out->x = view->scene_tree->node.x;
	out->y = view->scene_tree->node.y;
	out->width = surface->current.width;
	out->height = surface->current.height;
	return true;
}

/**
 * True when the client is expected to own interactive edge resizing inside its visible frame.
 *
 * Native CSD clients that negotiate xdg-decoration in client-side mode typically provide
 * their own resize borders and cursor updates. Legacy clients and clients without an
 * xdg-decoration object need a compositor-owned fallback resize ring instead.
 */
static bool toplevel_uses_client_side_resize(struct comp_toplevel *view)
{
	if (!view || !view->xdg_decoration)
	{
		return false;
	}
	return view->xdg_decoration->current.mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE ||
		   view->xdg_decoration->scheduled_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
}

/**
 * True when Morph must provide resize edges despite negotiated client-side decorations.
 *
 * Xwayland bridges can advertise CSD while placing a synthetic titlebar outside the base
 * surface. Such windows look decorated but do not issue xdg_toplevel.request_resize for
 * their frame. Native CSD generally keeps its window geometry within the base surface and
 * continues to own its inner resize border.
 */
static bool toplevel_needs_compositor_resize_fallback(struct comp_toplevel *view)
{
	if (!view || !view->xdg_toplevel || !view->xdg_toplevel->base)
	{
		return false;
	}
	const struct wlr_xdg_surface *xdg = view->xdg_toplevel->base;
	const struct wlr_surface *surface = xdg->surface;
	const struct wlr_box geometry = xdg->geometry;
	if (!surface || surface->current.width <= 0 || surface->current.height <= 0 ||
		geometry.width <= 0 || geometry.height <= 0)
	{
		return !toplevel_uses_client_side_resize(view);
	}

	const bool geometry_outside_surface =
		geometry.x < 0 || geometry.y < 0 ||
		geometry.x + geometry.width > surface->current.width ||
		geometry.y + geometry.height > surface->current.height;
	if (geometry_outside_surface)
	{
		return true;
	}

	/* Insets identify native CSD shadows even when the client never binds xdg-decoration.
	 * Mousepad, for example, reports a 9 px inset on every side. */
	const bool geometry_inset =
		geometry.x > 0 || geometry.y > 0 ||
		geometry.width < surface->current.width ||
		geometry.height < surface->current.height;
	if (geometry_inset)
	{
		return false;
	}

	return !toplevel_uses_client_side_resize(view);
}

/**
 * Resolve the visible frame used to arm compositor-owned resize edges.
 *
 * xwayland-satellite reports its synthetic titlebar through negative XDG geometry while
 * its visible frame can extend beyond the base surface through subsurfaces. Keep XDG
 * geometry for configure sizes, but use the complete mapped surface extents for this
 * bridge-only hitbox so resize edges follow the frame that was actually drawn.
 */
static bool toplevel_get_resize_hit_box(struct comp_toplevel *view, struct wlr_box *out)
{
	if (!toplevel_get_hit_box(view, out))
	{
		return false;
	}
	if (!toplevel_needs_compositor_resize_fallback(view) || !view->xdg_toplevel->base->surface)
	{
		return true;
	}

	struct wlr_surface *surface = view->xdg_toplevel->base->surface;
	struct wlr_box extents = {0};
	wlr_surface_get_extents(surface, &extents);
	if (extents.width > 0 && extents.height > 0)
	{
		out->x = view->scene_tree->node.x + extents.x;
		out->y = view->scene_tree->node.y + extents.y;
		out->width = extents.width;
		out->height = extents.height;
	}
	return true;
}

/**
 * Expose compositor-owned interactive resize state to the client for the lifetime of the grab.
 *
 * Some client-side decorated and bridged toolkits accept the requested size only while the
 * xdg-toplevel is explicitly marked as being resized by the compositor.
 */
static void toplevel_set_resizing(struct comp_toplevel *view, bool resizing)
{
	if (!view || !view->xdg_toplevel || !toplevel_surface_initialized(view))
	{
		return;
	}
	toplevel_track_configure(view,
		wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, resizing));
}

/**
 * Apply client-provided xdg_toplevel min/max constraints without moving the opposite edge.
 *
 * When resizing from left or top, a size clamp must shift the requested origin by the
 * rejected delta. Otherwise the fixed far edge jumps even though the client only limited
 * its usable content size.
 */
static void toplevel_constrain_resize(struct comp_toplevel *view, uint32_t edges,
								  int *x, int *y, int *width, int *height)
{
	if (!view || !view->xdg_toplevel || !x || !y || !width || !height)
	{
		return;
	}

	const struct wlr_xdg_toplevel_state *state = &view->xdg_toplevel->current;
	int constrained_width = *width < 1 ? 1 : *width;
	int constrained_height = *height < 1 ? 1 : *height;
	if (state->min_width > 0 && constrained_width < state->min_width)
	{
		constrained_width = state->min_width;
	}
	if (state->max_width > 0 && constrained_width > state->max_width)
	{
		constrained_width = state->max_width;
	}
	if (state->min_height > 0 && constrained_height < state->min_height)
	{
		constrained_height = state->min_height;
	}
	if (state->max_height > 0 && constrained_height > state->max_height)
	{
		constrained_height = state->max_height;
	}

	if ((edges & WLR_EDGE_LEFT) && constrained_width != *width)
	{
		*x += *width - constrained_width;
	}
	if ((edges & WLR_EDGE_TOP) && constrained_height != *height)
	{
		*y += *height - constrained_height;
	}
	*width = constrained_width;
	*height = constrained_height;
}

/** Reposition a committed left/top resize buffer while keeping its opposite edge fixed. */
static void toplevel_update_resize_anchor(struct comp_toplevel *view)
{
	if (!view || !view->resize_anchor_active || !view->xdg_toplevel || !view->scene_tree)
	{
		return;
	}

	const struct wlr_box geometry = view->xdg_toplevel->base->geometry;
	if (geometry.width <= 0 || geometry.height <= 0)
	{
		return;
	}

	int scene_x = view->scene_tree->node.x;
	int scene_y = view->scene_tree->node.y;
	if (view->resize_anchor_edges & WLR_EDGE_LEFT)
	{
		scene_x = view->resize_anchor_right - geometry.width - geometry.x;
	}
	if (view->resize_anchor_edges & WLR_EDGE_TOP)
	{
		scene_y = view->resize_anchor_bottom - geometry.height - geometry.y;
	}
	wlr_scene_node_set_position(&view->scene_tree->node, scene_x, scene_y);

	if (view->resize_anchor_finishing &&
		!view->xdg_toplevel->current.resizing && !view->xdg_toplevel->pending.resizing)
	{
		/* Wait until both current and pending state are non-resizing. GTK2 bridge clients
		 * can commit one last resized buffer after the grab has ended; dropping the anchor
		 * earlier lets that commit pull the left/top edge back to the old position. */
		view->resize_anchor_active = false;
		view->resize_anchor_finishing = false;
	}
}

/**
 * Apply one compositor resize target in XDG window-geometry coordinates.
 *
 * The client configure uses window-geometry size, while the scene node is positioned in
 * layout coordinates. Left/top anchored resizes keep the scene node fixed until the next
 * commit reports the new geometry, then toplevel_update_resize_anchor() corrects it.
 */
static void toplevel_apply_resize_box(struct comp_toplevel *view, int x, int y, int width, int height)
{
	const struct wlr_box geometry = view->xdg_toplevel->base->geometry;
	if (geometry.width > 0 && geometry.height > 0)
	{
		const int scene_x = view->resize_anchor_active &&
			(view->resize_anchor_edges & WLR_EDGE_LEFT)
				? view->scene_tree->node.x
				: x - geometry.x;
		const int scene_y = view->resize_anchor_active &&
			(view->resize_anchor_edges & WLR_EDGE_TOP)
				? view->scene_tree->node.y
				: y - geometry.y;
		wlr_scene_node_set_position(&view->scene_tree->node, scene_x, scene_y);
	}
	else
	{
		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	}
	toplevel_arrange_tile(view, x, y, width, height);
}

/** True when layout point lies within box bounds. */
static bool point_in_box(const struct wlr_box *box, double x, double y)
{
	if (!box)
	{
		return false;
	}
	return x >= (double)box->x && x < (double)(box->x + box->width) &&
		   y >= (double)box->y && y < (double)(box->y + box->height);
}

/** Clamp a floating toplevel geometry box into the usable output workarea. */
static void toplevel_clamp_floating_box_to_workarea(struct comp_toplevel *view,
													int *scene_x, int *scene_y, int *width, int *height)
{
	if (!view || !view->xdg_toplevel || !scene_x || !scene_y || !width || !height)
	{
		return;
	}
	struct comp_output *out = toplevel_preferred_output(view);
	if (!out || out->layer_workarea.width <= 0 || out->layer_workarea.height <= 0)
	{
		return;
	}
	struct wlr_box usable = out->layer_workarea;
	const struct wlr_box geometry = view->xdg_toplevel->base->geometry;

	if (*width > usable.width)
	{
		*width = usable.width;
	}
	if (*height > usable.height)
	{
		*height = usable.height;
	}

	/* Scene node coordinates include xdg window-geometry offsets. Clamp the visible
	 * toplevel geometry, then convert the corrected position back to scene space. */
	int geometry_x = *scene_x + geometry.x;
	int geometry_y = *scene_y + geometry.y;
	const int max_x = usable.x + usable.width - *width;
	const int max_y = usable.y + usable.height - *height;
	if (geometry_x < usable.x)
	{
		geometry_x = usable.x;
	}
	else if (geometry_x > max_x)
	{
		geometry_x = max_x;
	}
	if (geometry_y < usable.y)
	{
		geometry_y = usable.y;
	}
	else if (geometry_y > max_y)
	{
		geometry_y = max_y;
	}
	*scene_x = geometry_x - geometry.x;
	*scene_y = geometry_y - geometry.y;
}

/** Determine resize edge mask from global cursor coords against toplevel geometry bounds. */
static uint32_t toplevel_resize_edges_at(struct comp_toplevel *view, double cx, double cy)
{
	if (!view || !view->xdg_toplevel || !view->scene_tree)
	{
		return 0;
	}
	struct wlr_box box;
	if (!toplevel_get_resize_hit_box(view, &box))
	{
		return 0;
	}

	/* Native CSD owns its inner frame. Legacy and bridge-provided decorations need Morph's ring. */
	const bool compositor_fallback = toplevel_needs_compositor_resize_fallback(view);
	const int border = compositor_fallback ? 6 : 1;
	const double left = (double)box.x;
	double top = (double)box.y;
	const double right = left + (double)box.width;
	/* Right/bottom cursor shapes often have a hotspot perceived a few px inward.
	 * Shift detection slightly so visual edge alignment feels consistent. */
	const double hotspot_comp = border >= 4 ? 2.0 : 0.0;
	const double right_comp = hotspot_comp;
	const double bottom_comp = 0.0;
	double bottom = top + (double)box.height;
	bool compact_bridge_frame = false;
	if (compositor_fallback)
	{
		const struct wlr_xdg_surface *xdg = view->xdg_toplevel->base;
		struct wlr_box extents = {0};
		wlr_surface_get_extents(xdg->surface, &extents);
		const int geometry_bottom = xdg->geometry.y + xdg->geometry.height;
		const int extents_bottom = extents.y + extents.height;
		if (xdg->geometry.y < 0 && extents_bottom <= geometry_bottom)
		{
			compact_bridge_frame = true;
			/* Compact satellite frames expose the titlebar above the base surface but no
			 * complete outer frame extents. Move the resize edges past that transparent
			 * margin, unlike native CSD which reports its complete shadow extents. */
			top += (double)(border * 2) + hotspot_comp;
			bottom += (double)(border * 2) + hotspot_comp;
		}
	}

	uint32_t edges = 0;
	const double vertical_band_top = compact_bridge_frame ? top : top - (double)border;
	const bool within_y_band = cy >= vertical_band_top && cy <= bottom + (double)border;
	const bool within_x_band = cx >= left - (double)border && cx <= right + (double)border;

	if (!compositor_fallback)
	{
		/* Outside-only edge zones: do not arm compositor resize from inside CSD client content. */
		if (within_y_band && cx >= left - (double)border && cx < left)
		{
			edges |= WLR_EDGE_LEFT;
		}
		else if (within_y_band && (cx + right_comp) > right &&
				 (cx + right_comp) <= right + (double)border)
		{
			edges |= WLR_EDGE_RIGHT;
		}

		if (within_x_band && cy >= top - (double)border && cy < top)
		{
			edges |= WLR_EDGE_TOP;
		}
		else if (within_x_band && (cy + bottom_comp) > bottom &&
				 (cy + bottom_comp) <= bottom + (double)border)
		{
			edges |= WLR_EDGE_BOTTOM;
		}
	}
	else
	{
		/* Legacy and bridge-decorated clients need an inner ring because no client resize request arrives. */
		if (within_y_band && cx >= left - (double)border && cx < left + (double)border)
		{
			edges |= WLR_EDGE_LEFT;
		}
		else if (within_y_band && (cx + right_comp) > right - (double)border &&
				 (cx + right_comp) <= right + (double)border)
		{
			edges |= WLR_EDGE_RIGHT;
		}

		if (within_x_band && cy >= vertical_band_top && cy < top + (double)border)
		{
			edges |= WLR_EDGE_TOP;
		}
		else if (within_x_band && (cy + bottom_comp) >= bottom &&
				 (cy + bottom_comp) <= bottom + (double)border)
		{
			edges |= WLR_EDGE_BOTTOM;
		}
	}

	return edges;
}

/** Determine resize edge mask from current global cursor position for one toplevel. */
static uint32_t toplevel_resize_edges_at_cursor(struct comp_server *server, struct comp_toplevel *view)
{
	if (!server || !server->cursor || !view || !view->scene_tree)
	{
		return 0;
	}
	/* Cursor position is in global layout coordinates, matching hit-box space. */
	return toplevel_resize_edges_at(view, server->cursor->x, server->cursor->y);
}

/** Effective resize edges at cursor (outside-only policy handled in hit-test). */
static uint32_t toplevel_effective_resize_edges_at_cursor(struct comp_server *server,
														  struct comp_toplevel *view)
{
	return toplevel_resize_edges_at_cursor(server, view);
}

/**
 * Focused top-level frame under the cursor even when transparent holes expose lower clients.
 *
 * xwayland-satellite frames can have transparent titlebar/border regions. Scene hit-testing
 * then sees the window underneath, but input policy still has to treat the focused frame as
 * the owner so lower clients cannot steal cursors or pointer focus through those holes.
 */
static struct comp_toplevel *focused_toplevel_frame_at_cursor(struct comp_server *server)
{
	if (!server || !server->cursor || !server->focused_toplevel)
	{
		return NULL;
	}
	struct comp_toplevel *focused = server->focused_toplevel;
	if (focused->workspace != server->current_workspace || focused->minimized ||
		!toplevel_surface_mapped(focused))
	{
		return NULL;
	}
	struct wlr_box box;
	if (!toplevel_get_hit_box(focused, &box) ||
		!point_in_box(&box, server->cursor->x, server->cursor->y))
	{
		return NULL;
	}
	return focused;
}

/**
 * Find a toplevel near the cursor whose border ring currently matches resize hit-test.
 *
 * This deliberately prefers the focused frame before probing scene surfaces. Bridged
 * decorations can expose lower clients through transparent pixels, but those lower clients
 * must not receive resize/cursor authority while the pointer is still on the top frame.
 */
static struct comp_toplevel *toplevel_near_cursor_resize_edge(struct comp_server *server)
{
	if (!server || !server->cursor)
	{
		return NULL;
	}
	const double cx = server->cursor->x;
	const double cy = server->cursor->y;

	/* Bridge-decorated frames can contain transparent titlebar/border holes. Scene
	 * hit-testing may then resolve the client underneath, but the focused top-level
	 * still owns the visible edge under the pointer and must keep resize/cursor control. */
	struct comp_toplevel *focused = server->focused_toplevel;
	if (focused && focused->workspace == server->current_workspace && !focused->minimized &&
		toplevel_can_direct_resize(server, focused) && toplevel_resize_edges_at(focused, cx, cy))
	{
		return focused;
	}
	const bool cursor_inside_focused_frame = focused_toplevel_frame_at_cursor(server) != NULL;

	/* Prefer the visible surface under the cursor. Layer-shell surfaces, popups, and
	 * client-owned CSD controls must not be treated as empty space just because they
	 * do not resolve to a managed toplevel. Otherwise panels such as sfwbar can lose
	 * pointer focus while hovering over a maximized window's hidden resize ring. */
	double sx, sy;
	struct wlr_surface *surface = surface_at(server, cx, cy, &sx, &sy);
	if (surface)
	{
		struct wlr_surface *root = wlr_surface_get_root_surface(surface);
		struct comp_toplevel *hit;
		wl_list_for_each(hit, &server->toplevels, link)
		{
			if (hit->xdg_toplevel && hit->xdg_toplevel->base->surface == root)
			{
				if (cursor_inside_focused_frame && hit != focused)
				{
					return NULL;
				}
				if (toplevel_can_direct_resize(server, hit) && toplevel_resize_edges_at(hit, cx, cy))
				{
					return hit;
				}
				return NULL;
			}
		}
		/* Do not probe lower windows when any non-toplevel surface owns the pointer position. */
		return NULL;
	}

	/* Also allow the outside part of the edge ring (no surface under cursor). */
	struct comp_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link)
	{
		if (!toplevel_can_direct_resize(server, v) || v->workspace != server->current_workspace || v->minimized)
		{
			continue;
		}
		if (toplevel_resize_edges_at(v, cx, cy))
		{
			return v;
		}
	}
	return NULL;
}

/** Map wlroots edge bitmask to standard xcursor resize shape names. */
static const char *cursor_name_for_resize_edges(uint32_t edges)
{
	switch (edges)
	{
	case WLR_EDGE_TOP | WLR_EDGE_LEFT:
		return "top_left_corner";
	case WLR_EDGE_TOP | WLR_EDGE_RIGHT:
		return "top_right_corner";
	case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT:
		return "bottom_left_corner";
	case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT:
		return "bottom_right_corner";
	case WLR_EDGE_LEFT:
		return "left_side";
	case WLR_EDGE_RIGHT:
		return "right_side";
	case WLR_EDGE_TOP:
		return "top_side";
	case WLR_EDGE_BOTTOM:
		return "bottom_side";
	default:
		return NULL;
	}
}

/** Start resize grab and cache the initial geometry for edge-constrained resizing. */
static void begin_resize(struct comp_server *server, struct comp_toplevel *view, uint32_t edges)
{
	if (!view || !toplevel_surface_mapped(view))
	{
		return;
	}
	server->grab = COMP_GRAB_RESIZE;
	server->grabbed_toplevel = view;
	server->grab_cursor_x = server->cursor->x;
	server->grab_cursor_y = server->cursor->y;
	server->resize_edges = edges;
	server->resize_last_configure_msec = 0;
	server->resize_pending_valid = false;

	struct wlr_box box;
	if (toplevel_get_hit_box(view, &box))
	{
		server->grab_view_x = box.x;
		server->grab_view_y = box.y;
		server->grab_view_width = box.width;
		server->grab_view_height = box.height;
	}
	else
	{
		const struct wlr_box geo = view->xdg_toplevel->base->geometry;
		server->grab_view_x = view->scene_tree->node.x + geo.x;
		server->grab_view_y = view->scene_tree->node.y + geo.y;
		server->grab_view_width = geo.width;
		server->grab_view_height = geo.height;
	}
	view->resize_anchor_edges = edges;
	view->resize_anchor_right = server->grab_view_x + server->grab_view_width;
	view->resize_anchor_bottom = server->grab_view_y + server->grab_view_height;
	view->resize_anchor_active = (edges & (WLR_EDGE_LEFT | WLR_EDGE_TOP)) != 0;
	view->resize_anchor_finishing = false;
	log_resize_state("begin", view, server->grab_view_x, server->grab_view_y,
					 server->grab_view_width, server->grab_view_height, edges);
	toplevel_set_resizing(view, true);
	server->swallow_left_release = false;
}

/** Compute near-square tile grid dimensions for `n` windows. */
static void tile_grid_dims(size_t n, int *cols_out, int *rows_out)
{
	/* ~square grid: ceil(sqrt(n)) columns so both axes get space (not only full-height strips). */
	int cols = 1;
	while ((size_t)cols * (size_t)cols < n)
	{
		cols++;
	}
	int rows = (int)((n + (size_t)cols - 1) / (size_t)cols);
	*cols_out = cols;
	*rows_out = rows;
}

/** Arrange mapped non-floating toplevels for tile or scroll layout across outputs. */
void server_arrange_toplevels(struct comp_server *server)
{
	if (server->layout != COMP_LAYOUT_TILE && server->layout != COMP_LAYOUT_SCROLL)
	{
		return;
	}
	if (wl_list_empty(&server->outputs))
	{
		return;
	}
	size_t n_full = 0;
	struct comp_toplevel **full = tile_sorted_views(server, &n_full);
	if (!full)
	{
		struct comp_toplevel *u;
		wl_list_for_each(u, &server->toplevels, link)
		{
			u->layout_anim_tracked = false;
		}
		return;
	}

	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (!toplevel_surface_mapped(t))
		{
			continue;
		}
		if (t->tile_float || !toplevel_surface_initialized(t))
		{
			t->layout_anim_tracked = false;
		}
	}

	const bool anim_on = layout_anim_effective(server);
	struct comp_output *out;
	wl_list_for_each(out, &server->outputs, link)
	{
		struct wlr_box box = out->layer_workarea;
		if (box.width <= 0 || box.height <= 0)
		{
			continue;
		}
		size_t n_tile = 0;
		struct comp_toplevel **arr = tile_sorted_views_on_output(server, out, full, n_full, &n_tile);
		if (!arr || n_tile == 0)
		{
			free(arr);
			continue;
		}

		if (server->layout == COMP_LAYOUT_SCROLL)
		{
			/* Scroll layout is a 1D strip per output; workspace slot selects the visible column. */
			int *const scr = &out->workspace_scroll_slot[server->current_workspace];
			if (*scr < 0)
			{
				*scr = 0;
			}
			if (*scr >= (int)n_tile)
			{
				*scr = (int)n_tile - 1;
			}
			for (size_t j = 0; j < n_tile; j++)
			{
				struct comp_toplevel *v = arr[j];
				if (!toplevel_surface_initialized(v))
				{
					log_xdg_state("arrange-scroll:skip-not-initialized", v);
					continue;
				}
				const int rel = (int)j - *scr;
				const int x = box.x + rel * box.width;
				const int y = box.y;
				v->layout_tgt_x = x;
				v->layout_tgt_y = y;
				log_xdg_state("arrange-scroll:set_size", v);
				toplevel_arrange_tile(v, x, y, box.width, box.height);
				if (!anim_on || !v->layout_anim_tracked)
				{
					v->layout_anim_x = (double)x;
					v->layout_anim_y = (double)y;
					wlr_scene_node_set_position(&v->scene_tree->node, x, y);
					v->layout_anim_tracked = true;
				}
			}
			free(arr);
			continue;
		}

		int cols, rows;
		tile_grid_dims(n_tile, &cols, &rows);
		const int cell_w = box.width / cols;
		const int cell_h = box.height / rows;

		for (size_t j = 0; j < n_tile; j++)
		{
			struct comp_toplevel *v = arr[j];
			if (!toplevel_surface_initialized(v))
			{
				log_xdg_state("arrange-tile:skip-not-initialized", v);
				continue;
			}
			const int col = (int)(j % (size_t)cols);
			const int row = (int)(j / (size_t)cols);
			const int x = box.x + col * cell_w;
			const int y = box.y + row * cell_h;
			const int w = (col == cols - 1) ? (box.x + box.width - x) : cell_w;
			const int h = (row == rows - 1) ? (box.y + box.height - y) : cell_h;
			v->layout_tgt_x = x;
			v->layout_tgt_y = y;
			log_xdg_state("arrange-tile:set_size", v);
			toplevel_arrange_tile(v, x, y, w, h);
			if (!anim_on || !v->layout_anim_tracked)
			{
				v->layout_anim_x = (double)x;
				v->layout_anim_y = (double)y;
				wlr_scene_node_set_position(&v->scene_tree->node, x, y);
				v->layout_anim_tracked = true;
			}
		}
		free(arr);
	}
	free(full);
	layout_anim_kick_outputs(server);
}

/** Enable only windows belonging to the active workspace. */
void server_workspace_apply_visibility(struct comp_server *server)
{
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (!toplevel_surface_mapped(t))
		{
			wlr_scene_node_set_enabled(&t->scene_tree->node, false);
			continue;
		}
		const bool vis = t->workspace == server->current_workspace && !t->minimized;
		wlr_scene_node_set_enabled(&t->scene_tree->node, vis);
	}
}

/** Switch active workspace with bounds clamp and focus handoff. */
void server_workspace_go(struct comp_server *server, int idx)
{
	if (idx < 0)
	{
		idx = 0;
	}
	if (idx >= COMP_WORKSPACE_COUNT)
	{
		idx = COMP_WORKSPACE_COUNT - 1;
	}
	if (idx == server->current_workspace)
	{
		return;
	}
	server->current_workspace = idx;
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		t->layout_anim_tracked = false;
	}
	if (server->focused_toplevel && server->focused_toplevel->workspace != server->current_workspace)
	{
		clear_keyboard_focus(server);
	}
	struct comp_toplevel *pick = NULL;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (t->workspace == server->current_workspace && toplevel_surface_mapped(t) &&
			toplevel_surface_initialized(t) && !t->minimized)
		{
			pick = t;
			break;
		}
	}
	if (pick)
	{
		focus_toplevel(server, pick);
	}
	server_workspace_apply_visibility(server);
	if (server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL)
	{
		layer_shell_arrange(server);
	}
	comp_config_sync_shell_env(server);
	ext_workspace_notify(server);
	foreign_toplevel_sync_all(server);
}

/** Relative workspace navigation with wrap-around. */
void server_workspace_relative(struct comp_server *server, int delta)
{
	if (delta == 0)
	{
		return;
	}
	const int n = COMP_WORKSPACE_COUNT;
	int idx = server->current_workspace + delta;
	idx %= n;
	if (idx < 0)
	{
		idx += n;
	}
	server_workspace_go(server, idx);
}

/** Move focused window to another workspace, then repair focus/visibility. */
void server_workspace_move_focused(struct comp_server *server, int target)
{
	struct comp_toplevel *f = server->focused_toplevel;
	if (!f || !toplevel_surface_mapped(f))
	{
		return;
	}
	if (target < 0 || target >= COMP_WORKSPACE_COUNT || f->workspace == target)
	{
		return;
	}
	const bool was_focused = server->focused_toplevel == f;
	f->workspace = target;
	f->layout_anim_tracked = false;
	if (was_focused && target != server->current_workspace)
	{
		struct comp_toplevel *pick = NULL;
		struct comp_toplevel *t;
		wl_list_for_each(t, &server->toplevels, link)
		{
			if (t != f && t->workspace == server->current_workspace && toplevel_surface_mapped(t))
			{
				pick = t;
				break;
			}
		}
		if (pick)
		{
			focus_toplevel(server, pick);
		}
		else
		{
			clear_keyboard_focus(server);
		}
	}
	server_workspace_apply_visibility(server);
	if (server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL)
	{
		layer_shell_arrange(server);
	}
	comp_config_sync_shell_env(server);
	server_sync_xdg_decorations(server);
	foreign_toplevel_sync_all(server);
}

/** Move focused tiled view by N slots in sort order on its assigned output. */
void server_tile_move_focused_n(struct comp_server *server, int steps)
{
	if ((server->layout != COMP_LAYOUT_TILE && server->layout != COMP_LAYOUT_SCROLL) || steps == 0)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	if (!f || f->tile_float || !toplevel_surface_mapped(f))
	{
		return;
	}

	const int sig = steps > 0 ? 1 : -1;
	const int nabs = steps > 0 ? steps : -steps;
	struct comp_output *out = toplevel_tile_output(f);
	for (int c = 0; c < nabs; c++)
	{
		size_t n_full = 0;
		struct comp_toplevel **full = tile_sorted_views(server, &n_full);
		if (!full)
		{
			return;
		}
		size_t n = 0;
		struct comp_toplevel **sorted = tile_sorted_views_on_output(server, out, full, n_full, &n);
		free(full);
		if (!sorted || n < 2)
		{
			free(sorted);
			return;
		}
		const int i = tile_sorted_index(sorted, n, f);
		if (i < 0)
		{
			free(sorted);
			return;
		}
		const int j = i + sig;
		if (j < 0 || j >= (int)n)
		{
			free(sorted);
			return;
		}
		tile_swap_sort_keys(f, sorted[(size_t)j]);
		free(sorted);
	}
	server_arrange_toplevels(server);
}

/** Move scroll viewport slot by N on the focused/primary output in scroll layout. */
void server_scroll_move(struct comp_server *server, int steps)
{
	if (server->layout != COMP_LAYOUT_SCROLL || steps == 0)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	struct comp_output *out;
	if (f && !f->tile_float && toplevel_surface_mapped(f))
	{
		out = toplevel_tile_output(f);
	}
	else
	{
		out = comp_output_from_wlr(server, primary_wlr_output(server));
	}
	if (!out)
	{
		return;
	}
	size_t n_full = 0;
	struct comp_toplevel **full = tile_sorted_views(server, &n_full);
	if (!full)
	{
		return;
	}
	size_t n = 0;
	struct comp_toplevel **arr = tile_sorted_views_on_output(server, out, full, n_full, &n);
	free(full);
	if (!arr || n == 0)
	{
		free(arr);
		return;
	}
	int *const scr = &out->workspace_scroll_slot[server->current_workspace];
	int idx = *scr + steps;
	if (idx < 0)
	{
		idx = 0;
	}
	else if (idx >= (int)n)
	{
		idx = (int)n - 1;
	}
	*scr = idx;
	free(arr);
	server_arrange_toplevels(server);
}

/** Move focused tiled view to start/end of sort order on its assigned output. */
void server_tile_move_focused_edge(struct comp_server *server, bool to_first)
{
	if (server->layout != COMP_LAYOUT_TILE && server->layout != COMP_LAYOUT_SCROLL)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	if (!f || f->tile_float || !toplevel_surface_mapped(f))
	{
		return;
	}
	struct comp_output *out = toplevel_tile_output(f);
	for (;;)
	{
		size_t n_full = 0;
		struct comp_toplevel **full = tile_sorted_views(server, &n_full);
		if (!full)
		{
			return;
		}
		size_t n = 0;
		struct comp_toplevel **sorted = tile_sorted_views_on_output(server, out, full, n_full, &n);
		free(full);
		if (!sorted || n < 2)
		{
			free(sorted);
			return;
		}
		const int i = tile_sorted_index(sorted, n, f);
		if (i < 0)
		{
			free(sorted);
			return;
		}
		const int j = to_first ? i - 1 : i + 1;
		if (j < 0 || j >= (int)n)
		{
			free(sorted);
			break;
		}
		tile_swap_sort_keys(f, sorted[(size_t)j]);
		free(sorted);
	}
	server_arrange_toplevels(server);
}

/** Move focused tiled view vertically in grid coordinates by N rows. */
void server_tile_move_focused_grid_vert(struct comp_server *server, int steps)
{
	if (server->layout != COMP_LAYOUT_TILE || steps == 0)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	if (!f || f->tile_float || !toplevel_surface_mapped(f))
	{
		return;
	}
	const int sig = steps > 0 ? 1 : -1;
	const int nabs = steps > 0 ? steps : -steps;
	struct comp_output *out = toplevel_tile_output(f);
	for (int c = 0; c < nabs; c++)
	{
		size_t n_full = 0;
		struct comp_toplevel **full = tile_sorted_views(server, &n_full);
		if (!full)
		{
			return;
		}
		size_t n = 0;
		struct comp_toplevel **sorted = tile_sorted_views_on_output(server, out, full, n_full, &n);
		free(full);
		if (!sorted || n < 2)
		{
			free(sorted);
			return;
		}
		int cols, rows;
		tile_grid_dims(n, &cols, &rows);
		(void)rows;
		const int i = tile_sorted_index(sorted, n, f);
		if (i < 0)
		{
			free(sorted);
			return;
		}
		const int j = sig > 0 ? i + cols : i - cols;
		if (j < 0 || j >= (int)n)
		{
			free(sorted);
			return;
		}
		tile_swap_sort_keys(f, sorted[(size_t)j]);
		free(sorted);
	}
	server_arrange_toplevels(server);
}

/** Move focused tiled view to top/bottom edge of its current tile column. */
void server_tile_move_focused_grid_vert_edge(struct comp_server *server, bool to_top)
{
	if (server->layout != COMP_LAYOUT_TILE)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	if (!f || f->tile_float || !toplevel_surface_mapped(f))
	{
		return;
	}
	struct comp_output *out = toplevel_tile_output(f);
	for (;;)
	{
		size_t n_full = 0;
		struct comp_toplevel **full = tile_sorted_views(server, &n_full);
		if (!full)
		{
			return;
		}
		size_t n = 0;
		struct comp_toplevel **sorted = tile_sorted_views_on_output(server, out, full, n_full, &n);
		free(full);
		if (!sorted || n < 2)
		{
			free(sorted);
			return;
		}
		int cols, rows;
		tile_grid_dims(n, &cols, &rows);
		(void)rows;
		const int i = tile_sorted_index(sorted, n, f);
		if (i < 0)
		{
			free(sorted);
			return;
		}
		int j;
		if (to_top)
		{
			if (i < cols)
			{
				free(sorted);
				break;
			}
			j = i - cols;
		}
		else
		{
			if (i + cols >= (int)n)
			{
				free(sorted);
				break;
			}
			j = i + cols;
		}
		tile_swap_sort_keys(f, sorted[(size_t)j]);
		free(sorted);
	}
	server_arrange_toplevels(server);
}

/** Move focused tiled view horizontally within its current grid row by N columns. */
void server_tile_move_focused_grid_horiz(struct comp_server *server, int steps)
{
	if (server->layout != COMP_LAYOUT_TILE || steps == 0)
	{
		return;
	}
	struct comp_toplevel *f = server->focused_toplevel;
	if (!f || f->tile_float || !toplevel_surface_mapped(f))
	{
		return;
	}
	const int sig = steps > 0 ? 1 : -1;
	const int nabs = steps > 0 ? steps : -steps;
	struct comp_output *out = toplevel_tile_output(f);
	for (int c = 0; c < nabs; c++)
	{
		size_t n_full = 0;
		struct comp_toplevel **full = tile_sorted_views(server, &n_full);
		if (!full)
		{
			return;
		}
		size_t n = 0;
		struct comp_toplevel **sorted = tile_sorted_views_on_output(server, out, full, n_full, &n);
		free(full);
		if (!sorted || n < 2)
		{
			free(sorted);
			return;
		}
		int cols, rows;
		tile_grid_dims(n, &cols, &rows);
		(void)rows;
		const int i = tile_sorted_index(sorted, n, f);
		if (i < 0)
		{
			free(sorted);
			return;
		}
		const int j = i + sig;
		if (j < 0 || j >= (int)n)
		{
			free(sorted);
			return;
		}
		if (j / cols != i / cols)
		{
			free(sorted);
			return;
		}
		tile_swap_sort_keys(f, sorted[(size_t)j]);
		free(sorted);
	}
	server_arrange_toplevels(server);
}

/** Parse and execute textual tile-grid movement command forms. */
void server_tile_grid_run_command(struct comp_server *server, const char *cmd_in)
{
	char buf[256];
	if (!cmd_in)
	{
		return;
	}
	strncpy(buf, cmd_in, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	char *s = buf;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
	{
		s++;
	}
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
	{
		end--;
	}
	*end = '\0';
	if (!s[0])
	{
		return;
	}

	if (!strcasecmp(s, "up"))
	{
		server_tile_move_focused_grid_vert(server, -1);
		return;
	}
	if (!strcasecmp(s, "down"))
	{
		server_tile_move_focused_grid_vert(server, 1);
		return;
	}
	if (!strcasecmp(s, "left"))
	{
		server_tile_move_focused_grid_horiz(server, -1);
		return;
	}
	if (!strcasecmp(s, "right"))
	{
		server_tile_move_focused_grid_horiz(server, 1);
		return;
	}
	if (!strcasecmp(s, "top"))
	{
		server_tile_move_focused_grid_vert_edge(server, true);
		return;
	}
	if (!strcasecmp(s, "bottom"))
	{
		server_tile_move_focused_grid_vert_edge(server, false);
		return;
	}

	char dir[32];
	int steps = 0;
	const int n = sscanf(s, "%31s %d", dir, &steps);
	if (n == 2 && steps > 0)
	{
		if (!strcasecmp(dir, "left"))
		{
			server_tile_move_focused_grid_horiz(server, -steps);
		}
		else if (!strcasecmp(dir, "right"))
		{
			server_tile_move_focused_grid_horiz(server, steps);
		}
		else if (!strcasecmp(dir, "up"))
		{
			server_tile_move_focused_grid_vert(server, -steps);
		}
		else if (!strcasecmp(dir, "down"))
		{
			server_tile_move_focused_grid_vert(server, steps);
		}
		else
		{
			wlr_log(WLR_INFO, "tile grid: unknown direction '%s'", dir);
		}
		return;
	}
	if (n == 2 && steps <= 0)
	{
		wlr_log(WLR_INFO, "tile grid: count after direction must be ≥1");
		return;
	}

	char *ep = NULL;
	const long v = strtol(s, &ep, 10);
	if (ep != s && (!ep || !*ep))
	{
		server_tile_move_focused_grid_vert(server, (int)v);
		return;
	}
	wlr_log(WLR_INFO, "tile grid: unknown '%s'", s);
}

/** Switch compositor layout and perform required per-layout state transitions. */
void server_set_layout(struct comp_server *server, enum comp_layout layout)
{
	if (server->layout == layout)
	{
		return;
	}
	server->layout = layout;
	if (layout == COMP_LAYOUT_TILE || layout == COMP_LAYOUT_SCROLL)
	{
		server_refresh_all_tile_props(server);
		if (layout == COMP_LAYOUT_SCROLL)
		{
			struct comp_output *o;
			wl_list_for_each(o, &server->outputs, link)
			{
				o->workspace_scroll_slot[server->current_workspace] = 0;
			}
			scroll_sync_to_focused(server);
		}
		server_arrange_toplevels(server);
	}
	else
	{
		struct comp_toplevel *v;
		wl_list_for_each(v, &server->toplevels, link)
		{
			v->layout_anim_tracked = false;
		}
		server->layout_anim_last_ns = 0;
		if (server->focused_toplevel)
		{
			wlr_scene_node_raise_to_top(&server->focused_toplevel->scene_tree->node);
		}
	}
	server_sync_xdg_decorations(server);
	comp_config_sync_shell_env(server);
}

/** Toggle stack/tile; scroll toggles back to stack for deterministic cycling. */
void server_toggle_layout(struct comp_server *server)
{
	if (server->layout == COMP_LAYOUT_SCROLL)
	{
		server_set_layout(server, COMP_LAYOUT_STACK);
		return;
	}
	server_set_layout(server, server->layout == COMP_LAYOUT_TILE
								  ? COMP_LAYOUT_STACK
								  : COMP_LAYOUT_TILE);
}

/** Build the Unix socket path for compositor IPC under XDG_RUNTIME_DIR. */
static bool ipc_socket_path(char *out, size_t out_sz)
{
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (!rt || !rt[0])
	{
		return false;
	}
	const int n = snprintf(out, out_sz, "%s/morph-ipc.sock", rt);
	if (n < 0 || (size_t)n >= out_sz)
	{
		return false;
	}
	return true;
}

/* Returns 0 if the line was delivered to a listening morph, -1 otherwise. */
static int ipc_client_send_line(const char *line)
{
	char path[108];
	if (!ipc_socket_path(path, sizeof(path)))
	{
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
	{
		return -1;
	}
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	const size_t path_len = strlen(path);
	if (path_len >= sizeof(addr.sun_path))
	{
		close(fd);
		return -1;
	}
	memcpy(addr.sun_path, path, path_len + 1);
	socklen_t slen = (socklen_t)offsetof(struct sockaddr_un, sun_path) +
					 (socklen_t)path_len;
	if (connect(fd, (struct sockaddr *)&addr, slen) < 0)
	{
		close(fd);
		return -1;
	}
	const size_t len = strlen(line);
	if (write(fd, line, len) != (ssize_t)len)
	{
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/** Parse and execute one inbound IPC command line. */
static void ipc_process_line(struct comp_server *server, char *line)
{
	/* Accept commands with leading whitespace for shell/script friendliness. */
	while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
	{
		line++;
	}
	if (!line[0])
	{
		return;
	}
	char *end = line + strlen(line);
	while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
	{
		end--;
	}
	*end = '\0';

	if (!strncmp(line, "layout ", 7))
	{
		/* Layout changes are runtime operations; no config reload required. */
		const char *rest = line + 7;
		while (*rest == ' ' || *rest == '\t')
		{
			rest++;
		}
		if (!strcmp(rest, "toggle"))
		{
			server_toggle_layout(server);
		}
		else if (!strcmp(rest, "tile"))
		{
			server_set_layout(server, COMP_LAYOUT_TILE);
		}
		else if (!strcmp(rest, "scroll"))
		{
			server_set_layout(server, COMP_LAYOUT_SCROLL);
		}
		else if (!strcmp(rest, "stack"))
		{
			server_set_layout(server, COMP_LAYOUT_STACK);
		}
		else
		{
			wlr_log(WLR_INFO, "ipc: unknown layout subcommand '%s'", rest);
		}
		return;
	}
	if (!strncmp(line, "workspace ", 10))
	{
		const char *rest = line + 10;
		while (*rest == ' ' || *rest == '\t')
		{
			rest++;
		}
		if (!strncmp(rest, "move ", 5))
		{
			rest += 5;
			while (*rest == ' ' || *rest == '\t')
			{
				rest++;
			}
			char *end = NULL;
			const long w = strtol(rest, &end, 10);
			if (end != rest && (!end || !*end) && w >= 1 && w <= COMP_WORKSPACE_COUNT)
			{
				server_workspace_move_focused(server, (int)w - 1);
			}
			else
			{
				wlr_log(WLR_INFO, "ipc: workspace move needs 1..%d", COMP_WORKSPACE_COUNT);
			}
			return;
		}
		if (!strcmp(rest, "next"))
		{
			server_workspace_relative(server, 1);
			return;
		}
		if (!strcmp(rest, "prev"))
		{
			server_workspace_relative(server, -1);
			return;
		}
		char *end = NULL;
		const long v = strtol(rest, &end, 10);
		if (end != rest && (!end || !*end) && v >= 1 && v <= COMP_WORKSPACE_COUNT)
		{
			server_workspace_go(server, (int)v - 1);
		}
		else
		{
			wlr_log(WLR_INFO, "ipc: unknown workspace '%s' (use 1..%d, next, prev, move N)", rest,
					COMP_WORKSPACE_COUNT);
		}
		return;
	}
	if (!strncmp(line, "tile move ", 10))
	{
		const char *rest = line + 10;
		while (*rest == ' ' || *rest == '\t')
		{
			rest++;
		}
		if (!strcmp(rest, "prev"))
		{
			server_tile_move_focused_n(server, -1);
		}
		else if (!strcmp(rest, "next"))
		{
			server_tile_move_focused_n(server, 1);
		}
		else if (!strcmp(rest, "first"))
		{
			server_tile_move_focused_edge(server, true);
		}
		else if (!strcmp(rest, "last"))
		{
			server_tile_move_focused_edge(server, false);
		}
		else
		{
			char *end = NULL;
			long v = strtol(rest, &end, 10);
			if (end != rest && (!end || !*end))
			{
				server_tile_move_focused_n(server, (int)v);
			}
			else
			{
				wlr_log(WLR_INFO, "ipc: unknown tile move '%s'", rest);
			}
		}
		return;
	}
	if (!strncmp(line, "tile grid ", 10))
	{
		const char *rest = line + 10;
		while (*rest == ' ' || *rest == '\t')
		{
			rest++;
		}
		server_tile_grid_run_command(server, rest);
		return;
	}
	if (!strncmp(line, "scroll ", 7))
	{
		const char *rest = line + 7;
		while (*rest == ' ' || *rest == '\t')
		{
			rest++;
		}
		if (!strncmp(rest, "move ", 5))
		{
			rest += 5;
			while (*rest == ' ' || *rest == '\t')
			{
				rest++;
			}
		}
		if (!strcmp(rest, "prev") || !strcmp(rest, "left"))
		{
			server_scroll_move(server, -1);
		}
		else if (!strcmp(rest, "next") || !strcmp(rest, "right"))
		{
			server_scroll_move(server, 1);
		}
		else
		{
			char *end = NULL;
			long v = strtol(rest, &end, 10);
			if (end != rest && (!end || !*end))
			{
				server_scroll_move(server, (int)v);
			}
			else
			{
				wlr_log(WLR_INFO, "ipc: unknown scroll '%s'", rest);
			}
		}
		return;
	}
	if (!strcasecmp(line, "reload config") || !strcasecmp(line, "reload"))
	{
		if (!server_reload_config(server))
		{
			wlr_log(WLR_ERROR, "ipc: config reload failed");
		}
		return;
	}
	if (!strncmp(line, "focus ", 6))
	{
		const char *rest = line + 6;
		while (*rest == ' ' || *rest == '\t')
		{
			rest++;
		}
		enum comp_focus_policy pol;
		if (!comp_config_parse_focus_policy(rest, &pol))
		{
			wlr_log(WLR_INFO,
					"ipc: unknown focus '%s' (use ClickToFocus, FocusFollowsMouse, or SloppyFocus)", rest);
			return;
		}
		if (!server->config)
		{
			wlr_log(WLR_ERROR, "ipc: focus set failed (no config)");
			return;
		}
		server->config->focus_policy = pol;
		wlr_log(WLR_INFO, "ipc: focus policy set to %s", comp_config_focus_policy_name(pol));
		return;
	}
	wlr_log(WLR_INFO, "ipc: unknown command '%s'", line);
}

/** Reload config from remembered/default path and apply runtime updates atomically. */
static bool server_reload_config(struct comp_server *server)
{
	const char *path = server->config_path;
	char fallback[PATH_MAX];
	if (!path || !path[0])
	{
		if (comp_config_default_path(fallback, sizeof(fallback)))
		{
			path = fallback;
		}
		else if (!comp_config_builtin_fallback_enabled())
		{
			wlr_log(WLR_ERROR, "reload: no config path and no default config file");
			return false;
		}
		else
		{
			wlr_log(WLR_INFO, "reload: no config file resolved; using built-in defaults");
		}
	}
	struct comp_config *new_cfg = NULL;
	if (!comp_config_load(path, &new_cfg))
	{
		return false;
	}
	struct comp_config *old = server->config;
	server->config = new_cfg;
	comp_config_free(old);
	if (server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL)
	{
		server_refresh_all_tile_props(server);
		server_arrange_toplevels(server);
	}
	server_sync_xdg_decorations(server);
	comp_config_sync_shell_env(server);
	comp_config_run_reload(server->config);
	server_apply_input_device_maps(server);
	wlr_log(WLR_INFO, "reload: loaded config from %s", path && path[0] ? path : "<built-in defaults>");
	return true;
}

/** Event-loop callback for accepted IPC client sockets. */
static int ipc_on_listen(int fd, uint32_t mask, void *data)
{
	(void)mask;
	struct comp_server *server = data;
	int c = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
	if (c < 0)
	{
		if (errno != EAGAIN && errno != EWOULDBLOCK)
		{
			wlr_log_errno(WLR_ERROR, "ipc accept");
		}
		return 0;
	}
	char buf[512];
	ssize_t n = read(c, buf, sizeof(buf) - 1);
	close(c);
	if (n <= 0)
	{
		return 0;
	}
	buf[n] = '\0';
	ipc_process_line(server, buf);
	return 0;
}

/** Create, bind, and register the compositor IPC socket in the Wayland event loop. */
static bool ipc_init(struct comp_server *server)
{
	if (!ipc_socket_path(server->ipc_socket_path, sizeof(server->ipc_socket_path)))
	{
		wlr_log(WLR_ERROR, "ipc: XDG_RUNTIME_DIR is not set or path too long");
		server->ipc_socket_path[0] = '\0';
		return false;
	}
	unlink(server->ipc_socket_path);

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
	{
		wlr_log_errno(WLR_ERROR, "ipc: socket");
		return false;
	}
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	const size_t socket_path_len = strlen(server->ipc_socket_path);
	if (socket_path_len >= sizeof(addr.sun_path))
	{
		wlr_log(WLR_ERROR, "ipc: socket path is too long");
		close(fd);
		server->ipc_socket_path[0] = '\0';
		return false;
	}
	memcpy(addr.sun_path, server->ipc_socket_path, socket_path_len + 1);
	socklen_t slen = (socklen_t)offsetof(struct sockaddr_un, sun_path) + (socklen_t)socket_path_len;
	if (bind(fd, (struct sockaddr *)&addr, slen) < 0)
	{
		wlr_log_errno(WLR_ERROR, "ipc: bind %s", server->ipc_socket_path);
		close(fd);
		server->ipc_socket_path[0] = '\0';
		return false;
	}
	if (listen(fd, 8) < 0)
	{
		wlr_log_errno(WLR_ERROR, "ipc: listen");
		close(fd);
		unlink(server->ipc_socket_path);
		server->ipc_socket_path[0] = '\0';
		return false;
	}
	server->ipc_listen_fd = fd;
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	server->ipc_event_source = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, ipc_on_listen, server);
	if (!server->ipc_event_source)
	{
		wlr_log(WLR_ERROR, "ipc: wl_event_loop_add_fd failed");
		close(fd);
		unlink(server->ipc_socket_path);
		server->ipc_socket_path[0] = '\0';
		server->ipc_listen_fd = -1;
		return false;
	}
	wlr_log(WLR_INFO, "ipc: listening on %s", server->ipc_socket_path);
	return true;
}

/** Tear down IPC socket/event source on compositor shutdown. */
static void ipc_fini(struct comp_server *server)
{
	if (server->ipc_event_source)
	{
		wl_event_source_remove(server->ipc_event_source);
		server->ipc_event_source = NULL;
	}
	if (server->ipc_listen_fd >= 0)
	{
		close(server->ipc_listen_fd);
		server->ipc_listen_fd = -1;
	}
	if (server->ipc_socket_path[0])
	{
		unlink(server->ipc_socket_path);
		server->ipc_socket_path[0] = '\0';
	}
}

/** Keyboard wrapper destroy callback: remove listeners and free state. */
static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_keyboard *kbd = wl_container_of(listener, kbd, destroy);
	wl_list_remove(&kbd->destroy.link);
	wl_list_remove(&kbd->key.link);
	wl_list_remove(&kbd->modifiers.link);
	free(kbd);
}

/* wlr_keyboard_notify_key() re-emits keyboard->events.key; ignore nested calls. */
static int keyboard_key_dispatch_depth;
static void keyboard_handle_key(struct wl_listener *listener, void *data);
static void keyboard_handle_modifiers(struct wl_listener *listener, void *data);

void server_keyboard_register(struct comp_server *server, struct wlr_keyboard *wlr_kbd)
{
	struct wlr_input_device *dev = &wlr_kbd->base;
	struct comp_keyboard *kbd = calloc(1, sizeof(*kbd));
	if (!kbd)
	{
		wlr_log(WLR_ERROR, "Out of memory allocating keyboard state");
		return;
	}
	kbd->server = server;
	kbd->dev = dev;

	kbd->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&dev->events.destroy, &kbd->destroy);
	kbd->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_kbd->events.key, &kbd->key);
	kbd->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_kbd->events.modifiers, &kbd->modifiers);

	wlr_seat_set_keyboard(server->seat, wlr_kbd);
}

/**
 * Keyboard key callback.
 *
 * Resolves keysym/modifier state for binding matching and forwards events to
 * focused clients. A recursion guard avoids re-entry via wlroots callbacks.
 */
static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
	if (keyboard_key_dispatch_depth > 0)
	{
		return;
	}
	keyboard_key_dispatch_depth++;

	struct comp_keyboard *kbd = wl_container_of(listener, kbd, key);
	/* During shutdown wlroots may emit late key events; avoid seat access then. */
	if (!kbd->server || !kbd->server->seat || kbd->server->display_terminate_requested)
	{
		keyboard_key_dispatch_depth--;
		return;
	}
	struct wlr_keyboard_key_event *event = data;
	struct wlr_keyboard *wlr_kbd = wlr_keyboard_from_input_device(kbd->dev);

	uint32_t mods_filtered = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;
	if (wlr_kbd->xkb_state && wlr_kbd->keymap)
	{
		mods_filtered = wlr_keyboard_get_modifiers(wlr_kbd) & COMP_BIND_MOD_FILTER;
		/* wlroots uses Linux evdev codes; XKB uses evdev + 8. Never call xkb with a
		 * keycode outside this keymap's range (libxkbcommon can fault otherwise). */
		xkb_keycode_t xkbc = (xkb_keycode_t)((uint64_t)event->keycode + 8u);
		xkb_keycode_t lo = xkb_keymap_min_keycode(wlr_kbd->keymap);
		xkb_keycode_t hi = xkb_keymap_max_keycode(wlr_kbd->keymap);
		if (xkbc >= lo && xkbc <= hi)
		{
			sym = xkb_state_key_get_one_sym(wlr_kbd->xkb_state, xkbc);
		}
	}

	const bool pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
	if (pressed)
	{
		const char *kdbg = getenv("MORPH_LOG_KEYS");
		if (kdbg && kdbg[0] && strcmp(kdbg, "0") != 0 && sym != XKB_KEY_NoSymbol)
		{
			char name[128];
			if (xkb_keysym_get_name(sym, name, sizeof(name)) < 0)
			{
				snprintf(name, sizeof(name), "(bad)");
			}
			wlr_log(WLR_INFO, "keyboard: keysym=%s mods=0x%x", name, mods_filtered);
		}
	}

	wlr_seat_set_keyboard(kbd->server->seat, wlr_kbd);
	wlr_keyboard_notify_key(wlr_kbd, event);
	if (pressed && comp_config_try_bindings(kbd->server->config, kbd->server, pressed, mods_filtered, sym))
	{
		if (mods_filtered & WLR_MODIFIER_LOGO)
		{
			kbd->server->suppress_logo_pointer_drag = true;
		}
		keyboard_key_dispatch_depth--;
		return;
	}
	wlr_seat_keyboard_notify_key(kbd->server->seat, event->time_msec, event->keycode, event->state);
	keyboard_key_dispatch_depth--;
}

/** Keyboard modifiers callback: forward effective modifiers to seat. */
static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_keyboard *kbd = wl_container_of(listener, kbd, modifiers);
	if (!kbd->server || !kbd->server->seat || kbd->server->display_terminate_requested)
	{
		return;
	}
	struct wlr_keyboard *wlr_kbd = wlr_keyboard_from_input_device(kbd->dev);
	wlr_seat_set_keyboard(kbd->server->seat, wlr_kbd);
	wlr_seat_keyboard_notify_modifiers(kbd->server->seat, &wlr_kbd->modifiers);
	if ((wlr_kbd->modifiers.depressed & WLR_MODIFIER_LOGO) == 0)
	{
		kbd->server->suppress_logo_pointer_drag = false;
	}
}

/** Resolve configured output name to current wlroots output object. */
static struct wlr_output *output_by_name(struct comp_server *server, const char *name)
{
	if (!name || !name[0])
	{
		return NULL;
	}
	struct comp_output *o;
	wl_list_for_each(o, &server->outputs, link)
	{
		if (strcmp(o->wlr_output->name, name) == 0)
		{
			return o->wlr_output;
		}
	}
	return NULL;
}

/** Apply configured input-to-output mapping rules to all tracked non-keyboard devices. */
void server_apply_input_device_maps(struct comp_server *server)
{
	if (!server->cursor || !server->config)
	{
		return;
	}
	struct comp_tracked_input *ti;
	wl_list_for_each(ti, &server->tracked_inputs, link)
	{
		struct wlr_input_device *dev = ti->dev;
		uint32_t want = 0;
		switch (dev->type)
		{
		case WLR_INPUT_DEVICE_TOUCH:
			want = COMP_INPUT_MAP_TYPE_TOUCH;
			break;
		case WLR_INPUT_DEVICE_TABLET:
			want = COMP_INPUT_MAP_TYPE_TABLET;
			break;
		case WLR_INPUT_DEVICE_POINTER:
			want = COMP_INPUT_MAP_TYPE_POINTER;
			break;
		default:
			want = 0;
			break;
		}
		if (!want)
		{
			continue;
		}
		struct wlr_output *mapped = NULL;
		for (size_t i = 0; i < server->config->n_input_map_rules; i++)
		{
			const struct comp_input_map_rule *r = &server->config->input_map_rules[i];
			if (!(r->types & want) || !r->have_name)
			{
				continue;
			}
			if (regexec(&r->name_re, dev->name, 0, NULL, 0) != 0)
			{
				continue;
			}
			mapped = output_by_name(server, r->output_name);
			if (!mapped)
			{
				wlr_log(WLR_INFO, "input_map: output '%s' not found for device '%s' (trying next rule)",
						r->output_name, dev->name);
				continue;
			}
			break;
		}
		wlr_cursor_map_input_to_output(server->cursor, dev, mapped);
		if (mapped)
		{
			wlr_log(WLR_INFO, "input_map: device '%s' -> output '%s'", dev->name, mapped->name);
		}
	}
}

/** Track device lifetime so hot-unplug and remap updates stay consistent. */
static void tracked_input_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_tracked_input *ti = wl_container_of(listener, ti, destroy);
	wl_list_remove(&ti->destroy.link);
	wl_list_remove(&ti->link);
	server_update_seat_capabilities(ti->server);
	server_apply_input_device_maps(ti->server);
	free(ti);
}

/** Register one device for lifecycle + mapping updates. */
static void track_input_device(struct comp_server *server, struct wlr_input_device *dev)
{
	struct comp_tracked_input *ti = calloc(1, sizeof(*ti));
	if (!ti)
	{
		wlr_log(WLR_ERROR, "Out of memory allocating tracked input");
		return;
	}
	ti->server = server;
	ti->dev = dev;
	ti->destroy.notify = tracked_input_destroy;
	wl_signal_add(&dev->events.destroy, &ti->destroy);
	wl_list_insert(&server->tracked_inputs, &ti->link);
	server_update_seat_capabilities(server);
	server_apply_input_device_maps(server);
}

/** Drop tracked-input wrappers before wlroots object destruction to avoid late destroy callbacks. */
static void server_clear_tracked_inputs(struct comp_server *server)
{
	if (!server)
	{
		return;
	}
	struct comp_tracked_input *ti, *tmp;
	wl_list_for_each_safe(ti, tmp, &server->tracked_inputs, link)
	{
		detach_listener_if_linked(&ti->destroy);
		wl_list_remove(&ti->link);
		free(ti);
	}
}

/** Recompute wl_seat capability bitset from currently tracked devices. */
void server_update_seat_capabilities(struct comp_server *server)
{
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
	struct comp_tracked_input *ti;
	wl_list_for_each(ti, &server->tracked_inputs, link)
	{
		if (ti->dev->type == WLR_INPUT_DEVICE_TOUCH)
		{
			caps |= WL_SEAT_CAPABILITY_TOUCH;
			break;
		}
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

/** Resolve compositor tablet wrapper from wlroots tablet pointer. */
static struct comp_tablet *comp_tablet_from_wlr(struct comp_server *server, struct wlr_tablet *wt)
{
	struct comp_tablet *t;
	wl_list_for_each(t, &server->tablets, link)
	{
		if (t->wlr_tablet == wt)
		{
			return t;
		}
	}
	return NULL;
}

/** Tablet wrapper destroy callback: detach list/listener and free state. */
static void comp_tablet_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_tablet *tab = wl_container_of(listener, tab, destroy);
	wl_list_remove(&tab->destroy.link);
	wl_list_remove(&tab->link);
	free(tab);
}

/** Tablet-v2 set_cursor callback: route cursor surface updates to compositor cursor. */
static void tablet_tool_set_cursor(struct wl_listener *listener, void *data)
{
	struct comp_tablet_tool *tt = wl_container_of(listener, tt, set_cursor);
	struct wlr_tablet_v2_event_cursor *ev = data;
	struct comp_server *server = tt->tablet->server;
	wlr_cursor_set_surface(server->cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
}

/** Tablet tool destroy callback: detach listeners and clear back-pointer on wlr tool. */
static void tablet_tool_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_tablet_tool *tt = wl_container_of(listener, tt, destroy);
	wl_list_remove(&tt->destroy.link);
	wl_list_remove(&tt->set_cursor.link);
	tt->wlr_tool->data = NULL;
	free(tt);
}

/** Return existing tablet-tool wrapper from `wtool->data` or create and wire a new one. */
static struct comp_tablet_tool *tablet_tool_get_or_create(struct comp_server *srv, struct comp_tablet *tab,
														  struct wlr_tablet_tool *wtool)
{
	if (!srv || !srv->seat || srv->display_terminate_requested)
	{
		return NULL;
	}
	if (wtool->data)
	{
		return wtool->data;
	}
	struct comp_tablet_tool *tt = calloc(1, sizeof(*tt));
	if (!tt)
	{
		return NULL;
	}
	tt->wlr_tool = wtool;
	tt->tablet = tab;
	tt->v2_tool = wlr_tablet_tool_create(srv->tablet_manager, srv->seat, wtool);
	if (!tt->v2_tool)
	{
		free(tt);
		return NULL;
	}
	tt->destroy.notify = tablet_tool_handle_destroy;
	wl_signal_add(&wtool->events.destroy, &tt->destroy);
	tt->set_cursor.notify = tablet_tool_set_cursor;
	wl_signal_add(&tt->v2_tool->events.set_cursor, &tt->set_cursor);
	wtool->data = tt;
	return tt;
}

/**
 * Apply absolute tablet position and route motion.
 *
 * When no tablet-v2 target surface is eligible, fall back to compositor
 * pointer motion so hover feedback still works.
 */
static void tablet_tool_position(struct comp_server *server, struct comp_tablet_tool *tt, bool change_x,
								 bool change_y, double x, double y, uint32_t time_msec)
{
	if (!server || !server->seat || server->display_terminate_requested)
	{
		return;
	}
	if (!change_x && !change_y)
	{
		return;
	}
	struct wlr_input_device *tab_dev = &tt->tablet->wlr_tablet->base;
	wlr_cursor_warp_absolute(server->cursor, tab_dev, change_x ? x : NAN, change_y ? y : NAN);

	double sx, sy;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	struct wlr_tablet_v2_tablet *v2tab = tt->tablet->v2_tablet;

	if (surface && (wlr_surface_accepts_tablet_v2(surface, v2tab) ||
					wlr_tablet_tool_v2_has_implicit_grab(tt->v2_tool)))
	{
		wlr_tablet_v2_tablet_tool_notify_proximity_in(tt->v2_tool, v2tab, surface);
		wlr_tablet_v2_tablet_tool_notify_motion(tt->v2_tool, sx, sy);
	}
	else
	{
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tt->v2_tool);
		process_cursor_motion(server, time_msec);
		wlr_seat_pointer_notify_frame(server->seat);
	}
}

/** Tablet axis callback: update position and forward all changed tool axes/states. */
static void server_cursor_tablet_tool_axis(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_tablet_tool_axis);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_tablet_tool_axis_event *event = data;
	struct comp_tablet *tab = comp_tablet_from_wlr(server, event->tablet);
	if (!tab)
	{
		return;
	}
	struct comp_tablet_tool *tt = tablet_tool_get_or_create(server, tab, event->tool);
	if (!tt)
	{
		return;
	}

	tablet_tool_position(server, tt, (bool)(event->updated_axes & WLR_TABLET_TOOL_AXIS_X),
						 (bool)(event->updated_axes & WLR_TABLET_TOOL_AXIS_Y), event->x, event->y, event->time_msec);

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
	{
		wlr_tablet_v2_tablet_tool_notify_pressure(tt->v2_tool, event->pressure);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
	{
		wlr_tablet_v2_tablet_tool_notify_distance(tt->v2_tool, event->distance);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X)
	{
		tt->tilt_x = event->tilt_x;
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
	{
		tt->tilt_y = event->tilt_y;
	}
	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
	{
		wlr_tablet_v2_tablet_tool_notify_tilt(tt->v2_tool, tt->tilt_x, tt->tilt_y);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION)
	{
		wlr_tablet_v2_tablet_tool_notify_rotation(tt->v2_tool, event->rotation);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER)
	{
		wlr_tablet_v2_tablet_tool_notify_slider(tt->v2_tool, event->slider);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL)
	{
		wlr_tablet_v2_tablet_tool_notify_wheel(tt->v2_tool, event->wheel_delta, 0);
	}
}

/** Tablet proximity callback: enter/leave proximity and update absolute position. */
static void server_cursor_tablet_tool_proximity(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_tablet_tool_proximity);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_tablet_tool_proximity_event *event = data;
	struct comp_tablet *tab = comp_tablet_from_wlr(server, event->tablet);
	if (!tab)
	{
		return;
	}
	struct comp_tablet_tool *tt = tablet_tool_get_or_create(server, tab, event->tool);
	if (!tt)
	{
		return;
	}
	if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT)
	{
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tt->v2_tool);
		return;
	}
	tablet_tool_position(server, tt, true, true, event->x, event->y, event->time_msec);
}

/** Tablet tip callback: deliver tablet events or fallback pointer emulation path. */
static void server_cursor_tablet_tool_tip(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_tablet_tool_tip);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_tablet_tool_tip_event *event = data;
	struct comp_tablet *tab = comp_tablet_from_wlr(server, event->tablet);
	if (!tab)
	{
		return;
	}
	struct comp_tablet_tool *tt = tablet_tool_get_or_create(server, tab, event->tool);
	if (!tt)
	{
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	struct wlr_tablet_v2_tablet *v2tab = tab->v2_tablet;

	if (event->state == WLR_TABLET_TOOL_TIP_UP)
	{
		if (tt->emulating_pointer_from_tip)
		{
			tt->emulating_pointer_from_tip = false;
			wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
										   WL_POINTER_BUTTON_STATE_RELEASED);
			wlr_seat_pointer_notify_frame(server->seat);
		}
		else
		{
			wlr_tablet_v2_tablet_tool_notify_up(tt->v2_tool);
		}
		return;
	}

	/* TIP_DOWN */
	if (!surface || !wlr_surface_accepts_tablet_v2(surface, v2tab))
	{
		tt->emulating_pointer_from_tip = true;
		process_cursor_motion(server, event->time_msec);
		struct comp_toplevel *v = toplevel_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (v)
		{
			focus_toplevel(server, v);
		}
		else
		{
			layer_surface_try_keyboard_focus_click(server, server->cursor->x, server->cursor->y);
		}
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
									   WL_POINTER_BUTTON_STATE_PRESSED);
		wlr_seat_pointer_notify_frame(server->seat);
		return;
	}

	wlr_tablet_v2_tablet_tool_notify_proximity_in(tt->v2_tool, v2tab, surface);
	wlr_tablet_v2_tablet_tool_notify_motion(tt->v2_tool, sx, sy);
	wlr_tablet_v2_tablet_tool_notify_down(tt->v2_tool);
	struct comp_toplevel *v = toplevel_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (v)
	{
		focus_toplevel(server, v);
	}
	else
	{
		layer_surface_try_keyboard_focus_click(server, server->cursor->x, server->cursor->y);
	}
}

/** Tablet button callback: forward to tablet-v2 or pointer fallback when needed. */
static void server_cursor_tablet_tool_button(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_tablet_tool_button);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_tablet_tool_button_event *event = data;
	struct comp_tablet *tab = comp_tablet_from_wlr(server, event->tablet);
	if (!tab)
	{
		return;
	}
	struct comp_tablet_tool *tt = tablet_tool_get_or_create(server, tab, event->tool);
	if (!tt)
	{
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	struct wlr_tablet_v2_tablet *v2tab = tab->v2_tablet;
	if (!surface || !wlr_surface_accepts_tablet_v2(surface, v2tab))
	{
		uint32_t btn = BTN_RIGHT;
		enum wl_pointer_button_state st = event->state == WLR_BUTTON_PRESSED
											  ? WL_POINTER_BUTTON_STATE_PRESSED
											  : WL_POINTER_BUTTON_STATE_RELEASED;
		process_cursor_motion(server, event->time_msec);
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, btn, st);
		wlr_seat_pointer_notify_frame(server->seat);
		return;
	}
	wlr_tablet_v2_tablet_tool_notify_button(tt->v2_tool, event->button,
											(enum zwp_tablet_pad_v2_button_state)event->state);
}

/** Touch-down callback: prefer native touch delivery, fallback to pointer emulation. */
static void server_cursor_touch_down(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_touch_down);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_touch_down_event *event = data;
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = surface_at(server, lx, ly, &sx, &sy);

	if (surface && wlr_surface_accepts_touch(surface, server->seat))
	{
		server->touch_pointer_emu = false;
		wlr_seat_touch_notify_down(server->seat, surface, event->time_msec, event->touch_id, sx, sy);
		struct comp_toplevel *v = toplevel_at(server, lx, ly, &sx, &sy);
		if (v)
		{
			focus_toplevel(server, v);
		}
		else
		{
			layer_surface_try_keyboard_focus_click(server, lx, ly);
		}
		return;
	}

	wlr_cursor_warp_closest(server->cursor, &event->touch->base, lx, ly);
	process_cursor_motion(server, event->time_msec);
	struct comp_toplevel *v = toplevel_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (v)
	{
		focus_toplevel(server, v);
	}
	else
	{
		layer_surface_try_keyboard_focus_click(server, server->cursor->x, server->cursor->y);
	}
	if (server->grab == COMP_GRAB_NONE)
	{
		server->touch_pointer_emu = true;
		server->touch_pointer_emu_id = event->touch_id;
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
									   WL_POINTER_BUTTON_STATE_PRESSED);
		wlr_seat_pointer_notify_frame(server->seat);
	}
}

/** Touch-up callback: release emulated pointer press or forward touch up. */
static void server_cursor_touch_up(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_touch_up);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_touch_up_event *event = data;
	if (server->touch_pointer_emu && event->touch_id == server->touch_pointer_emu_id)
	{
		server->touch_pointer_emu = false;
		if (server->grab == COMP_GRAB_NONE)
		{
			wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
										   WL_POINTER_BUTTON_STATE_RELEASED);
			wlr_seat_pointer_notify_frame(server->seat);
		}
		return;
	}
	wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
}

/** Touch-motion callback: route to touch surface or pointer emulation path. */
static void server_cursor_touch_motion(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_touch_motion);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_touch_motion_event *event = data;
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y, &lx, &ly);
	double sx, sy;
	if (server->touch_pointer_emu && event->touch_id == server->touch_pointer_emu_id)
	{
		wlr_cursor_warp_closest(server->cursor, &event->touch->base, lx, ly);
		process_cursor_motion(server, event->time_msec);
		return;
	}
	struct wlr_surface *surface = surface_at(server, lx, ly, &sx, &sy);
	if (surface && wlr_surface_accepts_touch(surface, server->seat))
	{
		wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id, sx, sy);
	}
	else
	{
		wlr_cursor_warp_closest(server->cursor, &event->touch->base, lx, ly);
		process_cursor_motion(server, event->time_msec);
	}
}

/** Touch-cancel callback: cancel emulation or notify touch client cancellation. */
static void server_cursor_touch_cancel(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_server *server = wl_container_of(listener, server, cursor_touch_cancel);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_touch_cancel_event *event = data;
	if (server->touch_pointer_emu && event->touch_id == server->touch_pointer_emu_id)
	{
		server->touch_pointer_emu = false;
		if (server->grab == COMP_GRAB_NONE)
		{
			wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
										   WL_POINTER_BUTTON_STATE_RELEASED);
			wlr_seat_pointer_notify_frame(server->seat);
		}
		return;
	}
	struct wlr_touch_point *pt = wlr_seat_touch_get_point(server->seat, event->touch_id);
	if (pt && pt->client)
	{
		wlr_seat_touch_notify_cancel(server->seat, pt->client);
	}
}

/** Touch-frame callback: flush touch event batch to seat. */
static void server_cursor_touch_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_server *server = wl_container_of(listener, server, cursor_touch_frame);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	wlr_seat_touch_notify_frame(server->seat);
}

/**
 * Apply Morph's default libinput touchpad policy to a newly discovered device.
 *
 * Tap-to-click is handled entirely by libinput, so wlroots only exposes it when
 * the compositor opts in on each touchpad. Restricting this to libinput-backed
 * touchpads keeps mice, trackpoints, tablets, and other devices on their native
 * behavior.
 */
static void input_device_apply_libinput_defaults(struct wlr_input_device *dev)
{
	if (!dev || !wlr_input_device_is_libinput(dev))
	{
		return;
	}
	struct libinput_device *lid = wlr_libinput_get_device_handle(dev);
	if (!lid || libinput_device_config_tap_get_finger_count(lid) <= 0)
	{
		return;
	}
	/* A device may expose libinput without supporting tap; log and leave it untouched in that case. */
	const enum libinput_config_status tap_status =
		libinput_device_config_tap_set_enabled(lid, LIBINPUT_CONFIG_TAP_ENABLED);
	if (tap_status != LIBINPUT_CONFIG_STATUS_SUCCESS)
	{
		wlr_log(WLR_INFO, "libinput: tap-to-click enable unsupported for %s", dev->name);
		return;
	}
	/* Keep libinput's tap drag and left/right/middle mapping aligned with regular button behavior. */
	(void)libinput_device_config_tap_set_drag_enabled(lid, LIBINPUT_CONFIG_DRAG_ENABLED);
	(void)libinput_device_config_tap_set_button_map(lid, LIBINPUT_CONFIG_TAP_MAP_LRM);
	wlr_log(WLR_INFO, "libinput: enabled tap-to-click for %s", dev->name);
}

/** new_input callback: initialize device-specific handlers and seat integration. */
static void server_new_input(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *dev = data;

	switch (dev->type)
	{
	case WLR_INPUT_DEVICE_KEYBOARD:
	{
		struct wlr_keyboard *wlr_kbd = wlr_keyboard_from_input_device(dev);

		const char *xkb_layout = getenv("XKB_DEFAULT_LAYOUT");
		const char *xkb_model = getenv("XKB_DEFAULT_MODEL");
		const char *xkb_variant = getenv("XKB_DEFAULT_VARIANT");
		const char *xkb_options = getenv("XKB_DEFAULT_OPTIONS");
		if (xkb_layout && !xkb_layout[0])
		{
			xkb_layout = NULL;
		}
		if (xkb_model && !xkb_model[0])
		{
			xkb_model = NULL;
		}
		if (xkb_variant && !xkb_variant[0])
		{
			xkb_variant = NULL;
		}
		if (xkb_options && !xkb_options[0])
		{
			xkb_options = NULL;
		}
		if (xkb_layout || xkb_model || xkb_variant || xkb_options)
		{
			wlr_log(WLR_INFO,
					"XKB rules from environment: layout='%s' model='%s' variant='%s' options='%s'",
					xkb_layout ? xkb_layout : "",
					xkb_model ? xkb_model : "",
					xkb_variant ? xkb_variant : "",
					xkb_options ? xkb_options : "");
		}
		struct xkb_rule_names rules = {
			.layout = xkb_layout,
			.model = xkb_model,
			.variant = xkb_variant,
			.options = xkb_options,
		};

		struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!ctx)
		{
			wlr_log(WLR_ERROR, "Failed to create XKB context");
			exit(1);
		}
		struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (!map)
		{
			wlr_log(WLR_ERROR,
					"Failed to compile XKB keymap (layout='%s' model='%s' variant='%s' options='%s')",
					xkb_layout ? xkb_layout : "",
					xkb_model ? xkb_model : "",
					xkb_variant ? xkb_variant : "",
					xkb_options ? xkb_options : "");
			xkb_context_unref(ctx);
			exit(1);
		}
		wlr_keyboard_set_keymap(wlr_kbd, map);
		xkb_keymap_unref(map);
		xkb_context_unref(ctx);
		wlr_keyboard_set_repeat_info(wlr_kbd, 25, 600);

		server_keyboard_register(server, wlr_kbd);
		server_update_seat_capabilities(server);
		/* Do not wlr_cursor_attach_input_device(keyboard): only pointer/touch/tablet. */
		break;
	}
	case WLR_INPUT_DEVICE_POINTER:
		input_device_apply_libinput_defaults(dev);
		wlr_cursor_attach_input_device(server->cursor, dev);
		track_input_device(server, dev);
		server_update_seat_capabilities(server);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		wlr_cursor_attach_input_device(server->cursor, dev);
		track_input_device(server, dev);
		server_update_seat_capabilities(server);
		break;
	case WLR_INPUT_DEVICE_TABLET:
	{
		struct comp_tablet *tab = calloc(1, sizeof(*tab));
		if (!tab)
		{
			wlr_log(WLR_ERROR, "Out of memory allocating tablet state");
			return;
		}
		tab->server = server;
		tab->dev = dev;
		tab->wlr_tablet = wlr_tablet_from_input_device(dev);
		tab->v2_tablet = wlr_tablet_create(server->tablet_manager, server->seat, dev);
		if (!tab->v2_tablet)
		{
			free(tab);
			wlr_log(WLR_ERROR, "wlr_tablet_create failed for %s", dev->name);
			return;
		}
		tab->destroy.notify = comp_tablet_handle_destroy;
		wl_signal_add(&dev->events.destroy, &tab->destroy);
		wl_list_insert(&server->tablets, &tab->link);
		wlr_cursor_attach_input_device(server->cursor, dev);
		track_input_device(server, dev);
		server_update_seat_capabilities(server);
		wlr_log(WLR_INFO, "tablet: %s", dev->name);
		break;
	}
	default:
		break;
	}
}

/** Translate surface-local coordinates into global compositor layout coordinates. */
static bool surface_local_to_layout(struct comp_server *server, struct wlr_surface *surface,
									double sx, double sy, double *lx, double *ly)
{
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	struct comp_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link)
	{
		if (t->xdg_toplevel && t->xdg_toplevel->base->surface == root)
		{
			const struct wlr_box *geo = &t->xdg_toplevel->base->geometry;
			*lx = (double)t->scene_tree->node.x + (double)geo->x + sx;
			*ly = (double)t->scene_tree->node.y + (double)geo->y + sy;
			return true;
		}
	}
	struct wlr_scene_tree *tree = surface->data;
	if (tree)
	{
		int node_x, node_y;
		wlr_scene_node_coords(&tree->node, &node_x, &node_y);
		*lx = (double)node_x + sx;
		*ly = (double)node_y + sy;
		return true;
	}
	return false;
}

/** Re-evaluate active pointer-constraint region and optionally warp into valid bounds. */
static void pointer_constraint_check_region(struct comp_server *server)
{
	struct wlr_pointer_constraint_v1 *constraint = server->active_pointer_constraint;
	if (!constraint)
	{
		return;
	}
	pixman_region32_t *region = &constraint->region;
	if (server->pointer_confine_requires_warp)
	{
		server->pointer_confine_requires_warp = false;
		double sx, sy;
		if (!surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy))
		{
			return;
		}
		if (!pixman_region32_contains_point(region, (int)floor(sx), (int)floor(sy), NULL))
		{
			int nboxes;
			pixman_box32_t *boxes = pixman_region32_rectangles(region, &nboxes);
			if (nboxes > 0)
			{
				sx = (boxes[0].x1 + boxes[0].x2) / 2.0;
				sy = (boxes[0].y1 + boxes[0].y2) / 2.0;
				double lx, ly;
				if (surface_local_to_layout(server, constraint->surface, sx, sy, &lx, &ly))
				{
					wlr_cursor_warp_closest(server->cursor, NULL, lx, ly);
				}
			}
		}
	}
	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED)
	{
		pixman_region32_copy(&server->pointer_confine, region);
	}
	else
	{
		pixman_region32_clear(&server->pointer_confine);
	}
}

/** Apply pointer-constraint cursor hint (if enabled) as layout-space warp target. */
static void pointer_constraint_warp_hint(struct comp_server *server,
										 struct wlr_pointer_constraint_v1 *constraint)
{
	if (!constraint->current.cursor_hint.enabled)
	{
		return;
	}
	double lx, ly;
	if (surface_local_to_layout(server, constraint->surface,
								constraint->current.cursor_hint.x, constraint->current.cursor_hint.y, &lx, &ly))
	{
		wlr_cursor_warp_closest(server->cursor, NULL, lx, ly);
	}
}

/** Activate/deactivate pointer confinement and keep region/listener state synchronized. */
static void cursor_constrain(struct comp_server *server, struct wlr_pointer_constraint_v1 *constraint,
							 double sx, double sy)
{
	(void)sx;
	(void)sy;
	if (server->active_pointer_constraint == constraint)
	{
		return;
	}
	if (server->pointer_constraint_commit.link.next)
	{
		wl_list_remove(&server->pointer_constraint_commit.link);
	}
	if (server->active_pointer_constraint)
	{
		if (!constraint)
		{
			pointer_constraint_warp_hint(server, server->active_pointer_constraint);
		}
		wlr_pointer_constraint_v1_send_deactivated(server->active_pointer_constraint);
	}
	server->active_pointer_constraint = constraint;
	if (!constraint)
	{
		wl_list_init(&server->pointer_constraint_commit.link);
		return;
	}
	server->pointer_confine_requires_warp = true;
	if (pixman_region32_not_empty(&constraint->current.region))
	{
		pixman_region32_intersect(&constraint->region, &constraint->surface->input_region,
								  &constraint->current.region);
	}
	else
	{
		pixman_region32_copy(&constraint->region, &constraint->surface->input_region);
	}
	pointer_constraint_check_region(server);
	wlr_pointer_constraint_v1_send_activated(constraint);
	server->pointer_constraint_commit.notify = pointer_constraint_handle_commit;
	wl_signal_add(&constraint->surface->events.commit, &server->pointer_constraint_commit);
}

/** Constraint-surface commit callback: refresh effective confinement region. */
static void pointer_constraint_handle_commit(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, pointer_constraint_commit);
	(void)data;
	if (!server->active_pointer_constraint)
	{
		return;
	}
	pointer_constraint_check_region(server);
}

/** Constraint set_region callback: request deferred confinement warp/revalidation. */
static void pointer_constraint_handle_set_region(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_pointer_constraint *pc = wl_container_of(listener, pc, set_region);
	pc->server->pointer_confine_requires_warp = true;
}

/** Constraint destroy callback: detach listeners and deactivate if currently active. */
static void pointer_constraint_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_pointer_constraint *pc = wl_container_of(listener, pc, destroy);
	struct comp_server *server = pc->server;
	wl_list_remove(&pc->destroy.link);
	wl_list_remove(&pc->set_region.link);
	if (server->active_pointer_constraint == pc->constraint)
	{
		cursor_constrain(server, NULL, NAN, NAN);
	}
	free(pc);
}

/** new_constraint callback: allocate wrapper, register listeners, and activate on focus match. */
static void handle_new_pointer_constraint(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, new_pointer_constraint);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_pointer_constraint_v1 *constraint = data;

	struct comp_pointer_constraint *pc = calloc(1, sizeof(*pc));
	if (!pc)
	{
		return;
	}
	pc->server = server;
	pc->constraint = constraint;
	pc->destroy.notify = pointer_constraint_handle_destroy;
	wl_signal_add(&constraint->events.destroy, &pc->destroy);
	pc->set_region.notify = pointer_constraint_handle_set_region;
	wl_signal_add(&constraint->events.set_region, &pc->set_region);

	struct wlr_surface *focused = server->seat->pointer_state.focused_surface;
	if (focused == constraint->surface)
	{
		double sx, sy;
		if (surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy))
		{
			cursor_constrain(server, constraint, sx, sy);
		}
	}
}

/** Fill stable debug labels for a surface's root protocol role. */
static void pointer_debug_describe_surface(struct wlr_surface *surface,
										   const char **role, const char **name, const char **title)
{
	*role = "none";
	*name = "";
	*title = "";
	if (!surface)
	{
		return;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	struct wlr_layer_surface_v1 *layer = wlr_layer_surface_v1_try_from_wlr_surface(root);
	if (layer)
	{
		*role = "layer";
		*name = layer->namespace ? layer->namespace : "";
		return;
	}
	struct wlr_xdg_toplevel *toplevel = wlr_xdg_toplevel_try_from_wlr_surface(root);
	if (toplevel)
	{
		*role = "xdg";
		*name = toplevel->app_id ? toplevel->app_id : "";
		*title = toplevel->title ? toplevel->title : "";
		return;
	}
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(root);
	if (popup)
	{
		*role = "popup";
		return;
	}
	*role = "surface";
}

/** Pointer-focus change callback: activate/deactivate constraints for new focused surface. */
static void seat_pointer_focus_change(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, seat_pointer_focus_change);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_seat_pointer_focus_change_event *ev = data;
	if (pointer_focus_debug_logs_enabled)
	{
		const char *old_role, *old_name, *old_title;
		const char *new_role, *new_name, *new_title;
		pointer_debug_describe_surface(ev->old_surface, &old_role, &old_name, &old_title);
		pointer_debug_describe_surface(ev->new_surface, &new_role, &new_name, &new_title);
		wlr_log(WLR_INFO,
				"ptrdbg:focus old=%s name='%s' title='%s' new=%s name='%s' title='%s' sx=%.1f sy=%.1f cursor=%.1f,%.1f",
				old_role, old_name, old_title, new_role, new_name, new_title,
				ev->sx, ev->sy, server->cursor ? server->cursor->x : 0.0,
				server->cursor ? server->cursor->y : 0.0);
	}
	if (layer_hit_debug_logs_enabled)
	{
		const char *old_role, *old_name, *old_title;
		const char *new_role, *new_name, *new_title;
		pointer_debug_describe_surface(ev->old_surface, &old_role, &old_name, &old_title);
		pointer_debug_describe_surface(ev->new_surface, &new_role, &new_name, &new_title);
		layer_debug_log_hit_context(server, old_role, new_role,
									server->cursor ? server->cursor->x : 0.0,
									server->cursor ? server->cursor->y : 0.0);
	}
	struct wlr_pointer_constraint_v1 *constraint =
		wlr_pointer_constraints_v1_constraint_for_surface(server->pointer_constraints,
														  ev->new_surface, server->seat);
	cursor_constrain(server, constraint, ev->sx, ev->sy);
}

/** Forward relative-motion protocol events and apply active confinement before cursor move. */
static void apply_pointer_motion(struct comp_server *server, struct wlr_input_device *dev,
								 uint32_t time_msec, double dx, double dy, double dx_unaccel, double dy_unaccel)
{
	if (!server || !server->seat || server->display_terminate_requested)
	{
		return;
	}

	if (server->relative_pointer_manager)
	{
		wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_manager,
															 server->seat, (uint64_t)time_msec * 1000, dx, dy, dx_unaccel, dy_unaccel);
	}

	if (server->active_pointer_constraint && dev && dev->type == WLR_INPUT_DEVICE_POINTER)
	{
		double sx, sy;
		struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (!surface || server->active_pointer_constraint->surface != surface)
		{
			return;
		}
		double sx_confined, sy_confined;
		if (!wlr_region_confine(&server->pointer_confine, sx, sy, sx + dx, sy + dy,
								&sx_confined, &sy_confined))
		{
			return;
		}
		dx = sx_confined - sx;
		dy = sy_confined - sy;
	}

	wlr_cursor_move(server->cursor, dev, dx, dy);
}

/**
 * Route cursor motion according to current grab state, then update pointer focus
 * and motion delivery for the surface currently under the cursor.
 */
static void process_cursor_motion(struct comp_server *server, uint32_t time_msec)
{
	if (!server || !server->seat || server->display_terminate_requested)
	{
		return;
	}

	if (server->grab == COMP_GRAB_MOVE && server->grabbed_toplevel)
	{
		if (server->seat->pointer_state.button_count == 0)
		{
			cancel_active_grab(server);
		}
	}
	if (server->grab == COMP_GRAB_MOVE && server->grabbed_toplevel)
	{
		struct comp_toplevel *v = server->grabbed_toplevel;
		double dx = server->cursor->x - server->grab_cursor_x;
		double dy = server->cursor->y - server->grab_cursor_y;
		wlr_scene_node_set_position(&v->scene_tree->node,
									server->grab_view_x + (int)dx, server->grab_view_y + (int)dy);
	}
	else if (server->grab == COMP_GRAB_RESIZE && server->grabbed_toplevel)
	{
		struct comp_toplevel *v = server->grabbed_toplevel;
		double dx = server->cursor->x - server->grab_cursor_x;
		double dy = server->cursor->y - server->grab_cursor_y;

		/* Resize math runs in xdg window-geometry coordinates so it matches the edge
		 * hit box that armed the grab. Convert back to scene coordinates only when
		 * moving the scene node. */
		int x = server->grab_view_x;
		int y = server->grab_view_y;
		int w = server->grab_view_width;
		int h = server->grab_view_height;

		if (server->resize_edges & WLR_EDGE_LEFT)
		{
			x = server->grab_view_x + (int)dx;
			w = server->grab_view_width - (int)dx;
		}
		else if (server->resize_edges & WLR_EDGE_RIGHT)
		{
			w = server->grab_view_width + (int)dx;
		}
		if (server->resize_edges & WLR_EDGE_TOP)
		{
			y = server->grab_view_y + (int)dy;
			h = server->grab_view_height - (int)dy;
		}
		else if (server->resize_edges & WLR_EDGE_BOTTOM)
		{
			h = server->grab_view_height + (int)dy;
		}

		toplevel_constrain_resize(v, server->resize_edges, &x, &y, &w, &h);

		log_resize_state("motion", v, x, y, w, h, server->resize_edges);
		if (toplevel_needs_compositor_resize_fallback(v) &&
			server->resize_last_configure_msec != 0 &&
			time_msec - server->resize_last_configure_msec < bridge_resize_interval_msec)
		{
			/* xwayland-satellite acknowledges XDG configure serials before the underlying
			 * X11 client has processed ConfigureNotify. Pace legacy bridge traffic at the
			 * configured rate and retain only the newest pointer target between intervals. */
			server->resize_pending_x = x;
			server->resize_pending_y = y;
			server->resize_pending_width = w;
			server->resize_pending_height = h;
			server->resize_pending_valid = true;
		}
		else
		{
			toplevel_apply_resize_box(v, x, y, w, h);
			server->resize_last_configure_msec = time_msec;
			server->resize_pending_valid = false;
		}
	}

	if (server->grab == COMP_GRAB_RESIZE)
	{
		const char *cursor_name = cursor_name_for_resize_edges(server->resize_edges);
		if (cursor_name)
		{
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cursor_name);
		}
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}

	/* Bridge-provided CSD can cover the visible frame with a client surface without ever
	 * issuing request_resize. Give Morph's fallback ring first chance on those windows. */
	struct comp_toplevel *edge_view = toplevel_near_cursor_resize_edge(server);
	if (edge_view && toplevel_can_direct_resize(server, edge_view) &&
		toplevel_needs_compositor_resize_fallback(edge_view))
	{
		const uint32_t edges = toplevel_resize_edges_at_cursor(server, edge_view);
		const char *cursor_name = cursor_name_for_resize_edges(edges);
		if (cursor_name)
		{
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cursor_name);
			wlr_seat_pointer_notify_clear_focus(server->seat);
			return;
		}
	}

	double sx, sy;
	struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
	if (surface)
	{
		struct comp_toplevel *focused_frame = focused_toplevel_frame_at_cursor(server);
		if (focused_frame)
		{
			struct wlr_surface *root = wlr_surface_get_root_surface(surface);
			if (focused_frame->xdg_toplevel && focused_frame->xdg_toplevel->base->surface != root &&
				wlr_xdg_toplevel_try_from_wlr_surface(root))
			{
				/* Transparent titlebar/frame holes must not transfer pointer ownership to
				 * clients below the focused window; otherwise their text/hand cursors leak
				 * through and hide Morph's resize/default cursor policy for the top frame. */
				wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
				wlr_seat_pointer_notify_clear_focus(server->seat);
				return;
			}
		}
		if (blocking_layer_surface_at(server, server->cursor->x, server->cursor->y) &&
			pointer_focus_is_layer_surface(server))
		{
			struct wlr_surface *root = wlr_surface_get_root_surface(surface);
			if (!wlr_layer_surface_v1_try_from_wlr_surface(root))
			{
				/* Keep pointer ownership on panels while the cursor moves through their
				 * guarded edge band. This avoids layer-shell task buttons losing focus to a
				 * normal window just underneath sparse or transparent panel pixels, while
				 * still allowing real panel surface pixels to receive motion normally. */
				return;
			}
		}
		/* Clearing pointer focus while Morph owns a resize edge prevents a bridged client
		 * from replacing that cursor. Restore a neutral image before re-entering the surface;
		 * older X11 toolkits do not necessarily submit another cursor request until a click. */
		if (!server->seat->pointer_state.focused_surface)
		{
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		}
		/* Re-entering transfers cursor ownership back to the client immediately. */
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
		/* FocusFollowsMouse / SloppyFocus: keyboard follows XDG toplevel enter only.
		 * Desktop-like BACKGROUND/BOTTOM layers count as empty root; panels do not. */
		if (server_focus_policy(server) != COMP_FOCUS_CLICK)
		{
			struct comp_toplevel *under = NULL;
			const bool empty_root = pointer_focus_target_at(server, server->cursor->x, server->cursor->y, &under);
			apply_motion_focus_policy(server, under, empty_root);
		}
		return;
	}

	edge_view = toplevel_near_cursor_resize_edge(server);
	if (edge_view && toplevel_can_direct_resize(server, edge_view))
	{
		struct wlr_box box;
		if (toplevel_get_hit_box(edge_view, &box) && point_in_box(&box, server->cursor->x, server->cursor->y))
		{
			/* Cursor is inside the window bounds but surface_at() returned NULL.
			 * Treat this as client interior, not as outside resize zone, to avoid
			 * spurious resizer jumps in transient surface holes. */
			/* Keep last client-owned pointer focus/cursor untouched to prevent
			 * mode flapping while crossing tiny no-surface gaps near edges. */
			apply_motion_focus_policy(server, edge_view, false);
			return;
		}
		const uint32_t edges = toplevel_resize_edges_at_cursor(server, edge_view);
		const char *cursor_name = cursor_name_for_resize_edges(edges);
		if (cursor_name)
		{
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cursor_name);
		}
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}
	if (blocking_layer_surface_at(server, server->cursor->x, server->cursor->y))
	{
		/* Some panels draw hoverable controls inside transparent or sparse input
		 * regions. If scene hit-testing finds no concrete buffer at this exact
		 * coordinate, keep the existing pointer focus instead of sending a leave
		 * that makes the panel immediately repaint back and forth. */
		return;
	}

	/* Background/empty space: compositor default cursor and no focused pointer surface. */
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	wlr_seat_pointer_notify_clear_focus(server->seat);
	apply_motion_focus_policy(server, NULL, true);
}

/** Relative pointer motion callback. */
static void server_cursor_motion(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *ev = data;
	apply_pointer_motion(server, &ev->pointer->base, ev->time_msec, ev->delta_x, ev->delta_y,
						 ev->unaccel_dx, ev->unaccel_dy);
	process_cursor_motion(server, ev->time_msec);
}

/** Absolute pointer motion callback. */
static void server_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *ev = data;
	wlr_cursor_warp_absolute(server->cursor, &ev->pointer->base, ev->x, ev->y);
	process_cursor_motion(server, ev->time_msec);
}

/**
 * Try transferring keyboard focus to interactive layer-surfaces on click/tap,
 * respecting layer-shell keyboard interactivity semantics.
 */
static void layer_surface_try_keyboard_focus_click(struct comp_server *server, double lx, double ly)
{
	if (!server || !server->seat || server->display_terminate_requested)
	{
		return;
	}
	double sx, sy;
	struct wlr_surface *surf = surface_at(server, lx, ly, &sx, &sy);
	if (!surf)
	{
		return;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surf);
	struct wlr_layer_surface_v1 *ls = wlr_layer_surface_v1_try_from_wlr_surface(root);
	if (!ls || !ls->surface->mapped)
	{
		return;
	}
	const enum zwlr_layer_surface_v1_keyboard_interactivity ki = ls->current.keyboard_interactive;
	const bool top_or_overlay = ls->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP ||
								ls->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
	if (ki == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
	{
		return;
	}
	if (ki == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE && !top_or_overlay)
	{
		return;
	}
	struct wlr_seat *seat = server->seat;
	struct wlr_keyboard *kbd = wlr_seat_get_keyboard(seat);
	if (kbd)
	{
		wlr_seat_keyboard_notify_enter(seat, ls->surface, kbd->keycodes, kbd->num_keycodes, &kbd->modifiers);
	}
	else
	{
		wlr_seat_keyboard_notify_enter(seat, ls->surface, NULL, 0, NULL);
	}
}

/** Pointer button callback: grab lifecycle, focus policy, and client button forwarding. */
static void server_cursor_button(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *ev = data;
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED && server->grab != COMP_GRAB_NONE &&
		server->seat->pointer_state.button_count == 0)
	{
		/* A missed release can leave the compositor in move/resize mode before the
		 * next click. Clear it here so title-bar controls receive a normal press. */
		cancel_active_grab(server);
	}

	uint32_t mods = 0;
	struct wlr_keyboard *kbd = wlr_seat_get_keyboard(server->seat);
	if (kbd)
	{
		mods = kbd->modifiers.depressed;
	}

	if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED && server->grab == COMP_GRAB_NONE)
	{
		/* Tap-to-click and some synthetic pointer sources can deliver a button press
		 * without any preceding motion event. Refresh pointer focus from the current
		 * cursor position first so the press is dispatched to the surface actually
		 * under the pointer instead of a stale previous target. */
		process_cursor_motion(server, ev->time_msec);
	}

	if (ev->state == WL_POINTER_BUTTON_STATE_RELEASED && server->grab != COMP_GRAB_NONE)
	{
		struct comp_toplevel *dragged = server->grabbed_toplevel;
		bool was_move = server->grab == COMP_GRAB_MOVE;
		bool was_resize = server->grab == COMP_GRAB_RESIZE;
		if (was_resize && dragged)
		{
			int release_x = server->grab_view_x;
			int release_y = server->grab_view_y;
			int release_width = server->grab_view_width;
			int release_height = server->grab_view_height;
			if (server->resize_pending_valid)
			{
				/* Apply the exact final pointer target even if it fell inside the throttle interval. */
				release_x = server->resize_pending_x;
				release_y = server->resize_pending_y;
				release_width = server->resize_pending_width;
				release_height = server->resize_pending_height;
				toplevel_apply_resize_box(dragged,
					release_x, release_y, release_width, release_height);
				server->resize_pending_valid = false;
			}
			log_resize_state("release", dragged,
				release_x, release_y, release_width, release_height,
				server->resize_edges);
			dragged->resize_anchor_finishing = true;
			toplevel_set_resizing(dragged, false);
		}
		server->grab = COMP_GRAB_NONE;
		server->grabbed_toplevel = NULL;
		server->resize_edges = 0;
		if (was_move && (server->layout == COMP_LAYOUT_TILE || server->layout == COMP_LAYOUT_SCROLL) && dragged)
		{
			double sx, sy;
			struct comp_toplevel *drop =
				toplevel_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
			if (drop && drop != dragged && !dragged->tile_float && !drop->tile_float &&
				dragged->workspace == drop->workspace)
			{
				tile_swap_sort_keys(dragged, drop);
			}
			if (dragged->layout_anim_tracked)
			{
				dragged->layout_anim_x = (double)dragged->scene_tree->node.x;
				dragged->layout_anim_y = (double)dragged->scene_tree->node.y;
			}
			server_arrange_toplevels(server);
		}
		if (ev->button == BTN_LEFT && server->swallow_left_release)
		{
			server->swallow_left_release = false;
			process_cursor_motion(server, ev->time_msec);
			return;
		}
		wlr_seat_pointer_notify_button(server->seat, ev->time_msec, ev->button, ev->state);
		process_cursor_motion(server, ev->time_msec);
		return;
	}

	if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED && ev->button == BTN_LEFT &&
		(mods & WLR_MODIFIER_LOGO) && !server->suppress_logo_pointer_drag)
	{
		double sx, sy;
		struct comp_toplevel *v = toplevel_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (v)
		{
			begin_move(server, v, true);
			focus_toplevel(server, v);
			return;
		}
	}

	if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED && ev->button == BTN_LEFT)
	{
		struct comp_toplevel *edge_v = toplevel_near_cursor_resize_edge(server);
		if (edge_v && toplevel_can_direct_resize(server, edge_v))
		{
			const uint32_t edges = toplevel_effective_resize_edges_at_cursor(server, edge_v);
			if (edges)
			{
				focus_toplevel(server, edge_v);
				begin_resize(server, edge_v, edges);
				server->swallow_left_release = true;
				return;
			}
		}

		double sx, sy;
		struct comp_toplevel *v = toplevel_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (v)
		{
			focus_toplevel(server, v);
		}
		else
		{
			layer_surface_try_keyboard_focus_click(server, server->cursor->x, server->cursor->y);
			/* ClickToFocus and FocusFollowsMouse clear on empty root / desktop backdrop;
			 * SloppyFocus keeps focus. Panels are not empty root. */
			struct comp_toplevel *under = NULL;
			if (server_focus_policy(server) != COMP_FOCUS_SLOPPY &&
				pointer_focus_target_at(server, server->cursor->x, server->cursor->y, &under) && !under)
			{
				clear_keyboard_focus(server);
			}
		}
	}

	wlr_seat_pointer_notify_button(server->seat, ev->time_msec, ev->button, ev->state);
}

/** Pointer axis callback (scroll/wheel). */
static void server_cursor_axis(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *ev = data;
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	wlr_seat_pointer_notify_axis(server->seat, ev->time_msec, ev->orientation,
								 ev->delta, ev->delta_discrete, ev->source, ev->relative_direction);
}

/** Pointer frame callback: flush pointer frame and restore default cursor image. */
static void server_cursor_frame(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_server *server = wl_container_of(listener, server, cursor_frame);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	wlr_seat_pointer_notify_frame(server->seat);
}

/** Seat request_set_cursor callback: allow client cursor surface updates. */
static void seat_request_cursor(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, seat_request_cursor);
	if (server->display_terminate_requested)
	{
		return;
	}
	/* Keep compositor-controlled resize cursor authoritative during interactive grabs. */
	if (server->grab != COMP_GRAB_NONE)
	{
		return;
	}
	/* Morph owns both outside resize zones and the inner fallback ring used by bridged CSD. */
	struct comp_toplevel *v = toplevel_near_cursor_resize_edge(server);
	if (v && toplevel_can_direct_resize(server, v) &&
		toplevel_effective_resize_edges_at_cursor(server, v))
	{
		return;
	}
	struct comp_toplevel *focused_frame = focused_toplevel_frame_at_cursor(server);
	if (focused_frame)
	{
		double sx, sy;
		struct wlr_surface *surface = surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (surface)
		{
			struct wlr_surface *root = wlr_surface_get_root_surface(surface);
			if (focused_frame->xdg_toplevel && focused_frame->xdg_toplevel->base->surface != root &&
				wlr_xdg_toplevel_try_from_wlr_surface(root))
			{
				/* A lower client may request a text/hand cursor after scene hit-testing sees it
				 * through a transparent bridged frame. Reject that request so the focused
				 * frame keeps Morph's resize/default cursor until the pointer really leaves. */
				return;
			}
		}
	}
	struct wlr_seat_pointer_request_set_cursor_event *ev = data;
	wlr_cursor_set_surface(server->cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
}

/** Seat request_set_selection callback: route clipboard selection ownership. */
static void seat_request_set_selection(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, seat_request_set_selection);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_seat_request_set_selection_event *ev = data;
	wlr_seat_set_selection(server->seat, ev->source, ev->serial);
}

/** Seat request_set_primary_selection callback: route primary selection ownership. */
static void seat_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, seat_request_set_primary_selection);
	if (!server->seat || server->display_terminate_requested)
	{
		return;
	}
	struct wlr_seat_request_set_primary_selection_event *ev = data;
	wlr_seat_set_primary_selection(server->seat, ev->source, ev->serial);
}

/** Backend destroy callback: terminate the Wayland event loop on backend teardown. */
static void server_backend_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_server *server = wl_container_of(listener, server, backend_destroy);
	if (!server->wl_display)
	{
		return;
	}
	/*
	 * Nested backends (e.g. X11) can tear down from outside the Wayland loop
	 * when the host window is closed. Terminating here ensures wl_display_run()
	 * exits and the compositor process does not linger in the background.
	 */
	wlr_log(WLR_INFO, "Backend destroyed, terminating display loop");
	server_request_terminate(server, NULL);
}

/** Initialize wlroots objects, protocol globals, listeners, and compositor runtime state. */
bool server_init(struct comp_server *server)
{
	server->wl_display = wl_display_create();
	if (!server->wl_display)
	{
		return false;
	}
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);

	server->backend = wlr_backend_autocreate(loop, &server->session);
	if (!server->backend)
	{
		return false;
	}
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (!server->renderer)
	{
		return false;
	}
	wlr_renderer_init_wl_display(server->renderer, server->wl_display);

	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (!server->allocator)
	{
		return false;
	}

	struct wl_display *dpy = server->wl_display;
	server->compositor = wlr_compositor_create(dpy, 6, server->renderer);
	server->viewporter = wlr_viewporter_create(dpy);
	if (!server->viewporter)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_viewporter (required for xwayland-satellite)");
		return false;
	}
	server->subcompositor = wlr_subcompositor_create(dpy);
	server->data_device_mgr = wlr_data_device_manager_create(dpy);
	server->primary_selection_mgr = wlr_primary_selection_v1_device_manager_create(dpy);
	if (!server->primary_selection_mgr)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_primary_selection_v1_device_manager");
		return false;
	}
	server->output_layout = wlr_output_layout_create(dpy);
	server->xdg_output_manager = wlr_xdg_output_manager_v1_create(dpy, server->output_layout);
	if (!server->xdg_output_manager)
	{
		return false;
	}
	/* Needed so launchers and Xwayland bridges can hand focus to newly created windows. */
	server->xdg_activation = wlr_xdg_activation_v1_create(dpy);
	if (!server->xdg_activation)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_xdg_activation_v1");
		return false;
	}

	server->screencopy_manager = wlr_screencopy_manager_v1_create(dpy);
	if (!server->screencopy_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_screencopy_manager_v1");
		return false;
	}

	server->export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(dpy);
	if (!server->export_dmabuf_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_export_dmabuf_manager_v1");
		return false;
	}

	server->ext_data_control_manager = wlr_ext_data_control_manager_v1_create(dpy, 1);
	if (!server->ext_data_control_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_ext_data_control_manager_v1");
		return false;
	}

	server->pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	if (!server->pointer_constraints)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_pointer_constraints_v1");
		return false;
	}
	server->relative_pointer_manager = wlr_relative_pointer_manager_v1_create(dpy);
	if (!server->relative_pointer_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_relative_pointer_manager_v1");
		return false;
	}
	pixman_region32_init(&server->pointer_confine);
	server->active_pointer_constraint = NULL;
	server->pointer_confine_requires_warp = false;
	wl_list_init(&server->pointer_constraint_commit.link);

	server->scene = wlr_scene_create();
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] =
		wlr_scene_tree_create(&server->scene->tree);
	server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] =
		wlr_scene_tree_create(&server->scene->tree);
	server->windows_tree = wlr_scene_tree_create(&server->scene->tree);
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP] = wlr_scene_tree_create(&server->scene->tree);
	server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] =
		wlr_scene_tree_create(&server->scene->tree);

	if (!gamma_control_init(server))
	{
		return false;
	}

	server->xdg_shell = wlr_xdg_shell_create(dpy, 3);
	server->foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(dpy);
	if (!server->foreign_toplevel_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_foreign_toplevel_manager_v1");
		return false;
	}
	/* Keep backend lifetime wired to compositor shutdown. */
	server->backend_destroy.notify = server_backend_destroy;
	wl_signal_add(&server->backend->events.destroy, &server->backend_destroy);
	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->xdg_shell_new_toplevel.notify = xdg_shell_new_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->xdg_shell_new_toplevel);
	/* Wire xdg-activation after the global exists so token-backed requests can reuse Morph focus rules. */
	server->xdg_activation_request_activate.notify = xdg_activation_handle_request_activate;
	wl_signal_add(&server->xdg_activation->events.request_activate,
				  &server->xdg_activation_request_activate);

	server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(dpy);
	if (!server->xdg_decoration_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_xdg_decoration_manager_v1");
		return false;
	}
	server->new_xdg_decoration.notify = xdg_new_toplevel_decoration;
	wl_signal_add(&server->xdg_decoration_manager->events.new_toplevel_decoration, &server->new_xdg_decoration);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	if (!server->cursor_mgr)
	{
		return false;
	}

	server->layer_shell = wlr_layer_shell_v1_create(dpy, 4);
	if (!server->layer_shell)
	{
		return false;
	}
	wl_list_init(&server->layers);
	server->layer_shell_new_surface.notify = layer_shell_new_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->layer_shell_new_surface);

	server->seat = wlr_seat_create(dpy, "seat0");
	server->tablet_manager = wlr_tablet_v2_create(dpy);
	if (!server->tablet_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_tablet_v2 manager");
		return false;
	}
	if (!virtual_input_init(server))
	{
		return false;
	}
	wl_list_init(&server->tablets);
	wl_list_init(&server->tracked_inputs);

	server->cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = server_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
	server->cursor_touch_down.notify = server_cursor_touch_down;
	wl_signal_add(&server->cursor->events.touch_down, &server->cursor_touch_down);
	server->cursor_touch_up.notify = server_cursor_touch_up;
	wl_signal_add(&server->cursor->events.touch_up, &server->cursor_touch_up);
	server->cursor_touch_motion.notify = server_cursor_touch_motion;
	wl_signal_add(&server->cursor->events.touch_motion, &server->cursor_touch_motion);
	server->cursor_touch_cancel.notify = server_cursor_touch_cancel;
	wl_signal_add(&server->cursor->events.touch_cancel, &server->cursor_touch_cancel);
	server->cursor_touch_frame.notify = server_cursor_touch_frame;
	wl_signal_add(&server->cursor->events.touch_frame, &server->cursor_touch_frame);
	server->cursor_tablet_tool_axis.notify = server_cursor_tablet_tool_axis;
	wl_signal_add(&server->cursor->events.tablet_tool_axis, &server->cursor_tablet_tool_axis);
	server->cursor_tablet_tool_proximity.notify = server_cursor_tablet_tool_proximity;
	wl_signal_add(&server->cursor->events.tablet_tool_proximity, &server->cursor_tablet_tool_proximity);
	server->cursor_tablet_tool_tip.notify = server_cursor_tablet_tool_tip;
	wl_signal_add(&server->cursor->events.tablet_tool_tip, &server->cursor_tablet_tool_tip);
	server->cursor_tablet_tool_button.notify = server_cursor_tablet_tool_button;
	wl_signal_add(&server->cursor->events.tablet_tool_button, &server->cursor_tablet_tool_button);

	server->seat_request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->seat_request_cursor);
	server->seat_request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->seat_request_set_selection);
	server->seat_request_set_primary_selection.notify = seat_request_set_primary_selection;
	wl_signal_add(&server->seat->events.request_set_primary_selection,
				  &server->seat_request_set_primary_selection);
	server->seat_pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server->seat->pointer_state.events.focus_change, &server->seat_pointer_focus_change);
	server->new_pointer_constraint.notify = handle_new_pointer_constraint;
	wl_signal_add(&server->pointer_constraints->events.new_constraint, &server->new_pointer_constraint);

	wl_list_init(&server->outputs);
	wl_list_init(&server->toplevels);

	if (!output_power_init(server))
	{
		return false;
	}
	if (!output_mgmt_init(server))
	{
		return false;
	}

	server->ipc_listen_fd = -1;
	server->ipc_socket_path[0] = '\0';
	server->grab = COMP_GRAB_NONE;
	if (server->ipc_enabled && !ipc_init(server))
	{
		wlr_log(WLR_ERROR, "ipc: disabled (initialization failed)");
		server->ipc_enabled = false;
	}
	server_update_seat_capabilities(server);
	ext_workspace_init(server);
	return true;
}

/** Shutdown helper: run hooks and release high-level runtime state. */
static void server_finish(struct comp_server *server)
{
	if (compositor_session_active && server->config)
	{
		comp_config_run_shutdown(server->config);
	}
	/*
	 * Input devices may be destroyed after wl_seat teardown during wl_display_destroy().
	 * Remove tracked-input destroy listeners now so they cannot call seat capability updates
	 * on partially destroyed seat/client objects.
	 */
	server_clear_tracked_inputs(server);
	/*
	 * Prevent wlroots destroy-time assertions by removing our listeners from
	 * protocol/backend listener lists before the display/global teardown starts.
	 */
	server_detach_global_listeners(server);
	motion_focus_idle_cancel(server);
	compositor_session_active = false;
	gamma_control_fini(server);
	ext_workspace_fini(server);
	ipc_fini(server);
	comp_config_free(server->config);
	server->config = NULL;
	free(server->config_path);
	server->config_path = NULL;
}

/** Print command-line usage and available startup/IPC flags. */
static void print_usage(const char *argv0)
{
	const char *prog = (argv0 && argv0[0]) ? argv0 : "morph";
	printf("Usage: %s [options]\n", prog);
	printf("\n");
	printf("General:\n");
	printf("  -h, --help                 Show this help and exit\n");
	printf("  -c, --config PATH          Load config from PATH\n");
	printf("\n");
	printf("Logging:\n");
	printf("  --log-level LEVEL          silent|error|info|debug\n");
	printf("  --quiet                    Shortcut for --log-level error\n");
	printf("  --verbose                  Shortcut for --log-level debug\n");
	printf("  --log-file PATH            Append logs to PATH\n");
	printf("\n");
	printf("Crash handling:\n");
	printf("  --crash-log PATH           Append crash markers to PATH\n");
	printf("  --no-crash-handler         Disable crash handler installation\n");
	printf("  --crash-test               Trigger a deliberate SIGSEGV after startup\n");
	printf("\n");
	printf("Runtime control (IPC-aware):\n");
	printf("  --layout stack|tile|scroll\n");
	printf("  --scroll                   Same as --layout scroll\n");
	printf("  --tile-move ARG            prev|next|left|right|first|last|N\n");
	printf("  --tile-grid ARG [COUNT]    up|down|left|right|top|bottom or signed N\n");
	printf("  --scroll-move ARG          prev|next|left|right|N\n");
	printf("  --workspace ARG            1..%d|next|prev\n", COMP_WORKSPACE_COUNT);
	printf("  --workspace-move N         Move focused window to workspace N\n");
	printf("  --focus POLICY             ClickToFocus|FocusFollowsMouse|SloppyFocus\n");
	printf("  --reload-config            Send reload request to running compositor\n");
	printf("  --allow-builtin-fallback   Start with synthesized config defaults if no file resolves\n");
	printf("  --ipc                      Keep compatibility; IPC is default-on\n");
	printf("  --no-ipc                   Disable IPC socket for this instance\n");
}

/** Process CLI/IPC startup flow, initialize compositor, and run Wayland event loop. */
int main(int argc, char **argv)
{
	wlr_log_init(WLR_INFO, NULL);
	enum wlr_log_importance startup_log_level = WLR_INFO;
	const char *startup_log_file_path = NULL;
	const char *startup_crash_log_path = NULL;
	bool disable_crash_handler = false;
	bool crash_test_from_argv = false;
	/*
	 * First pass: parse logging flags before wlroots init so early errors and
	 * startup diagnostics already use the requested level and sinks.
	 */
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
		{
			print_usage(argv[0]);
			return 0;
		}
		if (!strcmp(argv[i], "--verbose"))
		{
			startup_log_level = WLR_DEBUG;
			continue;
		}
		if (!strcmp(argv[i], "--quiet"))
		{
			startup_log_level = WLR_ERROR;
			continue;
		}
		if (!strcmp(argv[i], "--log-level"))
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "Missing value after --log-level\n");
				return 1;
			}
			enum wlr_log_importance lvl;
			if (!morph_parse_log_level(argv[i + 1], &lvl))
			{
				fprintf(stderr,
						"Unknown --log-level %s (use silent, error, info, debug)\n",
						argv[i + 1]);
				return 1;
			}
			startup_log_level = lvl;
			i++;
			continue;
		}
		if (!strncmp(argv[i], "--log-level=", 12))
		{
			enum wlr_log_importance lvl;
			if (!morph_parse_log_level(argv[i] + 12, &lvl))
			{
				fprintf(stderr,
						"Unknown --log-level %s (use silent, error, info, debug)\n",
						argv[i] + 12);
				return 1;
			}
			startup_log_level = lvl;
			continue;
		}
		if (!strcmp(argv[i], "--log-file"))
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "Missing path after --log-file\n");
				return 1;
			}
			startup_log_file_path = argv[i + 1];
			i++;
			continue;
		}
		if (!strncmp(argv[i], "--log-file=", 11))
		{
			startup_log_file_path = argv[i] + 11;
			continue;
		}
		if (!strcmp(argv[i], "--crash-log"))
		{
			if (i + 1 >= argc)
			{
				fprintf(stderr, "Missing path after --crash-log\n");
				return 1;
			}
			startup_crash_log_path = argv[i + 1];
			i++;
			continue;
		}
		if (!strncmp(argv[i], "--crash-log=", 12))
		{
			startup_crash_log_path = argv[i] + 12;
			continue;
		}
		if (!strcmp(argv[i], "--no-crash-handler"))
		{
			disable_crash_handler = true;
			continue;
		}
		if (!strcmp(argv[i], "--crash-test"))
		{
			crash_test_from_argv = true;
			continue;
		}
	}

	if (startup_log_file_path && startup_log_file_path[0])
	{
		/* Append and line-buffer: readable during live sessions without full buffering delay. */
		morph_log_file = fopen(startup_log_file_path, "a");
		if (!morph_log_file)
		{
			fprintf(stderr, "Failed to open --log-file %s: %s\n", startup_log_file_path,
					strerror(errno));
			return 1;
		}
		setvbuf(morph_log_file, NULL, _IOLBF, 0);
		(void)atexit(morph_log_close_file);
	}

	wlr_log_init(startup_log_level, morph_log_callback);
	morph_active_log_level = startup_log_level;
	{
		const char *e = getenv("MORPH_DEBUG_XDG");
		xdg_debug_logs_enabled = e && e[0] && strcmp(e, "0") != 0;
	}
	{
		const char *e = getenv("MORPH_DEBUG_XDG_COMMITS");
		xdg_commit_debug_logs_enabled = xdg_debug_logs_enabled &&
			e && e[0] && strcmp(e, "0") != 0;
	}
	{
		const char *e = getenv("MORPH_DEBUG_POINTER_FOCUS");
		pointer_focus_debug_logs_enabled = e && e[0] && strcmp(e, "0") != 0;
	}
	{
		const char *e = getenv("MORPH_DEBUG_LAYER_HIT");
		layer_hit_debug_logs_enabled = e && e[0] && strcmp(e, "0") != 0;
	}
	configure_bridge_resize_rate_from_env();
	/*
	 * Some parent processes leave SIGCHLD ignored; the kernel then auto-reaps
	 * children and waitpid() in when= / shutdown hooks fails with ECHILD.
	 */
	if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
	{
		wlr_log_errno(WLR_ERROR, "signal(SIGCHLD, SIG_DFL)");
		return 1;
	}

	if (!disable_crash_handler && !morph_crash_handler_install(startup_crash_log_path))
	{
		wlr_log(WLR_ERROR, "Failed to install crash handler");
		return 1;
	}

	const char *cfg_path = getenv("MORPH_CONFIG");
	char cfg_buf[PATH_MAX];
	enum comp_layout initial_layout = COMP_LAYOUT_STACK;
	bool layout_from_argv = false;
	bool tile_move_from_argv = false;
	char tile_move_line[64];
	bool tile_grid_from_argv = false;
	char tile_grid_line[64];
	bool scroll_move_from_argv = false;
	char scroll_move_line[64];
	bool workspace_from_argv = false;
	char workspace_line[64];
	bool workspace_move_from_argv = false;
	char workspace_move_line[64];
	bool no_ipc = false;
	bool reload_config_from_argv = false;
	bool allow_builtin_fallback = false;
	bool focus_from_argv = false;
	enum comp_focus_policy initial_focus_policy = COMP_FOCUS_CLICK;

	for (int i = 1; i < argc; i++)
	{
		/* Already handled in the early logging pass. */
		if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "--quiet"))
		{
			continue;
		}
		if (!strcmp(argv[i], "--log-level") || !strcmp(argv[i], "--log-file"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after %s", argv[i]);
				return 1;
			}
			i++;
			continue;
		}
		if (!strncmp(argv[i], "--log-level=", 12) || !strncmp(argv[i], "--log-file=", 11))
		{
			continue;
		}
		if (!strcmp(argv[i], "--crash-log"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --crash-log");
				return 1;
			}
			i++;
			continue;
		}
		if (!strncmp(argv[i], "--crash-log=", 12) || !strcmp(argv[i], "--no-crash-handler") ||
			!strcmp(argv[i], "--crash-test"))
		{
			continue;
		}
		if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing path after %s", argv[i]);
				return 1;
			}
			cfg_path = argv[++i];
		}
		else if (!strcmp(argv[i], "--layout"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --layout");
				return 1;
			}
			const char *v = argv[++i];
			if (!strcasecmp(v, "tile"))
			{
				initial_layout = COMP_LAYOUT_TILE;
			}
			else if (!strcasecmp(v, "scroll"))
			{
				initial_layout = COMP_LAYOUT_SCROLL;
			}
			else if (!strcasecmp(v, "stack"))
			{
				initial_layout = COMP_LAYOUT_STACK;
			}
			else
			{
				wlr_log(WLR_ERROR, "Unknown --layout %s (use stack, tile, or scroll)", v);
				return 1;
			}
			layout_from_argv = true;
		}
		else if (!strcmp(argv[i], "--scroll"))
		{
			initial_layout = COMP_LAYOUT_SCROLL;
			layout_from_argv = true;
		}
		else if (!strcmp(argv[i], "--tile-move"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --tile-move");
				return 1;
			}
			const char *v = argv[++i];
			if (!strcasecmp(v, "prev") || !strcasecmp(v, "left"))
			{
				snprintf(tile_move_line, sizeof(tile_move_line), "tile move prev\n");
			}
			else if (!strcasecmp(v, "next") || !strcasecmp(v, "right"))
			{
				snprintf(tile_move_line, sizeof(tile_move_line), "tile move next\n");
			}
			else if (!strcasecmp(v, "first"))
			{
				snprintf(tile_move_line, sizeof(tile_move_line), "tile move first\n");
			}
			else if (!strcasecmp(v, "last"))
			{
				snprintf(tile_move_line, sizeof(tile_move_line), "tile move last\n");
			}
			else
			{
				char *end = NULL;
				(void)strtol(v, &end, 10);
				if (!end || end == v || *end)
				{
					wlr_log(WLR_ERROR,
							"Unknown --tile-move %s (use prev, next, left, right, first, last, or a signed integer)",
							v);
					return 1;
				}
				snprintf(tile_move_line, sizeof(tile_move_line), "tile move %s\n", v);
			}
			tile_move_from_argv = true;
		}
		else if (!strcmp(argv[i], "--tile-grid"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --tile-grid");
				return 1;
			}
			const char *v = argv[++i];
			if (i + 1 < argc)
			{
				const char *v2 = argv[i + 1];
				char *e2 = NULL;
				const long cnt = strtol(v2, &e2, 10);
				if (e2 != v2 && *e2 == '\0' && cnt > 0 &&
					(!strcasecmp(v, "left") || !strcasecmp(v, "right") || !strcasecmp(v, "up") ||
					 !strcasecmp(v, "down")))
				{
					snprintf(tile_grid_line, sizeof(tile_grid_line), "tile grid %s %s\n", v, v2);
					i++;
					tile_grid_from_argv = true;
					continue;
				}
			}
			if (!strcasecmp(v, "up") || !strcasecmp(v, "down") || !strcasecmp(v, "left") ||
				!strcasecmp(v, "right") || !strcasecmp(v, "top") || !strcasecmp(v, "bottom"))
			{
				snprintf(tile_grid_line, sizeof(tile_grid_line), "tile grid %s\n", v);
			}
			else
			{
				char *end = NULL;
				(void)strtol(v, &end, 10);
				if (!end || end == v || *end)
				{
					wlr_log(WLR_ERROR,
							"Unknown --tile-grid %s (use up, down, left, right, top, bottom, DIR COUNT, or a signed integer)",
							v);
					return 1;
				}
				snprintf(tile_grid_line, sizeof(tile_grid_line), "tile grid %s\n", v);
			}
			tile_grid_from_argv = true;
		}
		else if (!strcmp(argv[i], "--scroll-move"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --scroll-move");
				return 1;
			}
			const char *v = argv[++i];
			if (!strcasecmp(v, "prev") || !strcasecmp(v, "left"))
			{
				snprintf(scroll_move_line, sizeof(scroll_move_line), "scroll prev\n");
			}
			else if (!strcasecmp(v, "next") || !strcasecmp(v, "right"))
			{
				snprintf(scroll_move_line, sizeof(scroll_move_line), "scroll next\n");
			}
			else
			{
				char *end = NULL;
				(void)strtol(v, &end, 10);
				if (!end || end == v || *end)
				{
					wlr_log(WLR_ERROR,
							"Unknown --scroll-move %s (use prev, next, left, right, or a signed integer)",
							v);
					return 1;
				}
				snprintf(scroll_move_line, sizeof(scroll_move_line), "scroll %s\n", v);
			}
			scroll_move_from_argv = true;
		}
		else if (!strcmp(argv[i], "--workspace"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --workspace");
				return 1;
			}
			const char *v = argv[++i];
			if (!strcasecmp(v, "next"))
			{
				snprintf(workspace_line, sizeof(workspace_line), "workspace next\n");
			}
			else if (!strcasecmp(v, "prev"))
			{
				snprintf(workspace_line, sizeof(workspace_line), "workspace prev\n");
			}
			else
			{
				char *end = NULL;
				const long n = strtol(v, &end, 10);
				if (!end || end == v || *end || n < 1 || n > COMP_WORKSPACE_COUNT)
				{
					wlr_log(WLR_ERROR,
							"Unknown --workspace %s (use 1..%d, next, or prev)", v,
							COMP_WORKSPACE_COUNT);
					return 1;
				}
				snprintf(workspace_line, sizeof(workspace_line), "workspace %ld\n", n);
			}
			workspace_from_argv = true;
		}
		else if (!strcmp(argv[i], "--workspace-move"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --workspace-move");
				return 1;
			}
			const char *v = argv[++i];
			char *end = NULL;
			const long n = strtol(v, &end, 10);
			if (!end || end == v || *end || n < 1 || n > COMP_WORKSPACE_COUNT)
			{
				wlr_log(WLR_ERROR, "Unknown --workspace-move %s (use 1..%d)", v,
						COMP_WORKSPACE_COUNT);
				return 1;
			}
			snprintf(workspace_move_line, sizeof(workspace_move_line), "workspace move %ld\n", n);
			workspace_move_from_argv = true;
		}
		else if (!strcmp(argv[i], "--ipc"))
		{
			/* IPC is default-on when XDG_RUNTIME_DIR is set; flag kept for scripts. */
		}
		else if (!strcmp(argv[i], "--no-ipc"))
		{
			no_ipc = true;
		}
		else if (!strcmp(argv[i], "--reload-config"))
		{
			reload_config_from_argv = true;
		}
		else if (!strcmp(argv[i], "--focus"))
		{
			if (i + 1 >= argc)
			{
				wlr_log(WLR_ERROR, "Missing value after --focus");
				return 1;
			}
			const char *v = argv[++i];
			if (!comp_config_parse_focus_policy(v, &initial_focus_policy))
			{
				wlr_log(WLR_ERROR,
						"Unknown --focus %s (use ClickToFocus, FocusFollowsMouse, or SloppyFocus)", v);
				return 1;
			}
			focus_from_argv = true;
		}
		else if (!strcmp(argv[i], "--allow-builtin-fallback"))
		{
			allow_builtin_fallback = true;
		}
		else
		{
			wlr_log(WLR_ERROR, "Unknown argument: %s", argv[i]);
			return 1;
		}
	}
	if (allow_builtin_fallback)
	{
		/* Export the choice so the shared config loader and reload path both
		 * follow the same explicit fallback contract during this process. */
		setenv("MORPH_ALLOW_BUILTIN_FALLBACK", "1", 1);
	}
	if (reload_config_from_argv)
	{
		if (ipc_client_send_line("reload config\n") != 0)
		{
			wlr_log(WLR_ERROR, "No running morph or IPC failed for --reload-config");
			return 1;
		}
		wlr_log(WLR_INFO, "Sent reload config to running morph");
		return 0;
	}
	if (tile_move_from_argv)
	{
		if (ipc_client_send_line(tile_move_line) != 0)
		{
			wlr_log(WLR_ERROR, "No running morph or IPC failed for --tile-move");
			return 1;
		}
		wlr_log(WLR_INFO, "Sent tile move to running morph via IPC");
	}
	if (tile_grid_from_argv)
	{
		if (ipc_client_send_line(tile_grid_line) != 0)
		{
			wlr_log(WLR_ERROR, "No running morph or IPC failed for --tile-grid");
			return 1;
		}
		wlr_log(WLR_INFO, "Sent tile grid to running morph via IPC");
	}
	if (scroll_move_from_argv)
	{
		if (ipc_client_send_line(scroll_move_line) != 0)
		{
			wlr_log(WLR_ERROR, "No running morph or IPC failed for --scroll-move");
			return 1;
		}
		wlr_log(WLR_INFO, "Sent scroll move to running morph via IPC");
	}
	if (workspace_move_from_argv)
	{
		if (ipc_client_send_line(workspace_move_line) != 0)
		{
			wlr_log(WLR_ERROR, "No running morph or IPC failed for --workspace-move");
			return 1;
		}
		wlr_log(WLR_INFO, "Sent workspace move to running morph via IPC");
	}
	if (workspace_from_argv)
	{
		if (ipc_client_send_line(workspace_line) != 0)
		{
			wlr_log(WLR_ERROR, "No running morph or IPC failed for --workspace");
			return 1;
		}
		wlr_log(WLR_INFO, "Sent workspace to running morph via IPC");
	}
	if (layout_from_argv)
	{
		char line[48];
		const char *layout_word = initial_layout == COMP_LAYOUT_TILE ? "tile" : (initial_layout == COMP_LAYOUT_SCROLL ? "scroll" : "stack");
		snprintf(line, sizeof(line), "layout %s\n", layout_word);
		if (ipc_client_send_line(line) == 0)
		{
			wlr_log(WLR_INFO, "Applied layout to running morph via IPC");
			return 0;
		}
	}
	if (focus_from_argv)
	{
		char line[64];
		snprintf(line, sizeof(line), "focus %s\n", comp_config_focus_policy_name(initial_focus_policy));
		if (ipc_client_send_line(line) == 0)
		{
			wlr_log(WLR_INFO, "Applied focus policy to running morph via IPC");
			return 0;
		}
	}
	if (tile_move_from_argv || tile_grid_from_argv || scroll_move_from_argv ||
		workspace_from_argv || workspace_move_from_argv)
	{
		return 0;
	}
	if (!cfg_path)
	{
		if (comp_config_default_path(cfg_buf, sizeof(cfg_buf)))
		{
			cfg_path = cfg_buf;
		}
		else if (!comp_config_builtin_fallback_enabled())
		{
			wlr_log(WLR_ERROR, "No readable config found in user or system locations");
			return 1;
		}
		else
		{
			/* Let comp_config_load(NULL, ...) synthesize the documented built-in
			 * defaults instead of rejecting the no-config startup earlier. */
			wlr_log(WLR_INFO, "No config file resolved; using built-in defaults");
		}
	}

	struct comp_config *cfg = NULL;
	if (!comp_config_load(cfg_path, &cfg))
	{
		wlr_log(WLR_ERROR, "Failed to load keybind config");
		return 1;
	}
	if (focus_from_argv)
	{
		cfg->focus_policy = initial_focus_policy;
		wlr_log(WLR_INFO, "Startup focus policy override: %s",
				comp_config_focus_policy_name(initial_focus_policy));
	}

	char ipc_probe[108];
	struct comp_server server = {0};
	server.config = cfg;
	server.layout = initial_layout;
	server.current_workspace = 0;
	comp_config_sync_shell_env(&server);
	server.ipc_enabled = !no_ipc && ipc_socket_path(ipc_probe, sizeof(ipc_probe));
	if (cfg_path && cfg_path[0])
	{
		server.config_path = strdup(cfg_path);
	}
	if (!server_init(&server))
	{
		wlr_log(WLR_ERROR, "Failed to initialize compositor");
		free(server.config_path);
		server.config_path = NULL;
		comp_config_free(cfg);
		return 1;
	}

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket)
	{
		wlr_log(WLR_ERROR, "Unable to add Wayland socket");
		server_finish(&server);
		wl_display_destroy_clients(server.wl_display);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	if (!wlr_backend_start(server.backend))
	{
		wlr_log(WLR_ERROR, "Failed to start backend");
		server_destroy_xwayland(&server);
		server_finish(&server);
		wl_display_destroy_clients(server.wl_display);
		wl_display_destroy(server.wl_display);
		return 1;
	}
	compositor_session_active = true;
	setenv("WAYLAND_DISPLAY", socket, true);
	/*
	 * Do not set QT_QPA_PLATFORM globally: many bundles (e.g. Qt AppImages) ship only the
	 * xcb plugin; forcing "wayland" makes Qt abort before falling back. Native Wayland Qt
	 * apps still see WAYLAND_DISPLAY; X11 apps use DISPLAY from xwayland-satellite.
	 */
	wlr_log(WLR_INFO, "Running compositor on WAYLAND_DISPLAY=%s", socket);
	spawn_xwayland_satellite(socket);
	comp_config_run_startup(server.config);
	if (crash_test_from_argv)
	{
		/* Deterministic test hook for validating crash-handler marker output. */
		wlr_log(WLR_ERROR, "--crash-test requested: triggering SIGSEGV for crash-handler test");
		raise(SIGSEGV);
	}

	wl_display_run(server.wl_display);

	server_destroy_xwayland(&server);
	server_finish(&server);
	wl_display_destroy_clients(server.wl_display);
	wl_display_destroy(server.wl_display);
	return 0;
}
