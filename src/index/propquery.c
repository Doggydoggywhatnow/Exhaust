/* Tags and properties: reading them back from the index, and the vault's property types. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_nfc.h"
#include "exhaust_platform.h"
#include "exhaust_props.h"
#include "store_internal.h"

static const char *const TYPE_NAMES[] = {"text", "list", "number", "checkbox", "date", "datetime", "links"};

const char *ex_prop_type_name(int type) { return type >= 0 && type <= EX_PROP_LINKS ? TYPE_NAMES[type] : "text"; }

int ex_prop_type_from_name(const char *name) {
    int i;
    for (i = 0; i <= EX_PROP_LINKS; i++) if (!strcmp(name, TYPE_NAMES[i])) return i;
    return -1;
}

static char *dupcol(sqlite3_stmt *st, int c) { const char *t = (const char *)sqlite3_column_text(st, c); return ex_strdup(t ? t : ""); }

void ex_tags_free(ExTagInfo *t, size_t n) { size_t i; for (i = 0; i < n; i++) free(t[i].tag); free(t); }
void ex_paths_free(char **p, size_t n) { size_t i; for (i = 0; i < n; i++) free(p[i]); free(p); }

int ex_tags_all(ExIndex *r, ExTagInfo **out, size_t *n) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT min(tag), count(DISTINCT file_id) FROM tags GROUP BY tag_key ORDER BY tag_key");
    ExTagInfo *v = NULL;
    size_t cnt = 0, cap = 0;
    *out = NULL; *n = 0;
    if (!st) return -1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 32; ExTagInfo *nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt].tag = dupcol(st, 0);
        v[cnt].notes = (long)sqlite3_column_int64(st, 1);
        cnt++;
    }
    sqlite3_finalize(st);
    *out = v; *n = cnt;
    return 0;
}

int ex_tag_notes(ExIndex *r, const char *tag, int nested, char ***paths, size_t *n) {
    sqlite3_stmt *st;
    char *key;
    char **v = NULL;
    size_t cnt = 0, cap = 0;
    *paths = NULL; *n = 0;
    while (*tag == '#') tag++;
    key = ex_name_key(tag, strlen(tag));
    if (!key) return -1;
    st = ex_db_prepare(r, nested ?
        "SELECT DISTINCT f.path FROM tags t JOIN files f ON f.id = t.file_id WHERE t.tag_key = ?1 OR substr(t.tag_key, 1, length(?1) + 1) = ?1 || '/' ORDER BY f.path" :
        "SELECT DISTINCT f.path FROM tags t JOIN files f ON f.id = t.file_id WHERE t.tag_key = ?1 ORDER BY f.path");
    if (!st) { free(key); return -1; }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 32; char **nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt++] = dupcol(st, 0);
    }
    sqlite3_finalize(st);
    free(key);
    *paths = v; *n = cnt;
    return 0;
}

void ex_note_props_free(ExNoteProp *p, size_t n) { size_t i; for (i = 0; i < n; i++) { free(p[i].name); free(p[i].value); } free(p); }

int ex_note_props(ExIndex *r, const char *path, ExNoteProp **out, size_t *n) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT p.name, p.type, p.value, p.start, p.end_, p.line FROM props p JOIN files f ON f.id = p.file_id WHERE f.path = ?1 ORDER BY p.ord");
    ExNoteProp *v = NULL;
    size_t cnt = 0, cap = 0;
    *out = NULL; *n = 0;
    if (!st) return -1;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 8; ExNoteProp *nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt].name = dupcol(st, 0); v[cnt].type = sqlite3_column_int(st, 1); v[cnt].value = dupcol(st, 2);
        v[cnt].start = (size_t)sqlite3_column_int64(st, 3); v[cnt].end = (size_t)sqlite3_column_int64(st, 4); v[cnt].line = (uint32_t)sqlite3_column_int(st, 5);
        cnt++;
    }
    sqlite3_finalize(st);
    *out = v; *n = cnt;
    return 0;
}

void ex_props_all_free(ExPropInfo *p, size_t n) { size_t i; for (i = 0; i < n; i++) free(p[i].name); free(p); }

int ex_props_all(ExIndex *r, const char *root, ExPropInfo **out, size_t *n) {
    /* for every name (without regard to case): the notes that have it and the most common type among them */
    sqlite3_stmt *st = ex_db_prepare(r,
        "SELECT min(name), type, count(*) AS c FROM props GROUP BY name_key, type ORDER BY name_key, c DESC, type");
    ExPropInfo *v = NULL;
    size_t cnt = 0, cap = 0;
    char *last = NULL;
    *out = NULL; *n = 0;
    if (!st) return -1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        long c = (long)sqlite3_column_int64(st, 2);
        if (last && !strcmp(last, name) && cnt) { v[cnt - 1].notes += c; continue; }   /* a less common type of the same name */
        free(last);
        last = ex_strdup(name);
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 16; ExPropInfo *nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt].name = ex_strdup(name);
        v[cnt].type = sqlite3_column_int(st, 1);
        v[cnt].notes = c;
        cnt++;
    }
    sqlite3_finalize(st);
    free(last);
    if (root) {
        size_t i;
        for (i = 0; i < cnt; i++) { int t = ex_prop_type_get(root, v[i].name); if (t >= 0) v[i].type = t; }
    }
    *out = v; *n = cnt;
    return 0;
}

