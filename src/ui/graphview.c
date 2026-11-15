#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graphopts.h"
#include "exhaust_graphview.h"
#include "exhaust_layout.h"
#include "exhaust_render.h"

/* Sizes that are ratios, not pixels: the pixels come from the theme's spacing token and the display scale. */
#define NODE_BASE 0.35f          /* a leaf's radius, in spacing units */
#define LABEL_ZOOM 0.9f          /* labels appear above this zoom (pixels per layout unit) and fade in over the next half of it */
#define FIT_FILL 0.6f            /* the fitted graph fills this much of the view: a small cluster in the middle of the pane, as in screenshots/graph-view.webp */
#define MAX_LABELS 150
#define LABEL_CANDIDATES 800     /* with more nodes than this on screen, no labels: the picture is too dense to read */
#define ZOOM_MIN 0.01f
#define ZOOM_MAX 200.0f           /* 5a stopped at 8; 5g goes deeper, with the circles and the labels growing */
#define LABEL_GROW_MAX 6.0f       /* a label grows with the zoom up to this many times its size */
#define DRAG_PX 5.0f              /* a press that moves further than this (in device-independent pixels) is a drag */
#define KEY_PULL_PX 40.0f         /* Alt+arrow pulls the selected node this far, for KEY_PULL_MS */
#define KEY_PULL_MS 400
#define FADED 0.22f              /* the alpha of what is not near the hovered node */

/* ---- the model ---- */

struct ExGraphModel {
    ExGraph *g;
    ExLayout *layout;
    const float *xy;
    uint64_t gen;
    int have_xy;
    uint32_t *adj_off, *adj;                 /* the neighbours of every node (a link either way) */
    uint8_t *adj_dir;                        /* for each entry of adj: 1 if the link goes from the node to the neighbour, 0 if it comes to the node */
    float minx, maxx, miny, maxy;
    float cs, gx0, gy0;                      /* the grid: cell size and the corner it starts at */
    int gw, gh;
    uint32_t *cell_start, *cell_items;
    size_t cell_cap;
    int active;                              /* the node of the open note, or -1 */
    char *active_path;
    int animating;
    ExGraphOptions opts;                     /* the settings of the gear panel (5b) */
    uint32_t *rank;                          /* each node's place in the order of creation, worked out when an animation first needs it (5c) */
    int is_local;                            /* the model of a local graph tab */
};

ExGraphModel *ex_graphmodel_create(void) {
    ExGraphModel *m = calloc(1, sizeof *m);
    if (m) { m->active = -1; ex_graphopts_defaults(&m->opts); }
    return m;
}

static void model_clear_graph(ExGraphModel *m) {
    ex_layout_free(m->layout);
    m->layout = NULL;
    if (m->g) { ex_graph_free(m->g); free(m->g); m->g = NULL; }
    free(m->adj_off); free(m->adj); free(m->adj_dir); m->adj_off = m->adj = NULL; m->adj_dir = NULL;
    free(m->rank); m->rank = NULL;
    free(m->cell_start); free(m->cell_items); m->cell_start = m->cell_items = NULL; m->cell_cap = 0;
    m->xy = NULL; m->have_xy = 0; m->gw = m->gh = 0;
}

void ex_graphmodel_free(ExGraphModel *m) {
    if (!m) return;
    model_clear_graph(m);
    free(m->active_path);
    free(m);
}

static void model_find_active(ExGraphModel *m) {
    size_t i;
    m->active = -1;
    if (!m->g || !m->active_path) return;
    for (i = 0; i < m->g->n; i++) if (m->g->nodes[i].kind == EX_GRAPH_NOTE && !strcmp(m->g->nodes[i].path, m->active_path)) { m->active = (int)i; return; }
}

void ex_graphmodel_set_active(ExGraphModel *m, const char *path) {
    if (!m) return;
    if ((path == NULL) == (m->active_path == NULL) && (!path || !strcmp(path, m->active_path))) return;
    free(m->active_path);
    m->active_path = NULL;
    if (path) { size_t l = strlen(path) + 1; m->active_path = malloc(l); if (m->active_path) memcpy(m->active_path, path, l); }
    model_find_active(m);
}

static void build_adjacency(ExGraphModel *m) {
    const ExGraph *g = m->g;
    size_t i, *fill = calloc(g->n + 1, sizeof *fill);
    m->adj_off = calloc(g->n + 2, sizeof *m->adj_off);
    m->adj = malloc((2 * g->ne + 1) * sizeof *m->adj);
    m->adj_dir = malloc((2 * g->ne + 1) * sizeof *m->adj_dir);
    if (!fill || !m->adj_off || !m->adj || !m->adj_dir) { free(fill); free(m->adj_off); free(m->adj); free(m->adj_dir); m->adj_off = m->adj = NULL; m->adj_dir = NULL; return; }
    for (i = 0; i < g->ne; i++) { m->adj_off[g->edges[i].from + 1]++; m->adj_off[g->edges[i].to + 1]++; }
    for (i = 0; i < g->n; i++) m->adj_off[i + 1] += m->adj_off[i];
    for (i = 0; i < g->ne; i++) {
        uint32_t a = g->edges[i].from, b = g->edges[i].to;
        m->adj_dir[m->adj_off[a] + fill[a]] = 1; m->adj[m->adj_off[a] + fill[a]++] = b;
        m->adj_dir[m->adj_off[b] + fill[b]] = 0; m->adj[m->adj_off[b] + fill[b]++] = a;
    }
    free(fill);
}

static void apply_params(ExGraphModel *m) {
    ExLayoutParams p;
    if (!m->layout) return;
    p.center = m->opts.center; p.charge = -3.0f * m->opts.repel; p.link_strength = powf(m->opts.link_strength, 5.0f);     /* the slider's value to the fourth power: the length of a link goes with the logarithm of the strength (5g, formats.md) */ p.link_distance = m->opts.link_distance;
    ex_layout_set_params(m->layout, &p);
}

int ex_graphmodel_set_options(ExGraphModel *m, const ExGraphOptions *o) {
    ExGraphOptions n;
    int filters, forces;
    if (!m || !o) return 0;
    n = *o;
    ex_graphopts_clamp(&n);
    filters = !ex_graphopts_filters_equal(&m->opts, &n);
    forces = n.center != m->opts.center || n.repel != m->opts.repel || n.link_strength != m->opts.link_strength || n.link_distance != m->opts.link_distance;
    m->opts = n;
    if (forces) apply_params(m);
    return filters;
}
const ExGraphOptions *ex_graphmodel_options(const ExGraphModel *m) { return m ? &m->opts : NULL; }

void ex_graphmodel_set_local(ExGraphModel *m, int local) { if (m) m->is_local = local != 0; }

