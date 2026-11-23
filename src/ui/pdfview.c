/* The PDF tab: see include/exhaust_pdfview.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_pdfview.h"
#include "exhaust_render.h"

#define MAX_HITS 20000
#define KEEP_PAGES 4                    /* pages kept (textures and text) on each side of the window */

typedef struct PageState {
    ExTexture *tex; int tw, th; float rscale;          /* the texture's size and the scale (pixels per point) it was drawn at */
    int req_w, req_h;                                  /* the size of the render asked for and not yet back */
    uint32_t *chars; float *boxes; size_t nchars; int text_req;
    ExPdfLink *links; size_t nlinks; int links_req;
    unsigned long used;
} PageState;

typedef struct View {
    ExUi *ui;
    char *path;
    ExPdfViewHandlers h;
    ExPdf *doc; int loading, failed, load_asked; char err[300];
    PageState *pg; int npg;
    double *top; int top_for_pages; float top_scale;   /* the pages' tops in view units, for the scale they were made at */
    int mode; float zoom;                              /* 0 fit width, 1 fit page, 2 custom */
    float scale;                                       /* the scale drawn */
    float scroll;                                      /* pixels from the top of the first page */
    float content_h, view_w, view_h, area_x, area_y;
    int toc_open; float toc_scroll;
    int search_open, match_case, highlight_all; char query[200]; ExPdfHit *hits; size_t nhits, nhits_cap; size_t hit_index; int search_done;
    int sel_page, sel_start, sel_end, sel_anchor, selecting;
    int want_page, want_sel_s, want_sel_e;             /* a goto that waits for the document or the text */
    int goto_sel_pending;
    unsigned long uploads, frame;
    float tb_h;
    struct { float x, y, w, h; int id; } btn[10]; int nbtn;
    float toc_w;
    ExPdfViewStats st;
    double own_ms;
    int dragging_scroll; float press_x, press_y;
    int last_page_shown;
} View;

enum { B_TOC = 1, B_FITW, B_FITP, B_ZOUT, B_ZIN, B_SEARCH, B_CASE, B_ALL, B_PREV, B_NEXT };

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_PDF ? (View *)w->owned : NULL; }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void page_free(PageState *p) {
    ex_texture_free(p->tex); free(p->chars); free(p->boxes); free(p->links);
    memset(p, 0, sizeof *p);
}

static void ask_load(View *v) { if (v->load_asked || !v->h.load) return; v->load_asked = 1; v->loading = 1; v->h.load(v->h.user, v->path); }

void ex_pdfview_set_doc(ExWidget *w, ExPdf *doc, const char *error) {
    View *v = view_of(w);
    int i;
    if (!v) { ex_pdf_close_async(doc); return; }
    v->loading = 0;
    if (!doc) { v->failed = 1; snprintf(v->err, sizeof v->err, "%s", error ? error : "This PDF cannot be opened."); return; }
    if (v->doc) ex_pdf_close_async(v->doc);
    for (i = 0; i < v->npg; i++) page_free(&v->pg[i]);
    free(v->pg); free(v->top);
    v->doc = doc; v->failed = 0;
    v->npg = ex_pdf_info(doc)->pages;
    v->pg = calloc((size_t)v->npg, sizeof *v->pg);
    v->top = NULL; v->top_for_pages = 0;
    v->nhits = 0; v->hit_index = 0; v->sel_page = -1;
    if (ex_pdf_info(doc)->ntoc > 0) v->toc_open = 0;
}
const char *ex_pdfview_path(const ExWidget *w) { const View *v = view_of(w); return v ? v->path : ""; }
int ex_pdfview_has_doc(const ExWidget *w) { const View *v = view_of(w); return v && v->doc != NULL; }

/* ---- layout ---- */

static void compute_scale(View *v, const ExPdfInfo *in, float sc) {
    float margin = 16.0f * sc, maxw = 1.0f, avail_w = v->view_w - 2.0f * margin, avail_h = v->view_h - 2.0f * margin;
    int i, cur = v->last_page_shown;
    for (i = 0; i < in->pages && i < 4000; i++) if ((float)in->w[i] > maxw) maxw = (float)in->w[i];
    if (cur < 0 || cur >= in->pages) cur = 0;
    if (v->mode == 0) v->scale = avail_w / maxw;
    else if (v->mode == 1) v->scale = fminf(avail_w / (float)in->w[cur], avail_h / (float)in->h[cur]);
    else v->scale = v->zoom;
    v->scale = clampf(v->scale, 0.05f, 8.0f);
}

static void compute_tops(View *v, const ExPdfInfo *in, float sc) {
    int i;
    double y = 16.0 * sc, gap = 12.0 * sc;
    if (v->top && v->top_for_pages == in->pages && fabsf(v->top_scale - v->scale) < 1e-6f) return;
    free(v->top);
    v->top = malloc(((size_t)in->pages + 1) * sizeof *v->top);
    if (!v->top) return;
    for (i = 0; i < in->pages; i++) { v->top[i] = y; y += in->h[i] * (double)v->scale + gap; }
    v->top[in->pages] = y + 4.0 * sc;
    v->top_for_pages = in->pages; v->top_scale = v->scale;
    v->content_h = (float)v->top[in->pages];
}

static int page_at(const View *v, int pages, double y) {          /* the page whose top is at or above y (binary search) */
    int lo = 0, hi = pages - 1;
    if (!v->top) return 0;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (v->top[mid] <= y) lo = mid; else hi = mid - 1; }
    return lo;
}

