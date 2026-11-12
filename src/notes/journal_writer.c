#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_worker.h"

#define MAX_FILE_BYTES (64LL * 1024 * 1024)

enum { JOB_WRITE = 1, JOB_SYNC = 2, JOB_FLUSH = 3 };

struct ExJournalWriter {
    char *dir;
    long number;
    ExAppendFile *file;          /* touched by the worker thread only */
    ExWorker *worker;
    ExQueue *results;
    int flush_ms;
    uint64_t last_sync_ms;       /* worker thread */
    SDL_AtomicInt unsynced;
    SDL_AtomicInt failed;
    char error[160];             /* written once, before `failed` is set */
    uint64_t last_tick_post;     /* UI thread */
};

/* ---- file numbering ---- */

typedef struct Found { long *nums; size_t n, cap; } Found;

static SDL_EnumerationResult find_cb(void *ud, const char *dirname, const char *fname) {
    Found *f = ud;
    char *end;
    long num = strtol(fname, &end, 10);
    (void)dirname;
    if (end != fname && strcmp(end, ".jrn") == 0 && num >= 0) {
        if (f->n == f->cap) {
            size_t nc = f->cap ? f->cap * 2 : 16;
            long *nn = realloc(f->nums, nc * sizeof *nn);
            if (!nn) return SDL_ENUM_FAILURE;
            f->nums = nn;
            f->cap = nc;
        }
        f->nums[f->n++] = num;
    }
    return SDL_ENUM_CONTINUE;
}

static int cmp_long(const void *a, const void *b) { long x = *(const long *)a, y = *(const long *)b; return x < y ? -1 : x > y; }

static int list_journals(const char *dir, Found *f) {
    memset(f, 0, sizeof *f);
    if (!SDL_EnumerateDirectory(dir, find_cb, f)) { free(f->nums); f->nums = NULL; f->n = 0; return -1; }
    if (f->n > 1) qsort(f->nums, f->n, sizeof *f->nums, cmp_long);
    return 0;
}

static char *journal_path(const char *dir, long number) {
    char *p = malloc(strlen(dir) + 32);
    if (p) sprintf(p, "%s/%ld.jrn", dir, number);
    return p;
}

static int open_next(ExJournalWriter *w) {
    Found f;
    long next = 0;
    char *path;
    ExBytes hdr = {0};
    if (list_journals(w->dir, &f) == 0) { if (f.n) next = f.nums[f.n - 1] + 1; free(f.nums); }
    if (w->number >= next) next = w->number + 1;
    path = journal_path(w->dir, next);
    if (!path) return -1;
    if (w->file) ex_append_close(w->file);
    w->file = ex_append_open(path);
    free(path);
    if (!w->file) return -1;
    w->number = next;
    if (ex_journal_write_header(&hdr) || ex_append_write(w->file, hdr.data, hdr.len)) { ex_bytes_free(&hdr); return -1; }
    ex_bytes_free(&hdr);
    return 0;
}

/* ---- the writer thread ---- */

static void fail(ExJournalWriter *w, const char *why) {
    if (!SDL_GetAtomicInt(&w->failed)) { snprintf(w->error, sizeof w->error, "%s", why); SDL_SetAtomicInt(&w->failed, 1); }
}

static void do_sync(ExJournalWriter *w) {
    if (w->file && ex_append_sync(w->file) != 0) fail(w, "cannot flush the recovery journal to disk");
    w->last_sync_ms = SDL_GetTicks();
    SDL_SetAtomicInt(&w->unsynced, 0);
}

static void job(ExWorker *self, const ExMessage *m, void *user) {
    ExJournalWriter *w = user;
    (void)self;
    if (m->type == JOB_WRITE) {
        if (!w->file && open_next(w) != 0) { fail(w, "cannot open the recovery journal"); return; }
        if (ex_append_write(w->file, m->payload, m->size) != 0) {
            fail(w, "cannot write the recovery journal (disk full?)");
            /* try a fresh file next time */
            ex_append_close(w->file); w->file = NULL;
            return;
        }
        SDL_SetAtomicInt(&w->unsynced, 1);
        if (SDL_GetTicks() - w->last_sync_ms >= (uint64_t)w->flush_ms) do_sync(w);
        if (ex_append_size(w->file) > MAX_FILE_BYTES) { do_sync(w); open_next(w); }
    } else if (m->type == JOB_SYNC) {
        if (SDL_GetAtomicInt(&w->unsynced)) do_sync(w);
    } else if (m->type == JOB_FLUSH) {
        if (SDL_GetAtomicInt(&w->unsynced)) do_sync(w);
        SDL_SignalSemaphore((SDL_Semaphore *)m->payload);
    }
}

