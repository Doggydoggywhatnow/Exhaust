/* The Bases view: see include/exhaust_baseview.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_baseview.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"

enum { L_TABLE, L_LIST, L_CARDS, L_KANBAN, L_MAP, L_COUNT };
enum { HIT_NONE, HIT_VIEWS, HIT_RESULTS, HIT_SETTINGS, HIT_SOURCE, HIT_FILE, HIT_URL, HIT_GROUP, HIT_REFRESH };
#define MAX_HITS 4096

typedef struct Hit { float x, y, w, h; int kind; size_t a; char *s; } Hit;

typedef struct View {
    ExUi *ui;
    ExDoc *doc;
    char *path;
    ExBaseViewHandlers h;
    ExBase *base;
    char err[300];
    unsigned long seen;                  /* the document's version the base was read at */
    size_t view;                         /* the view on show */
    ExBaseResult *res;                   /* the result on show */
    unsigned long req, last_req;
    uint32_t last_run_ms;
    int stale, urgent, loading, failed;               /* stale: the vault changed (run again, at most about once a second); urgent: the base or the view changed (run at once) */
    char run_error[300];
    unsigned long runs, results, commits;
    float scroll_x, scroll_y;
    float content_h, content_w;
    char **collapsed; size_t ncollapsed;
    Hit hits[MAX_HITS]; size_t nhits;
    ExWidget *source;                    /* the editor of the YAML while "Edit source" is on */
    int dirty;
    size_t items_drawn;
    double own_ms;
    int layout;
    ExGeom *geom;
    float toolbar_h;
    char menu_file[600];
} View;

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_BASE ? (View *)w->owned : NULL; }
static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

void ex_baseresult_free_heap(ExBaseResult *r) { if (!r) return; ex_base_result_free(r); free(r); }

/* ---- the document ---- */

static int blank(const char *t, size_t n) { size_t i; for (i = 0; i < n; i++) if (t[i] != ' ' && t[i] != '\t' && t[i] != '\n' && t[i] != '\r') return 0; return 1; }

static void request_run(View *v) {
    char *text;
    size_t len = 0;
    if (!v->h.run || !v->base || !ex_base_view_count(v->base)) { v->loading = 0; return; }
    text = ex_buffer_text(ex_doc_buffer(v->doc), &len);
    if (!text) return;
    v->req = v->h.run(v->h.user, text, len, v->view, v->path);
    v->last_req = v->req;
    v->runs++;
    v->loading = 1; v->stale = 0; v->urgent = 0;
    v->last_run_ms = SDL_GetTicks();
    free(text);
}

static int layout_of(const View *v) {
    const char *t;
    if (!v->base || !ex_base_view_count(v->base)) return L_TABLE;
    t = ex_base_view_type(v->base, v->view);
    if (!strcmp(t, "list")) return L_LIST;
    if (!strcmp(t, "cards")) return L_CARDS;
    if (!strcmp(t, "kanban")) return L_KANBAN;
    if (!strcmp(t, "map")) return L_MAP;
    return L_TABLE;
}

static void reload(View *v) {
    size_t len = 0;
    char *text = ex_buffer_text(ex_doc_buffer(v->doc), &len);
    ExBase *b = NULL;
    v->err[0] = 0;
    if (text && blank(text, len)) b = ex_base_new();
    else if (text) b = ex_base_parse(text, len, v->err, sizeof v->err);
    free(text);
    ex_base_free(v->base);
    v->base = b;
    v->seen = ex_doc_version(v->doc);
    if (v->base && v->view >= ex_base_view_count(v->base)) v->view = 0;
    v->layout = layout_of(v);
    if (v->base) v->stale = v->urgent = 1;                       /* a new base or a changed one: run it again */
}

/* Writes the base into the document as one edit. */
static void commit(View *v) {
    size_t newlen = 0, oldlen = 0, p = 0, s = 0;
    char *neu, *old;
    ExChange ch;
    if (!v->base || !v->dirty) return;
    v->dirty = 0;
    neu = ex_base_write(v->base, &newlen);
    old = ex_buffer_text(ex_doc_buffer(v->doc), &oldlen);
    if (!neu || !old) { free(neu); free(old); return; }
    while (p < oldlen && p < newlen && old[p] == neu[p]) p++;
    while (s < oldlen - p && s < newlen - p && old[oldlen - 1 - s] == neu[newlen - 1 - s]) s++;
    while (p > 0 && ((unsigned char)old[p] & 0xC0) == 0x80) p--;
    while (s > 0 && oldlen - s < oldlen && ((unsigned char)old[oldlen - s] & 0xC0) == 0x80) s--;
    ch.offset = p; ch.remove_len = oldlen - p - s; ch.text = neu + p; ch.text_len = newlen - p - s;
    if (ch.remove_len || ch.text_len) { ex_doc_seal(v->doc); ex_doc_apply(v->doc, &ch, 1, 0, NULL, 0); ex_doc_seal(v->doc); v->commits++; }
    v->seen = ex_doc_version(v->doc);
    v->layout = layout_of(v);
    v->stale = v->urgent = 1;
    free(neu); free(old);
}

static ExYNode *cur_view(View *v) { return v->base ? (ExYNode *)(void *)(size_t)(const void *)ex_base_view(v->base, v->view) : NULL; }
static void set_key(View *v, const char *key, const char *value, int plain) {
    ExYNode *vn = cur_view(v);
    if (!vn) return;
    if (!value || !*value) ex_y_remove(vn, key);
    else ex_y_set(vn, key, plain ? ex_y_scalar_style(value, EXY_PLAIN) : ex_y_scalar(value));
    v->dirty = 1;
    commit(v);
}
static const char *get_key(View *v, const char *key, const char *dflt) {
    const ExYNode *vn = v->base ? ex_base_view(v->base, v->view) : NULL;
    const char *s = vn ? ex_y_str(ex_y_get(vn, key)) : NULL;
    return s && *s ? s : dflt;
}
static double get_num(View *v, const char *key, double dflt) {
    const char *s = get_key(v, key, NULL);
    char *end;
    double d;
    if (!s) return dflt;
    d = strtod(s, &end);
    return end != s && !*end && d > 0 ? d : dflt;
}

/* ---- results ---- */

void ex_baseview_set_result(ExWidget *w, unsigned long req, ExBaseResult *result, const char *error) {
    View *v = view_of(w);
    if (!v) { ex_baseresult_free_heap(result); return; }
    if (req != v->last_req) { ex_baseresult_free_heap(result); return; }              /* an answer to an earlier question */
    ex_baseresult_free_heap(v->res);
    v->res = result;
    v->loading = 0;
    v->failed = result == NULL;
    v->run_error[0] = 0;
    if (error) snprintf(v->run_error, sizeof v->run_error, "%s", error);
    v->results++;
    {   /* the scroll stays inside the new content */
        v->scroll_y = v->scroll_y < 0 ? 0 : v->scroll_y;
    }
}
int ex_baseview_owns_request(const ExWidget *w, unsigned long req) { const View *v = view_of(w); return v && req && v->last_req == req; }
void ex_baseview_invalidate(ExWidget *w) { View *v = view_of(w); if (v) v->stale = 1; }
size_t ex_baseview_current_view(const ExWidget *w) { const View *v = view_of(w); return v ? v->view : 0; }
void ex_baseview_set_view(ExWidget *w, size_t view) {
    View *v = view_of(w);
    if (!v || !v->base || view >= ex_base_view_count(v->base) || view == v->view) return;
    v->view = view; v->layout = layout_of(v);
    v->scroll_x = v->scroll_y = 0;
    { size_t i; for (i = 0; i < v->ncollapsed; i++) free(v->collapsed[i]); free(v->collapsed); v->collapsed = NULL; v->ncollapsed = 0; }
    ex_baseresult_free_heap(v->res); v->res = NULL;
    v->stale = v->urgent = 1;
}
const ExBase *ex_baseview_base(const ExWidget *w) { const View *v = view_of(w); return v ? v->base : NULL; }
const ExBaseResult *ex_baseview_result(const ExWidget *w) { const View *v = view_of(w); return v ? v->res : NULL; }
void ex_baseview_stats(const ExWidget *w, ExBaseViewStats *out) {
    const View *v = view_of(w);
    memset(out, 0, sizeof *out);
    out->layout = -1;
    if (!v) return;
    out->runs = v->runs; out->results = v->results; out->layout = v->base ? v->layout : -1;
    if (v->res) { out->rows = v->res->nrows; out->cols = v->res->ncols; out->groups = v->res->ngroups; }
    out->items_drawn = v->items_drawn; out->loading = v->loading; out->failed = v->failed || v->err[0]; out->source = v->source != NULL;
    out->scroll_x = v->scroll_x; out->scroll_y = v->scroll_y; out->own_ms = v->own_ms;
}
char *ex_baseview_copy_text(const ExWidget *w) { const View *v = view_of(w); return v && v->res ? ex_base_result_markdown(v->res, NULL) : NULL; }
char *ex_baseview_csv(const ExWidget *w, size_t *len) { const View *v = view_of(w); return v && v->res ? ex_base_result_csv(v->res, len) : NULL; }
int ex_baseview_editing_source(const ExWidget *w) { const View *v = view_of(w); return v && v->source != NULL; }

