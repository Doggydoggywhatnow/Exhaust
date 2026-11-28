/* The canvas view (6a): see include/exhaust_canvasview.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_canvasview.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_settings.h"

/* ---- numbers (canvas units are device-independent pixels at zoom 1) ---- */
#define ZOOM_MIN 0.02f
#define ZOOM_MAX 6.0f
#define GRID 20.0
#define MIN_CARD 40.0
#define HANDLE_PX 7.0f              /* the border of a selected card that resizes it, and the size of the dots at the sides */
#define DOT_OFFSET_PX 12.0f         /* the dots for new edges sit this far outside the sides */
#define EDGE_HIT_PX 6.0f
#define DRAG_PX 5.0f
#define DOUBLE_MS 400
#define SNAP_PX 6.0f
#define MAX_TEXT_LINES 400
#define PASTE_OFFSET 40.0
#define MAX_SEL_KEEP 512

enum { MODE_NONE, MODE_PAN, MODE_MOVE, MODE_RESIZE, MODE_EDGE, MODE_BOX };

/* ---- the cache of what file cards show ---- */

typedef struct CacheEntry { char *path; char *text; int state; } CacheEntry;   /* state: 0 asked, 1 have, 2 cannot be read */
struct ExCanvasCache { CacheEntry *v; size_t n, cap; unsigned long version; };

static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

ExCanvasCache *ex_canvascache_create(void) { return calloc(1, sizeof(ExCanvasCache)); }
static void cache_clear(ExCanvasCache *c) { size_t i; for (i = 0; i < c->n; i++) { free(c->v[i].path); free(c->v[i].text); } c->n = 0; }
void ex_canvascache_free(ExCanvasCache *c) { if (!c) return; cache_clear(c); free(c->v); free(c); }
static CacheEntry *cache_find(ExCanvasCache *c, const char *path) { size_t i; for (i = 0; i < c->n; i++) if (!strcmp(c->v[i].path, path)) return &c->v[i]; return NULL; }
static CacheEntry *cache_add(ExCanvasCache *c, const char *path) {
    if (c->n >= 2000) cache_clear(c);                                                  /* a canvas with more notes than this asks again for the ones it still shows */
    if (c->n == c->cap) { size_t nc = c->cap ? c->cap * 2 : 32; CacheEntry *nv = realloc(c->v, nc * sizeof *nv); if (!nv) return NULL; c->v = nv; c->cap = nc; }
    c->v[c->n].path = dups(path); c->v[c->n].text = NULL; c->v[c->n].state = 0;
    if (!c->v[c->n].path) return NULL;
    return &c->v[c->n++];
}
void ex_canvascache_set(ExCanvasCache *c, const char *path, const char *text) {
    CacheEntry *e;
    if (!c || !path) return;
    e = cache_find(c, path);
    if (!e) e = cache_add(c, path);
    if (!e) return;
    free(e->text);
    e->text = text ? dups(text) : NULL;
    e->state = text ? 1 : 2;
    c->version++;
}
void ex_canvascache_invalidate(ExCanvasCache *c, const char *path) {
    size_t i;
    if (!c) return;
    if (!path || !*path) { cache_clear(c); c->version++; return; }
    {   /* the file itself and every key that names a part of it (`path#Heading`, `path#^block`) */
        size_t len = strlen(path), k = 0;
        for (i = 0; i < c->n; i++) {
            if (!strncmp(c->v[i].path, path, len) && (c->v[i].path[len] == 0 || c->v[i].path[len] == '#')) { free(c->v[i].path); free(c->v[i].text); continue; }
            c->v[k++] = c->v[i];
        }
        if (k != c->n) { c->n = k; c->version++; }
    }
}
unsigned long ex_canvascache_version(const ExCanvasCache *c) { return c ? c->version : 0; }

/* ---- the view ---- */

typedef struct View {
    ExUi *ui;
    ExDoc *doc;
    char *path;
    ExCanvasHandlers h;
    ExCanvas *c;
    char err[240];
    int broken;
    unsigned long seen;                      /* the document's version the canvas was read at (or written at) */
    unsigned long reads, commits;
    ExGeom *geom;
    /* the camera */
    double cx, cy;                           /* the canvas point at the middle of the widget */
    float zoom;
    int fitted;                              /* the first fit has been done (or a view was set) */
    /* interaction */
    int mode;
    int pressed, moved;
    float press_x, press_y, cur_x, cur_y;    /* window pixels */
    double press_cx, press_cy;               /* the camera when the press began */
    int grab;                                /* the card the press began on, or -1 */
    int grab_edge;                           /* the edge the press began on, or -1 */
    int handle;                              /* RESIZE: which sides move (bit 1 left, 2 right, 4 top, 8 bottom) */
    double r_x, r_y, r_w, r_h;               /* RESIZE: the card's box at the press */
    size_t *mv; double *mv_x, *mv_y; size_t nmv, mvcap;   /* MOVE: the cards that move and where they were */
    int edge_from, edge_side;                /* EDGE: the card and the side the new edge leaves by */
    int hover, hover_dot;                    /* the card under the pointer (-1) and the dot (side) under it (-1) */
    int shift_at_press, ctrl_at_press;
    uint32_t last_click_ms; float last_click_x, last_click_y;
    int dirty;                               /* the model has changes the document does not have yet */
    /* editing a card's text */
    ExWidget *edit;
    ExDoc *edit_doc;
    int edit_node;                           /* index of the card, or -1 */
    int edit_close;                          /* close it at the next frame (committing its text) */
    int edit_cancel;
    /* what the last frame did */
    ExCanvasViewStats stats;
    float unit;                              /* a spacing unit in pixels, for the position of the editor */
    unsigned long cache_seen;
} View;

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_CANVAS ? (View *)w->owned : NULL; }

/* ---- reading and writing the document ---- */

static int blank(const char *t, size_t n) { size_t i; for (i = 0; i < n; i++) if (t[i] != ' ' && t[i] != '\t' && t[i] != '\n' && t[i] != '\r') return 0; return 1; }

static void reload(View *v) {
    size_t len = 0, i, nkeep = 0;
    char *text = ex_buffer_text(ex_doc_buffer(v->doc), &len);
    char *keep[MAX_SEL_KEEP];
    int keep_edge[MAX_SEL_KEEP];
    ExCanvas *c;
    if (v->c) {                                                                   /* the selection survives a re-read: by the ids of its cards and edges */
        for (i = 0; i < v->c->n && nkeep < MAX_SEL_KEEP; i++) if (v->c->nodes[i].sel && v->c->nodes[i].id) { keep[nkeep] = dups(v->c->nodes[i].id); keep_edge[nkeep++] = 0; }
        for (i = 0; i < v->c->ne && nkeep < MAX_SEL_KEEP; i++) if (v->c->edges[i].sel && v->c->edges[i].id) { keep[nkeep] = dups(v->c->edges[i].id); keep_edge[nkeep++] = 1; }
    }
    ex_canvas_free(v->c);
    v->c = NULL; v->broken = 0; v->err[0] = 0;
    if (!text) { v->broken = 1; snprintf(v->err, sizeof v->err, "out of memory"); }
    else if (blank(text, len)) c = v->c = ex_canvas_new();
    else { c = ex_canvas_parse(text, len, v->err, sizeof v->err); v->c = c; if (!c) v->broken = 1; }
    free(text);
    if (v->c) {
        size_t k;
        for (k = 0; k < nkeep; k++) {
            if (!keep_edge[k]) { int idx = ex_canvas_find_node(v->c, keep[k]); if (idx >= 0) v->c->nodes[idx].sel = 1; }
            else { size_t e; for (e = 0; e < v->c->ne; e++) if (v->c->edges[e].id && !strcmp(v->c->edges[e].id, keep[k])) v->c->edges[e].sel = 1; }
        }
    }
    for (i = 0; i < nkeep; i++) free(keep[i]);
    v->seen = ex_doc_version(v->doc);
    v->reads++;
    v->hover = v->grab = v->grab_edge = -1;
    v->dirty = 0;
}

/* Writes the model into the document as one edit (nothing when the text is the same). */
static void commit(View *v) {
    size_t newlen = 0, oldlen = 0, p = 0, s = 0;
    char *neu, *old;
    ExChange ch;
    if (!v->c || v->broken) { v->dirty = 0; return; }
    if (!v->dirty) return;
    v->dirty = 0;
    neu = ex_canvas_write(v->c, &newlen);
    old = ex_buffer_text(ex_doc_buffer(v->doc), &oldlen);
    if (!neu || !old) { free(neu); free(old); return; }
    while (p < oldlen && p < newlen && old[p] == neu[p]) p++;
    while (s < oldlen - p && s < newlen - p && old[oldlen - 1 - s] == neu[newlen - 1 - s]) s++;
    while (p > 0 && ((unsigned char)old[p] & 0xC0) == 0x80) p--;                   /* edits start and end between characters */
    while (s > 0 && oldlen - s < oldlen && ((unsigned char)old[oldlen - s] & 0xC0) == 0x80) s--;
    ch.offset = p; ch.remove_len = oldlen - p - s; ch.text = neu + p; ch.text_len = newlen - p - s;
    if (ch.remove_len || ch.text_len) {
        ex_doc_seal(v->doc);
        ex_doc_apply(v->doc, &ch, 1, 0, NULL, 0);
        ex_doc_seal(v->doc);
        v->commits++;
    }
    v->seen = ex_doc_version(v->doc);
    free(neu); free(old);
}

static void touch(View *v) { ex_canvas_touch(v->c); v->dirty = 1; }

/* ---- the camera ---- */

static float scale_of(const View *v) { return v->zoom * ex_renderer_scale(ex_ui_renderer(v->ui)); }
static void to_screen(const View *v, const ExWidget *w, double X, double Y, float *sx, float *sy) {
    double s = scale_of(v);
    *sx = w->x + w->w / 2.0f + (float)((X - v->cx) * s);
    *sy = w->y + w->h / 2.0f + (float)((Y - v->cy) * s);
}
static void to_canvas(const View *v, const ExWidget *w, float sx, float sy, double *X, double *Y) {
    double s = scale_of(v);
    *X = v->cx + (sx - (w->x + w->w / 2.0f)) / s;
    *Y = v->cy + (sy - (w->y + w->h / 2.0f)) / s;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void zoom_about(View *v, const ExWidget *w, float factor, float sx, float sy) {
    double X, Y, s;
    float nz = clampf(v->zoom * factor, ZOOM_MIN, ZOOM_MAX);
    to_canvas(v, w, sx, sy, &X, &Y);
    v->zoom = nz;
    s = scale_of(v);
    v->cx = X - (sx - (w->x + w->w / 2.0f)) / s;
    v->cy = Y - (sy - (w->y + w->h / 2.0f)) / s;
    v->fitted = 1;
}

static void fit_box(View *v, const ExWidget *w, double x0, double y0, double x1, double y1) {
    double bw = x1 - x0, bh = y1 - y0, sc = ex_renderer_scale(ex_ui_renderer(v->ui)), zx, zy, margin = 0.88;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    zx = w->w * margin / (bw * sc); zy = w->h * margin / (bh * sc);
    v->zoom = clampf((float)(zx < zy ? zx : zy), ZOOM_MIN, 1.0f > ZOOM_MAX ? ZOOM_MAX : 1.0f);
    v->cx = (x0 + x1) / 2.0; v->cy = (y0 + y1) / 2.0;
    v->fitted = 1;
}

static int selection_bounds(const View *v, double *x0, double *y0, double *x1, double *y1) {
    size_t i;
    int any = 0;
    if (!v->c) return 0;
    for (i = 0; i < v->c->n; i++) {
        const ExCanvasNode *n = &v->c->nodes[i];
        if (!n->sel || n->raw || n->kind == EX_CN_OTHER) continue;
        if (!any) { *x0 = n->x; *y0 = n->y; *x1 = n->x + n->w; *y1 = n->y + n->h; any = 1; }
        else { if (n->x < *x0) *x0 = n->x; if (n->y < *y0) *y0 = n->y; if (n->x + n->w > *x1) *x1 = n->x + n->w; if (n->y + n->h > *y1) *y1 = n->y + n->h; }
    }
    return any;
}

void ex_canvasview_fit(ExWidget *w, int selection_only) {
    View *v = view_of(w);
    double x0, y0, x1, y1;
    if (!v || !v->c) return;
    if ((selection_only && selection_bounds(v, &x0, &y0, &x1, &y1)) || ex_canvas_bounds(v->c, &x0, &y0, &x1, &y1)) fit_box(v, w, x0, y0, x1, y1);
    else { v->cx = v->cy = 0; v->zoom = 1.0f; v->fitted = 1; }
}

/* ---- colors ---- */

static ExColor mix(ExColor a, ExColor b, float t) {     /* a towards b */
    ExColor c;
    c.r = (uint8_t)((float)a.r + ((float)b.r - (float)a.r) * t + 0.5f); c.g = (uint8_t)((float)a.g + ((float)b.g - (float)a.g) * t + 0.5f);
    c.b = (uint8_t)((float)a.b + ((float)b.b - (float)a.b) * t + 0.5f); c.a = (uint8_t)((float)a.a + ((float)b.a - (float)a.a) * t + 0.5f);
    return c;
}
static ExColor with_alpha(ExColor c, float a) { c.a = (uint8_t)((float)c.a * a + 0.5f); return c; }

/* The colour a card or edge names ("1".."6" are theme colours, "#rrggbb" is the file's own); returns 0 when it has none. */
static int named_color(const ExTheme *t, const char *color, ExColor *out) {
    static const ExColorToken PRESET[6] = { EX_COLOR_ERROR, EX_COLOR_WARNING, EX_COLOR_CALLOUT_QUESTION, EX_COLOR_SUCCESS, EX_COLOR_CALLOUT_TIP, EX_COLOR_CALLOUT_EXAMPLE };
    uint8_t rgb[3];
    int k = ex_canvas_color(color, rgb);
    if (k >= 1 && k <= 6) { *out = ex_theme_color(t, PRESET[k - 1]); return 1; }
    if (k == 0) { out->r = rgb[0]; out->g = rgb[1]; out->b = rgb[2]; out->a = 255; return 1; }
    return 0;
}

/* ---- text ---- */

typedef struct Line { size_t off, len; int head; } Line;

static int utf8_len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }

