/* Exhaust's own PDF writer (sub-slice 6e-iv): see include/exhaust_pdfwrite.h. */
#include "exhaust_pdfwrite.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "exhaust_fontsubset.h"

typedef struct Buf { unsigned char *p; size_t n, cap; int oom; } Buf;

static void buf_put(Buf *b, const void *s, size_t n) {
    if (b->oom || n == 0) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        unsigned char *np;
        while (nc < b->n + n + 1) nc *= 2;
        np = realloc(b->p, nc);
        if (!np) { b->oom = 1; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void buf_str(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
static void buf_fmt(Buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    int k;
    va_start(ap, fmt);
    k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k > 0) buf_put(b, tmp, (size_t)(k < (int)sizeof tmp ? k : (int)sizeof tmp - 1));
}
static void buf_free(Buf *b) { free(b->p); memset(b, 0, sizeof *b); }

/* deflate into `out` (appended) */
static int flate(const void *src, size_t n, Buf *out) {
    z_stream zs;
    size_t bound;
    unsigned char *tmp;
    int rc;
    memset(&zs, 0, sizeof zs);
    if (deflateInit(&zs, 6) != Z_OK) return -1;
    bound = deflateBound(&zs, (uLong)n) + 16;
    tmp = malloc(bound);
    if (!tmp) { deflateEnd(&zs); return -1; }
    zs.next_in = (Bytef *)src; zs.avail_in = (uInt)n;
    zs.next_out = tmp; zs.avail_out = (uInt)bound;
    rc = deflate(&zs, Z_FINISH);
    if (rc == Z_STREAM_END) buf_put(out, tmp, bound - zs.avail_out);
    deflateEnd(&zs);
    free(tmp);
    return rc == Z_STREAM_END && !out->oom ? 0 : -1;
}

typedef struct Link { float x, y, w, h; char *uri; } Link;

typedef struct Page {
    float w, h;
    Buf content;
    int *fonts; size_t nfonts, capfonts;
    int *imgs; size_t nimgs, capimgs;
    Link *links; size_t nlinks, caplinks;
} Page;

typedef struct Font {
    char name[48];
    const unsigned char *data; size_t len;
    ExPdfAdvanceFn adv; void *user;
    int ascent, descent, fixed, italic, cff;
    uint8_t *used;          /* bit set of 65536 glyph ids */
    uint32_t *cp;           /* glyph id -> code point (0 none) */
    size_t nused;
} Font;

typedef struct Image { Buf rgb, alpha; int w, h, has_alpha; } Image;

struct ExPdfDoc {
    Page *pages; size_t npages, cappages;
    Font *fonts; size_t nfonts, capfonts;
    Image *imgs; size_t nimgs, capimgs;
    char *title;
    int oom;
};

ExPdfDoc *ex_pdfw_create(void) { return calloc(1, sizeof(ExPdfDoc)); }

void ex_pdfw_free(ExPdfDoc *d) {
    size_t i, k;
    if (!d) return;
    for (i = 0; i < d->npages; i++) {
        buf_free(&d->pages[i].content);
        free(d->pages[i].fonts); free(d->pages[i].imgs);
        for (k = 0; k < d->pages[i].nlinks; k++) free(d->pages[i].links[k].uri);
        free(d->pages[i].links);
    }
    for (i = 0; i < d->nfonts; i++) { free(d->fonts[i].used); free(d->fonts[i].cp); }
    for (i = 0; i < d->nimgs; i++) { buf_free(&d->imgs[i].rgb); buf_free(&d->imgs[i].alpha); }
    free(d->pages); free(d->fonts); free(d->imgs); free(d->title);
    free(d);
}

void ex_pdfw_set_title(ExPdfDoc *d, const char *utf8) {
    if (!d) return;
    free(d->title);
    d->title = utf8 ? malloc(strlen(utf8) + 1) : NULL;
    if (d->title) strcpy(d->title, utf8);
}

int ex_pdfw_font_add(ExPdfDoc *d, const char *name, const unsigned char *data, size_t len, ExPdfAdvanceFn adv, void *user, int ascent, int descent, int fixed_pitch, int italic) {
    Font *f;
    size_t i, k = 0;
    if (!d || !data || len < 12 || !adv) return -1;
    if (d->nfonts == d->capfonts) {
        size_t nc = d->capfonts ? d->capfonts * 2 : 8;
        Font *nf = realloc(d->fonts, nc * sizeof *nf);
        if (!nf) return -1;
        d->fonts = nf; d->capfonts = nc;
    }
    f = &d->fonts[d->nfonts];
    memset(f, 0, sizeof *f);
    for (i = 0; name && name[i] && k < sizeof f->name - 1; i++) if ((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= 'a' && name[i] <= 'z')) f->name[k++] = name[i];
    if (k == 0) snprintf(f->name, sizeof f->name, "Font%zu", d->nfonts);
    f->data = data; f->len = len; f->adv = adv; f->user = user;
    f->ascent = ascent; f->descent = descent; f->fixed = fixed_pitch; f->italic = italic;
    f->cff = !memcmp(data, "OTTO", 4);
    f->used = calloc(65536 / 8, 1);
    f->cp = calloc(65536, sizeof *f->cp);
    if (!f->used || !f->cp) { free(f->used); free(f->cp); return -1; }
    return (int)d->nfonts++;
}

int ex_pdfw_page_begin(ExPdfDoc *d, float width, float height) {
    Page *p;
    if (!d || width <= 0 || height <= 0) return -1;
    if (d->npages == d->cappages) {
        size_t nc = d->cappages ? d->cappages * 2 : 16;
        Page *np = realloc(d->pages, nc * sizeof *np);
        if (!np) return -1;
        d->pages = np; d->cappages = nc;
    }
    p = &d->pages[d->npages];
    memset(p, 0, sizeof *p);
    p->w = width; p->h = height;
    return (int)d->npages++;
}

size_t ex_pdfw_page_count(const ExPdfDoc *d) { return d ? d->npages : 0; }

static Page *cur(ExPdfDoc *d) { return d && d->npages ? &d->pages[d->npages - 1] : NULL; }

static void use_int(int **a, size_t *n, size_t *cap, int v) {
    size_t i;
    for (i = 0; i < *n; i++) if ((*a)[i] == v) return;
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        int *na = realloc(*a, nc * sizeof **a);
        if (!na) return;
        *a = na; *cap = nc;
    }
    (*a)[(*n)++] = v;
}

/* a number with at most two decimals, no trailing zeros */
static void num(Buf *b, float v) {
    char t[40];
    int k;
    if (v != v) v = 0;
    k = snprintf(t, sizeof t, "%.2f", (double)v);
    while (k > 0 && t[k - 1] == '0') k--;
    if (k > 0 && t[k - 1] == '.') k--;
    if (k == 0 || (k == 2 && t[0] == '-' && t[1] == '0')) { t[0] = '0'; k = 1; }
    buf_put(b, t, (size_t)k);
    buf_put(b, " ", 1);
}
static void color_op(Buf *b, const ExPdfColor *c, const char *op) { num(b, c->r); num(b, c->g); num(b, c->b); buf_str(b, op); buf_put(b, "\n", 1); }

void ex_pdfw_text(ExPdfDoc *d, int font, float size, float x, float y, const uint32_t *glyphs, const uint32_t *cps, size_t n, ExPdfColor c) {
    Page *p = cur(d);
    Font *f;
    size_t i;
    if (!p || font < 0 || (size_t)font >= d->nfonts || n == 0 || size <= 0) return;
    f = &d->fonts[font];
    use_int(&p->fonts, &p->nfonts, &p->capfonts, font);
    buf_str(&p->content, "q\n");
    color_op(&p->content, &c, "rg");
    buf_fmt(&p->content, "BT /F%d ", font);
    num(&p->content, size);
    buf_str(&p->content, "Tf ");
    num(&p->content, x); num(&p->content, p->h - y);
    buf_str(&p->content, "Td <");
    for (i = 0; i < n; i++) {
        uint32_t g = glyphs[i] & 0xffff;
        if (!(f->used[g >> 3] & (1u << (g & 7)))) { f->used[g >> 3] |= (uint8_t)(1u << (g & 7)); f->nused++; }
        if (cps && cps[i] && !f->cp[g]) f->cp[g] = cps[i];
        buf_fmt(&p->content, "%04X", (unsigned)g);
    }
    buf_str(&p->content, "> Tj ET\nQ\n");
}

static void paint(Buf *b, const ExPdfColor *fill, const ExPdfColor *stroke, int closed) {
    if (fill && stroke) buf_str(b, closed ? "b\n" : "B\n");
    else if (fill) buf_str(b, "f\n");
    else if (stroke) buf_str(b, closed ? "s\n" : "S\n");
    else buf_str(b, "n\n");
}

static void style(Buf *b, const ExPdfColor *fill, const ExPdfColor *stroke, float w, int dash) {
    if (fill) color_op(b, fill, "rg");
    if (stroke) {
        color_op(b, stroke, "RG");
        num(b, w > 0 ? w : 0.5f); buf_str(b, "w\n");
        if (dash == 1) buf_str(b, "[4 3] 0 d\n");
        else if (dash == 2) buf_str(b, "[1 2] 0 d\n");
    }
}

static void rrect_path(Buf *b, float x, float y, float w, float h, float r, float H) {
    const float k = 0.5523f;
    float yb = H - y - h;     /* the bottom edge in PDF's coordinates */
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0.01f) { num(b, x); num(b, yb); num(b, w); num(b, h); buf_str(b, "re\n"); return; }
    num(b, x + r); num(b, yb); buf_str(b, "m\n");
    num(b, x + w - r); num(b, yb); buf_str(b, "l\n");
    num(b, x + w - r + r * k); num(b, yb); num(b, x + w); num(b, yb + r - r * k); num(b, x + w); num(b, yb + r); buf_str(b, "c\n");
    num(b, x + w); num(b, yb + h - r); buf_str(b, "l\n");
    num(b, x + w); num(b, yb + h - r + r * k); num(b, x + w - r + r * k); num(b, yb + h); num(b, x + w - r); num(b, yb + h); buf_str(b, "c\n");
    num(b, x + r); num(b, yb + h); buf_str(b, "l\n");
    num(b, x + r - r * k); num(b, yb + h); num(b, x); num(b, yb + h - r + r * k); num(b, x); num(b, yb + h - r); buf_str(b, "c\n");
    num(b, x); num(b, yb + r); buf_str(b, "l\n");
    num(b, x); num(b, yb + r - r * k); num(b, x + r - r * k); num(b, yb); num(b, x + r); num(b, yb); buf_str(b, "c\nh\n");
}

