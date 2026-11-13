/* navigation_panes (sub-slice 4b-iv): outline reordering changes the note text as expected, bookmarks save and reload (also
 * from the worker thread, and when a note is renamed), the random note picks a note of the vault, and the footnotes pane lists
 * a sample note's footnotes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_nav.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "nav_test_vault"

static void write_file(const char *rel, const char *content) {
    char path[512], *dir, *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    dir = malloc(strlen(path) + 1); strcpy(dir, path);
    slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    free(dir);
    f = fopen(path, "wb");
    if (f) { fwrite(content, 1, strlen(content), f); fclose(f); }
}

static void test_outline(void) {
    const char *t = "Intro\n\n# One\none text\n## One A\nA text\n# Two\ntwo text\n# Three\nthree text\n";
    ExOutlineItem *it;
    size_t n, len;
    char *out;
    CHECK(ex_outline(t, strlen(t), &it, &n) == 0 && n == 4);
    CHECK(n == 4 && it[0].level == 1 && !strcmp(it[0].text, "One") && it[1].level == 2 && !strcmp(it[1].text, "One A") && it[2].line == 7 && !strncmp(t + it[2].start, "# Two", 5));
    CHECK(it[0].section_end == it[2].start && it[1].section_end == it[2].start);           /* One holds One A */
    ex_outline_free(it, n);
    /* Two moves above One: the sections below it come along */
    out = ex_outline_move(t, strlen(t), 2, 0, &len);
    CHECK(out && len == strlen(t) && !strcmp(out, "Intro\n\n# Two\ntwo text\n# One\none text\n## One A\nA text\n# Three\nthree text\n"));
    free(out);
    /* One (with One A) moves to the end */
    out = ex_outline_move(t, strlen(t), 0, 4, &len);
    CHECK(out && !strcmp(out, "Intro\n\n# Two\ntwo text\n# Three\nthree text\n# One\none text\n## One A\nA text\n"));
    free(out);
    /* Three moves before Two */
    out = ex_outline_move(t, strlen(t), 3, 2, &len);
    CHECK(out && !strcmp(out, "Intro\n\n# One\none text\n## One A\nA text\n# Three\nthree text\n# Two\ntwo text\n"));
    free(out);
    /* refused: into itself, onto itself, where it already is */
    CHECK(ex_outline_move(t, strlen(t), 0, 1, &len) == NULL);
    CHECK(ex_outline_move(t, strlen(t), 1, 1, &len) == NULL);
    CHECK(ex_outline_move(t, strlen(t), 0, 2, &len) == NULL);
    CHECK(ex_outline_move(t, strlen(t), 3, 4, &len) == NULL);
    CHECK(ex_outline_move(t, strlen(t), 7, 0, &len) == NULL);
    /* a note that does not end with a line break keeps that */
    {
        const char *u = "# A\na\n# B\nb";
        out = ex_outline_move(u, strlen(u), 1, 0, &len);
        CHECK(out && !strcmp(out, "# B\nb\n# A\na"));
        free(out);
        out = ex_outline_move(u, strlen(u), 0, 2, &len);
        CHECK(out && !strcmp(out, "# B\nb\n# A\na"));
        free(out);
    }
}

