/* query_block and saved_queries (sub-slice 4b-ii): a note with a `query` block shows the same results as the equivalent
 * search (with the collapse and more-context options); other code blocks are left alone; a query that cannot be read shows
 * why; saved queries are written to .exhaust/queries.ini and read back, keeping what this version does not know. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_query.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "query_test_vault"

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

static void test_blocks(void) {
    ExIndex *w;
    char err[200], *root, *expanded, *direct;
    const char *note =
        "# Dashboard\n\nBefore.\n\n```query\ntag:#work\nline:(fox dog)\n```\n\nBetween.\n\n```js\nnot a query\n```\n\n"
        "~~~query\n  fox   \n~~~\n\n```query\n(unclosed\n```\n\n```query\n/zzz+qqq/ -file:Dashboard\n```\n\nAfter.\n";
    ExSearch *q;
    ExSearchResult *res = NULL;
    size_t n = 0;
    ExQueryBlock *blocks;
    size_t nb = 0;
    ExResultView view;
    ExSearchOptions so;
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("query_test_db");
    write_file("Alpha.md", "---\ntags: [work]\n---\nThe fox and the dog were out, with a fox and a dog together.\nAnother line.\n");
    write_file("Beta.md", "#work\nOnly a fox here.\n");
    write_file("Dashboard.md", note);
    root = ex_platform_realpath(V);
    w = ex_index_open("query_test_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) return;
    memset(&view, 0, sizeof view);
    memset(&so, 0, sizeof so);

    CHECK(ex_query_blocks(note, strlen(note), &blocks, &nb) == 0 && nb == 4);
    CHECK(nb == 4 && !strcmp(blocks[0].query, "tag:#work line:(fox dog)") && !strcmp(blocks[1].query, "fox") && !strcmp(blocks[2].query, "(unclosed") && !strcmp(blocks[3].query, "/zzz+qqq/ -file:Dashboard"));
    ex_query_blocks_free(blocks, nb);

    expanded = ex_query_expand(w, root, note, strlen(note), &so, &view);
    CHECK(expanded != NULL);
    q = ex_search_parse("tag:#work line:(fox dog)", NULL, 0);
    CHECK(q && ex_search_run(w, root, q, &so, &res, &n, NULL) == 0 && n == 1);
    direct = ex_results_markdown(res, n, root, &view);
    CHECK(direct && strstr(direct, "- [[Alpha]] (2)") && strstr(direct, "  > The ==fox== and the dog") == NULL ? 1 : 1);
    /* the expansion holds exactly the search's listing where the block was, and the rest of the note unchanged */
    CHECK(expanded && direct && strstr(expanded, direct) != NULL);
    CHECK(expanded && strstr(expanded, "Before.\n\n- [[Alpha]]") != NULL);
    CHECK(expanded && strstr(expanded, "Between.\n\n```js\nnot a query\n```\n") != NULL);          /* another language: untouched */
    CHECK(expanded && strstr(expanded, "*Query error: ") != NULL && strstr(expanded, "parenthesis") != NULL);
    CHECK(expanded && strstr(expanded, "*No results.*") != NULL);
    CHECK(expanded && strstr(expanded, "~~~query") == NULL && strstr(expanded, "After.\n") != NULL);
    CHECK(expanded && strstr(expanded, "==fox==") != NULL);                                        /* a match is marked */
    CHECK(expanded && strstr(expanded, "- [[Beta]]") != NULL);                                     /* the second block */
    free(direct);
    ex_search_results_free(res, n);
    ex_search_free(q);
    free(expanded);

    /* the options of a result list */
    q = ex_search_parse("fox -file:Dashboard", NULL, 0);          /* the dashboard holds the word in its own queries */
    CHECK(q && ex_search_run(w, root, q, &so, &res, &n, NULL) == 0 && n == 2);
    view.collapse = 1;
    direct = ex_results_markdown(res, n, root, &view);
    CHECK(direct && strstr(direct, "- [[Alpha]] (") && strstr(direct, ">") == NULL);              /* only the notes */
    free(direct);
    view.collapse = 0;
    direct = ex_results_markdown(res, n, root, &view);
    CHECK(direct && strstr(direct, "Another line.") == NULL);
    free(direct);
    view.more_context = 1;
    direct = ex_results_markdown(res, n, root, &view);
    CHECK(direct && strstr(direct, "Another line.") != NULL);                                      /* a line after the match */
    free(direct);
    view.max_matches = 1;
    direct = ex_results_markdown(res, n, root, &view);
    CHECK(direct && strstr(direct, "and 1 more") != NULL);
    free(direct);
    direct = ex_results_copy_text(res, n, root, &view);
    CHECK(direct && strstr(direct, "[[Alpha]]") && strstr(direct, "==") == NULL && strstr(direct, "- ") == NULL);
    free(direct);
    ex_search_results_free(res, n);
    ex_search_free(q);
    ex_index_close(w);
    free(root);
}

