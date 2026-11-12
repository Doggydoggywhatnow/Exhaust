#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_a11y.h"

typedef struct Node {
    uint64_t id, parent;
    ExA11yRole role;
    char *name, *value;
    unsigned actions;
    float x, y, w, h;
    uint64_t *kids;
    size_t nkids, capkids;
} Node;

struct ExA11yTree {
    Node *nodes;           /* in creation order */
    size_t n, cap;
    uint64_t root, focus;
};

static const char *const ROLE_NAMES[EX_A11Y_ROLE_COUNT] = {"window", "group", "label", "button", "text-input", "list",
                                                           "list-item", "tree", "tree-item", "tab-list", "tab"};

const char *ex_a11y_role_name(ExA11yRole r) { return (int)r >= 0 && r < EX_A11Y_ROLE_COUNT ? ROLE_NAMES[r] : "?"; }

static char *dup_str(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = malloc(n + 1);
    if (p) { if (n) memcpy(p, s, n); p[n] = 0; }
    return p;
}

ExA11yTree *ex_a11y_create(void) { return calloc(1, sizeof(ExA11yTree)); }

void ex_a11y_free(ExA11yTree *t) {
    size_t i;
    if (!t) return;
    for (i = 0; i < t->n; i++) { free(t->nodes[i].name); free(t->nodes[i].value); free(t->nodes[i].kids); }
    free(t->nodes);
    free(t);
}

static Node *find(const ExA11yTree *t, uint64_t id) {
    size_t i;
    if (id == 0) return NULL;
    for (i = 0; i < t->n; i++) if (t->nodes[i].id == id) return &t->nodes[i];
    return NULL;
}

static int is_ancestor(const ExA11yTree *t, uint64_t anc, uint64_t id) {
    while (id != 0) {
        Node *n = find(t, id);
        if (!n) return 0;
        if (id == anc) return 1;
        id = n->parent;
    }
    return 0;
}

static void unlink_child(ExA11yTree *t, Node *parent, uint64_t id) {
    size_t i;
    (void)t;
    for (i = 0; i < parent->nkids; i++)
        if (parent->kids[i] == id) { memmove(parent->kids + i, parent->kids + i + 1, (parent->nkids - i - 1) * sizeof *parent->kids); parent->nkids--; return; }
}

static int link_child(Node *parent, uint64_t id) {
    if (parent->nkids == parent->capkids) {
        size_t nc = parent->capkids ? parent->capkids * 2 : 4;
        uint64_t *nk = realloc(parent->kids, nc * sizeof *nk);
        if (!nk) return -1;
        parent->kids = nk;
        parent->capkids = nc;
    }
    parent->kids[parent->nkids++] = id;
    return 0;
}

int ex_a11y_set_node(ExA11yTree *t, uint64_t id, uint64_t parent, ExA11yRole role, const char *name) {
    Node *n = find(t, id), *p = NULL;
    char *nm;
    if (id == 0 || (int)role < 0 || role >= EX_A11Y_ROLE_COUNT) return -1;
    if (parent == 0) {
        if (t->root != 0 && t->root != id) return -1; /* one root */
    } else {
        p = find(t, parent);
        if (!p || parent == id || is_ancestor(t, id, parent)) return -1;
    }
    nm = dup_str(name);
    if (!nm) return -1;
    if (!n) {
        if (t->n == t->cap) {
            size_t nc = t->cap ? t->cap * 2 : 16;
            Node *nn = realloc(t->nodes, nc * sizeof *nn);
            if (!nn) { free(nm); return -1; }
            t->nodes = nn;
            t->cap = nc;
            p = parent ? find(t, parent) : NULL; /* the array moved */
        }
        n = &t->nodes[t->n++];
        memset(n, 0, sizeof *n);
        n->id = id;
        n->value = dup_str("");
        if (!n->value) { t->n--; free(nm); return -1; }
        if (p) { if (link_child(p, id)) { free(n->value); t->n--; free(nm); return -1; } n->parent = parent; }
        else n->parent = 0;
    } else if (n->parent != parent) { /* moved */
        if (n->parent) unlink_child(t, find(t, n->parent), id);
        if (p && link_child(p, id)) { free(nm); return -1; }
        n->parent = parent;
    }
    if (parent == 0) t->root = id;
    free(n->name);
    n->name = nm;
    n->role = role;
    return 0;
}

#define NODE_OR_FAIL Node *n = find(t, id); if (!n) return -1

int ex_a11y_set_bounds(ExA11yTree *t, uint64_t id, float x, float y, float w, float h) {
    NODE_OR_FAIL;
    n->x = x; n->y = y; n->w = w; n->h = h;
    return 0;
}

int ex_a11y_set_actions(ExA11yTree *t, uint64_t id, unsigned actions) { NODE_OR_FAIL; n->actions = actions; return 0; }

