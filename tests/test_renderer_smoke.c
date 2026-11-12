/* renderer_smoke: with SDL's dummy video driver and software renderer, render
 * glyphs from the atlas at 1x and 2x scale and compare them with stored images
 * within a tolerance. Run with UPDATE_GOLDEN=1 to rewrite the stored images
 * (after reviewing the change). Usage: test_renderer_smoke <golden-dir> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_render.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* Test colors (the tests are not the product; the product reads theme tokens). */
static const ExColor BG = {0x1e, 0x1f, 0x2d, 0xff};
static const ExColor FG = {0xca, 0xd1, 0xf2, 0xff};
static const ExColor ACCENT = {0x81, 0x67, 0xeb, 0xff};
static const ExColor FAINT = {0x7f, 0x83, 0x9b, 0xff};

#define W 256
#define H 96

static void draw_scene(ExRenderer *r) {
    ExFontLib *lib = ex_renderer_fonts(r);
    ExFont *ui = ex_font_builtin(lib, EX_FONT_UI_REGULAR);
    ExFont *bold = ex_font_builtin(lib, EX_FONT_UI_BOLD);
    ExFont *italic = ex_font_builtin(lib, EX_FONT_UI_ITALIC);
    ExFont *mono = ex_font_builtin(lib, EX_FONT_MONO_REGULAR);
    float s = ex_renderer_scale(r);
    const char *hello = "Exhaust: notes, links & graphs";
    const char *accents = "H\xC3\xA9llo w\xC3\xB6rld \xC2\xA1\xC3\xA7\xC4\x85! 0123456789";
    const char *code = "int main(void) { return 0; }";

    ex_renderer_clear(r, BG);
    ex_fill_rect(r, 8 * s, 4 * s, 240 * s, 2 * s, ACCENT);
    ex_text_draw(r, ui, 16, 8 * s, 10 * s, hello, strlen(hello), FG);
    ex_text_draw(r, bold, 14, 8 * s, 32 * s, accents, strlen(accents), FG);
    ex_text_draw(r, italic, 12, 8 * s, 52 * s, hello, strlen(hello), FAINT);
    ex_text_draw(r, mono, 12, 8 * s, 70 * s, code, strlen(code), ACCENT);
}

static uint8_t *read_ppm(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    int maxv = 0;
    uint8_t *px;
    size_t n;
    if (!f) return NULL;
    if (fscanf(f, "P6 %d %d %d", w, h, &maxv) != 3 || maxv != 255 || *w <= 0 || *h <= 0 || *w > 8192 || *h > 8192) {
        fclose(f);
        return NULL;
    }
    fgetc(f); /* the single whitespace after the header */
    n = (size_t)*w * (size_t)*h * 3;
    px = malloc(n);
    if (px && fread(px, 1, n, f) != n) { free(px); px = NULL; }
    fclose(f);
    return px;
}

static int write_ppm(const char *path, const uint8_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(px, 1, (size_t)w * (size_t)h * 3, f);
    return fclose(f);
}

/* Tolerance: the mean absolute error per channel must be small and only a tiny
 * share of pixels may differ strongly. This allows for rasterizer or blending
 * rounding differences between platforms, not for different glyphs. */
static int matches(const uint8_t *a, const uint8_t *b, int w, int h, const char *name) {
    size_t n = (size_t)w * (size_t)h, i, strong = 0;
    double sum = 0;
    for (i = 0; i < n * 3; i++) {
        int d = abs((int)a[i] - (int)b[i]);
        sum += d;
        if (d > 48) strong++;
    }
    {
        double mean = sum / (double)(n * 3);
        double strong_share = (double)strong / (double)(n * 3);
        fprintf(stderr, "%s: mean abs error %.4f, strong pixels %.4f%%\n", name, mean, strong_share * 100.0);
        return mean <= 0.75 && strong_share <= 0.01;
    }
}

static long ink(const uint8_t *px, int w, int h) {
    long count = 0;
    size_t i, n = (size_t)w * (size_t)h;
    for (i = 0; i < n; i++)
        if (px[i * 3] != BG.r || px[i * 3 + 1] != BG.g || px[i * 3 + 2] != BG.b) count++;
    return count;
}

static uint8_t *render_at(float scale, int *w, int *h, float *hello_width) {
    ExRenderer *r;
    uint8_t *px;
    int ow = 0, oh = 0;
    r = ex_renderer_create("smoke", (int)(W * scale), (int)(H * scale));
    if (!r) {
        fprintf(stderr, "renderer: %s\n", ex_render_error());
        failures++;
        return NULL;
    }
    ex_renderer_set_display_scale(r, scale);
    ex_renderer_size(r, &ow, &oh);
    CHECK(ow == (int)(W * scale) && oh == (int)(H * scale));
    CHECK(fabsf(ex_renderer_scale(r) - scale) < 1e-4f);
    draw_scene(r);
    *hello_width = ex_text_measure(r, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16, "Exhaust: notes", 14);
    px = ex_renderer_read_rgb(r, w, h);
    ex_renderer_present(r);
    ex_renderer_destroy(r);
    return px;
}

