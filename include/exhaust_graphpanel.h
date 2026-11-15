/* The graph's gear panel (sub-slice 5b): a gear button floating at the top right of a graph widget, and under it a panel with the filters,
 * groups, display options and forces (collapsible sections). It edits the options of the graph's model; the application hears of each change
 * through the callback (to save it in the session, and, when the filters changed, to have the graph read again). */
#ifndef EXHAUST_GRAPHPANEL_H
#define EXHAUST_GRAPHPANEL_H
#include <stdint.h>
#include "exhaust_graphview.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExGraphPanel ExGraphPanel;
/* filters: 1 if the change is to the filters or groups (the graph must be read again with them) */
typedef void (*ExGraphPanelChanged)(void *user, const ExGraphOptions *o, int filters);

/* Adds the gear and the panel as children of the graph widget and gives the widget the state to free with itself. The model must outlive it. */
ExGraphPanel *ex_graphpanel_create(ExUi *ui, ExWidget *graph, ExGraphModel *m, ExGraphPanelChanged changed, void *user);
/* Once a frame (the graph widget calls it itself): commits a text field after each keystroke, and brings the controls up to date when the model's options were
 * changed from elsewhere (a restored session, another graph pane). */
void ex_graphpanel_tick(ExGraphPanel *p, int64_t now_ms);
/* For the tests: the panel box, whether it is open, the focusable control whose accessible name starts with a given text (NULL if none). */
ExWidget *ex_graphpanel_box(const ExGraphPanel *p);
ExWidget *ex_graphpanel_gear(const ExGraphPanel *p);
void ex_graphpanel_open(ExGraphPanel *p, int open);
int ex_graphpanel_is_open(const ExGraphPanel *p);
ExWidget *ex_graphpanel_find(const ExGraphPanel *p, const char *name);

#ifdef __cplusplus
}
#endif
#endif
