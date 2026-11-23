/* The diagram core (6d-ii): the item list, text sizes, the golden listing, the dispatch on the first line of a ```mermaid block and the cache. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

/* ---- reading lines ---- */
int dg_next_line(DgLines *L, const char **line, size_t *len) {
    while (L->pos < L->n) {
        const char *s = L->s + L->pos, *e = memchr(s, '\n', L->n - L->pos);
        size_t n = e ? (size_t)(e - s) : L->n - L->pos;
        L->pos += e ? n + 1 : n;
        while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
        while (n > 0 && (*s == ' ' || *s == '\t')) { s++; n--; }
        if (n == 0 || (n >= 2 && s[0] == '%' && s[1] == '%')) continue;
        *line = s; *len = n;
        return 1;
    }
    return 0;
}

/* ---- building ---- */
void dg_fail(ExDiagram *d, const char *fmt, ...) {
    va_list ap;
    if (d->has_err) return;
    va_start(ap, fmt);
    vsnprintf(d->err, sizeof d->err, fmt, ap);
    va_end(ap);
    d->has_err = 1;
}

ExDItem *dg_add(ExDiagram *d, int kind) {
    ExDItem *it;
    if (d->n == d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 64;
        ExDItem *t = realloc(d->items, nc * sizeof *t);
        if (!t) { dg_fail(d, "out of memory"); return NULL; }
        d->items = t; d->cap = nc;
    }
    it = &d->items[d->n++];
    memset(it, 0, sizeof *it);
    it->kind = (uint8_t)kind;
    return it;
}

uint32_t dg_add_points(ExDiagram *d, const float *xy, size_t np) {
    uint32_t at = (uint32_t)d->npts;
    if (d->npts + np * 2 > d->cappts) {
        size_t nc = d->cappts ? d->cappts * 2 : 256;
        float *t;
        while (nc < d->npts + np * 2) nc *= 2;
        t = realloc(d->pts, nc * sizeof *t);
        if (!t) { dg_fail(d, "out of memory"); return 0; }
        d->pts = t; d->cappts = nc;
    }
    memcpy(d->pts + d->npts, xy, np * 2 * sizeof *xy);
    d->npts += np * 2;
    return at;
}

uint32_t dg_add_string(ExDiagram *d, const char *s, size_t n) {
    uint32_t at = (uint32_t)d->ntext;
    if (d->ntext + n + 1 > d->captext) {
        size_t nc = d->captext ? d->captext * 2 : 512;
        char *t;
        while (nc < d->ntext + n + 1) nc *= 2;
        t = realloc(d->text, nc);
        if (!t) { dg_fail(d, "out of memory"); return 0; }
        d->text = t; d->captext = nc;
    }
    memcpy(d->text + d->ntext, s, n);
    d->text[d->ntext + n] = 0;
    d->ntext += n + 1;
    return at;
}

ExDItem *dg_box(ExDiagram *d, int kind, float x, float y, float w, float h, int fill, int stroke) {
    ExDItem *it = dg_add(d, kind);
    if (!it) return NULL;
    it->x = x; it->y = y; it->w = w; it->h = h; it->fill = (uint8_t)fill; it->stroke = (uint8_t)stroke; it->width = 0.08f;
    return it;
}

ExDItem *dg_line(ExDiagram *d, const float *xy, size_t np, int color, int dash, int hs, int he) {
    uint32_t at = dg_add_points(d, xy, np);
    ExDItem *it = dg_add(d, EXD_LINE);
    if (!it) return NULL;
    it->pt0 = at; it->npt = (uint32_t)np; it->stroke = (uint8_t)color; it->dash = (uint8_t)dash; it->head_start = (uint8_t)hs; it->head_end = (uint8_t)he; it->width = 0.08f;
    return it;
}

