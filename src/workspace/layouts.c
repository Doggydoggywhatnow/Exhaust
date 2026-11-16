/* Saved workspace layouts: see include/exhaust_layouts.h and "workspaces.ini" in docs/formats.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ini.h"
#include "exhaust_layouts.h"
#include "exhaust_platform.h"
#include "exhaust_worker.h"

struct ExLayouts {
    char *path, *dir;
    ExIni *ini;
    char **names; size_t n;          /* sorted */
    ExWorker *worker;
    ExQueue *results;
    SDL_AtomicInt pending;
};

typedef struct WriteJob { char *path, *dir; } WriteJob;
enum { JOB_WRITE = 1 };

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ci_cmp(const char *a, const char *b) {
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int ex_layouts_name_valid(const char *name) {
    size_t n, i;
    if (!name) return 0;
    n = strlen(name);
    if (n < 1 || n > 60 || name[0] == ' ' || name[n - 1] == ' ') return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F || c == '[' || c == ']' || c == '/') return 0;
    }
    return 1;
}

static void worker_fn(ExWorker *self, const ExMessage *job, void *user) {
    ExLayouts *l = user;
    (void)self;
    if (job->type == JOB_WRITE && job->payload) {
        WriteJob *w = job->payload;
        char *body = (char *)job->payload + sizeof *w;
        if (w->dir) ex_platform_make_dirs(w->dir);
        if (ex_platform_write_file_atomic(w->path, body, job->size - sizeof *w) != 0) fprintf(stderr, "exhaust: could not write %s\n", w->path);
        SDL_AddAtomicInt(&l->pending, -1);
    }
}

static void queue_write(ExLayouts *l) {
    WriteJob *w;
    ExMessage m;
    size_t len = 0;
    char *text;
    if (!l->path || !l->worker) return;
    text = ex_ini_write(l->ini, &len);
    if (!text) return;
    w = malloc(sizeof *w + len + 1);
    if (!w) { free(text); return; }
    memcpy((char *)w + sizeof *w, text, len);
    free(text);
    w->path = l->path; w->dir = l->dir;
    memset(&m, 0, sizeof m);
    m.type = JOB_WRITE; m.payload = w; m.size = sizeof *w + len; m.free_payload = free;
    SDL_AddAtomicInt(&l->pending, 1);
    if (ex_worker_post(l->worker, &m) != 0) { SDL_AddAtomicInt(&l->pending, -1); free(w); }
}

static int cmp_names(const void *a, const void *b) { return ci_cmp(*(char *const *)a, *(char *const *)b); }

/* The names are the sections [workspace.<name>] that hold a layout. */
static void rebuild_names(ExLayouts *l) {
    size_t i, k;
    for (i = 0; i < l->n; i++) free(l->names[i]);
    free(l->names);
    l->names = NULL; l->n = 0;
    k = ex_ini_section_count(l->ini);
    if (!k) return;
    l->names = calloc(k, sizeof *l->names);
    if (!l->names) return;
    for (i = 0; i < k; i++) {
        const char *s = ex_ini_section_name(l->ini, i);
        if (strncmp(s, "workspace.", 10) != 0 || strchr(s + 10, '/') || !ex_layouts_name_valid(s + 10)) continue;
        l->names[l->n] = dup_str(s + 10);
        if (l->names[l->n]) l->n++;
    }
    if (l->n > 1) qsort(l->names, l->n, sizeof *l->names, cmp_names);
}

ExLayouts *ex_layouts_open(const char *path) {
    ExLayouts *l = calloc(1, sizeof *l);
    char *text = NULL;
    size_t len = 0;
    if (!l) return NULL;
    if (path) {
        char *slash;
        l->path = dup_str(path);
        l->dir = dup_str(path);
        if (l->dir && (slash = strrchr(l->dir, '/')) != NULL) *slash = 0;
        text = ex_platform_read_file(path, &len);
        l->results = ex_queue_create(NULL, NULL);
        l->worker = l->results ? ex_worker_start("layouts", 5, worker_fn, l, l->results) : NULL;
    }
    l->ini = text ? ex_ini_parse(text, len) : ex_ini_new();
    free(text);
    if (!l->ini) { ex_layouts_close(l); return NULL; }
    rebuild_names(l);
    return l;
}

