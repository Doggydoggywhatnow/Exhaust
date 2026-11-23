/* Math layout (sub-slice 6d-i): a LaTeX subset laid out with TeX's rules and the numbers of STIX Two Math's OpenType MATH table (through hb-ot-math).
 * The result is a list of glyphs and rules in units of the font size, so one layout serves every pixel size. See docs/formats.md (Math). UI thread
 * only (HarfBuzz fonts are not shared between threads), but one equation takes well under a millisecond. */
#ifndef EXHAUST_MATH_H
#define EXHAUST_MATH_H

#include <stddef.h>
#include <stdint.h>
#include "exhaust_text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExMathItemKind { EX_MATH_GLYPH = 0, EX_MATH_RULE = 1 } ExMathItemKind;

/* Units: em of the equation's font size; y grows downwards from the baseline.
 * Glyph: (x, y) is the pen position on the baseline of the glyph, a is its size as a fraction of the font size (1, 0.7, 0.55: the font's ScriptPercentScaleDown and ScriptScriptPercentScaleDown).
 * Rule:  (x, y) is the top left corner, a the width and b the height. */
typedef struct ExMathItem {
    uint8_t kind;
    uint32_t glyph;
    float x, y, a, b;
} ExMathItem;

typedef struct ExMath ExMath;

/* Lays out `src` (the text between the dollar signs) in display or text style using the MATH font. Returns NULL when the font has no MATH table or on
 * memory failure. A parse error does not return NULL: ex_math_error() is then 1 and the layout is the source text itself, in the text font, so a
 * caller can draw it in the error color. */
ExMath *ex_math_layout(ExFont *font, const char *src, size_t len, int display);
void ex_math_free(ExMath *m);
int ex_math_error(const ExMath *m);
/* Box in em: width, height above the baseline, depth below it (all at least 0). */
float ex_math_width(const ExMath *m);
float ex_math_ascent(const ExMath *m);
float ex_math_descent(const ExMath *m);
const ExMathItem *ex_math_items(const ExMath *m, size_t *n);
ExFont *ex_math_font(const ExMath *m);

/* A cache of layouts keyed by a hash of (font, style, source). ex_mathcache_begin starts a pass (a layout of a page); layouts asked for since then
 * stay valid until the next begin; the ones not asked for in a pass are freed at the next begin. */
typedef struct ExMathCache ExMathCache;
ExMathCache *ex_mathcache_create(void);
void ex_mathcache_free(ExMathCache *c);
void ex_mathcache_begin(ExMathCache *c);
const ExMath *ex_mathcache_get(ExMathCache *c, ExFont *font, const char *src, size_t len, int display);
/* The same without making a layout: NULL when it is not in the cache (a found layout counts as asked for in this pass). */
const ExMath *ex_mathcache_lookup(ExMathCache *c, ExFont *font, const char *src, size_t len, int display);
size_t ex_mathcache_count(const ExMathCache *c);
/* Layouts made (not found) since the cache was created: for tests. */
size_t ex_mathcache_misses(const ExMathCache *c);

#ifdef __cplusplus
}
#endif
#endif
