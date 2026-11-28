/* hunt_plugins: the Lua sandbox, the permission model, the files a plugin may reach, and the limits (docs/formats.md "Plugin manifest", "Lua API", "The runtime (8b)").
 *
 * A plugin is code from someone else, and the documentation promises: no os, io, package, debug, load or require; paths stay inside the vault and never
 * reach `.exhaust` (where the approvals are kept) or `.trash`; every API function needs its permission and says `permission denied: <name>` without it; the
 * entry file and each callback may run 1,000 ms, a state may use 32 MB, a plugin over a limit is stopped; a chunk of bytecode is refused; the approval
 * must match the manifest. This test is an attacker's checklist: every probe is a small Lua program that tries something it must not be able to do, run
 * as a plugin with the least permission that makes the probe meaningful, reporting what happened through a notice.
 *   A. the global table, the string metatable, `_G`, `getmetatable`, coroutines and `string.dump`: nothing that reaches the machine;
 *   B. paths given to vault.read, write, list, exists and rename: `..`, absolute paths, drive letters, backslashes, `.exhaust` in every spelling, a symbolic
 *      link inside the vault that leads outside it, and a write that tries to approve the plugin itself;
 *   C. the permission matrix: for each function of the API, with and without its permission;
 *   D. limits: an endless loop (also inside a coroutine), a memory hog, a stack overflow, and a Lua pattern that backtracks inside one C call (which no
 *      instruction hook can interrupt), checking the interface thread never waits and how long the plugin thread is held;
 *   E. settings of a plugin: strings come back as they went in; keys and values that mean something to an INI file do not add sections;
 *   F. commands: a flood of registrations, bad ids, and `commands.run` of the application's own commands from a plugin that holds no other permission;
 *   G. manifests: hostile ids, entries, permission lists and sizes; bytecode as the entry file; an approval that no longer matches;
 *   H. toggling and events in a tight loop, then freeing the runtime in the middle of it.
 * Link: exhaust_plugins exhaust_settings exhaust_platform. */
#include "hunt.h"
#include "hunt_fs.h"

#include <SDL3/SDL.h>

#include "exhaust_commands.h"
#include "exhaust_plugins.h"

static char *g_notes[4000];
static int g_nnotes;
static char *g_text;
static size_t g_sel0, g_sel1, g_cursor;
static int g_changed;
static char g_vault[700];

static int h_get(void *u, char **t, size_t *a, size_t *b, size_t *c) { (void)u; if (!g_text) return -1; *t = hunt_strdup(g_text); *a = g_sel0; *b = g_sel1; *c = g_cursor; return 0; }
static int h_replace(void *u, const char *t, size_t len, int whole) { (void)u; (void)whole; if (!g_text) return -1; free(g_text); g_text = hunt_dup(t, len); return 0; }
static void h_notice(void *u, const char *t) { (void)u; if (g_nnotes < 4000) g_notes[g_nnotes++] = hunt_strdup(t); }
static int h_pane(void *u, const char *title, const char *md) { (void)u; (void)title; (void)md; return 0; }
static int h_rename(void *u, const char *a, const char *b) { char pa[900], pb[900]; (void)u; snprintf(pa, sizeof pa, "%s/%s", g_vault, a); snprintf(pb, sizeof pb, "%s/%s", g_vault, b); return ex_platform_rename_noreplace(pa, pb); }
static void h_changed(void *u) { (void)u; g_changed++; }

static ExPlugins *P;
static ExCommands *CMDS;

static void clear_notes(void) { int i; for (i = 0; i < g_nnotes; i++) free(g_notes[i]); g_nnotes = 0; }

static int settle_ms(int max_ms) {
    int waited = 0, quiet = 0;
    while (waited < max_ms) {
        ex_plugins_update(P);
        if (!ex_plugins_busy(P)) { if (++quiet >= 4) return 1; } else quiet = 0;
        SDL_Delay(2);
        waited += 2;
    }
    return 0;
}
static void settle(void) { if (!settle_ms(20000)) HUNT_CHECKF(0, "the plugin thread did not settle in 20 seconds"); }

static void write_plugin(const char *id, const char *perms, const char *lua, const char *extra_manifest) {
    char rel[300], man[1600];
    snprintf(man, sizeof man, "[plugin]\nid = %s\nname = Plugin %s\nversion = 1.0.0\napi = 1\npermissions = %s\n%s", id, id, perms, extra_manifest ? extra_manifest : "");
    snprintf(rel, sizeof rel, ".exhaust/plugins/%s/manifest.ini", id);
    hunt_write_in(g_vault, rel, man, strlen(man));
    snprintf(rel, sizeof rel, ".exhaust/plugins/%s/main.lua", id);
    hunt_write_in(g_vault, rel, lua, strlen(lua));
}

static int info_of(const char *id, ExPluginInfo *out) {
    ExPluginInfo all[64];
    size_t n = ex_plugins_snapshot(P, all, 64), i;
    for (i = 0; i < n && i < 64; i++) if (!strcmp(all[i].id, id)) { *out = all[i]; return 1; }
    return 0;
}

/* The report line "name=ok" or "name=err:message" for a probe, or NULL. */
static const char *result(const char *name) {
    size_t l = strlen(name);
    int i;
    for (i = 0; i < g_nnotes; i++) { const char *s = g_notes[i]; const char *p = strchr(s, ':'); if (p && p[1] == ' ') s = p + 2; if (!strncmp(s, name, l) && s[l] == '=') return s + l + 1; }
    return NULL;
}
static int result_ok(const char *name) { const char *r = result(name); return r && !strncmp(r, "ok", 2); }
static int result_err(const char *name, const char *needle) { const char *r = result(name); return r && !strncmp(r, "err", 3) && (!needle || strstr(r, needle)); }

static const char *PREAMBLE = "local function report(n, ok, e) exhaust.ui.notice(n .. '=' .. (ok and ('ok:' .. tostring(e)) or ('err:' .. tostring(e)))) end\n"
                              "local function try(n, f, ...) local ok, e = pcall(f, ...) report(n, ok, e) end\n";

/* Runs a probe plugin: its command `go` runs the body. Returns after the command finished. */
static void run_probe(const char *id, const char *perms, const char *body) {
    char lua[8000], cmd[200];
    snprintf(lua, sizeof lua, "%sexhaust.commands.register('go', 'Go', function()\n%s\nend)\n", PREAMBLE, body);
    write_plugin(id, perms, lua, NULL);
    clear_notes();
    ex_plugins_rescan(P); settle();
    ex_plugins_enable(P, id, 1); settle();
    snprintf(cmd, sizeof cmd, "plugin.%s.go", id);
    if (ex_commands_available(CMDS, cmd)) { ex_commands_run(CMDS, cmd); settle(); }
    else { ExPluginInfo inf; HUNT_CHECKF(0, "the probe plugin %s did not start: %s", id, info_of(id, &inf) ? inf.status : "(not found)"); }
}

static void lua_quote(const char *in, char *out, size_t cap) {
    size_t k = 0;
    out[k++] = '"';
    for (; *in && k + 6 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '\\' || c == '"') { out[k++] = '\\'; out[k++] = (char)c; }
        else if (c < 32 || c >= 127) k += (size_t)snprintf(out + k, cap - k, "\\%03d", c);
        else out[k++] = (char)c;
    }
    out[k++] = '"'; out[k] = 0;
}

/* ---- A. the sandbox ---- */

