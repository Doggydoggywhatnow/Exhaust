#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "atlas.h"
#include "exhaust_render.h"
#include "exhaust_fontdb.h"
#include "exhaust_shape.h"

#define BATCH_GLYPHS 128

struct ExRenderer {
    SDL_Window *window;
    SDL_Renderer *renderer;
    ExAtlas *atlas;
    ExFontLib *fonts;
    float display_scale, zoom;
};

static char g_error[256] = "";

static void set_error(const char *what) {
    snprintf(g_error, sizeof g_error, "%s: %s", what, SDL_GetError());
}

const char *ex_render_error(void) { return g_error; }

ExRenderer *ex_renderer_create(const char *title, int width, int height) {
    ExRenderer *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->display_scale = 1.0f;
    r->zoom = 1.0f;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { set_error("SDL_InitSubSystem"); free(r); return NULL; }
    if (!SDL_CreateWindowAndRenderer(title, width, height,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &r->window, &r->renderer)) {
        set_error("SDL_CreateWindowAndRenderer");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        free(r);
        return NULL;
    }
    SDL_SetRenderVSync(r->renderer, 1);
    SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_BLEND);
    r->atlas = ex_atlas_create(r->renderer);
    r->fonts = ex_fontlib_create();
    if (!r->atlas || !r->fonts) {
        snprintf(g_error, sizeof g_error, "out of memory or FreeType failure");
        ex_renderer_destroy(r);
        return NULL;
    }
    return r;
}

