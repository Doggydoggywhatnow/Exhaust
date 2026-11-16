/* The operating systems' own file watchers (sub-slice 7i): ReadDirectoryChangesW on Windows, FSEvents on macOS (Linux's inotify is in
 * src/notes/watcher.c, which uses the queue below too). Each backend has the shape ExWatchBackend asks for: start (NULL when the system refuses or
 * the volume cannot be watched: a network drive; the watcher then polls), a non-blocking read of one changed path, and stop.
 *
 * A burst is coalesced: a path that is already waiting to be read is not queued again, so a thousand writes to a thousand files are at most a thousand
 * reports, and a file written a thousand times is one. When the system says it dropped events (a buffer overflow), the queue reports the empty path
 * once: everything may have changed. */
#ifndef EXHAUST_WATCH_NATIVE_H
#define EXHAUST_WATCH_NATIVE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the queue of changed paths (thread safe) ---- */
typedef struct ExPathQueue ExPathQueue;
ExPathQueue *ex_pathq_create(void);
void ex_pathq_free(ExPathQueue *q);
void ex_pathq_push(ExPathQueue *q, const char *rel);        /* not queued again while it is still waiting */
void ex_pathq_overflow(ExPathQueue *q);                     /* the empty path, once, until it is read */
int ex_pathq_pop(ExPathQueue *q, char *rel, size_t cap);    /* 1 and the path, or 0 when nothing waits */
size_t ex_pathq_count(ExPathQueue *q);

/* ---- the native watcher ---- */
typedef struct ExNativeWatch ExNativeWatch;
/* Watches a folder and everything below it. NULL when this system has no native watcher here, refuses, or the folder is on a network volume. Names that
 * start with a dot (at any depth) are never reported. */
ExNativeWatch *ex_native_watch_start(const char *root);
int ex_native_watch_read(ExNativeWatch *w, char *rel, size_t cap);
void ex_native_watch_stop(ExNativeWatch *w);

#ifdef __cplusplus
}
#endif
#endif
