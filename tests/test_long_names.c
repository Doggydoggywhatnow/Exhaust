/* long_names (TESTS.md T-TEXT-BOX-01, ISSUES 19): very long and very short names and input, everywhere.
 *  - a vault of notes, folders, aliases, headings, tags and properties with very long names (no blanks, with blanks, CJK, combining
 *    marks) is shown in every panel, at the narrowest sidebar width too, and in the dialogs (quick switcher, command palette, rename,
 *    move, file menu);
 *  - every text box found in those screens is given empty input, one character, 5,000 characters in one word, 5,000 with blanks, CJK,
 *    right-to-left text, combining marks, pasted line breaks and emoji;
 *  - after every step the layout is checked: no visible widget lies outside the window, no visible button is cut off by its parent,
 *    nothing is NaN or negative; and the frame is drawn (the sanitizer builds catch overruns).
 * A new text box that appears in these screens is covered without writing a new test. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_findbar.h"
#include "exhaust_graphpanel.h"
#include "exhaust_panes.h"
#include "exhaust_graphview.h"
#include "exhaust_panels.h"
#include "exhaust_vaultscreen.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "long_names_vault"
#define WIN_W 1000
#define WIN_H 700

static ExPanels *g_p;
static ExUi *g_ui;
static char g_active[260];

static void put(const char *rel, const char *text) {
    char p[800], *d, *s;
    snprintf(p, sizeof p, V "/%s", rel);
    d = malloc(strlen(p) + 1); strcpy(d, p);
    s = strrchr(d, '/'); if (s) { *s = 0; ex_platform_make_dirs(d); }
    free(d);
    ex_platform_write_file_atomic(p, text, strlen(text));
}

static void h_open(void *u, const char *path, size_t offset, int nt) { (void)u; (void)path; (void)offset; (void)nt; }
static const char *h_active(void *u) { (void)u; return g_active[0] ? g_active : NULL; }
static char *h_text(void *u, size_t *len) { char p[800]; (void)u; snprintf(p, sizeof p, V "/%s", g_active); return g_active[0] ? ex_platform_read_file(p, len) : NULL; }
static char *h_read(void *u, const char *path, size_t *len) { char p[800]; (void)u; snprintf(p, sizeof p, V "/%s", path); return ex_platform_read_file(p, len); }
static int h_replace(void *u, const char *path, const char *text, size_t len) { (void)u; (void)path; (void)text; (void)len; return 0; }
static void h_select(void *u, size_t from, size_t to) { (void)u; (void)from; (void)to; }
static void h_msg(void *u, const char *t) { (void)u; (void)t; }
static int h_move(void *u, const char *from, const char *to) { (void)u; (void)from; (void)to; return 0; }
static void h_flush(void *u) { (void)u; }
static void h_query(void *u, const char *path, const char *text) { (void)u; (void)path; (void)text; }
static int h_fileop(void *u, int op, const char *path, const char *arg) { (void)u; (void)op; (void)path; (void)arg; return 0; }

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

static void pump(int ms) { int i; for (i = 0; i < ms / 5; i++) { ex_panels_update(g_p); SDL_Delay(5); } }

/* ---- the layout checks ---- */

static int visible(const ExWidget *w) { for (; w; w = w->parent) if (w->hidden) return 0; return 1; }

static int g_problems;
static void problem(const char *where, const ExWidget *w, const char *what) {
    fprintf(stderr, "%s: \"%s\" (type %d) %s: x=%.0f y=%.0f w=%.0f h=%.0f\n", where, w->name ? w->name : "?", (int)w->type, what, w->x, w->y, w->w, w->h);
    g_problems++;
}

