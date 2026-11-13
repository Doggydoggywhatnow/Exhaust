/* Saved queries and embedded queries (sub-slice 4b-ii).
 *
 * A fenced code block whose language is `query` holds a search (the syntax of exhaust_search.h; line breaks count as
 * spaces). Reading mode and live preview show its results where the block is: the same list the search panel gives for the
 * same text. The result options (the buttons of a result list) are collapse (only the notes), more context (a line
 * before and after each match) and copy. Saved queries are kept in .exhaust/queries.ini (docs/formats.md, "Saved queries"). */
#ifndef EXHAUST_QUERY_H
#define EXHAUST_QUERY_H
#include <stddef.h>
#include "exhaust_search.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- query blocks in a note ---- */
typedef struct ExQueryBlock {
    size_t start, end;            /* the whole block, fences included */
    char *query;                  /* the search text, line breaks turned into spaces and trimmed */
} ExQueryBlock;
int ex_query_blocks(const char *text, size_t len, ExQueryBlock **out, size_t *n);
void ex_query_blocks_free(ExQueryBlock *b, size_t n);

/* ---- showing results ---- */
typedef struct ExResultView {
    int collapse;                 /* only the names of the notes */
    int more_context;             /* one more line before and after each match */
    size_t max_matches;           /* shown for each note, 0: 5 (the rest is counted: "and 3 more") */
} ExResultView;
/* The results as Markdown: for each note a list item with a link, the number of matches and, unless collapsed, the lines
 * of the matches as quotes with the match marked ==like this==. malloc'ed. */
char *ex_results_markdown(const ExSearchResult *r, size_t n, const char *vault_root, const ExResultView *view);
/* What "copy" puts on the clipboard: the same listing as plain text (note links, and the lines unless collapsed). */
char *ex_results_copy_text(const ExSearchResult *r, size_t n, const char *vault_root, const ExResultView *view);

/* Replaces every query block of a note by its results (Markdown), for the reading view. A block with a search that cannot be
 * parsed shows the reason. Everything else of the text is left as it is. malloc'ed. */
char *ex_query_expand(ExIndex *r, const char *vault_root, const char *text, size_t len, const ExSearchOptions *opt, const ExResultView *view);

/* ---- saved queries ---- */
typedef struct ExSavedQuery { char *name; char *text; int sort; int collapse; int more_context; int match_case; } ExSavedQuery;
typedef struct ExSavedQueries ExSavedQueries;
/* Loads <vault>/.exhaust/queries.ini (a missing file is an empty list). Unknown keys and sections are kept for saving. */
ExSavedQueries *ex_saved_queries_load(const char *vault_root);
void ex_saved_queries_free(ExSavedQueries *q);
size_t ex_saved_queries_count(const ExSavedQueries *q);
const ExSavedQuery *ex_saved_queries_get(const ExSavedQueries *q, size_t i);
const ExSavedQuery *ex_saved_queries_find(const ExSavedQueries *q, const char *name);
/* Adds a query, or replaces the one with the same name (compared without regard to case). */
int ex_saved_queries_put(ExSavedQueries *q, const ExSavedQuery *query);
int ex_saved_queries_remove(ExSavedQueries *q, const char *name);
/* Writes the file atomically. 0 on success. */
int ex_saved_queries_save(const ExSavedQueries *q);

#ifdef __cplusplus
}
#endif
#endif