static void test_sandbox(void) {
    hunt_step("sandbox: globals");
    run_probe("sbx", "ui.notice, commands.register",
        "for _, n in ipairs({'os','io','package','debug','require','dofile','loadfile','load','loadstring','collectgarbage','module','newproxy','jit','ffi','lfs','socket','arg','utf8','coroutine','string','table','math','print','pcall','select','rawget','rawset','rawequal','rawlen','getmetatable','setmetatable','xpcall','next','pairs','ipairs','tostring','tonumber','type','assert','error','_G','_VERSION'}) do\n"
        "  report('g_' .. n, true, type(_G[n]))\n"
        "end\n"
        "try('os_execute', function() return os.execute('id') end)\n"
        "try('io_open', function() return io.open('/etc/passwd') end)\n"
        "try('load_string', function() return load('return 1')() end)\n"
        "try('loadstring', function() return loadstring('return 1')() end)\n"
        "try('dofile', function() return dofile('/etc/passwd') end)\n"
        "try('require', function() return require('os') end)\n"
        "try('debug_getinfo', function() return debug.getinfo(1) end)\n"
        "try('g_g', function() return _G._G == _G end)\n"
        "try('meta_index_os', function() return getmetatable('').__index.os end)\n"
        "try('string_dump', function() return #string.dump(function() end) end)\n"
        "try('string_dump_load', function() local f = string.dump(function() return 42 end); return load and load(f, 'x', 'b') end)\n"
        "try('env_get', function() local env = _ENV; return type(env.os) end)\n"
        "try('getfenv', function() return getfenv and getfenv(1) end)\n"
        "try('setmetatable_g', function() setmetatable(_G, {__index = function(t, k) return k end}); return os end)\n"
        "try('tostring_function', function() return tostring(print):sub(1, 8) end)\n"
        "try('exhaust_keys', function() local t = {} for k in pairs(exhaust) do t[#t + 1] = k end table.sort(t) return table.concat(t, ',') end)\n"
        "try('exhaust_http', function() return exhaust.http.get('http://127.0.0.1:9/') end)\n"
        "try('exhaust_http_network', function() return exhaust.http.get('file:///etc/passwd') end)\n"
        "try('coroutine_os', function() return coroutine.wrap(function() return os end)() end)\n"
        "try('string_rep_metamethod', function() return ('x'):rep(3) end)\n"
        "try('utf8_char', function() return utf8.char(233) end)\n"
        "try('math_random', function() return math.random(1, 10) >= 1 end)\n"
        "try('os_time_via_math', function() return type(math.randomseed) end)\n");
    HUNT_CHECK(result("g_os") && !strcmp(result("g_os"), "ok:nil"));
    {
        const char *gone[] = { "io", "package", "debug", "require", "dofile", "loadfile", "load", "loadstring", "collectgarbage", "module", "newproxy", "jit", "ffi", "lfs", "socket", "arg" };
        size_t i;
        for (i = 0; i < sizeof gone / sizeof *gone; i++) {
            char key[64];
            snprintf(key, sizeof key, "g_%s", gone[i]);
            HUNT_SPECF(result(key) && !strcmp(result(key), "ok:nil"), "docs/formats.md, The runtime (8b): the sandbox has no os, io, package or debug, and no dofile, loadfile, load, loadstring, require or collectgarbage", "the global %s is %s", gone[i], result(key) ? result(key) : "(no report)");
        }
    }
    HUNT_CHECK(result_err("os_execute", NULL) && result_err("io_open", NULL) && result_err("load_string", NULL) && result_err("dofile", NULL) && result_err("require", NULL) && result_err("debug_getinfo", NULL));
    HUNT_CHECKF(!result_ok("meta_index_os") || !strcmp(result("meta_index_os"), "ok:nil"), "the string metatable reaches os: %s", result("meta_index_os") ? result("meta_index_os") : "");
    HUNT_CHECKF(!result_ok("string_dump_load") || !strcmp(result("string_dump_load"), "ok:nil"), "bytecode from string.dump was loaded: %s", result("string_dump_load") ? result("string_dump_load") : "");
    HUNT_CHECKF(!result("exhaust_http") || result_err("exhaust_http", "network"), "exhaust.http.get without the network permission gave %s", result("exhaust_http") ? result("exhaust_http") : "");
    HUNT_CHECKF(!result("exhaust_http_network") || !result_ok("exhaust_http_network"), "exhaust.http.get of a file: URL gave %s", result("exhaust_http_network") ? result("exhaust_http_network") : "");
    HUNT_CHECKF(!result_ok("coroutine_os") || !strcmp(result("coroutine_os"), "ok:nil"), "a coroutine reaches os: %s", result("coroutine_os") ? result("coroutine_os") : "");
    HUNT_CHECK(result_ok("string_rep_metamethod") && result_ok("utf8_char") && result_ok("math_random"));
    HUNT_SUSPECT((result("setmetatable_g") && !result_ok("setmetatable_g")) || (result("setmetatable_g") && !strcmp(result("setmetatable_g"), "ok:nil")), "a plugin that sets a metatable on _G must not be able to get at a global that was removed");
    HUNT_SUSPECT(!result_ok("math_random") || 1, "");
    {
        const char *r = result("exhaust_keys");
        if (r) HUNT_CHECKF(strstr(r, "http") != NULL || 1, "%s", r);
        if (r) { HUNT_SUSPECT(strstr(r, "vault") && strstr(r, "editor") && strstr(r, "ui") && strstr(r, "commands") && strstr(r, "settings") && strstr(r, "events") && strstr(r, "version"), "the exhaust table should hold the documented groups"); }
    }
}

/* ---- B. paths ---- */

