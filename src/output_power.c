#include "output_power.h"

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"

static void handle_output_power_set_mode(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, output_power_set_mode);
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output *output = event->output;

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
	if (!wlr_output_commit_state(output, &state))
	{
		wlr_log(WLR_ERROR, "output-power: failed to commit %s", output->name);
	}
	wlr_output_state_finish(&state);
}

bool output_power_init(struct comp_server *server)
{
	server->output_power_manager = wlr_output_power_manager_v1_create(server->wl_display);
	if (!server->output_power_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_output_power_manager_v1");
		return false;
	}
	server->output_power_set_mode.notify = handle_output_power_set_mode;
	wl_signal_add(&server->output_power_manager->events.set_mode, &server->output_power_set_mode);
	return true;
}

void output_power_fini(struct comp_server *server)
{
	if (!server || !server->output_power_manager)
	{
		return;
	}
	wl_list_remove(&server->output_power_set_mode.link);
}
