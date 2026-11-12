#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "exhaust_text.h"
#include "fonts_data.h"

struct ExFont {
    ExFontLib *lib;
    FT_Face face;
    uint32_t id;
    int px; /* size currently set on the face, 0 = none */
    struct ExFont *next;
};

struct ExFontLib {
    FT_Library ft;
    ExFont *fonts;
    ExFont *builtin[EX_FONT_BUILTIN_COUNT];
};

static uint32_t g_next_font_id = 1;

ExFontLib *ex_fontlib_create(void) {
    ExFontLib *lib = calloc(1, sizeof *lib);
    if (!lib) return NULL;
    if (FT_Init_FreeType(&lib->ft)) { free(lib); return NULL; }
    return lib;
}

void ex_fontlib_destroy(ExFontLib *lib) {
    ExFont *f, *n;
    if (!lib) return;
    for (f = lib->fonts; f; f = n) {
        n = f->next;
        FT_Done_Face(f->face);
        free(f);
    }
    FT_Done_FreeType(lib->ft);
    free(lib);
}

ExFont *ex_font_open_memory(ExFontLib *lib, const unsigned char *data, size_t len) {
    ExFont *f;
    FT_Face face;
    if (!lib || !data || len == 0 || len > 0x7fffffffu) return NULL;
    if (FT_New_Memory_Face(lib->ft, data, (FT_Long)len, 0, &face)) return NULL;
    if (!FT_IS_SCALABLE(face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
        FT_Done_Face(face);
        return NULL;
    }
    f = calloc(1, sizeof *f);
    if (!f) { FT_Done_Face(face); return NULL; }
    f->lib = lib;
    f->face = face;
    f->id = g_next_font_id++;
    f->next = lib->fonts;
    lib->fonts = f;
    return f;
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

uint32_t ex_font_glyph_index(ExFont *font, uint32_t cp) {
    return font ? (uint32_t)FT_Get_Char_Index(font->face, cp) : 0;
}

static int set_size(ExFont *font, int px) {
    if (px < 1 || px > 4096) return -1;
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
    out->ascent = (int)((m->ascender + 63) >> 6);
    out->descent = (int)((-m->descender + 63) >> 6);
    out->line_height = (int)((m->height + 63) >> 6);
    if (out->line_height < out->ascent + out->descent) out->line_height = out->ascent + out->descent;
    return 0;
}

int ex_font_rasterize(ExFont *font, uint32_t glyph, int px, ExGlyphBitmap *out) {
    FT_GlyphSlot slot;
    if (!font || !out || set_size(font, px)) return -1;
    if (FT_Load_Glyph(font->face, glyph, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)) return -1;
    slot = font->face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL))
        return -1;
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
    if (FT_Load_Glyph(font->face, glyph, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)) return 0.0f;
    return (float)font->face->glyph->linearHoriAdvance / 65536.0f;
}

float ex_font_kerning(ExFont *font, uint32_t left, uint32_t right, int px) {
    FT_Vector k;
    if (!font || !left || !right || !FT_HAS_KERNING(font->face) || set_size(font, px)) return 0.0f;
    if (FT_Get_Kerning(font->face, left, right, FT_KERNING_UNFITTED, &k)) return 0.0f;
    return (float)k.x / 64.0f;
}
