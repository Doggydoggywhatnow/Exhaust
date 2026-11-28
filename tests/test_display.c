/* editor_display: the settings that change how a note is laid out: readable_line_length (the editor's text is a centered column
 * no wider than 720 px at 100%, the title above it lines up with it, and off the text takes the whole pane), line_numbers (the text
 * moves right by the width of the numbers and wraps at the narrower room), indent_guides (drawn without disturbing the text, and
 * only for indented lines), and strict_line_breaks (a single line break is a line break in the reading model, or a blank). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_reading.h"
#include "exhaust_render.h"
#include "exhaust_theme.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void caret_x(ExEditor *e, ExRenderer *r, float *x) { float y, w, h; ex_editor_caret_rect(e, r, x, &y, &w, &h); }

int main(void) {
    ExRenderer *r;
    ExTheme th;
    ExUi *ui;
    ExDoc *d;
    ExWidget *root, *ed;
    ExEditor *e;
    float x_plain, x_nums;
    size_t rows_wide, rows_nums;
    char *t;
    ExReading *m;
    char long_line[400];

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("display", 1400, 600);
    if (!r) return 1;
    ex_theme_init_builtin(&th, EX_THEME_DARK);
    ui = ex_ui_create(r, &th, 16.0f);
    memset(long_line, 'a', sizeof long_line - 1);
    long_line[sizeof long_line - 1] = 0;
    {
        char *doc = malloc(sizeof long_line + 64);
        snprintf(doc, sizeof long_line + 64, "first\n    - nested\n\t- tabbed\n%s", long_line);
        d = ex_doc_create(doc, strlen(doc));
        free(doc);
    }
    ed = ex_widget_editor(ui, d, "note");
    root = ed;
    e = ed->editor;
    ex_editor_set_mode(e, EX_EDITOR_LIVE);
    ex_editor_set_wrap(e, 1);
    ex_ui_layout(ui, root, 1400, 600);

    /* no limit: the text starts at the left of the widget, and wraps at its whole width */
    ed->text_max = 0;
    ex_ui_layout(ui, root, 1400, 600);
    ex_editor_set_viewport(e, ed->x, ed->y, ed->w, ed->h);
    rows_wide = ex_editor_row_count(e);
    ex_doc_set_caret(d, 0);
    caret_x(e, r, &x_plain);
    CHECK(x_plain < 40.0f);

    /* the readable column: 720 px wide, centered in 1400, so the text starts at (1400 - 720) / 2 + the inset */
    ed->text_max = 720.0f;
    ex_ui_layout(ui, root, 1400, 600);
    caret_x(e, r, &x_nums);
    CHECK(x_nums > x_plain + 300.0f && x_nums < x_plain + 360.0f);
    {
        /* the long line wraps into more rows than with the whole width */
        float h_narrow = ex_editor_content_height(e, r);
        ed->text_max = 0;
        ex_ui_layout(ui, root, 1400, 600);
        CHECK(h_narrow > ex_editor_content_height(e, r));
        ed->text_max = 720.0f;
        ex_ui_layout(ui, root, 1400, 600);
    }
    /* a pane narrower than the column: nothing changes */
    ex_ui_layout(ui, root, 500, 600);
    caret_x(e, r, &x_nums);
    CHECK(x_nums < 40.0f);
    ed->text_max = 0;
    ex_ui_layout(ui, root, 1400, 600);

    /* line numbers move the text right, and the wrap uses the room that is left */
    caret_x(e, r, &x_plain);
    rows_wide = ex_editor_row_count(e);
    ex_editor_set_display(e, 1, 0);
    caret_x(e, r, &x_nums);
    CHECK(x_nums > x_plain + 10.0f);
    rows_nums = ex_editor_row_count(e);
    CHECK(rows_nums == rows_wide);                                            /* the same number of lines */
    {
        float h_with = ex_editor_content_height(e, r);
        ex_editor_set_display(e, 0, 0);
        CHECK(h_with >= ex_editor_content_height(e, r));
    }
    /* drawing with numbers and guides on does not change the text and does not crash */
    ex_editor_set_display(e, 1, 1);
    ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
    ex_editor_draw(e, r, &th, 1, 1);
    t = ex_buffer_text(ex_doc_buffer(d), NULL);
    CHECK(strncmp(t, "first\n    - nested\n\t- tabbed\naaaa", 33) == 0);
    free(t);
    /* a document of 1000 lines needs wider numbers than one of 10 */
    {
        ExDoc *big;
        ExEditor *be;
        float xs, xb;
        char *s = malloc(1000 * 2 + 1);
        size_t i;
        for (i = 0; i < 1000; i++) { s[2 * i] = 'x'; s[2 * i + 1] = '\n'; }
        s[2000] = 0;
        big = ex_doc_create(s, 2000);
        be = ex_editor_create(big);
        ex_editor_set_font(be, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
        ex_editor_set_viewport(be, 0, 0, 800, 600);
        ex_editor_set_mode(be, EX_EDITOR_SOURCE);
        ex_editor_set_display(be, 1, 0);
        ex_doc_set_caret(big, 0);
        caret_x(be, r, &xb);
        {
            ExDoc *small = ex_doc_create("x\nx", 3);
            ExEditor *se = ex_editor_create(small);
            ex_editor_set_font(se, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
            ex_editor_set_viewport(se, 0, 0, 800, 600);
            ex_editor_set_mode(se, EX_EDITOR_SOURCE);
            ex_editor_set_display(se, 1, 0);
            ex_doc_set_caret(small, 0);
            caret_x(se, r, &xs);
            CHECK(xb > xs + 5.0f);
            ex_editor_free(se); ex_doc_free(small);
        }
        ex_editor_free(be); ex_doc_free(big); free(s);
    }
    /* a plain field never shows them */
    {
        ExDoc *pd = ex_doc_create("title", 5);
        ExEditor *pe = ex_editor_create(pd);
        float xp, xq;
        ex_editor_set_font(pe, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
        ex_editor_set_viewport(pe, 0, 0, 800, 100);
        ex_doc_set_caret(pd, 0);
        caret_x(pe, r, &xp);
        ex_editor_set_display(pe, 1, 1);
        caret_x(pe, r, &xq);
        CHECK(xp == xq);
        ex_editor_free(pe); ex_doc_free(pd);
    }

    /* strict line breaks, in the reading model */
    m = ex_reading_build_ex("one\ntwo\n\nthree  \nfour", 21, 0);
    t = ex_reading_plain_text(m);
    CHECK(strstr(t, "one\ntwo") != NULL);
    free(t); ex_reading_free(m);
    m = ex_reading_build_ex("one\ntwo\n\nthree  \nfour", 21, 1);
    t = ex_reading_plain_text(m);
    CHECK(strstr(t, "one two") != NULL);
    CHECK(strstr(t, "three\nfour") != NULL);                                   /* two blanks at the end of a line are always a break */
    free(t); ex_reading_free(m);
    m = ex_reading_build("one\ntwo", 7);                                       /* the plain call keeps the default */
    t = ex_reading_plain_text(m);
    CHECK(strstr(t, "one\ntwo") != NULL);
    free(t); ex_reading_free(m);

    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("editor_display: ok\n");
    return 0;
}