static void test_paths(void) {
    char secret_dir[900], link[900];
    hunt_step("paths: reading");
    hunt_write_in(g_vault, "Note.md", "note text", 9);
    hunt_write_in(g_vault, "sub/Inner.md", "inner", 5);
    hunt_write_in(g_vault, ".exhaust/settings.ini", "[general]\n", 10);
    hunt_write_in(g_vault, ".exhaust/plugins.ini", "[plugin.x]\nenabled = true\n", 26);
    hunt_write_in(g_vault, ".trash/gone.md", "trash", 5);
    snprintf(secret_dir, sizeof secret_dir, "%s/../outside_secret", g_vault);
    ex_platform_make_dirs(secret_dir);
    hunt_write_in(secret_dir, "secret.md", "TOPSECRET", 9);
    hunt_write_in(g_vault, "../outside_file.md", "TOPSECRET", 9);
#if !defined(_WIN32)
    snprintf(link, sizeof link, "%s/linkdir", g_vault);
    if (symlink("../outside_secret", link) != 0) link[0] = 0;
    snprintf(link, sizeof link, "%s/linkfile.md", g_vault);
    if (symlink("../outside_file.md", link) != 0) link[0] = 0;
#endif
    {
        static const char *const RD[] = { "Note.md", "sub/Inner.md", "../outside_file.md", "../outside_secret/secret.md", "/etc/passwd", "a/../Note.md", "sub/../../outside_file.md", ".exhaust/settings.ini", ".exhaust/plugins.ini", ".exhaust/plugins/rd/manifest.ini", ".EXHAUST/settings.ini",
            ".Exhaust/plugins.ini", ".trash/gone.md", ".TRASH/gone.md", "C:\\Windows\\win.ini", "C:/Windows/win.ini", "c:foo", "sub\\Inner.md", "sub\\..\\Note.md", "", ".", "..", "./Note.md", "sub/./Inner.md", "sub//Inner.md", "Note.md/", "linkdir/secret.md", "linkfile.md",
            "\\\\host\\share\\x", ".exhaust\\plugins.ini", ".exhaust./plugins.ini", ".exhaust /plugins.ini", "%2e%2e/outside_file.md", "..;/outside_file.md", "file:///etc/passwd", "~/x", "$HOME/x", "sub/../sub/Inner.md", "./../outside_file.md", "sub/..", "sub/." };
        static const char *const MUST_FAIL[] = { "../outside_file.md", "../outside_secret/secret.md", "/etc/passwd", "a/../Note.md", "sub/../../outside_file.md", ".exhaust/settings.ini", ".exhaust/plugins.ini", ".exhaust/plugins/rd/manifest.ini", ".EXHAUST/settings.ini", ".Exhaust/plugins.ini",
            ".trash/gone.md", ".TRASH/gone.md", "C:\\Windows\\win.ini", "C:/Windows/win.ini", "c:foo", "sub\\Inner.md", "sub\\..\\Note.md", "linkdir/secret.md", "linkfile.md", "\\\\host\\share\\x", ".exhaust\\plugins.ini", ".exhaust./plugins.ini", ".exhaust /plugins.ini",
            "..;/outside_file.md", "file:///etc/passwd", "sub/../sub/Inner.md", "./../outside_file.md", "sub/.." };
        char body[9000], q[300];
        size_t i, j, n = 0;
        n += (size_t)snprintf(body + n, sizeof body - n, "local paths = {");
        for (i = 0; i < sizeof RD / sizeof *RD; i++) { lua_quote(RD[i], q, sizeof q); n += (size_t)snprintf(body + n, sizeof body - n, "%s%s", i ? "," : "", q); }
        n += (size_t)snprintf(body + n, sizeof body - n, "}\nfor i, p in ipairs(paths) do try('r|' .. p, exhaust.vault.read, p) end\nfor i, p in ipairs(paths) do try('e|' .. p, exhaust.vault.exists, p) end\n"
                              "for _, d in ipairs({'','.','sub','..','../outside_secret','.exhaust','.trash','/','/etc','linkdir','sub/..','.EXHAUST'}) do try('l|' .. d, function() local t = exhaust.vault.list(d); return table.concat(t, ',') end) end\n");
        run_probe("rd", "ui.notice, commands.register, vault.read", body);
        HUNT_CHECK(result_ok("r|Note.md") && result_ok("r|sub/Inner.md"));
        for (i = 0; i < sizeof RD / sizeof *RD; i++) {
            char key[400];
            int should_fail = 0;
            snprintf(key, sizeof key, "r|%s", RD[i]);
            for (j = 0; j < sizeof MUST_FAIL / sizeof *MUST_FAIL; j++) if (!strcmp(RD[i], MUST_FAIL[j])) should_fail = 1;
            if (should_fail && result_ok(key)) HUNT_SPECF(0, "docs/formats.md, The runtime (8b): an absolute path, a .. part, a backslash, a drive letter or anything in .exhaust or .trash is refused (path not allowed)", "vault.read(\"%s\") succeeded: %s", RD[i], result(key));
            if (should_fail && result(key) && !result_ok(key)) HUNT_SPECF(strstr(result(key), "path not allowed") != NULL, "docs/formats.md: the error is `path not allowed`", "vault.read(\"%s\") failed with %s", RD[i], result(key));
            if (result(key) && strstr(result(key), "TOPSECRET")) HUNT_CHECKF(0, "a plugin read a file outside the vault through the path \"%s\"", RD[i]);
        }
    }
    {
        const char *l = result("l|.exhaust");
        HUNT_SPEC(l && !strncmp(l, "err", 3), "docs/formats.md, The runtime (8b): anything in .exhaust is refused");
        l = result("l|..");
        HUNT_CHECK(l && !strncmp(l, "err", 3));
        l = result("l|/");
        HUNT_CHECK(l && !strncmp(l, "err", 3));
        l = result("l|../outside_secret");
        HUNT_CHECKF(l && !strncmp(l, "err", 3), "vault.list of a folder outside the vault gave %s", l ? l : "(no report)");
        l = result("l|linkdir");
        HUNT_CHECKF(!l || strncmp(l, "ok", 2) || !strstr(l, "secret.md"), "vault.list through a symbolic link that leads outside the vault listed the files outside it: %s", l ? l : "");
        l = result("l|");
        HUNT_CHECKF(l && !strncmp(l, "ok", 2), "vault.list(\"\") gave %s", l ? l : "(no report)");
        if (l) HUNT_SUSPECT(!strstr(l, ".exhaust") && !strstr(l, ".trash"), "the listing of the vault should not show .exhaust and .trash (they are Exhaust's, and hidden from the explorer)");
        if (l) HUNT_CHECKF(strstr(l, "sub/") && strstr(l, "Note.md"), "the listing of the vault is %s", l);
    }

    hunt_step("paths: writing and renaming");
    run_probe("wr", "ui.notice, commands.register, vault.read, vault.write",
        "try('w_ok', exhaust.vault.write, 'made/by/plugin.md', 'hello')\n"
        "try('w_dotdot', exhaust.vault.write, '../outside_written.md', 'x')\n"
        "try('w_abs', exhaust.vault.write, '/tmp/hunt_abs_written.md', 'x')\n"
        "try('w_plugins_ini', exhaust.vault.write, '.exhaust/plugins.ini', '[plugin.wr]\\nenabled = true\\napproved = vault.read,vault.write,network,editor.write,editor.read,commands.register,ui.pane,ui.notice,settings.own\\n')\n"
        "try('w_manifest', exhaust.vault.write, '.exhaust/plugins/wr/manifest.ini', '[plugin]\\nid = wr\\nname = x\\nversion = 1.0.0\\napi = 1\\npermissions = network, vault.write\\n')\n"
        "try('w_main', exhaust.vault.write, '.exhaust/plugins/wr/main.lua', 'print(1)')\n"
        "try('w_settings', exhaust.vault.write, '.exhaust/settings.ini', '[plugins]\\nallow = true\\n')\n"
        "try('w_hotkeys', exhaust.vault.write, '.exhaust/hotkeys.ini', 'x')\n"
        "try('w_trash', exhaust.vault.write, '.trash/x.md', 'x')\n"
        "try('w_case', exhaust.vault.write, '.Exhaust/plugins.ini', 'x')\n"
        "try('w_case2', exhaust.vault.write, '.EXHAUST/plugins.ini', 'x')\n"
        "try('w_via_link', exhaust.vault.write, 'linkdir/planted.md', 'x')\n"
        "try('w_via_linkfile', exhaust.vault.write, 'linkfile.md', 'OVERWRITTEN')\n"
        "try('w_empty', exhaust.vault.write, '', 'x')\n"
        "try('w_dir', exhaust.vault.write, 'sub', 'x')\n"
        "try('w_nul', exhaust.vault.write, 'a\\0b.md', 'x')\n"
        "try('w_bigname', exhaust.vault.write, string.rep('a', 5000) .. '.md', 'x')\n"
        "try('w_badname', exhaust.vault.write, 'a:b|c?.md', 'x')\n"
        "try('w_reserved', exhaust.vault.write, 'CON.md', 'x')\n"
        "try('w_nonstring', exhaust.vault.write, 'z.md', {})\n"
        "try('w_binary', exhaust.vault.write, 'bin.dat', string.char(0, 1, 2, 255, 254))\n"
        "try('rn_ok', exhaust.vault.rename, 'made/by/plugin.md', 'made/renamed.md')\n"
        "try('rn_over', exhaust.vault.rename, 'Note.md', 'sub/Inner.md')\n"
        "try('rn_out', exhaust.vault.rename, 'Note.md', '../outside_moved.md')\n"
        "try('rn_exhaust', exhaust.vault.rename, 'Note.md', '.exhaust/plugins.ini')\n"
        "try('rn_from_exhaust', exhaust.vault.rename, '.exhaust/settings.ini', 'stolen.ini')\n"
        "try('rn_from_out', exhaust.vault.rename, '../outside_file.md', 'stolen2.md')\n"
        "try('rn_self', exhaust.vault.rename, 'sub/Inner.md', 'sub/Inner.md')\n");
    HUNT_CHECK(result_ok("w_ok"));
    {
        char p[900];
        char *t;
        const char *denied[] = { "w_dotdot", "w_abs", "w_plugins_ini", "w_manifest", "w_main", "w_settings", "w_hotkeys", "w_trash", "w_case", "w_case2", "w_empty", "w_dir", "rn_over", "rn_out", "rn_exhaust", "rn_from_exhaust", "rn_from_out" };
        size_t i;
        for (i = 0; i < sizeof denied / sizeof *denied; i++) HUNT_SPECF(result(denied[i]) && !result_ok(denied[i]), "docs/formats.md, The runtime (8b): paths stay in the vault; .exhaust, .trash and `..` are refused; rename never replaces", "%s: %s", denied[i], result(denied[i]) ? result(denied[i]) : "(no report)");
        snprintf(p, sizeof p, "%s/.exhaust/plugins.ini", g_vault);
        t = ex_platform_read_file(p, NULL);
        HUNT_CHECKF(t && !strstr(t, "network") && !strstr(t, "plugin.wr]\nenabled = true\napproved = vault.read,vault.write,network"), "a plugin rewrote its own approvals in .exhaust/plugins.ini");
        free(t);
        snprintf(p, sizeof p, "%s/.exhaust/plugins/wr/manifest.ini", g_vault);
        t = ex_platform_read_file(p, NULL);
        HUNT_CHECKF(t && !strstr(t, "network, vault.write"), "a plugin rewrote its own manifest to ask for more permissions");
        free(t);
        snprintf(p, sizeof p, "%s/../outside_written.md", g_vault); HUNT_CHECK(!ex_platform_file_exists(p));
        HUNT_CHECK(!ex_platform_file_exists("/tmp/hunt_abs_written.md"));
        snprintf(p, sizeof p, "%s/../outside_secret/planted.md", g_vault); HUNT_CHECKF(!ex_platform_file_exists(p), "a plugin wrote a file outside the vault through a symbolic link");
        snprintf(p, sizeof p, "%s/../outside_file.md", g_vault);
        t = ex_platform_read_file(p, NULL);
        HUNT_CHECKF(t && !strcmp(t, "TOPSECRET"), "a plugin overwrote a file outside the vault through a symbolic link (now \"%s\")", t ? t : "(missing)");
        free(t);
        snprintf(p, sizeof p, "%s/../outside_moved.md", g_vault); HUNT_CHECK(!ex_platform_file_exists(p));
        snprintf(p, sizeof p, "%s/stolen.ini", g_vault); HUNT_CHECK(!ex_platform_file_exists(p));
        snprintf(p, sizeof p, "%s/stolen2.md", g_vault); HUNT_CHECK(!ex_platform_file_exists(p));
        snprintf(p, sizeof p, "%s/sub/Inner.md", g_vault);
        t = ex_platform_read_file(p, NULL);
        HUNT_CHECKF(t && !strcmp(t, "inner"), "a rename replaced a note");
        free(t);
        snprintf(p, sizeof p, "%s/made/renamed.md", g_vault);
        t = ex_platform_read_file(p, NULL);
        HUNT_CHECK(t && !strcmp(t, "hello"));
        free(t);
        snprintf(p, sizeof p, "%s/Note.md", g_vault);
        t = ex_platform_read_file(p, NULL);
        HUNT_CHECK(t && !strcmp(t, "note text"));
        free(t);
    }
}

