/* Export of a note to PDF (sub-slice 6e-iv): see include/exhaust_pdfexport.h.
 *
 * The note is laid out once into a tall virtual page of the text column's width: positioned pieces (words, rectangles, rules, pictures, equations,
 * diagrams) in points, with the places where a page may end (the top of a line, a table row, a picture). The virtual page is then cut into pages at
 * those places and each page is written with the PDF writer. The layout follows the reading view's (headings, lists, quotes and callouts, code, tables,
 * footnotes) so a note looks the same on paper as in reading mode, but with its own text measuring: this code runs on a thread of its own and never
 * touches the renderer or the fonts of the interface. */
#include "exhaust_pdfexport.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_diagram.h"
#include "exhaust_image.h"
#include "exhaust_math.h"
#include "exhaust_pdfwrite.h"
#include "exhaust_platform.h"
#include "exhaust_reading.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"
#include "fonts_data.h"

enum { FU_REG = 0, FU_BOLD, FU_ITALIC, FU_BI, FM_REG, FM_BOLD, FM_ITALIC, FM_BI, FMATH, NFONTS };

typedef struct EFont {
    ExFont *f;
    const unsigned char *data; size_t len;
    float *adv; uint8_t *have;
    int pdf;                       /* the writer's font number, -1 until first used */
    int asc, desc, lineh;          /* at 1000 px */
    int mono, italic;
    const char *name;
} EFont;

typedef enum ItemKind { IT_TEXT, IT_RECT, IT_BOX, IT_IMAGE, IT_MATH, IT_DIAGRAM, IT_CARD } ItemKind;

typedef struct Item {
    ItemKind kind;
    float x, y, w, h;
    int font; float size;
    int fg, bg, stroke;            /* theme tokens, -1 none */
    int alpha;                     /* RECT: 0 opaque, else 1..255 over the page */
    float radius;
    unsigned flags;                /* EX_RS_UNDERLINE, EX_RS_STRIKE */
    const char *text; size_t len;
    const char *uri;
    int img, checked;
    const ExMath *math; const ExDiagram *dg;
    float base_y;
} Item;

typedef struct Deco { float y0, y1, x, w; int color, alpha; } Deco;

typedef struct ImgRef { char *path; int id; int w, h; int failed; } ImgRef;

typedef struct Ctx {
    const ExPdfExportIn *in;
    ExFontLib *lib;
    EFont fonts[NFONTS];
    ExPdfDoc *doc;
    ExTheme theme;
    ExMathCache *mc;
    float k, base, lh_mul;
    float page_w, page_h, margin, cw, ch;       /* content width and height of a page */
    Item *items; size_t n, cap;
    Deco *decos; size_t nd, capd;
    float *brk; size_t nbrk, capbrk;
    char **owned; size_t nown, capown;
    ImgRef *imgs; size_t nimgs, capimgs;
    ExDiagram **dgs; size_t ndg, capdg;
    float x, y, x_left, x_right, line_h;
    int suppress_break;
    int cancelled, oom;
    char err[200];
} Ctx;

/* ---- fonts ---- */

static float adv_of(EFont *e, uint32_t g) {
    if (g >= 65536) return 0;
    if (!(e->have[g >> 3] & (1u << (g & 7)))) { e->adv[g] = ex_font_advance(e->f, g, 1000); e->have[g >> 3] |= (uint8_t)(1u << (g & 7)); }
    return e->adv[g];
}
static float adv_cb(void *u, uint32_t g) { return adv_of((EFont *)u, g); }

static uint32_t glyph_of(EFont *e, uint32_t cp, int *subst) {
    uint32_t g = ex_font_glyph_index(e->f, cp);
    *subst = 0;
    if (g) return g;
    *subst = 1;
    g = ex_font_glyph_index(e->f, '?');
    return g;
}

static int open_fonts(Ctx *c) {
    static const ExBuiltinFont which[NFONTS] = { EX_FONT_UI_REGULAR, EX_FONT_UI_BOLD, EX_FONT_UI_ITALIC, EX_FONT_UI_BOLD_ITALIC,
                                                 EX_FONT_MONO_REGULAR, EX_FONT_MONO_BOLD, EX_FONT_MONO_ITALIC, EX_FONT_MONO_BOLD_ITALIC, EX_FONT_MATH };
    static const char *const names[NFONTS] = { "AtkinsonHyperlegible-Regular", "AtkinsonHyperlegible-Bold", "AtkinsonHyperlegible-Italic", "AtkinsonHyperlegible-BoldItalic",
                                               "AtkinsonHyperlegibleMono-Regular", "AtkinsonHyperlegibleMono-Bold", "AtkinsonHyperlegibleMono-Italic", "AtkinsonHyperlegibleMono-BoldItalic",
                                               "STIXTwoMath-Regular" };
    int i;
    c->lib = ex_fontlib_create();
    if (!c->lib) return -1;
    for (i = 0; i < NFONTS; i++) {
        EFont *e = &c->fonts[i];
        ExEmbeddedFont d = ex_embedded_font(which[i]);
        ExFontMetrics m;
        e->f = ex_font_builtin(c->lib, which[i]);
        e->data = d.data; e->len = d.size; e->name = names[i];
        e->mono = i >= FM_REG && i < FMATH; e->italic = i == FU_ITALIC || i == FU_BI || i == FM_ITALIC || i == FM_BI;
        e->pdf = -1;
        e->adv = calloc(65536, sizeof *e->adv);
        e->have = calloc(65536 / 8, 1);
        if (!e->f || !e->data || !e->adv || !e->have) return -1;
        if (ex_font_metrics(e->f, 1000, &m) == 0) { e->asc = m.ascent; e->desc = m.descent; e->lineh = m.line_height; }
        else { e->asc = 950; e->desc = 250; e->lineh = 1200; }
    }
    return 0;
}

static int style_font(unsigned st) {
    int mono = (st & EX_RS_CODE) != 0, bold = (st & EX_RS_BOLD) != 0, italic = (st & EX_RS_ITALIC) != 0;
    return (mono ? FM_REG : FU_REG) + (bold ? 1 : 0) + (italic ? 2 : 0);
}

static float measure(Ctx *c, int font, float size, const char *s, size_t n) {
    EFont *e = &c->fonts[font];
    float w = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t cp = ex_utf8_decode(s, n, &i);
        int sub;
        if (cp == '\t') cp = ' ';
        w += adv_of(e, glyph_of(e, cp, &sub));
    }
    return w * size / 1000.0f;
}

static int pdf_font(Ctx *c, int font) {
    EFont *e = &c->fonts[font];
    if (e->pdf < 0) e->pdf = ex_pdfw_font_add(c->doc, e->name, e->data, e->len, adv_cb, e, e->asc, e->desc, e->mono, e->italic);
    return e->pdf;
}

/* ---- colors ---- */

static ExPdfColor tok(const Ctx *c, int token) {
    ExColor k = ex_theme_color(&c->theme, (ExColorToken)token);
    ExPdfColor o;
    o.r = (float)k.r / 255.0f; o.g = (float)k.g / 255.0f; o.b = (float)k.b / 255.0f;
    return o;
}
/* a color over the page's white: alpha 0 is opaque */
static ExPdfColor tint(const Ctx *c, int token, int alpha) {
    ExPdfColor o = tok(c, token), bg = tok(c, EX_COLOR_BACKGROUND);
    float a = alpha > 0 ? (float)alpha / 255.0f : 1.0f;
    o.r = o.r * a + bg.r * (1 - a); o.g = o.g * a + bg.g * (1 - a); o.b = o.b * a + bg.b * (1 - a);
    return o;
}

/* ---- storage ---- */

static Item *new_item(Ctx *c, ItemKind kind) {
    Item *it;
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 1024;
        Item *ni = realloc(c->items, nc * sizeof *ni);
        if (!ni) { c->oom = 1; return NULL; }
        c->items = ni; c->cap = nc;
    }
    it = &c->items[c->n++];
    memset(it, 0, sizeof *it);
    it->kind = kind; it->fg = it->bg = it->stroke = -1; it->img = -1;
    return it;
}