/* The same nodes (kind, path, group) in the same order and the same links: a graph read again after something that changed nothing in it. */
static int graph_same(const ExGraph *a, const ExGraph *b, int *same_nodes) {
    size_t i;
    *same_nodes = 0;
    if (!a || !b || a->n != b->n) return 0;
    for (i = 0; i < a->n; i++) if (a->nodes[i].kind != b->nodes[i].kind || a->nodes[i].group != b->nodes[i].group || strcmp(a->nodes[i].path, b->nodes[i].path)) return 0;
    *same_nodes = 1;
    if (a->ne != b->ne) return 0;
    for (i = 0; i < a->ne; i++) if (a->edges[i].from != b->edges[i].from || a->edges[i].to != b->edges[i].to) return 0;
    return 1;
}

void ex_graphmodel_set_graph(ExGraphModel *m, ExGraph *g) {
    ExLayout *l;
    int same_nodes;
    if (!m || !g) return;
    if (m->g && m->layout && graph_same(m->g, g, &same_nodes)) { ex_graph_free(g); free(g); return; }   /* nothing changed (a note was saved): the picture stays as it is */
    l = ex_layout_create(g, m->g, m->have_xy ? m->xy : NULL, 1u);      /* warm: the old graph and where its nodes are */
    ex_layout_free(m->layout);
    m->layout = NULL;
    if (m->g) { ex_graph_free(m->g); free(m->g); }
    free(m->adj_off); free(m->adj); free(m->adj_dir); m->adj_off = m->adj = NULL; m->adj_dir = NULL;
    m->g = g;
    m->layout = l;
    apply_params(m);
    m->xy = NULL; m->have_xy = 0; m->gen = 0;
    free(m->rank); m->rank = NULL;
    build_adjacency(m);
    model_find_active(m);
    m->animating = 1;
}

/* The grid over the current positions: nodes by cell, so a view reads only the cells it shows and a pointer looks only at its own. */
static void build_grid(ExGraphModel *m) {
    size_t n = m->g->n, i, cells;
    float w, h;
    uint32_t *fill;
    if (n == 0) { m->gw = m->gh = 0; return; }
    m->minx = m->maxx = m->xy[0]; m->miny = m->maxy = m->xy[1];
    for (i = 1; i < n; i++) {
        float x = m->xy[2 * i], y = m->xy[2 * i + 1];
        if (x < m->minx) m->minx = x;
        if (x > m->maxx) m->maxx = x;
        if (y < m->miny) m->miny = y;
        if (y > m->maxy) m->maxy = y;
    }
    w = m->maxx - m->minx + 1.0f; h = m->maxy - m->miny + 1.0f;
    m->cs = 40.0f;
    while ((w / m->cs + 1.0f) * (h / m->cs + 1.0f) > (float)(4 * n + 16)) m->cs *= 1.5f;      /* never more cells than a few for each node */
    m->gx0 = m->minx; m->gy0 = m->miny;
    m->gw = (int)(w / m->cs) + 1; m->gh = (int)(h / m->cs) + 1;
    cells = (size_t)m->gw * (size_t)m->gh;
    if (cells + 1 > m->cell_cap) {
        uint32_t *ns = realloc(m->cell_start, (cells + 1) * sizeof *ns);
        if (!ns) { m->gw = m->gh = 0; return; }
        m->cell_start = ns; m->cell_cap = cells + 1;
    }
    {
        uint32_t *ni = realloc(m->cell_items, n * sizeof *ni);
        if (!ni) { m->gw = m->gh = 0; return; }
        m->cell_items = ni;
    }
    fill = calloc(cells + 1, sizeof *fill);
    if (!fill) { m->gw = m->gh = 0; return; }
    memset(m->cell_start, 0, (cells + 1) * sizeof *m->cell_start);
    for (i = 0; i < n; i++) {
        int cx = (int)((m->xy[2 * i] - m->gx0) / m->cs), cy = (int)((m->xy[2 * i + 1] - m->gy0) / m->cs);
        m->cell_start[(size_t)cy * (size_t)m->gw + (size_t)cx + 1]++;
    }
    for (i = 0; i < cells; i++) m->cell_start[i + 1] += m->cell_start[i];
    for (i = 0; i < n; i++) {
        int cx = (int)((m->xy[2 * i] - m->gx0) / m->cs), cy = (int)((m->xy[2 * i + 1] - m->gy0) / m->cs);
        size_t c = (size_t)cy * (size_t)m->gw + (size_t)cx;
        m->cell_items[m->cell_start[c] + fill[c]++] = (uint32_t)i;
    }
    free(fill);
}

int ex_graphmodel_update(ExGraphModel *m) {
    uint64_t gen = 0;
    const float *xy;
    ExLayoutStats st;
    int changed = 0;
    if (!m || !m->layout || !m->g) return 0;
    xy = ex_layout_positions(m->layout, &gen);
    if (!m->have_xy || gen != m->gen) { m->xy = xy; m->gen = gen; m->have_xy = 1; build_grid(m); changed = 1; }
    ex_layout_stats(m->layout, &st);
    if (m->animating != !st.settled) changed = 1;
    m->animating = !st.settled;
    return changed;
}

void ex_graphmodel_drag(ExGraphModel *m, int node, float x, float y) { if (m && m->layout) ex_layout_pull(m->layout, node, x, y); }
int ex_graphmodel_animating(const ExGraphModel *m) { return m && m->animating; }
float ex_graphmodel_start_temperature(const ExGraphModel *m) { ExLayoutStats st; if (!m || !m->layout) return 0.0f; ex_layout_stats(m->layout, &st); return st.start_alpha; }
float ex_graphmodel_temperature(const ExGraphModel *m) { ExLayoutStats st; if (!m || !m->layout) return 0.0f; ex_layout_stats(m->layout, &st); return st.alpha; }
size_t ex_graphmodel_node_count(const ExGraphModel *m) { return m && m->g ? m->g->n : 0; }
size_t ex_graphmodel_edge_count(const ExGraphModel *m) { return m && m->g ? m->g->ne : 0; }
const ExGraph *ex_graphmodel_graph(const ExGraphModel *m) { return m ? m->g : NULL; }
const float *ex_graphmodel_positions(const ExGraphModel *m) { return m && m->have_xy ? m->xy : NULL; }

/* ---- the view ---- */