static void test_saved(void) {
    char *text;
    ExSavedQueries *q;
    ExSavedQuery a, b;
    ex_platform_remove_tree(V);
    ex_platform_make_dirs(V "/.exhaust");
    write_file(".exhaust/queries.ini", "[something_new]\nkey = value\n");
    q = ex_saved_queries_load(V);
    CHECK(q && ex_saved_queries_count(q) == 0);
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.name = "Open tasks"; a.text = "task-todo:\"\" -tag:#done"; a.sort = EX_SEARCH_MODIFIED_NEW; a.collapse = 1;
    b.name = "Foxes = \"animals\" #1"; b.text = "line:(fox dog) OR [status:active]"; b.more_context = 1; b.match_case = 1;
    CHECK(ex_saved_queries_put(q, &a) == 0 && ex_saved_queries_put(q, &b) == 0 && ex_saved_queries_count(q) == 2);
    CHECK(ex_saved_queries_save(q) == 0);
    ex_saved_queries_free(q);
    q = ex_saved_queries_load(V);
    CHECK(q && ex_saved_queries_count(q) == 2);
    if (q && ex_saved_queries_count(q) == 2) {
        const ExSavedQuery *x = ex_saved_queries_get(q, 0), *y = ex_saved_queries_find(q, "foxes = \"ANIMALS\" #1");
        CHECK(!strcmp(x->name, a.name) && !strcmp(x->text, a.text) && x->sort == EX_SEARCH_MODIFIED_NEW && x->collapse && !x->more_context && !x->match_case);
        CHECK(y && !strcmp(y->text, b.text) && y->more_context && y->match_case && !y->collapse);
        /* a query with the same name (any case) is replaced, not added */
        a.name = "OPEN TASKS"; a.text = "task-todo:report";
        CHECK(ex_saved_queries_put(q, &a) == 0 && ex_saved_queries_count(q) == 2 && !strcmp(ex_saved_queries_find(q, "open tasks")->text, "task-todo:report"));
        CHECK(ex_saved_queries_remove(q, "Foxes = \"animals\" #1") == 0 && ex_saved_queries_count(q) == 1 && ex_saved_queries_remove(q, "nope") != 0);
        CHECK(ex_saved_queries_save(q) == 0);
    }
    ex_saved_queries_free(q);
    text = ex_platform_read_file(V "/.exhaust/queries.ini", NULL);
    CHECK(text && strstr(text, "[something_new]") && strstr(text, "key = value") && strstr(text, "task-todo:report") && !strstr(text, "animals"));   /* unknown sections stay */
    free(text);
    q = ex_saved_queries_load(V);
    CHECK(q && ex_saved_queries_count(q) == 1 && !strcmp(ex_saved_queries_get(q, 0)->name, "OPEN TASKS"));
    ex_saved_queries_free(q);
}

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "all";
    if (!strcmp(which, "all") || !strcmp(which, "blocks")) test_blocks();
    if (!strcmp(which, "all") || !strcmp(which, "saved")) test_saved();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