/* Wraps text at words to maxw pixels, up to maxlines lines (the text after that is not looked at: a card shows only as much as fits). A line that
 * begins with one to six # and a space is a heading (the marks are left out). Returns the number of lines. */
static size_t wrap_text(ExRenderer *r, ExFont *font, float size, const char *text, size_t tlen, float maxw, Line *out, size_t maxlines) {
    size_t n = 0, pos = 0;
    float space = ex_text_measure(r, font, size, " ", 1);
    while (pos <= tlen && n < maxlines) {
        size_t eol = pos, ls, i, line_start;
        int head = 0;
        float x = 0.0f;
        while (eol < tlen && text[eol] != '\n') eol++;
        ls = pos;
        while (ls < eol && text[ls] == '#' && ls - pos < 6) ls++;
        if (ls > pos && ls < eol && text[ls] == ' ') { head = (int)(ls - pos); ls++; } else ls = pos;
        if (ls == eol) { out[n].off = ls; out[n].len = 0; out[n].head = head; n++; pos = eol + 1; continue; }          /* an empty line */
        line_start = ls;
        i = ls;
        while (i < eol && n < maxlines) {
            size_t ws = i, we;
            float ww;
            while (i < eol && text[i] != ' ') i++;
            we = i;
            while (i < eol && text[i] == ' ') i++;                                                                    /* the spaces after a word go with it */
            ww = ex_text_measure(r, font, size, text + ws, we - ws);
            if (x > 0.0f && x + ww > maxw) {                                                                          /* the word does not fit: a new line */
                size_t e = ws;
                while (e > line_start && text[e - 1] == ' ') e--;
                out[n].off = line_start; out[n].len = e - line_start; out[n].head = head; n++;
                line_start = ws; x = 0.0f;
                if (n >= maxlines) break;
            }
            if (ww > maxw && x == 0.0f) {                                                                             /* a word longer than the line: cut it between characters */
                size_t p = ws;
                float cw = 0.0f;
                size_t cut_start = ws;
                while (p < we && n < maxlines) {
                    size_t cl = (size_t)utf8_len((unsigned char)text[p]);
                    float gw;
                    if (p + cl > we) cl = we - p;
                    gw = ex_text_measure(r, font, size, text + p, cl);
                    if (cw > 0.0f && cw + gw > maxw) { out[n].off = cut_start; out[n].len = p - cut_start; out[n].head = head; n++; cut_start = p; cw = 0.0f; }
                    cw += gw; p += cl;
                }
                line_start = cut_start; x = cw + space;
                continue;
            }
            x += ww + space;
        }
        if (n < maxlines) {
            size_t e = eol;
            while (e > line_start && text[e - 1] == ' ') e--;
            out[n].off = line_start; out[n].len = e - line_start; out[n].head = head; n++;
        }
        pos = eol + 1;
        if (eol >= tlen) break;
    }
    return n;
}

/* Draws the lines of a text inside a box. Returns the number of lines drawn. */
static size_t draw_text_box(View *v, ExRenderer *r, const char *text, size_t tlen, float x, float y, float w, float h, float size, ExColor color, ExColor head_color) {
    ExFont *f = ex_ui_font(v->ui, EX_FONT_TOKEN_TEXT, 0), *fb = ex_ui_font(v->ui, EX_FONT_TOKEN_TEXT, 1);
    ExFontMetrics fm;
    static Line lines[MAX_TEXT_LINES];
    size_t n, i, drawn = 0;
    float ly = y, lh;
    if (ex_text_metrics(r, f, size, &fm) != 0 || w < 4.0f || h < 4.0f) return 0;
    lh = (float)fm.line_height;
    n = wrap_text(r, f, size, text, tlen, w, lines, (size_t)(h / (lh > 1.0f ? lh : 1.0f)) + 2 > MAX_TEXT_LINES ? MAX_TEXT_LINES : (size_t)(h / (lh > 1.0f ? lh : 1.0f)) + 2);
    ex_ui_clip_to(v->ui, x, y, w, h);
    for (i = 0; i < n; i++) {
        float hs = lines[i].head ? size * (1.5f - 0.1f * (float)(lines[i].head > 4 ? 4 : lines[i].head)) : size;
        ExFontMetrics hm;
        if (ly > y + h) break;
        if (lines[i].head && ex_text_metrics(r, fb, hs, &hm) == 0) {
            if (lines[i].len) ex_text_draw(r, fb, hs, x, ly, text + lines[i].off, lines[i].len, head_color);
            ly += (float)hm.line_height;
        } else {
            if (lines[i].len) ex_text_draw(r, f, size, x, ly, text + lines[i].off, lines[i].len, color);
            ly += lh;
        }
        drawn++;
    }
    ex_ui_clip_restore(v->ui);
    v->stats.texts_laid_out++;
    return drawn;
}

/* ---- settings ---- */

static int setting_bool(const View *v, const char *key, int dflt) { return v->h.settings ? ex_settings_get_bool(v->h.settings, "canvas", key) : dflt; }
static int threshold_percent(const View *v) { return v->h.settings ? (int)ex_settings_get_int(v->h.settings, "canvas", "zoom_hide_threshold") : 100; }
static const char *setting_str(const View *v, const char *key, const char *dflt) { const char *s = v->h.settings ? ex_settings_get(v->h.settings, "canvas", key) : NULL; return s && *s ? s : dflt; }

/* ---- the edges ---- */

static void side_normal(int side, double *nx, double *ny) {
    *nx = *ny = 0;
    switch (side) { case EX_CS_TOP: *ny = -1; break; case EX_CS_RIGHT: *nx = 1; break; case EX_CS_BOTTOM: *ny = 1; break; case EX_CS_LEFT: *nx = -1; break; default: break; }
}

/* The Bezier of an edge in canvas units: its end points and control points. Returns 0 when an end is missing. */
static int edge_curve(const ExCanvas *c, const ExCanvasEdge *e, double p[8]) {
    double x1, y1, x2, y2, n1x, n1y, n2x, n2y, d;
    int s1, s2;
    if (!ex_canvas_edge_ends(c, e, &x1, &y1, &s1, &x2, &y2, &s2)) return 0;
    side_normal(s1, &n1x, &n1y); side_normal(s2, &n2x, &n2y);
    d = hypot(x2 - x1, y2 - y1) * 0.5;
    if (d < 40.0) d = 40.0;
    if (d > 300.0) d = 300.0;
    p[0] = x1; p[1] = y1; p[2] = x1 + n1x * d; p[3] = y1 + n1y * d; p[4] = x2 + n2x * d; p[5] = y2 + n2y * d; p[6] = x2; p[7] = y2;
    return 1;
}
static void bez_point(const double p[8], double t, double *x, double *y) {
    double u = 1.0 - t;
    *x = u * u * u * p[0] + 3 * u * u * t * p[2] + 3 * u * t * t * p[4] + t * t * t * p[6];
    *y = u * u * u * p[1] + 3 * u * u * t * p[3] + 3 * u * t * t * p[5] + t * t * t * p[7];
}

/* The edge nearest to a canvas point within `reach` canvas units, or -1. */
static int edge_at(const View *v, double X, double Y, double reach) {
    size_t e;
    int best = -1;
    double bd = reach;
    if (!v->c) return -1;
    for (e = 0; e < v->c->ne; e++) {
        double p[8], lo_x, hi_x, lo_y, hi_y;
        int k;
        if (!edge_curve(v->c, &v->c->edges[e], p)) continue;
        lo_x = fmin(fmin(p[0], p[6]), fmin(p[2], p[4])); hi_x = fmax(fmax(p[0], p[6]), fmax(p[2], p[4]));
        lo_y = fmin(fmin(p[1], p[7]), fmin(p[3], p[5])); hi_y = fmax(fmax(p[1], p[7]), fmax(p[3], p[5]));
        if (X < lo_x - reach || X > hi_x + reach || Y < lo_y - reach || Y > hi_y + reach) continue;
        for (k = 0; k < 24; k++) {
            double ax, ay, bx, by, dx, dy, t, ql, px, py, d;
            bez_point(p, (double)k / 24.0, &ax, &ay); bez_point(p, (double)(k + 1) / 24.0, &bx, &by);
            dx = bx - ax; dy = by - ay; ql = dx * dx + dy * dy;
            t = ql > 0 ? ((X - ax) * dx + (Y - ay) * dy) / ql : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
            px = ax + t * dx; py = ay + t * dy;
            d = hypot(X - px, Y - py);
            if (d < bd) { bd = d; best = (int)e; }
        }
    }
    return best;
}

/* ---- drawing ---- */

static int visible(const ExWidget *w, float x, float y, float cw, float ch) { return x + cw >= w->x && x <= w->x + w->w && y + ch >= w->y && y <= w->y + w->h; }

static const char *base_name(const char *path, size_t *len) {
    const char *s = strrchr(path, '/'), *b = s ? s + 1 : path;
    size_t n = strlen(b);
    if (n > 3 && !strcmp(b + n - 3, ".md")) n -= 3;
    *len = n;
    return b;
}

static void ensure_text(View *v, const char *path, const char **text, int *state) {
    CacheEntry *e;
    *text = NULL; *state = 0;
    if (!v->h.cache || !path || !*path) { *state = 2; return; }
    e = cache_find(v->h.cache, path);
    if (!e) {
        e = cache_add(v->h.cache, path);
        if (e && v->h.want_text) v->h.want_text(v->h.user, path);
    }
    if (e) { *text = e->text; *state = e->state; }
}

static int is_image_file(const char *path) {
    static const char *const ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".avif", ".svg" };
    size_t n = strlen(path), i;
    for (i = 0; i < sizeof ext / sizeof *ext; i++) {
        size_t e = strlen(ext[i]), k;
        if (n < e) continue;
        for (k = 0; k < e; k++) { char c = path[n - e + k]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != ext[i][k]) break; }
        if (k == e) return 1;
    }
    return 0;
}

