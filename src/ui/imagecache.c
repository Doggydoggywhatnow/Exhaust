/* The picture cache: see include/exhaust_imagecache.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_imagecache.h"

#define PIXELS_PER_FRAME 1000000

typedef struct Entry {
    char *path;
    int svg_width;                 /* the bucket, 0 for the rest */
    ExImageState state;
    char err[160];
    ExTexture *tex;
    ExImage img;                   /* the pixels, until they are all on the card */
    int rows_done;
    int w, h, src_w, src_h, has_alpha;
    unsigned long used;            /* the frame it was last asked for */
} Entry;

struct ExImageCache {
    Entry *e; size_t n, cap;
    size_t budget, bytes;
    void (*want)(void *user, const char *path, int svg_width);
    void *user;
    unsigned long frame;
    ExRenderer *owner;             /* the window the textures belong to (a texture is drawn only by the renderer that made it) */
    size_t requests, rows, evictions;
    unsigned long version;
};

static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

ExImageCache *ex_imagecache_create(size_t budget_bytes) {
    ExImageCache *c = calloc(1, sizeof *c);
    if (c) c->budget = budget_bytes ? budget_bytes : (size_t)128 << 20;
    return c;
}

static void entry_free(ExImageCache *c, Entry *e) {
    if (e->tex) { c->bytes -= (size_t)e->w * (size_t)e->h * 4; ex_texture_free(e->tex); }
    ex_image_free(&e->img);
    free(e->path);
    memset(e, 0, sizeof *e);
}

void ex_imagecache_free(ExImageCache *c) {
    size_t i;
    if (!c) return;
    for (i = 0; i < c->n; i++) entry_free(c, &c->e[i]);
    free(c->e);
    free(c);
}

void ex_imagecache_set_loader(ExImageCache *c, void (*want)(void *user, const char *path, int svg_width), void *user) { if (c) { c->want = want; c->user = user; } }

unsigned long ex_imagecache_version(const ExImageCache *c) { return c ? c->version : 0; }

int ex_imagecache_svg_bucket(int px) { int b = 256; while (b < px && b < 4096) b *= 2; return b; }

static int is_svg_path(const char *path) {
    size_t n = strlen(path);
    return n > 4 && (path[n - 4] == '.') && (path[n - 3] == 's' || path[n - 3] == 'S') && (path[n - 2] == 'v' || path[n - 2] == 'V') && (path[n - 1] == 'g' || path[n - 1] == 'G');
}

static Entry *find(ExImageCache *c, const char *path, int svg_width) {
    size_t i;
    for (i = 0; i < c->n; i++) if (c->e[i].path && c->e[i].svg_width == svg_width && !strcmp(c->e[i].path, path)) return &c->e[i];
    return NULL;
}

static void evict(ExImageCache *c) {
    while (c->bytes > c->budget) {
        size_t i, best = (size_t)-1;
        for (i = 0; i < c->n; i++) if (c->e[i].path && c->e[i].tex && c->e[i].used != c->frame && (best == (size_t)-1 || c->e[i].used < c->e[best].used)) best = i;
        if (best == (size_t)-1) return;                                     /* everything is in use this frame */
        entry_free(c, &c->e[best]);
        c->evictions++;
    }
}

void ex_imagecache_get(ExImageCache *c, ExRenderer *r, const char *path, int svg_width, ExImageRef *out) {
    Entry *e;
    int key = 0;
    memset(out, 0, sizeof *out);
    out->state = EX_IMAGE_LOADING;
    if (c && r && c->owner && r != c->owner) { out->state = EX_IMAGE_FAILED; out->err = "Pictures are shown in the main window only."; return; }
    if (!c || !path || !*path) { out->state = EX_IMAGE_FAILED; out->err = "no picture"; return; }
    if (is_svg_path(path)) key = ex_imagecache_svg_bucket(svg_width > 0 ? svg_width : 256);
    e = find(c, path, key);
    if (!e) {
        size_t i;
        for (i = 0; i < c->n && c->e[i].path; i++) {}
        if (i == c->n) {
            if (c->n == c->cap) { size_t nc = c->cap ? c->cap * 2 : 16; Entry *ne = realloc(c->e, nc * sizeof *ne); if (!ne) { out->state = EX_IMAGE_FAILED; out->err = "out of memory"; return; } memset(ne + c->cap, 0, (nc - c->cap) * sizeof *ne); c->e = ne; c->cap = nc; }
            c->n++;
        }
        e = &c->e[i];
        memset(e, 0, sizeof *e);
        e->path = dups(path); e->svg_width = key; e->state = EX_IMAGE_LOADING;
        if (!e->path) { out->state = EX_IMAGE_FAILED; out->err = "out of memory"; return; }
        c->requests++;
        if (c->want) c->want(c->user, path, key);
    }
    e->used = c->frame;
    out->state = e->state;
    if (e->state == EX_IMAGE_FAILED) { out->err = e->err; return; }
    if (e->state == EX_IMAGE_READY) {                                   /* ready means every row is on the card */
        out->tex = e->tex; out->w = e->w; out->h = e->h; out->src_w = e->src_w; out->src_h = e->src_h; out->has_alpha = e->has_alpha;
    }
}