void ex_layouts_flush(ExLayouts *l) { while (l && SDL_GetAtomicInt(&l->pending) > 0) SDL_Delay(1); /* ui-ok: only tests call it; the program waits in ex_layouts_close at exit */ }

void ex_layouts_close(ExLayouts *l) {
    size_t i;
    if (!l) return;
    if (l->worker) ex_worker_stop(l->worker);
    if (l->results) ex_queue_destroy(l->results);
    for (i = 0; i < l->n; i++) free(l->names[i]);
    free(l->names);
    ex_ini_free(l->ini);
    free(l->path); free(l->dir);
    free(l);
}

size_t ex_layouts_count(const ExLayouts *l) { return l ? l->n : 0; }
const char *ex_layouts_name(const ExLayouts *l, size_t i) { return l && i < l->n ? l->names[i] : NULL; }

const char *ex_layouts_find(const ExLayouts *l, const char *name) {
    size_t i;
    if (!l || !name) return NULL;
    for (i = 0; i < l->n; i++) if (ci_cmp(l->names[i], name) == 0) return l->names[i];
    return NULL;
}

int64_t ex_layouts_saved_time(const ExLayouts *l, const char *name) {
    char sec[100];
    const char *c = ex_layouts_find(l, name);
    if (!c) return 0;
    snprintf(sec, sizeof sec, "workspace.%s", c);
    return (int64_t)ex_ini_get_int(l->ini, sec, "saved", 0);
}

/* The sections of the session file that a layout holds. */
static int layout_section(const char *s) {
    return !strcmp(s, "layout") || !strncmp(s, "window.", 7) || !strncmp(s, "split.", 6) || !strncmp(s, "group.", 6) || !strncmp(s, "tab.", 4);
}
static int window_rect_key(const char *k) { return !strcmp(k, "x") || !strcmp(k, "y") || !strcmp(k, "width") || !strcmp(k, "height") || !strcmp(k, "maximized"); }

static void remove_layout_sections(ExLayouts *l, const char *name) {
    char head[100], pre[100];
    size_t i, plen;
    snprintf(head, sizeof head, "workspace.%s", name);
    snprintf(pre, sizeof pre, "workspace.%s/", name);
    plen = strlen(pre);
    for (i = ex_ini_section_count(l->ini); i-- > 0;) {
        const char *s = ex_ini_section_name(l->ini, i);
        char copy[200];
        if (strcmp(s, head) != 0 && strncmp(s, pre, plen) != 0) continue;
        snprintf(copy, sizeof copy, "%s", s);
        ex_ini_remove_section(l->ini, copy);
    }
}

int ex_layouts_save(ExLayouts *l, const char *name, const ExWorkspace *ws, int64_t now) {
    size_t len = 0, i, k;
    char *text;
    ExIni *cur;
    const char *same;
    char head[100], sec[200], nm[64];
    if (!l || !ws || !ex_layouts_name_valid(name)) return -1;
    text = ex_ws_serialize(ws, &len);
    if (!text) return -1;
    cur = ex_ini_parse(text, len);
    free(text);
    if (!cur) return -1;
    same = ex_layouts_find(l, name);
    snprintf(nm, sizeof nm, "%s", same ? same : name);             /* a layout of the same name keeps the spelling it had */
    remove_layout_sections(l, nm);
    name = nm;
    snprintf(head, sizeof head, "workspace.%s", name);
    ex_ini_set_int(l->ini, head, "saved", now);
    for (i = 0; i < ex_ini_section_count(cur); i++) {
        const char *s = ex_ini_section_name(cur, i);
        if (!layout_section(s)) continue;
        snprintf(sec, sizeof sec, "workspace.%s/%s", name, s);
        for (k = 0; k < ex_ini_key_count(cur, s); k++) {
            const char *key = ex_ini_key_name(cur, s, k);
            if (!strncmp(s, "window.", 7) && window_rect_key(key)) continue;
            ex_ini_set(l->ini, sec, key, ex_ini_get(cur, s, key));
        }
        if (!ex_ini_has_section(l->ini, sec)) ex_ini_set(l->ini, sec, "_", "");     /* an empty section would be lost */
    }
    ex_ini_free(cur);
    rebuild_names(l);
    queue_write(l);
    return 0;
}

