/* render_geometry (sub-slice 5a): the batched line and circle drawing of the renderer. With SDL's dummy video driver and the software
 * renderer, shapes are drawn into the window and the pixels are read back; the buffer's counts say what was built and what was left
 * out, and a second frame of the same size must not allocate. Each check can fail: draw nothing, draw in the wrong place, skip the
 * cull or grow every frame, and one of them does. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_render.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 200
#define H 200

static const ExColor BLACK = {0, 0, 0, 255};
static const ExColor RED = {255, 0, 0, 255};

static const uint8_t *px(const uint8_t *img, int w, int x, int y) { return img + ((size_t)y * (size_t)w + (size_t)x) * 3; }
static int is_red(const uint8_t *p) { return p[0] > 240 && p[1] < 16 && p[2] < 16; }
static int is_black(const uint8_t *p) { return p[0] < 8 && p[1] < 8 && p[2] < 8; }

static uint8_t *frame(ExRenderer *r, ExGeom *g, int *w, int *h) {
    ex_renderer_clear(r, BLACK);
    ex_geom_submit(r, g);
    return ex_renderer_read_rgb(r, w, h);
}

static void test_shapes(ExRenderer *r) {
    ExGeom *g = ex_geom_create();
    ExGeomStats st;
    int w = 0, h = 0, x, y;
    uint8_t *img;
    long red = 0;

    CHECK(g != NULL);
    ex_geom_line(g, 20, 50, 180, 50, 4, RED);                    /* a horizontal bar, rows 48 to 51 */
    ex_geom_circle(g, 100, 130, 20, RED);
    ex_geom_stats(g, &st);
    CHECK(st.vertices == 4 + 41 && st.indices == 6 + 3 * 40);    /* a quad, and a fan of 40 segments for a big circle */
    img = frame(r, g, &w, &h);
    CHECK(img && w >= W && h >= H);
    if (!img) { ex_geom_free(g); return; }
    CHECK(is_red(px(img, w, 100, 49)) && is_red(px(img, w, 100, 50)));
    CHECK(is_black(px(img, w, 100, 44)) && is_black(px(img, w, 100, 56)));           /* above and below the bar */
    CHECK(is_black(px(img, w, 10, 50)) && is_black(px(img, w, 190, 50)));            /* before its start and past its end */
    CHECK(is_red(px(img, w, 100, 130)) && is_red(px(img, w, 115, 130)) && is_red(px(img, w, 100, 145)));
    CHECK(is_black(px(img, w, 125, 130)) && is_black(px(img, w, 100, 155)));         /* outside the circle */
    for (y = 100; y < 160; y++) for (x = 70; x < 130; x++) if (is_red(px(img, w, x, y))) red++;
    CHECK(red > 1150 && red < 1360);                                                 /* the area of a circle of radius 20: 1257 */
    free(img);

    /* Level of detail: fewer segments for a small circle, a plain square for a dot. */
    ex_geom_reset(g);
    ex_geom_circle(g, 50, 50, 1.0f, RED);
    ex_geom_stats(g, &st);
    CHECK(st.vertices == 4 && st.indices == 6);
    ex_geom_reset(g);
    ex_geom_circle(g, 50, 50, 3.0f, RED);
    ex_geom_stats(g, &st);
    CHECK(st.vertices == 7);                                     /* a hexagon and its centre */
    ex_geom_reset(g);
    ex_geom_circle(g, 50, 50, 5.0f, RED);
    ex_geom_stats(g, &st);
    CHECK(st.vertices == 13);
    ex_geom_reset(g);
    ex_geom_circle(g, 50, 50, 1.0f, RED);
    img = frame(r, g, &w, &h);
    CHECK(img && is_red(px(img, w, 50, 50)) && is_red(px(img, w, 49, 49)) && is_black(px(img, w, 55, 50)));   /* even a dot shows */
    free(img);

    /* A line thinner than a pixel is one pixel wide and fainter: never invisible, never as strong as a full one. */
    ex_geom_reset(g);
    ex_geom_line(g, 20, 70.5f, 180, 70.5f, 0.4f, RED);
    img = frame(r, g, &w, &h);
    CHECK(img && px(img, w, 100, 70)[0] > 40 && px(img, w, 100, 70)[0] < 160);
    free(img);

    /* A feather puts a soft edge outside the shape: faint pixels next to the bar, none further out. */
    ex_geom_reset(g);
    ex_geom_set_feather(g, 3.0f);
    ex_geom_line(g, 20, 50, 180, 50, 4, RED);
    img = frame(r, g, &w, &h);
    CHECK(img && is_red(px(img, w, 100, 50)) && px(img, w, 100, 52)[0] > 20 && px(img, w, 100, 52)[0] < 250 && is_black(px(img, w, 100, 58)));
    free(img);
    ex_geom_free(g);
}