void ex_pdfw_rect(ExPdfDoc *d, float x, float y, float w, float h, float r, const ExPdfColor *fill, const ExPdfColor *stroke, float stroke_w) {
    Page *p = cur(d);
    if (!p || w <= 0 || h <= 0) return;
    buf_str(&p->content, "q\n");
    style(&p->content, fill, stroke, stroke_w, 0);
    rrect_path(&p->content, x, y, w, h, r, p->h);
    paint(&p->content, fill, stroke, 0);
    buf_str(&p->content, "Q\n");
}

void ex_pdfw_poly(ExPdfDoc *d, const float *pts, size_t n, int closed, const ExPdfColor *fill, const ExPdfColor *stroke, float stroke_w, int dash) {
    Page *p = cur(d);
    size_t i;
    if (!p || n < 2) return;
    buf_str(&p->content, "q\n");
    style(&p->content, fill, stroke, stroke_w, dash);
    for (i = 0; i < n; i++) { num(&p->content, pts[2 * i]); num(&p->content, p->h - pts[2 * i + 1]); buf_str(&p->content, i ? "l\n" : "m\n"); }
    if (closed || (fill && !stroke)) paint(&p->content, fill, stroke, closed);
    else paint(&p->content, fill, stroke, 0);
    buf_str(&p->content, "Q\n");
}

