/* editing_slash: the slash menu lists the registered editor commands (the ones the command table marks Editor: yes) in table
 * order, narrows them with fuzzy matching, shows commands that are not bound as unavailable, and runs the selected one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_fuzzy.h"
#include "exhaust_slash.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int ran;
static void run_bold(void *u) { (void)u; ran++; }

static int top_is(ExSlashMenu *m, const char *query, const char *id) {
    const ExSlashItem *it;
    ex_slash_set_query(m, query);
    it = ex_slash_item(m, 0);
    if (!it || strcmp(it->id, id) != 0) { fprintf(stderr, "query \"%s\": first is %s, wanted %s\n", query, it ? it->id : "(none)", id); return 0; }
    return 1;
}

int main(void) {
    ExCommands *c = ex_commands_create(NULL);
    ExSlashMenu *m;
    size_t i, k, editor_commands = 0;
    CHECK(c != NULL);
    ex_commands_bind(c, "editor.toggle-bold", run_bold, NULL, NULL);
    m = ex_slash_create(c);
    CHECK(m != NULL);

    /* the list is exactly the editor commands of the table, in table order */
    for (i = 0; i < ex_command_table_count; i++) if (ex_command_table[i].editor) editor_commands++;
    CHECK(editor_commands == 75);
    CHECK(ex_slash_count(m) == editor_commands);
    for (i = 0, k = 0; i < ex_command_table_count; i++)
        if (ex_command_table[i].editor) {
            CHECK(strcmp(ex_slash_item(m, k)->id, ex_command_table[i].id) == 0);
            k++;
        }
    /* only the bound command is available; the rest is listed but disabled */
    for (i = 0; i < ex_slash_count(m); i++) CHECK(ex_slash_item(m, i)->available == (strcmp(ex_slash_item(m, i)->id, "editor.toggle-bold") == 0));

    /* fuzzy matching */
    CHECK(ex_fuzzy_score("scf", "Save current file") >= 0);
    CHECK(ex_fuzzy_score("scf", "Search current file") > ex_fuzzy_score("scf", "Insert scaffold"));   /* word starts beat scattered letters */
    CHECK(ex_fuzzy_score("xyz", "Save current file") < 0);
    CHECK(ex_fuzzy_score("SAVE", "save current file") >= 0);          /* case does not matter */
    CHECK(ex_fuzzy_score("", "anything") == 0);
    CHECK(ex_fuzzy_score("file", "Save current file") > ex_fuzzy_score("fle", "Save current file"));   /* a whole word beats scattered letters */
    CHECK(top_is(m, "toggle bold", "editor.toggle-bold"));
    CHECK(top_is(m, "tbold", "editor.toggle-bold"));
    CHECK(top_is(m, "bold", "editor.toggle-bold"));
    CHECK(top_is(m, "h1", "editor.set-as-heading-1"));
    CHECK(top_is(m, "Insert table", "editor.insert-table"));
    CHECK(top_is(m, "fold", "editor.fold-all-headings-and-lists") || top_is(m, "fold", "editor.fold-less") || top_is(m, "fold", "editor.fold-more"));
    ex_slash_set_query(m, "zzzz");
    CHECK(ex_slash_count(m) == 0 && ex_slash_run(m) == -1);
    ex_slash_set_query(m, "");
    CHECK(ex_slash_count(m) == editor_commands && ex_slash_selected(m) == 0);
    ex_slash_set_query(m, "table");
    CHECK(ex_slash_count(m) >= 10);                                  /* the table commands and "Insert table" */
    for (i = 0; i + 1 < ex_slash_count(m); i++) CHECK(ex_slash_item(m, i)->score >= ex_slash_item(m, i + 1)->score);

    /* selection and running */
    ex_slash_set_query(m, "bold");
    CHECK(strcmp(ex_slash_item(m, 0)->id, "editor.toggle-bold") == 0);
    CHECK(ex_slash_run(m) == 0 && ran == 1);
    ex_slash_set_query(m, "toggle");
    CHECK(ex_slash_count(m) >= 15 && ex_slash_selected(m) == 0);
    CHECK(strcmp(ex_slash_item(m, 0)->id, "editor.toggle-bold") != 0);
    CHECK(ex_slash_run(m) == -2 && ran == 1);                        /* not bound: nothing runs */
    ex_slash_move(m, 1);
    CHECK(ex_slash_selected(m) == 1);
    ex_slash_move(m, -1);
    ex_slash_move(m, -1);
    CHECK(ex_slash_selected(m) == ex_slash_count(m) - 1);            /* wraps around */
    ex_slash_move(m, 1);
    CHECK(ex_slash_selected(m) == 0);

    ex_slash_free(m);
    ex_commands_free(c);
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