static void draw_card(View *v, ExWidget *w, ExRenderer *r, const ExTheme *t, ExCanvasNode *n, int show_content, ExColor c_text, ExColor c_muted, ExColor c_surface, ExColor c_border, ExColor c_accent) {
    float sx, sy, sw, sh, rad, size, pad, lh = 0.0f;
    ExColor col, fill, line;
    ExFontMetrics fm;
    ExFont *f = ex_ui_font(v->ui, EX_FONT_TOKEN_TEXT, 0);
    double s = scale_of(v);
    to_screen(v, w, n->x, n->y, &sx, &sy);
    sw = (float)(n->w * s); sh = (float)(n->h * s);
    rad = (float)ex_px(r, 6.0f) * v->zoom; if (rad > 12.0f) rad = 12.0f;
    fill = c_surface; line = c_border;
    if (n->color && named_color(t, n->color, &col)) { fill = mix(c_surface, col, 0.14f); line = col; }
    if (n->sel) line = c_accent;
    ex_fill_round_rect(r, sx, sy, sw, sh, rad, fill);
    ex_stroke_round_rect(r, sx, sy, sw, sh, rad, (n->sel ? 2.0f : 1.0f) * (float)ex_px(r, 1.0f), line);
    if (!show_content) return;
    size = ex_ui_base_size(v->ui) * v->zoom;
    pad = 8.0f * (float)s;
    if (n->kind == EX_CN_TEXT && n->text) {
        if (v->edit && v->edit_node == (int)(n - v->c->nodes)) return;                         /* the editor shows it */
        draw_text_box(v, r, n->text, strlen(n->text), sx + pad, sy + pad, sw - 2 * pad, sh - 2 * pad, size, c_text, c_text);
    } else if (n->kind == EX_CN_FILE && n->file && v->h.images && is_image_file(n->file)) {
        ExImageRef ref;
        size_t nl = 0;
        const char *nm = base_name(n->file, &nl);
        ex_imagecache_get(v->h.images, r, n->file, (int)(sw > 0.0f ? sw : 256.0f), &ref);
        if (ref.state == EX_IMAGE_READY && ref.tex && ref.w > 0 && ref.h > 0) {            /* the picture, as large as fits the card and not distorted */
            float iw = sw - 2.0f * pad, ih = sh - 2.0f * pad, k = iw / (float)ref.w < ih / (float)ref.h ? iw / (float)ref.w : ih / (float)ref.h;
            if (iw > 4.0f && ih > 4.0f) ex_texture_draw(r, ref.tex, 0, 0, 0, 0, sx + (sw - (float)ref.w * k) / 2.0f, sy + (sh - (float)ref.h * k) / 2.0f, (float)ref.w * k, (float)ref.h * k, 255, k < 3.0f);
        } else {
            char msg[300];
            snprintf(msg, sizeof msg, "%.*s", (int)(nl > 200 ? 200 : nl), nm);
            ex_ui_clip_to(v->ui, sx, sy, sw, sh);
            ex_text_draw(r, f, size, sx + pad, sy + pad, ref.state == EX_IMAGE_LOADING ? "..." : msg, ref.state == EX_IMAGE_LOADING ? 3 : strlen(msg), c_muted);
            ex_ui_clip_restore(v->ui);
        }
    } else if (n->kind == EX_CN_FILE && n->file) {
        const char *text; int state;
        size_t nl = 0;
        const char *nm = base_name(n->file, &nl);
        char key[760];
        snprintf(key, sizeof key, "%s%s", n->file, n->subpath ? n->subpath : "");
        ensure_text(v, key, &text, &state);
        if (text && state == 1) draw_text_box(v, r, text, strlen(text), sx + pad, sy + pad, sw - 2 * pad, sh - 2 * pad, size * 0.9f, c_muted, c_text);
        else {
            char msg[300];
            snprintf(msg, sizeof msg, "%.*s", (int)(nl > 200 ? 200 : nl), nm);
            if (ex_text_metrics(r, f, size, &fm) == 0) lh = (float)fm.line_height;
            ex_ui_clip_to(v->ui, sx, sy, sw, sh);
            ex_text_draw(r, f, size, sx + pad, sy + pad, state == 0 ? "..." : msg, state == 0 ? 3 : strlen(msg), c_muted);
            ex_ui_clip_restore(v->ui);
            (void)lh;
        }
    } else if (n->kind == EX_CN_LINK && n->url) {
        ex_text_metrics(r, f, size, &fm);
        ex_ui_clip_to(v->ui, sx, sy, sw, sh);
        ex_text_draw(r, f, size, sx + pad, sy + pad, n->url, strlen(n->url), ex_theme_color(t, EX_COLOR_LINK));
        ex_ui_clip_restore(v->ui);
    }
}

/* ---- picking ---- */

/* The card or group at a window position (a group only at its border or its name), or -1. */
static int pick_card(const View *v, const ExWidget *w, float sx, float sy) {
    size_t i;
    double X, Y, s = scale_of(v), band = (double)HANDLE_PX * ex_renderer_scale(ex_ui_renderer(v->ui)) / s;
    if (!v->c) return -1;
    to_canvas(v, w, sx, sy, &X, &Y);
    for (i = v->c->n; i > 0; i--) {
        const ExCanvasNode *n = &v->c->nodes[i - 1];
        if (n->raw || n->kind == EX_CN_OTHER || n->kind == EX_CN_GROUP) continue;
        if (X >= n->x && X <= n->x + n->w && Y >= n->y && Y <= n->y + n->h) return (int)(i - 1);
    }
    for (i = v->c->n; i > 0; i--) {
        const ExCanvasNode *n = &v->c->nodes[i - 1];
        double lab = 28.0 / v->zoom;
        if (n->raw || n->kind != EX_CN_GROUP) continue;
        if (X < n->x - band || X > n->x + n->w + band || Y < n->y - band - lab || Y > n->y + n->h + band) continue;
        if (Y < n->y - band) { if (X <= n->x + n->w * 0.5) return (int)(i - 1); continue; }                  /* the name above the group */
        if (X <= n->x + band || X >= n->x + n->w - band || Y <= n->y + band || Y >= n->y + n->h - band) return (int)(i - 1);
    }
    return -1;
}

static void clear_selection(View *v) {
    size_t i;
    if (!v->c) return;
    for (i = 0; i < v->c->n; i++) v->c->nodes[i].sel = 0;
    for (i = 0; i < v->c->ne; i++) v->c->edges[i].sel = 0;
}
static size_t selected_nodes(const View *v) { size_t i, k = 0; if (v->c) for (i = 0; i < v->c->n; i++) k += v->c->nodes[i].sel != 0; return k; }
static size_t selected_edges(const View *v) { size_t i, k = 0; if (v->c) for (i = 0; i < v->c->ne; i++) k += v->c->edges[i].sel != 0; return k; }
static int only_selected_node(const View *v) {
    size_t i; int found = -1;
    if (!v->c || selected_edges(v) || selected_nodes(v) != 1) return -1;
    for (i = 0; i < v->c->n; i++) if (v->c->nodes[i].sel) found = (int)i;
    return found;
}

static void say(View *v, const char *msg) { if (v->h.message) v->h.message(v->h.user, msg); }

/* ---- snapping ---- */

/* Moves the box (x, y, w, h) by the snapping rules: to the edges and middles of the cards near it, else to the grid. Returns the adjustment. */
static void snap_box(const View *v, const double *skip_idx_unused, double x, double y, double bw, double bh, const size_t *skip, size_t nskip, double *dx, double *dy) {
    double reach = (double)SNAP_PX / v->zoom, bx = reach + 1, by = reach + 1;
    size_t i, k;
    int snapped_x = 0, snapped_y = 0;
    (void)skip_idx_unused;
    *dx = *dy = 0;
    if (!v->c) return;
    if (setting_bool(v, "snap_to_objects", 1)) {
        double mine_x[3] = { x, x + bw / 2, x + bw }, mine_y[3] = { y, y + bh / 2, y + bh };
        for (i = 0; i < v->c->n; i++) {
            const ExCanvasNode *n = &v->c->nodes[i];
            double ox[3], oy[3];
            int a, b, skipit = 0;
            if (n->raw || n->kind == EX_CN_OTHER) continue;
            for (k = 0; k < nskip; k++) if (skip[k] == i) { skipit = 1; break; }
            if (skipit) continue;
            if (n->x > x + bw + 400 / v->zoom || n->x + n->w < x - 400 / v->zoom || n->y > y + bh + 400 / v->zoom || n->y + n->h < y - 400 / v->zoom) continue;     /* far from the box */
            ox[0] = n->x; ox[1] = n->x + n->w / 2; ox[2] = n->x + n->w; oy[0] = n->y; oy[1] = n->y + n->h / 2; oy[2] = n->y + n->h;
            for (a = 0; a < 3; a++) for (b = 0; b < 3; b++) {
                double ddx = ox[b] - mine_x[a], ddy = oy[b] - mine_y[a];
                if (fabs(ddx) < fabs(bx) && fabs(ddx) <= reach) { bx = ddx; snapped_x = 1; }
                if (fabs(ddy) < fabs(by) && fabs(ddy) <= reach) { by = ddy; snapped_y = 1; }
            }
        }
    }
    if (snapped_x) *dx = bx;
    if (snapped_y) *dy = by;
    if (setting_bool(v, "snap_to_grid", 1)) {
        if (!snapped_x) *dx = floor(x / GRID + 0.5) * GRID - x;
        if (!snapped_y) *dy = floor(y / GRID + 0.5) * GRID - y;
    }
}

/* ---- editing a card's text ---- */

static void finish_edit(View *v, ExWidget *w) {
    char *text = NULL;
    if (!v->edit) return;
    if (!v->edit_cancel && v->c && v->edit_node >= 0 && (size_t)v->edit_node < v->c->n && v->c->nodes[v->edit_node].kind == EX_CN_TEXT) {
        size_t len = 0;
        text = ex_buffer_text(ex_doc_buffer(v->edit_doc), &len);
        if (text && strcmp(text, v->c->nodes[v->edit_node].text ? v->c->nodes[v->edit_node].text : "") != 0) {
            ex_canvas_set_string(v->c, &v->c->nodes[v->edit_node].text, text);
            touch(v);
        }
    }
    free(text);
    ex_ui_set_focus(v->ui, w);
    ex_ui_release(v->ui, v->edit);
    ex_widget_remove(w, v->edit);
    ex_widget_free(v->edit);
    ex_doc_free(v->edit_doc);
    v->edit = NULL; v->edit_doc = NULL; v->edit_node = -1; v->edit_close = 0; v->edit_cancel = 0;
    w->pad_x = w->pad_y = 0.0f;
    commit(v);
}

static int edit_key(ExWidget *ed, ExKey key, int mods, void *user) {
    View *v = user;
    (void)ed; (void)mods;
    if (key == EX_KEY_ESCAPE) { v->edit_close = 1; return 1; }
    return 0;
}

static void begin_edit(View *v, ExWidget *w, int idx) {
    ExCanvasNode *n;
    const char *text;
    float sx, sy, ew, eh, sc = ex_renderer_scale(ex_ui_renderer(v->ui)), unit;
    double s = scale_of(v);
    if (!v->c || v->broken || idx < 0 || (size_t)idx >= v->c->n) return;
    n = &v->c->nodes[idx];
    if (n->kind != EX_CN_TEXT) return;
    if (v->edit) finish_edit(v, w);
    text = n->text ? n->text : "";
    v->edit_doc = ex_doc_create(text, strlen(text));
    if (!v->edit_doc) return;
    v->edit = ex_widget_editor(v->ui, v->edit_doc, "Card text");
    if (!v->edit) { ex_doc_free(v->edit_doc); v->edit_doc = NULL; return; }
    ex_editor_set_wrap(v->edit->editor, 1);
    v->edit->on_key = edit_key; v->edit->on_key_user = v;
    v->edit_node = idx; v->edit_close = v->edit_cancel = 0;
    to_screen(v, w, n->x, n->y, &sx, &sy);
    ew = (float)(n->w * s); eh = (float)(n->h * s);
    if (ew < 260.0f * sc) ew = 260.0f * sc;
    if (eh < 120.0f * sc) eh = 120.0f * sc;
    unit = (float)ex_theme_size(ex_ui_theme(v->ui), EX_SIZE_SPACING) * sc;
    if (unit < 1.0f) unit = 1.0f;
    w->vertical = 1; w->align_x = 0; w->align_y = 0;
    w->pad_x = (sx - w->x) / unit; w->pad_y = (sy - w->y) / unit;
    v->edit->width = ex_size_px(v->ui, ew / sc); v->edit->height = ex_size_px(v->ui, eh / sc);
    ex_widget_add(w, v->edit);
    ex_ui_set_focus(v->ui, v->edit);
    ex_doc_set_caret(v->edit_doc, strlen(text));
}

/* ---- making and changing cards ---- */

static void place_new(const View *v, const ExWidget *w, float x, float y, double *X, double *Y) {
    if (x < 0.0f) { *X = v->cx; *Y = v->cy; }
    else to_canvas(v, w, x, y, X, Y);
}

