#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_workspace.h"

/* A comma separated list: returns each item in turn, trimmed (strtok_r is not available everywhere). */
static char *strtok_r_compat(char *s, char **save) {
    char *start, *end;
    if (s) *save = s;
    if (!*save || !**save) return NULL;
    start = *save;
    end = strchr(start, ',');
    if (end) { *end = 0; *save = end + 1; } else *save = start + strlen(start);
    while (*start == ' ') start++;
    {
        char *e = start + strlen(start);
        while (e > start && e[-1] == ' ') *--e = 0;
    }
    return start;
}

typedef struct Node {
    ExId id;
    ExNodeKind kind;
    ExId parent;
    ExSplitDir dir;
    ExId *kids; float *ratios; size_t nkids, capkids;      /* split */
    ExId *tabs; size_t ntabs, captabs; ExId active;          /* group */
    ExId *linked; size_t nlinked, caplinked;
    ExPlace *hist; size_t nhist, caphist; int hpos;           /* group: the places it showed (5d) */
} Node;

typedef struct Window { ExId id; int main; ExId root; int x, y, w, h, maximized; } Window;

typedef struct Sidebar { int visible, width; char **panels; size_t n, cap; } Sidebar;

struct ExWorkspace {
    Node *nodes; size_t n, cap;
    Window *wins; size_t nwin, capwin;
    ExTab *tabs; size_t ntabs, captabs;
    ExId next_node, next_tab, next_win, active_group;
    ExId side_group[2];     /* the group of tabs of a sidebar (5e), 0 for none; it belongs to no window */
    Sidebar side[2];
    char *search;
    ExIni *extra;           /* sections this version does not know, kept for writing back */
    unsigned long revision, layout_revision, epoch;
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void touch(ExWorkspace *ws) { ws->revision++; }
const char *ex_ws_search_text(const ExWorkspace *ws) { return ws->search ? ws->search : ""; }
void ex_ws_set_search_text(ExWorkspace *ws, const char *text) {
    size_t n = strlen(text);
    char *c;
    if (ws->search && !strcmp(ws->search, text)) return;
    c = malloc(n + 1);
    if (!c) return;
    memcpy(c, text, n + 1);
    free(ws->search);
    ws->search = c;
    ws->revision++;
}

static void touch_layout(ExWorkspace *ws) { ws->revision++; ws->layout_revision++; }

/* ---- the history of a pane (5d) ---- */

static Node *group_node(const ExWorkspace *ws, ExId g);
static void free_place(ExPlace *p);

void ex_ws_history_visit(ExWorkspace *ws, ExId group, const char *path, const char *view) {
    Node *nd = group_node(ws, group);
    ExPlace *cur = NULL;
    size_t i;
    if (!nd || !path || !view) return;
    if (nd->nhist && nd->hpos >= 0 && (size_t)nd->hpos < nd->nhist) cur = &nd->hist[nd->hpos];
    if (cur && !strcmp(cur->path, path) && !strcmp(cur->view, view)) return;
    if (cur) {                                                      /* leaving it: its cursor and scroll are what the tab holds now, if the tab is still there */
        for (i = 0; i < ws->ntabs; i++) {
            const ExTab *t = &ws->tabs[i];
            if (t->group == group && !strcmp(t->path, cur->path) && !strcmp(t->view, cur->view)) { cur->line = t->cursor_line; cur->column = t->cursor_column; cur->scroll = t->scroll_y; break; }
        }
    }
    for (i = nd->nhist; cur && i > (size_t)nd->hpos + 1; i--) free_place(&nd->hist[i - 1]);   /* what was ahead is gone */
    if (cur) nd->nhist = (size_t)nd->hpos + 1;
    if (nd->nhist == nd->caphist) {
        size_t nc = nd->caphist ? nd->caphist * 2 : 8;
        ExPlace *np = realloc(nd->hist, nc * sizeof *np);
        if (!np) return;
        nd->hist = np; nd->caphist = nc;
    }
    nd->hist[nd->nhist].path = dup_str(path); nd->hist[nd->nhist].view = dup_str(view);
    nd->hist[nd->nhist].line = nd->hist[nd->nhist].column = 0; nd->hist[nd->nhist].scroll = 0.0f;
    if (!nd->hist[nd->nhist].path || !nd->hist[nd->nhist].view) { free_place(&nd->hist[nd->nhist]); return; }
    nd->nhist++;
    if (nd->nhist > EX_HISTORY_MAX) {                               /* the oldest goes */
        free_place(&nd->hist[0]);
        memmove(nd->hist, nd->hist + 1, (nd->nhist - 1) * sizeof *nd->hist);
        nd->nhist--;
    }
    nd->hpos = (int)nd->nhist - 1;
    touch(ws);
}

size_t ex_ws_history_count(const ExWorkspace *ws, ExId group) { Node *nd = group_node(ws, group); return nd ? nd->nhist : 0; }
int ex_ws_history_pos(const ExWorkspace *ws, ExId group) { Node *nd = group_node(ws, group); return nd && nd->nhist ? nd->hpos : -1; }
const ExPlace *ex_ws_history_place(const ExWorkspace *ws, ExId group, size_t i) { Node *nd = group_node(ws, group); return nd && i < nd->nhist ? &nd->hist[i] : NULL; }
int ex_ws_history_can(const ExWorkspace *ws, ExId group, int dir) {
    Node *nd = group_node(ws, group);
    if (!nd || !nd->nhist) return 0;
    return dir < 0 ? nd->hpos > 0 : (size_t)nd->hpos + 1 < nd->nhist;
}
const ExPlace *ex_ws_history_go(ExWorkspace *ws, ExId group, int dir) {
    Node *nd = group_node(ws, group);
    if (!nd || !ex_ws_history_can(ws, group, dir)) return NULL;
    nd->hpos += dir < 0 ? -1 : 1;
    touch(ws);
    return &nd->hist[nd->hpos];
}
const ExPlace *ex_ws_history_go_valid(ExWorkspace *ws, ExId group, int dir, int (*exists)(const ExPlace *, void *), void *user) {
    Node *nd = group_node(ws, group);
    if (!nd) return NULL;
    for (;;) {
        int j = nd->hpos + (dir < 0 ? -1 : 1);
        if (!nd->nhist || j < 0 || j >= (int)nd->nhist) return NULL;
        if (!exists || exists(&nd->hist[j], user)) { nd->hpos = j; touch(ws); return &nd->hist[j]; }
        free_place(&nd->hist[j]);                                   /* gone: dropped, and the search goes on */
        memmove(nd->hist + j, nd->hist + j + 1, (nd->nhist - (size_t)j - 1) * sizeof *nd->hist);
        nd->nhist--;
        if (dir < 0) nd->hpos--;
        touch(ws);
    }
}

void ex_ws_history_remove(ExWorkspace *ws, ExId group, size_t i) {
    Node *nd = group_node(ws, group);
    if (!nd || i >= nd->nhist) return;
    free_place(&nd->hist[i]);
    memmove(nd->hist + i, nd->hist + i + 1, (nd->nhist - i - 1) * sizeof *nd->hist);
    nd->nhist--;
    if ((size_t)nd->hpos > i || (size_t)nd->hpos >= nd->nhist) nd->hpos--;
    if (nd->hpos < 0) nd->hpos = nd->nhist ? 0 : -1;
    touch(ws);
}

/* ---- lookup ---- */

static Node *node(const ExWorkspace *ws, ExId id) {
    size_t i;
    for (i = 0; i < ws->n; i++) if (ws->nodes[i].id == id) return &ws->nodes[i];
    return NULL;
}
static Node *group_node(const ExWorkspace *ws, ExId g) { Node *nd = node(ws, g); return nd && nd->kind == EX_NODE_GROUP ? nd : NULL; }
static Window *win(const ExWorkspace *ws, ExId id) {
    size_t i;
    for (i = 0; i < ws->nwin; i++) if (ws->wins[i].id == id) return &ws->wins[i];
    return NULL;
}
static ExTab *tab(const ExWorkspace *ws, ExId id) {
    size_t i;
    for (i = 0; i < ws->ntabs; i++) if (ws->tabs[i].id == id) return &ws->tabs[i];
    return NULL;
}

static Node *new_node(ExWorkspace *ws, ExNodeKind kind) {
    Node *nd;
    if (ws->n == ws->cap) {
        size_t nc = ws->cap ? ws->cap * 2 : 16;
        Node *nn = realloc(ws->nodes, nc * sizeof *nn);
        if (!nn) return NULL;
        ws->nodes = nn;
        ws->cap = nc;
    }
    nd = &ws->nodes[ws->n++];
    memset(nd, 0, sizeof *nd);
    nd->id = ws->next_node++;
    nd->kind = kind;
    return nd;
}

static void free_place(ExPlace *p) { free(p->path); free(p->view); p->path = p->view = NULL; }
static void free_node_data(Node *nd) {
    size_t i;
    free(nd->kids); free(nd->ratios); free(nd->tabs); free(nd->linked);
    for (i = 0; i < nd->nhist; i++) free_place(&nd->hist[i]);
    free(nd->hist);
}

static void delete_node(ExWorkspace *ws, ExId id) {
    Node *nd = node(ws, id);
    size_t i;
    if (!nd) return;
    free_node_data(nd);
    i = (size_t)(nd - ws->nodes);
    memmove(ws->nodes + i, ws->nodes + i + 1, (ws->n - i - 1) * sizeof *nd);
    ws->n--;
}

static void push_id(ExId **arr, size_t *n, size_t *cap, ExId v) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        ExId *na = realloc(*arr, nc * sizeof **arr);
        if (!na) return;
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*n)++] = v;
}

