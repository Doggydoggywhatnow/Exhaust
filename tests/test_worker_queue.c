/* worker_queue: messages from several worker threads arrive in order for each
 * worker, the UI thread never waits for a worker (there is no lock for it to
 * hold), and shared snapshots are released exactly once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_worker.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

enum { MSG_RUN = 1, MSG_DATA = 2, MSG_SLOW = 3, MSG_SNAP = 4, MSG_DONE = 5 };

static SDL_AtomicInt g_wakes, g_freed_payloads, g_snapshot_frees, g_slow_jobs_run;

static void wake(void *u) { (void)u; SDL_AddAtomicInt(&g_wakes, 1); }
static void count_free(void *p) { SDL_AddAtomicInt(&g_freed_payloads, 1); free(p); }
static void snapshot_free(void *p) { SDL_AddAtomicInt(&g_snapshot_frees, 1); free(p); }

#define PER_WORKER 20000

static void handler(ExWorker *self, const ExMessage *job, void *user) {
    int i;
    (void)user;
    if (job->type == MSG_RUN) {
        for (i = 0; i < PER_WORKER; i++) {
            int *v = malloc(sizeof *v);
            *v = i;
            ex_worker_send(self, MSG_DATA, v, sizeof *v, free);
            if (i % 997 == 0) SDL_Delay(1); /* let the workers interleave */
        }
        ex_worker_send(self, MSG_DONE, NULL, 0, NULL);
    } else if (job->type == MSG_SLOW) {
        SDL_AddAtomicInt(&g_slow_jobs_run, 1);
        if (SDL_GetAtomicInt(&g_slow_jobs_run) == 1) SDL_Delay(300); /* the first job holds this worker for a while */
    } else if (job->type == MSG_SNAP) {
        ExSnapshot *s = (ExSnapshot *)job->payload;
        CHECK(ex_snapshot_size(s) == 64 && ((const unsigned char *)ex_snapshot_data(s))[0] == 7);
        ex_snapshot_unref(s); /* the job owned one reference */
    }
}

typedef struct Producer { ExQueue *q; uint32_t id; int count; } Producer;

static int producer_main(void *arg) {
    Producer *p = arg;
    int i;
    for (i = 0; i < p->count; i++) {
        ExMessage m;
        memset(&m, 0, sizeof m);
        m.type = MSG_DATA;
        m.sender = p->id;
        m.seq = (uint64_t)i + 1;
        CHECK(ex_queue_push(p->q, &m) == 0);
    }
    return 0;
}

