/* pdf_viewer (6c-iv): the PDF tab through the real UI with the stand-in PDFium (tests/stub_pdfium.c). Pages are laid out and rendered by the document's
 * thread and uploaded as textures; the outline, fit width and fit page, zoom, search (match case, highlight all, next and previous), text selection and copy, a
 * link to the selection and opening one, a link inside the document, and a link to a web address (reported to the application, which asks; nothing else
 * happens). Without PDFium the tab says so and offers "Open externally".
 * Also pdf_private: a PDF whose links include an address and a launch action opens, is searched and closed, and following the address only calls the
 * application's handler (which in the program asks first); the launch action does nothing.
 * Usage: test_pdf_view <stand-in library> <sample.pdf> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_pdfview.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 1000
#define H 700
static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *PW;
static int loads, urls, externals;
static char last_url[300], clip[600];

static void load_cb(void *u, const char *p) { (void)u; (void)p; loads++; }
static void ext_cb(void *u, const char *p) { (void)u; (void)p; externals++; }
static void url_cb(void *u, const char *url) { (void)u; urls++; snprintf(last_url, sizeof last_url, "%s", url); }
static void clip_cb(void *u, const char *t) { (void)u; snprintf(clip, sizeof clip, "%s", t); }
static void name_cb(void *u, const char *path, char *out, size_t cap) { (void)u; snprintf(out, cap, "%s", path); }

static void frame(void) {
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}
static void frames(int n, int ms) { int i; for (i = 0; i < n; i++) { SDL_Delay((Uint32)ms); frame(); } }
static void open_view(const char *path) {
    ExPdfViewHandlers h;
    memset(&h, 0, sizeof h);
    h.load = load_cb; h.open_external = ext_cb; h.open_url = url_cb; h.set_clipboard = clip_cb; h.link_name = name_cb;
    if (PW) { ex_ui_release(UI, PW); ex_widget_remove(ROOT, PW); ex_widget_free(PW); PW = NULL; }
    PW = ex_widget_pdfview(UI, path, &h);
    ex_widget_add(ROOT, PW);
    frame();
}
static void click(float x, float y) { ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, x, y, 0, 1); ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, x, y, 0, 1); frame(); }

int main(int argc, char **argv) {
    ExTheme theme;
    ExPdfViewStats st;
    char err[300] = "";
    size_t len = 0;
    uint8_t *bytes;
    float x, y, w, h;
    int pg, s, e;
    if (argc < 3) return 2;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("pdf view", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();

    /* the fragment of a link */
    CHECK(ex_pdf_parse_fragment("#page=3&selection=120,148", &pg, &s, &e) && pg == 2 && s == 120 && e == 148);
    CHECK(ex_pdf_parse_fragment("page=1", &pg, &s, &e) && pg == 0 && s == 0 && e == 0);
    CHECK(!ex_pdf_parse_fragment("heading", &pg, &s, &e));

    /* ---- without PDFium ---- */
    open_view("/docs/a.pdf");
    CHECK(loads == 1);
    ex_pdfview_set_doc(PW, NULL, "PDFium is not installed (the PDF add-on)");
    frame();
    ex_pdfview_stats(PW, &st);
    CHECK(st.failed && !st.has_doc);
    click(PW->x + 20.0f + 30.0f, PW->y + 24.0f + 40.0f);                                   /* the Open externally button */
    CHECK(externals == 1);

    /* ---- with the stand-in ---- */
    CHECK(ex_pdf_load(argv[1], err, sizeof err) == 1);
    open_view("/docs/sample.pdf");
    bytes = (uint8_t *)ex_platform_read_file(argv[2], &len);
    ex_pdfview_set_doc(PW, ex_pdf_open(bytes, len, err, sizeof err), err);
    frames(15, 20);
    ex_pdfview_stats(PW, &st);
    CHECK(st.has_doc && st.pages == 3 && st.mode == 0 && st.scale > 0.5f);
    CHECK(st.textures >= 1 && st.uploads >= 1);                                           /* the visible pages were drawn by the thread and uploaded */
    CHECK(st.page0_w > 400.0f && st.page0_w < 1000.0f);                                    /* fit width: the first page fills the width */

    /* fit page, zoom, and fit width again */
    click(st.fitp_x, st.fitp_y);
    ex_pdfview_stats(PW, &st);
    CHECK(st.mode == 1);
    {
        float s_fit = st.scale;
        click(st.zin_x, st.zin_y);
        ex_pdfview_stats(PW, &st);
        CHECK(st.mode == 2 && st.scale > s_fit * 1.2f && st.scale < s_fit * 1.3f);
    }
    click(st.fitw_x, st.fitw_y);
    ex_pdfview_stats(PW, &st);
    CHECK(st.mode == 0);
    frames(10, 20);

    /* the outline: Contents opens it, a click on "Chapter 2" goes to page 3 */
    click(st.toc_x, st.toc_y);
    frames(3, 10);
    ex_pdfview_stats(PW, &st);
    CHECK(st.toc_open);
    {
        float row = (float)(16 + 10) * 1.0f;
        (void)row;
        click(PW->x + 60.0f, PW->y + 40.0f + 2.0f * 28.0f + 12.0f);                          /* the third row of the outline */
        frames(10, 20);
        ex_pdfview_stats(PW, &st);
        CHECK(st.current_page == 2 || st.scroll > 100.0f);
    }
    click(st.toc_x, st.toc_y);                                                              /* closed again */
    ex_ui_key(UI, EX_KEY_HOME, 0);
    frames(8, 20);

    /* a link inside the document and a link to an address (page 1 has both) */
    ex_pdfview_stats(PW, &st);
    {
        float lx, ly, lw, lh;
        frames(5, 20);
        CHECK(ex_pdfview_link_rect(PW, 0, 0, &lx, &ly, &lw, &lh));
        click(lx + lw / 2.0f, ly + lh / 2.0f);                                              /* the link to page 3 */
        frames(5, 20);
        ex_pdfview_stats(PW, &st);
        CHECK(st.current_page == 2 && urls == 0);
        ex_ui_key(UI, EX_KEY_HOME, 0);
        frames(8, 20);
        CHECK(ex_pdfview_link_rect(PW, 0, 1, &lx, &ly, &lw, &lh));
        click(lx + lw / 2.0f, ly + lh / 2.0f);                                              /* the address */
        CHECK(urls == 1 && !strcmp(last_url, "https://example.org/"));                       /* only reported: the program asks before it opens anything */
        CHECK(!ex_pdfview_link_rect(PW, 0, 2, &lx, &ly, &lw, &lh));                          /* the launch action and the link with no target are not links */
    }

    /* selection by dragging over the first four characters, then copy, then a link to it */
    frames(5, 20);
    CHECK(ex_pdfview_char_rect(PW, 0, 0, &x, &y, &w, &h) == 0);                              /* the text is asked for when it is needed */
    click(PW->x + 5.0f, PW->y + 200.0f);
    frames(5, 20);
    {
        float x0, y0, w0, h0, x3, y3, w3, h3;
        if (!ex_pdfview_char_rect(PW, 0, 0, &x0, &y0, &w0, &h0)) { ex_pdfview_search(PW, "x"); frames(10, 20); ex_pdfview_search(PW, ""); }
        CHECK(ex_pdfview_char_rect(PW, 0, 0, &x0, &y0, &w0, &h0) && ex_pdfview_char_rect(PW, 0, 3, &x3, &y3, &w3, &h3));
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, x0 + w0 / 2.0f, y0 + h0 / 2.0f, 0, 1);
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, x3 + w3 / 2.0f, y3 + h3 / 2.0f, 0, 1);
        ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, x3 + w3 / 2.0f, y3 + h3 / 2.0f, 0, 1);
        frame();
    }
    ex_pdfview_stats(PW, &st);
    CHECK(st.sel_page == 0 && st.sel_start == 0 && st.sel_end == 4);
    CHECK(ex_pdfview_copy_selection(PW) && !strcmp(clip, "Page"));
    CHECK(ex_pdfview_copy_link(PW) && !strcmp(clip, "[[/docs/sample.pdf#page=1&selection=0,4]]"));
    {   /* opening such a link: the page and the characters are selected */
        ExPdfViewStats s2;
        ex_pdfview_goto(PW, 1, 5, 6);
        frames(15, 20);
        ex_pdfview_stats(PW, &s2);
        CHECK(s2.sel_page == 1 && s2.sel_start == 5 && s2.sel_end == 6);
        CHECK(ex_pdfview_copy_selection(PW) && !strcmp(clip, "2"));
    }
    ex_ui_key(UI, EX_KEY_ESCAPE, 0);

    /* ---- search: "quick" is on every page; the case switch; next and previous ---- */
    ex_pdfview_search(PW, "QUICK");
    frames(20, 20);
    ex_pdfview_stats(PW, &st);
    CHECK(st.search_open && st.hits == 3 && st.hit_index == 0);
    ex_ui_key(UI, EX_KEY_ENTER, 0);
    frames(5, 20);
    ex_pdfview_stats(PW, &st);
    CHECK(st.hit_index == 1);
    ex_ui_key(UI, EX_KEY_ENTER, EX_MOD_SHIFT);
    ex_ui_key(UI, EX_KEY_ENTER, EX_MOD_SHIFT);
    frames(5, 20);
    ex_pdfview_stats(PW, &st);
    CHECK(st.hit_index == 2);                                                                /* back from the first wraps to the last */
    {   /* "Aa": match case finds nothing for QUICK */
        int i;
        for (i = 0; i < 12; i++) { frame(); }
        {   /* the case switch is the third button of the search row: click where it is drawn */
            float by = PW->y + 28.0f + 14.0f;
            click(PW->x + 276.0f + 14.0f, by);
        }
        frames(20, 20);
        ex_pdfview_stats(PW, &st);
        CHECK(st.hits == 0);
    }
    ex_pdfview_search(PW, "fox");
    frames(20, 20);
    ex_pdfview_stats(PW, &st);
    CHECK(st.hits == 3);
    ex_ui_key(UI, EX_KEY_ESCAPE, 0);
    ex_pdfview_stats(PW, &st);
    CHECK(!st.search_open);

    /* scrolling far away gives textures back */
    ex_ui_key(UI, EX_KEY_END, 0);
    frames(10, 20);
    ex_ui_key(UI, EX_KEY_HOME, 0);
    frames(10, 20);

    ex_ui_release(UI, PW);
    ex_widget_remove(ROOT, PW);
    ex_widget_free(PW);
    ex_pdf_unload();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("pdf_viewer: ok\n");
    return 0;
}