void ex_imagecache_result(ExImageCache *c, const char *path, int svg_width, ExImage *img, const char *err) {
    Entry *e;
    if (!c || !path) { if (img) ex_image_free(img); return; }
    e = find(c, path, svg_width);
    if (!e) { if (img) ex_image_free(img); return; }                       /* it was dropped while the service worked */
    ex_image_free(&e->img);
    if (e->tex) { c->bytes -= (size_t)e->w * (size_t)e->h * 4; ex_texture_free(e->tex); e->tex = NULL; }
    if (!img || !img->rgba) { c->version++; e->state = EX_IMAGE_FAILED; snprintf(e->err, sizeof e->err, "%s", err && *err ? err : "this picture cannot be opened"); return; }
    e->img = *img;                                                       /* the cache owns the pixels now */
    memset(img, 0, sizeof *img);
    e->w = e->img.w; e->h = e->img.h; e->src_w = e->img.src_w; e->src_h = e->img.src_h; e->has_alpha = e->img.has_alpha;
    e->rows_done = 0;
    c->version++;
    e->state = EX_IMAGE_LOADING;                                         /* ready when the first rows are on the card (pump) */
}

void ex_imagecache_invalidate(ExImageCache *c, const char *path) {
    size_t i;
    if (!c) return;
    for (i = 0; i < c->n; i++) if (c->e[i].path && (!path || !*path || !strcmp(c->e[i].path, path))) entry_free(c, &c->e[i]);
}

void ex_imagecache_pump(ExImageCache *c, ExRenderer *r) {
    size_t i;
    int budget = PIXELS_PER_FRAME;
    if (!c || !r) return;
    c->owner = r;
    c->frame++;
    for (i = 0; i < c->n && budget > 0; i++) {
        Entry *e = &c->e[i];
        int rows;
        if (!e->path || !e->img.rgba) continue;
        if (!e->tex) {
            if (e->w > ex_texture_max_size(r) || e->h > ex_texture_max_size(r)) { ex_image_free(&e->img); e->state = EX_IMAGE_FAILED; snprintf(e->err, sizeof e->err, "this picture is bigger than the video card allows"); continue; }
            e->tex = ex_texture_create(r, e->w, e->h);
            if (!e->tex) { ex_image_free(&e->img); e->state = EX_IMAGE_FAILED; snprintf(e->err, sizeof e->err, "the video card cannot hold this picture"); continue; }
            c->bytes += (size_t)e->w * (size_t)e->h * 4;
        }
        rows = budget / (e->w > 0 ? e->w : 1);
        if (rows < 1) rows = 1;
        if (e->rows_done + rows > e->h) rows = e->h - e->rows_done;
        if (rows > 0 && ex_texture_update_rows(e->tex, e->img.rgba, e->rows_done, rows) == 0) { e->rows_done += rows; budget -= rows * e->w; c->rows += (size_t)rows; }
        else if (rows > 0) { e->rows_done = e->h; }
        if (e->rows_done >= e->h) { ex_image_free(&e->img); e->img.rgba = NULL; e->state = EX_IMAGE_READY; }
    }
    evict(c);
}

int ex_imagecache_busy(const ExImageCache *c) {
    size_t i;
    if (!c) return 0;
    for (i = 0; i < c->n; i++) if (c->e[i].path && (c->e[i].state == EX_IMAGE_LOADING || c->e[i].img.rgba)) return 1;
    return 0;
}

void ex_imagecache_stats(const ExImageCache *c, ExImageCacheStats *out) {
    size_t i;
    memset(out, 0, sizeof *out);
    if (!c) return;
    for (i = 0; i < c->n; i++) if (c->e[i].path) out->entries++;
    out->bytes = c->bytes; out->requests = c->requests; out->uploads_rows = c->rows; out->evictions = c->evictions;
}