ExDItem *dg_label(ExDiagram *d, float x, float y, const char *s, size_t n, int align, float size, int color, int bold) {
    uint32_t at = dg_add_string(d, s, n);
    ExDItem *it = dg_add(d, EXD_TEXT);
    if (!it) return NULL;
    it->x = x; it->y = y; it->text = at; it->tlen = (uint32_t)n; it->align = (uint8_t)align; it->size = size; it->stroke = (uint8_t)color; it->bold = (uint8_t)bold;
    return it;
}

char *dg_strndup(const char *s, size_t n) {
    char *c = malloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n);
    c[n] = 0;
    return c;
}

/* ---- text ---- */
float ex_diagram_text_width(ExFont *font, const char *s, size_t n) {
    float w = 0;
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = c;
        size_t k = 1;
        if (c >= 0xF0 && i + 3 < n) { cp = ((c & 7u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu); k = 4; }
        else if (c >= 0xE0 && i + 2 < n) { cp = ((c & 15u) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu); k = 3; }
        else if (c >= 0xC0 && i + 1 < n) { cp = ((c & 31u) << 6) | (s[i + 1] & 0x3Fu); k = 2; }
        i += k;
        {
            uint32_t g = ex_font_glyph_index(font, cp);
            float a = g ? ex_font_advance(font, g, 64) : 0.0f;
            if (!g) a = 38.0f;   /* a missing glyph: a typical width, so a layout never depends on a fallback font */
            w += a / 64.0f;
        }
    }
    return w;
}

float dg_line_height(void) { return 1.25f; }

void dg_text_size(ExDiagram *d, const char *s, size_t n, float size, float *w, float *h) {
    size_t i = 0, start = 0;
    int lines = 1;
    float mw = 0;
    if (size <= 0) size = 1.0f;
    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == '\n') {
            float lw = ex_diagram_text_width(d->font, s + start, i - start);
            if (lw > mw) mw = lw;
            start = i + 1;
            if (i < n) lines++;
        }
    }
    if (w) *w = mw * size;
    if (h) *h = (float)lines * dg_line_height() * size;
}

char *dg_clean_label(const char *s, size_t n) {
    char *o = malloc(n + 1);
    size_t i = 0, k = 0;
    if (!o) return NULL;
    while (i < n && (s[i] == ' ')) i++;
    while (n > i && (s[n - 1] == ' ')) n--;
    if (n >= i + 2 && s[i] == '"' && s[n - 1] == '"') { i++; n--; }
    while (i < n) {
        if (s[i] == '<') {
            size_t j = i + 1;
            while (j < n && s[j] != '>') j++;
            if (j < n) {
                if ((j - i >= 3 && (s[i + 1] == 'b' || s[i + 1] == 'B') && (s[i + 2] == 'r' || s[i + 2] == 'R')) && (j - i == 3 || s[i + 3] == '/' || s[i + 3] == ' ')) o[k++] = '\n';
                i = j + 1;
                continue;
            }
        }
        if (s[i] == '#' && i + 1 < n) {
            static const struct { const char *e; char c; } ent[] = { {"#quot;", '"'}, {"#amp;", '&'}, {"#lt;", '<'}, {"#gt;", '>'}, {"#35;", '#'}, {"#59;", ';'} };
            size_t t;
            int hit = 0;
            for (t = 0; t < sizeof ent / sizeof ent[0]; t++) {
                size_t l = strlen(ent[t].e);
                if (n - i >= l && !memcmp(s + i, ent[t].e, l)) { o[k++] = ent[t].c; i += l; hit = 1; break; }
            }
            if (hit) continue;
        }
        if (s[i] == '\\' && i + 1 < n && s[i + 1] == 'n') { o[k++] = '\n'; i += 2; continue; }
        o[k++] = s[i++];
    }
    o[k] = 0;
    return o;
}

