/* Find and replace in a text (sub-slice 3d): plain text or regular expressions (PCRE2, UTF-8 and Unicode
 * properties on), with case and whole-word options. Text is UTF-8; a subject that is not valid UTF-8 never
 * matches. A pattern that takes too long to match (catastrophic backtracking) reports EX_FIND_LIMIT instead of
 * hanging the application. Offsets are bytes. */
#ifndef EXHAUST_FIND_H
#define EXHAUST_FIND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExFindOptions {
    int regex;          /* the pattern is a regular expression; otherwise it is plain text */
    int match_case;     /* otherwise letters match without regard to case (Unicode simple case folding) */
    int whole_word;     /* a match must not touch a letter, digit or underscore on either side */
} ExFindOptions;

typedef struct ExFind ExFind;

/* NULL if the pattern is empty or does not compile; err (if given) then says why. */
ExFind *ex_find_compile(const char *pattern, size_t len, const ExFindOptions *opt, char *err, size_t errcap);
void ex_find_free(ExFind *f);

typedef enum ExFindResult { EX_FIND_NONE = 0, EX_FIND_MATCH = 1, EX_FIND_LIMIT = -1, EX_FIND_BAD_TEXT = -2 } ExFindResult;

/* The first match that starts at or after `from`. */
ExFindResult ex_find_next(ExFind *f, const char *text, size_t len, size_t from, size_t *start, size_t *end);
/* The last match that starts before `before` (searching backwards from the caret). */
ExFindResult ex_find_prev(ExFind *f, const char *text, size_t len, size_t before, size_t *start, size_t *end);
/* The number of matches (empty matches count, each ends where the next search starts one character on), at most `limit`. */
size_t ex_find_count(ExFind *f, const char *text, size_t len, size_t limit);

/* The replacement text for the match at [start, end) of text. Plain mode: the replacement as it is. Regex mode:
 * `$0` to `$9` and `${n}` are the matched groups, `\n`, `\t`, `\r` and `\\` and `\$` are escapes, `$$` is a dollar sign.
 * malloc'ed, *outlen is its length. */
char *ex_find_replacement(ExFind *f, const char *text, size_t len, size_t start, size_t end, const char *repl, size_t rlen, size_t *outlen);

/* Replace all: the changes to apply (sorted, not overlapping) to turn every match into its replacement. */
typedef struct ExFindEdit { size_t offset, remove_len; char *text; size_t text_len; } ExFindEdit;
/* Returns the number of edits (0 if none or on failure); *edits is malloc'ed (free each text, then the array) or NULL. */
size_t ex_find_replace_all(ExFind *f, const char *text, size_t len, const char *repl, size_t rlen, ExFindEdit **edits);
void ex_find_edits_free(ExFindEdit *edits, size_t n);

#ifdef __cplusplus
}
#endif
#endif
