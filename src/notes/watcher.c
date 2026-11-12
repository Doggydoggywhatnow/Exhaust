#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#ifdef __linux__
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"
#include "exhaust_worker.h"

/* ---- the operating system's backend (inotify on Linux) ---- */

#ifdef __linux__

typedef struct Ino {
    int fd;
    int *wd;
    char **dir;          /* the watched folder (vault-relative) for each watch */
    size_t n, cap;
    char root[1024];
    char **pending; size_t npending, cappending;
} Ino;

static void ino_push(Ino *in, const char *rel) {
    if (in->npending == in->cappending) {
        size_t nc = in->cappending ? in->cappending * 2 : 32;
        char **np = realloc(in->pending, nc * sizeof *np);
        if (!np) return;
        in->pending = np;
        in->cappending = nc;
    }
    in->pending[in->npending] = malloc(strlen(rel) + 1);
    if (in->pending[in->npending]) strcpy(in->pending[in->npending++], rel);
}

typedef struct IterNames { char **v; size_t n, cap; } IterNames;

static SDL_EnumerationResult collect_names(void *ud, const char *dirname, const char *fname) {
    IterNames *nm = ud;
    (void)dirname;
    if (nm->n == nm->cap) {
        size_t nc = nm->cap ? nm->cap * 2 : 16;
        char **nv = realloc(nm->v, nc * sizeof *nv);
        if (!nv) return SDL_ENUM_FAILURE;
        nm->v = nv;
        nm->cap = nc;
    }
    nm->v[nm->n] = malloc(strlen(fname) + 1);
    if (!nm->v[nm->n]) return SDL_ENUM_FAILURE;
    strcpy(nm->v[nm->n++], fname);
    return SDL_ENUM_CONTINUE;
}

static int ino_add_dir(Ino *in, const char *rel, int depth);

static int ino_add_dir(Ino *in, const char *rel, int depth) {
    char abs[2048];
    int wd;
    ExPathInfo info;
    if (depth > 30) return 0;
    snprintf(abs, sizeof abs, "%s%s%s", in->root, rel[0] ? "/" : "", rel);
    wd = inotify_add_watch(in->fd, abs, IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF);
    if (wd < 0) return -1;                        /* ENOSPC: the OS refuses more watches */
    if (in->n == in->cap) {
        size_t nc = in->cap ? in->cap * 2 : 64;
        int *nw = realloc(in->wd, nc * sizeof *nw);
        char **nd = realloc(in->dir, nc * sizeof *nd);
        if (!nw || !nd) return -1;
        in->wd = nw; in->dir = nd; in->cap = nc;
    }
    in->wd[in->n] = wd;
    in->dir[in->n] = malloc(strlen(rel) + 1);
    if (!in->dir[in->n]) return -1;
    strcpy(in->dir[in->n++], rel);
    /* its subfolders */
    {
        IterNames names = {0};
        size_t k;
        int rc = 0;
        SDL_EnumerateDirectory(abs, collect_names, &names);
        for (k = 0; k < names.n; k++) {
            char child_rel[2048], child_abs[2200];
            if (names.v[k][0] != '.') {                               /* hidden folders are not watched */
                snprintf(child_rel, sizeof child_rel, "%s%s%s", rel, rel[0] ? "/" : "", names.v[k]);
                snprintf(child_abs, sizeof child_abs, "%s/%s", abs, names.v[k]);
                if (ex_platform_path_info(child_abs, &info) == 0 && info.is_dir && rc == 0)
                    rc = ino_add_dir(in, child_rel, depth + 1);     /* loops are cut by the depth limit */
            }
            free(names.v[k]);
        }
        free(names.v);
        if (rc != 0) return -1;
    }
    return 0;
}

/* A folder that appeared after the watches were set: whatever was put into it before its watch
 * existed is reported as changed (nothing is missed in that gap). */
static void ino_emit_existing(Ino *in, const char *rel, int depth) {
    char abs[2048];
    IterNames names = {0};
    size_t k;
    ExPathInfo info;
    if (depth > 30) return;
    snprintf(abs, sizeof abs, "%s%s%s", in->root, rel[0] ? "/" : "", rel);
    SDL_EnumerateDirectory(abs, collect_names, &names);
    for (k = 0; k < names.n; k++) {
        char child_rel[2048], child_abs[2200];
        if (names.v[k][0] != '.') {
            snprintf(child_rel, sizeof child_rel, "%s/%s", rel, names.v[k]);
            snprintf(child_abs, sizeof child_abs, "%s/%s", abs, names.v[k]);
            ino_push(in, child_rel);
            if (ex_platform_path_info(child_abs, &info) == 0 && info.is_dir) ino_emit_existing(in, child_rel, depth + 1);
        }
        free(names.v[k]);
    }
    free(names.v);
}

