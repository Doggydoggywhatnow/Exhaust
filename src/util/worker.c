#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_worker.h"

typedef struct QNode {
    struct QNode *volatile next;
    ExMessage msg;
} QNode;

struct ExQueue {
    QNode *volatile head;   /* consumer end; only the consumer touches it */
    QNode *volatile tail;   /* producers exchange this */
    QNode stub;
    void (*wake)(void *);
    void *wake_user;
};

ExQueue *ex_queue_create(void (*wake)(void *), void *user) {
    ExQueue *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    q->head = q->tail = &q->stub;
    q->wake = wake;
    q->wake_user = user;
    return q;
}

static void release(ExMessage *m) {
    if (m->payload && m->free_payload) m->free_payload(m->payload);
    m->payload = NULL;
}

void ex_queue_destroy(ExQueue *q) {
    ExMessage m;
    if (!q) return;
    while (ex_queue_pop(q, &m)) release(&m);
    free(q);
}

int ex_queue_push(ExQueue *q, const ExMessage *m) {
    QNode *n = calloc(1, sizeof *n), *prev;
    if (!n) return -1;
    n->msg = *m;
    /* Swap ourselves in as the tail, then link the previous tail to us. Between the
     * two steps the consumer sees the queue as empty at that point and tries again
     * next time; it never waits. */
    prev = (QNode *)SDL_SetAtomicPointer((void **)&q->tail, n);
    SDL_SetAtomicPointer((void **)&prev->next, n);
    if (q->wake) q->wake(q->wake_user);
    return 0;
}

int ex_queue_pop(ExQueue *q, ExMessage *out) {
    QNode *head = q->head, *next = (QNode *)SDL_GetAtomicPointer((void **)&head->next);
    if (head == &q->stub) {
        if (!next) return 0;
        q->head = next;
        head = next;
        next = (QNode *)SDL_GetAtomicPointer((void **)&head->next);
    }
    if (next) {
        q->head = next;
        *out = head->msg;
        free(head);
        return 1;
    }
    if (head != (QNode *)SDL_GetAtomicPointer((void **)&q->tail)) return 0; /* a push is half done */
    /* head is the last real node: put the stub behind it so head can be returned */
    q->stub.next = NULL;
    {
        QNode *prev = (QNode *)SDL_SetAtomicPointer((void **)&q->tail, &q->stub);
        SDL_SetAtomicPointer((void **)&prev->next, &q->stub);
    }
    next = (QNode *)SDL_GetAtomicPointer((void **)&head->next);
    if (next) {
        q->head = next;
        *out = head->msg;
        free(head);
        return 1;
    }
    return 0;
}

/* ---- workers ---- */

struct ExWorker {
    SDL_Thread *thread;
    SDL_Semaphore *sem;
    ExQueue *inbox;
    ExQueue *results;
    ExWorkerFn fn;
    void *user;
    uint32_t id;
    uint64_t seq;           /* only the worker thread touches it */
    int stop;               /* set before the final wake-up */
};

#define STOP_TYPE 0xFFFFFFFFu

static int worker_main(void *arg) {
    ExWorker *w = arg;
    for (;;) {
        ExMessage m;
        SDL_WaitSemaphore(w->sem);
        while (ex_queue_pop(w->inbox, &m)) {
            if (m.type == STOP_TYPE) { release(&m); return 0; }
            w->fn(w, &m, w->user);
            release(&m);
        }
    }
}

ExWorker *ex_worker_start(const char *name, uint32_t id, ExWorkerFn fn, void *user, ExQueue *results) {
    ExWorker *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->sem = SDL_CreateSemaphore(0);
    w->inbox = ex_queue_create(NULL, NULL);
    w->results = results;
    w->fn = fn;
    w->user = user;
    w->id = id;
    if (!w->sem || !w->inbox) goto fail;
    w->thread = SDL_CreateThread(worker_main, name ? name : "worker", w);
    if (!w->thread) goto fail;
    return w;
fail:
    if (w->sem) SDL_DestroySemaphore(w->sem);
    ex_queue_destroy(w->inbox);
    free(w);
    return NULL;
}

int ex_worker_post(ExWorker *w, const ExMessage *job) {
    if (ex_queue_push(w->inbox, job)) return -1;
    SDL_SignalSemaphore(w->sem);
    return 0;
}

int ex_worker_send(ExWorker *self, uint32_t type, void *payload, size_t size, void (*free_payload)(void *)) {
    ExMessage m;
    m.type = type;
    m.sender = self->id;
    m.seq = ++self->seq;
    m.payload = payload;
    m.size = size;
    m.free_payload = free_payload;
    return ex_queue_push(self->results, &m);
}

uint32_t ex_worker_id(const ExWorker *w) { return w->id; }

void ex_worker_stop(ExWorker *w) {
    ExMessage m;
    if (!w) return;
    memset(&m, 0, sizeof m);
    m.type = STOP_TYPE;
    ex_worker_post(w, &m); /* queued after the jobs already posted */
    SDL_WaitThread(w->thread, NULL);
    SDL_DestroySemaphore(w->sem);
    ex_queue_destroy(w->inbox);
    free(w);
}

/* ---- snapshots ---- */

struct ExSnapshot {
    SDL_AtomicInt refs;
    void *data;
    size_t size;
    void (*free_fn)(void *);
};

ExSnapshot *ex_snapshot_new(void *data, size_t size, void (*free_fn)(void *)) {
    ExSnapshot *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    SDL_SetAtomicInt(&s->refs, 1);
    s->data = data;
    s->size = size;
    s->free_fn = free_fn;
    return s;
}

ExSnapshot *ex_snapshot_ref(ExSnapshot *s) { SDL_AddAtomicInt(&s->refs, 1); return s; }

void ex_snapshot_unref(ExSnapshot *s) {
    if (s && SDL_AddAtomicInt(&s->refs, -1) == 1) {
        if (s->free_fn) s->free_fn(s->data);
        free(s);
    }
}

const void *ex_snapshot_data(const ExSnapshot *s) { return s->data; }
size_t ex_snapshot_size(const ExSnapshot *s) { return s->size; }