static void remove_at(ExId *arr, size_t *n, size_t i) { memmove(arr + i, arr + i + 1, (*n - i - 1) * sizeof *arr); (*n)--; }

ExId ex_ws_window_of_node(const ExWorkspace *ws, ExId id) {
    Node *nd = node(ws, id);
    size_t i;
    while (nd && nd->parent) nd = node(ws, nd->parent);
    if (!nd) return 0;
    for (i = 0; i < ws->nwin; i++) if (ws->wins[i].root == nd->id) return ws->wins[i].id;
    return 0;
}

/* ---- creating ---- */

static Window *add_window(ExWorkspace *ws, int is_main) {
    Window *w;
    Node *g;
    if (ws->nwin == ws->capwin) {
        size_t nc = ws->capwin ? ws->capwin * 2 : 4;
        Window *nw = realloc(ws->wins, nc * sizeof *nw);
        if (!nw) return NULL;
        ws->wins = nw;
        ws->capwin = nc;
    }
    g = new_node(ws, EX_NODE_GROUP);
    if (!g) return NULL;
    w = &ws->wins[ws->nwin++];
    memset(w, 0, sizeof *w);
    w->id = ws->next_win++;
    w->main = is_main;
    w->root = g->id;
    w->w = is_main ? 1100 : 800;
    w->h = is_main ? 750 : 600;
    return w;
}

ExWorkspace *ex_ws_create(void) {
    ExWorkspace *ws = calloc(1, sizeof *ws);
    Window *w;
    if (!ws) return NULL;
    ws->next_node = ws->next_tab = ws->next_win = 1;
    ws->side[EX_SIDE_LEFT].visible = ws->side[EX_SIDE_RIGHT].visible = 1;
    ws->side[EX_SIDE_LEFT].width = 260;
    ws->side[EX_SIDE_RIGHT].width = 300;
    w = add_window(ws, 1);
    if (!w) { free(ws); return NULL; }
    ws->active_group = w->root;
    ws->extra = ex_ini_new();
    return ws;
}

static void clear_all(ExWorkspace *ws) {
    size_t i, k;
    ws->epoch++;
    for (i = 0; i < ws->n; i++) free_node_data(&ws->nodes[i]);
    ws->n = 0;
    ws->nwin = 0;
    for (i = 0; i < ws->ntabs; i++) { free(ws->tabs[i].path); free(ws->tabs[i].view); free(ws->tabs[i].mode); }
    ws->ntabs = 0;
    for (i = 0; i < 2; i++) { for (k = 0; k < ws->side[i].n; k++) free(ws->side[i].panels[k]); ws->side[i].n = 0; }
    free(ws->search); ws->search = NULL;
    ws->side_group[0] = ws->side_group[1] = 0;
}

void ex_ws_free(ExWorkspace *ws) {
    size_t i;
    if (!ws) return;
    clear_all(ws);
    for (i = 0; i < 2; i++) free(ws->side[i].panels);
    free(ws->nodes); free(ws->wins); free(ws->tabs);
    ex_ini_free(ws->extra);
    free(ws);
}

unsigned long ex_ws_revision(const ExWorkspace *ws) { return ws->revision; }
unsigned long ex_ws_layout_revision(const ExWorkspace *ws) { return ws->layout_revision; }
unsigned long ex_ws_epoch(const ExWorkspace *ws) { return ws->epoch; }

/* ---- windows and nodes: queries ---- */

size_t ex_ws_window_count(const ExWorkspace *ws) { return ws->nwin; }
ExId ex_ws_window_at(const ExWorkspace *ws, size_t i) { return i < ws->nwin ? ws->wins[i].id : 0; }
ExId ex_ws_main_window(const ExWorkspace *ws) { size_t i; for (i = 0; i < ws->nwin; i++) if (ws->wins[i].main) return ws->wins[i].id; return 0; }
ExId ex_ws_window_root(const ExWorkspace *ws, ExId w) { Window *x = win(ws, w); return x ? x->root : 0; }

void ex_ws_window_get_rect(const ExWorkspace *ws, ExId w, int *x, int *y, int *wd, int *h, int *max) {
    Window *wn = win(ws, w);
    if (!wn) return;
    if (x) *x = wn->x;
    if (y) *y = wn->y;
    if (wd) *wd = wn->w;
    if (h) *h = wn->h;
    if (max) *max = wn->maximized;
}

void ex_ws_window_set_rect(ExWorkspace *ws, ExId w, int x, int y, int wd, int h, int max) {
    Window *wn = win(ws, w);
    if (!wn) return;
    wn->x = x; wn->y = y; wn->w = wd; wn->h = h; wn->maximized = max;
    touch(ws);
}

ExNodeKind ex_ws_node_kind(const ExWorkspace *ws, ExId id) { Node *nd = node(ws, id); return nd ? nd->kind : EX_NODE_NONE; }
ExId ex_ws_node_parent(const ExWorkspace *ws, ExId id) { Node *nd = node(ws, id); return nd ? nd->parent : 0; }
ExSplitDir ex_ws_split_dir(const ExWorkspace *ws, ExId id) { Node *nd = node(ws, id); return nd ? nd->dir : EX_SPLIT_HORIZONTAL; }
size_t ex_ws_split_count(const ExWorkspace *ws, ExId id) { Node *nd = node(ws, id); return nd && nd->kind == EX_NODE_SPLIT ? nd->nkids : 0; }
ExId ex_ws_split_child(const ExWorkspace *ws, ExId id, size_t i) { Node *nd = node(ws, id); return nd && i < nd->nkids ? nd->kids[i] : 0; }
float ex_ws_split_ratio(const ExWorkspace *ws, ExId id, size_t i) { Node *nd = node(ws, id); return nd && i < nd->nkids ? nd->ratios[i] : 0; }

int ex_ws_set_ratio(ExWorkspace *ws, ExId id, size_t i, float ratio) {
    Node *nd = node(ws, id);
    float total;
    if (!nd || nd->kind != EX_NODE_SPLIT || i + 1 >= nd->nkids) return -1;
    total = nd->ratios[i] + nd->ratios[i + 1];
    if (ratio < 0.05f) ratio = 0.05f;
    if (ratio > total - 0.05f) ratio = total - 0.05f;
    nd->ratios[i + 1] = total - ratio;
    nd->ratios[i] = ratio;
    touch(ws);
    return 0;
}

