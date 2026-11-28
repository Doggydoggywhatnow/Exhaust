/* JSON Canvas (sub-slice 6a): the model of a `.canvas` file, reading and writing it, and the geometry the canvas view needs.
 * Format: https://jsoncanvas.org (version 1.0). A canvas is an object with `nodes` and `edges`; a node is a text, file, link or group card
 * with a place and a size, and an edge joins two nodes with an optional label.
 *
 * Nothing the file says is lost. Keys this version does not know are kept with their values as JSON text, on the canvas, on every node and
 * on every edge, and written back; so is the order of the keys of each object, and a node of a type this version does not draw is kept as
 * it is. A file is written only after the user changed something (the view does not write an unedited canvas). The writer uses the layout
 * the JSON Canvas samples and Obsidian use (tab indentation, one node or edge per line, no space after `:` or `,`), so a file in that layout
 * is written back byte for byte; a file in another layout is rewritten in that layout after the first edit, with the same content.
 *
 * Plain C, no threads and no I/O: the caller reads and writes the text (through the note manager, so a canvas gets the journal and the atomic
 * save of every note). */
#ifndef EXHAUST_CANVAS_H
#define EXHAUST_CANVAS_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExCanvasKind {
    EX_CN_TEXT,       /* "type": "text":  `text` is Markdown */
    EX_CN_FILE,       /* "type": "file":  `file` is a path in the vault (`subpath` a heading or block, starting with # or #^) */
    EX_CN_LINK,       /* "type": "link":  `url` */
    EX_CN_GROUP,      /* "type": "group": `label`, an optional background image */
    EX_CN_OTHER       /* a type this version does not know (or something that is not an object): kept, never drawn */
} ExCanvasKind;

typedef enum ExCanvasSide { EX_CS_NONE = -1, EX_CS_TOP = 0, EX_CS_RIGHT = 1, EX_CS_BOTTOM = 2, EX_CS_LEFT = 3 } ExCanvasSide;
typedef enum ExCanvasEnd { EX_CE_DEFAULT = -1, EX_CE_NONE = 0, EX_CE_ARROW = 1 } ExCanvasEnd;   /* DEFAULT: the file does not say (none at the start, an arrow at the end) */

typedef struct ExCanvasKV { char *key; char *json; } ExCanvasKV;       /* a key this version does not know, and its value as compact JSON */

typedef struct ExCanvasNode {
    char *id;                         /* unique in the canvas */
    ExCanvasKind kind;
    char *type;                       /* the "type" string of the file: for EX_CN_OTHER the one it had (NULL when it had none) */
    double x, y, w, h;                /* the file's x, y, width and height */
    char *color;                      /* "1" to "6" (preset) or "#rrggbb" (ex_canvas_color also reads "#rgb"); NULL: none */
    char *text;                       /* EX_CN_TEXT (never NULL for one) */
    char *file, *subpath;             /* EX_CN_FILE (file never NULL for one) */
    char *url;                        /* EX_CN_LINK (never NULL for one) */
    char *label;                      /* EX_CN_GROUP */
    char *background, *background_style;   /* EX_CN_GROUP */
    ExCanvasKV *extra; size_t nextra; /* the keys this version does not know */
    char **order; size_t norder;      /* the keys of the object in the order the file had them */
    char *numtext[4];                 /* x, y, width and height as the file spelled them ("1e2", "250.0"): written back as they were while the value is the same */
    char *raw;                        /* an array element that was not an object: its JSON; everything else is unused */
    int sel;                          /* for the view: the card is selected (not part of the file) */
} ExCanvasNode;

typedef struct ExCanvasEdge {
    char *id;
    char *from, *to;                  /* node ids (fromNode, toNode) */
    int from_side, to_side;           /* ExCanvasSide */
    int from_end, to_end;             /* ExCanvasEnd */
    char *color, *label;
    ExCanvasKV *extra; size_t nextra;
    char **order; size_t norder;
    char *raw;
    int sel;                          /* for the view: the edge is selected (not part of the file) */
} ExCanvasEdge;

