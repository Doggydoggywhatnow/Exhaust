/* Glyph atlas: rasterized glyphs packed into RGBA textures (white, with the
 * glyph coverage in alpha) so any text color is a vertex color. Pages are
 * 1024x1024 (or the renderer's maximum texture size if smaller) packed in
 * shelves with a 1 pixel transparent border. Keyed by (font, glyph, pixel
 * size), so a different scale makes new entries and the old ones are dropped by
 * ex_atlas_clear(). */
#ifndef EXHAUST_ATLAS_H
#define EXHAUST_ATLAS_H

#include <SDL3/SDL.h>
#include "exhaust_render.h"

typedef struct ExAtlasGlyph {
    int page;          /* -1: no pixels (space) */
    int x, y, w, h;    /* in the page texture */
    int color;         /* 1: the pixels are RGBA (emoji) and not tinted */
    int left, top;     /* bearing, pixels */
    float advance;     /* pixels */
} ExAtlasGlyph;

typedef struct ExAtlas ExAtlas;

ExAtlas *ex_atlas_create(SDL_Renderer *renderer);
void ex_atlas_destroy(ExAtlas *a);
/* Drops every glyph and page (call between frames). */
void ex_atlas_clear(ExAtlas *a);
/* Looks up or rasterizes a glyph. Returns NULL if it cannot be rasterized.
 * The pointer is valid until the atlas is cleared or destroyed. */
const ExAtlasGlyph *ex_atlas_glyph(ExAtlas *a, ExFont *font, uint32_t glyph, int px);
/* Uploads any pixels added since the last call and returns the page texture. */
SDL_Texture *ex_atlas_texture(ExAtlas *a, int page, int *tex_w, int *tex_h);
/* True when too many pages are in use; the renderer clears at the next frame. */
int ex_atlas_wants_reset(const ExAtlas *a);
void ex_atlas_get_stats(const ExAtlas *a, ExAtlasStats *out);

#endif