static void *ino_start(const char *root) {
    Ino *in = calloc(1, sizeof *in);
    if (!in) return NULL;
    snprintf(in->root, sizeof in->root, "%s", root);
    in->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (in->fd < 0 || ino_add_dir(in, "", 0) != 0) {
        size_t i;
        if (in->fd >= 0) close(in->fd);
        for (i = 0; i < in->n; i++) free(in->dir[i]);
        free(in->wd); free(in->dir); free(in);
        return NULL;
    }
    return in;
}

static int ino_read(void *ctx, char *rel, size_t cap) {
    Ino *in = ctx;
    char buf[16384] __attribute__((aligned(8)));
    ssize_t len;
    while (in->npending == 0 && (len = read(in->fd, buf, sizeof buf)) > 0) {
        char *p = buf;
        while (p < buf + len) {
            struct inotify_event *ev = (struct inotify_event *)p;
            size_t i;
            const char *dir = "";
            for (i = 0; i < in->n; i++) if (in->wd[i] == ev->wd) { dir = in->dir[i]; break; }
            if (ev->len > 0 && ev->name[0] != '.') {
                char path[2200];
                snprintf(path, sizeof path, "%s%s%s", dir, dir[0] ? "/" : "", ev->name);
                if ((ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO)) && ino_add_dir(in, path, 1) == 0) ino_emit_existing(in, path, 1);
                ino_push(in, path);
            }
            p += sizeof *ev + ev->len;
        }
    }
    if (in->npending == 0) return 0;
    snprintf(rel, cap, "%s", in->pending[0]);
    free(in->pending[0]);
    memmove(in->pending, in->pending + 1, (in->npending - 1) * sizeof *in->pending);
    in->npending--;
    return 1;
}

static void ino_stop(void *ctx) {
    Ino *in = ctx;
    size_t i;
    close(in->fd);
    for (i = 0; i < in->n; i++) free(in->dir[i]);
    for (i = 0; i < in->npending; i++) free(in->pending[i]);
    free(in->wd); free(in->dir); free(in->pending); free(in);
}

static const ExWatchBackend NATIVE = {"inotify", ino_start, ino_read, ino_stop};
#define HAVE_NATIVE 1
#else
#define HAVE_NATIVE 0
#endif

/* ---- the polling fallback: modification times every poll_ms on a worker thread ---- */

typedef struct Entry { char *rel; int64_t mtime; int64_t size; } Entry;
typedef struct Snapshot { Entry *e; size_t n, cap; } Snapshot;

typedef struct ScanCtx { const char *root; size_t rootlen; const char *const *excl; size_t nexcl; Snapshot *s; } ScanCtx;

static int scan_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    ScanCtx *c = user;
    const char *rel;
    const char *p;
    if (strlen(path) <= c->rootlen) return 0;
    rel = path + c->rootlen + 1;
    for (p = rel; *p; p++) if (p == rel || p[-1] == '/') if (*p == '.') return 0;     /* hidden names are not watched */
    if (ex_patterns_match(c->excl, c->nexcl, rel)) return 0;                          /* excluded files are never watched */
    {
        ExPathInfo info;
        Snapshot *s = c->s;
        if (s->n == s->cap) {
            size_t nc = s->cap ? s->cap * 2 : 256;
            Entry *ne = realloc(s->e, nc * sizeof *ne);
            if (!ne) return 1;
            s->e = ne;
            s->cap = nc;
        }
        s->e[s->n].rel = malloc(strlen(rel) + 1);
        if (!s->e[s->n].rel) return 1;
        strcpy(s->e[s->n].rel, rel);
        s->e[s->n].mtime = mtime;
        s->e[s->n].size = !is_dir && ex_platform_path_info(path, &info) == 0 ? info.size : -1;
        s->n++;
    }
    return 0;
}

static int cmp_entry(const void *a, const void *b) { return strcmp(((const Entry *)a)->rel, ((const Entry *)b)->rel); }

static void free_snapshot(Snapshot *s) {
    size_t i;
    for (i = 0; i < s->n; i++) free(s->e[i].rel);
    free(s->e);
    memset(s, 0, sizeof *s);
}

struct ExWatcher {
    char *root;
    char **excl; size_t nexcl;
    const ExWatchBackend *backend;
    void *ctx;                 /* the backend's, when it accepted */
    int polling;
    int poll_ms;
    SDL_Thread *thread;
    SDL_AtomicInt stop;
    ExQueue *events;           /* from the polling thread */
};

