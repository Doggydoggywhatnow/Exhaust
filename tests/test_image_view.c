/* image_view (6c-i): the picture cache and the image tab through the real UI. A picture is asked for once, arrives from the "service" (this test decodes
 * the sample), goes to the card a few rows a frame and is then ready; the view fits it, zooms about the pointer, pans, shows its own size and keeps
 * working when the picture is missing or damaged; an SVG is asked for at a width in steps of a power of two; a changed file is asked for again; and
 * the textures are kept under the budget by dropping the ones drawn longest ago.
 * Usage: test_image_view <folder with the sample pictures> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_imageview.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 900
#define H 600

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *VW;
static ExImageCache *CACHE;
static const char *DIR;

typedef struct Asked { int n; char path[300]; int svg_width; } Asked;
static Asked asked;
static void want(void *u, const char *path, int svg_width) { Asked *a = u; a->n++; snprintf(a->path, sizeof a->path, "%s", path); a->svg_width = svg_width; }

/* what the service thread does: reads the file and decodes it */
static void serve(const char *path, int svg_width) {
    char full[700];
    size_t len = 0;
    uint8_t *d;
    ExImage *im = calloc(1, sizeof *im);
    char err[200] = "";
    snprintf(full, sizeof full, "%s/%s", DIR, path);
    d = (uint8_t *)ex_platform_read_file(full, &len);
    if (d && ex_image_decode(d, len, 4096, svg_width, im, err, sizeof err) == 0) ex_imagecache_result(CACHE, path, svg_width, im, NULL);
    else ex_imagecache_result(CACHE, path, svg_width, NULL, d ? err : "This file cannot be read.");
    free(im); free(d);
}

static void frame(void) {
    ex_imagecache_pump(CACHE, R);
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}
static void open_view(const char *path) {
    if (VW) { ex_ui_release(UI, VW); ex_widget_remove(ROOT, VW); ex_widget_free(VW); VW = NULL; }
    VW = ex_widget_imageview(UI, CACHE, path);
    ex_widget_add(ROOT, VW);
    frame();
}
static void typed(const char *s) { VW->on_text(VW, s, strlen(s), VW->on_text_user); frame(); }

