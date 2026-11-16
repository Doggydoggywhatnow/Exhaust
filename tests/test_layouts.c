/* layouts: saved workspace layouts (sub-slice 7e). A layout saved under a name comes back the same (splits, ratios, tabs, pinned, cursor and
 * scroll, sidebars, panels); names are checked and compared without the case of ASCII letters; a layout of the same name is replaced;
 * the file is written by a worker and read again at the next start, with sections this version does not know kept; the window's place, the
 * graph's options and the search text are not part of a layout and stay; tabs of notes that are gone are dropped; an unreadable layout
 * changes nothing. Works in ./layouts_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_layouts.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "layouts_test"
#define FILE_ DIR "/workspaces.ini"

static ExWorkspace *build(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g0 = ex_ws_active_group(ws), a = ex_ws_new_tab(ws, 0, "a.md", "note"), b = ex_ws_new_tab(ws, 0, "folder/b.md", "note"), c = ex_ws_new_tab(ws, 0, "", "graph"), g1;
    g1 = ex_ws_split(ws, g0, EX_SPLIT_HORIZONTAL, 1, b);
    ex_ws_split(ws, g1, EX_SPLIT_VERTICAL, 1, c);
    ex_ws_set_pinned(ws, a, 1);
    ex_ws_set_tab_state(ws, a, 7, 2, 123.0f);
    ex_ws_set_sidebar(ws, EX_SIDE_LEFT, 1, 333);
    ex_ws_set_sidebar(ws, EX_SIDE_RIGHT, 0, 410);
    ex_ws_open_panel(ws, EX_SIDE_LEFT, "search");
    ex_ws_window_set_rect(ws, ex_ws_main_window(ws), 10, 20, 1000, 700, 0);
    ex_ws_extra_set(ws, "graph", "open", "true");
    ex_ws_set_search_text(ws, "needle");
    return ws;
}

/* The layout part of a session text: the sections a layout holds, without the window's place. */
static char *layout_part(const ExWorkspace *ws) {
    size_t len = 0, i;
    char *t = ex_ws_serialize(ws, &len), *out;
    size_t o = 0;
    int keep = 0;
    char *line;
    out = calloc(len + 2, 1);
    for (line = t; line && *line;) {
        char *nl = strchr(line, '\n');
        size_t ll = nl ? (size_t)(nl - line) : strlen(line);
        if (line[0] == '[') keep = !strncmp(line, "[layout]", 8) || !strncmp(line, "[window.", 8) || !strncmp(line, "[split.", 7) || !strncmp(line, "[group.", 7) || !strncmp(line, "[tab.", 5);
        if (keep && strncmp(line, "x = ", 4) && strncmp(line, "y = ", 4) && strncmp(line, "width = ", 8) && strncmp(line, "height = ", 9) && strncmp(line, "maximized = ", 12)) { memcpy(out + o, line, ll); o += ll; out[o++] = '\n'; }
        line = nl ? nl + 1 : NULL;
    }
    (void)i;
    free(t);
    return out;
}

static int exists_all(void *u, const char *p) { (void)u; (void)p; return 1; }
static int exists_not_b(void *u, const char *p) { (void)u; return strcmp(p, "folder/b.md") != 0; }

