/* Search (sub-slice 4b-i): the query language of the README ("Finding things") over the notes of the vault.
 *
 * Syntax. Terms are separated by spaces (all of them must match: AND); `OR` between terms; `-` in front of a term,
 * operator or group excludes the notes that match it; parentheses group; "quotes" make a phrase; /regex/ is a regular
 * expression (PCRE2, Unicode). A term matches the name of a note or its text, on parts of words, without regard to case.
 * Operators, each with a term, a "phrase", a /regex/ or a (group):
 *   file:    the file name        path:   the path from the vault       content:  the text only
 *   match-case: / ignore-case:    turn case on or off for the term
 *   tag:     a tag (#a also finds #a/b)
 *   line:(a b)     all terms on one line          block:(a b)  in one block (paragraph or list item)
 *   section:(a b)  in one section (under one heading)
 *   task:(a b)     in a task, task-todo: in an open task, task-done: in a finished one
 * Property filters: [name] (has it), [name:value] (equals, or one of a list's items), [name:null] (empty),
 * [name:>5] [name:<=2024-06-01] (number or date order), [name:a OR b].
 * The index names the notes that can contain the terms (trigram lookup); only those files are read. */
#ifndef EXHAUST_SEARCH_H
#define EXHAUST_SEARCH_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExSearch ExSearch;

/* NULL on a syntax error (err says what) or an empty query. */
ExSearch *ex_search_parse(const char *query, char *err, size_t errcap);
void ex_search_free(ExSearch *q);

typedef enum ExSearchSort {
    EX_SEARCH_PATH_AZ, EX_SEARCH_PATH_ZA, EX_SEARCH_MODIFIED_NEW, EX_SEARCH_MODIFIED_OLD, EX_SEARCH_MATCHES_MOST, EX_SEARCH_MATCHES_LEAST
} ExSearchSort;

typedef struct ExSearchOptions {
    int sort;                  /* ExSearchSort */
    size_t max_results;        /* 0: 1000 */
    size_t max_matches;        /* kept for each note, 0: 200 (the count is still complete up to 5000) */
    int match_case;            /* the default for terms without match-case: or ignore-case: */
} ExSearchOptions;

typedef struct ExSearchMatch { size_t start, end; uint32_t line; } ExSearchMatch;   /* bytes of the note's text; 1-based line */
typedef struct ExSearchResult {
    char *path;
    int64_t mtime;
    ExSearchMatch *matches; size_t nmatches;     /* sorted by position; empty if only the name or the properties matched */
    size_t total_matches;
    int name_match;                              /* a term matched the name of the note */
} ExSearchResult;

/* Runs a query. *files_read gets the number of note files that were read (may be NULL). 0 on success. */
int ex_search_run(ExIndex *r, const char *vault_root, ExSearch *q, const ExSearchOptions *opt, ExSearchResult **out, size_t *n, size_t *files_read);
void ex_search_results_free(ExSearchResult *r, size_t n);

/* The lines around a match for the result list: the line of the match and `extra` lines before and after, without the
 * line breaks at the ends (malloc'ed); *offset is where the match starts in the returned text. */
char *ex_search_context(const char *text, size_t len, const ExSearchMatch *m, int extra, size_t *offset);

#ifdef __cplusplus
}
#endif
#endif
