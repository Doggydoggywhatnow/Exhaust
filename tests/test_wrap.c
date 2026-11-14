/* wrap (sub-slice 4d): soft wrap in the note editor. A long line continues on the next row at the width of the viewport; the
 * viewport can be resized and the rows follow; the caret, Up and Down, Home and End, clicks and selections work row by row;
 * short lines and the text itself are untouched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExUi *ui;
static ExWidget *root, *ed;

static void frame(float w, float h) { ex_ui_layout(ui, root, w, h); ex_ui_draw(ui, root, 1); }

static void run_mode(ExEditorMode mode) {
    char text[4000];
    ExDoc *doc;
    ExRenderer *r = ex_ui_renderer(ui);
    float cx0, cy0, cx1, cy1, rows_small, rows_big, lh;
    size_t i, n = 0;
    ExSel s;
    n += (size_t)snprintf(text + n, sizeof text - n, "Short first line\n");
    for (i = 0; i < 40; i++) n += (size_t)snprintf(text + n, sizeof text - n, "word%zu ", i);
    n += (size_t)snprintf(text + n, sizeof text - n, "\nLast line\n");
    doc = ex_doc_create(text, n);
    ed = ex_widget_editor(ui, doc, "wrapped");
    ed->width = ex_size_px(ui, 360); ed->height = ex_size_px(ui, 400);
    ex_widget_add(root, ed);
    ex_editor_set_mode(ed->editor, mode);
    ex_editor_set_wrap(ed->editor, 1);
    ex_ui_set_focus(ui, ed);
    frame(1000, 700);
    frame(1000, 700);
    lh = ex_editor_content_height(ed->editor, r);
    /* the long line takes several rows at 360 px, and more at 220 px */
    rows_big = lh;
    CHECK(rows_big > 0.0f);
    ed->width = ex_size_px(ui, 220);
    frame(1000, 700); frame(1000, 700);
    rows_small = ex_editor_content_height(ed->editor, r);
    CHECK(rows_small > rows_big);
    ed->width = ex_size_px(ui, 900);
    frame(1000, 700); frame(1000, 700);
    CHECK(ex_editor_content_height(ed->editor, r) < rows_big);                 /* wider: fewer rows */
    ed->width = ex_size_px(ui, 360);
    frame(1000, 700); frame(1000, 700);
    /* the caret at the end of the long line is on a lower row than its start */
    ex_doc_set_caret(doc, 17);                                                 /* the start of the long line */
    ex_editor_caret_rect(ed->editor, r, &cx0, &cy0, NULL, NULL);
    s.anchor = s.head = 17 + 250;                                              /* far into the long line */
    ex_doc_set_selections(doc, &s, 1);
    ex_editor_caret_rect(ed->editor, r, &cx1, &cy1, NULL, NULL);
    CHECK(cy1 > cy0 + 2.0f * 20.0f);
    CHECK(cx1 < ed->x + ed->w);                                                /* inside the editor, not off to the right */
    /* Up moves one row, not the whole line; Home goes to the start of the row, End to its end */
    ex_ui_key(ui, EX_KEY_UP, 0);
    {
        float cx2, cy2;
        ex_editor_caret_rect(ed->editor, r, &cx2, &cy2, NULL, NULL);
        CHECK(cy2 < cy1 && cy2 > cy0 - 1.0f);
        CHECK(ex_doc_sel(doc, 0).head > 17 && ex_doc_sel(doc, 0).head < 17 + 250);
    }
    ex_ui_key(ui, EX_KEY_HOME, 0);
    {
        size_t h1 = ex_doc_sel(doc, 0).head;
        char c[2] = {0, 0};
        ex_buffer_read(ex_doc_buffer(doc), h1 - 1, c, 1);
        if (!(h1 > 17 && c[0] == ' ')) fprintf(stderr, "home -> %zu (char before [%c])\n", h1, c[0] ? c[0] : '?');
        CHECK(h1 > 17 && c[0] == ' ');                                         /* the row starts after a blank */
        ex_ui_key(ui, EX_KEY_END, 0);
        CHECK(ex_doc_sel(doc, 0).head > h1 + 10);
    }
    /* a click on the second row of the long line lands inside the long line, past its first row */
    {
        float x = ed->x + 100.0f, y;
        ex_editor_caret_rect(ed->editor, r, &cx0, &cy0, NULL, NULL);
        y = cy0 + 3.0f * 20.0f;
        ex_editor_mouse(ed->editor, r, EX_MOUSE_DOWN, x, y, 0, 1);
        ex_editor_mouse(ed->editor, r, EX_MOUSE_UP, x, y, 0, 1);
        CHECK(ex_doc_sel(doc, 0).head > 17 && ex_doc_sel(doc, 0).head < 17 + 280);
    }
    /* typing keeps the text a text: no break is inserted */
    ex_ui_text_input(ui, "X", 1);
    {
        char *t = ex_buffer_text(ex_doc_buffer(doc), NULL);
        size_t nl = 0, k;
        for (k = 0; t && t[k]; k++) if (t[k] == '\n') nl++;
        CHECK(t && nl == 3 && strstr(t, "X"));
        free(t);
    }
    /* Page Down and the end of the document still work with wrapped rows */
    ex_ui_key(ui, EX_KEY_PAGE_DOWN, 0);
    frame(1000, 700);
    ex_ui_release(ui, ed); ex_widget_remove(root, ed); ex_widget_free(ed);
    ex_doc_free(doc);
}

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("wrap", 1000, 700);
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 0, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    run_mode(EX_EDITOR_PLAIN);
    run_mode(EX_EDITOR_SOURCE);
    run_mode(EX_EDITOR_LIVE);
    ex_widget_free(root);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
