/* How one line of a note is shown in the editor. Source mode shows the text unchanged with the
 * syntax in quieter colors; live preview also hides the syntax of the constructs the cursor is not in,
 * and styles what is left (bold, italic, links, headings, code...). A line becomes a list of runs: each
 * run is a piece of the source with one style, or a stand-in for a piece of the source (a bullet for
 * "- ", a bar for ">", a checkbox for "[ ]") or nothing at all (hidden syntax). The runs know the
 * source range they stand for, so carets, selections and mouse clicks map between the text and the
 * screen. */
#ifndef EXHAUST_EDITOR_LAYOUT_H
#define EXHAUST_EDITOR_LAYOUT_H

#include <stddef.h>

#include "exhaust_buffer.h"
#include "exhaust_markdown.h"
#include "exhaust_render.h"
#include "exhaust_shape.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

typedef enum LRunKind { LRUN_TEXT, LRUN_HIDDEN, LRUN_SUBST, LRUN_BAR, LRUN_CHECKBOX } LRunKind;

enum { LF_BOLD = 1, LF_ITALIC = 2, LF_MONO = 4, LF_STRIKE = 8, LF_UNDERLINE = 16 };

typedef struct LRun {
    LRunKind kind;
    size_t s, e;                 /* the source bytes this run stands for */
    const char *text; size_t tlen;   /* what is drawn (a TEXT run: a slice of the source; a SUBST run: the stand-in) */
    ExFont *font;
    float size;                  /* font size at 100% zoom */
    int fg;                      /* ExColorToken */
    int bg;                      /* ExColorToken, or -1 */
    unsigned flags;
    int checked;                 /* CHECKBOX: 1 done */
    float x, w;                  /* position and width in pixels */
    ExShaped sh;
    int shaped;
} LRun;

typedef struct LLine {
    LRun *runs; size_t n;
    float width;
    int callout;                 /* 0, or 1 + the callout type: the line is inside a callout */
    int code;                    /* the line is part of a code block or a math block */
    int rule;                    /* draw a horizontal rule (a hidden --- line) */
    int quote_depth;
    char *text;                  /* a copy of the line's text; the runs point into it */
} LLine;

typedef struct LEnv {
    ExRenderer *r;
    float size;                  /* base font size at 100% zoom */
    int live;                    /* hide the syntax of constructs the cursor is not in */
    const ExMd *md;              /* tokens of the whole note; NULL: no styling */
    const ExDoc *doc;            /* selections */
    const char *line;            /* the line's text */
    size_t ls, le;               /* its byte range in the note */
} LEnv;

LLine *ll_build(const LEnv *env);
void ll_free(LLine *l);
float ll_x_of(const LLine *l, size_t offset);        /* x of a source offset (relative to the line's left edge) */
size_t ll_offset_at(const LLine *l, float x, size_t ls, size_t le);
size_t ll_display(const LLine *l, char *out, size_t cap);   /* the text that is shown, for tests */

#endif