void ex_pdfw_ellipse(ExPdfDoc *d, float cx, float cy, float rx, float ry, const ExPdfColor *fill, const ExPdfColor *stroke, float stroke_w) {
    Page *p = cur(d);
    const float k = 0.5523f;
    float H, y;
    if (!p || rx <= 0 || ry <= 0) return;
    H = p->h; y = H - cy;
    buf_str(&p->content, "q\n");
    style(&p->content, fill, stroke, stroke_w, 0);
    num(&p->content, cx + rx); num(&p->content, y); buf_str(&p->content, "m\n");
    num(&p->content, cx + rx); num(&p->content, y + ry * k); num(&p->content, cx + rx * k); num(&p->content, y + ry); num(&p->content, cx); num(&p->content, y + ry); buf_str(&p->content, "c\n");
    num(&p->content, cx - rx * k); num(&p->content, y + ry); num(&p->content, cx - rx); num(&p->content, y + ry * k); num(&p->content, cx - rx); num(&p->content, y); buf_str(&p->content, "c\n");
    num(&p->content, cx - rx); num(&p->content, y - ry * k); num(&p->content, cx - rx * k); num(&p->content, y - ry); num(&p->content, cx); num(&p->content, y - ry); buf_str(&p->content, "c\n");
    num(&p->content, cx + rx * k); num(&p->content, y - ry); num(&p->content, cx + rx); num(&p->content, y - ry * k); num(&p->content, cx + rx); num(&p->content, y); buf_str(&p->content, "c\nh\n");
    paint(&p->content, fill, stroke, 1);
    buf_str(&p->content, "Q\n");
}