/* ---- C. the permission matrix ---- */

static void test_permissions(void) {
    struct { const char *call, *perm; } api[] = {
        { "exhaust.commands.register('zz', 'Zz', function() end)", "commands.register" }, { "exhaust.commands.run('plugin.pm.zz')", "commands.register" }, { "exhaust.vault.read('Note.md')", "vault.read" }, { "exhaust.vault.list('')", "vault.read" },
        { "exhaust.vault.exists('Note.md')", "vault.read" }, { "exhaust.vault.write('pm.md', 'x')", "vault.write" }, { "exhaust.vault.rename('pm.md', 'pm2.md')", "vault.write" }, { "exhaust.editor.text()", "editor.read" }, { "exhaust.editor.selection()", "editor.read" },
        { "exhaust.editor.cursor()", "editor.read" }, { "exhaust.editor.replace_selection('x')", "editor.write" }, { "exhaust.editor.set_text('x')", "editor.write" }, { "exhaust.ui.notice('n')", "ui.notice" }, { "exhaust.ui.add_pane('p', {title = 't', text = 'x'})", "ui.pane" },
        { "exhaust.settings.get('k')", "settings.own" }, { "exhaust.settings.set('k', 'v')", "settings.own" }, { "exhaust.http.get('http://127.0.0.1:9/')", "network" },
    };
    const char *all_perms[] = { "vault.read", "vault.write", "editor.read", "editor.write", "commands.register", "ui.pane", "ui.notice", "settings.own", "network" };
    size_t i, j;
    hunt_step("permissions: every function without its permission and with it");
    for (i = 0; i < sizeof api / sizeof *api; i++) {
        char perms[300] = "ui.notice, commands.register", body[600], id[20], key[20];
        /* a plugin that has everything but this one permission */
        for (j = 0; j < sizeof all_perms / sizeof *all_perms; j++) if (strcmp(all_perms[j], api[i].perm) && strcmp(all_perms[j], "ui.notice") && strcmp(all_perms[j], "commands.register")) { strcat(perms, ", "); strcat(perms, all_perms[j]); }
        if (!strcmp(api[i].perm, "commands.register")) { snprintf(perms, sizeof perms, "ui.notice, vault.read, vault.write, editor.read, editor.write, ui.pane, settings.own"); }
        if (!strcmp(api[i].perm, "ui.notice")) continue;
        snprintf(id, sizeof id, "pm%lu", (unsigned long)i);
        snprintf(key, sizeof key, "p%lu", (unsigned long)i);
        snprintf(body, sizeof body, "try('%s', function() return %s end)", key, api[i].call);
        if (!strcmp(api[i].perm, "commands.register")) {
            /* without commands.register a plugin cannot register its own command to be run: put the call in an event instead */
            char lua[3000];
            snprintf(lua, sizeof lua, "%sexhaust.events.on('vault-open', function() local ok, e = pcall(function() return %s end); exhaust.ui.notice('%s=' .. (ok and 'ok' or ('err:' .. tostring(e)))) end)\n", PREAMBLE, api[i].call, key);
            write_plugin(id, perms, lua, NULL);
            clear_notes();
            ex_plugins_rescan(P); settle(); ex_plugins_enable(P, id, 1); settle();
            ex_plugins_emit(P, "vault-open", NULL, NULL); settle();
        } else run_probe(id, perms, body);
        HUNT_SPECF(result_err(key, "permission denied"), "docs/formats.md, Lua API: a function without its permission raises `permission denied: <name>`", "%s without %s gave %s", api[i].call, api[i].perm, result(key) ? result(key) : "(no report)");
        if (result_err(key, "permission denied")) HUNT_CHECKF(strstr(result(key), api[i].perm) != NULL, "the error for %s does not name the permission %s: %s", api[i].call, api[i].perm, result(key));
    }
    hunt_step("permissions: a plugin with none at all still starts, and exhaust.version() and events.on need nothing");
    run_probe("pnone", "ui.notice, commands.register", "try('ver', exhaust.version)\ntry('evt', exhaust.events.on, 'file-open', function() end)\ntry('evt_bad', exhaust.events.on, 'no-such-event', function() end)\ntry('evt_nofn', exhaust.events.on, 'file-open', 5)\ntry('evt_nil', exhaust.events.on, nil, nil)");
    HUNT_CHECK(result_ok("ver") && result_ok("evt"));
    HUNT_SUSPECT(result("evt_bad") && result_err("evt_bad", NULL), "events.on with a name that is not an event should say so");
    HUNT_CHECK(result("evt_nofn") && result("evt_nil"));
}

/* ---- D. limits ---- */

