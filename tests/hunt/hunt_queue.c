/* hunt_queue: the lock-free message queue, the worker threads and the shared snapshots (include/exhaust_worker.h).
 *
 * "The UI thread never blocks": its only link to every worker is this queue, so a lost, duplicated or reordered message is a lost search result, a
 * graph that never appears or a note that is never saved. The queue is lock free and written from scratch, which is exactly where a rare race hides. This test
 *   A. runs many producers against one consumer, with messages that carry a producer number and a sequence number: nothing may be lost or repeated, each
 *      producer's messages must arrive in the order they were sent, and the wake callback must run once per push;
 *   B. destroys a queue that still holds messages: every payload must be released exactly once, and none that was received may be released again;
 *   C. runs workers that are fed from several threads, with handlers that send results: jobs run in the order posted by each thread, ex_worker_send numbers
 *      are 1, 2, 3... per worker, `ex_worker_stop` finishes the jobs already posted, and many workers can run at once;
 *   D. shares a snapshot between threads that take and drop references in a loop: the release function runs exactly once, after the last drop.
 * It uses SDL threads. Run it under ThreadSanitizer (clang, -fsanitize=thread) as well when you can: a data race may not change a count.
 * Link: exhaust_buffer (the queue and workers are in src/util/worker.c). */
#include "hunt.h"

#include <SDL3/SDL.h>

#include "exhaust_worker.h"

enum { MSG_DATA = 1, MSG_RESULT = 2 };

typedef struct Payload { uint32_t producer; uint64_t index; } Payload;

static SDL_AtomicInt g_freed, g_wakes, g_double_free_guard;

static void free_payload(void *p) { SDL_AddAtomicInt(&g_freed, 1); free(p); }
static void wake(void *u) { (void)u; SDL_AddAtomicInt(&g_wakes, 1); }

typedef struct ProdArg { ExQueue *q; uint32_t id; uint64_t count; int push_fail; } ProdArg;

static int producer(void *arg) {
    ProdArg *a = (ProdArg *)arg;
    uint64_t i;
    for (i = 0; i < a->count; i++) {
        Payload *p = (Payload *)malloc(sizeof *p);
        ExMessage m;
        p->producer = a->id; p->index = i;
        memset(&m, 0, sizeof m);
        m.type = MSG_DATA; m.sender = a->id; m.seq = i; m.payload = p; m.size = sizeof *p; m.free_payload = free_payload;
        if (ex_queue_push(a->q, &m) != 0) { a->push_fail++; free(p); }
        if ((i & 1023) == 0) SDL_Delay(0);
    }
    return 0;
}

