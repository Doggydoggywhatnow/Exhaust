/* The accessibility tree. Exhaust keeps its own plain-C copy of the tree (so it can
 * be read back, dumped and tested on every platform) and turns it into AccessKit
 * updates for the operating system's screen reader interface (UI Automation on
 * Windows, NSAccessibility on macOS, AT-SPI on Linux).
 *
 * There is one node for each widget (see ex_ui_a11y_sync in exhaust_ui.h). A node has
 * a role, a name, bounds in window pixels, a set of actions it supports, an optional
 * text value and a parent; at most one node has the keyboard focus. Node ids are the
 * widget ids and are never 0. The tree is used on the UI thread only; the operating
 * system's thread gets immutable snapshots (ExA11ySnapshot). */
#ifndef EXHAUST_A11Y_H
#define EXHAUST_A11Y_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExA11yRole {
    EX_A11Y_WINDOW,
    EX_A11Y_GROUP,
    EX_A11Y_LABEL,
    EX_A11Y_BUTTON,
    EX_A11Y_TEXT_INPUT,       /* multi-line text editor */
    EX_A11Y_LIST,
    EX_A11Y_LIST_ITEM,
    EX_A11Y_TREE,
    EX_A11Y_TREE_ITEM,
    EX_A11Y_TAB_LIST,
    EX_A11Y_TAB,
    EX_A11Y_ROLE_COUNT
} ExA11yRole;

enum {
    EX_A11Y_ACTION_CLICK = 1,   /* "press" a button, open an item */
    EX_A11Y_ACTION_FOCUS = 2    /* move the keyboard focus here */
};

typedef struct ExA11yTree ExA11yTree;

typedef struct ExA11yInfo {
    uint64_t id, parent;        /* parent is 0 for the root */
    ExA11yRole role;
    const char *name;           /* valid until the node changes or the tree is freed */
    const char *value;          /* text of an editor, or "" */
    unsigned actions;           /* EX_A11Y_ACTION_* bits */
    float x, y, w, h;
    size_t child_count;
} ExA11yInfo;

ExA11yTree *ex_a11y_create(void);
void ex_a11y_free(ExA11yTree *t);

/* Adds a node, or changes the role, name and parent of an existing one. parent 0
 * makes it the root (there is one root). A parent must already exist, and a node
 * cannot become its own ancestor. Children keep the order in which they were added.
 * Returns 0, or -1 for a bad id or parent, or when out of memory. */
int ex_a11y_set_node(ExA11yTree *t, uint64_t id, uint64_t parent, ExA11yRole role, const char *name);
int ex_a11y_set_bounds(ExA11yTree *t, uint64_t id, float x, float y, float w, float h);
int ex_a11y_set_actions(ExA11yTree *t, uint64_t id, unsigned actions);
int ex_a11y_set_value(ExA11yTree *t, uint64_t id, const char *value);
/* 0 clears the focus. Fails if the node does not exist or cannot take focus. */
int ex_a11y_set_focus(ExA11yTree *t, uint64_t id);
/* Removes a node and all its descendants (the focus goes with them). */
int ex_a11y_remove(ExA11yTree *t, uint64_t id);

size_t ex_a11y_count(const ExA11yTree *t);
uint64_t ex_a11y_root(const ExA11yTree *t);
uint64_t ex_a11y_focus(const ExA11yTree *t);
/* Fills *out; returns 0, or -1 if there is no such node. */
int ex_a11y_get(const ExA11yTree *t, uint64_t id, ExA11yInfo *out);
uint64_t ex_a11y_child(const ExA11yTree *t, uint64_t id, size_t index); /* 0 if out of range */
/* Nodes that can take focus, in tree order (the Tab order). Returns the count. */
size_t ex_a11y_focus_order(const ExA11yTree *t, uint64_t *out, size_t max);
const char *ex_a11y_role_name(ExA11yRole role);

/* A text dump, one line per node: indentation, role, name in quotes, focus and
 * actions, in tree order. malloc'ed. This is what the a11y_dump test compares with
 * a golden file (sub-slice 1b-iii). */
char *ex_a11y_dump(const ExA11yTree *t);

/* ---- snapshots for the operating system's thread ---- */

typedef struct ExA11ySnapshot ExA11ySnapshot;
ExA11ySnapshot *ex_a11y_snapshot(const ExA11yTree *t); /* an immutable copy, reference counted */
ExA11ySnapshot *ex_a11y_snapshot_ref(ExA11ySnapshot *s);
void ex_a11y_snapshot_unref(ExA11ySnapshot *s);
size_t ex_a11y_snapshot_count(const ExA11ySnapshot *s);

#ifdef EX_HAVE_A11Y
struct accesskit_tree_update;
/* An AccessKit tree update with every node of the snapshot, for the first
 * activation of a screen reader and after any change (nodes are sent in full; the
 * trees here are small). The caller (or AccessKit) owns the result. */
struct accesskit_tree_update *ex_a11y_build_update(const ExA11ySnapshot *s);
#endif

#ifdef __cplusplus
}
#endif
#endif