static void compare_golden(const char *dir, const char *file, const uint8_t *px, int w, int h) {
    char path[1024];
    int gw = 0, gh = 0;
    uint8_t *golden;
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (getenv("UPDATE_GOLDEN")) {
        CHECK(write_ppm(path, px, w, h) == 0);
        fprintf(stderr, "wrote %s\n", path);
        return;
    }
    golden = read_ppm(path, &gw, &gh);
    if (!golden) { fprintf(stderr, "cannot read %s\n", path); failures++; return; }
    CHECK(gw == w && gh == h);
    if (gw == w && gh == h) CHECK(matches(px, golden, w, h, file));
    free(golden);
}

static void test_atlas_and_scale(void) {
    ExRenderer *r = ex_renderer_create("atlas", 200, 80);
    ExAtlasStats a, b, c;
    ExFont *ui;
    ExFontMetrics m1, m2;
    const char *t = "Atlas test";
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR);
    CHECK(ui && strcmp(ex_font_family(ui), "Atkinson Hyperlegible") == 0);
    CHECK(strcmp(ex_font_family(ex_font_builtin(ex_renderer_fonts(r), EX_FONT_MONO_REGULAR)),
                 "Atkinson Hyperlegible Mono") == 0);
    CHECK(ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR) == ui); /* shared */
    CHECK(ex_font_glyph_index(ui, 'A') != 0);
    CHECK(ex_font_glyph_index(ui, 0x4E2D) == 0); /* no CJK in the built-in font: fallback is 1a-ii */

    ex_text_draw(r, ui, 16, 4, 4, t, strlen(t), FG);
    ex_atlas_stats(r, &a);
    CHECK(a.pages == 1 && a.glyphs_cached > 5 && a.rasterized_total == a.glyphs_cached);
    ex_text_draw(r, ui, 16, 4, 30, t, strlen(t), FG);
    ex_atlas_stats(r, &b);
    CHECK(b.rasterized_total == a.rasterized_total); /* second draw hits the cache */

    /* A zoom change re-rasterizes at the new pixel size instead of stretching. */
    ex_text_metrics(r, ui, 16, &m1);
    ex_renderer_set_zoom(r, 2.0f);
    ex_text_metrics(r, ui, 16, &m2);
    CHECK(m2.line_height >= 2 * m1.line_height - 2 && m2.line_height <= 2 * m1.line_height + 2);
    CHECK(ex_px(r, 16) == 32);
    ex_text_draw(r, ui, 16, 4, 4, t, strlen(t), FG);
    ex_atlas_stats(r, &c);
    CHECK(c.resets == b.resets + 1 && c.glyphs_cached == a.glyphs_cached);
    ex_renderer_set_zoom(r, 100.0f);
    CHECK(fabsf(ex_renderer_scale(r) - 4.0f) < 1e-4f); /* clamped */
    ex_renderer_set_zoom(r, 0.0f);
    CHECK(fabsf(ex_renderer_scale(r) - 0.5f) < 1e-4f);

    /* Empty, space-only, control-character and malformed input draw safely. */
    ex_text_draw(r, ui, 16, 0, 0, "", 0, FG);
    ex_text_draw(r, ui, 16, 0, 0, "   \t\n", 5, FG);
    ex_text_draw(r, ui, 16, 0, 0, "\xFF\xFE\xC3", 3, FG);
    CHECK(ex_text_draw(r, NULL, 16, 0, 0, "x", 1, FG) == 0.0f);
    ex_renderer_destroy(r);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "tests/golden";
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    float m1 = 0, m2 = 0;
    uint8_t *one, *two;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    one = render_at(1.0f, &w1, &h1, &m1);
    two = render_at(2.0f, &w2, &h2, &m2);
    if (one && two) {
        long ink1 = ink(one, w1, h1), ink2 = ink(two, w2, h2);
        fprintf(stderr, "ink pixels: 1x=%ld 2x=%ld, width 1x=%.2f 2x=%.2f\n", ink1, ink2, m1, m2);
        CHECK(ink1 > 500);
        CHECK(ink2 > ink1 * 3 && ink2 < ink1 * 5);
        CHECK(m1 > 50 && fabsf(m2 / m1 - 2.0f) < 0.1f);
        compare_golden(dir, "smoke_1x.ppm", one, w1, h1);
        compare_golden(dir, "smoke_2x.ppm", two, w2, h2);
    }
    free(one);
    free(two);
    test_atlas_and_scale();
    SDL_Quit();
    return failures ? 1 : 0;
}