size_t ex_ws_group_count(const ExWorkspace *ws) { size_t i, c = 0; for (i = 0; i < ws->n; i++) if (ws->nodes[i].kind == EX_NODE_GROUP) c++; return c; }
size_t ex_ws_group_tab_count(const ExWorkspace *ws, ExId g) { Node *nd = node(ws, g); return nd && nd->kind == EX_NODE_GROUP ? nd->ntabs : 0; }
ExId ex_ws_group_tab(const ExWorkspace *ws, ExId g, size_t i) { Node *nd = node(ws, g); return nd && i < nd->ntabs ? nd->tabs[i] : 0; }
ExId ex_ws_group_active(const ExWorkspace *ws, ExId g) { Node *nd = node(ws, g); return nd ? nd->active : 0; }
ExId ex_ws_active_group(const ExWorkspace *ws) { return ws->active_group; }
const ExTab *ex_ws_tab(const ExWorkspace *ws, ExId id) { return tab(ws, id); }
size_t ex_ws_tab_count(const ExWorkspace *ws) { return ws->ntabs; }
ExId ex_ws_tab_at(const ExWorkspace *ws, size_t i) { return i < ws->ntabs ? ws->tabs[i].id : 0; }

int ex_ws_set_active_group(ExWorkspace *ws, ExId g) {
    Node *nd = node(ws, g);
    if (!nd || nd->kind != EX_NODE_GROUP) return -1;
    if (ws->active_group != g) { ws->active_group = g; touch(ws); }
    return 0;
}

ExId ex_ws_find_tab(const ExWorkspace *ws, const char *path) {
    size_t i;
    for (i = 0; i < ws->ntabs; i++) if (ws->tabs[i].path && strcmp(ws->tabs[i].path, path) == 0) return ws->tabs[i].id;
    return 0;
}

/* ---- tree surgery ---- */

/* Puts `repl` where `old` is (in its parent split, or as its window's root). */
static void replace_in_parent(ExWorkspace *ws, Node *old, ExId repl) {
    Node *p = old->parent ? node(ws, old->parent) : NULL;
    if (p) {
        size_t i;
        for (i = 0; i < p->nkids; i++) if (p->kids[i] == old->id) { p->kids[i] = repl; break; }
    } else {
        size_t i;
        for (i = 0; i < ws->nwin; i++) if (ws->wins[i].root == old->id) ws->wins[i].root = repl;
    }
}

/* Removes a group from its window's tree; collapses a split left with one child. The group's
 * node stays allocated (the caller deletes it). Returns the id of a neighbouring group. */
static ExId detach_group(ExWorkspace *ws, ExId gid) {
    Node *g = node(ws, gid), *p;
    ExId neighbour = 0;
    size_t i, k;
    float lost;
    if (!g || !g->parent) return 0;
    p = node(ws, g->parent);
    if (!p) return 0;
    for (i = 0; i < p->nkids && p->kids[i] != gid; i++) {}
    if (i == p->nkids) return 0;
    lost = p->ratios[i];
    remove_at(p->kids, &p->nkids, i);
    memmove(p->ratios + i, p->ratios + i + 1, (p->nkids - i) * sizeof *p->ratios);
    for (k = 0; k < p->nkids; k++) p->ratios[k] += lost / (float)p->nkids;       /* the freed share goes to the neighbours */
    {
        ExId n0 = p->kids[i < p->nkids ? i : p->nkids - 1];
        Node *nn = node(ws, n0);
        while (nn && nn->kind == EX_NODE_SPLIT && nn->nkids) nn = node(ws, nn->kids[0]);
        neighbour = nn ? nn->id : 0;
    }
    if (p->nkids == 1) { /* a split with one child is that child */
        ExId only = p->kids[0];
        Node *c = node(ws, only);
        replace_in_parent(ws, p, only);
        if (c) c->parent = p->parent;
        delete_node(ws, p->id);
    }
    g->parent = 0;
    return neighbour;
}

static void unlink_all(ExWorkspace *ws, ExId gid) {
    size_t i, k;
    for (i = 0; i < ws->n; i++) {
        Node *nd = &ws->nodes[i];
        for (k = 0; k < nd->nlinked; k++) if (nd->linked[k] == gid) { remove_at(nd->linked, &nd->nlinked, k); k--; }
    }
}

static void drop_window_if_empty_popout(ExWorkspace *ws, ExId window);

/* ---- tabs ---- */

static size_t pinned_count(const ExWorkspace *ws, const Node *g) {
    size_t i, c = 0;
    for (i = 0; i < g->ntabs; i++) { ExTab *t = tab(ws, g->tabs[i]); if (t && t->pinned) c++; }
    return c;
}

ExId ex_ws_new_tab(ExWorkspace *ws, ExId group, const char *path, const char *view) {
    Node *g;
    ExTab *t;
    if (group == 0) group = ws->active_group;
    g = node(ws, group);
    if (!g || g->kind != EX_NODE_GROUP) return 0;
    if (ws->ntabs == ws->captabs) {
        size_t nc = ws->captabs ? ws->captabs * 2 : 16;
        ExTab *nt = realloc(ws->tabs, nc * sizeof *nt);
        if (!nt) return 0;
        ws->tabs = nt;
        ws->captabs = nc;
    }
    t = &ws->tabs[ws->ntabs++];
    memset(t, 0, sizeof *t);
    t->id = ws->next_tab++;
    t->group = group;
    t->path = dup_str(path ? path : "");
    t->view = dup_str(view ? view : "note");
    t->mode = dup_str("live");
    g = node(ws, group);
    push_id(&g->tabs, &g->ntabs, &g->captabs, t->id);
    g->active = t->id;
    ws->active_group = group;
    touch_layout(ws);
    return t->id;
}

static void free_tab(ExWorkspace *ws, ExId id) {
    ExTab *t = tab(ws, id);
    size_t i;
    if (!t) return;
    free(t->path); free(t->view); free(t->mode);
    i = (size_t)(t - ws->tabs);
    memmove(ws->tabs + i, ws->tabs + i + 1, (ws->ntabs - i - 1) * sizeof *t);
    ws->ntabs--;
}

/* Takes a tab out of its group; fixes the active tab; removes an emptied group when it is not the
 * last one of a main window. Returns 1 if the group was removed. */
static int detach_tab(ExWorkspace *ws, ExId tid) {
    ExTab *t = tab(ws, tid);
    Node *g;
    size_t i;
    ExId gid, window;
    if (!t) return 0;
    gid = t->group;
    g = node(ws, gid);
    if (!g) return 0;
    for (i = 0; i < g->ntabs && g->tabs[i] != tid; i++) {}
    if (i < g->ntabs) {
        remove_at(g->tabs, &g->ntabs, i);
        if (g->active == tid) g->active = g->ntabs ? g->tabs[i < g->ntabs ? i : g->ntabs - 1] : 0;
    }
    t->group = 0;
    if (g->ntabs > 0) return 0;
    window = ex_ws_window_of_node(ws, gid);
    if (g->parent) {                                /* another group is in the window: this one goes */
        ExId neighbour = detach_group(ws, gid);
        unlink_all(ws, gid);
        delete_node(ws, gid);
        if (ws->active_group == gid) ws->active_group = neighbour ? neighbour : ex_ws_window_root(ws, ex_ws_main_window(ws));
        return 1;
    }
    drop_window_if_empty_popout(ws, window);
    return 0;
}