static void select_only_node(View *v, int idx) { size_t i; clear_selection(v); for (i = 0; i < v->c->n; i++) v->c->nodes[i].sel = (int)i == idx; }

static int add_card(View *v, ExWidget *w, ExCanvasKind kind, double X, double Y, double cw, double ch, int snap) {
    int idx;
    if (!v->c || v->broken) return -1;
    if (snap) { X = floor((X - cw / 2) / GRID + 0.5) * GRID; Y = floor((Y - ch / 2) / GRID + 0.5) * GRID; } else { X -= cw / 2; Y -= ch / 2; }
    idx = ex_canvas_add_node(v->c, kind, X, Y, cw, ch);
    if (idx < 0) return -1;
    select_only_node(v, idx);
    touch(v);
    (void)w;
    return idx;
}

int ex_canvasview_add_text(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    double X, Y;
    int idx;
    if (!v || !v->c || v->broken) return -1;
    if (v->edit) finish_edit(v, w);
    place_new(v, w, x, y, &X, &Y);
    idx = add_card(v, w, EX_CN_TEXT, X, Y, 260, 120, 1);
    if (idx < 0) return -1;
    commit(v);
    begin_edit(v, w, idx);
    return idx;
}

int ex_canvasview_add_group(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    double X, Y;
    int idx;
    if (!v || !v->c || v->broken) return -1;
    if (v->edit) finish_edit(v, w);
    place_new(v, w, x, y, &X, &Y);
    idx = add_card(v, w, EX_CN_GROUP, X, Y, 600, 400, 1);
    if (idx < 0) return -1;
    ex_canvas_set_string(v->c, &v->c->nodes[idx].label, "Group");
    touch(v);
    commit(v);
    return idx;
}

int ex_canvasview_add_file(ExWidget *w, const char *path, float x, float y) {
    View *v = view_of(w);
    double X, Y;
    int idx;
    if (!v || !v->c || v->broken || !path || !*path) return -1;
    if (v->edit) finish_edit(v, w);
    place_new(v, w, x, y, &X, &Y);
    idx = add_card(v, w, EX_CN_FILE, X, Y, 360, 300, 1);
    if (idx < 0) return -1;
    ex_canvas_set_string(v->c, &v->c->nodes[idx].file, path);
    touch(v);
    commit(v);
    return idx;
}

typedef struct FilePrompt { View *v; ExWidget *w; float x, y; } FilePrompt;
static FilePrompt g_prompt;
static void file_prompt_done(const char *text, void *arg) {
    FilePrompt p = *(FilePrompt *)arg;
    char path[600], url[600];
    if (!text || !*text) return;
    if (!strncmp(text, "http://", 7) || !strncmp(text, "https://", 8)) {                    /* a web address becomes a link card */
        double X, Y;
        int idx;
        snprintf(url, sizeof url, "%s", text);
        place_new(p.v, p.w, p.x, p.y, &X, &Y);
        idx = add_card(p.v, p.w, EX_CN_LINK, X, Y, 320, 90, 1);
        if (idx >= 0) { ex_canvas_set_string(p.v->c, &p.v->c->nodes[idx].url, url); touch(p.v); commit(p.v); }
        return;
    }
    if (p.v->h.resolve && p.v->h.resolve(p.v->h.user, text, path, sizeof path)) ex_canvasview_add_file(p.w, path, p.x, p.y);
    else { char msg[700]; snprintf(msg, sizeof msg, "No note or file called %s was found in the vault.", text); say(p.v, msg); }
}
void ex_canvasview_add_file_prompt(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    if (!v || !v->h.panels) return;
    g_prompt.v = v; g_prompt.w = w; g_prompt.x = x; g_prompt.y = y;
    ex_panels_ask_text(v->h.panels, "Add to canvas", "The name or path of a note or file, or a web address", "", file_prompt_done, &g_prompt);
}

static void delete_selection(View *v) {
    size_t i;
    if (!v->c || v->broken) return;
    for (i = v->c->ne; i > 0; i--) if (v->c->edges[i - 1].sel) ex_canvas_remove_edge(v->c, i - 1);
    for (i = v->c->n; i > 0; i--) if (v->c->nodes[i - 1].sel) ex_canvas_remove_node(v->c, i - 1);
    v->dirty = 1;
    commit(v);
}

/* Cards and the edges between them as JSON Canvas text (for the clipboard). */
static char *selection_text(const View *v) {
    ExCanvas *frag = ex_canvas_new();
    size_t i, e, *map;
    char *out = NULL;
    if (!frag || !v->c) { ex_canvas_free(frag); return NULL; }
    map = malloc((v->c->n + 1) * sizeof *map);
    if (!map) { ex_canvas_free(frag); return NULL; }
    for (i = 0; i < v->c->n; i++) {
        const ExCanvasNode *n = &v->c->nodes[i];
        int k;
        map[i] = (size_t)-1;
        if (!n->sel || n->raw || n->kind == EX_CN_OTHER) continue;
        k = ex_canvas_add_node(frag, n->kind, n->x, n->y, n->w, n->h);
        if (k < 0) continue;
        map[i] = (size_t)k;
        ex_canvas_set_string(frag, &frag->nodes[k].color, n->color);
        if (n->text) ex_canvas_set_string(frag, &frag->nodes[k].text, n->text);
        if (n->file) ex_canvas_set_string(frag, &frag->nodes[k].file, n->file);
        if (n->subpath) ex_canvas_set_string(frag, &frag->nodes[k].subpath, n->subpath);
        if (n->url) ex_canvas_set_string(frag, &frag->nodes[k].url, n->url);
        if (n->label) ex_canvas_set_string(frag, &frag->nodes[k].label, n->label);
        if (n->background) ex_canvas_set_string(frag, &frag->nodes[k].background, n->background);
        if (n->background_style) ex_canvas_set_string(frag, &frag->nodes[k].background_style, n->background_style);
    }
    for (e = 0; e < v->c->ne; e++) {
        const ExCanvasEdge *ed = &v->c->edges[e];
        int a, b, k;
        if (ed->raw) continue;
        a = ex_canvas_find_node(v->c, ed->from); b = ex_canvas_find_node(v->c, ed->to);
        if (a < 0 || b < 0 || map[a] == (size_t)-1 || map[b] == (size_t)-1) continue;
        k = ex_canvas_add_edge(frag, map[a], ed->from_side, map[b], ed->to_side);
        if (k < 0) continue;
        frag->edges[k].from_end = ed->from_end; frag->edges[k].to_end = ed->to_end;
        ex_canvas_set_string(frag, &frag->edges[k].color, ed->color);
        ex_canvas_set_string(frag, &frag->edges[k].label, ed->label);
    }
    if (frag->n) out = ex_canvas_write(frag, NULL);
    free(map);
    ex_canvas_free(frag);
    return out;
}

static void copy_selection(View *v) {
    char *t = selection_text(v);
    if (t) { ex_clipboard_set_text(t); free(t); say(v, "Copied the selected cards."); }
}

static void paste(View *v, ExWidget *w) {
    char *clip = ex_clipboard_get_text();
    ExCanvas *frag;
    if (!clip || !*clip || !v->c || v->broken) { free(clip); return; }
    frag = ex_canvas_parse(clip, strlen(clip), NULL, 0);
    if (frag && frag->n) {
        size_t i, e, *map = calloc(frag->n, sizeof *map);
        double minx = 1e300, miny = 1e300, tx, ty;
        if (map) {
            for (i = 0; i < frag->n; i++) if (!frag->nodes[i].raw && frag->nodes[i].kind != EX_CN_OTHER) { if (frag->nodes[i].x < minx) minx = frag->nodes[i].x; if (frag->nodes[i].y < miny) miny = frag->nodes[i].y; }
            tx = v->cx - 100; ty = v->cy - 50;                                         /* the pasted cards land at the middle of the view */
            if (minx > 1e299) { minx = 0; miny = 0; }
            clear_selection(v);
            for (i = 0; i < frag->n; i++) {
                const ExCanvasNode *n = &frag->nodes[i];
                int k;
                map[i] = (size_t)-1;
                if (n->raw || n->kind == EX_CN_OTHER) continue;
                k = ex_canvas_add_node(v->c, n->kind, floor((n->x - minx + tx) / GRID + 0.5) * GRID, floor((n->y - miny + ty) / GRID + 0.5) * GRID, n->w, n->h);
                if (k < 0) continue;
                map[i] = (size_t)k;
                v->c->nodes[k].sel = 1;
                ex_canvas_set_string(v->c, &v->c->nodes[k].color, n->color);
                if (n->text) ex_canvas_set_string(v->c, &v->c->nodes[k].text, n->text);
                if (n->file) ex_canvas_set_string(v->c, &v->c->nodes[k].file, n->file);
                if (n->subpath) ex_canvas_set_string(v->c, &v->c->nodes[k].subpath, n->subpath);
                if (n->url) ex_canvas_set_string(v->c, &v->c->nodes[k].url, n->url);
                if (n->label) ex_canvas_set_string(v->c, &v->c->nodes[k].label, n->label);
                if (n->background) ex_canvas_set_string(v->c, &v->c->nodes[k].background, n->background);
                if (n->background_style) ex_canvas_set_string(v->c, &v->c->nodes[k].background_style, n->background_style);
            }
            for (e = 0; e < frag->ne; e++) {
                const ExCanvasEdge *ed = &frag->edges[e];
                int a, b, k;
                if (ed->raw) continue;
                a = ex_canvas_find_node(frag, ed->from); b = ex_canvas_find_node(frag, ed->to);
                if (a < 0 || b < 0 || map[a] == (size_t)-1 || map[b] == (size_t)-1) continue;
                k = ex_canvas_add_edge(v->c, map[a], ed->from_side, map[b], ed->to_side);
                if (k < 0) continue;
                v->c->edges[k].from_end = ed->from_end; v->c->edges[k].to_end = ed->to_end;
                ex_canvas_set_string(v->c, &v->c->edges[k].color, ed->color);
                ex_canvas_set_string(v->c, &v->c->edges[k].label, ed->label);
            }
            free(map);
            touch(v);
            commit(v);
        }
    } else if (!frag && strlen(clip) < 100000 && !blank(clip, strlen(clip))) {         /* text that is not a canvas: a new text card */
        double X = v->cx, Y = v->cy;
        int idx = add_card(v, w, EX_CN_TEXT, X, Y, 300, 140, 1);
        if (idx >= 0) { ex_canvas_set_string(v->c, &v->c->nodes[idx].text, clip); touch(v); commit(v); }
    }
    ex_canvas_free(frag);
    free(clip);
}

static void set_color_selected(View *v, const char *color) {
    size_t i;
    if (!v->c) return;
    for (i = 0; i < v->c->n; i++) if (v->c->nodes[i].sel) ex_canvas_set_string(v->c, &v->c->nodes[i].color, color);
    for (i = 0; i < v->c->ne; i++) if (v->c->edges[i].sel) ex_canvas_set_string(v->c, &v->c->edges[i].color, color);
    touch(v);
    commit(v);
}

static void nudge(View *v, double dx, double dy) {
    size_t i, k = 0, *members;
    if (!v->c || !selected_nodes(v)) return;
    members = malloc(v->c->n * sizeof *members);
    if (!members) return;
    for (i = 0; i < v->c->n; i++) if (v->c->nodes[i].sel) members[k++] = i;
    for (i = 0; i < k; i++) {
        size_t in[256], cnt = ex_canvas_group_members(v->c, members[i], in, 256), m;
        for (m = 0; m < cnt; m++) if (!v->c->nodes[in[m]].sel) { v->c->nodes[in[m]].x += dx; v->c->nodes[in[m]].y += dy; }
    }
    for (i = 0; i < k; i++) { v->c->nodes[members[i]].x += dx; v->c->nodes[members[i]].y += dy; }
    free(members);
    touch(v);
    commit(v);
}

static void edit_label(View *v, int edge);
static void edit_group_name(View *v, int idx);

static void activate_card(View *v, ExWidget *w, int idx, int new_tab) {
    ExCanvasNode *n;
    if (!v->c || idx < 0 || (size_t)idx >= v->c->n) return;
    n = &v->c->nodes[idx];
    if (n->kind == EX_CN_TEXT) begin_edit(v, w, idx);
    else if (n->kind == EX_CN_FILE && n->file && v->h.open_file) v->h.open_file(v->h.user, n->file, n->subpath ? n->subpath : "", new_tab);
    else if (n->kind == EX_CN_LINK && n->url && v->h.open_url) v->h.open_url(v->h.user, n->url);
    else if (n->kind == EX_CN_GROUP) edit_group_name(v, idx);
}

