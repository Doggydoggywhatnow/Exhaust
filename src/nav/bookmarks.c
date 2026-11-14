#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_nav.h"
#include "exhaust_worker.h"
#include "../index/index_internal.h"

struct ExBookmarks {
    char *path;
    ExBookmark *v; size_t n, cap;
    int next_id;
    ExIni *unknown;                 /* sections and keys this version does not know, written back */
    ExWorker *worker; ExQueue *results;
    int writes_queued;
};

static void bm_free_fields(ExBookmark *b) { free(b->title); free(b->path); free(b->heading); free(b->block); free(b->query); }
static char *dup_or_null(const char *s) { return s ? ex_strdup(s) : NULL; }

static const char *const TYPES[] = {"note", "heading", "block", "search", "graph", "group"};

/* "%" and the characters an INI value cannot hold (" #", line breaks) are written as %XX */
static char *enc(const char *s) {
    size_t n = strlen(s), i, o = 0;
    char *out = malloc(n * 3 + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '%' || s[i] == '#' || s[i] == '\n' || s[i] == '\r') o += (size_t)sprintf(out + o, "%%%02X", (unsigned char)s[i]);
        else out[o++] = s[i];
    }
    out[o] = 0;
    return out;
}
static char *dec(const char *s) {
    size_t n = strlen(s), i, o = 0;
    char *out = malloc(n + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (s[i] == '%' && i + 2 < n + 1 && sscanf(s + i + 1, "%2x", &v) == 1) { out[o++] = (char)v; i += 2; }
        else out[o++] = s[i];
    }
    out[o] = 0;
    return out;
}

static void write_worker(ExWorker *self, const ExMessage *job, void *user) {
    ExBookmarks *b = user;
    (void)self;
    if (job->payload) ex_platform_save_file(b->path, job->payload, job->size, NULL);
}

static char *serialize(const ExBookmarks *b, size_t *len) {
    ExIni *ini = ex_ini_new();
    size_t i, k;
    char sec[32], num[16], *text;
    if (!ini) return NULL;
    for (i = 0; i < ex_ini_section_count(b->unknown); i++) {
        const char *name = ex_ini_section_name(b->unknown, i);
        for (k = 0; k < ex_ini_key_count(b->unknown, name); k++) ex_ini_set(ini, name, ex_ini_key_name(b->unknown, name, k), ex_ini_get(b->unknown, name, ex_ini_key_name(b->unknown, name, k)));
    }
    ex_ini_set_int(ini, "bookmarks", "next_id", b->next_id);
    for (i = 0; i < b->n; i++) {
        const ExBookmark *m = &b->v[i];
        char *t = enc(m->title ? m->title : "");
        snprintf(sec, sizeof sec, "bookmark.%d", m->id);
        ex_ini_set(ini, sec, "type", TYPES[m->type]);
        if (t) ex_ini_set(ini, sec, "title", t);
        free(t);
        if (m->parent) { snprintf(num, sizeof num, "%d", m->parent); ex_ini_set(ini, sec, "parent", num); }
        { const char *key[3] = {"path", "heading", "block"}, *val[3]; int j; val[0] = m->path; val[1] = m->heading; val[2] = m->block;
          for (j = 0; j < 3; j++) if (val[j]) { char *e = enc(val[j]); if (e) ex_ini_set(ini, sec, key[j], e); free(e); } }
        if (m->query) { char *e = enc(m->query); if (e) ex_ini_set(ini, sec, "query", e); free(e); }
    }
    text = ex_ini_write(ini, len);
    ex_ini_free(ini);
    return text;
}

static void persist(ExBookmarks *b) {
    size_t len = 0;
    char *text = serialize(b, &len);
    if (!text) return;
    if (b->worker) {
        ExMessage m;
        memset(&m, 0, sizeof m);
        m.type = 1; m.payload = text; m.size = len; m.free_payload = free;
        if (ex_worker_post(b->worker, &m) != 0) free(text);
    } else {
        char *dir = ex_strdup(b->path), *slash = dir ? strrchr(dir, '/') : NULL;
        if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
        free(dir);
        ex_platform_save_file(b->path, text, len, NULL);
        free(text);
    }
}

