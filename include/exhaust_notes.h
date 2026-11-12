/* Open notes and keeping them safe (the README's "Saving"): the file format rules
 * (UTF-8, byte-order mark, line endings), atomic saves, the append-only recovery
 * journal, autosave, replay after a crash, outside changes and conflicts, and the
 * file watcher with its polling fallback.
 *
 * Threads: the UI thread owns every ExNotes and ExNote and calls ex_notes_tick() often
 * (a few times a second is enough). Disk work (journal writes, saves, polling) runs on
 * worker threads and reports back through message queues; the UI thread never waits
 * for the disk except in ex_notes_flush(), which is for closing. */
#ifndef EXHAUST_NOTES_H
#define EXHAUST_NOTES_H

#include <stddef.h>
#include <stdint.h>

#include "exhaust_buffer.h"
#include "exhaust_vault.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the note file format ---- */

typedef enum ExEol { EX_EOL_LF, EX_EOL_CRLF, EX_EOL_MIXED } ExEol;
typedef struct ExNoteFormat {
    int bom;       /* the file starts with a UTF-8 byte-order mark */
    ExEol eol;     /* CRLF files are edited with LF; MIXED files are edited and saved byte for byte */
} ExNoteFormat;

/* Strict UTF-8: no overlong forms, surrogates or values above U+10FFFF. */
int ex_utf8_valid(const char *s, size_t len);
/* File bytes to editing text. Returns 0, or -1 if the bytes are not valid UTF-8 (nothing is
 * produced). *text is malloc'ed and NUL terminated. */
int ex_note_decode(const uint8_t *bytes, size_t len, char **text, size_t *text_len, ExNoteFormat *fmt);
/* Editing text to file bytes (malloc'ed; *out_len set): adds the BOM and the line endings back. */
uint8_t *ex_note_encode(const char *text, size_t len, const ExNoteFormat *fmt, size_t *out_len);
void ex_hash16(const void *bytes, size_t len, uint8_t out[16]); /* first 16 bytes of the SHA-256 */

/* A unified diff of two texts (line based), "" if they are equal. malloc'ed. */
char *ex_diff_unified(const char *a, const char *b, const char *label_a, const char *label_b);

/* ---- the watcher ---- */

typedef struct ExWatchBackend {
    const char *name;
    /* Starts watching a folder tree; NULL means "refused" (for example the OS has no more
     * watches): the watcher then polls modification times instead. */
    void *(*start)(const char *root);
    /* Non-blocking: copies the vault-relative path of one changed file or folder into rel and
     * returns 1, or returns 0 if there is nothing new. */
    int (*read)(void *ctx, char *rel, size_t cap);
    void (*stop)(void *ctx);
} ExWatchBackend;

typedef struct ExWatcher ExWatcher;
/* backend NULL: the operating system's (inotify on Linux; elsewhere polling for now). Excluded
 * files are never reported. poll_ms is the polling interval (the README says 2000). */
ExWatcher *ex_watcher_start(const char *root, const ExWatchBackend *backend, const char *const *excluded, size_t nexcluded, int poll_ms);
/* Returns 1 and fills rel when something changed, else 0. Never blocks. */
int ex_watcher_poll(ExWatcher *w, char *rel, size_t cap);
int ex_watcher_is_polling(const ExWatcher *w); /* the status bar says so when this is true */
void ex_watcher_stop(ExWatcher *w);

/* ---- the journal ---- */

typedef struct ExJournalWriter ExJournalWriter;
/* dir is the vault's journal folder; a new numbered file starts on every launch and after
 * 64 MB. flush_ms is journal_flush_ms. Returns NULL if the folder cannot be written. */
ExJournalWriter *ex_jw_create(const char *dir, int flush_ms);
void ex_jw_base(ExJournalWriter *w, const char *path, uint64_t size, const uint8_t hash16[16]);
void ex_jw_edit(ExJournalWriter *w, const char *path, uint64_t offset, uint64_t removed, const char *text, size_t len);
void ex_jw_saved(ExJournalWriter *w, const char *path, uint64_t size, const uint8_t hash16[16]);
/* Called from the UI loop: makes sure written records reach the disk within flush_ms. */
void ex_jw_tick(ExJournalWriter *w, uint64_t now_ms);
/* Waits (up to 5 s) until every record queued so far is written and flushed to disk. */
int ex_jw_flush(ExJournalWriter *w);
long ex_jw_number(const ExJournalWriter *w); /* number of the journal file opened at start */
const char *ex_jw_error(const ExJournalWriter *w); /* NULL, or why writing failed (the text is still in memory) */
void ex_jw_close(ExJournalWriter *w);

typedef struct ExRecoveredEdit { uint64_t offset, removed; char *text; size_t len; } ExRecoveredEdit;
typedef struct ExRecoveredNote {
    char *path;                      /* vault-relative */
    uint64_t base_size;              /* of the file as it was when the edits started */
    uint8_t base_hash[16];
    ExRecoveredEdit *edits; size_t nedits;
    int base_changed;                /* a later launch saw a different file: the journal's base is stale */
} ExRecoveredNote;
typedef struct ExRecovery { ExRecoveredNote *notes; size_t n; } ExRecovery;
/* Reads every journal in dir and lists the notes with edits that were never saved. */
int ex_recovery_scan(const char *dir, ExRecovery *out);
void ex_recovery_free(ExRecovery *r);
/* Removes old journal files when no note has unsaved edits in them (nothing is lost: the text
 * is all in the note files). keep is the number of the file that is open now, or -1. */
