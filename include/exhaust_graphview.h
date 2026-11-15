/* The graph view (sub-slice 5a): the widget that draws the graph of the vault and the model it draws from.
 *
 * Model. One ExGraphModel for the window holds the graph (from the index, read on the service thread), its layout (running on its own
 * worker thread) and what the views need from the two: the newest positions, a grid over them and the neighbours of every node. Any
 * number of graph widgets can show the same model (a graph in two panes runs one layout). Call ex_graphmodel_update once a frame, on
 * the UI thread, before laying out and drawing.
 *
 * View. A graph widget draws only what is on screen (the grid says which nodes) into one geometry buffer per frame, finds the node under
 * the pointer through the same grid, and keeps its own zoom, centre, hover and selection. It follows the layout (fits the whole graph
 * in the view) until the user pans or zooms. Mouse: drag pans, the wheel zooms about the pointer, a click opens the note, a right click
 * asks for a menu. Keyboard (the widget takes the focus with a click or Tab): arrow keys pan, + and - zoom, 0 fits the graph, Page Down
 * and Page Up step through the nodes, Enter opens the selected node, Escape clears the selection. */
#ifndef EXHAUST_GRAPHVIEW_H
#define EXHAUST_GRAPHVIEW_H
#include "exhaust_graph.h"
#include "exhaust_graphopts.h"
#include <stdint.h>
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExGraphModel ExGraphModel;

ExGraphModel *ex_graphmodel_create(void);
void ex_graphmodel_free(ExGraphModel *m);
/* Takes a graph (malloc'ed, to be freed with ex_graph_free and free) and starts its layout; a graph that replaces an older one starts
 * where the old one was. */
void ex_graphmodel_set_graph(ExGraphModel *m, ExGraph *g);
/* UI thread, once a frame: takes the newest positions. Returns 1 if anything the views draw changed. */
int ex_graphmodel_update(ExGraphModel *m);
/* The settings of the gear panel (5b). The forces reach the layout at once and the display options the next frame; the return value is 1
 * when the filters or groups changed: the caller must have the graph read again with them (the service thread applies them). */
int ex_graphmodel_set_options(ExGraphModel *m, const ExGraphOptions *o);
const ExGraphOptions *ex_graphmodel_options(const ExGraphModel *m);
/* The model of a local graph tab (5c): only the message shown when there is nothing to draw differs. */
void ex_graphmodel_set_local(ExGraphModel *m, int local);
/* The note open in the main text view (drawn in the focus color); NULL for none. */
void ex_graphmodel_set_active(ExGraphModel *m, const char *path);
int ex_graphmodel_animating(const ExGraphModel *m);     /* the layout is still moving the nodes: draw every frame */
/* The layout's temperature (1 at a cold start, falling to 0): for the tests, which check that a graph read again after a save restarts it gently. */
float ex_graphmodel_temperature(const ExGraphModel *m);
/* The temperature the current layout started at: not a moving number, so a test can read it at any time. */
float ex_graphmodel_start_temperature(const ExGraphModel *m);
size_t ex_graphmodel_node_count(const ExGraphModel *m);
size_t ex_graphmodel_edge_count(const ExGraphModel *m);
const ExGraph *ex_graphmodel_graph(const ExGraphModel *m);
/* For tests: the newest positions (x and y interleaved). */
const float *ex_graphmodel_positions(const ExGraphModel *m);

typedef struct ExGraphHandlers {
    /* a click on a node, or Enter on the selected one; path is the note's path, or for an unresolved node the name it is linked as */
    void (*open)(void *user, const char *path, int kind, int new_tab);
    /* a right click: x and y in window pixels; node is -1 when the click was on empty space */
    void (*context)(void *user, const char *path, int kind, float x, float y);
    /* the pointer moved onto a node (path is NULL: off every node), x and y in window pixels */
    void (*hover)(void *user, const char *path, int kind, float x, float y);
    /* the keyboard asks for the page preview of the selected node (5c), at x and y in window pixels; path is NULL to close it */
    void (*preview)(void *user, const char *path, int kind, float x, float y);
    void *user;
} ExGraphHandlers;

/* The widget. The model must outlive it. */
ExWidget *ex_widget_graph(ExUi *ui, ExGraphModel *m, const char *name, const ExGraphHandlers *h);
/* Hands the widget something it frees with itself and ticks once a frame, before it draws (the gear panel's state, 5b); act is given the
 * keys the panel owns (5c: 'g' the gear, '[' and ']' the depth of a local graph) and returns 1 if it used one. */
void ex_graphview_set_extra(ExWidget *w, void *extra, void (*free_extra)(void *), void (*tick)(void *, int64_t), int (*act)(void *, int));
/* The animation (5c): the nodes appear in the order their files were created, over about ten seconds; a name or file that is not a note
 * appears with the first note that links to it. It ends when everything is shown or when stopped. animations_running counts the views
 * that are animating, so the main loop draws every frame; seek (for tests) moves a running animation to a fraction of its length. */
void ex_graphview_animate(ExWidget *w, int on);
void ex_graphview_animation_toggle(ExWidget *w);
int ex_graphview_animation_running(const ExWidget *w);
int ex_graphview_animations_running(void);
void ex_graphview_animation_seek(ExWidget *w, float fraction);
/* Mouse wheel over the widget: dy > 0 zooms in (a notch is 1), about the pointer at window position x, y. */
void ex_graphview_wheel(ExWidget *w, float dy, float x, float y);
/* The pointer is at x, y (window pixels), over this widget or, with inside = 0, has left it. */
void ex_graphview_hover(ExWidget *w, float x, float y, int inside);
/* A right click at x, y. */
void ex_graphview_context(ExWidget *w, float x, float y);
/* Where nodes are on the screen, for the tests: the number of nodes and edges drawn in the last frame, the nodes under the cull
 * rectangle, and a node's centre in window pixels. */
typedef struct ExGraphViewStats {
    size_t nodes_drawn, edges_drawn, labels_drawn, triangles, geom_grows; float zoom; int hover, selected, following;
    float line_px, node_px, label_zoom;      /* the width of a line, the radius of a node with one link, and the zoom above which labels show (5b) */
} ExGraphViewStats;
void ex_graphview_stats(const ExWidget *w, ExGraphViewStats *out);
int ex_graphview_node_at(ExWidget *w, float x, float y);                  /* the node under a window position, or -1 */
int ex_graphview_node_screen(ExWidget *w, int node, float *x, float *y);  /* 0 if it is off the view */
void ex_graphview_select(ExWidget *w, int node);
/* The path of the node the pointer is over (a note's path, or the name of an unresolved node) and its kind, or NULL. For the page preview. */
const char *ex_graphview_hover_path(const ExWidget *w, int *kind);
/* A one-line description for screen readers (malloc'ed): counts and the selected node. */
char *ex_graphview_summary(const ExWidget *w);

#ifdef __cplusplus
}
#endif
#endif
