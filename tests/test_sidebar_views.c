/* sidebar_views (sub-slice 5e): the right sidebar holds a group of tabs.
 *  - The workspace: the group is made empty and on demand, a tab moves into it and back, it is never split, it keeps its place when its last
 *    tab leaves, it is saved in the session ([layout] right_group and its [group] section) and a damaged file without it puts the tabs in the
 *    main area.
 *  - The panes: the sidebar group is built with its own tab strip, header and content, next to the main panes; a note and the graph show in it
 *    (the graph draws within the sidebar's width and a click on its node opens a note); a tab dragged from the main strip and dropped on the
 *    sidebar moves there, and one moved back is in the main area again; the strip and the pane header of 5d are in the sidebar group too.
 * (Typing in a note in the sidebar and saving it, the restore and the move back are checked in the real program by app_scripts.)
 * Usage: test_sidebar_views */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graphview.h"
#include "exhaust_panes.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void workspace(void) {
    ExWorkspace *ws = ex_ws_create(), *re = ex_ws_create(), *bad = ex_ws_create();
    ExId main_g = ex_ws_active_group(ws), a, b, g, t;
    char *text;
    size_t len;
    CHECK(ex_ws_side_group(ws, EX_SIDE_RIGHT) == 0 && ex_ws_side_group(ws, EX_SIDE_LEFT) == 0);
    a = ex_ws_new_tab(ws, main_g, "A.md", "note");
    b = ex_ws_new_tab(ws, main_g, "", "graph");
    g = ex_ws_side_group_ensure(ws, EX_SIDE_RIGHT);
    CHECK(g != 0 && g != main_g && ex_ws_side_group(ws, EX_SIDE_RIGHT) == g && ex_ws_side_group_ensure(ws, EX_SIDE_RIGHT) == g);   /* made once */
    CHECK(ex_ws_side_group_ensure(ws, EX_SIDE_LEFT) == 0);                                       /* only the right side has one */
    CHECK(ex_ws_group_tab_count(ws, g) == 0);
    CHECK(ex_ws_move_tab(ws, a, g, (size_t)-1) == 0);
    t = ex_ws_group_active(ws, g);
    CHECK(t == a && ex_ws_tab(ws, a)->group == g && ex_ws_group_tab_count(ws, main_g) == 1);
    CHECK(ex_ws_split(ws, g, EX_SPLIT_HORIZONTAL, 1, 0) == 0);                                    /* never split */
    CHECK(ex_ws_move_tab(ws, b, g, 0) == 0 && ex_ws_group_tab_count(ws, g) == 2 && ex_ws_group_tab_count(ws, main_g) == 0);
    CHECK(ex_ws_group_count(ws) == 2);                                                            /* the main group stays, empty, and so does the sidebar's */

    text = ex_ws_serialize(ws, &len);
    CHECK(text && strstr(text, "right_group") != NULL);
    CHECK(ex_ws_deserialize(re, text, len) == 0);
    CHECK(ex_ws_side_group(re, EX_SIDE_RIGHT) == g && ex_ws_group_tab_count(re, g) == 2 && !strcmp(ex_ws_tab(re, a)->path, "A.md") && ex_ws_tab(re, a)->group == g);
    {   /* without the group's section the tabs go to the main area */
        char *p = strstr(text, "right_group");
        CHECK(p != NULL);
        if (p) { memcpy(p, "wrong___grp", 11); }
        CHECK(ex_ws_deserialize(bad, text, len) == 0);
        CHECK(ex_ws_side_group(bad, EX_SIDE_RIGHT) == 0 && ex_ws_tab_count(bad) == 2);
        CHECK(ex_ws_group_tab_count(bad, ex_ws_active_group(bad)) == 2);
    }
    free(text);

    CHECK(ex_ws_close_tab(ws, a) == 0 && ex_ws_close_tab(ws, b) == 0);
    CHECK(ex_ws_side_group(ws, EX_SIDE_RIGHT) == g && ex_ws_group_tab_count(ws, g) == 0);          /* the empty group stays */
    t = ex_ws_new_tab(ws, g, "C.md", "note");
    CHECK(t && ex_ws_tab(ws, t)->group == g);
    CHECK(ex_ws_move_tab(ws, t, main_g, (size_t)-1) == 0 && ex_ws_tab(ws, t)->group == main_g && ex_ws_group_tab_count(ws, g) == 0);   /* and back */
    ex_ws_free(ws); ex_ws_free(re); ex_ws_free(bad);
}

/* ---- the panes ---- */