static void drop_window_if_empty_popout(ExWorkspace *ws, ExId window) {
    Window *w = win(ws, window);
    size_t i;
    if (!w || w->main) return;
    {
        Node *root = node(ws, w->root);
        if (!root || root->kind != EX_NODE_GROUP || root->ntabs > 0) return;
        if (ws->active_group == root->id) ws->active_group = ex_ws_window_root(ws, ex_ws_main_window(ws));
        unlink_all(ws, root->id);
        delete_node(ws, root->id);
    }
    i = (size_t)(w - ws->wins);
    memmove(ws->wins + i, ws->wins + i + 1, (ws->nwin - i - 1) * sizeof *w);
    ws->nwin--;
}

ExId ex_ws_side_group(const ExWorkspace *ws, ExSide side) { return side == EX_SIDE_RIGHT ? ws->side_group[EX_SIDE_RIGHT] : 0; }

ExId ex_ws_side_group_ensure(ExWorkspace *ws, ExSide side) {
    Node *g;
    if (side != EX_SIDE_RIGHT) return 0;
    if (ws->side_group[EX_SIDE_RIGHT] && node(ws, ws->side_group[EX_SIDE_RIGHT])) return ws->side_group[EX_SIDE_RIGHT];
    g = new_node(ws, EX_NODE_GROUP);
    if (!g) return 0;
    ws->side_group[EX_SIDE_RIGHT] = g->id;
    touch_layout(ws);
    return g->id;
}

int ex_ws_close_tab(ExWorkspace *ws, ExId tid) {
    if (!tab(ws, tid)) return -1;
    detach_tab(ws, tid);
    free_tab(ws, tid);
    touch_layout(ws);
    return 0;
}

int ex_ws_activate(ExWorkspace *ws, ExId tid) {
    ExTab *t = tab(ws, tid);
    Node *g;
    if (!t || !(g = node(ws, t->group))) return -1;
    g->active = tid;
    ws->active_group = g->id;
    touch_layout(ws);
    return 0;
}

int ex_ws_move_tab(ExWorkspace *ws, ExId tid, ExId to, size_t index) {
    ExTab *t = tab(ws, tid);
    Node *dst;
    size_t pin, i;
    int same;
    if (!t) return -1;
    dst = node(ws, to);
    if (!dst || dst->kind != EX_NODE_GROUP) return -1;
    same = t->group == to;
    if (!same) detach_tab(ws, tid);
    else {
        for (i = 0; i < dst->ntabs; i++) if (dst->tabs[i] == tid) { remove_at(dst->tabs, &dst->ntabs, i); break; }
    }
    dst = node(ws, to);                                 /* detach_tab may have moved memory */
    if (!dst) return -1;
    pin = pinned_count(ws, dst);
    t = tab(ws, tid);
    if (t->pinned) { if (index > pin) index = pin; } else if (index < pin) index = pin;
    if (index > dst->ntabs) index = dst->ntabs;
    push_id(&dst->tabs, &dst->ntabs, &dst->captabs, 0);
    memmove(dst->tabs + index + 1, dst->tabs + index, (dst->ntabs - 1 - index) * sizeof *dst->tabs);
    dst->tabs[index] = tid;
    t->group = to;
    dst->active = tid;
    ws->active_group = to;
    touch_layout(ws);
    return 0;
}

ExId ex_ws_split(ExWorkspace *ws, ExId gid, ExSplitDir dir, int after, ExId move_tab) {
    Node *g = node(ws, gid), *ng, *p;
    ExId new_id;
    float share;
    size_t at;
    if (!g || g->kind != EX_NODE_GROUP || gid == ws->side_group[EX_SIDE_RIGHT]) return 0;     /* the sidebar's group is not split */
    if (move_tab) {
        ExTab *t = tab(ws, move_tab);
        if (!t) return 0;
        if (t->group == gid && g->ntabs == 1) return 0;   /* splitting off the only tab changes nothing */
    }
    ng = new_node(ws, EX_NODE_GROUP);
    if (!ng) return 0;
    new_id = ng->id;
    g = node(ws, gid);
    p = g->parent ? node(ws, g->parent) : NULL;
    if (p && p->dir == dir) {                             /* the same direction: one more column or row in the parent */
        size_t i;
        for (i = 0; i < p->nkids && p->kids[i] != gid; i++) {}
        at = after ? i + 1 : i;
        share = p->ratios[i] / 2.0f;
        p->ratios[i] = share;
        push_id(&p->kids, &p->nkids, &p->capkids, 0);
        p->ratios = realloc(p->ratios, p->capkids * sizeof *p->ratios);
        memmove(p->kids + at + 1, p->kids + at, (p->nkids - 1 - at) * sizeof *p->kids);
        memmove(p->ratios + at + 1, p->ratios + at, (p->nkids - 1 - at) * sizeof *p->ratios);
        p->kids[at] = new_id;
        p->ratios[at] = share;
        node(ws, new_id)->parent = p->id;
    } else {                                              /* a new split takes the group's place */
        Node *sp = new_node(ws, EX_NODE_SPLIT);
        if (!sp) return 0;
        g = node(ws, gid);
        ng = node(ws, new_id);
        sp->dir = dir;
        sp->parent = g->parent;
        replace_in_parent(ws, g, sp->id);
        sp->kids = malloc(2 * sizeof *sp->kids);
        sp->ratios = malloc(2 * sizeof *sp->ratios);
        sp->capkids = 2;
        sp->nkids = 2;
        sp->kids[after ? 0 : 1] = gid;
        sp->kids[after ? 1 : 0] = new_id;
        sp->ratios[0] = sp->ratios[1] = 0.5f;
        g->parent = sp->id;
        ng->parent = sp->id;
    }
    if (move_tab) ex_ws_move_tab(ws, move_tab, new_id, 0);
    else ws->active_group = new_id;
    touch_layout(ws);
    return new_id;
}

int ex_ws_close_group(ExWorkspace *ws, ExId gid) {
    Node *g = node(ws, gid);
    if (!g || g->kind != EX_NODE_GROUP) return -1;
    while (g && g->ntabs > 0) { ex_ws_close_tab(ws, g->tabs[g->ntabs - 1]); g = node(ws, gid); }
    if (g && g->parent) {                                  /* the loop above already removed it unless it was the last tab's group */
        ExId nb = detach_group(ws, gid);
        unlink_all(ws, gid);
        delete_node(ws, gid);
        if (ws->active_group == gid) ws->active_group = nb;
    }
    touch_layout(ws);
    return 0;
}

int ex_ws_set_pinned(ExWorkspace *ws, ExId tid, int pinned) {
    ExTab *t = tab(ws, tid);
    Node *g;
    size_t i, pin;
    if (!t || !(g = node(ws, t->group))) return -1;
    if (t->pinned == (pinned != 0)) return 0;
    t->pinned = pinned != 0;
    for (i = 0; i < g->ntabs; i++) if (g->tabs[i] == tid) { remove_at(g->tabs, &g->ntabs, i); break; }
    pin = pinned_count(ws, g);
    push_id(&g->tabs, &g->ntabs, &g->captabs, 0);
    {   /* pinned tabs go to the end of the pinned block; unpinned ones go right after it */
        size_t at = pin;
        memmove(g->tabs + at + 1, g->tabs + at, (g->ntabs - 1 - at) * sizeof *g->tabs);
        g->tabs[at] = tid;
    }
    touch_layout(ws);
    return 0;
}

int ex_ws_set_stacked(ExWorkspace *ws, ExId tid, int stacked) { ExTab *t = tab(ws, tid); if (!t) return -1; t->stacked = stacked != 0; touch_layout(ws); return 0; }

int ex_ws_set_tab_path(ExWorkspace *ws, ExId tid, const char *path, const char *view) {
    ExTab *t = tab(ws, tid);
    char *p, *v;
    if (!t) return -1;
    p = dup_str(path ? path : ""); v = dup_str(view ? view : t->view);
    if (!p || !v) { free(p); free(v); return -1; }
    free(t->path); free(t->view);
    t->path = p; t->view = v;
    t->cursor_line = t->cursor_column = 0;
    t->scroll_y = 0;
    touch_layout(ws);
    return 0;
}

