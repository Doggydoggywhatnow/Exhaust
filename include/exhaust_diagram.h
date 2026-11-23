/* Diagrams (sub-slices 6d-ii and 6d-iii): Mermaid text is parsed and laid out into a list of shapes, lines and texts in units of the font size (em), with
 * no renderer involved, so a layout can be compared with a golden listing (ex_diagram_dump) and drawn at any size. See docs/formats.md (Mermaid).
 * Supported: flowchart and sequence diagrams (6d-ii); class, state, entity-relationship, pie and gantt diagrams (6d-iii). UI thread only (the
 * font is not shared between threads), but a layout takes a millisecond or two; the reading view makes at most a few new ones in a pass. */
#ifndef EXHAUST_DIAGRAM_H
#define EXHAUST_DIAGRAM_H

#include <stddef.h>
#include <stdint.h>
#include "exhaust_text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExDiagKind { EXD_UNKNOWN = 0, EXD_FLOWCHART, EXD_SEQUENCE, EXD_CLASS, EXD_STATE, EXD_ER, EXD_PIE, EXD_GANTT } ExDiagKind;

typedef enum ExDItemKind {
    EXD_RECT, EXD_ROUND, EXD_STADIUM, EXD_DIAMOND, EXD_ELLIPSE, EXD_HEXAGON, EXD_PARALLELOGRAM, EXD_PARALLELOGRAM_ALT, EXD_TRAPEZOID, EXD_TRAPEZOID_ALT,
    EXD_CYLINDER, EXD_SUBROUTINE, EXD_FLAG, EXD_DOUBLE_CIRCLE,   /* shapes with a box (x, y, w, h) */
    EXD_LINE,                                                  /* a polyline through pts[pt0 .. pt0+npt) with heads at its ends */
    EXD_TEXT,                                                  /* text (lines separated by \n) anchored at (x, y), y = the middle of the first line */
    EXD_WEDGE                                                  /* a pie slice: centre (x, y), radius w, from angle a0 to a1 (radians, clockwise from 12 o'clock) */
} ExDItemKind;

/* Colors are roles; the drawing maps them to theme tokens (CAT + n: the n-th callout color, for pie slices, bars and classes). */
typedef enum ExDColor {
    EXD_C_NONE = 0, EXD_C_TEXT, EXD_C_MUTED, EXD_C_NODE_FILL, EXD_C_NODE_STROKE, EXD_C_ACCENT, EXD_C_ACCENT_FILL, EXD_C_PANEL, EXD_C_LINE, EXD_C_LABEL_BG,
    EXD_C_NOTE_FILL, EXD_C_ERROR,
    EXD_C_BEST,      /* text only: the theme's text or background color, whichever contrasts more with the item's `fill` role (text on a colored slice or bar) */
    EXD_C_CAT = 16   /* EXD_C_CAT + 0 .. 12 */
} ExDColor;

typedef enum ExDHead {
    EXD_H_NONE = 0, EXD_H_ARROW, EXD_H_OPEN, EXD_H_CROSS, EXD_H_CIRCLE,
    EXD_H_TRIANGLE, EXD_H_DIAMOND, EXD_H_DIAMOND_FILLED,        /* UML: inheritance, aggregation, composition */
    EXD_H_ONE, EXD_H_ZERO_ONE, EXD_H_MANY, EXD_H_ONE_MANY, EXD_H_ZERO_MANY, EXD_H_ONLY_ONE   /* entity-relationship cardinality marks */
} ExDHead;

typedef struct ExDItem {
    uint8_t kind, fill, stroke, dash;            /* dash: 0 solid, 1 dashed, 2 dotted */
    uint8_t head_end, head_start, align, bold;   /* align: 0 centered, 1 left, 2 right */
    float x, y, w, h;
    float width;                                  /* stroke width in em */
    float a0, a1;
    float size;                                   /* text size as a multiple of the font size (0 = 1) */
    uint32_t pt0, npt;                            /* LINE: first point (x, y pairs) and number of points */
    uint32_t text, tlen;                          /* TEXT: offset and length in the text pool */
} ExDItem;

typedef struct ExDiagram ExDiagram;

/* Parses and lays out `src` (the text of a ```mermaid block). Never NULL except when memory is out: a diagram that cannot be drawn has ex_diagram_error() set
 * and a message, and its items are empty. */
ExDiagram *ex_diagram_parse(ExFont *font, const char *src, size_t len);
void ex_diagram_free(ExDiagram *d);
const char *ex_diagram_error(const ExDiagram *d);   /* NULL when it can be drawn */
ExDiagKind ex_diagram_kind(const ExDiagram *d);
float ex_diagram_width(const ExDiagram *d);         /* em */
float ex_diagram_height(const ExDiagram *d);
const ExDItem *ex_diagram_items(const ExDiagram *d, size_t *n);
const float *ex_diagram_points(const ExDiagram *d, size_t *n);   /* x, y pairs: n is the number of floats */
const char *ex_diagram_text(const ExDiagram *d, const ExDItem *it);   /* NUL-terminated copy owned by the diagram */
ExFont *ex_diagram_font(const ExDiagram *d);
/* A listing of the whole layout, one item a line, numbers to two decimals: the "golden render output" the tests compare. Returns the length written. */
size_t ex_diagram_dump(const ExDiagram *d, char *buf, size_t cap);
/* The width of a text in em at the font size 1 (what the layout uses): kerning is not applied. */
float ex_diagram_text_width(ExFont *font, const char *s, size_t n);

/* A cache of diagrams keyed by a hash of (font, source), the same as the math cache: layouts asked for in a pass stay until the next begin. */
typedef struct ExDiagramCache ExDiagramCache;
ExDiagramCache *ex_diagcache_create(void);
void ex_diagcache_free(ExDiagramCache *c);
void ex_diagcache_begin(ExDiagramCache *c);
const ExDiagram *ex_diagcache_lookup(ExDiagramCache *c, ExFont *font, const char *src, size_t len);
const ExDiagram *ex_diagcache_get(ExDiagramCache *c, ExFont *font, const char *src, size_t len);
size_t ex_diagcache_count(const ExDiagramCache *c);
size_t ex_diagcache_misses(const ExDiagramCache *c);

#ifdef __cplusplus
}
#endif
#endif
