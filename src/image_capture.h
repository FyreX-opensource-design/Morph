#pragma once

#include <stdbool.h>

struct comp_server;
struct comp_toplevel;

bool image_capture_init(struct comp_server *server);

/** Create ext-foreign-toplevel handle for window capture clients. */
void image_capture_toplevel_create(struct comp_toplevel *view);

/** Sync title/app_id onto the ext-foreign-toplevel handle. */
void image_capture_toplevel_refresh(struct comp_toplevel *view);

/** Tear down capture source + ext-foreign-toplevel handle. */
void image_capture_toplevel_destroy(struct comp_toplevel *view);
