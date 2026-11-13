/* switcher (sub-slice 4b-iii): `dn` matches "Daily notes", aliases are found, Enter on a name that matches nothing creates the
 * note, and the command palette lists every registered command, ranks by the fuzzy match and honors pinned commands. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_nav.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "switcher_test_vault"

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

static void test_switcher(void) {
    ExIndex *w;
    char err[200], *root;
    ExSwitcher *sw;
    ExSwitchItem items[20];
    ExSwitchAction act;
    size_t n;
    const char *recent[] = {"Project plan.md", "Gone.md", "Daily notes.md"};
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("switcher_test_db");
    write_file("Daily notes.md", "x\n");
    write_file("Project plan.md", "x\n");
    write_file("Dinner notes.md", "x\n");
    write_file("Beta.md", "---\naliases: [Bee, The second one]\n---\nx\n");
    write_file("archive/Old ideas.md", "x\n");
    write_file("pic.png", "PNG");
    root = ex_platform_realpath(V);
    w = ex_index_open("switcher_test_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) return;
    sw = ex_switcher_load(w, 0);
    CHECK(sw != NULL);
    n = ex_switcher_query(sw, "dn", NULL, 0, 20, items);
    CHECK(n >= 2 && !strcmp(items[0].path, "Daily notes.md"));                 /* the start of both words: before Dinner notes */
    CHECK(n >= 2 && !strcmp(items[1].path, "Dinner notes.md") && items[0].score >= items[1].score);
    n = ex_switcher_query(sw, "bee", NULL, 0, 20, items);
    CHECK(n >= 1 && !strcmp(items[0].path, "Beta.md") && items[0].via_alias && !strcmp(items[0].label, "Bee"));
    n = ex_switcher_query(sw, "second one", NULL, 0, 20, items);
    CHECK(n >= 1 && !strcmp(items[0].path, "Beta.md") && items[0].via_alias);
    n = ex_switcher_query(sw, "ideas", NULL, 0, 20, items);
    CHECK(n == 1 && !strcmp(items[0].path, "archive/Old ideas.md") && !items[0].via_alias);
    n = ex_switcher_query(sw, "archive", NULL, 0, 20, items);                  /* the folder: the path matches */
    CHECK(n >= 1 && !strcmp(items[0].path, "archive/Old ideas.md"));
    n = ex_switcher_query(sw, "pic", NULL, 0, 20, items);                      /* attachments are off */
    CHECK(n == 0);
    n = ex_switcher_query(sw, "zzz", NULL, 0, 20, items);
    CHECK(n == 0);
    n = ex_switcher_query(sw, "", recent, 3, 20, items);                       /* empty: the recent notes that still exist */
    CHECK(n == 2 && !strcmp(items[0].path, "Project plan.md") && !strcmp(items[1].path, "Daily notes.md"));
    /* Enter */
    n = ex_switcher_query(sw, "dn", NULL, 0, 20, items);
    CHECK(ex_switcher_enter(sw, "dn", &items[0], 0, &act) == 0 && !act.create && !strcmp(act.path, "Daily notes.md"));
    CHECK(ex_switcher_enter(sw, "Brand new note", NULL, 0, &act) == 0 && act.create && !strcmp(act.path, "Brand new note.md"));
    CHECK(ex_switcher_enter(sw, "  Ideas/Deeper note.md ", NULL, 0, &act) == 0 && act.create && !strcmp(act.path, "Ideas/Deeper note.md"));
    CHECK(ex_switcher_enter(sw, "bad#name", NULL, 0, &act) != 0);
    CHECK(ex_switcher_enter(sw, "a|b", NULL, 0, &act) != 0);
    CHECK(ex_switcher_enter(sw, "CON", NULL, 0, &act) != 0);
    CHECK(ex_switcher_enter(sw, "", NULL, 0, &act) != 0);
    CHECK(ex_switcher_enter(sw, "new", NULL, 1, &act) != 0);                   /* show_existing_only: nothing is created */
    ex_switcher_free(sw);
    sw = ex_switcher_load(w, 1);
    n = ex_switcher_query(sw, "pic", NULL, 0, 20, items);
    CHECK(n == 1 && !strcmp(items[0].path, "pic.png") && !items[0].is_note);
    ex_switcher_free(sw);
    ex_index_close(w);
    free(root);
}

static void test_palette(void) {
    ExCommands *c = ex_commands_create(NULL);
    ExPaletteItem *items = NULL;
    size_t n, i, total = ex_commands_count(c);
    const char *pinned[] = {"daily-notes.open-today-s-daily-note", "app.rename-file"};
    CHECK(c != NULL && total > 150);
    n = ex_palette_query(c, "", NULL, 0, &items);
    CHECK(n == total);                                                          /* every command of the registry */
    for (i = 0; i < n; i++) CHECK(!strcmp(items[i].id, ex_commands_at(c, i)->id));
    free(items);
    n = ex_palette_query(c, "", pinned, 2, &items);
    CHECK(n == total && !strcmp(items[0].id, pinned[0]) && items[0].pinned && !strcmp(items[1].id, pinned[1]) && items[1].pinned && !items[2].pinned);
    free(items);
    n = ex_palette_query(c, "open today", NULL, 0, &items);
    CHECK(n >= 1 && n < total && !strcmp(items[0].id, "daily-notes.open-today-s-daily-note"));
    for (i = 1; i < n; i++) CHECK(items[i - 1].score >= items[i].score);
    free(items);
    n = ex_palette_query(c, "rename", pinned, 2, &items);                       /* a pinned command ranks above the others that match */
    CHECK(n >= 2 && !strcmp(items[0].id, "app.rename-file") && items[0].pinned);
    free(items);
    n = ex_palette_query(c, "qqqzzz", NULL, 0, &items);
    CHECK(n == 0);
    free(items);
    ex_commands_free(c);
}

int main(void) {
    test_switcher();
    test_palette();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
