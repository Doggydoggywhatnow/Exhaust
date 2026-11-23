/* math (6d-i): the sample equations of docs/formats.md (Math) are laid out with STIX Two Math and match golden metrics (width, height and depth in em,
 * and the number of glyphs and rules); the layout follows the TeX rules (a fraction's numerator is above its bar, scripts are smaller and shifted, a
 * delimiter grows with its content, the spaces of the atom classes), bad input gives the source back flagged as an error, and the cache keeps layouts
 * between passes. Usage: test_math [--dump | --ppm FILE] */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_math.h"
#include "exhaust_render.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"
#include "reading_view.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExFont *g_font;

typedef struct Golden { const char *src; int display; float w, h, d; int glyphs, rules; } Golden;

/* Made by `test_math --dump` and looked at (--ppm) on the day the engine was written: the numbers are em at the font size, so they do not depend on the
 * pixel size. They are not copies of another program's output; if the rules change on purpose, look at the pictures again and write them anew. */
static const Golden goldens[] = {
    { "x", 0, 0.559f, 0.479f, 0.010f, 1, 0 },
    { "x", 1, 0.559f, 0.479f, 0.010f, 1, 0 },
    { "x^2", 0, 0.943f, 0.811f, 0.010f, 2, 0 },
    { "x^2", 1, 0.943f, 0.811f, 0.010f, 2, 0 },
    { "x_i", 0, 0.779f, 0.479f, 0.216f, 2, 0 },
    { "x_i", 1, 0.779f, 0.479f, 0.216f, 2, 0 },
    { "x_i^2", 0, 0.943f, 0.831f, 0.242f, 3, 0 },
    { "x_i^2", 1, 0.943f, 0.831f, 0.242f, 3, 0 },
    { "a+b=c", 0, 3.927f, 0.705f, 0.040f, 5, 0 },
    { "a+b=c", 1, 3.927f, 0.705f, 0.040f, 5, 0 },
    { "\\frac{a}{b}", 0, 0.508f, 0.920f, 0.593f, 2, 1 },
    { "\\frac{a}{b}", 1, 0.675f, 1.119f, 0.652f, 2, 1 },
    { "\\frac{1}{2}", 0, 0.466f, 1.032f, 0.585f, 2, 1 },
    { "\\frac{1}{2}", 1, 0.615f, 1.279f, 0.640f, 2, 1 },
    { "\\sqrt{x}", 0, 1.353f, 0.905f, 0.282f, 2, 1 },
    { "\\sqrt{x}", 1, 1.353f, 0.947f, 0.240f, 2, 1 },
    { "\\sqrt[3]{x+1}", 0, 3.015f, 0.970f, 0.218f, 5, 1 },
    { "\\sqrt[3]{x+1}", 1, 3.015f, 1.012f, 0.175f, 5, 1 },
    { "\\sum_{i=1}^{n} i", 0, 2.447f, 0.947f, 0.375f, 6, 0 },
    { "\\sum_{i=1}^{n} i", 1, 1.553f, 1.556f, 1.081f, 6, 0 },
    { "\\int_0^1 f(x)\\,dx", 0, 4.587f, 1.021f, 0.338f, 9, 0 },
    { "\\int_0^1 f(x)\\,dx", 1, 5.245f, 1.717f, 1.035f, 9, 0 },
    { "\\left( \\frac{a}{b} \\right)", 0, 1.363f, 1.091f, 0.593f, 4, 1 },
    { "\\left( \\frac{a}{b} \\right)", 1, 1.529f, 1.119f, 0.652f, 4, 1 },
    { "e^{i\\pi}+1=0", 0, 4.518f, 0.826f, 0.040f, 7, 0 },
    { "e^{i\\pi}+1=0", 1, 4.518f, 0.826f, 0.040f, 7, 0 },
    { "\\alpha+\\beta", 0, 2.299f, 0.717f, 0.216f, 3, 0 },
    { "\\alpha+\\beta", 1, 2.299f, 0.717f, 0.216f, 3, 0 },
    { "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}", 0, 3.257f, 1.361f, 0.845f, 6, 0 },
    { "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}", 1, 3.257f, 1.361f, 0.845f, 6, 0 },
    { "\\hat{x}", 0, 0.559f, 0.674f, 0.010f, 2, 0 },
    { "\\hat{x}", 1, 0.559f, 0.674f, 0.010f, 2, 0 },
    { "\\overline{xy}", 0, 1.069f, 0.793f, 0.220f, 2, 1 },
    { "\\overline{xy}", 1, 1.069f, 0.793f, 0.220f, 2, 1 },
    { "\\lim_{n\\to\\infty} a_n", 0, 4.319f, 0.706f, 0.217f, 8, 0 },
    { "\\lim_{n\\to\\infty} a_n", 1, 2.871f, 0.706f, 0.677f, 8, 0 },
    { "\\mathbb{R}", 0, 0.729f, 0.657f, 0.000f, 1, 0 },
    { "\\mathbb{R}", 1, 0.729f, 0.657f, 0.000f, 1, 0 },
    { "\\text{if } x>0", 0, 3.157f, 0.713f, 0.025f, 6, 0 },
    { "\\text{if } x>0", 1, 3.157f, 0.713f, 0.025f, 6, 0 },
    { "x^{2}_{3}", 0, 0.943f, 0.831f, 0.230f, 3, 0 },
    { "x^{2}_{3}", 1, 0.943f, 0.831f, 0.230f, 3, 0 },
    { "\\binom{n}{k}", 0, 1.424f, 0.971f, 0.455f, 4, 0 },
    { "\\binom{n}{k}", 1, 1.685f, 1.259f, 0.700f, 4, 0 },
    { "\\begin{cases} 1 & x>0 \\\\ 0 & \\text{else} \\end{cases}", 0, 4.362f, 1.358f, 0.842f, 10, 0 },
    { "\\begin{cases} 1 & x>0 \\\\ 0 & \\text{else} \\end{cases}", 1, 4.362f, 1.358f, 0.842f, 10, 0 },
    { "a'", 0, 0.790f, 0.852f, 0.012f, 2, 0 },
    { "a'", 1, 0.790f, 0.852f, 0.012f, 2, 0 },
    { "\\frac{\\frac{a}{b}}{c}", 0, 0.509f, 1.449f, 0.593f, 3, 2 },
    { "\\frac{\\frac{a}{b}}{c}", 1, 0.628f, 1.956f, 0.652f, 3, 2 },
};