void ex_pdfw_clip_begin(ExPdfDoc *d, float x, float y, float w, float h) {
    Page *p = cur(d);
    if (!p) return;
    buf_str(&p->content, "q\n");
    num(&p->content, x); num(&p->content, p->h - y - h); num(&p->content, w > 0 ? w : 0); num(&p->content, h > 0 ? h : 0);
    buf_str(&p->content, "re W n\n");
}
void ex_pdfw_clip_end(ExPdfDoc *d) { Page *p = cur(d); if (p) buf_str(&p->content, "Q\n"); }

int ex_pdfw_image_add(ExPdfDoc *d, const uint8_t *rgba, int w, int h) {
    Image *im;
    uint8_t *rgb = NULL, *al = NULL;
    size_t i, np;
    int has_alpha = 0;
    if (!d || !rgba || w <= 0 || h <= 0 || (size_t)w * (size_t)h > 100000000u) return -1;
    np = (size_t)w * (size_t)h;
    rgb = malloc(np * 3); al = malloc(np);
    if (!rgb || !al) { free(rgb); free(al); return -1; }
    for (i = 0; i < np; i++) {
        uint8_t a = rgba[4 * i + 3];
        rgb[3 * i] = rgba[4 * i]; rgb[3 * i + 1] = rgba[4 * i + 1]; rgb[3 * i + 2] = rgba[4 * i + 2];
        al[i] = a;
        if (a != 255) has_alpha = 1;
    }
    if (d->nimgs == d->capimgs) {
        size_t nc = d->capimgs ? d->capimgs * 2 : 8;
        Image *ni = realloc(d->imgs, nc * sizeof *ni);
        if (!ni) { free(rgb); free(al); return -1; }
        d->imgs = ni; d->capimgs = nc;
    }
    im = &d->imgs[d->nimgs];
    memset(im, 0, sizeof *im);
    im->w = w; im->h = h; im->has_alpha = has_alpha;
    if (flate(rgb, np * 3, &im->rgb) != 0 || (has_alpha && flate(al, np, &im->alpha) != 0)) { buf_free(&im->rgb); buf_free(&im->alpha); free(rgb); free(al); return -1; }
    free(rgb); free(al);
    return (int)d->nimgs++;
}

void ex_pdfw_image_draw(ExPdfDoc *d, int image, float x, float y, float w, float h) {
    Page *p = cur(d);
    if (!p || image < 0 || (size_t)image >= d->nimgs || w <= 0 || h <= 0) return;
    use_int(&p->imgs, &p->nimgs, &p->capimgs, image);
    buf_str(&p->content, "q\n");
    num(&p->content, w); buf_str(&p->content, "0 0 "); num(&p->content, h); num(&p->content, x); num(&p->content, p->h - y - h);
    buf_fmt(&p->content, "cm /I%d Do\nQ\n", image);
}

void ex_pdfw_link(ExPdfDoc *d, float x, float y, float w, float h, const char *uri) {
    Page *p = cur(d);
    Link *l;
    if (!p || !uri || !*uri) return;
    if (p->nlinks == p->caplinks) {
        size_t nc = p->caplinks ? p->caplinks * 2 : 8;
        Link *nl = realloc(p->links, nc * sizeof *nl);
        if (!nl) return;
        p->links = nl; p->caplinks = nc;
    }
    l = &p->links[p->nlinks];
    l->x = x; l->y = y; l->w = w; l->h = h;
    l->uri = malloc(strlen(uri) + 1);
    if (!l->uri) return;
    strcpy(l->uri, uri);
    p->nlinks++;
}

/* ---- writing the file ---- */

typedef struct Out { Buf b; size_t *off; size_t noff, capoff; } Out;

