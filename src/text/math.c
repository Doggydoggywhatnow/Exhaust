/* Math layout (6d-i). A parser for a LaTeX subset that lays out as it reads (the style is known going down), with TeX's rules (Appendix G of the
 * TeXbook) and the constants of the font's MATH table. A box is a range of a pool of items (glyphs and rules); putting a box into another copies
 * its range with an offset, so there is nothing to free on a parse error but the pool. See docs/formats.md (Math). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hb.h>
#include <hb-ot.h>

#include "exhaust_math.h"
#include "text_internal.h"

#define MAX_INPUT 8192
#define MAX_DEPTH 64
#define MAX_POOL 400000

enum { C_ORD, C_OP, C_BIN, C_REL, C_OPEN, C_CLOSE, C_PUNCT, C_INNER, C_NONE };
enum { T_END, T_RBRACE, T_RIGHT, T_AMP, T_ROWSEP, T_ENDENV };
enum { A_DEFAULT, A_RM, A_BF, A_IT, A_BB, A_CAL, A_FRAK, A_SF, A_TT, A_BFIT };
enum { LIM_DEFAULT, LIM_YES, LIM_NO };

typedef struct Box {
    float w, h, d;   /* em; h above the baseline, d below */
    float ital;      /* italic correction */
    float attach;    /* top accent attachment, or -1 */
    size_t s, e;     /* range in the pool */
} Box;

typedef struct Atom {
    Box b;
    int cls;
    int glue;        /* a space: no class, never changes the class of the next atom */
    int single;      /* a single character: scripts do not drop below its own box */
    int lim;         /* LIM_*, for an operator */
    int bigop;       /* a big operator symbol: a larger variant in display style */
    int limdefault;  /* limits above and below in display style when not set otherwise */
    int textop;      /* a function name such as sin */
} Atom;

struct ExMath {
    ExFont *font;
    ExMathItem *items;
    size_t n;
    float w, h, d;
    int error;
};

typedef struct Ctx {
    ExFont *font;
    hb_font_t *hf;
    int upem;
    ExMathItem *pool;
    size_t n, cap;
    float k[80];
    const char *p, *end;
    int err, depth, term, alpha;
} Ctx;

/* ---- symbols ---- */
typedef struct Sym { const char *name; uint32_t cp; unsigned char cls, flags; } Sym;
#define F_BIGOP 1
#define F_LIMITS 2
static const Sym symtab[] = {
    /* greek, lower case (italic) */
    {"alpha", 0x3B1, C_ORD, 0}, {"beta", 0x3B2, C_ORD, 0}, {"gamma", 0x3B3, C_ORD, 0}, {"delta", 0x3B4, C_ORD, 0}, {"epsilon", 0x3F5, C_ORD, 0},
    {"varepsilon", 0x3B5, C_ORD, 0}, {"zeta", 0x3B6, C_ORD, 0}, {"eta", 0x3B7, C_ORD, 0}, {"theta", 0x3B8, C_ORD, 0}, {"vartheta", 0x3D1, C_ORD, 0},
    {"iota", 0x3B9, C_ORD, 0}, {"kappa", 0x3BA, C_ORD, 0}, {"lambda", 0x3BB, C_ORD, 0}, {"mu", 0x3BC, C_ORD, 0}, {"nu", 0x3BD, C_ORD, 0},
    {"xi", 0x3BE, C_ORD, 0}, {"omicron", 0x3BF, C_ORD, 0}, {"pi", 0x3C0, C_ORD, 0}, {"varpi", 0x3D6, C_ORD, 0}, {"rho", 0x3C1, C_ORD, 0},
    {"varrho", 0x3F1, C_ORD, 0}, {"sigma", 0x3C3, C_ORD, 0}, {"varsigma", 0x3C2, C_ORD, 0}, {"tau", 0x3C4, C_ORD, 0}, {"upsilon", 0x3C5, C_ORD, 0},
    {"phi", 0x3D5, C_ORD, 0}, {"varphi", 0x3C6, C_ORD, 0}, {"chi", 0x3C7, C_ORD, 0}, {"psi", 0x3C8, C_ORD, 0}, {"omega", 0x3C9, C_ORD, 0},
    /* greek, capitals (upright) */
    {"Gamma", 0x393, C_ORD, 0}, {"Delta", 0x394, C_ORD, 0}, {"Theta", 0x398, C_ORD, 0}, {"Lambda", 0x39B, C_ORD, 0}, {"Xi", 0x39E, C_ORD, 0},
    {"Pi", 0x3A0, C_ORD, 0}, {"Sigma", 0x3A3, C_ORD, 0}, {"Upsilon", 0x3A5, C_ORD, 0}, {"Phi", 0x3A6, C_ORD, 0}, {"Psi", 0x3A8, C_ORD, 0},
    {"Omega", 0x3A9, C_ORD, 0},
    /* binary operators */
    {"pm", 0xB1, C_BIN, 0}, {"mp", 0x2213, C_BIN, 0}, {"times", 0xD7, C_BIN, 0}, {"div", 0xF7, C_BIN, 0}, {"cdot", 0x22C5, C_BIN, 0},
    {"ast", 0x2217, C_BIN, 0}, {"star", 0x22C6, C_BIN, 0}, {"circ", 0x2218, C_BIN, 0}, {"bullet", 0x2219, C_BIN, 0}, {"oplus", 0x2295, C_BIN, 0},
    {"ominus", 0x2296, C_BIN, 0}, {"otimes", 0x2297, C_BIN, 0}, {"odot", 0x2299, C_BIN, 0}, {"cup", 0x222A, C_BIN, 0}, {"cap", 0x2229, C_BIN, 0},
    {"vee", 0x2228, C_BIN, 0}, {"wedge", 0x2227, C_BIN, 0}, {"lor", 0x2228, C_BIN, 0}, {"land", 0x2227, C_BIN, 0}, {"setminus", 0x2216, C_BIN, 0},
    {"uplus", 0x228E, C_BIN, 0}, {"sqcup", 0x2294, C_BIN, 0}, {"sqcap", 0x2293, C_BIN, 0}, {"wr", 0x2240, C_BIN, 0}, {"dagger", 0x2020, C_BIN, 0},
    /* relations */
    {"leq", 0x2264, C_REL, 0}, {"le", 0x2264, C_REL, 0}, {"geq", 0x2265, C_REL, 0}, {"ge", 0x2265, C_REL, 0}, {"neq", 0x2260, C_REL, 0},
    {"ne", 0x2260, C_REL, 0}, {"approx", 0x2248, C_REL, 0}, {"equiv", 0x2261, C_REL, 0}, {"sim", 0x223C, C_REL, 0}, {"simeq", 0x2243, C_REL, 0},
    {"cong", 0x2245, C_REL, 0}, {"propto", 0x221D, C_REL, 0}, {"ll", 0x226A, C_REL, 0}, {"gg", 0x226B, C_REL, 0}, {"prec", 0x227A, C_REL, 0},
    {"succ", 0x227B, C_REL, 0}, {"in", 0x2208, C_REL, 0}, {"notin", 0x2209, C_REL, 0}, {"ni", 0x220B, C_REL, 0}, {"subset", 0x2282, C_REL, 0},
    {"supset", 0x2283, C_REL, 0}, {"subseteq", 0x2286, C_REL, 0}, {"supseteq", 0x2287, C_REL, 0}, {"perp", 0x22A5, C_REL, 0},
    {"parallel", 0x2225, C_REL, 0}, {"mid", 0x2223, C_REL, 0}, {"vdash", 0x22A2, C_REL, 0}, {"models", 0x22A8, C_REL, 0},
    {"to", 0x2192, C_REL, 0}, {"rightarrow", 0x2192, C_REL, 0}, {"leftarrow", 0x2190, C_REL, 0}, {"gets", 0x2190, C_REL, 0},
    {"leftrightarrow", 0x2194, C_REL, 0}, {"uparrow", 0x2191, C_REL, 0}, {"downarrow", 0x2193, C_REL, 0}, {"Rightarrow", 0x21D2, C_REL, 0},
    {"Leftarrow", 0x21D0, C_REL, 0}, {"Leftrightarrow", 0x21D4, C_REL, 0}, {"implies", 0x27F9, C_REL, 0}, {"iff", 0x27FA, C_REL, 0},
    {"mapsto", 0x21A6, C_REL, 0}, {"longrightarrow", 0x27F6, C_REL, 0}, {"longleftarrow", 0x27F5, C_REL, 0}, {"hookrightarrow", 0x21AA, C_REL, 0},
    /* ordinary symbols */
    {"infty", 0x221E, C_ORD, 0}, {"partial", 0x2202, C_ORD, 0}, {"nabla", 0x2207, C_ORD, 0}, {"forall", 0x2200, C_ORD, 0},
    {"exists", 0x2203, C_ORD, 0}, {"emptyset", 0x2205, C_ORD, 0}, {"varnothing", 0x2205, C_ORD, 0}, {"neg", 0xAC, C_ORD, 0}, {"lnot", 0xAC, C_ORD, 0},
    {"prime", 0x2032, C_ORD, 0}, {"hbar", 0x210F, C_ORD, 0}, {"ell", 0x2113, C_ORD, 0}, {"Re", 0x211C, C_ORD, 0}, {"Im", 0x2111, C_ORD, 0},
    {"aleph", 0x2135, C_ORD, 0}, {"angle", 0x2220, C_ORD, 0}, {"degree", 0xB0, C_ORD, 0}, {"top", 0x22A4, C_ORD, 0}, {"bot", 0x22A5, C_ORD, 0},
    {"ldots", 0x2026, C_INNER, 0}, {"dots", 0x2026, C_INNER, 0}, {"cdots", 0x22EF, C_INNER, 0}, {"vdots", 0x22EE, C_ORD, 0}, {"ddots", 0x22F1, C_ORD, 0},
    {"S", 0xA7, C_ORD, 0}, {"P", 0xB6, C_ORD, 0}, {"dag", 0x2020, C_ORD, 0}, {"ddag", 0x2021, C_ORD, 0},
    /* big operators */
    {"sum", 0x2211, C_OP, F_BIGOP | F_LIMITS}, {"prod", 0x220F, C_OP, F_BIGOP | F_LIMITS}, {"coprod", 0x2210, C_OP, F_BIGOP | F_LIMITS},
    {"int", 0x222B, C_OP, F_BIGOP}, {"iint", 0x222C, C_OP, F_BIGOP}, {"iiint", 0x222D, C_OP, F_BIGOP}, {"oint", 0x222E, C_OP, F_BIGOP},
    {"bigcup", 0x22C3, C_OP, F_BIGOP | F_LIMITS}, {"bigcap", 0x22C2, C_OP, F_BIGOP | F_LIMITS}, {"bigvee", 0x22C1, C_OP, F_BIGOP | F_LIMITS},
    {"bigwedge", 0x22C0, C_OP, F_BIGOP | F_LIMITS}, {"bigoplus", 0x2A01, C_OP, F_BIGOP | F_LIMITS}, {"bigotimes", 0x2A02, C_OP, F_BIGOP | F_LIMITS},
    /* delimiters used on their own */
    {"langle", 0x27E8, C_OPEN, 0}, {"rangle", 0x27E9, C_CLOSE, 0}, {"lfloor", 0x230A, C_OPEN, 0}, {"rfloor", 0x230B, C_CLOSE, 0},
    {"lceil", 0x2308, C_OPEN, 0}, {"rceil", 0x2309, C_CLOSE, 0}, {"lbrace", '{', C_OPEN, 0}, {"rbrace", '}', C_CLOSE, 0}, {"vert", '|', C_ORD, 0},
    {"Vert", 0x2016, C_ORD, 0}, {"lbrack", '[', C_OPEN, 0}, {"rbrack", ']', C_CLOSE, 0},
};
#define NSYM (sizeof symtab / sizeof symtab[0])