static const char *const samples[] = {
    "x", "x^2", "x_i", "x_i^2", "a+b=c", "\\frac{a}{b}", "\\frac{1}{2}", "\\sqrt{x}", "\\sqrt[3]{x+1}", "\\sum_{i=1}^{n} i", "\\int_0^1 f(x)\\,dx",
    "\\left( \\frac{a}{b} \\right)", "e^{i\\pi}+1=0", "\\alpha+\\beta", "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}",
    "\\hat{x}", "\\overline{xy}", "\\lim_{n\\to\\infty} a_n", "\\mathbb{R}", "\\text{if } x>0", "x^{2}_{3}", "\\binom{n}{k}",
    "\\begin{cases} 1 & x>0 \\\\ 0 & \\text{else} \\end{cases}", "a'", "\\frac{\\frac{a}{b}}{c}",
    NULL
};

static void count(const ExMath *m, int *glyphs, int *rules) {
    size_t n, i;
    const ExMathItem *it = ex_math_items(m, &n);
    *glyphs = *rules = 0;
    for (i = 0; i < n; i++) { if (it[i].kind == EX_MATH_GLYPH) (*glyphs)++; else (*rules)++; }
}

static ExMath *lay(const char *src, int display) { return ex_math_layout(g_font, src, strlen(src), display); }
static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static void dump(void) {
    size_t i;
    for (i = 0; samples[i]; i++) {
        int d;
        for (d = 0; d < 2; d++) {
            ExMath *m = lay(samples[i], d);
            int gl, ru;
            if (!m) { printf("{ \"%s\", %d, NULL },\n", samples[i], d); continue; }
            count(m, &gl, &ru);
            printf("    { \"%s\", %d, %.3ff, %.3ff, %.3ff, %d, %d },%s\n", samples[i], d, ex_math_width(m), ex_math_ascent(m), ex_math_descent(m), gl, ru, ex_math_error(m) ? " /* error */" : "");
            ex_math_free(m);
        }
    }
}