ExJournalWriter *ex_jw_create(const char *dir, int flush_ms) {
    ExJournalWriter *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->dir = malloc(strlen(dir) + 1);
    if (!w->dir) { free(w); return NULL; }
    strcpy(w->dir, dir);
    w->flush_ms = flush_ms > 0 ? flush_ms : 200;
    w->number = -1;
    if (ex_platform_make_dirs(dir) != 0 || open_next(w) != 0) { ex_append_close(w->file); free(w->dir); free(w); return NULL; }
    w->results = ex_queue_create(NULL, NULL);
    w->worker = ex_worker_start("journal", 100, job, w, w->results);
    if (!w->worker) { ex_append_close(w->file); ex_queue_destroy(w->results); free(w->dir); free(w); return NULL; }
    w->last_sync_ms = SDL_GetTicks();
    return w;
}

static void post(ExJournalWriter *w, int type, void *payload, size_t size) {
    ExMessage m;
    memset(&m, 0, sizeof m);
    m.type = (uint32_t)type;
    m.payload = payload;
    m.size = size;
    m.free_payload = type == JOB_WRITE ? free : NULL;
    if (ex_worker_post(w->worker, &m) != 0 && type == JOB_WRITE) free(payload);
}

static void post_bytes(ExJournalWriter *w, ExBytes *b) {
    if (!b->data) return;
    post(w, JOB_WRITE, b->data, b->len); /* the worker owns the bytes now */
    b->data = NULL; b->len = b->cap = 0;
}

void ex_jw_base(ExJournalWriter *w, const char *path, uint64_t size, const uint8_t hash16[16]) {
    ExBytes b = {0};
    if (ex_journal_write_mark(&b, 'B', (uint64_t)ex_platform_unix_time() * 1000u, path, size, hash16) == 0) post_bytes(w, &b); else ex_bytes_free(&b);
}

void ex_jw_saved(ExJournalWriter *w, const char *path, uint64_t size, const uint8_t hash16[16]) {
    ExBytes b = {0};
    if (ex_journal_write_mark(&b, 'S', (uint64_t)ex_platform_unix_time() * 1000u, path, size, hash16) == 0) post_bytes(w, &b); else ex_bytes_free(&b);
}

void ex_jw_edit(ExJournalWriter *w, const char *path, uint64_t offset, uint64_t removed, const char *text, size_t len) {
    ExBytes b = {0};
    if (ex_journal_write_edit(&b, (uint64_t)ex_platform_unix_time() * 1000u, path, offset, removed, text, len) == 0) post_bytes(w, &b); else ex_bytes_free(&b);
}

void ex_jw_tick(ExJournalWriter *w, uint64_t now_ms) {
    if (SDL_GetAtomicInt(&w->unsynced) && now_ms - w->last_tick_post >= (uint64_t)w->flush_ms) {
        w->last_tick_post = now_ms;
        post(w, JOB_SYNC, NULL, 0);
    }
}

int ex_jw_flush(ExJournalWriter *w) {
    SDL_Semaphore *sem = SDL_CreateSemaphore(0);
    int ok;
    if (!sem) return -1;
    post(w, JOB_FLUSH, sem, 0);
    ok = SDL_WaitSemaphoreTimeout(sem, 5000);
    if (ok) SDL_DestroySemaphore(sem); /* on a timeout the semaphore is leaked rather than freed under the worker */
    return ok && !SDL_GetAtomicInt(&w->failed) ? 0 : -1;
}

long ex_jw_number(const ExJournalWriter *w) { return w->number; }

const char *ex_jw_error(const ExJournalWriter *w) { return SDL_GetAtomicInt((SDL_AtomicInt *)&w->failed) ? w->error : NULL; }

void ex_jw_close(ExJournalWriter *w) {
    if (!w) return;
    ex_jw_flush(w);
    ex_worker_stop(w->worker);
    ex_queue_destroy(w->results);
    if (w->file) { ex_append_sync(w->file); ex_append_close(w->file); }
    free(w->dir);
    free(w);
}

/* ---- recovery ---- */

typedef struct PathState {
    char *path;
    int have_base;
    uint64_t base_size; uint8_t base_hash[16];
    uint64_t pend_base_size; uint8_t pend_base_hash[16]; int pend_has_base;
    ExRecoveredEdit *edits; size_t nedits, cap;
    int base_changed;
} PathState;

static PathState *state_for(PathState **list, size_t *n, size_t *cap, const char *path, size_t plen) {
    size_t i;
    PathState *s;
    for (i = 0; i < *n; i++) if (strlen((*list)[i].path) == plen && memcmp((*list)[i].path, path, plen) == 0) return &(*list)[i];
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        PathState *nl = realloc(*list, nc * sizeof *nl);
        if (!nl) return NULL;
        *list = nl;
        *cap = nc;
    }
    s = &(*list)[(*n)++];
    memset(s, 0, sizeof *s);
    s->path = malloc(plen + 1);
    if (!s->path) { (*n)--; return NULL; }
    memcpy(s->path, path, plen);
    s->path[plen] = 0;
    return s;
}

static void clear_edits(PathState *s) {
    size_t i;
    for (i = 0; i < s->nedits; i++) free(s->edits[i].text);
    s->nedits = 0;
    s->pend_has_base = 0;
    s->base_changed = 0;
}

