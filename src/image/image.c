/* Image decoding: see include/exhaust_image.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include "webp/decode.h"

#include "exhaust_image.h"
#include "image_internal.h"

static void seterr(char *err, size_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }

const char *ex_image_format_name(ExImageFormat f) {
    static const char *n[] = { "unknown", "PNG", "JPEG", "GIF", "BMP", "WebP", "AVIF", "SVG" };
    return (unsigned)f < sizeof n / sizeof *n ? n[f] : n[0];
}

static int has_svg(const uint8_t *p, size_t len) {
    size_t i, n = len < 2048 ? len : 2048;
    for (i = 0; i + 4 <= n; i++) if (p[i] == '<' && (p[i + 1] == 's' || p[i + 1] == 'S') && (p[i + 2] == 'v' || p[i + 2] == 'V') && (p[i + 3] == 'g' || p[i + 3] == 'G')) return 1;
    return 0;
}

ExImageFormat ex_image_sniff(const void *data, size_t len) {
    const uint8_t *p = data;
    if (len >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8)) return EX_IMG_PNG;
    if (len >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return EX_IMG_JPEG;
    if (len >= 6 && (!memcmp(p, "GIF87a", 6) || !memcmp(p, "GIF89a", 6))) return EX_IMG_GIF;
    if (len >= 26 && p[0] == 'B' && p[1] == 'M') return EX_IMG_BMP;
    if (len >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WEBP", 4)) return EX_IMG_WEBP;
    if (len >= 12 && !memcmp(p + 4, "ftyp", 4) && (!memcmp(p + 8, "avif", 4) || !memcmp(p + 8, "avis", 4) || !memcmp(p + 8, "mif1", 4))) return EX_IMG_AVIF;
    if (len >= 5 && has_svg(p, len)) return EX_IMG_SVG;
    return EX_IMG_UNKNOWN;
}

static int side_ok(long long w, long long h, char *err, size_t cap) {
    if (w <= 0 || h <= 0) { seterr(err, cap, "the picture has no size"); return 0; }
    if (w > EX_IMAGE_MAX_SIDE || h > EX_IMAGE_MAX_SIDE || w * h > EX_IMAGE_MAX_PIXELS) { seterr(err, cap, "the picture is too big (over 16384 pixels on a side or 100 million pixels)"); return 0; }
    return 1;
}

/* ---- SVG ---- */

static NSVGimage *svg_parse(const void *data, size_t len, char *err, size_t cap) {
    char *copy;
    NSVGimage *im;
    if (len > 32u * 1024u * 1024u) { seterr(err, cap, "the SVG file is too big"); return NULL; }
    copy = malloc(len + 1);
    if (!copy) { seterr(err, cap, "out of memory"); return NULL; }
    memcpy(copy, data, len); copy[len] = 0;
    im = nsvgParse(copy, "px", 96.0f);                                  /* nanosvg edits the text */
    free(copy);
    if (!im || im->width <= 0.0f || im->height <= 0.0f || !(im->width < 1e7f) || !(im->height < 1e7f)) { if (im) nsvgDelete(im); seterr(err, cap, "this SVG has no size or cannot be read"); return NULL; }
    return im;
}

