#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "atlas.h"
#include "exhaust_render.h"
#include "exhaust_fontdb.h"
#include "exhaust_shape.h"
#include "exhaust_text.h"

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

/* ---- rounded rectangles ---- */

#define RR_MAX_POINTS 128

/* Outline of a rounded rectangle, clockwise from the top-left corner's end. */
static int rr_points(float x, float y, float w, float h, float rad, SDL_FPoint *pts) {
    int seg = (int)(rad / 2.0f), n = 0, corner, k;
    float cx[4], cy[4];
    static const float start[4] = {180.0f, 270.0f, 0.0f, 90.0f};
    if (seg < 3) seg = 3;
    if (seg > 24) seg = 24;
    if (rad <= 0.0f) {
        pts[0].x = x; pts[0].y = y; pts[1].x = x + w; pts[1].y = y;
        pts[2].x = x + w; pts[2].y = y + h; pts[3].x = x; pts[3].y = y + h;
        return 4;
    }
    cx[0] = x + rad;     cy[0] = y + rad;
    cx[1] = x + w - rad; cy[1] = y + rad;
    cx[2] = x + w - rad; cy[2] = y + h - rad;
    cx[3] = x + rad;     cy[3] = y + h - rad;
    for (corner = 0; corner < 4; corner++)
        for (k = 0; k <= seg; k++) {
            float a = (start[corner] + 90.0f * (float)k / (float)seg) * 3.14159265f / 180.0f;
            pts[n].x = cx[corner] + cosf(a) * rad;
            pts[n].y = cy[corner] + sinf(a) * rad;
            n++;
        }
    return n;
}

static SDL_FColor fcolor(ExColor c) {
    SDL_FColor f;
    f.r = c.r / 255.0f; f.g = c.g / 255.0f; f.b = c.b / 255.0f; f.a = c.a / 255.0f;
    return f;
}

void ex_fill_round_rect(ExRenderer *r, float x, float y, float w, float h, float rad, ExColor c) {
    SDL_FPoint pts[RR_MAX_POINTS];
    SDL_Vertex v[RR_MAX_POINTS + 1];
    int idx[RR_MAX_POINTS * 3], n, i;
    float lim = (w < h ? w : h) / 2.0f;
    if (w <= 0 || h <= 0) return;
    if (rad > lim) rad = lim;
    if (rad < 1.0f) { ex_fill_rect(r, x, y, w, h, c); return; }
    n = rr_points(x, y, w, h, rad, pts);
    v[0].position.x = x + w / 2.0f; v[0].position.y = y + h / 2.0f; v[0].color = fcolor(c);
    v[0].tex_coord.x = v[0].tex_coord.y = 0;
    for (i = 0; i < n; i++) {
        v[i + 1].position = pts[i]; v[i + 1].color = v[0].color; v[i + 1].tex_coord = v[0].tex_coord;
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % n + 1;
    }
    SDL_RenderGeometry(r->renderer, NULL, v, n + 1, idx, n * 3);
}

void ex_stroke_round_rect(ExRenderer *r, float x, float y, float w, float h, float rad, float t, ExColor c) {
    SDL_FPoint outer[RR_MAX_POINTS], inner[RR_MAX_POINTS];
    SDL_Vertex v[RR_MAX_POINTS * 2];
    int idx[RR_MAX_POINTS * 6], n, n2, i;
    float lim = (w < h ? w : h) / 2.0f;
    if (w <= 0 || h <= 0 || t <= 0) return;
    if (t > lim) t = lim;
    if (rad > lim) rad = lim;
    n = rr_points(x, y, w, h, rad, outer);
    n2 = rr_points(x + t, y + t, w - 2 * t, h - 2 * t, rad > t ? rad - t : 0.0f, inner);
    if (n != n2) { /* the inner corner became square: pad by repeating its corner points */
        float cx[4] = {x + t, x + w - t, x + w - t, x + t}, cy[4] = {y + t, y + t, y + h - t, y + h - t};
        int per = n / 4, corner, k;
        n2 = 0;
        for (corner = 0; corner < 4; corner++)
            for (k = 0; k < per; k++) { inner[n2].x = cx[corner]; inner[n2].y = cy[corner]; n2++; }
    }
    for (i = 0; i < n; i++) {
        v[i].position = outer[i]; v[i].color = fcolor(c); v[i].tex_coord.x = v[i].tex_coord.y = 0;
        v[n + i].position = inner[i]; v[n + i].color = v[i].color; v[n + i].tex_coord = v[i].tex_coord;
        idx[i * 6 + 0] = i; idx[i * 6 + 1] = (i + 1) % n; idx[i * 6 + 2] = n + i;
        idx[i * 6 + 3] = (i + 1) % n; idx[i * 6 + 4] = n + (i + 1) % n; idx[i * 6 + 5] = n + i;
    }
    SDL_RenderGeometry(r->renderer, NULL, v, n * 2, idx, n * 6);
}