/* ---- the edge's label ---- */

typedef struct LabelPrompt { View *v; char id[40]; } LabelPrompt;
static LabelPrompt g_label;
static void label_done(const char *text, void *arg) {
    LabelPrompt *p = arg;
    size_t e;
    if (!text || !p->v->c) return;
    for (e = 0; e < p->v->c->ne; e++) if (p->v->c->edges[e].id && !strcmp(p->v->c->edges[e].id, p->id)) {
        ex_canvas_set_string(p->v->c, &p->v->c->edges[e].label, *text ? text : NULL);
        touch(p->v); commit(p->v);
        return;
    }
}
static void edit_label(View *v, int edge) {
    if (!v->h.panels || !v->c || edge < 0 || (size_t)edge >= v->c->ne || !v->c->edges[edge].id) return;
    g_label.v = v; snprintf(g_label.id, sizeof g_label.id, "%s", v->c->edges[edge].id);
    ex_panels_ask_text(v->h.panels, "Edge label", "Label", v->c->edges[edge].label ? v->c->edges[edge].label : "", label_done, &g_label);
}

typedef struct GroupPrompt { View *v; char id[40]; } GroupPrompt;
static GroupPrompt g_group;
static void group_name_done(const char *text, void *arg) {
    GroupPrompt *p = arg;
    int idx;
    if (!text || !p->v->c) return;
    idx = ex_canvas_find_node(p->v->c, p->id);
    if (idx >= 0) { ex_canvas_set_string(p->v->c, &p->v->c->nodes[idx].label, *text ? text : NULL); touch(p->v); commit(p->v); }
}
static void edit_group_name(View *v, int idx) {
    if (!v->h.panels || !v->c || idx < 0 || (size_t)idx >= v->c->n || !v->c->nodes[idx].id) return;
    g_group.v = v; snprintf(g_group.id, sizeof g_group.id, "%s", v->c->nodes[idx].id);
    ex_panels_ask_text(v->h.panels, "Group name", "Name", v->c->nodes[idx].label ? v->c->nodes[idx].label : "", group_name_done, &g_group);
}

/* ---- convert to file ---- */

int ex_canvasview_convert_selected(ExWidget *w) {
    View *v = view_of(w);
    int idx = v ? only_selected_node(v) : -1;
    char path[600];
    const char *text;
    if (!v || idx < 0 || v->c->nodes[idx].kind != EX_CN_TEXT || !v->h.create_note) return -1;
    text = v->c->nodes[idx].text ? v->c->nodes[idx].text : "";
    if (!v->h.create_note(v->h.user, text, path, sizeof path)) return -1;
    {
        ExCanvasNode *n = &v->c->nodes[idx];
        n->kind = EX_CN_FILE;
        ex_canvas_set_string(v->c, &n->file, path);
        ex_canvas_set_string(v->c, &n->text, NULL);
        free(n->type); n->type = NULL;
        touch(v);
        commit(v);
    }
    return 0;
}

/* ---- menus ---- */

enum { M_EDIT = 1, M_OPEN, M_OPEN_TAB, M_ADD_TEXT, M_ADD_FILE, M_ADD_GROUP, M_FIT, M_DELETE, M_CONVERT, M_LABEL, M_RAISE, M_RENAME_GROUP, M_COLOR0 = 20 };
typedef struct MenuCtx { View *v; ExWidget *w; double X, Y; float sx, sy; int card, edge; } MenuCtx;
static MenuCtx g_menu;

static void menu_done(ExPanels *p, int id, void *arg) {
    MenuCtx *m = arg;
    View *v = m->v;
    ExWidget *w = m->w;
    static const char *const COLORS[7] = { NULL, "1", "2", "3", "4", "5", "6" };
    (void)p;
    if (!v->c) return;
    switch (id) {
    case M_EDIT: if (m->card >= 0) activate_card(v, w, m->card, 0); break;
    case M_OPEN: if (m->card >= 0) activate_card(v, w, m->card, 0); break;
    case M_OPEN_TAB: if (m->card >= 0) activate_card(v, w, m->card, 1); break;
    case M_ADD_TEXT: ex_canvasview_add_text(w, m->sx, m->sy); break;
    case M_ADD_FILE: ex_canvasview_add_file_prompt(w, m->sx, m->sy); break;
    case M_ADD_GROUP: ex_canvasview_add_group(w, m->sx, m->sy); break;
    case M_FIT: ex_canvasview_fit(w, 0); break;
    case M_DELETE: delete_selection(v); break;
    case M_CONVERT: ex_canvasview_convert_selected(w); break;
    case M_LABEL: edit_label(v, m->edge); break;
    case M_RENAME_GROUP: edit_group_name(v, m->card); break;
    case M_RAISE: if (m->card >= 0 && (size_t)m->card < v->c->n) { int k = ex_canvas_raise_node(v->c, (size_t)m->card); if (k >= 0) { select_only_node(v, k); touch(v); commit(v); } } break;
    default:
        if (id >= M_COLOR0 && id < M_COLOR0 + 7) set_color_selected(v, COLORS[id - M_COLOR0]);
        break;
    }
}

void ex_canvasview_context(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    ExMenuItem items[24];
    size_t n = 0;
    double X, Y;
    int card, edge = -1, k;
    static const char *const CNAMES[7] = { "Color: none", "Color: red", "Color: orange", "Color: yellow", "Color: green", "Color: cyan", "Color: purple" };
    if (!v || !v->c || v->broken || !v->h.panels) return;
    if (v->edit) finish_edit(v, w);
    to_canvas(v, w, x, y, &X, &Y);
    card = pick_card(v, w, x, y);
    if (card < 0) edge = edge_at(v, X, Y, (double)EDGE_HIT_PX * ex_renderer_scale(ex_ui_renderer(v->ui)) / scale_of(v));
    if (card >= 0 && !v->c->nodes[card].sel) select_only_node(v, card);
    if (edge >= 0 && !v->c->edges[edge].sel) { clear_selection(v); v->c->edges[edge].sel = 1; }
    memset(items, 0, sizeof items);
    g_menu.v = v; g_menu.w = w; g_menu.X = X; g_menu.Y = Y; g_menu.sx = x; g_menu.sy = y; g_menu.card = card; g_menu.edge = edge;
    if (card >= 0) {
        const ExCanvasNode *c = &v->c->nodes[card];
        if (c->kind == EX_CN_TEXT) { items[n].label = "Edit"; items[n].id = M_EDIT; n++; }
        if (c->kind == EX_CN_FILE) { items[n].label = "Open"; items[n].id = M_OPEN; n++; items[n].label = "Open in a new tab"; items[n].id = M_OPEN_TAB; n++; }
        if (c->kind == EX_CN_LINK) { items[n].label = "Open the address"; items[n].id = M_OPEN; n++; }
        if (c->kind == EX_CN_GROUP) { items[n].label = "Rename the group"; items[n].id = M_RENAME_GROUP; n++; }
        if (c->kind == EX_CN_TEXT && v->h.create_note) { items[n].label = "Convert to file..."; items[n].id = M_CONVERT; n++; }
        items[n].label = "Bring to front"; items[n].id = M_RAISE; n++;
    } else if (edge >= 0) { items[n].label = "Edit the label"; items[n].id = M_LABEL; n++; }
    else {
        items[n].label = "Add a text card here"; items[n].id = M_ADD_TEXT; n++;
        items[n].label = "Add a note or file here..."; items[n].id = M_ADD_FILE; n++;
        items[n].label = "Add a group here"; items[n].id = M_ADD_GROUP; n++;
        items[n].label = "Zoom to fit"; items[n].id = M_FIT; n++;
    }
    if (card >= 0 || edge >= 0) {
        for (k = 0; k < 7; k++) { items[n].label = CNAMES[k]; items[n].id = M_COLOR0 + k; items[n].sep_before = k == 0; n++; }
        items[n].label = "Delete"; items[n].id = M_DELETE; items[n].danger = 1; items[n].sep_before = 1; n++;
    }
    ex_panels_popup(v->h.panels, x, y, items, n, menu_done, &g_menu);
}

/* ---- the camera: the session's text ---- */

void ex_canvasview_get_view(const ExWidget *w, char *out, size_t cap) {
    const View *v = view_of(w);
    if (!cap) return;
    out[0] = 0;
    if (v && v->fitted) snprintf(out, cap, "%.0f %.0f %.3f", v->cx, v->cy, (double)v->zoom);
}
int ex_canvasview_set_view(ExWidget *w, const char *text) {
    View *v = view_of(w);
    double x, y, z;
    if (!v || !text || sscanf(text, "%lf %lf %lf", &x, &y, &z) != 3 || !isfinite(x) || !isfinite(y) || !isfinite(z) || z < ZOOM_MIN || z > ZOOM_MAX || fabs(x) > 1e9 || fabs(y) > 1e9) return -1;
    v->cx = x; v->cy = y; v->zoom = (float)z; v->fitted = 1;
    return 0;
}
void ex_canvasview_commit(ExWidget *w) { View *v = view_of(w); if (v) { if (v->edit) finish_edit(v, w); commit(v); } }
int ex_canvasview_has_selection(const ExWidget *w) { const View *v = view_of(w); return v && (selected_nodes(v) || selected_edges(v)); }

void ex_canvasview_jump_to_group_prompt(ExWidget *w);

/* ---- jump to a group ---- */

typedef struct JumpCtx { View *v; ExWidget *w; size_t idx[64]; size_t n; char labels[64][80]; const char *ptrs[64]; } JumpCtx;
static JumpCtx g_jump;
static void jump_done(size_t index, void *arg) {
    JumpCtx *j = arg;
    if (index < j->n && j->v->c && j->idx[index] < j->v->c->n) {
        const ExCanvasNode *n = &j->v->c->nodes[j->idx[index]];
        fit_box(j->v, j->w, n->x, n->y, n->x + n->w, n->y + n->h);
        select_only_node(j->v, (int)j->idx[index]);
    }
}
void ex_canvasview_jump_to_group_prompt(ExWidget *w) {
    View *v = view_of(w);
    size_t i;
    if (!v || !v->c || !v->h.panels) return;
    g_jump.v = v; g_jump.w = w; g_jump.n = 0;
    for (i = 0; i < v->c->n && g_jump.n < 64; i++) {
        const ExCanvasNode *n = &v->c->nodes[i];
        if (n->raw || n->kind != EX_CN_GROUP) continue;
        snprintf(g_jump.labels[g_jump.n], sizeof g_jump.labels[0], "%s", n->label && *n->label ? n->label : "Group without a name");
        g_jump.ptrs[g_jump.n] = g_jump.labels[g_jump.n];
        g_jump.idx[g_jump.n++] = i;
    }
    if (!g_jump.n) { say(v, "This canvas has no groups."); return; }
    ex_panels_ask_choice(v->h.panels, "Jump to group", g_jump.ptrs, g_jump.n, jump_done, &g_jump);
}

/* ---- drawing ---- */

static void draw_message(View *v, ExWidget *w, const char *msg, ExColor c) {
    ExRenderer *r = ex_ui_renderer(v->ui);
    ExFont *f = ex_ui_font(v->ui, EX_FONT_TOKEN_INTERFACE, 0);
    float size = ex_ui_base_size(v->ui);
    ExFontMetrics fm;
    if (ex_text_metrics(r, f, size, &fm) != 0) return;
    draw_text_box(v, r, msg, strlen(msg), w->x + 24.0f, w->y + 24.0f, w->w - 48.0f, w->h - 48.0f, size, c, c);
}

