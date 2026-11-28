/* plugins (sub-slice 8b): manifests, the user's approval, the sandbox (no os, io, load, require, debug or bytecode; paths that stay in the vault; a time limit and a
 * memory limit that a plugin cannot catch), the API with its permissions, commands and panes that appear in the registry and go away with the plugin,
 * events, a plugin's own settings, and the master switch. A fake application answers the calls the way the interface thread does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_commands.h"
#include "exhaust_platform.h"
#include "exhaust_plugins.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "plugins_test_vault"

/* ---- a fake application ---- */
static char *g_text;                       /* the open note's text; NULL: no note is open */
static size_t g_sel0, g_sel1, g_cursor;
static char g_notices[40][300]; static int g_nnotices;
static char g_pane_title[120], g_pane_text[2000]; static int g_npanes;
static char g_rename_from[200], g_rename_to[200];
static int g_changed;

static int h_get(void *u, char **t, size_t *a, size_t *b, size_t *c) { (void)u; if (!g_text) return -1; *t = malloc(strlen(g_text) + 1); strcpy(*t, g_text); *a = g_sel0; *b = g_sel1; *c = g_cursor; return 0; }
static int h_replace(void *u, const char *t, size_t len, int whole) {
    (void)u;
    if (!g_text) return -1;
    if (whole) { free(g_text); g_text = malloc(len + 1); memcpy(g_text, t, len); g_text[len] = 0; }
    else { size_t n = strlen(g_text); char *nt = malloc(n - (g_sel1 - g_sel0) + len + 1); memcpy(nt, g_text, g_sel0); memcpy(nt + g_sel0, t, len); strcpy(nt + g_sel0 + len, g_text + g_sel1); free(g_text); g_text = nt; }
    return 0;
}
static void h_notice(void *u, const char *t) { (void)u; if (g_nnotices < 40) snprintf(g_notices[g_nnotices++], sizeof g_notices[0], "%s", t); }
static int h_pane(void *u, const char *title, const char *md) { (void)u; snprintf(g_pane_title, sizeof g_pane_title, "%s", title); snprintf(g_pane_text, sizeof g_pane_text, "%s", md); g_npanes++; return 0; }
static int h_rename(void *u, const char *a, const char *b) {
    char pa[400], pb[400];
    (void)u;
    snprintf(g_rename_from, sizeof g_rename_from, "%s", a); snprintf(g_rename_to, sizeof g_rename_to, "%s", b);
    snprintf(pa, sizeof pa, "%s/%s", V, a); snprintf(pb, sizeof pb, "%s/%s", V, b);
    return ex_platform_rename_noreplace(pa, pb);
}
static void h_changed(void *u) { (void)u; g_changed++; }

static ExPlugins *P;
static ExCommands *CMDS;

static void settle(void) {
    int i, quiet = 0;
    for (i = 0; i < 4000; i++) {
        ex_plugins_update(P);
        if (!ex_plugins_busy(P)) { if (++quiet >= 3) return; } else quiet = 0;
        SDL_Delay(2);
    }
    fprintf(stderr, "the plugin thread did not settle\n");
    failures++;
}

static void write_str(const char *rel, const char *s) {
    char path[600], d[600], *sl;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    snprintf(d, sizeof d, "%s", path);
    sl = strrchr(d, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(d); }
    f = fopen(path, "wb");
    if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
}
static void plugin(const char *id, const char *perms, const char *lua) {
    char rel[200], man[1000];
    snprintf(rel, sizeof rel, ".exhaust/plugins/%s/manifest.ini", id);
    snprintf(man, sizeof man, "[plugin]\nid = %s\nname = Plugin %s\nversion = 1.0.0\napi = 1\nauthor = Test\ndescription = A test plugin\npermissions = %s\n", id, id, perms);
    write_str(rel, man);
    snprintf(rel, sizeof rel, ".exhaust/plugins/%s/main.lua", id);
    write_str(rel, lua);
}
static int has_rel(const char *rel) { char p[600]; char *t; snprintf(p, sizeof p, "%s/%s", V, rel); t = ex_platform_read_file(p, NULL); free(t); return t != NULL; }
static char *read_rel(const char *rel) { char p[600]; snprintf(p, sizeof p, "%s/%s", V, rel); return ex_platform_read_file(p, NULL); }
static int notice_has(const char *needle) { int i; for (i = 0; i < g_nnotices; i++) if (strstr(g_notices[i], needle)) return 1; return 0; }
static void clear_notices(void) { g_nnotices = 0; }
static int find_info(const char *id, ExPluginInfo *out) {
    ExPluginInfo all[64];
    size_t n = ex_plugins_snapshot(P, all, 64), i;
    for (i = 0; i < n && i < 64; i++) if (!strcmp(all[i].id, id)) { *out = all[i]; return 1; }
    return 0;
}
static int cmd_ok(const char *id) { return ex_commands_available(CMDS, id); }
static void run_cmd(const char *id) { CHECK(ex_commands_run(CMDS, id) == 0); settle(); }
static void turn_on(const char *id) { ex_plugins_enable(P, id, 1); settle(); }