static const char *const funcs[] = { "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "log", "ln", "lg",
    "exp", "det", "dim", "ker", "gcd", "deg", "arg", "hom", "Pr", "lim", "limsup", "liminf", "max", "min", "sup", "inf", NULL };
static int func_limits(const char *n) { /* lim and the extremes take limits above and below in display style */
    return !strcmp(n, "lim") || !strcmp(n, "limsup") || !strcmp(n, "liminf") || !strcmp(n, "max") || !strcmp(n, "min") || !strcmp(n, "sup") ||
           !strcmp(n, "inf") || !strcmp(n, "Pr") || !strcmp(n, "det") || !strcmp(n, "gcd");
}

/* TeX's table of space between atoms, in mu (1/18 em): row = left class, column = right class. Negative: only outside script styles. */
static const signed char spacing[8][8] = {
    /*        ORD OP BIN REL OPEN CLOSE PUNCT INNER */
    /* ORD */ { 0, 1, -2, -3, 0, 0, 0, -1 },
    /* OP  */ { 1, 1, 0, -3, 0, 0, 0, -1 },
    /* BIN */ { -2, -2, 0, 0, -2, 0, 0, -2 },
    /* REL */ { -3, -3, 0, 0, -3, 0, 0, -3 },
    /* OPEN*/ { 0, 0, 0, 0, 0, 0, 0, 0 },
    /* CLS */ { 0, 1, -2, -3, 0, 0, 0, -1 },
    /* PNCT*/ { -1, -1, 0, -1, -1, -1, -1, -1 },
    /* INNR*/ { -1, 1, -2, -3, -1, 0, -1, -1 },
};

/* ---- the pool ---- */
static int pool_reserve(Ctx *C, size_t extra) {
    if (C->n + extra > MAX_POOL) { C->err = 1; return 0; }
    if (C->n + extra > C->cap) {
        size_t nc = C->cap ? C->cap * 2 : 256;
        ExMathItem *np;
        while (nc < C->n + extra) nc *= 2;
        np = realloc(C->pool, nc * sizeof *np);
        if (!np) { C->err = 1; return 0; }
        C->pool = np; C->cap = nc;
    }
    return 1;
}

static Box bx_empty(Ctx *C) {
    Box b;
    memset(&b, 0, sizeof b);
    b.attach = -1.0f;
    b.s = b.e = C->n;
    return b;
}

static void bx_append(Ctx *C, Box *dst, const Box *src, float dx, float dy) {
    size_t cnt = src->e - src->s, i;
    if (C->err) return;
    if (dst->e == src->s && src->e == C->n) {   /* the box just made lies right after the one it joins: move it in place (a text of n glyphs stays linear) */
        for (i = src->s; i < src->e; i++) { C->pool[i].x += dx; C->pool[i].y += dy; }
        dst->e = src->e;
        return;
    }
    if (dst->e != C->n) { /* not the last range of the pool: copy it to the end first */
        size_t m = dst->e - dst->s;
        if (!pool_reserve(C, m + cnt)) return;
        memmove(C->pool + C->n, C->pool + dst->s, m * sizeof *C->pool);
        dst->s = C->n; dst->e = C->n + m; C->n += m;
    }
    if (!pool_reserve(C, cnt)) return;
    for (i = 0; i < cnt; i++) {
        ExMathItem it = C->pool[src->s + i];
        it.x += dx; it.y += dy;
        C->pool[C->n++] = it;
    }
    dst->e = C->n;
}

static void bx_shift(Ctx *C, Box *b, float dx, float dy) {
    size_t i;
    for (i = b->s; i < b->e; i++) { C->pool[i].x += dx; C->pool[i].y += dy; }
}

static void bx_rule(Ctx *C, Box *b, float x, float y, float w, float h) {
    if (!pool_reserve(C, 1)) return;
    if (b->e != C->n) { Box t = bx_empty(C); bx_append(C, &t, b, 0, 0); t.w = b->w; t.h = b->h; t.d = b->d; t.ital = b->ital; t.attach = b->attach; *b = t; if (C->err) return; if (!pool_reserve(C, 1)) return; }
    C->pool[C->n].kind = EX_MATH_RULE; C->pool[C->n].glyph = 0;
    C->pool[C->n].x = x; C->pool[C->n].y = y; C->pool[C->n].a = w; C->pool[C->n].b = h;
    C->n++;
    b->e = C->n;
}

/* ---- fonts ---- */
static uint32_t gid_of(Ctx *C, uint32_t cp) {
    hb_codepoint_t g = 0;
    if (!hb_font_get_nominal_glyph(C->hf, cp, &g)) return 0;
    return g;
}

static float K(const Ctx *C, int id) { return C->k[id]; }
static float style_scale(const Ctx *C, int st) { int lvl = st >> 1; return lvl <= 1 ? 1.0f : (lvl == 2 ? K(C, HB_OT_MATH_CONSTANT_SCRIPT_PERCENT_SCALE_DOWN) : K(C, HB_OT_MATH_CONSTANT_SCRIPT_SCRIPT_PERCENT_SCALE_DOWN)); }
static int st_make(int lvl, int cramped) { return lvl * 2 + (cramped ? 1 : 0); }
static int st_lvl(int st) { return st >> 1; }
static int st_cramped(int st) { return st & 1; }
static int st_sup(int st) { int l = st_lvl(st); return st_make(l <= 1 ? 2 : 3, st_cramped(st)); }
static int st_sub(int st) { int l = st_lvl(st); return st_make(l <= 1 ? 2 : 3, 1); }
static int st_num(int st) { int l = st_lvl(st); return st_make(l == 0 ? 1 : (l == 1 ? 2 : 3), st_cramped(st)); }
static int st_den(int st) { int l = st_lvl(st); return st_make(l == 0 ? 1 : (l == 1 ? 2 : 3), 1); }
static int st_crampedof(int st) { return st | 1; }

/* A box holding one glyph at pen position 0. */
static Box bx_glyph(Ctx *C, uint32_t gid, float sc) {
    Box b = bx_empty(C);
    hb_glyph_extents_t ex;
    hb_position_t adv = hb_font_get_glyph_h_advance(C->hf, gid);
    float u = sc / (float)C->upem;
    b.w = (float)adv * u;
    if (hb_font_get_glyph_extents(C->hf, gid, &ex)) {
        b.h = (float)ex.y_bearing * u;
        b.d = (float)(-(ex.y_bearing + ex.height)) * u;
        if (b.h < 0) b.h = 0;
        if (b.d < 0) b.d = 0;
    }
    b.ital = (float)hb_ot_math_get_glyph_italics_correction(C->hf, gid) * u;
    {
        hb_position_t a = hb_ot_math_get_glyph_top_accent_attachment(C->hf, gid);
        b.attach = a ? (float)a * u : -1.0f;
    }
    if (pool_reserve(C, 1)) {
        ExMathItem *it = &C->pool[C->n++];
        it->kind = EX_MATH_GLYPH; it->glyph = gid; it->x = 0; it->y = 0; it->a = sc; it->b = 0;
        b.e = C->n;
    }
    return b;
}

static Box bx_space(Ctx *C, float w) { Box b = bx_empty(C); b.w = w; return b; }

/* Text in one font at scale sc (UTF-8). */
static Box bx_text(Ctx *C, const char *s, size_t len, float sc, int alpha_map) {
    Box b = bx_empty(C);
    size_t i = 0;
    (void)alpha_map;
    while (i < len && !C->err) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = c;
        int n = 1;
        if (c >= 0xF0 && i + 3 < len + 0) { cp = ((c & 7u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu); n = 4; }
        else if (c >= 0xE0 && i + 2 < len + 0) { cp = ((c & 15u) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu); n = 3; }
        else if (c >= 0xC0 && i + 1 < len + 0) { cp = ((c & 31u) << 6) | (s[i + 1] & 0x3Fu); n = 2; }
        i += (size_t)n;
        {
            uint32_t g = gid_of(C, cp);
            Box gb;
            if (cp == ' ' && !g) { b.w += 0.25f * sc; continue; }
            if (!g) g = gid_of(C, '?');
            gb = bx_glyph(C, g, sc);
            if (C->err) break;
            bx_append(C, &b, &gb, b.w, 0);
            if (gb.h > b.h) b.h = gb.h;
            if (gb.d > b.d) b.d = gb.d;
            b.w += gb.w;
            b.ital = gb.ital;
        }
    }
    return b;
}

