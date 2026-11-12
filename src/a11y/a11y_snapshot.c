/* Immutable copies of the tree for the operating system's accessibility thread,
 * and the conversion to AccessKit updates. */
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_a11y.h"

#ifdef EX_HAVE_A11Y
#include "accesskit.h"
#endif

typedef struct SNode {
    uint64_t id, parent;
    ExA11yRole role;
    char *name, *value;
    unsigned actions;
    float x, y, w, h;
    uint64_t *kids;
    size_t nkids;
} SNode;

struct ExA11ySnapshot {
    SDL_AtomicInt refs;
    SNode *nodes;
    size_t n;
    uint64_t root, focus;
};

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

ExA11ySnapshot *ex_a11y_snapshot(const ExA11yTree *t) {
    ExA11ySnapshot *s = calloc(1, sizeof *s);
    size_t i, n = ex_a11y_count(t), k = 0;
    uint64_t *order;
    if (!s) return NULL;
    SDL_SetAtomicInt(&s->refs, 1);
    s->root = ex_a11y_root(t);
    s->focus = ex_a11y_focus(t);
    s->nodes = calloc(n ? n : 1, sizeof *s->nodes);
    order = malloc((n ? n : 1) * sizeof *order);
    if (!s->nodes || !order) { free(order); ex_a11y_snapshot_unref(s); return NULL; }
    /* tree order: parents before children, children in order */
    {
        size_t head = 0;
        if (s->root) order[k++] = s->root;
        while (head < k) {
            ExA11yInfo info;
            size_t c;
            if (ex_a11y_get(t, order[head], &info) == 0)
                for (c = 0; c < info.child_count && k < n; c++) order[k++] = ex_a11y_child(t, order[head], c);
            head++;
        }
    }
    for (i = 0; i < k; i++) {
        ExA11yInfo info;
        SNode *d = &s->nodes[i];
        size_t c;
        if (ex_a11y_get(t, order[i], &info) != 0) continue;
        d->id = info.id; d->parent = info.parent; d->role = info.role;
        d->name = dup_str(info.name); d->value = dup_str(info.value);
        d->actions = info.actions; d->x = info.x; d->y = info.y; d->w = info.w; d->h = info.h;
        d->nkids = info.child_count;
        d->kids = malloc((d->nkids ? d->nkids : 1) * sizeof *d->kids);
        if (!d->name || !d->value || !d->kids) { free(order); s->n = i + 1; ex_a11y_snapshot_unref(s); return NULL; }
        for (c = 0; c < d->nkids; c++) d->kids[c] = ex_a11y_child(t, info.id, c);
    }
    s->n = k;
    free(order);
    return s;
}

ExA11ySnapshot *ex_a11y_snapshot_ref(ExA11ySnapshot *s) { SDL_AddAtomicInt(&s->refs, 1); return s; }

void ex_a11y_snapshot_unref(ExA11ySnapshot *s) {
    size_t i;
    if (!s || SDL_AddAtomicInt(&s->refs, -1) != 1) return;
    for (i = 0; i < s->n; i++) { free(s->nodes[i].name); free(s->nodes[i].value); free(s->nodes[i].kids); }
    free(s->nodes);
    free(s);
}

size_t ex_a11y_snapshot_count(const ExA11ySnapshot *s) { return s->n; }

#ifdef EX_HAVE_A11Y
static accesskit_role map_role(ExA11yRole r) {
    switch (r) {
    case EX_A11Y_WINDOW: return ACCESSKIT_ROLE_WINDOW;
    case EX_A11Y_LABEL: return ACCESSKIT_ROLE_LABEL;
    case EX_A11Y_BUTTON: return ACCESSKIT_ROLE_BUTTON;
    case EX_A11Y_TEXT_INPUT: return ACCESSKIT_ROLE_MULTILINE_TEXT_INPUT;
    case EX_A11Y_LIST: return ACCESSKIT_ROLE_LIST;
    case EX_A11Y_LIST_ITEM: return ACCESSKIT_ROLE_LIST_ITEM;
    case EX_A11Y_TREE: return ACCESSKIT_ROLE_TREE;
    case EX_A11Y_TREE_ITEM: return ACCESSKIT_ROLE_TREE_ITEM;
    case EX_A11Y_TAB_LIST: return ACCESSKIT_ROLE_TAB_LIST;
    case EX_A11Y_TAB: return ACCESSKIT_ROLE_TAB;
    default: return ACCESSKIT_ROLE_GROUP;
    }
}

struct accesskit_tree_update *ex_a11y_build_update(const ExA11ySnapshot *s) {
    struct accesskit_tree_update *u;
    struct accesskit_tree_info *info;
    size_t i;
    if (!s || s->root == 0) return NULL;
    u = accesskit_tree_update_with_capacity_and_focus(s->n, s->focus ? s->focus : s->root);
    if (!u) return NULL;
    for (i = 0; i < s->n; i++) {
        const SNode *d = &s->nodes[i];
        struct accesskit_node *n = accesskit_node_new(map_role(d->role));
        if (!n) continue;
        if (d->name[0]) accesskit_node_set_label(n, d->name);
        if (d->value[0]) accesskit_node_set_value(n, d->value);
        if (d->w > 0 && d->h > 0) accesskit_node_set_bounds(n, accesskit_rect_new(d->x, d->y, d->x + d->w, d->y + d->h));
        if (d->actions & EX_A11Y_ACTION_CLICK) accesskit_node_add_action(n, ACCESSKIT_ACTION_CLICK);
        if (d->actions & EX_A11Y_ACTION_FOCUS) accesskit_node_add_action(n, ACCESSKIT_ACTION_FOCUS);
        if (d->nkids) accesskit_node_set_children(n, d->nkids, (const accesskit_node_id *)d->kids);
        accesskit_tree_update_push_node(u, d->id, n);
    }
    info = accesskit_tree_info_new(s->root);
    if (info) {
        accesskit_tree_info_set_toolkit_name(info, "Exhaust");
        accesskit_tree_update_set_tree_info(u, info);
    }
    return u;
}
#endif