void ex_fill_triangle(ExRenderer *r, float x1, float y1, float x2, float y2, float x3, float y3, ExColor c) {
    SDL_Vertex v[3];
    int idx[3] = {0, 1, 2};
    v[0].position.x = x1; v[0].position.y = y1;
    v[1].position.x = x2; v[1].position.y = y2;
    v[2].position.x = x3; v[2].position.y = y3;
    v[0].color = v[1].color = v[2].color = fcolor(c);
    v[0].tex_coord.x = v[0].tex_coord.y = v[1].tex_coord.x = v[1].tex_coord.y = v[2].tex_coord.x = v[2].tex_coord.y = 0;
    SDL_RenderGeometry(r->renderer, NULL, v, 3, idx, 3);
}

/* ---- batched geometry ---- */

struct ExGeom {
    SDL_Vertex *v;
    int *idx;
    size_t nv, ni, capv, capi, culled, grows;
    float feather;
    int cull;
    float cx, cy, cw, ch;
};

ExGeom *ex_geom_create(void) { return calloc(1, sizeof(ExGeom)); }

void ex_geom_free(ExGeom *g) {
    if (!g) return;
    free(g->v);
    free(g->idx);
    free(g);
}

void ex_geom_reset(ExGeom *g) { if (g) { g->nv = g->ni = 0; g->culled = 0; } }
void ex_geom_set_feather(ExGeom *g, float px) { if (g) g->feather = px > 0.0f ? px : 0.0f; }
void ex_geom_set_cull(ExGeom *g, float x, float y, float w, float h) {
    if (!g) return;
    g->cull = w >= 0.0f;
    g->cx = x; g->cy = y; g->cw = w; g->ch = h;
}

void ex_geom_stats(const ExGeom *g, ExGeomStats *out) {
    memset(out, 0, sizeof *out);
    if (!g) return;
    out->vertices = g->nv; out->indices = g->ni; out->culled = g->culled; out->capacity_vertices = g->capv; out->grows = g->grows;
    if (g->nv) {
        size_t i;
        out->min_x = out->max_x = g->v[0].position.x; out->min_y = out->max_y = g->v[0].position.y;
        for (i = 1; i < g->nv; i++) {
            float x = g->v[i].position.x, y = g->v[i].position.y;
            if (x < out->min_x) out->min_x = x;
            if (x > out->max_x) out->max_x = x;
            if (y < out->min_y) out->min_y = y;
            if (y > out->max_y) out->max_y = y;
        }
    }
}

/* Room for nv more vertices and ni more indices; 0 on success. Grows by doubling, so a frame the same size as the last one never grows. */
static int geom_reserve(ExGeom *g, size_t nv, size_t ni) {
    if (g->nv + nv > g->capv) {
        size_t cap = g->capv ? g->capv : 1024;
        SDL_Vertex *p;
        while (cap < g->nv + nv) cap *= 2;
        p = realloc(g->v, cap * sizeof *p);
        if (!p) return -1;
        g->v = p; g->capv = cap; g->grows++;
    }
    if (g->ni + ni > g->capi) {
        size_t cap = g->capi ? g->capi : 3072;
        int *p;
        while (cap < g->ni + ni) cap *= 2;
        p = realloc(g->idx, cap * sizeof *p);
        if (!p) return -1;
        g->idx = p; g->capi = cap; g->grows++;
    }
    return 0;
}

static int geom_vertex(ExGeom *g, float x, float y, SDL_FColor c) {
    SDL_Vertex *v = &g->v[g->nv];
    v->position.x = x; v->position.y = y; v->color = c; v->tex_coord.x = v->tex_coord.y = 0.0f;
    return (int)g->nv++;
}

static void geom_tri(ExGeom *g, int a, int b, int c) { g->idx[g->ni++] = a; g->idx[g->ni++] = b; g->idx[g->ni++] = c; }

static int geom_outside(ExGeom *g, float x0, float y0, float x1, float y1) {
    if (!g->cull) return 0;
    if (x1 < g->cx || x0 > g->cx + g->cw || y1 < g->cy || y0 > g->cy + g->ch) { g->culled++; return 1; }
    return 0;
}

