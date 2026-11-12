/* Text shaping: UTF-8 in, positioned glyphs out.
 *
 * ex_shape() splits text into grapheme clusters (UAX #29), resolves bidirectional
 * order (UAX #9, SheenBidi), splits it into runs of one script and one font
 * (choosing fallback fonts, with color emoji preferred for emoji sequences) and
 * shapes each run with HarfBuzz. Glyphs come out in visual order, left to right.
 * Positions are in pixels at the given pixel size. */
#ifndef EXHAUST_SHAPE_H
#define EXHAUST_SHAPE_H

#include <stddef.h>
#include <stdint.h>
#include "exhaust_text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExGlyph {
    ExFont *font;
    uint32_t glyph;     /* glyph index in font; 0 is the font's missing-glyph box */
    uint32_t cluster;   /* byte offset in the text of the cluster this glyph belongs to */
    float advance;      /* pixels */
    float x_offset;     /* pixels, from the pen position */
    float y_offset;     /* pixels, up is positive */
    uint32_t run;       /* index into ExShaped.runs */
} ExGlyph;

/* A run of text in one font, one script and one direction. Runs are listed in
 * visual order, left to right. */
typedef struct ExRun {
    ExFont *font;
    size_t text_start, text_end; /* byte range in the text */
    size_t first_glyph, glyph_count;
    uint8_t level;               /* bidi embedding level; odd means right to left */
    uint32_t script;             /* ISO 15924 tag, for example 'Latn' as 0x4C61746E */
    int color;                   /* the font is a color font (emoji) */
} ExRun;

typedef struct ExShaped {
    ExGlyph *glyphs;
    size_t nglyphs;
    ExRun *runs;
    size_t nruns;
    float width;                 /* sum of advances, pixels */
    int base_rtl;                /* the paragraph's base direction is right to left */
} ExShaped;

/* Shapes one line of text. primary is the preferred font. Returns 0 on success;
 * on failure *out is empty. Free with ex_shaped_free. */
int ex_shape(ExFontLib *lib, ExFont *primary, int px, const char *utf8, size_t len, ExShaped *out);
void ex_shaped_free(ExShaped *s);

/* Caret geometry for a shaped line. offset is a byte offset at a cluster
 * boundary (0 to the text length). The x of a caret is the leading edge of the
 * cluster that starts there, or the trailing edge of the last cluster at the end
 * of the text, so carets in right-to-left runs sit on the right of their cluster. */
float ex_shaped_x_for_offset(const ExShaped *s, size_t offset, size_t text_len);
/* The cluster boundary nearest to x (pixels from the line's left edge). */
size_t ex_shaped_offset_for_x(const ExShaped *s, float x, size_t text_len);

#define EX_TAG(a, b, c, d) (((uint32_t)(unsigned char)(a) << 24) | ((uint32_t)(unsigned char)(b) << 16) | \
                            ((uint32_t)(unsigned char)(c) << 8) | (uint32_t)(unsigned char)(d))

#ifdef __cplusplus
}
#endif
#endif