/* ---- small helpers ---- */

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static ExColor with_alpha(ExColor c, float a) { c.a = (uint8_t)((float)c.a * a + 0.5f); return c; }

static void add_hit(View *v, float x, float y, float w, float h, int kind, size_t a, const char *s) {
    Hit *t;
    if (v->nhits >= MAX_HITS) return;
    t = &v->hits[v->nhits++];
    t->x = x; t->y = y; t->w = w; t->h = h; t->kind = kind; t->a = a; t->s = s ? dups(s) : NULL;
}
static void clear_hits(View *v) { size_t i; for (i = 0; i < v->nhits; i++) free(v->hits[i].s); v->nhits = 0; }

static int is_collapsed(View *v, const char *label) { size_t i; for (i = 0; i < v->ncollapsed; i++) if (!strcmp(v->collapsed[i], label)) return 1; return 0; }
static void toggle_collapsed(View *v, const char *label) {
    size_t i;
    for (i = 0; i < v->ncollapsed; i++) if (!strcmp(v->collapsed[i], label)) { free(v->collapsed[i]); v->collapsed[i] = v->collapsed[--v->ncollapsed]; return; }
    { char **n = realloc(v->collapsed, (v->ncollapsed + 1) * sizeof *n); if (!n) return; v->collapsed = n; v->collapsed[v->ncollapsed++] = dups(label); }
}

/* what a cell shows: its text, and the file or address it opens (if it is a link, a file or a URL) */
static const char *cell_target(const ExValue *val, char *buf, size_t cap, int *is_url) {
    *is_url = 0;
    if (val->t == EXV_LINK) { snprintf(buf, cap, "%s", val->u.link.path); return buf; }
    if (val->t == EXV_FILE) { snprintf(buf, cap, "%s", val->u.file); return buf; }
    if (val->t == EXV_URL) { snprintf(buf, cap, "%s", val->u.url.url); *is_url = 1; return buf; }
    if (val->t == EXV_LIST && val->u.list.n >= 1) return cell_target(&val->u.list.v[0], buf, cap, is_url);
    return NULL;
}

/* what a cell opens: its own link, file or address, or (for the name of a file) the row's file */
static const char *target_of(const ExBaseResult *res, size_t row, size_t col, char *buf, size_t cap, int *is_url) {
    const char *t = cell_target(&res->cells[row * res->ncols + col], buf, cap, is_url);
    if (!t && res->paths && (!strcmp(res->cols[col].id, "file.name") || !strcmp(res->cols[col].id, "file.basename") || !strcmp(res->cols[col].id, "file.path"))) { *is_url = 0; return res->paths[row]; }
    return t;
}

static void draw_cell(View *v, ExRenderer *r, ExFont *f, float size, const ExBaseResult *res, size_t row, size_t col, float x, float y, float w, float h, ExColor c_text, ExColor c_link, ExColor c_muted, float sc) {
    const ExValue *val = &res->cells[row * res->ncols + col];
    const char *t = ex_base_cell_text(res, row, col);
    char fit[400], tgt[700];
    int url = 0;
    const char *target = target_of(res, row, col, tgt, sizeof tgt, &url);
    ExColor color = target ? c_link : c_text;
    if (val->t == EXV_BOOL) {                                                 /* a checkbox */
        float bs = size * sc * 0.9f;
        ex_stroke_round_rect(r, x, y + (h - bs) / 2.0f, bs, bs, 3.0f, 1.0f * sc, c_muted);
        if (val->u.num) ex_fill_round_rect(r, x + bs * 0.25f, y + (h - bs) / 2.0f + bs * 0.25f, bs * 0.5f, bs * 0.5f, 2.0f, c_link);
        return;
    }
    if (val->t == EXV_ERROR) color = ex_theme_color(ex_ui_theme(v->ui), EX_COLOR_ERROR);
    else if (!*t) return;
    ex_text_fit(r, f, size, t, strlen(t), w, fit, sizeof fit);
    {
        ExFontMetrics fm;
        float ty = y + (h > 0 ? 0 : 0);
        if (ex_text_metrics(r, f, size, &fm) == 0) ty = y + (h - (float)fm.line_height) / 2.0f;
        ex_text_draw(r, f, size, x, ty, fit, strlen(fit), color);
    }
    if (target) add_hit(v, x, y, w, h, url ? HIT_URL : HIT_FILE, 0, target);
}

/* ---- layouts ---- */

typedef struct Ctx { View *v; ExRenderer *r; const ExTheme *t; ExWidget *w; ExFont *f, *fb; float size, sc, x0, y0, bw, bh; ExColor c_text, c_muted, c_surface, c_border, c_accent, c_link, c_bg; const ExBaseResult *res; } Ctx;

static float row_height_for(View *v, float line_h, float sc) {
    const char *rh = get_key(v, "rowHeight", "short");
    float lines = !strcmp(rh, "medium") ? 2.0f : !strcmp(rh, "tall") ? 3.0f : (!strcmp(rh, "extra tall") || !strcmp(rh, "extra-tall") || !strcmp(rh, "extraTall")) ? 4.0f : 1.0f;
    return line_h * lines + 12.0f * sc;
}

static void draw_group_header(Ctx *c, float x, float y, float w, float h, const char *label, size_t count, int collapsed) {
    char text[500];
    ExFontMetrics fm;
    snprintf(text, sizeof text, "%s  (%zu)", label && *label ? label : "None", count);
    ex_fill_rect(c->r, x, y, w, h, c->c_surface);
    ex_icon_draw(c->r, collapsed ? EX_ICON_CHEVRON_RIGHT : EX_ICON_CHEVRON_DOWN, x + 6.0f * c->sc, y + (h - 14.0f * c->sc) / 2.0f, 14.0f * c->sc, 14.0f * c->sc, c->c_muted);
    if (ex_text_metrics(c->r, c->fb, c->size, &fm) == 0) ex_text_draw(c->r, c->fb, c->size, x + 26.0f * c->sc, y + (h - (float)fm.line_height) / 2.0f, text, strlen(text), c->c_text);
    add_hit(c->v, x, y, w, h, HIT_GROUP, 0, label ? label : "");
}

