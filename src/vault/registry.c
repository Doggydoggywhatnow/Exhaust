/* vaults.ini: the vaults Exhaust remembers (format in docs/formats.md). The file
 * is read into a small INI model that keeps sections and keys it does not know. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_util.h"
#include "exhaust_vault.h"

typedef struct Pair { char *key, *value; } Pair;
typedef struct Section { char *name; Pair *pairs; size_t n; } Section;

struct ExRegistry {
    char *path;
    Section *sec; size_t n;
    /* the entry view handed out by ex_registry_get */
    char *view_id;
};

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static Section *find_section(const ExRegistry *r, const char *name) {
    size_t i;
    for (i = 0; i < r->n; i++) if (strcmp(r->sec[i].name, name) == 0) return &r->sec[i];
    return NULL;
}

static Section *add_section(ExRegistry *r, const char *name) {
    Section *ns = realloc(r->sec, (r->n + 1) * sizeof *ns), *s;
    if (!ns) return NULL;
    r->sec = ns;
    s = &r->sec[r->n];
    memset(s, 0, sizeof *s);
    s->name = dup_str(name);
    if (!s->name) return NULL;
    r->n++;
    return s;
}

static const char *get(const Section *s, const char *key) {
    size_t i;
    if (!s) return NULL;
    for (i = 0; i < s->n; i++) if (strcmp(s->pairs[i].key, key) == 0) return s->pairs[i].value;
    return NULL;
}

static int set(Section *s, const char *key, const char *value) {
    size_t i;
    char *v = dup_str(value);
    if (!v) return -1;
    for (i = 0; i < s->n; i++)
        if (strcmp(s->pairs[i].key, key) == 0) { free(s->pairs[i].value); s->pairs[i].value = v; return 0; }
    {
        Pair *np = realloc(s->pairs, (s->n + 1) * sizeof *np);
        if (!np) { free(v); return -1; }
        s->pairs = np;
        np[s->n].key = dup_str(key);
        np[s->n].value = v;
        if (!np[s->n].key) { free(v); return -1; }
        s->n++;
    }
    return 0;
}

