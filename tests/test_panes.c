/* workspace_layout_panes: the pane widgets built from a workspace, driven by scripted mouse
 * input and drops: click a tab, drag a tab to the edge of a pane (split), drag a divider, drop
 * text and files on the right pane, close a tab with its button, drag a tab out of the window. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_panes.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct Ctx {
    ExUi *ui;
    ExDoc *docs[16];
    ExId doc_tab[16];
    int ndocs;
    ExId dropped_group;
    char dropped_path[128];
    ExId popped;
    int new_tabs;
} Ctx;

static ExWidget *make_content(ExPanes *p, const ExTab *tab, void *user) {
    Ctx *c = user;
    (void)p;
    c->docs[c->ndocs] = ex_doc_create("hello", 5);
    c->doc_tab[c->ndocs] = tab->id;
    return ex_widget_editor(c->ui, c->docs[c->ndocs++], tab->path);
}
static int drop_file(ExPanes *p, ExId group, const char *path, void *user) {
    Ctx *c = user;
    (void)p;
    c->dropped_group = group;
    snprintf(c->dropped_path, sizeof c->dropped_path, "%s", path);
    return 1;
}
static void popout(ExPanes *p, ExId tab, void *user) { (void)p; ((Ctx *)user)->popped = tab; }
static void new_tab(ExPanes *p, ExId group, void *user) { (void)p; (void)group; ((Ctx *)user)->new_tabs++; }

static ExDoc *doc_of(Ctx *c, ExId tab) {
    int i;
    for (i = 0; i < c->ndocs; i++) if (c->doc_tab[i] == tab) return c->docs[i];
    return NULL;
}

static ExWidget *find(ExWidget *w, const char *name) {
    for (; w; w = w->next) {
        ExWidget *f;
        if (w->name && strcmp(w->name, name) == 0) return w;
        f = find(w->first_child, name);
        if (f) return f;
    }
    return NULL;
}
static int count_named(const ExWidget *w, const char *name) {
    int n = 0;
    for (; w; w = w->next) { if (w->name && strcmp(w->name, name) == 0) n++; n += count_named(w->first_child, name); }
    return n;
}

static ExWidget *g_left, *g_right, *g_redge, *g_ledge;
static void frame(ExUi *ui, ExPanes *p, ExWidget *root) {
    ex_sidebar_sync(g_left);
    ex_sidebar_sync(g_right);
    ex_panes_update(p);
    ex_ui_layout(ui, root, 1000, 600);
}
static void click(ExUi *ui, ExWidget *root, float x, float y) {
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, y, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, x, y, 0, 1);
}
static void drag(ExUi *ui, ExWidget *root, float x0, float y0, float x1, float y1) {
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x0, y0, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_DRAG, (x0 + x1) / 2, (y0 + y1) / 2, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_DRAG, x1, y1, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, x1, y1, 0, 1);
}
static float cx(const ExWidget *w) { return w->x + w->w / 2; }
static float cy(const ExWidget *w) { return w->y + w->h / 2; }

static char *doc_text(ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExWorkspace *ws = ex_ws_create();
    ExPanesHooks hooks;
    ExPanes *panes;
    ExWidget *root, *win, *w;
    Ctx ctx;
    ExId g0, a, b, c, g1;
    int i;

    memset(&ctx, 0, sizeof ctx);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("panes", 1000, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    ui = ex_ui_create(r, &theme, 16.0f);
    ctx.ui = ui;
    memset(&hooks, 0, sizeof hooks);
    hooks.make_content = make_content;
    hooks.drop_file = drop_file;
    hooks.popout = popout;
    hooks.new_tab = new_tab;
    panes = ex_panes_create(ui, ws, &hooks, &ctx);
    win = ex_widget_box(ui, 0, "window");
    win->width = ex_size_grow(); win->height = ex_size_grow();
    root = ex_panes_root(panes);
    g_left = ex_sidebar_create(ui, ws, EX_SIDE_LEFT, ex_widget_label(ui, "files"), "Left sidebar");
    g_right = ex_sidebar_create(ui, ws, EX_SIDE_RIGHT, ex_widget_label(ui, "panels"), "Right sidebar");
    g_ledge = ex_sidebar_edge_create(g_left);
    g_redge = ex_sidebar_edge_create(g_right);
    ex_widget_add(win, g_ledge);
    ex_widget_add(win, g_left);
    ex_widget_add(win, root);
    ex_widget_add(win, g_right);
    ex_widget_add(win, g_redge);

    g0 = ex_ws_active_group(ws);
    a = ex_ws_new_tab(ws, 0, "a.md", "note");
    b = ex_ws_new_tab(ws, 0, "b.md", "note");
    c = ex_ws_new_tab(ws, 0, "c.md", "note");
    frame(ui, panes, win);
    CHECK(count_named(win, "Pane") == 1);
    CHECK(ex_panes_content(panes, c) != NULL && ex_panes_content(panes, a) == NULL);   /* only the shown tab has content */

    /* a click on a tab shows it */
    w = find(win, "Tab: a");
    CHECK(w != NULL);
    click(ui, win, cx(w), cy(w));
    frame(ui, panes, win);
    CHECK(ex_ws_group_active(ws, g0) == a && ex_panes_content(panes, a) != NULL);
    CHECK(ex_panes_content(panes, c) != NULL);                     /* kept for when it is shown again */
    click(ui, win, cx(find(win, "Tab: c")), cy(find(win, "Tab: c")));
    frame(ui, panes, win);
    CHECK(ex_ws_group_active(ws, g0) == c);

    /* the "+" button asks the application for a tab */
    w = find(win, "New tab");
    CHECK(w != NULL);
    click(ui, win, cx(w), cy(w));
    CHECK(ctx.new_tabs == 1);

    /* dragging tab c to the right edge of the pane splits it */
    {
        ExWidget *content = find(win, "Content");
        w = find(win, "Tab: c");
        CHECK(content && w);
        drag(ui, win, cx(w), cy(w), content->x + content->w - 20, cy(content));
        frame(ui, panes, win);
        CHECK(ex_ws_group_count(ws) == 2);
        g1 = ex_ws_tab(ws, c)->group;
        CHECK(g1 != g0);
        CHECK(count_named(win, "Pane") == 2);
        CHECK(ex_ws_node_kind(ws, ex_ws_node_parent(ws, g1)) == EX_NODE_SPLIT);
    }

    /* dragging the divider changes the shares */
    {
        ExWidget *split = find(win, "Split"), *left;
        float edge;
        CHECK(split && split->first_child && split->first_child->next);
        left = split->first_child;
        edge = left->x + left->w;
        drag(ui, win, edge, split->y + split->h / 2, 270, split->y + split->h / 2);      /* too far: the pane keeps 160 px */
        frame(ui, panes, win);
        CHECK(split->first_child->w >= 159 && split->first_child->w <= 161 + 1);
        CHECK(split->first_child->next->x >= split->first_child->x + split->first_child->w - 1);   /* no overlap */
        edge = split->first_child->x + split->first_child->w;
        drag(ui, win, edge, split->y + split->h / 2, 500, split->y + split->h / 2);
        frame(ui, panes, win);
        CHECK(fabsf(ex_ws_split_ratio(ws, ex_ws_node_parent(ws, g1), 0) - (500 - split->x) / split->w) < 0.02f);
        CHECK(fabsf(split->first_child->w - (500 - split->x)) < 6);
    }

    /* text dropped on a pane goes to that pane's editor; a file to that pane's hook */
    {
        ExWidget *right_content = find(ex_panes_content(panes, c)->parent->parent->parent, "Content");
        ExWidget *ed = ex_panes_content(panes, c), *led = ex_panes_content(panes, ex_ws_group_active(ws, g0));
        char *t;
        (void)right_content;
        CHECK(ed && led && ed != led);
        CHECK(ex_ui_drop(ui, win, 0, "XYZ", cx(ed), cy(ed)) == 1);
        t = doc_text(doc_of(&ctx, c));
        CHECK(strstr(t, "XYZ") != NULL);
        free(t);
        t = doc_text(doc_of(&ctx, ex_ws_group_active(ws, g0)));
        CHECK(strstr(t, "XYZ") == NULL);                           /* not into the other pane */
        free(t);
        CHECK(ex_ui_drop(ui, win, 0, "LEFT", cx(led), cy(led)) == 1);
        t = doc_text(doc_of(&ctx, ex_ws_group_active(ws, g0)));
        CHECK(strstr(t, "LEFT") != NULL);
        free(t);
        CHECK(ex_ui_drop(ui, win, 1, "notes/dropped.md", cx(ed), cy(ed)) == 1);
        CHECK(ctx.dropped_group == g1 && strcmp(ctx.dropped_path, "notes/dropped.md") == 0);
        CHECK(ex_ui_drop(ui, win, 1, "x.md", 5000, 5000) == 0);    /* nothing there */
    }

    /* the close button closes the tab; the emptied pane goes away */
    w = find(win, "Close c");
    CHECK(w != NULL);
    click(ui, win, cx(w), cy(w));
    frame(ui, panes, win);
    CHECK(ex_ws_tab(ws, c) == NULL && ex_ws_group_count(ws) == 1 && count_named(win, "Pane") == 1);
    CHECK(ex_panes_content(panes, c) == NULL);

    /* a tab dragged out of the window is offered to the pop-out hook and stays put until the hook acts */
    w = find(win, "Tab: b");
    CHECK(w != NULL);
    drag(ui, win, cx(w), cy(w), 2000, 300);
    CHECK(ctx.popped == b && ex_ws_tab(ws, b)->group == g0);

    /* reordering inside a pane: b dropped on the strip before a */
    w = find(win, "Tab: b");
    {
        ExWidget *wa = find(win, "Tab: a");
        drag(ui, win, cx(w), cy(w), wa->x + 2, cy(wa));
        frame(ui, panes, win);
        CHECK(ex_ws_group_tab(ws, g0, 0) == b && ex_ws_group_tab(ws, g0, 1) == a);
    }
    (void)i;

    /* sidebars: default widths, dragging an edge resizes (and stays within limits), hiding frees the space */
    frame(ui, panes, win);
    CHECK(fabsf(g_left->w - 260) < 2 && fabsf(g_right->w - 300) < 2);
    CHECK(fabsf(root->w - (1000 - 260 - 300)) < 3);
    drag(ui, win, g_left->x + g_left->w - 1, 300, 350, 300);
    frame(ui, panes, win);
    CHECK(ex_ws_sidebar_width(ws, EX_SIDE_LEFT) >= 348 && ex_ws_sidebar_width(ws, EX_SIDE_LEFT) <= 352 && fabsf(g_left->w - (float)ex_ws_sidebar_width(ws, EX_SIDE_LEFT)) < 2);
    drag(ui, win, g_right->x + 1, 300, g_right->x + 200, 300);         /* wider on the right means narrower: dragging right shrinks it */
    frame(ui, panes, win);
    CHECK(ex_ws_sidebar_visible(ws, EX_SIDE_RIGHT) && ex_ws_sidebar_width(ws, EX_SIDE_RIGHT) == 120);    /* never narrower than 120 while it is shown */
    drag(ui, win, g_right->x + 1, 300, g_right->x + 600, 300);          /* dragged right out of the window: the sidebar hides and keeps its width */
    frame(ui, panes, win);
    CHECK(!ex_ws_sidebar_visible(ws, EX_SIDE_RIGHT) && g_right->hidden && !g_redge->hidden && ex_ws_sidebar_width(ws, EX_SIDE_RIGHT) == 120);   /* its width is kept */
    CHECK(fabsf(root->w - (1000 - 350 - g_redge->w)) < 3);               /* the note view takes the space, minus the handle at the edge */
    drag(ui, win, g_redge->x + 2, 300, 700, 300);                         /* the handle pulled left to x = 700 of 1000: the sidebar is back, 300 px wide */
    frame(ui, panes, win);
    CHECK(ex_ws_sidebar_visible(ws, EX_SIDE_RIGHT) && !g_right->hidden && g_redge->hidden && ex_ws_sidebar_width(ws, EX_SIDE_RIGHT) >= 296 && ex_ws_sidebar_width(ws, EX_SIDE_RIGHT) <= 304);
    {   /* the note view shrinks with the window and the sidebars, down to nothing; its content follows it (it used to stop at its tab strip's width) */
        ExWidget *content;
        ex_ws_set_sidebar(ws, EX_SIDE_LEFT, 1, 470);
        ex_ws_set_sidebar(ws, EX_SIDE_RIGHT, 1, 470);
        frame(ui, panes, win);
        content = ex_panes_content(panes, ex_ws_group_active(ws, g0));
        CHECK(root->w < 70 && content && fabsf(content->w - root->w) < 2);
    }
    ex_ws_set_sidebar(ws, EX_SIDE_LEFT, 0, ex_ws_sidebar_width(ws, EX_SIDE_LEFT));
    frame(ui, panes, win);
    CHECK(g_left->hidden && !g_ledge->hidden && fabsf(root->x - g_ledge->w) < 2);   /* a hidden sidebar leaves its handle at the edge */

    ex_panes_free(panes);
    ex_widget_free(win);
    for (i = 0; i < ctx.ndocs; i++) ex_doc_free(ctx.docs[i]);
    ex_ws_free(ws);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("workspace_layout_panes: ok");
    return 0;
}
