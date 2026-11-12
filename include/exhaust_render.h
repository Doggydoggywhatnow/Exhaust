/* Exhaust renderer: a window, an SDL_Renderer and a FreeType glyph atlas.
 *
 * Units. Everything is drawn in physical pixels of the renderer output.
 * Sizes that come from theme tokens or settings are given at 100% zoom and are
 * converted with ex_px(); ex_text_* take the font size at 100% zoom and scale
 * it themselves. The scale is the display scale (HiDPI) times the user's
 * interface zoom. When the scale changes the glyph atlas is rebuilt at the new
 * pixel size, so text is re-rasterized and stays sharp, never stretched.
 *
 * No colors are defined here: callers pass them (from theme tokens). UI thread
 * only. */
#ifndef EXHAUST_RENDER_H
#define EXHAUST_RENDER_H

#include <stddef.h>
#include <stdint.h>
#include "exhaust_text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExColor { uint8_t r, g, b, a; } ExColor;

typedef struct ExRenderer ExRenderer;
struct SDL_Window;

typedef struct ExAtlasStats {
    int pages;
    int glyphs_cached;
    int rasterized_total; /* glyphs rasterized since creation */
    int resets;           /* times the atlas was emptied */
} ExAtlasStats;

/* Creates a resizable, high-pixel-density window with a renderer. Honors the
 * SDL hints SDL_HINT_VIDEO_DRIVER and SDL_HINT_RENDER_DRIVER (tests use the
 * dummy video driver and the software renderer). Returns NULL on failure; the
 * reason is in ex_render_error(). */
ExRenderer *ex_renderer_create(const char *title, int width, int height);
void ex_renderer_destroy(ExRenderer *r);
const char *ex_render_error(void);

struct SDL_Window *ex_renderer_window(ExRenderer *r);
ExFontLib *ex_renderer_fonts(ExRenderer *r);
/* Gives the renderer a system font database for fallback fonts (it takes
 * ownership). Scan on a worker thread (ex_fontdb_scan) and hand the result over
 * on the UI thread. */
struct ExFontDb;
void ex_renderer_set_font_db(ExRenderer *r, struct ExFontDb *db);

/* Output size in pixels. */
void ex_renderer_size(ExRenderer *r, int *w, int *h);

/* Scale. display_scale is the OS content scale for the window (1.0, 1.5, 2.0);
 * zoom is the user's interface zoom (1.0 = 100%, clamped to 0.5..4.0). */
float ex_renderer_detect_display_scale(ExRenderer *r); /* asks the window */
void ex_renderer_set_display_scale(ExRenderer *r, float scale);
void ex_renderer_set_zoom(ExRenderer *r, float zoom);
float ex_renderer_scale(const ExRenderer *r); /* display_scale * zoom */
int ex_px(const ExRenderer *r, float size_at_100); /* rounded, at least 1 if size > 0 */

void ex_renderer_clear(ExRenderer *r, ExColor c);
void ex_fill_rect(ExRenderer *r, float x, float y, float w, float h, ExColor c);
void ex_renderer_present(ExRenderer *r);
/* Rounded rectangle, filled or as an outline of the given thickness drawn inside
 * its bounds. radius is clamped to half the shorter side. */
void ex_fill_round_rect(ExRenderer *r, float x, float y, float w, float h, float radius, ExColor c);
void ex_stroke_round_rect(ExRenderer *r, float x, float y, float w, float h, float radius, float thickness, ExColor c);
void ex_fill_triangle(ExRenderer *r, float x1, float y1, float x2, float y2, float x3, float y3, ExColor c);
/* Clips drawing to a rectangle until the next call; w < 0 removes the clip. */
void ex_set_clip(ExRenderer *r, int x, int y, int w, int h);

/* Copies the current output (call after drawing, before present) into RGB24,
 * 3 bytes per pixel, rows top to bottom. Returns a malloc'ed buffer or NULL. */
uint8_t *ex_renderer_read_rgb(ExRenderer *r, int *w, int *h);

/* Text. size is the font size at 100% zoom; (x, y) is the top-left of the line
 * box in pixels; the baseline is y + ascent. Text is laid out left to right
 * with the font's own kerning; shaping, bidi and fallback come in 1a-ii.
 * Returns the advance width in pixels. */
float ex_text_draw(ExRenderer *r, ExFont *font, float size, float x, float y,
                   const char *utf8, size_t len, ExColor color);
float ex_text_measure(ExRenderer *r, ExFont *font, float size, const char *utf8, size_t len);
int ex_text_metrics(ExRenderer *r, ExFont *font, float size, ExFontMetrics *out);

void ex_atlas_stats(const ExRenderer *r, ExAtlasStats *out);

#ifdef __cplusplus
}
#endif
#endif
