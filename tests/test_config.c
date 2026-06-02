#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/box.h>

#include "config.h"
#include "server.h"

/* Stubs for server actions referenced by comp_config_try_bindings in config.c. */
void server_set_layout(struct comp_server *server, enum comp_layout layout)
{
    (void)server;
    (void)layout;
}

void server_toggle_layout(struct comp_server *server)
{
    (void)server;
}

void server_arrange_toplevels(struct comp_server *server)
{
    (void)server;
}

void server_workspace_apply_visibility(struct comp_server *server)
{
    (void)server;
}

void server_workspace_go(struct comp_server *server, int idx)
{
    (void)server;
    (void)idx;
}

void server_workspace_relative(struct comp_server *server, int delta)
{
    (void)server;
    (void)delta;
}

void server_workspace_move_focused(struct comp_server *server, int target)
{
    (void)server;
    (void)target;
}

void server_tile_move_focused_n(struct comp_server *server, int steps)
{
    (void)server;
    (void)steps;
}

void server_tile_move_focused_edge(struct comp_server *server, bool to_first)
{
    (void)server;
    (void)to_first;
}

void server_scroll_move(struct comp_server *server, int steps)
{
    (void)server;
    (void)steps;
}

void server_tile_move_focused_grid_vert(struct comp_server *server, int steps)
{
    (void)server;
    (void)steps;
}

void server_tile_move_focused_grid_vert_edge(struct comp_server *server, bool to_top)
{
    (void)server;
    (void)to_top;
}

void server_tile_move_focused_grid_horiz(struct comp_server *server, int steps)
{
    (void)server;
    (void)steps;
}

void server_tile_grid_run_command(struct comp_server *server, const char *cmd)
{
    (void)server;
    (void)cmd;
}

void server_sync_xdg_decorations(struct comp_server *server)
{
    (void)server;
}

bool server_init(struct comp_server *server)
{
    (void)server;
    return true;
}

/**
 * Write `content` to a unique temporary file and return its path.
 *
 * Caller owns cleanup of the resulting file path (unlink when done).
 */
static bool write_temp_file(const char *content, char *out_path, size_t out_len)
{
    char tmpl[] = "/tmp/stackcomp-config-test-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
    {
        return false;
    }
    size_t len = strlen(content);
    ssize_t w = write(fd, content, len);
    close(fd);
    if (w != (ssize_t)len)
    {
        unlink(tmpl);
        return false;
    }
    if (snprintf(out_path, out_len, "%s", tmpl) >= (int)out_len)
    {
        unlink(tmpl);
        return false;
    }
    return true;
}

/**
 * Happy-path config parsing test.
 *
 * Verifies explicit bind/rule parsing and selected default behavior fallbacks.
 */
static int test_valid_config_parse(void)
{
    const char *cfg_text =
        "[bind]\n"
        "mods = Super\n"
        "key = Return\n"
        "action = exec\n"
        "command = foot\n"
        "\n"
        "[tile_rule]\n"
        "app_id = ^mpv$\n"
        "mode = float\n"
        "order = -5\n"
        "\n"
        "[layout_anim]\n"
        "enabled = no\n"
        "lambda = 20\n"
        "epsilon = 0.5\n"
        "\n"
        "[decoration_rule]\n"
        "app_id = ^foot$\n"
        "strip = no\n";

    char path[128];
    if (!write_temp_file(cfg_text, path, sizeof(path)))
    {
        fprintf(stderr, "failed to create temp config\n");
        return 1;
    }

    /* Parse from disk to exercise the same loader path used in production. */
    struct comp_config *cfg = NULL;
    if (!comp_config_load(path, &cfg))
    {
        fprintf(stderr, "valid config parse failed\n");
        unlink(path);
        return 1;
    }

    if (!cfg || cfg->n_binds != 1)
    {
        fprintf(stderr, "expected exactly one bind from test config\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }

    if (cfg->layout_anim_enabled || cfg->layout_anim_lambda != 20.0 || cfg->layout_anim_epsilon != 0.5)
    {
        fprintf(stderr, "layout_anim settings mismatch\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }

    /* Verify that one matching tile_rule mutates float/order as expected. */
    bool float_mode = false;
    int order = 0;
    comp_config_tile_props_for_toplevel(cfg, "mpv", "video", &float_mode, &order);
    if (!float_mode || order != -5)
    {
        fprintf(stderr, "tile_rule resolution mismatch\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }

    /* strip=no means keep client-side decorations for matching windows. */
    if (comp_config_decoration_prefer_server_side_tile_scroll(cfg, "foot", "terminal"))
    {
        fprintf(stderr, "decoration_rule strip=no should keep client-side mode\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }

    if (!comp_config_decoration_prefer_server_side_tile_scroll(cfg, "other", "window"))
    {
        fprintf(stderr, "default decoration behavior should prefer server-side in tile/scroll\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }

    comp_config_free(cfg);
    unlink(path);
    return 0;
}

/**
 * Negative parsing test for tile_grid_move validation.
 *
 * `left 0` is invalid because directional counts must be >= 1.
 */
static int test_invalid_tile_grid_command(void)
{
    const char *cfg_text =
        "[bind]\n"
        "mods = Super\n"
        "key = T\n"
        "action = tile_grid_move\n"
        "command = left 0\n";

    char path[128];
    if (!write_temp_file(cfg_text, path, sizeof(path)))
    {
        fprintf(stderr, "failed to create temp invalid config\n");
        return 1;
    }

    struct comp_config *cfg = NULL;
    bool ok = comp_config_load(path, &cfg);
    unlink(path);
    if (ok)
    {
        fprintf(stderr, "invalid config unexpectedly parsed\n");
        comp_config_free(cfg);
        return 1;
    }
    return 0;
}

/**
 * Missing config file should not hard-fail startup.
 *
 * Loader is expected to synthesize defaults when the file is absent.
 */
static int test_missing_config_falls_back_to_defaults(void)
{
    struct comp_config *cfg = NULL;
    bool ok = comp_config_load("/tmp/stackcomp-config-this-file-does-not-exist", &cfg);
    if (!ok || !cfg)
    {
        fprintf(stderr, "missing config should fall back to defaults\n");
        return 1;
    }
    if (cfg->n_binds == 0)
    {
        fprintf(stderr, "default config should provide built-in binds\n");
        comp_config_free(cfg);
        return 1;
    }
    comp_config_free(cfg);
    return 0;
}

/** Execute all config parser regression tests; return non-zero on first failure. */
int main(void)
{
    if (test_valid_config_parse() != 0)
    {
        return 1;
    }
    if (test_invalid_tile_grid_command() != 0)
    {
        return 1;
    }
    if (test_missing_config_falls_back_to_defaults() != 0)
    {
        return 1;
    }
    return 0;
}