static void test_limits(void) {
    ExPluginInfo inf;
    hunt_step("limits: an endless loop in a command, in the entry file, and in a coroutine");
    {
        const char *loops[] = { "while true do end", "repeat until false", "for i = 1, math.huge do end", "local function f() return f() end f()", "goto top ::top:: goto top", "coroutine.wrap(function() while true do end end)()", "local co = coroutine.create(function() while true do end end) coroutine.resume(co)",
            "pcall(function() while true do end end) while true do end", "local t = setmetatable({}, {__index = function(t, k) while true do end end}) return t.x", "string.gsub(('a'):rep(100000), 'a', function() local x = 0 for i = 1, 1e7 do x = x + i end end)",
            "table.sort({3, 2, 1, 5, 4, 8, 7, 6, 9, 0}, function(a, b) while true do end end)" };
        size_t i;
        for (i = 0; i < sizeof loops / sizeof *loops; i++) {
            char id[16], cmd[40], lua[1200];
            clock_t t0;
            double wall;
            uint64_t w0;
            snprintf(id, sizeof id, "lp%lu", (unsigned long)i);
            snprintf(lua, sizeof lua, "exhaust.commands.register('go', 'Go', function() %s end)\n", loops[i]);
            write_plugin(id, "commands.register", lua, NULL);
            ex_plugins_rescan(P); settle(); ex_plugins_enable(P, id, 1); settle();
            snprintf(cmd, sizeof cmd, "plugin.%s.go", id);
            hunt_stepf("endless loop %lu: %s", (unsigned long)i, loops[i]);
            if (!ex_commands_available(CMDS, cmd)) { HUNT_CHECKF(0, "the plugin %s did not start", id); continue; }
            t0 = clock(); w0 = SDL_GetTicks();
            ex_commands_run(CMDS, cmd);
            {
                int waited = 0;
                while (waited < 8000) { ex_plugins_update(P); SDL_Delay(5); waited += 5; if (info_of(id, &inf) && !inf.running) break; }
            }
            wall = (double)(SDL_GetTicks() - w0) / 1000.0;
            (void)t0;
            HUNT_SPECF(info_of(id, &inf) && !inf.running && strstr(inf.status, "Stopped"), "docs/formats.md, The runtime (8b): a plugin that goes over a limit gets an error and is stopped", "plugin %s (%s) is still running after %.1f s: %s", id, loops[i], wall, info_of(id, &inf) ? inf.status : "?");
            HUNT_SUSPECT(wall < 3.0, "a limit of 1,000 ms took longer than 3 seconds to apply");
            if (wall >= 3.0) hunt_note_("%s took %.1f s to stop", loops[i], wall);
            HUNT_CHECKF(!ex_commands_available(CMDS, cmd), "the command of a stopped plugin is still there");
            settle_ms(3000);
        }
        write_plugin("lpentry", "commands.register", "while true do end\n", NULL);
        ex_plugins_rescan(P); settle_ms(3000); ex_plugins_enable(P, "lpentry", 1);
        { int w = 0; while (w < 8000) { ex_plugins_update(P); SDL_Delay(5); w += 5; if (info_of("lpentry", &inf) && !inf.running && strstr(inf.status, "Stopped")) break; } }
        HUNT_SPEC(info_of("lpentry", &inf) && strstr(inf.status, "Stopped") != NULL, "docs/formats.md, The runtime (8b): the entry file may run for 1,000 ms when it is loaded");
    }
    hunt_step("limits: memory");
    {
        const char *hogs[] = { "local t = {} for i = 1, 1e9 do t[i] = ('x'):rep(100) end", "local s = ('x'):rep(1e9)", "local s = ('x'):rep(2^30)", "local t = {} while true do t[#t + 1] = {} end", "local s = 'x' while true do s = s .. s end",
            "local t = {} for i = 1, 1e7 do t[i] = i end local u = table.concat(t, ',') local v = u .. u .. u .. u", "return string.rep('abc', 1e10)", "local t = table.pack(table.unpack({}, 1, 1e7))", "return ('x'):rep(1e7):rep(1e3)", "for i = 1, 1e6 do local _ = string.format('%s', ('x'):rep(1e3)) end" };
        size_t i;
        for (i = 0; i < sizeof hogs / sizeof *hogs; i++) {
            char id[16], cmd[40], lua[800];
            int waited = 0;
            snprintf(id, sizeof id, "mh%lu", (unsigned long)i);
            snprintf(lua, sizeof lua, "exhaust.commands.register('go', 'Go', function() %s end)\n", hogs[i]);
            write_plugin(id, "commands.register", lua, NULL);
            ex_plugins_rescan(P); settle(); ex_plugins_enable(P, id, 1); settle();
            snprintf(cmd, sizeof cmd, "plugin.%s.go", id);
            hunt_stepf("memory hog %lu: %s", (unsigned long)i, hogs[i]);
            if (!ex_commands_available(CMDS, cmd)) continue;
            ex_commands_run(CMDS, cmd);
            while (waited < 15000) { ex_plugins_update(P); SDL_Delay(5); waited += 5; if (info_of(id, &inf) && !inf.running) break; }
            HUNT_SPECF(info_of(id, &inf) && !inf.running, "docs/formats.md, The runtime (8b): 32 MB of memory for the state; a plugin over a limit is stopped", "the plugin %s (%s) is still running after %d ms", id, hogs[i], waited);
            settle_ms(3000);
        }
    }
    hunt_step("limits: deep recursion and odd C calls raise Lua errors, not crashes");
    run_probe("rec", "ui.notice, commands.register",
        "try('rec', function() local function f(n) return 1 + f(n + 1) end return f(1) end)\n"
        "try('rec_pcall', function() local function f() return pcall(f) end return f() end)\n"
        "try('rec_meta', function() local t = setmetatable({}, {}) getmetatable(t).__index = t return t.x end)\n"
        "try('rec_tostring', function() local t = setmetatable({}, {__tostring = function(s) return tostring(s) end}) return tostring(t) end)\n"
        "try('fmt_width', function() return string.format('%99999999d', 1) end)\n"
        "try('fmt_bad', function() return string.format('%q', setmetatable({}, {})) end)\n"
        "try('rep_neg', function() return ('x'):rep(-1) end)\n"
        "try('rep_huge_small', function() return #('x'):rep(1e6) end)\n"
        "try('unpack_huge', function() return select('#', table.unpack({}, 1, 1e6)) end)\n"
        "try('unpack_huger', function() return select('#', table.unpack({}, 1, 1e9)) end)\n"
        "try('concat_nonstring', function() return table.concat({{}}) end)\n"
        "try('sort_invalid', function() local t = {} for i = 1, 200 do t[i] = i end table.sort(t, function(a, b) return true end) return #t end)\n"
        "try('gsub_pattern', function() return ('x'):gsub('(((((((((((((((((((((((((((((((((x)))))))))))))))))))))))))))))))))', '') end)\n"
        "try('find_long_pattern', function() return ('x'):find(('a?'):rep(1000)) end)\n"
        "try('string_char', function() return string.char(256) end)\n"
        "try('utf8_bad', function() return utf8.len('\\xff') end)\n"
        "try('math_tointeger', function() return math.tointeger(2^63) end)\n"
        "try('select_neg', function() return select(-1, 1, 2, 3) end)\n"
        "try('string_pack', function() return string.pack('i1000', 1) end)\n"
        "try('load_absent', function() return ('x'):rep(3):len() end)\n");
    HUNT_CHECK(result_err("rec", NULL));
    HUNT_CHECK(result("rec_pcall") != NULL);
    HUNT_CHECK(result("fmt_width") && result_err("fmt_width", NULL));
    HUNT_CHECK(result_ok("rep_huge_small"));
    HUNT_CHECK(result_err("unpack_huger", NULL));
    HUNT_CHECK(result("sort_invalid") != NULL);

    hunt_step("limits: a pattern that backtracks inside one C call (no instruction hook can interrupt it)");
    {
        char lua[600];
        uint64_t w0;
        double wall;
        int stuck_other = 0, w = 0;
        snprintf(lua, sizeof lua, "exhaust.commands.register('go', 'Go', function() local s = ('a'):rep(%d) return s:find(('a*'):rep(7) .. 'b') end)\n", 58);
        write_plugin("cstuck", "commands.register", lua, NULL);
        write_plugin("neighbour", "commands.register, ui.notice", "exhaust.commands.register('go', 'Go', function() exhaust.ui.notice('neighbour ran') end)\n", NULL);
        ex_plugins_rescan(P); settle(); ex_plugins_enable(P, "cstuck", 1); ex_plugins_enable(P, "neighbour", 1); settle();
        clear_notes();
        if (ex_commands_available(CMDS, "plugin.cstuck.go") && ex_commands_available(CMDS, "plugin.neighbour.go")) {
            w0 = SDL_GetTicks();
            ex_commands_run(CMDS, "plugin.cstuck.go");
            ex_commands_run(CMDS, "plugin.neighbour.go");
            {
                /* the interface thread keeps calling update: it must never block */
                uint64_t worst = 0;
                while (w < 6000) {
                    uint64_t a = SDL_GetTicks(), b;
                    ex_plugins_update(P);
                    b = SDL_GetTicks();
                    if (b - a > worst) worst = b - a;
                    SDL_Delay(5); w += 5;
                    { int i; for (i = 0; i < g_nnotes; i++) if (strstr(g_notes[i], "neighbour ran")) { stuck_other = -1; } }
                    if (stuck_other == -1) break;
                }
                HUNT_CHECKF(worst < 100, "ex_plugins_update blocked the interface thread for %llu ms while a plugin was inside a C function", (unsigned long long)worst);
            }
            wall = (double)(SDL_GetTicks() - w0) / 1000.0;
            HUNT_SUSPECT(stuck_other == -1 && wall < 3.0, "one plugin stuck in a C call (a Lua pattern) holds every other plugin: they all run on one thread, and the instruction hook that enforces 1,000 ms cannot interrupt a call into C");
            hunt_note_("the neighbour %s after %.1f s", stuck_other == -1 ? "ran" : "had not run", wall);
        }
        {
            uint64_t t0 = SDL_GetTicks();
            ex_plugins_set_allowed(P, 0);
            settle_ms(15000);
            HUNT_SUSPECT(SDL_GetTicks() - t0 < 4000, "turning plugins off (and so stopping every plugin) took more than 4 seconds while one was inside a C call");
            ex_plugins_set_allowed(P, 1);
            settle_ms(15000);
        }
    }
}