static int write_ppm(const char *path) {
    ExRenderer *r;
    ExColor bg = {250, 250, 250, 255}, fg = {20, 20, 20, 255}, bad = {200, 0, 0, 255};
    float y = 10.0f;
    size_t i;
    int w, h;
    uint8_t *rgb;
    FILE *f;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("math", 1000, 1600);
    if (!r) return 1;
    ex_renderer_clear(r, bg);
    for (i = 0; samples[i]; i++) {
        int d;
        float x = 10.0f, rowh = 0;
        for (d = 0; d < 2; d++) {
            ExMath *m = lay(samples[i], d);
            if (!m) continue;
            ex_math_draw(r, m, 36.0f, x, y + ex_math_ascent(m) * 36.0f, ex_math_error(m) ? bad : fg);
            x += ex_math_width(m) * 36.0f + 60.0f;
            if ((ex_math_ascent(m) + ex_math_descent(m)) * 36.0f > rowh) rowh = (ex_math_ascent(m) + ex_math_descent(m)) * 36.0f;
            ex_math_free(m);
        }
        y += rowh + 14.0f;
    }
    rgb = ex_renderer_read_rgb(r, &w, &h);
    if (!rgb) return 1;
    f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 3, (size_t)w * (size_t)h, f);
    fclose(f);
    free(rgb);
    ex_renderer_destroy(r);
    return 0;
}

static void test_goldens(void) {
    size_t i;
    for (i = 0; i < sizeof goldens / sizeof goldens[0]; i++) {
        const Golden *g = &goldens[i];
        ExMath *m = lay(g->src, g->display);
        int gl = 0, ru = 0;
        CHECK(m != NULL);
        if (!m) continue;
        count(m, &gl, &ru);
        if (!near(ex_math_width(m), g->w, 0.006f) || !near(ex_math_ascent(m), g->h, 0.006f) || !near(ex_math_descent(m), g->d, 0.006f) || gl != g->glyphs || ru != g->rules)
            fprintf(stderr, "golden %s (display %d): got %.3f %.3f %.3f %d %d, want %.3f %.3f %.3f %d %d\n", g->src, g->display, ex_math_width(m), ex_math_ascent(m),
                    ex_math_descent(m), gl, ru, g->w, g->h, g->d, g->glyphs, g->rules);
        CHECK(near(ex_math_width(m), g->w, 0.006f));
        CHECK(near(ex_math_ascent(m), g->h, 0.006f));
        CHECK(near(ex_math_descent(m), g->d, 0.006f));
        CHECK(gl == g->glyphs && ru == g->rules);
        CHECK(!ex_math_error(m));
        ex_math_free(m);
    }
}