static int svg_decode(const void *data, size_t len, int svg_width, int max_dim, ExImage *out, char *err, size_t cap) {
    NSVGimage *im = svg_parse(data, len, err, cap);
    NSVGrasterizer *rast;
    float scale;
    int w, h;
    uint8_t *px;
    if (!im) return -1;
    scale = svg_width > 0 ? (float)svg_width / im->width : 1.0f;
    if (im->width * scale > 4096.0f) scale = 4096.0f / im->width;
    if (im->height * scale > 4096.0f) scale = 4096.0f / im->height;
    if (im->width * scale > (float)max_dim) scale = (float)max_dim / im->width;
    if (im->height * scale > (float)max_dim) scale = (float)max_dim / im->height;
    w = (int)ceilf(im->width * scale); h = (int)ceilf(im->height * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    rast = nsvgCreateRasterizer();
    px = calloc((size_t)w * (size_t)h, 4);
    if (!rast || !px) { free(px); if (rast) nsvgDeleteRasterizer(rast); nsvgDelete(im); seterr(err, cap, "out of memory"); return -1; }
    nsvgRasterize(rast, im, 0, 0, scale, px, w, h, w * 4);
    out->w = w; out->h = h; out->src_w = (int)ceilf(im->width); out->src_h = (int)ceilf(im->height);
    out->rgba = px; out->format = EX_IMG_SVG;
    nsvgDeleteRasterizer(rast); nsvgDelete(im);
    return 0;
}

/* ---- JPEG orientation (EXIF) ---- */

static int jpeg_orientation(const uint8_t *p, size_t len) {
    size_t i = 2;
    while (i + 4 <= len) {
        size_t seg;
        if (p[i] != 0xFF) return 1;
        if (p[i + 1] == 0xFF) { i++; continue; }
        if (p[i + 1] == 0xD8 || (p[i + 1] >= 0xD0 && p[i + 1] <= 0xD7) || p[i + 1] == 0x01) { i += 2; continue; }
        if (p[i + 1] == 0xDA || p[i + 1] == 0xD9) return 1;                         /* the picture's data starts: no EXIF before it */
        seg = ((size_t)p[i + 2] << 8) | p[i + 3];
        if (seg < 2 || i + 2 + seg > len) return 1;
        if (p[i + 1] == 0xE1 && seg >= 16 && !memcmp(p + i + 4, "Exif\0\0", 6)) {
            const uint8_t *t = p + i + 10;
            size_t tl = seg - 8, off, n, k;
            int le;
#define R16(o) (le ? (unsigned)(t[o] | (t[(o) + 1] << 8)) : (unsigned)((t[o] << 8) | t[(o) + 1]))
#define R32(o) (le ? ((unsigned)t[o] | ((unsigned)t[(o) + 1] << 8) | ((unsigned)t[(o) + 2] << 16) | ((unsigned)t[(o) + 3] << 24)) : (((unsigned)t[o] << 24) | ((unsigned)t[(o) + 1] << 16) | ((unsigned)t[(o) + 2] << 8) | (unsigned)t[(o) + 3]))
            if (tl < 8) return 1;
            if (t[0] == 'I' && t[1] == 'I') le = 1; else if (t[0] == 'M' && t[1] == 'M') le = 0; else return 1;
            off = R32(4);
            if (off + 2 > tl) return 1;
            n = R16(off);
            for (k = 0; k < n && off + 2 + (k + 1) * 12 <= tl; k++) {
                size_t e = off + 2 + k * 12;
                if (R16(e) == 0x0112) { unsigned v = R16(e + 8); return v >= 1 && v <= 8 ? (int)v : 1; }
            }
            return 1;
#undef R16
#undef R32
        }
        i += 2 + seg;
    }
    return 1;
}

/* Turns RGBA pixels by an EXIF orientation (2 mirror, 3 half turn, 4 flip, 5 transpose, 6 a quarter clockwise, 7 transverse, 8 a quarter counter-clockwise). */
static int orient(uint8_t **px, int *w, int *h, int o) {
    int W = *w, H = *h, nw = (o >= 5) ? H : W, nh = (o >= 5) ? W : H, x, y;
    uint8_t *src = *px, *dst;
    if (o <= 1 || o > 8) return 0;
    dst = malloc((size_t)nw * (size_t)nh * 4);
    if (!dst) return -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int dx, dy;
            switch (o) {
            case 2: dx = W - 1 - x; dy = y; break;
            case 3: dx = W - 1 - x; dy = H - 1 - y; break;
            case 4: dx = x; dy = H - 1 - y; break;
            case 5: dx = y; dy = x; break;
            case 6: dx = H - 1 - y; dy = x; break;
            case 7: dx = H - 1 - y; dy = W - 1 - x; break;
            default: dx = y; dy = W - 1 - x; break;
            }
            memcpy(dst + ((size_t)dy * (size_t)nw + (size_t)dx) * 4, src + ((size_t)y * (size_t)W + (size_t)x) * 4, 4);
        }
    free(src);
    *px = dst; *w = nw; *h = nh;
    return 0;
}

/* ---- reducing ---- */

static int reduce(uint8_t **px, int *w, int *h, int max_dim) {
    int W = *w, H = *h, nw, nh, x, y;
    uint8_t *src = *px, *dst;
    if (W <= max_dim && H <= max_dim) return 0;
    if (W >= H) { nw = max_dim; nh = (int)((long long)H * max_dim / W); } else { nh = max_dim; nw = (int)((long long)W * max_dim / H); }
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    dst = malloc((size_t)nw * (size_t)nh * 4);
    if (!dst) return -1;
    for (y = 0; y < nh; y++) {
        int y0 = (int)((long long)y * H / nh), y1 = (int)((long long)(y + 1) * H / nh);
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < nw; x++) {
            int x0 = (int)((long long)x * W / nw), x1 = (int)((long long)(x + 1) * W / nw), xx, yy;
            unsigned long long acc[4] = { 0, 0, 0, 0 }, wa = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            for (yy = y0; yy < y1; yy++)
                for (xx = x0; xx < x1; xx++) {
                    const uint8_t *s = src + ((size_t)yy * (size_t)W + (size_t)xx) * 4;
                    acc[0] += (unsigned)s[0] * s[3]; acc[1] += (unsigned)s[1] * s[3]; acc[2] += (unsigned)s[2] * s[3];     /* colors weigh by their alpha */
                    wa += s[3]; acc[3] += s[3]; n++;
                }
            {
                uint8_t *d = dst + ((size_t)y * (size_t)nw + (size_t)x) * 4;
                if (wa) { d[0] = (uint8_t)(acc[0] / wa); d[1] = (uint8_t)(acc[1] / wa); d[2] = (uint8_t)(acc[2] / wa); } else d[0] = d[1] = d[2] = 0;
                d[3] = (uint8_t)(acc[3] / n);
            }
        }
    }
    free(src);
    *px = dst; *w = nw; *h = nh;
    return 0;
}

