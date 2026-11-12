/* Exhaust text primitives: UTF-8 decoding and FreeType fonts.
 *
 * Fonts are loaded from memory. The built-in fonts are Atkinson Hyperlegible
 * (interface and editor) and Atkinson Hyperlegible Mono (code), embedded in
 * the executable. UI thread only: FreeType is not thread safe per library. */
#ifndef EXHAUST_TEXT_H
#define EXHAUST_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- UTF-8 ---- */

/* Decodes one code point from s[*i], advancing *i past it. Invalid, overlong,
 * surrogate or truncated sequences yield U+FFFD and consume one byte (a
 * truncated sequence consumes the bytes that were valid). Never reads past
 * s[len - 1]. Requires *i < len. */
uint32_t ex_utf8_decode(const char *s, size_t len, size_t *i);

/* Encodes cp into out (at least 4 bytes). Returns the byte count; encodes
 * U+FFFD for values that are not Unicode scalar values. */
int ex_utf8_encode(uint32_t cp, char *out);

/* ---- Grapheme clusters (UAX #29) ---- */

/* Returns the byte offset of the next extended grapheme cluster boundary after
 * pos, which must itself be a boundary (0 or a value returned earlier). Returns
 * len when no boundary follows. Invalid UTF-8 bytes count as one character
 * each, as U+FFFD. */
size_t ex_grapheme_next(const char *s, size_t len, size_t pos);
int ex_is_extended_pictographic(uint32_t cp);
int ex_is_emoji_presentation(uint32_t cp); /* emoji by default, not text */

/* ---- Fonts ---- */

typedef struct ExFontLib ExFontLib;
typedef struct ExFont ExFont;

typedef enum ExBuiltinFont {
    EX_FONT_UI_REGULAR,
    EX_FONT_UI_BOLD,
    EX_FONT_UI_ITALIC,
    EX_FONT_UI_BOLD_ITALIC,
    EX_FONT_MONO_REGULAR,
    EX_FONT_MONO_BOLD,
    EX_FONT_MONO_ITALIC,
    EX_FONT_MONO_BOLD_ITALIC,
    EX_FONT_BUILTIN_COUNT
} ExBuiltinFont;

typedef struct ExFontMetrics {
    int ascent;      /* pixels above the baseline */
    int descent;     /* pixels below the baseline, positive */
    int line_height; /* baseline to baseline */
} ExFontMetrics;

/* A rasterized glyph. The buffer belongs to the font and is valid until the
 * next call that rasterizes on the same font. Coverage is 8 bits per pixel. */
typedef struct ExGlyphBitmap {
    const uint8_t *coverage; /* gray glyphs: 8-bit coverage; NULL for color glyphs */
    const uint8_t *rgba;     /* color glyphs (emoji): straight-alpha RGBA, pitch = 4 * width */
    int color;
    int width, height, pitch;
    int left; /* pixels from the pen to the left edge */
    int top;  /* pixels from the baseline up to the top edge */
    float advance; /* pixels, not rounded */
} ExGlyphBitmap;

ExFontLib *ex_fontlib_create(void);
void ex_fontlib_destroy(ExFontLib *lib); /* closes every font opened from it */

/* The data must outlive the font. Returns NULL if it is not a usable font. */
ExFont *ex_font_open_memory(ExFontLib *lib, const unsigned char *data, size_t len);
/* Opens one face of a font file (index selects a face of a collection). */
ExFont *ex_font_open_file(ExFontLib *lib, const char *path, int index);
/* Built-in fonts are opened once per library and shared. */
ExFont *ex_font_builtin(ExFontLib *lib, ExBuiltinFont which);

const char *ex_font_family(const ExFont *font);
int ex_font_has_color(const ExFont *font);
/* Lets the library pick fallback fonts from a system font database
 * (exhaust_fontdb.h). With take_ownership the library frees it. */
struct ExFontDb;
void ex_fontlib_set_db(ExFontLib *lib, struct ExFontDb *db, int take_ownership);
uint32_t ex_font_glyph_index(ExFont *font, uint32_t codepoint); /* 0 = missing */
int ex_font_metrics(ExFont *font, int px, ExFontMetrics *out);
/* Rasterizes one glyph at a pixel size. Returns 0 on success. */
int ex_font_rasterize(ExFont *font, uint32_t glyph, int px, ExGlyphBitmap *out);
/* Advance in pixels, or 0 on failure. */
float ex_font_advance(ExFont *font, uint32_t glyph, int px);
/* Kerning between two glyphs in pixels (the font's `kern` table only; OpenType
 * GPOS kerning arrives with HarfBuzz in sub-slice 1a-ii). */
float ex_font_kerning(ExFont *font, uint32_t left, uint32_t right, int px);
/* Process-unique id, used as a cache key. */
uint32_t ex_font_id(const ExFont *font);

#ifdef __cplusplus
}
#endif
#endif