int main(void) {
    ExQueue *ui = ex_queue_create(wake, NULL);
    ExWorker *w[4];
    uint64_t last_seq[5] = {0}, total = 0;
    int done = 0, i;
    uint64_t t0;

    /* 1. four workers each send 20,000 messages while the UI polls */
    for (i = 0; i < 4; i++) w[i] = ex_worker_start("test worker", (uint32_t)i + 1, handler, NULL, ui);
    for (i = 0; i < 4; i++) { ExMessage run; memset(&run, 0, sizeof run); run.type = MSG_RUN; CHECK(ex_worker_post(w[i], &run) == 0); }
    {
        int idle = 0;
        while (done < 4 && idle < 20000) {
            ExMessage m;
            if (!ex_queue_pop(ui, &m)) { SDL_Delay(1); idle++; continue; }
            idle = 0;
            CHECK(m.sender >= 1 && m.sender <= 4);
            CHECK(m.seq == last_seq[m.sender] + 1);   /* in order, none lost, none doubled */
            last_seq[m.sender] = m.seq;
            if (m.type == MSG_DATA) {
                CHECK(m.payload && *(int *)m.payload == (int)(m.seq - 1));
                total++;
                free(m.payload);
            } else if (m.type == MSG_DONE) done++;
        }
    }
    CHECK(done == 4 && total == 4 * PER_WORKER);
    CHECK(SDL_GetAtomicInt(&g_wakes) == (int)(4 * (PER_WORKER + 1)));
    for (i = 0; i < 4; i++) ex_worker_stop(w[i]);

    /* 2. the UI never waits for a worker: worker 5 is busy for 300 ms while the UI
     * posts 10,000 jobs to it and polls 10,000 times */
    {
        ExWorker *slow = ex_worker_start("slow", 5, handler, NULL, ui);
        ExMessage job;
        ExMessage m;
        memset(&job, 0, sizeof job);
        job.type = MSG_SLOW;
        ex_worker_post(slow, &job);
        SDL_Delay(30); /* the worker is inside its 300 ms job */
        t0 = SDL_GetTicksNS();
        for (i = 0; i < 10000; i++) {
            CHECK(ex_worker_post(slow, &job) == 0);
            (void)ex_queue_pop(ui, &m);
        }
        {
            uint64_t ms = (SDL_GetTicksNS() - t0) / 1000000u;
            fprintf(stderr, "posting 10000 jobs to a busy worker took %llu ms\n", (unsigned long long)ms);
            CHECK(ms < 200);
        }
        ex_worker_stop(slow); /* waits for every posted job */
        CHECK(SDL_GetAtomicInt(&g_slow_jobs_run) == 10001);
    }

    /* 3. eight plain threads push into one queue; the consumer sees each one's messages in order */
    {
        ExQueue *q = ex_queue_create(NULL, NULL);
        SDL_Thread *t[8];
        Producer p[8];
        uint64_t last[9] = {0}, got = 0;
        int k;
        for (k = 0; k < 8; k++) { p[k].q = q; p[k].id = (uint32_t)k + 1; p[k].count = 50000; t[k] = SDL_CreateThread(producer_main, "producer", &p[k]); }
        while (got < 8 * 50000) {
            ExMessage m;
            if (!ex_queue_pop(q, &m)) { SDL_Delay(0); continue; }
            CHECK(m.seq == last[m.sender] + 1);
            last[m.sender] = m.seq;
            got++;
        }
        for (k = 0; k < 8; k++) SDL_WaitThread(t[k], NULL);
        { ExMessage m; CHECK(ex_queue_pop(q, &m) == 0); }
        ex_queue_destroy(q);
    }

    /* 4. messages left in a destroyed queue release their payloads */
    {
        ExQueue *q = ex_queue_create(NULL, NULL);
        int before = SDL_GetAtomicInt(&g_freed_payloads);
        for (i = 0; i < 10; i++) {
            ExMessage m;
            memset(&m, 0, sizeof m);
            m.payload = malloc(4);
            m.free_payload = count_free;
            ex_queue_push(q, &m);
        }
        ex_queue_destroy(q);
        CHECK(SDL_GetAtomicInt(&g_freed_payloads) - before == 10);
    }

    /* 5. an immutable snapshot shared with several workers is freed exactly once */
    {
        unsigned char *data = calloc(64, 1);
        ExSnapshot *s;
        ExWorker *a = ex_worker_start("snap a", 6, handler, NULL, ui), *b = ex_worker_start("snap b", 7, handler, NULL, ui);
        data[0] = 7;
        s = ex_snapshot_new(data, 64, snapshot_free);
        for (i = 0; i < 100; i++) {
            ExMessage m;
            memset(&m, 0, sizeof m);
            m.type = MSG_SNAP;
            m.payload = ex_snapshot_ref(s);
            ex_worker_post(i % 2 ? a : b, &m);
        }
        ex_worker_stop(a);
        ex_worker_stop(b);
        CHECK(SDL_GetAtomicInt(&g_snapshot_frees) == 0); /* the creator still holds one */
        ex_snapshot_unref(s);
        CHECK(SDL_GetAtomicInt(&g_snapshot_frees) == 1);
    }

    {   ExMessage m; while (ex_queue_pop(ui, &m)) {} }
    ex_queue_destroy(ui);
    return failures ? 1 : 0;
}
