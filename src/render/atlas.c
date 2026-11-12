#include <stdlib.h>
#include <string.h>

#include "atlas.h"

#define PAGE_SIZE 1024
#define PAD 1
#define MAX_PAGES_BEFORE_RESET 4

typedef struct Page {
    SDL_Texture *tex;
    uint8_t *pix; /* RGBA, PAGE w * h * 4 */
    int w, h;
    int cur_x, cur_y, row_h;
    int dirty;                 /* any pixels not uploaded yet */
    int dx0, dy0, dx1, dy1;    /* dirty rectangle */
} Page;

typedef struct Entry {
    uint32_t font, glyph;
    int px;
    int used;
    ExAtlasGlyph g;
} Entry;

struct ExAtlas {
    SDL_Renderer *renderer;
    int page_size;
    Page *pages;
    int npages, cap_pages;
    Entry *table;
    size_t cap, count;
    int rasterized_total, resets;
};

ExAtlas *ex_atlas_create(SDL_Renderer *renderer) {
    ExAtlas *a = calloc(1, sizeof *a);
    SDL_PropertiesID props;
    Sint64 max_tex = 0;
    if (!a) return NULL;
    a->renderer = renderer;
    a->page_size = PAGE_SIZE;
    props = SDL_GetRendererProperties(renderer);
    if (props) max_tex = SDL_GetNumberProperty(props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    if (max_tex > 0 && max_tex < a->page_size) a->page_size = (int)max_tex;
    return a;
}

static void free_pages(ExAtlas *a) {
    int i;
    for (i = 0; i < a->npages; i++) {
        if (a->pages[i].tex) SDL_DestroyTexture(a->pages[i].tex);
        free(a->pages[i].pix);
    }
    free(a->pages);
    a->pages = NULL;
    a->npages = a->cap_pages = 0;
}

void ex_atlas_clear(ExAtlas *a) {
    free_pages(a);
    free(a->table);
    a->table = NULL;
    a->cap = a->count = 0;
    a->resets++;
}

void ex_atlas_destroy(ExAtlas *a) {
    if (!a) return;
    free_pages(a);
    free(a->table);
    free(a);
}

static size_t hash_key(uint32_t font, uint32_t glyph, int px) {
    uint64_t h = ((uint64_t)font << 40) ^ ((uint64_t)glyph << 12) ^ (uint64_t)(unsigned)px;
    h *= 0x9E3779B97F4A7C15ull;
    return (size_t)(h >> 20);
}

static Entry *find(ExAtlas *a, uint32_t font, uint32_t glyph, int px) {
    size_t i;
    if (!a->cap) return NULL;
    for (i = hash_key(font, glyph, px) & (a->cap - 1);; i = (i + 1) & (a->cap - 1)) {
        Entry *e = &a->table[i];
        if (!e->used) return NULL;
        if (e->font == font && e->glyph == glyph && e->px == px) return e;
    }
}

static int grow(ExAtlas *a) {
    size_t ncap = a->cap ? a->cap * 2 : 256, i;
    Entry *nt = calloc(ncap, sizeof *nt);
    if (!nt) return -1;
    for (i = 0; i < a->cap; i++) {
        size_t j;
        if (!a->table[i].used) continue;
        for (j = hash_key(a->table[i].font, a->table[i].glyph, a->table[i].px) & (ncap - 1);
             nt[j].used; j = (j + 1) & (ncap - 1)) {}
        nt[j] = a->table[i];
    }
    free(a->table);
    a->table = nt;
    a->cap = ncap;
    return 0;
}

static int add_page(ExAtlas *a) {
    Page *p;
    if (a->npages == a->cap_pages) {
        int nc = a->cap_pages ? a->cap_pages * 2 : 2;
        Page *np = realloc(a->pages, (size_t)nc * sizeof *np);
        if (!np) return -1;
        a->pages = np;
        a->cap_pages = nc;
    }
    p = &a->pages[a->npages];
    memset(p, 0, sizeof *p);
    p->w = p->h = a->page_size;
    p->pix = calloc((size_t)p->w * (size_t)p->h, 4);
    if (!p->pix) return -1;
    p->tex = SDL_CreateTexture(a->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, p->w, p->h);
    if (!p->tex) { free(p->pix); return -1; }
    SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(p->tex, SDL_SCALEMODE_NEAREST);
    p->cur_x = p->cur_y = PAD;
    return a->npages++;
}

/* Finds room for w x h (plus borders) on the newest page, else on a new page.
 * A fresh page always fits a glyph that passed the size check. */
static int place(ExAtlas *a, int w, int h, int *ox, int *oy) {
    if (w + 2 * PAD > a->page_size || h + 2 * PAD > a->page_size) return -1;
    for (;;) {
        if (a->npages > 0) {
            Page *p = &a->pages[a->npages - 1];
            if (p->cur_x + w + PAD > p->w) {
                p->cur_y += p->row_h + PAD;
                p->cur_x = PAD;
                p->row_h = 0;
            }
            if (p->cur_y + h + PAD <= p->h) {
                *ox = p->cur_x;
                *oy = p->cur_y;
                p->cur_x += w + PAD;
                if (h > p->row_h) p->row_h = h;
                return a->npages - 1;
            }
        }
        if (add_page(a) < 0) return -1;
    }
}

const ExAtlasGlyph *ex_atlas_glyph(ExAtlas *a, ExFont *font, uint32_t glyph, int px) {
    Entry *e = find(a, ex_font_id(font), glyph, px);
    ExGlyphBitmap bm;
    ExAtlasGlyph g;
    size_t i;
    if (e) return &e->g;
    if (ex_font_rasterize(font, glyph, px, &bm)) return NULL;

    memset(&g, 0, sizeof g);
    g.page = -1;
    g.left = bm.left;
    g.top = bm.top;
    g.advance = bm.advance;
    if (bm.width > 0 && bm.height > 0) {
        int ox, oy, pi, row, col;
        Page *p;
        pi = place(a, bm.width, bm.height, &ox, &oy);
        if (pi < 0) return NULL;
        p = &a->pages[pi];
        for (row = 0; row < bm.height; row++) {
            const uint8_t *src = bm.coverage + (ptrdiff_t)row * bm.pitch;
            uint8_t *dst = p->pix + ((size_t)(oy + row) * (size_t)p->w + (size_t)ox) * 4;
            for (col = 0; col < bm.width; col++) {
                dst[col * 4 + 0] = 255;
                dst[col * 4 + 1] = 255;
                dst[col * 4 + 2] = 255;
                dst[col * 4 + 3] = src[col];
            }
        }
        if (!p->dirty) {
            p->dx0 = ox; p->dy0 = oy; p->dx1 = ox + bm.width; p->dy1 = oy + bm.height;
            p->dirty = 1;
        } else {
            if (ox < p->dx0) p->dx0 = ox;
            if (oy < p->dy0) p->dy0 = oy;
            if (ox + bm.width > p->dx1) p->dx1 = ox + bm.width;
            if (oy + bm.height > p->dy1) p->dy1 = oy + bm.height;
        }
        g.page = pi; g.x = ox; g.y = oy; g.w = bm.width; g.h = bm.height;
    }

    if ((a->count + 1) * 2 > a->cap && grow(a) < 0) return NULL;
    for (i = hash_key(ex_font_id(font), glyph, px) & (a->cap - 1); a->table[i].used; i = (i + 1) & (a->cap - 1)) {}
    a->table[i].used = 1;
    a->table[i].font = ex_font_id(font);
    a->table[i].glyph = glyph;
    a->table[i].px = px;
    a->table[i].g = g;
    a->count++;
    a->rasterized_total++;
    return &a->table[i].g;
}

SDL_Texture *ex_atlas_texture(ExAtlas *a, int page, int *tex_w, int *tex_h) {
    Page *p;
    if (page < 0 || page >= a->npages) return NULL;
    p = &a->pages[page];
    if (p->dirty) {
        SDL_Rect r;
        r.x = p->dx0; r.y = p->dy0; r.w = p->dx1 - p->dx0; r.h = p->dy1 - p->dy0;
        SDL_UpdateTexture(p->tex, &r, p->pix + ((size_t)r.y * (size_t)p->w + (size_t)r.x) * 4, p->w * 4);
        p->dirty = 0;
    }
    if (tex_w) *tex_w = p->w;
    if (tex_h) *tex_h = p->h;
    return p->tex;
}

int ex_atlas_wants_reset(const ExAtlas *a) { return a->npages > MAX_PAGES_BEFORE_RESET; }

void ex_atlas_get_stats(const ExAtlas *a, ExAtlasStats *out) {
    out->pages = a->npages;
    out->glyphs_cached = (int)a->count;
    out->rasterized_total = a->rasterized_total;
    out->resets = a->resets;
}