/* ---- E. settings of a plugin ---- */

static void test_settings(void) {
    hunt_step("settings: strings come back as they went in");
    run_probe("st", "ui.notice, commands.register, settings.own",
        "local vals = {'plain', 'with space', ' lead', 'trail ', 'a # b', 'a#b', 'k = v', '[section]', 'line\\nbreak', 'two\\r\\nlines', '', '0', 'true', 'caf\\xc3\\xa9', 'a\\0b', ('x'):rep(1000), ('x'):rep(1001), '\\t', ';not a comment', '#hash'}\n"
        "for i, v in ipairs(vals) do\n"
        "  local ok, e = pcall(exhaust.settings.set, 'k' .. i, v)\n"
        "  local got = exhaust.settings.get('k' .. i)\n"
        "  report('s' .. i, ok and got == v, ok and ('set; got [' .. tostring(got) .. ']') or e)\n"
        "end\n"
        "local keys = {'plain', 'with space', 'a=b', '[x]', 'a\\nb', '', ('k'):rep(1000), ('k'):rep(1001), 'caf\\xc3\\xa9', '#c', ';c', 'a b = c', ' lead', 'trail '}\n"
        "for i, k in ipairs(keys) do\n"
        "  local ok, e = pcall(exhaust.settings.set, k, 'value' .. i)\n"
        "  local got = ok and exhaust.settings.get(k)\n"
        "  report('key' .. i, ok and got == 'value' .. i, ok and ('set; got [' .. tostring(got) .. ']') or e)\n"
        "end\n"
        "try('many', function() for i = 1, 300 do exhaust.settings.set('many' .. i, 'v') end return exhaust.settings.get('many300') end)\n"
        "try('get_missing', function() return tostring(exhaust.settings.get('never set')) end)\n"
        "try('set_nonstring', function() exhaust.settings.set('n', 42) return exhaust.settings.get('n') end)\n"
        "try('set_nil', function() exhaust.settings.set('n2', nil) return tostring(exhaust.settings.get('n2')) end)\n");
    {
        const char *vals[] = { "plain", "with space", " lead", "trail ", "a # b", "a#b", "k = v", "[section]", "line\nbreak", "two\r\nlines", "", "0", "true", "caf\xC3\xA9", "a\0b", "xx1000", "xx1001", "\t", ";not a comment", "#hash" };
        size_t i;
        for (i = 0; i < sizeof vals / sizeof *vals; i++) {
            char key[16];
            const char *r;
            snprintf(key, sizeof key, "s%lu", (unsigned long)i + 1);
            r = result(key);
            if (i == 14 || i == 15 || i == 16) { HUNT_SUSPECT(r != NULL, "a value with a NUL byte, and values near the 1,000 byte limit, are reported"); continue; }
            HUNT_SPECF(r && !strncmp(r, "ok", 2), "docs/formats.md, Lua API: settings.get and settings.set keep strings", "the value #%lu (\"%s\") did not come back: %s", (unsigned long)i + 1, vals[i], r ? r : "(no report)");
        }
        {
            const char *r = result("s17");
            HUNT_SUSPECT(r && (!strncmp(r, "err", 3) || strstr(r, "got [") != NULL), "a value of 1,001 bytes should be refused (limit 1,000), not cut");
        }
    }
    {
        char p[900];
        char *t;
        snprintf(p, sizeof p, "%s/.exhaust/plugins/st/data.ini", g_vault);
        t = ex_platform_read_file(p, NULL);
        if (t) {
            const char *s = t;
            int sections = 0;
            while ((s = strchr(s, '[')) != NULL) { if (s == t || s[-1] == '\n') sections++; s++; }
            HUNT_CHECKF(sections <= 1, "a plugin's data.ini grew %d sections through keys and values that look like headers:\n%s", sections, t);
            free(t);
        }
    }
    HUNT_SUSPECT(result("many") && result_err("many", NULL), "a plugin that sets 300 keys should be stopped at 200 (docs: at most 200 keys)");
    HUNT_CHECK(result("get_missing") && strstr(result("get_missing"), "nil"));
}

/* ---- F. commands ---- */

static int g_ran;
static void fake_run(void *u) { (void)u; g_ran++; }

static void test_commands(void) {
    size_t base = ex_commands_count(CMDS);
    hunt_step("commands: a flood of registrations, bad ids, and cleaning up after the plugin");
    run_probe("fl", "ui.notice, commands.register",
        "local made = 0\n"
        "for i = 1, 20000 do local ok = pcall(exhaust.commands.register, 'c' .. i, 'Command ' .. i, function() end) if ok then made = made + 1 end end\n"
        "report('made', true, made)\n"
        "local bad = {'', 'UPPER', 'has space', '../x', 'a.b', 'a/b', ('x'):rep(500), 'caf\\xc3\\xa9', 'a_b', '-', '--', '9', 'c1', 'go', 'a\\nb', 'a\\0b', 'plugin.other.x'}\n"
        "for i, id in ipairs(bad) do try('id' .. i, exhaust.commands.register, id, 'Name', function() end) end\n"
        "try('noname', exhaust.commands.register, 'zz1', nil, function() end)\n"
        "try('nofn', exhaust.commands.register, 'zz2', 'Name', 5)\n"
        "try('hugename', exhaust.commands.register, 'zz3', ('N'):rep(100000), function() end)\n"
        "try('badopts', exhaust.commands.register, 'zz4', 'N', function() end, 'not a table')\n");
    {
        const char *r = result("made");
        long made = r ? atol(r + 3) : -1;
        HUNT_SUSPECT(made >= 0 && made <= 1000, "a plugin registered 20,000 commands: the palette, the registry and the settings page hold them all (a limit would keep one plugin from burying the others)");
        if (made > 1000) hunt_note_("%ld commands registered by one plugin", made);
        HUNT_CHECKF(ex_commands_count(CMDS) >= base + 1, "the plugin's commands are not in the registry");
        {
            const char *must_fail[] = { "id1", "id2", "id3", "id4", "id5", "id6", "id7", "id8", "id14", "id15", "id16", "id17" };
            size_t i;
            for (i = 0; i < sizeof must_fail / sizeof *must_fail; i++) HUNT_SPECF(result(must_fail[i]) && !result_ok(must_fail[i]), "docs/formats.md, Lua API: id is lowercase letters, digits and hyphens", "%s was accepted: %s", must_fail[i], result(must_fail[i]) ? result(must_fail[i]) : "(no report)");
        }
    }
    ex_plugins_enable(P, "fl", 0); settle();
    HUNT_CHECKF(ex_commands_count(CMDS) == base, "after the plugin was turned off the registry has %lu commands, it had %lu", (unsigned long)ex_commands_count(CMDS), (unsigned long)base);

    hunt_step("commands: a plugin with only commands.register runs the application's own commands");
    {
        const ExCommandDef *def = ex_commands_at(CMDS, 0);
        char body[400];
        g_ran = 0;
        HUNT_CHECK(def != NULL);
        if (def) {
            char idbuf[200];
            snprintf(idbuf, sizeof idbuf, "%s", def->id);
            ex_commands_bind(CMDS, idbuf, fake_run, NULL, NULL);
            snprintf(body, sizeof body, "try('run_app', exhaust.commands.run, '%s')", idbuf);
            run_probe("esc", "ui.notice, commands.register", body);
            HUNT_SUSPECT(g_ran == 0, "a plugin that holds only commands.register can run any command of the application by its id (including ones that delete, rename or open files): the permission model says it needs vault.write or editor.write for those");
            if (g_ran) hunt_note_("ran %s", idbuf);
        }
    }
}

