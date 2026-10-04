#pragma once

#include <stdbool.h>

struct comp_server;

bool gamma_control_init(struct comp_server *server);
void gamma_control_fini(struct comp_server *server);
