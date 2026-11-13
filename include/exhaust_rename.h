/* Link-safe rename and move (sub-slice 4a-iv). When notes or files are renamed or moved, every link that points at them
 * is rewritten so it points at the new place: wikilinks, embeds, Markdown links (relative to their note or from the vault,
 * URL-encoded), links inside properties, and links with a #heading or #^block part (those parts are kept). A moved note's
 * own relative Markdown links are rewritten for its new folder. Only the characters of the link's target change; the text
 * of a note outside the targets is never touched.
 *
 * The caller asks the user first (or not, by the setting), moves the files (ex_vault_rename or ex_vault_move), and then calls
 * ex_rename_links with the index as it was before the move (the indexer has not seen the move yet); afterwards the index is
 * told with ex_index_rename_file. The notes are read and written through an ExNoteIO, so open notes change in their buffers.
 * .canvas, .base and bookmark files are handled in sub-slices 6a, 6b-ii and 4b-iv. */
#ifndef EXHAUST_RENAME_H
#define EXHAUST_RENAME_H
#include <stddef.h>
#include "exhaust_index.h"
#include "exhaust_props.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExMove { const char *from, *to; } ExMove;      /* vault-relative paths */

typedef struct ExRenameReport {
    char **paths;          /* the notes that change (or would change), by the path they have after the move */
    size_t *links;         /* how many links change in each */
    size_t n;
    size_t total_links;
    int failed;            /* notes that could not be read or written */
} ExRenameReport;
void ex_rename_report_free(ExRenameReport *r);

/* What would change, without writing anything (the confirmation dialog shows it). */
int ex_rename_plan(ExIndex *r, const ExNoteIO *io, const ExMove *moves, size_t n, ExRenameReport *report);
/* Rewrites the links. Returns 0 if every note was handled (report->failed counts the ones that were not). */
int ex_rename_links(ExIndex *r, const ExNoteIO *io, const ExMove *moves, size_t n, ExRenameReport *report);

#ifdef __cplusplus
}
#endif
#endif
