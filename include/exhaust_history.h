/* File recovery snapshots (sub-slice 7e): earlier saved versions of a note, kept as files in the vault's folder under the OS local-data
 * folder (`.../recovery`). The names, when a snapshot is taken, and what is pruned are in "File recovery snapshots" in docs/formats.md.
 * These are plain functions on a folder: no thread, no global state; the program calls them from a worker (the note saver) or from a job
 * of the service, never from the interface thread. Snapshots are user data: only the pruning by age and Clear history delete any, and only
 * those of kind EX_SNAP_AUTO. */
#ifndef EXHAUST_HISTORY_H
#define EXHAUST_HISTORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExSnapKind {
    EX_SNAP_AUTO = 0,        /* taken by the interval: `.snap` */
    EX_SNAP_CONFLICT = 1     /* the other side of a conflict: `.bak`; never deleted by Exhaust */
} ExSnapKind;

typedef struct ExSnapshot {
    char *file;              /* the file name inside the folder (give it to ex_history_read) */
    int64_t time;            /* Unix seconds, from the stamp in the name (local time) */
    int64_t size;            /* bytes */
    int kind;                /* ExSnapKind */
} ExSnapshot;

/* The file-name form of a note's path: "/" becomes "__" and "_" becomes "_u". Returns the length, or 0 if it does not fit. */
size_t ex_history_encode(const char *rel, char *out, size_t cap);

/* Writes a snapshot of `rel` (vault-relative path) with exactly these bytes, stamped with `now` (Unix seconds). A snapshot of the same
 * note in the same second is replaced. Returns 0, or -1 if it could not be written. */
int ex_history_take(const char *dir, const char *rel, const void *bytes, size_t len, int64_t now, ExSnapKind kind);

/* The snapshots of one note, newest first (malloc'ed array; free with ex_history_free). Returns the count (0 when there are none). */
size_t ex_history_list(const char *dir, const char *rel, ExSnapshot **out);
void ex_history_free(ExSnapshot *list, size_t n);

/* The time of the newest automatic snapshot of the note, or 0 if it has none. */
int64_t ex_history_last_auto(const char *dir, const char *rel);

/* "YYYY-MM-DD HH:MM:SS" in local time (thread safe); returns buf. */
char *ex_history_format_time(int64_t t, char *buf, size_t cap);

/* A snapshot's bytes (malloc'ed, with a trailing NUL); NULL if the name is not a snapshot file name or it cannot be read. */
char *ex_history_read(const char *dir, const char *file, size_t *len);

/* Deletes the automatic snapshots (of kind EX_SNAP_AUTO) older than `days` days at `now`; days below 1 count as 1. Returns how many. */
size_t ex_history_prune(const char *dir, int64_t now, int days);
/* Deletes every automatic snapshot, of one note (rel) or of all (rel NULL). Conflict copies stay. Returns how many. */
size_t ex_history_clear(const char *dir, const char *rel);

#ifdef __cplusplus
}
#endif
#endif
