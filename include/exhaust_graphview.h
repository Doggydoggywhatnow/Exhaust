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
/* The note open in the main text view (drawn in the focus color); NULL for none. */
void ex_graphmodel_set_active(ExGraphModel *m, const char *path);
int ex_graphmodel_animating(const ExGraphModel *m);     /* the layout is still moving the nodes: draw every frame */
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
    void *user;
} ExGraphHandlers;

/* The widget. The model must outlive it. */
ExWidget *ex_widget_graph(ExUi *ui, ExGraphModel *m, const char *name, const ExGraphHandlers *h);
/* Mouse wheel over the widget: dy > 0 zooms in (a notch is 1), about the pointer at window position x, y. */
void ex_graphview_wheel(ExWidget *w, float dy, float x, float y);
/* The pointer is at x, y (window pixels), over this widget or, with inside = 0, has left it. */
void ex_graphview_hover(ExWidget *w, float x, float y, int inside);
/* A right click at x, y. */
void ex_graphview_context(ExWidget *w, float x, float y);
/* Where nodes are on the screen, for the tests: the number of nodes and edges drawn in the last frame, the nodes under the cull
 * rectangle, and a node's centre in window pixels. */
typedef struct ExGraphViewStats { size_t nodes_drawn, edges_drawn, labels_drawn, triangles, geom_grows; float zoom; int hover, selected, following; } ExGraphViewStats;
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