static void new_deco(Ctx *c, float y0, float y1, float x, float w, int color, int alpha) {
    Deco *d;
    if (c->nd == c->capd) {
        size_t nc = c->capd ? c->capd * 2 : 128;
        Deco *nd = realloc(c->decos, nc * sizeof *nd);
        if (!nd) { c->oom = 1; return; }
        c->decos = nd; c->capd = nc;
    }
    d = &c->decos[c->nd++];
    d->y0 = y0; d->y1 = y1; d->x = x; d->w = w; d->color = color; d->alpha = alpha;
}

/* a place where a page may end: the top of a unit that is not cut (a line, a row, a picture) */
static void add_break(Ctx *c, float y) {
    if (c->suppress_break) { c->suppress_break = 0; return; }
    if (c->nbrk && y <= c->brk[c->nbrk - 1] + 0.01f) return;
    if (c->nbrk == c->capbrk) {
        size_t nc = c->capbrk ? c->capbrk * 2 : 256;
        float *nb = realloc(c->brk, nc * sizeof *nb);
        if (!nb) { c->oom = 1; return; }
        c->brk = nb; c->capbrk = nc;
    }
    c->brk[c->nbrk++] = y;
}

static const char *own(Ctx *c, const char *s) {
    char *d;
    if (c->nown == c->capown) {
        size_t nc = c->capown ? c->capown * 2 : 64;
        char **no = realloc(c->owned, nc * sizeof *no);
        if (!no) { c->oom = 1; return ""; }
        c->owned = no; c->capown = nc;
    }
    d = malloc(strlen(s) + 1);
    if (!d) { c->oom = 1; return ""; }
    strcpy(d, s);
    c->owned[c->nown++] = d;
    return d;
}

static float line_height(const Ctx *c, float size) { return size * (float)c->fonts[FU_REG].lineh / 1000.0f * c->lh_mul; }
/* the baseline below the top of a line of height lh whose text has size `size` */
static float baseline_off(const Ctx *c, float lh, float size) {
    const EFont *e = &c->fonts[FU_REG];
    return (lh - size * (float)e->lineh / 1000.0f) / 2.0f + size * (float)e->asc / 1000.0f;
}

static int is_url(const char *s) { return s && (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8) || !strncmp(s, "mailto:", 7)); }

/* ---- flowing text ---- */

static void flow(Ctx *c, const ExRSpan *sp, size_t n, unsigned base_style, float size_mul, int base_fg) {
    size_t i;
    float size = c->base * size_mul, lh = line_height(c, size);
    c->line_h = lh;
    c->x = c->x_left;
    add_break(c, c->y);
    for (i = 0; i < n; i++) {
        const ExRSpan *s = &sp[i];
        unsigned st = s->style | base_style;
        int fg = base_fg, bg = -1;
        float sz = size, yoff = 0;
        const char *t = s->text ? s->text : "";
        char buf[400];
        size_t pos = 0, tl = strlen(t);
        const char *uri = NULL;
        if (s->kind == EX_RSK_BREAK) {
            if (!t[0]) { c->y += lh; c->x = c->x_left; add_break(c, c->y); }
            else c->x += size * 0.3f;
            continue;
        }
        if (s->kind == EX_RSK_LINK_EXTERNAL || s->kind == EX_RSK_LINK_INTERNAL) { fg = EX_COLOR_LINK; st |= EX_RS_UNDERLINE; if (s->kind == EX_RSK_LINK_EXTERNAL && is_url(s->target)) uri = s->target; }
        else if (s->kind == EX_RSK_TAG) fg = EX_COLOR_TAG;
        else if (s->kind == EX_RSK_FOOTNOTE_REF) { fg = EX_COLOR_LINK; sz = size * 0.75f; yoff = -size * 0.3f; }
        else if (s->kind == EX_RSK_IMAGE) { snprintf(buf, sizeof buf, "[image: %s]", t); t = own(c, buf); tl = strlen(t); fg = EX_COLOR_TEXT_MUTED; bg = EX_COLOR_CODE_BACKGROUND; }
        else if (s->kind == EX_RSK_EMBED) { snprintf(buf, sizeof buf, "[embedded: %s]", t); t = own(c, buf); tl = strlen(t); fg = EX_COLOR_TEXT_MUTED; bg = EX_COLOR_CODE_BACKGROUND; }
        else if (s->kind == EX_RSK_MATH) {
            const ExMath *m = c->mc ? ex_mathcache_get(c->mc, c->fonts[FMATH].f, t, tl, 0) : NULL;
            if (m && !ex_math_error(m)) {
                float mw = ex_math_width(m) * sz;
                Item *it;
                if (c->x + mw > c->x_right && c->x > c->x_left) { c->y += lh; c->x = c->x_left; add_break(c, c->y); }
                it = new_item(c, IT_MATH);
                if (it) { it->x = c->x; it->y = c->y; it->w = mw; it->h = lh; it->math = m; it->size = sz; it->fg = EX_COLOR_TEXT; it->base_y = baseline_off(c, lh, sz); }
                c->x += mw;
                continue;
            }
            fg = m ? EX_COLOR_ERROR : EX_COLOR_CODE_TEXT; bg = EX_COLOR_CODE_BACKGROUND; st |= EX_RS_CODE;
        }
        if (st & EX_RS_CODE && s->kind == EX_RSK_TEXT) { fg = EX_COLOR_CODE_TEXT; bg = EX_COLOR_CODE_BACKGROUND; }
        if (st & EX_RS_SUB) { sz = size * 0.75f; yoff = size * 0.2f; }
        if ((st & EX_RS_SUP) && s->kind != EX_RSK_FOOTNOTE_REF) { sz = size * 0.75f; yoff = -size * 0.3f; }
        if (st & EX_RS_HIGHLIGHT) bg = EX_COLOR_HIGHLIGHT;
        if (s->kind == EX_RSK_LINK_INTERNAL && s->target && !strcmp(t, s->target)) {     /* a wikilink without its own label shows "Page > Heading" */
            char *o = malloc(tl * 2 + 8), *w;
            size_t q;
            if (o) {
                w = o;
                for (q = 0; q < tl; q++) {
                    if (t[q] == '#' && t[q + 1] == '^') { memcpy(w, " > ", 3); w += 3; q++; }
                    else if (t[q] == '#') { memcpy(w, " > ", 3); w += 3; }
                    else *w++ = t[q];
                }
                *w = 0;
                t = own(c, o); tl = strlen(t);
                free(o);
            }
        }
        {
            int font = style_font(st);
            while (pos < tl) {
                size_t e = pos, tok_end;
                float w, vis;
                Item *it;
                while (e < tl && t[e] != ' ' && t[e] != '\n') e++;
                tok_end = e;
                while (tok_end < tl && (t[tok_end] == ' ' || t[tok_end] == '\n')) tok_end++;
                if (tok_end == pos) tok_end = pos + 1;
                w = measure(c, font, sz, t + pos, tok_end - pos);
                vis = measure(c, font, sz, t + pos, e - pos);
                if (c->x + vis > c->x_right && c->x > c->x_left) { c->y += lh; c->x = c->x_left; add_break(c, c->y); }
                it = new_item(c, IT_TEXT);
                if (it) {
                    it->x = c->x; it->y = c->y + yoff; it->w = w; it->h = lh; it->text = t + pos; it->len = tok_end - pos;
                    it->font = font; it->size = sz; it->fg = fg; it->bg = bg; it->flags = st; it->uri = uri; it->base_y = baseline_off(c, lh, sz);
                }
                c->x += w;
                pos = tok_end;
            }
        }
    }
    c->y += lh;
    c->x = c->x_left;
}

/* ---- pictures and cards ---- */

