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
    char tmpl[] = "/tmp/morph-config-test-XXXXXX";
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
        "strip = no\n"
        "\n"
        "[focus]\n"
        "policy = FocusFollowsMouse\n";

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

    if (cfg->focus_policy != COMP_FOCUS_FOLLOWS_MOUSE)
    {
        fprintf(stderr, "focus policy should parse FocusFollowsMouse\n");
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
    fprintf(stderr, "NOTE: the following config parser ERROR messages are expected; this test intentionally feeds invalid config.\n");

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
 * Missing config file should hard-fail when an explicit path was requested.
 */
static int test_missing_config_fails(void)
{
    fprintf(stderr, "NOTE: the following missing-config ERROR message is expected; this test verifies hard-fail behavior.\n");

    struct comp_config *cfg = NULL;
    unsetenv("MORPH_ALLOW_BUILTIN_FALLBACK");
    bool ok = comp_config_load("/tmp/morph-config-this-file-does-not-exist", &cfg);
    if (ok)
    {
        fprintf(stderr, "missing config should fail instead of falling back silently\n");
        comp_config_free(cfg);
        return 1;
    }
    return 0;
}

/**
 * Missing config file may fall back to builtin defaults when explicitly enabled.
 */
static int test_missing_config_can_use_builtin_fallback(void)
{
    fprintf(stderr, "NOTE: the following missing-config ERROR message is expected before builtin fallback is accepted.\n");

    struct comp_config *cfg = NULL;
    setenv("MORPH_ALLOW_BUILTIN_FALLBACK", "1", 1);
    bool ok = comp_config_load("/tmp/morph-config-this-file-does-not-exist", &cfg);
    unsetenv("MORPH_ALLOW_BUILTIN_FALLBACK");
    if (!ok || !cfg)
    {
        fprintf(stderr, "missing config should use builtin fallback when enabled\n");
        return 1;
    }
    if (cfg->n_binds == 0)
    {
        fprintf(stderr, "builtin fallback should provide default binds\n");
        comp_config_free(cfg);
        return 1;
    }
    comp_config_free(cfg);
    return 0;
}

/** Missing default config path must use the same explicit fallback as a missing file path. */
static int test_unresolved_config_path_can_use_builtin_fallback(void)
{
    struct comp_config *cfg = NULL;
    setenv("MORPH_ALLOW_BUILTIN_FALLBACK", "1", 1);
    bool ok = comp_config_load(NULL, &cfg);
    unsetenv("MORPH_ALLOW_BUILTIN_FALLBACK");
    if (!ok || !cfg || cfg->n_binds != 4 ||
        cfg->binds[0].mods != WLR_MODIFIER_LOGO || cfg->binds[0].keysym != XKB_KEY_Return ||
        cfg->binds[0].action != COMP_KEYBIND_EXEC || strcmp(cfg->binds[0].command, "${TERMINAL:-foot}") != 0 ||
        cfg->binds[1].mods != (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT) || cfg->binds[1].keysym != XKB_KEY_Q ||
        cfg->binds[1].action != COMP_KEYBIND_CLOSE || cfg->binds[2].mods != WLR_MODIFIER_LOGO ||
        cfg->binds[2].keysym != XKB_KEY_Escape || cfg->binds[2].action != COMP_KEYBIND_QUIT ||
        cfg->binds[3].mods != WLR_MODIFIER_LOGO || cfg->binds[3].keysym != XKB_KEY_t ||
        cfg->binds[3].action != COMP_KEYBIND_LAYOUT_TOGGLE)
    {
        fprintf(stderr, "unresolved config path should use all builtin default binds when enabled\n");
        comp_config_free(cfg);
        return 1;
    }
    comp_config_free(cfg);
    return 0;
}

/**
 * Focus policy parsing: defaults, aliases, and rejection of unknown values.
 */
static int test_focus_policy_parse(void)
{
    const char *cfg_sloppy =
        "[bind]\n"
        "mods = Super\n"
        "key = Escape\n"
        "action = quit\n"
        "\n"
        "[focus]\n"
        "mode = sloppy\n";

    char path[128];
    if (!write_temp_file(cfg_sloppy, path, sizeof(path)))
    {
        fprintf(stderr, "failed to create temp focus config\n");
        return 1;
    }

    struct comp_config *cfg = NULL;
    if (!comp_config_load(path, &cfg) || !cfg || cfg->focus_policy != COMP_FOCUS_SLOPPY)
    {
        fprintf(stderr, "sloppy focus alias should parse as SloppyFocus\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }
    comp_config_free(cfg);
    unlink(path);

    const char *cfg_default =
        "[bind]\n"
        "mods = Super\n"
        "key = Escape\n"
        "action = quit\n";
    if (!write_temp_file(cfg_default, path, sizeof(path)))
    {
        fprintf(stderr, "failed to create temp default-focus config\n");
        return 1;
    }
    cfg = NULL;
    if (!comp_config_load(path, &cfg) || !cfg || cfg->focus_policy != COMP_FOCUS_CLICK)
    {
        fprintf(stderr, "omitted [focus] should default to ClickToFocus\n");
        comp_config_free(cfg);
        unlink(path);
        return 1;
    }
    comp_config_free(cfg);
    unlink(path);

    enum comp_focus_policy pol = COMP_FOCUS_CLICK;
    if (!comp_config_parse_focus_policy("mouse", &pol) || pol != COMP_FOCUS_FOLLOWS_MOUSE)
    {
        fprintf(stderr, "mouse alias should parse as FocusFollowsMouse\n");
        return 1;
    }
    if (!comp_config_parse_focus_policy("sloppy", &pol) || pol != COMP_FOCUS_SLOPPY)
    {
        fprintf(stderr, "sloppy alias should parse as SloppyFocus\n");
        return 1;
    }

    fprintf(stderr, "NOTE: the following config parser ERROR messages are expected; this test intentionally feeds invalid focus policy.\n");
    const char *cfg_bad =
        "[focus]\n"
        "policy = NotARealPolicy\n";
    if (!write_temp_file(cfg_bad, path, sizeof(path)))
    {
        fprintf(stderr, "failed to create temp invalid focus config\n");
        return 1;
    }
    cfg = NULL;
    bool ok = comp_config_load(path, &cfg);
    unlink(path);
    if (ok)
    {
        fprintf(stderr, "invalid focus policy unexpectedly parsed\n");
        comp_config_free(cfg);
        return 1;
    }
    return 0;
}

/** Execute all config parser regression tests; return non-zero on first failure. */
int main(void)
{
    if (test_valid_config_parse() != 0)
    {
        return 1;
    }
    if (test_focus_policy_parse() != 0)
    {
        return 1;
    }
    if (test_invalid_tile_grid_command() != 0)
    {
        return 1;
    }
    if (test_missing_config_fails() != 0)
    {
        return 1;
    }
    if (test_missing_config_can_use_builtin_fallback() != 0)
    {
        return 1;
    }
    if (test_unresolved_config_path_can_use_builtin_fallback() != 0)
    {
        return 1;
    }
    return 0;
}