typedef struct View {
    ExUi *ui;
    ExGraphModel *m;
    ExGraphHandlers h;
    ExGeom *geom;
    float zoom, cx, cy;                      /* pixels per layout unit, and the layout point at the middle of the widget */
    int following;                           /* the view fits the whole graph until the user pans or zooms */
    int hover, selected;                     /* nodes, or -1 */
    uint8_t *mark;                           /* 1: a neighbour of the hovered node, 2: the hovered node itself (n entries, kept clean) */
    size_t mark_n;
    int mark_for;                            /* the node `mark` describes */
    float *sxy;                              /* every node's place on the screen this frame (x and y interleaved) */
    size_t sxy_cap;
    uint32_t *vis;                           /* the nodes in view this frame */
    size_t vis_cap, nvis;
    int pressed, moved;
    int grab, dragging;                      /* the node a press landed on (-1: none), and whether it is being dragged now (5g) */
    float grab_dx, grab_dy;                  /* from the pointer to the node, in layout units, at the press: the node does not jump to the pointer */
    int kb_pull;                             /* an Alt+arrow pull is running */
    Uint64 kb_until;
    float press_x, press_y, press_cx, press_cy;
    ExGraphViewStats stats;
    void *extra; void (*free_extra)(void *); void (*tick)(void *, int64_t);   /* the gear panel's state (5b), freed with the view, and what it does once a frame */
    int (*act)(void *, int);                 /* a key the panel handles (g: the gear; [ and ]: the local graph's depth): 1 if it did */
    int anim_on;                             /* the animation (5c): nodes appear in the order they were created */
    Uint64 anim_t0;
    uint32_t anim_shown;                     /* the nodes with a rank below this are shown */
} View;

static int node_shown(const View *v, uint32_t i);

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_GRAPH ? (View *)w->owned : NULL; }

static void view_free(void *p) {
    View *v = p;
    if (!v) return;
    if (v->dragging || v->kb_pull) ex_graphmodel_drag(v->m, -1, 0, 0);               /* never leave a node pulled by a view that is gone */
    ex_geom_free(v->geom);
    if (v->free_extra) v->free_extra(v->extra);
    free(v->mark); free(v->vis); free(v->sxy);
    free(v);
}

/* A node's radius in pixels: bigger for a busier note, and a little bigger when zoomed in. radius_k is what does not depend on the node
 * (the spacing, the zoom and the node size), worked out once for a frame. */
static float radius_k(const ExUi *ui, const View *v) {
    const ExTheme *t = ex_ui_theme(ui);
    float base = (float)ex_theme_size(t, EX_SIZE_SPACING) * ex_renderer_scale(ex_ui_renderer((ExUi *)ui)) * NODE_BASE;
    float zf = sqrtf(v->zoom);
    if (zf < 0.7f) zf = 0.7f;
    return base * zf * v->m->opts.node_size;
}
static float radius_of(float rk, uint32_t degree) { return rk * (0.85f + 0.3f * sqrtf((float)degree)); }
static float node_radius(const ExUi *ui, const View *v, uint32_t degree) { return radius_of(radius_k(ui, v), degree); }

static void to_screen(const View *v, const ExWidget *w, float lx, float ly, float *sx, float *sy) {
    *sx = (lx - v->cx) * v->zoom + w->x + w->w / 2.0f;
    *sy = (ly - v->cy) * v->zoom + w->y + w->h / 2.0f;
}

static void to_layout(const View *v, const ExWidget *w, float sx, float sy, float *lx, float *ly) {
    *lx = (sx - w->x - w->w / 2.0f) / v->zoom + v->cx;
    *ly = (sy - w->y - w->h / 2.0f) / v->zoom + v->cy;
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void fit(View *v, const ExWidget *w) {
    const ExGraphModel *m = v->m;
    float bw = m->maxx - m->minx, bh = m->maxy - m->miny;
    float zx, zy;
    if (!m->have_xy || m->g->n == 0) return;
    v->cx = (m->minx + m->maxx) / 2.0f; v->cy = (m->miny + m->maxy) / 2.0f;
    zx = bw > 1.0f ? w->w * FIT_FILL / bw : 2.0f;
    zy = bh > 1.0f ? w->h * FIT_FILL / bh : 2.0f;
    v->zoom = clampf(zx < zy ? zx : zy, ZOOM_MIN, 2.0f);
}

/* ---- hit testing through the grid ---- */

static void visible_nodes(View *v, const ExWidget *w, float margin_px) {
    const ExGraphModel *m = v->m;
    float lx0, ly0, lx1, ly1, mr = margin_px / v->zoom;
    int cx0, cy0, cx1, cy1, cx, cy;
    size_t n = m->g->n;
    v->nvis = 0;
    if (!m->have_xy || m->gw == 0 || n == 0) return;
    if (v->vis_cap < n) { uint32_t *nv = realloc(v->vis, n * sizeof *nv); if (!nv) return; v->vis = nv; v->vis_cap = n; }
    to_layout(v, w, w->x, w->y, &lx0, &ly0);
    to_layout(v, w, w->x + w->w, w->y + w->h, &lx1, &ly1);
    cx0 = (int)floorf((lx0 - mr - m->gx0) / m->cs); cx1 = (int)floorf((lx1 + mr - m->gx0) / m->cs);
    cy0 = (int)floorf((ly0 - mr - m->gy0) / m->cs); cy1 = (int)floorf((ly1 + mr - m->gy0) / m->cs);
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= m->gw) cx1 = m->gw - 1;
    if (cy1 >= m->gh) cy1 = m->gh - 1;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) {
            size_t c = (size_t)cy * (size_t)m->gw + (size_t)cx, k;
            for (k = m->cell_start[c]; k < m->cell_start[c + 1]; k++) v->vis[v->nvis++] = m->cell_items[k];
        }
}

int ex_graphview_node_at(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    const ExGraphModel *m;
    float lx, ly, reach, best_d = 1e30f;
    int best = -1, cx0, cx1, cy0, cy1, cx, cy;
    if (!v) return -1;
    m = v->m;
    if (!m->g || !m->have_xy || m->gw == 0 || x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h) return -1;
    to_layout(v, w, x, y, &lx, &ly);
    reach = 40.0f / v->zoom;                                                       /* the biggest circle is well under 40 pixels across */
    cx0 = (int)floorf((lx - reach - m->gx0) / m->cs); cx1 = (int)floorf((lx + reach - m->gx0) / m->cs);
    cy0 = (int)floorf((ly - reach - m->gy0) / m->cs); cy1 = (int)floorf((ly + reach - m->gy0) / m->cs);
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= m->gw) cx1 = m->gw - 1;
    if (cy1 >= m->gh) cy1 = m->gh - 1;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) {
            size_t c = (size_t)cy * (size_t)m->gw + (size_t)cx, k;
            for (k = m->cell_start[c]; k < m->cell_start[c + 1]; k++) {
                uint32_t i = m->cell_items[k];
                float sx, sy, dx, dy, d, r;
                if (!node_shown(v, i)) continue;                                   /* an animation has not created it yet */
                r = node_radius(v->ui, v, m->g->nodes[i].degree);
                if (r < 5.0f) r = 5.0f;                                            /* a small dot is still easy to hit */
                to_screen(v, w, m->xy[2 * i], m->xy[2 * i + 1], &sx, &sy);
                dx = sx - x; dy = sy - y; d = dx * dx + dy * dy;
                if (d <= r * r && d < best_d) { best_d = d; best = (int)i; }
            }
        }
    return best;
}