void ex_geom_line(ExGeom *g, float x1, float y1, float x2, float y2, float width, ExColor c) {
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy), nx, ny, f, half;
    SDL_FColor col, clear;
    int l1, l2, r1, r2;
    if (!g || len < 0.001f || width <= 0.0f) return;
    if (width < 1.0f) { c.a = (uint8_t)((float)c.a * width + 0.5f); width = 1.0f; }       /* a hairline: one pixel, as faint as it is thin */
    f = g->feather;
    half = width / 2.0f + f;
    if (geom_outside(g, (x1 < x2 ? x1 : x2) - half, (y1 < y2 ? y1 : y2) - half, (x1 > x2 ? x1 : x2) + half, (y1 > y2 ? y1 : y2) + half)) return;
    if (g->cull) {                                               /* only the part inside the cull rectangle is built: a line that runs far off the screen is cut at its edge */
        float t0 = 0.0f, t1 = 1.0f, p[4], q[4];
        int k;
        p[0] = -dx; q[0] = x1 - (g->cx - half);
        p[1] = dx;  q[1] = (g->cx + g->cw + half) - x1;
        p[2] = -dy; q[2] = y1 - (g->cy - half);
        p[3] = dy;  q[3] = (g->cy + g->ch + half) - y1;
        for (k = 0; k < 4; k++) {
            if (p[k] == 0.0f) { if (q[k] < 0.0f) { g->culled++; return; } continue; }
            if (p[k] < 0.0f) { float t = q[k] / p[k]; if (t > t0) t0 = t; }
            else { float t = q[k] / p[k]; if (t < t1) t1 = t; }
        }
        if (t0 > t1) { g->culled++; return; }
        if (t0 > 0.0f || t1 < 1.0f) {
            float nx1 = x1 + t0 * dx, ny1 = y1 + t0 * dy, nx2 = x1 + t1 * dx, ny2 = y1 + t1 * dy;
            x1 = nx1; y1 = ny1; x2 = nx2; y2 = ny2;
            dx = x2 - x1; dy = y2 - y1; len = sqrtf(dx * dx + dy * dy);
            if (len < 0.001f) return;
        }
    }
    if (geom_reserve(g, 8, 18) != 0) return;
    col = fcolor(c);
    clear = col; clear.a = 0.0f;
    nx = -dy / len * width / 2.0f; ny = dx / len * width / 2.0f;
    l1 = geom_vertex(g, x1 + nx, y1 + ny, col); l2 = geom_vertex(g, x2 + nx, y2 + ny, col);
    r1 = geom_vertex(g, x1 - nx, y1 - ny, col); r2 = geom_vertex(g, x2 - nx, y2 - ny, col);
    geom_tri(g, l1, l2, r2); geom_tri(g, l1, r2, r1);
    if (f > 0.0f) {
        float fx = -dy / len * (width / 2.0f + f), fy = dx / len * (width / 2.0f + f);
        int ol1 = geom_vertex(g, x1 + fx, y1 + fy, clear), ol2 = geom_vertex(g, x2 + fx, y2 + fy, clear);
        int or1 = geom_vertex(g, x1 - fx, y1 - fy, clear), or2 = geom_vertex(g, x2 - fx, y2 - fy, clear);
        geom_tri(g, ol1, ol2, l2); geom_tri(g, ol1, l2, l1);
        geom_tri(g, r1, r2, or2); geom_tri(g, r1, or2, or1);
    }
}

/* Segments of a circle: a few for a dot, more as it grows, so every size looks round and a small one costs little. */
static int circle_segments(float r) {
    if (r < 3.5f) return 6;                                      /* a hexagon is a circle at this size */
    if (r < 6.0f) return 12;
    if (r < 8.0f) return 20;
    if (r < 16.0f) return 28;
    return 40;
}