static void walk(const ExWidget *w, const ExWidget *root, const char *where) {
    for (; w; w = w->next) {
        if (w->hidden) continue;
        if (!isfinite(w->x) || !isfinite(w->y) || !isfinite(w->w) || !isfinite(w->h) || w->w < 0 || w->h < 0) problem(where, w, "has a bad rectangle");
        else if (w->w > 0 && w->h > 0) {
            if (w->x < -1.0f || w->y < -1.0f || w->x + w->w > (float)WIN_W + 1.0f || w->y + w->h > (float)WIN_H + 1.0f) problem(where, w, "lies outside the window");
            if (w->type == EX_WIDGET_BUTTON && w->parent && w->parent != root && w->parent->w > 0 &&
                (w->x + w->w > w->parent->x + w->parent->w + 1.0f || w->x < w->parent->x - 1.0f))
                problem(where, w, "is a button cut off by its parent");
        }
        walk(w->first_child, root, where);
    }
}

static void check_layout(ExRenderer *r, ExWidget *root, const char *where) {
    ExWidget *ov = ex_panels_overlay(g_p);
    ex_ui_layout(g_ui, root, (float)WIN_W, (float)WIN_H);
    ex_renderer_clear(r, ex_theme_color(ex_ui_theme(g_ui), EX_COLOR_BACKGROUND));
    ex_ui_draw(g_ui, root, 1);
    walk(root, root, where);
    if (ov) {
        ex_ui_layout(g_ui, ov, (float)WIN_W, (float)WIN_H);
        ex_ui_draw(g_ui, ov, 1);
        walk(ov, ov, where);
    }
}

static void collect_editors(ExWidget *w, ExWidget **out, size_t *n, size_t cap) {
    for (; w; w = w->next) {
        if (w->type == EX_WIDGET_EDITOR && !w->hidden && visible(w) && *n < cap) out[(*n)++] = w;
        collect_editors(w->first_child, out, n, cap);
    }
}

/* ---- the inputs ---- */

static char *repeat(const char *unit, size_t times) {
    size_t u = strlen(unit);
    char *s = malloc(u * times + 1), *p = s;
    size_t i;
    for (i = 0; i < times; i++) { memcpy(p, unit, u); p += u; }
    *p = 0;
    return s;
}

typedef struct Input { const char *name; char *text; } Input;

