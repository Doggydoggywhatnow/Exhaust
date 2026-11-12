/* editing_rect: rectangular selection on a column of text: one selection per line covering the same columns, short lines
 * clipped, typing and deleting act on every line, and dragging back up works the same. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_render.h"
#include "exhaust_text.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static float col_x(ExEditor *e, ExDoc *d, ExRenderer *r, size_t line, size_t col, float *y) {
    float x;
    ex_doc_set_caret(d, ex_buffer_line_start(ex_doc_buffer(d), line) + col);
    ex_editor_caret_rect(e, r, &x, y, NULL, NULL);
    return x;
}

static char *text_of(ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }

int main(void) {
    const char *src = "abcdef\nabcdef\nab\nabcdef\n";
    ExRenderer *r;
    ExDoc *d;
    ExEditor *e;
    float x1, x4, y0, y3, ty;
    char *t, *sel;
    size_t len = 0, i;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("rect", 800, 600);
    if (!r) return 1;
    d = ex_doc_create(src, strlen(src));
    e = ex_editor_create(d);
    ex_editor_set_font(e, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
    ex_editor_set_viewport(e, 0, 0, 800, 600);
    x1 = col_x(e, d, r, 0, 1, &y0);
    x4 = col_x(e, d, r, 0, 4, &ty);
    col_x(e, d, r, 3, 0, &y3);

    /* a rectangle from column 1 of line 0 to column 4 of line 3 */
    ex_editor_mouse(e, r, EX_MOUSE_DOWN, x1, y0 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    ex_editor_mouse(e, r, EX_MOUSE_DRAG, x4, y3 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    ex_editor_mouse(e, r, EX_MOUSE_UP, x4, y3 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    CHECK(ex_doc_sel_count(d) == 4);
    sel = ex_editor_selected_text(e, &len);
    CHECK(sel && strcmp(sel, "bcd\nbcd\nb\nbcd") == 0);
    free(sel);
    CHECK(ex_doc_sel(d, 0).anchor == 1 && ex_doc_sel(d, 0).head == 4);
    ex_editor_text_input(e, "X", 1);                                   /* typing replaces the column on every line */
    t = text_of(d);
    CHECK(strcmp(t, "aXef\naXef\naX\naXef\n") == 0);
    free(t);
    ex_doc_undo(d);
    t = text_of(d);
    CHECK(strcmp(t, src) == 0);
    free(t);

    /* dragging from bottom right to top left gives the same selections, and Delete removes the columns */
    ex_editor_mouse(e, r, EX_MOUSE_DOWN, x4, y3 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    ex_editor_mouse(e, r, EX_MOUSE_DRAG, x1, y0 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    ex_editor_mouse(e, r, EX_MOUSE_UP, x1, y0 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    CHECK(ex_doc_sel_count(d) == 4);
    ex_editor_key(e, r, EX_KEY_DELETE, 0);
    t = text_of(d);
    CHECK(strcmp(t, "aef\naef\na\naef\n") == 0);
    free(t);
    ex_doc_undo(d);

    /* a zero-width rectangle is a column of carets (multiple cursors), and a click without the keys is an ordinary click */
    ex_editor_mouse(e, r, EX_MOUSE_DOWN, x4, y0 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    ex_editor_mouse(e, r, EX_MOUSE_DRAG, x4, y3 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    ex_editor_mouse(e, r, EX_MOUSE_UP, x4, y3 + 2, EX_MOD_ALT | EX_MOD_SHIFT, 1);
    CHECK(ex_doc_sel_count(d) == 4);
    for (i = 0; i < 4; i++) CHECK(ex_doc_sel(d, i).anchor == ex_doc_sel(d, i).head);
    ex_editor_text_input(e, "-", 1);
    t = text_of(d);
    CHECK(strcmp(t, "abcd-ef\nabcd-ef\nab-\nabcd-ef\n") == 0);
    free(t);
    ex_editor_mouse(e, r, EX_MOUSE_DOWN, x1, y0 + 2, 0, 1);
    ex_editor_mouse(e, r, EX_MOUSE_UP, x1, y0 + 2, 0, 1);
    CHECK(ex_doc_sel_count(d) == 1);

    ex_editor_free(e);
    ex_doc_free(d);
    ex_renderer_destroy(r);
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