static void free_section(Section *s) {
    size_t i;
    for (i = 0; i < s->n; i++) { free(s->pairs[i].key); free(s->pairs[i].value); }
    free(s->pairs);
    free(s->name);
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

ExRegistry *ex_registry_load(const char *path) {
    ExRegistry *r = calloc(1, sizeof *r);
    char *dir, *text, *line, *next;
    Section *cur = NULL;
    if (!r) return NULL;
    if (path) r->path = dup_str(path);
    else {
        char *d = ex_platform_config_dir();
        size_t n = d ? strlen(d) + 16 : 0;
        if (d) { r->path = malloc(n); if (r->path) snprintf(r->path, n, "%s/vaults.ini", d); }
        free(d);
    }
    if (!r->path) { free(r); return NULL; }
    (void)dir;
    text = ex_platform_read_file(r->path, NULL);
    if (text) {
        for (line = text; line; line = next) {
            char *eq, *l;
            next = strchr(line, '\n');
            if (next) *next++ = 0;
            l = trim(line);
            if (!*l || *l == '#' || *l == ';') continue;
            if (*l == '[') {
                char *close = strchr(l, ']');
                if (!close) continue;
                *close = 0;
                cur = find_section(r, l + 1);
                if (!cur) cur = add_section(r, l + 1);
                continue;
            }
            eq = strchr(l, '=');
            if (!eq || !cur) continue;
            *eq = 0;
            {
                char *key = trim(l), *val = trim(eq + 1), *hash = strstr(val, " #");
                if (hash) { *hash = 0; val = trim(val); }
                set(cur, key, val);
            }
        }
        free(text);
    }
    return r;
}

void ex_registry_free(ExRegistry *r) {
    size_t i;
    if (!r) return;
    for (i = 0; i < r->n; i++) free_section(&r->sec[i]);
    free(r->sec); free(r->path); free(r->view_id);
    free(r);
}

int ex_registry_save(const ExRegistry *r) {
    size_t cap = 256, n = 0, i, k;
    char *buf = malloc(cap), *dir, *slash;
    int rc;
    if (!buf) return -1;
    for (i = 0; i < r->n; i++) {
        const Section *s = &r->sec[i];
        size_t need = strlen(s->name) + 8;
        for (k = 0; k < s->n; k++) need += strlen(s->pairs[k].key) + strlen(s->pairs[k].value) + 8;
        if (n + need > cap) { char *nb; cap = (n + need) * 2; nb = realloc(buf, cap); if (!nb) { free(buf); return -1; } buf = nb; }
        n += (size_t)sprintf(buf + n, "[%s]\n", s->name);
        for (k = 0; k < s->n; k++) n += (size_t)sprintf(buf + n, "%s = %s\n", s->pairs[k].key, s->pairs[k].value);
        buf[n++] = '\n';
    }
    dir = dup_str(r->path);
    if (dir && (slash = strrchr(dir, '/')) != NULL) { *slash = 0; ex_platform_make_dirs(dir); }
    free(dir);
    rc = ex_platform_write_file_atomic(r->path, buf, n);
    free(buf);
    return rc;
}

/* ---- entries: the sections named vault.<id> ---- */

static size_t entry_index(const ExRegistry *r, size_t nth, const Section **out) {
    size_t i, seen = 0;
    for (i = 0; i < r->n; i++)
        if (strncmp(r->sec[i].name, "vault.", 6) == 0) {
            if (seen == nth) { *out = &r->sec[i]; return i; }
            seen++;
        }
    *out = NULL;
    return (size_t)-1;
}

size_t ex_registry_count(const ExRegistry *r) {
    size_t i, c = 0;
    for (i = 0; i < r->n; i++) if (strncmp(r->sec[i].name, "vault.", 6) == 0) c++;
    return c;
}

int ex_registry_get(const ExRegistry *r, size_t i, ExRegistryEntry *out) {
    const Section *s;
    const char *opened;
    entry_index(r, i, &s);
    if (!s) return -1;
    out->id = s->name + 6;
    out->path = get(s, "path") ? get(s, "path") : "";
    out->name = get(s, "name") ? get(s, "name") : "";
    opened = get(s, "opened");
    out->opened = opened ? (int64_t)strtoll(opened, NULL, 10) : 0;
    return 0;
}

static Section *by_id(const ExRegistry *r, const char *id) {
    size_t n = strlen(id) + 8;
    char *name = malloc(n);
    Section *s;
    if (!name) return NULL;
    snprintf(name, n, "vault.%s", id);
    s = find_section(r, name);
    free(name);
    return s;
}

const char *ex_registry_add(ExRegistry *r, const char *folder) {
    char *real = ex_platform_realpath(folder), *name, hex[65], section[80];
    uint8_t digest[32];
    ExPathInfo info;
    Section *s;
    const char *slash;
    if (!real || ex_platform_path_info(real, &info) != 0 || !info.is_dir) { free(real); return NULL; }
    ex_sha256(real, strlen(real), digest);
    ex_hex(digest, 16, hex);
    snprintf(section, sizeof section, "vault.%s", hex);
    s = find_section(r, section);
    if (!s) {
        s = add_section(r, section);
        slash = strrchr(real, '/');
        name = dup_str(slash && slash[1] ? slash + 1 : real);
        if (!s || !name || set(s, "path", real) || set(s, "name", name) || set(s, "opened", "0")) { free(real); free(name); return NULL; }
        free(name);
    }
    free(real);
    return s->name + 6;
}

int ex_registry_remove(ExRegistry *r, const char *id) {
    Section *s = by_id(r, id), *last;
    size_t i;
    if (!s) return -1;
    i = (size_t)(s - r->sec);
    free_section(s);
    memmove(r->sec + i, r->sec + i + 1, (r->n - i - 1) * sizeof *r->sec);
    r->n--;
    last = find_section(r, "vaults");
    if (last && get(last, "last") && strcmp(get(last, "last"), id) == 0) set(last, "last", "");
    return 0;
}

int ex_registry_rename(ExRegistry *r, const char *id, const char *name) {
    Section *s = by_id(r, id);
    return s && *name ? set(s, "name", name) : -1;
}

const char *ex_registry_move(ExRegistry *r, const char *id, const char *new_folder) {
    Section *old = by_id(r, id);
    char *keep_name;
    const char *new_id;
    if (!old) return NULL;
    keep_name = get(old, "name") ? dup_str(get(old, "name")) : NULL;
    new_id = ex_registry_add(r, new_folder);
    if (new_id && strcmp(new_id, id) != 0) {
        Section *ns = by_id(r, new_id);
        if (ns && keep_name) set(ns, "name", keep_name);
        ex_registry_remove(r, id);
        new_id = by_id(r, new_id) ? by_id(r, new_id)->name + 6 : NULL;
    }
    free(keep_name);
    return new_id;
}

int ex_registry_touch(ExRegistry *r, const char *id, int64_t when) {
    Section *s = by_id(r, id), *g;
    char num[32];
    if (!s) return -1;
    snprintf(num, sizeof num, "%lld", (long long)when);
    if (set(s, "opened", num)) return -1;
    g = find_section(r, "vaults");
    if (!g) g = add_section(r, "vaults");
    return g ? set(g, "last", id) : -1;
}

const char *ex_registry_last(const ExRegistry *r) {
    const char *l = get(find_section(r, "vaults"), "last");
    return l && *l ? l : NULL;
}