void ex_canvasview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    double t_start = ex_platform_work_ms();                       /* CPU time of this thread: what our own code costs, not what a busy machine adds */
    const ExTheme *t;
    ExRenderer *r;
    ExColor c_bg, c_grid, c_text, c_muted, c_surface, c_border, c_accent, c_link;
    size_t i, e;
    double s, X0, Y0, X1, Y1;
    float sc, lw;
    int show_content;
    int hide_pct;
    if (!v) return;
    t = ex_ui_theme(ui); r = ex_ui_renderer(ui);
    if (v->edit_close) finish_edit(v, w);
    if (ex_doc_version(v->doc) != v->seen && !v->edit) reload(v);
    memset(&v->stats, 0, sizeof v->stats);
    v->stats.reads = v->reads; v->stats.commits = v->commits; v->stats.editing = v->edit != NULL; v->stats.mode = v->mode; v->stats.broken = v->broken;
    c_bg = ex_theme_color(t, EX_COLOR_BACKGROUND); c_grid = ex_theme_color(t, EX_COLOR_CANVAS_GRID); c_text = ex_theme_color(t, EX_COLOR_TEXT);
    c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED); c_surface = ex_theme_color(t, EX_COLOR_SURFACE); c_border = ex_theme_color(t, EX_COLOR_BORDER);
    c_accent = ex_theme_color(t, EX_COLOR_ACCENT); c_link = ex_theme_color(t, EX_COLOR_LINK);
    ex_fill_rect(r, w->x, w->y, w->w, w->h, c_bg);
    if (v->broken || !v->c) {
        char msg[400];
        snprintf(msg, sizeof msg, "%s is not a canvas Exhaust can read, so it is shown here and never changed. %s", v->path, v->err);
        draw_message(v, w, msg, ex_theme_color(t, EX_COLOR_ERROR));
        return;
    }
    if (!v->fitted && w->w > 8.0f && w->h > 8.0f) ex_canvasview_fit(w, 0);
    sc = ex_renderer_scale(r);
    s = scale_of(v);
    lw = (float)ex_px(r, 1.0f);
    hide_pct = threshold_percent(v);
    show_content = (int)(v->zoom * 100.0f + 0.5f) >= hide_pct;
    v->stats.content_hidden = !show_content;
    v->stats.zoom = v->zoom; v->stats.cx = (float)v->cx; v->stats.cy = (float)v->cy;
    v->stats.cards = v->c->n; v->stats.edges = v->c->ne;
    to_canvas(v, w, w->x, w->y, &X0, &Y0); to_canvas(v, w, w->x + w->w, w->y + w->h, &X1, &Y1);
    if (!v->geom) { v->geom = ex_geom_create(); if (!v->geom) return; }
    ex_geom_reset(v->geom);
    ex_geom_set_feather(v->geom, 0.0f);
    ex_geom_set_cull(v->geom, w->x - 8.0f, w->y - 8.0f, w->w + 16.0f, w->h + 16.0f);
    /* the grid: dots, left out when they would be closer than 8 pixels */
    {
        double step = GRID * s;
        while (step < 8.0 * sc && step > 0.0) step *= 2.0;                       /* every second dot, then every fourth */
        if (step >= 8.0 * sc) {
            double gx = floor(X0 / (step / s)) * (step / s), gy0 = floor(Y0 / (step / s)) * (step / s), gx_end = X1, gy_end = Y1, gy;
            double cs = step / s;
            float dot = (float)ex_px(r, 1.0f);
            size_t dots = 0;
            for (gx = floor(X0 / cs) * cs; gx <= gx_end && dots < 40000; gx += cs)
                for (gy = gy0; gy <= gy_end && dots < 40000; gy += cs) {
                    float sx, sy;
                    to_screen(v, w, gx, gy, &sx, &sy);
                    ex_geom_rect(v->geom, sx - dot / 2.0f, sy - dot / 2.0f, dot, dot, c_grid);
                    dots++;
                }
        }
    }
    /* groups: a faint fill and a border; their names are drawn after the batch */
    for (i = 0; i < v->c->n; i++) {
        ExCanvasNode *n = &v->c->nodes[i];
        float sx, sy, sw, sh;
        ExColor col = c_border, fill;
        if (n->raw || n->kind != EX_CN_GROUP) continue;
        to_screen(v, w, n->x, n->y, &sx, &sy);
        sw = (float)(n->w * s); sh = (float)(n->h * s);
        if (!visible(w, sx, sy - 30.0f, sw, sh + 30.0f)) continue;
        if (n->color) named_color(t, n->color, &col);
        fill = with_alpha(col, 0.10f);
        ex_geom_rect(v->geom, sx, sy, sw, sh, fill);
        ex_geom_rect_outline(v->geom, sx, sy, sw, sh, (n->sel ? 3.0f : 1.5f) * lw, n->sel ? c_accent : col);
        v->stats.cards_drawn++;
    }
    /* edges, under the cards */
    ex_geom_set_feather(v->geom, v->zoom >= 0.5f ? 0.6f : 0.0f);                   /* a soft edge only where a line is wide enough to show it */
    for (e = 0; e < v->c->ne; e++) {
        ExCanvasEdge *ed = &v->c->edges[e];
        double p[8];
        float sp[8], ew;
        int k;
        ExColor col = ex_theme_color(t, EX_COLOR_TEXT_FAINT);
        if (!edge_curve(v->c, ed, p)) continue;
        for (k = 0; k < 4; k++) to_screen(v, w, p[2 * k], p[2 * k + 1], &sp[2 * k], &sp[2 * k + 1]);
        if (ed->color) named_color(t, ed->color, &col);
        if (ed->sel) col = c_accent;
        ew = clampf(2.0f * v->zoom * lw, lw, 4.0f * lw) * (ed->sel ? 1.6f : 1.0f);
        ex_geom_curve(v->geom, sp[0], sp[1], sp[2], sp[3], sp[4], sp[5], sp[6], sp[7], ew, col);
        {
            float hs = clampf(12.0f * v->zoom * sc, 7.0f, 26.0f);
            double nx, ny;
            int to_arrow = ed->to_end != EX_CE_NONE, from_arrow = ed->from_end == EX_CE_ARROW;
            if (to_arrow) { nx = sp[6] - sp[4]; ny = sp[7] - sp[5]; if (fabs(nx) + fabs(ny) < 0.01) { nx = sp[6] - sp[0]; ny = sp[7] - sp[1]; } ex_geom_arrow(v->geom, sp[6], sp[7], (float)nx, (float)ny, hs, col); }
            if (from_arrow) { nx = sp[0] - sp[2]; ny = sp[1] - sp[3]; if (fabs(nx) + fabs(ny) < 0.01) { nx = sp[0] - sp[6]; ny = sp[1] - sp[7]; } ex_geom_arrow(v->geom, sp[0], sp[1], (float)nx, (float)ny, hs, col); }
        }
        v->stats.edges_drawn++;
    }
    ex_geom_set_feather(v->geom, 0.0f);
    /* cards: with their content when the zoom allows it, else all in this batch */
    if (!show_content) {
        for (i = 0; i < v->c->n; i++) {
            ExCanvasNode *n = &v->c->nodes[i];
            float sx, sy, sw, sh;
            ExColor fill = c_surface, line = c_border, col;
            if (n->raw || n->kind == EX_CN_GROUP || n->kind == EX_CN_OTHER) continue;
            to_screen(v, w, n->x, n->y, &sx, &sy);
            sw = (float)(n->w * s); sh = (float)(n->h * s);
            if (!visible(w, sx, sy, sw, sh)) continue;
            if (n->color && named_color(t, n->color, &col)) { fill = mix(c_surface, col, 0.25f); line = col; }
            if (n->sel) line = c_accent;
            ex_geom_rect(v->geom, sx, sy, sw, sh, fill);
            ex_geom_rect_outline(v->geom, sx, sy, sw, sh, (n->sel ? 2.0f : 1.0f) * lw, line);
            v->stats.cards_drawn++;
        }
    }
    v->stats.triangles = ex_geom_submit(r, v->geom);
    ex_geom_reset(v->geom);
    ex_geom_set_cull(v->geom, w->x - 8.0f, w->y - 8.0f, w->w + 16.0f, w->h + 16.0f);
    /* group names */
    for (i = 0; i < v->c->n; i++) {
        const ExCanvasNode *n = &v->c->nodes[i];
        float sx, sy, size, tw, lh;
        ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 1);
        ExFontMetrics fm;
        const char *label;
        char fit[200];
        if (n->raw || n->kind != EX_CN_GROUP || v->zoom < 0.15f) continue;
        label = n->label && *n->label ? n->label : "";
        if (!*label) continue;
        size = ex_ui_base_size(ui) * clampf(v->zoom, 0.6f, 1.4f);
        to_screen(v, w, n->x, n->y, &sx, &sy);
        if (ex_text_metrics(r, f, size, &fm) != 0) continue;
        lh = (float)fm.line_height;
        if (!visible(w, sx, sy - lh - 8.0f, (float)(n->w * s), lh + 8.0f)) continue;
        ex_text_fit(r, f, size, label, strlen(label), (float)(n->w * s) - 16.0f, fit, sizeof fit);
        tw = ex_text_measure(r, f, size, fit, strlen(fit));
        ex_fill_round_rect(r, sx, sy - lh - 6.0f, tw + 14.0f, lh + 4.0f, 4.0f, with_alpha(c_surface, 1.0f));
        ex_text_draw(r, f, size, sx + 7.0f, sy - lh - 4.0f, fit, strlen(fit), c_text);
    }
    /* cards with content, in the order they stack */
    if (show_content) {
        for (i = 0; i < v->c->n; i++) {
            ExCanvasNode *n = &v->c->nodes[i];
            float sx, sy, sw, sh;
            if (n->raw || n->kind == EX_CN_GROUP || n->kind == EX_CN_OTHER) continue;
            to_screen(v, w, n->x, n->y, &sx, &sy);
            sw = (float)(n->w * s); sh = (float)(n->h * s);
            if (!visible(w, sx, sy - 24.0f, sw, sh + 24.0f)) continue;
            draw_card(v, w, r, t, n, 1, c_text, c_muted, c_surface, c_border, c_accent);
            v->stats.cards_drawn++;
        }
    }
    /* the names of file cards, above them */
    {
        const char *mode = setting_str(v, "show_card_names", "always");
        ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
        float size = ex_ui_base_size(ui) * clampf(v->zoom, 0.7f, 1.2f) * 0.85f;
        ExFontMetrics fm;
        if (strcmp(mode, "never") != 0 && v->zoom >= 0.25f && ex_text_metrics(r, f, size, &fm) == 0) {
            for (i = 0; i < v->c->n; i++) {
                const ExCanvasNode *n = &v->c->nodes[i];
                float sx, sy;
                size_t nl = 0;
                const char *nm;
                char fit[200];
                if (n->raw || n->kind != EX_CN_FILE || !n->file) continue;
                if (!strcmp(mode, "selected") && !n->sel) continue;
                to_screen(v, w, n->x, n->y, &sx, &sy);
                if (!visible(w, sx, sy - (float)fm.line_height, (float)(n->w * s), (float)fm.line_height)) continue;
                nm = base_name(n->file, &nl);
                { char tmp[300]; snprintf(tmp, sizeof tmp, "%.*s", (int)(nl > 250 ? 250 : nl), nm); ex_text_fit(r, f, size, tmp, strlen(tmp), (float)(n->w * s), fit, sizeof fit); }
                ex_text_draw(r, f, size, sx, sy - (float)fm.line_height - 2.0f, fit, strlen(fit), c_muted);
            }
        }
    }
    /* the labels of edges */
    if (v->zoom >= 0.3f) {
        ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
        float size = ex_ui_base_size(ui) * clampf(v->zoom, 0.7f, 1.3f) * 0.9f;
        ExFontMetrics fm;
        if (ex_text_metrics(r, f, size, &fm) == 0)
            for (e = 0; e < v->c->ne; e++) {
                const ExCanvasEdge *ed = &v->c->edges[e];
                double p[8], mx, my;
                float sx, sy, tw;
                char fit[200];
                if (!ed->label || !*ed->label || !edge_curve(v->c, ed, p)) continue;
                bez_point(p, 0.5, &mx, &my);
                to_screen(v, w, mx, my, &sx, &sy);
                if (!visible(w, sx - 200.0f, sy - 30.0f, 400.0f, 60.0f)) continue;
                ex_text_fit(r, f, size, ed->label, strlen(ed->label), 300.0f * sc, fit, sizeof fit);
                tw = ex_text_measure(r, f, size, fit, strlen(fit));
                ex_fill_round_rect(r, sx - tw / 2.0f - 6.0f, sy - (float)fm.line_height / 2.0f - 2.0f, tw + 12.0f, (float)fm.line_height + 4.0f, 5.0f, c_bg);
                ex_stroke_round_rect(r, sx - tw / 2.0f - 6.0f, sy - (float)fm.line_height / 2.0f - 2.0f, tw + 12.0f, (float)fm.line_height + 4.0f, 5.0f, lw, ed->sel ? c_accent : c_border);
                ex_text_draw(r, f, size, sx - tw / 2.0f, sy - (float)fm.line_height / 2.0f, fit, strlen(fit), c_text);
            }
    }
    /* what is selected: the dots at the sides of a single card, the border that resizes */
    {
        int one = only_selected_node(v);
        if (one >= 0 && !v->edit) {
            const ExCanvasNode *n = &v->c->nodes[one];
            int side;
            for (side = 0; side < 4; side++) {
                double px, py, nx, ny;
                float sx, sy;
                ex_canvas_side_point(n, side, &px, &py);
                side_normal(side, &nx, &ny);
                to_screen(v, w, px, py, &sx, &sy);
                sx += (float)nx * DOT_OFFSET_PX * sc; sy += (float)ny * DOT_OFFSET_PX * sc;
                ex_geom_circle(v->geom, sx, sy, HANDLE_PX * sc * 0.8f, c_accent);
            }
        }
    }
    /* an edge being drawn, and a box being selected */
    ex_geom_set_feather(v->geom, 0.6f);
    if (v->mode == MODE_EDGE && v->moved && v->c && v->edge_from >= 0 && (size_t)v->edge_from < v->c->n) {
        double px, py;
        float sx, sy;
        ex_canvas_side_point(&v->c->nodes[v->edge_from], v->edge_side, &px, &py);
        to_screen(v, w, px, py, &sx, &sy);
        ex_geom_line(v->geom, sx, sy, v->cur_x, v->cur_y, 2.0f * lw, c_accent);
        ex_geom_circle(v->geom, v->cur_x, v->cur_y, 4.0f * sc, c_accent);
    }
    if (v->mode == MODE_BOX && v->moved) {
        float bx = fminf(v->press_x, v->cur_x), by = fminf(v->press_y, v->cur_y), bw = fabsf(v->cur_x - v->press_x), bh = fabsf(v->cur_y - v->press_y);
        ex_geom_rect(v->geom, bx, by, bw, bh, with_alpha(c_accent, 0.12f));
        ex_geom_rect_outline(v->geom, bx, by, bw, bh, lw, c_accent);
    }
    v->stats.triangles += ex_geom_submit(r, v->geom);
    {
        ExGeomStats gs;
        ex_geom_stats(v->geom, &gs);
        v->stats.geom_grows = gs.grows;
    }
    v->stats.selected_cards = selected_nodes(v); v->stats.selected_edges = selected_edges(v);
    v->stats.own_ms = ex_platform_work_ms() - t_start;
    (void)c_link;
    if (!v->c->n && !v->edit) {
        ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
        const char *msg = "An empty canvas. Double click to add a text card, or right click for more.";
        float size = ex_ui_base_size(ui), tw = ex_text_measure(r, f, size, msg, strlen(msg));
        ExFontMetrics fm;
        if (ex_text_metrics(r, f, size, &fm) == 0) ex_text_draw(r, f, size, w->x + (w->w - tw) / 2.0f, w->y + (w->h - (float)fm.line_height) / 2.0f, msg, strlen(msg), c_muted);
    }
}