/* The rules of the layout, checked by position rather than by number. */
static void test_structure(void) {
    size_t n, i;
    const ExMathItem *it;
    ExMath *m;
    float top_y = 1e9f, bottom_y = -1e9f, rule_y = 0, rule_h = 0, rule_w = 0;
    int rules = 0;

    /* a fraction: the rule is the font's thickness, centered on the math axis (258 and 68 units of 1000), the numerator above it and the denominator below */
    m = lay("\\frac{a}{b}", 0);
    CHECK(m != NULL);
    it = ex_math_items(m, &n);
    for (i = 0; i < n; i++) {
        if (it[i].kind == EX_MATH_RULE) { rule_y = it[i].y; rule_h = it[i].b; rule_w = it[i].a; rules++; }
        else { if (it[i].y < top_y) top_y = it[i].y; if (it[i].y > bottom_y) bottom_y = it[i].y; }
    }
    CHECK(rules == 1);
    CHECK(near(rule_h, 0.068f, 0.001f));
    CHECK(near(rule_y + rule_h / 2.0f, -0.258f, 0.001f));
    CHECK(top_y < rule_y && bottom_y > rule_y + rule_h);
    CHECK(rule_w >= ex_math_width(m) - 0.13f);
    ex_math_free(m);

    /* scripts: 70% size, the superscript above the baseline, the subscript below, and both when both are given */
    m = lay("x^2", 0);
    it = ex_math_items(m, &n);
    CHECK(n == 2 && near(it[0].a, 1.0f, 1e-4f) && near(it[1].a, 0.7f, 1e-4f) && it[1].y < -0.2f && it[1].x > it[0].x);
    ex_math_free(m);
    m = lay("x_i", 0);
    it = ex_math_items(m, &n);
    CHECK(n == 2 && near(it[1].a, 0.7f, 1e-4f) && it[1].y > 0.1f);
    ex_math_free(m);
    m = lay("x_i^2", 0);
    it = ex_math_items(m, &n);
    CHECK(n == 3 && it[1].y < it[0].y && it[2].y > it[0].y);   /* the superscript is put first, then the subscript */
    ex_math_free(m);
    m = lay("x^{y^z}", 0);                                       /* a script of a script is 55% (the font's value) */
    it = ex_math_items(m, &n);
    CHECK(n == 3 && near(it[2].a, 0.55f, 1e-4f));
    ex_math_free(m);

    /* atom spacing: a binary operator gets a medium space on both sides, a relation a thick one, in text style; in a script none */
    {
        ExMath *a = lay("a", 0), *p = lay("+", 0), *b = lay("b", 0), *ab = lay("a+b", 0), *eq = lay("=", 0), *aeb = lay("a=b", 0);
        float base = ex_math_width(a) + ex_math_width(p) + ex_math_width(b);
        CHECK(near(ex_math_width(ab) - base, 2.0f * 4.0f / 18.0f, 0.004f));
        base = ex_math_width(a) + ex_math_width(eq) + ex_math_width(b);
        CHECK(near(ex_math_width(aeb) - base, 2.0f * 5.0f / 18.0f, 0.004f));
        ex_math_free(a); ex_math_free(p); ex_math_free(b); ex_math_free(ab); ex_math_free(eq); ex_math_free(aeb);
    }
    {
        ExMath *s1 = lay("x^{a+b}", 0), *s2 = lay("x^{ab}", 0), *pl = lay("+", 0);
        CHECK(near(ex_math_width(s1) - ex_math_width(s2), 0.7f * ex_math_width(pl), 0.004f));
        ex_math_free(s1); ex_math_free(s2); ex_math_free(pl);
    }
    { /* a minus sign at the start of a group is a sign, not an operator: no space before it */
        ExMath *neg = lay("-a", 0), *m1 = lay("-", 0), *a = lay("a", 0);
        CHECK(near(ex_math_width(neg), ex_math_width(m1) + ex_math_width(a), 0.004f));
        ex_math_free(neg); ex_math_free(m1); ex_math_free(a);
    }
    { /* thin space commands */
        ExMath *a = lay("a\\,b", 0), *b = lay("ab", 0);
        CHECK(near(ex_math_width(a) - ex_math_width(b), 3.0f / 18.0f, 0.004f));
        ex_math_free(a); ex_math_free(b);
        a = lay("a\\quad b", 0);
        b = lay("ab", 0);
        CHECK(near(ex_math_width(a) - ex_math_width(b), 1.0f, 0.004f));
        ex_math_free(a); ex_math_free(b);
    }

    /* display style makes sums and integrals bigger, and puts the limits above and below */
    {
        ExMath *t = lay("\\sum_{i=1}^{n}", 0), *d = lay("\\sum_{i=1}^{n}", 1);
        CHECK(ex_math_ascent(d) + ex_math_descent(d) > 1.5f * (ex_math_ascent(t) + ex_math_descent(t)));
        CHECK(ex_math_width(d) < ex_math_width(t) + 0.5f);
        ex_math_free(t); ex_math_free(d);
        t = lay("\\sum_{i=1}^{n}", 0);
        d = lay("\\sum_{i=1}^{n}", 1);
        ex_math_free(t); ex_math_free(d);
        t = lay("\\sum\\nolimits_{i=1}^{n}", 1);
        d = lay("\\sum_{i=1}^{n}", 1);
        CHECK(ex_math_ascent(t) + ex_math_descent(t) < ex_math_ascent(d) + ex_math_descent(d));   /* side scripts are shorter than limits */
        ex_math_free(t); ex_math_free(d);
    }

    /* \left( ... \right) grows with its content and is centered on the axis */
    {
        ExMath *plain = lay("(\\frac{a}{b})", 1), *grown = lay("\\left(\\frac{a}{b}\\right)", 1), *small = lay("\\left(x\\right)", 1);
        size_t n1, n2;
        const ExMathItem *a = ex_math_items(plain, &n1), *b = ex_math_items(grown, &n2);
        CHECK(a && b && a[0].glyph != b[0].glyph);                     /* a bigger variant of the parenthesis */
        CHECK(ex_math_ascent(grown) + ex_math_descent(grown) >= ex_math_ascent(plain) + ex_math_descent(plain) - 0.001f);
        ex_math_free(plain); ex_math_free(grown); ex_math_free(small);
    }
    /* a very tall content is stretched with an assembly of several glyphs */
    {
        ExMath *tall = lay("\\left(\\frac{\\frac{\\frac{a}{b}}{\\frac{c}{d}}}{\\frac{\\frac{e}{f}}{\\frac{g}{h}}}\\right)", 1), *one = lay("\\left(x\\right)", 1);
        int g1, r1, g2, r2;
        count(tall, &g1, &r1); count(one, &g2, &r2);
        CHECK(g1 > 8 + 2);
        ex_math_free(tall); ex_math_free(one);
    }

    /* a square root: one rule above the radicand, as thick as the font says (the sign hangs from it) */
    m = lay("\\sqrt{x}", 0);
    it = ex_math_items(m, &n);
    rules = 0;
    for (i = 0; i < n; i++) if (it[i].kind == EX_MATH_RULE) { rules++; rule_y = it[i].y; rule_h = it[i].b; }
    CHECK(rules == 1 && rule_y < -0.4f);
    CHECK(ex_math_ascent(m) >= -rule_y - 0.001f);
    ex_math_free(m);

    /* a matrix: two rows of two, aligned in columns */
    m = lay("\\begin{matrix} a & b \\\\ c & d \\end{matrix}", 1);
    it = ex_math_items(m, &n);
    CHECK(n == 4);
    if (n == 4) {
        CHECK(near(it[0].y, it[1].y, 1e-4f) && near(it[2].y, it[3].y, 1e-4f) && it[2].y > it[0].y + 0.5f);
        CHECK(it[1].x > it[0].x + 0.5f && near(it[0].x, it[2].x, 0.2f));
    }
    ex_math_free(m);

    /* alphabets: \mathbb{R} is one glyph that is not the italic R; \mathrm{d} is upright */
    {
        ExMath *bb = lay("\\mathbb{R}", 0), *it_r = lay("R", 0), *rm = lay("\\mathrm{d}", 0), *d = lay("d", 0);
        const ExMathItem *a = ex_math_items(bb, &n), *b = ex_math_items(it_r, &n);
        CHECK(a && b && a[0].glyph != b[0].glyph);
        a = ex_math_items(rm, &n); b = ex_math_items(d, &n);
        CHECK(a && b && a[0].glyph != b[0].glyph);
        ex_math_free(bb); ex_math_free(it_r); ex_math_free(rm); ex_math_free(d);
    }
}

