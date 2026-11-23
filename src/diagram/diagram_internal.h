/* Shared by the diagram parsers and layouts (6d-ii, 6d-iii); not public. */
#ifndef EXHAUST_DIAGRAM_INTERNAL_H
#define EXHAUST_DIAGRAM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "exhaust_diagram.h"

#define DG_MAX_INPUT 40000
#define DG_MAX_NODES 600

struct ExDiagram {
    ExFont *font;
    ExDiagKind kind;
    ExDItem *items; size_t n, cap;
    float *pts; size_t npts, cappts;
    char *text; size_t ntext, captext;
    float w, h;
    char err[120];
    int has_err;
};

/* line-by-line reading of the source: blank lines and comment lines (%%) are skipped, the line is trimmed */
typedef struct DgLines { const char *s; size_t n, pos; } DgLines;
int dg_next_line(DgLines *L, const char **line, size_t *len);

void dg_fail(ExDiagram *d, const char *fmt, ...);
ExDItem *dg_add(ExDiagram *d, int kind);                                    /* a zeroed item of this kind (NULL when out of memory), at the end */
uint32_t dg_add_points(ExDiagram *d, const float *xy, size_t npoints);      /* returns the first index in pts (in floats) */
uint32_t dg_add_string(ExDiagram *d, const char *s, size_t n);

/* shortcuts */
ExDItem *dg_box(ExDiagram *d, int kind, float x, float y, float w, float h, int fill, int stroke);
ExDItem *dg_line(ExDiagram *d, const float *xy, size_t npoints, int color, int dash, int head_start, int head_end);
ExDItem *dg_label(ExDiagram *d, float x, float y, const char *s, size_t n, int align, float size, int color, int bold);

/* text in em at font size 1: width of the widest line, height of all lines (1.25 per line) */
void dg_text_size(ExDiagram *d, const char *s, size_t n, float size, float *w, float *h);
float dg_line_height(void);
/* a label cleaned for drawing: <br> becomes a line break, other tags go, #quot; #amp; #lt; #gt; are decoded, surrounding quotes go. malloc'ed. */
char *dg_clean_label(const char *s, size_t n);
char *dg_strndup(const char *s, size_t n);
/* moves everything so the content starts at (pad, pad) and sets the size */
void dg_finalize(ExDiagram *d, float pad);

int dg_flowchart(ExDiagram *d, DgLines *L, const char *first_line, size_t first_len);
int dg_sequence(ExDiagram *d, DgLines *L);
int dg_class(ExDiagram *d, DgLines *L);
int dg_state(ExDiagram *d, DgLines *L, const char *first_line, size_t first_len);
int dg_er(ExDiagram *d, DgLines *L);
int dg_pie(ExDiagram *d, DgLines *L, const char *first_line, size_t first_len);
int dg_gantt(ExDiagram *d, DgLines *L);

/* a layered graph layout shared by the flowchart, the class, the state and the entity-relationship diagrams */
typedef struct LgNode { float w, h; float x, y; } LgNode;    /* x, y: the centre after the layout */
typedef struct LgEdge { int a, b; float lw, lh; int minlen; float *pts; int npts; float lx, ly; } LgEdge;   /* pts: the route's points; lx, ly: where a label goes */
typedef struct LgGraph {
    LgNode *nodes; int nn;
    LgEdge *edges; int ne;
    int dir;               /* 0 TB, 1 BT, 2 LR, 3 RL */
    float nodesep, ranksep;
} LgGraph;
/* shared by the graph-like types (class, state, entity-relationship) */
void dg_clip_box(float cx, float cy, float w, float h, int shape, float tx, float ty, float *ox, float *oy);   /* shape: EXD_RECT (any box), EXD_ELLIPSE or EXD_DIAMOND */
ExDItem *dg_edge_line(ExDiagram *d, const LgGraph *g, int e, int shape_a, int shape_b, int color, int dash, int hs, int he);   /* a link as a line from node a to node b */
void dg_edge_label(ExDiagram *d, float cx, float cy, const char *s, float size);    /* a text on a background that hides the line under it */
void dg_append(ExDiagram *dst, const ExDiagram *src, float dx, float dy);           /* copies the items of another diagram, moved */
/* parses `<number>` of a text; returns the end of the number or NULL */
const char *dg_parse_number(const char *s, double *v);

int lg_layout(LgGraph *g);           /* fills nodes[].x, y and edges[].pts (malloc'ed, freed by lg_free), lx, ly; returns 0 on success */
void lg_free(LgGraph *g);            /* frees the routes only */

#endif