static void test_bookmarks(void) {
    ExBookmarks *b;
    ExBookmark a;
    int note, head, group, search, inner;
    ex_platform_remove_tree(V);
    ex_platform_make_dirs(V "/.exhaust");
    write_file(".exhaust/bookmarks.ini", "[something_new]\nkey = value\n");
    b = ex_bookmarks_load(V, 0);
    CHECK(b && ex_bookmarks_count(b) == 0);
    memset(&a, 0, sizeof a);
    a.type = EX_BM_NOTE; a.path = "Projects/Plan.md";
    note = ex_bookmarks_add(b, &a, 0);
    a.type = EX_BM_GROUP; a.title = "Reading #1 (100%)"; a.path = NULL;
    group = ex_bookmarks_add(b, &a, 0);
    memset(&a, 0, sizeof a);
    a.type = EX_BM_HEADING; a.path = "Projects/Plan.md"; a.heading = "Goals";
    head = ex_bookmarks_add(b, &a, group);
    memset(&a, 0, sizeof a);
    a.type = EX_BM_SEARCH; a.title = "Open tasks"; a.query = "task-todo:\"\" -tag:#done";
    search = ex_bookmarks_add(b, &a, 0);
    memset(&a, 0, sizeof a);
    a.type = EX_BM_GRAPH; a.title = "Graph"; a.query = "depth=2";
    inner = ex_bookmarks_add(b, &a, group);
    CHECK(note && group && head && search && inner && ex_bookmarks_count(b) == 5);
    memset(&a, 0, sizeof a); a.type = EX_BM_NOTE; a.path = "x.md";
    CHECK(ex_bookmarks_add(b, &a, 999) == 0 && ex_bookmarks_add(b, &a, note) == 0);              /* only a group can hold bookmarks */
    CHECK(ex_bookmarks_rename(b, note, "The plan") == 0);
    ex_bookmarks_free(b);
    /* saved: a new launch has the same */
    b = ex_bookmarks_load(V, 0);
    CHECK(b && ex_bookmarks_count(b) == 5);
    if (b && ex_bookmarks_count(b) == 5) {
        const ExBookmark *g = ex_bookmarks_find(b, group), *s = ex_bookmarks_find(b, search), *h = ex_bookmarks_find(b, head);
        CHECK(ex_bookmarks_find(b, note) && !strcmp(ex_bookmarks_find(b, note)->title, "The plan") && !strcmp(ex_bookmarks_find(b, note)->path, "Projects/Plan.md"));
        CHECK(g && g->type == EX_BM_GROUP && !strcmp(g->title, "Reading #1 (100%)"));
        CHECK(s && s->type == EX_BM_SEARCH && !strcmp(s->query, "task-todo:\"\" -tag:#done"));
        CHECK(h && h->type == EX_BM_HEADING && h->parent == group && !strcmp(h->heading, "Goals"));
        /* move, rename of a note and of a folder, removal of a group */
        CHECK(ex_bookmarks_move(b, search, group, 0) == 0 && ex_bookmarks_find(b, search)->parent == group);
        CHECK(ex_bookmarks_move(b, group, group, 0) != 0 && ex_bookmarks_move(b, search, 999, 0) != 0);
        CHECK(ex_bookmarks_follow_rename(b, "Projects/Plan.md", "Projects/New plan.md") == 2);      /* the note and its heading */
        CHECK(ex_bookmarks_follow_rename(b, "Projects", "Work") == 2 && !strcmp(ex_bookmarks_find(b, note)->path, "Work/New plan.md"));
        CHECK(ex_bookmarks_follow_rename(b, "Proj", "X") == 0);
        CHECK(ex_bookmarks_remove(b, group) == 0 && ex_bookmarks_find(b, head)->parent == 0 && ex_bookmarks_count(b) == 4);
        CHECK(ex_bookmarks_remove(b, 12345) != 0);
    }
    ex_bookmarks_free(b);
    /* written by the worker thread, and the unknown section kept */
    b = ex_bookmarks_load(V, 1);
    CHECK(b && ex_bookmarks_count(b) == 4);
    memset(&a, 0, sizeof a); a.type = EX_BM_NOTE; a.path = "Late.md";
    CHECK(b && ex_bookmarks_add(b, &a, 0) != 0);
    ex_bookmarks_flush(b);
    {
        char *text = ex_platform_read_file(V "/.exhaust/bookmarks.ini", NULL);
        CHECK(text && strstr(text, "Late.md") && strstr(text, "[something_new]") && strstr(text, "key = value"));
        free(text);
    }
    ex_bookmarks_free(b);
    b = ex_bookmarks_load(V, 0);
    CHECK(b && ex_bookmarks_count(b) == 5);
    /* ids are not reused */
    if (b) { int id; memset(&a, 0, sizeof a); a.type = EX_BM_NOTE; a.path = "n.md"; id = ex_bookmarks_add(b, &a, 0); CHECK(id > search && id > inner); }
    ex_bookmarks_free(b);
}