int ex_graphview_node_screen(ExWidget *w, int node, float *x, float *y) {
    View *v = view_of(w);
    float sx, sy;
    if (!v || !v->m->g || !v->m->have_xy || node < 0 || (size_t)node >= v->m->g->n) return 0;
    to_screen(v, w, v->m->xy[2 * node], v->m->xy[2 * node + 1], &sx, &sy);
    if (x) *x = sx;
    if (y) *y = sy;
    return sx >= w->x && sx < w->x + w->w && sy >= w->y && sy < w->y + w->h;
}

/* ---- drawing ---- */

static ExColor with_alpha(ExColor c, float a) { c.a = (uint8_t)clampf((float)c.a * a + 0.5f, 0.0f, 255.0f); return c; }

typedef struct Cand { uint32_t degree, idx; } Cand;
static int cmp_cand(const void *a, const void *b) {
    const Cand *x = a, *y = b;
    if (x->degree != y->degree) return x->degree > y->degree ? -1 : 1;
    return x->idx < y->idx ? -1 : x->idx > y->idx;
}

typedef struct Rect { float x0, y0, x1, y1; } Rect;

static void set_marks(View *v) {
    const ExGraphModel *m = v->m;
    size_t i;
    if (v->mark_for == v->hover) return;
    if (v->mark && v->mark_for >= 0 && m->adj_off) {                              /* clear the old ones: only the old node and its neighbours were set */
        size_t o = m->adj_off[v->mark_for], e = m->adj_off[v->mark_for + 1];
        v->mark[v->mark_for] = 0;
        for (i = o; i < e; i++) v->mark[m->adj[i]] = 0;
    }
    v->mark_for = v->hover;
    if (v->hover < 0 || !m->adj_off) return;
    if (v->mark_n < m->g->n) {
        free(v->mark);
        v->mark = calloc(m->g->n + 1, 1);
        v->mark_n = v->mark ? m->g->n : 0;
        if (!v->mark) { v->mark_for = -1; return; }
    }
    for (i = m->adj_off[v->hover]; i < m->adj_off[v->hover + 1]; i++) v->mark[m->adj[i]] = 1;
    v->mark[v->hover] = 2;
}

/* ---- the animation (5c) ---- */

#define ANIM_MS 10000
static int s_anims;                                              /* views whose animation is running (UI thread only): the loop draws every frame while it is not 0 */

typedef struct KeyIdx { int64_t key; uint32_t idx; } KeyIdx;
static int cmp_keyidx(const void *a, const void *b) {
    const KeyIdx *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->idx < y->idx ? -1 : x->idx > y->idx;
}

/* Each node's place in the order of creation: a note by its file's ctime (unknown counts as the oldest), a name or file that is not a note
 * with the first note that links to it. One sort, when an animation starts. */
static void ensure_rank(ExGraphModel *m) {
    const ExGraph *g = m->g;
    KeyIdx *k;
    size_t i;
    if (m->rank || !g || g->n == 0) return;
    k = malloc(g->n * sizeof *k);
    m->rank = malloc(g->n * sizeof *m->rank);
    if (!k || !m->rank) { free(k); free(m->rank); m->rank = NULL; return; }
    for (i = 0; i < g->n; i++) { k[i].idx = (uint32_t)i; k[i].key = g->nodes[i].kind == EX_GRAPH_NOTE ? g->nodes[i].ctime : INT64_MAX; }
    for (i = 0; i < g->ne; i++) {
        uint32_t a = g->edges[i].from, b = g->edges[i].to;
        if (g->nodes[a].kind == EX_GRAPH_NOTE && g->nodes[b].kind != EX_GRAPH_NOTE && g->nodes[a].ctime < k[b].key) k[b].key = g->nodes[a].ctime;
    }
    for (i = 0; i < g->n; i++) if (k[i].key == INT64_MAX) k[i].key = 0;
    qsort(k, g->n, sizeof *k, cmp_keyidx);
    for (i = 0; i < g->n; i++) m->rank[k[i].idx] = (uint32_t)i;
    free(k);
}

static void anim_stop(View *v) { if (v->anim_on) { v->anim_on = 0; if (s_anims > 0) s_anims--; } }

void ex_graphview_animate(ExWidget *w, int on) {
    View *v = view_of(w);
    if (!v) return;
    if (!on) { anim_stop(v); return; }
    if (!v->m->g || v->m->g->n == 0) return;
    ensure_rank(v->m);
    if (!v->m->rank) return;
    if (!v->anim_on) s_anims++;
    v->anim_on = 1; v->anim_t0 = SDL_GetTicks(); v->anim_shown = 0;
}
void ex_graphview_animation_toggle(ExWidget *w) { View *v = view_of(w); if (v) ex_graphview_animate(w, !v->anim_on); }
int ex_graphview_animation_running(const ExWidget *w) { const View *v = view_of(w); return v && v->anim_on; }
int ex_graphview_animations_running(void) { return s_anims; }
void ex_graphview_animation_seek(ExWidget *w, float fraction) {
    View *v = view_of(w);
    if (!v || !v->anim_on) return;
    v->anim_t0 = SDL_GetTicks() - (Uint64)(fraction * (float)ANIM_MS);
}

/* Moves the animation on to now; the number of nodes shown. Ends it when all are shown. */
static void anim_step(View *v) {
    const ExGraph *g = v->m->g;
    float f;
    if (!v->anim_on) return;
    if (!g || !v->m->rank) { anim_stop(v); return; }
    f = (float)(SDL_GetTicks() - v->anim_t0) / (float)ANIM_MS;
    if (f >= 1.0f) { v->anim_shown = (uint32_t)g->n; anim_stop(v); return; }
    v->anim_shown = (uint32_t)(f * (float)g->n);
}
static int node_shown(const View *v, uint32_t i) { return !v->anim_on || (v->m->rank && v->m->rank[i] < v->anim_shown); }

/* The colour of a node: its group's, else the node's own. */
static ExColor node_color(const ExTheme *t, const ExGraphOptions *o, const ExGraphNode *nd, ExColor c_node, ExColor c_unres) {
    static const int slot[6] = {EX_COLOR_ACCENT, EX_COLOR_SUCCESS, EX_COLOR_WARNING, EX_COLOR_ERROR, EX_COLOR_TAG, EX_COLOR_LINK};
    if (nd->group >= 1 && nd->group <= EX_GRAPH_MAX_GROUPS) return ex_theme_color(t, (ExColorToken)slot[o->groups[nd->group - 1].color % 6]);
    return nd->kind == EX_GRAPH_UNRESOLVED ? c_unres : c_node;
}