static void layout_table(Ctx *c) {
    View *v = c->v;
    const ExBaseResult *res = c->res;
    ExFontMetrics fm;
    float lh, rowh, headh, colw, total_w, y, content_h = 0;
    size_t i, j, g, nrows = res->nrows, shown = 0;
    int have_summary = 0;
    if (ex_text_metrics(c->r, c->f, c->size, &fm) != 0) return;
    lh = (float)fm.line_height;
    rowh = row_height_for(v, lh, c->sc); headh = lh + 14.0f * c->sc;
    colw = fmaxf(150.0f * c->sc, (c->bw - 2.0f) / (float)(res->ncols ? res->ncols : 1));
    if (res->ncols && !strcmp(res->cols[0].id, "file.name")) colw = fmaxf(colw, 200.0f * c->sc);
    total_w = colw * (float)res->ncols;
    for (j = 0; j < res->ncols; j++) if (res->cols[j].summary_name) have_summary = 1;
    v->content_w = total_w;
    /* the header stays at the top */
    {
        float hy = c->y0;
        float x = c->x0 - v->scroll_x;
        size_t k;
        /* rows first (under the header), then the header on top */
        y = c->y0 + headh - v->scroll_y;
        {
            size_t group = 0, row = 0;
            size_t ngroups = res->ngroups ? res->ngroups : 1;
            for (g = 0; g < ngroups; g++) {
                size_t first = res->ngroups ? res->groups[g].first : 0, count = res->ngroups ? res->groups[g].count : nrows;
                int collapsed = res->ngroups && is_collapsed(v, res->groups[g].label);
                if (res->ngroups) {
                    float gh = lh + 12.0f * c->sc;
                    if (y + gh >= c->y0 + headh && y <= c->y0 + c->bh) draw_group_header(c, c->x0, y, c->bw, gh, res->groups[g].label, count, collapsed);
                    y += gh; content_h += gh;
                }
                if (!collapsed) {
                    size_t skip = 0;
                    if (y + rowh * (float)count < c->y0 + headh) { y += rowh * (float)count; content_h += rowh * (float)count; shown += 0; continue; }       /* the whole group is above the view */
                    if (y < c->y0 + headh) skip = (size_t)((c->y0 + headh - y) / rowh);
                    if (skip > count) skip = count;
                    y += rowh * (float)skip;
                    for (row = first + skip; row < first + count; row++) {
                        if (y > c->y0 + c->bh) { y += rowh * (float)(first + count - row); content_h += rowh * (float)(first + count - row - skip); break; }
                        if (((row - first) & 1) == 0) ex_fill_rect(c->r, c->x0, y, c->bw, rowh, with_alpha(c->c_surface, 0.45f));
                        for (j = 0; j < res->ncols; j++) draw_cell(v, c->r, c->f, c->size, res, row, j, x + (float)j * colw + 8.0f * c->sc, y, colw - 16.0f * c->sc, rowh, c->c_text, c->c_link, c->c_muted, c->sc);
                        ex_fill_rect(c->r, c->x0, y + rowh - 1.0f, c->bw, 1.0f, with_alpha(c->c_border, 0.6f));
                        y += rowh; shown++;
                    }
                    content_h += rowh * (float)(count - skip);
                    content_h += rowh * (float)skip;
                    (void)group;
                }
            }
        }
        /* the total height: every row of every open group, counted without drawing */
        content_h = headh;
        for (g = 0; g < (res->ngroups ? res->ngroups : 1); g++) {
            size_t count = res->ngroups ? res->groups[g].count : nrows;
            if (res->ngroups) { content_h += lh + 12.0f * c->sc; if (is_collapsed(v, res->groups[g].label)) continue; }
            content_h += rowh * (float)count;
        }
        if (have_summary) content_h += rowh;
        /* the summary row after the last row, when it is in view */
        if (have_summary) {
            float sy = c->y0 + content_h - rowh - v->scroll_y;
            if (sy < c->y0 + c->bh && sy + rowh > c->y0 + headh) {
                ex_fill_rect(c->r, c->x0, sy, c->bw, rowh, c->c_surface);
                for (j = 0; j < res->ncols; j++) {
                    char t[300], val[200];
                    ExExprHost h;
                    if (!res->cols[j].summary_name) continue;
                    memset(&h, 0, sizeof h); h.tz_min = res->tz_min;
                    { ExArena *a = ex_arena_new(); char *s = a ? ex_value_to_string(a, &h, res->cols[j].summary) : NULL; snprintf(val, sizeof val, "%s", s ? s : ""); ex_arena_free(a); }
                    snprintf(t, sizeof t, "%s  %s", res->cols[j].summary_name, val);
                    { char fit[300]; ex_text_fit(c->r, c->fb, c->size, t, strlen(t), colw - 16.0f * c->sc, fit, sizeof fit); ex_text_draw(c->r, c->fb, c->size, x + (float)j * colw + 8.0f * c->sc, sy + (rowh - lh) / 2.0f, fit, strlen(fit), c->c_text); }
                }
            }
        }
        /* the header */
        ex_fill_rect(c->r, c->x0, hy, c->bw, headh, c->c_surface);
        for (k = 0; k < res->ncols; k++) {
            char fit[300];
            ex_text_fit(c->r, c->fb, c->size, res->cols[k].name, strlen(res->cols[k].name), colw - 16.0f * c->sc, fit, sizeof fit);
            ex_text_draw(c->r, c->fb, c->size, x + (float)k * colw + 8.0f * c->sc, hy + (headh - lh) / 2.0f, fit, strlen(fit), c->c_muted);
            if (k) ex_fill_rect(c->r, x + (float)k * colw, hy + 4.0f * c->sc, 1.0f, headh - 8.0f * c->sc, with_alpha(c->c_border, 0.8f));
        }
        ex_fill_rect(c->r, c->x0, hy + headh - 1.0f, c->bw, 1.0f, c->c_border);
    }
    v->content_h = content_h;
    v->items_drawn = shown;
    (void)i;
}

static void layout_list(Ctx *c) {
    View *v = c->v;
    const ExBaseResult *res = c->res;
    ExFontMetrics fm;
    float lh, y, content_h = 0, indent;
    size_t g, row, j, shown = 0, num = 0;
    const char *markers = get_key(v, "markers", "bullet"), *sep = get_key(v, "separator", ", ");
    int nested = !strcmp(get_key(v, "indentProperties", "false"), "true");
    if (ex_text_metrics(c->r, c->f, c->size, &fm) != 0) return;
    lh = (float)fm.line_height + 6.0f * c->sc;
    indent = 24.0f * c->sc;
    y = c->y0 + 8.0f * c->sc - v->scroll_y;
    for (g = 0; g < (res->ngroups ? res->ngroups : 1); g++) {
        size_t first = res->ngroups ? res->groups[g].first : 0, count = res->ngroups ? res->groups[g].count : res->nrows;
        int collapsed = res->ngroups && is_collapsed(v, res->groups[g].label);
        if (res->ngroups) {
            float gh = lh + 8.0f * c->sc;
            if (y + gh >= c->y0 && y <= c->y0 + c->bh) draw_group_header(c, c->x0, y, c->bw, gh, res->groups[g].label, count, collapsed);
            y += gh; content_h += gh;
        }
        if (collapsed) continue;
        {   /* every item is as tall as the next: the ones above the view and below it are skipped by arithmetic, not visited */
            float ih = lh * (nested ? (float)(res->ncols ? res->ncols : 1) : 1.0f), item_h = ih, y0 = y;
            size_t skip = y + ih * (float)count < c->y0 ? count : (y < c->y0 ? (size_t)((c->y0 - y) / ih) : 0), last;
            if (skip > count) skip = count;
            last = y0 + ih * (float)skip <= c->y0 + c->bh ? skip + (size_t)((c->y0 + c->bh - (y0 + ih * (float)skip)) / ih) + 2 : skip;
            if (last > count) last = count;
            y = y0 + ih * (float)skip;
            for (row = first + skip; row < first + last; row++) {
                num++;
            if (y + item_h >= c->y0 && y <= c->y0 + c->bh) {
                char mark[40], line[1400];
                size_t o = 0;
                float x = c->x0 + 12.0f * c->sc;
                if (!strcmp(markers, "number") || !strcmp(markers, "numbers")) snprintf(mark, sizeof mark, "%zu.", row - first + 1);
                else if (!strcmp(markers, "none")) mark[0] = 0;
                else snprintf(mark, sizeof mark, "\xe2\x80\xa2");
                if (mark[0]) ex_text_draw(c->r, c->f, c->size, x, y + 3.0f * c->sc, mark, strlen(mark), c->c_muted);
                x += mark[0] ? indent : 0.0f;
                if (!nested) {
                    float cx = x;
                    for (j = 0; j < res->ncols; j++) {
                        const char *t = ex_base_cell_text(res, row, j);
                        float tw;
                        char tgt[700], fit[300];
                        int url = 0;
                        const char *target = j == 0 ? target_of(res, row, j, tgt, sizeof tgt, &url) : NULL;
                        if (j && !*t) continue;
                        if (j) { ex_text_draw(c->r, c->f, c->size, cx, y + 3.0f * c->sc, sep, strlen(sep), c->c_muted); cx += ex_text_measure(c->r, c->f, c->size, sep, strlen(sep)); }
                        ex_text_fit(c->r, c->f, c->size, t, strlen(t), c->x0 + c->bw - cx - 8.0f * c->sc, fit, sizeof fit);
                        tw = ex_text_measure(c->r, c->f, c->size, fit, strlen(fit));
                        ex_text_draw(c->r, c->f, c->size, cx, y + 3.0f * c->sc, fit, strlen(fit), target ? c->c_link : (j ? c->c_muted : c->c_text));
                        if (target) add_hit(v, cx, y, tw, lh, url ? HIT_URL : HIT_FILE, 0, target);
                        cx += tw;
                    }
                    (void)line; (void)o;
                } else {
                    for (j = 0; j < res->ncols; j++) {
                        char tgt[700], fit[400], text[500];
                        int url = 0;
                        const char *t = ex_base_cell_text(res, row, j);
                        const char *target = j == 0 ? target_of(res, row, j, tgt, sizeof tgt, &url) : NULL;
                        float tx = x + (j ? indent : 0.0f);
                        if (j && !*t) continue;
                        snprintf(text, sizeof text, "%s%s%s", j ? res->cols[j].name : "", j ? ": " : "", t);
                        ex_text_fit(c->r, c->f, c->size, text, strlen(text), c->x0 + c->bw - tx - 8.0f * c->sc, fit, sizeof fit);
                        ex_text_draw(c->r, c->f, c->size, tx, y + (float)j * lh + 3.0f * c->sc, fit, strlen(fit), target ? c->c_link : (j ? c->c_muted : c->c_text));
                        if (target) add_hit(v, tx, y + (float)j * lh, ex_text_measure(c->r, c->f, c->size, fit, strlen(fit)), lh, url ? HIT_URL : HIT_FILE, 0, target);
                    }
                }
                shown++;
            }
            y += item_h;
            }
            y = y0 + ih * (float)count; content_h += ih * (float)count;
        }
    }
    v->content_h = content_h + 16.0f * c->sc;
    v->content_w = c->bw;
    v->items_drawn = shown;
    (void)num;
}