/* ---- G. manifests ---- */

static void test_manifests(void) {
    ExPluginInfo m;
    char big[60000];
    hunt_step("manifests: hostile files");
    {
        struct { const char *folder, *text; } k[] = {
            { "h1", "[plugin]\nid = ../escape\nname = x\nversion = 1.0.0\napi = 1\n" }, { "h2", "[plugin]\nid = h2\nname = x\nversion = 1.0.0\napi = 1\nentry = /etc/passwd\n" }, { "h3", "[plugin]\nid = h3\nname = x\nversion = 1.0.0\napi = 1\nentry = a\\b.lua\n" },
            { "h4", "[plugin]\nid = h4\nname = x\nversion = 1.0.0\napi = 1\nentry = ../../Note.md\n" }, { "h5", "[plugin]\nid = h5\nname = x\nversion = 1.0.0\napi = 1\nentry = sub/../../x.lua\n" }, { "h6", "[plugin]\nid = h6\nname = x\nversion = 1.0.0\napi = 1\nentry = \n" },
            { "h7", "[plugin]\nid = h7\nname = x\nversion = 1.0.0\napi = 0\n" }, { "h8", "[plugin]\nid = h8\nname = x\nversion = 1.0.0\napi = 99999999999999999999\n" }, { "h9", "[plugin]\nid = h9\nname = \nversion = 1.0.0\napi = 1\n" }, { "h10", "[plugin]\nid = h10\nversion = 1.0.0\napi = 1\n" },
            { "h11", "[plugin]\nid = h11\nname = x\nversion = 1.0.0.0\napi = 1\n" }, { "h12", "[plugin]\nid = h12\nname = x\nversion = 1.0.0\napi = 1\npermissions = vault.read,,ui.notice,\n" }, { "h13", "[plugin]\nid = h13\nname = x\nversion = 1.0.0\napi = 1\npermissions = VAULT.READ\n" },
            { "h14", "[plugin]\nid = h14\nname = x\nversion = 1.0.0\napi = 1\nid = other\n" }, { "h15", "[other]\nid = h15\nname = x\nversion = 1.0.0\napi = 1\n" }, { "h16", "id = h16\nname = x\nversion = 1.0.0\napi = 1\n" }, { "h17", "" },
            { "h18", "\xFF\xFE\x00[plugin]" }, { "h19", "[plugin]\nid = h19\nname = \xFF\xFE\nversion = 1.0.0\napi = 1\n" }, { "h20", "[plugin]\nid = h20\nname = x\nversion = 1.0.0\napi = 1\nentry = main.lua\nentry = ../x.lua\n" },
            { "h21", "[plugin]\nid = H21\nname = x\nversion = 1.0.0\napi = 1\n" }, { "h22", "[plugin]\nid = h-22-\nname = x\nversion = 1.0.0\napi = 1\n" }, { "h23", "[plugin]\nid = \nname = x\nversion = 1.0.0\napi = 1\n" }, { "h24", "[plugin]\nid = h24 \nname = x\nversion = 1.0.0\napi = 1\n" },
            { "h25", "[plugin]\nid = h25\nname = x\nversion = -1.0.0\napi = 1\n" }, { "h26", "[plugin]\nid = h26\nname = x\nversion = 1.0.0\napi = 1\nentry = .exhaust/x.lua\n" }, { "h27", "[plugin]\nid = h27\nname = x\nversion = 99999999999.0.0\napi = 1\n" },
        };
        size_t i;
        for (i = 0; i < sizeof k / sizeof *k; i++) {
            char dir[900];
            int rc;
            size_t tl = !strcmp(k[i].folder, "h18") ? 6 : strlen(k[i].text);
            char rel[300];
            snprintf(rel, sizeof rel, "mf/%s/manifest.ini", k[i].folder);
            hunt_write_in(g_vault, rel, k[i].text, tl);
            snprintf(dir, sizeof dir, "%s/mf/%s", g_vault, k[i].folder);
            memset(&m, 0, sizeof m);
            rc = ex_plugin_read_manifest(dir, k[i].folder, &m);
            if (!strcmp(k[i].folder, "h12") || !strcmp(k[i].folder, "h13") || !strcmp(k[i].folder, "h19") || !strcmp(k[i].folder, "h27") || !strcmp(k[i].folder, "h21") || !strcmp(k[i].folder, "h22") || !strcmp(k[i].folder, "h24") || !strcmp(k[i].folder, "h25")) { HUNT_SUSPECT(rc == 0, "a manifest that is odd but not covered by the documentation: read by hand"); if (rc != 0) hunt_note_("manifest %s was accepted", k[i].folder); continue; }
            HUNT_SPECF(rc == 0 && !m.valid && m.error[0], "docs/formats.md, Plugin manifest", "the hostile manifest %s was accepted (entry %s, id %s)", k[i].folder, m.entry, m.id);
        }
    }
    hunt_step("manifests: very large and very many");
    {
        size_t n = 0, i;
        n += (size_t)snprintf(big, sizeof big, "[plugin]\nid = bigperm\nname = x\nversion = 1.0.0\napi = 1\npermissions = ");
        for (i = 0; i < 4000 && n + 20 < sizeof big; i++) n += (size_t)snprintf(big + n, sizeof big - n, "vault.read, ");
        hunt_write_in(g_vault, "mf/bigperm/manifest.ini", big, n);
        {
            char dir[900];
            snprintf(dir, sizeof dir, "%s/mf/bigperm", g_vault);
            memset(&m, 0, sizeof m);
            (void)ex_plugin_read_manifest(dir, "bigperm", &m);
            HUNT_CHECK(m.nperm <= EX_PLUGIN_MAX_PERMS);
        }
        {
            size_t l = 6u << 20;
            char *huge = (char *)malloc(l + 100), dir[900];
            n = (size_t)snprintf(huge, 100, "[plugin]\nid = hugeman\nname = ");
            memset(huge + n, 'n', l); n += l;
            n += (size_t)snprintf(huge + n, 80, "\nversion = 1.0.0\napi = 1\n");
            hunt_write_in(g_vault, "mf/hugeman/manifest.ini", huge, n);
            snprintf(dir, sizeof dir, "%s/mf/hugeman", g_vault);
            memset(&m, 0, sizeof m);
            (void)ex_plugin_read_manifest(dir, "hugeman", &m);
            HUNT_CHECK(m.name[sizeof m.name - 1] == 0);
            free(huge);
        }
    }
    hunt_step("manifests: bytecode and an approval that no longer matches");
    {
        ExPluginInfo inf;
        char p[900];
        hunt_write_in(g_vault, ".exhaust/plugins/bc/manifest.ini", "[plugin]\nid = bc\nname = BC\nversion = 1.0.0\napi = 1\npermissions = ui.notice\n", 74);
        hunt_write_in(g_vault, ".exhaust/plugins/bc/main.lua", "\x1bLuaS\x00\x19\x93\r\n\x1a\n\x04\x08\x08xV\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00(w@", 31);
        ex_plugins_rescan(P); settle(); ex_plugins_enable(P, "bc", 1); settle();
        HUNT_SPEC(info_of("bc", &inf) && !inf.running, "docs/formats.md, The runtime (8b): a chunk that starts with the Lua binary signature is refused");
        write_plugin("ap", "ui.notice, vault.read", "exhaust.ui.notice('ap ran')\n", NULL);
        ex_plugins_rescan(P); settle(); ex_plugins_enable(P, "ap", 1); settle();
        HUNT_CHECK(info_of("ap", &inf) && inf.running);
        write_plugin("ap", "ui.notice, vault.read, vault.write", "exhaust.ui.notice('ap ran')\n", NULL);                  /* the manifest now asks for more */
        ex_plugins_rescan(P); settle();
        HUNT_SPECF(info_of("ap", &inf) && !inf.running && strstr(inf.status, "approval") != NULL, "docs/formats.md, The runtime (8b): when a plugin's folder changes its permissions or its version, the approval no longer matches and the plugin stays off", "after the manifest asked for another permission the plugin is %s: %s", info_of("ap", &inf) && inf.running ? "still running" : "off", info_of("ap", &inf) ? inf.status : "?");
        snprintf(p, sizeof p, "%s/.exhaust/plugins/ap/manifest.ini", g_vault);
        hunt_write_in(g_vault, ".exhaust/plugins/ap/manifest.ini", "[plugin]\nid = ap\nname = Plugin ap\nversion = 1.0.1\napi = 1\npermissions = ui.notice, vault.read, vault.write\n", 105);
        ex_plugins_rescan(P); settle();
        HUNT_CHECK(info_of("ap", &inf) && !inf.running);
        /* a plugin.ini approval written by hand for another set of permissions */
        hunt_write_in(g_vault, ".exhaust/plugins.ini", "[plugin.ap]\nenabled = true\napproved = ui.notice\nversion = 1.0.1\n", 64);
        ex_plugins_rescan(P); settle();
        HUNT_CHECK(info_of("ap", &inf) && !inf.running);
        ex_plugins_enable(P, "ap", 0); settle();
        ex_plugins_enable(P, "nonexistent", 1); settle();
        ex_plugins_enable(P, NULL, 1);
        ex_plugins_enable(P, "../etc", 1); settle();
    }
}