ExBookmarks *ex_bookmarks_load(const char *root, int async) {
    ExBookmarks *b = calloc(1, sizeof *b);
    char *text, *dir, *slash;
    size_t len = 0, i, k;
    ExIni *ini;
    if (!b) return NULL;
    b->path = malloc(strlen(root) + 32);
    if (!b->path) { free(b); return NULL; }
    sprintf(b->path, "%s/.exhaust/bookmarks.ini", root);
    b->unknown = ex_ini_new();
    b->next_id = 1;
    text = ex_platform_read_file(b->path, &len);
    ini = text ? ex_ini_parse(text, len) : ex_ini_new();
    free(text);
    if (ini) {
        b->next_id = (int)ex_ini_get_int(ini, "bookmarks", "next_id", 1);
        for (i = 0; i < ex_ini_section_count(ini); i++) {
            const char *sec = ex_ini_section_name(ini, i), *type;
            int t = -1, id;
            ExBookmark *m;
            if (!strncmp(sec, "bookmark.", 9)) {
                type = ex_ini_get(ini, sec, "type");
                for (k = 0; type && k < 6; k++) if (!strcmp(type, TYPES[k])) t = (int)k;
                id = atoi(sec + 9);
                if (t < 0 || id <= 0) continue;
                if (b->n == b->cap) { size_t nc = b->cap ? b->cap * 2 : 16; ExBookmark *nv = realloc(b->v, nc * sizeof *nv); if (!nv) break; b->v = nv; b->cap = nc; }
                m = &b->v[b->n++];
                memset(m, 0, sizeof *m);
                m->id = id; m->type = t;
                m->title = dec(ex_ini_get(ini, sec, "title") ? ex_ini_get(ini, sec, "title") : "");
                m->parent = (int)ex_ini_get_int(ini, sec, "parent", 0);
                if (ex_ini_get(ini, sec, "path")) m->path = dec(ex_ini_get(ini, sec, "path"));
                if (ex_ini_get(ini, sec, "heading")) m->heading = dec(ex_ini_get(ini, sec, "heading"));
                if (ex_ini_get(ini, sec, "block")) m->block = dec(ex_ini_get(ini, sec, "block"));
                if (ex_ini_get(ini, sec, "query")) m->query = dec(ex_ini_get(ini, sec, "query"));
                if (id >= b->next_id) b->next_id = id + 1;
            } else if (strcmp(sec, "bookmarks")) {
                for (k = 0; k < ex_ini_key_count(ini, sec); k++) ex_ini_set(b->unknown, sec, ex_ini_key_name(ini, sec, k), ex_ini_get(ini, sec, ex_ini_key_name(ini, sec, k)));
            }
        }
        ex_ini_free(ini);
    }
    if (async) {
        dir = ex_strdup(b->path); slash = dir ? strrchr(dir, '/') : NULL;
        if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
        free(dir);
        b->results = ex_queue_create(NULL, NULL);
        b->worker = b->results ? ex_worker_start("bookmarks", 4, write_worker, b, b->results) : NULL;
    }
    return b;
}

void ex_bookmarks_flush(ExBookmarks *b) {
    if (!b->worker) return;
    ex_worker_stop(b->worker);                      /* waits for the queued writes */
    b->worker = ex_worker_start("bookmarks", 4, write_worker, b, b->results);
}

void ex_bookmarks_free(ExBookmarks *b) {
    size_t i;
    if (!b) return;
    if (b->worker) ex_worker_stop(b->worker);
    ex_queue_destroy(b->results);
    for (i = 0; i < b->n; i++) bm_free_fields(&b->v[i]);
    free(b->v); ex_ini_free(b->unknown); free(b->path); free(b);
}

size_t ex_bookmarks_count(const ExBookmarks *b) { return b->n; }
const ExBookmark *ex_bookmarks_at(const ExBookmarks *b, size_t i) { return i < b->n ? &b->v[i] : NULL; }
const ExBookmark *ex_bookmarks_find(const ExBookmarks *b, int id) { size_t i; for (i = 0; i < b->n; i++) if (b->v[i].id == id) return &b->v[i]; return NULL; }

static int index_of(const ExBookmarks *b, int id) { size_t i; for (i = 0; i < b->n; i++) if (b->v[i].id == id) return (int)i; return -1; }

