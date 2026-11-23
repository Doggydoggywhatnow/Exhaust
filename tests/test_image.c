/* images (6c-i): one sample of each accepted picture type decodes to the expected size and pixels (exact for the lossless ones, close for the lossy
 * ones, and a checksum of every result so a change of a decoder shows), a JPEG is turned by its EXIF orientation, a big picture is reduced, an SVG is
 * drawn at the width asked for, and damaged or hostile input is an error and never a crash.
 * Usage: test_image <folder with the sample pictures> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_image.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 16
#define H 12
static const char *DIR;

static uint8_t *load(const char *name, size_t *len) {
    char path[600];
    uint8_t *d;
    snprintf(path, sizeof path, "%s/%s", DIR, name);
    d = (uint8_t *)ex_platform_read_file(path, len);
    if (!d) fprintf(stderr, "cannot read %s\n", path);
    return d;
}
static void src_px(int x, int y, uint8_t out[4]) { out[0] = (uint8_t)(x * 16); out[1] = (uint8_t)(y * 20); out[2] = (uint8_t)((x * y) % 256); out[3] = (x + y) % 5 ? 255 : 128; }
static unsigned long fnv(const uint8_t *p, size_t n) { unsigned long h = 2166136261UL; size_t i; for (i = 0; i < n; i++) { h ^= p[i]; h = (h * 16777619UL) & 0xffffffffUL; } return h; }

/* the mean difference of the colour channels from the pattern (alpha when asked for) */
static double mean_diff(const ExImage *im, int with_alpha) {
    double sum = 0;
    int x, y, c, n = with_alpha ? 4 : 3;
    for (y = 0; y < im->h; y++)
        for (x = 0; x < im->w; x++) {
            uint8_t e[4];
            src_px(x, y, e);
            for (c = 0; c < n; c++) { int d = (int)im->rgba[((size_t)y * (size_t)im->w + (size_t)x) * 4 + (size_t)c] - (int)e[c]; sum += d < 0 ? -d : d; }
        }
    return sum / ((double)im->w * (double)im->h * (double)n);
}

static void one(const char *file, ExImageFormat fmt, int exact, int has_alpha, double tol, unsigned long sum_expect) {
    size_t len = 0;
    uint8_t *d = load(file, &len);
    ExImage im;
    char err[200] = "";
    int w = 0, h = 0;
    if (!d) { failures++; return; }
    CHECK(ex_image_sniff(d, len) == fmt);
    CHECK(ex_image_info(d, len, &w, &h, err, sizeof err) == 0 && w == W && h == H);
    CHECK(ex_image_decode(d, len, 0, 0, &im, err, sizeof err) == 0);
    if (im.rgba) {
        double diff = mean_diff(&im, has_alpha);
        CHECK(im.w == W && im.h == H && im.src_w == W && im.src_h == H && im.format == fmt);
        if (exact) CHECK(diff == 0.0); else CHECK(diff < tol);
        CHECK(!has_alpha ? im.has_alpha == 0 : im.has_alpha == 1);
        if (sum_expect && fnv(im.rgba, (size_t)W * H * 4) != sum_expect) fprintf(stderr, "%s: checksum is %lu\n", file, fnv(im.rgba, (size_t)W * H * 4));
        if (sum_expect) CHECK(fnv(im.rgba, (size_t)W * H * 4) == sum_expect);
        if (diff >= tol && !exact) fprintf(stderr, "%s: mean difference %.2f\n", file, diff);
        ex_image_free(&im);
    }
    free(d);
}

