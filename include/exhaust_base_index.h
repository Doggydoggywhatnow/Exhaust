/* The Bases module and the index (sub-slice 6b-ii): the files a base is run over, read from the index, and the Markdown a note shows for a base it holds.
 * Both run on the service thread. */
#ifndef EXHAUST_BASE_INDEX_H
#define EXHAUST_BASE_INDEX_H
#include <stddef.h>
#include "exhaust_base.h"
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every file of the vault with its properties, tags, links and embeds (times in milliseconds). 0, or -1. */
int ex_base_data_load(ExIndex *r, ExBaseData *out);

/* The text of a note with its bases shown: a fenced block ```base ... ``` and a line that is only an embed of a `.base` file (`![[File.base]]` or
 * `![[File.base#View]]`) become the Markdown of the view (a table; a list view is a bulleted list). Everything else is as it was. Returns the new text
 * (malloc'ed), or NULL when the note holds neither. this_path is the note (what `this` is in an embedded base). */
char *ex_base_expand(ExIndex *r, const char *root, const char *this_path, const char *text, size_t len, int tz_min, double now_ms, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
