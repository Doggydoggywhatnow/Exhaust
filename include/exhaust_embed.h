/* Embedded notes (sub-slice 6c-v): the text of a note with its note embeds shown. A line that is only `![[Note]]`, `![[Note#Heading]]` or `![[Note#^block]]`
 * (a note: a name with no extension or `.md`) is replaced by the embedded text as a quotation headed by the note's name, so reading mode shows it in place.
 * Lines inside code fences, embeds of files that are not notes (pictures, audio, video, PDFs, canvases and bases are drawn or handled by the reading view and
 * the Bases code), and embeds that do not resolve are left as they are. The text of the embedded note is not expanded again (embeds inside it stay as written, so
 * a note that embeds itself cannot loop). At most 8 embeds are expanded in a note and 3000 bytes of each. Nothing is written: this is the text reading mode
 * shows. Runs on the service thread. */
#ifndef EXHAUST_EMBED_H
#define EXHAUST_EMBED_H
#include <stddef.h>
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the new text (malloc'ed), or NULL when no embed was expanded. this_path is the note the text belongs to (links resolve from it). */
char *ex_embed_expand(ExIndex *r, const char *root, const char *this_path, const char *text, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