static int poll_main(void *arg) {
    ExWatcher *w = arg;
    Snapshot prev = {0}, cur;
    ScanCtx c;
    int first = 1;
    while (!SDL_GetAtomicInt(&w->stop)) {
        int waited;
        memset(&cur, 0, sizeof cur);
        c.root = w->root; c.rootlen = strlen(w->root); c.excl = (const char *const *)w->excl; c.nexcl = w->nexcl; c.s = &cur;
        ex_platform_walk(w->root, scan_cb, &c);
        if (cur.n > 1) qsort(cur.e, cur.n, sizeof *cur.e, cmp_entry);
        if (!first) {
            size_t i = 0, j = 0;
            while (i < prev.n || j < cur.n) {
                int cmp = i >= prev.n ? 1 : (j >= cur.n ? -1 : strcmp(prev.e[i].rel, cur.e[j].rel));
                const char *rel = NULL;
                if (cmp < 0) { rel = prev.e[i].rel; i++; }                        /* gone */
                else if (cmp > 0) { rel = cur.e[j].rel; j++; }                    /* new */
                else {
                    if (prev.e[i].mtime != cur.e[j].mtime || prev.e[i].size != cur.e[j].size) rel = cur.e[j].rel;   /* changed */
                    i++; j++;
                }
                if (rel) {
                    ExMessage m;
                    char *copy = malloc(strlen(rel) + 1);
                    if (copy) { strcpy(copy, rel); memset(&m, 0, sizeof m); m.payload = copy; m.free_payload = free; ex_queue_push(w->events, &m); }
                }
            }
        }
        free_snapshot(&prev);
        prev = cur;
        first = 0;
        for (waited = 0; waited < w->poll_ms && !SDL_GetAtomicInt(&w->stop); waited += 10) SDL_Delay(10);
    }
    free_snapshot(&prev);
    return 0;
}

ExWatcher *ex_watcher_start(const char *root, const ExWatchBackend *backend, const char *const *excluded, size_t nexcluded, int poll_ms) {
    ExWatcher *w = calloc(1, sizeof *w);
    size_t i;
    if (!w) return NULL;
    w->root = malloc(strlen(root) + 1);
    if (!w->root) { free(w); return NULL; }
    strcpy(w->root, root);
    w->poll_ms = poll_ms > 0 ? poll_ms : 2000;
    if (nexcluded) {
        w->excl = calloc(nexcluded, sizeof *w->excl);
        for (i = 0; w->excl && i < nexcluded; i++) { w->excl[i] = malloc(strlen(excluded[i]) + 1); if (w->excl[i]) { strcpy(w->excl[i], excluded[i]); w->nexcl++; } }
    }
#if HAVE_NATIVE
    if (!backend) backend = &NATIVE;
#endif
    w->backend = backend;
    if (backend && backend->start) w->ctx = backend->start(root);
    if (!w->ctx) { /* refused (or no native backend here): poll modification times */
        w->polling = 1;
        w->events = ex_queue_create(NULL, NULL);
        SDL_SetAtomicInt(&w->stop, 0);
        w->thread = w->events ? SDL_CreateThread(poll_main, "file poll", w) : NULL;
        if (!w->thread) { ex_watcher_stop(w); return NULL; }
    }
    return w;
}

int ex_watcher_poll(ExWatcher *w, char *rel, size_t cap) {
    for (;;) {
        int got;
        if (w->polling) {
            ExMessage m;
            if (!ex_queue_pop(w->events, &m)) return 0;
            snprintf(rel, cap, "%s", (const char *)m.payload);
            free(m.payload);
            got = 1;
        } else {
            got = w->backend->read(w->ctx, rel, cap);
        }
        if (!got) return 0;
        if (ex_patterns_match((const char *const *)w->excl, w->nexcl, rel)) continue;   /* never report excluded files */
        return 1;
    }
}

int ex_watcher_is_polling(const ExWatcher *w) { return w->polling; }

void ex_watcher_stop(ExWatcher *w) {
    size_t i;
    if (!w) return;
    if (w->thread) { SDL_SetAtomicInt(&w->stop, 1); SDL_WaitThread(w->thread, NULL); }
    if (w->ctx && w->backend && w->backend->stop) w->backend->stop(w->ctx);
    ex_queue_destroy(w->events);
    for (i = 0; i < w->nexcl; i++) free(w->excl[i]);
    free(w->excl); free(w->root);
    free(w);
}
