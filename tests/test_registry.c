/* registry: the feature-toggle registry and the command registry. Features read and write
 * `[features]` of settings.ini (write-through), call back on a change only, refuse unknown ids and stubs;
 * the generated command table matches docs/formats.md (208 commands), commands are only available when
 * bound and when their feature is on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_commands.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "registry_test"

static int changes, last_enabled;
static char last_id[64];
static void on_feature(const char *id, int enabled, void *user) { (void)user; changes++; last_enabled = enabled; snprintf(last_id, sizeof last_id, "%s", id); }

static int ran;
static void run_it(void *user) { ran += user ? *(int *)user : 1; }
static int can_run_flag = 1;
static int check_it(void *user) { (void)user; return can_run_flag; }

static void test_table(void) {
    size_t i, j, editor = 0;
    char *doc = ex_platform_read_file(DOC_PATH, NULL);
    CHECK(doc != NULL);
    CHECK(ex_command_table_count == 208);                        /* the count the document states */
    for (i = 0; i < ex_command_table_count; i++) {
        const ExCommandDef *d = &ex_command_table[i];
        const char *p;
        char needle[200];
        CHECK(d->id[0] && d->name[0]);
        for (p = d->id; *p; p++) CHECK((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-');
        CHECK(strchr(d->id, '.') != NULL);
        for (j = 0; j < i; j++) CHECK(strcmp(ex_command_table[j].id, d->id) != 0);       /* unique */
        snprintf(needle, sizeof needle, "| `%s` | %s |", d->id, d->name);
        CHECK(doc && strstr(doc, needle) != NULL);
        editor += (size_t)d->editor;
    }
    CHECK(editor > 40);
    CHECK(strcmp(ex_command_feature("daily-notes.open-next-daily-note"), "daily_notes") == 0);
    CHECK(strcmp(ex_command_feature("graph-view.open-graph-view"), "graph") == 0);
    CHECK(strcmp(ex_command_feature("unique-note-creator.add-unique-internal-link"), "unique_note") == 0);
    CHECK(ex_command_feature("app.close-current-tab") == NULL && ex_command_feature("editor.add-tag") == NULL);
    CHECK(strcmp(ex_command_feature("sync.set-up-sync"), "sync") == 0);
    CHECK(ex_command_feature("table.add-row-above") == NULL && ex_command_feature("format-converter.frontmatter-migration") == NULL);
    /* every feature area of the table names a switch that exists */
    for (i = 0; i < ex_command_table_count; i++) {
        const char *f = ex_command_feature(ex_command_table[i].id);
        if (f) CHECK(ex_settings_def("features", f) != NULL);
    }
    free(doc);
}

int main(void) {
    ExSettings *s;
    ExFeatures *f;
    ExCommands *c;
    int one = 1, five = 5;
    char *t;

    test_table();
    ex_platform_remove_tree(DIR);
    ex_platform_make_dirs(DIR "/vault");
    s = ex_settings_open(DIR "/vault/.exhaust", NULL);
    f = ex_features_create(s);
    c = ex_commands_create(f);

    /* features */
    CHECK(ex_features_register(f, "graph", "Graph view", on_feature, NULL) == 0);
    CHECK(ex_features_register(f, "graph", "again", NULL, NULL) == -1);           /* twice */
    CHECK(ex_features_register(f, "not_a_feature", "x", NULL, NULL) == -1);       /* not in the document */
    CHECK(ex_features_register(f, "sync", "Sync", on_feature, NULL) == 0);
    CHECK(ex_features_register(f, "daily_notes", "Daily notes", on_feature, NULL) == 0);
    CHECK(ex_features_count(f) == 3 && strcmp(ex_features_id(f, 0), "graph") == 0 && strcmp(ex_features_label(f, 0), "Graph view") == 0);
    CHECK(!ex_features_is_stub(f, 0) && ex_features_is_stub(f, 1));
    CHECK(ex_features_enabled(f, "graph") == 1 && ex_features_enabled(f, "sync") == 0 && ex_features_enabled(f, "slides") == 0 && ex_features_enabled(f, "nonsense") == 0);
    CHECK(ex_features_set(f, "graph", 0) == 0 && ex_features_enabled(f, "graph") == 0);
    CHECK(changes == 1 && last_enabled == 0 && strcmp(last_id, "graph") == 0);
    CHECK(ex_features_set(f, "graph", 0) == 0 && changes == 1);                   /* no change, no call */
    CHECK(ex_features_set(f, "sync", 1) == -1 && ex_features_enabled(f, "sync") == 0 && changes == 1);   /* a stub stays off */
    CHECK(ex_features_set(f, "nonsense", 1) == -1);
    CHECK(ex_features_set(f, "slides", 1) == 0 && ex_features_enabled(f, "slides") == 1);   /* a feature nobody registered still has its switch */
    ex_settings_flush(s);
    t = ex_platform_read_file(DIR "/vault/.exhaust/settings.ini", NULL);
    CHECK(t && strstr(t, "[features]") && strstr(t, "graph = false") && strstr(t, "slides = true"));
    free(t);

    /* commands: declared, bound, available */
    CHECK(ex_commands_count(c) == 208 && ex_commands_at(c, 0) != NULL && ex_commands_at(c, 208) == NULL);
    CHECK(ex_commands_find(c, "app.close-current-tab") != NULL && strcmp(ex_commands_find(c, "app.close-current-tab")->default_keys, "Ctrl+W") == 0);
    CHECK(ex_commands_find(c, "app.no-such-thing") == NULL);
    CHECK(ex_commands_run(c, "app.no-such-thing") == -1);
    CHECK(ex_commands_run(c, "app.close-current-tab") == -2 && !ex_commands_available(c, "app.close-current-tab"));   /* declared, not bound */
    CHECK(ex_commands_bind(c, "app.no-such-thing", run_it, NULL, NULL) == -1);
    CHECK(ex_commands_bind(c, "app.close-current-tab", run_it, check_it, &five) == 0);
    CHECK(ex_commands_bound(c, "app.close-current-tab") && ex_commands_available(c, "app.close-current-tab"));
    CHECK(ex_commands_run(c, "app.close-current-tab") == 0 && ran == 5);
    can_run_flag = 0;
    CHECK(ex_commands_run(c, "app.close-current-tab") == -3 && ran == 5);          /* its check says no */
    can_run_flag = 1;
    /* a command of a feature that is off is not available; turning the feature on makes it so */
    {
        const char *gid = "graph-view.open-graph-view";
        CHECK(ex_commands_bind(c, gid, run_it, NULL, &one) == 0);
        ran = 0;
        CHECK(ex_commands_run(c, gid) == -3 && ran == 0);                           /* graph was switched off above */
        ex_features_set(f, "graph", 1);
        CHECK(ex_commands_run(c, gid) == 0 && ran == 1);
    }
    /* commands of a stub feature stay unavailable */
    {
        size_t i;
        for (i = 0; i < ex_commands_count(c); i++)
            if (strncmp(ex_commands_at(c, i)->id, "sync.", 5) == 0) { ex_commands_bind(c, ex_commands_at(c, i)->id, run_it, NULL, NULL); CHECK(!ex_commands_available(c, ex_commands_at(c, i)->id)); }
    }
    ex_commands_free(c);
    ex_features_free(f);
    ex_settings_close(s);
    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("registry: ok");
    return 0;
}