static int parse_hex(const char *s, ExColor *out) {
    unsigned int v;
    if (!s || s[0] != '#' || strlen(s) != 7 || sscanf(s + 1, "%x", &v) != 1) return 0;
    out->r = (uint8_t)(v >> 16); out->g = (uint8_t)(v >> 8); out->b = (uint8_t)v; out->a = 255;
    return 1;
}

/* one card: a title (the first column), the other columns as lines, an optional cover */
static void draw_card(Ctx *c, size_t row, float x, float y, float w, float h, int cover, float cover_h) {
    const ExBaseResult *res = c->res;
    View *v = c->v;
    ExFontMetrics fm;
    float ty = y + 10.0f * c->sc, lh;
    size_t j;
    ex_fill_round_rect(c->r, x, y, w, h, 6.0f * c->sc, c->c_surface);
    ex_stroke_round_rect(c->r, x, y, w, h, 6.0f * c->sc, 1.0f * c->sc, c->c_border);
    if (ex_text_metrics(c->r, c->f, c->size, &fm) != 0) return;
    lh = (float)fm.line_height + 3.0f * c->sc;
    if (cover) {
        const char *imgcol = get_key(v, "image", "");
        ExColor col;
        const char *val = "";
        size_t k;
        for (k = 0; k < res->ncols; k++) if (!strcmp(res->cols[k].id, imgcol)) val = ex_base_cell_text(res, row, k);
        if (parse_hex(val, &col)) ex_fill_rect(c->r, x + 1.0f, y + 1.0f, w - 2.0f, cover_h, col);
        else { ex_fill_rect(c->r, x + 1.0f, y + 1.0f, w - 2.0f, cover_h, with_alpha(c->c_border, 0.5f)); if (*val) { char fit[200]; ex_text_fit(c->r, c->f, c->size, val, strlen(val), w - 20.0f * c->sc, fit, sizeof fit); ex_text_draw(c->r, c->f, c->size, x + 10.0f * c->sc, y + cover_h / 2.0f - lh / 2.0f, fit, strlen(fit), c->c_muted); } }
        ty += cover_h;
    }
    ex_ui_clip_to(v->ui, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f);
    for (j = 0; j < res->ncols; j++) {
        const char *t = ex_base_cell_text(res, row, j);
        char text[500], fit[500], tgt[700];
        int url = 0;
        const char *target = j == 0 ? target_of(res, row, j, tgt, sizeof tgt, &url) : NULL;
        if (ty + lh > y + h) break;
        if (j && !*t) continue;
        if (j == 0) snprintf(text, sizeof text, "%s", t); else snprintf(text, sizeof text, "%s: %s", res->cols[j].name, t);
        ex_text_fit(c->r, j == 0 ? c->fb : c->f, c->size, text, strlen(text), w - 20.0f * c->sc, fit, sizeof fit);
        ex_text_draw(c->r, j == 0 ? c->fb : c->f, c->size, x + 10.0f * c->sc, ty, fit, strlen(fit), target ? c->c_link : (j ? c->c_muted : c->c_text));
        if (target) add_hit(v, x + 10.0f * c->sc, ty, w - 20.0f * c->sc, lh, url ? HIT_URL : HIT_FILE, 0, target);
        ty += lh;
    }
    ex_ui_clip_restore(v->ui);
}

static void layout_cards(Ctx *c) {
    View *v = c->v;
    const ExBaseResult *res = c->res;
    float cw = (float)get_num(v, "cardSize", 220.0) * c->sc, gap = 12.0f * c->sc, ch, y, content_h = 0, ratio = (float)get_num(v, "imageAspectRatio", 1.0), cover_h;
    int cover = get_key(v, "image", "")[0] != 0, per_row;
    size_t g, row, shown = 0;
    ExFontMetrics fm;
    if (ex_text_metrics(c->r, c->f, c->size, &fm) != 0) return;
    if (cw < 100.0f * c->sc) cw = 100.0f * c->sc;
    per_row = (int)((c->bw - gap) / (cw + gap));
    if (per_row < 1) { per_row = 1; cw = c->bw - 2.0f * gap; }
    cw = fminf(cw, (c->bw - gap * (float)(per_row + 1)) / (float)per_row + 0.0f) < 100.0f * c->sc ? cw : cw;
    cover_h = cw * clampf(ratio, 0.2f, 3.0f);
    ch = (float)(fm.line_height + 3.0f * c->sc) * (float)(res->ncols < 5 ? (res->ncols ? res->ncols : 1) : 5) + 20.0f * c->sc + (cover ? cover_h : 0.0f);
    y = c->y0 + gap - v->scroll_y;
    for (g = 0; g < (res->ngroups ? res->ngroups : 1); g++) {
        size_t first = res->ngroups ? res->groups[g].first : 0, count = res->ngroups ? res->groups[g].count : res->nrows;
        int collapsed = res->ngroups && is_collapsed(v, res->groups[g].label);
        if (res->ngroups) {
            float gh = (float)fm.line_height + 12.0f * c->sc;
            if (y + gh >= c->y0 && y <= c->y0 + c->bh) draw_group_header(c, c->x0, y, c->bw, gh, res->groups[g].label, count, collapsed);
            y += gh + gap; content_h += gh + gap;
        }
        if (collapsed) continue;
        {   /* only the lines of cards that reach into the view are visited */
            size_t line0 = y < c->y0 ? (size_t)((c->y0 - y) / (ch + gap)) : 0, line1 = y + (ch + gap) * (float)line0 > c->y0 + c->bh ? line0 : line0 + (size_t)((c->y0 + c->bh - y - (ch + gap) * (float)line0) / (ch + gap)) + 1;
            size_t from = line0 * (size_t)per_row, to = (line1 + 1) * (size_t)per_row;
            if (from > count) from = count;
            if (to > count) to = count;
            for (row = first + from; row < first + to; row++) {
                size_t idx = row - first;
                float x = c->x0 + gap + (float)(idx % (size_t)per_row) * (cw + gap), ry = y + (float)(idx / (size_t)per_row) * (ch + gap);
                if (ry + ch >= c->y0 && ry <= c->y0 + c->bh) { draw_card(c, row, x, ry, cw, ch, cover, cover_h); shown++; }
            }
        }
        { size_t rows_n = (count + (size_t)per_row - 1) / (size_t)per_row; y += (float)rows_n * (ch + gap); content_h += (float)rows_n * (ch + gap); }
    }
    v->content_h = content_h + gap; v->content_w = c->bw;
    v->items_drawn = shown;
}

