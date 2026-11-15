/* graph_keyboard (sub-slice 5c): every action of the graph works from the keyboard, the animation shows the notes in the order they were
 * created, and the local graph holds the notes within the depth asked for.
 *  - Local graph (ex_graph_local): depth 1 and 2 give the expected neighbours, a link counts in either direction, an unresolved name that
 *    looks like the centre is not the centre, a centre that is not in the graph gives an empty graph, degrees and edge order stay right.
 *  - Keys: Shift+arrows select the nearest node in a direction, Page Down and Page Up step through the nodes, Enter opens, Ctrl+Enter opens
 *    in a new tab, m asks for the menu at the node, p for the page preview (Escape closes it), a starts the animation, g opens the gear
 *    panel, [ and ] change the depth of a local graph, + - 0 zoom and fit, the arrows pan, Escape clears; Tab reaches the gear and the wand
 *    and Enter presses them.
 *  - Animation: with the animation at 45% of its length exactly the four oldest of eleven nodes are drawn, the oldest is there to hit and
 *    the newest is not; at the end everything is drawn and the animation has stopped.
 * Usage: test_graph_keyboard */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graph.h"
#include "exhaust_graphpanel.h"
#include "exhaust_graphview.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *dup_s(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

/* A graph from a list of names and links: "A>B" is a link from A to B. A name that starts with "?" is an unresolved one. */
static ExGraph *build(const char *const *names, size_t n, const char *const *links, size_t nl) {
    ExGraph *g = calloc(1, sizeof *g);
    size_t i, k, ne = 0;
    g->nodes = calloc(n, sizeof *g->nodes);
    g->edges = calloc(nl + 1, sizeof *g->edges);
    for (i = 0; i < n; i++) {
        g->nodes[i].kind = names[i][0] == '?' ? EX_GRAPH_UNRESOLVED : EX_GRAPH_NOTE;
        g->nodes[i].path = dup_s(names[i][0] == '?' ? names[i] + 1 : names[i]);
        g->nodes[i].ctime = (int64_t)(1000 + 10 * (long)i);
    }
    g->n = n;
    for (k = 0; k < nl; k++) {
        size_t a = 0, b = 0;
        char fa[40], fb[40];
        sscanf(links[k], "%39[^>]>%39s", fa, fb);
        for (i = 0; i < n; i++) { if (!strcmp(g->nodes[i].path, fa)) a = i; if (!strcmp(g->nodes[i].path, fb)) b = i; }
        g->edges[ne].from = (uint32_t)a; g->edges[ne].to = (uint32_t)b; ne++;
    }
    /* sorted by (from, to) */
    for (i = 1; i < ne; i++) { ExGraphEdge e = g->edges[i]; size_t j = i; while (j > 0 && (g->edges[j - 1].from > e.from || (g->edges[j - 1].from == e.from && g->edges[j - 1].to > e.to))) { g->edges[j] = g->edges[j - 1]; j--; } g->edges[j] = e; }
    g->ne = ne;
    for (i = 0; i < ne; i++) { g->nodes[g->edges[i].from].degree++; g->nodes[g->edges[i].to].degree++; }
    return g;
}

static int has(const ExGraph *g, const char *path) { size_t i; for (i = 0; i < g->n; i++) if (!strcmp(g->nodes[i].path, path)) return 1; return 0; }

static void local(void) {
    static const char *names[] = {"A", "B", "C", "D", "E", "F", "?G"};
    static const char *links[] = {"A>B", "B>C", "D>C", "C>E", "A>G", "F>F"};
    ExGraph *g;
    size_t i;
    g = build(names, 7, links, 6);
    CHECK(ex_graph_local(g, "A", 1) == 0);                          /* depth 1 from A: B and the unresolved G */
    CHECK(g->n == 3 && has(g, "A") && has(g, "B") && has(g, "G") && g->ne == 2);
    ex_graph_free(g); free(g);
    g = build(names, 7, links, 6);
    CHECK(ex_graph_local(g, "A", 2) == 0);                          /* depth 2: and C (through B) */
    CHECK(g->n == 4 && has(g, "C") && !has(g, "D") && !has(g, "E") && g->ne == 3);
    ex_graph_free(g); free(g);
    g = build(names, 7, links, 6);
    CHECK(ex_graph_local(g, "C", 1) == 0);                          /* a link counts either way: B and D point at C, C points at E */
    CHECK(g->n == 4 && has(g, "B") && has(g, "D") && has(g, "E") && !has(g, "A"));
    for (i = 1; i < g->ne; i++) CHECK(g->edges[i - 1].from < g->edges[i].from || (g->edges[i - 1].from == g->edges[i].from && g->edges[i - 1].to < g->edges[i].to));
    for (i = 0; i < g->n; i++) if (!strcmp(g->nodes[i].path, "C")) CHECK(g->nodes[i].degree == 3);
    ex_graph_free(g); free(g);
    g = build(names, 7, links, 6);
    CHECK(ex_graph_local(g, "C", 5) == 0);                          /* a deep reach takes the whole component: not F (only itself) */
    CHECK(g->n == 6 && !has(g, "F"));
    ex_graph_free(g); free(g);
    g = build(names, 7, links, 6);
    CHECK(ex_graph_local(g, "G", 3) == 1 && g->n == 0 && g->ne == 0);   /* G is an unresolved name, not a note: there is no centre */
    ex_graph_free(g); free(g);
    g = build(names, 7, links, 6);
    CHECK(ex_graph_local(g, "", 2) == 1 && g->n == 0);              /* no note is open */
    ex_graph_free(g); free(g);
}