static int image_for(Ctx *c, const char *path, int *w, int *h, int svg_px) {
    size_t i, len = 0;
    char *data;
    ExImage img;
    char err[120];
    int id;
    for (i = 0; i < c->nimgs; i++) if (!strcmp(c->imgs[i].path, path)) { *w = c->imgs[i].w; *h = c->imgs[i].h; return c->imgs[i].failed ? -1 : c->imgs[i].id; }
    if (c->nimgs == c->capimgs) {
        size_t nc = c->capimgs ? c->capimgs * 2 : 16;
        ImgRef *ni = realloc(c->imgs, nc * sizeof *ni);
        if (!ni) { c->oom = 1; return -1; }
        c->imgs = ni; c->capimgs = nc;
    }
    c->imgs[c->nimgs].path = malloc(strlen(path) + 1);
    if (!c->imgs[c->nimgs].path) { c->oom = 1; return -1; }
    strcpy(c->imgs[c->nimgs].path, path);
    c->imgs[c->nimgs].failed = 1; c->imgs[c->nimgs].id = -1; c->imgs[c->nimgs].w = c->imgs[c->nimgs].h = 0;
    c->nimgs++;
    data = ex_platform_read_file(path, &len);
    if (!data) return -1;
    memset(&img, 0, sizeof img);
    if (ex_image_decode(data, len, 2400, svg_px > 0 ? svg_px : 0, &img, err, sizeof err) != 0) { free(data); return -1; }
    free(data);
    id = ex_pdfw_image_add(c->doc, img.rgba, img.w, img.h);
    c->imgs[c->nimgs - 1].w = *w = img.src_w ? img.src_w : img.w;
    c->imgs[c->nimgs - 1].h = *h = img.src_h ? img.src_h : img.h;
    if (id >= 0) { c->imgs[c->nimgs - 1].id = id; c->imgs[c->nimgs - 1].failed = 0; }
    ex_image_free(&img);
    return id;
}

static void card(Ctx *c, const char *label, float x, float w) {
    float lh = line_height(c, c->base), h = lh * 1.9f;
    Item *it;
    add_break(c, c->y);
    it = new_item(c, IT_CARD);
    if (it) {
        it->x = x; it->y = c->y; it->w = w; it->h = h; it->text = own(c, label); it->len = strlen(it->text); it->font = FU_REG; it->size = c->base;
        it->fg = EX_COLOR_TEXT_MUTED; it->bg = EX_COLOR_CODE_BACKGROUND; it->stroke = EX_COLOR_BORDER; it->radius = c->base * 0.3f; it->base_y = baseline_off(c, h, c->base);
    }
    c->y += h + 0.4f * lh;
}

/* a paragraph that is only a picture or an embed of a file: 1 when it was handled */
static int layout_media(Ctx *c, const ExRBlock *b, float col_right) {
    const ExRSpan *s = NULL;
    size_t i;
    char name[400], size[40], frag[200], path[800];
    int kind;
    for (i = 0; i < b->nspans; i++) {
        if (b->spans[i].kind == EX_RSK_BREAK) continue;
        if (s) return 0;
        s = &b->spans[i];
    }
    if (!s || (s->kind != EX_RSK_EMBED && s->kind != EX_RSK_IMAGE) || !s->target || b->marker) return 0;
    ex_embed_split(s->kind == EX_RSK_IMAGE && s->text && s->text[0] && strchr(s->text, '|') ? s->text : s->target, name, sizeof name, size, sizeof size, frag, sizeof frag);
    if (s->kind == EX_RSK_IMAGE) {
        const char *bar = s->text ? strrchr(s->text, '|') : NULL;
        ex_embed_split(s->target, name, sizeof name, NULL, 0, frag, sizeof frag);
        size[0] = 0;
        if (bar) snprintf(size, sizeof size, "%s", bar + 1);
    }
    if (is_url(name)) {
        char label[500];
        snprintf(label, sizeof label, "External image, not loaded: %s", name);
        card(c, label, c->x_left, col_right - c->x_left);
        return 1;
    }
    kind = ex_embed_media_kind(name);
    if (!kind) return 0;
    if (!c->in->resolve || !c->in->resolve(c->in->user, name, path, sizeof path)) { char label[500]; snprintf(label, sizeof label, "%s (not found)", name); card(c, label, c->x_left, col_right - c->x_left); return 1; }
    if (kind == 1) {
        int w = 0, h = 0, nw = 0, nh = 0, id;
        float dw, dh, avail = col_right - c->x_left, maxh = c->ch;
        Item *it;
        if (sscanf(size, "%dx%d", &w, &h) != 2) { h = 0; w = atoi(size); }
        id = image_for(c, path, &nw, &nh, w > 0 ? (int)(w * 2) : 1600);
        if (id < 0 || nw <= 0 || nh <= 0) { char label[500]; snprintf(label, sizeof label, "Picture could not be read: %s", name); card(c, label, c->x_left, avail); return 1; }
        dw = (w > 0 ? (float)w : (float)nw) * 0.75f * c->k;
        if (dw > avail) dw = avail;
        dh = h > 0 && w > 0 ? dw * (float)h / (float)w : dw * (float)nh / (float)nw;
        if (dh > maxh) { dw *= maxh / dh; dh = maxh; }
        add_break(c, c->y);
        it = new_item(c, IT_IMAGE);
        if (it) { it->x = c->x_left; it->y = c->y; it->w = dw; it->h = dh; it->img = id; }
        c->y += dh + 0.4f * line_height(c, c->base);
        return 1;
    } else {
        char label[1100];
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        snprintf(label, sizeof label, "%s  %s%s%s%s", kind == 2 ? "Audio" : kind == 3 ? "Video" : kind == 4 ? "PDF" : "Canvas", base, frag[0] && kind == 4 ? "  (" : "", frag[0] && kind == 4 ? frag : "", frag[0] && kind == 4 ? ")" : "");
        card(c, label, c->x_left, col_right - c->x_left);
        return 1;
    }
}

/* ---- the blocks ---- */

static int strcasecmp_ascii(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (y >= 'A' && y <= 'Z') y = (char)(y + 32); if (x != y) return 1; }
    return *a || *b;
}

static void layout_code(Ctx *c, const char *code, int error, float x0, float col_right) {
    float size = c->base * 0.92f, lh = line_height(c, size), padc = 0.5f * c->base, avail = col_right - c->x_left - 2 * padc;
    float cwid = measure(c, FM_REG, size, "0", 1), y;
    size_t per = cwid > 0 ? (size_t)(avail / cwid) : 80, lines = 0;
    const char *p = code;
    (void)x0;
    if (per < 8) per = 8;
    y = c->y + padc;
    c->y += padc;
    for (;;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p), off = 0;
        do {                                                  /* a long line wraps at the column's width */
            size_t take = n - off > per ? per : n - off;
            Item *it, *bg;
            add_break(c, y);
            bg = new_item(c, IT_RECT);
            if (bg) { bg->x = c->x_left; bg->y = y; bg->w = col_right - c->x_left; bg->h = lh; bg->bg = EX_COLOR_CODE_BACKGROUND; }
            it = new_item(c, IT_TEXT);
            if (it) { it->x = c->x_left + padc; it->y = y; it->w = measure(c, FM_REG, size, p + off, take); it->h = lh; it->text = p + off; it->len = take; it->font = FM_REG; it->size = size;
                      it->fg = error ? EX_COLOR_ERROR : EX_COLOR_CODE_TEXT; it->flags = EX_RS_CODE; it->base_y = baseline_off(c, lh, size); }
            y += lh; lines++;
            off += take;
        } while (off < n);
        if (!e) break;
        p = e + 1;
    }
    c->y = y + padc;
    (void)lines;
}

static void draw_stub(void) {}

static void layout_diagram(Ctx *c, const ExDiagram *dg, float col_right) {
    float em = c->base, dw = ex_diagram_width(dg) * em, dh = ex_diagram_height(dg) * em, avail = col_right - c->x_left, fit = dw > avail ? avail / dw : 1.0f, dpad = 0.4f * em;
    Item *it;
    if (dh * fit > c->ch - 2 * dpad) { float f2 = (c->ch - 2 * dpad) / dh; if (f2 < fit) fit = f2; }
    add_break(c, c->y);
    it = new_item(c, IT_DIAGRAM);
    if (it) { it->dg = dg; it->size = em * fit; it->w = dw * fit; it->h = dh * fit; it->x = c->x_left + (avail - it->w) / 2.0f; it->y = c->y + dpad; }
    c->y += dh * fit + 2 * dpad;
    draw_stub();
}