static void layout_kanban(Ctx *c) {
    View *v = c->v;
    const ExBaseResult *res = c->res;
    float cw = (float)get_num(v, "columnWidth", 260.0) * c->sc, gap = 12.0f * c->sc, x, hh, ch, content_h = 0, ratio = (float)get_num(v, "imageAspectRatio", 1.0), cover_h;
    int cover = get_key(v, "image", "")[0] != 0;
    size_t g, shown = 0, ncols = res->ngroups ? res->ngroups : 1;
    ExFontMetrics fm;
    if (ex_text_metrics(c->r, c->f, c->size, &fm) != 0) return;
    hh = (float)fm.line_height + 16.0f * c->sc;
    cover_h = cw * clampf(ratio, 0.2f, 3.0f);
    ch = (float)(fm.line_height + 3.0f * c->sc) * (float)(res->ncols < 5 ? (res->ncols ? res->ncols : 1) : 5) + 20.0f * c->sc + (cover ? cover_h : 0.0f);
    x = c->x0 + gap - v->scroll_x;
    for (g = 0; g < ncols; g++) {
        size_t first = res->ngroups ? res->groups[g].first : 0, count = res->ngroups ? res->groups[g].count : res->nrows, row;
        float y = c->y0 + gap - v->scroll_y;
        float col_h = hh + (float)count * (ch + gap);
        if (x + cw >= c->x0 && x <= c->x0 + c->bw) {
            char title[400];
            ex_fill_round_rect(c->r, x, y, cw, hh, 6.0f * c->sc, c->c_surface);
            snprintf(title, sizeof title, "%s  %zu", res->ngroups ? (res->groups[g].label[0] ? res->groups[g].label : "None") : "All", count);
            ex_text_draw(c->r, c->fb, c->size, x + 10.0f * c->sc, y + 8.0f * c->sc, title, strlen(title), c->c_text);
            y += hh + gap;
            {   /* the cards above the view are skipped by arithmetic */
                size_t from = y < c->y0 ? (size_t)((c->y0 - y) / (ch + gap)) : 0, to;
                if (from > count) from = count;
                y += (ch + gap) * (float)from;
                to = y > c->y0 + c->bh ? from : from + (size_t)((c->y0 + c->bh - y) / (ch + gap)) + 2;
                if (to > count) to = count;
                for (row = first + from; row < first + to; row++) {
                    if (y + ch >= c->y0 && y <= c->y0 + c->bh) { draw_card(c, row, x, y, cw, ch, cover, cover_h); shown++; }
                    y += ch + gap;
                }
            }
        }
        if (col_h + gap > content_h) content_h = col_h + gap;
        x += cw + gap;
    }
    v->content_h = content_h + gap; v->content_w = (float)ncols * (cw + gap) + gap;
    v->items_drawn = shown;
}

/* the map without tiles: the points on a plain grid of longitude and latitude */
static int num_of(const ExValue *x, double *out) {
    char *end;
    if (x->t == EXV_NUM) { *out = x->u.num; return 1; }
    if (x->t == EXV_STR) { *out = strtod(x->u.str, &end); return end != x->u.str; }
    return 0;
}
static int coords_of(const ExValue *val, double *lat, double *lng) {
    if (val->t == EXV_LIST && val->u.list.n >= 2 && num_of(&val->u.list.v[0], lat) && num_of(&val->u.list.v[1], lng)) return 1;
    if (val->t == EXV_STR) { double a, b; if (sscanf(val->u.str, "%lf , %lf", &a, &b) == 2 || sscanf(val->u.str, "%lf %lf", &a, &b) == 2) { *lat = a; *lng = b; return 1; } }
    return 0;
}

static void layout_map(Ctx *c) {
    View *v = c->v;
    const ExBaseResult *res = c->res;
    const char *prop = get_key(v, "coordinates", "note.coordinates");
    size_t row, col = (size_t)-1, j, shown = 0, npts = 0;
    double lat0 = 1e9, lat1 = -1e9, lng0 = 1e9, lng1 = -1e9;
    float mx = 40.0f * c->sc, my = 40.0f * c->sc, pw, ph;
    ExFontMetrics fm;
    char note[300];
    for (j = 0; j < res->ncols; j++) if (!strcmp(res->cols[j].id, prop) || (!strncmp(prop, "note.", 5) && !strcmp(res->cols[j].id, prop + 5)) || (!strncmp(res->cols[j].id, "note.", 5) && !strcmp(res->cols[j].id + 5, prop))) col = j;
    for (row = 0; col != (size_t)-1 && row < res->nrows; row++) { double la, ln; if (coords_of(&res->cells[row * res->ncols + col], &la, &ln)) { npts++; if (la < lat0) lat0 = la; if (la > lat1) lat1 = la; if (ln < lng0) lng0 = ln; if (ln > lng1) lng1 = ln; } }
    snprintf(note, sizeof note, "Map tiles are off: the pins are drawn on a plain grid of longitude and latitude.");
    ex_text_metrics(c->r, c->f, c->size, &fm);
    { char fit[300]; ex_text_fit(c->r, c->f, c->size, note, strlen(note), c->bw - 24.0f * c->sc, fit, sizeof fit); ex_text_draw(c->r, c->f, c->size, c->x0 + 12.0f * c->sc, c->y0 + 8.0f * c->sc, fit, strlen(fit), c->c_muted); }
    if (col == (size_t)-1) {
        char msg[400];
        snprintf(msg, sizeof msg, "Add %s to the columns (order) of this view, or set `coordinates` to the property that holds [latitude, longitude].", prop);
        ex_text_draw(c->r, c->f, c->size, c->x0 + 12.0f * c->sc, c->y0 + 12.0f * c->sc + (float)fm.line_height * 2.0f, msg, strlen(msg), c->c_muted);
        v->content_h = 0; v->content_w = c->bw; v->items_drawn = 0;
        return;
    }
    pw = c->bw - 2.0f * mx; ph = c->bh - 2.0f * my - (float)fm.line_height;
    if (lng1 - lng0 < 1e-9) { lng0 -= 1; lng1 += 1; }
    if (lat1 - lat0 < 1e-9) { lat0 -= 1; lat1 += 1; }
    ex_stroke_round_rect(c->r, c->x0 + mx, c->y0 + my + (float)fm.line_height, pw, ph, 2.0f, 1.0f * c->sc, c->c_border);
    for (row = 0; row < res->nrows; row++) {
        double la, ln;
        float px, py;
        char fit[200];
        if (!coords_of(&res->cells[row * res->ncols + col], &la, &ln)) continue;
        px = c->x0 + mx + (float)((ln - lng0) / (lng1 - lng0)) * pw;
        py = c->y0 + my + (float)fm.line_height + (float)((lat1 - la) / (lat1 - lat0)) * ph;
        ex_fill_round_rect(c->r, px - 5.0f * c->sc, py - 5.0f * c->sc, 10.0f * c->sc, 10.0f * c->sc, 5.0f * c->sc, c->c_accent);
        { const char *t = ex_base_cell_text(res, row, 0); ex_text_fit(c->r, c->f, c->size, t, strlen(t), 200.0f * c->sc, fit, sizeof fit); ex_text_draw(c->r, c->f, c->size, px + 8.0f * c->sc, py - (float)fm.line_height / 2.0f, fit, strlen(fit), c->c_text); }
        { char tgt[700]; int url = 0; const char *target = target_of(res, row, 0, tgt, sizeof tgt, &url); if (target) add_hit(v, px - 6.0f * c->sc, py - 6.0f * c->sc, 12.0f * c->sc, 12.0f * c->sc, url ? HIT_URL : HIT_FILE, 0, target); }
        shown++;
    }
    v->content_h = 0; v->content_w = c->bw; v->items_drawn = shown;
    (void)npts;
}

/* ---- the toolbar and the whole draw ---- */

