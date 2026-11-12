#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_session.h"
#include "exhaust_worker.h"

enum { JOB_WRITE = 1 };

struct ExSession {
    char *path;
    int debounce_ms;
    ExWorker *worker;
    ExQueue *results;
    unsigned long seen_revision;     /* the last revision a write was queued for */
    unsigned long pending_revision;  /* a change not yet queued */
    int pending;
    int64_t changed_at;
    SDL_AtomicInt writes;
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

static void worker_fn(ExWorker *self, const ExMessage *job, void *user) {
    ExSession *s = user;
    (void)self;
    if (job->type == JOB_WRITE && job->payload) {
        if (ex_platform_write_file_atomic(s->path, job->payload, job->size) == 0) SDL_AddAtomicInt(&s->writes, 1);
        else fprintf(stderr, "exhaust: could not write %s\n", s->path);   /* the worker releases the payload */
    }
}

ExSession *ex_session_create(const ExVault *vault, const char *data_dir, int debounce_ms) {
    ExSession *s = calloc(1, sizeof *s);
    char *base = NULL, *dir;
    if (!s) return NULL;
    s->debounce_ms = debounce_ms > 0 ? debounce_ms : 300;
    if (data_dir) base = dup_str(data_dir);
    else {
        char *root = ex_platform_local_data_dir();
        if (root) {
            base = malloc(strlen(root) + strlen(ex_vault_id(vault)) + 16);
            if (base) sprintf(base, "%s/vaults/%s", root, ex_vault_id(vault));
            free(root);
        }
    }
    if (!base) { free(s); return NULL; }
    ex_platform_make_dirs(base);
    dir = base;
    s->path = malloc(strlen(dir) + 16);
    if (!s->path) { free(base); free(s); return NULL; }
    sprintf(s->path, "%s/session.ini", dir);
    free(base);
    s->results = ex_queue_create(NULL, NULL);
    s->worker = s->results ? ex_worker_start("session", 2, worker_fn, s, s->results) : NULL;
    if (!s->worker) { ex_queue_destroy(s->results); free(s->path); free(s); return NULL; }
    return s;
}

const char *ex_session_path(const ExSession *s) { return s->path; }
int ex_session_writes(const ExSession *s) { return SDL_GetAtomicInt((SDL_AtomicInt *)&s->writes); }

int ex_session_load(ExSession *s, ExWorkspace *ws) {
    size_t len = 0;
    char *text = ex_platform_read_file(s->path, &len);
    if (!text) {
        ExPathInfo info;
        return ex_platform_path_info(s->path, &info) == 0 ? -1 : 0;     /* unreadable vs. simply not there */
    }
    if (ex_ws_deserialize(ws, text, len) != 0) {
        char *bad = malloc(strlen(s->path) + 16);
        if (bad) { sprintf(bad, "%s.damaged", s->path); ex_platform_write_file_atomic(bad, text, len); free(bad); }
        free(text);
        return -1;
    }
    free(text);
    s->seen_revision = ex_ws_revision(ws);       /* what was just loaded is already on disk */
    return 1;
}

static void queue_write(ExSession *s, ExWorkspace *ws) {
    ExMessage m;
    size_t len = 0;
    char *text = ex_ws_serialize(ws, &len);
    if (!text) return;
    memset(&m, 0, sizeof m);
    m.type = JOB_WRITE;
    m.payload = text;
    m.size = len;
    m.free_payload = free;
    if (ex_worker_post(s->worker, &m) != 0) free(text);
    s->seen_revision = ex_ws_revision(ws);
    s->pending = 0;
}

void ex_session_tick(ExSession *s, ExWorkspace *ws, int64_t now_ms) {
    unsigned long rev = ex_ws_revision(ws);
    if (rev != s->seen_revision) {
        if (!s->pending || rev != s->pending_revision) { s->pending = 1; s->pending_revision = rev; s->changed_at = now_ms; }
        else if (now_ms - s->changed_at >= s->debounce_ms) queue_write(s, ws);
    }
}

int ex_session_flush(ExSession *s, ExWorkspace *ws) {
    int before;
    if (ex_ws_revision(ws) != s->seen_revision || s->pending) queue_write(s, ws);
    before = ex_session_writes(s);
    (void)before;
    return 0;
}

void ex_session_destroy(ExSession *s, ExWorkspace *ws) {
    if (!s) return;
    if (ws) ex_session_flush(s, ws);
    ex_worker_stop(s->worker);                   /* waits for the queued writes */
    ex_queue_destroy(s->results);
    free(s->path);
    free(s);
}