int ex_bookmarks_add(ExBookmarks *b, const ExBookmark *what, int parent) {
    ExBookmark *m;
    if (parent && (index_of(b, parent) < 0 || b->v[index_of(b, parent)].type != EX_BM_GROUP)) return 0;
    if (b->n == b->cap) { size_t nc = b->cap ? b->cap * 2 : 16; ExBookmark *nv = realloc(b->v, nc * sizeof *nv); if (!nv) return 0; b->v = nv; b->cap = nc; }
    m = &b->v[b->n++];
    memset(m, 0, sizeof *m);
    m->id = b->next_id++;
    m->type = what->type;
    m->parent = parent;
    m->path = dup_or_null(what->path); m->heading = dup_or_null(what->heading); m->block = dup_or_null(what->block); m->query = dup_or_null(what->query);
    if (what->title && what->title[0]) m->title = ex_strdup(what->title);
    else if (!what->heading && !what->query && what->path) {                  /* a note: its name without ".md" */
        const char *sl = strrchr(what->path, '/'), *base = sl ? sl + 1 : what->path;
        size_t bn = strlen(base);
        if (bn > 3 && !strcmp(base + bn - 3, ".md")) bn -= 3;
        m->title = malloc(bn + 1);
        if (m->title) { memcpy(m->title, base, bn); m->title[bn] = 0; }
    } else m->title = ex_strdup(what->heading ? what->heading : what->query ? what->query : what->path ? what->path : "Bookmark");
    persist(b);
    return m->id;
}

static void collect_descendants(const ExBookmarks *b, int id, int *ids, size_t *n) {
    size_t i;
    for (i = 0; i < b->n; i++) if (b->v[i].parent == id) { ids[(*n)++] = b->v[i].id; }
}

int ex_bookmarks_remove(ExBookmarks *b, int id) {
    int i = index_of(b, id);
    size_t k;
    if (i < 0) return -1;
    for (k = 0; k < b->n; k++) if (b->v[k].parent == id) b->v[k].parent = b->v[i].parent;          /* the children of a group move up */
    bm_free_fields(&b->v[i]);
    memmove(&b->v[i], &b->v[i + 1], (b->n - (size_t)i - 1) * sizeof *b->v);
    b->n--;
    persist(b);
    return 0;
}

int ex_bookmarks_rename(ExBookmarks *b, int id, const char *title) {
    int i = index_of(b, id);
    char *t;
    if (i < 0 || !title || !title[0]) return -1;
    t = ex_strdup(title);
    if (!t) return -1;
    free(b->v[i].title); b->v[i].title = t;
    persist(b);
    return 0;
}

int ex_bookmarks_move(ExBookmarks *b, int id, int parent, int before) {
    int i = index_of(b, id), p;
    ExBookmark tmp;
    size_t at;
    if (i < 0 || id == parent || id == before) return -1;
    if (parent) {                                                   /* the group must exist, and must not be inside the bookmark moved */
        int q = index_of(b, parent);
        if (q < 0 || b->v[q].type != EX_BM_GROUP) return -1;
        for (p = parent; p; p = b->v[index_of(b, p)].parent) if (p == id) return -1;
    }
    if (before) { int q = index_of(b, before); if (q < 0 || b->v[q].parent != parent) return -1; }
    tmp = b->v[i];
    memmove(&b->v[i], &b->v[i + 1], (b->n - (size_t)i - 1) * sizeof *b->v);
    b->n--;
    tmp.parent = parent;
    if (before) at = (size_t)index_of(b, before);
    else {                                                          /* the end of that group: after the last bookmark in it, or at the end of the list */
        at = b->n;
    }
    memmove(&b->v[at + 1], &b->v[at], (b->n - at) * sizeof *b->v);
    b->v[at] = tmp;
    b->n++;
    persist(b);
    (void)collect_descendants;
    return 0;
}

int ex_bookmarks_follow_rename(ExBookmarks *b, const char *from, const char *to) {
    size_t i, fl = strlen(from);
    int changed = 0;
    for (i = 0; i < b->n; i++) {
        ExBookmark *m = &b->v[i];
        char *np;
        if (!m->path) continue;
        if (!strcmp(m->path, from)) np = ex_strdup(to);
        else if (!strncmp(m->path, from, fl) && m->path[fl] == '/') { np = malloc(strlen(to) + strlen(m->path + fl) + 1); if (np) { strcpy(np, to); strcat(np, m->path + fl); } }
        else continue;
        if (!np) continue;
        free(m->path); m->path = np;
        changed++;
    }
    if (changed) persist(b);
    return changed;
}