/* ---- mathematical alphabets ---- */
static uint32_t map_alpha(int alpha, uint32_t c) {
    int up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', dg = c >= '0' && c <= '9';
    uint32_t U = 0, L = 0, D = 0;
    if (!up && !lo && !dg) return c;
    switch (alpha) {
    case A_BF: U = 0x1D400; L = 0x1D41A; D = 0x1D7CE; break;
    case A_IT: U = 0x1D434; L = 0x1D44E; if (c == 'h') return 0x210E; break;
    case A_BFIT: U = 0x1D468; L = 0x1D482; break;
    case A_CAL: U = 0x1D49C; L = 0x1D4B6;
        switch (c) { case 'B': return 0x212C; case 'E': return 0x2130; case 'F': return 0x2131; case 'H': return 0x210B; case 'I': return 0x2110;
                     case 'L': return 0x2112; case 'M': return 0x2133; case 'R': return 0x211B; case 'e': return 0x212F; case 'g': return 0x210A;
                     case 'o': return 0x2134; default: break; }
        break;
    case A_FRAK: U = 0x1D504; L = 0x1D51E;
        switch (c) { case 'C': return 0x212D; case 'H': return 0x210C; case 'I': return 0x2111; case 'R': return 0x211C; case 'Z': return 0x2128; default: break; }
        break;
    case A_BB: U = 0x1D538; L = 0x1D552; D = 0x1D7D8;
        switch (c) { case 'C': return 0x2102; case 'H': return 0x210D; case 'N': return 0x2115; case 'P': return 0x2119; case 'Q': return 0x211A;
                     case 'R': return 0x211D; case 'Z': return 0x2124; default: break; }
        break;
    case A_SF: U = 0x1D5A0; L = 0x1D5BA; D = 0x1D7E2; break;
    case A_TT: U = 0x1D670; L = 0x1D68A; D = 0x1D7F6; break;
    default: return c;
    }
    if (up && U) return U + (c - 'A');
    if (lo && L) return L + (c - 'a');
    if (dg && D) return D + (c - '0');
    return c;
}

/* Greek in math italic: alpha..omega are one block starting at U+1D6FC (final sigma included), the variant forms follow at U+1D716. */
static uint32_t greek_italic(uint32_t cp) {
    if (cp >= 0x3B1 && cp <= 0x3C9) return 0x1D6FC + (cp - 0x3B1);
    switch (cp) { case 0x3F5: return 0x1D716; case 0x3D1: return 0x1D717; case 0x3F0: return 0x1D718; case 0x3D5: return 0x1D719; case 0x3F1: return 0x1D71A; case 0x3D6: return 0x1D71B; default: return cp; }
}

/* ---- parsing helpers ---- */
static void skip_ws(Ctx *C) { while (C->p < C->end && (*C->p == ' ' || *C->p == '\t' || *C->p == '\n' || *C->p == '\r')) C->p++; }

static int read_name(Ctx *C, char *out, size_t cap) { /* after a backslash: letters, or one other character */
    size_t n = 0;
    if (C->p >= C->end) return 0;
    if ((*C->p >= 'a' && *C->p <= 'z') || (*C->p >= 'A' && *C->p <= 'Z')) {
        while (C->p < C->end && ((*C->p >= 'a' && *C->p <= 'z') || (*C->p >= 'A' && *C->p <= 'Z'))) { if (n + 1 < cap) out[n++] = *C->p; C->p++; }
    } else { out[n++] = *C->p++; }
    out[n] = 0;
    return 1;
}

static int read_braced_name(Ctx *C, char *out, size_t cap) { /* {name} */
    size_t n = 0;
    skip_ws(C);
    if (C->p >= C->end || *C->p != '{') { C->err = 1; return 0; }
    C->p++;
    while (C->p < C->end && *C->p != '}') { if (n + 1 < cap) out[n++] = *C->p; C->p++; }
    if (C->p >= C->end) { C->err = 1; return 0; }
    C->p++;
    out[n] = 0;
    return 1;
}

static Box parse_atoms(Ctx *C, int st);
static Box parse_arg(Ctx *C, int st);

/* ---- stretchy glyphs ---- */
typedef struct Var { uint32_t gid; float size; } Var;

/* A vertical (or horizontal) stretchy glyph at least `target` em long at scale sc: a size variant or an assembly. Vertical results have their baseline at the
 * bottom for assemblies and at the glyph's own baseline for single glyphs: use h and d. */
static Box make_stretch(Ctx *C, uint32_t gid, float target, int vertical, float sc) {
    hb_direction_t dir = vertical ? HB_DIRECTION_TTB : HB_DIRECTION_LTR;
    hb_ot_math_glyph_variant_t vars[24];
    hb_ot_math_glyph_part_t parts[16];
    unsigned nv = 24, np = 16, total, i, best = 0;
    float u = sc / (float)C->upem;
    Box b;
    int have = 0;
    total = hb_ot_math_get_glyph_variants(C->hf, gid, dir, 0, &nv, vars);
    (void)total;
    for (i = 0; i < nv; i++) {
        if ((float)vars[i].advance * u >= target) { best = i; have = 1; break; }
        best = i;
    }
    if (nv == 0) return bx_glyph(C, gid, sc);
    if (have || !hb_ot_math_get_glyph_assembly(C->hf, gid, dir, 0, &np, parts, NULL) || np == 0) {
        Box g = bx_glyph(C, vars[best].glyph, sc);
        return g;
    }
    {   /* an assembly of parts, extenders repeated until the length reaches the target */
        hb_position_t overlap = hb_ot_math_get_min_connector_overlap(C->hf, dir);
        unsigned nparts = 0, r, j, seqlen;
        unsigned seq[64];
        float pos = 0.0f;
        int has_ext = 0;
        np = hb_ot_math_get_glyph_assembly(C->hf, gid, dir, 0, &np, parts, NULL) ? np : 0;
        for (j = 0; j < np; j++) if (parts[j].flags & HB_OT_MATH_GLYPH_PART_FLAG_EXTENDER) has_ext = 1;
        for (r = 0; r <= 24; r++) {
            float len = 0.0f;
            nparts = 0;
            for (j = 0; j < np; j++) {
                unsigned rep = (parts[j].flags & HB_OT_MATH_GLYPH_PART_FLAG_EXTENDER) ? r : 1;
                unsigned q;
                for (q = 0; q < rep && nparts < 64; q++) seq[nparts++] = j;
            }
            seqlen = nparts;
            for (j = 0; j < seqlen; j++) len += (float)parts[seq[j]].full_advance;
            if (seqlen > 1) len -= (float)overlap * (float)(seqlen - 1);
            if (len * u >= target || !has_ext) break;
        }
        b = bx_empty(C);
        for (j = 0; j < nparts; j++) {
            hb_glyph_extents_t ex;
            const hb_ot_math_glyph_part_t *pt = &parts[seq[j]];
            float inkbottom = 0.0f;
            Box g;
            if (hb_font_get_glyph_extents(C->hf, pt->glyph, &ex)) inkbottom = (float)(ex.y_bearing + ex.height);
            g = bx_glyph(C, pt->glyph, sc);
            bx_append(C, &b, &g, 0, -(pos - inkbottom) * u);
            if (g.w > b.w) b.w = g.w;
            pos += (float)pt->full_advance - (j + 1 < nparts ? (float)overlap : 0.0f);
        }
        b.h = pos * u;
        b.d = 0;
        return b;
    }
}

static void center_on_axis(Ctx *C, Box *b, float sc) {
    float dy = (b->h - b->d) / 2.0f - K(C, HB_OT_MATH_CONSTANT_AXIS_HEIGHT) * sc;
    bx_shift(C, b, 0, dy);
    b->h -= dy; b->d += dy;
    if (b->h < 0) b->h = 0;
    if (b->d < 0) b->d = 0;
}

/* A delimiter for content of height h and depth d around the axis (TeX's rule: at least 901/1000 of twice the larger half, and not less than the
 * content minus a short fall). cp 0 is the empty delimiter. */
static Box make_delim(Ctx *C, uint32_t cp, float h, float d, float sc) {
    float axis = K(C, HB_OT_MATH_CONSTANT_AXIS_HEIGHT) * sc, delta = fmaxf(h - axis, d + axis), target, minh;
    uint32_t gid;
    Box b;
    if (!cp) { b = bx_empty(C); b.w = 0.12f * sc; return b; }
    gid = gid_of(C, cp);
    if (!gid) { b = bx_empty(C); return b; }
    minh = K(C, HB_OT_MATH_CONSTANT_DELIMITED_SUB_FORMULA_MIN_HEIGHT) * sc;
    target = fmaxf(delta / 500.0f * 901.0f, 2.0f * delta - 0.5f * sc);
    if (target < minh) target = minh;
    b = make_stretch(C, gid, target, 1, sc);
    center_on_axis(C, &b, sc);
    b.ital = 0;
    return b;
}

/* ---- joining atoms ---- */
static Box join_atoms(Ctx *C, Atom *a, size_t n, int st) {
    Box r = bx_empty(C);
    size_t i;
    int prev = C_NONE, script = st_lvl(st) >= 2;
    float pen = 0.0f, sc = style_scale(C, st);
    /* binary operators that have no left operand, or no right operand, are ordinary */
    for (i = 0; i < n; i++) {
        int j;
        if (a[i].glue) continue;
        if (a[i].cls == C_BIN) {
            int pc = C_NONE;
            for (j = (int)i - 1; j >= 0; j--) if (!a[j].glue) { pc = a[j].cls; break; }
            if (pc == C_NONE || pc == C_BIN || pc == C_OP || pc == C_REL || pc == C_OPEN || pc == C_PUNCT) a[i].cls = C_ORD;
        } else if (a[i].cls == C_REL || a[i].cls == C_CLOSE || a[i].cls == C_PUNCT) {
            for (j = (int)i - 1; j >= 0; j--) if (!a[j].glue) { if (a[j].cls == C_BIN) a[j].cls = C_ORD; break; }
        }
    }
    for (i = n; i > 0; i--) { if (a[i - 1].glue) continue; if (a[i - 1].cls == C_BIN) a[i - 1].cls = C_ORD; break; }
    for (i = 0; i < n && !C->err; i++) {
        if (!a[i].glue) {
            if (prev != C_NONE) {
                int sp = spacing[prev][a[i].cls];
                if (sp < 0) sp = script ? 0 : -sp;
                pen += (float)(sp == 0 ? 0 : (sp == 1 ? 3 : (sp == 2 ? 4 : 5))) / 18.0f * sc;   /* thin, medium, thick: 3, 4 and 5 mu */
            }
            prev = a[i].cls;
        }
        bx_append(C, &r, &a[i].b, pen, 0);
        if (a[i].b.h > r.h) r.h = a[i].b.h;
        if (a[i].b.d > r.d) r.d = a[i].b.d;
        pen += a[i].b.w;
        r.ital = a[i].glue ? r.ital : a[i].b.ital;
    }
    r.w = pen;
    return r;
}