static void test_cull_and_reuse(ExRenderer *r) {
    ExGeom *g = ex_geom_create();
    ExGeomStats a, b, c;
    int i, frame_no;
    size_t grows_after_first = 0;

    /* Nothing outside the cull rectangle is built. */
    ex_geom_set_cull(g, 0, 0, 100, 100);
    ex_geom_line(g, 150, 150, 190, 150, 2, RED);                 /* wholly outside */
    ex_geom_circle(g, 300, 300, 10, RED);                        /* wholly outside */
    ex_geom_line(g, 50, 50, 150, 50, 2, RED);                    /* crosses the edge: kept */
    ex_geom_circle(g, 95, 95, 10, RED);                          /* half inside: kept */
    ex_geom_stats(g, &a);
    CHECK(a.culled == 2 && a.vertices == 4 + 29);                 /* the crossing line (4) and the half-inside circle (1 + 28 segments for a radius of 10) */
    ex_geom_set_cull(g, 0, 0, -1, 0);                            /* culling off */
    ex_geom_reset(g);
    ex_geom_line(g, 150, 150, 190, 150, 2, RED);
    ex_geom_stats(g, &b);
    CHECK(b.culled == 0 && b.vertices == 4);

    /* A line that runs far outside the cull rectangle is cut at its edge: nothing is built beyond it, and what is inside still draws. */
    ex_geom_reset(g);
    ex_geom_set_cull(g, 10, 10, 100, 100);
    ex_geom_line(g, -50000, 60, 50000, 60, 2, RED);              /* horizontal, through the rectangle */
    ex_geom_line(g, -30000, -30000, 30000, 30000, 2, RED);       /* diagonal, through it */
    ex_geom_line(g, 60, -9000, 60, -8000, 2, RED);               /* wholly above it */
    ex_geom_stats(g, &a);
    CHECK(a.culled == 1 && a.vertices == 8);
    CHECK(a.min_x > 0 && a.max_x < 130 && a.min_y > 0 && a.max_y < 130);                       /* every vertex is near the rectangle, none at +-50000 */
    ex_geom_set_cull(g, 0, 0, -1, 0);
    ex_geom_reset(g);

    /* A graph's worth of shapes, frame after frame: the buffer grows during the first frame and never again. */
    for (frame_no = 0; frame_no < 4; frame_no++) {
        ex_geom_reset(g);
        for (i = 0; i < 20000; i++) ex_geom_line(g, (float)(i % 190), (float)(i % 170), (float)((i * 7) % 190), (float)((i * 3) % 170), 1.0f, RED);
        for (i = 0; i < 5000; i++) ex_geom_circle(g, (float)(i % 190), (float)((i * 5) % 190), 1.0f + (float)(i % 9), RED);
        ex_geom_stats(g, &c);
        if (frame_no == 0) grows_after_first = c.grows;
        CHECK(c.vertices > 100000);
        if (frame_no == 0) { ex_renderer_clear(r, BLACK); CHECK(ex_geom_submit(r, g) == c.indices / 3); }    /* drawing it all in one call works */
    }
    CHECK(c.grows == grows_after_first);                         /* no allocation after the first frame */
    CHECK(c.capacity_vertices >= c.vertices);
    ex_geom_free(g);
}

int main(void) {
    ExRenderer *r;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("render geometry", W, H);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    test_shapes(r);
    test_cull_and_reuse(r);
    ex_renderer_destroy(r);
    SDL_Quit();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