/* ---- the mouse ---- */

static int is_double(View *v, float x, float y) {
    uint32_t now = SDL_GetTicks();
    float sc = ex_renderer_scale(ex_ui_renderer(v->ui));
    int dbl = v->last_click_ms && now - v->last_click_ms <= DOUBLE_MS && fabsf(x - v->last_click_x) <= DRAG_PX * sc && fabsf(y - v->last_click_y) <= DRAG_PX * sc;
    v->last_click_ms = dbl ? 0 : now; v->last_click_x = x; v->last_click_y = y;
    return dbl;
}

static void start_move(View *v) {
    size_t i, k;
    v->nmv = 0;
    for (i = 0; i < v->c->n; i++) {
        const ExCanvasNode *n = &v->c->nodes[i];
        size_t in[512], cnt, m;
        if (!n->sel || n->raw || n->kind == EX_CN_OTHER) continue;
        if (n->kind == EX_CN_GROUP) {
            cnt = ex_canvas_group_members(v->c, i, in, 512);
            for (m = 0; m < cnt; m++) {
                int dup = 0;
                for (k = 0; k < v->nmv; k++) if (v->mv[k] == in[m]) { dup = 1; break; }
                if (!dup && !v->c->nodes[in[m]].sel) { if (v->nmv == v->mvcap) { size_t nc = v->mvcap ? v->mvcap * 2 : 16; v->mv = realloc(v->mv, nc * sizeof *v->mv); v->mv_x = realloc(v->mv_x, nc * sizeof *v->mv_x); v->mv_y = realloc(v->mv_y, nc * sizeof *v->mv_y); v->mvcap = nc; } if (!v->mv || !v->mv_x || !v->mv_y) { v->nmv = 0; return; } v->mv[v->nmv] = in[m]; v->nmv++; }
            }
        }
    }
    for (i = 0; i < v->c->n; i++) {
        const ExCanvasNode *n = &v->c->nodes[i];
        if (!n->sel || n->raw || n->kind == EX_CN_OTHER) continue;
        { int dup = 0; for (k = 0; k < v->nmv; k++) if (v->mv[k] == i) { dup = 1; break; } if (dup) continue; }
        if (v->nmv == v->mvcap) { size_t nc = v->mvcap ? v->mvcap * 2 : 16; v->mv = realloc(v->mv, nc * sizeof *v->mv); v->mv_x = realloc(v->mv_x, nc * sizeof *v->mv_x); v->mv_y = realloc(v->mv_y, nc * sizeof *v->mv_y); v->mvcap = nc; }
        if (!v->mv || !v->mv_x || !v->mv_y) { v->nmv = 0; return; }
        v->mv[v->nmv++] = i;
    }
    for (k = 0; k < v->nmv; k++) { v->mv_x[k] = v->c->nodes[v->mv[k]].x; v->mv_y[k] = v->c->nodes[v->mv[k]].y; }
}

/* The side of a card an edge dropped on it at (X, Y) enters by: the side nearest to the point, or, near the middle of the card, the side that faces the card it comes from. */
static int drop_side(const ExCanvas *c, int to, double X, double Y, int from) {
    const ExCanvasNode *n = &c->nodes[to];
    double w = n->w > 1 ? n->w : 1, h = n->h > 1 ? n->h : 1, dx = (X - (n->x + n->w / 2)) / w, dy = (Y - (n->y + n->h / 2)) / h;
    if (fabs(dx) < 0.2 && fabs(dy) < 0.2 && from >= 0 && (size_t)from < c->n) {
        const ExCanvasNode *f = &c->nodes[from];
        return ex_canvas_nearest_side(n, f->x + f->w / 2, f->y + f->h / 2);
    }
    return ex_canvas_nearest_side(n, X, Y);
}

static int handle_at(const View *v, const ExWidget *w, int idx, float sx, float sy) {
    const ExCanvasNode *n = &v->c->nodes[idx];
    float x0, y0, x1, y1, band = HANDLE_PX * ex_renderer_scale(ex_ui_renderer(v->ui));
    int mask = 0;
    to_screen(v, w, n->x, n->y, &x0, &y0);
    to_screen(v, w, n->x + n->w, n->y + n->h, &x1, &y1);
    if (sx < x0 - band || sx > x1 + band || sy < y0 - band || sy > y1 + band) return 0;
    if (fabsf(sx - x0) <= band) mask |= 1;
    if (fabsf(sx - x1) <= band) mask |= 2;
    if (fabsf(sy - y0) <= band) mask |= 4;
    if (fabsf(sy - y1) <= band) mask |= 8;
    if ((mask & 3) == 3) mask &= ~((fabsf(sx - x0) < fabsf(sx - x1)) ? 2 : 1);
    if ((mask & 12) == 12) mask &= ~((fabsf(sy - y0) < fabsf(sy - y1)) ? 8 : 4);
    return mask;
}