/* ---- scripts (TeX rule 18, with the MATH constants) ---- */
static Box with_scripts(Ctx *C, int st, Atom *nuc, const Box *sup, const Box *sub) {
    float sc = style_scale(C, st), scs = style_scale(C, st_sup(st));
    float u = 0, v = 0, kern = K(C, HB_OT_MATH_CONSTANT_SPACE_AFTER_SCRIPT) * scs;
    Box r = bx_empty(C);
    const Box *N = &nuc->b;
    if (!nuc->single) {
        u = N->h - K(C, HB_OT_MATH_CONSTANT_SUPERSCRIPT_BASELINE_DROP_MAX) * scs;
        v = N->d + K(C, HB_OT_MATH_CONSTANT_SUBSCRIPT_BASELINE_DROP_MIN) * scs;
    }
    if (sup) {
        float up = K(C, st_cramped(st) ? HB_OT_MATH_CONSTANT_SUPERSCRIPT_SHIFT_UP_CRAMPED : HB_OT_MATH_CONSTANT_SUPERSCRIPT_SHIFT_UP) * sc;
        if (u < up) u = up;
        if (u < sup->d + K(C, HB_OT_MATH_CONSTANT_SUPERSCRIPT_BOTTOM_MIN) * sc) u = sup->d + K(C, HB_OT_MATH_CONSTANT_SUPERSCRIPT_BOTTOM_MIN) * sc;
    }
    if (sub) {
        float dn = K(C, HB_OT_MATH_CONSTANT_SUBSCRIPT_SHIFT_DOWN) * sc;
        if (v < dn) v = dn;
        if (!sup && v < sub->h - K(C, HB_OT_MATH_CONSTANT_SUBSCRIPT_TOP_MAX) * sc) v = sub->h - K(C, HB_OT_MATH_CONSTANT_SUBSCRIPT_TOP_MAX) * sc;
    }
    if (sup && sub) {
        float gap = (u - sup->d) - (sub->h - v), mingap = K(C, HB_OT_MATH_CONSTANT_SUB_SUPERSCRIPT_GAP_MIN) * sc, psi;
        if (gap < mingap) { v += mingap - gap; }
        psi = K(C, HB_OT_MATH_CONSTANT_SUPERSCRIPT_BOTTOM_MAX_WITH_SUBSCRIPT) * sc - (u - sup->d);
        if (psi > 0) { u += psi; v -= psi; }
    }
    bx_append(C, &r, N, 0, 0);
    r.h = N->h; r.d = N->d;
    if (sup) {
        bx_append(C, &r, sup, N->w + N->ital, -u);
        if (sup->h + u > r.h) r.h = sup->h + u;
        if (sup->d - u > r.d) r.d = sup->d - u;
    }
    if (sub) {
        bx_append(C, &r, sub, N->w, v);
        if (sub->h - v > r.h) r.h = sub->h - v;
        if (sub->d + v > r.d) r.d = sub->d + v;
    }
    {
        float w1 = sup ? N->w + N->ital + sup->w : 0.0f, w2 = sub ? N->w + sub->w : 0.0f;
        r.w = fmaxf(N->w, fmaxf(w1, w2)) + kern;
    }
    r.ital = 0;
    return r;
}

/* Limits above and below a big operator (rule 13a). */
static Box with_limits(Ctx *C, int st, Atom *nuc, const Box *sup, const Box *sub) {
    float sc = style_scale(C, st);
    float w = nuc->b.w;
    Box r = bx_empty(C);
    float ni = nuc->b.ital;
    float wu = sup ? sup->w : 0, wl = sub ? sub->w : 0;
    if (wu > w) w = wu;
    if (wl > w) w = wl;
    bx_append(C, &r, &nuc->b, (w - nuc->b.w) / 2.0f, 0);
    r.h = nuc->b.h; r.d = nuc->b.d;
    if (sup) {
        float gap = fmaxf(K(C, HB_OT_MATH_CONSTANT_UPPER_LIMIT_GAP_MIN) * sc, K(C, HB_OT_MATH_CONSTANT_UPPER_LIMIT_BASELINE_RISE_MIN) * sc - sup->d);
        float y = nuc->b.h + gap + sup->d;
        bx_append(C, &r, sup, (w - sup->w) / 2.0f + ni / 2.0f, -y);
        r.h = y + sup->h;
    }
    if (sub) {
        float gap = fmaxf(K(C, HB_OT_MATH_CONSTANT_LOWER_LIMIT_GAP_MIN) * sc, K(C, HB_OT_MATH_CONSTANT_LOWER_LIMIT_BASELINE_DROP_MIN) * sc - sub->h);
        float y = nuc->b.d + gap + sub->h;
        bx_append(C, &r, sub, (w - sub->w) / 2.0f - ni / 2.0f, y);
        r.d = y + sub->d;
    }
    r.w = w;
    return r;
}

/* ---- fractions, roots, accents, bars ---- */
static Box make_frac(Ctx *C, int st, int rule, int forced_style) {
    int fs = forced_style >= 0 ? forced_style : st, nst, dst;
    float sc = style_scale(C, fs), t, axis, u, v, gn, gd, pad;
    Box num, den, r;
    int disp = st_lvl(fs) == 0;
    nst = st_num(fs); dst = st_den(fs);
    num = parse_arg(C, nst);
    den = parse_arg(C, dst);
    if (C->err) return bx_empty(C);
    axis = K(C, HB_OT_MATH_CONSTANT_AXIS_HEIGHT) * sc;
    t = rule ? K(C, HB_OT_MATH_CONSTANT_FRACTION_RULE_THICKNESS) * sc : 0.0f;
    pad = 0.06f * sc;
    if (rule) {
        u = K(C, disp ? HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_DISPLAY_STYLE_SHIFT_UP : HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_SHIFT_UP) * sc;
        v = K(C, disp ? HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_DISPLAY_STYLE_SHIFT_DOWN : HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_SHIFT_DOWN) * sc;
        gn = K(C, disp ? HB_OT_MATH_CONSTANT_FRACTION_NUM_DISPLAY_STYLE_GAP_MIN : HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_GAP_MIN) * sc;
        gd = K(C, disp ? HB_OT_MATH_CONSTANT_FRACTION_DENOM_DISPLAY_STYLE_GAP_MIN : HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_GAP_MIN) * sc;
        {
            float c1 = (u - num.d) - (axis + t / 2.0f), c2 = (axis - t / 2.0f) - (den.h - v);
            if (c1 < gn) u += gn - c1;
            if (c2 < gd) v += gd - c2;
        }
    } else {
        float gap = K(C, disp ? HB_OT_MATH_CONSTANT_STACK_DISPLAY_STYLE_GAP_MIN : HB_OT_MATH_CONSTANT_STACK_GAP_MIN) * sc, c;
        u = K(C, disp ? HB_OT_MATH_CONSTANT_STACK_TOP_DISPLAY_STYLE_SHIFT_UP : HB_OT_MATH_CONSTANT_STACK_TOP_SHIFT_UP) * sc;
        v = K(C, disp ? HB_OT_MATH_CONSTANT_STACK_BOTTOM_DISPLAY_STYLE_SHIFT_DOWN : HB_OT_MATH_CONSTANT_STACK_BOTTOM_SHIFT_DOWN) * sc;
        c = (u - num.d) - (den.h - v);
        if (c < gap) { float e = (gap - c) / 2.0f; u += e; v += e; }
    }
    r = bx_empty(C);
    {
        float w = fmaxf(num.w, den.w);
        bx_append(C, &r, &num, pad + (w - num.w) / 2.0f, -u);
        bx_append(C, &r, &den, pad + (w - den.w) / 2.0f, v);
        if (rule) bx_rule(C, &r, pad, -(axis + t / 2.0f), w, t);
        r.w = w + 2.0f * pad;
    }
    r.h = u + num.h;
    r.d = v + den.d;
    if (rule) { if (axis + t / 2.0f > r.h) r.h = axis + t / 2.0f; }
    return r;
}

static Box make_sqrt(Ctx *C, int st) {
    float sc = style_scale(C, st), theta, phi, need, gap;
    Box rad, sign, r, deg;
    int has_deg = 0;
    uint32_t gid = gid_of(C, 0x221A);
    skip_ws(C);
    deg = bx_empty(C);
    if (C->p < C->end && *C->p == '[') {
        const char *q = C->p + 1;
        int depth = 0;
        const char *s0 = q;
        while (q < C->end && !(*q == ']' && depth == 0)) { if (*q == '{') depth++; else if (*q == '}') depth--; q++; }
        if (q >= C->end) { C->err = 1; return bx_empty(C); }
        {
            const char *save_end = C->end;
            C->p = s0; C->end = q;
            deg = parse_atoms(C, st_make(3, 0));
            C->end = save_end;
            if (C->term != T_END) C->err = 1;
            C->p = q + 1;
        }
        has_deg = 1;
    }
    rad = parse_arg(C, st_crampedof(st));
    if (C->err || !gid) return bx_empty(C);
    theta = K(C, HB_OT_MATH_CONSTANT_RADICAL_RULE_THICKNESS) * sc;
    phi = K(C, st_lvl(st) == 0 ? HB_OT_MATH_CONSTANT_RADICAL_DISPLAY_STYLE_VERTICAL_GAP : HB_OT_MATH_CONSTANT_RADICAL_VERTICAL_GAP) * sc;
    need = rad.h + rad.d + phi + theta;
    sign = make_stretch(C, gid, need, 1, sc);
    /* the sign hangs so that its top meets the top of the rule; if it is deeper than the content, the extra is shared */
    gap = (sign.h + sign.d) - need;
    if (gap > 0) phi += gap / 2.0f;
    {
        float top = rad.h + phi + theta;
        float dy = sign.h - top;     /* down shift of the sign so its top is at `top` */
        float kb = has_deg ? K(C, HB_OT_MATH_CONSTANT_RADICAL_KERN_BEFORE_DEGREE) * sc : 0.0f;
        float ka = has_deg ? K(C, HB_OT_MATH_CONSTANT_RADICAL_KERN_AFTER_DEGREE) * sc : 0.0f;
        float x0 = 0.0f, degtop = 0.0f;
        r = bx_empty(C);
        if (has_deg) {
            float H = sign.h + sign.d;
            float y = (sign.d + dy) - K(C, HB_OT_MATH_CONSTANT_RADICAL_DEGREE_BOTTOM_RAISE_PERCENT) * H - deg.d;   /* the degree's bottom at 60% of the sign */
            x0 = kb + deg.w + ka;
            if (x0 < 0) x0 = 0;
            bx_append(C, &r, &deg, kb, y);
            degtop = -y + deg.h;
        }
        bx_append(C, &r, &sign, x0, dy);
        bx_append(C, &r, &rad, x0 + sign.w, 0);
        bx_rule(C, &r, x0 + sign.w, -top, rad.w, theta);
        r.w = x0 + sign.w + rad.w;
        r.h = fmaxf(top, degtop);
        r.d = fmaxf(rad.d, sign.d + dy);
        if (r.d < 0) r.d = 0;
    }
    return r;
}

