/* The pane area: turns a workspace (exhaust_workspace.h) into widgets: for each group a strip of
 * tabs above the content of the active tab, and splits with draggable dividers. The workspace is
 * the single source of truth; this module only reads it, rebuilds the widgets when its layout
 * changes, and turns mouse input (clicking and dragging tabs, dragging dividers, drops from the
 * desktop) into workspace operations. What a tab shows is up to the application (hooks). */
#ifndef EXHAUST_PANES_H
#define EXHAUST_PANES_H

#include "exhaust_ui.h"
#include "exhaust_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExPanes ExPanes;

typedef struct ExPanesHooks {
    /* The widget that shows a tab's view. It is created when the tab first becomes visible and kept
     * (with its scroll position and cursor) until the tab closes. */
    ExWidget *(*make_content)(ExPanes *p, const ExTab *tab, void *user);
    /* Called when a tab is gone; the application releases what the content stood for. The widget is
     * freed afterwards by this module. May be NULL. */
    void (*content_closed)(ExPanes *p, ExId tab, ExWidget *content, void *user);
    /* A tab was activated by the user, or its pane got the focus. May be NULL. */
    void (*tab_shown)(ExPanes *p, ExId tab, void *user);
    /* The user asked to close a tab (its close button). Return 0 to refuse. NULL means allowed. */
    int (*close_tab)(ExPanes *p, ExId tab, void *user);
    /* The "+" button of a pane. May be NULL (the button is then not shown). */
    void (*new_tab)(ExPanes *p, ExId group, void *user);
    /* A file was dropped on a pane; return 1 if it was used. */
    int (*drop_file)(ExPanes *p, ExId group, const char *path, void *user);
    /* A tab was dragged out of the window. May be NULL (the tab then stays where it was). */
    void (*popout)(ExPanes *p, ExId tab, void *user);
} ExPanesHooks;

ExPanes *ex_panes_create(ExUi *ui, ExWorkspace *ws, const ExPanesHooks *hooks, void *user);
void ex_panes_free(ExPanes *p);
/* The box that holds the panes of the main window; add it to the window layout. */
ExWidget *ex_panes_root(ExPanes *p);
/* Rebuilds the widgets if the workspace's layout changed and refreshes which pane is active.
 * Call once per frame, before layout. */
void ex_panes_update(ExPanes *p);
/* Swaps the content widget of a tab for another (reading mode instead of the editor, say) without telling the
 * application the tab closed. The old widget is freed. */
void ex_panes_replace_content(ExPanes *p, ExId tab, ExWidget *content);
/* The content widget of a tab if it has been created. */
ExWidget *ex_panes_content(ExPanes *p, ExId tab);
/* A widget shown between the tab strip and the content of the active group (the inline title). The panes do not own it. */
void ex_panes_set_header(ExPanes *p, ExWidget *header);
/* The pane (group) that is under a window position (after the last layout), or 0. */
ExId ex_panes_group_at(ExPanes *p, float x, float y);
/* The text a tab shows in its tab strip. */
void ex_panes_tab_title(const ExTab *tab, char *out, size_t cap);

/* A sidebar: a box with the width and visibility the workspace holds for that side, around
 * `content`, with a grab area on its inner edge for resizing (the new width goes into the
 * workspace as it is dragged; a shown sidebar is never narrower than EX_SIDEBAR_MIN_WIDTH, and never wider than the window leaves room for).
 * Call ex_sidebar_sync before layout whenever the workspace may have changed. */
ExWidget *ex_sidebar_create(ExUi *ui, ExWorkspace *ws, ExSide side, ExWidget *content, const char *name);
/* The strip to put between the sidebar and the text (right of the left sidebar, left of the right one): dragging it resizes the
 * sidebar, as wide as the window allows (the text keeps 240 px; the other sidebar and the ribbon keep theirs). */
ExWidget *ex_sidebar_divider_create(ExWidget *sidebar);
/* The thin handle shown at the window's edge while the sidebar is hidden (right sidebar: add it as the last child of the row; left
 * sidebar: before the sidebar). Dragging a sidebar's divider in until it is under 60 px hides it, keeping its width; dragging this
 * handle out shows it again. */
ExWidget *ex_sidebar_edge_create(ExWidget *sidebar);
void ex_sidebar_sync(ExWidget *sidebar);
/* Widens the sidebar as far as the window allows, so the main text view between the sidebars is reduced to its handle. Returns the width
 * it had (pixels at 100%), to give back with ex_ws_set_sidebar. */
int ex_sidebar_fill(ExWidget *sidebar);

#ifdef __cplusplus
}
#endif
#endif