static int dot_at(const View *v, const ExWidget *w, int idx, float sx, float sy) {
    const ExCanvasNode *n = &v->c->nodes[idx];
    float sc = ex_renderer_scale(ex_ui_renderer(v->ui));
    int side;
    for (side = 0; side < 4; side++) {
        double px, py, nx, ny;
        float dx, dy;
        ex_canvas_side_point(n, side, &px, &py);
        side_normal(side, &nx, &ny);
        to_screen(v, w, px, py, &dx, &dy);
        dx += (float)nx * DOT_OFFSET_PX * sc; dy += (float)ny * DOT_OFFSET_PX * sc;
        if (hypotf(sx - dx, sy - dy) <= HANDLE_PX * sc * 1.4f) return side;
    }
    return -1;
}

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    double s = scale_of(v), sc = ex_renderer_scale(ex_ui_renderer(v->ui));
    if (kind == EX_MOUSE_DOWN) {
        SDL_Keymod mods;
        int idx, one, dbl;
        if (under != w) return 0;                                                  /* a press on the card editor is its own */
        if (v->edit) finish_edit(v, w);
        ex_ui_set_focus(v->ui, w);
        v->pressed = 1; v->moved = 0; v->mode = MODE_NONE; v->grab = v->grab_edge = -1;
        v->press_x = v->cur_x = x; v->press_y = v->cur_y = y; v->press_cx = v->cx; v->press_cy = v->cy;
        mods = SDL_GetModState();
        v->shift_at_press = (mods & SDL_KMOD_SHIFT) != 0;
        v->ctrl_at_press = (mods & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (!v->c || v->broken) { v->mode = MODE_PAN; return 1; }
        dbl = is_double(v, x, y);
        one = only_selected_node(v);
        if (!dbl && one >= 0) {
            int side = dot_at(v, w, one, x, y), mask;
            if (side >= 0) { v->mode = MODE_EDGE; v->edge_from = one; v->edge_side = side; return 1; }
            mask = handle_at(v, w, one, x, y);
            if (mask && v->c->nodes[one].kind != EX_CN_OTHER) {
                const ExCanvasNode *n = &v->c->nodes[one];
                v->mode = MODE_RESIZE; v->handle = mask; v->grab = one;
                v->r_x = n->x; v->r_y = n->y; v->r_w = n->w; v->r_h = n->h;
                return 1;
            }
        }
        idx = pick_card(v, w, x, y);
        if (dbl) {
            double X, Y;
            to_canvas(v, w, x, y, &X, &Y);
            if (idx >= 0) {
                if (v->c->nodes[idx].kind == EX_CN_GROUP) edit_group_name(v, idx); else activate_card(v, w, idx, 0);
            } else {
                int ed = edge_at(v, X, Y, EDGE_HIT_PX * sc / s);
                if (ed >= 0) edit_label(v, ed); else ex_canvasview_add_text(w, x, y);
            }
            v->pressed = 0;
            return 1;
        }
        if (idx >= 0) {
            ExCanvasNode *n = &v->c->nodes[idx];
            v->grab = idx;
            if (!n->sel) { if (!v->shift_at_press) clear_selection(v); n->sel = 1; }
            else if (!v->shift_at_press && selected_edges(v)) { size_t i; for (i = 0; i < v->c->ne; i++) v->c->edges[i].sel = 0; }
            v->mode = MODE_MOVE;
            return 1;
        }
        {
            double X, Y;
            int ed;
            to_canvas(v, w, x, y, &X, &Y);
            ed = edge_at(v, X, Y, EDGE_HIT_PX * sc / s);
            if (ed >= 0) {
                if (!v->shift_at_press) clear_selection(v);
                v->c->edges[ed].sel = 1;
                v->grab_edge = ed;
                v->mode = MODE_NONE;
                return 1;
            }
        }
        if (v->shift_at_press || v->ctrl_at_press) { v->mode = MODE_BOX; return 1; }
        clear_selection(v);
        v->mode = MODE_PAN;
        return 1;
    }
    if (!v->pressed) return 0;
    if (kind == EX_MOUSE_DRAG) {
        float dx = x - v->press_x, dy = y - v->press_y;
        v->cur_x = x; v->cur_y = y;
        if (!v->moved && dx * dx + dy * dy > DRAG_PX * DRAG_PX * sc * sc) { v->moved = 1; if (v->mode == MODE_MOVE) start_move(v); }
        if (!v->moved) return 1;
        if (v->mode == MODE_PAN) { v->cx = v->press_cx - dx / s; v->cy = v->press_cy - dy / s; v->fitted = 1; }
        else if (v->mode == MODE_MOVE && v->nmv) {
            double ddx = dx / s, ddy = dy / s, adx, ady, bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
            size_t k;
            for (k = 0; k < v->nmv; k++) {
                const ExCanvasNode *n = &v->c->nodes[v->mv[k]];
                if (v->mv_x[k] < bx0) bx0 = v->mv_x[k];
                if (v->mv_y[k] < by0) by0 = v->mv_y[k];
                if (v->mv_x[k] + n->w > bx1) bx1 = v->mv_x[k] + n->w;
                if (v->mv_y[k] + n->h > by1) by1 = v->mv_y[k] + n->h;
            }
            snap_box(v, NULL, bx0 + ddx, by0 + ddy, bx1 - bx0, by1 - by0, v->mv, v->nmv, &adx, &ady);
            for (k = 0; k < v->nmv; k++) { v->c->nodes[v->mv[k]].x = v->mv_x[k] + ddx + adx; v->c->nodes[v->mv[k]].y = v->mv_y[k] + ddy + ady; }
            ex_canvas_touch(v->c); v->dirty = 1;
        } else if (v->mode == MODE_RESIZE && v->grab >= 0 && (size_t)v->grab < v->c->n) {
            ExCanvasNode *n = &v->c->nodes[v->grab];
            double ddx = dx / s, ddy = dy / s, x0 = v->r_x, y0 = v->r_y, x1 = v->r_x + v->r_w, y1 = v->r_y + v->r_h;
            if (v->handle & 1) x0 += ddx;
            if (v->handle & 2) x1 += ddx;
            if (v->handle & 4) y0 += ddy;
            if (v->handle & 8) y1 += ddy;
            if (setting_bool(v, "snap_to_grid", 1)) {
                if (v->handle & 1) x0 = floor(x0 / GRID + 0.5) * GRID;
                if (v->handle & 2) x1 = floor(x1 / GRID + 0.5) * GRID;
                if (v->handle & 4) y0 = floor(y0 / GRID + 0.5) * GRID;
                if (v->handle & 8) y1 = floor(y1 / GRID + 0.5) * GRID;
            }
            if (x1 - x0 < MIN_CARD) { if (v->handle & 1) x0 = x1 - MIN_CARD; else x1 = x0 + MIN_CARD; }
            if (y1 - y0 < MIN_CARD) { if (v->handle & 4) y0 = y1 - MIN_CARD; else y1 = y0 + MIN_CARD; }
            n->x = x0; n->y = y0; n->w = x1 - x0; n->h = y1 - y0;
            ex_canvas_touch(v->c); v->dirty = 1;
        }
        return 1;
    }
    /* released */
    v->pressed = 0;
    {
        int moved = v->moved, mode = v->mode;
        v->mode = MODE_NONE;
        if (mode == MODE_MOVE) {
            if (moved) commit(v);
            else if (v->grab >= 0 && (size_t)v->grab < v->c->n) {                   /* a click, no drag: it selects only this card (or takes it off a selection with Shift) */
                if (v->shift_at_press) { /* the press already added it; a second Shift click removes it */ }
                else select_only_node(v, v->grab);
            }
        } else if (mode == MODE_RESIZE) { if (moved) commit(v); }
        else if (mode == MODE_EDGE && v->c) {
            int to = pick_card(v, w, x, y);
            if (moved) {
                if (to >= 0 && to != v->edge_from && 1) {
                    double X, Y;
                    int side, k;
                    to_canvas(v, w, x, y, &X, &Y);
                    side = drop_side(v->c, to, X, Y, v->edge_from);
                    k = ex_canvas_add_edge(v->c, (size_t)v->edge_from, v->edge_side, (size_t)to, side);
                    if (k >= 0) { clear_selection(v); v->c->edges[k].sel = 1; touch(v); commit(v); }
                } else if (to < 0) {                                                  /* dropped on empty space: a new text card there, joined */
                    double X, Y;
                    int idx, k, side;
                    to_canvas(v, w, x, y, &X, &Y);
                    idx = add_card(v, w, EX_CN_TEXT, X, Y, 260, 120, 1);
                    if (idx >= 0) {
                        side = drop_side(v->c, idx, v->c->nodes[idx].x + v->c->nodes[idx].w / 2, v->c->nodes[idx].y + v->c->nodes[idx].h / 2, v->edge_from);
                        k = ex_canvas_add_edge(v->c, (size_t)v->edge_from, v->edge_side, (size_t)idx, side);
                        (void)k;
                        touch(v); commit(v);
                        begin_edit(v, w, idx);
                    }
                }
            }
        } else if (mode == MODE_BOX && moved && v->c) {
            float bx0 = fminf(v->press_x, x), by0 = fminf(v->press_y, y), bx1 = fmaxf(v->press_x, x), by1 = fmaxf(v->press_y, y);
            size_t i;
            const char *dragmode = setting_str(v, "ctrl_drag", "menu");
            if (v->ctrl_at_press && !v->shift_at_press && !strcmp(dragmode, "menu") && v->h.panels) {            /* Ctrl-drag: a box, then a menu of what to make in it */
                double X0, Y0, X1, Y1;
                ExMenuItem items[3];
                to_canvas(v, w, bx0, by0, &X0, &Y0); to_canvas(v, w, bx1, by1, &X1, &Y1);
                memset(items, 0, sizeof items);
                items[0].label = "Add a text card here"; items[0].id = M_ADD_TEXT;
                items[1].label = "Add a note or file here..."; items[1].id = M_ADD_FILE;
                items[2].label = "Add a group here"; items[2].id = M_ADD_GROUP;
                g_menu.v = v; g_menu.w = w; g_menu.sx = (bx0 + bx1) / 2.0f; g_menu.sy = (by0 + by1) / 2.0f; g_menu.card = g_menu.edge = -1;
                ex_panels_popup(v->h.panels, x, y, items, 3, menu_done, &g_menu);
            } else {
                if (!v->shift_at_press) clear_selection(v);
                for (i = 0; i < v->c->n; i++) {
                    const ExCanvasNode *n = &v->c->nodes[i];
                    float sx, sy, sw, sh;
                    if (n->raw || n->kind == EX_CN_OTHER) continue;
                    to_screen(v, w, n->x, n->y, &sx, &sy);
                    sw = (float)(n->w * s); sh = (float)(n->h * s);
                    if (sx < bx1 && sx + sw > bx0 && sy < by1 && sy + sh > by0) v->c->nodes[i].sel = 1;
                }
            }
        }
        v->grab = -1;
    }
    return 1;
}

void ex_canvasview_wheel(ExWidget *w, float dy, float x, float y) {
    View *v = view_of(w);
    int ctrl, zoom;
    if (!v || dy == 0.0f) return;
    if (v->edit) finish_edit(v, w);
    ctrl = (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    zoom = !strcmp(setting_str(v, "wheel", "pan"), "zoom");
    if (ctrl) zoom = !zoom;
    if (zoom) zoom_about(v, w, powf(1.12f, dy), x, y);
    else { v->cy -= (double)dy * 60.0 / v->zoom; v->fitted = 1; }
}

/* ---- the keyboard ---- */

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    int primary = (mods & EX_MOD_PRIMARY) != 0;
    double step = (mods & EX_MOD_SHIFT) ? GRID * 5 : GRID;
    if (!v->c || v->broken) return 0;
    if (primary) {
        switch (key) {
        case EX_KEY_A: { size_t i; clear_selection(v); for (i = 0; i < v->c->n; i++) if (!v->c->nodes[i].raw && v->c->nodes[i].kind != EX_CN_OTHER) v->c->nodes[i].sel = 1; return 1; }
        case EX_KEY_C: if (selected_nodes(v)) copy_selection(v); return 1;
        case EX_KEY_X: if (selected_nodes(v)) { copy_selection(v); delete_selection(v); } return 1;
        case EX_KEY_V: paste(v, w); return 1;
        case EX_KEY_Z: if (mods & EX_MOD_SHIFT) ex_doc_redo(v->doc); else ex_doc_undo(v->doc); return 1;
        case EX_KEY_Y: ex_doc_redo(v->doc); return 1;
        default: return 0;
        }
    }
    switch (key) {
    case EX_KEY_DELETE: case EX_KEY_BACKSPACE: if (selected_nodes(v) || selected_edges(v)) { delete_selection(v); return 1; } return 0;
    case EX_KEY_ENTER:
        if (only_selected_node(v) >= 0) { activate_card(v, w, only_selected_node(v), (mods & EX_MOD_SHIFT) ? 1 : 0); return 1; }
        if (selected_edges(v) == 1 && !selected_nodes(v)) { size_t e; for (e = 0; e < v->c->ne; e++) if (v->c->edges[e].sel) { edit_label(v, (int)e); return 1; } }
        return 0;
    case EX_KEY_ESCAPE: if (selected_nodes(v) || selected_edges(v)) { clear_selection(v); return 1; } return 0;
    case EX_KEY_HOME: ex_canvasview_fit(w, selected_nodes(v) > 0); return 1;
    case EX_KEY_LEFT: case EX_KEY_RIGHT: case EX_KEY_UP: case EX_KEY_DOWN: {
        double dx = key == EX_KEY_LEFT ? -step : key == EX_KEY_RIGHT ? step : 0, dy = key == EX_KEY_UP ? -step : key == EX_KEY_DOWN ? step : 0;
        if (selected_nodes(v)) { nudge(v, dx, dy); return 1; }
        v->cx += dx * 3.0 / v->zoom * 0.3; v->cy += dy * 3.0 / v->zoom * 0.3; v->fitted = 1;
        return 1;
    }
    default: return 0;
    }
}

static int text_hook(ExWidget *w, const char *s, size_t len, void *user) {
    View *v = user;
    if (len != 1) return 0;
    if (s[0] == '+' || s[0] == '=') { zoom_about(v, w, 1.25f, w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    if (s[0] == '-') { zoom_about(v, w, 0.8f, w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    if (s[0] == '0') { ex_canvasview_fit(w, 0); return 1; }
    if (s[0] == '1') { v->zoom = 1.0f; v->fitted = 1; return 1; }
    return 0;
}

/* ---- the widget ---- */

static void view_free(void *p) {
    View *v = p;
    if (!v) return;
    if (v->edit_doc) ex_doc_free(v->edit_doc);
    ex_canvas_free(v->c);
    ex_geom_free(v->geom);
    free(v->mv); free(v->mv_x); free(v->mv_y);
    free(v->path);
    free(v);
}

ExWidget *ex_widget_canvas(ExUi *ui, ExDoc *doc, const char *path, const ExCanvasHandlers *h) {
    ExWidget *w = ex_widget_box(ui, 0, path && *path ? path : "Canvas");
    View *v;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    v->ui = ui; v->doc = doc; v->path = dups(path ? path : ""); v->zoom = 1.0f; v->edit_node = -1; v->hover = v->grab = v->grab_edge = -1;
    if (h) v->h = *h;
    w->type = EX_WIDGET_CANVAS;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1;
    w->clip = 1;
    w->vertical = 1; w->align_x = 0; w->align_y = 0;
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    w->on_text = text_hook; w->on_text_user = v;
    reload(v);
    return w;
}

/* ---- for the tests and the screen reader ---- */

void ex_canvasview_stats(const ExWidget *w, ExCanvasViewStats *out) {
    const View *v = view_of(w);
    memset(out, 0, sizeof *out);
    if (v) *out = v->stats;
}
const ExCanvas *ex_canvasview_model(const ExWidget *w) { const View *v = view_of(w); return v ? v->c : NULL; }
int ex_canvasview_card_rect(const ExWidget *w, size_t index, float *x, float *y, float *wd, float *ht) {
    const View *v = view_of(w);
    double s;
    if (!v || !v->c || index >= v->c->n || v->c->nodes[index].raw) return 0;
    s = scale_of(v);
    to_screen(v, w, v->c->nodes[index].x, v->c->nodes[index].y, x, y);
    *wd = (float)(v->c->nodes[index].w * s); *ht = (float)(v->c->nodes[index].h * s);
    return 1;
}
char *ex_canvasview_summary(const ExWidget *w) {
    const View *v = view_of(w);
    char *s = malloc(700);
    if (!s) return NULL;
    if (!v || !v->c) { snprintf(s, 700, "This file is not a canvas Exhaust can read."); return s; }
    snprintf(s, 700, "Canvas with %zu cards and %zu edges.", v->c->n, v->c->ne);
    if (selected_nodes(v) || selected_edges(v)) {
        size_t used = strlen(s);
        snprintf(s + used, 700 - used, " %zu card%s and %zu edge%s selected.", selected_nodes(v), selected_nodes(v) == 1 ? "" : "s", selected_edges(v), selected_edges(v) == 1 ? "" : "s");
    }
    return s;
}