static void obj_begin(Out *o, size_t id) {
    if (id >= o->capoff) {
        size_t nc = o->capoff ? o->capoff : 64;
        size_t *no;
        while (nc <= id) nc *= 2;
        no = realloc(o->off, nc * sizeof *no);
        if (!no) { o->b.oom = 1; return; }
        memset(no + o->capoff, 0, (nc - o->capoff) * sizeof *no);
        o->off = no; o->capoff = nc;
    }
    if (id + 1 > o->noff) o->noff = id + 1;
    o->off[id] = o->b.n;
    buf_fmt(&o->b, "%zu 0 obj\n", id);
}
static void obj_end(Out *o) { buf_str(&o->b, "endobj\n"); }

/* a stream object: `dict` is the entries before /Length; the data is deflated when `deflate` is set */
static void stream_obj(Out *o, size_t id, const char *dict, const void *data, size_t n, int deflate) {
    Buf z = {0};
    const void *p = data;
    size_t pn = n;
    obj_begin(o, id);
    if (deflate && flate(data, n, &z) == 0) { p = z.p; pn = z.n; buf_fmt(&o->b, "<< %s /Filter /FlateDecode /Length %zu >>\nstream\n", dict, pn); }
    else buf_fmt(&o->b, "<< %s /Length %zu >>\nstream\n", dict, pn);
    buf_put(&o->b, p, pn);
    buf_str(&o->b, "\nendstream\n");
    obj_end(o);
    buf_free(&z);
}

static void hex16(Buf *b, uint32_t cp) {
    if (cp > 0x10FFFFu || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp >= 0x10000) { uint32_t v = cp - 0x10000; buf_fmt(b, "%04X%04X", (unsigned)(0xD800 + (v >> 10)), (unsigned)(0xDC00 + (v & 0x3ff))); }
    else buf_fmt(b, "%04X", (unsigned)cp);
}

static void tounicode(Buf *b, const Font *f) {
    size_t g, in_chunk = 0;
    buf_str(b, "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
               "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n");
    for (g = 0; g < 65536; g++) {
        if (!(f->used[g >> 3] & (1u << (g & 7))) || !f->cp[g]) continue;
        if (in_chunk == 0) {
            size_t k = 0, h;
            for (h = g; h < 65536 && k < 100; h++) if ((f->used[h >> 3] & (1u << (h & 7))) && f->cp[h]) k++;
            buf_fmt(b, "%zu beginbfchar\n", k);
        }
        buf_fmt(b, "<%04X> <", (unsigned)g); hex16(b, f->cp[g]); buf_str(b, ">\n");
        if (++in_chunk == 100) { buf_str(b, "endbfchar\n"); in_chunk = 0; }
    }
    if (in_chunk) buf_str(b, "endbfchar\n");
    buf_str(b, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
}

static void subset_tag(char tag[7], const Font *f) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < 65536 / 8; i++) h = (h ^ f->used[i]) * 16777619u;
    for (i = 0; i < f->len && i < 4096; i += 7) h = (h ^ f->data[i]) * 16777619u;
    for (i = 0; i < 6; i++) { tag[i] = (char)('A' + h % 26); h = h / 26 + (uint32_t)i * 2654435761u; }
    tag[6] = 0;
}

