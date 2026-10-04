#pragma once

#include <stdbool.h>

struct comp_server;

void output_mgmt_update_config(struct comp_server *server);
bool output_mgmt_init(struct comp_server *server);
void output_mgmt_fini(struct comp_server *server);
