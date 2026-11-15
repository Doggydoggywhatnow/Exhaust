/* pane_header (sub-slice 5d): the header of every pane and the history behind its back and forward arrows.
 *  - History (the workspace): five places visited in two panes are walked back and forward by each pane on its own; a note that no longer
 *    exists is skipped and dropped; visiting after going back drops what was ahead; the same place twice changes nothing; the list is capped;
 *    the cursor and scroll a tab had are kept when the pane leaves it; the history survives a session round trip and a damaged entry is
 *    ignored.
 *  - The header (the panes): the title of a note and of a graph, back and forward enabled only where there is somewhere to go, the reading
 *    switch only for a note and pressed in reading mode, each button runs its hook, every button has a drawn icon, a name and a keyboard
 *    route, and the strip hides with the setting.
 * Usage: test_pane_header */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_panes.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int not_gone(const ExPlace *pl, void *u) { return strcmp(pl->path, (const char *)u) != 0; }
static const char *place_path(const ExWorkspace *ws, ExId g, size_t i) { const ExPlace *p = ex_ws_history_place(ws, g, i); return p ? p->path : NULL; }

static void history(void) {
    ExWorkspace *ws = ex_ws_create(), *re = ex_ws_create();
    ExId ga = ex_ws_active_group(ws), gb, t;
    const ExPlace *pl;
    char *text;
    size_t len, i;
    int k;
    gb = ex_ws_split(ws, ga, EX_SPLIT_HORIZONTAL, 1, 0);
    CHECK(gb != 0 && gb != ga);
    CHECK(ex_ws_history_count(ws, ga) == 0 && ex_ws_history_pos(ws, ga) == -1 && !ex_ws_history_can(ws, ga, -1) && !ex_ws_history_can(ws, ga, 1));

    /* five places in two panes: three in A, two in B */
    ex_ws_history_visit(ws, ga, "A1.md", "note"); ex_ws_history_visit(ws, ga, "A2.md", "note"); ex_ws_history_visit(ws, ga, "A3.md", "note");
    ex_ws_history_visit(ws, gb, "B1.md", "note"); ex_ws_history_visit(ws, gb, "", "graph");
    CHECK(ex_ws_history_count(ws, ga) == 3 && ex_ws_history_pos(ws, ga) == 2 && ex_ws_history_count(ws, gb) == 2 && ex_ws_history_pos(ws, gb) == 1);
    ex_ws_history_visit(ws, ga, "A3.md", "note");                                 /* the same place again: nothing changes */
    CHECK(ex_ws_history_count(ws, ga) == 3);
    CHECK(ex_ws_history_can(ws, ga, -1) && !ex_ws_history_can(ws, ga, 1));
    pl = ex_ws_history_go(ws, ga, -1);
    CHECK(pl && !strcmp(pl->path, "A2.md") && ex_ws_history_pos(ws, ga) == 1);
    pl = ex_ws_history_go(ws, ga, -1);
    CHECK(pl && !strcmp(pl->path, "A1.md") && !ex_ws_history_can(ws, ga, -1) && ex_ws_history_go(ws, ga, -1) == NULL);
    CHECK(ex_ws_history_pos(ws, gb) == 1 && ex_ws_history_can(ws, gb, -1) && !ex_ws_history_can(ws, gb, 1));   /* B did not move */
    pl = ex_ws_history_go(ws, gb, -1);
    CHECK(pl && !strcmp(pl->path, "B1.md") && !strcmp(pl->view, "note"));
    pl = ex_ws_history_go(ws, ga, 1);
    CHECK(pl && !strcmp(pl->path, "A2.md") && ex_ws_history_pos(ws, gb) == 0);                     /* A forward, B where it was */
    pl = ex_ws_history_go(ws, gb, 1);
    CHECK(pl && !strcmp(pl->view, "graph") && pl->path[0] == 0);

    /* a note that no longer exists is skipped and dropped, in both directions */
    ex_ws_history_go(ws, ga, 1);                                                  /* A3 */
    pl = ex_ws_history_go_valid(ws, ga, -1, not_gone, (void *)"A2.md");
    CHECK(pl && !strcmp(pl->path, "A1.md") && ex_ws_history_count(ws, ga) == 2 && ex_ws_history_pos(ws, ga) == 0);
    CHECK(!strcmp(place_path(ws, ga, 1), "A3.md"));
    pl = ex_ws_history_go_valid(ws, ga, 1, not_gone, (void *)"A3.md");           /* the only place ahead is gone: nowhere to go */
    CHECK(pl == NULL && ex_ws_history_count(ws, ga) == 1 && ex_ws_history_pos(ws, ga) == 0 && !ex_ws_history_can(ws, ga, 1));
    ex_ws_history_visit(ws, ga, "A4.md", "note"); ex_ws_history_visit(ws, ga, "A5.md", "note");
    ex_ws_history_go(ws, ga, -1);                                                 /* at A4, A5 ahead */
    ex_ws_history_visit(ws, ga, "A6.md", "note");                                 /* a new place: A5 is dropped */
    CHECK(ex_ws_history_count(ws, ga) == 3 && !strcmp(place_path(ws, ga, 2), "A6.md") && !ex_ws_history_can(ws, ga, 1));

    /* the list is capped; the oldest goes */
    for (k = 0; k < 60; k++) { char p[24]; snprintf(p, sizeof p, "N%d.md", k); ex_ws_history_visit(ws, gb, p, "note"); }
    CHECK(ex_ws_history_count(ws, gb) == EX_HISTORY_MAX && !strcmp(place_path(ws, gb, EX_HISTORY_MAX - 1), "N59.md") && !strcmp(place_path(ws, gb, 0), "N10.md"));

    /* the cursor and scroll a tab had are kept when the pane leaves its place */
    t = ex_ws_new_tab(ws, ga, "C1.md", "note");
    ex_ws_set_tab_state(ws, t, 12, 5, 340.5f);
    ex_ws_history_visit(ws, ga, "C1.md", "note");
    ex_ws_history_visit(ws, ga, "C2.md", "note");
    for (i = 0; i < ex_ws_history_count(ws, ga); i++) {
        const ExPlace *q = ex_ws_history_place(ws, ga, i);
        if (!strcmp(q->path, "C1.md")) CHECK(q->line == 12 && q->column == 5 && q->scroll == 340.5f);
    }

    /* the session keeps it */
    text = ex_ws_serialize(ws, &len);
    CHECK(text != NULL && strstr(text, "history_pos") && strstr(text, "history_0"));
    CHECK(ex_ws_deserialize(re, text, len) == 0);
    {
        size_t cnt = ex_ws_history_count(ws, ga);
        CHECK(ex_ws_history_count(re, ga) == cnt && ex_ws_history_pos(re, ga) == ex_ws_history_pos(ws, ga) && ex_ws_history_count(re, gb) == EX_HISTORY_MAX);
        for (i = 0; i < cnt; i++) {
            const ExPlace *x = ex_ws_history_place(ws, ga, i), *y = ex_ws_history_place(re, ga, i);
            CHECK(y && !strcmp(x->path, y->path) && !strcmp(x->view, y->view) && x->line == y->line && x->column == y->column && x->scroll == y->scroll);
        }
    }
    free(text);
    {   /* a damaged entry is ignored and the rest kept; a position out of range is brought back */
        ExWorkspace *w2 = ex_ws_create(), *w3 = ex_ws_create();
        ExId g2 = ex_ws_active_group(w2);
        char *t2, *p;
        size_t l2;
        ex_ws_history_visit(w2, g2, "X.md", "note"); ex_ws_history_visit(w2, g2, "Y.md", "note"); ex_ws_history_visit(w2, g2, "Z.md", "note");
        t2 = ex_ws_serialize(w2, &l2);
        p = strstr(t2, "history_1 = ");
        CHECK(p != NULL);
        if (p) { memcpy(p + 12, "garbage", 7); p[19] = '\n'; }
        p = strstr(t2, "history_pos = ");
        if (p) { p[14] = '9'; }
        CHECK(ex_ws_deserialize(w3, t2, l2) == 0);
        CHECK(ex_ws_history_count(w3, g2) >= 2 && ex_ws_history_pos(w3, g2) >= 0 && ex_ws_history_pos(w3, g2) < (int)ex_ws_history_count(w3, g2));
        free(t2);
        ex_ws_free(w2); ex_ws_free(w3);
    }
    ex_ws_free(ws); ex_ws_free(re);
}