void ex_graphview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    ExGraphModel *m;
    const ExTheme *t;
    ExRenderer *r;
    const ExGraph *g;
    ExColor c_node, c_unres, c_link, c_focus, c_text, c_muted;
    float lw, fade, rk;
    size_t i, k, edges = 0, nodes = 0;
    if (!v) return;
    if (v->tick) v->tick(v->extra, (int64_t)SDL_GetTicks());
    anim_step(v);
    if (v->kb_pull && SDL_GetTicks() >= v->kb_until) { v->kb_pull = 0; ex_graphmodel_drag(v->m, -1, 0, 0); }
    if (v->grab >= 0 && (!v->m->g || (size_t)v->grab >= v->m->g->n)) { v->grab = -1; v->dragging = 0; }      /* the graph was read again: the node is gone */
    m = v->m; t = ex_ui_theme(ui); r = ex_ui_renderer(ui); g = m->g;
    memset(&v->stats, 0, sizeof v->stats);
    v->stats.hover = v->hover; v->stats.selected = v->selected; v->stats.following = v->following;
    c_text = ex_theme_color(t, EX_COLOR_TEXT); c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    if (!g || !m->have_xy || g->n == 0) {                                          /* nothing to show: say why */
        ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
        const char *msg = !g ? "Reading the links of your notes..." : m->is_local ? "Open a note to see the notes around it." : "There are no notes in this vault yet.";
        float tw = ex_text_measure(r, f, ex_ui_base_size(ui), msg, strlen(msg));
        ExFontMetrics fm;
        if (ex_text_metrics(r, f, ex_ui_base_size(ui), &fm) == 0)
            ex_text_draw(r, f, ex_ui_base_size(ui), w->x + (w->w - tw) / 2.0f, w->y + (w->h - (float)fm.line_height) / 2.0f, msg, strlen(msg), c_muted);
        return;
    }
    if (v->following) fit(v, w);
    v->hover = v->hover >= (int)g->n ? -1 : v->hover;
    if (v->selected >= (int)g->n) v->selected = -1;
    set_marks(v);
    c_node = ex_theme_color(t, EX_COLOR_GRAPH_NODE); c_unres = ex_theme_color(t, EX_COLOR_GRAPH_NODE_UNRESOLVED);
    c_link = ex_theme_color(t, EX_COLOR_GRAPH_LINK); c_focus = ex_theme_color(t, EX_COLOR_GRAPH_FOCUS);
    lw = (float)ex_px(r, 1.0f) * m->opts.link_thickness;
    rk = radius_k(ui, v);
    fade = v->hover >= 0 ? FADED : 1.0f;
    if (!v->geom) { v->geom = ex_geom_create(); if (!v->geom) return; }
    ex_geom_reset(v->geom);
    ex_geom_set_feather(v->geom, 0.0f);                                          /* lines: one crisp pixel */
    ex_geom_set_cull(v->geom, w->x - 8.0f, w->y - 8.0f, w->w + 16.0f, w->h + 16.0f);
    visible_nodes(v, w, 40.0f);
    if (v->sxy_cap < g->n) { float *ns = realloc(v->sxy, 2 * g->n * sizeof *ns); if (!ns) return; v->sxy = ns; v->sxy_cap = g->n; }
    for (i = 0; i < g->n; i++) to_screen(v, w, m->xy[2 * i], m->xy[2 * i + 1], &v->sxy[2 * i], &v->sxy[2 * i + 1]);   /* where every node is this frame */

    /* the lines, under everything; those at the hovered node wait for later */
    for (k = 0; k < g->ne; k++) {
        uint32_t a = g->edges[k].from, b = g->edges[k].to;
        float ax, ay, bx, by;
        if (v->hover >= 0 && ((int)a == v->hover || (int)b == v->hover)) continue;
        if (v->anim_on && (!node_shown(v, a) || !node_shown(v, b))) continue;                 /* not created yet */
        ax = v->sxy[2 * a]; ay = v->sxy[2 * a + 1];
        bx = v->sxy[2 * b]; by = v->sxy[2 * b + 1];
        if (fabsf(ax - bx) < 1.0f && fabsf(ay - by) < 1.0f) continue;                  /* shorter than a pixel: nothing to see */
        if ((ax < w->x && bx < w->x) || (ax > w->x + w->w && bx > w->x + w->w) || (ay < w->y && by < w->y) || (ay > w->y + w->h && by > w->y + w->h)) continue;
        ex_geom_line(v->geom, ax, ay, bx, by, lw, with_alpha(c_link, fade));
        if (m->opts.arrows) {                                                          /* the head at the note that is linked to, at the rim of its circle */
            float dl = hypotf(bx - ax, by - ay), rr = radius_of(rk, g->nodes[b].degree), hs = (float)ex_px(r, 6.0f) * (0.6f + 0.4f * m->opts.link_thickness);
            if (dl > rr + hs * 0.5f) ex_geom_arrow(v->geom, bx - (bx - ax) / dl * rr, by - (by - ay) / dl * rr, bx - ax, by - ay, hs, with_alpha(c_link, fade));
        }
        edges++;
    }
    /* the circles of what is in view, with a soft edge */
    ex_geom_set_feather(v->geom, 1.0f);
    for (k = 0; k < v->nvis; k++) {
        uint32_t i2 = v->vis[k];
        const ExGraphNode *nd = &g->nodes[i2];
        float sx, sy;
        ExColor col = node_color(t, &m->opts, nd, c_node, c_unres);
        if ((int)i2 == m->active) continue;                                        /* drawn last, in the focus color */
        if (!node_shown(v, i2)) continue;
        if (v->hover >= 0 && v->mark && v->mark[i2]) continue;                     /* the hovered node and its neighbours, later */
        sx = v->sxy[2 * i2]; sy = v->sxy[2 * i2 + 1];
        ex_geom_circle(v->geom, sx, sy, radius_of(rk, nd->degree), with_alpha(col, fade));
        nodes++;
    }
    if (v->hover >= 0 && m->adj_off) {                                             /* the hovered node's lines, its neighbours and itself */
        size_t o = m->adj_off[v->hover], e = m->adj_off[v->hover + 1];
        float hx, hy;
        hx = v->sxy[2 * v->hover]; hy = v->sxy[2 * v->hover + 1];
        for (i = o; i < e; i++) {
            uint32_t nb = m->adj[i];
            float nx, ny;
            if (!node_shown(v, nb)) continue;
            nx = v->sxy[2 * nb]; ny = v->sxy[2 * nb + 1];
            ex_geom_line(v->geom, hx, hy, nx, ny, lw * 1.5f, c_focus);
            if (m->opts.arrows && m->adj_dir) {                                     /* the head at the end the link points to, at the rim of that circle */
                int out = m->adj_dir[i];
                float tx = out ? nx : hx, ty = out ? ny : hy, fx = out ? hx : nx, fy = out ? hy : ny, dl = hypotf(tx - fx, ty - fy);
                float rr = out ? radius_of(rk, g->nodes[nb].degree) : radius_of(rk, g->nodes[v->hover].degree) * 1.15f, hs = (float)ex_px(r, 6.0f) * (0.6f + 0.4f * m->opts.link_thickness);
                if (dl > rr + hs * 0.5f) ex_geom_arrow(v->geom, tx - (tx - fx) / dl * rr, ty - (ty - fy) / dl * rr, tx - fx, ty - fy, hs, c_focus);
            }
            edges++;
        }
        for (i = o; i < e; i++) {
            uint32_t nb = m->adj[i];
            float nx, ny;
            if (!node_shown(v, nb)) continue;
            nx = v->sxy[2 * nb]; ny = v->sxy[2 * nb + 1];
            ex_geom_circle(v->geom, nx, ny, node_radius(ui, v, g->nodes[nb].degree), node_color(t, &m->opts, &g->nodes[nb], c_node, c_unres));
            nodes++;
        }
        ex_geom_circle(v->geom, hx, hy, node_radius(ui, v, g->nodes[v->hover].degree) * 1.15f, c_focus);
        nodes++;
    }
    if (m->active >= 0) {                                                          /* the note that is open */
        float sx, sy;
        sx = v->sxy[2 * m->active]; sy = v->sxy[2 * m->active + 1];
        ex_geom_circle(v->geom, sx, sy, node_radius(ui, v, g->nodes[m->active].degree) * 1.15f, c_focus);
        nodes++;
    }
    if (v->selected >= 0) {                                                        /* the keyboard's node: a ring round it */
        float sx, sy, rr = node_radius(ui, v, g->nodes[v->selected].degree) * 1.15f + 3.0f * ex_renderer_scale(r);
        sx = v->sxy[2 * v->selected]; sy = v->sxy[2 * v->selected + 1];
        ex_geom_circle(v->geom, sx, sy, rr, c_focus);
        ex_geom_circle(v->geom, sx, sy, rr - 2.0f * ex_renderer_scale(r), with_alpha(c_node, 1.0f));
        ex_geom_circle(v->geom, sx, sy, node_radius(ui, v, g->nodes[v->selected].degree), c_focus);
        nodes++;
    }
    v->stats.triangles = ex_geom_submit(r, v->geom);
    { ExGeomStats gs; ex_geom_stats(v->geom, &gs); v->stats.geom_grows = gs.grows; }
    v->stats.nodes_drawn = nodes; v->stats.edges_drawn = edges;
    v->stats.zoom = v->zoom; v->stats.dragging = v->dragging || v->kb_pull;
    v->stats.line_px = lw; v->stats.node_px = node_radius(ui, v, 1); v->stats.label_zoom = LABEL_ZOOM * exp2f(m->opts.text_fade);

    /* labels: above the zoom threshold, the busiest first, none on top of another; the hovered and selected node always have one */
    {
        ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
        float lz = LABEL_ZOOM * exp2f(m->opts.text_fade), alpha = clampf((v->zoom - lz) / (lz * 0.5f), 0.0f, 1.0f);
        float grow = v->zoom > lz ? exp2f(roundf(log2f(sqrtf(v->zoom / lz)) * 4.0f) / 4.0f) : 1.0f;     /* past the zoom where labels appear they grow with it, in steps of a quarter octave */
        float mult = (grow > LABEL_GROW_MAX ? LABEL_GROW_MAX : grow) * m->opts.text_size, size;
        if (mult > 8.0f) mult = 8.0f;                                                                   /* never a label that fills the pane, whatever the two settings say */
        size = ex_ui_base_size(ui) * 0.8f * mult;
        Rect placed[MAX_LABELS + 2];
        size_t np = 0, shown = 0, nc = 0, nlab;
        Cand *cand = NULL;
        ExFontMetrics fm;
        int forced[2] = {v->hover, v->selected};
        if (ex_text_metrics(r, f, size, &fm) == 0) {
            if (alpha > 0.0f && v->nvis <= LABEL_CANDIDATES && v->nvis > 0) {
                cand = malloc(v->nvis * sizeof *cand);
                if (cand) {
                    for (k = 0; k < v->nvis; k++) {
                        float sx, sy;
                        uint32_t i2 = v->vis[k];
                        sx = v->sxy[2 * i2]; sy = v->sxy[2 * i2 + 1];
                        if (node_shown(v, i2) && sx >= w->x && sx < w->x + w->w && sy >= w->y && sy < w->y + w->h) { cand[nc].degree = g->nodes[i2].degree; cand[nc].idx = i2; nc++; }
                    }
                    qsort(cand, nc, sizeof *cand, cmp_cand);
                }
            }
            nlab = nc + 2;
            for (k = 0; k < nlab && shown < MAX_LABELS; k++) {
                int idx, force = 0;
                float sx, sy, tw, lx, ly, maxw = 160.0f * ex_renderer_scale(r) * (size / (ex_ui_base_size(ui) * 0.8f));
                char buf[300];
                const char *lab;
                size_t llen, fl;
                ExColor col;
                Rect rc;
                size_t j;
                int dup = 0;
                if (k < 2) { idx = forced[k]; force = 1; if (idx < 0) continue; }
                else { idx = (int)cand[k - 2].idx; }
                if (!force) for (j = 0; j < 2; j++) if (forced[j] == idx) dup = 1;
                if (dup) continue;
                lab = ex_graph_label(&g->nodes[idx], &llen);
                fl = ex_text_fit(r, f, size, lab, llen, maxw, buf, sizeof buf);
                if (!fl) continue;
                tw = ex_text_measure(r, f, size, buf, fl);
                sx = v->sxy[2 * idx]; sy = v->sxy[2 * idx + 1];
                lx = sx - tw / 2.0f; ly = sy + node_radius(ui, v, g->nodes[idx].degree) + 2.0f * ex_renderer_scale(r);
                rc.x0 = lx - 2.0f; rc.x1 = lx + tw + 2.0f; rc.y0 = ly; rc.y1 = ly + (float)fm.line_height;
                if (rc.x1 < w->x || rc.x0 > w->x + w->w || rc.y1 < w->y || rc.y0 > w->y + w->h) continue;
                if (!force) {
                    for (j = 0; j < np; j++) if (rc.x0 < placed[j].x1 && rc.x1 > placed[j].x0 && rc.y0 < placed[j].y1 && rc.y1 > placed[j].y0) break;
                    if (j < np) continue;
                }
                if (np < MAX_LABELS + 2) placed[np++] = rc;
                col = g->nodes[idx].kind == EX_GRAPH_UNRESOLVED ? c_muted : c_text;
                ex_text_draw(r, f, size, lx, ly, buf, fl, force ? c_text : with_alpha(col, alpha * (v->hover >= 0 && !(v->mark && v->mark[idx]) ? FADED : 1.0f)));
                shown++;
            }
        }
        free(cand);
        v->stats.labels_drawn = shown; v->stats.label_px = size;
    }
}