int ex_journal_compact(const char *dir, long keep);

/* ---- open notes ---- */

typedef struct ExNotes ExNotes;
typedef struct ExNote ExNote;

typedef struct ExNotesConfig {
    int autosave_idle_ms;      /* default 1000 */
    int autosave_max_ms;       /* default 5000 */
    int journal_flush_ms;      /* default 200 */
    int retry_ms;              /* after a failed save, default 2000 */
    int poll_ms;               /* watcher polling interval, default 2000 */
    const char *data_dir;      /* the vault's folder under the OS local-data folder; NULL: the standard one */
    const ExWatchBackend *watch_backend; /* NULL: native */
    uint64_t (*clock)(void *user); /* milliseconds; NULL: the real clock (tests pass a fake one) */
    void *clock_user;
} ExNotesConfig;

typedef enum ExNoteEventKind {
    EX_NOTE_EVENT_SAVED,         /* a save finished */
    EX_NOTE_EVENT_SAVE_FAILED,   /* it did not; the text is safe in the journal and the save is retried */
    EX_NOTE_EVENT_RELOADED,      /* an outside change was loaded into the unedited buffer */
    EX_NOTE_EVENT_CONFLICT,      /* the file changed on disk while the buffer had unsaved edits */
    EX_NOTE_EVENT_TREE_CHANGED   /* something in the vault changed outside Exhaust (rescan the tree) */
} ExNoteEventKind;

typedef struct ExNoteEvent {
    ExNoteEventKind kind;
    ExNote *note;                /* NULL for TREE_CHANGED */
    char rel[512];
    char message[800];           /* SAVE_FAILED: names the file and says why */
} ExNoteEvent;

ExNotes *ex_notes_create(ExVault *vault, const ExNotesConfig *cfg);
/* Flushes everything (journal and unsaved notes) and frees; see ex_notes_flush. */
void ex_notes_destroy(ExNotes *m);
/* Frees without saving the open notes (the journal is still flushed): what the program state
 * looks like to the next launch after a crash. For tests. */
void ex_notes_destroy_without_saving(ExNotes *m);
/* Drives autosave, retries, the watcher and the worker messages. Cheap; call it a few times a second. */
void ex_notes_tick(ExNotes *m);
int ex_notes_poll_event(ExNotes *m, ExNoteEvent *ev);
/* The clean close: waits for the journal, saves every unsaved note and waits for the saves.
 * Returns 0 if everything is on disk, else the number of notes that could not be saved (their
 * text is in the journal). Used on close, SIGTERM, SIGHUP, session end and focus loss. */
int ex_notes_flush(ExNotes *m);
/* Is the file watcher polling because the OS refused watches? */
int ex_notes_watcher_polling(const ExNotes *m);

typedef enum ExOpenResult {
    EX_OPEN_OK,
    EX_OPEN_RECOVERED,      /* the journal had unsaved edits from a crash: they were restored (banner) */
    EX_OPEN_CONFLICT,       /* ...but the file on disk changed since: both versions are kept (conflict banner) */
    EX_OPEN_INVALID_UTF8,   /* not a UTF-8 file: not opened, never written */
    EX_OPEN_IO,             /* cannot be read */
    EX_OPEN_NOT_FOUND
} ExOpenResult;

/* Opens a note by vault-relative path (or returns the one already open). After a crash the
 * unsaved text is replayed first. */
ExNote *ex_notes_open(ExNotes *m, const char *rel, ExOpenResult *result);
/* A new, empty note that does not exist on disk until something is typed in it. */
ExNote *ex_notes_new(ExNotes *m, const char *parent_rel, const char *name_without_extension);
/* Saves now if dirty (waits). Then frees the note and its document. */
int ex_notes_close(ExNotes *m, ExNote *n);
/* Crash recovery: notes with journaled but unsaved edits, opened or restored by ex_notes_open. */
size_t ex_notes_unsaved_count(ExNotes *m);

ExDoc *ex_note_doc(ExNote *n);
const char *ex_note_path(const ExNote *n);          /* vault-relative */
int ex_note_dirty(const ExNote *n);                 /* edits not yet on disk */
int ex_note_exists_on_disk(const ExNote *n);        /* false for a new note nothing was typed into */
const char *ex_note_error(const ExNote *n);         /* NULL, or the last save failure (persistent until a save works) */
int ex_note_has_conflict(const ExNote *n);
const char *ex_note_conflict_disk_text(const ExNote *n); /* the version on disk, in editing form */
char *ex_note_conflict_diff(const ExNote *n);       /* unified diff, mine against the disk version */
int ex_note_recovered(const ExNote *n);             /* restored from the journal: show the banner */
const ExNoteFormat *ex_note_format(const ExNote *n);

typedef enum ExConflictChoice { EX_KEEP_MINE, EX_USE_DISK } ExConflictChoice;
/* Resolves a conflict. KEEP_MINE first copies the version on disk to the recovery folder (so it is
 * never lost), then the next save writes the buffer. USE_DISK puts the disk text in the buffer as
 * one undoable edit (the buffer's own text stays reachable with undo). Returns 0. */
int ex_note_resolve_conflict(ExNote *n, ExConflictChoice choice);
/* Saves now, waiting for the result. Returns 0 on success. */
int ex_note_save_now(ExNote *n);

#ifdef __cplusplus
}
#endif
#endif