static int write_font(Out *o, const Font *f, size_t base) {
    uint32_t *gl = malloc((f->nused + 1) * sizeof *gl);
    size_t i, ng = 0, slen = 0;
    unsigned char *sub;
    Buf w = {0}, tu = {0};
    char tag[7], dict[256];
    if (!gl) return -1;
    for (i = 0; i < 65536; i++) if (f->used[i >> 3] & (1u << (i & 7))) gl[ng++] = (uint32_t)i;
    sub = ex_font_subset(f->data, f->len, gl, ng, NULL, 0, &slen);
    if (!sub) { free(gl); return -1; }
    subset_tag(tag, f);
    /* base: Type0, base+1: CIDFont, base+2: descriptor, base+3: font file, base+4: ToUnicode */
    obj_begin(o, base);
    buf_fmt(&o->b, "<< /Type /Font /Subtype /Type0 /BaseFont /%s+%s /Encoding /Identity-H /DescendantFonts [%zu 0 R] /ToUnicode %zu 0 R >>\n", tag, f->name, base + 1, base + 4);
    obj_end(o);
    buf_str(&w, "[");
    for (i = 0; i < ng; i++) { float a = f->adv(f->user, gl[i]); buf_fmt(&w, "%u [%d] ", (unsigned)gl[i], (int)(a + (a < 0 ? -0.5f : 0.5f))); }
    buf_str(&w, "]");
    obj_begin(o, base + 1);
    buf_fmt(&o->b, "<< /Type /Font /Subtype /%s /BaseFont /%s+%s /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor %zu 0 R /DW 1000 /W ",
            f->cff ? "CIDFontType0" : "CIDFontType2", tag, f->name, base + 2);
    buf_put(&o->b, w.p ? w.p : (const unsigned char *)"[]", w.p ? w.n : 2);
    buf_str(&o->b, f->cff ? " >>\n" : " /CIDToGIDMap /Identity >>\n");
    obj_end(o);
    obj_begin(o, base + 2);
    buf_fmt(&o->b, "<< /Type /FontDescriptor /FontName /%s+%s /Flags %d /FontBBox [-200 %d 1200 %d] /ItalicAngle %d /Ascent %d /Descent %d /CapHeight %d /StemV 80 /%s %zu 0 R >>\n",
            tag, f->name, 32 | (f->fixed ? 1 : 0) | (f->italic ? 64 : 0), -f->descent, f->ascent, f->italic ? -12 : 0, f->ascent, -f->descent, f->ascent * 7 / 10,
            f->cff ? "FontFile3" : "FontFile2", base + 3);
    obj_end(o);
    if (f->cff) snprintf(dict, sizeof dict, "/Subtype /OpenType");
    else snprintf(dict, sizeof dict, "/Length1 %zu", slen);
    stream_obj(o, base + 3, dict, sub, slen, 1);
    tounicode(&tu, f);
    stream_obj(o, base + 4, "", tu.p, tu.n, 1);
    buf_free(&w); buf_free(&tu); free(sub); free(gl);
    return 0;
}

static void put_uri(Buf *b, const char *u) {
    buf_str(b, "(");
    for (; *u; u++) {
        unsigned char c = (unsigned char)*u;
        if (c == '(' || c == ')' || c == '\\') { buf_put(b, "\\", 1); buf_put(b, &c, 1); }
        else if (c < 33 || c > 126) buf_fmt(b, "%%%02X", c);
        else buf_put(b, &c, 1);
    }
    buf_str(b, ")");
}

static void put_title(Buf *b, const char *s) {
    size_t i = 0, n = strlen(s);
    buf_str(b, "<FEFF");
    while (i < n) {
        uint32_t cp = 0xFFFD;
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > n) len = 1;
        if (len == 1) cp = c < 0x80 ? c : 0xFFFD;
        else if (len == 2) cp = ((c & 0x1f) << 6) | (s[i + 1] & 0x3f);
        else if (len == 3) cp = ((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f);
        else cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[i + 1] & 0x3f) << 12) | ((s[i + 2] & 0x3f) << 6) | (s[i + 3] & 0x3f);
        hex16(b, cp);
        i += len;
    }
    buf_str(b, ">");
}