int ex_ws_set_tab_state(ExWorkspace *ws, ExId tid, int line, int col, float scroll) {
    ExTab *t = tab(ws, tid);
    if (!t) return -1;
    if (t->cursor_line != line || t->cursor_column != col || t->scroll_y != scroll) { t->cursor_line = line; t->cursor_column = col; t->scroll_y = scroll; touch(ws); }
    return 0;
}

int ex_ws_set_tab_mode(ExWorkspace *ws, ExId tid, const char *mode) {
    ExTab *t = tab(ws, tid);
    char *m;
    if (!t || !mode) return -1;
    m = dup_str(mode);
    if (!m) return -1;
    free(t->mode);
    t->mode = m;
    touch_layout(ws);
    return 0;
}

/* ---- links ---- */

static int add_link(ExWorkspace *ws, ExId a, ExId b) {
    Node *x = node(ws, a);
    size_t i;
    if (!x) return -1;
    for (i = 0; i < x->nlinked; i++) if (x->linked[i] == b) return 0;
    push_id(&x->linked, &x->nlinked, &x->caplinked, b);
    return 0;
}

int ex_ws_link_groups(ExWorkspace *ws, ExId a, ExId b) {
    Node *x = node(ws, a), *y = node(ws, b);
    if (!x || !y || x->kind != EX_NODE_GROUP || y->kind != EX_NODE_GROUP || a == b) return -1;
    add_link(ws, a, b);
    add_link(ws, b, a);
    touch_layout(ws);
    return 0;
}

int ex_ws_unlink_group(ExWorkspace *ws, ExId gid) {
    Node *g = node(ws, gid);
    if (!g) return -1;
    unlink_all(ws, gid);
    g = node(ws, gid);
    g->nlinked = 0;
    touch_layout(ws);
    return 0;
}

size_t ex_ws_linked_count(const ExWorkspace *ws, ExId g) { Node *nd = node(ws, g); return nd ? nd->nlinked : 0; }
ExId ex_ws_linked_group(const ExWorkspace *ws, ExId g, size_t i) { Node *nd = node(ws, g); return nd && i < nd->nlinked ? nd->linked[i] : 0; }

/* ---- pop-out windows ---- */

ExId ex_ws_popout(ExWorkspace *ws, ExId tid) {
    ExTab *t = tab(ws, tid);
    Window *w;
    ExId wid;
    if (!t) return 0;
    /* the only tab of a pop-out window's only group: nothing to do */
    {
        ExId cur = ex_ws_window_of_node(ws, t->group);
        Window *cw = win(ws, cur);
        Node *g = node(ws, t->group);
        if (cw && !cw->main && g && g->ntabs == 1 && !g->parent) return cur;
    }
    w = add_window(ws, 0);
    if (!w) return 0;
    wid = w->id;
    ex_ws_move_tab(ws, tid, w->root, 0);
    touch_layout(ws);
    return wid;
}

int ex_ws_close_window(ExWorkspace *ws, ExId wid) {
    Window *w = win(ws, wid);
    ExId main_group;
    if (!w || w->main) return -1;
    main_group = ws->active_group;
    if (ex_ws_window_of_node(ws, main_group) != ex_ws_main_window(ws)) main_group = ex_ws_window_root(ws, ex_ws_main_window(ws));
    for (;;) {
        ExId any = 0;
        size_t i;
        w = win(ws, wid);
        if (!w) break;
        /* find a tab that lives in this window */
        for (i = 0; i < ws->ntabs; i++) if (ex_ws_window_of_node(ws, ws->tabs[i].group) == wid) { any = ws->tabs[i].id; break; }
        if (!any) break;
        ex_ws_move_tab(ws, any, main_group, (size_t)-1);
    }
    w = win(ws, wid);
    if (w) { /* an empty window that is still there: remove it and its groups */
        size_t i;
        for (i = 0; i < ws->n; i++) if (ws->nodes[i].kind == EX_NODE_GROUP && ex_ws_window_of_node(ws, ws->nodes[i].id) == wid) { ex_ws_close_group(ws, ws->nodes[i].id); break; }
        drop_window_if_empty_popout(ws, wid);
    }
    touch_layout(ws);
    return 0;
}

/* ---- sidebars ---- */

int ex_ws_sidebar_visible(const ExWorkspace *ws, ExSide s) { return ws->side[s].visible; }
int ex_ws_sidebar_width(const ExWorkspace *ws, ExSide s) { return ws->side[s].width; }
void ex_ws_set_sidebar(ExWorkspace *ws, ExSide s, int visible, int width) {
    if (width < EX_SIDEBAR_MIN_WIDTH) width = EX_SIDEBAR_MIN_WIDTH;
    if (width > 4000) width = 4000;       /* the window decides how wide it can really be (ex_sidebar_divider_create) */
    ws->side[s].visible = visible != 0;
    ws->side[s].width = width;
    touch(ws);
}
size_t ex_ws_panel_count(const ExWorkspace *ws, ExSide s) { return ws->side[s].n; }
const char *ex_ws_panel(const ExWorkspace *ws, ExSide s, size_t i) { return i < ws->side[s].n ? ws->side[s].panels[i] : NULL; }

int ex_ws_open_panel(ExWorkspace *ws, ExSide s, const char *name) {
    Sidebar *sb = &ws->side[s];
    size_t i;
    for (i = 0; i < sb->n; i++) if (strcmp(sb->panels[i], name) == 0) return 0;
    if (sb->n == sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 4;
        char **np = realloc(sb->panels, nc * sizeof *np);
        if (!np) return -1;
        sb->panels = np;
        sb->cap = nc;
    }
    sb->panels[sb->n] = dup_str(name);
    if (!sb->panels[sb->n]) return -1;
    sb->n++;
    touch(ws);
    return 0;
}

int ex_ws_close_panel(ExWorkspace *ws, ExSide s, const char *name) {
    Sidebar *sb = &ws->side[s];
    size_t i;
    for (i = 0; i < sb->n; i++)
        if (strcmp(sb->panels[i], name) == 0) {
            free(sb->panels[i]);
            memmove(sb->panels + i, sb->panels + i + 1, (sb->n - i - 1) * sizeof *sb->panels);
            sb->n--;
            touch(ws);
            return 0;
        }
    return -1;
}

/* ---- the session file ---- */

static void node_ref(char *out, size_t cap, const ExWorkspace *ws, ExId id) {
    Node *nd = node(ws, id);
    snprintf(out, cap, "%c%u", nd && nd->kind == EX_NODE_SPLIT ? 's' : 'g', (unsigned)id);
}

static void write_list_ids(char *out, size_t cap, const ExId *ids, size_t n, char prefix) {
    size_t i, used = 0;
    out[0] = 0;
    for (i = 0; i < n && used + 16 < cap; i++) used += (size_t)snprintf(out + used, cap - used, "%s%s%u", i ? "," : "", prefix ? (char[2]){prefix, 0} : "", (unsigned)ids[i]);
}

