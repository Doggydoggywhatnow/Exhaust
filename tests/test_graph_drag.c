/* graph_drag (sub-slice 5g): dragging nodes in the graph, the keyboard pull, and the deep zoom with growing labels.
 *  - A press on a node and a move drags it: the node goes towards the pointer (in layout units, the same amount the pointer moved), its
 *    neighbours follow through their links, and the layout is held warm while it is dragged. It is pulled, not pinned: the links hold a
 *    node that is dragged far from them back.
 *  - Let go: a node dragged a short way along its link's circle settles near where it was dropped; one dragged far settles back towards its
 *    links.
 *  - A press that does not move is a click (it opens the note); a press on empty space pans and drags nothing.
 *  - The handlers never wait for the layout: the mean time of a drag event is far under a frame.
 *  - Alt+arrow pulls the selected node for a moment, then lets go.
 *  - At the deepest zoom the labels and circles are larger than where labels first appear, and the labels follow the text size option. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graph.h"
#include "exhaust_graphview.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 800
#define H 600

static char *dup_s(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

/* A hub with eight leaves and a chain of three off the first leaf: node 0 is the hub, 1 to 8 the leaves, 9 to 11 the chain. */
static ExGraph *build(void) {
    ExGraph *g = calloc(1, sizeof *g);
    size_t i, ne = 0;
    g->n = 12;
    g->nodes = calloc(g->n, sizeof *g->nodes);
    g->edges = calloc(12, sizeof *g->edges);
    for (i = 0; i < g->n; i++) {
        char p[24];
        snprintf(p, sizeof p, i == 0 ? "Hub.md" : "N%zu.md", i);
        g->nodes[i].path = dup_s(p);
        g->nodes[i].kind = EX_GRAPH_NOTE;
        g->nodes[i].ctime = (int64_t)(1000 + 10 * (long)i);
    }
    for (i = 1; i <= 8; i++) { g->edges[ne].from = 0; g->edges[ne].to = (uint32_t)i; ne++; }
    g->edges[ne].from = 1; g->edges[ne].to = 9; ne++;
    g->edges[ne].from = 9; g->edges[ne].to = 10; ne++;
    g->edges[ne].from = 10; g->edges[ne].to = 11; ne++;
    g->ne = ne;
    for (i = 0; i < ne; i++) { g->nodes[g->edges[i].from].degree++; g->nodes[g->edges[i].to].degree++; }
    return g;
}

typedef struct Got { int opens; char path[80]; } Got;
static void on_open(void *u, const char *path, int kind, int new_tab) { Got *g = u; (void)kind; (void)new_tab; g->opens++; snprintf(g->path, sizeof g->path, "%s", path); }

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *GW;
static ExGraphModel *M;

static void frame(void) {
    ex_graphmodel_update(M);
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}
static void pump(int ms) { Uint64 t0 = SDL_GetTicks(); while (SDL_GetTicks() - t0 < (Uint64)ms) { frame(); SDL_Delay(4); } }
static void settle(int timeout_ms) {
    Uint64 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < (Uint64)timeout_ms) { frame(); if (!ex_graphmodel_animating(M)) break; SDL_Delay(4); }
    frame();
}
static void pos(int node, float *x, float *y) { const float *xy = ex_graphmodel_positions(M); *x = xy[2 * node]; *y = xy[2 * node + 1]; }
static double dist(int a, int b) { float ax, ay, bx, by; pos(a, &ax, &ay); pos(b, &bx, &by); return hypot((double)ax - bx, (double)ay - by); }
static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

