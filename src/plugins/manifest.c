/* Plugin manifests and the user's approvals (sub-slice 8b): see docs/formats.md (Plugin manifest, The runtime). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_plugins.h"

static const char *const PERMS[] = { "vault.read", "vault.write", "editor.read", "editor.write", "commands.register", "ui.pane", "ui.notice", "settings.own", "network" };

int ex_plugin_permission_valid(const char *name) {
    size_t i;
    for (i = 0; name && i < sizeof PERMS / sizeof *PERMS; i++) if (!strcmp(PERMS[i], name)) return 1;
    return 0;
}

static void trim(char *s) {
    size_t n = strlen(s), a = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
    while (s[a] == ' ' || s[a] == '\t') a++;
    if (a) memmove(s, s + a, strlen(s + a) + 1);
}

static int id_ok(const char *id) {
    size_t i, n = strlen(id);
    if (n == 0 || n > 60) return 0;
    for (i = 0; i < n; i++) if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) return 0;
    return id[0] != '-' && id[n - 1] != '-';
}
static int version_ok(const char *v) {
    int dots = 0, digits = 0;
    for (; *v; v++) { if (*v == '.') { if (!digits) return 0; dots++; digits = 0; } else if (*v >= '0' && *v <= '9') digits++; else return 0; }
    return dots == 2 && digits > 0;
}
static int entry_ok(const char *e) {
    size_t i, n = strlen(e);
    if (n < 5 || n > 100 || strcmp(e + n - 4, ".lua")) return 0;
    if (e[0] == '/' || e[0] == '.' || strchr(e, '\\') || strchr(e, ':')) return 0;
    for (i = 0; i + 1 < n; i++) if (e[i] == '.' && e[i + 1] == '.') return 0;
    return 1;
}

static void fail(ExPluginInfo *o, const char *why) { snprintf(o->error, sizeof o->error, "%s", why); o->valid = 0; }

int ex_plugin_read_manifest(const char *folder, const char *folder_name, ExPluginInfo *o) {
    char path[1400], *text;
    size_t len = 0;
    ExIni *ini;
    const char *v;
    memset(o, 0, sizeof *o);
    snprintf(o->id, sizeof o->id, "%s", folder_name);
    snprintf(o->name, sizeof o->name, "%s", folder_name);
    snprintf(path, sizeof path, "%s/manifest.ini", folder);
    text = ex_platform_read_file(path, &len);
    if (!text) { fail(o, "no manifest.ini"); return 0; }
    if (len > 16384) { free(text); fail(o, "manifest.ini is too big"); return 0; }
    ini = ex_ini_parse(text, len);
    free(text);
    if (!ini) { fail(o, "out of memory"); return 0; }
    if (!ex_ini_has_section(ini, "plugin")) { ex_ini_free(ini); fail(o, "manifest.ini has no [plugin] section"); return 0; }
#define GET(dst, key) do { v = ex_ini_get(ini, "plugin", key); if (v) { snprintf(dst, sizeof dst, "%s", v); trim(dst); } } while (0)
    { char idbuf[64] = ""; GET(idbuf, "id"); if (!id_ok(idbuf)) { ex_ini_free(ini); fail(o, "the id is missing or has characters other than lowercase letters, digits and hyphens"); return 0; }
      if (strcmp(idbuf, folder_name)) { ex_ini_free(ini); fail(o, "the id is not the name of the plugin's folder"); return 0; }
      snprintf(o->id, sizeof o->id, "%s", idbuf); }
    o->name[0] = 0; GET(o->name, "name");
    if (!o->name[0]) { ex_ini_free(ini); snprintf(o->name, sizeof o->name, "%s", o->id); fail(o, "the name is missing"); return 0; }
    GET(o->version, "version");
    if (!version_ok(o->version)) { ex_ini_free(ini); fail(o, "the version is missing or is not x.y.z"); return 0; }
    o->api = (int)ex_ini_get_int(ini, "plugin", "api", 0);
    if (o->api != 1) { ex_ini_free(ini); fail(o, "api must be 1 (this version of Exhaust knows API version 1)"); return 0; }
    GET(o->author, "author"); GET(o->description, "description");
    snprintf(o->entry, sizeof o->entry, "main.lua");
    v = ex_ini_get(ini, "plugin", "entry");
    if (v && *v) { snprintf(o->entry, sizeof o->entry, "%s", v); trim(o->entry); if (!entry_ok(o->entry)) { ex_ini_free(ini); fail(o, "the entry is not a relative path of a .lua file"); return 0; } }
    v = ex_ini_get(ini, "plugin", "permissions");
    if (v) {
        char list[400];
        const char *q = list;
        snprintf(list, sizeof list, "%s", v);
        while (*q) {
            char tok[40];
            size_t tl = 0, k;
            int dup = 0;
            while (*q == ',' || *q == ' ' || *q == '\t') q++;
            while (*q && *q != ',' && *q != ' ' && *q != '\t' && tl < sizeof tok - 1) tok[tl++] = *q++;
            tok[tl] = 0;
            if (!tl) break;
            if (!ex_plugin_permission_valid(tok)) { char m[200]; ex_ini_free(ini); snprintf(m, sizeof m, "unknown permission \"%.60s\"", tok); fail(o, m); return 0; }
            for (k = 0; k < o->nperm; k++) if (!strcmp(o->permissions[k], tok)) dup = 1;
            if (!dup && o->nperm < EX_PLUGIN_MAX_PERMS) snprintf(o->permissions[o->nperm++], sizeof o->permissions[0], "%.23s", tok);
        }
    }
#undef GET
    ex_ini_free(ini);
    o->valid = 1;
    return 1;
}