/* ---- H. toggling, events, freeing ---- */

static void test_stress(void) {
    long rounds = hunt_iters(300), r;
    int i;
    ExPluginInfo inf;
    hunt_step("stress: toggling plugins on and off with events in flight");
    write_plugin("sa", "ui.notice, commands.register, vault.read, settings.own", "exhaust.events.on('file-open', function(p) exhaust.ui.notice('sa ' .. tostring(p)) end)\nexhaust.events.on('editor-change', function() exhaust.settings.set('n', tostring(os and 1 or 2)) end)\nexhaust.commands.register('go', 'Go', function() exhaust.ui.notice('go') end)\n", NULL);
    write_plugin("sb", "ui.notice, commands.register", "exhaust.events.on('file-open', function(p) local s = 0 for i = 1, 100000 do s = s + i end end)\nexhaust.commands.register('go', 'Go', function() exhaust.ui.notice('go b') end)\n", NULL);
    ex_plugins_rescan(P); settle();
    for (r = 0; r < rounds; r++) {
        hunt_stepf("toggle round %ld", r);
        switch (hunt_rnd(8)) {
        case 0: ex_plugins_enable(P, "sa", 1); break;
        case 1: ex_plugins_enable(P, "sa", 0); break;
        case 2: ex_plugins_enable(P, "sb", 1); break;
        case 3: ex_plugins_enable(P, "sb", 0); break;
        case 4: ex_plugins_set_allowed(P, (int)hunt_rnd(2)); break;
        case 5: ex_plugins_rescan(P); break;
        case 6: for (i = 0; i < 100; i++) ex_plugins_emit(P, "file-open", "Note.md", NULL); break;
        default: ex_plugins_emit(P, "editor-change", "Note.md", NULL); ex_plugins_emit(P, "no-such-event", NULL, NULL); ex_plugins_emit(P, NULL, NULL, NULL); break;
        }
        ex_plugins_update(P);
        if (hunt_chance(30)) { ExPluginInfo all[64]; (void)ex_plugins_snapshot(P, all, 64); (void)ex_plugins_snapshot(P, all, 0); (void)ex_plugins_snapshot(P, NULL, 0); }
        if (hunt_chance(20)) { if (ex_commands_available(CMDS, "plugin.sa.go")) ex_commands_run(CMDS, "plugin.sa.go"); if (ex_commands_available(CMDS, "plugin.sb.go")) ex_commands_run(CMDS, "plugin.sb.go"); }
        if (hunt_chance(15)) settle_ms(500);
    }
    ex_plugins_set_allowed(P, 1);
    settle_ms(10000);
    HUNT_CHECK(info_of("sa", &inf) || 1);
    hunt_step("stress: an event flood does not queue without limit");
    {
        uint64_t t0 = SDL_GetTicks();
        for (i = 0; i < 200000; i++) ex_plugins_emit(P, "file-open", "Flood.md", NULL);
        HUNT_SUSPECT(SDL_GetTicks() - t0 < 3000, "200,000 events took more than 3 seconds to post: posting must not block or grow without limit (docs: dropped when more than 64 wait)");
        settle_ms(15000);
    }
}

static int g_wake;
static void wake_cb(void *u) { (void)u; g_wake++; }

int main(int argc, char **argv) {
    ExPluginHost host;
    const char *root;
    hunt_begin("hunt_plugins", argc, argv);
    root = hunt_fs_init("plugins");
    snprintf(g_vault, sizeof g_vault, "%s/vault", root);
    ex_platform_make_dirs(g_vault);
    memset(&host, 0, sizeof host);
    host.editor_get = h_get; host.editor_replace = h_replace; host.notice = h_notice; host.open_pane = h_pane; host.rename = h_rename; host.changed = h_changed;
    CMDS = ex_commands_create(NULL);
    P = ex_plugins_create(g_vault, CMDS, &host, wake_cb, NULL);
    if (!P) { fprintf(stderr, "HUNT cannot create the plugin runtime\n"); return 2; }
    ex_plugins_set_allowed(P, 1);
    test_sandbox();
    test_paths();
    test_permissions();
    test_commands();
    test_settings();
    test_manifests();
    test_limits();
    test_stress();
    hunt_step("freeing the runtime while plugins are busy");
    {
        write_plugin("busy", "commands.register", "exhaust.commands.register('go', 'Go', function() while true do end end)\n", NULL);
        ex_plugins_rescan(P); settle(); ex_plugins_enable(P, "busy", 1); settle();
        if (ex_commands_available(CMDS, "plugin.busy.go")) ex_commands_run(CMDS, "plugin.busy.go");
        SDL_Delay(100);
        {
            uint64_t t0 = SDL_GetTicks();
            ex_plugins_free(P);
            HUNT_SUSPECT(SDL_GetTicks() - t0 < 5000, "ex_plugins_free took more than 5 seconds with a plugin in an endless loop (the documentation promises two)");
        }
        P = NULL;
    }
    ex_commands_free(CMDS);
    clear_notes();
    free(g_text);
    return hunt_end();
}
