/* Links (sub-slice 4a-ii): how a link finds its note, the links a note holds (outgoing), the notes that link to it
 * (backlinks), the places that mention its name without linking (unlinked mentions), and the preview of a link's target.
 *
 * Resolution. A link names a note by its file name without ".md" ([[Note]]), by a path from the vault ([[Folder/Note]],
 * which also matches a path that ends so), or by one of the aliases of a note. Names are compared by their name key (NFC
 * and case folding). Other files are named with their extension ([[image.png]]). If several files fit, the one in the
 * folder of the note that holds the link wins, then the one with the shortest path, then the first by path. A Markdown
 * link ([text](Folder/Note.md)) is tried as a path from its own note's folder first, with %20 and the like decoded. */
#ifndef EXHAUST_LINKS_H
#define EXHAUST_LINKS_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_extract.h"
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The file a link points at (its id), or 0. from_path is the note that holds the link (vault-relative). */
int64_t ex_link_resolve(ExIndex *r, const char *from_path, const char *target, int link_kind);

typedef struct ExLinkItem {
    int kind;                 /* ExLinkKind */
    char *source;             /* the note that holds the link */
    char *target, *heading, *block, *alias;
    char *dest;               /* the path it resolves to, or NULL (an unresolved link) */
    size_t start, end;        /* in the source note */
    uint32_t line;
} ExLinkItem;
void ex_link_items_free(ExLinkItem *items, size_t n);

/* Everything a note points at, in the order of the note. */
int ex_links_outgoing(ExIndex *r, const char *path, ExLinkItem **out, size_t *n);
/* Every link in other notes that resolves to this note, by note and position. */
int ex_links_backlinks(ExIndex *r, const char *path, ExLinkItem **out, size_t *n);

typedef struct ExMention { char *source; size_t start, end; uint32_t line; } ExMention;
void ex_mentions_free(ExMention *m, size_t n);
/* Places in other notes where the note's name or an alias is written as a whole word and not inside a link. The index
 * names the notes that can hold the name (trigram lookup) and only those files are read. Names under three characters
 * are not looked for. */
int ex_links_unlinked(ExIndex *r, const char *vault_root, const char *path, ExMention **out, size_t *n);

/* The text of a line of a note (malloc'ed, without the line break), for showing a link in context. */
char *ex_links_line_text(const char *vault_root, const char *path, uint32_t line);
/* The page preview of a link's target: the section under a heading, the block of a ^id, or the start of the note;
 * at most max_bytes (cut at a line break when possible). malloc'ed; NULL if the target does not exist. */
char *ex_link_preview(ExIndex *r, const char *vault_root, const char *path, const char *heading, const char *block, size_t max_bytes);

#ifdef __cplusplus
}
#endif
#endif