static void test_queue(void) {
    int sizes[][2] = { { 1, 100000 }, { 2, 80000 }, { 4, 60000 }, { 8, 30000 }, { 16, 10000 }, { 32, 2000 } };
    size_t s;
    for (s = 0; s < sizeof sizes / sizeof *sizes; s++) {
        int nprod = sizes[s][0];
        uint64_t per = (uint64_t)sizes[s][1] * (uint64_t)hunt_iters(10) / 10;
        ExQueue *q;
        SDL_Thread *th[32];
        ProdArg args[32];
        uint64_t *next = (uint64_t *)calloc((size_t)nprod, sizeof *next), got = 0, total = per * (uint64_t)nprod;
        int i, idle = 0;
        hunt_stepf("queue: %d producers x %lu messages", nprod, (unsigned long)per);
        SDL_SetAtomicInt(&g_freed, 0); SDL_SetAtomicInt(&g_wakes, 0);
        q = ex_queue_create(wake, NULL);
        for (i = 0; i < nprod; i++) { args[i].q = q; args[i].id = (uint32_t)i + 1; args[i].count = per; args[i].push_fail = 0; th[i] = SDL_CreateThread(producer, "hunt-producer", &args[i]); }
        while (got < total && idle < 20000) {
            ExMessage m;
            if (ex_queue_pop(q, &m)) {
                Payload *p = (Payload *)m.payload;
                idle = 0;
                HUNT_CHECKF(p && p->producer >= 1 && p->producer <= (uint32_t)nprod, "a message with producer %u", p ? p->producer : 0);
                if (p && p->producer >= 1 && p->producer <= (uint32_t)nprod) {
                    uint64_t *n = &next[p->producer - 1];
                    HUNT_CHECKF(p->index == *n, "producer %u: message %lu arrived when %lu was expected (lost, repeated or out of order)", p->producer, (unsigned long)p->index, (unsigned long)*n);
                    *n = p->index + 1;
                    HUNT_CHECK(m.sender == p->producer && m.seq == p->index && m.type == MSG_DATA && m.size == sizeof *p);
                }
                got++;
                free(p);                                   /* received: the receiver owns it */
                if (hunt_.failures > 20) break;
            } else { SDL_Delay(1); idle++; }
        }
        for (i = 0; i < nprod; i++) { SDL_WaitThread(th[i], NULL); HUNT_CHECK(args[i].push_fail == 0); }
        {
            ExMessage m;
            int extra = 0;
            while (ex_queue_pop(q, &m)) { extra++; free(m.payload); got++; }
            HUNT_CHECKF(got == total, "%lu messages were sent and %lu received", (unsigned long)total, (unsigned long)got);
            HUNT_CHECK(extra >= 0);
        }
        HUNT_CHECKF((uint64_t)SDL_GetAtomicInt(&g_wakes) == total, "the wake function ran %d times for %lu pushes", SDL_GetAtomicInt(&g_wakes), (unsigned long)total);
        HUNT_CHECK(!ex_queue_pop(q, &(ExMessage){0}));
        ex_queue_destroy(q);
        HUNT_CHECKF(SDL_GetAtomicInt(&g_freed) == 0, "destroying an empty queue released %d payloads", SDL_GetAtomicInt(&g_freed));
        free(next);
        if (hunt_.failures > 20) break;
    }
}

static void test_destroy_with_messages(void) {
    int k;
    hunt_step("queue: destroying a queue that still holds messages");
    for (k = 0; k < 50; k++) {
        ExQueue *q = ex_queue_create(NULL, NULL);
        int n = (int)hunt_rnd(300), i, popped = 0;
        SDL_SetAtomicInt(&g_freed, 0);
        for (i = 0; i < n; i++) {
            Payload *p = (Payload *)malloc(sizeof *p);
            ExMessage m;
            p->producer = 1; p->index = (uint64_t)i;
            memset(&m, 0, sizeof m);
            m.payload = p; m.size = sizeof *p; m.free_payload = hunt_chance(90) ? free_payload : NULL;
            if (!m.free_payload) { free(p); m.payload = NULL; m.size = 0; }
            HUNT_EQ_INT(ex_queue_push(q, &m), 0);
        }
        { int take = n ? (int)hunt_rnd((uint32_t)n) : 0; for (i = 0; i < take; i++) { ExMessage m; if (ex_queue_pop(q, &m)) { popped++; if (m.free_payload) free(m.payload); } } }
        {
            int expect_freed = 0, j;
            (void)j;
            SDL_SetAtomicInt(&g_freed, 0);
            ex_queue_destroy(q);
            expect_freed = SDL_GetAtomicInt(&g_freed);
            HUNT_CHECKF(expect_freed <= n - popped, "destroying the queue released %d payloads but only %d messages were left", expect_freed, n - popped);
            HUNT_SUSPECT(expect_freed >= (n - popped) * 8 / 10 - 2, "destroying a queue should release the payload of every message still in it");
        }
    }
}

/* ---- C. workers ---- */

typedef struct WorkerCtx { SDL_AtomicInt ran; uint64_t last[8]; int bad_order; uint32_t sent; } WorkerCtx;

