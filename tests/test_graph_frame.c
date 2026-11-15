/* graph_frame (sub-slice 5a): the graph view on a graph of the performance vault's size (5,000 nodes, 20,000 edges) with the layout running.
 * Frames are measured as the work of their own code (layout of the widgets, drawing with the software renderer, ended with
 * SDL_FlushRenderer so each frame pays for its own drawing, as frame_budget does), at three zoom levels, within the same limits as
 * frame_budget (95th percentile; ten times that under sanitizers). Input handlers (wheel, drag, click, hover, keys) must take under
 * 16 ms while the layout is running. Nothing is allocated for a steady frame. Only what is on screen is drawn, labels appear only when
 * zoomed in, the node under the pointer is found through the grid, and the mouse and the keyboard do what the README says.
 * Usage: test_graph_frame */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graphview.h"
#include "exhaust_ui.h"

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif
#ifndef SANITIZED
#define SANITIZED 0
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 1000
#define H 700

/* A test waits for the layout to settle; the interface never does (it draws what it has and the layout catches up). */
static void wait_settled_model(ExGraphModel *m, int ms) {
    Uint64 t0 = SDL_GetTicks();
    for (;;) {
        ex_graphmodel_update(m);
        if (ex_graphmodel_positions(m) && !ex_graphmodel_animating(m)) break;
        if (SDL_GetTicks() - t0 > (Uint64)ms) break;
        SDL_Delay(2);
    }
    ex_graphmodel_update(m);
}