/* Input that cannot be laid out gives the source as text, flagged. */
static void test_errors(void) {
    static const char *const bad[] = { "\\frac{1", "\\unknowncommand", "x^", "{", "}", "\\left(", "\\left( x \\right", "\\begin{foo} x \\end{foo}", "\\begin{matrix} a \\end{pmatrix}",
        "\\sqrt", "\\frac{1}", "a & b", "\\\\", "\\end{matrix}", "\\right)", "x_", "\\limits", NULL };
    size_t i;
    char *big, *deep;
    ExMath *m;
    for (i = 0; bad[i]; i++) {
        m = lay(bad[i], 0);
        CHECK(m != NULL);
        if (m) {
            if (!ex_math_error(m)) fprintf(stderr, "no error for %s\n", bad[i]);
            CHECK(ex_math_error(m));
            CHECK(ex_math_width(m) > 0.0f || bad[i][0] == 0);
            ex_math_free(m);
        }
    }
    big = malloc(9000);
    memset(big, 'x', 9000);
    m = ex_math_layout(g_font, big, 9000, 0);
    CHECK(m && ex_math_error(m));
    ex_math_free(m);
    free(big);
    deep = malloc(400);
    memset(deep, '{', 200); memset(deep + 200, '}', 200);
    m = ex_math_layout(g_font, deep, 400, 0);
    CHECK(m && ex_math_error(m));
    ex_math_free(m);
    free(deep);
    m = ex_math_layout(g_font, "", 0, 0);
    CHECK(m && !ex_math_error(m) && ex_math_width(m) == 0.0f);
    ex_math_free(m);
    m = lay("\\text{caf\xC3\xA9 \xE2\x82\xAC}", 0);       /* UTF-8 in text */
    CHECK(m && !ex_math_error(m));
    ex_math_free(m);
    CHECK(ex_math_layout(NULL, "x", 1, 0) == NULL);
}

