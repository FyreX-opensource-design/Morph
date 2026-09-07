#include "virtual_input.h"

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"

static void handle_new_virtual_pointer(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
	struct wlr_pointer *wlr_pointer = &pointer->pointer;

	if (event->suggested_output != NULL)
	{
		wlr_pointer->output_name = strdup(event->suggested_output->name);
	}

	wlr_cursor_attach_input_device(server->cursor, &wlr_pointer->base);
	if (event->suggested_output != NULL)
	{
		wlr_cursor_map_input_to_output(server->cursor, &wlr_pointer->base, event->suggested_output);
	}
	server_update_seat_capabilities(server);
}

static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *keyboard = data;
	struct wlr_keyboard *wlr_kbd = &keyboard->keyboard;

	server_keyboard_register(server, wlr_kbd);
	server_update_seat_capabilities(server);
}

bool virtual_input_init(struct comp_server *server)
{
	server->virtual_pointer_manager = wlr_virtual_pointer_manager_v1_create(server->wl_display);
	if (!server->virtual_pointer_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_virtual_pointer_manager_v1");
		return false;
	}
	server->new_virtual_pointer.notify = handle_new_virtual_pointer;
	wl_signal_add(&server->virtual_pointer_manager->events.new_virtual_pointer, &server->new_virtual_pointer);

	server->virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(server->wl_display);
	if (!server->virtual_keyboard_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_virtual_keyboard_manager_v1");
		return false;
	}
	server->new_virtual_keyboard.notify = handle_new_virtual_keyboard;
	wl_signal_add(&server->virtual_keyboard_manager->events.new_virtual_keyboard, &server->new_virtual_keyboard);
	return true;
}

void virtual_input_fini(struct comp_server *server)
{
	if (!server)
	{
		return;
	}
	if (server->virtual_pointer_manager)
	{
		wl_list_remove(&server->new_virtual_pointer.link);
	}
	if (server->virtual_keyboard_manager)
	{
		wl_list_remove(&server->new_virtual_keyboard.link);
	}
}