int ex_pdfw_finish(ExPdfDoc *d, unsigned char **out, size_t *len) {
    Out o;
    size_t next = 4, i, k, *font_base = NULL, *img_id = NULL, *page_id = NULL, xref_at, size;
    int rc = -1;
    if (out) *out = NULL;
    if (len) *len = 0;
    if (!d || !out || !len || d->npages == 0) return -1;
    memset(&o, 0, sizeof o);
    font_base = calloc(d->nfonts + 1, sizeof *font_base);
    img_id = calloc(d->nimgs + 1, sizeof *img_id);
    page_id = calloc(d->npages + 1, sizeof *page_id);
    if (!font_base || !img_id || !page_id) goto done;
    buf_str(&o.b, "%PDF-1.5\n%\xE2\xE3\xCF\xD3\n");
    for (i = 0; i < d->nfonts; i++) {
        font_base[i] = next; next += 5;
        if (write_font(&o, &d->fonts[i], font_base[i]) != 0) goto done;
    }
    for (i = 0; i < d->nimgs; i++) {
        const Image *im = &d->imgs[i];
        char dict[256];
        img_id[i] = next++;
        if (im->has_alpha) {
            size_t sm = next++;
            snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8", im->w, im->h);
            obj_begin(&o, sm);
            buf_fmt(&o.b, "<< %s /Filter /FlateDecode /Length %zu >>\nstream\n", dict, im->alpha.n);
            buf_put(&o.b, im->alpha.p, im->alpha.n);
            buf_str(&o.b, "\nendstream\n");
            obj_end(&o);
            snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask %zu 0 R", im->w, im->h, sm);
        } else snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8", im->w, im->h);
        obj_begin(&o, img_id[i]);
        buf_fmt(&o.b, "<< %s /Filter /FlateDecode /Length %zu >>\nstream\n", dict, im->rgb.n);
        buf_put(&o.b, im->rgb.p, im->rgb.n);
        buf_str(&o.b, "\nendstream\n");
        obj_end(&o);
    }
    for (i = 0; i < d->npages; i++) {
        const Page *p = &d->pages[i];
        size_t content = next + 1, first_annot = next + 2;
        page_id[i] = next;
        next += 2 + p->nlinks;
        stream_obj(&o, content, "", p->content.p ? p->content.p : (unsigned char *)"", p->content.n, 1);
        for (k = 0; k < p->nlinks; k++) {
            const Link *l = &p->links[k];
            obj_begin(&o, first_annot + k);
            buf_str(&o.b, "<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [");
            num(&o.b, l->x); num(&o.b, p->h - l->y - l->h); num(&o.b, l->x + l->w); num(&o.b, p->h - l->y);
            buf_str(&o.b, "] /A << /S /URI /URI ");
            put_uri(&o.b, l->uri);
            buf_str(&o.b, " >> >>\n");
            obj_end(&o);
        }
        obj_begin(&o, page_id[i]);
        buf_str(&o.b, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
        num(&o.b, p->w); num(&o.b, p->h);
        buf_str(&o.b, "] /Resources << /ProcSet [/PDF /Text /ImageC /ImageB]");
        if (p->nfonts) {
            buf_str(&o.b, " /Font <<");
            for (k = 0; k < p->nfonts; k++) buf_fmt(&o.b, " /F%d %zu 0 R", p->fonts[k], font_base[p->fonts[k]]);
            buf_str(&o.b, " >>");
        }
        if (p->nimgs) {
            buf_str(&o.b, " /XObject <<");
            for (k = 0; k < p->nimgs; k++) buf_fmt(&o.b, " /I%d %zu 0 R", p->imgs[k], img_id[p->imgs[k]]);
            buf_str(&o.b, " >>");
        }
        buf_fmt(&o.b, " >> /Contents %zu 0 R", content);
        if (p->nlinks) {
            buf_str(&o.b, " /Annots [");
            for (k = 0; k < p->nlinks; k++) buf_fmt(&o.b, "%zu 0 R ", first_annot + k);
            buf_str(&o.b, "]");
        }
        buf_str(&o.b, " >>\n");
        obj_end(&o);
    }
    obj_begin(&o, 1);
    buf_str(&o.b, "<< /Type /Catalog /Pages 2 0 R >>\n");
    obj_end(&o);
    obj_begin(&o, 2);
    buf_fmt(&o.b, "<< /Type /Pages /Count %zu /Kids [", d->npages);
    for (i = 0; i < d->npages; i++) buf_fmt(&o.b, "%zu 0 R ", page_id[i]);
    buf_str(&o.b, "] >>\n");
    obj_end(&o);
    obj_begin(&o, 3);
    buf_str(&o.b, "<< /Producer (Exhaust)");
    if (d->title && *d->title) { buf_str(&o.b, " /Title "); put_title(&o.b, d->title); }
    buf_str(&o.b, " >>\n");
    obj_end(&o);
    size = next;
    xref_at = o.b.n;
    buf_fmt(&o.b, "xref\n0 %zu\n0000000000 65535 f \n", size);
    for (i = 1; i < size; i++) buf_fmt(&o.b, "%010zu 00000 n \n", i < o.noff ? o.off[i] : 0);
    buf_fmt(&o.b, "trailer\n<< /Size %zu /Root 1 0 R /Info 3 0 R >>\nstartxref\n%zu\n%%%%EOF\n", size, xref_at);
    if (o.b.oom) goto done;
    *out = o.b.p; *len = o.b.n;
    o.b.p = NULL;
    rc = 0;
done:
    buf_free(&o.b);
    free(o.off); free(font_base); free(img_id); free(page_id);
    return rc;
}