static void test_cache(void) {
    ExMathCache *c = ex_mathcache_create();
    const ExMath *a, *b, *d;
    CHECK(c != NULL);
    ex_mathcache_begin(c);
    a = ex_mathcache_get(c, g_font, "x^2", 3, 0);
    b = ex_mathcache_get(c, g_font, "x^2", 3, 0);
    d = ex_mathcache_get(c, g_font, "x^2", 3, 1);                /* another style is another layout */
    CHECK(a && a == b && d && d != a);
    CHECK(ex_mathcache_misses(c) == 2 && ex_mathcache_count(c) == 2);
    ex_mathcache_begin(c);                                        /* a second pass asks for the first again only */
    b = ex_mathcache_get(c, g_font, "x^2", 3, 0);
    CHECK(b != NULL && ex_mathcache_misses(c) == 2);
    ex_mathcache_begin(c);                                        /* the style-1 layout was not asked for in the last pass: gone */
    CHECK(ex_mathcache_count(c) == 1);
    ex_mathcache_begin(c);
    ex_mathcache_begin(c);
    CHECK(ex_mathcache_count(c) == 0);
    ex_mathcache_free(c);
}

/* Random garbage built from the tokens of the grammar never crashes and always gives a layout. */
static void test_random(void) {
    static const char *const bits[] = { "x", "y", "1", "+", "=", "^", "_", "{", "}", "\\frac", "\\sqrt", "[", "]", "\\left", "\\right", "(", ")", "\\sum", "\\int", "&", "\\\\",
        "\\begin{matrix}", "\\end{matrix}", "\\hat", "\\overline", "\\text", "\\mathbb", "\\alpha", "\\,", " ", "'", "\\binom", "\\limits", "\\lim", "\\operatorname", "~", "\\", "$", "\xC3" };
    unsigned long long st = 88172645463325252ull;
    int iter;
    for (iter = 0; iter < 3000; iter++) {
        char buf[200];
        size_t len = 0, k, parts;
        ExMath *m;
        st ^= st << 13; st ^= st >> 7; st ^= st << 17;
        parts = 1 + (size_t)(st % 14);
        for (k = 0; k < parts; k++) {
            const char *b;
            size_t bl;
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            b = bits[st % (sizeof bits / sizeof bits[0])];
            bl = strlen(b);
            if (len + bl < sizeof buf) { memcpy(buf + len, b, bl); len += bl; }
        }
        m = ex_math_layout(g_font, buf, len, (int)(st >> 40) & 1);
        CHECK(m != NULL);
        ex_math_free(m);
    }
}

/* The reading view draws display math centered with the text color, inline math in the line, and leaves what cannot be laid out as source. */
static void ink_extent(const uint8_t *rgb, int w, int h, int y0, int y1, ExColor bg, int *left, int *right, int *count) {
    int x, y;
    *left = w; *right = -1; *count = 0;
    for (y = y0; y < y1 && y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
            if (abs(p[0] - bg.r) + abs(p[1] - bg.g) + abs(p[2] - bg.b) > 60) { if (x < *left) *left = x; if (x > *right) *right = x; (*count)++; }
        }
}

