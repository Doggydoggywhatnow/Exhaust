/* Images (sub-slice 6c-i): decoding the image types Exhaust opens (PNG, JPEG, GIF, BMP, WebP, AVIF, SVG) into 8-bit RGBA pixels.
 *
 * Nothing here touches the screen or a file: the caller reads the bytes (on a worker) and decodes there, and the UI thread only uploads the finished
 * pixels as a texture (AGENTS.md: the UI thread never decodes). A decode never trusts its input: an image over 16384 pixels on a side, or over 100
 * million pixels, is refused before its pixels are allocated, and what is damaged is an error message, not a crash (the target `fuzz_image`).
 *
 * Pixels are straight (not premultiplied) alpha. A picture bigger than `max_dim` on its long side is reduced to fit it (a box filter), so a texture
 * is never bigger than the video card allows; `src_w` and `src_h` keep the size the file says. JPEG pictures are turned by their EXIF orientation.
 * A GIF gives its first frame (animation is not built). An SVG is drawn by nanosvg at `svg_width` pixels wide (0: its own size, at most 4096 pixels
 * on a side): it has no CSS, text or filters. */
#ifndef EXHAUST_IMAGE_H
#define EXHAUST_IMAGE_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExImageFormat { EX_IMG_UNKNOWN = 0, EX_IMG_PNG, EX_IMG_JPEG, EX_IMG_GIF, EX_IMG_BMP, EX_IMG_WEBP, EX_IMG_AVIF, EX_IMG_SVG } ExImageFormat;

typedef struct ExImage {
    int w, h;                 /* the pixels' size */
    int src_w, src_h;         /* the size the file says (the same unless the picture was reduced) */
    uint8_t *rgba;            /* w * h * 4, malloc'ed */
    int has_alpha;            /* some pixel is not opaque */
    ExImageFormat format;
} ExImage;

/* The format from the first bytes (and, for the text format SVG, from the name's extension or an `<svg` near the start). */
ExImageFormat ex_image_sniff(const void *data, size_t len);
const char *ex_image_format_name(ExImageFormat f);
/* The size without decoding the pixels (SVG: its own size, rounded). 0, or -1 with the reason in err. */
int ex_image_info(const void *data, size_t len, int *w, int *h, char *err, size_t errcap);
/* Decodes. max_dim 0 means 4096; svg_width 0 means the SVG's own size. 0, or -1 with the reason in err (out is not touched). */
int ex_image_decode(const void *data, size_t len, int max_dim, int svg_width, ExImage *out, char *err, size_t errcap);
void ex_image_free(ExImage *img);

#define EX_IMAGE_MAX_SIDE 16384
#define EX_IMAGE_MAX_PIXELS 100000000

#ifdef __cplusplus
}
#endif
#endif