static void scroll_to_page(View *v, int page) {
    if (!v->top || page < 0 || page >= v->top_for_pages) return;
    v->scroll = (float)v->top[page] - 8.0f;
    if (v->scroll < 0) v->scroll = 0;
}

/* a point in the page (points, origin bottom left) to the window */
static void to_screen(const View *v, int page, const ExPdfInfo *in, float fx, float fy, float *x, float *y, float ox) {
    float px = ox + (v->view_w - (float)in->w[page] * v->scale) / 2.0f;
    *x = px + fx * v->scale;
    *y = v->area_y + (float)v->top[page] - v->scroll + ((float)in->h[page] - fy) * v->scale;
}
static void page_origin(const View *v, int page, const ExPdfInfo *in, float ox, float *x, float *y) { to_screen(v, page, in, 0.0f, (float)in->h[page], x, y, ox); }

static const char *utf8_of(const uint32_t *c, size_t from, size_t to, char *out, size_t cap) {
    size_t i, o = 0;
    for (i = from; i < to && o + 5 < cap; i++) {
        uint32_t u = c[i];
        if (u == 0) continue;
        if (u < 0x80) out[o++] = (char)u;
        else if (u < 0x800) { out[o++] = (char)(0xC0 | (u >> 6)); out[o++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { out[o++] = (char)(0xE0 | (u >> 12)); out[o++] = (char)(0x80 | ((u >> 6) & 63)); out[o++] = (char)(0x80 | (u & 63)); }
        else { out[o++] = (char)(0xF0 | (u >> 18)); out[o++] = (char)(0x80 | ((u >> 12) & 63)); out[o++] = (char)(0x80 | ((u >> 6) & 63)); out[o++] = (char)(0x80 | (u & 63)); }
    }
    out[o] = 0;
    return out;
}

/* ---- results ---- */

static void pump_results(View *v, ExRenderer *r) {
    ExPdfResult res;
    int n = 0;
    while (n < 12 && ex_pdf_poll(v->doc, &res)) {
        n++;
        if (res.kind == EX_PDF_RENDER && res.page >= 0 && res.page < v->npg) {
            PageState *p = &v->pg[res.page];
            if (p->tex) ex_texture_free(p->tex);
            p->tex = ex_texture_create(r, res.w, res.h);
            if (p->tex && ex_texture_update_rows(p->tex, res.rgba, 0, res.h) == 0) { p->tw = res.w; p->th = res.h; p->rscale = (float)res.w / (float)(ex_pdf_info(v->doc)->w[res.page]); v->uploads++; }
            else { ex_texture_free(p->tex); p->tex = NULL; }
            p->req_w = p->req_h = 0;
            ex_pdf_result_free(&res);
            break;                                                                    /* one upload a frame */
        } else if (res.kind == EX_PDF_TEXT && res.page >= 0 && res.page < v->npg) {
            PageState *p = &v->pg[res.page];
            free(p->chars); free(p->boxes);
            p->chars = res.chars; p->boxes = res.boxes; p->nchars = res.nchars; res.chars = NULL; res.boxes = NULL;
            ex_pdf_result_free(&res);
        } else if (res.kind == EX_PDF_LINKS && res.page >= 0 && res.page < v->npg) {
            PageState *p = &v->pg[res.page];
            free(p->links);
            p->links = res.links; p->nlinks = res.nlinks; res.links = NULL;
            ex_pdf_result_free(&res);
        } else if (res.kind == EX_PDF_SEARCH) {
            size_t k;
            for (k = 0; k < res.nhits && v->nhits < MAX_HITS; k++) {
                if (v->nhits == v->nhits_cap) { size_t nc = v->nhits_cap ? v->nhits_cap * 2 : 64; ExPdfHit *nh = realloc(v->hits, nc * sizeof *nh); if (!nh) break; v->hits = nh; v->nhits_cap = nc; }
                v->hits[v->nhits++] = res.hits[k];
            }
            if (res.done) v->search_done = 1;
            ex_pdf_result_free(&res);
        } else ex_pdf_result_free(&res);
    }
}

/* ---- selection, search, goto ---- */

static void ensure_text(View *v, int page) { PageState *p = &v->pg[page]; if (!p->chars && !p->text_req) { p->text_req = 1; ex_pdf_request_text(v->doc, page); } }

static void select_range(View *v, int page, int s, int e) { v->sel_page = page; v->sel_start = s; v->sel_end = e; v->sel_anchor = s; }

static void apply_wanted(View *v) {
    if (v->want_page < 0) return;
    if (v->want_sel_e > v->want_sel_s) {
        PageState *p = &v->pg[v->want_page];
        ensure_text(v, v->want_page);
        if (!p->chars) return;                                                    /* wait for the text */
        {
            int s = v->want_sel_s, e = v->want_sel_e;
            if (s < 0) s = 0;
            if (e > (int)p->nchars) e = (int)p->nchars;
            if (e > s) select_range(v, v->want_page, s, e);
        }
    }
    v->want_page = -1;
}

void ex_pdfview_goto(ExWidget *w, int page, int s, int e) {
    View *v = view_of(w);
    if (!v) return;
    v->want_page = page < 0 ? 0 : page; v->want_sel_s = s; v->want_sel_e = e; v->goto_sel_pending = 1;
    if (v->doc && v->want_page >= v->npg) v->want_page = v->npg - 1;
    if (v->doc && v->top) scroll_to_page(v, v->want_page);
}

static void goto_hit(View *v, size_t i) {
    if (i >= v->nhits) return;
    v->hit_index = i;
    v->want_page = v->hits[i].page; v->want_sel_s = v->hits[i].start; v->want_sel_e = v->hits[i].start + v->hits[i].count;
    if (v->top) scroll_to_page(v, v->want_page);
}

static void start_search(View *v) {
    v->nhits = 0; v->hit_index = 0; v->search_done = 0;
    if (v->doc && v->query[0]) ex_pdf_request_search(v->doc, v->query, v->match_case);
    else if (v->doc) { ex_pdf_cancel_search(v->doc); v->search_done = 1; }
}

void ex_pdfview_search(ExWidget *w, const char *utf8) {
    View *v = view_of(w);
    if (!v) return;
    v->search_open = 1;
    snprintf(v->query, sizeof v->query, "%s", utf8);
    start_search(v);
}

static int hit_sorted_first_at_or_after(View *v, int page) {
    size_t i;
    for (i = 0; i < v->nhits; i++) if (v->hits[i].page >= page) return (int)i;
    return v->nhits ? 0 : -1;
}

int ex_pdfview_copy_selection(ExWidget *w) {
    View *v = view_of(w);
    if (!v || !v->doc || v->sel_page < 0 || v->sel_end <= v->sel_start) return 0;
    {
        PageState *p = &v->pg[v->sel_page];
        char *buf;
        size_t n;
        if (!p->chars) return 0;
        n = (size_t)(v->sel_end - v->sel_start) * 4 + 8;
        buf = malloc(n);
        if (!buf) return 0;
        utf8_of(p->chars, (size_t)v->sel_start, (size_t)v->sel_end > p->nchars ? p->nchars : (size_t)v->sel_end, buf, n);
        if (v->h.set_clipboard) v->h.set_clipboard(v->h.user, buf);
        free(buf);
    }
    return 1;
}

int ex_pdfview_copy_link(ExWidget *w) {
    View *v = view_of(w);
    char name[400], link[700];
    int page;
    if (!v || !v->doc) return 0;
    page = v->sel_page >= 0 ? v->sel_page : v->last_page_shown;
    if (v->h.link_name) v->h.link_name(v->h.user, v->path, name, sizeof name); else snprintf(name, sizeof name, "%s", v->path);
    if (v->sel_page >= 0 && v->sel_end > v->sel_start) snprintf(link, sizeof link, "[[%s#page=%d&selection=%d,%d]]", name, page + 1, v->sel_start, v->sel_end);
    else snprintf(link, sizeof link, "[[%s#page=%d]]", name, page + 1);
    if (v->h.set_clipboard) v->h.set_clipboard(v->h.user, link);
    if (v->h.message) v->h.message(v->h.user, "Link copied.");
    return 1;
}

int ex_pdf_parse_fragment(const char *frag, int *page, int *s, int *e) {
    const char *p = frag;
    int found = 0;
    *page = 0; *s = 0; *e = 0;
    if (!p) return 0;
    if (*p == '#') p++;
    while (*p) {
        if (!strncmp(p, "page=", 5)) { *page = atoi(p + 5) - 1; if (*page < 0) *page = 0; found = 1; }
        else if (!strncmp(p, "selection=", 10)) { int a = 0, b = 0; if (sscanf(p + 10, "%d,%d", &a, &b) == 2 && a >= 0 && b > a) { *s = a; *e = b; } }
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return found;
}

/* the character nearest to a window point on a page, or -1 */
static int char_at(const View *v, int page, const ExPdfInfo *in, float x, float y, float ox, int *inside) {
    const PageState *p = &v->pg[page];
    size_t i;
    int best = -1;
    float bd = 1e30f;
    *inside = 0;
    for (i = 0; i < p->nchars; i++) {
        float x0, y0, x1, y1, cx, cy, d;
        to_screen(v, page, in, p->boxes[i * 4], p->boxes[i * 4 + 3], &x0, &y0, ox);
        to_screen(v, page, in, p->boxes[i * 4 + 2], p->boxes[i * 4 + 1], &x1, &y1, ox);
        if (p->chars[i] == 0 || p->chars[i] == 10 || p->chars[i] == 13) continue;
        if (x >= x0 && x <= x1 && y >= y0 && y <= y1) { *inside = 1; return (int)i; }
        cx = (x0 + x1) / 2.0f; cy = (y0 + y1) / 2.0f;
        d = (cx - x) * (cx - x) + (cy - y) * (cy - y) * 4.0f;
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

int ex_pdfview_char_rect(const ExWidget *w, int page, int index, float *x, float *y, float *wd, float *ht) {
    const View *v = view_of(w);
    const ExPdfInfo *in;
    float x0, y0, x1, y1;
    if (!v || !v->doc || page < 0 || page >= v->npg || !v->top) return 0;
    in = ex_pdf_info(v->doc);
    if (!v->pg[page].chars || index < 0 || (size_t)index >= v->pg[page].nchars) return 0;
    to_screen(v, page, in, v->pg[page].boxes[index * 4], v->pg[page].boxes[index * 4 + 3], &x0, &y0, w->x);
    to_screen(v, page, in, v->pg[page].boxes[index * 4 + 2], v->pg[page].boxes[index * 4 + 1], &x1, &y1, w->x + (v->toc_open ? v->toc_w : 0.0f));
    *x = x0; *y = y0; *wd = x1 - x0; *ht = y1 - y0;
    return 1;
}
int ex_pdfview_link_rect(const ExWidget *w, int page, size_t index, float *x, float *y, float *wd, float *ht) {
    const View *v = view_of(w);
    const ExPdfInfo *in;
    const ExPdfLink *L;
    float x0, y0, x1, y1;
    if (!v || !v->doc || page < 0 || page >= v->npg || !v->top || index >= v->pg[page].nlinks) return 0;
    in = ex_pdf_info(v->doc);
    L = &v->pg[page].links[index];
    to_screen(v, page, in, L->l, L->t, &x0, &y0, w->x + (v->toc_open ? v->toc_w : 0.0f));
    to_screen(v, page, in, L->r, L->b, &x1, &y1, w->x + (v->toc_open ? v->toc_w : 0.0f));
    *x = x0; *y = y0; *wd = x1 - x0; *ht = y1 - y0;
    return 1;
}

void ex_pdfview_stats(const ExWidget *w, ExPdfViewStats *out) {
    const View *v = view_of(w);
    int i, k = 0;
    memset(out, 0, sizeof *out);
    if (!v) return;
    out->loading = v->loading; out->failed = v->failed; out->has_doc = v->doc != NULL; out->pages = v->npg; out->current_page = v->last_page_shown;
    out->scale = v->scale; out->mode = v->mode; out->toc_open = v->toc_open; out->search_open = v->search_open; out->hits = v->nhits; out->hit_index = v->hit_index;
    out->sel_page = v->sel_page; out->sel_start = v->sel_start; out->sel_end = v->sel_end; out->uploads = v->uploads; out->scroll = v->scroll; out->own_ms = v->own_ms;
    for (i = 0; i < v->npg; i++) if (v->pg[i].tex) k++;
    out->textures = (size_t)k;
    for (i = 0; i < v->nbtn; i++) {
        float cx = v->btn[i].x + v->btn[i].w / 2.0f, cy = v->btn[i].y + v->btn[i].h / 2.0f;
        switch (v->btn[i].id) {
        case B_TOC: out->toc_x = cx; out->toc_y = cy; break;
        case B_FITW: out->fitw_x = cx; out->fitw_y = cy; break;
        case B_FITP: out->fitp_x = cx; out->fitp_y = cy; break;
        case B_ZIN: out->zin_x = cx; out->zin_y = cy; break;
        case B_SEARCH: out->search_x = cx; out->search_y = cy; break;
        default: break;
        }
    }
    if (v->doc && v->top) {
        const ExPdfInfo *in = ex_pdf_info(v->doc);
        float x, y;
        page_origin(v, 0, in, w->x + (v->toc_open ? v->toc_w : 0.0f), &x, &y);
        out->page0_x = x; out->page0_y = y; out->page0_w = (float)in->w[0] * v->scale; out->page0_h = (float)in->h[0] * v->scale;
    }
}

/* ---- drawing ---- */

static void button(View *v, ExRenderer *r, ExFont *f, float size, float sc, float x, float y, float h, const char *label, int id, int on, ExColor c_text, ExColor c_surface, ExColor c_border, ExColor c_accent, float *wout) {
    float bw = ex_text_measure(r, f, size, label, strlen(label)) + 16.0f * sc;
    ExFontMetrics fm;
    ex_text_metrics(r, f, size, &fm);
    ex_fill_round_rect(r, x, y, bw, h, 5.0f * sc, on ? c_accent : c_surface);
    ex_stroke_round_rect(r, x, y, bw, h, 5.0f * sc, 1.0f * sc, c_border);
    ex_text_draw(r, f, size, x + 8.0f * sc, y + (h - (float)fm.line_height) / 2.0f, label, strlen(label), on ? ex_theme_color(ex_ui_theme(v->ui), EX_COLOR_BACKGROUND) : c_text);
    if (v->nbtn < 10) { v->btn[v->nbtn].x = x; v->btn[v->nbtn].y = y; v->btn[v->nbtn].w = bw; v->btn[v->nbtn].h = h; v->btn[v->nbtn].id = id; v->nbtn++; }
    *wout = bw;
}

void ex_pdfview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    ExRenderer *r;
    const ExTheme *t;
    ExFont *f;
    ExFontMetrics fm;
    ExColor c_bg, c_text, c_muted, c_surface, c_border, c_accent;
    float sc, size, x, ox;
    double t0;
    char line[400];
    const ExPdfInfo *in;
    int i, first, last;
    if (!v) return;
    t0 = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
    r = ex_ui_renderer(ui); t = ex_ui_theme(ui);
    sc = ex_renderer_scale(r); size = ex_ui_base_size(ui);
    c_bg = ex_theme_color(t, EX_COLOR_BACKGROUND); c_text = ex_theme_color(t, EX_COLOR_TEXT); c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    c_surface = ex_theme_color(t, EX_COLOR_SURFACE); c_border = ex_theme_color(t, EX_COLOR_BORDER); c_accent = ex_theme_color(t, EX_COLOR_ACCENT);
    f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
    ex_text_metrics(r, f, size, &fm);
    ex_fill_rect(r, w->x, w->y, w->w, w->h, c_bg);
    v->frame++;
    if (!v->doc && !v->loading && !v->failed) ask_load(v);
    if (!v->doc) {
        if (v->failed) {
            float y = w->y + 24.0f * sc;
            char fit[700];
            ex_text_fit(r, f, size, v->err, strlen(v->err), w->w - 40.0f * sc, fit, sizeof fit);
            ex_text_draw(r, f, size, w->x + 20.0f * sc, y, fit, strlen(fit), ex_theme_color(t, EX_COLOR_ERROR));
            if (v->h.open_external) {
                const char *label = "Open externally";
                float bw = ex_text_measure(r, f, size, label, strlen(label)) + 24.0f * sc, bh = (float)fm.line_height + 14.0f * sc;
                v->nbtn = 0;
                v->btn[0].x = w->x + 20.0f * sc; v->btn[0].y = y + (float)fm.line_height + 16.0f * sc; v->btn[0].w = bw; v->btn[0].h = bh; v->btn[0].id = 99; v->nbtn = 1;
                ex_fill_round_rect(r, v->btn[0].x, v->btn[0].y, bw, bh, 6.0f * sc, c_surface);
                ex_stroke_round_rect(r, v->btn[0].x, v->btn[0].y, bw, bh, 6.0f * sc, 1.0f * sc, c_border);
                ex_text_draw(r, f, size, v->btn[0].x + 12.0f * sc, v->btn[0].y + 7.0f * sc, label, strlen(label), c_text);
            }
        } else ex_text_draw(r, f, size, w->x + 20.0f * sc, w->y + 24.0f * sc, "Loading...", 10, c_muted);
        v->own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
        return;
    }
    in = ex_pdf_info(v->doc);
    pump_results(v, r);
    v->tb_h = (float)fm.line_height + 16.0f * sc;
    v->toc_w = 240.0f * sc < w->w * 0.6f ? 240.0f * sc : w->w * 0.6f;
    ox = w->x + (v->toc_open ? v->toc_w : 0.0f);
    v->view_w = w->w - (v->toc_open ? v->toc_w : 0.0f);
    v->area_y = w->y + v->tb_h + (v->search_open ? v->tb_h : 0.0f);
    v->view_h = w->y + w->h - v->area_y;
    compute_scale(v, in, sc);
    compute_tops(v, in, sc);
    if (v->goto_sel_pending && v->want_page >= 0 && v->top) { scroll_to_page(v, v->want_page); v->goto_sel_pending = 0; }
    v->scroll = clampf(v->scroll, 0.0f, fmaxf(0.0f, v->content_h - v->view_h));
    first = page_at(v, in->pages, (double)v->scroll - 20.0);
    last = page_at(v, in->pages, (double)v->scroll + (double)v->view_h);
    v->last_page_shown = page_at(v, in->pages, (double)v->scroll + (double)v->view_h * 0.35);
    apply_wanted(v);
    ex_ui_clip_to(ui, ox, v->area_y, v->view_w, v->view_h);
    for (i = first; i <= last && i < in->pages; i++) {
        PageState *p = &v->pg[i];
        float px, py, pw = (float)in->w[i] * v->scale, ph = (float)in->h[i] * v->scale;
        int dw = (int)(pw + 0.5f), dh = (int)(ph + 0.5f);
        page_origin(v, i, in, ox, &px, &py);
        p->used = v->frame;
        ex_fill_rect(r, px + 3.0f * sc, py + 3.0f * sc, pw, ph, (ExColor){ c_border.r, c_border.g, c_border.b, 90 });
        ex_fill_rect(r, px, py, pw, ph, (ExColor){ 255, 255, 255, 255 });
        if (p->tex && fabsf(p->rscale - v->scale) / v->scale < 0.15f) ex_texture_draw(r, p->tex, 0, 0, 0, 0, px, py, pw, ph, 255, 1);
        else if (p->tex) ex_texture_draw(r, p->tex, 0, 0, 0, 0, px, py, pw, ph, 255, 1);            /* a texture of another scale: shown stretched until the new one is back */
        if ((!p->tex || fabsf(p->rscale - v->scale) / v->scale >= 0.12f) && (p->req_w != dw || p->req_h != dh) && dw > 0 && dh > 0 && dw <= 8000 && dh <= 8000) { ex_pdf_request_render(v->doc, i, dw, dh); p->req_w = dw; p->req_h = dh; }
        if (!p->links && !p->links_req) { p->links_req = 1; ex_pdf_request_links(v->doc, i); }
        if ((v->sel_page == i || v->search_open) && !p->chars) ensure_text(v, i);
        {   /* the search hits and the selection, over the page */
            size_t k;
            if (p->chars && v->search_open && v->nhits) {
                for (k = 0; k < v->nhits; k++) {
                    int is_cur = k == v->hit_index;
                    int c;
                    if (v->hits[k].page != i) continue;
                    if (!v->highlight_all && !is_cur) continue;
                    for (c = v->hits[k].start; c < v->hits[k].start + v->hits[k].count && (size_t)c < p->nchars; c++) {
                        float x0, y0, x1, y1;
                        to_screen(v, i, in, p->boxes[c * 4], p->boxes[c * 4 + 3], &x0, &y0, ox);
                        to_screen(v, i, in, p->boxes[c * 4 + 2], p->boxes[c * 4 + 1], &x1, &y1, ox);
                        ex_fill_rect(r, x0, y0, x1 - x0, y1 - y0, (ExColor){ c_accent.r, c_accent.g, c_accent.b, is_cur ? 150 : 80 });
                    }
                }
            }
            if (p->chars && v->sel_page == i && v->sel_end > v->sel_start) {
                int c;
                for (c = v->sel_start; c < v->sel_end && (size_t)c < p->nchars; c++) {
                    float x0, y0, x1, y1;
                    if (p->chars[c] == 10 || p->chars[c] == 13) continue;
                    to_screen(v, i, in, p->boxes[c * 4], p->boxes[c * 4 + 3], &x0, &y0, ox);
                    to_screen(v, i, in, p->boxes[c * 4 + 2], p->boxes[c * 4 + 1], &x1, &y1, ox);
                    ex_fill_rect(r, x0, y0, x1 - x0, y1 - y0, (ExColor){ 60, 120, 220, 110 });
                }
            }
        }
    }
    ex_ui_clip_restore(ui);
    {   /* the pages far from the window give back their textures and text */
        for (i = 0; i < v->npg; i++) {
            PageState *p = &v->pg[i];
            if (i >= first - KEEP_PAGES && i <= last + KEEP_PAGES) continue;
            if (p->tex || p->chars || p->links) { int keep_sel = i == v->sel_page; ex_texture_free(p->tex); p->tex = NULL; p->req_w = p->req_h = 0; p->links_req = 0; free(p->links); p->links = NULL; p->nlinks = 0; if (!keep_sel) { free(p->chars); free(p->boxes); p->chars = NULL; p->boxes = NULL; p->nchars = 0; p->text_req = 0; } }
        }
    }
    /* the table of contents */
    if (v->toc_open) {
        float y = v->area_y - v->toc_scroll, rowh = (float)fm.line_height + 10.0f * sc;
        size_t k;
        ex_fill_rect(r, w->x, v->area_y, v->toc_w, v->view_h, c_surface);
        ex_fill_rect(r, w->x + v->toc_w - 1.0f, v->area_y, 1.0f, v->view_h, c_border);
        ex_ui_clip_to(ui, w->x, v->area_y, v->toc_w - 1.0f, v->view_h);
        if (!in->ntoc) ex_text_draw(r, f, size, w->x + 12.0f * sc, v->area_y + 10.0f * sc, "This PDF has no contents.", 25, c_muted);
        for (k = 0; k < in->ntoc; k++) {
            char fit[300];
            float indent = (float)in->toc[k].depth * 14.0f * sc;
            if (y + rowh >= v->area_y && y <= v->area_y + v->view_h) {
                ex_text_fit(r, f, size, in->toc[k].title, strlen(in->toc[k].title), v->toc_w - 24.0f * sc - indent, fit, sizeof fit);
                ex_text_draw(r, f, size, w->x + 12.0f * sc + indent, y + 5.0f * sc, fit, strlen(fit), in->toc[k].page == v->last_page_shown ? c_accent : c_text);
            }
            y += rowh;
        }
        ex_ui_clip_restore(ui);
    }
    /* the toolbar */
    v->nbtn = 0;
    ex_fill_rect(r, w->x, w->y, w->w, v->tb_h, c_surface);
    ex_fill_rect(r, w->x, w->y + v->tb_h - 1.0f, w->w, 1.0f, c_border);
    {
        float bh = v->tb_h - 10.0f * sc, bw, y = w->y + 5.0f * sc;
        x = w->x + 8.0f * sc;
        button(v, r, f, size, sc, x, y, bh, "Contents", B_TOC, v->toc_open, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y, bh, "Fit width", B_FITW, v->mode == 0, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y, bh, "Fit page", B_FITP, v->mode == 1, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y, bh, "-", B_ZOUT, 0, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y, bh, "+", B_ZIN, 0, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y, bh, "Search", B_SEARCH, v->search_open, c_text, c_bg, c_border, c_accent, &bw); x += bw + 12.0f * sc;
        snprintf(line, sizeof line, "%d / %d   %d%%", v->last_page_shown + 1, in->pages, (int)(v->scale / sc * 100.0f * (96.0f / 72.0f) + 0.5f));
        ex_text_draw(r, f, size, x, w->y + (v->tb_h - (float)fm.line_height) / 2.0f, line, strlen(line), c_muted);
    }
    if (v->search_open) {
        float y = w->y + v->tb_h, bh = v->tb_h - 10.0f * sc, bw;
        char fit[300];
        ex_fill_rect(r, w->x, y, w->w, v->tb_h, c_surface);
        ex_fill_rect(r, w->x, y + v->tb_h - 1.0f, w->w, 1.0f, c_border);
        ex_fill_round_rect(r, w->x + 8.0f * sc, y + 5.0f * sc, 260.0f * sc, bh, 5.0f * sc, c_bg);
        snprintf(line, sizeof line, "%s", v->query[0] ? v->query : "Find in this PDF");
        ex_text_fit(r, f, size, line, strlen(line), 244.0f * sc, fit, sizeof fit);
        ex_text_draw(r, f, size, w->x + 16.0f * sc, y + (v->tb_h - (float)fm.line_height) / 2.0f, fit, strlen(fit), v->query[0] ? c_text : c_muted);
        x = w->x + 276.0f * sc;
        button(v, r, f, size, sc, x, y + 5.0f * sc, bh, "Aa", B_CASE, v->match_case, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y + 5.0f * sc, bh, "All", B_ALL, v->highlight_all, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y + 5.0f * sc, bh, "<", B_PREV, 0, c_text, c_bg, c_border, c_accent, &bw); x += bw + 6.0f * sc;
        button(v, r, f, size, sc, x, y + 5.0f * sc, bh, ">", B_NEXT, 0, c_text, c_bg, c_border, c_accent, &bw); x += bw + 12.0f * sc;
        snprintf(line, sizeof line, v->nhits ? "%zu of %zu%s" : (v->search_done && v->query[0] ? "No matches" : (v->query[0] ? "Searching..." : "")), v->nhits ? v->hit_index + 1 : 0, v->nhits, v->search_done ? "" : "+");
        ex_text_draw(r, f, size, x, y + (v->tb_h - (float)fm.line_height) / 2.0f, line, strlen(line), c_muted);
    }
    v->own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
}

/* ---- input ---- */

static void set_scale_custom(View *v, float s) { v->mode = 2; v->zoom = clampf(s, 0.05f, 8.0f); }

static void zoom_by(View *v, ExWidget *w, float factor, float px, float py) {
    const ExPdfInfo *in = v->doc ? ex_pdf_info(v->doc) : NULL;
    double ydoc;
    float old = v->scale;
    if (!in || !v->top) return;
    ydoc = ((double)v->scroll + (double)(py - v->area_y)) / (double)old;
    set_scale_custom(v, old * factor);
    v->top_for_pages = 0;                                                    /* the tops are made again at the new scale on the next frame */
    v->scroll = (float)(ydoc * (double)v->zoom - (double)(py - v->area_y));
    if (v->scroll < 0) v->scroll = 0;
    (void)w; (void)px;
}

void ex_pdfview_wheel(ExWidget *w, float dy, int ctrl, float x, float y) {
    View *v = view_of(w);
    if (!v || !v->doc) return;
    if (v->toc_open && x < w->x + v->toc_w) { v->toc_scroll -= dy * 60.0f; if (v->toc_scroll < 0) v->toc_scroll = 0; return; }
    if (ctrl) zoom_by(v, w, powf(1.15f, dy), x, y);
    else { v->scroll -= dy * 80.0f * ex_renderer_scale(ex_ui_renderer(v->ui)); if (v->scroll < 0) v->scroll = 0; }
}

static void do_button(View *v, ExWidget *w, int id) {
    switch (id) {
    case B_TOC: v->toc_open = !v->toc_open; break;
    case B_FITW: v->mode = 0; break;
    case B_FITP: v->mode = 1; break;
    case B_ZOUT: zoom_by(v, w, 0.8f, w->x + w->w / 2.0f, v->area_y + v->view_h / 2.0f); break;
    case B_ZIN: zoom_by(v, w, 1.25f, w->x + w->w / 2.0f, v->area_y + v->view_h / 2.0f); break;
    case B_SEARCH: v->search_open = !v->search_open; if (!v->search_open && v->doc) ex_pdf_cancel_search(v->doc); break;
    case B_CASE: v->match_case = !v->match_case; start_search(v); break;
    case B_ALL: v->highlight_all = !v->highlight_all; break;
    case B_PREV: if (v->nhits) goto_hit(v, v->hit_index ? v->hit_index - 1 : v->nhits - 1); break;
    case B_NEXT: if (v->nhits) goto_hit(v, (v->hit_index + 1) % v->nhits); break;
    default: break;
    }
}

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    const ExPdfInfo *in = v->doc ? ex_pdf_info(v->doc) : NULL;
    float ox = w->x + (v->toc_open ? v->toc_w : 0.0f);
    int i;
    if (kind == EX_MOUSE_DOWN) {
        if (under != w) return 0;
        ex_ui_set_focus(v->ui, w);
        for (i = 0; i < v->nbtn; i++)
            if (x >= v->btn[i].x && x <= v->btn[i].x + v->btn[i].w && y >= v->btn[i].y && y <= v->btn[i].y + v->btn[i].h) {
                if (v->btn[i].id == 99) { if (v->h.open_external) v->h.open_external(v->h.user, v->path); return 1; }
                if (v->doc) do_button(v, w, v->btn[i].id);
                return 1;
            }
        if (!in || !v->top) return 1;
        if (v->toc_open && x < w->x + v->toc_w) {                                                         /* the outline */
            ExFontMetrics fm;
            float rowh, ry = y - (v->area_y - v->toc_scroll);
            ex_text_metrics(ex_ui_renderer(v->ui), ex_ui_font(v->ui, EX_FONT_TOKEN_INTERFACE, 0), ex_ui_base_size(v->ui), &fm);
            rowh = (float)fm.line_height + 10.0f * ex_renderer_scale(ex_ui_renderer(v->ui));
            if (ry >= 0 && (size_t)(ry / rowh) < in->ntoc) { int pg = in->toc[(size_t)(ry / rowh)].page; if (pg >= 0) scroll_to_page(v, pg < in->pages ? pg : in->pages - 1); }
            return 1;
        }
        if (y < v->area_y) return 1;
        {   /* a click on a page: a link, or the start of a selection */
            int pg = page_at(v, in->pages, (double)(v->scroll + (y - v->area_y)));
            float px, py;
            size_t k;
            page_origin(v, pg, in, ox, &px, &py);
            if (x >= px && x <= px + (float)in->w[pg] * v->scale && y >= py && y <= py + (float)in->h[pg] * v->scale) {
                for (k = 0; k < v->pg[pg].nlinks; k++) {
                    float lx, ly, lw, lh;
                    if (!ex_pdfview_link_rect(w, pg, k, &lx, &ly, &lw, &lh)) continue;
                    if (x >= lx && x <= lx + lw && y >= ly && y <= ly + lh) {
                        const ExPdfLink *L = &v->pg[pg].links[k];
                        if (L->uri[0]) { if (v->h.open_url) v->h.open_url(v->h.user, L->uri); }          /* the application asks the user first */
                        else if (L->page >= 0 && L->page < in->pages) scroll_to_page(v, L->page);
                        return 1;
                    }
                }
                ensure_text(v, pg);
                if (v->pg[pg].chars) {
                    int inside = 0, c = char_at(v, pg, in, x, y, ox, &inside);
                    if (c >= 0 && inside) { v->sel_page = pg; v->sel_anchor = c; v->sel_start = c; v->sel_end = c + 1; v->selecting = 1; return 1; }
                }
            }
            v->sel_page = -1; v->sel_start = v->sel_end = 0;
        }
        v->press_x = x; v->press_y = y;
        return 1;
    }
    if (kind == EX_MOUSE_DRAG && v->selecting && in && v->sel_page >= 0) {
        int inside = 0, c = char_at(v, v->sel_page, in, x, y, ox, &inside);
        if (c >= 0) { if (c >= v->sel_anchor) { v->sel_start = v->sel_anchor; v->sel_end = c + 1; } else { v->sel_start = c; v->sel_end = v->sel_anchor + 1; } }
        return 1;
    }
    if (kind == EX_MOUSE_UP) { v->selecting = 0; return 1; }
    return 0;
}

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    float line = 60.0f * ex_renderer_scale(ex_ui_renderer(v->ui)), page = v->view_h * 0.9f;
    if (!v->doc) return 0;
    if (mods & EX_MOD_PRIMARY) {
        if (key == EX_KEY_C) { if (mods & EX_MOD_SHIFT) ex_pdfview_copy_link(w); else ex_pdfview_copy_selection(w); return 1; }
        if (key == EX_KEY_F) { v->search_open = 1; return 1; }
        return 0;
    }
    if (v->search_open) {
        if (key == EX_KEY_ESCAPE) { v->search_open = 0; ex_pdf_cancel_search(v->doc); return 1; }
        if (key == EX_KEY_ENTER) {
            if (v->nhits) { if (mods & EX_MOD_SHIFT) goto_hit(v, v->hit_index ? v->hit_index - 1 : v->nhits - 1); else goto_hit(v, (v->hit_index + 1) % v->nhits); }
            return 1;
        }
        if (key == EX_KEY_BACKSPACE) {
            size_t n = strlen(v->query);
            while (n > 0 && ((unsigned char)v->query[n - 1] & 0xC0) == 0x80) n--;
            if (n > 0) v->query[n - 1] = 0;
            start_search(v);
            return 1;
        }
    }
    switch (key) {
    case EX_KEY_DOWN: v->scroll += line; return 1;
    case EX_KEY_UP: v->scroll -= line; if (v->scroll < 0) v->scroll = 0; return 1;
    case EX_KEY_PAGE_DOWN: v->scroll += page; return 1;
    case EX_KEY_PAGE_UP: v->scroll -= page; if (v->scroll < 0) v->scroll = 0; return 1;
    case EX_KEY_HOME: v->scroll = 0; return 1;
    case EX_KEY_END: v->scroll = 1e9f; return 1;
    case EX_KEY_ESCAPE: v->sel_page = -1; v->sel_start = v->sel_end = 0; return 1;
    default: return 0;
    }
}

static int text_hook(ExWidget *w, const char *s, size_t len, void *user) {
    View *v = user;
    if (!v->doc) return 0;
    if (v->search_open) {
        size_t n = strlen(v->query);
        if (n + len + 1 < sizeof v->query) { memcpy(v->query + n, s, len); v->query[n + len] = 0; start_search(v); }
        return 1;
    }
    if (len == 1) {
        if (s[0] == '+' || s[0] == '=') { zoom_by(v, w, 1.25f, w->x + w->w / 2.0f, v->area_y + v->view_h / 2.0f); return 1; }
        if (s[0] == '-') { zoom_by(v, w, 0.8f, w->x + w->w / 2.0f, v->area_y + v->view_h / 2.0f); return 1; }
        if (s[0] == '0') { v->mode = 0; return 1; }
        if (s[0] == '/') { v->search_open = 1; return 1; }
    }
    return 0;
}

static void view_free(void *p) {
    View *v = p;
    int i;
    if (!v) return;
    for (i = 0; i < v->npg; i++) page_free(&v->pg[i]);
    free(v->pg); free(v->top); free(v->hits);
    if (v->doc) ex_pdf_close_async(v->doc);
    free(v->path);
    free(v);
}

ExWidget *ex_widget_pdfview(ExUi *ui, const char *path, const ExPdfViewHandlers *h) {
    ExWidget *w = ex_widget_box(ui, 0, path && *path ? path : "PDF");
    View *v;
    size_t n;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    n = strlen(path ? path : "");
    v->path = malloc(n + 1);
    if (v->path) memcpy(v->path, path ? path : "", n + 1);
    v->ui = ui; v->sel_page = -1; v->want_page = -1; v->highlight_all = 1; v->zoom = 1.0f;
    if (h) v->h = *h;
    w->type = EX_WIDGET_PDF;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1; w->clip = 1; w->vertical = 1;
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    w->on_text = text_hook; w->on_text_user = v;
    (void)hit_sorted_first_at_or_after;
    return w;
}
