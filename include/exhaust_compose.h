/* Note composer (sub-slice 6e-ii): merge a note into another, and split a part of a note (a selection or a heading's section) into a new note, with the links
 * kept: a merge makes every link to the merged note point at the note it went into (link-safe rename's machinery, exhaust_rename.h); an extraction leaves a
 * link, an embed or nothing in its place. No UI here; the commands in src/main.c call these. See docs/formats.md (Note composer). */
#ifndef EXHAUST_COMPOSE_H
#define EXHAUST_COMPOSE_H

#include <stddef.h>
#include "exhaust_index.h"
#include "exhaust_props.h"
#include "exhaust_rename.h"
#include "exhaust_vault.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExAfterExtract { EX_AFTER_LINK = 0, EX_AFTER_EMBED = 1, EX_AFTER_NONE = 2 } ExAfterExtract;

typedef struct ExComposeConfig {
    ExAfterExtract after_extract;       /* [note_composer] text_after_extract */
    const char *template_text;          /* the template file's text, or NULL: {{content}} {{fromTitle}} {{newTitle}} {{title}} {{date:..}} {{time:..}} */
    size_t template_len;
    double now_ms; int tz_min;
    const char *date_format, *time_format;
} ExComposeConfig;

/* The section of the heading that governs offset `at`: from the start of the heading's line up to the start of the next heading of the same or a higher level
 * (or the end of the text). Headings are `#` lines outside code fences and outside the properties. Returns 0 and the heading's text (without the hashes), or
 * -1 when `at` is before the first heading or inside the properties. */
int ex_compose_heading_span(const char *text, size_t len, size_t at, size_t *start, size_t *end, char *heading, size_t cap);

/* A name for a new note from a text: the first line, with the characters a note name cannot have taken out, at most 60 characters; "Untitled" when nothing is
 * left. */
void ex_compose_suggest_name(const char *text, size_t len, char *out, size_t cap);

/* Makes the new note of an extraction beside `source_rel` (never replacing a file) with the template's text, or the content as it is when there is none (a
 * template without {{content}} gets the content after it), and gives what goes where the content was: a link, an embed or nothing. Returns 0 and the new path
 * (malloc'ed), or a negative ExVaultResult (invalid name, exists, io). */
int ex_compose_extract(ExVault *v, const char *source_rel, const char *content, size_t content_len, const char *name, const ExComposeConfig *cfg, char **new_rel, char **replacement);

/* The text of `into` after `from` is merged into it: from's properties are merged into into's (as a template's are, exhaust_templates.h), then from's text is
 * added at the end after a blank line; with a template, its text (with {{content}} = from's text without its properties, {{fromTitle}} and {{newTitle}}, the
 * title of `into`) is added instead. malloc'ed. */
char *ex_compose_merge_text(const char *from, size_t from_len, const char *from_title, const char *into, size_t into_len, const char *into_title, const ExComposeConfig *cfg, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