/* A box over a base: an accent glyph (a combining mark placed by its ink) or, for 'wide', a stretched one. */
static Box make_accent(Ctx *C, int st, uint32_t cp, int wide) {
    float sc = style_scale(C, st);
    Box base = parse_arg(C, st_crampedof(st)), r, ac;
    uint32_t gid = gid_of(C, cp);
    hb_glyph_extents_t ex;
    float u = sc / (float)C->upem, ink_cx, base_cx, shift;
    if (C->err) return bx_empty(C);
    if (!gid) return base;
    if (wide) ac = make_stretch(C, gid, base.w, 0, sc); else ac = bx_glyph(C, gid, sc);
    {   /* horizontal stretch picks a variant glyph: find its id from the item to measure the ink */
        uint32_t g2 = C->pool[ac.s].glyph;
        if (hb_font_get_glyph_extents(C->hf, g2, &ex)) ink_cx = ((float)ex.x_bearing + (float)ex.width / 2.0f) * u; else ink_cx = ac.w / 2.0f;
    }
    base_cx = base.attach >= 0 ? base.attach : base.w / 2.0f;
    shift = base_cx - ink_cx;
    r = bx_empty(C);
    bx_append(C, &r, &base, 0, 0);
    {
        float raise = base.h - K(C, HB_OT_MATH_CONSTANT_ACCENT_BASE_HEIGHT) * sc;
        if (raise < 0) raise = 0;
        bx_append(C, &r, &ac, shift, -raise);
        r.h = fmaxf(base.h, ac.h + raise);
    }
    r.w = base.w; r.d = base.d; r.ital = base.ital;
    return r;
}

static Box make_bar(Ctx *C, int st, int over) {
    float sc = style_scale(C, st);
    Box base = parse_arg(C, over ? st_crampedof(st) : st), r;
    float t = K(C, over ? HB_OT_MATH_CONSTANT_OVERBAR_RULE_THICKNESS : HB_OT_MATH_CONSTANT_UNDERBAR_RULE_THICKNESS) * sc;
    float gap = K(C, over ? HB_OT_MATH_CONSTANT_OVERBAR_VERTICAL_GAP : HB_OT_MATH_CONSTANT_UNDERBAR_VERTICAL_GAP) * sc;
    if (C->err) return bx_empty(C);
    r = bx_empty(C);
    bx_append(C, &r, &base, 0, 0);
    if (over) {
        bx_rule(C, &r, 0, -(base.h + gap + t), base.w, t);
        r.h = base.h + gap + t + K(C, HB_OT_MATH_CONSTANT_OVERBAR_EXTRA_ASCENDER) * sc; r.d = base.d;
    } else {
        bx_rule(C, &r, 0, base.d + gap, base.w, t);
        r.d = base.d + gap + t + K(C, HB_OT_MATH_CONSTANT_UNDERBAR_EXTRA_DESCENDER) * sc; r.h = base.h;
    }
    r.w = base.w;
    return r;
}

/* \overset{a}{b} (over) and \underset{a}{b} (under): a small box over or under b. */
static Box make_stack(Ctx *C, int st, int over) {
    float sc = style_scale(C, st);
    Box a = parse_arg(C, st_sup(st)), b = parse_arg(C, st), r;
    float w, gap;
    if (C->err) return bx_empty(C);
    w = fmaxf(a.w, b.w);
    gap = K(C, over ? HB_OT_MATH_CONSTANT_UPPER_LIMIT_GAP_MIN : HB_OT_MATH_CONSTANT_LOWER_LIMIT_GAP_MIN) * sc;
    r = bx_empty(C);
    bx_append(C, &r, &b, (w - b.w) / 2.0f, 0);
    r.h = b.h; r.d = b.d;
    if (over) { float y = b.h + gap + a.d; bx_append(C, &r, &a, (w - a.w) / 2.0f, -y); r.h = y + a.h; }
    else { float y = b.d + gap + a.h; bx_append(C, &r, &a, (w - a.w) / 2.0f, y); r.d = y + a.d; }
    r.w = w;
    return r;
}

/* ---- matrices and cases ---- */
static uint32_t delim_cp(const char *s) {
    if (!strcmp(s, ".")) return 0;
    if (!strcmp(s, "(") || !strcmp(s, ")") || !strcmp(s, "[") || !strcmp(s, "]") || !strcmp(s, "/") || !strcmp(s, "|")) return (unsigned char)s[0];
    if (!strcmp(s, "\\{") || !strcmp(s, "\\lbrace")) return '{';
    if (!strcmp(s, "\\}") || !strcmp(s, "\\rbrace")) return '}';
    if (!strcmp(s, "\\|") || !strcmp(s, "\\Vert")) return 0x2016;
    if (!strcmp(s, "\\vert")) return '|';
    if (!strcmp(s, "\\langle")) return 0x27E8;
    if (!strcmp(s, "\\rangle")) return 0x27E9;
    if (!strcmp(s, "\\lfloor")) return 0x230A;
    if (!strcmp(s, "\\rfloor")) return 0x230B;
    if (!strcmp(s, "\\lceil")) return 0x2308;
    if (!strcmp(s, "\\rceil")) return 0x2309;
    return 0xFFFFFFFFu;
}

static int read_delim(Ctx *C, uint32_t *cp) {
    char buf[64];
    skip_ws(C);
    if (C->p >= C->end) { C->err = 1; return 0; }
    if (*C->p == '\\') {
        char name[32];
        C->p++;
        if (!read_name(C, name, sizeof name)) { C->err = 1; return 0; }
        snprintf(buf, sizeof buf, "\\%s", name);
    } else { buf[0] = *C->p++; buf[1] = 0; }
    *cp = delim_cp(buf);
    if (*cp == 0xFFFFFFFFu) { C->err = 1; return 0; }
    return 1;
}

typedef struct EnvKind { const char *name; uint32_t l, r; int align; /* 0 centered, 1 left, 2 right-left pairs */ } EnvKind;
static const EnvKind envs[] = {
    {"matrix", 0, 0, 0}, {"pmatrix", '(', ')', 0}, {"bmatrix", '[', ']', 0}, {"Bmatrix", '{', '}', 0}, {"vmatrix", '|', '|', 0},
    {"Vmatrix", 0x2016, 0x2016, 0}, {"cases", '{', 0, 1}, {"aligned", 0, 0, 2}, {"smallmatrix", 0, 0, 0},
};

static Box make_env(Ctx *C, int st) {
    char name[32];
    const EnvKind *ek = NULL;
    size_t i, nrows = 0, rcap = 0, ncols = 0;
    Box *cells = NULL;
    size_t *rowlen = NULL;
    size_t ncells = 0, ccap = 0;
    Box result = bx_empty(C);
    float sc = style_scale(C, st), colgap, rowh;
    int cst = st_lvl(st) == 0 ? st_make(1, st_cramped(st)) : st;
    int done = 0;
    if (!read_braced_name(C, name, sizeof name)) return result;
    for (i = 0; i < sizeof envs / sizeof envs[0]; i++) if (!strcmp(envs[i].name, name)) ek = &envs[i];
    if (!ek) { C->err = 1; return result; }
    rowlen = NULL;
    while (!done && !C->err) {
        Box cell = parse_atoms(C, cst);
        int term = C->term;
        if (C->err) break;
        if (ncells == ccap) { size_t nc = ccap ? ccap * 2 : 16; Box *t = realloc(cells, nc * sizeof *t); if (!t) { C->err = 1; break; } cells = t; ccap = nc; }
        cells[ncells++] = cell;
        if (nrows == rcap) { size_t nc = rcap ? rcap * 2 : 8; size_t *t = realloc(rowlen, nc * sizeof *t); if (!t) { C->err = 1; break; } rowlen = t; rcap = nc; }
        if (nrows == 0 || rowlen[nrows - 1] == (size_t)-1) { rowlen[nrows] = 0; nrows++; }
        rowlen[nrows - 1]++;
        if (term == T_AMP) continue;
        if (term == T_ROWSEP) { rowlen[nrows - 1] = (rowlen[nrows - 1]); /* the next cell opens a row */ if (nrows == rcap) { size_t nc = rcap * 2; size_t *t = realloc(rowlen, nc * sizeof *t); if (!t) { C->err = 1; break; } rowlen = t; rcap = nc; } rowlen[nrows] = 0; nrows++; continue; }
        if (term == T_ENDENV) { done = 1; break; }
        C->err = 1;
    }
    if (!C->err) {
        char endname[32];
        if (!read_braced_name(C, endname, sizeof endname) || strcmp(endname, name)) C->err = 1;
    }
    if (!C->err && nrows > 0 && rowlen[nrows - 1] == 1 && cells[ncells - 1].e == cells[ncells - 1].s && cells[ncells - 1].w == 0 && nrows > 1) { nrows--; ncells--; } /* a trailing \\ */
    if (!C->err) {
        size_t r, c, idx = 0;
        float *colw, *rh, *rd, y = 0, total_h, total_w = 0;
        float *colx;
        for (r = 0; r < nrows; r++) if (rowlen[r] > ncols) ncols = rowlen[r];
        colw = calloc(ncols ? ncols : 1, sizeof *colw);
        colx = calloc(ncols ? ncols : 1, sizeof *colx);
        rh = calloc(nrows ? nrows : 1, sizeof *rh);
        rd = calloc(nrows ? nrows : 1, sizeof *rd);
        if (!colw || !colx || !rh || !rd) { C->err = 1; }
        else {
            colgap = ek->align == 2 ? 0.0f : 1.0f * sc;
            rowh = 1.2f * sc;
            for (r = 0; r < nrows; r++) {
                for (c = 0; c < rowlen[r]; c++) {
                    Box *cb = &cells[idx + c];
                    if (cb->w > colw[c]) colw[c] = cb->w;
                    rh[r] = fmaxf(rh[r], cb->h);
                    rd[r] = fmaxf(rd[r], cb->d);
                }
                idx += rowlen[r];
                rh[r] = fmaxf(rh[r], 0.7f * sc);
                rd[r] = fmaxf(rd[r], 0.3f * sc);
            }
            for (c = 0; c < ncols; c++) {
                colx[c] = total_w;
                total_w += colw[c] + (c + 1 < ncols ? (ek->align == 2 ? ((c % 2) ? 1.0f * sc : 0.0f) : colgap) : 0.0f);
            }
            /* rows */
            idx = 0;
            for (r = 0; r < nrows; r++) {
                float pitch = 0;
                if (r > 0) pitch = fmaxf(rowh, rd[r - 1] + rh[r] + 0.1f * sc) - (rd[r - 1] + rh[r]);
                y += (r > 0 ? rd[r - 1] + rh[r] + pitch : rh[r]);
                for (c = 0; c < rowlen[r]; c++) {
                    Box *cb = &cells[idx + c];
                    float dx = 0;
                    if (ek->align == 0) dx = (colw[c] - cb->w) / 2.0f;
                    else if (ek->align == 2 && (c % 2) == 0) dx = colw[c] - cb->w;
                    bx_append(C, &result, cb, colx[c] + dx, y);
                }
                idx += rowlen[r];
            }
            total_h = y + rd[nrows - 1];
            result.w = total_w;
            /* centered on the axis */
            {
                float up = total_h / 2.0f + K(C, HB_OT_MATH_CONSTANT_AXIS_HEIGHT) * sc;
                bx_shift(C, &result, 0, -(up - 0.0f) + 0.0f);
                /* after the shift row baselines are y - up; the box spans from -up to total_h - up */
                result.h = up; result.d = total_h - up;
                if (result.d < 0) result.d = 0;
            }
            if (ek->l || ek->r) {
                Box l = make_delim(C, ek->l, result.h, result.d, sc), rr = make_delim(C, ek->r, result.h, result.d, sc), out = bx_empty(C);
                float lw = ek->l ? l.w + 0.1f * sc : 0.0f;
                if (ek->l) bx_append(C, &out, &l, 0, 0);
                bx_append(C, &out, &result, lw, 0);
                if (ek->r) bx_append(C, &out, &rr, lw + result.w + 0.1f * sc, 0);
                out.w = lw + result.w + (ek->r ? 0.1f * sc + rr.w : 0.0f);
                out.h = fmaxf(result.h, fmaxf(l.h, rr.h)); out.d = fmaxf(result.d, fmaxf(l.d, rr.d));
                result = out;
            } else if (ek->align == 1 || ek->align == 2) { /* cases and aligned have no right delimiter */ }
        }
        free(colw); free(colx); free(rh); free(rd);
    }
    free(cells);
    free(rowlen);
    return result;
}