void ex_renderer_destroy(ExRenderer *r) {
    if (!r) return;
    ex_atlas_destroy(r->atlas);
    ex_fontlib_destroy(r->fonts);
    if (r->renderer) SDL_DestroyRenderer(r->renderer);
    if (r->window) SDL_DestroyWindow(r->window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    free(r);
}

SDL_Window *ex_renderer_window(ExRenderer *r) { return r->window; }
ExFontLib *ex_renderer_fonts(ExRenderer *r) { return r->fonts; }

void ex_renderer_set_font_db(ExRenderer *r, ExFontDb *db) { ex_fontlib_set_db(r->fonts, db, 1); }

void ex_renderer_size(ExRenderer *r, int *w, int *h) {
    int ww = 0, hh = 0;
    SDL_GetCurrentRenderOutputSize(r->renderer, &ww, &hh);
    if (w) *w = ww;
    if (h) *h = hh;
}

float ex_renderer_detect_display_scale(ExRenderer *r) {
    float s = SDL_GetWindowDisplayScale(r->window);
    return s > 0.0f ? s : 1.0f;
}

float ex_renderer_scale(const ExRenderer *r) { return r->display_scale * r->zoom; }

/* Changing the scale invalidates every cached glyph pixel size. */
static void scale_changed(ExRenderer *r, float old_scale) {
    if (fabsf(ex_renderer_scale(r) - old_scale) > 1e-4f) ex_atlas_clear(r->atlas);
}

void ex_renderer_set_display_scale(ExRenderer *r, float scale) {
    float old = ex_renderer_scale(r);
    if (!(scale >= 0.25f)) scale = 0.25f;
    if (scale > 8.0f) scale = 8.0f;
    r->display_scale = scale;
    scale_changed(r, old);
}

void ex_renderer_set_zoom(ExRenderer *r, float zoom) {
    float old = ex_renderer_scale(r);
    if (!(zoom >= 0.5f)) zoom = 0.5f;
    if (zoom > 4.0f) zoom = 4.0f;
    r->zoom = zoom;
    scale_changed(r, old);
}

int ex_px(const ExRenderer *r, float size_at_100) {
    int v = (int)lroundf(size_at_100 * ex_renderer_scale(r));
    return (v < 1 && size_at_100 > 0.0f) ? 1 : v;
}

void ex_renderer_clear(ExRenderer *r, ExColor c) {
    SDL_SetRenderDrawColor(r->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderClear(r->renderer);
}

void ex_fill_rect(ExRenderer *r, float x, float y, float w, float h, ExColor c) {
    SDL_FRect rect;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_SetRenderDrawColor(r->renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r->renderer, &rect);
}

void ex_renderer_present(ExRenderer *r) {
    SDL_RenderPresent(r->renderer);
    /* Between frames no glyph pointer is held, so the atlas may be emptied. */
    if (ex_atlas_wants_reset(r->atlas)) ex_atlas_clear(r->atlas);
}

uint8_t *ex_renderer_read_rgb(ExRenderer *r, int *w, int *h) {
    SDL_Surface *s = SDL_RenderReadPixels(r->renderer, NULL), *c;
    uint8_t *out;
    int y;
    if (!s) return NULL;
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(s);
    if (!c) return NULL;
    out = malloc((size_t)c->w * (size_t)c->h * 3);
    if (out) {
        for (y = 0; y < c->h; y++)
            memcpy(out + (size_t)y * (size_t)c->w * 3, (const uint8_t *)c->pixels + (size_t)y * (size_t)c->pitch,
                   (size_t)c->w * 3);
        *w = c->w;
        *h = c->h;
    }
    SDL_DestroySurface(c);
    return out;
}

/* ---- text ---- */

typedef struct Batch {
    float xy[BATCH_GLYPHS * 4 * 2];
    float uv[BATCH_GLYPHS * 4 * 2];
    SDL_FColor col[BATCH_GLYPHS * 4];
    int idx[BATCH_GLYPHS * 6];
    int glyphs;
    int page;
} Batch;

static void flush(ExRenderer *r, Batch *b) {
    if (b->glyphs > 0 && b->page >= 0) {
        int tw = 0, th = 0;
        SDL_Texture *tex = ex_atlas_texture(r->atlas, b->page, &tw, &th);
        if (tex)
            SDL_RenderGeometryRaw(r->renderer, tex, b->xy, 2 * (int)sizeof(float), b->col,
                                  (int)sizeof(SDL_FColor), b->uv, 2 * (int)sizeof(float), b->glyphs * 4, b->idx,
                                  b->glyphs * 6, (int)sizeof(int));
    }
    b->glyphs = 0;
}

static void emit(ExRenderer *r, Batch *b, const ExAtlasGlyph *g, float x, float y, SDL_FColor col) {
    int tw = 0, th = 0, v, k;
    float u0, v0, u1, v1;
    if (g->page < 0) return;
    if (b->glyphs > 0 && b->page != g->page) flush(r, b);
    if (b->glyphs == BATCH_GLYPHS) flush(r, b);
    b->page = g->page;
    ex_atlas_texture(r->atlas, g->page, &tw, &th); /* page size for texture coordinates */
    u0 = (float)g->x / (float)tw;
    v0 = (float)g->y / (float)th;
    u1 = (float)(g->x + g->w) / (float)tw;
    v1 = (float)(g->y + g->h) / (float)th;
    v = b->glyphs * 4;
    b->xy[v * 2 + 0] = x;            b->xy[v * 2 + 1] = y;
    b->xy[v * 2 + 2] = x + g->w;     b->xy[v * 2 + 3] = y;
    b->xy[v * 2 + 4] = x + g->w;     b->xy[v * 2 + 5] = y + g->h;
    b->xy[v * 2 + 6] = x;            b->xy[v * 2 + 7] = y + g->h;
    b->uv[v * 2 + 0] = u0; b->uv[v * 2 + 1] = v0;
    b->uv[v * 2 + 2] = u1; b->uv[v * 2 + 3] = v0;
    b->uv[v * 2 + 4] = u1; b->uv[v * 2 + 5] = v1;
    b->uv[v * 2 + 6] = u0; b->uv[v * 2 + 7] = v1;
    for (k = 0; k < 4; k++) b->col[v + k] = col;
    k = b->glyphs * 6;
    b->idx[k + 0] = v; b->idx[k + 1] = v + 1; b->idx[k + 2] = v + 2;
    b->idx[k + 3] = v; b->idx[k + 4] = v + 2; b->idx[k + 5] = v + 3;
    b->glyphs++;
}

static float layout(ExRenderer *r, ExFont *font, float size, float x, float y, const char *s, size_t len,
                    ExColor color, int draw) {
    Batch *b = NULL;
    SDL_FColor col, white;
    ExFontMetrics m;
    ExShaped sh;
    int px = ex_px(r, size);
    float pen = 0.0f, baseline;
    size_t i;

    if (!font || !s || ex_font_metrics(font, px, &m)) return 0.0f;
    if (ex_shape(r->fonts, font, px, s, len, &sh) < 0) return 0.0f;
    if (draw) {
        b = malloc(sizeof *b);
        if (!b) { ex_shaped_free(&sh); return 0.0f; }
        b->glyphs = 0;
        b->page = -1;
        col.r = color.r / 255.0f; col.g = color.g / 255.0f; col.b = color.b / 255.0f; col.a = color.a / 255.0f;
        white.r = white.g = white.b = 1.0f;
        white.a = col.a;
    }
    baseline = floorf(y + 0.5f) + (float)m.ascent;
    for (i = 0; draw && i < sh.nglyphs; i++) {
        const ExGlyph *gl = &sh.glyphs[i];
        const ExAtlasGlyph *pg = ex_atlas_glyph(r->atlas, gl->font, gl->glyph, px);
        if (pg) {
            ExAtlasGlyph g = *pg; /* copy: the table may move on the next lookup */
            emit(r, b, &g, floorf(x + pen + gl->x_offset + 0.5f) + (float)g.left,
                 baseline - (float)g.top - floorf(gl->y_offset + 0.5f), g.color ? white : col);
        }
        pen += gl->advance;
    }
    if (!draw) pen = sh.width;
    if (draw) { flush(r, b); free(b); }
    ex_shaped_free(&sh);
    return pen;
}

float ex_text_draw(ExRenderer *r, ExFont *font, float size, float x, float y, const char *utf8, size_t len,
                   ExColor color) {
    return layout(r, font, size, x, y, utf8, len, color, 1);
}

float ex_text_measure(ExRenderer *r, ExFont *font, float size, const char *utf8, size_t len) {
    ExColor none = {0, 0, 0, 0};
    return layout(r, font, size, 0, 0, utf8, len, none, 0);
}

int ex_text_metrics(ExRenderer *r, ExFont *font, float size, ExFontMetrics *out) {
    return ex_font_metrics(font, ex_px(r, size), out);
}

void ex_atlas_stats(const ExRenderer *r, ExAtlasStats *out) { ex_atlas_get_stats(r->atlas, out); }
