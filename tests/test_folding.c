/* reading_mode_folding: folding collapses and expands the right ranges. Headings fold up to the next heading
 * of the same or a higher level, list items fold their continuation and nested items, MORE and LESS work level by
 * level, the caret skips hidden lines and opens a fold it lands in, folds follow the edits, and no text changes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_render.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *NOTE =
    "# One\nintro\n## Two\na\nb\n## Three\nc\n# Four\n- item\n  continued\n  - nested\n    more\n- second\n";
/* lines: 0 # One, 1 intro, 2 ## Two, 3 a, 4 b, 5 ## Three, 6 c, 7 # Four, 8 - item, 9 continued, 10 - nested, 11 more, 12 - second, 13 (empty) */

static int hidden_set(ExEditor *e, const char *want) {   /* want: a string of 14 characters, 'h' hidden or '.' shown */
    size_t i, n = strlen(want);
    for (i = 0; i < n; i++) if (ex_editor_line_hidden(e, i) != (want[i] == 'h')) {
        fprintf(stderr, "line %zu: hidden=%d, wanted %c\n", i, ex_editor_line_hidden(e, i), want[i]);
        return 0;
    }
    return 1;
}

static char *doc_text(ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }

int main(int argc, char **argv) {
    ExRenderer *r;
    ExDoc *d;
    ExEditor *e;
    char *before, *after;
    size_t i;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("folding", 800, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    d = ex_doc_create(NOTE, strlen(NOTE));
    e = ex_editor_create(d);
    ex_editor_set_font(e, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
    ex_editor_set_viewport(e, 0, 0, 800, 600);
    before = doc_text(d);
    if (argc > 2 && strcmp(argv[1], "--ppm") == 0) {                    /* for looking at: the note drawn with some regions folded */
        ExTheme th;
        int w, h;
        uint8_t *px;
        FILE *f = fopen(argv[2], "wb");
        ex_theme_init_builtin(&th, EX_THEME_DARK);
        ex_editor_set_mode(e, EX_EDITOR_LIVE);
        ex_editor_toggle_fold_at_line(e, 2);
        ex_editor_toggle_fold_at_line(e, 8);
        ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), 5));
        ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
        ex_editor_draw(e, r, &th, 1, 1);
        px = ex_renderer_read_rgb(r, &w, &h);
        if (f && px) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(px, 1, (size_t)w * (size_t)h * 3, f); }
        if (f) fclose(f);
        free(px);
        return 0;
    }

    /* plain mode has no folding; source mode finds the six regions */
    CHECK(ex_editor_region_count(e) == 0);
    ex_editor_set_mode(e, EX_EDITOR_SOURCE);
    CHECK(ex_editor_region_count(e) == 6);
    CHECK(ex_editor_fold_count(e) == 0 && ex_editor_row_count(e) == 14);

    /* one region: the heading "Two" hides its two lines, and only those */
    CHECK(ex_editor_toggle_fold_at_line(e, 2) == 1);
    CHECK(hidden_set(e, "...hh........."));
    CHECK(ex_editor_row_count(e) == 12 && ex_editor_row_of_line(e, 5) == 3);
    CHECK(ex_editor_toggle_fold_at_line(e, 1) == 0);                    /* a line that is not a header */
    CHECK(ex_editor_toggle_fold_at_line(e, 2) == 1);
    CHECK(hidden_set(e, "..............") && ex_editor_fold_count(e) == 0);

    /* heading "One" hides everything up to "Four" including the nested headings */
    ex_editor_toggle_fold_at_line(e, 0);
    CHECK(hidden_set(e, ".hhhhhh.......") && ex_editor_row_count(e) == 8);
    ex_editor_fold(e, EX_FOLD_UNFOLD_ALL);
    CHECK(hidden_set(e, ".............."));

    /* list items: "item" hides its continuation and the nested item; "nested" only its own continuation */
    ex_editor_toggle_fold_at_line(e, 10);
    CHECK(hidden_set(e, "...........h.."));
    ex_editor_toggle_fold_at_line(e, 8);
    CHECK(hidden_set(e, ".........hhh.."));
    ex_editor_fold(e, EX_FOLD_UNFOLD_ALL);

    /* fold all, then less, level by level */
    ex_editor_fold(e, EX_FOLD_ALL);
    CHECK(hidden_set(e, ".hhhhhh.hhhhhh"));
    ex_editor_fold(e, EX_FOLD_LESS);                                    /* One and Four open; the deeper ones stay folded */
    CHECK(hidden_set(e, "...hh.h..hhh.."));
    ex_editor_fold(e, EX_FOLD_LESS);                                    /* Two, Three and item open; nested stays folded */
    CHECK(hidden_set(e, "...........h.."));
    ex_editor_fold(e, EX_FOLD_LESS);
    CHECK(hidden_set(e, ".............."));
    ex_editor_fold(e, EX_FOLD_LESS);                                    /* nothing folded: nothing happens */
    CHECK(ex_editor_fold_count(e) == 0);

    /* more: deepest level first */
    ex_editor_fold(e, EX_FOLD_MORE);
    CHECK(hidden_set(e, "...........h.."));
    ex_editor_fold(e, EX_FOLD_MORE);
    CHECK(hidden_set(e, "...hh.h..hhh.."));
    ex_editor_fold(e, EX_FOLD_MORE);
    CHECK(hidden_set(e, ".hhhhhh.hhhhhh"));
    ex_editor_fold(e, EX_FOLD_MORE);                                    /* everything is folded already */
    CHECK(hidden_set(e, ".hhhhhh.hhhhhh"));
    ex_editor_fold(e, EX_FOLD_UNFOLD_ALL);

    /* the setting switches: headings only, then lists only */
    ex_editor_set_folding(e, 1, 0);
    CHECK(ex_editor_region_count(e) == 4);
    ex_editor_set_folding(e, 0, 1);
    CHECK(ex_editor_region_count(e) == 2);
    ex_editor_fold(e, EX_FOLD_ALL);
    CHECK(hidden_set(e, ".........hhh.."));
    ex_editor_set_folding(e, 0, 0);
    CHECK(ex_editor_region_count(e) == 0 && hidden_set(e, ".............."));
    ex_editor_set_folding(e, 1, 1);
    CHECK(ex_editor_fold_count(e) == 0);                                /* turning a kind off forgot its folds */

    /* the caret skips hidden lines and a caret placed inside one opens the fold */
    ex_editor_toggle_fold_at_line(e, 2);
    ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), 2) + 3);
    ex_editor_key(e, r, EX_KEY_DOWN, 0);
    CHECK(ex_buffer_line_of_offset(ex_doc_buffer(d), ex_doc_sel(d, 0).head) == 5);
    ex_editor_key(e, r, EX_KEY_UP, 0);
    CHECK(ex_buffer_line_of_offset(ex_doc_buffer(d), ex_doc_sel(d, 0).head) == 2);
    ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), 2) + 6);       /* the end of the header */
    ex_editor_key(e, r, EX_KEY_RIGHT, 0);
    CHECK(ex_buffer_line_of_offset(ex_doc_buffer(d), ex_doc_sel(d, 0).head) == 5);
    ex_editor_key(e, r, EX_KEY_LEFT, 0);
    CHECK(ex_buffer_line_of_offset(ex_doc_buffer(d), ex_doc_sel(d, 0).head) == 2 && ex_editor_fold_count(e) == 1);
    ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), 4));            /* put inside the hidden text: the fold opens */
    CHECK(ex_editor_line_hidden(e, 4) == 0 && ex_editor_fold_count(e) == 0);
    /* folding the line the caret is in (a body line) folds the region around it and moves the caret to the header */
    ex_editor_fold(e, EX_FOLD_TOGGLE_LINE);
    CHECK(hidden_set(e, "...hh........."));
    CHECK(ex_buffer_line_of_offset(ex_doc_buffer(d), ex_doc_sel(d, 0).head) == 2);
    ex_editor_fold(e, EX_FOLD_TOGGLE_LINE);                             /* on the header: opens again */
    CHECK(ex_editor_fold_count(e) == 0);

    /* folds follow the edits: text typed above moves them, a deleted header drops its fold */
    ex_editor_toggle_fold_at_line(e, 2);
    ex_doc_set_caret(d, 0);
    ex_doc_replace_selections(d, "new line\n", 9, 0);
    CHECK(hidden_set(e, "....hh........."));                            /* lines moved down by one */
    ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), 0));
    {
        ExChange c;
        size_t a = ex_buffer_line_start(ex_doc_buffer(d), 3), b = ex_buffer_line_start(ex_doc_buffer(d), 4);   /* "## Two\n" */
        c.offset = a; c.remove_len = b - a; c.text = ""; c.text_len = 0;
        ex_doc_apply(d, &c, 1, 0, NULL, 0);
    }
    CHECK(ex_editor_fold_count(e) == 0 && hidden_set(e, ".............."));
    ex_doc_undo(d);
    ex_doc_undo(d);

    /* a long run of random folds and edits never leaves a caret in hidden text or changes text it was not asked to */
    after = doc_text(d);
    CHECK(strcmp(before, after) == 0);                                  /* undo got the text back: folding never touched it */
    free(after);
    srand(7);
    for (i = 0; i < 400; i++) {
        size_t lines;
        int op = rand() % 7;
        ExSel p;
        ex_editor_fold(e, op == 0 ? EX_FOLD_ALL : op == 1 ? EX_FOLD_MORE : op == 2 ? EX_FOLD_LESS : EX_FOLD_TOGGLE_LINE);
        lines = ex_buffer_line_count(ex_doc_buffer(d));
        if (op >= 3) ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), (size_t)rand() % lines));
        if (op == 6) {
            static const char *ins[] = { "# H\n", "x\n", "- li\n  more\n", "\n" };
            const char *t = ins[rand() % 4];
            ex_doc_replace_selections(d, t, strlen(t), 0);
        }
        ex_editor_key(e, r, rand() % 2 ? EX_KEY_DOWN : EX_KEY_UP, 0);
        (void)ex_editor_row_count(e);
        p = ex_doc_sel(d, 0);
        CHECK(!ex_editor_line_hidden(e, ex_buffer_line_of_offset(ex_doc_buffer(d), p.head)));
        CHECK(ex_editor_row_count(e) >= 1 && ex_editor_row_count(e) <= ex_buffer_line_count(ex_doc_buffer(d)));
    }

    free(before);
    ex_editor_free(e);
    ex_doc_free(d);
    ex_renderer_destroy(r);
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
