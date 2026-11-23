/* The ribbon (sub-slice 7f): the thin bar of buttons at the far left. The buttons are commands (docs/formats.md, "Shell"); which of them show
 * is the setting `ribbon_items`, and `show_ribbon` hides the bar. A button runs its command through the hook, so the ribbon holds no
 * knowledge of what the commands do. */
#ifndef EXHAUST_RIBBON_H
#define EXHAUST_RIBBON_H

#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExRibbon ExRibbon;

typedef struct ExRibbonHooks {
    void *user;
    void (*run)(void *user, const char *command_id);     /* a button was pressed */
} ExRibbonHooks;

/* Builds the bar with all its buttons. The widget is yours to place in the window; free the ExRibbon with ex_ribbon_free (the widgets go with the tree). */
ExRibbon *ex_ribbon_create(ExUi *ui, const ExRibbonHooks *hooks);
void ex_ribbon_free(ExRibbon *r);
ExWidget *ex_ribbon_widget(ExRibbon *r);
ExWidget *ex_ribbon_button(ExRibbon *r, const char *command_id);        /* NULL for an id that has no button */
/* show: the bar at all (show_ribbon); items: the value of ribbon_items; graph_feature: the Graph view feature is on (its button needs it). */
void ex_ribbon_apply(ExRibbon *r, int show, const char *items, int graph_feature, int canvas_feature);
/* Is the bar, or the button of that command, visible now? For tests and the keyboard. */
int ex_ribbon_visible(const ExRibbon *r);
int ex_ribbon_button_visible(const ExRibbon *r, const char *command_id);

#ifdef __cplusplus
}
#endif
#endif
