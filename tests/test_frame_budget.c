/* frame_budget (sub-slice 4d): the interface stays responsive. On a large note (6000 lines of live-preview Markdown) typing,
 * Enter in the middle of the text, paging, resizing the window and a full frame stay within budget: an input handler within
 * 16 ms (the project's rule that the interface thread never waits longer), a whole frame (layout and drawing with the
 * software renderer, the slowest there is) within 60 ms. The 95th percentile is checked, so one slow moment of a busy
 * machine does not fail the test; sanitizer builds are given ten times the time. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ui.h"

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif
#ifndef SANITIZED
#define SANITIZED 0
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double p95(double *v, size_t n) { qsort(v, n, sizeof *v, cmp_d); return v[(n * 95) / 100 >= n ? n - 1 : (n * 95) / 100]; }

static void report(const char *what, double *v, size_t n, double limit) {
    size_t k;
    double p;
    if (getenv("FRAME_BUDGET_VERBOSE")) for (k = 0; k < n; k++) if (v[k] > 4.0) printf("  %s #%zu: %.1f ms\n", what, k, v[k]);
    p = p95(v, n);
    printf("%s: 95th percentile %.2f ms (limit %.0f ms), worst %.2f ms\n", what, p, limit, v[n - 1]);
    if (p >= limit) { fprintf(stderr, "%s is over budget\n", what); failures++; }
}

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc;
    ExWidget *root, *ed;
    char *text;
    size_t i, len = 0, cap = 6000 * 90;
    double key_limit = SANITIZED ? 160 : 16, frame_limit = SANITIZED ? 600 : 60;
    double keys[200], frames[200];
    size_t nk = 0, nf = 0;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    text = malloc(cap);
    for (i = 0; i < 6000; i++) {
        switch (i % 6) {
        case 0: len += (size_t)snprintf(text + len, cap - len, "## Section %zu\n", i / 6); break;
        case 1: len += (size_t)snprintf(text + len, cap - len, "A paragraph with **bold**, *italic*, `code` and a [[Link %zu]] to another note.\n", i); break;
        case 2: len += (size_t)snprintf(text + len, cap - len, "- a list item with a [markdown link](Note %zu.md)\n", i); break;
        case 3: len += (size_t)snprintf(text + len, cap - len, "- [ ] a task #tag%zu\n", i % 40); break;
        case 4: len += (size_t)snprintf(text + len, cap - len, "> a quotation that is long enough to be a real line of text, with some words in it\n"); break;
        default: len += (size_t)snprintf(text + len, cap - len, "\n"); break;
        }
    }
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("frame budget", 1000, 700);
    ui = ex_ui_create(r, &theme, 16.0f);
    doc = ex_doc_create(text, len);
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    ed = ex_widget_editor(ui, doc, "note");
    ed->width = ex_size_grow(); ed->height = ex_size_grow();
    ex_editor_set_mode(ed->editor, EX_EDITOR_LIVE);
    ex_editor_set_wrap(ed->editor, 1);                         /* as the note editors are: long lines wrap */
    ex_widget_add(root, ed);
    ex_ui_set_focus(ui, ed);
    ex_ui_layout(ui, root, 1000, 700);
    ex_ui_draw(ui, root, 1);                                  /* the first frame loads fonts and builds the tokens */
    ex_doc_set_caret(doc, len / 2);

    /* typing: the handler, then the frame */
    for (i = 0; i < 60; i++) {
        double t0 = now_ms(), t1;
        ex_ui_text_input(ui, "x", 1);
        t1 = now_ms();
        if (nk < 200) keys[nk++] = t1 - t0;
        ex_ui_layout(ui, root, 1000, 700); ex_ui_draw(ui, root, 1);
        { double ft = now_ms() - t1; if (getenv("FRAME_BUDGET_VERBOSE") && ft > 20) printf("  slow frame after step %zu: %.1f ms (handler %.1f)\n", i, ft, keys[nk - 1]); if (nf < 200) frames[nf++] = ft; }
    }
    /* Enter in the middle of the text, and paging */
    for (i = 0; i < 40; i++) {
        double t0 = now_ms(), t1;
        ex_ui_key(ui, i % 4 == 3 ? EX_KEY_PAGE_DOWN : EX_KEY_ENTER, 0);
        t1 = now_ms();
        if (nk < 200) keys[nk++] = t1 - t0;
        ex_ui_layout(ui, root, 1000, 700); ex_ui_draw(ui, root, 1);
        { double ft = now_ms() - t1; if (getenv("FRAME_BUDGET_VERBOSE") && ft > 20) printf("  slow frame after step %zu: %.1f ms (handler %.1f)\n", i, ft, keys[nk - 1]); if (nf < 200) frames[nf++] = ft; }
    }
    /* resizing the window: layout and drawing at many sizes */
    for (i = 0; i < 40; i++) {
        double t0 = now_ms();
        float w = 700.0f + (float)((i * 37) % 300), h = 450.0f + (float)((i * 53) % 250);
        ex_ui_layout(ui, root, w, h); ex_ui_draw(ui, root, 1);
        if (nf < 200) frames[nf++] = now_ms() - t0;
    }
    report("an input handler (typing, Enter, Page Down)", keys, nk, key_limit);
    report("a frame (layout and drawing)", frames, nf, frame_limit);
    ex_widget_free(root);
    ex_doc_free(doc);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    free(text);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
