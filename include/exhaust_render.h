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
/* The same, with the window not shown yet (ex_renderer_show shows it): the screen reader interface of Windows must be attached before the window appears. */
ExRenderer *ex_renderer_create_hidden(const char *title, int width, int height);
void ex_renderer_show(ExRenderer *r);
void ex_renderer_destroy(ExRenderer *r);
const char *ex_render_error(void);

struct SDL_Window *ex_renderer_window(ExRenderer *r);
ExFontLib *ex_renderer_fonts(ExRenderer *r);
/* Gives the renderer a system font database for fallback fonts (it takes
 * ownership). Scan on a worker thread (ex_fontdb_scan) and hand the result over
 * on the UI thread. */
struct ExFontDb;
void ex_renderer_set_font_db(ExRenderer *r, struct ExFontDb *db);
/* The same database for another renderer (a pop-out window): not owned, so the owner must outlive it. */
void ex_renderer_set_font_db_shared(ExRenderer *r, struct ExFontDb *db);

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
/* ---- batched geometry (sub-slice 5a): lines and circles for the graph, and later the canvas ----
 * One ExGeom holds the triangles of a whole frame. Lines (quads) and circles (a fan; fewer segments the smaller the circle, a plain
 * square under about 1.5 px) are appended with no allocation once the buffer has grown to what a frame needs, and ex_geom_submit hands
 * all of it to the renderer in ONE call. Keep one ExGeom for the life of a view and call ex_geom_reset at the start of each frame.
 * Coordinates are window pixels. Primitives outside the clip rectangle set with ex_geom_set_cull are not added at all. A feather is a
 * ramp to transparent outside the shape, in pixels (0 = none): a cheap soft edge, since SDL draws without antialiasing. UI thread only. */
typedef struct ExGeom ExGeom;
ExGeom *ex_geom_create(void);
void ex_geom_free(ExGeom *g);
void ex_geom_reset(ExGeom *g);                                  /* empties it, keeps the memory */
void ex_geom_set_feather(ExGeom *g, float px);
void ex_geom_set_cull(ExGeom *g, float x, float y, float w, float h);   /* w < 0: no culling */
void ex_geom_line(ExGeom *g, float x1, float y1, float x2, float y2, float width, ExColor c);   /* a width under 1 px is drawn 1 px wide and fainter */
void ex_geom_circle(ExGeom *g, float cx, float cy, float radius, ExColor c);
/* An arrow head (a filled triangle): its tip at x, y, pointing along the direction dx, dy (any length but not zero), size long and 0.8 of
 * that wide at the base. Used at the end of a graph's lines. */
void ex_geom_arrow(ExGeom *g, float x, float y, float dx, float dy, float size, ExColor c);
/* Draws everything added since the last reset with one renderer call; returns the number of triangles drawn. */
size_t ex_geom_submit(ExRenderer *r, ExGeom *g);
typedef struct ExGeomStats {
    size_t vertices, indices;     /* in the buffer now */
    size_t culled;                /* primitives left out by the cull rectangle since the last reset */
    size_t capacity_vertices;     /* what it can hold without growing */
    size_t grows;                 /* times it had to grow since it was created: a steady frame adds none */
    float min_x, max_x, min_y, max_y;   /* the box round every vertex in the buffer (a scan: for tests) */
} ExGeomStats;
void ex_geom_stats(const ExGeom *g, ExGeomStats *out);

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
/* Fits text into max_w pixels: all of it when it fits, else the longest start of it (cut between grapheme clusters) followed by an
 * ellipsis (U+2026) that fits. Writes a NUL-terminated string of at most cap bytes to out and returns its length (an empty string when
 * not even the ellipsis fits). Used wherever a name may be longer than the room it has (rows of lists, the tree, popups). */
size_t ex_text_fit(ExRenderer *r, ExFont *font, float size, const char *utf8, size_t len, float max_w, char *out, size_t cap);
int ex_text_metrics(ExRenderer *r, ExFont *font, float size, ExFontMetrics *out);

void ex_atlas_stats(const ExRenderer *r, ExAtlasStats *out);

#ifdef __cplusplus
}
#endif
#endif