int ex_a11y_set_value(ExA11yTree *t, uint64_t id, const char *value) {
    char *v;
    NODE_OR_FAIL;
    v = dup_str(value);
    if (!v) return -1;
    free(n->value);
    n->value = v;
    return 0;
}

int ex_a11y_set_focus(ExA11yTree *t, uint64_t id) {
    if (id == 0) { t->focus = 0; return 0; }
    {
        Node *n = find(t, id);
        if (!n || !(n->actions & EX_A11Y_ACTION_FOCUS)) return -1;
    }
    t->focus = id;
    return 0;
}

static void remove_rec(ExA11yTree *t, uint64_t id) {
    Node *n = find(t, id);
    size_t i;
    if (!n) return;
    while (n->nkids > 0) { remove_rec(t, n->kids[n->nkids - 1]); n = find(t, id); } /* each child unlinks itself */
    if (n->parent) { Node *p = find(t, n->parent); if (p) unlink_child(t, p, id); n = find(t, id); }
    if (t->focus == id) t->focus = 0;
    if (t->root == id) t->root = 0;
    free(n->name); free(n->value); free(n->kids);
    i = (size_t)(n - t->nodes);
    memmove(t->nodes + i, t->nodes + i + 1, (t->n - i - 1) * sizeof *n);
    t->n--;
}

int ex_a11y_remove(ExA11yTree *t, uint64_t id) {
    if (!find(t, id)) return -1;
    remove_rec(t, id);
    return 0;
}

size_t ex_a11y_count(const ExA11yTree *t) { return t->n; }
uint64_t ex_a11y_root(const ExA11yTree *t) { return t->root; }
uint64_t ex_a11y_focus(const ExA11yTree *t) { return t->focus; }

int ex_a11y_get(const ExA11yTree *t, uint64_t id, ExA11yInfo *out) {
    Node *n = find(t, id);
    if (!n) return -1;
    out->id = n->id; out->parent = n->parent; out->role = n->role; out->name = n->name; out->value = n->value;
    out->actions = n->actions; out->x = n->x; out->y = n->y; out->w = n->w; out->h = n->h; out->child_count = n->nkids;
    return 0;
}

uint64_t ex_a11y_child(const ExA11yTree *t, uint64_t id, size_t index) {
    Node *n = find(t, id);
    return n && index < n->nkids ? n->kids[index] : 0;
}

static size_t order_rec(const ExA11yTree *t, uint64_t id, uint64_t *out, size_t max, size_t n) {
    Node *nd = find(t, id);
    size_t i;
    if (!nd) return n;
    if ((nd->actions & EX_A11Y_ACTION_FOCUS) && n < max) out[n++] = id;
    for (i = 0; i < nd->nkids; i++) n = order_rec(t, nd->kids[i], out, max, n);
    return n;
}

size_t ex_a11y_focus_order(const ExA11yTree *t, uint64_t *out, size_t max) { return order_rec(t, t->root, out, max, 0); }

/* ---- dump ---- */

typedef struct Buf { char *p; size_t n, cap; } Buf;

#include <stdarg.h>
static void bprintf(Buf *b, const char *fmt, ...) {
    va_list ap;
    int need;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) return;
    if (b->n + (size_t)need + 1 > b->cap) {
        size_t nc = (b->n + (size_t)need + 1) * 2;
        char *np = realloc(b->p, nc);
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    b->n += (size_t)need;
}

/* Quoted, with line breaks, tabs, quotes and backslashes escaped: one node per line. */
static void bquoted(Buf *b, const char *s) {
    bprintf(b, "\"");
    for (; *s; s++) {
        switch (*s) {
        case '\n': bprintf(b, "\\n"); break;
        case '\r': bprintf(b, "\\r"); break;
        case '\t': bprintf(b, "\\t"); break;
        case '"': bprintf(b, "\\\""); break;
        case '\\': bprintf(b, "\\\\"); break;
        default: bprintf(b, "%c", *s); break;
        }
    }
    bprintf(b, "\"");
}

static void dump_rec(const ExA11yTree *t, uint64_t id, int depth, Buf *b) {
    Node *n = find(t, id);
    size_t i;
    if (!n) return;
    bprintf(b, "%*s%s ", depth * 2, "", ex_a11y_role_name(n->role));
    bquoted(b, n->name);
    if (n->actions & EX_A11Y_ACTION_CLICK) bprintf(b, " [click]");
    if (n->actions & EX_A11Y_ACTION_FOCUS) bprintf(b, " [focusable]");
    if (t->focus == id) bprintf(b, " [focused]");
    if (n->value[0]) { bprintf(b, " value="); bquoted(b, n->value); }
    bprintf(b, "\n");
    for (i = 0; i < n->nkids; i++) dump_rec(t, n->kids[i], depth + 1, b);
}

char *ex_a11y_dump(const ExA11yTree *t) {
    Buf b = {0};
    dump_rec(t, t->root, 0, &b);
    if (!b.p) { b.p = malloc(1); if (b.p) b.p[0] = 0; }
    return b.p;
}
