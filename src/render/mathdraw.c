/* Draws a math layout (6d-i): its glyphs in one batch and its rules as rectangles. (x, y) is the left end of the baseline in pixels. */
#include <math.h>
#include <stdlib.h>

#include "exhaust_math.h"
#include "exhaust_render.h"

void ex_math_draw(ExRenderer *r, const ExMath *m, float size, float x, float y, ExColor color) {
    size_t n, i, ng = 0;
    const ExMathItem *it = ex_math_items(m, &n);
    float em = size * ex_renderer_scale(r);   /* pixels in one em */
    ExGlyphPlace stack[128], *g = stack;
    if (!m || n == 0) return;
    if (n > 128) { g = malloc(n * sizeof *g); if (!g) return; }
    for (i = 0; i < n; i++) {
        if (it[i].kind == EX_MATH_GLYPH) {
            g[ng].glyph = it[i].glyph;
            g[ng].x = x + it[i].x * em;
            g[ng].y = y + it[i].y * em;
            g[ng].size = size * it[i].a;
            ng++;
        } else {
            float rw = it[i].a * em, rh = it[i].b * em;
            if (rh < 1.0f) rh = 1.0f;
            ex_fill_rect(r, floorf(x + it[i].x * em + 0.5f), floorf(y + it[i].y * em + 0.5f), floorf(rw + 0.5f), floorf(rh + 0.5f), color);
        }
    }
    ex_glyphs_draw(r, ex_math_font(m), g, ng, color);
    if (g != stack) free(g);
}
