/* panels (sub-slice 4c): the panels and dialogs work on a real vault, headless: the quick switcher, the command palette, search,
 * backlinks (with unlinked mentions and the Link action), outgoing links, tags, properties, outline (with reordering), footnotes,
 * bookmarks, link suggestions, and renaming a note with the link update. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_panels.h"
#include "exhaust_platform.h"
#include "exhaust_query.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "panels_test_vault"

typedef struct Fake {
    ExPanels *p;
    ExVault *vault;
    char opened[260]; size_t offset; int opens;
    char active[260];
    char msg[300];
    int moved; char moved_from[260], moved_to[260];
    char *replaced; size_t selected_from;
} Fake;
static Fake F;

static void put(const char *rel, const char *text) {
    char p[400], *d, *s;
    snprintf(p, sizeof p, V "/%s", rel);
    d = malloc(strlen(p) + 1); strcpy(d, p);
    s = strrchr(d, '/'); if (s) { *s = 0; ex_platform_make_dirs(d); }
    free(d);
    ex_platform_write_file_atomic(p, text, strlen(text));
}

static void h_open(void *u, const char *path, size_t offset, int nt) { (void)u; (void)nt; snprintf(F.opened, sizeof F.opened, "%s", path); F.offset = offset; F.opens++; }
static const char *h_active(void *u) { (void)u; return F.active[0] ? F.active : NULL; }
static char *h_text(void *u, size_t *len) { char p[400]; (void)u; snprintf(p, sizeof p, V "/%s", F.active); return F.active[0] ? ex_platform_read_file(p, len) : NULL; }
static char *h_read(void *u, const char *path, size_t *len) { char p[400]; (void)u; snprintf(p, sizeof p, V "/%s", path); return ex_platform_read_file(p, len); }
static int h_replace(void *u, const char *path, const char *text, size_t len) { char p[400]; (void)u; free(F.replaced); F.replaced = malloc(len + 1); memcpy(F.replaced, text, len); F.replaced[len] = 0; snprintf(p, sizeof p, V "/%s", path); return ex_platform_write_file_atomic(p, text, len); }
static void h_select(void *u, size_t from, size_t to) { (void)u; (void)to; F.selected_from = from; }
static void h_msg(void *u, const char *t) { (void)u; snprintf(F.msg, sizeof F.msg, "%s", t); }
static int h_move(void *u, const char *from, const char *to) {
    char a[500], b[500], *d, *s;
    (void)u;
    snprintf(a, sizeof a, V "/%s", from); snprintf(b, sizeof b, V "/%s", to);
    d = malloc(strlen(b) + 1); strcpy(d, b); s = strrchr(d, '/'); if (s) { *s = 0; ex_platform_make_dirs(d); } free(d);
    F.moved++; snprintf(F.moved_from, sizeof F.moved_from, "%s", from); snprintf(F.moved_to, sizeof F.moved_to, "%s", to);
    return ex_platform_rename_noreplace(a, b);
}
static void h_flush(void *u) { (void)u; }
static char g_expanded[4000];
static void h_query(void *u, const char *path, const char *text) { (void)u; (void)path; snprintf(g_expanded, sizeof g_expanded, "%s", text); }

static ExWidget *find(ExWidget *w, ExWidgetType type, const char *name) {
    for (; w; w = w->next) {
        ExWidget *r;
        if (w->type == type && w->name && !strcmp(w->name, name)) return w;
        if ((r = find(w->first_child, type, name)) != NULL) return r;
    }
    return NULL;
}

static ExUi *g_ui;
static void pump(int ms) {
    int i;
    for (i = 0; i < ms / 5; i++) { ex_panels_update(F.p); SDL_Delay(5); }
}
static int wait_rows(ExWidget *list, size_t min, int ms) {
    int i;
    for (i = 0; i < ms / 5; i++) { ex_panels_update(F.p); if (ex_list_count(list) >= min) return 1; SDL_Delay(5); }
    return ex_list_count(list) >= min;
}
static int has_row(ExWidget *list, const char *text) {
    size_t i;
    for (i = 0; i < ex_list_count(list); i++) if (strstr(ex_list_row(list, i)->text, text)) return 1;
    return 0;
}
static void type(const char *s) { ex_ui_text_input(g_ui, s, strlen(s)); }
static void key(ExKey k, int mods) { ex_ui_key(g_ui, k, mods); }


static void shot(ExRenderer *r, ExWidget *root, const char *path) {
    int w, h, i;
    uint8_t *px;
    FILE *f;
    ex_renderer_size(r, &w, &h);
    ex_ui_layout(g_ui, root, (float)w, (float)h);
    ex_renderer_clear(r, ex_theme_color(ex_ui_theme(g_ui), EX_COLOR_BACKGROUND));
    ex_ui_draw(g_ui, root, 1);
    if (ex_panels_overlay(F.p)) { ExWidget *ov = ex_panels_overlay(F.p); ex_ui_layout(g_ui, ov, (float)w, (float)h); ex_ui_draw(g_ui, ov, 1); }
    px = ex_renderer_read_rgb(r, &w, &h);
    f = px ? fopen(path, "wb") : NULL;
    if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(px, 1, (size_t)w * (size_t)h * 3, f); fclose(f); }
    (void)i;
    free(px);
}

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExWidget *tree, *files, *list, *root;
    ExSettings *settings;
    ExFeatures *features;
    ExCommands *commands;
    ExWorkspace *ws;
    ExPanelsInit pi;
    char index_path[300];
    char *t;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("panels_test_index");
    put("Daily notes.md", "# Daily notes\n\nToday.\n");
    put("Alpha.md", "---\ntags: [work]\nstatus: active\naliases: [First]\n---\n# Alpha\n\nSee [[Beta]] and [[Missing note]]. #project/x\n\n## Details\n\nText with a note[^a] ^blk1\n\n# Second\n\nMore.\n\n[^a]: The footnote.\n");
    put("Beta.md", "# Beta\n\nBeta links to [[Alpha]]. Mentions Alpha without a link, and alpha again.\n\n#work\n");
    put("Gamma.md", "---\nstatus: waiting\n---\nPlain note about Beta, not linked.\n");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("panels", 1000, 700);
    g_ui = ex_ui_create(r, &theme, 16.0f);
    F.vault = ex_vault_open(V);
    settings = ex_settings_open(V "/.exhaust", NULL);
    features = ex_features_create(settings);
    commands = ex_commands_create(features);
    ws = ex_ws_create();
    files = ex_explorer_sidebar(g_ui, F.vault, NULL, NULL, &tree);
    memset(&pi, 0, sizeof pi);
    snprintf(index_path, sizeof index_path, "panels_test_index/index.db");
    pi.ui = g_ui; pi.vault_root = ex_vault_path(F.vault); pi.vault_id = ex_vault_id(F.vault); pi.settings = settings; pi.features = features; pi.commands = commands; pi.ws = ws; pi.files = files;
    pi.index_path = index_path;
    pi.host.open_note = h_open; pi.host.active_path = h_active; pi.host.active_text = h_text; pi.host.read_text = h_read; pi.host.replace_text = h_replace;
    pi.host.select_range = h_select; pi.host.move_file = h_move; pi.host.message = h_msg; pi.host.flush_notes = h_flush; pi.host.query_results = h_query;
    F.p = ex_panels_create(&pi);
    CHECK(F.p != NULL);
    if (!F.p) return 1;
    ex_panels_register(F.p);
    root = ex_widget_box(g_ui, 0, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    ex_panels_left(F.p)->width = ex_size_px(g_ui, 300);
    ex_panels_right(F.p)->width = ex_size_px(g_ui, 340);
    ex_panels_left(F.p)->bg_token = ex_panels_right(F.p)->bg_token = EX_COLOR_BACKGROUND_ALT;
    ex_widget_add(root, ex_panels_left(F.p));
    { ExWidget *mid = ex_widget_label(g_ui, "(the open note)"); mid->width = ex_size_grow(); mid->height = ex_size_grow(); mid->pad_x = 2; mid->pad_y = 2; mid->text_token = EX_COLOR_TEXT_MUTED; ex_widget_add(root, mid); }
    ex_widget_add(root, ex_panels_right(F.p));
    {   int i; for (i = 0; i < 400 && !ex_panels_index_ready(F.p); i++) { ex_panels_update(F.p); SDL_Delay(10); } }
    CHECK(ex_panels_index_ready(F.p));

    /* search: results, the file row and its matches; a click opens the note at the match */
    ex_panels_search_for(F.p, "beta");
    list = find(root, EX_WIDGET_LIST, "Search results");
    CHECK(list && wait_rows(list, 3, 3000));
    CHECK(list && has_row(list, "Alpha") && has_row(list, "Beta") && has_row(list, "Gamma"));
    if (list) {
        size_t i;
        for (i = 0; i < ex_list_count(list); i++) if (ex_list_row(list, i)->indent == 1 && strstr(ex_list_row(list, i)->text, "Beta links")) break;
        CHECK(i < ex_list_count(list));
        if (i < ex_list_count(list)) { ex_list_select(list, i); ex_ui_set_focus(g_ui, list); key(EX_KEY_ENTER, 0); CHECK(!strcmp(F.opened, "Beta.md") && F.offset > 0); }
    }
    ex_panels_search_for(F.p, "tag:#work");
    pump(600);
    CHECK(list && wait_rows(list, 2, 3000) && has_row(list, "Alpha") && has_row(list, "Beta") && !has_row(list, "Gamma"));
    CHECK(!strcmp(ex_ws_search_text(ws), "tag:#work"));
    ex_panels_search_for(F.p, "(unclosed");
    pump(600);
    CHECK(ex_list_count(list) == 0);

    /* backlinks of Alpha: linked and unlinked mentions, and the Link action */
    snprintf(F.active, sizeof F.active, "Alpha.md");
    ex_panels_show(F.p, "backlinks");
    list = find(root, EX_WIDGET_LIST, "Linked mentions");
    CHECK(list && wait_rows(list, 2, 3000));
    CHECK(list && has_row(list, "Beta") && has_row(list, "Beta links to"));
    {
        ExWidget *unl = find(root, EX_WIDGET_LIST, "Unlinked mentions"), *b = find(root, EX_WIDGET_BUTTON, "Show unlinked mentions");
        CHECK(unl && b);
        if (b && b->on_click) b->on_click(b, b->on_click_user);
        CHECK(unl && wait_rows(unl, 2, 3000));
        CHECK(unl && has_row(unl, "Mentions Alpha") && ex_list_row(unl, 1)->action != NULL);
        if (unl && ex_list_count(unl) >= 2) {
            ex_list_select(unl, 1);
            { ExListHandlers *dummy = NULL; (void)dummy; }
        }
    }

    /* outgoing links: resolved and unresolved */
    ex_panels_show(F.p, "outgoing");
    list = find(root, EX_WIDGET_LIST, "Outgoing links");
    CHECK(list && wait_rows(list, 2, 3000) && has_row(list, "Beta") && has_row(list, "Missing note"));
    if (list) {
        size_t i;
        for (i = 0; i < ex_list_count(list); i++) if (strstr(ex_list_row(list, i)->text, "Missing")) { CHECK(ex_list_row(list, i)->muted); ex_list_select(list, i); ex_ui_set_focus(g_ui, list); key(EX_KEY_ENTER, 0); CHECK(!strcmp(F.opened, "Missing note.md")); }
    }

    /* tags (nested), properties */
    ex_panels_show(F.p, "tags");
    list = find(root, EX_WIDGET_LIST, "Tags");
    CHECK(list && wait_rows(list, 3, 3000) && has_row(list, "#work") && has_row(list, "#project") && has_row(list, "#x"));
    ex_panels_show(F.p, "properties");
    list = find(root, EX_WIDGET_LIST, "Properties");
    CHECK(list && wait_rows(list, 3, 3000) && has_row(list, "status") && has_row(list, "tags") && has_row(list, "aliases"));

    /* outline and footnotes of the active note; reordering a section */
    ex_panels_show(F.p, "outline");
    list = find(root, EX_WIDGET_LIST, "Outline");
    CHECK(list && wait_rows(list, 3, 3000) && has_row(list, "Alpha") && has_row(list, "Details") && has_row(list, "Second"));
    if (list && ex_list_count(list) == 3) {
        ExListHandlers *none = NULL;
        (void)none;
        ex_ui_set_focus(g_ui, list);
        ex_list_select(list, 2);
        key(EX_KEY_ENTER, 0);
        CHECK(F.selected_from > 0);
    }
    ex_features_set(features, "footnotes", 1);                          /* off until the user turns it on */
    ex_panels_show(F.p, "footnotes");
    list = find(root, EX_WIDGET_LIST, "Footnotes");
    CHECK(list && wait_rows(list, 1, 3000) && has_row(list, "The footnote."));

    /* bookmarks: a command adds the open note; the list shows it; it follows a rename */
    CHECK(ex_commands_run(commands, "bookmarks.bookmark") == 0);
    ex_panels_show(F.p, "bookmarks");
    list = find(root, EX_WIDGET_LIST, "Bookmarks");
    pump(100);
    CHECK(list && wait_rows(list, 1, 1000) && has_row(list, "Alpha"));

    /* link suggestions for [[ */
    {
        ExLinkSuggestion s[8];
        size_t n = ex_panels_suggest_links(F.p, "alph", s, 8);
        CHECK(n >= 1 && !strcmp(s[0].insert, "Alpha"));
        n = ex_panels_suggest_links(F.p, "first", s, 8);
        CHECK(n >= 1 && !strcmp(s[0].insert, "Alpha|First"));
        n = ex_panels_suggest_links(F.p, "Alpha#", s, 8);
        CHECK(n == 3 && !strcmp(s[1].insert, "Alpha#Details"));
        n = ex_panels_suggest_links(F.p, "Alpha#^", s, 8);
        CHECK(n == 1 && !strcmp(s[0].insert, "Alpha#^blk1"));
        n = ex_panels_suggest_links(F.p, "#Sec", s, 8);
        CHECK(n == 1 && !strcmp(s[0].insert, "#Second"));
    }

    /* the quick switcher: fuzzy match, Enter opens, Enter on an unmatched name creates */
    ex_panels_open_switcher(F.p);
    CHECK(ex_panels_modal(F.p));
    type("dn");
    pump(60);
    list = find(ex_panels_overlay(F.p), EX_WIDGET_LIST, "Notes");
    CHECK(list && ex_list_count(list) >= 2 && !strcmp(ex_list_row(list, 0)->text, "Daily notes"));
    key(EX_KEY_ENTER, 0);
    CHECK(!ex_panels_modal(F.p) && !strcmp(F.opened, "Daily notes.md"));
    ex_panels_open_switcher(F.p);
    type("Brand new idea");
    pump(60);
    key(EX_KEY_ENTER, 0);
    CHECK(!ex_panels_modal(F.p) && !strcmp(F.opened, "Brand new idea.md"));
    ex_panels_open_switcher(F.p);
    type("bad#name");
    pump(60);
    key(EX_KEY_ENTER, 0);
    CHECK(ex_panels_modal(F.p));                                         /* refused: the dialog stays */
    key(EX_KEY_ESCAPE, 0);
    CHECK(!ex_panels_modal(F.p));

    /* the command palette */
    ex_panels_open_palette(F.p);
    type("open today");
    pump(60);
    list = find(ex_panels_overlay(F.p), EX_WIDGET_LIST, "Commands");
    CHECK(list && ex_list_count(list) >= 1 && strstr(ex_list_row(list, 0)->text, "Open today"));
    key(EX_KEY_SPACE, EX_MOD_PRIMARY);                                   /* pin it */
    CHECK(strstr(ex_settings_get(settings, "command_palette", "pinned"), "daily-notes.open-today-s-daily-note") != NULL);
    key(EX_KEY_ESCAPE, 0);
    ex_panels_open_palette(F.p);
    pump(60);
    list = find(ex_panels_overlay(F.p), EX_WIDGET_LIST, "Commands");
    CHECK(list && ex_list_count(list) == ex_commands_count(commands) && strstr(ex_list_row(list, 0)->detail ? ex_list_row(list, 0)->detail : "", "pinned") != NULL);
    key(EX_KEY_ESCAPE, 0);

    /* reading mode: a query block shows its results */
    { const char *q = "# Q\n\n```query\ntag:#work\n```\n\nafter\n"; ex_panels_expand_queries(F.p, "Q.md", q, strlen(q)); }
    pump(800);
    CHECK(strstr(g_expanded, "- [[Alpha]]") && strstr(g_expanded, "- [[Beta]]") && !strstr(g_expanded, "```query") && strstr(g_expanded, "after"));

    /* the Link action of an unlinked mention */
    snprintf(F.active, sizeof F.active, "Alpha.md");
    ex_panels_show(F.p, "backlinks");
    ex_ui_layout(g_ui, root, 1000, 700);
    {
        ExWidget *unl = find(root, EX_WIDGET_LIST, "Unlinked mentions");
        CHECK(unl && unl->h > 20);
        if (unl && ex_list_count(unl) >= 2) {
            float rh = ex_list_row_height(unl);
            ex_list_mouse(g_ui, unl, EX_MOUSE_DOWN, unl->x + unl->w - 8, unl->y + rh * 1.5f, 1);      /* the "Link" button of the first mention */
            pump(1000);
            t = ex_platform_read_file(V "/Beta.md", NULL);
            CHECK(t && strstr(t, "Mentions [[Alpha]] without a link") != NULL);
            free(t);
        }
    }

    /* the backlinks at the bottom of the note */
    ex_settings_set_bool(settings, "backlinks", "show_at_bottom", 1);
    pump(300);
    CHECK(!ex_panels_bottom(F.p)->hidden);
    list = find(ex_panels_bottom(F.p), EX_WIDGET_LIST, "Backlinks in the note");
    CHECK(list && wait_rows(list, 1, 2000));
    ex_settings_set_bool(settings, "backlinks", "show_at_bottom", 0);
    pump(100);
    CHECK(ex_panels_bottom(F.p)->hidden);

    /* the page preview: only with Ctrl held (the setting), after a pause, and gone when the pointer leaves */
    ex_ui_layout(g_ui, root, 1000, 700);
    list = find(root, EX_WIDGET_LIST, "Linked mentions");
    CHECK(list && ex_list_count(list) >= 1);
    if (list && ex_list_count(list) >= 1) {
        float x = list->x + 30, y = list->y + ex_list_row_height(list) * 0.5f;
        ex_panels_pointer(F.p, root, x, y, 0);
        pump(700);
        CHECK(ex_panels_overlay(F.p) == NULL);
        ex_panels_pointer(F.p, root, x, y, 1);
        pump(1200);
        CHECK(ex_panels_overlay(F.p) != NULL && !ex_panels_modal(F.p));
        ex_panels_pointer(F.p, root, 5, 5, 1);
        CHECK(ex_panels_overlay(F.p) == NULL);
    }

    /* outline: dragging a heading to another place reorders the sections of the note */
    ex_panels_show(F.p, "outline");
    ex_ui_layout(g_ui, root, 1000, 700);
    list = find(root, EX_WIDGET_LIST, "Outline");
    CHECK(list && wait_rows(list, 3, 2000));
    if (list && ex_list_count(list) == 3) {
        float rh = ex_list_row_height(list);
        ex_list_mouse(g_ui, list, EX_MOUSE_DOWN, list->x + 40, list->y + rh * 2.5f, 1);
        ex_list_mouse(g_ui, list, EX_MOUSE_DRAG, list->x + 40, list->y + rh * 0.1f, 1);
        ex_list_mouse(g_ui, list, EX_MOUSE_UP, list->x + 40, list->y + rh * 0.1f, 1);
        CHECK(F.replaced && strstr(F.replaced, "# Second") && strstr(F.replaced, "# Alpha") && strstr(F.replaced, "# Second") < strstr(F.replaced, "# Alpha"));
        CHECK(F.replaced && strncmp(F.replaced, "---\ntags: [work]", 16) == 0);                  /* the properties stay at the top */
        ex_panels_update(F.p);
    }
    /* put the note back as it was (the fake host wrote it to disk) */
    put("Alpha.md", "---\ntags: [work]\nstatus: active\naliases: [First]\n---\n# Alpha\n\nSee [[Beta]] and [[Missing note]]. #project/x\n\n## Details\n\nText with a note[^a] ^blk1\n\n# Second\n\nMore.\n\n[^a]: The footnote.\n");

    /* saved searches: Save asks for a name, the saved search is listed, a click runs it again */
    ex_panels_search_for(F.p, "tag:#work");
    pump(500);
    {
        ExWidget *save = find(root, EX_WIDGET_BUTTON, "Save this search"), *saved = find(root, EX_WIDGET_LIST, "Saved searches");
        CHECK(save && saved);
        if (save) save->on_click(save, save->on_click_user);
        CHECK(ex_panels_modal(F.p));
        key(EX_KEY_END, 0);
        type("Work notes");
        key(EX_KEY_ENTER, 0);
        pump(300);
        CHECK(!ex_panels_modal(F.p) && saved && ex_list_count(saved) == 1 && !strcmp(ex_list_row(saved, 0)->text, "Work notes"));
        ex_panels_search_for(F.p, "something else");
        pump(400);
        ex_ui_layout(g_ui, root, 1000, 700);
        if (saved && ex_list_count(saved) == 1) {
            ex_list_mouse(g_ui, saved, EX_MOUSE_DOWN, saved->x + 20, saved->y + ex_list_row_height(saved) * 0.5f, 1);       /* one click */
            pump(500);
            CHECK(!strcmp(ex_ws_search_text(ws), "tag:#work"));
        }
        {
            ExSavedQueries *q = ex_saved_queries_load(V);
            CHECK(q && ex_saved_queries_count(q) == 1 && !strcmp(ex_saved_queries_get(q, 0)->text, "tag:#work"));
            ex_saved_queries_free(q);
        }
    }
    /* the search options */
    {
        ExWidget *collapse = find(root, EX_WIDGET_BUTTON, "Collapse results"), *sort = find(root, EX_WIDGET_BUTTON, "Sort order");
        size_t before;
        list = find(root, EX_WIDGET_LIST, "Search results");
        ex_panels_search_for(F.p, "beta");
        pump(500);
        before = list ? ex_list_count(list) : 0;
        if (collapse) collapse->on_click(collapse, collapse->on_click_user);
        CHECK(list && ex_list_count(list) < before && ex_list_count(list) >= 2);
        if (collapse) collapse->on_click(collapse, collapse->on_click_user);
        CHECK(list && ex_list_count(list) == before);
        CHECK(sort && !strcmp(sort->text, "A to Z"));
        if (sort) sort->on_click(sort, sort->on_click_user);
        CHECK(sort && !strcmp(sort->text, "Z to A"));
    }

    /* bookmarks: a group, a bookmark inside it */
    ex_panels_show(F.p, "bookmarks");
    {
        ExWidget *group = find(root, EX_WIDGET_BUTTON, "New bookmark group"), *add = find(root, EX_WIDGET_BUTTON, "Bookmark the current note");
        list = find(root, EX_WIDGET_LIST, "Bookmarks");
        if (group) group->on_click(group, group->on_click_user);
        CHECK(ex_panels_modal(F.p));
        type("Reading");
        key(EX_KEY_ENTER, 0);
        CHECK(list && has_row(list, "Reading"));
        if (list) {
            size_t i;
            for (i = 0; i < ex_list_count(list); i++) if (!strcmp(ex_list_row(list, i)->text, "Reading")) ex_list_select(list, i);
            if (add) add->on_click(add, add->on_click_user);
            CHECK(ex_list_count(list) >= 3);
            for (i = 0; i < ex_list_count(list); i++) if (ex_list_row(list, i)->indent == 1 && !strcmp(ex_list_row(list, i)->text, "Alpha")) break;
            CHECK(i < ex_list_count(list));
        }
    }

    /* renaming a property in every note, from the properties pane */
    ex_panels_show(F.p, "properties");
    list = find(root, EX_WIDGET_LIST, "Properties");
    pump(300);
    {
        ExWidget *rn = find(root, EX_WIDGET_BUTTON, "Rename the selected property in every note");
        size_t i;
        for (i = 0; list && i < ex_list_count(list); i++) if (!strcmp(ex_list_row(list, i)->text, "status")) ex_list_select(list, i);
        if (rn) rn->on_click(rn, rn->on_click_user);
        CHECK(ex_panels_modal(F.p));
        key(EX_KEY_END, 0);
        type("state");
        key(EX_KEY_ENTER, 0);
        pump(800);
        t = ex_platform_read_file(V "/Gamma.md", NULL);
        CHECK(t && strstr(t, "state: waiting") && !strstr(t, "status:"));
        free(t);
        t = ex_platform_read_file(V "/Alpha.md", NULL);
        CHECK(t && strstr(t, "state: active"));
        free(t);
    }

    /* renaming Beta: the dialog, the question about the links, the update */
    ex_panels_rename_file(F.p, "Beta.md");
    CHECK(ex_panels_modal(F.p));
    key(EX_KEY_END, 0);                                                  /* the name is selected: typing replaces it */
    type("Beta renamed");
    key(EX_KEY_ENTER, 0);
    pump(400);
    CHECK(ex_panels_modal(F.p));                                         /* the confirmation */
    {
        ExWidget *yes = find(ex_panels_overlay(F.p), EX_WIDGET_BUTTON, "Update links");
        CHECK(yes != NULL);
        if (yes) yes->on_click(yes, yes->on_click_user);
    }
    pump(800);
    CHECK(F.moved == 1 && !strcmp(F.moved_to, "Beta renamed.md"));
    t = ex_platform_read_file(V "/Alpha.md", NULL);
    CHECK(t && strstr(t, "[[Beta renamed]]") && !strstr(t, "[[Beta]]"));
    free(t);
    t = ex_platform_read_file(V "/Gamma.md", NULL);
    CHECK(t && strstr(t, "Plain note about Beta, not linked."));        /* a mention without a link is not a link */
    free(t);

    if (getenv("PANELS_SHOT")) {
        char path[600];
        ex_panels_search_for(F.p, "beta");
        snprintf(F.active, sizeof F.active, "Alpha.md");
        ex_panels_show(F.p, "backlinks");
        ex_panels_show(F.p, "search");
        pump(900);
        {   ExWidget *b = find(root, EX_WIDGET_BUTTON, "Show unlinked mentions"); if (b) b->on_click(b, b->on_click_user); }
        pump(900);
        snprintf(path, sizeof path, "%s_panels.ppm", getenv("PANELS_SHOT"));
        shot(r, root, path);
        ex_panels_open_switcher(F.p);
        type("al");
        pump(100);
        snprintf(path, sizeof path, "%s_switcher.ppm", getenv("PANELS_SHOT"));
        shot(r, root, path);
        ex_panels_close_dialog(F.p);
        ex_panels_open_palette(F.p);
        type("outline");
        pump(100);
        snprintf(path, sizeof path, "%s_palette.ppm", getenv("PANELS_SHOT"));
        shot(r, root, path);
    }
    ex_panels_free(F.p);
    ex_widget_free(files);
    ex_widget_free(root);
    ex_commands_free(commands); ex_features_free(features); ex_settings_close(settings); ex_ws_free(ws); ex_vault_close(F.vault);
    ex_ui_free(g_ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    free(F.replaced);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