void ex_baseview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    ExRenderer *r;
    const ExTheme *t;
    Ctx c;
    double t0;
    float sc, tb_h, x;
    char label[400];
    if (!v) return;
    t0 = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
    t = ex_ui_theme(ui); r = ex_ui_renderer(ui);
    if (ex_doc_version(v->doc) != v->seen) reload(v);
    clear_hits(v);
    sc = ex_renderer_scale(r);
    memset(&c, 0, sizeof c);
    c.v = v; c.r = r; c.t = t; c.w = w; c.f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0); c.fb = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 1);
    c.size = ex_ui_base_size(ui); c.sc = sc;
    c.c_text = ex_theme_color(t, EX_COLOR_TEXT); c.c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED); c.c_surface = ex_theme_color(t, EX_COLOR_SURFACE);
    c.c_border = ex_theme_color(t, EX_COLOR_BORDER); c.c_accent = ex_theme_color(t, EX_COLOR_ACCENT); c.c_link = ex_theme_color(t, EX_COLOR_LINK); c.c_bg = ex_theme_color(t, EX_COLOR_BACKGROUND);
    ex_fill_rect(r, w->x, w->y, w->w, w->h, c.c_bg);
    {   /* a run is asked for when the base changed or the vault did (at most once a second) */
        uint32_t now = SDL_GetTicks();
        if (v->base && v->stale && !v->source && (v->urgent || !v->last_run_ms || now - v->last_run_ms >= 700)) request_run(v);
        else if (v->base && v->stale && v->source && now - v->last_run_ms >= 1500) request_run(v);
    }
    /* the toolbar */
    {
        ExFontMetrics fm;
        ex_text_metrics(r, c.f, c.size, &fm);
        tb_h = (float)fm.line_height + 18.0f * sc;
        v->toolbar_h = tb_h;
        ex_fill_rect(r, w->x, w->y, w->w, tb_h, c.c_surface);
        ex_fill_rect(r, w->x, w->y + tb_h - 1.0f, w->w, 1.0f, c.c_border);
        x = w->x + 10.0f * sc;
        {   /* the view: its name, a menu of the views */
            const char *nm = v->base && ex_base_view_count(v->base) ? ex_base_view_name(v->base, v->view) : "";
            float bw;
            snprintf(label, sizeof label, "%s", nm && *nm ? nm : (v->base && ex_base_view_count(v->base) ? ex_base_view_type(v->base, v->view) : "No view"));
            bw = ex_text_measure(r, c.fb, c.size, label, strlen(label)) + 34.0f * sc;
            ex_fill_round_rect(r, x, w->y + 6.0f * sc, bw, tb_h - 12.0f * sc, 5.0f * sc, with_alpha(c.c_border, 0.5f));
            ex_text_draw(r, c.fb, c.size, x + 8.0f * sc, w->y + (tb_h - (float)fm.line_height) / 2.0f, label, strlen(label), c.c_text);
            ex_icon_draw(r, EX_ICON_CHEVRON_DOWN, x + bw - 22.0f * sc, w->y + (tb_h - 14.0f * sc) / 2.0f, 14.0f * sc, 14.0f * sc, c.c_muted);
            add_hit(v, x, w->y, bw, tb_h, HIT_VIEWS, 0, NULL);
            x += bw + 8.0f * sc;
        }
        {   /* results: how many, and what to do with them */
            float bw;
            if (v->loading && !v->res) snprintf(label, sizeof label, "Loading...");
            else if (v->res) snprintf(label, sizeof label, "%zu result%s", v->res->nrows, v->res->nrows == 1 ? "" : "s");
            else snprintf(label, sizeof label, "Results");
            bw = ex_text_measure(r, c.f, c.size, label, strlen(label)) + 34.0f * sc;
            ex_text_draw(r, c.f, c.size, x + 8.0f * sc, w->y + (tb_h - (float)fm.line_height) / 2.0f, label, strlen(label), c.c_muted);
            ex_icon_draw(r, EX_ICON_CHEVRON_DOWN, x + bw - 22.0f * sc, w->y + (tb_h - 14.0f * sc) / 2.0f, 14.0f * sc, 14.0f * sc, c.c_muted);
            add_hit(v, x, w->y, bw, tb_h, HIT_RESULTS, 0, NULL);
            x += bw + 8.0f * sc;
        }
        {   /* view settings, and the source */
            float bw;
            snprintf(label, sizeof label, "Settings");
            bw = ex_text_measure(r, c.f, c.size, label, strlen(label)) + 34.0f * sc;
            ex_text_draw(r, c.f, c.size, x + 8.0f * sc, w->y + (tb_h - (float)fm.line_height) / 2.0f, label, strlen(label), c.c_muted);
            ex_icon_draw(r, EX_ICON_CHEVRON_DOWN, x + bw - 22.0f * sc, w->y + (tb_h - 14.0f * sc) / 2.0f, 14.0f * sc, 14.0f * sc, c.c_muted);
            add_hit(v, x, w->y, bw, tb_h, HIT_SETTINGS, 0, NULL);
            x += bw + 8.0f * sc;
            snprintf(label, sizeof label, "%s", v->source ? "Show the view" : "Edit source");
            bw = ex_text_measure(r, c.f, c.size, label, strlen(label)) + 16.0f * sc;
            ex_text_draw(r, c.f, c.size, x + 8.0f * sc, w->y + (tb_h - (float)fm.line_height) / 2.0f, label, strlen(label), v->source ? c.c_accent : c.c_muted);
            add_hit(v, x, w->y, bw, tb_h, HIT_SOURCE, 0, NULL);
        }
    }
    c.x0 = w->x; c.y0 = w->y + tb_h; c.bw = w->w; c.bh = w->h - tb_h;
    ex_ui_clip_to(ui, c.x0, c.y0, c.bw, c.bh);
    v->items_drawn = 0;
    if (v->source) { /* the editor draws itself */ }
    else if (!v->base) {
        char msg[500];
        snprintf(msg, sizeof msg, "This base cannot be read, so it is shown here and never changed: %s", v->err);
        ex_text_draw(r, c.f, c.size, c.x0 + 16.0f * sc, c.y0 + 16.0f * sc, msg, strlen(msg), ex_theme_color(t, EX_COLOR_ERROR));
    } else if (!ex_base_view_count(v->base)) {
        const char *msg = "This base has no views yet. Use the view menu to add one.";
        ex_text_draw(r, c.f, c.size, c.x0 + 16.0f * sc, c.y0 + 16.0f * sc, msg, strlen(msg), c.c_muted);
    } else if (!v->res) {
        const char *msg = v->failed ? (v->run_error[0] ? v->run_error : "This view could not be run.") : "Reading the files...";
        ex_text_draw(r, c.f, c.size, c.x0 + 16.0f * sc, c.y0 + 16.0f * sc, msg, strlen(msg), v->failed ? ex_theme_color(t, EX_COLOR_ERROR) : c.c_muted);
    } else {
        c.res = v->res;
        v->scroll_y = clampf(v->scroll_y, 0.0f, fmaxf(0.0f, v->content_h - c.bh));      /* the last frame's size: a wheel that went far past the end draws the end, not nothing */
        v->scroll_x = clampf(v->scroll_x, 0.0f, fmaxf(0.0f, v->content_w - c.bw));
        switch (v->layout) {
        case L_LIST: layout_list(&c); break;
        case L_CARDS: layout_cards(&c); break;
        case L_KANBAN: layout_kanban(&c); break;
        case L_MAP: layout_map(&c); break;
        default: layout_table(&c); break;
        }
        v->scroll_y = clampf(v->scroll_y, 0.0f, fmaxf(0.0f, v->content_h - c.bh));
        v->scroll_x = clampf(v->scroll_x, 0.0f, fmaxf(0.0f, v->content_w - c.bw));
        if (!v->res->nrows && v->layout != L_MAP) {
            const char *msg = "No files match this view.";
            ex_text_draw(r, c.f, c.size, c.x0 + 16.0f * sc, c.y0 + 56.0f * sc, msg, strlen(msg), c.c_muted);
        }
        if (v->res->nerrors) {                                                    /* what went wrong, at the bottom */
            size_t i;
            float ey = c.y0 + c.bh - (float)v->res->nerrors * ((float)ex_ui_base_size(ui) * sc + 6.0f * sc) - 6.0f * sc;
            for (i = 0; i < v->res->nerrors; i++) {
                char fit[300];
                ex_text_fit(r, c.f, c.size, v->res->errors[i], strlen(v->res->errors[i]), c.bw - 24.0f * sc, fit, sizeof fit);
                ex_text_draw(r, c.f, c.size, c.x0 + 12.0f * sc, ey + (float)i * (c.size * sc + 6.0f * sc), fit, strlen(fit), ex_theme_color(t, EX_COLOR_ERROR));
            }
        }
    }
    ex_ui_clip_restore(ui);
    if (v->source) { w->pad_y = tb_h / ((float)ex_theme_size(t, EX_SIZE_SPACING) * sc); }
    v->own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
}

/* ---- menus and actions ---- */

enum { M_IMG0 = 700, M_VIEW0 = 100, M_ADD_TABLE = 200, M_ADD_LIST, M_ADD_CARDS, M_ADD_KANBAN, M_ADD_MAP, M_RENAME, M_DELETE_VIEW, M_COPY = 300, M_CSV, M_LIMIT, M_REFRESH, M_OPEN, M_OPEN_TAB, M_SET0 = 400 };
typedef struct MenuCtx { View *v; ExWidget *w; } MenuCtx;
static MenuCtx g_menu;
static char g_menu_file[600];

static void copy_to_clipboard(View *v) {
    char *t = v->res ? ex_base_result_markdown(v->res, NULL) : NULL;
    if (!t) return;
    if (v->h.set_clipboard) v->h.set_clipboard(v->h.user, t); else ex_clipboard_set_text(t);
    if (v->h.message) v->h.message(v->h.user, "Copied the view as a Markdown table.");
    free(t);
}
static void export_csv(View *v) {
    size_t len = 0;
    char *csv = v->res ? ex_base_result_csv(v->res, &len) : NULL, name[400];
    const char *base = strrchr(v->path, '/');
    base = base ? base + 1 : v->path;
    if (!csv) return;
    snprintf(name, sizeof name, "%.*s - %s.csv", (int)(strlen(base) > 5 && !strcmp(base + strlen(base) - 5, ".base") ? strlen(base) - 5 : strlen(base)), base, v->base && ex_base_view_count(v->base) && *ex_base_view_name(v->base, v->view) ? ex_base_view_name(v->base, v->view) : "view");
    if (v->h.export_file) { if (v->h.export_file(v->h.user, name, csv, len) != 0 && v->h.message) v->h.message(v->h.user, "The CSV could not be written."); }
    free(csv);
}

