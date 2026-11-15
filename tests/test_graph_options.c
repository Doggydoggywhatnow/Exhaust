/* graph_options (sub-slice 5b): each filter, group, force and display option of the graph changes the data or the style as it should, on a
 * small vault written by the test.
 *  - Data (the service thread's job, run here directly): a search, a folder, a tag, a property, orphans, unresolved names, attachments, groups,
 *    a query that does not parse, and the degrees and edge order after nodes are removed.
 *  - Forces: the layout settles differently for each of the four forces (checked by measuring the picture), and a change wakes it.
 *  - Display: arrows, thickness, node size, the text fade threshold and the group colours change what the view draws (the colours are read
 *    back from the pixels, so a wrong colour fails).
 *  - Options: the keys of the [graph] section round trip, bad values are clamped, and the gear panel changes the model.
 * Usage: test_graph_options */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graph.h"
#include "exhaust_graphpanel.h"
#include "exhaust_graphview.h"
#include "exhaust_layout.h"
#include "exhaust_platform.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char *root, const char *rel, const char *text) {
    char path[512], dir[512], *slash;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    CHECK(ex_platform_write_file_atomic(path, text, strlen(text)) == 0);
}

static long node_of(const ExGraph *g, const char *path) {
    size_t i;
    for (i = 0; i < g->n; i++) if (!strcmp(g->nodes[i].path, path)) return (long)i;
    return -1;
}
static int has(const ExGraph *g, const char *path) { return node_of(g, path) >= 0; }
static int group_of(const ExGraph *g, const char *path) { long i = node_of(g, path); return i < 0 ? -1 : g->nodes[i].group; }

/* every edge joins two nodes, they are sorted and without repeats, and each degree is the number of edges at the node */
static void consistent(const ExGraph *g) {
    size_t i;
    uint32_t *deg = calloc(g->n + 1, sizeof *deg);
    for (i = 0; i < g->ne; i++) {
        CHECK(g->edges[i].from < g->n && g->edges[i].to < g->n);
        if (i) CHECK(g->edges[i - 1].from < g->edges[i].from || (g->edges[i - 1].from == g->edges[i].from && g->edges[i - 1].to < g->edges[i].to));
        if (g->edges[i].from < g->n) deg[g->edges[i].from]++;
        if (g->edges[i].to < g->n) deg[g->edges[i].to]++;
    }
    for (i = 0; i < g->n; i++) CHECK(g->nodes[i].degree == deg[i]);
    free(deg);
}

static ExIndex *g_r;
static const char *g_root;

/* Reads the whole graph and applies the options, as the service does. */
static char g_msg[220];
static int load(const ExGraphOptions *o, ExGraph *g, unsigned *bad) {
    memset(g, 0, sizeof *g);
    CHECK(ex_graph_load(g_r, g) == 0);
    return ex_graph_apply_options(g_r, g_root, o, g, bad, g_msg, sizeof g_msg);
}