int main(void) {
    ExTheme theme;
    ExGraphHandlers h;
    ExGraphViewStats st;
    Got got;
    ExGraph *g = build();
    float sx, sy, px0, py0, hx, hy, zoom;
    int leaf = 3;
    memset(&got, 0, sizeof got);
    memset(&h, 0, sizeof h);
    h.user = &got; h.open = on_open;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("graph drag", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    M = ex_graphmodel_create();
    GW = ex_widget_graph(UI, M, "Graph view", &h);
    ex_widget_add(ROOT, GW);
    ex_graphmodel_set_graph(M, g);
    settle(20000);
    CHECK(ex_graphmodel_positions(M) != NULL);
    frame();

    /* ---- a press that does not move is a click; a press on empty space pans and pulls nothing ---- */
    CHECK(ex_graphview_node_screen(GW, leaf, &sx, &sy));
    ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, sx, sy, 0, 1);
    ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, sx, sy, 0, 1);
    CHECK(got.opens == 1 && !strcmp(got.path, "N3.md"));
    {
        float bx, by, ax, ay, ex_, ey;
        int empty = 0;
        ex_graphview_node_screen(GW, leaf, &bx, &by);
        for (ex_ = 30; ex_ < W - 30 && !empty; ex_ += 37) for (ey = 30; ey < H - 30 && !empty; ey += 29) if (ex_graphview_node_at(GW, ex_, ey) < 0) { sx = ex_; sy = ey; empty = 1; }
        CHECK(empty);
        got.opens = 0;
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, sx, sy, 0, 1);
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, sx + 20, sy + 10, 0, 1);
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, sx + 40, sy + 20, 0, 1);
        frame();
        ex_graphview_stats(GW, &st);
        CHECK(st.dragging == 0);                                  /* nothing is being dragged: the view pans */
        ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, sx + 40, sy + 20, 0, 1);
        frame();
        CHECK(got.opens == 0);
        CHECK(ex_graphview_node_screen(GW, leaf, &ax, &ay) && fabsf((ax - bx) - 40.0f) < 1.5f && fabsf((ay - by) - 20.0f) < 1.5f);
    }
    ex_graphview_stats(GW, &st);
    zoom = st.zoom;
    CHECK(zoom > 0.0f);

    /* ---- a short drag, along the circle round the hub: the node goes with the pointer and settles near where it was dropped ---- */
    {
        double d_hub0, mv, fin_err, held;
        float rx, ry, tx, ux, uy, fx, fy, relx, rely;
        double t_start, per_call;
        int steps, k, ncalls = 0;
        frame();
        pos(leaf, &px0, &py0); pos(0, &hx, &hy);
        d_hub0 = dist(leaf, 0);
        rx = px0 - hx; ry = py0 - hy;                                /* hub to leaf */
        ux = -ry / (float)d_hub0; uy = rx / (float)d_hub0;           /* along the circle */
        CHECK(ex_graphview_node_screen(GW, leaf, &sx, &sy));
        tx = 25.0f;                                                  /* layout units to drag */
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, sx, sy, 0, 1);
        steps = 10;
        t_start = now_ms();
        for (k = 1; k <= steps; k++) {
            ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, sx + ux * tx * zoom * (float)k / (float)steps, sy + uy * tx * zoom * (float)k / (float)steps, 0, 1);
            ncalls++;
        }
        per_call = (now_ms() - t_start) / (double)ncalls;
        CHECK(per_call < 3.0);                                      /* the mean drag event is far under a frame: it never waits for the layout */
        pump(500);
        ex_graphview_stats(GW, &st);
        CHECK(st.dragging == 1);
        CHECK(ex_graphmodel_animating(M) && ex_graphmodel_temperature(M) >= 0.2f);      /* held warm while dragged */
        pos(leaf, &rx, &ry);
        mv = ((double)rx - px0) * ux + ((double)ry - py0) * uy;
        CHECK(mv > 0.5 * tx && mv < 1.1 * tx);                       /* it went most of the way to the pointer, along the drag */
        relx = rx; rely = ry;
        ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, sx + ux * tx * zoom, sy + uy * tx * zoom, 0, 1);
        frame();
        ex_graphview_stats(GW, &st);
        CHECK(st.dragging == 0);
        settle(20000);
        CHECK(!ex_graphmodel_animating(M));
        pos(leaf, &fx, &fy);
        fin_err = hypot((double)fx - relx, (double)fy - rely);
        held = fabs(dist(leaf, 0) - d_hub0);
        printf("graph_drag short: dragged %.1f of %.1f units, moved %.1f from where it was let go; hub distance %.1f -> %.1f\n", mv, (double)tx, fin_err, d_hub0, dist(leaf, 0));
        CHECK(fin_err < 15.0);                                        /* a place the forces accept: it stays near where it was let go */
        CHECK(held < 0.5 * d_hub0 + 20.0);
        pos(leaf, &px0, &py0);
    }

    /* ---- a long drag away from the links: the neighbours follow, and after it is let go the node is pulled back ---- */
    {
        double d_rel, d_fin, d0 = dist(leaf, 0), hub_moved;
        float rx, ry, ux, uy, hx0, hy0, hx1, hy1, a, b;
        double far_units = 300.0;
        pos(leaf, &a, &b); pos(0, &hx0, &hy0);
        rx = a - hx0; ry = b - hy0;
        ux = rx / (float)d0; uy = ry / (float)d0;                    /* straight out from the hub */
        pos(0, &hx0, &hy0);
        CHECK(ex_graphview_node_screen(GW, leaf, &sx, &sy));
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, sx, sy, 0, 1);
        { int k; for (k = 1; k <= 20; k++) ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, sx + ux * (float)far_units * zoom * (float)k / 20.0f, sy + uy * (float)far_units * zoom * (float)k / 20.0f, 0, 1); }
        pump(1500);
        d_rel = dist(leaf, 0);
        pos(0, &hx1, &hy1);
        hub_moved = hypot((double)hx1 - hx0, (double)hy1 - hy0);
        CHECK(d_rel > 2.0 * d0);                                     /* it was pulled a good way out ... */
        CHECK(d_rel < 0.9 * far_units);                              /* ... but not all the way to the pointer: its link holds it */
        CHECK(hub_moved > 1.0);                                      /* the hub, its neighbour, follows through the link */
        ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, sx + ux * (float)far_units * zoom, sy + uy * (float)far_units * zoom, 0, 1);
        settle(20000);
        d_fin = dist(leaf, 0);
        printf("graph_drag far: link length %.1f, held out at %.1f, hub followed by %.1f, after it was let go %.1f\n", d0, d_rel, hub_moved, d_fin);
        CHECK(d_fin < 0.5 * d_rel);                                  /* pulled back into its cluster */
    }

    /* ---- Alt+arrow pulls the selected node for a moment and lets go ---- */
    {
        float a, b, c, d;
        (void)b; (void)d;
        ex_ui_set_focus(UI, GW);
        ex_graphview_select(GW, 5);
        frame();
        pos(5, &a, &b);
        ex_ui_key(UI, EX_KEY_RIGHT, EX_MOD_ALT);
        frame();
        ex_graphview_stats(GW, &st);
        CHECK(st.dragging == 1);
        pump(250);
        pos(5, &c, &d);
        CHECK(c - a > 0.5f);                                        /* it moved to the right */
        pump(800);
        ex_graphview_stats(GW, &st);
        CHECK(st.dragging == 0);                                    /* the pull ended by itself */
        settle(20000);
    }

    /* ---- deep zoom: circles and labels grow; the label size option scales the labels ---- */
    {
        ExGraphViewStats lo, hi;
        ExGraphOptions o = *ex_graphmodel_options(M), o2;
        float cx = W / 2.0f, cy = H / 2.0f;
        int k;
        ex_graphview_select(GW, -1);
        ex_graphview_wheel(GW, -200.0f, cx, cy);                   /* far out first */
        frame();
        ex_graphview_wheel(GW, 200.0f, cx, cy);                    /* and in, as far as it goes */
        for (k = 0; k < 8; k++) ex_graphview_wheel(GW, 10.0f, cx, cy);
        frame();
        ex_graphview_stats(GW, &hi);
        CHECK(hi.zoom >= 100.0f);                                  /* well past 5a's limit of 8 */
        /* a zoom where labels have just appeared against the deepest */
        {
            ExGraphViewStats at_first;
            float lz = hi.label_zoom, target = lz * 1.6f, cur = hi.zoom;
            int guard = 0;
            while (cur > target && guard++ < 400) { ex_graphview_wheel(GW, -1.0f, cx, cy); frame(); ex_graphview_stats(GW, &at_first); cur = at_first.zoom; }
            ex_graphview_stats(GW, &at_first);
            printf("graph_drag zoom: label %.1f px at zoom %.2f, %.1f px at zoom %.0f; circle %.1f -> %.1f px\n", (double)at_first.label_px, (double)at_first.zoom, (double)hi.label_px, (double)hi.zoom, (double)at_first.node_px, (double)hi.node_px);
            CHECK(hi.label_px > at_first.label_px * 2.0f);          /* the labels grow past the point where they appear */
            CHECK(hi.node_px > at_first.node_px * 2.0f);            /* and so do the circles */
            CHECK(hi.label_px <= 8.0f * 16.0f);                     /* capped: a label never fills the pane */
            o2 = o; o2.text_size = 2.0f;                            /* the label size option scales them (here, short of the cap) */
            ex_graphmodel_set_options(M, &o2);
            frame();
            ex_graphview_stats(GW, &lo);
            CHECK(lo.label_px > at_first.label_px * 1.8f && lo.label_px < at_first.label_px * 2.2f);
            o2.text_size = 0.5f;
            ex_graphmodel_set_options(M, &o2);
            frame();
            ex_graphview_stats(GW, &lo);
            CHECK(lo.label_px > at_first.label_px * 0.4f && lo.label_px < at_first.label_px * 0.6f);
        }
        ex_graphmodel_set_options(M, &o);
        ex_graphview_stats(GW, &lo);
    }

    ex_ui_free(UI); ex_theme_free(&theme); ex_renderer_destroy(R);
    ex_graphmodel_free(M);
    if (failures) { fprintf(stderr, "graph_drag: %d failure(s)\n", failures); return 1; }
    printf("graph_drag: ok\n");
    return 0;
}
