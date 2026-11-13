/* The vault index (sub-slice 4a-i onward): a SQLite database in the OS cache folder, keyed by the vault's id, never
 * inside the vault. It is a cache: it can always be deleted and rebuilt from the notes, and a file that is damaged
 * or written by a newer version is replaced. The schema changes only by numbered migrations that add tables, indexes
 * and nullable or defaulted columns (docs/formats.md, "Index").
 *
 * Threads: one connection writes (the indexer, on a worker thread) and every other thread that needs the index opens
 * a reader of its own (WAL mode lets readers go on while the writer works). A connection is used by one thread. */
#ifndef EXHAUST_INDEX_H
#define EXHAUST_INDEX_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_worker.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EX_INDEX_SCHEMA_VERSION 3

typedef struct ExIndex ExIndex;

/* <cache>/vaults/<vault-id>/index.db (malloc'ed), or NULL if there is no cache folder. */
char *ex_index_path_for_vault(const char *vault_id);
/* Opens the index file, creating or migrating it when `writer` is set (a file that cannot be read, or that has a
 * newer schema than this program knows, is deleted and made again: *was_rebuilt). A reader opens an index a writer
 * has already made. NULL on failure, with a reason in err. */
ExIndex *ex_index_open(const char *path, int writer, char *err, size_t errcap);
/* For the compatibility test: a writer that stops at an earlier schema version. */
ExIndex *ex_index_open_at_version(const char *path, int version, char *err, size_t errcap);
void ex_index_close(ExIndex *ix);
int ex_index_schema_version(const ExIndex *ix);
int ex_index_was_rebuilt(const ExIndex *ix);
/* Deletes the index file (and its WAL files). The index must be closed. */
void ex_index_delete_files(const char *path);

typedef struct ExIndexStats { size_t scanned, added, updated, removed, unchanged; } ExIndexStats;
typedef void (*ExIndexProgress)(size_t done, size_t total, void *user);

/* Brings the index in line with the vault folder: files that are new or changed (modification time or size) are read
 * again, files that are gone are dropped. Hidden names (starting with a dot) and excluded files are never indexed.
 * Only a writer. Returns 0 or -1. */
int ex_index_sync(ExIndex *w, const char *vault_root, const char *const *excluded, size_t nexcluded, ExIndexStats *stats,
                  ExIndexProgress progress, void *user);
/* One file by its vault-relative path: added, updated, or removed if it is gone. */
int ex_index_update_file(ExIndex *w, const char *vault_root, const char *rel);
/* A note whose text is known (the open buffer); mtime 0 means "not on disk yet". */
int ex_index_update_text(ExIndex *w, const char *rel, const char *text, size_t len);
int ex_index_remove_file(ExIndex *w, const char *rel);
/* A note was renamed or moved: keeps its row (and what points at it) under the new path. */
int ex_index_rename_file(ExIndex *w, const char *from, const char *to);
/* Drops every row (the "Rebuild vault cache" button then syncs again). */
int ex_index_clear(ExIndex *w);

/* ---- reading ---- */
long ex_index_file_count(ExIndex *r, int kind);              /* kind is an ExFileKind, or -1 for every file */
int64_t ex_index_file_id(ExIndex *r, const char *rel);       /* 0 if unknown */
char *ex_index_file_path(ExIndex *r, int64_t id);            /* malloc'ed, NULL if unknown */
/* Candidate notes for a text: the ids of the notes whose text contains every three-character piece of needle (so, all
 * notes that contain it, and maybe a few that do not). A needle under three characters cannot use the index: every
 * note is returned and *scanned is set to 1. The caller reads the candidates to be sure. Case is ignored. */
int ex_index_candidates(ExIndex *r, const char *needle, size_t needle_len, int64_t **ids, size_t *n, int *scanned);
/* How many note files the index code has read since the program started (the tests look at it). */
long ex_index_note_reads(void);
/* The size of the search part of the index (the trigram tables) in bytes, and the size of the notes' text. */
long long ex_index_text_index_bytes(ExIndex *r);
long long ex_index_note_text_bytes(ExIndex *r);
/* A SHA-256 (hex, malloc'ed) over everything stored, by path rather than by id: two indexes built from the same notes
 * have the same digest. */
char *ex_index_digest(ExIndex *r);
/* Line information. The 1-based line of an offset, and the start of a line, for a note. */
int ex_index_line_count(ExIndex *r, int64_t id, uint32_t *count);
int ex_index_line_start(ExIndex *r, int64_t id, uint32_t line, size_t *start);

/* ---- the indexer thread ---- */
enum { EX_MSG_INDEX_PROGRESS = 0x100, EX_MSG_INDEX_DONE = 0x101 };
typedef struct ExIndexProgressMsg { size_t done, total; } ExIndexProgressMsg;
typedef struct ExIndexDoneMsg { ExIndexStats stats; int ok; } ExIndexDoneMsg;
typedef struct ExIndexer ExIndexer;
/* Starts the worker that owns the writer connection. Results (progress and done messages, sender = worker_id) go to
 * `results`. excluded patterns are copied. NULL on failure. */
ExIndexer *ex_indexer_start(const char *index_path, const char *vault_root, const char *const *excluded, size_t nexcluded,
                            uint32_t worker_id, ExQueue *results);
void ex_indexer_sync(ExIndexer *ixr);                         /* a full sync, in the background */
void ex_indexer_file_changed(ExIndexer *ixr, const char *rel);
void ex_indexer_file_removed(ExIndexer *ixr, const char *rel);
void ex_indexer_rebuild(ExIndexer *ixr);                      /* clear, then sync */
const char *ex_indexer_path(const ExIndexer *ixr);
/* Finishes the jobs already posted and stops. */
void ex_indexer_stop(ExIndexer *ixr);

#ifdef __cplusplus
}
#endif
#endif
