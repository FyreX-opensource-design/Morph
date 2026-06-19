#pragma once

#include <stdbool.h>

/**
 * Install a minimal async-signal-safe fatal crash handler.
 *
 * On fatal signals, the handler writes a short crash marker to stderr and,
 * when configured, to the path opened via `log_path`.
 *
 * Parameters:
 * - log_path: Optional file path for crash marker output. When NULL or empty,
 *   only stderr is used.
 *
 * Returns:
 * - true when all handler resources were initialized and signals were
 *   installed successfully.
 * - false when initialization failed (for example open/malloc/sigaltstack).
 */
bool stackcomp_crash_handler_install(const char *log_path);

/**
 * Release resources owned by the crash handler in normal shutdown paths.
 *
 * Safe to call multiple times; subsequent calls become no-ops.
 */
void stackcomp_crash_handler_fini(void);