static void write_node(const ExWorkspace *ws, ExIni *ini, ExId id) {
    Node *nd = node(ws, id);
    char sec[48], buf[1024];
    size_t i, used = 0;
    if (!nd) return;
    if (nd->kind == EX_NODE_SPLIT) {
        snprintf(sec, sizeof sec, "split.%u", (unsigned)id);
        ex_ini_set(ini, sec, "direction", nd->dir == EX_SPLIT_VERTICAL ? "vertical" : "horizontal");
        buf[0] = 0;
        for (i = 0; i < nd->nkids && used + 24 < sizeof buf; i++) { char r[24]; node_ref(r, sizeof r, ws, nd->kids[i]); used += (size_t)snprintf(buf + used, sizeof buf - used, "%s%s", i ? "," : "", r); }
        ex_ini_set(ini, sec, "children", buf);
        buf[0] = 0; used = 0;
        for (i = 0; i < nd->nkids && used + 24 < sizeof buf; i++) used += (size_t)snprintf(buf + used, sizeof buf - used, "%s%.4f", i ? "," : "", nd->ratios[i]);
        ex_ini_set(ini, sec, "ratios", buf);
        for (i = 0; i < nd->nkids; i++) write_node(ws, ini, nd->kids[i]);
    } else {
        snprintf(sec, sizeof sec, "group.%u", (unsigned)id);
        write_list_ids(buf, sizeof buf, nd->tabs, nd->ntabs, 0);
        ex_ini_set(ini, sec, "tabs", buf);
        if (nd->active) ex_ini_set_int(ini, sec, "active", nd->active);
        if (nd->nlinked) {
            buf[0] = 0; used = 0;
            for (i = 0; i < nd->nlinked && used + 24 < sizeof buf; i++) used += (size_t)snprintf(buf + used, sizeof buf - used, "%sg%u", i ? "," : "", (unsigned)nd->linked[i]);
            ex_ini_set(ini, sec, "linked", buf);
        }
        if (nd->nhist) {                                                    /* the pane's history (5d) */
            ex_ini_set_int(ini, sec, "history_pos", nd->hpos);
            for (i = 0; i < nd->nhist; i++) {
                char key[24], val[900];
                snprintf(key, sizeof key, "history_%u", (unsigned)i);
                snprintf(val, sizeof val, "%s|%s|%d|%d|%.1f", nd->hist[i].path, nd->hist[i].view, nd->hist[i].line, nd->hist[i].column, (double)nd->hist[i].scroll);
                ex_ini_set(ini, sec, key, val);
            }
        }
    }
}

char *ex_ws_serialize(const ExWorkspace *ws, size_t *len) {
    ExIni *ini = ex_ini_new();
    size_t i, k;
    char sec[48], buf[2048];
    char *text;
    ExId order[1];
    (void)order;
    if (!ini) return NULL;
    ex_ini_set(ini, "session", "version", "1");
    for (i = 0; i < ws->nwin; i++) {
        const Window *w = &ws->wins[i];
        char ref[24];
        snprintf(sec, sizeof sec, "window.%u", (unsigned)w->id);
        ex_ini_set(ini, sec, "main", w->main ? "true" : "false");
        ex_ini_set_int(ini, sec, "x", w->x); ex_ini_set_int(ini, sec, "y", w->y);
        ex_ini_set_int(ini, sec, "width", w->w); ex_ini_set_int(ini, sec, "height", w->h);
        ex_ini_set(ini, sec, "maximized", w->maximized ? "true" : "false");
        node_ref(ref, sizeof ref, ws, w->root);
        ex_ini_set(ini, sec, "root", ref);
    }
    ex_ini_set(ini, "layout", "left_visible", ws->side[EX_SIDE_LEFT].visible ? "true" : "false");
    ex_ini_set(ini, "layout", "right_visible", ws->side[EX_SIDE_RIGHT].visible ? "true" : "false");
    ex_ini_set_int(ini, "layout", "left_width", ws->side[EX_SIDE_LEFT].width);
    ex_ini_set_int(ini, "layout", "right_width", ws->side[EX_SIDE_RIGHT].width);
    buf[0] = 0;
    {
        size_t used = 0;
        static const char *const names[2] = {"left", "right"};
        for (k = 0; k < 2; k++)
            for (i = 0; i < ws->side[k].n && used + 100 < sizeof buf; i++)
                used += (size_t)snprintf(buf + used, sizeof buf - used, "%s%s:%s", used ? "," : "", names[k], ws->side[k].panels[i]);
    }
    ex_ini_set(ini, "layout", "open_panels", buf);
    {
        Node *ag = node(ws, ws->active_group);
        if (ag && ag->active) ex_ini_set_int(ini, "layout", "active_tab", ag->active);
        ex_ini_set_int(ini, "layout", "active_group", ws->active_group);
    }
    for (i = 0; i < ws->nwin; i++) write_node(ws, ini, ws->wins[i].root);
    if (ws->side_group[EX_SIDE_RIGHT] && node(ws, ws->side_group[EX_SIDE_RIGHT])) {              /* the right sidebar's group of tabs (5e) */
        ex_ini_set_int(ini, "layout", "right_group", ws->side_group[EX_SIDE_RIGHT]);
        write_node(ws, ini, ws->side_group[EX_SIDE_RIGHT]);
    }
    for (i = 0; i < ws->ntabs; i++) {
        const ExTab *t = &ws->tabs[i];
        snprintf(sec, sizeof sec, "tab.%u", (unsigned)t->id);
        ex_ini_set(ini, sec, "path", t->path);
        ex_ini_set(ini, sec, "view", t->view);
        ex_ini_set(ini, sec, "mode", t->mode);
        ex_ini_set(ini, sec, "pinned", t->pinned ? "true" : "false");
        ex_ini_set(ini, sec, "stacked", t->stacked ? "true" : "false");
        ex_ini_set_int(ini, sec, "group", t->group);
        ex_ini_set_int(ini, sec, "cursor_line", t->cursor_line);
        ex_ini_set_int(ini, sec, "cursor_column", t->cursor_column);
        snprintf(buf, sizeof buf, "%.1f", t->scroll_y);
        ex_ini_set(ini, sec, "scroll_y", buf);
    }
    if (ws->search && ws->search[0]) ex_ini_set(ini, "search", "text", ws->search);
    for (i = 0; i < ex_ini_section_count(ws->extra); i++) {   /* what this version does not know */
        const char *name = ex_ini_section_name(ws->extra, i);
        for (k = 0; k < ex_ini_key_count(ws->extra, name); k++) {
            const char *key = ex_ini_key_name(ws->extra, name, k);
            ex_ini_set(ini, name, key, ex_ini_get(ws->extra, name, key));
        }
    }
    text = ex_ini_write(ini, len);
    ex_ini_free(ini);
    return text;
}

static int parse_ref(const char *s, ExId *id, char *kind) {
    char *end;
    unsigned long v;
    while (*s == ' ') s++;
    if ((*s != 'g' && *s != 's') || !s[1]) return -1;
    *kind = *s;
    v = strtoul(s + 1, &end, 10);
    if (end == s + 1 || v == 0) return -1;
    *id = (ExId)v;
    return 0;
}

const char *ex_ws_extra_get(const ExWorkspace *ws, const char *section, const char *key) { return ws ? ex_ini_get(ws->extra, section, key) : NULL; }

void ex_ws_extra_set(ExWorkspace *ws, const char *section, const char *key, const char *value) {
    const char *old;
    if (!ws || !value) return;
    old = ex_ini_get(ws->extra, section, key);
    if (old && !strcmp(old, value)) return;
    ex_ini_set(ws->extra, section, key, value);
    touch(ws);
}

static int is_known_section(const char *name) {
    return !strncmp(name, "window.", 7) || !strncmp(name, "split.", 6) || !strncmp(name, "group.", 6) || !strncmp(name, "tab.", 4) ||
           !strcmp(name, "layout") || !strcmp(name, "session") || !strcmp(name, "search");
}