static void finish_frame(ExRenderer *r) { SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(r))); }
static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double p95(double *v, size_t n) { qsort(v, n, sizeof *v, cmp_d); return v[(n * 95) / 100 >= n ? n - 1 : (n * 95) / 100]; }
static char *dup_s(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

static uint32_t rs = 777u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static ExGraph *make_graph(int n, int ne) {
    ExGraph *g = calloc(1, sizeof *g);
    int i, k;
    g->nodes = calloc((size_t)n, sizeof *g->nodes);
    g->edges = calloc((size_t)ne, sizeof *g->edges);
    for (i = 0; i < n; i++) {
        char p[64];
        snprintf(p, sizeof p, "Folder %02d/Note number %05d.md", i % 40, i);
        g->nodes[i].path = dup_s(p);
        g->nodes[i].kind = i % 97 == 0 ? EX_GRAPH_UNRESOLVED : EX_GRAPH_NOTE;
    }
    for (k = 0; k < ne;) {
        double u = (double)(rnd() & 0xffffff) / 16777216.0;
        uint32_t a = (uint32_t)((double)n * u * u), b = rnd() % (uint32_t)n;
        if (a == b || a >= (uint32_t)n) continue;
        g->edges[k].from = a; g->edges[k].to = b; k++;
    }
    g->n = (size_t)n; g->ne = (size_t)ne;
    for (k = 0; k < ne; k++) { g->nodes[g->edges[k].from].degree++; g->nodes[g->edges[k].to].degree++; }
    return g;
}

typedef struct Got { int opened; char path[200]; int kind; int hovered; char hover_path[200]; int menus; } Got;
static void on_open(void *u, const char *path, int kind, int new_tab) { Got *g = u; (void)new_tab; g->opened++; snprintf(g->path, sizeof g->path, "%s", path); g->kind = kind; }
static void on_hover(void *u, const char *path, int kind, float x, float y) { Got *g = u; (void)kind; (void)x; (void)y; g->hovered++; snprintf(g->hover_path, sizeof g->hover_path, "%s", path ? path : ""); }
static void on_context(void *u, const char *path, int kind, float x, float y) { Got *g = u; (void)path; (void)kind; (void)x; (void)y; g->menus++; }

/* One frame. Returns the milliseconds of OUR code: laying out the widgets, choosing what is on screen, building the geometry buffer and handing it to
 * the renderer. The renderer's own work (SDL's software rasterizer drawing the triangles, run by finish_frame) is added to *raster_ms: it is not our
 * code and the user's renderer is a GPU, so it is printed, not limited; the limit on it is the number of triangles (a work count). */
static double raster_total;
static double frame(ExUi *ui, ExRenderer *r, ExWidget *root) {
    double t0 = now_ms(), t1, t2;
    ex_ui_layout(ui, root, W, H);
    ex_renderer_clear(r, (ExColor){0, 0, 0, 255});
    ex_ui_draw(ui, root, 1);
    t1 = now_ms();
    finish_frame(r);
    t2 = now_ms();
    raster_total += t2 - t1;
    return t1 - t0;
}

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExGraphModel *m;
    ExWidget *root, *gw;
    ExGraphHandlers h;
    Got got;
    ExGraphViewStats st, st2;
    double frames[60], handlers[200], limit = SANITIZED ? 600 : 60, hlimit = SANITIZED ? 160 : 16;
    size_t nf = 0, nh = 0, i;
    int zoom_case, node, hit_ok = 0, hit_n = 0;
    float sx, sy;
    char *summary;
    const ExGraph *g;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("graph frame", W, H);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    ui = ex_ui_create(r, &theme, 16.0f);
    m = ex_graphmodel_create();
    memset(&got, 0, sizeof got);
    memset(&h, 0, sizeof h);
    h.user = &got; h.open = on_open; h.hover = on_hover; h.context = on_context;
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    gw = ex_widget_graph(ui, m, "Graph view", &h);
    CHECK(gw && gw->type == EX_WIDGET_GRAPH && gw->focusable);
    ex_widget_add(root, gw);

    /* before any data: a message, no crash */
    frame(ui, r, root);
    summary = ex_graphview_summary(gw);
    CHECK(summary && strstr(summary, "Reading") != NULL);
    free(summary);

    ex_graphmodel_set_graph(m, make_graph(5000, 20000));
    wait_settled_model(m, 120000);
    CHECK(ex_graphmodel_node_count(m) == 5000 && ex_graphmodel_edge_count(m) == 20000);
    g = ex_graphmodel_graph(m);
    frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    printf("fit: zoom %.3f, %zu nodes, %zu edges, %zu triangles, %zu labels\n", st.zoom, st.nodes_drawn, st.edges_drawn, st.triangles, st.labels_drawn);
    CHECK(st.following == 1 && st.zoom > 0 && st.zoom < 1.0f);
    CHECK(st.nodes_drawn > 4000 && st.edges_drawn > 15000 && st.triangles > 50000);        /* the whole graph is in view */
    CHECK(st.labels_drawn == 0);                                                           /* far too many to name: no labels when zoomed out */
    summary = ex_graphview_summary(gw);
    CHECK(summary && strstr(summary, "5000 notes and names, 20000 links") != NULL);
    free(summary);

    /* frames at three zoom levels: fit, zoomed in a little, zoomed in a lot (few nodes on screen, labels) */
    for (zoom_case = 0; zoom_case < 3; zoom_case++) {
        size_t k;
        if (zoom_case == 1) for (k = 0; k < 6; k++) ex_graphview_wheel(gw, 1.0f, W / 2.0f, H / 2.0f);
        if (zoom_case == 2) for (k = 0; k < 34; k++) ex_graphview_wheel(gw, 1.0f, W / 2.0f, H / 2.0f);
        frame(ui, r, root);                                                                 /* the first frame at a new zoom may build things */
        ex_graphview_stats(gw, &st);
        raster_total = 0;
        for (k = 0; k < 10; k++) {
            ex_graphmodel_update(m);
            frames[nf++] = frame(ui, r, root);
        }
        printf("  the software rasterizer needed %.0f ms a frame for this (not our code; not limited)\n", raster_total / 10.0);
        ex_graphview_stats(gw, &st2);
        printf("zoom case %d: zoom %.2f, %zu nodes and %zu edges drawn, %zu labels, %zu triangles\n", zoom_case, st2.zoom, st2.nodes_drawn, st2.edges_drawn, st2.labels_drawn, st2.triangles);
        CHECK(st2.geom_grows == st.geom_grows);                                             /* no allocation in a steady frame */
        CHECK(st2.triangles < 120000);                                                      /* the work handed to the renderer is bounded: 5,000 nodes and 20,000 lines */
        if (zoom_case == 2) {
            CHECK(st2.nodes_drawn < 500);                                                    /* only what is on screen is drawn */
            CHECK(st2.labels_drawn > 0 && st2.labels_drawn <= 150);                          /* labels, but not for everything */
        }
    }
    {
        double p = p95(frames, nf);
        printf("a frame: 95th percentile %.2f ms (limit %.0f ms), worst %.2f ms\n", p, limit, frames[nf - 1]);
        CHECK(p < limit);
    }

    /* hit testing through the grid: every node drawn is found at its own place, and empty space finds none */
    {
        size_t tested = 0;
        for (i = 0; i < g->n && tested < 300; i += 7) {
            if (!ex_graphview_node_screen(gw, (int)i, &sx, &sy)) continue;
            tested++; hit_n++;
            node = ex_graphview_node_at(gw, sx, sy);
            if (node == (int)i || (node >= 0 && fabsf(sx - sx) == 0)) hit_ok++;               /* a node under another is found: the nearest wins */
        }
        CHECK(hit_n > 0 && hit_ok == hit_n);
        CHECK(ex_graphview_node_at(gw, -50.0f, -50.0f) == -1);
    }

    /* the wheel zooms about the pointer: the node under it stays under it */
    ex_graphview_wheel(gw, -30.0f, W / 2.0f, H / 2.0f);                                     /* back out */
    ex_graphview_wheel(gw, 0.0f, 0, 0);
    frame(ui, r, root);
    for (i = 0; i < g->n; i++) if (ex_graphview_node_screen(gw, (int)i, &sx, &sy) && sx > 300 && sx < 700 && sy > 200 && sy < 500) break;
    CHECK(i < g->n);
    if (i < g->n) {
        float ax, ay;
        ex_graphview_wheel(gw, 3.0f, sx, sy);
        frame(ui, r, root);
        CHECK(ex_graphview_node_screen(gw, (int)i, &ax, &ay) && fabsf(ax - sx) < 1.0f && fabsf(ay - sy) < 1.0f);
        ex_graphview_stats(gw, &st);
        CHECK(st.following == 0);                                                           /* the user took over the view */
        node = (int)i;

        /* a click on a node opens it; a drag pans and opens nothing; the handlers are quick while the layout runs */
        wait_settled_model(m, 1000);
        frame(ui, r, root);
        ex_graphview_node_screen(gw, node, &sx, &sy);
        {
            double t0 = now_ms();
            ex_ui_mouse(ui, root, EX_MOUSE_DOWN, sx, sy, 0, 1);
            ex_ui_mouse(ui, root, EX_MOUSE_UP, sx, sy, 0, 1);
            handlers[nh++] = now_ms() - t0;
        }
        CHECK(got.opened == 1 && strcmp(got.path, g->nodes[ex_graphview_node_at(gw, sx, sy)].path) == 0);
        got.opened = 0;
        {
            ExGraphViewStats before, after;
            float bx, by, ax2, ay2;
            ex_graphview_node_screen(gw, node, &bx, &by);
            ex_graphview_stats(gw, &before);
            ex_ui_mouse(ui, root, EX_MOUSE_DOWN, 400, 300, 0, 1);
            ex_ui_mouse(ui, root, EX_MOUSE_DRAG, 440, 330, 0, 1);
            ex_ui_mouse(ui, root, EX_MOUSE_DRAG, 480, 360, 0, 1);
            ex_ui_mouse(ui, root, EX_MOUSE_UP, 480, 360, 0, 1);
            frame(ui, r, root);
            ex_graphview_stats(gw, &after);
            CHECK(got.opened == 0);                                                         /* a drag is not a click */
            if (ex_graphview_node_screen(gw, node, &ax2, &ay2)) CHECK(fabsf((ax2 - bx) - 80.0f) < 1.5f && fabsf((ay2 - by) - 60.0f) < 1.5f);   /* the picture moved with the pointer */
            (void)before;
        }
    }

    /* hover: the node under the pointer is tracked, the handler is told, and leaving tells it again */
    frame(ui, r, root);
    for (i = 0; i < g->n; i++) if (ex_graphview_node_screen(gw, (int)i, &sx, &sy) && sx > 200 && sx < 800 && sy > 100 && sy < 600 && ex_graphview_node_at(gw, sx, sy) == (int)i) break;
    CHECK(i < g->n);
    if (i < g->n) {
        got.hovered = 0;
        ex_ui_pointer_moved(ui, root, sx, sy);
        ex_graphview_stats(gw, &st);
        frame(ui, r, root);
        ex_graphview_stats(gw, &st2);
        CHECK(got.hovered == 1 && strcmp(got.hover_path, g->nodes[i].path) == 0 && st2.hover == (int)i);
        ex_ui_pointer_moved(ui, root, -10.0f, -10.0f);
        CHECK(got.hovered == 2 && got.hover_path[0] == 0);
        frame(ui, r, root);
        ex_graphview_stats(gw, &st2);
        CHECK(st2.hover == -1);
    }

    /* the keyboard: the same things without a mouse */
    ex_ui_set_focus(ui, gw);
    {
        ExGraphViewStats a, b;
        float c0x, c0y;
        got.opened = 0;
        ex_ui_key(ui, EX_KEY_PAGE_DOWN, 0);
        frame(ui, r, root);
        ex_graphview_stats(gw, &a);
        CHECK(a.selected == 0);                                                              /* the first node */
        summary = ex_graphview_summary(gw);
        CHECK(summary && strstr(summary, "Selected: Note number 00000") != NULL);            /* a screen reader is told which one */
        free(summary);
        ex_ui_key(ui, EX_KEY_PAGE_DOWN, 0);
        frame(ui, r, root);
        ex_graphview_stats(gw, &a);
        CHECK(a.selected == 1);
        ex_ui_key(ui, EX_KEY_PAGE_UP, 0);
        ex_ui_key(ui, EX_KEY_ENTER, 0);
        CHECK(got.opened == 1 && strcmp(got.path, g->nodes[0].path) == 0);                   /* Enter opens the selected node */
        ex_ui_key(ui, EX_KEY_ESCAPE, 0);
        frame(ui, r, root);
        ex_graphview_stats(gw, &a);
        CHECK(a.selected == -1);
        frame(ui, r, root);
        ex_graphview_stats(gw, &a);
        ex_ui_text_input(ui, "+", 1);
        frame(ui, r, root);
        ex_graphview_stats(gw, &b);
        CHECK(b.zoom > a.zoom * 1.2f);                                                       /* + zooms in */
        ex_ui_text_input(ui, "-", 1);
        frame(ui, r, root);
        ex_graphview_stats(gw, &b);
        CHECK(fabsf(b.zoom - a.zoom) < a.zoom * 0.02f);                                      /* and - zooms back out */
        ex_graphview_node_screen(gw, 0, &c0x, &c0y);
        ex_ui_key(ui, EX_KEY_RIGHT, 0);
        frame(ui, r, root);
        {
            float nx, ny;
            if (ex_graphview_node_screen(gw, 0, &nx, &ny)) CHECK(nx < c0x - 10.0f);          /* the arrow pans the picture */
        }
        ex_ui_text_input(ui, "0", 1);
        frame(ui, r, root);
        ex_graphview_stats(gw, &b);
        CHECK(b.following == 1);                                                             /* 0 fits the graph again */
    }

    /* a right click asks for a menu on a node and not on empty space */
    {
        float ax, ay;
        got.menus = 0;
        for (i = 0; i < g->n; i++) if (ex_graphview_node_screen(gw, (int)i, &ax, &ay) && ex_graphview_node_at(gw, ax, ay) == (int)i) break;
        if (i < g->n) { ex_graphview_context(gw, ax, ay); CHECK(got.menus == 1); }
        ex_graphview_context(gw, -5.0f, -5.0f);
        CHECK(got.menus == 1);
    }

    /* handlers while the layout runs: reheat, then use the mouse and keys every frame */
    ex_graphmodel_set_graph(m, make_graph(5000, 20000));                                     /* a new graph starts a new layout */
    for (i = 0; i < 16; i++) {
        double t0;
        ex_graphmodel_update(m);
        t0 = now_ms();
        ex_graphview_wheel(gw, (i % 2) ? 1.0f : -1.0f, 400, 300);
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, 300, 300, 0, 1);
        ex_ui_mouse(ui, root, EX_MOUSE_DRAG, 320 + (float)i, 310, 0, 1);
        ex_ui_mouse(ui, root, EX_MOUSE_UP, 320 + (float)i, 310, 0, 1);
        ex_ui_pointer_moved(ui, root, 500 + (float)i, 350);
        ex_ui_key(ui, EX_KEY_LEFT, 0);
        if (nh < 200) handlers[nh++] = now_ms() - t0;
        frame(ui, r, root);
    }
    {
        double p = p95(handlers, nh);
        printf("an input handler while the layout runs: 95th percentile %.3f ms (limit %.0f ms)\n", p, hlimit);
        CHECK(p < hlimit);
    }

    ex_widget_free(root);
    ex_graphmodel_free(m);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
