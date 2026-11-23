/* Templates and daily notes (sub-slice 6e-i): the variables of a template, the merge of its properties into a note's own (as text edits that touch nothing
 * the template does not add), and the paths and the creation of daily notes. See docs/formats.md (Templates and daily notes). No UI here: the commands in
 * src/main.c call these, and the tests call them on plain text and on a vault in a temporary folder. */
#ifndef EXHAUST_TEMPLATES_H
#define EXHAUST_TEMPLATES_H

#include <stddef.h>
#include "exhaust_vault.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the variables of a template stand for. Any text may be NULL. date_format and time_format default to YYYY-MM-DD and HH:mm. */
typedef struct ExTplContext {
    const char *title;                       /* {{title}}: the name of the note */
    double now_ms;                           /* the moment {{date}} and {{time}} show (ms since 1970, UTC) */
    int tz_min;                              /* minutes east of UTC */
    double time_ms; int use_time_ms;         /* when set, {{time}} shows this moment instead (a daily note: {{date}} is its day, {{time}} the time it was made) */
    const char *date_format, *time_format;   /* {{date}} and {{time}} */
    const char *content, *from_title, *new_title;   /* the note composer's {{content}}, {{fromTitle}} and {{newTitle}} */
    int literal;                             /* the template is used as it is, without filling in variables (a merge of one note's text into another) */
} ExTplContext;

/* Replaces {{title}} {{date}} {{time}} {{date:FORMAT}} {{time:FORMAT}} {{content}} {{fromTitle}} {{newTitle}} (spaces inside the braces are allowed; FORMAT is any
 * Moment format). Anything else, including an unknown {{name}}, stays as written. Returns a malloc'ed NUL-terminated text. */
char *ex_tpl_expand(const char *tpl, size_t len, const ExTplContext *c, size_t *out_len);

/* The properties block at the start of a text: the lines between the first line `---` and the closing line `---` or `...`. Returns 1 and sets *inner0 (the
 * first byte after the opening line), *inner1 (the start of the closing line) and *after (the first byte after the closing line and its line break), or 0. */
int ex_tpl_front(const char *t, size_t n, size_t *inner0, size_t *inner1, size_t *after);

/* What inserting a template into a note changes, as two edits in the coordinates of the note as it is: the properties block (front_text replaces front_del
 * bytes at front_off; has_front is 0 when the template adds nothing there) and the template's text without its properties (body_text, inserted at body_off:
 * the cursor, or the start of the note's text after its properties when the cursor is inside them). front_off + front_del <= body_off. */
typedef struct ExTplPlan {
    int has_front;
    size_t front_off, front_del;
    char *front_text; size_t front_len;
    size_t body_off;
    char *body_text; size_t body_len;
} ExTplPlan;
int ex_tpl_plan(const char *note, size_t nlen, const char *tpl, size_t tlen, const ExTplContext *c, size_t cursor, ExTplPlan *plan);   /* 0, or -1 on memory */
void ex_tpl_plan_free(ExTplPlan *p);
/* The same as a new text (for tests and for a note made from a template): malloc'ed, *cursor_after is where the caret goes (the end of the inserted body). */
char *ex_tpl_apply(const char *note, size_t nlen, const char *tpl, size_t tlen, const ExTplContext *c, size_t cursor, size_t *out_len, size_t *cursor_after);

/* ---- daily notes ---- */
typedef struct ExDailyConfig {
    const char *date_format;     /* Moment tokens; may contain "/" for folders: YYYY/MM/YYYY-MM-DD. Default YYYY-MM-DD. */
    const char *folder;          /* a folder of the vault; empty for the top folder */
    const char *template_path;   /* a note of the vault whose text starts every daily note; empty for none (".md" is added when it has no extension) */
    int tz_min;
} ExDailyConfig;

/* The path of the daily note of the day that holds `ms` (rel to the vault, ".md" added). 0, or -1 when the name is not a valid note name or does not fit. */
int ex_daily_path(const ExDailyConfig *c, double ms, char *out, size_t cap);
/* The day (its start, in ms) of a path that follows the format and the folder, or -1: used to go to the next and the previous daily note. */
int ex_daily_date_of(const ExDailyConfig *c, const char *rel, double *ms);
/* The day before or after (`days` is 1 or -1 or any number): the start of the day that is that many calendar days away. */
double ex_daily_shift(const ExDailyConfig *c, double ms, int days);
/* Makes the note of the day if it is not there: the folders, then the file (never replacing one that exists) with the template's text with its variables
 * ({{title}} is the note's name) filled in. Returns 1 when it was made, 0 when it already existed, -1 on failure; rel_out gets its path. */
int ex_daily_ensure(ExVault *v, const ExDailyConfig *c, double ms, double now_ms, const char *time_format, char *rel_out, size_t cap);

/* ---- unique notes ---- */
typedef struct ExUniqueConfig {
    const char *prefix_format;   /* Moment tokens; default YYYYMMDDHHmm */
    const char *folder;          /* a folder of the vault; empty for the top folder */
    const char *template_path;   /* a note of the vault, or empty */
    int tz_min;
} ExUniqueConfig;
/* The name (without .md) of a unique note made at `ms`: the prefix format filled in. 0, or -1 when it is not a name a note can have. */
int ex_unique_name(const ExUniqueConfig *c, double ms, char *out, size_t cap);
/* Makes the note: its name is the formatted moment; if a note of that name is there, "-2", "-3"... is added until one is free (a note is never replaced or
 * touched). The folder is made when missing; the text is the template's with its variables ({{title}} is the name). Returns 1 and the path, or -1. */
int ex_unique_create(ExVault *v, const ExUniqueConfig *c, double now_ms, const char *date_format, const char *time_format, char *rel_out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