/* ---- the header ---- */

typedef struct Ctx { ExUi *ui; int back, fwd, reading, menus; ExId last_group; float menu_y; } Ctx;
static ExWidget *make_content(ExPanes *p, const ExTab *tab, void *user) { (void)p; return ex_widget_label(((Ctx *)user)->ui, tab->path[0] ? tab->path : "graph"); }
static void on_nav(ExPanes *p, ExId g, int dir, void *u) { Ctx *c = u; (void)p; c->last_group = g; if (dir < 0) c->back++; else c->fwd++; }
static void on_read(ExPanes *p, ExId g, void *u) { Ctx *c = u; (void)p; c->last_group = g; c->reading++; }
static void on_menu(ExPanes *p, ExId g, float x, float y, void *u) { Ctx *c = u; (void)p; (void)x; c->last_group = g; c->menus++; c->menu_y = y; }

static void click(ExUi *ui, ExWidget *root, ExWidget *b) {
    float x = b->x + b->w / 2.0f, y = b->y + b->h / 2.0f;
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, y, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, x, y, 0, 1);
}

static void header(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExWorkspace *ws = ex_ws_create();
    ExPanesHooks hooks;
    ExPanes *panes;
    Ctx ctx;
    ExWidget *win, *back, *fwd, *rd, *more, *hdr0, *hdr1;
    ExId ga = ex_ws_active_group(ws), gb, ta, tg;
    float content_y_with;
    int i;
    memset(&ctx, 0, sizeof ctx);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("pane header", 1000, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    ctx.ui = ui;
    memset(&hooks, 0, sizeof hooks);
    hooks.make_content = make_content; hooks.navigate = on_nav; hooks.toggle_reading = on_read; hooks.pane_menu = on_menu;
    panes = ex_panes_create(ui, ws, &hooks, &ctx);
    win = ex_widget_box(ui, 0, "window");
    win->width = ex_size_grow(); win->height = ex_size_grow();
    ex_widget_add(win, ex_panes_root(panes));
    ta = ex_ws_new_tab(ws, ga, "Folder/Note One.md", "note");
    gb = ex_ws_split(ws, ga, EX_SPLIT_HORIZONTAL, 1, 0);
    tg = ex_ws_new_tab(ws, gb, "", "graph");
    ex_panes_update(panes);
    ex_ui_layout(ui, win, 1000, 600);
    ex_panes_update(panes);                                                       /* the title is fitted to the room of the layout before: update once more */
    ex_ui_layout(ui, win, 1000, 600);

    hdr0 = ex_panes_header(panes, ga); hdr1 = ex_panes_header(panes, gb);
    CHECK(hdr0 && hdr1 && !hdr0->hidden && hdr0->h > 0 && hdr0->w > 0);
    CHECK(ex_panes_header_title(panes, ga) && !strcmp(ex_panes_header_title(panes, ga), "Note One"));    /* the title of a note: its name, without the folder and .md */
    CHECK(ex_panes_header_title(panes, gb) && !strcmp(ex_panes_header_title(panes, gb), "Graph"));       /* and of a graph */

    for (i = 0; i < 2; i++) {                                                     /* the same strip in each pane */
        ExId g = i ? gb : ga;
        back = ex_panes_header_button(panes, g, "Navigate back"); fwd = ex_panes_header_button(panes, g, "Navigate forward");
        rd = ex_panes_header_button(panes, g, "Toggle reading view"); more = ex_panes_header_button(panes, g, "Pane menu");
        CHECK(back && fwd && rd && more);
        CHECK(back->icon && fwd->icon && rd->icon && more->icon);              /* every button is drawn as an icon and has an accessible name */
        CHECK(back->w > 0 && more->w > 0 && more->x > back->x);
        CHECK(back->disabled && fwd->disabled);                                /* nowhere to go yet */
    }
    rd = ex_panes_header_button(panes, ga, "Toggle reading view");
    CHECK(!rd->hidden && !rd->selected);                                          /* a note can be switched to reading */
    CHECK(ex_panes_header_button(panes, gb, "Toggle reading view")->hidden);      /* a graph cannot */
    ex_ws_set_tab_mode(ws, ta, "reading");
    ex_panes_update(panes);
    CHECK(ex_panes_header_button(panes, ga, "Toggle reading view")->selected);    /* pressed in reading mode */
    ex_ws_set_tab_mode(ws, ta, "live");

    /* places make the arrows live */
    ex_ws_history_visit(ws, ga, "Folder/Note One.md", "note"); ex_ws_history_visit(ws, ga, "Other.md", "note");
    ex_panes_update(panes);
    ex_ui_layout(ui, win, 1000, 600);
    back = ex_panes_header_button(panes, ga, "Navigate back"); fwd = ex_panes_header_button(panes, ga, "Navigate forward");
    CHECK(!back->disabled && fwd->disabled);
    CHECK(ex_panes_header_button(panes, gb, "Navigate back")->disabled);          /* the other pane has none */
    click(ui, win, back);
    CHECK(ctx.back == 1 && ctx.last_group == ga);
    ex_ws_history_go(ws, ga, -1);
    ex_panes_update(panes);
    CHECK(!ex_panes_header_button(panes, ga, "Navigate forward")->disabled && ex_panes_header_button(panes, ga, "Navigate back")->disabled);
    click(ui, win, ex_panes_header_button(panes, ga, "Navigate forward"));
    CHECK(ctx.fwd == 1 && ctx.last_group == ga);
    click(ui, win, ex_panes_header_button(panes, ga, "Toggle reading view"));
    CHECK(ctx.reading == 1 && ctx.last_group == ga);
    more = ex_panes_header_button(panes, gb, "Pane menu");
    click(ui, win, more);
    CHECK(ctx.menus == 1 && ctx.last_group == gb && ctx.menu_y >= more->y + more->h - 1.0f);   /* the menu opens below the button, in the pane clicked */

    /* the keyboard reaches every button */
    {
        const char *want[4] = {"Navigate back", "Navigate forward", "Toggle reading view", "Pane menu"};
        int seen[4] = {0, 0, 0, 0}, steps;
        ex_ui_set_focus(ui, NULL);
        for (steps = 0; steps < 60; steps++) {
            ExWidget *f = ex_ui_focus_move(ui, 1);
            int k;
            if (!f) break;
            for (k = 0; k < 4; k++) if (f->name && !strcmp(f->name, want[k])) seen[k] = 1;
        }
        CHECK(seen[1] && seen[2] && seen[3]);                                  /* "Navigate back" of pane A is disabled just now (no focus), the others are reachable */
    }

    /* the strip hides with the setting; the content moves up into its room */
    ex_ui_layout(ui, win, 1000, 600);
    content_y_with = ex_panes_header(panes, ga)->y + ex_panes_header(panes, ga)->h;
    ex_panes_set_header_visible(panes, 0);
    ex_ui_layout(ui, win, 1000, 600);
    CHECK(ex_panes_header(panes, ga)->hidden && ex_panes_header(panes, ga)->h == 0.0f);
    {
        ExWidget *c = ex_panes_content(panes, ta);
        CHECK(c && c->y < content_y_with);
    }
    ex_panes_set_header_visible(panes, 1);
    ex_ui_layout(ui, win, 1000, 600);
    CHECK(!ex_panes_header(panes, ga)->hidden && ex_panes_header(panes, ga)->h > 0);
    (void)tg;

    ex_ui_release(ui, win);
    ex_widget_remove(win, ex_panes_root(panes));                                   /* the panes free their own root */
    ex_widget_free(win);
    ex_panes_free(panes);
    ex_ws_free(ws);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
}

int main(void) {
    history();
    header();
    if (failures) { fprintf(stderr, "pane_header: %d failure(s)\n", failures); return 1; }
    printf("pane_header: ok\n");
    return 0;
}
