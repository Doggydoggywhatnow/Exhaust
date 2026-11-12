/* Threads and messages. The UI thread does input and drawing only and never
 * waits for a worker: workers do disk, index and layout work and send messages
 * back; the UI polls them once a frame.
 *
 * ExQueue is a multi-producer, single-consumer queue without locks (Vyukov's
 * intrusive queue on SDL atomics): any number of threads push, one thread pops.
 * Pushing and popping never block, so the UI thread cannot be held up by a
 * worker holding a lock, because there is no lock to hold. Messages from one
 * producer arrive in the order it sent them.
 *
 * ExWorker is a thread with an input queue of its own: the UI posts jobs to it
 * (never blocking) and its handler runs on the worker thread, sending results to
 * the UI's queue. Data crosses threads by ownership transfer or as immutable,
 * reference-counted snapshots (ExSnapshot): after a message is sent, only the
 * receiver touches its payload. */
#ifndef EXHAUST_WORKER_H
#define EXHAUST_WORKER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExMessage {
    uint32_t type;      /* defined by the sender and receiver */
    uint32_t sender;    /* worker id, or 0 for the UI thread */
    uint64_t seq;       /* set by ExWorker senders: 1, 2, 3, ... per worker */
    void *payload;      /* owned by the receiver after the send */
    size_t size;
    void (*free_payload)(void *); /* how to release the payload if nobody receives it; may be NULL */
} ExMessage;

typedef struct ExQueue ExQueue;

/* wake is called after every push, from the pushing thread, to wake the UI loop
 * (for example by pushing an SDL event). It must not block. May be NULL. */
ExQueue *ex_queue_create(void (*wake)(void *user), void *user);
/* Frees the queue and every message still in it (releasing payloads). */
void ex_queue_destroy(ExQueue *q);
/* Any thread. Returns 0, or -1 if out of memory (the payload is not released). */
int ex_queue_push(ExQueue *q, const ExMessage *m);
/* The consumer thread only. Returns 1 and fills *m, or 0 if the queue is empty. Never blocks. */
int ex_queue_pop(ExQueue *q, ExMessage *m);

/* ---- workers ---- */

typedef struct ExWorker ExWorker;
/* Runs on the worker thread for each job posted to it. */
typedef void (*ExWorkerFn)(ExWorker *self, const ExMessage *job, void *user);

/* Starts a thread. Results go to `results` (the UI's queue). id must be nonzero
 * and identify the worker in messages. */
ExWorker *ex_worker_start(const char *name, uint32_t id, ExWorkerFn fn, void *user, ExQueue *results);
/* UI thread or any thread; never blocks. */
int ex_worker_post(ExWorker *w, const ExMessage *job);
/* From inside the handler: sends a result with this worker's id and the next sequence number. */
int ex_worker_send(ExWorker *self, uint32_t type, void *payload, size_t size, void (*free_payload)(void *));
uint32_t ex_worker_id(const ExWorker *w);
/* Waits for the jobs already posted to finish, then stops the thread. */
void ex_worker_stop(ExWorker *w);

/* ---- immutable shared data ---- */

typedef struct ExSnapshot ExSnapshot;
/* Takes ownership of data; free_fn runs when the last reference is released. */
ExSnapshot *ex_snapshot_new(void *data, size_t size, void (*free_fn)(void *));
ExSnapshot *ex_snapshot_ref(ExSnapshot *s);
void ex_snapshot_unref(ExSnapshot *s);
const void *ex_snapshot_data(const ExSnapshot *s);
size_t ex_snapshot_size(const ExSnapshot *s);

#ifdef __cplusplus
}
#endif
#endif
