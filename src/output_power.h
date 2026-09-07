#pragma once

#include <stdbool.h>

struct comp_server;

bool output_power_init(struct comp_server *server);
void output_power_fini(struct comp_server *server);