/* ---- input ---- */

static void open_node(View *v, int node, int new_tab) {
    const ExGraph *g = v->m->g;
    if (node < 0 || !g || (size_t)node >= g->n || !v->h.open) return;
    v->h.open(v->h.user, g->nodes[node].path, g->nodes[node].kind, new_tab);
}

static void zoom_about(View *v, const ExWidget *w, float factor, float sx, float sy) {
    float lx, ly, nz = clampf(v->zoom * factor, ZOOM_MIN, ZOOM_MAX);
    to_layout(v, w, sx, sy, &lx, &ly);
    v->zoom = nz;
    v->cx = lx - (sx - w->x - w->w / 2.0f) / nz;            /* the point under the pointer stays under it */
    v->cy = ly - (sy - w->y - w->h / 2.0f) / nz;
    v->following = 0;
}

void ex_graphview_wheel(ExWidget *w, float dy, float x, float y) {
    View *v = view_of(w);
    if (!v || dy == 0.0f) return;
    zoom_about(v, w, powf(1.12f, dy), x, y);
}

void ex_graphview_hover(ExWidget *w, float x, float y, int inside) {
    View *v = view_of(w);
    int node;
    if (!v || !v->m->g) return;
    node = inside && !v->pressed ? ex_graphview_node_at(w, x, y) : -1;
    if (node == v->hover) return;
    v->hover = node;
    if (v->h.hover) {
        const ExGraph *g = v->m->g;
        v->h.hover(v->h.user, node >= 0 ? g->nodes[node].path : NULL, node >= 0 ? g->nodes[node].kind : 0, x, y);
    }
}