int main(int argc, char **argv) {
    size_t len = 0;
    uint8_t *d;
    ExImage im;
    char err[200];
    int w, h, i;
    if (argc < 2) return 2;
    DIR = argv[1];

    one("pattern.png", EX_IMG_PNG, 1, 1, 0, 364987616UL);
    one("pattern.bmp", EX_IMG_BMP, 0, 0, 1.0, 3279767477UL);
    one("pattern.jpg", EX_IMG_JPEG, 0, 0, 6.0, 3449916446UL);
    one("pattern.gif", EX_IMG_GIF, 0, 0, 40.0, 1659511289UL);
    one("pattern_lossless.webp", EX_IMG_WEBP, 1, 1, 0, 364987616UL);
    one("pattern_lossy.webp", EX_IMG_WEBP, 0, 0, 8.0, 2480973880UL);

    /* a JPEG with an EXIF orientation of 6 (a quarter turn clockwise): 16 by 12 stored, 12 by 16 shown, and the pixel (x, y) of the file is at (11 - y, x) */
    d = load("rotated.jpg", &len);
    CHECK(d && ex_image_decode(d, len, 0, 0, &im, err, sizeof err) == 0);
    if (d && im.rgba) {
        double sum = 0;
        int x, y, c;
        CHECK(im.w == 12 && im.h == 16 && im.src_w == 12 && im.src_h == 16);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                uint8_t e[4];
                src_px(x, y, e);
                for (c = 0; c < 3; c++) { int dd = (int)im.rgba[((size_t)x * 12 + (size_t)(H - 1 - y)) * 4 + (size_t)c] - (int)e[c]; sum += dd < 0 ? -dd : dd; }
            }
        CHECK(sum / (W * H * 3.0) < 6.0);
        ex_image_free(&im);
    }
    free(d);

    /* a picture bigger than max_dim is reduced and keeps the size the file says */
    d = load("pattern.png", &len);
    CHECK(d && ex_image_decode(d, len, 8, 0, &im, err, sizeof err) == 0);
    if (d && im.rgba) { CHECK(im.w == 8 && im.h == 6 && im.src_w == 16 && im.src_h == 12); ex_image_free(&im); }
    free(d);

    /* an SVG: its own size, and at the width asked for */
    d = load("shapes.svg", &len);
    CHECK(d && ex_image_sniff(d, len) == EX_IMG_SVG && ex_image_info(d, len, &w, &h, err, sizeof err) == 0 && w == 40 && h == 20);
    CHECK(d && ex_image_decode(d, len, 0, 0, &im, err, sizeof err) == 0);
    if (d && im.rgba) {
        CHECK(im.w == 40 && im.h == 20 && im.format == EX_IMG_SVG);
        CHECK(im.rgba[(10 * 40 + 5) * 4] == 255 && im.rgba[(10 * 40 + 5) * 4 + 1] == 0 && im.rgba[(10 * 40 + 5) * 4 + 3] == 255);      /* the left half is red */
        CHECK(im.rgba[(10 * 40 + 35) * 4 + 2] == 255 && im.rgba[(10 * 40 + 35) * 4] == 0);                                                /* the right half is blue */
        ex_image_free(&im);
    }
    CHECK(d && ex_image_decode(d, len, 0, 160, &im, err, sizeof err) == 0);
    if (d && im.rgba) { CHECK(im.w == 160 && im.h == 80 && im.src_w == 40); ex_image_free(&im); }
    free(d);

    /* the AVIF sample: this file is the same pattern (lossy) */
    d = load("pattern.avif", &len);
    CHECK(d && ex_image_sniff(d, len) == EX_IMG_AVIF);
    if (d) {
        int rc = ex_image_decode(d, len, 0, 0, &im, err, sizeof err);
        CHECK(rc == 0);
        if (rc == 0) { CHECK(im.w == W && im.h == H); CHECK(mean_diff(&im, 0) < 12.0); ex_image_free(&im); }
        else fprintf(stderr, "avif: %s\n", err);
        free(d);
    }

    /* AVIF with transparency (a second AV1 picture that refers to the first) and the colour in full range, 4:4:4 */
    d = load("pattern_alpha.avif", &len);
    CHECK(d && ex_image_decode(d, len, 0, 0, &im, err, sizeof err) == 0);
    if (d && im.rgba) { CHECK(im.w == W && im.h == H && im.has_alpha); CHECK(mean_diff(&im, 1) < 6.0); ex_image_free(&im); }
    free(d);
    d = load("pattern_444.avif", &len);
    CHECK(d && ex_image_decode(d, len, 0, 0, &im, err, sizeof err) == 0);
    if (d && im.rgba) { CHECK(im.w == W && im.h == H && !im.has_alpha); CHECK(mean_diff(&im, 0) < 4.0); ex_image_free(&im); }
    free(d);

    /* not pictures, and damaged pictures: an error, never a crash */
    CHECK(ex_image_sniff("hello world, this is text", 25) == EX_IMG_UNKNOWN);
    CHECK(ex_image_decode("hello world, this is text", 25, 0, 0, &im, err, sizeof err) != 0 && err[0]);
    CHECK(ex_image_decode("", 0, 0, 0, &im, err, sizeof err) != 0);
    {
        static const char *names[] = { "pattern.png", "pattern.jpg", "pattern.gif", "pattern.bmp", "pattern_lossless.webp", "pattern_lossy.webp", "shapes.svg", "pattern.avif", "pattern_alpha.avif" };
        size_t k;
        for (k = 0; k < sizeof names / sizeof *names; k++) {
            size_t n = 0, cut;
            uint8_t *f = load(names[k], &n), *c;
            if (!f) { failures++; continue; }
            c = malloc(n);
            for (cut = 0; cut < n; cut += (n / 40 ? n / 40 : 1)) {                       /* every file cut short at many places */
                if (ex_image_decode(f, cut, 0, 0, &im, err, sizeof err) == 0) ex_image_free(&im);
            }
            for (i = 0; i < 300; i++) {                                                    /* and with bytes changed */
                memcpy(c, f, n);
                c[(size_t)(i * 7919) % n] ^= (uint8_t)(1 + i % 255);
                c[(size_t)(i * 104729 + 13) % n] = (uint8_t)(i * 31);
                if (ex_image_decode(c, n, 0, 0, &im, err, sizeof err) == 0) ex_image_free(&im);
            }
            free(c); free(f);
        }
    }
    {   /* a PNG that says it is 60,000 by 60,000 pixels is refused before anything is allocated */
        uint8_t big[33] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0xea, 0x60, 0, 0, 0xea, 0x60, 8, 6, 0, 0, 0, 0, 0, 0, 0 };
        CHECK(ex_image_decode(big, sizeof big, 0, 0, &im, err, sizeof err) != 0);
    }
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("images: ok\n");
    return 0;
}
