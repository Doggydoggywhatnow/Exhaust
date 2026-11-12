#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SIZES_H
#include FT_TRUETYPE_TABLES_H

#include <hb.h>
#include <hb-ft.h>

#include "exhaust_platform.h"
#include "fonts_data.h"
#include "text_internal.h"

struct ExFont {
    ExFontLib *lib;
    FT_Face face;
    hb_font_t *hb;
    uint32_t id;
    int px;            /* pixel size currently set on the face, 0 = none */
    int strike_ppem;   /* for bitmap-only faces: the strike in use */
    int bitmap_only;
    uint8_t *tmp;      /* converted bitmap for the last rasterized glyph */
    size_t tmp_cap;
    struct ExFont *next;
};

struct ExFontLib {
    FT_Library ft;
    ExFont *fonts;
    ExFont *builtin[EX_FONT_BUILTIN_COUNT];
    ExFontDb *db;
    int own_db;
    ExFont **dbfonts; /* by database index: NULL not yet opened, (ExFont *)1 failed */
    int ndb;
};

static uint32_t g_next_font_id = 1;

ExFontLib *ex_fontlib_create(void) {
    ExFontLib *lib = calloc(1, sizeof *lib);
    if (!lib) return NULL;
    if (FT_Init_FreeType(&lib->ft)) { free(lib); return NULL; }
    return lib;
}

static void font_close(ExFont *f) {
    hb_font_destroy(f->hb);
    FT_Done_Face(f->face);
    free(f->tmp);
    free(f);
}

void ex_fontlib_destroy(ExFontLib *lib) {
    ExFont *f, *n;
    if (!lib) return;
    for (f = lib->fonts; f; f = n) { n = f->next; font_close(f); }
    if (lib->own_db) ex_fontdb_free(lib->db);
    free(lib->dbfonts);
    FT_Done_FreeType(lib->ft);
    free(lib);
}

void ex_fontlib_set_db(ExFontLib *lib, ExFontDb *db, int take_ownership) {
    if (lib->own_db) ex_fontdb_free(lib->db);
    free(lib->dbfonts); /* indexes belong to the old database; the fonts stay open */
    lib->dbfonts = NULL;
    lib->ndb = 0;
    lib->db = db;
    lib->own_db = take_ownership;
}

ExFontDb *ex_fontlib_db(ExFontLib *lib) { return lib ? lib->db : NULL; }

static ExFont *adopt_face(ExFontLib *lib, FT_Face face) {
    ExFont *f;
    hb_face_t *hbface;
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) || (!FT_IS_SCALABLE(face) && !(FT_HAS_COLOR(face) && face->num_fixed_sizes > 0))) {
        FT_Done_Face(face);
        return NULL;
    }
    f = calloc(1, sizeof *f);
    hbface = f ? hb_ft_face_create_referenced(face) : NULL;
    if (!f || !hbface) { free(f); FT_Done_Face(face); return NULL; }
    f->hb = hb_font_create(hbface);
    hb_face_destroy(hbface); /* the font holds it */
    f->lib = lib;
    f->face = face;
    f->bitmap_only = !FT_IS_SCALABLE(face);
    f->id = g_next_font_id++;
    f->next = lib->fonts;
    lib->fonts = f;
    return f;
}

ExFont *ex_font_open_memory(ExFontLib *lib, const unsigned char *data, size_t len) {
    FT_Face face;
    if (!lib || !data || len == 0 || len > 0x7fffffffu) return NULL;
    if (FT_New_Memory_Face(lib->ft, data, (FT_Long)len, 0, &face)) return NULL;
    return adopt_face(lib, face);
}

ExFont *ex_font_open_file(ExFontLib *lib, const char *path, int index) {
    FT_Face face;
    if (!lib || !path) return NULL;
    if (FT_New_Face(lib->ft, path, index, &face)) return NULL;
    return adopt_face(lib, face);
}

ExFont *ex_font_builtin(ExFontLib *lib, ExBuiltinFont which) {
    if (!lib || (int)which < 0 || which >= EX_FONT_BUILTIN_COUNT) return NULL;
    if (!lib->builtin[which]) {
        ExEmbeddedFont e = ex_embedded_font(which);
        lib->builtin[which] = ex_font_open_memory(lib, e.data, e.size);
    }
    return lib->builtin[which];
}

const char *ex_font_family(const ExFont *font) {
    return font && font->face->family_name ? font->face->family_name : "";
}