typedef struct Ctx { ExUi *ui; ExGraphModel *gm; int opened; char opened_path[80]; } Ctx;
static void on_open(void *u, const char *path, int kind, int new_tab) { Ctx *c = u; (void)kind; (void)new_tab; c->opened++; snprintf(c->opened_path, sizeof c->opened_path, "%s", path); }
static ExWidget *make_content(ExPanes *p, const ExTab *tab, void *user) {
    Ctx *c = user;
    (void)p;
    if (!strcmp(tab->view, "graph")) {
        ExGraphHandlers h;
        memset(&h, 0, sizeof h);
        h.user = c; h.open = on_open;
        return ex_widget_graph(c->ui, c->gm, "Graph view", &h);
    }
    return ex_widget_label(c->ui, tab->path);
}

static ExWidget *find(ExWidget *w, const char *name) {
    for (; w; w = w->next) {
        ExWidget *f;
        if (w->name && !strcmp(w->name, name)) return w;
        f = find(w->first_child, name);
        if (f) return f;
    }
    return NULL;
}
static int inside(const ExWidget *w, const ExWidget *root) { for (; w; w = w->parent) if (w == root) return 1; return 0; }

static ExGraph *small_graph(void) {
    ExGraph *g = calloc(1, sizeof *g);
    int i;
    g->n = 4; g->nodes = calloc(4, sizeof *g->nodes);
    for (i = 0; i < 4; i++) { char p[32]; size_t l; snprintf(p, sizeof p, "Note %d.md", i); l = strlen(p) + 1; g->nodes[i].path = malloc(l); memcpy(g->nodes[i].path, p, l); g->nodes[i].kind = EX_GRAPH_NOTE; }
    g->edges = calloc(3, sizeof *g->edges);
    for (i = 0; i < 3; i++) { g->edges[i].from = (uint32_t)i; g->edges[i].to = (uint32_t)(i + 1); g->nodes[i].degree++; g->nodes[i + 1].degree++; }
    g->ne = 3;
    return g;
}