int main(void) {
    ExPluginHost host;
    ExPluginInfo info;
    size_t base_count;
    ex_platform_remove_tree(V);
    ex_platform_make_dirs(V);
    memset(&host, 0, sizeof host);
    host.editor_get = h_get; host.editor_replace = h_replace; host.notice = h_notice; host.open_pane = h_pane; host.rename = h_rename; host.changed = h_changed;
    CMDS = ex_commands_create(NULL);
    base_count = ex_commands_count(CMDS);

    /* ---- manifests ---- */
    {
        ExPluginInfo m;
        write_str("m/ok/manifest.ini", "[plugin]\nid = ok\nname = Fine\nversion = 2.0.1\napi = 1\npermissions = vault.read, ui.notice vault.read\nentry = src/start.lua\n");
        CHECK(ex_plugin_read_manifest(V "/m/ok", "ok", &m) == 1 && m.valid && m.nperm == 2 && !strcmp(m.entry, "src/start.lua") && !strcmp(m.permissions[1], "ui.notice"));
        write_str("m/bad1/manifest.ini", "[plugin]\nid = Bad_ID\nname = x\nversion = 1.0.0\napi = 1\n");
        CHECK(ex_plugin_read_manifest(V "/m/bad1", "bad1", &m) == 0 && !m.valid && strstr(m.error, "id"));
        write_str("m/bad2/manifest.ini", "[plugin]\nid = bad2\nname = x\nversion = 1.0\napi = 1\n");
        CHECK(ex_plugin_read_manifest(V "/m/bad2", "bad2", &m) == 0 && strstr(m.error, "version"));
        write_str("m/bad3/manifest.ini", "[plugin]\nid = bad3\nname = x\nversion = 1.0.0\napi = 2\n");
        CHECK(ex_plugin_read_manifest(V "/m/bad3", "bad3", &m) == 0 && strstr(m.error, "api"));
        write_str("m/bad4/manifest.ini", "[plugin]\nid = bad4\nname = x\nversion = 1.0.0\napi = 1\npermissions = vault.read, root\n");
        CHECK(ex_plugin_read_manifest(V "/m/bad4", "bad4", &m) == 0 && strstr(m.error, "unknown permission"));
        write_str("m/bad5/manifest.ini", "[plugin]\nid = other\nname = x\nversion = 1.0.0\napi = 1\n");
        CHECK(ex_plugin_read_manifest(V "/m/bad5", "bad5", &m) == 0 && strstr(m.error, "folder"));
        write_str("m/bad6/manifest.ini", "[plugin]\nid = bad6\nname = x\nversion = 1.0.0\napi = 1\nentry = ../x.lua\n");
        CHECK(ex_plugin_read_manifest(V "/m/bad6", "bad6", &m) == 0 && strstr(m.error, "entry"));
        CHECK(ex_plugin_read_manifest(V "/m/missing", "missing", &m) == 0);
        CHECK(ex_plugin_permission_valid("network") && !ex_plugin_permission_valid("root") && !ex_plugin_permission_valid(NULL));
        ex_platform_remove_tree(V "/m");
    }

    write_str("Note.md", "# Note\n");
    write_str("sub/Inner.md", "inner");
    write_str(".exhaust/settings.ini", "[general]\nlanguage = en\n");
    plugin("sample", "commands.register, ui.pane, ui.notice, vault.read, vault.write, editor.read, editor.write, settings.own",
        "exhaust.commands.register('hello', 'Say hello', function() exhaust.ui.notice('Hello from ' .. exhaust.version()) end)\n"
        "exhaust.commands.register('shout', 'Shout', function() local t = exhaust.editor.text(); if t then exhaust.editor.set_text(t:upper()) end end, {editor = true})\n"
        "exhaust.commands.register('count', 'Count notes', function() local l = exhaust.vault.list(''); exhaust.ui.notice('items: ' .. #l .. ' first: ' .. l[1]) end)\n"
        "exhaust.commands.register('replace', 'Replace the selection', function() exhaust.editor.replace_selection('<<' .. exhaust.editor.selection() .. '>>') end)\n"
        "exhaust.commands.register('store', 'Store', function() local n = tonumber(exhaust.settings.get('n') or '0') + 1; exhaust.settings.set('n', tostring(n)); exhaust.ui.notice('n=' .. n) end)\n"
        "exhaust.commands.register('write', 'Write', function() exhaust.vault.write('out/new.md', 'written by a plugin'); exhaust.ui.notice(tostring(exhaust.vault.exists('out/new.md'))) end)\n"
        "exhaust.ui.add_pane('info', {title = 'Info', render = function() return '# Info\\n\\nnotes: ' .. #exhaust.vault.list('') end})\n"
        "exhaust.ui.add_pane('fixed', {title = 'Fixed', text = 'Fixed *text*'})\n");
    plugin("denied", "commands.register, ui.notice",
        "exhaust.commands.register('try', 'Try', function()\n"
        "  local ok, e = pcall(exhaust.vault.read, 'Note.md'); exhaust.ui.notice('read: ' .. tostring(ok) .. ' ' .. tostring(e))\n"
        "  ok, e = pcall(exhaust.editor.text); exhaust.ui.notice('editor: ' .. tostring(ok) .. ' ' .. tostring(e))\n"
        "  ok, e = pcall(exhaust.http.get, 'https://example.org'); exhaust.ui.notice('http: ' .. tostring(ok) .. ' ' .. tostring(e))\n"
        "  ok, e = pcall(exhaust.ui.add_pane, 'p', {title = 'x', text = 'y'}); exhaust.ui.notice('pane: ' .. tostring(ok) .. ' ' .. tostring(e))\n"
        "  ok, e = pcall(exhaust.commands.run, 'app.rename-file'); exhaust.ui.notice('run: ' .. tostring(ok) .. ' ' .. tostring(e))\n"
        "end)\n");
    P = ex_plugins_create(V, CMDS, &host, NULL, NULL);
    CHECK(P != NULL);
    if (!P) return 1;

    /* nothing runs until plugins are allowed and the plugin is on */
    ex_plugins_rescan(P); settle();
    CHECK(find_info("sample", &info) && !info.running && !info.enabled && !strcmp(info.status, "Off") && info.nperm == 8);
    CHECK(ex_commands_count(CMDS) == base_count && !cmd_ok("plugin.sample.hello"));
    ex_plugins_enable(P, "sample", 1); settle();
    CHECK(find_info("sample", &info) && info.enabled && info.approved && !info.running && strstr(info.status, "not allowed"));
    CHECK(!cmd_ok("plugin.sample.hello"));
    {   char *ini = read_rel(".exhaust/plugins.ini");
        CHECK(ini && strstr(ini, "[plugin.sample]") && strstr(ini, "enabled = true") && strstr(ini, "approved = commands.register,ui.pane") && strstr(ini, "version = 1.0.0"));
        free(ini); }
    ex_plugins_set_allowed(P, 1); settle();
    CHECK(find_info("sample", &info) && info.running && !strcmp(info.status, "Running"));
    CHECK(cmd_ok("plugin.sample.hello") && cmd_ok("plugin.sample.shout") && cmd_ok("plugin.sample.pane-info") && cmd_ok("plugin.sample.pane-fixed"));
    CHECK(ex_commands_count(CMDS) == base_count + 8);
    CHECK(ex_commands_find(CMDS, "plugin.sample.hello") && !strcmp(ex_commands_find(CMDS, "plugin.sample.hello")->name, "Plugin sample: Say hello"));
    CHECK(ex_commands_find(CMDS, "plugin.sample.shout")->editor == 1 && ex_commands_find(CMDS, "plugin.sample.hello")->editor == 0);
    CHECK(g_changed > 0);

    /* commands and panes */
    clear_notices();
    run_cmd("plugin.sample.hello");
    CHECK(notice_has("Plugin sample: Hello from 0."));
    g_text = malloc(11); strcpy(g_text, "quiet text");
    run_cmd("plugin.sample.shout");
    CHECK(!strcmp(g_text, "QUIET TEXT"));
    g_sel0 = 0; g_sel1 = 5; g_cursor = 5;
    run_cmd("plugin.sample.replace");
    CHECK(!strcmp(g_text, "<<QUIET>> TEXT"));
    clear_notices();
    run_cmd("plugin.sample.count");
    CHECK(notice_has("items: 3 first: .exhaust") == 0 && notice_has("items: 2 first: Note.md"));      /* .exhaust is not listed; the folder sub/ is */
    run_cmd("plugin.sample.pane-info");
    CHECK(g_npanes == 1 && !strcmp(g_pane_title, "Info") && strstr(g_pane_text, "# Info") && strstr(g_pane_text, "notes: 2"));
    run_cmd("plugin.sample.pane-fixed");
    CHECK(g_npanes == 2 && !strcmp(g_pane_text, "Fixed *text*"));
    free(g_text); g_text = NULL;
    run_cmd("plugin.sample.shout");                                              /* no note open: nothing happens, no error */
    clear_notices();
    run_cmd("plugin.sample.store"); run_cmd("plugin.sample.store");
    CHECK(notice_has("n=1") && notice_has("n=2"));
    {   char *d = read_rel(".exhaust/plugins/sample/data.ini"); CHECK(d && strstr(d, "[data]") && strstr(d, "n = 2")); free(d); }
    clear_notices();
    run_cmd("plugin.sample.write");
    {   char *w = read_rel("out/new.md"); CHECK(w && !strcmp(w, "written by a plugin")); free(w); }
    CHECK(notice_has("true"));

    /* a call without its permission is an error (and nothing in the vault was touched) */
    turn_on("denied");
    CHECK(cmd_ok("plugin.denied.try"));
    clear_notices();
    run_cmd("plugin.denied.try");
    CHECK(notice_has("read: false") && notice_has("permission denied: vault.read"));
    CHECK(notice_has("editor: false") && notice_has("permission denied: editor.read"));
    CHECK(notice_has("http: false") && notice_has("permission denied: network"));
    CHECK(notice_has("pane: false") && notice_has("permission denied: ui.pane"));
    CHECK(notice_has("run: false") && notice_has("permission denied: vault.write or editor.write"));      /* a command of the application, with only commands.register */

    /* when a plugin's permissions change, the approval no longer matches: it stops and needs the user again */
    plugin("denied", "commands.register, ui.notice, vault.write", "exhaust.commands.register('try', 'Try', function() end)\n");
    ex_plugins_rescan(P); settle();
    CHECK(find_info("denied", &info) && !info.running && !info.approved && strstr(info.status, "Needs approval again"));
    CHECK(!cmd_ok("plugin.denied.try"));
    turn_on("denied");
    CHECK(find_info("denied", &info) && info.running && info.approved && cmd_ok("plugin.denied.try"));

    /* turning a plugin off removes its commands; the master switch stops all of them */
    ex_plugins_enable(P, "denied", 0); settle();
    CHECK(!cmd_ok("plugin.denied.try") && find_info("denied", &info) && !info.running && !strcmp(info.status, "Off"));
    ex_plugins_set_allowed(P, 0); settle();
    CHECK(ex_commands_count(CMDS) == base_count && !cmd_ok("plugin.sample.hello"));
    ex_plugins_set_allowed(P, 1); settle();
    CHECK(cmd_ok("plugin.sample.hello"));

    /* the sandbox */
    plugin("evil", "commands.register, ui.notice, vault.read, vault.write, settings.own",
        "local function t(name, v) exhaust.ui.notice(name .. '=' .. type(v)) end\n"
        "t('os', os); t('io', io); t('debug', debug); t('package', package); t('load', load); t('loadfile', loadfile); t('dofile', dofile); t('require', require)\n"
        "t('collectgarbage', collectgarbage); t('dump', string.dump); t('string', string); t('math', math); t('utf8', utf8); t('coroutine', coroutine)\n"
        "exhaust.commands.register('paths', 'Paths', function()\n"
        "  for _, p in ipairs({'../secret', '/etc/passwd', 'a\\\\b', 'C:/x', '.exhaust/settings.ini', '.EXHAUST/plugins.ini', '.trash/x', 'a//b', './a', 'a/../../b', ''}) do\n"
        "    local ok, e = pcall(exhaust.vault.read, p); exhaust.ui.notice('p[' .. p .. ']=' .. tostring(ok) .. ':' .. tostring(e):match('path not allowed') )\n"
        "  end\n"
        "  local ok, e = pcall(exhaust.vault.write, '../x.md', 'no'); exhaust.ui.notice('w=' .. tostring(ok))\n"
        "  ok, e = pcall(exhaust.vault.write, '.exhaust/hotkeys.ini', 'no'); exhaust.ui.notice('w2=' .. tostring(ok))\n"
        "  exhaust.ui.notice('nofile=' .. tostring(exhaust.vault.read('nothing.md')))\n"
        "end)\n"
        "exhaust.commands.register('loop', 'Loop', function() while true do end end)\n"
        "exhaust.commands.register('catch', 'Catch', function() while true do pcall(function() while true do end end) end end)\n"
        "exhaust.commands.register('co', 'Coroutine', function() local c = coroutine.create(function() while true do end end); while true do coroutine.resume(c) end end)\n"
        "exhaust.commands.register('mem', 'Memory', function() local t = {}; for i = 1, 1e9 do t[i] = string.rep('x', 1000) .. i end end)\n"
        "exhaust.commands.register('rep', 'Rep', function() local s = string.rep('x', 2^30); exhaust.ui.notice('survived') end)\n"
        "exhaust.commands.register('err', 'Err', function() error('plain error') end)\n");
    clear_notices();
    turn_on("evil");
    CHECK(find_info("evil", &info) && info.running);
    CHECK(notice_has("os=nil") && notice_has("io=nil") && notice_has("debug=nil") && notice_has("package=nil") && notice_has("load=nil") && notice_has("loadfile=nil"));
    CHECK(notice_has("dofile=nil") && notice_has("require=nil") && notice_has("collectgarbage=nil") && notice_has("dump=nil"));
    CHECK(notice_has("string=table") && notice_has("math=table") && notice_has("utf8=table") && notice_has("coroutine=table"));
    clear_notices();
    run_cmd("plugin.evil.paths");
    {   int i, refused = 0;
        for (i = 0; i < g_nnotices; i++) if (strstr(g_notices[i], "=false:path not allowed")) refused++;
        CHECK(refused == 11);                                                    /* every one of the eleven paths */
        CHECK(notice_has("w=false") && notice_has("w2=false") && notice_has("nofile=nil")); }
    {   char *w = read_rel("../x.md"); CHECK(w == NULL); free(w);
        w = read_rel(".exhaust/hotkeys.ini"); CHECK(w == NULL); free(w); }
    clear_notices();
    run_cmd("plugin.evil.err");
    CHECK(notice_has("plain error") && find_info("evil", &info) && info.running);                 /* an error is shown, the plugin goes on */
    {   char *log = read_rel(".exhaust/plugins/evil/log.txt"); CHECK(log && strstr(log, "plain error")); free(log); }

    {   static const char *const stops[][2] = { {"plugin.evil.loop", "time limit exceeded"}, {"plugin.evil.catch", "time limit exceeded"}, {"plugin.evil.co", "time limit exceeded"}, {"plugin.evil.mem", "not enough memory"}, {"plugin.evil.rep", "not enough memory"} };
        size_t k;
        for (k = 0; k < sizeof stops / sizeof *stops; k++) {
            uint64_t t0 = SDL_GetTicks();
            clear_notices();
            if (k > 0) { ex_plugins_enable(P, "evil", 0); settle(); turn_on("evil"); }
            run_cmd(stops[k][0]);
            CHECK(find_info("evil", &info) && !info.running && strstr(info.status, stops[k][1]) && strstr(info.status, "Stopped"));
            CHECK(!cmd_ok("plugin.evil.loop") && notice_has("was stopped"));
            CHECK(SDL_GetTicks() - t0 < 8000);
        }
    }
    ex_plugins_rescan(P); settle();                                              /* a stopped plugin stays stopped until the user turns it on again */
    CHECK(find_info("evil", &info) && !info.running && strstr(info.status, "Stopped"));
    ex_plugins_enable(P, "evil", 1); settle();
    CHECK(find_info("evil", &info) && info.running);

    /* syntax errors, bytecode, an entry that is missing, and a plugin that never returns while loading */
    plugin("broken", "ui.notice", "this is not lua (");
    turn_on("broken");
    CHECK(find_info("broken", &info) && !info.running && strstr(info.status, "Stopped"));
    plugin("bytecode", "ui.notice", "\x1bLua\x54\x00\x19\x93\r\n\x1a\n");
    turn_on("bytecode");
    CHECK(find_info("bytecode", &info) && !info.running && strstr(info.status, "Stopped"));
    plugin("spin", "ui.notice", "while true do end");
    turn_on("spin");
    CHECK(find_info("spin", &info) && !info.running && strstr(info.status, "time limit exceeded"));
    plugin("nomain", "ui.notice", "");
    { char p[400]; snprintf(p, sizeof p, "%s/.exhaust/plugins/nomain/main.lua", V); ex_platform_remove_tree(p); }
    turn_on("nomain");
    CHECK(find_info("nomain", &info) && !info.running && strstr(info.status, "Stopped"));

    /* events and rename */
    plugin("events", "ui.notice, vault.write, commands.register",
        "exhaust.events.on('file-open', function(p) exhaust.ui.notice('open ' .. p) end)\n"
        "exhaust.events.on('file-rename', function(a, b) exhaust.ui.notice('rename ' .. a .. ' -> ' .. b) end)\n"
        "exhaust.events.on('editor-change', function(p) exhaust.ui.notice('change ' .. p) end)\n"
        "exhaust.events.on('vault-open', function() exhaust.ui.notice('vault open') end)\n"
        "exhaust.commands.register('mv', 'Move', function() local ok, e = exhaust.vault.rename('Note.md', 'Moved.md'); exhaust.ui.notice('mv ' .. tostring(ok) .. ' ' .. tostring(e)) end)\n"
        "exhaust.commands.register('mv2', 'Move again', function() local ok, e = exhaust.vault.rename('Moved.md', 'sub/Inner.md'); exhaust.ui.notice('mv2 ' .. tostring(ok) .. ' ' .. tostring(e)) end)\n");
    turn_on("events");
    if (find_info("events", &info) && !info.running) fprintf(stderr, "events: %s\n", info.status);
    clear_notices();
    ex_plugins_emit(P, "vault-open", NULL, NULL); ex_plugins_emit(P, "file-open", "Note.md", NULL); ex_plugins_emit(P, "file-rename", "a.md", "b.md");
    ex_plugins_emit(P, "editor-change", "Note.md", NULL); ex_plugins_emit(P, "editor-change", "Note.md", NULL);       /* the second is dropped: less than 500 ms */
    settle();
    CHECK(notice_has("vault open") && notice_has("open Note.md") && notice_has("rename a.md -> b.md"));
    {   int i, changes = 0; for (i = 0; i < g_nnotices; i++) if (strstr(g_notices[i], "change Note.md")) changes++; CHECK(changes == 1); }
    clear_notices();
    run_cmd("plugin.events.mv");
    CHECK(notice_has("mv true") && !strcmp(g_rename_from, "Note.md") && !strcmp(g_rename_to, "Moved.md") && has_rel("Moved.md"));
    clear_notices();
    run_cmd("plugin.events.mv2");
    CHECK(notice_has("mv2 nil the file could not be renamed") && has_rel("Moved.md"));      /* a name that is taken is not replaced */

    /* the app can go away while a plugin waits for it, and while it is stuck */
    ex_plugins_free(P);
    P = NULL;
    ex_commands_free(CMDS);
    free(g_text);
    ex_platform_remove_tree(V);
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("plugins ok\n");
    return 0;
}
