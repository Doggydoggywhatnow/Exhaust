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

    /* the scroll bar: a thumb at the right edge of the widget, dragged with the pointer, and a press beside it jumps there */
    {
        ExDoc *tall;
        ExWidget *tw;
        ExEditor *te;
        char *s = malloc(300 * 8 + 1);
        size_t i;
        float y0, y1, before, mid;
        for (i = 0; i < 300; i++) memcpy(s + i * 8, "line xx\n", 8);
        s[300 * 8] = 0;
        tall = ex_doc_create(s, 300 * 8);
        tw = ex_widget_editor(ui, tall, "tall");
        te = tw->editor;
        ex_editor_set_mode(te, EX_EDITOR_LIVE);
        ex_editor_set_wrap(te, 1);
        tw->text_max = 720.0f;                                              /* a column in the middle of a wider widget */
        ex_ui_layout(ui, tw, 1400, 600);
        ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
        ex_editor_draw(te, r, &th, 1, 1);
        y0 = ex_editor_scroll_y(te);
        CHECK(y0 == 0.0f);
        before = y0;
        ex_editor_mouse(te, r, EX_MOUSE_DOWN, tw->x + tw->w - 6.0f, tw->y + 8.0f, 0, 1);         /* on the thumb at the top: it is taken, the caret does not move */
        ex_editor_mouse(te, r, EX_MOUSE_DRAG, tw->x + tw->w - 6.0f, tw->y + 108.0f, 0, 1);
        ex_editor_mouse(te, r, EX_MOUSE_UP, tw->x + tw->w - 6.0f, tw->y + 108.0f, 0, 1);
        y1 = ex_editor_scroll_y(te);
        CHECK(y1 > before + 200.0f);                                         /* 100 px of thumb is a lot of text */
        CHECK(ex_doc_sel(tall, 0).head == 0);
        mid = tw->y + tw->h - 10.0f;                                         /* beside the thumb, low in the strip: jump there */
        ex_editor_mouse(te, r, EX_MOUSE_DOWN, tw->x + tw->w - 6.0f, mid, 0, 1);
        ex_editor_mouse(te, r, EX_MOUSE_UP, tw->x + tw->w - 6.0f, mid, 0, 1);
        CHECK(ex_editor_scroll_y(te) > y1);
        CHECK(ex_doc_sel(tall, 0).head == 0);
        /* a short note has no bar: the strip is text */
        ex_ui_layout(ui, root, 1400, 600);
        ex_editor_mouse(e, r, EX_MOUSE_DOWN, ed->x + ed->w - 6.0f, ed->y + 40.0f, 0, 1);
        ex_editor_mouse(e, r, EX_MOUSE_UP, ed->x + ed->w - 6.0f, ed->y + 40.0f, 0, 1);
        CHECK(ex_editor_scroll_y(e) == 0.0f);
        ex_widget_free(tw);
        ex_doc_free(tall);
        free(s);
    }

    /* the wheel glides: a smooth scroll moves the text a part of the way each frame and ends where it was asked to */
    {
        ExDoc *tall;
        ExWidget *tw;
        char *s = malloc(300 * 8 + 1);
        size_t i;
        float a, b, c;
        int k;
        for (i = 0; i < 300; i++) memcpy(s + i * 8, "line xx\n", 8);
        s[300 * 8] = 0;
        tall = ex_doc_create(s, 300 * 8);
        tw = ex_widget_editor(ui, tall, "tall2");
        ex_editor_set_mode(tw->editor, EX_EDITOR_LIVE);
        ex_editor_set_wrap(tw->editor, 1);
        ex_ui_layout(ui, tw, 1000, 600);
        ex_editor_scroll_smooth(tw->editor, 480.0f);
        CHECK(ex_editor_scroll_y(tw->editor) == 0.0f);                      /* nothing moves until a frame is drawn */
        ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
        ex_editor_draw(tw->editor, r, &th, 1, 1);
        a = ex_editor_scroll_y(tw->editor);
        ex_editor_draw(tw->editor, r, &th, 1, 1);
        b = ex_editor_scroll_y(tw->editor);
        CHECK(a > 100.0f && a < 480.0f && b > a && b < 480.0f);              /* a part of the way, and then more */
        for (k = 0; k < 60; k++) ex_editor_draw(tw->editor, r, &th, 1, 1);
        c = ex_editor_scroll_y(tw->editor);
        CHECK(c > 479.0f && c < 481.0f);                                     /* and it ends where it was asked to */
        ex_editor_scroll_smooth(tw->editor, 480.0f);
        ex_editor_key(tw->editor, r, EX_KEY_RIGHT, 0);                       /* a key ends the glide */
        ex_editor_draw(tw->editor, r, &th, 1, 1);
        CHECK(ex_editor_scroll_y(tw->editor) < 481.0f);
        ex_widget_free(tw);
        ex_doc_free(tall);
        free(s);
    }

    /* the reading view has the same bar: a drag on the thumb scrolls the page */
    {
        char *big = malloc(400 * 12 + 1);
        ExWidget *rw;
        size_t i;
        float before;
        for (i = 0; i < 400; i++) memcpy(big + i * 12, "paragraph\n\n ", 12);
        big[400 * 12] = 0;
        rw = ex_widget_reading(ui, big, 400 * 12, "long reading");
        ex_ui_layout(ui, rw, 1000, 500);
        ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
        ex_ui_draw(ui, rw, 1);
        before = ex_reading_scroll_y(rw);
        ex_ui_mouse(ui, rw, EX_MOUSE_DOWN, rw->x + rw->w - 6.0f, rw->y + 8.0f, 0, 1);
        ex_ui_mouse(ui, rw, EX_MOUSE_DRAG, rw->x + rw->w - 6.0f, rw->y + 90.0f, 0, 1);
        ex_ui_mouse(ui, rw, EX_MOUSE_UP, rw->x + rw->w - 6.0f, rw->y + 90.0f, 0, 1);
        CHECK(ex_reading_scroll_y(rw) > before + 300.0f);
        ex_widget_free(rw);
        free(big);
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