int ex_recovery_scan(const char *dir, ExRecovery *out) {
    Found f;
    PathState *list = NULL;
    size_t n = 0, cap = 0, i, k;
    int rc = 0;
    memset(out, 0, sizeof *out);
    {
        ExPathInfo info;
        if (ex_platform_path_info(dir, &info) != 0) return 0;      /* no journal folder: nothing to recover */
    }
    if (list_journals(dir, &f) != 0) return -1;
    for (i = 0; i < f.n && rc == 0; i++) {
        char *path = journal_path(dir, f.nums[i]);
        size_t len = 0, pos = 8;
        char *data = path ? ex_platform_read_file(path, &len) : NULL;
        ExJournalRecord r;
        int next;
        free(path);
        if (!data) { rc = -1; break; }
        if (ex_journal_check_header((const uint8_t *)data, len) != 0) { free(data); continue; } /* not ours or newer: left untouched */
        while ((next = ex_journal_next((const uint8_t *)data, len, &pos, &r)) == 1) {
            PathState *s;
            if ((r.type != 'B' && r.type != 'S' && r.type != 'E') || r.path_len == 0) continue;
            s = state_for(&list, &n, &cap, r.path, r.path_len);
            if (!s) { rc = -1; break; }
            if (r.type == 'S') { clear_edits(s); s->have_base = 1; s->base_size = r.base_size; memcpy(s->base_hash, r.base_hash, 16); }
            else if (r.type == 'B') {
                if (s->nedits > 0) { if (r.base_size != s->base_size || memcmp(r.base_hash, s->base_hash, 16) != 0) s->base_changed = 1; }
                else { s->have_base = 1; s->base_size = r.base_size; memcpy(s->base_hash, r.base_hash, 16); }
            } else {
                if (s->nedits == s->cap) {
                    size_t nc = s->cap ? s->cap * 2 : 16;
                    ExRecoveredEdit *ne = realloc(s->edits, nc * sizeof *ne);
                    if (!ne) { rc = -1; break; }
                    s->edits = ne;
                    s->cap = nc;
                }
                if (s->nedits == 0) { s->pend_has_base = s->have_base; s->pend_base_size = s->base_size; memcpy(s->pend_base_hash, s->base_hash, 16); }
                s->edits[s->nedits].offset = r.offset;
                s->edits[s->nedits].removed = r.removed;
                s->edits[s->nedits].len = r.inserted_len;
                s->edits[s->nedits].text = malloc(r.inserted_len + 1);
                if (!s->edits[s->nedits].text) { rc = -1; break; }
                memcpy(s->edits[s->nedits].text, r.inserted, r.inserted_len);
                s->edits[s->nedits].text[r.inserted_len] = 0;
                s->nedits++;
            }
        }
        free(data);
    }
    free(f.nums);
    if (rc == 0) {
        for (k = 0; k < n; k++) if (list[k].nedits > 0) {
            ExRecoveredNote *nn = realloc(out->notes, (out->n + 1) * sizeof *nn);
            if (!nn) { rc = -1; break; }
            out->notes = nn;
            nn[out->n].path = list[k].path;
            nn[out->n].base_size = list[k].pend_base_size;
            memcpy(nn[out->n].base_hash, list[k].pend_base_hash, 16);
            nn[out->n].edits = list[k].edits;
            nn[out->n].nedits = list[k].nedits;
            nn[out->n].base_changed = list[k].base_changed || !list[k].pend_has_base;
            out->n++;
            list[k].path = NULL; list[k].edits = NULL; list[k].nedits = 0;
        }
    }
    for (k = 0; k < n; k++) { clear_edits(&list[k]); free(list[k].edits); free(list[k].path); }
    free(list);
    if (rc != 0) ex_recovery_free(out);
    return rc;
}

void ex_recovery_free(ExRecovery *r) {
    size_t i, k;
    for (i = 0; i < r->n; i++) {
        for (k = 0; k < r->notes[i].nedits; k++) free(r->notes[i].edits[k].text);
        free(r->notes[i].edits);
        free(r->notes[i].path);
    }
    free(r->notes);
    memset(r, 0, sizeof *r);
}

int ex_journal_compact(const char *dir, long keep) {
    ExRecovery rec;
    Found f;
    size_t i;
    int removed = 0;
    if (ex_recovery_scan(dir, &rec) != 0) return -1;           /* cannot tell what is pending: remove nothing */
    if (rec.n > 0) { ex_recovery_free(&rec); return 0; }      /* text that exists only in a journal: keep every journal */
    if (list_journals(dir, &f) != 0) return -1;
    for (i = 0; i < f.n; i++) {
        char *path;
        if (f.nums[i] == keep) continue;
        path = journal_path(dir, f.nums[i]);
        if (path && SDL_RemovePath(path)) removed++;
        free(path);
    }
    free(f.nums);
    return removed;
}
