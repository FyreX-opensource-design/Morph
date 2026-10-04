#include "output_mgmt.h"

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"

static bool output_mgmt_apply_head(struct comp_server *server, const struct wlr_output_head_v1_state *head_state,
								   bool test_only)
{
	struct wlr_output *output = head_state->output;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_head_v1_state_apply(head_state, &state);

	bool ok;
	if (test_only)
	{
		ok = wlr_output_test_state(output, &state);
	}
	else
	{
		ok = wlr_output_commit_state(output, &state);
		if (ok && head_state->enabled)
		{
			wlr_output_layout_add(server->output_layout, output, head_state->x, head_state->y);
		}
	}
	wlr_output_state_finish(&state);
	return ok;
}

static void output_mgmt_configure(struct comp_server *server, struct wlr_output_configuration_v1 *config, bool test_only)
{
	bool ok = true;
	struct wlr_output_configuration_head_v1 *head;

	wl_list_for_each(head, &config->heads, link)
	{
		if (!head->state.enabled)
		{
			ok &= output_mgmt_apply_head(server, &head->state, test_only);
		}
	}
	wl_list_for_each(head, &config->heads, link)
	{
		if (head->state.enabled)
		{
			ok &= output_mgmt_apply_head(server, &head->state, test_only);
		}
	}

	if (ok)
	{
		wlr_output_configuration_v1_send_succeeded(config);
	}
	else
	{
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);

	if (!test_only && ok)
	{
		output_mgmt_update_config(server);
	}
}

static void handle_output_manager_apply(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, output_manager_apply);
	output_mgmt_configure(server, data, false);
}

static void handle_output_manager_test(struct wl_listener *listener, void *data)
{
	struct comp_server *server = wl_container_of(listener, server, output_manager_test);
	output_mgmt_configure(server, data, true);
}

void output_mgmt_update_config(struct comp_server *server)
{
	if (!server->output_manager)
	{
		return;
	}

	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (!config)
	{
		wlr_log(WLR_ERROR, "output-mgmt: failed to allocate configuration");
		return;
	}

	struct comp_output *output;
	wl_list_for_each(output, &server->outputs, link)
	{
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, output->wlr_output);
		if (!head)
		{
			wlr_log(WLR_ERROR, "output-mgmt: failed to create configuration head");
			wlr_output_configuration_v1_destroy(config);
			return;
		}
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		head->state.enabled = output->wlr_output->enabled;
		head->state.x = box.x;
		head->state.y = box.y;
	}

	wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

bool output_mgmt_init(struct comp_server *server)
{
	server->output_manager = wlr_output_manager_v1_create(server->wl_display);
	if (!server->output_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_output_manager_v1");
		return false;
	}
	server->output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&server->output_manager->events.apply, &server->output_manager_apply);
	server->output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&server->output_manager->events.test, &server->output_manager_test);
	output_mgmt_update_config(server);
	return true;
}

void output_mgmt_fini(struct comp_server *server)
{
	if (!server || !server->output_manager)
	{
		return;
	}
	wl_list_remove(&server->output_manager_apply.link);
	wl_list_remove(&server->output_manager_test.link);
}