void ex_geom_circle(ExGeom *g, float cx, float cy, float rad, ExColor c) {
    SDL_FColor col, clear;
    float f;
    int seg, k, center, first_in, first_out;
    if (!g || rad <= 0.0f) return;
    f = g->feather;
    if (geom_outside(g, cx - rad - f, cy - rad - f, cx + rad + f, cy + rad + f)) return;
    col = fcolor(c);
    if (rad < 2.0f) {                                            /* a dot this small is a square of at least one pixel */
        float h = rad < 0.5f ? 0.5f : rad;
        int a, b, d, e;
        if (geom_reserve(g, 4, 6) != 0) return;
        a = geom_vertex(g, cx - h, cy - h, col); b = geom_vertex(g, cx + h, cy - h, col);
        d = geom_vertex(g, cx + h, cy + h, col); e = geom_vertex(g, cx - h, cy + h, col);
        geom_tri(g, a, b, d); geom_tri(g, a, d, e);
        return;
    }
    seg = circle_segments(rad);
    if (f > 0.0f && rad >= 3.5f) {                               /* a soft edge only where there is room for one: it triples the triangles */
        clear = col; clear.a = 0.0f;
        if (geom_reserve(g, (size_t)(2 * seg + 1), (size_t)(9 * seg)) != 0) return;
        center = geom_vertex(g, cx, cy, col);
        first_in = (int)g->nv;
        for (k = 0; k < seg; k++) { float a = (float)k * 6.28318530718f / (float)seg; geom_vertex(g, cx + cosf(a) * rad, cy + sinf(a) * rad, col); }
        first_out = (int)g->nv;
        for (k = 0; k < seg; k++) { float a = (float)k * 6.28318530718f / (float)seg; geom_vertex(g, cx + cosf(a) * (rad + f), cy + sinf(a) * (rad + f), clear); }
        for (k = 0; k < seg; k++) {
            int n = (k + 1) % seg;
            geom_tri(g, center, first_in + k, first_in + n);
            geom_tri(g, first_in + k, first_out + k, first_out + n);
            geom_tri(g, first_in + k, first_out + n, first_in + n);
        }
        return;
    }
    if (geom_reserve(g, (size_t)(seg + 1), (size_t)(3 * seg)) != 0) return;
    center = geom_vertex(g, cx, cy, col);
    first_in = (int)g->nv;
    for (k = 0; k < seg; k++) { float a = (float)k * 6.28318530718f / (float)seg; geom_vertex(g, cx + cosf(a) * rad, cy + sinf(a) * rad, col); }
    for (k = 0; k < seg; k++) geom_tri(g, center, first_in + k, first_in + (k + 1) % seg);
}

size_t ex_geom_submit(ExRenderer *r, ExGeom *g) {
    size_t tris;
    if (!r || !g || g->ni == 0) return 0;
    SDL_RenderGeometry(r->renderer, NULL, g->v, (int)g->nv, g->idx, (int)g->ni);
    tris = g->ni / 3;
    return tris;
}

void ex_set_clip(ExRenderer *r, int x, int y, int w, int h) {
    if (w < 0) { SDL_SetRenderClipRect(r->renderer, NULL); return; }
    {
        SDL_Rect rect;
        rect.x = x; rect.y = y; rect.w = w; rect.h = h;
        SDL_SetRenderClipRect(r->renderer, &rect);
    }
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
    ExColor unused; /* measuring draws nothing, so the color is never read */
    memset(&unused, 0, sizeof unused);
    return layout(r, font, size, 0, 0, utf8, len, unused, 0);
}

size_t ex_text_fit(ExRenderer *r, ExFont *font, float size, const char *utf8, size_t len, float max_w, char *out, size_t cap) {
    static const char ell[] = "\xE2\x80\xA6";
    size_t starts[1025], n = 0, pos = 0, lo, hi, best = 0;
    char tmp[1100];
    int found = 0;
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!utf8 || len == 0 || max_w <= 0.0f) return 0;
    if (len + 1 <= cap && ex_text_measure(r, font, size, utf8, len) <= max_w) { memcpy(out, utf8, len); out[len] = 0; return len; }
    if (cap < 4) return 0;
    while (pos < len && n < 1024) { starts[n++] = pos; pos = ex_grapheme_next(utf8, len, pos); if (pos <= starts[n - 1]) break; }
    starts[n] = pos < len ? pos : len;
    /* the longest prefix of k clusters (k = 0..n) whose text plus the ellipsis fits the width and the buffer */
    lo = 0; hi = n;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2, cut = mid < n ? starts[mid] : starts[n];
        int ok = cut + 3 + 1 <= cap && cut + 3 <= sizeof tmp;
        if (ok) {
            memcpy(tmp, utf8, cut); memcpy(tmp + cut, ell, 3);
            ok = ex_text_measure(r, font, size, tmp, cut + 3) <= max_w;
        }
        if (ok) { best = cut; found = 1; lo = mid + 1; } else { if (mid == 0) break; hi = mid - 1; }
    }
    if (!found) return 0;
    memcpy(out, utf8, best); memcpy(out + best, ell, 3); out[best + 3] = 0;
    return best + 3;
}

int ex_text_metrics(ExRenderer *r, ExFont *font, float size, ExFontMetrics *out) {
    return ex_font_metrics(font, ex_px(r, size), out);
}

void ex_atlas_stats(const ExRenderer *r, ExAtlasStats *out) { ex_atlas_get_stats(r->atlas, out); }
