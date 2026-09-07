#include "gamma_control.h"

#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"

bool gamma_control_init(struct comp_server *server)
{
	server->gamma_control_manager = wlr_gamma_control_manager_v1_create(server->wl_display);
	if (!server->gamma_control_manager)
	{
		wlr_log(WLR_ERROR, "Failed to create wlr_gamma_control_manager_v1");
		return false;
	}
	wlr_scene_set_gamma_control_manager_v1(server->scene, server->gamma_control_manager);
	return true;
}

void gamma_control_fini(struct comp_server *server)
{
	(void)server;
}