/* ---- the keys ---- */

#define W 800
#define H 600

typedef struct Got { int opens, new_tabs, menus, previews, preview_closes, changes; char open_path[80], menu_path[80], prev_path[80]; int depth; } Got;
static void on_open(void *u, const char *path, int kind, int new_tab) { Got *g = u; (void)kind; g->opens++; g->new_tabs += new_tab; snprintf(g->open_path, sizeof g->open_path, "%s", path); }
static void on_context(void *u, const char *path, int kind, float x, float y) { Got *g = u; (void)kind; (void)x; (void)y; g->menus++; snprintf(g->menu_path, sizeof g->menu_path, "%s", path); }
static void on_preview(void *u, const char *path, int kind, float x, float y) { Got *g = u; (void)kind; (void)x; (void)y; if (path) { g->previews++; snprintf(g->prev_path, sizeof g->prev_path, "%s", path); } else g->preview_closes++; }
static void on_changed(void *u, const ExGraphOptions *o, int filters) { Got *g = u; (void)filters; g->changes++; g->depth = o->local_depth; }

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

static void frame(ExUi *ui, ExRenderer *r, ExWidget *root) {
    ex_ui_layout(ui, root, W, H);
    ex_renderer_clear(r, (ExColor){0, 0, 0, 255});
    ex_ui_draw(ui, root, 1);
    ex_ui_layout(ui, root, W, H);                                    /* the panel sizes itself while drawing */
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(r)));
}

static void key(ExUi *ui, ExKey k, int mods) { ex_ui_key(ui, k, mods); }
static void type(ExUi *ui, const char *s) { ex_ui_text_input(ui, s, strlen(s)); }