static void test_reading(void) {
    ExRenderer *r;
    ExTheme theme;
    ExReadingView *v;
    ExColor bg;
    uint8_t *rgb;
    int w, h, l, rr, n;
    float h_ok, h_bad;
    const char *ok = "$$\\frac{a}{b} + \\sqrt{x}$$\n", *bad = "$$\\frac{a$$\n";
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("math-reading", 600, 300);
    CHECK(r != NULL);
    if (!r) return;
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    bg = ex_theme_color(&theme, EX_COLOR_BACKGROUND);
    v = ex_readingview_create(ok, strlen(ok));
    ex_readingview_set_viewport(v, 0, 0, 600, 300);
    ex_renderer_clear(r, bg);
    ex_readingview_draw(v, r, &theme, 16.0f);
    h_ok = ex_readingview_content_height(v);
    rgb = ex_renderer_read_rgb(r, &w, &h);
    CHECK(rgb != NULL);
    if (rgb) {
        ink_extent(rgb, w, h, 0, h, bg, &l, &rr, &n);
        CHECK(n > 100);
        CHECK(abs((l + rr) / 2 - w / 2) < 40);         /* centered in the column of the page */
        free(rgb);
    }
    ex_readingview_free(v);
    v = ex_readingview_create(bad, strlen(bad));
    ex_readingview_set_viewport(v, 0, 0, 600, 300);
    ex_renderer_clear(r, bg);
    ex_readingview_draw(v, r, &theme, 16.0f);
    h_bad = ex_readingview_content_height(v);
    CHECK(h_ok != h_bad);                              /* the source of a bad formula is a code block, not a formula */
    ex_readingview_free(v);
    { /* inline: a fraction in a line of text stays on that line, and a narrow view wraps around it */
        const char *t = "a line of words with $\\frac{1}{2}$ inside and more words after it\n";
        float h1, h2;
        v = ex_readingview_create(t, strlen(t));
        ex_readingview_set_viewport(v, 0, 0, 600, 300);
        ex_renderer_clear(r, bg);
        ex_readingview_draw(v, r, &theme, 16.0f);
        h1 = ex_readingview_content_height(v);
        ex_readingview_set_viewport(v, 0, 0, 140, 300);
        ex_renderer_clear(r, bg);
        ex_readingview_draw(v, r, &theme, 16.0f);
        h2 = ex_readingview_content_height(v);
        CHECK(h2 > h1);
        ex_readingview_free(v);
    }
    { /* a note full of equations: a layout makes at most 24, the others are made in the next frames, and it ends */
        char *big = malloc(40000);
        size_t len = 0;
        int i, draws = 0, first_pending;
        for (i = 0; i < 600; i++) len += (size_t)snprintf(big + len, 40000 - len, "$x^{%d}$ ", i);
        v = ex_readingview_create(big, len);
        ex_readingview_set_viewport(v, 0, 0, 600, 300);
        ex_renderer_clear(r, bg);
        ex_readingview_draw(v, r, &theme, 16.0f);
        first_pending = ex_readingview_math_pending(v);
        CHECK(first_pending == 1);
        while (ex_readingview_math_pending(v) && draws < 200) { ex_readingview_draw(v, r, &theme, 16.0f); draws++; }
        CHECK(!ex_readingview_math_pending(v) && draws >= 600 / 24 - 1 && draws < 200);
        ex_readingview_free(v);
        free(big);
    }
    ex_renderer_destroy(r);
}

int main(int argc, char **argv) {
    ExFontLib *lib = ex_fontlib_create();
    CHECK(lib != NULL);
    g_font = ex_font_builtin(lib, EX_FONT_MATH);
    CHECK(g_font != NULL);
    if (!g_font) return 1;
    if (argc > 2 && !strcmp(argv[1], "--ppm")) return write_ppm(argv[2]);
    if (argc > 2 && !strcmp(argv[1], "--show")) {   /* the items of one equation, for looking at a layout */
        ExMath *m = lay(argv[2], argc > 3);
        size_t n, k;
        const ExMathItem *it = ex_math_items(m, &n);
        printf("w=%.4f h=%.4f d=%.4f error=%d\n", ex_math_width(m), ex_math_ascent(m), ex_math_descent(m), ex_math_error(m));
        for (k = 0; k < n; k++) printf("  %s glyph %u x %.4f y %.4f a %.3f b %.3f\n", it[k].kind ? "rule " : "glyph", it[k].glyph, it[k].x, it[k].y, it[k].a, it[k].b);
        ex_math_free(m);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--dump")) { dump(); ex_fontlib_destroy(lib); return 0; }
    test_goldens();
    test_structure();
    test_errors();
    test_cache();
    test_random();
    test_reading();
    ex_fontlib_destroy(lib);
    if (failures) fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
