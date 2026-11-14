#ifndef EXHAUST_READING_VIEW_H
#define EXHAUST_READING_VIEW_H

#include "exhaust_editor.h"
#include "exhaust_reading.h"
#include "exhaust_render.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

typedef struct ExReadingView ExReadingView;
typedef void (*ExReadingLinkFn)(ExRSpanKind kind, const char *target, void *user);

ExReadingView *ex_readingview_create(const char *text, size_t len);
void ex_readingview_free(ExReadingView *v);
void ex_readingview_set_text(ExReadingView *v, const char *text, size_t len);
void ex_readingview_set_link_handler(ExReadingView *v, ExReadingLinkFn fn, void *user);
void ex_readingview_set_viewport(ExReadingView *v, float x, float y, float w, float h);
void ex_readingview_scroll(ExReadingView *v, float dy);
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

#endif