static void test_random(void) {
    ExIndex *w;
    char err[200], *root;
    unsigned long long st = 42;
    int seen[4] = {0, 0, 0, 0}, i;
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("nav_test_db");
    write_file("A.md", "a\n"); write_file("B.md", "b\n"); write_file("sub/C.md", "c\n"); write_file("D.md", "d\n"); write_file("pic.png", "PNG");
    root = ex_platform_realpath(V);
    w = ex_index_open("nav_test_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) return;
    for (i = 0; i < 200; i++) {
        char *p = ex_random_note(w, NULL, &st);
        CHECK(p != NULL);
        if (!p) break;
        if (!strcmp(p, "A.md")) seen[0]++; else if (!strcmp(p, "B.md")) seen[1]++; else if (!strcmp(p, "sub/C.md")) seen[2]++; else if (!strcmp(p, "D.md")) seen[3]++;
        else CHECK(0);                                                     /* only notes: never the picture */
        free(p);
    }
    CHECK(seen[0] > 20 && seen[1] > 20 && seen[2] > 20 && seen[3] > 20);
    for (i = 0; i < 100; i++) { char *p = ex_random_note(w, "B.md", &st); CHECK(p && strcmp(p, "B.md") != 0); free(p); }
    ex_index_close(w);
    ex_platform_remove_tree("nav_test_db_empty");
    ex_platform_remove_tree("nav_test_vault_empty");
    ex_platform_make_dirs("nav_test_vault_empty");
    {
        char *r2 = ex_platform_realpath("nav_test_vault_empty");
        ExIndex *e = ex_index_open("nav_test_db_empty/index.db", 1, err, sizeof err);
        CHECK(e && ex_index_sync(e, r2, NULL, 0, NULL, NULL, NULL) == 0 && ex_random_note(e, NULL, &st) == NULL);
        ex_index_close(e);
        free(r2);
    }
    free(root);
}

static void test_footnotes(void) {
    const char *t = "Text with a note[^b] and another[^a], then inline^[an inline note] and the first again[^b] and a missing[^zz].\n\n"
                    "[^a]: Definition of a,\n    continued here.\n[^b]: Definition of b.\n[^unused]: Nothing refers to this.\n";
    ExFootnote *f;
    size_t n;
    CHECK(ex_footnotes(t, strlen(t), &f, &n) == 0 && n == 5);
    if (n == 5) {
        CHECK(!strcmp(f[0].id, "b") && f[0].number == 1 && f[0].refs == 2 && f[0].text && !strcmp(f[0].text, "Definition of b."));
        CHECK(!strcmp(f[1].id, "a") && f[1].number == 2 && f[1].text && strstr(f[1].text, "continued here."));
        CHECK(f[2].inline_note && f[2].number == 3 && !strcmp(f[2].text, "an inline note") && f[2].first_ref == (size_t)(strstr(t, "^[an") - t));
        CHECK(!strcmp(f[3].id, "zz") && f[3].number == 4 && f[3].text == NULL);                     /* a reference without a definition */
        CHECK(!strcmp(f[4].id, "unused") && f[4].number == 0 && f[4].refs == 0 && !strcmp(f[4].text, "Nothing refers to this."));
        CHECK(f[0].def_end > f[0].def_start && !strncmp(t + f[0].def_start, "[^b]:", 5));
    }
    ex_footnotes_free(f, n);
    CHECK(ex_footnotes("No notes here.\n", 14, &f, &n) == 0 && n == 0);
    ex_footnotes_free(f, n);
}

int main(void) {
    test_outline();
    test_bookmarks();
    test_random();
    test_footnotes();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