static void layout_blocks(Ctx *c, const ExReading *model, int include_title, const char *title) {
    size_t i, nb = ex_reading_block_count(model);
    float em = c->base;
    if (include_title && title && *title) {
        ExRSpan sp;
        memset(&sp, 0, sizeof sp);
        sp.kind = EX_RSK_TEXT; sp.text = (char *)title;
        c->x_left = 0; c->x_right = c->cw;
        flow(c, &sp, 1, EX_RS_BOLD, 1.8f, EX_COLOR_TEXT);
        c->y += 0.6f * line_height(c, c->base);
    }
    for (i = 0; i < nb; i++) {
        const ExRBlock *b = ex_reading_block(model, i);
        float indent = (float)b->list_depth * em * 1.6f + (float)b->nframes * em * 1.0f, top = c->y, gap, bar_w = 3.0f * c->k;
        int f;
        if (c->in->cancel && *c->in->cancel) { c->cancelled = 1; return; }
        if (c->in->progress && (i & 31) == 0) c->in->progress(c->in->user, nb ? (int)(500 * i / nb) : 0, 0);
        if (c->oom) return;
        c->x_left = indent;
        c->x_right = c->cw;
        gap = b->list_depth > 0 && b->kind != EX_RBK_HEADING ? 0.25f * line_height(c, c->base) : 0.6f * line_height(c, c->base);
        c->x = c->x_left;
        switch (b->kind) {
        case EX_RBK_HEADING: {
            static const float sc[] = {1.0f, 1.8f, 1.5f, 1.3f, 1.15f, 1.05f, 1.0f};
            if (c->y > 0.01f) c->y += 0.5f * line_height(c, c->base);
            top = c->y;
            flow(c, b->spans, b->nspans, EX_RS_BOLD, sc[b->level >= 1 && b->level <= 6 ? b->level : 1], EX_COLOR_TEXT);
            c->suppress_break = 1;                                           /* a heading stays with the line after it */
            break;
        }
        case EX_RBK_PARAGRAPH: case EX_RBK_CALLOUT_TITLE: {
            float ty = c->y;
            if (b->kind == EX_RBK_PARAGRAPH && layout_media(c, b, c->cw)) break;
            if (b->marker) {
                float lh = line_height(c, c->base);
                Item *m;
                if (b->task) {
                    float bs = em * 0.9f;
                    m = new_item(c, IT_BOX);
                    if (m) { m->checked = b->task != 1; m->x = c->x_left - em * 1.4f; m->y = ty + (lh - bs) / 2.0f; m->w = bs; m->h = bs; m->fg = EX_COLOR_ACCENT; }
                } else {
                    float mw = measure(c, FU_REG, c->base, b->marker, strlen(b->marker));
                    m = new_item(c, IT_TEXT);
                    if (m) { m->x = c->x_left - mw - em * 0.4f; m->y = ty; m->w = mw; m->h = lh; m->text = b->marker; m->len = strlen(b->marker); m->font = FU_REG; m->size = c->base; m->fg = EX_COLOR_TEXT_MUTED; m->base_y = baseline_off(c, lh, c->base); }
                }
            }
            if (b->kind == EX_RBK_CALLOUT_TITLE) {
                int ct = b->nframes > 0 ? b->frame_callout[b->nframes - 1] : 0;
                flow(c, b->spans, b->nspans, EX_RS_BOLD, 1.0f, EX_COLOR_CALLOUT_NOTE + (ct >= 0 && ct < 13 ? ct : 0));
            } else flow(c, b->spans, b->nspans, 0, 1.0f, EX_COLOR_TEXT);
            break;
        }
        case EX_RBK_CODE: case EX_RBK_MATH: {
            const char *code = b->code ? b->code : "";
            int err = 0;
            const char *dg_msg = NULL;
            if (b->kind == EX_RBK_CODE && b->lang && !strcasecmp_ascii(b->lang, "mermaid")) {
                const ExDiagram *dg = ex_diagram_parse(c->fonts[FU_REG].f, code, strlen(code));
                if (dg && !ex_diagram_error(dg)) {
                    if (c->ndg == c->capdg) { size_t nc = c->capdg ? c->capdg * 2 : 8; ExDiagram **nd = realloc(c->dgs, nc * sizeof *nd); if (nd) { c->dgs = nd; c->capdg = nc; } }
                    if (c->ndg < c->capdg) { c->dgs[c->ndg++] = (ExDiagram *)dg; layout_diagram(c, dg, c->cw); }
                    else ex_diagram_free((ExDiagram *)dg);
                    break;
                }
                if (dg) dg_msg = own(c, ex_diagram_error(dg));
                if (dg) ex_diagram_free((ExDiagram *)dg);
            }
            if (b->kind == EX_RBK_MATH) {
                const ExMath *m = ex_mathcache_get(c->mc, c->fonts[FMATH].f, code, strlen(code), 1);
                if (m && !ex_math_error(m)) {
                    float msize = c->base * 1.1f, mw = ex_math_width(m) * msize, mh = (ex_math_ascent(m) + ex_math_descent(m)) * msize, mpad = 0.5f * em, avail = c->cw - c->x_left;
                    Item *it;
                    if (mw > avail) { float f2 = avail / mw; msize *= f2; mw = avail; mh *= f2; }
                    add_break(c, c->y);
                    it = new_item(c, IT_MATH);
                    if (it) { it->x = c->x_left + (avail - mw) / 2.0f; it->y = c->y + mpad; it->w = mw; it->h = mh; it->math = m; it->size = msize; it->base_y = ex_math_ascent(m) * msize; it->fg = EX_COLOR_TEXT; }
                    c->y += mh + 2.0f * mpad;
                    break;
                }
                err = m != NULL;
            }
            layout_code(c, code, err || dg_msg, 0, c->cw);
            if (dg_msg) {
                char msg[300];
                Item *mo;
                snprintf(msg, sizeof msg, "Mermaid: %s", dg_msg);
                mo = new_item(c, IT_TEXT);
                if (mo) { float lh = line_height(c, c->base * 0.9f); add_break(c, c->y); mo->x = c->x_left; mo->y = c->y; mo->text = own(c, msg); mo->len = strlen(mo->text); mo->h = lh; mo->w = measure(c, FU_REG, c->base * 0.9f, mo->text, mo->len);
                          mo->font = FU_REG; mo->size = c->base * 0.9f; mo->fg = EX_COLOR_TEXT_MUTED; mo->base_y = baseline_off(c, lh, mo->size); c->y += lh; }
            }
            break;
        }
        case EX_RBK_HR: {
            Item *r;
            add_break(c, c->y);
            r = new_item(c, IT_RECT);
            if (r) { r->x = 0; r->y = c->y + 0.4f * line_height(c, c->base); r->w = c->cw; r->h = 1.0f * c->k; r->bg = EX_COLOR_BORDER; }
            c->y += 0.8f * line_height(c, c->base);
            break;
        }
        case EX_RBK_FOOTNOTES_TITLE: {
            static const ExRSpan title = {EX_RSK_TEXT, EX_RS_BOLD, (char *)"Footnotes", NULL};
            Item *r;
            c->y += line_height(c, c->base);
            add_break(c, c->y);
            r = new_item(c, IT_RECT);
            if (r) { r->x = 0; r->y = c->y; r->w = c->cw; r->h = 1.0f * c->k; r->bg = EX_COLOR_BORDER; }
            c->y += 0.4f * line_height(c, c->base);
            top = c->y;
            flow(c, &title, 1, EX_RS_BOLD, 1.0f, EX_COLOR_TEXT_MUTED);
            break;
        }
        case EX_RBK_TABLE_ROW: {
            size_t j = i, k, cols = 0, cc;
            float *cw, total = 0, y = c->y, pad = 0.3f * em;
            while (j < nb && ex_reading_block(model, j)->kind == EX_RBK_TABLE_ROW && ex_reading_block(model, j)->nframes == b->nframes) j++;
            for (k = i; k < j; k++) if (ex_reading_block(model, k)->ncells > cols) cols = ex_reading_block(model, k)->ncells;
            cw = calloc(cols ? cols : 1, sizeof *cw);
            if (!cw) { c->oom = 1; break; }
            for (k = i; k < j; k++) {
                const ExRBlock *rb = ex_reading_block(model, k);
                for (cc = 0; cc < rb->ncells; cc++) {
                    float w = 0;
                    size_t q;
                    for (q = 0; q < rb->cells[cc].n; q++) {
                        const ExRSpan *sp = &rb->cells[cc].spans[q];
                        const char *tx = sp->text ? sp->text : "";
                        w += measure(c, style_font(sp->style | (rb->header_row ? EX_RS_BOLD : 0)), c->base, tx, strlen(tx));
                    }
                    w += 2 * pad + em * 0.4f;
                    if (w > cw[cc]) cw[cc] = w;
                }
            }
            for (cc = 0; cc < cols; cc++) total += cw[cc];
            if (total > c->cw - c->x_left) { float k2 = (c->cw - c->x_left) / total; for (cc = 0; cc < cols; cc++) cw[cc] *= k2; total = c->cw - c->x_left; }
            for (k = i; k < j; k++) {
                const ExRBlock *rb = ex_reading_block(model, k);
                float x = c->x_left, rowbottom = y;
                size_t first = c->n, brk_mark;
                add_break(c, y);
                brk_mark = c->nbrk;
                for (cc = 0; cc < rb->ncells; cc++) {
                    float save_l = c->x_left, save_r = c->x_right;
                    unsigned bold = rb->header_row ? EX_RS_BOLD : 0;
                    c->x_left = x + pad; c->x_right = x + cw[cc] - pad; c->y = y + pad;
                    if (rb->cells[cc].align == 2 || rb->cells[cc].align == 3) {      /* centered and right-aligned cells: one line, moved */
                        float tw = 0;
                        size_t q;
                        for (q = 0; q < rb->cells[cc].n; q++) { const char *tx = rb->cells[cc].spans[q].text ? rb->cells[cc].spans[q].text : ""; tw += measure(c, style_font(rb->cells[cc].spans[q].style | bold), c->base, tx, strlen(tx)); }
                        if (tw < c->x_right - c->x_left) c->x_left += rb->cells[cc].align == 2 ? (c->x_right - c->x_left - tw) / 2.0f : (c->x_right - c->x_left - tw);
                    }
                    flow(c, rb->cells[cc].spans, rb->cells[cc].n, bold, 1.0f, EX_COLOR_TEXT);
                    { size_t q2; for (q2 = c->n; q2 > first; q2--) if (c->items[q2 - 1].kind == IT_TEXT) { c->items[q2 - 1].flags |= 0x2000u; break; } }   /* a space after a cell's last word keeps the text of a row readable */
                    if (c->y + pad > rowbottom) rowbottom = c->y + pad;
                    c->x_left = save_l; c->x_right = save_r;
                    x += cw[cc];
                }
                /* the cells' breaks inside the row are not places to cut: only the row's top is */
                if (rowbottom < y + line_height(c, c->base) + 2 * pad) rowbottom = y + line_height(c, c->base) + 2 * pad;
                if (rb->header_row) new_deco(c, y, rowbottom, c->x_left, total, EX_COLOR_CODE_BACKGROUND, 0);
                {   Item *ln = new_item(c, IT_RECT);
                    if (ln) { ln->x = c->x_left; ln->y = y; ln->w = total; ln->h = 0.8f * c->k; ln->bg = EX_COLOR_BORDER; } }
                (void)first;
                c->nbrk = brk_mark;                                          /* the lines inside the row are not places to cut */
                y = rowbottom;
            }
            { Item *ln = new_item(c, IT_RECT); if (ln) { ln->x = c->x_left; ln->y = y; ln->w = total; ln->h = 0.8f * c->k; ln->bg = EX_COLOR_BORDER; } }
            c->y = y;
            free(cw);
            i = j - 1;
            break;
        }
        }
        for (f = 0; f < b->nframes; f++) {
            float fx = (float)f * em * 1.0f;
            if (b->frame_callout[f] >= 0) new_deco(c, top - gap / 2.0f, c->y + gap / 2.0f, fx, c->cw - fx, EX_COLOR_CALLOUT_NOTE + (b->frame_callout[f] % 13), 34);
            new_deco(c, top - gap / 2.0f, c->y + gap / 2.0f, fx, bar_w, b->frame_callout[f] >= 0 ? EX_COLOR_CALLOUT_NOTE + (b->frame_callout[f] % 13) : EX_COLOR_BORDER, 0);
        }
        c->y += gap;
    }
}

