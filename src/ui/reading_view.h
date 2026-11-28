#ifndef EXHAUST_READING_VIEW_H
#define EXHAUST_READING_VIEW_H

#include "exhaust_editor.h"
#include "exhaust_imagecache.h"
#include "exhaust_reading.h"
#include "exhaust_render.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

typedef struct ExReadingView ExReadingView;
typedef void (*ExReadingLinkFn)(ExRSpanKind kind, const char *target, void *user);

/* Embedded media (6c-v). A paragraph that is only `![[photo.png|640x480]]`, `![[song.flac]]`, `![[clip.mp4]]`, `![[f.pdf#page=3&height=400]]` or `![[x.canvas]]` (or a
 * Markdown image of a vault file) is drawn as the picture, or as a card with the file's name: a click on a card hands "path#fragment" to the link handler as an
 * EX_RSK_EMBED link (the application plays an audio file, or opens the file in a tab). The view only draws and finds what was clicked. */

void ex_readingview_set_media(ExReadingView *v, const ExReadingMedia *m);


ExReadingView *ex_readingview_create(const char *text, size_t len);
void ex_readingview_free(ExReadingView *v);
void ex_readingview_set_text(ExReadingView *v, const char *text, size_t len);
void ex_readingview_set_link_handler(ExReadingView *v, ExReadingLinkFn fn, void *user);
void ex_readingview_set_viewport(ExReadingView *v, float x, float y, float w, float h);
void ex_readingview_column(const ExReadingView *v, float *x, float *w);   /* left offset and width for a header whose text lines up with the page (after a layout; w is 0 before) */
void ex_readingview_set_head(ExReadingView *v, float px);   /* room above the first block for a header widget that scrolls with the page */
void ex_readingview_scroll(ExReadingView *v, float dy);
void ex_readingview_set_options(ExReadingView *v, int strict_line_breaks, int readable_line_length);   /* the settings of the same names */
void ex_readingview_set_page_scale(ExReadingView *v, float f);   /* the readable width of the page grows with the text (slides); the text size itself comes with draw's base_size */
float ex_readingview_scroll_y(const ExReadingView *v);
void ex_readingview_set_scroll(ExReadingView *v, float y);
/* Lays out (if needed) for the viewport width and draws. */
void ex_readingview_draw(ExReadingView *v, ExRenderer *r, const ExTheme *t, float base_size);
/* Mouse: returns 1 if the press is on something clickable (it is then captured until the release, which follows the link or toggles the callout). */
int ex_readingview_mouse(ExReadingView *v, ExMouse kind, float x, float y);
/* The internal link under a window position (its target as in the note: Note#Heading), or 0. */
int ex_readingview_internal_link_at(ExReadingView *v, float x, float y, char *target, size_t cap);
void ex_readingview_key(ExReadingView *v, ExKey key);
char *ex_readingview_plain_text(const ExReadingView *v);
float ex_readingview_content_height(const ExReadingView *v);
/* 1 while equations are still to be laid out (a layout makes at most 24 new ones, so a frame's cost does not grow with the number of equations); for tests. */
int ex_readingview_math_pending(const ExReadingView *v);
/* 1 while Mermaid diagrams are still to be laid out (a layout makes at most 3 new ones); for tests. */
int ex_readingview_diagram_pending(const ExReadingView *v);

#endif