int main(void) {
    ExLayouts *l;
    ExWorkspace *ws = build(), *other = ex_ws_create();
    char *before, *after, *t;
    size_t len;
    ex_platform_remove_tree(DIR);

    CHECK(ex_layouts_name_valid("Writing") && ex_layouts_name_valid("a b") && ex_layouts_name_valid("日記") && ex_layouts_name_valid("a.b"));
    CHECK(!ex_layouts_name_valid("") && !ex_layouts_name_valid(" x") && !ex_layouts_name_valid("x ") && !ex_layouts_name_valid("a/b") && !ex_layouts_name_valid("a[b")
          && !ex_layouts_name_valid("a]b") && !ex_layouts_name_valid("a\nb") && !ex_layouts_name_valid("a\tb") && !ex_layouts_name_valid(NULL));
    {   char sixty[80], sixtyone[80];
        memset(sixty, 'n', 60); sixty[60] = 0; memset(sixtyone, 'n', 61); sixtyone[61] = 0;
        CHECK(ex_layouts_name_valid(sixty) && !ex_layouts_name_valid(sixtyone)); }

    l = ex_layouts_open(FILE_);
    CHECK(l && ex_layouts_count(l) == 0);
    CHECK(ex_layouts_save(l, "bad/name", ws, 100) == -1 && ex_layouts_count(l) == 0);
    CHECK(ex_layouts_save(l, "Writing", ws, 1700000000) == 0 && ex_layouts_count(l) == 1 && !strcmp(ex_layouts_name(l, 0), "Writing"));
    CHECK(ex_layouts_saved_time(l, "writing") == 1700000000 && ex_layouts_find(l, "WRITING") != NULL && ex_layouts_find(l, "Other") == NULL);

    /* the layout comes back; what is not a layout stays */
    before = layout_part(ws);
    ex_ws_close_group(other, ex_ws_active_group(other));
    ex_ws_new_tab(other, 0, "zzz.md", "note");
    ex_ws_window_set_rect(other, ex_ws_main_window(other), 50, 60, 800, 600, 1);
    ex_ws_extra_set(other, "graph", "open", "false");
    ex_ws_set_search_text(other, "keep this");
    CHECK(ex_layouts_load(l, "writing", other, exists_all, NULL) == 0);
    after = layout_part(other);
    CHECK(strcmp(before, after) == 0);
    {   int x, y, w, h, mx;
        ex_ws_window_get_rect(other, ex_ws_main_window(other), &x, &y, &w, &h, &mx);
        CHECK(x == 50 && y == 60 && w == 800 && h == 600 && mx == 1); }
    CHECK(!strcmp(ex_ws_extra_get(other, "graph", "open"), "false") && !strcmp(ex_ws_search_text(other), "keep this"));
    CHECK(ex_ws_sidebar_width(other, EX_SIDE_LEFT) == 333 && ex_ws_sidebar_visible(other, EX_SIDE_RIGHT) == 0 && ex_ws_panel_count(other, EX_SIDE_LEFT) >= 1);
    free(before); free(after);

    /* the same name in another case replaces it and keeps the first spelling */
    ex_ws_new_tab(ws, 0, "later.md", "note");
    CHECK(ex_layouts_save(l, "WRITING", ws, 1700000100) == 0 && ex_layouts_count(l) == 1 && !strcmp(ex_layouts_name(l, 0), "Writing") && ex_layouts_saved_time(l, "Writing") == 1700000100);
    CHECK(ex_layouts_save(l, "Journal", ws, 1700000200) == 0 && ex_layouts_count(l) == 2 && !strcmp(ex_layouts_name(l, 0), "Journal") && !strcmp(ex_layouts_name(l, 1), "Writing"));

    /* the file is written by a worker and read again at the next start */
    ex_layouts_flush(l);
    ex_layouts_close(l);
    t = ex_platform_read_file(FILE_, &len);
    CHECK(t && strstr(t, "[workspace.Writing]") && strstr(t, "[workspace.Journal/tab.") && !strstr(t, "needle") && !strstr(t, "width = 1000"));
    free(t);
    l = ex_layouts_open(FILE_);
    CHECK(l && ex_layouts_count(l) == 2 && ex_layouts_find(l, "journal") != NULL && ex_layouts_saved_time(l, "Journal") == 1700000200);
    before = layout_part(ws);
    ex_ws_free(other);
    other = build();
    CHECK(ex_layouts_load(l, "Writing", other, exists_all, NULL) == 0);
    after = layout_part(other);
    CHECK(strcmp(before, after) == 0);
    free(before); free(after);

    /* a tab of a note that is gone is dropped */
    CHECK(ex_layouts_load(l, "Journal", other, exists_not_b, NULL) == 0);
    {   size_t i; int found_b = 0, found_a = 0;
        for (i = 0; i < ex_ws_tab_count(other); i++) { const ExTab *tb = ex_ws_tab(other, ex_ws_tab_at(other, i)); if (!strcmp(tb->path, "folder/b.md")) found_b = 1; if (!strcmp(tb->path, "a.md")) found_a = 1; }
        CHECK(!found_b && found_a); }

    /* an unknown name, and a layout that is not readable, change nothing */
    before = ex_ws_serialize(other, &len);
    CHECK(ex_layouts_load(l, "Nothing", other, exists_all, NULL) == -1);
    after = ex_ws_serialize(other, &len);
    CHECK(strcmp(before, after) == 0);
    free(before); free(after);
    ex_layouts_close(l);
    {   const char *broken = "[workspace.Broken]\nsaved = 5\n[workspace.Broken/tab.1]\npath = a.md\n[unknown]\nkey = value\n";
        CHECK(ex_platform_write_file_atomic(FILE_, broken, strlen(broken)) == 0);
        l = ex_layouts_open(FILE_);
        CHECK(l && ex_layouts_count(l) == 1);
        before = ex_ws_serialize(other, &len);
        CHECK(ex_layouts_load(l, "Broken", other, exists_all, NULL) == -1);
        after = ex_ws_serialize(other, &len);
        CHECK(strcmp(before, after) == 0);
        free(before); free(after);
        /* sections this version does not know stay when the file is written again */
        CHECK(ex_layouts_save(l, "New", other, 9) == 0);
        ex_layouts_flush(l);
        ex_layouts_close(l);
        t = ex_platform_read_file(FILE_, &len);
        CHECK(t && strstr(t, "[unknown]") && strstr(t, "key = value") && strstr(t, "[workspace.Broken/tab.1]") && strstr(t, "[workspace.New]"));
        free(t);
    }

    /* delete */
    l = ex_layouts_open(FILE_);
    CHECK(ex_layouts_count(l) == 2 && ex_layouts_delete(l, "broken") == 0 && ex_layouts_count(l) == 1 && ex_layouts_delete(l, "broken") == -1);
    ex_layouts_flush(l);
    ex_layouts_close(l);
    t = ex_platform_read_file(FILE_, &len);
    CHECK(t && !strstr(t, "Broken") && strstr(t, "[workspace.New]"));
    free(t);

    ex_ws_free(ws);
    ex_ws_free(other);
    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "layouts: %d failed\n", failures); return 1; }
    printf("layouts: ok\n");
    return 0;
}