static void panes(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExWorkspace *ws = ex_ws_create();
    ExPanesHooks hooks;
    ExPanes *pn;
    Ctx ctx;
    ExWidget *win, *side, *sroot, *sgroup, *hdr, *nt, *tab_btn, *main_pane;
    ExId mg = ex_ws_active_group(ws), a, sg;
    ExGraphViewStats st;
    Uint64 t0;
    memset(&ctx, 0, sizeof ctx);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("sidebar views", 1000, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    ctx.ui = ui;
    ctx.gm = ex_graphmodel_create();
    memset(&hooks, 0, sizeof hooks);
    hooks.make_content = make_content;
    pn = ex_panes_create(ui, ws, &hooks, &ctx);
    win = ex_widget_box(ui, 0, "window");
    win->width = ex_size_grow(); win->height = ex_size_grow();
    ex_widget_add(win, ex_panes_root(pn));
    side = ex_widget_box(ui, 1, "Right sidebar");
    side->width = ex_size_px(ui, 300); side->height = ex_size_grow();
    sroot = ex_panes_side_root(pn);
    ex_widget_add(side, sroot);
    ex_widget_add(win, side);
    a = ex_ws_new_tab(ws, mg, "Main note.md", "note");
    ex_ws_new_tab(ws, mg, "Second.md", "note");
    sg = ex_ws_side_group_ensure(ws, EX_SIDE_RIGHT);
    ex_ws_new_tab(ws, sg, "Side note.md", "note");
    ex_ws_new_tab(ws, sg, "", "graph");
    ex_panes_update(pn);
    ex_ui_layout(ui, win, 1000, 600);

    /* the sidebar group has its strip, header and content, in the sidebar */
    sgroup = sroot->first_child;
    CHECK(sgroup != NULL && sroot->w > 250.0f && sroot->w <= 320.0f);
    CHECK(find(sroot, "Tabs") != NULL && find(sroot, "Pane header") != NULL && find(sroot, "Content") != NULL);
    hdr = ex_panes_header(pn, sg);
    CHECK(hdr && inside(hdr, sroot) && hdr->w > 0 && ex_panes_header_button(pn, sg, "Pane menu") != NULL);
    CHECK(ex_panes_header_title(pn, sg) != NULL);
    CHECK(ex_panes_content(pn, ex_ws_group_active(ws, sg)) != NULL);                                    /* the active tab of the sidebar group has its content */

    /* the graph moved to the sidebar draws within its width and a click on a node opens a note */
    ex_graphmodel_set_graph(ctx.gm, small_graph());
    t0 = SDL_GetTicks();
    while (!ex_graphmodel_positions(ctx.gm) && SDL_GetTicks() - t0 < 20000) { ex_graphmodel_update(ctx.gm); SDL_Delay(2); }
    {
        ExId gt = 0;
        size_t i;
        for (i = 0; i < ex_ws_group_tab_count(ws, sg); i++) if (!strcmp(ex_ws_tab(ws, ex_ws_group_tab(ws, sg, i))->view, "graph")) gt = ex_ws_group_tab(ws, sg, i);
        CHECK(gt != 0);
        ex_ws_activate(ws, gt);
        ex_panes_update(pn);
        ex_ui_layout(ui, win, 1000, 600);
        {
            ExWidget *gw = ex_panes_content(pn, gt);
            float nx, ny;
            int k;
            CHECK(gw && gw->type == EX_WIDGET_GRAPH && inside(gw, sroot) && gw->w <= 305.0f && gw->w > 100.0f);
            ex_ui_draw(ui, win, 1);
            ex_graphview_stats(gw, &st);
            CHECK(st.nodes_drawn == 4 && st.edges_drawn == 3);
            for (k = 0; k < 4; k++) if (ex_graphview_node_screen(gw, k, &nx, &ny) && ex_graphview_node_at(gw, nx, ny) == k) break;
            CHECK(k < 4);
            if (k < 4) {
                ex_ui_mouse(ui, win, EX_MOUSE_DOWN, nx, ny, 0, 1);
                ex_ui_mouse(ui, win, EX_MOUSE_UP, nx, ny, 0, 1);
                CHECK(ctx.opened == 1 && strstr(ctx.opened_path, "Note ") != NULL);
            }
        }
    }

    /* a tab dragged from the main strip and dropped in the sidebar moves there */
    tab_btn = find(ex_panes_root(pn), "Tab: Second");
    if (!tab_btn) { ExWidget *w; for (w = ex_panes_root(pn)->first_child; w && !tab_btn; w = w->next) tab_btn = find(w, "Second"); }
    main_pane = ex_panes_root(pn);
    {
        ExId second = ex_ws_find_tab(ws, "Second.md");
        float sx, sy, dx, dy;
        CHECK(second != 0 && tab_btn != NULL);
        if (tab_btn) {
            sx = tab_btn->x + tab_btn->w / 2.0f; sy = tab_btn->y + tab_btn->h / 2.0f;
            nt = ex_panes_header(pn, sg);
            dx = sroot->x + sroot->w / 2.0f; dy = nt ? nt->y + 200.0f : sroot->y + 300.0f;                  /* the middle of the sidebar group's content */
            ex_ui_mouse(ui, win, EX_MOUSE_DOWN, sx, sy, 0, 1);
            ex_ui_mouse(ui, win, EX_MOUSE_DRAG, sx + 20.0f, sy + 20.0f, 0, 1);
            ex_ui_mouse(ui, win, EX_MOUSE_DRAG, dx, dy, 0, 1);
            ex_ui_mouse(ui, win, EX_MOUSE_UP, dx, dy, 0, 1);
            CHECK(ex_ws_tab(ws, second)->group == sg);
        }
    }
    (void)a; (void)main_pane;

    /* and moved back (the workspace call the application's command makes) */
    {
        ExId second = ex_ws_find_tab(ws, "Second.md");
        ex_ws_move_tab(ws, second, mg, (size_t)-1);
        ex_panes_update(pn);
        ex_ui_layout(ui, win, 1000, 600);
        CHECK(ex_ws_tab(ws, second)->group == mg && ex_ws_group_tab_count(ws, sg) == 2);
        CHECK(ex_panes_content(pn, second) != NULL);
    }
    /* the sidebar group with no tabs is built empty */
    while (ex_ws_group_tab_count(ws, sg) > 0) ex_ws_close_tab(ws, ex_ws_group_tab(ws, sg, 0));
    ex_panes_update(pn);
    CHECK(sroot->first_child != NULL);

    ex_ui_release(ui, win);
    ex_widget_remove(win, ex_panes_root(pn));
    ex_widget_remove(side, sroot);
    ex_widget_free(win);
    ex_panes_free(pn);
    ex_graphmodel_free(ctx.gm);
    ex_ws_free(ws);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
}

int main(void) {
    workspace();
    panes();
    if (failures) { fprintf(stderr, "sidebar_views: %d failure(s)\n", failures); return 1; }
    printf("sidebar_views: ok\n");
    return 0;
}