/* Builds the node for a reference from the file's sections; depth-limited so a bad file cannot loop. */
static ExId build_node(ExWorkspace *ws, const ExIni *ini, const char *ref, ExId parent, int depth, ExId *seen, size_t *nseen) {
    ExId id;
    char kind, sec[48];
    Node *nd;
    size_t i;
    if (depth > 32 || parse_ref(ref, &id, &kind) != 0) return 0;
    for (i = 0; i < *nseen; i++) if (seen[i] == id) return 0;       /* the same node twice: a damaged file */
    if (*nseen >= 512) return 0;
    seen[(*nseen)++] = id;
    if (kind == 's') {
        const char *ch, *ra;
        char list[1024], *tok, *save = NULL;
        ExId kids[64];
        float ratios[64];
        size_t nk = 0, k;
        snprintf(sec, sizeof sec, "split.%u", (unsigned)id);
        ch = ex_ini_get(ini, sec, "children");
        if (!ch) return 0;
        snprintf(list, sizeof list, "%s", ch);
        {
            nd = new_node(ws, EX_NODE_SPLIT);
            if (!nd) return 0;
            if (id >= ws->next_node) ws->next_node = id + 1;
        }
        /* keep the file's ids: reserve this id by renaming the node */
        nd->id = id;
        nd->parent = parent;
        nd->dir = ex_ini_get(ini, sec, "direction") && !strcmp(ex_ini_get(ini, sec, "direction"), "vertical") ? EX_SPLIT_VERTICAL : EX_SPLIT_HORIZONTAL;
        for (tok = strtok_r_compat(list, &save); tok && nk < 64; tok = strtok_r_compat(NULL, &save)) {
            ExId c = build_node(ws, ini, tok, id, depth + 1, seen, nseen);
            if (c) kids[nk++] = c;
        }
        nd = node(ws, id);
        if (nk == 0) { delete_node(ws, id); return 0; }
        ra = ex_ini_get(ini, sec, "ratios");
        for (k = 0; k < nk; k++) ratios[k] = 0;
        if (ra) {
            char rl[1024], *rt, *rs = NULL;
            snprintf(rl, sizeof rl, "%s", ra);
            for (k = 0, rt = strtok_r_compat(rl, &rs); rt && k < nk; rt = strtok_r_compat(NULL, &rs), k++) ratios[k] = (float)atof(rt);
        }
        {
            float sum = 0;
            for (k = 0; k < nk; k++) { if (ratios[k] <= 0) ratios[k] = 1.0f / (float)nk; sum += ratios[k]; }
            for (k = 0; k < nk; k++) ratios[k] /= sum;
        }
        nd->kids = malloc(nk * sizeof *nd->kids);
        nd->ratios = malloc(nk * sizeof *nd->ratios);
        if (!nd->kids || !nd->ratios) return 0;
        memcpy(nd->kids, kids, nk * sizeof *kids);
        memcpy(nd->ratios, ratios, nk * sizeof *ratios);
        nd->nkids = nd->capkids = nk;
        if (nk == 1) {                          /* a split with one child is that child */
            ExId only = kids[0];
            Node *c = node(ws, only);
            if (c) c->parent = parent;
            delete_node(ws, id);
            return only;
        }
        return id;
    }
    nd = new_node(ws, EX_NODE_GROUP);
    if (!nd) return 0;
    nd->id = id;
    nd->parent = parent;
    if (id >= ws->next_node) ws->next_node = id + 1;
    return id;
}