void ex_graphview_context(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    int node;
    if (!v || !v->m->g || !v->h.context) return;
    node = ex_graphview_node_at(w, x, y);
    if (node >= 0) v->h.context(v->h.user, v->m->g->nodes[node].path, v->m->g->nodes[node].kind, x, y);
}

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    if (kind == EX_MOUSE_DOWN) {
        if (under != w) return 0;                                                  /* a press on the gear or its panel is theirs */
        v->pressed = 1; v->moved = 0; v->dragging = 0;
        v->press_x = x; v->press_y = y; v->press_cx = v->cx; v->press_cy = v->cy;
        v->grab = ex_graphview_node_at(w, x, y);                                    /* a press on a node can become a drag of it; on empty space, a pan */
        if (v->grab >= 0 && v->m->have_xy) {
            float lx, ly;
            to_layout(v, w, x, y, &lx, &ly);
            v->grab_dx = v->m->xy[2 * v->grab] - lx; v->grab_dy = v->m->xy[2 * v->grab + 1] - ly;
        } else v->grab = -1;
        ex_ui_set_focus(v->ui, w);
        return 1;
    }
    if (!v->pressed) return 0;
    if (kind == EX_MOUSE_DRAG) {
        float dx = x - v->press_x, dy = y - v->press_y, sc = ex_renderer_scale(ex_ui_renderer(v->ui));
        if (!v->moved && dx * dx + dy * dy > DRAG_PX * DRAG_PX * sc * sc) v->moved = 1;
        if (v->moved && v->grab >= 0 && v->grab < (int)(v->m->g ? v->m->g->n : 0)) {   /* the node follows the pointer, held back by the layout's forces */
            float lx, ly;
            if (!v->dragging) { v->dragging = 1; v->following = 0; if (v->kb_pull) v->kb_pull = 0; }
            to_layout(v, w, x, y, &lx, &ly);                                        /* from the view as it is now: several drag events can arrive before a frame */
            ex_graphmodel_drag(v->m, v->grab, lx + v->grab_dx, ly + v->grab_dy);
        } else if (v->moved) { v->cx = v->press_cx - dx / v->zoom; v->cy = v->press_cy - dy / v->zoom; v->following = 0; }
        return 1;
    }
    /* released: a press that did not move is a click; a dragged node is let go */
    v->pressed = 0;
    if (v->dragging) { v->dragging = 0; ex_graphmodel_drag(v->m, -1, 0, 0); v->grab = -1; return 1; }
    v->grab = -1;
    if (!v->moved) open_node(v, ex_graphview_node_at(w, x, y), 0);
    return 1;
}

/* The node nearest in a direction (dx, dy: a unit step on the screen) from the selected node, or from the middle of the view when none is
 * selected: a node that lies within about sixty degrees of that direction, the nearest first. */
static int select_dir(View *v, const ExWidget *w, float dx, float dy) {
    const ExGraph *g = v->m->g;
    float ox, oy, best = 1e30f;
    int from = v->selected, pick = -1;
    size_t i;
    if (!g || !v->m->have_xy || g->n == 0) return -1;
    if (from >= 0) { ox = v->m->xy[2 * from]; oy = v->m->xy[2 * from + 1]; }
    else { ox = v->cx; oy = v->cy; (void)w; }
    for (i = 0; i < g->n; i++) {
        float ex = v->m->xy[2 * i] - ox, ey = v->m->xy[2 * i + 1] - oy, d = sqrtf(ex * ex + ey * ey), c;
        if ((int)i == from || !node_shown(v, (uint32_t)i)) continue;
        if (d < 0.0001f) { if (from < 0) { pick = (int)i; best = 0.0f; } continue; }
        c = (ex * dx + ey * dy) / d;
        if (from >= 0 && c < 0.5f) continue;                      /* not in that direction */
        if (from < 0) { if (d < best) { best = d; pick = (int)i; } continue; }     /* from the middle: simply the nearest */
        if (d / c < best) { best = d / c; pick = (int)i; }
    }
    return pick;
}