int ex_note_cssclasses(ExIndex *r, const char *path, char ***classes, size_t *n) {
    ExNoteProp *p;
    size_t np, i, cnt = 0;
    char **v = NULL;
    *classes = NULL; *n = 0;
    if (ex_note_props(r, path, &p, &np) != 0) return -1;
    for (i = 0; i < np; i++) {
        const char *s;
        if (strcmp(p[i].name, "cssclasses") && strcmp(p[i].name, "cssclass")) continue;
        s = p[i].value;
        while (*s) {
            size_t a = 0, k;
            char **nv;
            while (s[a] == ' ' || s[a] == ',' || s[a] == '\n') a++;
            k = a;
            while (s[k] && s[k] != ' ' && s[k] != ',' && s[k] != '\n') k++;
            if (k > a) {
                nv = realloc(v, (cnt + 1) * sizeof *v);
                if (!nv) break;
                v = nv;
                v[cnt] = malloc(k - a + 1);
                if (!v[cnt]) break;
                memcpy(v[cnt], s + a, k - a); v[cnt][k - a] = 0; cnt++;
            }
            s += k;
        }
    }
    ex_note_props_free(p, np);
    *classes = v; *n = cnt;
    return 0;
}

/* ---- .exhaust/types.ini ---- */

static char *types_path(const char *root) {
    size_t n = strlen(root) + 32;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/.exhaust/types.ini", root);
    return p;
}

int ex_prop_type_get(const char *root, const char *name) {
    char *path = types_path(root), *text;
    size_t len = 0;
    ExIni *ini;
    const char *v;
    int t = -1;
    if (!path) return -1;
    text = ex_platform_read_file(path, &len);
    free(path);
    if (!text) return -1;
    ini = ex_ini_parse(text, len);
    free(text);
    if (!ini) return -1;
    v = ex_ini_get(ini, "types", name);
    if (v) t = ex_prop_type_from_name(v);
    ex_ini_free(ini);
    return t;
}

int ex_prop_type_set(const char *root, const char *name, int type) {
    char *path = types_path(root), *text, *out;
    size_t len = 0, olen = 0;
    int rc;
    if (!path) return -1;
    text = ex_platform_read_file(path, &len);
    if (!text) { text = ex_strdup(""); len = 0; }
    out = ex_ini_patch(text, len, "types", name, ex_prop_type_name(type), &olen);
    free(text);
    if (!out) { free(path); return -1; }
    { char *dir = ex_strdup(path), *slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); } free(dir); }
    rc = ex_platform_save_file(path, out, olen, NULL);
    free(out); free(path);
    return rc;
}