uint32_t ex_font_id(const ExFont *font) { return font ? font->id : 0; }
int ex_font_has_color(const ExFont *font) { return font && FT_HAS_COLOR(font->face); }
hb_font_t *ex_font_hb(ExFont *font) { return font->hb; }
int ex_font_upem(ExFont *font) { return font->face->units_per_EM ? font->face->units_per_EM : 1000; }

uint32_t ex_font_glyph_index(ExFont *font, uint32_t cp) {
    return font ? (uint32_t)FT_Get_Char_Index(font->face, cp) : 0;
}

static int set_size(ExFont *font, int px) {
    if (px < 1 || px > 4096) return -1;
    if (font->bitmap_only) {
        FT_Face face = font->face;
        int i, best = 0, best_ppem = 0;
        for (i = 0; i < face->num_fixed_sizes; i++) {
            int ppem = (int)((face->available_sizes[i].y_ppem + 32) >> 6);
            if (ppem == 0) ppem = face->available_sizes[i].height;
            if (best_ppem == 0 || (ppem >= px && (best_ppem < px || ppem < best_ppem)) || (best_ppem < px && ppem > best_ppem)) {
                best = i; best_ppem = ppem;
            }
        }
        if (FT_Select_Size(face, best)) return -1;
        font->strike_ppem = best_ppem > 0 ? best_ppem : px;
        font->px = px;
        return 0;
    }
    if (font->px != px) {
        if (FT_Set_Pixel_Sizes(font->face, 0, (FT_UInt)px)) { font->px = 0; return -1; }
        font->px = px;
    }
    return 0;
}

int ex_font_metrics(ExFont *font, int px, ExFontMetrics *out) {
    const FT_Size_Metrics *m;
    if (!font || !out || set_size(font, px)) return -1;
    m = &font->face->size->metrics;
    if (font->bitmap_only) {
        float f = (float)px / (float)font->strike_ppem;
        out->ascent = (int)ceilf((float)(m->ascender >> 6) * f);
        out->descent = (int)ceilf((float)(-m->descender >> 6) * f);
        out->line_height = out->ascent + out->descent;
        return 0;
    }
    out->ascent = (int)((m->ascender + 63) >> 6);
    out->descent = (int)((-m->descender + 63) >> 6);
    out->line_height = (int)((m->height + 63) >> 6);
    if (out->line_height < out->ascent + out->descent) out->line_height = out->ascent + out->descent;
    return 0;
}

static uint8_t *scratch(ExFont *f, size_t n) {
    if (n > f->tmp_cap) {
        uint8_t *p = realloc(f->tmp, n);
        if (!p) return NULL;
        f->tmp = p;
        f->tmp_cap = n;
    }
    return f->tmp;
}

/* Converts FreeType's premultiplied BGRA to straight-alpha RGBA, scaling by f
 * (a box filter) when f < 1 (bitmap strikes are bigger than the wanted size). */
static int convert_color(ExFont *font, const FT_Bitmap *bm, float f, ExGlyphBitmap *out) {
    int sw = (int)bm->width, sh = (int)bm->rows;
    int dw = f < 1.0f ? (int)lroundf((float)sw * f) : sw;
    int dh = f < 1.0f ? (int)lroundf((float)sh * f) : sh;
    uint8_t *dst;
    int x, y;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    dst = scratch(font, (size_t)dw * (size_t)dh * 4);
    if (!dst) return -1;
    for (y = 0; y < dh; y++) {
        int y0 = (int)((float)y * (float)sh / (float)dh), y1 = (int)((float)(y + 1) * (float)sh / (float)dh);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh) y1 = sh;
        for (x = 0; x < dw; x++) {
            int x0 = (int)((float)x * (float)sw / (float)dw), x1 = (int)((float)(x + 1) * (float)sw / (float)dw);
            unsigned sum[4] = {0, 0, 0, 0}, count, a;
            int yy, xx;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw) x1 = sw;
            for (yy = y0; yy < y1; yy++)
                for (xx = x0; xx < x1; xx++) {
                    const uint8_t *s = bm->buffer + (ptrdiff_t)yy * bm->pitch + xx * 4; /* B G R A, premultiplied */
                    sum[0] += s[2]; sum[1] += s[1]; sum[2] += s[0]; sum[3] += s[3];
                }
            count = (unsigned)((y1 - y0) * (x1 - x0));
            a = sum[3] / count;
            {
                uint8_t *d = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4;
                if (a == 0) { d[0] = d[1] = d[2] = d[3] = 0; }
                else {
                    /* un-premultiply: channel = sum / sum_alpha * 255 */
                    unsigned c;
                    for (c = 0; c < 3; c++) {
                        unsigned v = (sum[c] * 255u + sum[3] / 2) / sum[3];
                        d[c] = (uint8_t)(v > 255 ? 255 : v);
                    }
                    d[3] = (uint8_t)a;
                }
            }
        }
    }
    out->rgba = dst;
    out->coverage = NULL;
    out->color = 1;
    out->width = dw;
    out->height = dh;
    out->pitch = dw * 4;
    return 0;
}