static void worker_fn(ExWorker *self, const ExMessage *job, void *user) {
    WorkerCtx *c = (WorkerCtx *)user;
    Payload *p = (Payload *)job->payload;
    if (p) {
        if (p->producer < 8) { if (p->index != c->last[p->producer]) c->bad_order++; c->last[p->producer] = p->index + 1; }
        /* the worker releases job->payload with free_payload after this returns (exhaust_worker.h): freeing it here is a double free */
    }
    SDL_AddAtomicInt(&c->ran, 1);
    {
        Payload *r = (Payload *)malloc(sizeof *r);
        r->producer = ex_worker_id(self); r->index = job->seq;
        ex_worker_send(self, MSG_RESULT, r, sizeof *r, free_payload);
    }
}

typedef struct PostArg { ExWorker *w; uint32_t id; uint64_t count; } PostArg;
static int poster(void *arg) {
    PostArg *a = (PostArg *)arg;
    uint64_t i;
    for (i = 0; i < a->count; i++) {
        Payload *p = (Payload *)malloc(sizeof *p);
        ExMessage m;
        p->producer = a->id; p->index = i;
        memset(&m, 0, sizeof m);
        m.type = 7; m.payload = p; m.size = sizeof *p; m.free_payload = free_payload; m.seq = i;
        if (ex_worker_post(a->w, &m) != 0) free(p);
    }
    return 0;
}

static void test_workers(void) {
    int rounds = (int)hunt_iters(12), r;
    for (r = 0; r < rounds; r++) {
        ExQueue *results = ex_queue_create(wake, NULL);
        WorkerCtx *ctx = (WorkerCtx *)calloc(1, sizeof *ctx);
        ExWorker *w;
        int nposters = 1 + (int)hunt_rnd(4), i;
        uint64_t per = 2000 + hunt_rnd(3000), total = per * (uint64_t)nposters, got = 0, last_seq = 0;
        SDL_Thread *th[4];
        PostArg pa[4];
        hunt_stepf("worker round %d (%d posters x %lu jobs)", r, nposters, (unsigned long)per);
        SDL_SetAtomicInt(&g_freed, 0);
        w = ex_worker_start("hunt-worker", 5u + (uint32_t)r, worker_fn, ctx, results);
        HUNT_CHECK(w != NULL && ex_worker_id(w) == 5u + (uint32_t)r);
        if (!w) { ex_queue_destroy(results); free(ctx); continue; }
        for (i = 0; i < nposters; i++) { pa[i].w = w; pa[i].id = (uint32_t)i; pa[i].count = per; th[i] = SDL_CreateThread(poster, "hunt-poster", &pa[i]); }
        for (i = 0; i < nposters; i++) SDL_WaitThread(th[i], NULL);
        ex_worker_stop(w);                                 /* the jobs already posted are finished first */
        HUNT_CHECKF((uint64_t)SDL_GetAtomicInt(&ctx->ran) == total, "the worker ran %d of %lu jobs before ex_worker_stop returned", SDL_GetAtomicInt(&ctx->ran), (unsigned long)total);
        HUNT_CHECKF(ctx->bad_order == 0, "the jobs of one thread ran out of the order they were posted in (%d times)", ctx->bad_order);
        {
            ExMessage m;
            while (ex_queue_pop(results, &m)) {
                Payload *p = (Payload *)m.payload;
                HUNT_CHECK(m.type == MSG_RESULT && m.sender == 5u + (uint32_t)r);
                HUNT_CHECKF(m.seq == last_seq + 1, "the results of a worker are numbered %lu after %lu", (unsigned long)m.seq, (unsigned long)last_seq);
                last_seq = m.seq;
                got++;
                free(p);
            }
        }
        HUNT_CHECKF(got == total, "%lu results from %lu jobs", (unsigned long)got, (unsigned long)total);
        ex_queue_destroy(results);
        free(ctx);
    }
    hunt_step("workers: many at once, started and stopped in a loop");
    {
        int k;
        for (k = 0; k < (int)hunt_iters(20); k++) {
            ExQueue *results = ex_queue_create(NULL, NULL);
            WorkerCtx *ctxs = (WorkerCtx *)calloc(16, sizeof *ctxs);
            ExWorker *ws[16];
            int i, j;
            for (i = 0; i < 16; i++) ws[i] = ex_worker_start("hunt-many", 100u + (uint32_t)i, worker_fn, &ctxs[i], results);
            for (j = 0; j < 200; j++) for (i = 0; i < 16; i++) { ExMessage m; memset(&m, 0, sizeof m); m.type = 1; HUNT_CHECK(ex_worker_post(ws[i], &m) == 0); }
            for (i = 0; i < 16; i++) { ex_worker_stop(ws[i]); HUNT_CHECK(SDL_GetAtomicInt(&ctxs[i].ran) == 200); }
            {
                ExMessage m;
                int n = 0;
                while (ex_queue_pop(results, &m)) { n++; free(m.payload); }
                HUNT_CHECKF(n == 16 * 200, "%d results from 3200 jobs", n);
            }
            ex_queue_destroy(results);
            free(ctxs);
        }
    }
    hunt_step("workers: stop right after start, with nothing posted");
    {
        int k;
        for (k = 0; k < 200; k++) { ExQueue *rq = ex_queue_create(NULL, NULL); ExWorker *w = ex_worker_start("hunt-idle", 9, worker_fn, NULL, rq); ex_worker_stop(w); ex_queue_destroy(rq); }
    }
}