static int any_alpha(const uint8_t *px, size_t n) { size_t i; for (i = 0; i < n; i++) if (px[i * 4 + 3] != 255) return 1; return 0; }

/* ---- the entry points ---- */

int ex_image_info(const void *data, size_t len, int *w, int *h, char *err, size_t errcap) {
    ExImageFormat f = ex_image_sniff(data, len);
    int x = 0, y = 0, comp;
    if (f == EX_IMG_UNKNOWN) { seterr(err, errcap, "this is not a picture Exhaust can open"); return -1; }
    if (len > 0x7fffffffu) { seterr(err, errcap, "the file is too big"); return -1; }
    if (f == EX_IMG_SVG) {
        NSVGimage *im = svg_parse(data, len, err, errcap);
        if (!im) return -1;
        *w = (int)ceilf(im->width); *h = (int)ceilf(im->height);
        nsvgDelete(im);
        return 0;
    }
    if (f == EX_IMG_WEBP) {
        if (!WebPGetInfo((const uint8_t *)data, len, &x, &y)) { seterr(err, errcap, "this WebP picture is damaged"); return -1; }
    } else if (f == EX_IMG_AVIF) {
        if (ex_avif_info(data, len, &x, &y, err, errcap) != 0) return -1;
    } else if (!stbi_info_from_memory((const stbi_uc *)data, (int)len, &x, &y, &comp)) { seterr(err, errcap, stbi_failure_reason() ? stbi_failure_reason() : "this picture is damaged"); return -1; }
    if (!side_ok(x, y, err, errcap)) return -1;
    *w = x; *h = y;
    return 0;
}

int ex_image_decode(const void *data, size_t len, int max_dim, int svg_width, ExImage *out, char *err, size_t errcap) {
    ExImageFormat f = ex_image_sniff(data, len);
    ExImage r;
    int x = 0, y = 0, comp;
    uint8_t *px = NULL;
    if (max_dim <= 0) max_dim = 4096;
    if (max_dim > EX_IMAGE_MAX_SIDE) max_dim = EX_IMAGE_MAX_SIDE;
    if (f == EX_IMG_UNKNOWN) { seterr(err, errcap, "this is not a picture Exhaust can open"); return -1; }
    if (len > 0x7fffffffu) { seterr(err, errcap, "the file is too big"); return -1; }
    memset(&r, 0, sizeof r);
    if (f == EX_IMG_SVG) {
        if (svg_decode(data, len, svg_width, max_dim, &r, err, errcap) != 0) return -1;
        r.has_alpha = 1;
        *out = r;
        return 0;
    }
    if (f == EX_IMG_WEBP) {
        if (!WebPGetInfo((const uint8_t *)data, len, &x, &y)) { seterr(err, errcap, "this WebP picture is damaged"); return -1; }
        if (!side_ok(x, y, err, errcap)) return -1;
        px = WebPDecodeRGBA((const uint8_t *)data, len, &x, &y);                               /* libwebp's allocator is malloc */
        if (!px) { seterr(err, errcap, "this WebP picture is damaged or uses a feature that is not built (animation)"); return -1; }
    } else if (f == EX_IMG_AVIF) {
        if (ex_avif_decode(data, len, &px, &x, &y, err, errcap) != 0) return -1;
    } else {
        if (!stbi_info_from_memory((const stbi_uc *)data, (int)len, &x, &y, &comp)) { seterr(err, errcap, stbi_failure_reason() ? stbi_failure_reason() : "this picture is damaged"); return -1; }
        if (!side_ok(x, y, err, errcap)) return -1;
        px = stbi_load_from_memory((const stbi_uc *)data, (int)len, &x, &y, &comp, 4);
        if (!px) { seterr(err, errcap, stbi_failure_reason() ? stbi_failure_reason() : "this picture is damaged"); return -1; }
    }
    r.src_w = x; r.src_h = y; r.format = f;
    if (f == EX_IMG_JPEG && orient(&px, &x, &y, jpeg_orientation(data, len)) != 0) { free(px); seterr(err, errcap, "out of memory"); return -1; }
    if (f == EX_IMG_JPEG) { r.src_w = x; r.src_h = y; }
    if (reduce(&px, &x, &y, max_dim) != 0) { free(px); seterr(err, errcap, "out of memory"); return -1; }
    r.w = x; r.h = y; r.rgba = px;
    r.has_alpha = any_alpha(px, (size_t)x * (size_t)y);
    *out = r;
    return 0;
}

void ex_image_free(ExImage *img) { if (img) { free(img->rgba); memset(img, 0, sizeof *img); } }
