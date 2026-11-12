/* The text model: a piece-table buffer, a document with an undo tree and
 * multiple selections, and the recovery journal's record format.
 *
 * Text is bytes of UTF-8; every offset is a byte offset. Exhaust never edits
 * through anything but a document, and a document reports every change to the
 * text as a plain edit (offset, bytes removed, bytes inserted) so the edit
 * stream can be written to the journal and replayed. */
#ifndef EXHAUST_BUFFER_H
#define EXHAUST_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Buffer: piece table over the original text and an append-only add buffer ---- */

typedef struct ExBuffer ExBuffer;

ExBuffer *ex_buffer_create(const char *text, size_t len); /* copies text; NULL/0 for empty */
void ex_buffer_free(ExBuffer *b);
size_t ex_buffer_size(const ExBuffer *b);
/* Number of lines: one more than the number of '\n' bytes. */
size_t ex_buffer_line_count(const ExBuffer *b);
/* Copies up to n bytes starting at offset into out; returns the number copied. */
size_t ex_buffer_read(const ExBuffer *b, size_t offset, char *out, size_t n);
/* A malloc'ed copy of the whole text, NUL terminated; *len gets the size. */
char *ex_buffer_text(const ExBuffer *b, size_t *len);
/* Both return 0 on success, -1 on a bad range or out of memory. */
int ex_buffer_insert(ExBuffer *b, size_t offset, const char *text, size_t len);
int ex_buffer_delete(ExBuffer *b, size_t offset, size_t len);
/* Offset of the first byte of a line (0-based); the size for line == line_count. */
size_t ex_buffer_line_start(const ExBuffer *b, size_t line);
/* The line (0-based) that contains offset; offset == size is on the last line. */
size_t ex_buffer_line_of_offset(const ExBuffer *b, size_t offset);
/* Number of pieces, for tests and diagnostics. */
size_t ex_buffer_piece_count(const ExBuffer *b);

/* ---- Document: buffer, selections and undo tree ---- */

typedef struct ExDoc ExDoc;

/* A selection. A caret has anchor == head. head is the end that moves. */
typedef struct ExSel { size_t anchor, head; } ExSel;

/* One replacement: remove remove_len bytes at offset and insert text. */
typedef struct ExChange {
    size_t offset, remove_len;
    const char *text;
    size_t text_len;
} ExChange;

/* Called for every change to the text, in the order it happens (also for undo
 * and redo). Replaying the calls on a copy of the original text reproduces it. */
typedef void (*ExEditFn)(void *user, size_t offset, size_t removed, const char *inserted, size_t inserted_len);

ExDoc *ex_doc_create(const char *text, size_t len);
void ex_doc_free(ExDoc *d);
const ExBuffer *ex_doc_buffer(const ExDoc *d);
void ex_doc_set_edit_listener(ExDoc *d, ExEditFn fn, void *user);

/* Selections are kept sorted, within the text, and merged when they overlap or
 * when two carets coincide. There is always at least one. */
size_t ex_doc_sel_count(const ExDoc *d);
ExSel ex_doc_sel(const ExDoc *d, size_t i);
size_t ex_doc_primary(const ExDoc *d); /* index of the selection that was set or added last */
void ex_doc_set_selections(ExDoc *d, const ExSel *sels, size_t n);
void ex_doc_set_caret(ExDoc *d, size_t offset); /* one caret, replacing all selections */
void ex_doc_add_cursor(ExDoc *d, size_t offset);
/* Typing groups end here: the next edit starts a new undo step (call it when the
 * selection moves). */
void ex_doc_seal(ExDoc *d);

/* Applies changes as one undo step. Changes must be sorted by offset, must not
 * overlap and must lie within the text; otherwise nothing happens and -1 is
 * returned. After the edit the selections are sel_after (in the new text), or
 * if NULL a caret after each change's inserted text. With coalesce, a single
 * insertion that continues the previous one extends that undo step (typing). */
int ex_doc_apply(ExDoc *d, const ExChange *changes, size_t n, int coalesce, const ExSel *sel_after, size_t nsel_after);
/* Replaces every selection with text: typing, pasting. */
int ex_doc_replace_selections(ExDoc *d, const char *text, size_t len, int coalesce);

/* Undo tree. Node 0 is the original text; every edit adds a child of the current
 * node. Redo follows the child that was current most recently. */
int ex_doc_undo(ExDoc *d);
int ex_doc_redo(ExDoc *d);
size_t ex_doc_node_count(const ExDoc *d);
size_t ex_doc_current_node(const ExDoc *d);
size_t ex_doc_node_parent(const ExDoc *d, size_t node); /* the root is its own parent */
/* Moves to any node by undoing to the common ancestor and redoing down. */
int ex_doc_goto_node(ExDoc *d, size_t node);

/* ---- Journal records (format in docs/formats.md, "Journal file") ---- */

uint32_t ex_crc32(uint32_t crc, const void *data, size_t len); /* start with 0 */

typedef struct ExBytes { uint8_t *data; size_t len, cap; } ExBytes;
void ex_bytes_free(ExBytes *b);

/* Writes the 8-byte file header (magic EXJ1 and version 1). */
int ex_journal_write_header(ExBytes *out);
/* Appends an edit record. path is the vault-relative path. */
int ex_journal_write_edit(ExBytes *out, uint64_t timestamp_ms, const char *path, uint64_t offset, uint64_t removed,
                          const char *inserted, size_t inserted_len);
/* Appends a base ('B') or saved ('S') record: type is 'B' or 'S'. */
int ex_journal_write_mark(ExBytes *out, char type, uint64_t timestamp_ms, const char *path, uint64_t size,
                          const uint8_t hash16[16]);

typedef struct ExJournalRecord {
    char type;               /* 'B', 'E' or 'S' */
    uint64_t timestamp_ms;
    const char *path;        /* not NUL terminated: path_len bytes */
    size_t path_len;
    uint64_t offset, removed;      /* E */
    const char *inserted;          /* E */
    size_t inserted_len;
    uint64_t base_size;            /* B, S */
    uint8_t base_hash[16];         /* B, S */
} ExJournalRecord;

/* Reads the record at *pos (start with 8, after the header). Returns 1 and moves
 * *pos on success; 0 at the end of the data, at a short record or at a bad
 * checksum, which ends replay: everything before it is still valid. Returns -1
 * if the header is not a journal this version understands. A record of an
 * unknown type is skipped (its checksum must still match), so a newer journal is
 * never discarded by an older reader. */
int ex_journal_next(const uint8_t *data, size_t len, size_t *pos, ExJournalRecord *rec);
/* Checks the header; returns 0 if it is a version 1 journal. */
int ex_journal_check_header(const uint8_t *data, size_t len);
/* Applies the edit records for path that come after the last saved record for
 * that path onto buf. *applied gets the number of edits. Returns 0, or -1 if an
 * edit does not fit the text (the journal does not belong to this text). */
int ex_journal_replay(const uint8_t *data, size_t len, const char *path, ExBuffer *buf, size_t *applied);

#ifdef __cplusplus
}
#endif
#endif