int ex_ws_deserialize(ExWorkspace *ws, const char *text, size_t len) {
    ExIni *ini = ex_ini_parse(text, len);
    size_t i, k;
    ExId seen[512], main_root = 0;
    size_t nseen = 0;
    ExId max_tab = 0;
    const char *panels;
    if (!ini) return -1;
    clear_all(ws);
    ex_ini_free(ws->extra);
    ws->extra = ex_ini_new();
    ws->next_node = ws->next_tab = ws->next_win = 1;
    ws->active_group = 0;
    ws->side[EX_SIDE_LEFT].visible = ws->side[EX_SIDE_RIGHT].visible = 1;
    ws->side[EX_SIDE_LEFT].width = 260;
    ws->side[EX_SIDE_RIGHT].width = 300;

    /* windows and their trees */
    for (i = 0; i < ex_ini_section_count(ini); i++) {
        const char *name = ex_ini_section_name(ini, i);
        Window *w;
        ExId root;
        unsigned wid;
        if (strncmp(name, "window.", 7) != 0 || sscanf(name + 7, "%u", &wid) != 1 || wid == 0 || ws->nwin >= 32) continue;
        if (!ex_ini_get(ini, name, "root")) continue;
        root = build_node(ws, ini, ex_ini_get(ini, name, "root"), 0, 0, seen, &nseen);
        if (!root) continue;
        if (ws->nwin == ws->capwin) {
            size_t nc = ws->capwin ? ws->capwin * 2 : 4;
            Window *nw = realloc(ws->wins, nc * sizeof *nw);
            if (!nw) continue;
            ws->wins = nw;
            ws->capwin = nc;
        }
        w = &ws->wins[ws->nwin++];
        memset(w, 0, sizeof *w);
        w->id = wid;
        w->root = root;
        w->main = ex_ini_get_bool(ini, name, "main", 0);
        w->x = (int)ex_ini_get_int(ini, name, "x", 0);
        w->y = (int)ex_ini_get_int(ini, name, "y", 0);
        w->w = (int)ex_ini_get_int(ini, name, "width", w->main ? 1100 : 800);
        w->h = (int)ex_ini_get_int(ini, name, "height", w->main ? 750 : 600);
        w->maximized = ex_ini_get_bool(ini, name, "maximized", 0);
        if (wid >= ws->next_win) ws->next_win = wid + 1;
        if (w->main && !main_root) main_root = root;
    }
    {                                                                       /* the right sidebar's group of tabs (5e) */
        long long rg = ex_ini_get_int(ini, "layout", "right_group", 0);
        if (rg > 0) {
            char ref[24];
            ExId id;
            snprintf(ref, sizeof ref, "g%lld", rg);
            id = build_node(ws, ini, ref, 0, 0, seen, &nseen);
            if (id && node(ws, id) && node(ws, id)->kind == EX_NODE_GROUP) ws->side_group[EX_SIDE_RIGHT] = id;
        }
    }
    if (ws->nwin > 0 && !main_root) { ws->wins[0].main = 1; main_root = ws->wins[0].root; }
    for (i = 0; i + 1 < ws->nwin; i++) for (k = i + 1; k < ws->nwin; k++) if (ws->wins[i].main && ws->wins[k].main) ws->wins[k].main = 0;
    if (ws->nwin == 0) {                                   /* no layout (an old or damaged file): one group */
        Window *w = add_window(ws, 1);
        if (w) main_root = w->root;
    }

    /* tabs */
    for (i = 0; i < ex_ini_section_count(ini); i++) {
        const char *name = ex_ini_section_name(ini, i);
        unsigned tid;
        ExTab *t;
        ExId gid;
        Node *g;
        if (strncmp(name, "tab.", 4) != 0 || sscanf(name + 4, "%u", &tid) != 1 || tid == 0 || tab(ws, tid)) continue;
        gid = (ExId)ex_ini_get_int(ini, name, "group", 0);
        g = node(ws, gid);
        if (!g || g->kind != EX_NODE_GROUP) { gid = 0; g = NULL; }
        if (ws->ntabs == ws->captabs) {
            size_t nc = ws->captabs ? ws->captabs * 2 : 16;
            ExTab *nt = realloc(ws->tabs, nc * sizeof *nt);
            if (!nt) continue;
            ws->tabs = nt;
            ws->captabs = nc;
        }
        t = &ws->tabs[ws->ntabs++];
        memset(t, 0, sizeof *t);
        t->id = tid;
        t->group = gid;                                    /* 0: placed in the main window below */
        t->path = dup_str(ex_ini_get(ini, name, "path") ? ex_ini_get(ini, name, "path") : "");
        t->view = dup_str(ex_ini_get(ini, name, "view") ? ex_ini_get(ini, name, "view") : "note");
        t->mode = dup_str(ex_ini_get(ini, name, "mode") ? ex_ini_get(ini, name, "mode") : "live");
        t->pinned = ex_ini_get_bool(ini, name, "pinned", 0);
        t->stacked = ex_ini_get_bool(ini, name, "stacked", 0);
        t->cursor_line = (int)ex_ini_get_int(ini, name, "cursor_line", 0);
        t->cursor_column = (int)ex_ini_get_int(ini, name, "cursor_column", 0);
        t->scroll_y = (float)ex_ini_get_float(ini, name, "scroll_y", 0);
        if (tid > max_tab) max_tab = tid;
    }
    ws->next_tab = max_tab + 1;

    /* each group's tab order, from `tabs`; tabs it does not list are appended; orphans go to the main window */
    for (i = 0; i < ws->n; i++) {
        Node *g = &ws->nodes[i];
        char sec[48], list[2048], *tok, *save = NULL;
        const char *tl;
        size_t t;
        if (g->kind != EX_NODE_GROUP) continue;
        snprintf(sec, sizeof sec, "group.%u", (unsigned)g->id);
        tl = ex_ini_get(ini, sec, "tabs");
        if (tl) {
            snprintf(list, sizeof list, "%s", tl);
            for (tok = strtok_r_compat(list, &save); tok; tok = strtok_r_compat(NULL, &save)) {
                ExTab *tb = tab(ws, (ExId)strtoul(tok, NULL, 10));
                if (tb && tb->group == g->id) {
                    size_t j;
                    int dup = 0;
                    for (j = 0; j < g->ntabs; j++) if (g->tabs[j] == tb->id) dup = 1;
                    if (!dup) push_id(&g->tabs, &g->ntabs, &g->captabs, tb->id);
                }
            }
        }
        for (t = 0; t < ws->ntabs; t++) {
            size_t j;
            int have = 0;
            if (ws->tabs[t].group != g->id) continue;
            for (j = 0; j < g->ntabs; j++) if (g->tabs[j] == ws->tabs[t].id) have = 1;
            if (!have) push_id(&g->tabs, &g->ntabs, &g->captabs, ws->tabs[t].id);
        }
        g->active = (ExId)ex_ini_get_int(ini, sec, "active", 0);
        {
            ExTab *at = tab(ws, g->active);
            if (!at || at->group != g->id) g->active = g->ntabs ? g->tabs[0] : 0;
        }
        {                                                                   /* the pane's history (5d): bad entries are skipped */
            size_t k;
            for (k = 0; k < EX_HISTORY_MAX; k++) {
                char key[24], val[900], *f[5];
                const char *v;
                int nf = 0;
                char *c;
                snprintf(key, sizeof key, "history_%u", (unsigned)k);
                v = ex_ini_get(ini, sec, key);
                if (!v) break;
                snprintf(val, sizeof val, "%s", v);
                for (c = val, f[nf++] = c; *c && nf < 5; c++) if (*c == '|') { *c = 0; f[nf++] = c + 1; }
                if (nf != 5 || !f[1][0]) continue;
                if (g->nhist == g->caphist) {
                    size_t nc = g->caphist ? g->caphist * 2 : 8;
                    ExPlace *np = realloc(g->hist, nc * sizeof *np);
                    if (!np) break;
                    g->hist = np; g->caphist = nc;
                }
                g->hist[g->nhist].path = dup_str(f[0]); g->hist[g->nhist].view = dup_str(f[1]);
                g->hist[g->nhist].line = atoi(f[2]); g->hist[g->nhist].column = atoi(f[3]); g->hist[g->nhist].scroll = (float)atof(f[4]);
                if (g->hist[g->nhist].line < 0) g->hist[g->nhist].line = 0;
                if (g->hist[g->nhist].column < 0) g->hist[g->nhist].column = 0;
                if (!g->hist[g->nhist].path || !g->hist[g->nhist].view) { free_place(&g->hist[g->nhist]); break; }
                g->nhist++;
            }
            g->hpos = (int)ex_ini_get_int(ini, sec, "history_pos", (long long)g->nhist - 1);
            if (g->hpos < 0 || (size_t)g->hpos >= g->nhist) g->hpos = (int)g->nhist - 1;
        }
        tl = ex_ini_get(ini, sec, "linked");
        if (tl) {
            snprintf(list, sizeof list, "%s", tl);
            for (tok = strtok_r_compat(list, &save); tok; tok = strtok_r_compat(NULL, &save)) {
                ExId lg; char kind;
                if (parse_ref(tok, &lg, &kind) == 0 && kind == 'g' && node(ws, lg) && lg != g->id) push_id(&g->linked, &g->nlinked, &g->caplinked, lg);
            }
        }
    }
    {
        Node *mr = node(ws, main_root);
        while (mr && mr->kind == EX_NODE_SPLIT && mr->nkids) mr = node(ws, mr->kids[0]);
        for (i = 0; i < ws->ntabs; i++) if (ws->tabs[i].group == 0 && mr) {
            ws->tabs[i].group = mr->id;
            push_id(&mr->tabs, &mr->ntabs, &mr->captabs, ws->tabs[i].id);
            if (!mr->active) mr->active = ws->tabs[i].id;
        }
    }
    /* pinned tabs come first in every group (stable) */
    for (i = 0; i < ws->n; i++) {
        Node *g = &ws->nodes[i];
        if (g->kind == EX_NODE_GROUP && g->ntabs > 1) {
            ExId *tmp = malloc(g->ntabs * sizeof *tmp);
            size_t n = 0, j;
            if (!tmp) continue;
            for (j = 0; j < g->ntabs; j++) { ExTab *t = tab(ws, g->tabs[j]); if (t && t->pinned) tmp[n++] = g->tabs[j]; }
            for (j = 0; j < g->ntabs; j++) { ExTab *t = tab(ws, g->tabs[j]); if (!(t && t->pinned)) tmp[n++] = g->tabs[j]; }
            memcpy(g->tabs, tmp, n * sizeof *tmp);
            free(tmp);
        }
    }

    /* sidebars, panels, active group, search text */
    ws->side[EX_SIDE_LEFT].visible = ex_ini_get_bool(ini, "layout", "left_visible", 1);
    ws->side[EX_SIDE_RIGHT].visible = ex_ini_get_bool(ini, "layout", "right_visible", 1);
    ws->side[EX_SIDE_LEFT].width = (int)ex_ini_get_int(ini, "layout", "left_width", 260);
    ws->side[EX_SIDE_RIGHT].width = (int)ex_ini_get_int(ini, "layout", "right_width", 300);
    for (k = 0; k < 2; k++) {
        if (ws->side[k].width < EX_SIDEBAR_MIN_WIDTH) ws->side[k].width = EX_SIDEBAR_MIN_WIDTH;
        if (ws->side[k].width > 1200) ws->side[k].width = 1200;
    }
    panels = ex_ini_get(ini, "layout", "open_panels");
    if (panels) {
        char list[1024], *tok, *save = NULL;
        snprintf(list, sizeof list, "%s", panels);
        for (tok = strtok_r_compat(list, &save); tok; tok = strtok_r_compat(NULL, &save)) {
            if (!strncmp(tok, "left:", 5)) ex_ws_open_panel(ws, EX_SIDE_LEFT, tok + 5);
            else if (!strncmp(tok, "right:", 6)) ex_ws_open_panel(ws, EX_SIDE_RIGHT, tok + 6);
        }
    }
    ws->active_group = (ExId)ex_ini_get_int(ini, "layout", "active_group", 0);
    if (!node(ws, ws->active_group) || node(ws, ws->active_group)->kind != EX_NODE_GROUP) {
        Node *mr = node(ws, main_root);
        while (mr && mr->kind == EX_NODE_SPLIT && mr->nkids) mr = node(ws, mr->kids[0]);
        ws->active_group = mr ? mr->id : 0;
    }
    if (ex_ini_get(ini, "search", "text")) ws->search = dup_str(ex_ini_get(ini, "search", "text"));

    /* unknown sections are kept */
    for (i = 0; i < ex_ini_section_count(ini); i++) {
        const char *name = ex_ini_section_name(ini, i);
        if (is_known_section(name)) continue;
        for (k = 0; k < ex_ini_key_count(ini, name); k++) {
            const char *key = ex_ini_key_name(ini, name, k);
            ex_ini_set(ws->extra, name, key, ex_ini_get(ini, name, key));
        }
    }
    ex_ini_free(ini);
    touch_layout(ws);
    return 0;
}