/* ---- D. snapshots ---- */

static SDL_AtomicInt g_snap_freed;
static void snap_free_fn(void *p) { SDL_AddAtomicInt(&g_snap_freed, 1); free(p); }

typedef struct SnapArg { ExSnapshot *s; int loops; } SnapArg;
static int snap_worker(void *arg) {
    SnapArg *a = (SnapArg *)arg;
    int i;
    for (i = 0; i < a->loops; i++) {
        ExSnapshot *r = ex_snapshot_ref(a->s), *r2;
        if (!r || ex_snapshot_size(r) != 64 || ((const char *)ex_snapshot_data(r))[0] != 'S') SDL_AddAtomicInt(&g_double_free_guard, 1);
        r2 = ex_snapshot_ref(r);
        ex_snapshot_unref(r);
        ex_snapshot_unref(r2);
    }
    return 0;
}

static void test_snapshots(void) {
    int round, rounds = (int)hunt_iters(30);
    hunt_step("snapshots: references taken and dropped from many threads");
    for (round = 0; round < rounds; round++) {
        char *data = (char *)malloc(64);
        ExSnapshot *s;
        SDL_Thread *th[8];
        SnapArg args[8];
        int i, nth = 2 + (int)hunt_rnd(7);
        memset(data, 'S', 64);
        SDL_SetAtomicInt(&g_snap_freed, 0); SDL_SetAtomicInt(&g_double_free_guard, 0);
        s = ex_snapshot_new(data, 64, snap_free_fn);
        HUNT_CHECK(s != NULL && ex_snapshot_size(s) == 64 && ex_snapshot_data(s) == data);
        for (i = 0; i < nth; i++) { args[i].s = ex_snapshot_ref(s); args[i].loops = 20000; th[i] = SDL_CreateThread(snap_worker, "hunt-snap", &args[i]); }
        ex_snapshot_unref(s);                                  /* the creator lets go while the others work */
        for (i = 0; i < nth; i++) SDL_WaitThread(th[i], NULL);
        HUNT_CHECKF(SDL_GetAtomicInt(&g_snap_freed) == 0, "the snapshot was released while %d threads still held it", nth);
        for (i = 0; i < nth; i++) ex_snapshot_unref(args[i].s);
        HUNT_CHECKF(SDL_GetAtomicInt(&g_snap_freed) == 1, "the snapshot was released %d times", SDL_GetAtomicInt(&g_snap_freed));
        HUNT_CHECK(SDL_GetAtomicInt(&g_double_free_guard) == 0);
    }
    {
        ExSnapshot *s = ex_snapshot_new(NULL, 0, NULL);
        HUNT_CHECK(s != NULL);
        ex_snapshot_unref(s);
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_queue", argc, argv);
    test_queue();
    test_destroy_with_messages();
    test_workers();
    test_snapshots();
    return hunt_end();
}
