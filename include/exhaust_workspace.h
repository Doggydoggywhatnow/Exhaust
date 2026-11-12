/* The workspace: what is open and where. Windows hold a tree of panes: a *group* is a strip
 * of tabs with one active tab, a *split* places groups (and further splits) side by side or
 * one above the other. There is one main window and any number of pop-out windows. The two
 * sidebars have a size, a visibility and a list of open panels. The whole state serializes
 * to the session file (docs/formats.md) and is saved as it changes. This module is the
 * model only; widgets are built from it in exhaust_ui.h and never hold layout state of their
 * own. All ids are positive; 0 means "none". */
#ifndef EXHAUST_WORKSPACE_H
#define EXHAUST_WORKSPACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t ExId;
typedef struct ExWorkspace ExWorkspace;

typedef enum ExSplitDir { EX_SPLIT_HORIZONTAL, EX_SPLIT_VERTICAL } ExSplitDir; /* horizontal: side by side */
typedef enum ExNodeKind { EX_NODE_NONE = 0, EX_NODE_GROUP, EX_NODE_SPLIT } ExNodeKind;
typedef enum ExSide { EX_SIDE_LEFT, EX_SIDE_RIGHT } ExSide;

typedef struct ExTab {
    ExId id;
    ExId group;
    char *path;                /* vault-relative path; "" for a tab with no file (graph, empty) */
    char *view;                /* "note", "graph", "canvas", "base", "pdf", "image", "empty" */
    char *mode;                /* note editing mode: "live", "source", "reading" */
    int pinned, stacked;
    int cursor_line, cursor_column;
    float scroll_y;
} ExTab;

ExWorkspace *ex_ws_create(void);   /* a main window with one empty group, both sidebars visible */
void ex_ws_free(ExWorkspace *ws);
unsigned long ex_ws_revision(const ExWorkspace *ws); /* grows with every change; the session saver watches it */
/* Grows only when the tree of panes or the tabs in it change (not for cursor and scroll positions,
 * sidebar sizes or split ratios); the widgets are rebuilt when it does. */
unsigned long ex_ws_layout_revision(const ExWorkspace *ws);

/* ---- windows ---- */
size_t ex_ws_window_count(const ExWorkspace *ws);
ExId ex_ws_window_at(const ExWorkspace *ws, size_t i);
ExId ex_ws_main_window(const ExWorkspace *ws);
ExId ex_ws_window_root(const ExWorkspace *ws, ExId window);
ExId ex_ws_window_of_node(const ExWorkspace *ws, ExId node);
void ex_ws_window_get_rect(const ExWorkspace *ws, ExId window, int *x, int *y, int *w, int *h, int *maximized);
void ex_ws_window_set_rect(ExWorkspace *ws, ExId window, int x, int y, int w, int h, int maximized);
/* Moves a tab into a new pop-out window (in a group of its own). Returns the window, or 0. */
ExId ex_ws_popout(ExWorkspace *ws, ExId tab);
/* Closes a pop-out window; its tabs move to the active group of the main window. The main
 * window cannot be closed. Returns 0 on success. */
int ex_ws_close_window(ExWorkspace *ws, ExId window);

/* ---- nodes ---- */
ExNodeKind ex_ws_node_kind(const ExWorkspace *ws, ExId node);
ExId ex_ws_node_parent(const ExWorkspace *ws, ExId node);          /* 0 for a window's root */
ExSplitDir ex_ws_split_dir(const ExWorkspace *ws, ExId split);
size_t ex_ws_split_count(const ExWorkspace *ws, ExId split);
ExId ex_ws_split_child(const ExWorkspace *ws, ExId split, size_t i);
float ex_ws_split_ratio(const ExWorkspace *ws, ExId split, size_t i); /* shares that add up to 1 */
int ex_ws_set_ratio(ExWorkspace *ws, ExId split, size_t i, float ratio); /* gives child i this share, taking from i+1 */