/* ---- writing the pages ---- */

typedef struct Pg { Ctx *c; float ox, oy; } Pg;   /* the page's origin in the virtual page: virtual (x, y) is at page (ox + x, margin + y - oy) */

static float px(const Pg *g, float x) { return g->ox + x; }
static float py(const Pg *g, float y) { return g->oy + y; }

static void put_text(Ctx *c, int font, float size, float x, float y, const char *s, size_t n, ExPdfColor col) {
    EFont *e = &c->fonts[font];
    uint32_t *gl = malloc((n + 1) * sizeof *gl), *cp = malloc((n + 1) * sizeof *cp);
    size_t i = 0, k = 0;
    int pf = pdf_font(c, font);
    if (!gl || !cp || pf < 0) { free(gl); free(cp); return; }
    while (i < n) {
        uint32_t u = ex_utf8_decode(s, n, &i);
        int sub;
        uint32_t g;
        if (u == '\t') u = ' ';
        if (u == '\n') u = ' ';
        if (u == '\r') continue;
        g = glyph_of(e, u, &sub);
        gl[k] = g; cp[k] = sub ? 0 : u; k++;
    }
    ex_pdfw_text(c->doc, pf, size, x, y, gl, cp, k, col);
    free(gl); free(cp);
}

static void put_card_text(Ctx *c, const Pg *g, const Item *it) {
    put_text(c, it->font, it->size, px(g, it->x) + c->base * 0.6f, py(g, it->y) + it->base_y, it->text, it->len, tok(c, it->fg));
}

static void tick(Ctx *c, const Pg *g, const Item *it) {
    ExPdfColor col = tok(c, it->fg);
    float x = px(g, it->x), y = py(g, it->y), s = it->w;
    ex_pdfw_rect(c->doc, x, y, s, s, s * 0.18f, it->checked ? &col : NULL, &col, 0.8f * c->k);
    if (it->checked) {
        ExPdfColor white = tok(c, EX_COLOR_BACKGROUND);
        float p[6] = { x + s * 0.22f, y + s * 0.52f, x + s * 0.43f, y + s * 0.74f, x + s * 0.78f, y + s * 0.28f };
        ex_pdfw_poly(c->doc, p, 3, 0, NULL, &white, 1.2f * c->k, 0);
    }
}

/* ---- diagrams ---- */

typedef struct Dg { Ctx *c; const Pg *g; float em, x0, y0; } Dg;