int ex_font_rasterize(ExFont *font, uint32_t glyph, int px, ExGlyphBitmap *out) {
    FT_GlyphSlot slot;
    FT_Int32 flags;
    float f = 1.0f;
    if (!font || !out || set_size(font, px)) return -1;
    memset(out, 0, sizeof *out);
    flags = FT_HAS_COLOR(font->face) ? (FT_LOAD_COLOR | FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING)
                                      : (FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP);
    if (FT_Load_Glyph(font->face, glyph, flags)) return -1;
    slot = font->face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL)) return -1;
    if (font->bitmap_only) f = (float)px / (float)font->strike_ppem;

    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        if (convert_color(font, &slot->bitmap, f, out)) return -1;
        out->left = (int)lroundf((float)slot->bitmap_left * f);
        out->top = (int)lroundf((float)slot->bitmap_top * f);
        out->advance = (float)slot->advance.x / 64.0f * f;
        return 0;
    }
    if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && slot->bitmap.width != 0) return -1;
    out->coverage = slot->bitmap.buffer;
    out->width = (int)slot->bitmap.width;
    out->height = (int)slot->bitmap.rows;
    out->pitch = slot->bitmap.pitch;
    out->left = slot->bitmap_left;
    out->top = slot->bitmap_top;
    out->advance = (float)slot->linearHoriAdvance / 65536.0f; /* unhinted: layout scales smoothly */
    return 0;
}

float ex_font_advance(ExFont *font, uint32_t glyph, int px) {
    if (!font || set_size(font, px)) return 0.0f;
    if (font->bitmap_only) {
        if (FT_Load_Glyph(font->face, glyph, FT_LOAD_COLOR | FT_LOAD_NO_HINTING)) return 0.0f;
        return (float)font->face->glyph->advance.x / 64.0f * (float)px / (float)font->strike_ppem;
    }
    if (FT_Load_Glyph(font->face, glyph, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)) return 0.0f;
    return (float)font->face->glyph->linearHoriAdvance / 65536.0f;
}

float ex_font_kerning(ExFont *font, uint32_t left, uint32_t right, int px) {
    FT_Vector k;
    if (!font || !left || !right || !FT_HAS_KERNING(font->face) || set_size(font, px)) return 0.0f;
    if (FT_Get_Kerning(font->face, left, right, FT_KERNING_UNFITTED, &k)) return 0.0f;
    return (float)k.x / 64.0f;
}

/* ---- fallback fonts from the system database, opened on demand ---- */

#include "exhaust_fontdb.h"

ExFont *ex_fontlib_db_font(ExFontLib *lib, int idx) {
    const ExFontEntry *e;
    if (!lib->db || !(e = ex_fontdb_entry(lib->db, idx))) return NULL;
    if (!lib->dbfonts) {
        lib->ndb = ex_fontdb_count(lib->db);
        lib->dbfonts = calloc((size_t)lib->ndb, sizeof *lib->dbfonts);
        if (!lib->dbfonts) return NULL;
    }
    if (idx >= lib->ndb || lib->dbfonts[idx] == (ExFont *)1) return NULL;
    if (!lib->dbfonts[idx]) {
        ExFont *f = ex_font_open_file(lib, e->path, e->index);
        lib->dbfonts[idx] = f ? f : (ExFont *)1; /* remember failures */
        return f;
    }
    return lib->dbfonts[idx];
}

unsigned ex_font_style_flags(const ExFont *font) {
    return (font->face->style_flags & FT_STYLE_FLAG_BOLD ? EX_FONT_FLAG_BOLD : 0) |
           (font->face->style_flags & FT_STYLE_FLAG_ITALIC ? EX_FONT_FLAG_ITALIC : 0);
}