int main(int argc, char **argv) {
    ExTheme theme;
    ExImageViewStats st;
    ExImageCacheStats cs;
    ExImageRef ref;
    int i;
    if (argc < 2) return 2;
    DIR = argv[1];
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("image view", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    CACHE = ex_imagecache_create(0);
    ex_imagecache_set_loader(CACHE, want, &asked);

    /* ---- a picture is asked for once, then shown ---- */
    open_view("pattern.png");
    ex_imageview_stats(VW, &st);
    CHECK(asked.n == 1 && !strcmp(asked.path, "pattern.png") && st.state == EX_IMAGE_LOADING);
    frame(); frame();
    CHECK(asked.n == 1);                                                          /* not again while it is on its way */
    CHECK(ex_imagecache_busy(CACHE));
    serve("pattern.png", 0);
    frame(); frame();
    ex_imageview_stats(VW, &st);
    CHECK(st.state == EX_IMAGE_READY && st.w == 16 && st.h == 12 && st.has_alpha && st.fitted);
    CHECK(st.scale == 1.0f);                                                      /* a small picture is not blown up when fitted */
    CHECK(!ex_imagecache_busy(CACHE));

    /* ---- zoom about the pointer, pan, own size, fit ---- */
    ex_imageview_wheel(VW, 3.0f, VW->x + VW->w / 2.0f, VW->y + VW->h / 2.0f);
    frame();
    ex_imageview_stats(VW, &st);
    CHECK(!st.fitted && st.scale > 1.5f);
    typed("0");
    ex_imageview_stats(VW, &st);
    CHECK(st.fitted);
    typed("+"); typed("+"); typed("+");
    ex_imageview_stats(VW, &st);
    CHECK(!st.fitted && st.scale > 1.9f && st.scale < 2.1f);                      /* 1.25 to the third */
    typed("1");
    ex_imageview_stats(VW, &st);
    CHECK(st.scale > 0.99f && st.scale < 1.01f);
    {
        float before_scale = st.scale;
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, VW->x + 200.0f, VW->y + 200.0f, 0, 1);
        ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, VW->x + 230.0f, VW->y + 210.0f, 0, 1);
        ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, VW->x + 230.0f, VW->y + 210.0f, 0, 1);
        frame();
        ex_imageview_stats(VW, &st);
        CHECK(st.scale == before_scale && !st.fitted);
    }

    /* ---- a picture that is not there, and a damaged one, are messages ---- */
    open_view("missing.png");
    serve("missing.png", 0);
    frame();
    ex_imageview_stats(VW, &st);
    CHECK(st.state == EX_IMAGE_FAILED);
    ex_imagecache_result(CACHE, "broken.png", 0, NULL, "x");                           /* an answer nobody asked for is dropped quietly */
    open_view("pattern.jpg");
    {
        ExImage *bad = calloc(1, sizeof *bad);
        char err[100];
        CHECK(ex_image_decode("\xff\xd8\xff not a jpeg", 17, 0, 0, bad, err, sizeof err) != 0);
        free(bad);
        ex_imagecache_result(CACHE, "pattern.jpg", 0, NULL, err);
    }
    frame();
    ex_imageview_stats(VW, &st);
    CHECK(st.state == EX_IMAGE_FAILED);

    /* ---- an SVG is asked for at a width in steps of a power of two ---- */
    CHECK(ex_imagecache_svg_bucket(1) == 256 && ex_imagecache_svg_bucket(256) == 256 && ex_imagecache_svg_bucket(257) == 512 && ex_imagecache_svg_bucket(9999) == 4096);
    memset(&asked, 0, sizeof asked);
    open_view("shapes.svg");
    CHECK(asked.n == 1 && asked.svg_width == 1024);
    serve("shapes.svg", asked.svg_width);
    frame(); frame();
    ex_imageview_stats(VW, &st);
    CHECK(st.state == EX_IMAGE_READY && st.src_w == 40 && st.w == 1024);
    for (i = 0; i < 12; i++) ex_imageview_wheel(VW, 3.0f, VW->x + VW->w / 2.0f, VW->y + VW->h / 2.0f);     /* zoomed in: a bigger raster is asked for */
    frame(); frame();
    CHECK(asked.n >= 2 && asked.svg_width > 1024);

    /* ---- a changed file is asked for again ---- */
    memset(&asked, 0, sizeof asked);
    open_view("pattern.png");
    ex_imagecache_get(CACHE, R, "pattern.png", 0, &ref);
    CHECK(ref.state == EX_IMAGE_READY && asked.n == 0);
    ex_imagecache_invalidate(CACHE, "pattern.png");
    ex_imagecache_get(CACHE, R, "pattern.png", 0, &ref);
    CHECK(ref.state == EX_IMAGE_LOADING && asked.n == 1);
    serve("pattern.png", 0);
    ex_imagecache_invalidate(CACHE, "");
    ex_imagecache_stats(CACHE, &cs);
    CHECK(cs.entries == 0 && cs.bytes == 0);

    /* ---- a big picture goes to the card a few rows a frame ---- */
    {
        ExImage *big = calloc(1, sizeof *big);
        size_t before_rows, rows_now, frames = 0;
        big->w = big->h = big->src_w = big->src_h = 3000;
        big->rgba = calloc((size_t)3000 * 3000, 4);
        big->format = EX_IMG_PNG;
        ex_imagecache_get(CACHE, R, "big.png", 0, &ref);
        ex_imagecache_result(CACHE, "big.png", 0, big, NULL);
        free(big);
        ex_imagecache_stats(CACHE, &cs);
        before_rows = cs.uploads_rows;
        while (ex_imagecache_busy(CACHE) && frames < 100) {
            ex_imagecache_pump(CACHE, R);
            ex_imagecache_stats(CACHE, &cs);
            rows_now = cs.uploads_rows;
            CHECK(rows_now - before_rows <= 400);                                  /* about a million pixels a frame */
            before_rows = rows_now;
            frames++;
        }
        CHECK(frames >= 8 && frames <= 20 && !ex_imagecache_busy(CACHE));
        ex_imagecache_get(CACHE, R, "big.png", 0, &ref);
        CHECK(ref.state == EX_IMAGE_READY && ref.w == 3000);
    }

    /* ---- the textures are kept under the budget: the one drawn longest ago goes first ---- */
    {
        ExImageCache *small = ex_imagecache_create(2 * 100 * 100 * 4 + 10);     /* room for two 100 by 100 pictures */
        const char *names[3] = { "a", "b", "c" };
        ExImageCacheStats s2;
        ex_imagecache_set_loader(small, want, &asked);
        for (i = 0; i < 3; i++) {
            ExImage *im = calloc(1, sizeof *im);
            im->w = im->h = im->src_w = im->src_h = 100;
            im->rgba = calloc(100 * 100, 4);
            ex_imagecache_get(small, R, names[i], 0, &ref);
            ex_imagecache_result(small, names[i], 0, im, NULL);
            free(im);
            ex_imagecache_pump(small, R);
            ex_imagecache_pump(small, R);
        }
        ex_imagecache_stats(small, &s2);
        CHECK(s2.entries == 2 && s2.evictions == 1 && s2.bytes <= 2 * 100 * 100 * 4 + 10);
        memset(&asked, 0, sizeof asked);
        ex_imagecache_get(small, R, "a", 0, &ref);                                 /* the oldest, "a", is the one that went */
        CHECK(ref.state == EX_IMAGE_LOADING && asked.n == 1);
        ex_imagecache_free(small);
    }

    ex_ui_release(UI, VW);
    ex_widget_remove(ROOT, VW);
    ex_widget_free(VW);
    ex_imagecache_free(CACHE);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("image_view: ok\n");
    return 0;
}
