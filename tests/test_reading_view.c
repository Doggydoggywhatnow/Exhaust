/* reading_view: the reading view lays the note out for a viewport, scrolls, follows links when clicked, folds
 * callouts, and gives a screen reader the note as plain text. Usage: test_reading_view <sample-note.md> [--ppm FILE] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_theme.h"
#include "reading_view.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExRenderer *g_r;
static ExTheme g_theme;

static struct { int calls; ExRSpanKind kind; char target[200]; } g_link;
static void on_link(ExRSpanKind kind, const char *target, void *user) {
    (void)user;
    g_link.calls++;
    g_link.kind = kind;
    snprintf(g_link.target, sizeof g_link.target, "%s", target ? target : "");
}

static ExReadingView *make(const char *md, float w, float h) {
    ExReadingView *v = ex_readingview_create(md, strlen(md));
    ex_readingview_set_viewport(v, 0, 0, w, h);
    ex_renderer_clear(g_r, ex_theme_color(&g_theme, EX_COLOR_BACKGROUND));
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    return v;
}

/* Clicks every point of a grid until something clickable reacts; returns the point count that did. */
static int click_grid(ExReadingView *v, float w, float h) {
    float x, y;
    int hits = 0;
    for (y = 4; y < h; y += 6)
        for (x = 4; x < w; x += 6)
            if (ex_readingview_mouse(v, EX_MOUSE_DOWN, x, y)) { hits++; ex_readingview_mouse(v, EX_MOUSE_UP, x, y); }
    return hits;
}

/* One click at the first clickable point of the grid; returns 1 if there was one. */
static int click_first(ExReadingView *v, float w, float h) {
    float x, y;
    for (y = 4; y < h; y += 6)
        for (x = 4; x < w; x += 6)
            if (ex_readingview_mouse(v, EX_MOUSE_DOWN, x, y)) { ex_readingview_mouse(v, EX_MOUSE_UP, x, y); return 1; }
    return 0;
}

static int write_ppm(const char *sample, const char *path) {
    size_t len = 0;
    char *text = ex_platform_read_file(sample, &len);
    ExReadingView *v;
    int w, h;
    uint8_t *px;
    FILE *f;
    if (!text) return 1;
    v = ex_readingview_create(text, len);
    ex_readingview_set_viewport(v, 0, 0, 980, 1500);
    ex_renderer_clear(g_r, ex_theme_color(&g_theme, EX_COLOR_BACKGROUND));
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    px = ex_renderer_read_rgb(g_r, &w, &h);
    f = fopen(path, "wb");
    if (f && px) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(px, 1, (size_t)w * (size_t)h * 3, f); }
    if (f) fclose(f);
    free(px);
    ex_readingview_free(v);
    free(text);
    return 0;
}

int main(int argc, char **argv) {
    size_t len = 0;
    char *text, *plain;
    ExReadingView *v;
    float h1, h2;
    if (argc < 2) { fprintf(stderr, "usage: test_reading_view SAMPLE [--ppm FILE]\n"); return 2; }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    g_r = ex_renderer_create("reading", argc > 3 ? 980 : 800, argc > 3 ? 1500 : 500);
    if (!g_r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    ex_theme_init_builtin(&g_theme, EX_THEME_DARK);
    if (argc > 3 && strcmp(argv[2], "--ppm") == 0) return write_ppm(argv[1], argv[3]);

    /* the sample: laid out, taller than the viewport, scrollable, and a screen reader gets plain text */
    text = ex_platform_read_file(argv[1], &len);
    CHECK(text != NULL);
    if (!text) return 1;
    v = ex_readingview_create(text, len);
    ex_readingview_set_viewport(v, 0, 0, 800, 500);
    ex_renderer_clear(g_r, ex_theme_color(&g_theme, EX_COLOR_BACKGROUND));
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    CHECK(ex_readingview_content_height(v) > 500.0f);
    CHECK(ex_readingview_scroll_y(v) == 0.0f);
    ex_readingview_scroll(v, 300.0f);
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    CHECK(ex_readingview_scroll_y(v) == 300.0f);
    ex_readingview_scroll(v, -10000.0f);
    CHECK(ex_readingview_scroll_y(v) == 0.0f);                          /* never above the top */
    ex_readingview_scroll(v, 1e6f);
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    CHECK(ex_readingview_scroll_y(v) <= ex_readingview_content_height(v));   /* nor far below the end */
    plain = ex_readingview_plain_text(v);
    CHECK(plain != NULL && strstr(plain, "**") == NULL);
    free(plain);
    /* a narrower viewport wraps more, so the content gets taller */
    h1 = ex_readingview_content_height(v);
    ex_readingview_set_viewport(v, 0, 0, 400, 500);
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    h2 = ex_readingview_content_height(v);
    CHECK(h2 > h1);
    ex_readingview_free(v);
    free(text);

    /* links: a click on an external or an internal link reaches the handler with the target */
    v = make("An [external link](https://example.com/x) in a line.\n", 800, 200);
    ex_readingview_set_link_handler(v, on_link, NULL);
    CHECK(click_grid(v, 800, 200) > 0);
    CHECK(g_link.calls > 0 && g_link.kind == EX_RSK_LINK_EXTERNAL && strcmp(g_link.target, "https://example.com/x") == 0);
    ex_readingview_free(v);
    memset(&g_link, 0, sizeof g_link);
    v = make("See [[Other note#Heading|shown]] now.\n", 800, 200);
    ex_readingview_set_link_handler(v, on_link, NULL);
    CHECK(click_grid(v, 800, 200) > 0);
    CHECK(g_link.calls > 0 && g_link.kind == EX_RSK_LINK_INTERNAL && strncmp(g_link.target, "Other note", 10) == 0);
    ex_readingview_free(v);
    memset(&g_link, 0, sizeof g_link);
    v = make("No links here, only text.\n", 800, 200);
    ex_readingview_set_link_handler(v, on_link, NULL);
    CHECK(click_grid(v, 800, 200) == 0 && g_link.calls == 0);
    ex_readingview_free(v);

    /* a callout marked foldable starts folded with "-" and a click on its title opens it, and again closes it */
    v = make("> [!note]- Folded title\n> The hidden body of the callout\n> over two lines.\n\nAfter.\n", 800, 400);
    h1 = ex_readingview_content_height(v);
    CHECK(click_first(v, 800, 120));
    ex_renderer_clear(g_r, ex_theme_color(&g_theme, EX_COLOR_BACKGROUND));
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    h2 = ex_readingview_content_height(v);
    CHECK(h2 > h1);                                                     /* opened: the body is there */
    CHECK(click_first(v, 800, 120));
    ex_readingview_draw(v, g_r, &g_theme, 16.0f);
    CHECK(ex_readingview_content_height(v) == h1);                      /* closed again */
    ex_readingview_free(v);

    printf("%s\n", failures ? "FAIL" : "ok");
    ex_renderer_destroy(g_r);
    return failures ? 1 : 0;
}