/* ---- finishing ---- */
static void bounds(ExDiagram *d, float *x0, float *y0, float *x1, float *y1) {
    size_t i, j;
    *x0 = *y0 = 1e30f; *x1 = *y1 = -1e30f;
#define UP(x, y) do { float xx = (x), yy = (y); if (xx < *x0) *x0 = xx; if (xx > *x1) *x1 = xx; if (yy < *y0) *y0 = yy; if (yy > *y1) *y1 = yy; } while (0)
    for (i = 0; i < d->n; i++) {
        const ExDItem *it = &d->items[i];
        if (it->kind == EXD_LINE) { for (j = 0; j < it->npt; j++) UP(d->pts[it->pt0 + 2 * j], d->pts[it->pt0 + 2 * j + 1]); }
        else if (it->kind == EXD_TEXT) {
            float w, h;
            dg_text_size(d, d->text + it->text, it->tlen, it->size, &w, &h);
            {
                float lx = it->align == 1 ? it->x : (it->align == 2 ? it->x - w : it->x - w / 2.0f);
                float lh = dg_line_height() * (it->size > 0 ? it->size : 1.0f);
                UP(lx, it->y - lh / 2.0f);
                UP(lx + w, it->y - lh / 2.0f + h);
            }
        } else if (it->kind == EXD_WEDGE) { UP(it->x - it->w, it->y - it->w); UP(it->x + it->w, it->y + it->w); }
        else { UP(it->x, it->y); UP(it->x + it->w, it->y + it->h); }
    }
#undef UP
}

void dg_finalize(ExDiagram *d, float pad) {
    float x0, y0, x1, y1, dx, dy;
    size_t i;
    if (d->n == 0) { d->w = d->h = 2.0f * pad; return; }
    bounds(d, &x0, &y0, &x1, &y1);
    dx = pad - x0; dy = pad - y0;
    for (i = 0; i < d->n; i++) {
        ExDItem *it = &d->items[i];
        if (it->kind != EXD_LINE) { it->x += dx; it->y += dy; }
    }
    for (i = 0; i < d->npts; i += 2) { d->pts[i] += dx; d->pts[i + 1] += dy; }
    d->w = x1 - x0 + 2.0f * pad;
    d->h = y1 - y0 + 2.0f * pad;
}

/* ---- helpers for the graph-like types ---- */
void dg_clip_box(float cx, float cy, float w, float h, int shape, float tx, float ty, float *ox, float *oy) {
    float dx = tx - cx, dy = ty - cy, hw = w / 2.0f, hh = h / 2.0f, t;
    if ((dx == 0 && dy == 0) || hw <= 0 || hh <= 0) { *ox = cx; *oy = cy; return; }
    if (shape == EXD_ELLIPSE) t = 1.0f / sqrtf(dx * dx / (hw * hw) + dy * dy / (hh * hh));
    else if (shape == EXD_DIAMOND) t = 1.0f / (fabsf(dx) / hw + fabsf(dy) / hh);
    else { float a = fabsf(dx) / hw, b = fabsf(dy) / hh; t = 1.0f / (a > b ? a : b); }
    *ox = cx + dx * t; *oy = cy + dy * t;
}

ExDItem *dg_edge_line(ExDiagram *d, const LgGraph *g, int e, int shape_a, int shape_b, int color, int dash, int hs, int he) {
    const LgEdge *le = &g->edges[e];
    const LgNode *na = &g->nodes[le->a], *nb = &g->nodes[le->b];
    if (le->a == le->b) {   /* a loop on the right side of the node */
        float q[8], hw = na->w / 2.0f;
        q[0] = na->x + hw; q[1] = na->y - na->h * 0.25f; q[2] = na->x + hw + 1.0f; q[3] = q[1]; q[4] = q[2]; q[5] = na->y + na->h * 0.25f; q[6] = na->x + hw; q[7] = q[5];
        return dg_line(d, q, 4, color, dash, hs, he);
    }
    if (le->npts >= 2 && le->pts) {
        int np = le->npts;
        float *p = malloc((size_t)np * 2 * sizeof(float));
        ExDItem *it;
        if (!p) return NULL;
        memcpy(p, le->pts, (size_t)np * 2 * sizeof(float));
        dg_clip_box(na->x, na->y, na->w, na->h, shape_a, p[2], p[3], &p[0], &p[1]);
        dg_clip_box(nb->x, nb->y, nb->w, nb->h, shape_b, p[2 * np - 4], p[2 * np - 3], &p[2 * np - 2], &p[2 * np - 1]);
        it = dg_line(d, p, (size_t)np, color, dash, hs, he);
        free(p);
        return it;
    }
    return NULL;
}