static void limit_done(const char *text, void *arg) {
    View *v = ((MenuCtx *)arg)->v;
    char *end;
    long n;
    if (!text) return;
    if (!*text) { set_key(v, "limit", NULL, 1); return; }
    n = strtol(text, &end, 10);
    if (*end || n < 0) { if (v->h.message) v->h.message(v->h.user, "A limit is a whole number of results (empty for no limit)."); return; }
    set_key(v, "limit", text, 1);
}
static void name_done(const char *text, void *arg) { View *v = ((MenuCtx *)arg)->v; if (text && *text) set_key(v, "name", text, 0); }

static void add_view(View *v, const char *type, const char *name) {
    ExYNode *views, *nv;
    if (!v->base) return;
    views = ex_y_get(ex_base_root(v->base), "views");
    if (!views || views->kind != EXY_SEQ) { views = ex_y_seq(); if (!views || ex_y_set(ex_base_root(v->base), "views", views)) return; }
    nv = ex_y_map();
    if (!nv) return;
    ex_y_set(nv, "type", ex_y_scalar(type)); ex_y_set(nv, "name", ex_y_scalar(name));
    if (ex_y_push(views, nv) != 0) { ex_y_free(nv); return; }
    v->view = views->n - 1;
    v->dirty = 1;
    commit(v);
    v->layout = layout_of(v);
    ex_baseresult_free_heap(v->res); v->res = NULL;
}

static void delete_view(View *v) {
    ExYNode *views;
    size_t i;
    if (!v->base) return;
    views = ex_y_get(ex_base_root(v->base), "views");
    if (!views || views->kind != EXY_SEQ || v->view >= views->n) return;
    ex_y_free(views->kids[v->view]);
    for (i = v->view; i + 1 < views->n; i++) views->kids[i] = views->kids[i + 1];
    views->n--;
    if (v->view && v->view >= views->n) v->view = views->n ? views->n - 1 : 0;
    v->dirty = 1;
    commit(v);
    v->layout = layout_of(v);
    ex_baseresult_free_heap(v->res); v->res = NULL;
}

static const struct { const char *layout, *key, *label; const char *options; } SETTINGS[] = {
    { "table", "rowHeight", "Row height", "short|medium|tall|extra tall" },
    { "list", "markers", "Markers", "bullet|number|none" },
    { "list", "indentProperties", "Properties on their own lines", "false|true" },
    { "list", "separator", "Separator", ", |; | / | - " },
    { "cards", "cardSize", "Card size", "160|220|300|400" },
    { "cards", "imageFit", "Image fit", "cover|contain" },
    { "cards", "imageAspectRatio", "Image shape (height over width)", "0.5|0.75|1|1.5" },
    { "kanban", "columnWidth", "Column width", "200|260|340|440" },
    { "kanban", "imageFit", "Image fit", "cover|contain" },
    { "kanban", "imageAspectRatio", "Image shape (height over width)", "0.5|0.75|1|1.5" },
};
static const char *LAYOUT_NAMES[L_COUNT] = { "table", "list", "cards", "kanban", "map" };

static void menu_done(ExPanels *p, int id, void *arg) {
    MenuCtx *m = arg;
    View *v = m->v;
    (void)p;
    if (id >= M_VIEW0 && id < M_VIEW0 + 90) { ex_baseview_set_view(m->w, (size_t)(id - M_VIEW0)); return; }
    switch (id) {
    case M_ADD_TABLE: add_view(v, "table", "Table"); break;
    case M_ADD_LIST: add_view(v, "list", "List"); break;
    case M_ADD_CARDS: add_view(v, "cards", "Cards"); break;
    case M_ADD_KANBAN: add_view(v, "kanban", "Kanban"); break;
    case M_ADD_MAP: add_view(v, "map", "Map"); break;
    case M_RENAME: if (v->h.panels) ex_panels_ask_text(v->h.panels, "Rename the view", "Name", ex_base_view_name(v->base, v->view), name_done, m); break;
    case M_DELETE_VIEW: delete_view(v); break;
    case M_COPY: copy_to_clipboard(v); break;
    case M_CSV: export_csv(v); break;
    case M_LIMIT: if (v->h.panels) ex_panels_ask_text(v->h.panels, "Limit the results", "How many results at most (empty for all)", get_key(v, "limit", ""), limit_done, m); break;
    case M_REFRESH: v->stale = v->urgent = 1; break;
    case M_OPEN: if (g_menu_file[0] && v->h.open_file) v->h.open_file(v->h.user, g_menu_file, 0); break;
    case M_OPEN_TAB: if (g_menu_file[0] && v->h.open_file) v->h.open_file(v->h.user, g_menu_file, 1); break;
    default:
        if (id >= M_IMG0 && id < M_IMG0 + 100) {                                  /* the property of the cards' image */
            size_t k = (size_t)(id - M_IMG0);
            if (k == 0) set_key(v, "image", NULL, 0);
            else if (v->res && k - 1 < v->res->ncols) set_key(v, "image", v->res->cols[k - 1].id, 0);
            break;
        }
        if (id >= M_SET0 && id < M_SET0 + 200) {
            size_t k = (size_t)(id - M_SET0) / 10, o = (size_t)(id - M_SET0) % 10, n = 0, i;
            const char *opt = NULL;
            char buf[200];
            for (i = 0; i < sizeof SETTINGS / sizeof *SETTINGS; i++) {
                if (strcmp(SETTINGS[i].layout, LAYOUT_NAMES[v->layout])) continue;
                if (n == k) {
                    const char *s = SETTINGS[i].options;
                    size_t at = 0;
                    while (s && *s) { const char *e = strchr(s, '|'); size_t len = e ? (size_t)(e - s) : strlen(s); if (at == o) { snprintf(buf, sizeof buf, "%.*s", (int)len, s); opt = buf; break; } at++; s = e ? e + 1 : NULL; }
                    if (opt) set_key(v, SETTINGS[i].key, opt, strcmp(SETTINGS[i].key, "rowHeight") != 0 && strcmp(SETTINGS[i].key, "markers") != 0 && strcmp(SETTINGS[i].key, "imageFit") != 0 && strcmp(SETTINGS[i].key, "separator") != 0);
                    break;
                }
                n++;
            }
        }
        break;
    }
}