int ex_layouts_delete(ExLayouts *l, const char *name) {
    const char *same = l ? ex_layouts_find(l, name) : NULL;
    char keep[100];
    if (!same) return -1;
    snprintf(keep, sizeof keep, "%s", same);
    remove_layout_sections(l, keep);
    rebuild_names(l);
    queue_write(l);
    return 0;
}

int ex_layouts_load(const ExLayouts *l, const char *name, ExWorkspace *ws, int (*exists)(void *user, const char *path), void *user) {
    const char *same = l ? ex_layouts_find(l, name) : NULL;
    size_t len = 0, i, k;
    char *text, pre[100], *out;
    size_t plen, outlen = 0;
    ExIni *cur, *next;
    int main_x = 0, main_y = 0, main_w = 0, main_h = 0, main_max = 0, have_rect = 0, saw_window = 0;
    if (!same || !ws) return -1;
    snprintf(pre, sizeof pre, "workspace.%s/", same);
    plen = strlen(pre);
    for (i = 0; i < ex_ini_section_count(l->ini); i++) {
        const char *s = ex_ini_section_name(l->ini, i);
        if (!strncmp(s, pre, plen) && !strncmp(s + plen, "window.", 7)) saw_window = 1;
    }
    if (!saw_window) return -1;                                  /* nothing to show: not a layout we can read */
    text = ex_ws_serialize(ws, &len);
    if (!text) return -1;
    cur = ex_ini_parse(text, len);
    free(text);
    if (!cur) return -1;
    next = ex_ini_new();
    if (!next) { ex_ini_free(cur); return -1; }
    /* what stays: every section that is not a layout section, and the main window's place */
    for (i = 0; i < ex_ini_section_count(cur); i++) {
        const char *s = ex_ini_section_name(cur, i);
        if (!strncmp(s, "window.", 7) && ex_ini_get_bool(cur, s, "main", 0) && !have_rect) {
            main_x = (int)ex_ini_get_int(cur, s, "x", 0); main_y = (int)ex_ini_get_int(cur, s, "y", 0);
            main_w = (int)ex_ini_get_int(cur, s, "width", 0); main_h = (int)ex_ini_get_int(cur, s, "height", 0);
            main_max = ex_ini_get_bool(cur, s, "maximized", 0);
            have_rect = 1;
        }
        if (layout_section(s)) continue;
        for (k = 0; k < ex_ini_key_count(cur, s); k++) ex_ini_set(next, s, ex_ini_key_name(cur, s, k), ex_ini_get(cur, s, ex_ini_key_name(cur, s, k)));
        if (!ex_ini_has_section(next, s)) ex_ini_set(next, s, "_", "");
    }
    for (i = 0; i < ex_ini_section_count(l->ini); i++) {
        const char *s = ex_ini_section_name(l->ini, i);
        char sec[120];
        if (strncmp(s, pre, plen) != 0) continue;
        snprintf(sec, sizeof sec, "%s", s + plen);
        if (!layout_section(sec)) continue;
        for (k = 0; k < ex_ini_key_count(l->ini, s); k++) {
            const char *key = ex_ini_key_name(l->ini, s, k);
            if (!strcmp(key, "_")) continue;
            ex_ini_set(next, sec, key, ex_ini_get(l->ini, s, key));
        }
        if (!ex_ini_has_section(next, sec)) ex_ini_set(next, sec, "_", "");
        if (!strncmp(sec, "window.", 7) && have_rect && ex_ini_get_bool(l->ini, s, "main", 0)) {
            ex_ini_set_int(next, sec, "x", main_x); ex_ini_set_int(next, sec, "y", main_y);
            ex_ini_set_int(next, sec, "width", main_w); ex_ini_set_int(next, sec, "height", main_h);
            ex_ini_set(next, sec, "maximized", main_max ? "true" : "false");
        }
    }
    ex_ini_free(cur);
    out = ex_ini_write(next, &outlen);
    ex_ini_free(next);
    if (!out) return -1;
    {
        int rc = ex_ws_deserialize(ws, out, outlen);
        free(out);
        if (rc != 0) return -1;
    }
    if (exists) {   /* notes that are gone: their tabs go too */
        for (i = ex_ws_tab_count(ws); i-- > 0;) {
            ExId id = ex_ws_tab_at(ws, i);
            const ExTab *t = ex_ws_tab(ws, id);
            if (t && t->path && t->path[0] && !exists(user, t->path)) ex_ws_close_tab(ws, id);
        }
    }
    return 0;
}
