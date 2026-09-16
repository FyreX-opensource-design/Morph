#include "image_capture.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"

static void handle_toplevel_capture_source_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct comp_toplevel *view = wl_container_of(listener, view, image_capture_source_destroy);
	wl_list_remove(&view->image_capture_source_destroy.link);
	wl_list_init(&view->image_capture_source_destroy.link);
	view->image_capture_source = NULL;
}

static void handle_toplevel_capture_request(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, image_capture_new_request);
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
	struct comp_toplevel *view = request->toplevel_handle ? request->toplevel_handle->data : NULL;

	if (!view || !view->scene_tree)
	{
		wlr_log(WLR_ERROR, "image-capture: request for unknown or destroyed toplevel");
		return;
	}

	if (!view->image_capture_source)
	{
		struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
		view->image_capture_source = wlr_ext_image_capture_source_v1_create_with_scene_node(
			&view->scene_tree->node, loop, server->allocator, server->renderer);
		if (!view->image_capture_source)
		{
			wlr_log(WLR_ERROR, "image-capture: failed to create scene-node source");
			return;
		}
		view->image_capture_source_destroy.notify = handle_toplevel_capture_source_destroy;
		wl_signal_add(&view->image_capture_source->events.destroy, &view->image_capture_source_destroy);
	}

	if (!wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
			request, view->image_capture_source))
	{
		wlr_log(WLR_ERROR, "image-capture: failed to accept capture request");
	}
}

bool image_capture_init(struct comp_server *server)
{
	wl_list_init(&server->image_capture_new_request.link);

	server->ext_image_copy_capture_manager =
		wlr_ext_image_copy_capture_manager_v1_create(server->wl_display, 1);
	if (!server->ext_image_copy_capture_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_ext_image_copy_capture_manager_v1");
		return false;
	}

	if (!wlr_ext_output_image_capture_source_manager_v1_create(server->wl_display, 1))
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_ext_output_image_capture_source_manager_v1");
		return false;
	}

	server->ext_foreign_toplevel_list =
		wlr_ext_foreign_toplevel_list_v1_create(server->wl_display, 1);
	if (!server->ext_foreign_toplevel_list)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_ext_foreign_toplevel_list_v1");
		return false;
	}

	server->ext_foreign_toplevel_image_capture_manager =
		wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(server->wl_display, 1);
	if (!server->ext_foreign_toplevel_image_capture_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create foreign-toplevel image capture source manager");
		return false;
	}

	server->image_capture_new_request.notify = handle_toplevel_capture_request;
	wl_signal_add(&server->ext_foreign_toplevel_image_capture_manager->events.new_request,
				  &server->image_capture_new_request);
	return true;
}

void image_capture_toplevel_create(struct comp_toplevel *view)
{
	struct comp_server *server = view->server;
	wl_list_init(&view->image_capture_source_destroy.link);
	view->image_capture_source = NULL;
	view->ext_foreign_toplevel = NULL;

	if (!server->ext_foreign_toplevel_list)
	{
		return;
	}

	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = "",
		.app_id = "",
	};
	view->ext_foreign_toplevel =
		wlr_ext_foreign_toplevel_handle_v1_create(server->ext_foreign_toplevel_list, &state);
	if (!view->ext_foreign_toplevel)
	{
		wlr_log(WLR_ERROR, "Failed to create ext_foreign_toplevel_handle_v1");
		return;
	}
	view->ext_foreign_toplevel->data = view;
}

void image_capture_toplevel_refresh(struct comp_toplevel *view)
{
	if (!view || !view->ext_foreign_toplevel)
	{
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view->foreign_title ? view->foreign_title : "",
		.app_id = view->foreign_app_id ? view->foreign_app_id : "",
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(view->ext_foreign_toplevel, &state);
}

void image_capture_toplevel_destroy(struct comp_toplevel *view)
{
	if (!view)
	{
		return;
	}
	if (view->image_capture_source_destroy.link.prev)
	{
		wl_list_remove(&view->image_capture_source_destroy.link);
		wl_list_init(&view->image_capture_source_destroy.link);
	}
	view->image_capture_source = NULL;
	if (view->ext_foreign_toplevel)
	{
		view->ext_foreign_toplevel->data = NULL;
		wlr_ext_foreign_toplevel_handle_v1_destroy(view->ext_foreign_toplevel);
		view->ext_foreign_toplevel = NULL;
	}
}