/* ---- one nucleus ---- */
static Atom atom_of(Box b, int cls) { Atom a; memset(&a, 0, sizeof a); a.b = b; a.cls = cls; return a; }

static const Sym *find_sym(const char *name) {
    size_t i;
    for (i = 0; i < NSYM; i++) if (!strcmp(symtab[i].name, name)) return &symtab[i];
    return NULL;
}

static Atom symbol_atom(Ctx *C, int st, const Sym *sy) {
    float sc = style_scale(C, st);
    uint32_t cp = sy->cp, gid;
    Atom a;
    if (sy->cls == C_ORD && cp >= 0x3B1 && cp <= 0x3F6 && C->alpha == A_DEFAULT) cp = greek_italic(cp);
    gid = gid_of(C, cp);
    if (!gid) gid = gid_of(C, sy->cp);
    if (sy->flags & F_BIGOP) {
        Box b;
        if (st_lvl(st) == 0 && gid) {
            hb_ot_math_glyph_variant_t vars[16];
            unsigned nv = 16, i;
            float minh = K(C, HB_OT_MATH_CONSTANT_DISPLAY_OPERATOR_MIN_HEIGHT);
            uint32_t pick = gid;
            hb_ot_math_get_glyph_variants(C->hf, gid, HB_DIRECTION_TTB, 0, &nv, vars);
            for (i = 0; i < nv; i++) { pick = vars[i].glyph; if ((float)vars[i].advance / (float)C->upem >= minh) break; }
            b = bx_glyph(C, pick, sc);
        } else b = bx_glyph(C, gid, sc);
        center_on_axis(C, &b, sc);
        a = atom_of(b, C_OP);
        a.bigop = 1;
        a.limdefault = (sy->flags & F_LIMITS) != 0;
        return a;
    }
    {
        Box b = gid ? bx_glyph(C, gid, sc) : bx_empty(C);
        a = atom_of(b, sy->cls);
        a.single = 1;
        if (sy->cls == C_ORD && cp >= 0x1D6FC) { /* italic greek: has an italic correction */ }
    }
    return a;
}

static Atom char_atom(Ctx *C, int st, unsigned char ch) {
    float sc = style_scale(C, st);
    uint32_t cp = ch;
    int cls = C_ORD;
    Box b;
    Atom a;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) cp = map_alpha(C->alpha == A_DEFAULT ? A_IT : C->alpha, ch);
    else if (ch >= '0' && ch <= '9') cp = map_alpha(C->alpha == A_RM ? A_DEFAULT : (C->alpha == A_IT ? A_DEFAULT : C->alpha), ch);
    else switch (ch) {
        case '+': cls = C_BIN; break;
        case '-': cls = C_BIN; cp = 0x2212; break;
        case '*': cls = C_BIN; cp = 0x2217; break;
        case '=': case '<': case '>': cls = C_REL; break;
        case ':': cls = C_REL; break;
        case ',': case ';': cls = C_PUNCT; break;
        case '(': case '[': cls = C_OPEN; break;
        case ')': case ']': cls = C_CLOSE; break;
        case '!': case '?': case '.': case '/': case '|': case '@': case '"': cls = C_ORD; break;
        default: break;
    }
    if (ch >= 'a' && ch <= 'z' && C->alpha == A_RM) cp = ch;
    if (ch >= 'A' && ch <= 'Z' && C->alpha == A_RM) cp = ch;
    b = bx_glyph(C, gid_of(C, cp) ? gid_of(C, cp) : gid_of(C, ch), sc);
    a = atom_of(b, cls);
    a.single = 1;
    return a;
}

static Atom text_atom(Ctx *C, int st, const char *s, size_t len, int cls) {
    float sc = style_scale(C, st);
    Atom a = atom_of(bx_text(C, s, len, sc, 0), cls);
    return a;
}

/* \mathrm{...} style arguments set the alphabet while the group is parsed. */
static Box parse_with_alpha(Ctx *C, int st, int alpha) {
    int save = C->alpha;
    Box b;
    C->alpha = alpha;
    b = parse_arg(C, st);
    C->alpha = save;
    return b;
}

static Box parse_text_arg(Ctx *C, int st, int bold, int italic) {
    float sc = style_scale(C, st);
    const char *s0;
    int depth = 0;
    Box b;
    (void)bold; (void)italic;
    skip_ws(C);
    if (C->p >= C->end || *C->p != '{') { C->err = 1; return bx_empty(C); }
    C->p++;
    s0 = C->p;
    while (C->p < C->end && !(*C->p == '}' && depth == 0)) { if (*C->p == '{') depth++; else if (*C->p == '}') depth--; C->p++; }
    if (C->p >= C->end) { C->err = 1; return bx_empty(C); }
    {
        char buf[512];
        size_t n = (size_t)(C->p - s0), o = 0, i;
        for (i = 0; i < n && o + 4 < sizeof buf; i++) {
            if (s0[i] == '\\' && i + 1 < n) { i++; if (s0[i] == ',' || s0[i] == ' ' || s0[i] == ';' || s0[i] == ':') { buf[o++] = ' '; continue; } }
            if (s0[i] == '{' || s0[i] == '}') continue;
            buf[o++] = s0[i];
        }
        if (bold) { /* map ASCII letters to bold */
            Box r = bx_empty(C);
            size_t k;
            for (k = 0; k < o; k++) {
                uint32_t cp = map_alpha(A_BF, (unsigned char)buf[k]);
                uint32_t g = gid_of(C, cp) ? gid_of(C, cp) : gid_of(C, (unsigned char)buf[k]);
                Box gb;
                if (buf[k] == ' ') { r.w += 0.25f * sc; continue; }
                gb = bx_glyph(C, g, sc);
                bx_append(C, &r, &gb, r.w, 0);
                r.w += gb.w; if (gb.h > r.h) r.h = gb.h; if (gb.d > r.d) r.d = gb.d;
            }
            C->p++;
            return r;
        }
        b = bx_text(C, buf, o, sc, 0);
        if (italic) { /* italic text: use the math italic letters */
            Box r = bx_empty(C);
            size_t k;
            for (k = 0; k < o; k++) {
                uint32_t cp = map_alpha(A_IT, (unsigned char)buf[k]);
                uint32_t g = gid_of(C, cp) ? gid_of(C, cp) : gid_of(C, (unsigned char)buf[k]);
                Box gb;
                if (buf[k] == ' ') { r.w += 0.25f * sc; continue; }
                gb = bx_glyph(C, g, sc);
                bx_append(C, &r, &gb, r.w, 0);
                r.w += gb.w; if (gb.h > r.h) r.h = gb.h; if (gb.d > r.d) r.d = gb.d;
            }
            C->p++;
            return r;
        }
    }
    C->p++;
    return b;
}