static int dcolor_token(int k) {
    switch (k) {
    case EXD_C_TEXT: return EX_COLOR_TEXT;
    case EXD_C_MUTED: case EXD_C_LINE: return EX_COLOR_TEXT_MUTED;
    case EXD_C_NODE_FILL: return EX_COLOR_SURFACE;
    case EXD_C_NODE_STROKE: case EXD_C_ACCENT: case EXD_C_ACCENT_FILL: return EX_COLOR_ACCENT;
    case EXD_C_PANEL: return EX_COLOR_BACKGROUND_ALT;
    case EXD_C_LABEL_BG: return EX_COLOR_BACKGROUND;
    case EXD_C_NOTE_FILL: return EX_COLOR_HIGHLIGHT;
    case EXD_C_ERROR: return EX_COLOR_ERROR;
    default:
        if (k >= EXD_C_CAT) {
            static const unsigned char order[13] = { 0, 5, 6, 7, 9, 11, 4, 10, 1, 8, 12, 2, 3 };
            return EX_COLOR_CALLOUT_NOTE + order[(k - EXD_C_CAT) % 13];
        }
        return EX_COLOR_TEXT;
    }
}
static ExPdfColor dcol(const Dg *d, int k) {
    if (k == EXD_C_ACCENT_FILL) return tint(d->c, EX_COLOR_ACCENT, 70);
    if (k == EXD_C_NOTE_FILL) return tint(d->c, EX_COLOR_HIGHLIGHT, 120);
    return tok(d->c, dcolor_token(k));
}
static float dlum(ExPdfColor c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static void d_poly(Dg *d, const float *xy, int n, int closed, const ExPdfColor *fill, const ExPdfColor *line, float lw, int dash) {
    float *p = malloc((size_t)n * 2 * sizeof *p);
    int i;
    if (!p) return;
    for (i = 0; i < n; i++) { p[2 * i] = px(d->g, xy[2 * i]); p[2 * i + 1] = py(d->g, xy[2 * i + 1]); }
    ex_pdfw_poly(d->c->doc, p, (size_t)n, closed, fill, line, lw, dash);
    free(p);
}

static void d_head(Dg *d, int kind, float tx, float ty, float dx, float dy, float lw, ExPdfColor line, ExPdfColor bg) {
    float len = sqrtf(dx * dx + dy * dy), pxv, pyv, s = 0.75f * d->em, w = 0.32f * d->em;
    if (len < 1e-4f || kind == EXD_H_NONE) return;
    dx /= len; dy /= len; pxv = -dy; pyv = dx;
    switch (kind) {
    case EXD_H_ARROW: { float a[6] = { tx, ty, tx - dx * s + pxv * w, ty - dy * s + pyv * w, tx - dx * s - pxv * w, ty - dy * s - pyv * w }; d_poly(d, a, 3, 1, &line, NULL, lw, 0); break; }
    case EXD_H_OPEN: { float a[6] = { tx - dx * s + pxv * w, ty - dy * s + pyv * w, tx, ty, tx - dx * s - pxv * w, ty - dy * s - pyv * w }; d_poly(d, a, 3, 0, NULL, &line, lw, 0); break; }
    case EXD_H_CROSS: {
        float k = 0.35f * d->em, mx = tx - dx * k, my = ty - dy * k;
        float a[4] = { mx - dx * k + pxv * k, my - dy * k + pyv * k, mx + dx * k - pxv * k, my + dy * k - pyv * k };
        float b[4] = { mx - dx * k - pxv * k, my - dy * k - pyv * k, mx + dx * k + pxv * k, my + dy * k + pyv * k };
        d_poly(d, a, 2, 0, NULL, &line, lw * 1.4f, 0); d_poly(d, b, 2, 0, NULL, &line, lw * 1.4f, 0);
        break;
    }
    case EXD_H_CIRCLE: { float r0 = 0.28f * d->em; ex_pdfw_ellipse(d->c->doc, px(d->g, tx - dx * r0), py(d->g, ty - dy * r0), r0, r0, &bg, &line, lw); break; }
    case EXD_H_TRIANGLE: {
        float a[6] = { tx, ty, tx - dx * s + pxv * w * 1.2f, ty - dy * s + pyv * w * 1.2f, tx - dx * s - pxv * w * 1.2f, ty - dy * s - pyv * w * 1.2f };
        d_poly(d, a, 3, 1, &bg, &line, lw, 0);
        break;
    }
    case EXD_H_DIAMOND: case EXD_H_DIAMOND_FILLED: {
        float a[8] = { tx, ty, tx - dx * s * 0.6f + pxv * w, ty - dy * s * 0.6f + pyv * w, tx - dx * s * 1.2f, ty - dy * s * 1.2f, tx - dx * s * 0.6f - pxv * w, ty - dy * s * 0.6f - pyv * w };
        d_poly(d, a, 4, 1, kind == EXD_H_DIAMOND_FILLED ? &line : &bg, &line, lw, 0);
        break;
    }
    case EXD_H_ONE: case EXD_H_ONLY_ONE: case EXD_H_ZERO_ONE: case EXD_H_MANY: case EXD_H_ONE_MANY: case EXD_H_ZERO_MANY: {
        float k = 0.45f * d->em;
        int bar = kind == EXD_H_ONE || kind == EXD_H_ONLY_ONE || kind == EXD_H_ZERO_ONE || kind == EXD_H_ONE_MANY;
        int ring = kind == EXD_H_ZERO_ONE || kind == EXD_H_ZERO_MANY;
        int crow = kind == EXD_H_MANY || kind == EXD_H_ONE_MANY || kind == EXD_H_ZERO_MANY;
        float bx = tx - dx * (crow ? 1.0f : 0.45f) * k * 1.6f, by = ty - dy * (crow ? 1.0f : 0.45f) * k * 1.6f;
        if (crow) {
            float fx = tx - dx * k * 1.8f, fy = ty - dy * k * 1.8f;
            float a[4] = { fx, fy, tx + pxv * k, ty + pyv * k }, b[4] = { fx, fy, tx - pxv * k, ty - pyv * k }, cc[4] = { fx, fy, tx, ty };
            d_poly(d, a, 2, 0, NULL, &line, lw, 0); d_poly(d, b, 2, 0, NULL, &line, lw, 0); d_poly(d, cc, 2, 0, NULL, &line, lw, 0);
            bx = tx - dx * k * 2.1f; by = ty - dy * k * 2.1f;
        }
        if (bar) {
            float a[4];
            if (kind == EXD_H_ONLY_ONE) { float bx2 = bx - dx * k * 0.5f, by2 = by - dy * k * 0.5f; a[0] = bx2 + pxv * k; a[1] = by2 + pyv * k; a[2] = bx2 - pxv * k; a[3] = by2 - pyv * k; d_poly(d, a, 2, 0, NULL, &line, lw, 0); }
            a[0] = bx + pxv * k; a[1] = by + pyv * k; a[2] = bx - pxv * k; a[3] = by - pyv * k;
            d_poly(d, a, 2, 0, NULL, &line, lw, 0);
        }
        if (ring) { float r0 = 0.28f * d->em; ex_pdfw_ellipse(d->c->doc, px(d->g, bx - dx * (crow ? 0.6f : 1.4f) * k), py(d->g, by - dy * (crow ? 0.6f : 1.4f) * k), r0, r0, &bg, &line, lw); }
        break;
    }
    default: break;
    }
}

static void d_shape(Dg *d, const ExDItem *it) {
    float em = d->em, x = d->x0 + it->x * em, y = d->y0 + it->y * em, w = it->w * em, h = it->h * em, lw = it->width * em, cx = x + w / 2.0f, cy = y + h / 2.0f, p[16];
    ExPdfColor fill = dcol(d, it->fill), line = dcol(d, it->stroke);
    const ExPdfColor *pf = it->fill != EXD_C_NONE ? &fill : NULL, *pl = it->stroke != EXD_C_NONE ? &line : NULL;
    Ctx *c = d->c;
    int n = 0;
    if (lw < 0.5f) lw = 0.5f;
    switch (it->kind) {
    case EXD_RECT: ex_pdfw_rect(c->doc, px(d->g, x), py(d->g, y), w, h, 0, pf, pl, lw); return;
    case EXD_ROUND: ex_pdfw_rect(c->doc, px(d->g, x), py(d->g, y), w, h, 0.5f * em, pf, pl, lw); return;
    case EXD_STADIUM: ex_pdfw_rect(c->doc, px(d->g, x), py(d->g, y), w, h, h / 2.0f, pf, pl, lw); return;
    case EXD_SUBROUTINE: {
        float l1[4] = { x + 0.4f * em, y, x + 0.4f * em, y + h }, l2[4] = { x + w - 0.4f * em, y, x + w - 0.4f * em, y + h };
        ex_pdfw_rect(c->doc, px(d->g, x), py(d->g, y), w, h, 0, pf, pl, lw);
        if (pl) { d_poly(d, l1, 2, 0, NULL, pl, lw, 0); d_poly(d, l2, 2, 0, NULL, pl, lw, 0); }
        return;
    }
    case EXD_DIAMOND: p[0] = cx; p[1] = y; p[2] = x + w; p[3] = cy; p[4] = cx; p[5] = y + h; p[6] = x; p[7] = cy; n = 4; break;
    case EXD_ELLIPSE: ex_pdfw_ellipse(c->doc, px(d->g, cx), py(d->g, cy), w / 2.0f, h / 2.0f, pf, pl, lw); return;
    case EXD_DOUBLE_CIRCLE:
        ex_pdfw_ellipse(c->doc, px(d->g, cx), py(d->g, cy), w / 2.0f, h / 2.0f, pf, pl, lw);
        ex_pdfw_ellipse(c->doc, px(d->g, cx), py(d->g, cy), w / 2.0f - 0.3f * em, h / 2.0f - 0.3f * em, NULL, pl, lw);
        return;
    case EXD_HEXAGON: { float k = 0.7f * em; p[0] = x + k; p[1] = y; p[2] = x + w - k; p[3] = y; p[4] = x + w; p[5] = cy; p[6] = x + w - k; p[7] = y + h; p[8] = x + k; p[9] = y + h; p[10] = x; p[11] = cy; n = 6; break; }
    case EXD_PARALLELOGRAM: { float k = 0.7f * em; p[0] = x + k; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w - k; p[5] = y + h; p[6] = x; p[7] = y + h; n = 4; break; }
    case EXD_PARALLELOGRAM_ALT: { float k = 0.7f * em; p[0] = x; p[1] = y; p[2] = x + w - k; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x + k; p[7] = y + h; n = 4; break; }
    case EXD_TRAPEZOID: { float k = 0.7f * em; p[0] = x + k; p[1] = y; p[2] = x + w - k; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x; p[7] = y + h; n = 4; break; }
    case EXD_TRAPEZOID_ALT: { float k = 0.7f * em; p[0] = x; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w - k; p[5] = y + h; p[6] = x + k; p[7] = y + h; n = 4; break; }
    case EXD_FLAG: { float k = 0.8f * em; p[0] = x + k; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x + k; p[7] = y + h; p[8] = x; p[9] = cy; n = 5; break; }
    case EXD_CYLINDER: {
        float ry = 0.45f * em;
        ex_pdfw_rect(c->doc, px(d->g, x), py(d->g, y + ry), w, h - 2.0f * ry, 0, pf, NULL, 0);
        ex_pdfw_ellipse(c->doc, px(d->g, cx), py(d->g, y + h - ry), w / 2.0f, ry, pf, pl, lw);
        ex_pdfw_ellipse(c->doc, px(d->g, cx), py(d->g, y + ry), w / 2.0f, ry, pf, pl, lw);
        if (pl) { float l1[4] = { x, y + ry, x, y + h - ry }, l2[4] = { x + w, y + ry, x + w, y + h - ry }; d_poly(d, l1, 2, 0, NULL, pl, lw, 0); d_poly(d, l2, 2, 0, NULL, pl, lw, 0); }
        return;
    }
    default: return;
    }
    if (n) d_poly(d, p, n, 1, pf, pl, lw, 0);
}

static void d_line(Dg *d, const ExDiagram *dg, const ExDItem *it) {
    size_t np = it->npt, i, total;
    float *pts = malloc(np * 2 * sizeof(float)), lw = it->width * d->em;
    const float *src = ex_diagram_points(dg, &total);
    ExPdfColor line = dcol(d, it->stroke), bg = dcol(d, EXD_C_LABEL_BG);
    if (!pts) return;
    for (i = 0; i < np; i++) { pts[2 * i] = d->x0 + src[it->pt0 + 2 * i] * d->em; pts[2 * i + 1] = d->y0 + src[it->pt0 + 2 * i + 1] * d->em; }
    if (lw < 0.5f) lw = 0.5f;
    if (np >= 2) {
        d_poly(d, pts, (int)np, 0, NULL, &line, lw, it->dash);
        if (it->head_end) d_head(d, it->head_end, pts[2 * np - 2], pts[2 * np - 1], pts[2 * np - 2] - pts[2 * np - 4], pts[2 * np - 1] - pts[2 * np - 3], lw, line, bg);
        if (it->head_start) d_head(d, it->head_start, pts[0], pts[1], pts[0] - pts[2], pts[1] - pts[3], lw, line, bg);
    }
    free(pts);
}

static void d_text(Dg *d, const ExDiagram *dg, const ExDItem *it) {
    const char *s = ex_diagram_text(dg, it);
    float k = it->size > 0 ? it->size : 1.0f, size = d->em * k, pitch = 1.25f * k * d->em, x = d->x0 + it->x * d->em, yc = d->y0 + it->y * d->em;
    int font = it->bold ? FU_BOLD : FU_REG;
    const EFont *e = &d->c->fonts[font];
    ExPdfColor color = dcol(d, it->stroke);
    size_t pos = 0, len = it->tlen;
    if (it->stroke == EXD_C_BEST) {
        ExPdfColor back = dcol(d, it->fill), a = dcol(d, EXD_C_TEXT), b = dcol(d, EXD_C_LABEL_BG);
        float lb = dlum(back), la = dlum(a), lbb = dlum(b);
        float ca = (la > lb ? la + 0.05f : lb + 0.05f) / (la > lb ? lb + 0.05f : la + 0.05f), cb = (lbb > lb ? lbb + 0.05f : lb + 0.05f) / (lbb > lb ? lb + 0.05f : lbb + 0.05f);
        color = ca >= cb ? a : b;
    }
    while (pos <= len) {
        size_t en = pos;
        float w;
        while (en < len && s[en] != '\n') en++;
        w = measure(d->c, font, size, s + pos, en - pos);
        {
            float lx = it->align == 1 ? x : (it->align == 2 ? x - w : x - w / 2.0f);
            float base = yc - size * (float)e->lineh / 2000.0f + size * (float)e->asc / 1000.0f;
            put_text(d->c, font, size, px(d->g, lx), py(d->g, base), s + pos, en - pos, color);
        }
        yc += pitch;
        if (en >= len) break;
        pos = en + 1;
    }
}

static void d_wedge(Dg *d, const ExDItem *it) {
    float cx = d->x0 + it->x * d->em, cy = d->y0 + it->y * d->em, r = it->w * d->em, a0 = it->a0, a1 = it->a1;
    int seg = (int)((a1 - a0) / 0.1f) + 2, i;
    float p[2 * 130];
    ExPdfColor fill = dcol(d, it->fill), line = dcol(d, it->stroke);
    if (seg > 120) seg = 120;
    if (a1 - a0 >= 6.2831f) { ex_pdfw_ellipse(d->c->doc, px(d->g, cx), py(d->g, cy), r, r, &fill, it->stroke ? &line : NULL, it->width * d->em); return; }
    p[0] = cx; p[1] = cy;
    for (i = 0; i <= seg; i++) { float a = a0 + (a1 - a0) * (float)i / (float)seg; p[2 * (i + 1)] = cx + r * sinf(a); p[2 * (i + 1) + 1] = cy - r * cosf(a); }
    d_poly(d, p, seg + 2, 1, &fill, it->stroke ? &line : NULL, it->width * d->em, 0);
}

static void draw_diagram(Ctx *c, const Pg *g, const Item *it) {
    Dg d;
    size_t n, i;
    const ExDItem *di = ex_diagram_items(it->dg, &n);
    d.c = c; d.g = g; d.em = it->size; d.x0 = it->x; d.y0 = it->y;
    for (i = 0; i < n; i++) {
        switch (di[i].kind) {
        case EXD_LINE: d_line(&d, it->dg, &di[i]); break;
        case EXD_TEXT: d_text(&d, it->dg, &di[i]); break;
        case EXD_WEDGE: d_wedge(&d, &di[i]); break;
        default: d_shape(&d, &di[i]); break;
        }
    }
}

/* ---- math ---- */

static void draw_math(Ctx *c, const Pg *g, const Item *it) {
    size_t n, i;
    const ExMathItem *mi = ex_math_items(it->math, &n);
    int pf = pdf_font(c, FMATH);
    ExPdfColor col = tok(c, it->fg);
    float bx = px(g, it->x), by = py(g, it->y) + it->base_y;
    if (pf < 0) return;
    for (i = 0; i < n; i++) {
        if (mi[i].kind == EX_MATH_GLYPH) {
            uint32_t gl = mi[i].glyph, cp = 0;
            ex_pdfw_text(c->doc, pf, it->size * mi[i].a, bx + mi[i].x * it->size, by + mi[i].y * it->size, &gl, &cp, 1, col);
        } else ex_pdfw_rect(c->doc, bx + mi[i].x * it->size, by + mi[i].y * it->size, mi[i].a * it->size, mi[i].b * it->size, 0, &col, NULL, 0);
    }
}

/* ---- one page ---- */

static void draw_item(Ctx *c, const Pg *g, const Item *it) {
    switch (it->kind) {
    case IT_TEXT: {
        float x = px(g, it->x), y = py(g, it->y);
        ExPdfColor col = tok(c, it->fg);
        size_t vis = it->len;
        float base = y + it->base_y, vw;
        while (vis > 0 && (it->text[vis - 1] == ' ' || it->text[vis - 1] == '\n')) vis--;
        vw = measure(c, it->font, it->size, it->text, vis);
        if (it->bg >= 0) { ExPdfColor bgc = it->bg == EX_COLOR_HIGHLIGHT ? tint(c, it->bg, 120) : tok(c, it->bg); ex_pdfw_rect(c->doc, x - 1.0f, y + it->h * 0.08f, it->w + 1.0f, it->h * 0.84f, it->size * 0.15f, &bgc, NULL, 0); }
        put_text(c, it->font, it->size, x, base, it->text, it->len, col);
        if (it->flags & EX_RS_UNDERLINE) ex_pdfw_rect(c->doc, x, base + it->size * 0.12f, vw, it->size * 0.06f, 0, &col, NULL, 0);
        if (it->flags & EX_RS_STRIKE) ex_pdfw_rect(c->doc, x, base - it->size * 0.3f, vw, it->size * 0.06f, 0, &col, NULL, 0);
        if (it->flags & 0x2000u) put_text(c, it->font, it->size, x + it->w, base, " ", 1, col);
        if (it->uri) ex_pdfw_link(c->doc, x, y, vw, it->h, it->uri);
        break;
    }
    case IT_RECT: {
        ExPdfColor col = it->alpha ? tint(c, it->bg, it->alpha) : tok(c, it->bg);
        ex_pdfw_rect(c->doc, px(g, it->x), py(g, it->y), it->w, it->h, it->radius, &col, NULL, 0);
        break;
    }
    case IT_BOX: tick(c, g, it); break;
    case IT_IMAGE: ex_pdfw_image_draw(c->doc, it->img, px(g, it->x), py(g, it->y), it->w, it->h); break;
    case IT_MATH: draw_math(c, g, it); break;
    case IT_DIAGRAM: draw_diagram(c, g, it); break;
    case IT_CARD: {
        ExPdfColor f = tok(c, it->bg), s = tok(c, it->stroke);
        ex_pdfw_rect(c->doc, px(g, it->x), py(g, it->y), it->w, it->h, it->radius, &f, &s, 0.6f * c->k);
        put_card_text(c, g, it);
        break;
    }
    }
}

static void write_page(Ctx *c, float y0, float y1) {
    Pg g;
    size_t i;
    g.c = c; g.ox = c->margin; g.oy = c->margin - y0;
    ex_pdfw_page_begin(c->doc, c->page_w, c->page_h);
    for (i = 0; i < c->nd; i++) {
        const Deco *d = &c->decos[i];
        float a = d->y0 > y0 ? d->y0 : y0, b = d->y1 < y1 ? d->y1 : y1;
        if (b - a > 0.01f) {
            ExPdfColor col = tint(c, d->color, d->alpha);
            ex_pdfw_rect(c->doc, px(&g, d->x), py(&g, a), d->w, b - a, 0, &col, NULL, 0);
        }
    }
    for (i = 0; i < c->n; i++) {
        const Item *it = &c->items[i];
        if (it->y >= y0 - 0.01f && it->y < y1 - 0.01f) draw_item(c, &g, it);
    }
}

/* ---- the whole export ---- */

int ex_pdf_page_size(const char *name, float *w, float *h) {
    static const struct { const char *n; float w, h; } sz[] = { {"A3", 841.89f, 1190.55f}, {"A4", 595.28f, 841.89f}, {"A5", 419.53f, 595.28f}, {"Legal", 612.0f, 1008.0f}, {"Letter", 612.0f, 792.0f}, {"Tabloid", 792.0f, 1224.0f} };
    size_t i;
    for (i = 0; name && i < sizeof sz / sizeof *sz; i++) if (!strcmp(name, sz[i].n)) { *w = sz[i].w; *h = sz[i].h; return 0; }
    return -1;
}

void ex_pdf_export_free(ExPdfExportOut *out) { if (out) { free(out->pdf); out->pdf = NULL; out->len = 0; } }

static void free_ctx(Ctx *c) {
    size_t i;
    int f;
    for (i = 0; i < c->nown; i++) free(c->owned[i]);
    free(c->owned); free(c->items); free(c->decos); free(c->brk);
    for (i = 0; i < c->nimgs; i++) free(c->imgs[i].path);
    free(c->imgs);
    for (i = 0; i < c->ndg; i++) ex_diagram_free(c->dgs[i]);
    free(c->dgs);
    for (f = 0; f < NFONTS; f++) { free(c->fonts[f].adv); free(c->fonts[f].have); }
    ex_mathcache_free(c->mc);
    ex_pdfw_free(c->doc);
    ex_theme_free(&c->theme);
    ex_fontlib_destroy(c->lib);
}

int ex_pdf_export(const ExPdfExportIn *in, ExPdfExportOut *out) {
    Ctx c;
    ExReading *model = NULL;
    float pw = 595.28f, ph = 841.89f, y0 = 0, total;
    int rc = -1, k;
    size_t bi = 0;
    memset(out, 0, sizeof *out);
    if (!in || !in->text) { snprintf(out->err, sizeof out->err, "There is nothing to export."); return -1; }
    memset(&c, 0, sizeof c);
    c.in = in;
    ex_pdf_page_size(in->opt.page_size, &pw, &ph);
    if (in->opt.landscape) { float t = pw; pw = ph; ph = t; }
    c.page_w = pw; c.page_h = ph;
    c.margin = in->opt.margin && !strcmp(in->opt.margin, "none") ? 0.0f : in->opt.margin && !strcmp(in->opt.margin, "minimal") ? 28.35f : 56.7f;
    k = in->opt.downscale_percent;
    if (k < 10) k = 10;
    if (k > 200) k = 200;
    c.k = (float)k / 100.0f;
    c.base = 11.0f * c.k; c.lh_mul = 1.35f;
    c.cw = pw - 2 * c.margin; c.ch = ph - 2 * c.margin;
    if (c.cw < 60 || c.ch < 60) { snprintf(out->err, sizeof out->err, "The margins leave no room for text on this page size."); return -1; }
    if (ex_theme_init_builtin(&c.theme, EX_THEME_LIGHT) != 0) { snprintf(out->err, sizeof out->err, "The light theme could not be loaded."); return -1; }
    if (open_fonts(&c) != 0) { snprintf(out->err, sizeof out->err, "The fonts could not be loaded."); goto done; }
    c.mc = ex_mathcache_create();
    c.doc = ex_pdfw_create();
    if (!c.doc || !c.mc) { snprintf(out->err, sizeof out->err, "Out of memory."); goto done; }
    model = ex_reading_build(in->text, in->len);
    if (!model) { snprintf(out->err, sizeof out->err, "The note could not be read."); goto done; }
    ex_pdfw_set_title(c.doc, in->title);
    layout_blocks(&c, model, in->opt.include_title, in->title);
    if (c.cancelled) { out->cancelled = 1; snprintf(out->err, sizeof out->err, "Cancelled"); goto done; }
    if (c.oom) { snprintf(out->err, sizeof out->err, "Out of memory."); goto done; }
    total = c.y;
    if (total < 1.0f) total = 1.0f;
    /* cut the virtual page where a unit starts */
    for (;;) {
        float limit = y0 + c.ch, end;
        int last = total <= limit + 0.5f;
        if (in->cancel && *in->cancel) { out->cancelled = 1; snprintf(out->err, sizeof out->err, "Cancelled"); goto done; }
        if (last) end = total + 1.0f;
        else {
            end = limit;
            while (bi < c.nbrk && c.brk[bi] <= y0 + 0.01f) bi++;
            {   size_t j = bi;
                float best = -1;
                while (j < c.nbrk && c.brk[j] <= limit + 0.01f) { best = c.brk[j]; j++; }
                if (best > y0 + 1.0f) end = best;
            }
        }
        write_page(&c, y0, end);
        if (in->progress) in->progress(in->user, 500 + (int)(500.0f * (end > total ? total : end) / total), (int)ex_pdfw_page_count(c.doc));
        if (last) break;
        y0 = end;
    }
    if (ex_pdfw_finish(c.doc, &out->pdf, &out->len) != 0) { snprintf(out->err, sizeof out->err, "The PDF could not be written (out of memory or a font that cannot be cut)."); goto done; }
    out->pages = (int)ex_pdfw_page_count(c.doc);
    rc = 0;
done:
    ex_reading_free(model);
    free_ctx(&c);
    return rc;
}