static void data(void) {
    ExIndex *w;
    char err[200];
    ExGraphOptions o;
    ExGraph g;
    unsigned bad;
    size_t n0;
    ex_platform_remove_tree("go_vault");
    put("go_vault", "A.md", "---\nkind: person\n---\nTag #alpha. Copper and [[B]] and ![[pic.png]] and [[Missing]].\n");
    put("go_vault", "Folder/B.md", "---\nkind: place\n---\nTag #beta. Back to [[A]].\n");
    put("go_vault", "C.md", "Tag #alpha. Copper alone, no links.\n");
    put("go_vault", "D.md", "Nothing here.\n");
    put("go_vault", "pic.png", "not really a png");
    g_root = ex_platform_realpath("go_vault");
    ex_platform_remove_tree("go_db");
    w = ex_index_open("go_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, g_root, NULL, 0, NULL, NULL, NULL) == 0);
    g_r = ex_index_open("go_db/index.db", 0, err, sizeof err);
    CHECK(g_r != NULL);
    if (!w || !g_r) return;

    ex_graphopts_defaults(&o);
    CHECK(load(&o, &g, &bad) == 0 && bad == 0 && g_msg[0] == 0);                  /* defaults: nothing removed, nothing in a group, nothing to say */
    n0 = g.n;
    CHECK(n0 == 6 && has(&g, "A.md") && has(&g, "Folder/B.md") && has(&g, "C.md") && has(&g, "D.md") && has(&g, "pic.png") && has(&g, "Missing"));
    CHECK(group_of(&g, "A.md") == 0);
    consistent(&g); ex_graph_free(&g);

    snprintf(o.search, sizeof o.search, "copper");                                /* a search: the notes it finds, and what they link to */
    CHECK(load(&o, &g, &bad) == 0 && bad == 0);
    CHECK(g.n == 4 && has(&g, "A.md") && has(&g, "C.md") && has(&g, "pic.png") && has(&g, "Missing") && !has(&g, "Folder/B.md") && !has(&g, "D.md"));
    CHECK(g.ne == 2);                                                              /* A to pic and to Missing; the edges to and from B are gone */
    consistent(&g); ex_graph_free(&g);

    snprintf(o.search, sizeof o.search, "path:Folder");                           /* a folder */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(g.n == 1 && has(&g, "Folder/B.md") && g.ne == 0);
    consistent(&g); ex_graph_free(&g);

    snprintf(o.search, sizeof o.search, "tag:#alpha");                            /* a tag */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(has(&g, "A.md") && has(&g, "C.md") && !has(&g, "Folder/B.md") && !has(&g, "D.md"));
    consistent(&g); ex_graph_free(&g);

    snprintf(o.search, sizeof o.search, "[kind:place]");                          /* a property */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(g.n == 1 && has(&g, "Folder/B.md"));
    consistent(&g); ex_graph_free(&g);

    snprintf(o.search, sizeof o.search, "tag:#alpha -copper");                    /* a query with an exclusion finds none here: the graph is empty, not broken */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(g.n == 0 && g.ne == 0);
    ex_graph_free(&g);

    o.search[0] = 0;
    o.show_orphans = 0;                                                            /* orphans off: C and D go (no edges), the rest stays */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(g.n == 4 && !has(&g, "C.md") && !has(&g, "D.md") && has(&g, "A.md") && has(&g, "Folder/B.md") && has(&g, "pic.png") && has(&g, "Missing"));
    consistent(&g); ex_graph_free(&g);

    o.show_unresolved = 0;                                                         /* and no unresolved names: Missing goes, A and B and the picture stay */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(g.n == 3 && !has(&g, "Missing") && g.nodes[node_of(&g, "A.md")].degree == 3);   /* A: B twice (to and from) and the picture */
    consistent(&g); ex_graph_free(&g);

    o.show_attachments = 0;
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(g.n == 2 && !has(&g, "pic.png") && g.nodes[node_of(&g, "A.md")].degree == 2);
    consistent(&g); ex_graph_free(&g);

    ex_graphopts_defaults(&o);                                                     /* attachments off alone */
    o.show_attachments = 0;
    CHECK(load(&o, &g, &bad) == 0 && g.n == 5 && !has(&g, "pic.png") && has(&g, "Missing"));
    consistent(&g); ex_graph_free(&g);

    ex_graphopts_defaults(&o);                                                     /* groups: the first query that finds a note gives its group */
    snprintf(o.groups[0].query, sizeof o.groups[0].query, "tag:#alpha");
    snprintf(o.groups[1].query, sizeof o.groups[1].query, "path:Folder");
    snprintf(o.groups[2].query, sizeof o.groups[2].query, "copper");
    CHECK(load(&o, &g, &bad) == 0 && bad == 0 && g.n == 6);                       /* a group never removes a node */
    CHECK(group_of(&g, "A.md") == 1 && group_of(&g, "C.md") == 1);                 /* in groups 1 and 3: the lowest */
    CHECK(group_of(&g, "Folder/B.md") == 2 && group_of(&g, "D.md") == 0 && group_of(&g, "pic.png") == 0 && group_of(&g, "Missing") == 0);
    ex_graph_free(&g);

    o.groups[0].query[0] = 0;                                                      /* without group 1, copper takes A and C */
    CHECK(load(&o, &g, &bad) == 0);
    CHECK(group_of(&g, "A.md") == 3 && group_of(&g, "C.md") == 3 && group_of(&g, "Folder/B.md") == 2);
    ex_graph_free(&g);

    ex_graphopts_defaults(&o);                                                     /* a query that does not parse is ignored and reported */
    snprintf(o.search, sizeof o.search, "tag:(");
    snprintf(o.groups[1].query, sizeof o.groups[1].query, "\"unclosed");
    CHECK(load(&o, &g, &bad) == 0);
    CHECK((bad & 1u) && (bad & 4u) && g.n == 6);                                  /* search is bit 0, group 2 is bit 2: the whole graph is shown */
    CHECK(strstr(g_msg, "The search \"tag:(\" does not work: ") && strstr(g_msg, "\nGroup 2 \"") && strchr(g_msg, '\n'));   /* one line for each, saying why */
    CHECK(g_msg[0] != 0 && strlen(g_msg) > 40);
    ex_graph_free(&g);

    ex_index_close(g_r); ex_index_close(w);
}

/* ---- forces ---- */

static ExGraph *ring_graph(void) {
    ExGraph *g = calloc(1, sizeof *g);
    int n = 30, iso = 6, i;
    g->n = (size_t)(n + iso);
    g->nodes = calloc(g->n, sizeof *g->nodes);
    g->edges = calloc((size_t)n, sizeof *g->edges);
    for (i = 0; i < (int)g->n; i++) { char p[32]; snprintf(p, sizeof p, "N%02d.md", i); g->nodes[i].path = malloc(strlen(p) + 1); strcpy(g->nodes[i].path, p); g->nodes[i].kind = EX_GRAPH_NOTE; }
    for (i = 0; i < n; i++) { g->edges[i].from = (uint32_t)(i < n - 1 ? i : 0); g->edges[i].to = (uint32_t)(i < n - 1 ? i + 1 : n - 1); g->nodes[g->edges[i].from].degree++; g->nodes[g->edges[i].to].degree++; }
    g->ne = (size_t)n;
    return g;
}

static void settle(ExLayout *l) {
    Uint64 t0 = SDL_GetTicks();
    ExLayoutStats st;
    for (;;) {
        ex_layout_stats(l, &st);
        if (st.settled || SDL_GetTicks() - t0 > 20000) break;
        SDL_Delay(5);
    }
}

typedef struct Shape { double edge, iso; } Shape;     /* the mean length of a link, and the mean distance of the unlinked nodes from the middle */

static Shape shape_of(ExLayout *l, const ExGraph *g) {
    uint64_t gen;
    const float *xy = ex_layout_positions(l, &gen);
    Shape s = {0, 0};
    size_t i;
    for (i = 0; i < g->ne; i++) { uint32_t a = g->edges[i].from, b = g->edges[i].to; s.edge += hypot(xy[2 * a] - xy[2 * b], xy[2 * a + 1] - xy[2 * b + 1]); }
    s.edge /= (double)g->ne;
    for (i = 30; i < g->n; i++) s.iso += hypot(xy[2 * i], xy[2 * i + 1]);
    s.iso /= (double)(g->n - 30);
    return s;
}

static Shape run_layout(float center, float charge, float strength, float distance) {
    ExGraph *g = ring_graph();
    ExLayoutParams p;
    ExLayout *l = ex_layout_create(g, NULL, NULL, 5u);
    Shape s;
    p.center = center; p.charge = charge; p.link_strength = strength; p.link_distance = distance;
    ex_layout_set_params(l, &p);
    settle(l);
    s = shape_of(l, g);
    ex_layout_free(l);
    ex_graph_free(g); free(g);
    return s;
}

static void forces(void) {
    Shape base = run_layout(0.0f, -30.0f, 1.0f, 30.0f), far_ = run_layout(0.0f, -30.0f, 1.0f, 90.0f), push = run_layout(0.0f, -120.0f, 1.0f, 30.0f);
    Shape slack = run_layout(0.0f, -30.0f, 0.0f, 30.0f), pull = run_layout(1.0f, -30.0f, 1.0f, 30.0f);
    printf("graph_options forces: edge %.1f, link distance 90: %.1f, repel x4: %.1f, no springs: %.1f; unlinked %.1f, centre 1: %.1f\n", base.edge, far_.edge, push.edge, slack.edge, base.iso, pull.iso);
    CHECK(far_.edge > base.edge * 1.5);        /* a longer spring: longer links */
    CHECK(push.edge > base.edge * 1.2);        /* more repulsion: the ring spreads */
    CHECK(slack.edge > base.edge * 1.5);       /* springs off: nothing holds the links short */
    CHECK(pull.iso < base.iso * 0.8);          /* a pull to the middle brings the unlinked nodes in */
    {   /* a change wakes a layout that has settled */
        ExGraph *g = ring_graph();
        ExLayout *l = ex_layout_create(g, NULL, NULL, 5u);
        ExLayoutParams p;
        ExLayoutStats st;
        Uint64 t0;
        int woke = 0;
        unsigned long it0;
        settle(l);
        ex_layout_stats(l, &st);
        it0 = st.iterations;
        p.center = 0.0f; p.charge = -30.0f; p.link_strength = 1.0f; p.link_distance = 120.0f;
        ex_layout_set_params(l, &p);
        for (t0 = SDL_GetTicks(); SDL_GetTicks() - t0 < 5000 && !woke;) { ex_layout_stats(l, &st); woke = st.iterations > it0 + 5; SDL_Delay(5); }
        CHECK(woke);
        settle(l);
        ex_layout_free(l);
        ex_graph_free(g); free(g);
    }
}

/* ---- display ---- */

#define W 700
#define H 500

static char *dup_s(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

static ExGraph *small_graph(void) {
    ExGraph *g = calloc(1, sizeof *g);
    int i;
    g->n = 5; g->nodes = calloc(5, sizeof *g->nodes);
    for (i = 0; i < 5; i++) { char p[32]; snprintf(p, sizeof p, "Note %d.md", i); g->nodes[i].path = dup_s(p); g->nodes[i].kind = EX_GRAPH_NOTE; }
    g->edges = calloc(4, sizeof *g->edges);
    for (i = 0; i < 4; i++) { g->edges[i].from = (uint32_t)i; g->edges[i].to = (uint32_t)(i + 1); g->nodes[i].degree++; g->nodes[i + 1].degree++; }
    g->ne = 4;
    return g;
}

static void wait_model(ExGraphModel *m) {
    Uint64 t0 = SDL_GetTicks();
    for (;;) {
        ex_graphmodel_update(m);
        if (ex_graphmodel_positions(m) && !ex_graphmodel_animating(m)) break;
        if (SDL_GetTicks() - t0 > 20000) break;
        SDL_Delay(2);
    }
    ex_graphmodel_update(m);
}

/* 5g: the Link force slider matters over its whole range. The picture of a ring at three values of the slider (through the model, which
 * applies the curve of formats.md) has clearly different link lengths: each lower value gives links at least 20% longer. */
static double mean_edge_at(float strength) {
    ExGraphModel *m = ex_graphmodel_create();
    ExGraphOptions o = *ex_graphmodel_options(m);
    const ExGraph *g;
    const float *xy;
    double sum = 0;
    size_t i;
    o.link_strength = strength;
    ex_graphmodel_set_options(m, &o);
    ex_graphmodel_set_graph(m, ring_graph());
    wait_model(m);
    g = ex_graphmodel_graph(m); xy = ex_graphmodel_positions(m);
    for (i = 0; i < g->ne; i++) { uint32_t a2 = g->edges[i].from, b2 = g->edges[i].to; sum += hypot(xy[2 * a2] - xy[2 * b2], xy[2 * a2 + 1] - xy[2 * b2 + 1]); }
    sum /= (double)g->ne;
    ex_graphmodel_free(m);
    return sum;
}

static void link_slider(void) {
    double full = mean_edge_at(1.0f), high = mean_edge_at(0.75f), mid = mean_edge_at(0.5f), low = mean_edge_at(0.25f);
    printf("graph_options link force slider: 1 -> %.1f, 0.75 -> %.1f, 0.5 -> %.1f, 0.25 -> %.1f\n", full, high, mid, low);
    CHECK(high > full * 1.2);
    CHECK(mid > high * 1.2);
    CHECK(low > mid * 1.2);
}

static void frame(ExUi *ui, ExRenderer *r, ExWidget *root) {
    ex_ui_layout(ui, root, W, H);
    ex_renderer_clear(r, (ExColor){0, 0, 0, 255});
    ex_ui_draw(ui, root, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(r)));
}

/* The colour of one pixel of the frame just drawn. */
static int pixel(ExRenderer *r, int x, int y, uint8_t rgb[3]) {
    SDL_Rect rc = {x, y, 1, 1};
    SDL_Surface *s = SDL_RenderReadPixels(SDL_GetRenderer(ex_renderer_window(r)), &rc), *c;
    int ok = 0;
    if (!s) return 0;
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    if (c) { const uint8_t *px = c->pixels; rgb[0] = px[0]; rgb[1] = px[1]; rgb[2] = px[2]; ok = 1; SDL_DestroySurface(c); }
    SDL_DestroySurface(s);
    return ok;
}

static int near_color(const uint8_t rgb[3], ExColor c) { return abs(rgb[0] - c.r) <= 6 && abs(rgb[1] - c.g) <= 6 && abs(rgb[2] - c.b) <= 6; }

static void display(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExGraphModel *m = ex_graphmodel_create();
    ExWidget *root, *gw;
    ExGraphViewStats a, b;
    ExGraphOptions o;
    ExGraph *g = small_graph();
    uint8_t rgb[3];
    float sx, sy;
    size_t tri0, edges0;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("graph options", W, H);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    gw = ex_widget_graph(ui, m, "Graph view", NULL);
    ex_widget_add(root, gw);
    ex_graphmodel_set_graph(m, g);
    wait_model(m);

    ex_graphview_select(gw, -1);
    frame(ui, r, root);
    ex_graphview_stats(gw, &a);
    tri0 = a.triangles; edges0 = a.edges_drawn;
    CHECK(edges0 == 4 && a.nodes_drawn == 5);

    /* arrows: one more triangle for every line */
    o = *ex_graphmodel_options(m); o.arrows = 1;
    CHECK(ex_graphmodel_set_options(m, &o) == 0);                                  /* not a filter */
    frame(ui, r, root);
    ex_graphview_stats(gw, &b);
    CHECK(b.triangles == tri0 + edges0);
    o.arrows = 0; ex_graphmodel_set_options(m, &o);
    frame(ui, r, root);
    ex_graphview_stats(gw, &b);
    CHECK(b.triangles == tri0);

    /* with a node hovered the lines at it get their heads too (they are drawn in a pass of their own): one more triangle for every line, in both directions */
    {
        float hx, hy;
        size_t off_tri, on_tri;
        CHECK(ex_graphview_node_screen(gw, 2, &hx, &hy));
        ex_graphview_hover(gw, hx, hy, 1);
        frame(ui, r, root);
        ex_graphview_stats(gw, &b);
        CHECK(b.hover == 2);
        off_tri = b.triangles;
        o.arrows = 1; ex_graphmodel_set_options(m, &o);
        frame(ui, r, root);
        ex_graphview_stats(gw, &b);
        on_tri = b.triangles;
        CHECK(on_tri == off_tri + b.edges_drawn);                                  /* hovered lines (4 of the 4: node 2 touches two, the others are the rest) all have one */
        o.arrows = 0; ex_graphmodel_set_options(m, &o);
        ex_graphview_hover(gw, -50.0f, -50.0f, 0);
        frame(ui, r, root);
    }

    /* thickness, node size, text fade */
    o.link_thickness = 3.0f; o.node_size = 2.0f; o.text_fade = -2.0f;
    ex_graphmodel_set_options(m, &o);
    frame(ui, r, root);
    ex_graphview_stats(gw, &b);
    CHECK(fabsf(b.line_px / a.line_px - 3.0f) < 0.01f);
    CHECK(fabsf(b.node_px / a.node_px - 2.0f) < 0.01f);
    CHECK(fabsf(b.label_zoom / a.label_zoom - 0.25f) < 0.01f);
    {   /* with a low threshold the labels are there at once; with a high one, not */
        size_t low, high;
        o.text_fade = -3.0f; ex_graphmodel_set_options(m, &o); frame(ui, r, root); ex_graphview_stats(gw, &b); low = b.labels_drawn;
        o.text_fade = 3.0f; ex_graphmodel_set_options(m, &o); frame(ui, r, root); ex_graphview_stats(gw, &b); high = b.labels_drawn;
        CHECK(low > 0 && high == 0);
    }
    {   /* values out of range are clamped */
        ExGraphOptions bad = o;
        bad.node_size = 99.0f; bad.link_thickness = -4.0f; bad.repel = 1000.0f; bad.text_fade = 50.0f; bad.center = -1.0f;
        ex_graphmodel_set_options(m, &bad);
        CHECK(ex_graphmodel_options(m)->node_size == 3.0f && ex_graphmodel_options(m)->link_thickness == 0.3f && ex_graphmodel_options(m)->repel == 20.0f);
        CHECK(ex_graphmodel_options(m)->text_fade == 3.0f && ex_graphmodel_options(m)->center == 0.0f);
    }

    /* group colours: a grouped node is drawn in its slot's colour, the rest in the node colour */
    ex_graphopts_defaults(&o);
    o.node_size = 3.0f;                                                            /* big circles, so the middle pixel is solid */
    ex_graphmodel_set_options(m, &o);
    g = (ExGraph *)ex_graphmodel_graph(m);
    g->nodes[1].group = 1; g->nodes[2].group = 3;                                  /* groups 1 and 3 (slots 0 and 2: accent, warning) */
    frame(ui, r, root);
    CHECK(ex_graphview_node_screen(gw, 1, &sx, &sy) && pixel(r, (int)sx, (int)sy, rgb) && near_color(rgb, ex_theme_color(&theme, EX_COLOR_ACCENT)));
    CHECK(ex_graphview_node_screen(gw, 2, &sx, &sy) && pixel(r, (int)sx, (int)sy, rgb) && near_color(rgb, ex_theme_color(&theme, EX_COLOR_WARNING)));
    CHECK(ex_graphview_node_screen(gw, 3, &sx, &sy) && pixel(r, (int)sx, (int)sy, rgb) && near_color(rgb, ex_theme_color(&theme, EX_COLOR_GRAPH_NODE)));
    o.groups[2].color = 3;                                                         /* the colour of group 3 is changed to slot 3 (error) */
    ex_graphmodel_set_options(m, &o);
    frame(ui, r, root);
    CHECK(ex_graphview_node_screen(gw, 2, &sx, &sy) && pixel(r, (int)sx, (int)sy, rgb) && near_color(rgb, ex_theme_color(&theme, EX_COLOR_ERROR)));
    {   /* the filters are what the caller must reload for: a changed query says so, a changed colour or force does not */
        ExGraphOptions f = o;
        snprintf(f.groups[0].query, sizeof f.groups[0].query, "tag:#x");
        CHECK(ex_graphmodel_set_options(m, &f) == 1);
        f.groups[0].color = 4;
        CHECK(ex_graphmodel_set_options(m, &f) == 0);
        f.repel = 3.0f;
        CHECK(ex_graphmodel_set_options(m, &f) == 0);
        f.show_orphans = 0;
        CHECK(ex_graphmodel_set_options(m, &f) == 1);
    }
    ex_widget_free(root);
    ex_graphmodel_free(m);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
}

/* ---- the keys, and the gear panel ---- */

typedef struct Kv { char k[40][48], v[40][220]; int n; } Kv;
static void kv_set(void *u, const char *key, const char *value) { Kv *kv = u; snprintf(kv->k[kv->n], 48, "%s", key); snprintf(kv->v[kv->n], 220, "%s", value); kv->n++; }
static const char *kv_get(void *u, const char *key) { Kv *kv = u; int i; for (i = 0; i < kv->n; i++) if (!strcmp(kv->k[i], key)) return kv->v[i]; return NULL; }

static int g_changes, g_filter_changes;
static void on_changed(void *u, const ExGraphOptions *o, int filters) { (void)u; (void)o; g_changes++; g_filter_changes += filters; }

static void keys_and_panel(void) {
    ExGraphOptions o, p;
    Kv *kv = calloc(1, sizeof *kv);
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExGraphModel *m = ex_graphmodel_create();
    ExWidget *root, *gw, *s, *t;
    ExGraphPanel *panel;
    ex_graphopts_defaults(&o);
    snprintf(o.search, sizeof o.search, "tag:#a OR path:Folder");
    o.show_orphans = 0; o.show_attachments = 0; o.arrows = 1;
    snprintf(o.groups[2].query, sizeof o.groups[2].query, "path:x"); o.groups[2].color = 5;
    o.center = 0.5f; o.repel = 12.5f; o.link_strength = 0.25f; o.link_distance = 55.0f; o.text_fade = -1.5f; o.node_size = 1.3f; o.link_thickness = 2.1f;
    ex_graphopts_save(&o, kv_set, kv);
    ex_graphopts_load(&p, kv_get, kv);
    CHECK(ex_graphopts_equal(&o, &p));
    kv->n = 0;                                                                     /* nothing saved: the defaults */
    ex_graphopts_load(&p, kv_get, kv);
    ex_graphopts_defaults(&o);
    CHECK(ex_graphopts_equal(&o, &p) && p.show_unresolved && p.repel == 10.0f);
    snprintf(kv->k[0], 48, "repel_force"); snprintf(kv->v[0], 220, "banana"); snprintf(kv->k[1], 48, "node_size"); snprintf(kv->v[1], 220, "50"); snprintf(kv->k[2], 48, "group_1_color"); snprintf(kv->v[2], 220, "9"); kv->n = 3;
    ex_graphopts_load(&p, kv_get, kv);                                             /* a bad number keeps the default; one out of range is clamped */
    CHECK(p.repel == 10.0f && p.node_size == 3.0f && p.groups[0].color == 0);
    free(kv);

    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("graph options panel", W, H);
    if (!r) { failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    gw = ex_widget_graph(ui, m, "Graph view", NULL);
    ex_widget_add(root, gw);
    panel = ex_graphpanel_create(ui, gw, m, 0, on_changed, NULL);
    CHECK(panel != NULL);
    if (!panel) return;
    CHECK(!ex_graphpanel_is_open(panel) && ex_graphpanel_box(panel)->hidden);
    ex_graphpanel_open(panel, 1);
    frame(ui, r, root);
    CHECK(ex_graphpanel_gear(panel)->w > 0 && ex_graphpanel_gear(panel)->x > (float)W / 2);       /* the gear floats at the right */
    CHECK(ex_graphpanel_gear(panel)->y < 80);                                                      /* near the top */
    t = ex_graphpanel_find(panel, "Forces");                                                       /* the sections start collapsed */
    CHECK(t && strstr(t->name, "collapsed") && ex_graphpanel_find(panel, "Repel force")->w == 0.0f);
    if (t) { t->on_click(t, t->on_click_user); CHECK(strstr(t->name, "expanded")); }
    frame(ui, r, root);
    s = ex_graphpanel_find(panel, "Repel force");
    CHECK(s && s->name && strstr(s->name, "10") && s->w > 0.0f);                                                  /* the name carries the value */
    if (s) {
        float v0 = ex_slider_value(s);
        CHECK(s->focusable);
        ex_ui_set_focus(ui, s);
        ex_ui_key(ui, EX_KEY_RIGHT, 0);                                                      /* the keyboard route: one step */
        CHECK(ex_slider_value(s) > v0 && ex_graphmodel_options(m)->repel == ex_slider_value(s));
        CHECK(strstr(s->name, "10.5") != NULL && g_changes >= 1);
        ex_ui_key(ui, EX_KEY_END, 0);
        CHECK(ex_slider_value(s) == 20.0f && ex_graphmodel_options(m)->repel == 20.0f);
        ex_ui_key(ui, EX_KEY_HOME, 0);
        CHECK(ex_slider_value(s) == 0.0f);
        frame(ui, r, root);
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, s->x + s->w * 0.5f, s->y + s->h * 0.5f, 0, 1);       /* the mouse: a press at the middle */
        ex_ui_mouse(ui, root, EX_MOUSE_UP, s->x + s->w * 0.5f, s->y + s->h * 0.5f, 0, 1);
        CHECK(fabsf(ex_slider_value(s) - 10.0f) <= 1.0f);
    }
    t = ex_graphpanel_find(panel, "Arrows");
    CHECK(t && t->on_click);
    if (t) { t->on_click(t, t->on_click_user); CHECK(ex_graphmodel_options(m)->arrows == 1 && strstr(t->name, "on")); g_filter_changes = 0; }
    t = ex_graphpanel_find(panel, "Orphans");
    if (t) { t->on_click(t, t->on_click_user); CHECK(ex_graphmodel_options(m)->show_orphans == 0 && g_filter_changes == 1); }      /* a filter says so */
    {   /* options changed from elsewhere (a restored session) reach the controls */
        ExGraphOptions e = *ex_graphmodel_options(m);
        e.node_size = 2.5f; e.link_distance = 80.0f;
        ex_graphmodel_set_options(m, &e);
        ex_graphpanel_tick(panel, 0);
        s = ex_graphpanel_find(panel, "Node size");
        CHECK(s && ex_slider_value(s) == 2.5f);
        s = ex_graphpanel_find(panel, "Link distance");
        CHECK(s && ex_slider_value(s) == 80.0f);
    }
    ex_widget_free(root);
    ex_graphmodel_free(m);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
}

int main(void) {
    data();
    forces();
    link_slider();
    display();
    keys_and_panel();
    if (failures) { fprintf(stderr, "graph_options: %d failure(s)\n", failures); return 1; }
    printf("graph_options: ok\n");
    return 0;
}