/* ---- groups and tabs ---- */
size_t ex_ws_group_count(const ExWorkspace *ws);
size_t ex_ws_group_tab_count(const ExWorkspace *ws, ExId group);
ExId ex_ws_group_tab(const ExWorkspace *ws, ExId group, size_t i);
ExId ex_ws_group_active(const ExWorkspace *ws, ExId group);
ExId ex_ws_active_group(const ExWorkspace *ws);                    /* the group that has the focus */
int ex_ws_set_active_group(ExWorkspace *ws, ExId group);
const ExTab *ex_ws_tab(const ExWorkspace *ws, ExId tab);
size_t ex_ws_tab_count(const ExWorkspace *ws);
ExId ex_ws_tab_at(const ExWorkspace *ws, size_t i);

/* Opens a tab at the end of a group (after the pinned tabs) and makes it active; group 0 is the
 * active group of the main window. Returns the tab id, or 0. */
ExId ex_ws_new_tab(ExWorkspace *ws, ExId group, const char *path, const char *view);
/* Closes a tab. Its group goes away when it was the last tab and there is another group in the
 * window (a split with one child is replaced by the child); a pop-out window with no tab left closes. */
int ex_ws_close_tab(ExWorkspace *ws, ExId tab);
int ex_ws_activate(ExWorkspace *ws, ExId tab);
/* Moves a tab to a position in a group, which may be in another window. Pinned tabs stay in front. */
int ex_ws_move_tab(ExWorkspace *ws, ExId tab, ExId to_group, size_t index);
/* Splits a group: a new group is placed after (right or below) or before it. With move_tab != 0 that
 * tab moves into the new group (not allowed for the only tab of `group`); otherwise the new group is
 * empty. Returns the new group, or 0. */
ExId ex_ws_split(ExWorkspace *ws, ExId group, ExSplitDir dir, int after, ExId move_tab);
/* Closes every tab of a group and the group (the last group of the main window stays, empty). */
int ex_ws_close_group(ExWorkspace *ws, ExId group);
int ex_ws_set_pinned(ExWorkspace *ws, ExId tab, int pinned);
int ex_ws_set_stacked(ExWorkspace *ws, ExId tab, int stacked);
int ex_ws_set_tab_path(ExWorkspace *ws, ExId tab, const char *path, const char *view);  /* the tab shows another file */
int ex_ws_set_tab_state(ExWorkspace *ws, ExId tab, int cursor_line, int cursor_column, float scroll_y);
int ex_ws_set_tab_mode(ExWorkspace *ws, ExId tab, const char *mode);
/* Linked panes follow each other. */
int ex_ws_link_groups(ExWorkspace *ws, ExId a, ExId b);
int ex_ws_unlink_group(ExWorkspace *ws, ExId group);
size_t ex_ws_linked_count(const ExWorkspace *ws, ExId group);
ExId ex_ws_linked_group(const ExWorkspace *ws, ExId group, size_t i);
/* Finds an open tab for a file in the main window (or any window), 0 if none. */
ExId ex_ws_find_tab(const ExWorkspace *ws, const char *path);

/* ---- sidebars ---- */
int ex_ws_sidebar_visible(const ExWorkspace *ws, ExSide side);
int ex_ws_sidebar_width(const ExWorkspace *ws, ExSide side);   /* pixels at 100% zoom; defaults 260 and 300 */
void ex_ws_set_sidebar(ExWorkspace *ws, ExSide side, int visible, int width);
size_t ex_ws_panel_count(const ExWorkspace *ws, ExSide side);
const char *ex_ws_panel(const ExWorkspace *ws, ExSide side, size_t i);
int ex_ws_open_panel(ExWorkspace *ws, ExSide side, const char *name);   /* adds it if not there */
int ex_ws_close_panel(ExWorkspace *ws, ExSide side, const char *name);

/* ---- the session file ---- */
char *ex_ws_serialize(const ExWorkspace *ws, size_t *len);          /* session.ini text, malloc'ed */
/* Replaces the state with the text's. Keeps unknown sections and keys for writing back. Bad or
 * missing parts fall back to defaults (an old file without a layout tree gives one group). */
int ex_ws_deserialize(ExWorkspace *ws, const char *text, size_t len);

#ifdef __cplusplus
}
#endif
#endif