void dg_edge_label(ExDiagram *d, float cx, float cy, const char *s, float size) {
    float tw, th, lh;
    char *c = dg_clean_label(s, strlen(s));
    int lines = 1;
    const char *q;
    if (!c) return;
    dg_text_size(d, c, strlen(c), size, &tw, &th);
    for (q = c; *q; q++) if (*q == '\n') lines++;
    lh = dg_line_height() * size;
    dg_box(d, EXD_RECT, cx - tw / 2.0f - 0.2f, cy - th / 2.0f - 0.1f, tw + 0.4f, th + 0.2f, EXD_C_LABEL_BG, 0);
    dg_label(d, cx, cy - (float)(lines - 1) * lh / 2.0f, c, strlen(c), 0, size, EXD_C_MUTED, 0);
    free(c);
}

void dg_append(ExDiagram *dst, const ExDiagram *src, float dx, float dy) {
    size_t i, j;
    for (i = 0; i < src->n; i++) {
        const ExDItem *it = &src->items[i];
        ExDItem *o = dg_add(dst, it->kind);
        if (!o) return;
        *o = *it;
        if (it->kind == EXD_LINE) {
            float *tmp = malloc((size_t)it->npt * 2 * sizeof(float));
            if (!tmp) return;
            for (j = 0; j < it->npt; j++) { tmp[2 * j] = src->pts[it->pt0 + 2 * j] + dx; tmp[2 * j + 1] = src->pts[it->pt0 + 2 * j + 1] + dy; }
            o->pt0 = dg_add_points(dst, tmp, it->npt);
            free(tmp);
        } else {
            o->x += dx; o->y += dy;
            if (it->kind == EXD_TEXT) o->text = dg_add_string(dst, src->text + it->text, it->tlen);
        }
    }
}