static void make_inputs(Input *in) {
    in[0].name = "empty";            in[0].text = repeat("", 1);
    in[1].name = "one character";    in[1].text = repeat("x", 1);
    in[2].name = "one wide";         in[2].text = repeat("\xE6\x97\xA5", 1);
    in[3].name = "5000 one word";    in[3].text = repeat("W", 5000);
    in[4].name = "5000 with blanks"; in[4].text = repeat("word ", 1000);
    in[5].name = "CJK";              in[5].text = repeat("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 700);
    in[6].name = "right to left";    in[6].text = repeat("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D ", 400);
    in[7].name = "combining marks";  in[7].text = repeat("e\xCC\x81\xCC\x82\xCC\x83", 800);
    in[8].name = "pasted breaks";    in[8].text = repeat("line one\r\nline two\n", 60);
    in[9].name = "emoji";            in[9].text = repeat("\xF0\x9F\x98\x80\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", 300);
    in[10].name = "only blanks";     in[10].text = repeat(" ", 300);
}
#define NINPUTS 11

static void clear_field(void) {
    ex_ui_key(g_ui, EX_KEY_A, EX_MOD_CTRL);
    ex_ui_key(g_ui, EX_KEY_BACKSPACE, 0);
}

static size_t g_editors_tested;

/* every visible text box of a screen gets every input */
static void test_text_boxes(ExRenderer *r, ExWidget *root, const char *screen, Input *in) {
    ExWidget *ed[32];
    size_t n = 0, i;
    int k;
    ExWidget *ov = ex_panels_overlay(g_p);
    ex_ui_layout(g_ui, root, (float)WIN_W, (float)WIN_H);
    collect_editors(root, ed, &n, 32);
    if (ov) { ex_ui_layout(g_ui, ov, (float)WIN_W, (float)WIN_H); collect_editors(ov, ed, &n, 32); }
    for (i = 0; i < n; i++) {
        for (k = 0; k < NINPUTS; k++) {
            char where[200];
            ex_ui_set_focus(g_ui, ed[i]);
            clear_field();
            ex_ui_text_input(g_ui, in[k].text, strlen(in[k].text));
            snprintf(where, sizeof where, "%s / %s / %s", screen, ed[i]->name ? ed[i]->name : "text box", in[k].name);
            pump(20);
            check_layout(r, root, where);
        }
        ex_ui_set_focus(g_ui, ed[i]);
        clear_field();
        g_editors_tested++;
    }
}

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExWidget *files, *tree, *root;
    ExSettings *settings;
    ExFeatures *features;
    ExCommands *commands;
    ExWorkspace *ws;
    ExVault *vault;
    ExPanelsInit pi;
    Input in[NINPUTS];
    char *w200, *wb, *cjk, text[6000], rel[1200], *bigtag;
    static const char *panels[] = { "files", "search", "bookmarks", "backlinks", "outgoing", "tags", "properties", "outline", "footnotes" };
    size_t i;
    int narrow;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("long_names_index");

    w200 = repeat("N", 200);                                   /* the longest name most file systems allow is 255 bytes */
    wb = repeat("A very long name with many words in it ", 5);
    cjk = repeat("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 30);
    bigtag = repeat("t", 150);
    snprintf(rel, sizeof rel, "%s.md", w200);
    snprintf(text, sizeof text, "---\naliases: [%s, %s]\n%s: %s\ntags: [%s]\n---\n# %s\n\nSee [[%s]] and [[%s]] and [[%s]]. #%s\n\n## %s\n\nText[^1].\n\n[^1]: %s\n", wb, cjk, w200, wb, bigtag, wb, w200, wb, cjk, bigtag, w200, wb);
    put(rel, text);
    snprintf(rel, sizeof rel, "%s.md", wb);
    snprintf(text, sizeof text, "# %s\n\nLinks back to [[%s]]. Mentions %s without a link.\n", wb, w200, w200);
    put(rel, text);
    snprintf(rel, sizeof rel, "%s.md", cjk);
    put(rel, "# cjk\n\ntext\n");
    snprintf(rel, sizeof rel, "%s/%s/Inner note.md", w200, wb);
    put(rel, "# Inner\n");
    put("Home.md", "# Home\n\nHello.\n");
    snprintf(g_active, sizeof g_active, "%s.md", w200);

    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("long_names", WIN_W, WIN_H);
    g_ui = ex_ui_create(r, &theme, 16.0f);
    vault = ex_vault_open(V);
    settings = ex_settings_open(V "/.exhaust", NULL);
    features = ex_features_create(settings);
    commands = ex_commands_create(features);
    ws = ex_ws_create();
    files = ex_explorer_sidebar(g_ui, vault, NULL, NULL, &tree);
    memset(&pi, 0, sizeof pi);
    pi.ui = g_ui; pi.vault_root = ex_vault_path(vault); pi.vault_id = ex_vault_id(vault); pi.settings = settings; pi.features = features; pi.commands = commands; pi.ws = ws; pi.files = files;
    pi.index_path = "long_names_index/index.db";
    pi.host.open_note = h_open; pi.host.active_path = h_active; pi.host.active_text = h_text; pi.host.read_text = h_read; pi.host.replace_text = h_replace;
    pi.host.select_range = h_select; pi.host.move_file = h_move; pi.host.message = h_msg; pi.host.flush_notes = h_flush; pi.host.query_results = h_query; pi.host.file_op = h_fileop;
    g_p = ex_panels_create(&pi);
    CHECK(g_p != NULL);
    if (!g_p) return 1;
    ex_panels_register(g_p);
    root = ex_widget_box(g_ui, 0, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    ex_widget_add(root, ex_panels_left(g_p));
    { ExWidget *mid = ex_widget_label(g_ui, "(the open note)"); mid->width = ex_size_grow(); mid->height = ex_size_grow(); ex_widget_add(root, mid); }
    ex_widget_add(root, ex_panels_right(g_p));
    ex_panels_left(g_p)->bg_token = ex_panels_right(g_p)->bg_token = EX_COLOR_BACKGROUND_ALT;
    { int j; for (j = 0; j < 600 && !ex_panels_index_ready(g_p); j++) { ex_panels_update(g_p); SDL_Delay(10); } }
    CHECK(ex_panels_index_ready(g_p));
    make_inputs(in);

    /* 1. every panel shows the long names, at the usual and at the narrowest sidebar width */
    for (narrow = 0; narrow < 2; narrow++) {
        ex_panels_left(g_p)->width = ex_size_px(g_ui, narrow ? EX_SIDEBAR_MIN_WIDTH : 300);
        ex_panels_right(g_p)->width = ex_size_px(g_ui, narrow ? EX_SIDEBAR_MIN_WIDTH : 340);
        for (i = 0; i < sizeof panels / sizeof *panels; i++) {
            char where[100];
            ex_panels_show(g_p, panels[i]);
            pump(150);
            snprintf(where, sizeof where, "panel %s (%s)", panels[i], narrow ? "minimum width" : "usual");
            check_layout(r, root, where);
            test_text_boxes(r, root, where, in);
        }
        ex_panels_search_for(g_p, w200);
        pump(300);
        check_layout(r, root, narrow ? "search results for a long name (minimum width)" : "search results for a long name");
    }
    ex_panels_left(g_p)->width = ex_size_px(g_ui, 300);
    ex_panels_right(g_p)->width = ex_size_px(g_ui, 340);

    /* 2. the dialogs */
    ex_panels_open_switcher(g_p);   pump(100); check_layout(r, root, "switcher");   test_text_boxes(r, root, "switcher", in);
    ex_ui_text_input(g_ui, "N", 1); pump(100); check_layout(r, root, "switcher, a long name matched");
    ex_panels_close_dialog(g_p);
    ex_panels_open_palette(g_p);    pump(100); check_layout(r, root, "palette");    test_text_boxes(r, root, "palette", in);
    ex_panels_close_dialog(g_p);
    snprintf(rel, sizeof rel, "%s.md", w200);
    ex_panels_rename_file(g_p, rel);        pump(100); check_layout(r, root, "rename dialog, a long name");       test_text_boxes(r, root, "rename dialog", in);
    ex_panels_close_dialog(g_p);
    ex_panels_move_file_prompt(g_p, rel);   pump(100); check_layout(r, root, "move dialog, a long name");         test_text_boxes(r, root, "move dialog", in);
    ex_panels_close_dialog(g_p);
    ex_panels_file_menu(g_p, rel);          pump(100); check_layout(r, root, "file menu, a long name");
    ex_panels_close_dialog(g_p);

    /* 3. the text boxes that are not in the panels: the find bar and its replace row, the inline title, the vault manager */
    {
        ExDoc *doc = ex_doc_create("Some text\nto search in\n", 23), *tdoc = ex_doc_create("Title", 5);
        ExWidget *ed = ex_widget_editor(g_ui, doc, "note"), *title = ex_widget_editor(g_ui, tdoc, "Note title"), *box = ex_widget_box(g_ui, 1, "find screen"), *tbox = ex_widget_box(g_ui, 1, "title screen");
        ExFindBar *fb = ex_findbar_create(g_ui);
        ExVaultManager *mgr;
        ExVaultScreen *vs;
        ExVaultScreenHooks hooks;
        char vp[600];
        ex_editor_set_mode(ed->editor, EX_EDITOR_SOURCE);
        ed->width = ex_size_grow(); ed->height = ex_size_grow();
        box->width = ex_size_grow(); box->height = ex_size_grow();
        ex_widget_add(box, ed);
        ex_widget_add(box, ex_findbar_widget(fb));
        ex_findbar_open(fb, ed, 1);
        test_text_boxes(r, box, "find and replace bar", in);
        ex_findbar_set_pattern(fb, in[3].text);                       /* a very long pattern and replacement, in every state of the bar */
        ex_findbar_set_replacement(fb, in[3].text);
        ex_findbar_update(fb);
        check_layout(r, box, "find bar with a very long pattern");
        ex_findbar_set_option(fb, EX_FINDBAR_REGEX, 1);
        ex_findbar_set_pattern(fb, "(((");                           /* an error message in the count */
        ex_findbar_update(fb);
        check_layout(r, box, "find bar with a pattern error");

        ex_editor_set_mode(title->editor, EX_EDITOR_PLAIN);
        ex_editor_set_wrap(title->editor, 1);
        title->text_scale = 2.0f; title->bold = 1;
        title->width = ex_size_grow(); title->height = ex_size_token(EX_SIZE_SPACING, 7.0f);
        tbox->width = ex_size_grow(); tbox->height = ex_size_grow();
        ex_widget_add(tbox, title);
        test_text_boxes(r, tbox, "inline title", in);

        snprintf(vp, sizeof vp, "long_names_data/%s", w200);
        ex_platform_remove_tree("long_names_data");
        ex_platform_make_dirs(vp);
        mgr = ex_vaultmgr_create("long_names_data/vaults.ini", "long_names_data/state");
        CHECK(mgr != NULL);
        if (mgr) {
            memset(&hooks, 0, sizeof hooks);
            ex_vaultmgr_open_folder(mgr, vp, NULL);
            snprintf(vp, sizeof vp, "long_names_data/%s", wb);
            ex_platform_make_dirs(vp);
            ex_vaultmgr_open_folder(mgr, vp, NULL);
            vs = ex_vaultscreen_create(g_ui, mgr, &hooks, NULL, "0.1.0");
            if (vs) {
                ex_vaultscreen_update(vs);
                check_layout(r, ex_vaultscreen_root(vs), "vault manager, long vault names");
                test_text_boxes(r, ex_vaultscreen_root(vs), "vault manager", in);
                ex_vaultscreen_free(vs);
            }
            ex_vaultmgr_free(mgr);
        }
        {   /* the graph: nodes with very long names, labels drawn at every zoom, nothing outside the widget */
            ExGraphModel *gm = ex_graphmodel_create();
            ExGraph *gr = calloc(1, sizeof *gr);
            ExWidget *gbox = ex_widget_box(g_ui, 1, "graph box"), *gv;
            ExGraphViewStats gs;
            const char *names[4] = {NULL, NULL, "Short.md", NULL};
            char long1[400], long2[400];
            int k, zoom;
            snprintf(long1, sizeof long1, "%s.md", w200);
            snprintf(long2, sizeof long2, "Folder/%s %s.md", cjk, w200);
            names[0] = long1; names[1] = long2; names[3] = "A name with blanks that goes on and on and on and on and on and on and on and on and on.md";
            gr->nodes = calloc(4, sizeof *gr->nodes); gr->edges = calloc(4, sizeof *gr->edges);
            for (k = 0; k < 4; k++) { size_t l = strlen(names[k]) + 1; gr->nodes[k].path = malloc(l); memcpy(gr->nodes[k].path, names[k], l); gr->nodes[k].kind = k == 3 ? EX_GRAPH_UNRESOLVED : EX_GRAPH_NOTE; }
            for (k = 0; k < 4; k++) { gr->edges[k].from = (uint32_t)k; gr->edges[k].to = (uint32_t)((k + 1) % 4); gr->nodes[k].degree = 2; }
            gr->n = 4; gr->ne = 4;
            ex_graphmodel_set_graph(gm, gr);
            wait_settled_model(gm, 20000);
            gbox->width = ex_size_grow(); gbox->height = ex_size_grow();
            gv = ex_widget_graph(g_ui, gm, "Graph view", NULL);
            ex_widget_add(gbox, gv);
            for (zoom = 0; zoom < 4; zoom++) {
                int w = 0, h = 0;
                ex_renderer_size(r, &w, &h);
                ex_graphview_wheel(gv, zoom == 0 ? 0.0f : 6.0f, (float)w / 2.0f, (float)h / 2.0f);
                ex_ui_layout(g_ui, gbox, (float)w, (float)h);
                ex_ui_draw(g_ui, gbox, 1);
                check_layout(r, gbox, "graph view, long names");
                ex_graphview_stats(gv, &gs);
                CHECK(gs.nodes_drawn <= 4);
                if (zoom == 3) CHECK(gs.labels_drawn > 0);
            }
            ex_ui_release(g_ui, gbox);
            ex_widget_free(gbox);
            ex_graphmodel_free(gm);
        }
        {   /* the pane header (5d): a note with a very long name, in a pane as narrow as one can be, and a graph */
            ExWorkspace *ws = ex_ws_create();
            ExPanesHooks ph;
            ExPanes *panes;
            ExWidget *win = ex_widget_box(g_ui, 0, "window"), *hdr;
            ExId ga = ex_ws_active_group(ws), gb;
            char longrel[400];
            int pass;
            memset(&ph, 0, sizeof ph);
            panes = ex_panes_create(g_ui, ws, &ph, NULL);
            win->width = ex_size_grow(); win->height = ex_size_grow();
            ex_widget_add(win, ex_panes_root(panes));
            snprintf(longrel, sizeof longrel, "Folder/%s.md", w200);
            ex_ws_new_tab(ws, ga, longrel, "note");
            gb = ex_ws_split(ws, ga, EX_SPLIT_HORIZONTAL, 1, 0);
            ex_ws_new_tab(ws, gb, "", "graph");
            ex_ws_history_visit(ws, ga, longrel, "note");
            for (pass = 0; pass < 3; pass++) {                                           /* the title is cut to the room of the last layout: lay out again */
                ex_panes_update(panes);
                check_layout(r, win, "pane header, a long title");
            }
            {   /* the right sidebar's group of tabs (5e): the same long title in a sidebar as narrow as one can be */
                ExId sg2 = ex_ws_side_group_ensure(ws, EX_SIDE_RIGHT);
                ExWidget *sb = ex_widget_box(g_ui, 1, "sidebar");
                sb->width = ex_size_px(g_ui, 200); sb->height = ex_size_grow();
                ex_widget_add(sb, ex_panes_side_root(panes));
                ex_widget_add(win, sb);
                ex_ws_new_tab(ws, sg2, longrel, "note");                                   /* one tab: a strip that is narrower than its tabs is clipped, as in a narrow main pane */
                for (pass = 0; pass < 3; pass++) { ex_panes_update(panes); check_layout(r, win, "sidebar tabs, a long title"); }
                CHECK(ex_panes_header(panes, sg2) != NULL && ex_panes_header(panes, sg2)->w > 0);
                ex_ui_release(g_ui, win);
                ex_widget_remove(sb, ex_panes_side_root(panes));
            }
            hdr = ex_panes_header(panes, ga);
            CHECK(hdr && hdr->w > 0 && hdr->h > 0);
            {
                const char *shown = ex_panes_header_title(panes, ga);
                CHECK(shown && strlen(shown) < strlen(longrel) && strstr(shown, "\xE2\x80\xA6"));                  /* cut with an ellipsis, the whole title stays the accessible name */
            }
            ex_ui_release(g_ui, win);
            ex_widget_remove(win, ex_panes_root(panes));
            ex_widget_free(win);
            ex_panes_free(panes);
            ex_ws_free(ws);
        }
        {   /* the graph's gear panel (5b): every section open, long queries in every field, and a pane as narrow as a sidebar can be */
            ExGraphModel *gm = ex_graphmodel_create();
            ExWidget *gbox = ex_widget_box(g_ui, 1, "graph box"), *gv;
            ExGraphPanel *gp;
            static const char *heads[4] = {"Filters", "Groups", "Display", "Forces"};
            int k;
            gbox->width = ex_size_grow(); gbox->height = ex_size_grow();
            gv = ex_widget_graph(g_ui, gm, "Graph view", NULL);
            ex_widget_add(gbox, gv);
            gp = ex_graphpanel_create(g_ui, gv, gm, 0, NULL, NULL);
            CHECK(gp != NULL);
            if (gp) {
                ExWidget *ng;
                ex_graphpanel_open(gp, 1);
                ng = ex_graphpanel_find(gp, "New group");
                for (k = 0; k < 6 && ng; k++) ng->on_click(ng, ng->on_click_user);
                for (k = 0; k < 4; k++) {                                              /* one section at a time: each with its fields */
                    ExWidget *hd = ex_graphpanel_find(gp, heads[k]);
                    if (hd) hd->on_click(hd, hd->on_click_user);
                    pump(50);
                    check_layout(r, gbox, "graph settings panel");
                    test_text_boxes(r, gbox, heads[k], in);
                }
                ex_ui_layout(g_ui, gbox, 200.0f, 400.0f);                              /* a narrow pane: the panel may not push anything out of the widget */
                ex_ui_draw(g_ui, gbox, 1);
                ex_ui_layout(g_ui, gbox, 200.0f, 400.0f);                              /* (the panel sizes itself while drawing, so lay out again) */
                ex_ui_draw(g_ui, gbox, 1);
                CHECK(ex_graphpanel_box(gp)->x >= gbox->x - 0.5f && ex_graphpanel_box(gp)->x + ex_graphpanel_box(gp)->w <= gbox->x + gbox->w + 0.5f);
            }
            ex_ui_release(g_ui, gbox);
            ex_widget_free(gbox);
            ex_graphmodel_free(gm);
        }
        {   /* the graph's gear panel (5c: the local graph's, with the Depth slider and the wand): every section open, long queries in every field, and a pane as narrow as a sidebar can be */
            ExGraphModel *gm = ex_graphmodel_create();
            ex_graphmodel_set_local(gm, 1);
            ExWidget *gbox = ex_widget_box(g_ui, 1, "graph box"), *gv;
            ExGraphPanel *gp;
            static const char *heads[4] = {"Filters", "Groups", "Display", "Forces"};
            int k;
            gbox->width = ex_size_grow(); gbox->height = ex_size_grow();
            gv = ex_widget_graph(g_ui, gm, "Graph view", NULL);
            ex_widget_add(gbox, gv);
            gp = ex_graphpanel_create(g_ui, gv, gm, 1, NULL, NULL);
            CHECK(gp != NULL);
            if (gp) {
                ExWidget *ng;
                ex_graphpanel_open(gp, 1);
                ng = ex_graphpanel_find(gp, "New group");
                for (k = 0; k < 6 && ng; k++) ng->on_click(ng, ng->on_click_user);
                for (k = 0; k < 4; k++) {                                              /* one section at a time: each with its fields */
                    ExWidget *hd = ex_graphpanel_find(gp, heads[k]);
                    if (hd) hd->on_click(hd, hd->on_click_user);
                    pump(50);
                    check_layout(r, gbox, "local graph settings panel");
                    test_text_boxes(r, gbox, heads[k], in);
                }
                ex_ui_layout(g_ui, gbox, 200.0f, 400.0f);                              /* a narrow pane: the panel may not push anything out of the widget */
                ex_ui_draw(g_ui, gbox, 1);
                ex_ui_layout(g_ui, gbox, 200.0f, 400.0f);                              /* (the panel sizes itself while drawing, so lay out again) */
                ex_ui_draw(g_ui, gbox, 1);
                CHECK(ex_graphpanel_box(gp)->x >= gbox->x - 0.5f && ex_graphpanel_box(gp)->x + ex_graphpanel_box(gp)->w <= gbox->x + gbox->w + 0.5f);
            }
            ex_ui_release(g_ui, gbox);
            ex_widget_free(gbox);
            ex_graphmodel_free(gm);
        }
        ex_findbar_free(fb);
        ex_widget_free(box);
        ex_widget_free(tbox);
        ex_doc_free(doc);
        ex_doc_free(tdoc);
        ex_platform_remove_tree("long_names_data");
    }

    CHECK(g_editors_tested > 0);
    CHECK(g_problems == 0);
    fprintf(stderr, "long_names: %zu text boxes, %d problem(s)\n", g_editors_tested, g_problems);

    for (i = 0; i < NINPUTS; i++) free(in[i].text);
    free(w200); free(wb); free(cjk); free(bigtag);
    ex_panels_free(g_p);
    ex_widget_free(files);
    ex_widget_free(root);
    ex_commands_free(commands); ex_features_free(features); ex_settings_close(settings); ex_ws_free(ws); ex_vault_close(vault);
    ex_ui_free(g_ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    puts("ok");
    return 0;
}
