/* title_scroll: a header widget that is a child of an editor (the inline title) scrolls with the text, so each pane has its own (ISSUES 11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ui.h"
#include "reading_view.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static float caret_y(ExWidget *ed, ExRenderer *r) { float x, y, w, h; ex_editor_caret_rect(ed->editor, r, &x, &y, &w, &h); return y; }

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExWidget *root, *ed, *title;
    ExDoc *doc, *tdoc = ex_doc_create("Title", 5);
    char text[4000];
    size_t i, len = 0;
    float y0, th;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    for (i = 0; i < 100; i++) len += (size_t)snprintf(text + len, sizeof text - len, "line %zu\n", i);
    doc = ex_doc_create(text, len);
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("title scroll", 600, 400);
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    ed = ex_widget_editor(ui, doc, "note");
    title = ex_widget_editor(ui, tdoc, "title");
    title->width = ex_size_grow(); title->height = ex_size_token(EX_SIZE_SPACING, 7.0f);
    ex_editor_set_mode(ed->editor, EX_EDITOR_PLAIN);
    ex_widget_add(ed, title);
    ex_widget_add(root, ed);
    ex_ui_layout(ui, root, 600, 400);
    th = title->h;
    CHECK(th > 20.0f && title->y == ed->y && title->x == ed->x && title->w == ed->w);
    y0 = caret_y(ed, r);                                          /* the first row starts under the title */
    CHECK(y0 >= ed->y + th - 1.0f);
    CHECK(ex_ui_widget_at(root, ed->x + 10, ed->y + 5) == title);

    ex_editor_scroll(ed->editor, 20.0f);                          /* the title and the text move together */
    ex_ui_layout(ui, root, 600, 400);
    CHECK(title->y == ed->y - 20.0f);
    CHECK(caret_y(ed, r) == y0 - 20.0f);
    CHECK(ex_ui_widget_at(root, ed->x + 10, ed->y + 5) == title); /* 20 px of it is still in view */

    ex_editor_scroll(ed->editor, th);                             /* scrolled out: the point is the text's, and nothing above the editor is the title's */
    ex_ui_layout(ui, root, 600, 400);
    CHECK(ex_ui_widget_at(root, ed->x + 10, ed->y + 5) == ed);
    CHECK(ex_ui_widget_at(root, ed->x + 10, ed->y - 5) != title);

    ex_editor_scroll(ed->editor, -100000.0f);                     /* back at the top: the title shows again */
    ex_ui_layout(ui, root, 600, 400);
    CHECK(title->y == ed->y && caret_y(ed, r) == y0);

    title->hidden = 1;                                            /* no title (not a Markdown note): the text starts at the top */
    ex_ui_layout(ui, root, 600, 400);
    CHECK(caret_y(ed, r) < y0);

    {   /* the reading view too (ISSUES 25): the title is above the page and scrolls with it */
        ExDoc *rdoc = ex_doc_create("Title", 5);
        ExWidget *rv, *rt, *root2 = ex_widget_box(ui, 1, "root2");
        char page[4000];
        size_t pn = 0;
        float h0;
        for (i = 0; i < 100; i++) pn += (size_t)snprintf(page + pn, sizeof page - pn, "paragraph %zu\n\n", i);      /* a page much taller than the view */
        root2->width = ex_size_grow(); root2->height = ex_size_grow();
        rv = ex_widget_reading(ui, page, pn, "page");
        rt = ex_widget_editor(ui, rdoc, "title");
        rt->width = ex_size_grow(); rt->height = ex_size_token(EX_SIZE_SPACING, 7.0f);
        ex_widget_add(rv, rt);
        ex_widget_add(root2, rv);
        ex_ui_layout(ui, root2, 600, 400);
        ex_ui_draw(ui, root2, 1);                                 /* the page is laid out when it is drawn */
        h0 = ex_readingview_content_height(rv->reading);
        CHECK(rt->h > 20.0f && rt->y == rv->y && rt->w == rv->w);
        ex_reading_scroll(rv, 30.0f);
        ex_ui_layout(ui, root2, 600, 400);
        CHECK(rt->y == rv->y - 30.0f);
        CHECK(ex_ui_widget_at(root2, rv->x + 10, rv->y + 5) == rt);
        ex_reading_scroll(rv, rt->h);
        ex_ui_layout(ui, root2, 600, 400);
        CHECK(ex_ui_widget_at(root2, rv->x + 10, rv->y + 5) == rv);
        rt->hidden = 1;                                           /* no title: the page is shorter by the title's height */
        ex_ui_layout(ui, root2, 600, 400);
        ex_ui_draw(ui, root2, 1);
        CHECK(ex_readingview_content_height(rv->reading) < h0);
        ex_widget_free(root2);
        ex_doc_free(rdoc);
    }

    ex_widget_free(root);
    ex_doc_free(doc); ex_doc_free(tdoc);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    SDL_Quit();
    return failures ? 1 : 0;
}