const char *dg_parse_number(const char *s, double *v) {
    char *e;
    double x;
    while (*s == ' ') s++;
    if (!((*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '+')) return NULL;
    x = strtod(s, &e);
    if (e == s) return NULL;
    *v = x;
    return e;
}

/* ---- parsing ---- */
static int starts_with(const char *s, size_t n, const char *w) {
    size_t l = strlen(w);
    return n >= l && !strncmp(s, w, l) && (n == l || s[l] == ' ' || s[l] == '\t' || s[l] == '-' || s[l] == ';');
}

ExDiagram *ex_diagram_parse(ExFont *font, const char *src, size_t len) {
    ExDiagram *d = calloc(1, sizeof *d);
    DgLines L;
    const char *line;
    size_t ln;
    if (!d) return NULL;
    d->font = font;
    if (!font || !src) { dg_fail(d, "no font"); d->w = d->h = 1; return d; }
    if (len > DG_MAX_INPUT) { dg_fail(d, "the diagram is longer than %d bytes", DG_MAX_INPUT); d->w = d->h = 1; return d; }
    L.s = src; L.n = len; L.pos = 0;
    /* front matter (--- ... ---) and %%{init}%% directives before the type line */
    for (;;) {
        if (!dg_next_line(&L, &line, &ln)) { dg_fail(d, "the diagram is empty"); break; }
        if (ln == 3 && !strncmp(line, "---", 3)) {
            while (dg_next_line(&L, &line, &ln) && !(ln == 3 && !strncmp(line, "---", 3))) {}
            continue;
        }
        if (starts_with(line, ln, "flowchart") || starts_with(line, ln, "graph")) { d->kind = EXD_FLOWCHART; dg_flowchart(d, &L, line, ln); }
        else if (starts_with(line, ln, "sequenceDiagram")) { d->kind = EXD_SEQUENCE; dg_sequence(d, &L); }
        else if (starts_with(line, ln, "classDiagram")) { d->kind = EXD_CLASS; dg_class(d, &L); }
        else if (starts_with(line, ln, "stateDiagram")) { d->kind = EXD_STATE; dg_state(d, &L, line, ln); }
        else if (starts_with(line, ln, "erDiagram")) { d->kind = EXD_ER; dg_er(d, &L); }
        else if (starts_with(line, ln, "pie")) { d->kind = EXD_PIE; dg_pie(d, &L, line, ln); }
        else if (starts_with(line, ln, "gantt")) { d->kind = EXD_GANTT; dg_gantt(d, &L); }
        else dg_fail(d, "unknown diagram type: %.*s", (int)(ln > 30 ? 30 : ln), line);
        break;
    }
    if (d->has_err) { d->n = 0; d->npts = 0; d->ntext = 0; d->w = d->h = 1.0f; }
    return d;
}

void ex_diagram_free(ExDiagram *d) {
    if (!d) return;
    free(d->items); free(d->pts); free(d->text);
    free(d);
}
const char *ex_diagram_error(const ExDiagram *d) { return d && d->has_err ? d->err : NULL; }
ExDiagKind ex_diagram_kind(const ExDiagram *d) { return d ? d->kind : EXD_UNKNOWN; }
float ex_diagram_width(const ExDiagram *d) { return d ? d->w : 0; }
float ex_diagram_height(const ExDiagram *d) { return d ? d->h : 0; }
const ExDItem *ex_diagram_items(const ExDiagram *d, size_t *n) { if (n) *n = d ? d->n : 0; return d ? d->items : NULL; }
const float *ex_diagram_points(const ExDiagram *d, size_t *n) { if (n) *n = d ? d->npts : 0; return d ? d->pts : NULL; }
const char *ex_diagram_text(const ExDiagram *d, const ExDItem *it) { return d && it ? d->text + it->text : ""; }
ExFont *ex_diagram_font(const ExDiagram *d) { return d ? d->font : NULL; }

/* ---- the golden listing ---- */
static const char *const kind_names[] = { "rect", "round", "stadium", "diamond", "ellipse", "hexagon", "parallelogram", "parallelogram_alt", "trapezoid", "trapezoid_alt",
    "cylinder", "subroutine", "flag", "double_circle", "line", "text", "wedge" };

size_t ex_diagram_dump(const ExDiagram *d, char *buf, size_t cap) {
    size_t o = 0, i, j;
    char tmp[512];
    int k;
#define PUT(...) do { k = snprintf(tmp, sizeof tmp, __VA_ARGS__); if (k > 0) { size_t m = (size_t)k; if (o + m >= cap) m = cap > o + 1 ? cap - o - 1 : 0; memcpy(buf + o, tmp, m); o += m; } } while (0)
    if (!buf || cap == 0) return 0;
    PUT("diagram %d %.2f %.2f%s%s\n", (int)d->kind, d->w, d->h, d->has_err ? " error: " : "", d->has_err ? d->err : "");
    for (i = 0; i < d->n; i++) {
        const ExDItem *it = &d->items[i];
        if (it->kind == EXD_LINE) {
            PUT("line fg=%d dash=%d hs=%d he=%d:", it->stroke, it->dash, it->head_start, it->head_end);
            for (j = 0; j < it->npt; j++) PUT(" %.2f,%.2f", d->pts[it->pt0 + 2 * j], d->pts[it->pt0 + 2 * j + 1]);
            PUT("\n");
        } else if (it->kind == EXD_TEXT) {
            char txt[200];
            size_t t = 0, q;
            for (q = 0; q < it->tlen && t + 2 < sizeof txt; q++) { char c = d->text[it->text + q]; if (c == '\n') { txt[t++] = '\\'; txt[t++] = 'n'; } else txt[t++] = c; }
            txt[t] = 0;
            PUT("text %.2f %.2f al=%d sz=%.2f fg=%d b=%d \"%s\"\n", it->x, it->y, it->align, it->size > 0 ? it->size : 1.0f, it->stroke, it->bold, txt);
        } else if (it->kind == EXD_WEDGE) {
            PUT("wedge %.2f %.2f r=%.2f %.3f %.3f fill=%d\n", it->x, it->y, it->w, it->a0, it->a1, it->fill);
        } else {
            PUT("%s %.2f %.2f %.2f %.2f fill=%d stroke=%d\n", kind_names[it->kind], it->x, it->y, it->w, it->h, it->fill, it->stroke);
        }
    }
#undef PUT
    buf[o < cap ? o : cap - 1] = 0;
    return o;
}

/* ---- the cache ---- */
typedef struct CEntry { uint64_t key; size_t len; ExDiagram *d; unsigned gen; char *src; uint32_t font; } CEntry;
struct ExDiagramCache { CEntry *e; size_t n, cap; unsigned gen; size_t misses; };

ExDiagramCache *ex_diagcache_create(void) { return calloc(1, sizeof(ExDiagramCache)); }
void ex_diagcache_free(ExDiagramCache *c) {
    size_t i;
    if (!c) return;
    for (i = 0; i < c->n; i++) { ex_diagram_free(c->e[i].d); free(c->e[i].src); }
    free(c->e); free(c);
}
void ex_diagcache_begin(ExDiagramCache *c) {
    size_t i, o = 0;
    if (!c) return;
    for (i = 0; i < c->n; i++) { if (c->e[i].gen == c->gen) c->e[o++] = c->e[i]; else { ex_diagram_free(c->e[i].d); free(c->e[i].src); } }
    c->n = o;
    c->gen++;
}
static uint64_t hash(const char *s, size_t n, uint32_t font) {
    uint64_t h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ull; }
    h ^= font; h *= 1099511628211ull;
    return h;
}
const ExDiagram *ex_diagcache_lookup(ExDiagramCache *c, ExFont *font, const char *src, size_t len) {
    uint64_t key;
    size_t i;
    uint32_t fid;
    if (!c || !font || !src) return NULL;
    fid = ex_font_id(font);
    key = hash(src, len, fid);
    for (i = 0; i < c->n; i++)
        if (c->e[i].key == key && c->e[i].len == len && c->e[i].font == fid && !memcmp(c->e[i].src, src, len)) { c->e[i].gen = c->gen; return c->e[i].d; }
    return NULL;
}
const ExDiagram *ex_diagcache_get(ExDiagramCache *c, ExFont *font, const char *src, size_t len) {
    const ExDiagram *hit;
    ExDiagram *d;
    if (!c || !font || !src) return NULL;
    hit = ex_diagcache_lookup(c, font, src, len);
    if (hit) return hit;
    d = ex_diagram_parse(font, src, len);
    c->misses++;
    if (!d) return NULL;
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 16;
        CEntry *t = realloc(c->e, nc * sizeof *t);
        if (!t) { ex_diagram_free(d); return NULL; }
        c->e = t; c->cap = nc;
    }
    c->e[c->n].src = malloc(len ? len : 1);
    if (!c->e[c->n].src) { ex_diagram_free(d); return NULL; }
    memcpy(c->e[c->n].src, src, len);
    c->e[c->n].key = hash(src, len, ex_font_id(font)); c->e[c->n].len = len; c->e[c->n].d = d; c->e[c->n].gen = c->gen; c->e[c->n].font = ex_font_id(font);
    c->n++;
    return d;
}
size_t ex_diagcache_count(const ExDiagramCache *c) { return c ? c->n : 0; }
size_t ex_diagcache_misses(const ExDiagramCache *c) { return c ? c->misses : 0; }