static void keys(void) {
    static const char *names[] = {"N0", "N1", "N2", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "?Missing"};
    static const char *links[] = {"N0>N1", "N1>N2", "N2>N3", "N3>N4", "N4>N5", "N5>N6", "N6>N7", "N7>N8", "N8>N9", "N0>N5", "N2>N7", "N9>Missing"};
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExGraphModel *m = ex_graphmodel_create();
    ExWidget *root, *gw, *t;
    ExGraphPanel *panel;
    ExGraphHandlers h;
    ExGraphViewStats st;
    Got got;
    float sx, sy, px, py;
    int first, sel;
    ExGraph *g = build(names, 11, links, 12);
    size_t i;
    memset(&got, 0, sizeof got);
    memset(&h, 0, sizeof h);
    h.user = &got; h.open = on_open; h.context = on_context; h.preview = on_preview;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("graph keyboard", W, H);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    gw = ex_widget_graph(ui, m, "Graph view", &h);
    ex_widget_add(root, gw);
    ex_graphmodel_set_local(m, 1);
    panel = ex_graphpanel_create(ui, gw, m, 1, on_changed, &got);
    CHECK(panel != NULL);
    ex_graphmodel_set_graph(m, g);
    wait_model(m);
    frame(ui, r, root);
    ex_ui_set_focus(ui, gw);

    /* selecting: Page Down from nothing is the first node, Page Up from the first the last */
    key(ui, EX_KEY_PAGE_DOWN, 0); frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.selected == 0);
    key(ui, EX_KEY_PAGE_UP, 0); frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.selected == 10);
    key(ui, EX_KEY_PAGE_DOWN, 0); key(ui, EX_KEY_PAGE_DOWN, 0); frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.selected == 1);
    key(ui, EX_KEY_ESCAPE, 0); frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.selected == -1);

    /* Shift+arrows: from the selected node to the nearest in that direction, which is then to the right, left, above or below it */
    key(ui, EX_KEY_PAGE_DOWN, 0); frame(ui, r, root);
    for (i = 0; i < 4; i++) {
        static const ExKey dirs[4] = {EX_KEY_RIGHT, EX_KEY_LEFT, EX_KEY_DOWN, EX_KEY_UP};
        static const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
        int hops;
        for (hops = 0; hops < 12; hops++) {                           /* wherever the layout put them, some node has a neighbour that way; try from each in turn */
            ExGraphViewStats s0;
            ex_graphview_stats(gw, &s0);
            { const float *xy = ex_graphmodel_positions(m); px = xy[2 * s0.selected]; py = xy[2 * s0.selected + 1]; }          /* in layout units: the view moves with the selection */
            key(ui, dirs[i], EX_MOD_SHIFT); frame(ui, r, root);
            ex_graphview_stats(gw, &st);
            if (st.selected != s0.selected) {
                float nx, ny;
                { const float *xy = ex_graphmodel_positions(m); nx = xy[2 * st.selected]; ny = xy[2 * st.selected + 1]; }
                CHECK((nx - px) * (float)ddx[i] + (ny - py) * (float)ddy[i] > 0.0f);                     /* it moved the way the key points */
                break;
            }
            key(ui, EX_KEY_PAGE_DOWN, 0); frame(ui, r, root);                                          /* nothing that way from here: try the next node */
        }
        CHECK(hops < 12);
    }

    /* open, open in a new tab, menu, preview */
    sel = st.selected;
    CHECK(sel >= 0);
    key(ui, EX_KEY_ENTER, 0);
    CHECK(got.opens == 1 && got.new_tabs == 0 && !strcmp(got.open_path, names[sel][0] == '?' ? names[sel] + 1 : names[sel]));
    key(ui, EX_KEY_ENTER, EX_MOD_PRIMARY);
    CHECK(got.opens == 2 && got.new_tabs == 1);
    type(ui, "m");
    CHECK(got.menus == 1 && !strcmp(got.menu_path, got.open_path));
    type(ui, "p");
    CHECK(got.previews == 1 && !strcmp(got.prev_path, got.open_path));
    key(ui, EX_KEY_ESCAPE, 0);                                       /* the first Escape closes the preview, the second clears the selection */
    CHECK(got.preview_closes == 1);
    frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.selected == sel);
    key(ui, EX_KEY_ESCAPE, 0); frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.selected == -1);

    /* zoom, fit and pan */
    ex_graphview_stats(gw, &st);
    {
        float z0 = st.zoom;
        type(ui, "+"); frame(ui, r, root); ex_graphview_stats(gw, &st);
        CHECK(st.zoom > z0 && st.following == 0);
        type(ui, "-"); type(ui, "-"); frame(ui, r, root); ex_graphview_stats(gw, &st);
        CHECK(st.zoom < z0 * 1.25f);
        type(ui, "0"); frame(ui, r, root); ex_graphview_stats(gw, &st);
        CHECK(st.following == 1);
        key(ui, EX_KEY_LEFT, 0); frame(ui, r, root); ex_graphview_stats(gw, &st);
        CHECK(st.following == 0);
        key(ui, EX_KEY_HOME, 0); frame(ui, r, root); ex_graphview_stats(gw, &st);
        CHECK(st.following == 1);
    }

    /* the gear and the wand: Tab reaches them and Enter presses them; g opens the panel from the graph */
    CHECK(!ex_graphpanel_is_open(panel));
    type(ui, "g");
    CHECK(ex_graphpanel_is_open(panel));
    type(ui, "g");
    CHECK(!ex_graphpanel_is_open(panel));
    {
        ExWidget *gear = ex_graphpanel_gear(panel), *wand = ex_graphpanel_find(panel, "Animate the graph");
        int tries;
        CHECK(gear && gear->focusable && wand && wand->focusable);
        ex_ui_set_focus(ui, gw);
        for (tries = 0; tries < 20 && ex_ui_focus(ui) != gear; tries++) { key(ui, EX_KEY_TAB, 0); }
        CHECK(ex_ui_focus(ui) == gear);
        key(ui, EX_KEY_ENTER, 0);
        CHECK(ex_graphpanel_is_open(panel));
        ex_graphpanel_open(panel, 0);
        ex_ui_set_focus(ui, gw);
        for (tries = 0; tries < 20 && ex_ui_focus(ui) != wand; tries++) { key(ui, EX_KEY_TAB, 0); }
        CHECK(ex_ui_focus(ui) == wand);
        key(ui, EX_KEY_ENTER, 0);
        CHECK(ex_graphview_animation_running(gw));
        key(ui, EX_KEY_ENTER, 0);
        CHECK(!ex_graphview_animation_running(gw));
    }
    ex_ui_set_focus(ui, gw);

    /* the depth of a local graph: [ and ] */
    got.changes = 0;
    type(ui, "]"); CHECK(got.depth == 2 && ex_graphmodel_options(m)->local_depth == 2 && got.changes == 1);
    type(ui, "]"); type(ui, "]"); type(ui, "]"); type(ui, "]"); CHECK(ex_graphmodel_options(m)->local_depth == 5);          /* it stops at 5 */
    type(ui, "["); CHECK(ex_graphmodel_options(m)->local_depth == 4);
    { ExWidget *d = ex_graphpanel_find(panel, "Depth"); CHECK(d && ex_slider_value(d) == 4.0f && strstr(d->name, "4")); }

    /* the animation: the nodes appear in the order of their creation (here the order of the names), the unresolved name with the note that links to it */
    ex_graphview_animate(gw, 1);
    CHECK(ex_graphview_animation_running(gw) && ex_graphview_animations_running() == 1);
    ex_graphview_animation_seek(gw, 0.45f);
    frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    if (st.nodes_drawn != 4 || st.edges_drawn != 3) fprintf(stderr, "drawn %zu nodes, %zu edges\n", st.nodes_drawn, st.edges_drawn);
    CHECK(st.nodes_drawn == 4 && st.edges_drawn == 3);                                       /* N0 to N3 and the links among them */
    CHECK(ex_graphview_node_screen(gw, 0, &sx, &sy) && ex_graphview_node_at(gw, sx, sy) == 0);
    CHECK(ex_graphview_node_screen(gw, 9, &sx, &sy) && ex_graphview_node_at(gw, sx, sy) != 9);   /* N9 has not been created yet */
    ex_graphview_animation_seek(gw, 0.99f);
    frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(st.nodes_drawn == 10 && ex_graphview_animation_running(gw));                       /* all but the one created last */
    ex_graphview_animation_seek(gw, 1.2f);
    frame(ui, r, root);
    ex_graphview_stats(gw, &st);
    CHECK(!ex_graphview_animation_running(gw) && ex_graphview_animations_running() == 0 && st.nodes_drawn == 11);
    type(ui, "a");                                                    /* the key starts it again */
    CHECK(ex_graphview_animation_running(gw));
    type(ui, "a");
    CHECK(!ex_graphview_animation_running(gw) && ex_graphview_animations_running() == 0);
    (void)first;

    /* a graph read again after a save (ISSUES 31): the same graph changes nothing; one with a new link and the same notes restarts the layout
     * gently (the notes keep their places and the temperature starts low), and one with a new note restarts it as warm as 5a did */
    {
        static const char *more[] = {"N0>N1", "N1>N2", "N2>N3", "N3>N4", "N4>N5", "N5>N6", "N6>N7", "N7>N8", "N8>N9", "N0>N5", "N2>N7", "N9>Missing", "N1>N8"};
        static const char *names2[] = {"N0", "N1", "N2", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "?Missing", "Extra"};
        ex_graphmodel_set_graph(m, build(names, 11, links, 12));
        wait_model(m);
        CHECK(ex_graphmodel_temperature(m) < 0.002f);
        ex_graphmodel_set_graph(m, build(names, 11, links, 12));                              /* nothing changed */
        ex_graphmodel_update(m);
        CHECK(!ex_graphmodel_animating(m) && ex_graphmodel_temperature(m) < 0.002f);
        ex_graphmodel_set_graph(m, build(names, 11, more, 13));                              /* one new link, the same notes */
        CHECK(ex_graphmodel_start_temperature(m) > 0.05f && ex_graphmodel_start_temperature(m) < 0.12f);          /* the start, not the live value: a fast machine cools it before we look */
        wait_model(m);
        ex_graphmodel_set_graph(m, build(names2, 12, links, 12));                            /* a new note */
        CHECK(ex_graphmodel_start_temperature(m) > 0.25f);
    }

    ex_ui_release(ui, root);
    ex_widget_free(root);
    ex_graphmodel_free(m);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    (void)t;
}

int main(void) {
    local();
    keys();
    if (failures) { fprintf(stderr, "graph_keyboard: %d failure(s)\n", failures); return 1; }
    printf("graph_keyboard: ok\n");
    return 0;
}
