#pragma once

#include <stdbool.h>

/**
 * Install a minimal async-signal-safe fatal crash handler.
 *
 * On fatal signals, the handler writes a short crash marker to stderr and,
 * when configured, to the path opened via `log_path`.
 */
bool stackcomp_crash_handler_install(const char *log_path);

/**
 * Release resources owned by the crash handler in normal shutdown paths.
 */
void stackcomp_crash_handler_fini(void);
