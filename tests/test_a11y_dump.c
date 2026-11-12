/* a11y_dump: because a screen reader cannot run in CI, this dumps the sample
 * window's accessibility tree as text and checks it against a golden file, and
 * checks roles, names and the Tab (focus) order directly. Run with
 * UPDATE_GOLDEN=1 to rewrite the golden file after reviewing a change.
 * Usage: test_a11y_dump <golden-file> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ui.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* Compares ignoring carriage returns (git may check the golden out with CRLF on Windows). */
static int same_text(const char *a, const char *b) {
    for (;;) {
        while (*a == '\r') a++;
        while (*b == '\r') b++;
        if (*a != *b) return 0;
        if (!*a) return 1;
        a++; b++;
    }
}

static void on_toggle(ExWidget *w, void *u) { (void)w; (void)u; }

int main(int argc, char **argv) {
    const char *golden = argc > 1 ? argv[1] : "tests/golden/a11y_sample.txt";
    const char *intro = "Welcome to Exhaust.\n\nType here. \xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A \xE4\xBD\xA0\xE5\xA5\xBD\n";
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc;
    ExWidget *root, *editor;
    ExA11yTree *t;
    char *dump;
    uint64_t order[8];
    size_t n;
    ExA11yInfo in;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("a11y dump", 960, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    ui = ex_ui_create(r, &theme, 16.0f);
    doc = ex_doc_create(intro, strlen(intro));
    root = ex_sample_window(ui, doc, on_toggle, NULL, &editor);
    ex_ui_layout(ui, root, 960, 600);
    ex_ui_set_focus(ui, editor);
    t = ex_a11y_create();
    CHECK(ex_ui_a11y_sync(ui, root, t) == 0);

    /* roles, names and parents */
    CHECK(ex_a11y_count(t) == ex_widget_count(root));
    CHECK(ex_a11y_get(t, root->id, &in) == 0 && in.role == EX_A11Y_WINDOW && strcmp(in.name, "Exhaust") == 0);
    CHECK(ex_a11y_get(t, editor->id, &in) == 0 && in.role == EX_A11Y_TEXT_INPUT && strcmp(in.name, "Note text") == 0);
    CHECK(ex_a11y_focus(t) == editor->id);

    /* the Tab order: the theme button, then the editor */
    n = ex_a11y_focus_order(t, order, 8);
    CHECK(n == 2);
    if (n == 2) {
        ExA11yInfo a, b;
        CHECK(ex_a11y_get(t, order[0], &a) == 0 && ex_a11y_get(t, order[1], &b) == 0);
        CHECK(a.role == EX_A11Y_BUTTON && strcmp(a.name, "Switch light and dark theme") == 0);
        CHECK(b.role == EX_A11Y_TEXT_INPUT && order[1] == editor->id);
    }
    /* every node that can be focused or clicked has a name a screen reader can speak */
    {
        size_t i;
        uint64_t all[16];
        size_t cnt = ex_a11y_focus_order(t, all, 16);
        for (i = 0; i < cnt; i++) { ExA11yInfo x; CHECK(ex_a11y_get(t, all[i], &x) == 0 && x.name[0] != 0); }
    }

    /* the dump against the golden file */
    dump = ex_a11y_dump(t);
    CHECK(dump != NULL);
    if (dump) {
        if (getenv("UPDATE_GOLDEN")) {
            CHECK(ex_platform_write_file_atomic(golden, dump, strlen(dump)) == 0);
            fprintf(stderr, "wrote %s\n", golden);
        } else {
            char *want = ex_platform_read_file(golden, NULL);
            if (!want) { fprintf(stderr, "cannot read %s\n", golden); failures++; }
            else {
                if (!same_text(dump, want)) {
                    fprintf(stderr, "accessibility tree differs from the golden file.\n--- got ---\n%s--- want ---\n%s", dump, want);
                    failures++;
                }
                free(want);
            }
        }
        free(dump);
    }

    ex_widget_free(root);
    ex_a11y_free(t);
    ex_doc_free(doc);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
    SDL_Quit();
    return failures ? 1 : 0;
}