/* Parses the nucleus at the cursor. Returns 0 at the end of a group or list (the terminator is left or consumed as the caller expects). */
static int parse_nucleus(Ctx *C, int *st, Atom *out) {
    char name[48];
    skip_ws(C);
    if (C->p >= C->end) { C->term = T_END; return 0; }
    {
        char ch = *C->p;
        float sc = style_scale(C, *st);
        if (ch == '}') { C->term = T_RBRACE; return 0; }
        if (ch == '&') { C->term = T_AMP; return 0; }
        if (ch == '{') {
            Box g;
            C->p++;
            g = parse_atoms(C, *st);
            if (C->term != T_RBRACE) { C->err = 1; return 0; }
            *out = atom_of(g, C_ORD);
            return 1;
        }
        if (ch == '^' || ch == '_') { *out = atom_of(bx_empty(C), C_ORD); return 1; }  /* an empty nucleus */
        if (ch == '~') { C->p++; *out = atom_of(bx_space(C, 0.3f * sc), C_ORD); out->glue = 1; return 1; }
        if (ch == '\'') { /* primes become a superscript prime */
            *out = atom_of(bx_empty(C), C_ORD);
            return 1;
        }
        if (ch != '\\') { C->p++; *out = char_atom(C, *st, (unsigned char)ch); return 1; }
    }
    C->p++;
    if (!read_name(C, name, sizeof name)) { C->err = 1; return 0; }
    {
        float sc = style_scale(C, *st);
        /* spaces */
        if (!strcmp(name, ",")) { *out = atom_of(bx_space(C, 3.0f / 18.0f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, ":") || !strcmp(name, ">")) { *out = atom_of(bx_space(C, 4.0f / 18.0f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, ";")) { *out = atom_of(bx_space(C, 5.0f / 18.0f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "!")) { *out = atom_of(bx_space(C, -3.0f / 18.0f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, " ")) { *out = atom_of(bx_space(C, 0.3f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "quad")) { *out = atom_of(bx_space(C, 1.0f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "qquad")) { *out = atom_of(bx_space(C, 2.0f * sc), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "displaystyle")) { *st = st_make(0, st_cramped(*st)); *out = atom_of(bx_empty(C), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "textstyle")) { *st = st_make(1, st_cramped(*st)); *out = atom_of(bx_empty(C), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "scriptstyle")) { *st = st_make(2, st_cramped(*st)); *out = atom_of(bx_empty(C), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "scriptscriptstyle")) { *st = st_make(3, st_cramped(*st)); *out = atom_of(bx_empty(C), C_ORD); out->glue = 1; return 1; }
        if (!strcmp(name, "limits") || !strcmp(name, "nolimits")) { C->err = 1; return 0; } /* only valid after an operator, handled by the caller */
        if (!strcmp(name, "right")) { C->term = T_RIGHT; return 0; }
        if (!strcmp(name, "end")) { C->term = T_ENDENV; return 0; }
        if (!strcmp(name, "\\")) { C->term = T_ROWSEP; return 0; }
        if (!strcmp(name, "{") || !strcmp(name, "}") || !strcmp(name, "|") || !strcmp(name, "_") || !strcmp(name, "%") || !strcmp(name, "$") || !strcmp(name, "#") || !strcmp(name, "&")) {
            uint32_t cp = name[0] == '|' ? 0x2016 : (unsigned char)name[0];
            Box b = bx_glyph(C, gid_of(C, cp), sc);
            *out = atom_of(b, name[0] == '{' ? C_OPEN : (name[0] == '}' ? C_CLOSE : C_ORD));
            out->single = 1;
            return 1;
        }
        if (!strcmp(name, "begin")) { Box b = make_env(C, *st); *out = atom_of(b, C_INNER); return 1; }
        if (!strcmp(name, "frac") || !strcmp(name, "dfrac") || !strcmp(name, "tfrac") || !strcmp(name, "cfrac")) {
            int forced = name[0] == 'd' || name[0] == 'c' ? st_make(0, st_cramped(*st)) : (name[0] == 't' ? st_make(1, st_cramped(*st)) : -1);
            *out = atom_of(make_frac(C, *st, 1, forced), C_INNER);
            return 1;
        }
        if (!strcmp(name, "binom") || !strcmp(name, "dbinom") || !strcmp(name, "tbinom")) {
            int forced = name[0] == 'd' ? st_make(0, st_cramped(*st)) : (name[0] == 't' ? st_make(1, st_cramped(*st)) : -1);
            Box in = make_frac(C, *st, 0, forced), l, r, res = bx_empty(C);
            float sc2 = style_scale(C, *st);
            if (C->err) return 0;
            l = make_delim(C, '(', in.h, in.d, sc2); r = make_delim(C, ')', in.h, in.d, sc2);
            bx_append(C, &res, &l, 0, 0);
            bx_append(C, &res, &in, l.w + 0.05f * sc2, 0);
            bx_append(C, &res, &r, l.w + in.w + 0.1f * sc2, 0);
            res.w = l.w + in.w + r.w + 0.1f * sc2;
            res.h = fmaxf(in.h, fmaxf(l.h, r.h)); res.d = fmaxf(in.d, fmaxf(l.d, r.d));
            *out = atom_of(res, C_INNER);
            return 1;
        }
        if (!strcmp(name, "sqrt")) { *out = atom_of(make_sqrt(C, *st), C_ORD); return 1; }
        if (!strcmp(name, "left")) {
            uint32_t lc, rc;
            Box in, l, r, res;
            int term;
            if (!read_delim(C, &lc)) return 0;
            if (++C->depth > MAX_DEPTH) { C->err = 1; return 0; }
            in = parse_atoms(C, *st);
            C->depth--;
            term = C->term;
            if (term != T_RIGHT) { C->err = 1; return 0; }
            if (!read_delim(C, &rc)) return 0;
            l = make_delim(C, lc, in.h, in.d, sc);
            r = make_delim(C, rc, in.h, in.d, sc);
            res = bx_empty(C);
            bx_append(C, &res, &l, 0, 0);
            bx_append(C, &res, &in, l.w, 0);
            bx_append(C, &res, &r, l.w + in.w, 0);
            res.w = l.w + in.w + r.w;
            res.h = fmaxf(in.h, fmaxf(l.h, r.h)); res.d = fmaxf(in.d, fmaxf(l.d, r.d));
            *out = atom_of(res, C_INNER);
            return 1;
        }
        {   /* accents and bars */
            static const struct { const char *n; uint32_t cp; int wide; } acc[] = {
                {"hat", 0x302, 0}, {"check", 0x30C, 0}, {"tilde", 0x303, 0}, {"bar", 0x304, 0}, {"vec", 0x20D7, 0}, {"dot", 0x307, 0}, {"ddot", 0x308, 0},
                {"acute", 0x301, 0}, {"grave", 0x300, 0}, {"breve", 0x306, 0}, {"mathring", 0x30A, 0}, {"widehat", 0x302, 1}, {"widetilde", 0x303, 1},
            };
            size_t i;
            for (i = 0; i < sizeof acc / sizeof acc[0]; i++)
                if (!strcmp(name, acc[i].n)) { *out = atom_of(make_accent(C, *st, acc[i].cp, acc[i].wide), C_ORD); return 1; }
        }
        if (!strcmp(name, "overline")) { *out = atom_of(make_bar(C, *st, 1), C_ORD); return 1; }
        if (!strcmp(name, "underline")) { *out = atom_of(make_bar(C, *st, 0), C_ORD); return 1; }
        if (!strcmp(name, "overset") || !strcmp(name, "stackrel")) { *out = atom_of(make_stack(C, *st, 1), C_REL); return 1; }
        if (!strcmp(name, "underset")) { *out = atom_of(make_stack(C, *st, 0), C_REL); return 1; }
        /* alphabets */
        {
            static const struct { const char *n; int a; } al[] = { {"mathrm", A_RM}, {"mathit", A_IT}, {"mathbf", A_BF}, {"boldsymbol", A_BFIT}, {"bm", A_BFIT},
                {"mathbb", A_BB}, {"mathcal", A_CAL}, {"mathscr", A_CAL}, {"mathfrak", A_FRAK}, {"mathsf", A_SF}, {"mathtt", A_TT}, {"mathnormal", A_DEFAULT} };
            size_t i;
            for (i = 0; i < sizeof al / sizeof al[0]; i++)
                if (!strcmp(name, al[i].n)) {
                    Box b = parse_with_alpha(C, *st, al[i].a);
                    *out = atom_of(b, C_ORD);
                    return 1;
                }
        }
        if (!strcmp(name, "text") || !strcmp(name, "textrm") || !strcmp(name, "mbox") || !strcmp(name, "textnormal")) { *out = atom_of(parse_text_arg(C, *st, 0, 0), C_ORD); return 1; }
        if (!strcmp(name, "textbf")) { *out = atom_of(parse_text_arg(C, *st, 1, 0), C_ORD); return 1; }
        if (!strcmp(name, "textit") || !strcmp(name, "emph")) { *out = atom_of(parse_text_arg(C, *st, 0, 1), C_ORD); return 1; }
        if (!strcmp(name, "operatorname")) {
            char word[64];
            Atom a;
            if (!read_braced_name(C, word, sizeof word)) return 0;
            a = text_atom(C, *st, word, strlen(word), C_OP);
            a.textop = 1;
            *out = a;
            return 1;
        }
        {   /* named functions */
            size_t i;
            for (i = 0; funcs[i]; i++)
                if (!strcmp(name, funcs[i])) {
                    Atom a = text_atom(C, *st, funcs[i], strlen(funcs[i]), C_OP);
                    a.textop = 1;
                    a.limdefault = func_limits(funcs[i]);
                    *out = a;
                    return 1;
                }
        }
        {
            const Sym *sy = find_sym(name);
            if (sy) { *out = symbol_atom(C, *st, sy); return 1; }
        }
    }
    C->err = 1;
    return 0;
}

/* The argument of a command or script: a group or one nucleus. */
static Box parse_arg(Ctx *C, int st) {
    Atom a;
    int st2 = st;
    skip_ws(C);
    if (C->err) return bx_empty(C);
    if (C->p >= C->end) { C->err = 1; return bx_empty(C); }
    if (*C->p == '{') {
        Box g;
        C->p++;
        if (++C->depth > MAX_DEPTH) { C->err = 1; return bx_empty(C); }
        g = parse_atoms(C, st);
        C->depth--;
        if (C->term != T_RBRACE) C->err = 1;
        return g;
    }
    if (!parse_nucleus(C, &st2, &a) || C->err) { C->err = 1; return bx_empty(C); }
    return a.b;
}

/* A list of atoms up to a closing brace, \right, &, \\, \end or the end of the input. C->term says which. */
static Box parse_atoms(Ctx *C, int st) {
    Atom *atoms = NULL;
    size_t n = 0, cap = 0;
    Box r;
    int cur = st;
    int save_alpha = C->alpha;
    if (++C->depth > MAX_DEPTH) { C->err = 1; C->depth--; return bx_empty(C); }
    C->term = T_END;
    while (!C->err) {
        Atom a;
        Box sup, sub;
        int has_sup = 0, has_sub = 0, nprime = 0;
        if (!parse_nucleus(C, &cur, &a)) break;
        if (C->err) break;
        /* \limits and \nolimits */
        skip_ws(C);
        if (a.cls == C_OP && C->p < C->end && *C->p == '\\') {
            const char *q = C->p + 1;
            if (!strncmp(q, "limits", 6) && !((q[6] >= 'a' && q[6] <= 'z') || (q[6] >= 'A' && q[6] <= 'Z'))) { a.lim = LIM_YES; C->p = q + 6; }
            else if (!strncmp(q, "nolimits", 8) && !((q[8] >= 'a' && q[8] <= 'z') || (q[8] >= 'A' && q[8] <= 'Z'))) { a.lim = LIM_NO; C->p = q + 8; }
        }
        /* scripts */
        sup = sub = bx_empty(C);
        for (;;) {
            skip_ws(C);
            if (C->p >= C->end) break;
            if (*C->p == '\'') {
                float sc = style_scale(C, st_sup(cur));
                uint32_t g = gid_of(C, 0x2032);
                C->p++;
                nprime++;
                if (!has_sup) { sup = bx_empty(C); has_sup = 1; }
                {
                    Box pb = bx_glyph(C, g, sc);
                    bx_append(C, &sup, &pb, sup.w, 0);
                    sup.w += pb.w; sup.h = fmaxf(sup.h, pb.h); sup.d = fmaxf(sup.d, pb.d);
                }
                continue;
            }
            if (*C->p == '^' && !(has_sup && nprime == 0)) {
                Box t;
                C->p++;
                t = parse_arg(C, st_sup(cur));
                if (has_sup && nprime) { Box j = bx_empty(C); bx_append(C, &j, &sup, 0, 0); bx_append(C, &j, &t, sup.w, 0); j.w = sup.w + t.w; j.h = fmaxf(sup.h, t.h); j.d = fmaxf(sup.d, t.d); t = j; }
                sup = t; has_sup = 1; nprime = 0;
                continue;
            }
            if (*C->p == '_' && !has_sub) {
                C->p++;
                sub = parse_arg(C, st_sub(cur));
                has_sub = 1;
                continue;
            }
            break;
        }
        if (C->err) break;
        if (has_sup || has_sub) {
            int limits = a.cls == C_OP && (a.lim == LIM_YES || (a.lim == LIM_DEFAULT && a.limdefault && st_lvl(cur) == 0));
            Atom na = a;
            if (a.cls == C_OP && a.textop && a.lim == LIM_DEFAULT && a.limdefault && st_lvl(cur) == 0) limits = 1;
            if (limits) na.b = with_limits(C, cur, &a, has_sup ? &sup : NULL, has_sub ? &sub : NULL);
            else na.b = with_scripts(C, cur, &a, has_sup ? &sup : NULL, has_sub ? &sub : NULL);
            na.single = 0;
            a = na;
        }
        if (n == cap) { size_t nc = cap ? cap * 2 : 16; Atom *t = realloc(atoms, nc * sizeof *t); if (!t) { C->err = 1; break; } atoms = t; cap = nc; }
        atoms[n++] = a;
        if (a.cls == C_OP && !a.glue && !a.bigop) { /* a function name is followed by a thin space only by the class table */ }
    }
    {
        int term = C->term;
        if (C->p < C->end && C->term == T_END && !C->err) { /* stopped on '}' etc. handled by the nucleus */ }
        r = C->err ? bx_empty(C) : join_atoms(C, atoms, n, cur);
        C->term = term;
    }
    free(atoms);
    C->alpha = save_alpha;
    C->depth--;
    /* a closing brace is consumed here; & \\ and \end are consumed by the matrix code through the terminator */
    if (C->term == T_RBRACE && C->p < C->end && *C->p == '}') C->p++;
    else if (C->term == T_AMP && C->p < C->end && *C->p == '&') C->p++;
    return r;
}

/* ---- the whole equation ---- */
static void ctx_constants(Ctx *C) {
    int i;
    for (i = 0; i <= (int)HB_OT_MATH_CONSTANT_RADICAL_DEGREE_BOTTOM_RAISE_PERCENT && i < 80; i++) {
        hb_position_t v = hb_ot_math_get_constant(C->hf, (hb_ot_math_constant_t)i);
        if (i == HB_OT_MATH_CONSTANT_SCRIPT_PERCENT_SCALE_DOWN || i == HB_OT_MATH_CONSTANT_SCRIPT_SCRIPT_PERCENT_SCALE_DOWN ||
            i == HB_OT_MATH_CONSTANT_RADICAL_DEGREE_BOTTOM_RAISE_PERCENT)
            C->k[i] = (float)v / 100.0f;
        else if (i == HB_OT_MATH_CONSTANT_DELIMITED_SUB_FORMULA_MIN_HEIGHT || i == HB_OT_MATH_CONSTANT_DISPLAY_OPERATOR_MIN_HEIGHT)
            C->k[i] = (float)v / (float)C->upem;
        else
            C->k[i] = (float)v / (float)C->upem;
    }
}

ExMath *ex_math_layout(ExFont *font, const char *src, size_t len, int display) {
    Ctx C;
    ExMath *m;
    hb_font_t *hf;
    hb_face_t *face;
    Box b;
    if (!font || !src) return NULL;
    face = hb_font_get_face(ex_font_hb(font));
    if (!face || !hb_ot_math_has_data(face)) return NULL;
    memset(&C, 0, sizeof C);
    hf = hb_font_create(face);
    if (!hf) return NULL;
    C.upem = ex_font_upem(font);
    hb_font_set_scale(hf, C.upem, C.upem);
    hb_ot_font_set_funcs(hf);
    C.hf = hf; C.font = font;
    ctx_constants(&C);
    m = calloc(1, sizeof *m);
    if (!m) { hb_font_destroy(hf); return NULL; }
    m->font = font;
    C.p = src; C.end = src + len;
    if (len > MAX_INPUT) C.err = 1;
    b = bx_empty(&C);
    if (!C.err) {
        b = parse_atoms(&C, st_make(display ? 0 : 1, 0));
        if (!C.err && (C.term != T_END || C.p < C.end)) C.err = 1;
    }
    if (C.err) { /* the source as text */
        size_t k = len > MAX_INPUT ? MAX_INPUT : len;
        free(C.pool); C.pool = NULL; C.n = C.cap = 0; C.err = 0;
        b = bx_text(&C, src, k, 1.0f, 0);
        m->error = 1;
        if (C.err) { free(C.pool); free(m); hb_font_destroy(hf); return NULL; }
    }
    m->n = b.e - b.s;
    m->items = m->n ? malloc(m->n * sizeof *m->items) : NULL;
    if (m->n && !m->items) { free(C.pool); free(m); hb_font_destroy(hf); return NULL; }
    if (m->n) memcpy(m->items, C.pool + b.s, m->n * sizeof *m->items);
    m->w = b.w; m->h = b.h < 0 ? 0 : b.h; m->d = b.d < 0 ? 0 : b.d;
    free(C.pool);
    hb_font_destroy(hf);
    return m;
}

void ex_math_free(ExMath *m) { if (m) { free(m->items); free(m); } }
int ex_math_error(const ExMath *m) { return m ? m->error : 1; }
float ex_math_width(const ExMath *m) { return m ? m->w : 0; }
float ex_math_ascent(const ExMath *m) { return m ? m->h : 0; }
float ex_math_descent(const ExMath *m) { return m ? m->d : 0; }
const ExMathItem *ex_math_items(const ExMath *m, size_t *n) { if (n) *n = m ? m->n : 0; return m ? m->items : NULL; }
ExFont *ex_math_font(const ExMath *m) { return m ? m->font : NULL; }

/* ---- cache ---- */
typedef struct CacheEntry { uint64_t key; size_t len; ExMath *m; unsigned gen; char *src; int display; uint32_t font; } CacheEntry;
struct ExMathCache { CacheEntry *e; size_t n, cap; unsigned gen; size_t misses; };

ExMathCache *ex_mathcache_create(void) { return calloc(1, sizeof(ExMathCache)); }
void ex_mathcache_free(ExMathCache *c) {
    size_t i;
    if (!c) return;
    for (i = 0; i < c->n; i++) { ex_math_free(c->e[i].m); free(c->e[i].src); }
    free(c->e);
    free(c);
}
void ex_mathcache_begin(ExMathCache *c) {
    size_t i, o = 0;
    if (!c) return;
    for (i = 0; i < c->n; i++) {
        if (c->e[i].gen == c->gen) { c->e[o++] = c->e[i]; }
        else { ex_math_free(c->e[i].m); free(c->e[i].src); }
    }
    c->n = o;
    c->gen++;
}
static uint64_t fnv(const char *s, size_t n, int display, uint32_t font) {
    uint64_t h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    h ^= (uint64_t)display + 31u * (uint64_t)font; h *= 1099511628211ull;
    return h;
}
const ExMath *ex_mathcache_lookup(ExMathCache *c, ExFont *font, const char *src, size_t len, int display) {
    uint64_t key;
    size_t i;
    uint32_t fid;
    if (!c || !font || !src) return NULL;
    fid = ex_font_id(font);
    key = fnv(src, len, display, fid);
    for (i = 0; i < c->n; i++)
        if (c->e[i].key == key && c->e[i].len == len && c->e[i].display == display && c->e[i].font == fid && !memcmp(c->e[i].src, src, len)) { c->e[i].gen = c->gen; return c->e[i].m; }
    return NULL;
}

const ExMath *ex_mathcache_get(ExMathCache *c, ExFont *font, const char *src, size_t len, int display) {
    size_t i;
    ExMath *m;
    uint32_t fid;
    uint64_t key;
    const ExMath *hit;
    if (!c || !font || !src) return NULL;
    hit = ex_mathcache_lookup(c, font, src, len, display);
    if (hit) return hit;
    fid = ex_font_id(font);
    key = fnv(src, len, display, fid);
    (void)i;
    m = ex_math_layout(font, src, len, display);
    c->misses++;
    if (!m) return NULL;
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 32;
        CacheEntry *t = realloc(c->e, nc * sizeof *t);
        if (!t) { ex_math_free(m); return NULL; }
        c->e = t; c->cap = nc;
    }
    c->e[c->n].src = malloc(len ? len : 1);
    if (!c->e[c->n].src) { ex_math_free(m); return NULL; }
    memcpy(c->e[c->n].src, src, len);
    c->e[c->n].key = key; c->e[c->n].len = len; c->e[c->n].m = m; c->e[c->n].gen = c->gen; c->e[c->n].display = display; c->e[c->n].font = fid;
    c->n++;
    return m;
}
size_t ex_mathcache_count(const ExMathCache *c) { return c ? c->n : 0; }
size_t ex_mathcache_misses(const ExMathCache *c) { return c ? c->misses : 0; }
