/* The pictures on screen (6c-i): a cache of textures, one for the window, shared by everything that draws a picture (the image tab, canvas image cards,
 * and the embeds of 6c-v). Nothing here reads a file or decodes: the application's `want` callback asks the service thread to do both, and the answer
 * (decoded pixels, or an error) is handed to ex_imagecache_result. The pixels go to the video card a few rows a frame, so a big picture never holds a
 * frame back. Entries are found by path (and, for an SVG, by the width it is drawn at, in steps of a power of two); the least recently drawn are dropped
 * when the textures are over the budget. */
#ifndef EXHAUST_IMAGECACHE_H
#define EXHAUST_IMAGECACHE_H
#include "exhaust_image.h"
#include "exhaust_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExImageCache ExImageCache;
typedef enum ExImageState { EX_IMAGE_LOADING = 0, EX_IMAGE_READY = 1, EX_IMAGE_FAILED = -1 } ExImageState;

typedef struct ExImageRef {
    ExImageState state;
    ExTexture *tex;           /* valid while the entry lives (until the next call that may evict: use it in the same frame) */
    int w, h;                 /* the texture's size */
    int src_w, src_h;         /* the size the file says */
    int has_alpha;
    const char *err;          /* when failed */
} ExImageRef;

/* budget_bytes: the limit for the textures (4 bytes a pixel); 0 means 128 MB. */
ExImageCache *ex_imagecache_create(size_t budget_bytes);
void ex_imagecache_free(ExImageCache *c);
void ex_imagecache_set_loader(ExImageCache *c, void (*want)(void *user, const char *path, int svg_width), void *user);

/* The state of a picture; the first call for a path asks for it. svg_width is the width in pixels it is drawn at (ignored for other formats; 0: its own size). */
void ex_imagecache_get(ExImageCache *c, ExRenderer *r, const char *path, int svg_width, ExImageRef *out);
/* The service thread's answer (the cache takes the pixels; img NULL: err says why). svg_width as it was asked. */
void ex_imagecache_result(ExImageCache *c, const char *path, int svg_width, ExImage *img, const char *err);
/* The file changed or went (an empty path: everything): the next get asks again. */
void ex_imagecache_invalidate(ExImageCache *c, const char *path);
/* Grows whenever a picture arrived, failed or was dropped: a layout that depends on picture sizes lays out again. */
unsigned long ex_imagecache_version(const ExImageCache *c);
/* Sends the next rows of pictures that are being uploaded to the card (about a million pixels a frame); the application calls it once a frame. */
void ex_imagecache_pump(ExImageCache *c, ExRenderer *r);
/* True while some picture is loading or uploading (the main loop keeps drawing). */
int ex_imagecache_busy(const ExImageCache *c);

typedef struct ExImageCacheStats { size_t entries, bytes, requests, uploads_rows, evictions; } ExImageCacheStats;
void ex_imagecache_stats(const ExImageCache *c, ExImageCacheStats *out);

/* The width bucket an SVG is asked for: the smallest of 256, 512 ... 4096 that is at least `px`. */
int ex_imagecache_svg_bucket(int px);

#ifdef __cplusplus
}
#endif
#endif