static void show_menu(View *v, ExWidget *w, float x, float y, int kind) {
    ExMenuItem items[64];
    char labels[64][120];
    size_t n = 0, i;
    memset(items, 0, sizeof items);
    if (!v->h.panels) return;
    g_menu.v = v; g_menu.w = w;
    if (kind == HIT_VIEWS && v->base) {
        for (i = 0; i < ex_base_view_count(v->base) && n < 40; i++) {
            const char *nm = ex_base_view_name(v->base, i);
            snprintf(labels[n], sizeof labels[n], "%s (%s)%s", *nm ? nm : "Unnamed", ex_base_view_type(v->base, i), i == v->view ? "  - shown" : "");
            items[n].label = labels[n]; items[n].id = M_VIEW0 + (int)i; n++;
        }
        items[n].label = "Add a table view"; items[n].id = M_ADD_TABLE; items[n].sep_before = 1; n++;
        items[n].label = "Add a list view"; items[n].id = M_ADD_LIST; n++;
        items[n].label = "Add a cards view"; items[n].id = M_ADD_CARDS; n++;
        items[n].label = "Add a kanban view"; items[n].id = M_ADD_KANBAN; n++;
        items[n].label = "Add a map view"; items[n].id = M_ADD_MAP; n++;
        if (ex_base_view_count(v->base)) { items[n].label = "Rename this view..."; items[n].id = M_RENAME; items[n].sep_before = 1; n++; items[n].label = "Delete this view"; items[n].id = M_DELETE_VIEW; items[n].danger = 1; n++; }
    } else if (kind == HIT_RESULTS) {
        items[n].label = "Copy to the clipboard"; items[n].id = M_COPY; n++;
        items[n].label = "Export as CSV"; items[n].id = M_CSV; n++;
        items[n].label = "Limit the results..."; items[n].id = M_LIMIT; items[n].sep_before = 1; n++;
        items[n].label = "Run again"; items[n].id = M_REFRESH; n++;
    } else if (kind == HIT_SETTINGS && v->base) {
        size_t k = 0;
        for (i = 0; i < sizeof SETTINGS / sizeof *SETTINGS && n < 60; i++) {
            const char *cur, *s = SETTINGS[i].options;
            size_t o = 0;
            if (strcmp(SETTINGS[i].layout, LAYOUT_NAMES[v->layout])) continue;
            cur = get_key(v, SETTINGS[i].key, "");
            while (s && *s && n < 60) {
                const char *e = strchr(s, '|');
                size_t len = e ? (size_t)(e - s) : strlen(s);
                snprintf(labels[n], sizeof labels[n], "%s: %.*s%s", SETTINGS[i].label, (int)len, s, (strlen(cur) == len && !strncmp(cur, s, len)) ? "  - on" : "");
                items[n].label = labels[n]; items[n].id = M_SET0 + (int)(k * 10 + o); items[n].sep_before = o == 0 && n > 0;
                n++; o++;
                s = e ? e + 1 : NULL;
            }
            k++;
        }
        if ((v->layout == L_CARDS || v->layout == L_KANBAN) && v->res) {
            size_t j;
            const char *cur = get_key(v, "image", "");
            snprintf(labels[n], sizeof labels[n], "Image: none%s", cur[0] ? "" : "  - on");
            items[n].label = labels[n]; items[n].id = M_IMG0; items[n].sep_before = n > 0; n++;
            for (j = 0; j < v->res->ncols && n < 60; j++) {
                snprintf(labels[n], sizeof labels[n], "Image: %s%s", v->res->cols[j].name, strcmp(cur, v->res->cols[j].id) ? "" : "  - on");
                items[n].label = labels[n]; items[n].id = M_IMG0 + 1 + (int)j; n++;
            }
        }
        if (!n) { snprintf(labels[0], sizeof labels[0], "This layout has no settings here"); items[0].label = labels[0]; items[0].id = -1; n = 1; }
    }
    if (n) ex_panels_popup(v->h.panels, x, y, items, n, menu_done, &g_menu);
}

void ex_baseview_context(ExWidget *w, float x, float y) {
    View *v = view_of(w);
    ExMenuItem items[8];
    size_t n = 0, i;
    memset(items, 0, sizeof items);
    if (!v || !v->h.panels) return;
    g_menu.v = v; g_menu.w = w;
    g_menu_file[0] = 0;
    for (i = v->nhits; i > 0; i--) { const Hit *h = &v->hits[i - 1]; if ((h->kind == HIT_FILE) && x >= h->x && x <= h->x + h->w && y >= h->y && y <= h->y + h->h && h->s) { snprintf(g_menu_file, sizeof g_menu_file, "%s", h->s); break; } }
    if (g_menu_file[0]) { items[n].label = "Open"; items[n].id = M_OPEN; n++; items[n].label = "Open in a new tab"; items[n].id = M_OPEN_TAB; n++; }
    items[n].label = "Copy to the clipboard"; items[n].id = M_COPY; items[n].sep_before = g_menu_file[0]; n++;
    items[n].label = "Export as CSV"; items[n].id = M_CSV; n++;
    ex_panels_popup(v->h.panels, x, y, items, n, menu_done, &g_menu);
}

/* ---- source, mouse, keys ---- */

void ex_baseview_toggle_source(ExWidget *w) {
    View *v = view_of(w);
    if (!v) return;
    if (v->source) {
        ex_ui_set_focus(v->ui, w);
        ex_ui_release(v->ui, v->source);
        ex_widget_remove(w, v->source);
        ex_widget_free(v->source);
        v->source = NULL;
        w->pad_y = 0.0f;
        v->stale = 1;
        return;
    }
    commit(v);
    v->source = ex_widget_editor(v->ui, v->doc, "Base source");
    if (!v->source) return;
    ex_editor_set_wrap(v->source->editor, 0);
    v->source->width = ex_size_grow(); v->source->height = ex_size_grow();
    w->vertical = 1; w->align_x = 0; w->align_y = 0;
    ex_widget_add(w, v->source);
    ex_ui_set_focus(v->ui, v->source);
}
void ex_baseview_commit(ExWidget *w) { View *v = view_of(w); if (v) commit(v); }

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    size_t i;
    if (kind != EX_MOUSE_DOWN) return 0;
    if (under != w) return 0;
    ex_ui_set_focus(v->ui, w);
    for (i = v->nhits; i > 0; i--) {
        const Hit *h = &v->hits[i - 1];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        switch (h->kind) {
        case HIT_VIEWS: case HIT_RESULTS: case HIT_SETTINGS: show_menu(v, w, h->x, h->y + h->h, h->kind); return 1;
        case HIT_SOURCE: ex_baseview_toggle_source(w); return 1;
        case HIT_FILE: if (h->s && v->h.open_file) v->h.open_file(v->h.user, h->s, 0); return 1;
        case HIT_URL: if (h->s && v->h.open_url) v->h.open_url(v->h.user, h->s); return 1;
        case HIT_GROUP: if (h->s) toggle_collapsed(v, h->s); return 1;
        default: break;
        }
    }
    return 1;
}

void ex_baseview_wheel(ExWidget *w, float dx, float dy, float x, float y) {
    View *v = view_of(w);
    (void)x; (void)y;
    if (!v || v->source) return;
    v->scroll_y -= dy * 60.0f * ex_renderer_scale(ex_ui_renderer(v->ui));
    v->scroll_x -= dx * 60.0f * ex_renderer_scale(ex_ui_renderer(v->ui));
    if (v->scroll_y < 0) v->scroll_y = 0;
    if (v->scroll_x < 0) v->scroll_x = 0;
}

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    float page = w->h * 0.85f, line = 40.0f * ex_renderer_scale(ex_ui_renderer(v->ui));
    (void)mods;
    switch (key) {
    case EX_KEY_DOWN: v->scroll_y += line; return 1;
    case EX_KEY_UP: v->scroll_y -= line; if (v->scroll_y < 0) v->scroll_y = 0; return 1;
    case EX_KEY_RIGHT: v->scroll_x += line; return 1;
    case EX_KEY_LEFT: v->scroll_x -= line; if (v->scroll_x < 0) v->scroll_x = 0; return 1;
    case EX_KEY_PAGE_DOWN: v->scroll_y += page; return 1;
    case EX_KEY_PAGE_UP: v->scroll_y -= page; if (v->scroll_y < 0) v->scroll_y = 0; return 1;
    case EX_KEY_HOME: v->scroll_y = 0; v->scroll_x = 0; return 1;
    case EX_KEY_END: v->scroll_y = 1e9f; return 1;
    case EX_KEY_C: if (mods & EX_MOD_PRIMARY) { copy_to_clipboard(v); return 1; } return 0;
    default: return 0;
    }
}

static void view_free(void *p) {
    View *v = p;
    size_t i;
    if (!v) return;
    clear_hits(v);
    ex_baseresult_free_heap(v->res);
    ex_base_free(v->base);
    for (i = 0; i < v->ncollapsed; i++) free(v->collapsed[i]);
    free(v->collapsed);
    ex_geom_free(v->geom);
    free(v->path);
    free(v);
}

ExWidget *ex_widget_base(ExUi *ui, ExDoc *doc, const char *path, const ExBaseViewHandlers *h) {
    ExWidget *w = ex_widget_box(ui, 0, path && *path ? path : "Base");
    View *v;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    v->ui = ui; v->doc = doc; v->path = dups(path ? path : "");
    if (h) v->h = *h;
    w->type = EX_WIDGET_BASE;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1; w->clip = 1; w->vertical = 1; w->align_x = 0; w->align_y = 0;
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    reload(v);
    return w;
}

void ex_baseview_copy(ExWidget *w) { View *v = view_of(w); if (v) copy_to_clipboard(v); }
void ex_baseview_add_view(ExWidget *w, const char *type) {
    View *v = view_of(w);
    char name[40];
    if (!v || !type || !*type) return;
    snprintf(name, sizeof name, "%c%s", (type[0] >= 'a' && type[0] <= 'z') ? type[0] - 32 : type[0], type + 1);
    add_view(v, type, name);
}
void ex_baseview_view_menu(ExWidget *w) { View *v = view_of(w); if (v) show_menu(v, w, w->x + 10.0f, w->y + v->toolbar_h, HIT_VIEWS); }