typedef struct ExCanvas {
    ExCanvasNode *nodes; size_t n, ncap;          /* the order is the stacking order: later nodes are above earlier ones */
    ExCanvasEdge *edges; size_t ne, necap;
    ExCanvasKV *extra; size_t nextra;             /* top-level keys this version does not know */
    char **order; size_t norder;
    int has_nodes, has_edges;                     /* the file had the key (a new canvas has both) */
    int final_newline;                            /* the file ended with a line break */
    int repaired;                                 /* an id was missing or repeated and was replaced: the text the file has is not what would be written */
    unsigned long rev;                            /* grows with every change made through the functions below */
    size_t *hash; size_t hcap; int hvalid;        /* an index of the cards by id, built when ex_canvas_find_node first needs it (and after a card is added, removed or moved in the order) */
} ExCanvas;

/* An empty canvas (nodes and edges both empty). */
ExCanvas *ex_canvas_new(void);
/* Reads the text of a file (UTF-8, no BOM). Returns NULL when it is not a JSON object, with the reason in err (if given). */
ExCanvas *ex_canvas_parse(const char *text, size_t len, char *err, size_t errcap);
void ex_canvas_free(ExCanvas *c);
/* The file's text (malloc'ed, NUL terminated; *len its size). */
char *ex_canvas_write(const ExCanvas *c, size_t *len);

/* Nodes. Indexes are positions in c->nodes; the functions that add or remove move the indexes after them. */
int ex_canvas_find_node(const ExCanvas *c, const char *id);                  /* -1 if none */
/* A new card of that kind at the place and size, on top; its id is new. Returns its index, or -1 (out of memory). */
int ex_canvas_add_node(ExCanvas *c, ExCanvasKind kind, double x, double y, double w, double h);
/* Removes the card and every edge that joins it. Returns 0, or -1 for a bad index. */
int ex_canvas_remove_node(ExCanvas *c, size_t i);
/* Moves a card to the top of the stacking order; returns its new index. */
int ex_canvas_raise_node(ExCanvas *c, size_t i);
/* Sets a text field of a node (any of its string members, by pointer to the member): copies s, or clears it when s is NULL. Returns 0, or -1. */
int ex_canvas_set_string(ExCanvas *c, char **field, const char *s);
void ex_canvas_touch(ExCanvas *c);                                           /* a caller that changes a member itself (x, y, w, h) tells the canvas */

/* Edges. */
int ex_canvas_add_edge(ExCanvas *c, size_t from, int from_side, size_t to, int to_side);   /* ids new; index, or -1 */
int ex_canvas_remove_edge(ExCanvas *c, size_t i);

/* Geometry, in canvas units. */
/* The box round every card (empty canvas: returns 0). */
int ex_canvas_bounds(const ExCanvas *c, double *x0, double *y0, double *x1, double *y1);
/* The topmost card at a point (a group only where no other card is), or -1. Cards that are not drawn (EX_CN_OTHER) are never hit. */
int ex_canvas_node_at(const ExCanvas *c, double x, double y);
/* The sides an edge leaves and enters by (the file's, or the ones that face each other) and the points on them. Returns 0 if an end is missing. */
int ex_canvas_edge_ends(const ExCanvas *c, const ExCanvasEdge *e, double *x1, double *y1, int *side1, double *x2, double *y2, int *side2);
/* The point on the middle of a side of a card. */
void ex_canvas_side_point(const ExCanvasNode *n, int side, double *x, double *y);
/* The side of a card nearest to a point. */
int ex_canvas_nearest_side(const ExCanvasNode *n, double x, double y);
/* The cards a group holds: the cards and smaller groups whose boxes lie wholly inside the group's box.
 * Fills out (up to max) and returns how many there are. */
size_t ex_canvas_group_members(const ExCanvas *c, size_t group, size_t *out, size_t max);

/* Colors. A preset "1".."6" returns the number 1..6 (red, orange, yellow, green, cyan, purple); a "#rrggbb" returns 0 and fills rgb; anything else -1. */
int ex_canvas_color(const char *color, uint8_t rgb[3]);

/* A new unique id of 16 lowercase hexadecimal digits (for the test: the seed of the generator can be set). */
void ex_canvas_set_seed(uint64_t seed);
void ex_canvas_new_id(const ExCanvas *c, char out[17]);

#ifdef __cplusplus
}
#endif
#endif
