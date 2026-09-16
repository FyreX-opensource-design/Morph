#pragma once

#include <stdbool.h>

struct comp_server;

bool virtual_input_init(struct comp_server *server);
void virtual_input_fini(struct comp_server *server);