static void select_node(View *v, int node) {
    v->selected = node;
    if (node >= 0 && v->m->have_xy) { v->cx = v->m->xy[2 * node]; v->cy = v->m->xy[2 * node + 1]; v->following = 0; }   /* bring it into the middle */
}

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    float step = 60.0f * ex_renderer_scale(ex_ui_renderer(v->ui)) / v->zoom;
    const ExGraph *g = v->m->g;
    if (!g) return 0;
    if ((mods & EX_MOD_SHIFT) && (key == EX_KEY_LEFT || key == EX_KEY_RIGHT || key == EX_KEY_UP || key == EX_KEY_DOWN)) {   /* spatial selection */
        float dx = key == EX_KEY_LEFT ? -1.0f : key == EX_KEY_RIGHT ? 1.0f : 0.0f, dy = key == EX_KEY_UP ? -1.0f : key == EX_KEY_DOWN ? 1.0f : 0.0f;
        int n = select_dir(v, w, dx, dy);
        if (n >= 0) select_node(v, n);
        return 1;
    }
    if ((mods & EX_MOD_ALT) && v->selected >= 0 && v->m->have_xy && (size_t)v->selected < g->n &&
        (key == EX_KEY_LEFT || key == EX_KEY_RIGHT || key == EX_KEY_UP || key == EX_KEY_DOWN)) {       /* Alt+arrow: pull the selected node that way (5g) */
        float d = KEY_PULL_PX * ex_renderer_scale(ex_ui_renderer(v->ui)) / v->zoom;
        float tx = v->m->xy[2 * v->selected] + (key == EX_KEY_LEFT ? -d : key == EX_KEY_RIGHT ? d : 0.0f);
        float ty = v->m->xy[2 * v->selected + 1] + (key == EX_KEY_UP ? -d : key == EX_KEY_DOWN ? d : 0.0f);
        ex_graphmodel_drag(v->m, v->selected, tx, ty);
        v->kb_pull = 1; v->kb_until = SDL_GetTicks() + KEY_PULL_MS;
        return 1;
    }
    switch (key) {
    case EX_KEY_LEFT: v->cx -= step; v->following = 0; return 1;
    case EX_KEY_RIGHT: v->cx += step; v->following = 0; return 1;
    case EX_KEY_UP: v->cy -= step; v->following = 0; return 1;
    case EX_KEY_DOWN: v->cy += step; v->following = 0; return 1;
    case EX_KEY_PAGE_DOWN: case EX_KEY_PAGE_UP:
        if (g->n == 0) return 1;
        {
            int dir = key == EX_KEY_PAGE_DOWN ? 1 : -1, cur = v->selected, tries;
            for (tries = 0; tries < (int)g->n; tries++) {                 /* the next node that is shown */
                cur = cur < 0 ? (dir > 0 ? 0 : (int)g->n - 1) : (int)(((size_t)cur + g->n + (size_t)(dir > 0 ? 1 : g->n - 1)) % g->n);
                if (node_shown(v, (uint32_t)cur)) break;
            }
            select_node(v, cur);
        }
        return 1;
    case EX_KEY_ENTER: if (v->selected >= 0) { open_node(v, v->selected, (mods & EX_MOD_PRIMARY) ? 1 : 0); return 1; } return 0;
    case EX_KEY_ESCAPE:
        if (v->hover >= 0 && v->h.preview) { v->h.preview(v->h.user, NULL, 0, 0, 0); v->hover = -1; return 1; }
        if (v->selected >= 0) { v->selected = -1; return 1; }
        return 0;
    case EX_KEY_HOME: v->following = 1; return 1;
    default: return 0;
    }
}

static int text_hook(ExWidget *w, const char *s, size_t len, void *user) {
    View *v = user;
    float sx, sy;
    if (len != 1) return 0;
    if (s[0] == '+' || s[0] == '=') { zoom_about(v, w, 1.25f, w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    if (s[0] == '-') { zoom_about(v, w, 0.8f, w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    if (s[0] == '0') { v->following = 1; return 1; }
    if (s[0] == 'a') { ex_graphview_animation_toggle(w); return 1; }
    if ((s[0] == 'm' || s[0] == 'p') && v->selected >= 0 && v->m->g && ex_graphview_node_screen(w, v->selected, &sx, &sy)) {
        const ExGraphNode *nd = &v->m->g->nodes[v->selected];
        if (s[0] == 'm') { if (v->h.context) v->h.context(v->h.user, nd->path, nd->kind, sx, sy); }
        else if (v->h.preview) {                                       /* the page preview of the selected node: the view acts as if the pointer were on it */
            v->hover = v->selected;
            v->h.preview(v->h.user, nd->path, nd->kind, sx, sy);
        }
        return 1;
    }
    if ((s[0] == 'g' || s[0] == '[' || s[0] == ']') && v->act) return v->act(v->extra, s[0]);
    return 0;
}

ExWidget *ex_widget_graph(ExUi *ui, ExGraphModel *m, const char *name, const ExGraphHandlers *h) {
    ExWidget *w = ex_widget_box(ui, 0, name);
    View *v;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    v->ui = ui; v->m = m; v->zoom = 1.0f; v->following = 1; v->hover = v->selected = v->mark_for = v->grab = -1;
    if (h) v->h = *h;
    w->type = EX_WIDGET_GRAPH;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1;
    w->clip = 1;
    w->vertical = 1; w->align_x = 2; w->align_y = 0; w->pad_x = 1.0f; w->pad_y = 1.0f;   /* the gear and its panel (children) float at the top right */
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    w->on_text = text_hook; w->on_text_user = v;
    return w;
}

void ex_graphview_set_extra(ExWidget *w, void *extra, void (*free_extra)(void *), void (*tick)(void *, int64_t), int (*act)(void *, int)) {
    View *v = view_of(w);
    if (!v) { if (free_extra) free_extra(extra); return; }
    if (v->free_extra) v->free_extra(v->extra);
    v->extra = extra; v->free_extra = free_extra; v->tick = tick; v->act = act;
}

void ex_graphview_stats(const ExWidget *w, ExGraphViewStats *out) {
    const View *v = view_of(w);
    memset(out, 0, sizeof *out);
    if (v) *out = v->stats;
}

void ex_graphview_select(ExWidget *w, int node) { View *v = view_of(w); if (v) v->selected = node; }

const char *ex_graphview_hover_path(const ExWidget *w, int *kind) {
    const View *v = view_of(w);
    if (!v || !v->m->g || v->hover < 0 || (size_t)v->hover >= v->m->g->n) return NULL;
    if (kind) *kind = v->m->g->nodes[v->hover].kind;
    return v->m->g->nodes[v->hover].path;
}

char *ex_graphview_summary(const ExWidget *w) {
    const View *v = view_of(w);
    char *s = malloc(600);
    const ExGraph *g;
    if (!s) return NULL;
    g = v ? v->m->g : NULL;
    if (!g) { snprintf(s, 600, "Reading the links of your notes"); return s; }
    snprintf(s, 600, "%zu notes and names, %zu links.", g->n, g->ne);
    if (v->selected >= 0 && (size_t)v->selected < g->n) {
        size_t len = 0;
        const char *l = ex_graph_label(&g->nodes[v->selected], &len);
        size_t used = strlen(s);
        snprintf(s + used, 600 - used, " Selected: %.*s, %u link%s%s.", (int)(len > 200 ? 200 : len), l, g->nodes[v->selected].degree, g->nodes[v->selected].degree == 1 ? "" : "s",
                 g->nodes[v->selected].kind == EX_GRAPH_UNRESOLVED ? ", not created yet" : "");
    }
    return s;
}
