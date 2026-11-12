#include <stdlib.h>
#include <string.h>

#include "exhaust_ui.h"
#include "reading_view.h"

static ExA11yRole role_of(const ExWidget *w) {
    switch (w->role) {
    case EX_ROLE_WINDOW: return EX_A11Y_WINDOW;
    case EX_ROLE_LABEL: return EX_A11Y_LABEL;
    case EX_ROLE_BUTTON: return EX_A11Y_BUTTON;
    case EX_ROLE_TEXT_INPUT: return EX_A11Y_TEXT_INPUT;
    case EX_ROLE_TREE: return EX_A11Y_TREE;
    default: return EX_A11Y_GROUP;
    }
}

static int sync_widget(const ExUi *ui, const ExWidget *w, uint64_t parent, ExA11yTree *t) {
    const ExWidget *c;
    if (w->hidden) return 0;                 /* hidden widgets are not in the accessibility tree (their old node is removed) */
    unsigned actions = 0;
    const char *name = w->name ? w->name : (w->text ? w->text : "");
    if (ex_a11y_set_node(t, w->id, parent, role_of(w), name)) return -1;
    if (w->type == EX_WIDGET_BUTTON && w->on_click) actions |= EX_A11Y_ACTION_CLICK;
    if (w->focusable) actions |= EX_A11Y_ACTION_FOCUS;
    ex_a11y_set_actions(t, w->id, actions);
    ex_a11y_set_bounds(t, w->id, w->x, w->y, w->w, w->h);
    if (w->type == EX_WIDGET_EDITOR && w->editor) {
        char *text = ex_buffer_text(ex_doc_buffer(ex_editor_doc(w->editor)), NULL);
        ex_a11y_set_value(t, w->id, text ? text : "");
        free(text);
    } else if (w->type == EX_WIDGET_READING && w->reading) {
        char *text = ex_readingview_plain_text(w->reading);         /* a screen reader reads the note as plain text */
        ex_a11y_set_value(t, w->id, text ? text : "");
        free(text);
    } else {
        ex_a11y_set_value(t, w->id, "");
    }
    if (w->type == EX_WIDGET_TREE && w->vault) { /* the visible rows are tree items below the tree */
        size_t i, rows = ex_vault_row_count(w->vault);
        for (i = 0; i < rows && i < 65535; i++) {
            const ExNode *n = ex_vault_row(w->vault, i, NULL);
            uint64_t id = 0x100000000ull + (uint64_t)w->id * 65536u + i;
            if (n && ex_a11y_set_node(t, id, w->id, EX_A11Y_TREE_ITEM, n->name) == 0)
                ex_a11y_set_actions(t, id, EX_A11Y_ACTION_CLICK);
        }
    }
    (void)ui;
    for (c = w->first_child; c; c = c->next) if (sync_widget(ui, c, w->id, t)) return -1;
    return 0;
}

/* Collects the ids of the widgets that exist, to remove the nodes that do not. */
static size_t collect(const ExWidget *w, uint64_t *ids, size_t n, size_t cap) {
    for (; w; w = w->next) {
        if (w->hidden) continue;
        if (n < cap) ids[n] = w->id;
        n++;
        n = collect(w->first_child, ids, n, cap);
    }
    return n;
}

static int exists(const uint64_t *ids, size_t n, uint64_t id) {
    size_t i;
    for (i = 0; i < n; i++) if (ids[i] == id) return 1;
    return 0;
}

int ex_ui_a11y_sync(ExUi *ui, ExWidget *root, ExA11yTree *t) {
    size_t n = ex_widget_count(root), have = ex_a11y_count(t), i, got;
    uint64_t *ids = malloc((n ? n : 1) * sizeof *ids), *stale;
    ExA11yInfo info;
    if (!ids) return -1;
    got = collect(root, ids, 0, n);   /* hidden widgets are not collected, so got can be less than n */
    /* remove nodes whose widgets are gone: walk the tree from the root and collect the stale ids first */
    stale = malloc((have ? have : 1) * sizeof *stale);
    if (stale) {
        size_t ns = 0, head = 0, queue_cap = have ? have : 1;
        uint64_t *queue = malloc(queue_cap * sizeof *queue);
        if (queue && ex_a11y_root(t)) {
            size_t qn = 0;
            queue[qn++] = ex_a11y_root(t);
            while (head < qn) {
                uint64_t id = queue[head++];
                size_t c;
                if (id < 0x100000000ull && !exists(ids, got, id)) { stale[ns++] = id; continue; } /* its subtree goes with it */
                if (id >= 0x100000000ull) { stale[ns++] = id; continue; }                       /* tree rows are rebuilt below */
                if (ex_a11y_get(t, id, &info) == 0)
                    for (c = 0; c < info.child_count && qn < queue_cap; c++) queue[qn++] = ex_a11y_child(t, id, c);
            }
        }
        free(queue);
        for (i = 0; i < ns; i++) ex_a11y_remove(t, stale[i]);
        free(stale);
    }
    /* a root that changed identity replaces the old tree */
    if (ex_a11y_root(t) && ex_a11y_root(t) != root->id) ex_a11y_remove(t, ex_a11y_root(t));
    free(ids);
    if (sync_widget(ui, root, 0, t)) return -1;
    ex_a11y_set_focus(t, ex_ui_focus(ui) ? ex_ui_focus(ui)->id : 0);
    return 0;
}
