#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <hb.h>

#include "editor_fold.h"
#include "editor_layout.h"
#include "exhaust_editor.h"
#include "exhaust_markdown.h"
#include "exhaust_platform.h"
#include "exhaust_shape.h"

#define CACHE_LINES 96

typedef struct CachedLine {
    char *text;
    size_t len;
    int px;
    uint32_t font;
    ExShaped sh;
    unsigned long used;
    int valid;
} CachedLine;

struct ExEditor {
    ExDoc *doc;
    ExFont *font;
    float size;
    int line_pct;
    float vx, vy, vw, vh;       /* viewport, pixels */
    float scroll_y;
    char composition[512];
    float *goal;                 /* per selection: x to keep while moving vertically; <0 none */
    size_t ngoal;
    int last_vertical;
    ExClipboard clip;
    int have_clip;
    CachedLine cache[CACHE_LINES];
    unsigned long tick;
    size_t drag_anchor;
    int dragging;
    unsigned long caret_dirty;
    int readonly;
    ExEditorMode mode;
    ExMd *md;                    /* tokens of the document, for the modes that style it */
    const ExDoc *md_doc;
    unsigned long md_version;
    int md_valid;
    /* folding: the regions that can be folded (from the tokens), which of them are folded (by the byte offset
     * where their hidden lines start, moved along with the edits), and the hidden line ranges that follow */
    int fold_headings, fold_lists;
    ExFolds folds;
    unsigned long folds_version;
    int folds_valid;
    size_t *folded;
    size_t nfolded, capfolded;
    unsigned long folded_version;
    struct { size_t first, last; } *hid;
    size_t nhid;
};

/* ---- clipboard default ---- */

static char *os_get(void *u) { (void)u; return ex_clipboard_get_text(); }
static void os_set(void *u, const char *t) { (void)u; ex_clipboard_set_text(t); }

ExEditor *ex_editor_create(ExDoc *doc) {
    ExEditor *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->doc = doc;
    e->size = 16.0f;
    e->line_pct = 150;
    e->clip.get = os_get;
    e->clip.set = os_set;
    e->fold_headings = e->fold_lists = 1;
    return e;
}

static void clear_cache(ExEditor *e) {
    int i;
    for (i = 0; i < CACHE_LINES; i++)
        if (e->cache[i].valid) { free(e->cache[i].text); ex_shaped_free(&e->cache[i].sh); e->cache[i].valid = 0; }
}

void ex_editor_free(ExEditor *e) {
    if (!e) return;
    ex_md_free(e->md);
    clear_cache(e);
    ex_folds_free(&e->folds);
    free(e->folded);
    free(e->hid);
    free(e->goal);
    free(e);
}

ExDoc *ex_editor_doc(ExEditor *e) { return e->doc; }
void ex_editor_set_readonly(ExEditor *e, int readonly) { e->readonly = readonly != 0; }
int ex_editor_readonly(const ExEditor *e) { return e->readonly; }
void ex_editor_set_doc(ExEditor *e, ExDoc *doc) {
    e->md_valid = 0;
    e->folds_valid = 0;
    e->nfolded = 0;
    e->nhid = 0;
    clear_cache(e);
    e->doc = doc;
    e->scroll_y = 0;
    e->composition[0] = 0;
    e->last_vertical = 0;
    free(e->goal);
    e->goal = NULL;
    e->ngoal = 0;
}
void ex_editor_set_clipboard(ExEditor *e, const ExClipboard *cb) { e->clip = *cb; }

void ex_editor_set_font(ExEditor *e, ExFont *font, float size, int line_height_pct) {
    if (e->font != font || e->size != size) clear_cache(e);
    e->font = font;
    e->size = size;
    e->line_pct = line_height_pct > 50 ? line_height_pct : 150;
}

void ex_editor_set_viewport(ExEditor *e, float x, float y, float w, float h) { e->vx = x; e->vy = y; e->vw = w; e->vh = h; }
float ex_editor_scroll_y(const ExEditor *e) { return e->scroll_y; }


/* ---- lines ---- */

static const ExBuffer *buf(const ExEditor *e) { return ex_doc_buffer(e->doc); }

static size_t line_count(const ExEditor *e) { return ex_buffer_line_count(buf(e)); }

/* Byte range of a line without its line break. */
static void line_range(const ExEditor *e, size_t line, size_t *start, size_t *end, size_t *next) {
    size_t s = ex_buffer_line_start(buf(e), line), n = ex_buffer_line_start(buf(e), line + 1), en = n;
    if (line + 1 < line_count(e) && en > s) en--; /* the '\n' */
    *start = s; *end = en; *next = n;
}

static char *line_text(const ExEditor *e, size_t line, size_t *len) {
    size_t s, en, n;
    char *t;
    line_range(e, line, &s, &en, &n);
    t = malloc(en - s + 1);
    if (!t) return NULL;
    *len = ex_buffer_read(buf(e), s, t, en - s);
    t[*len] = 0;
    return t;
}

static int line_px(const ExEditor *e, ExRenderer *r) { return ex_px(r, e->size); }
static int gutter_px(ExRenderer *r) { return ex_px(r, 14); }                  /* room for the fold marker */
static int pad_px(ExRenderer *r) { return gutter_px(r) + ex_px(r, 8); }       /* from the left edge to the text */

static int line_height_px(const ExEditor *e, ExRenderer *r) {
    ExFontMetrics m;
    int lh;
    if (ex_font_metrics(e->font, line_px(e, r), &m)) return 20;
    lh = (m.line_height * e->line_pct + 50) / 100;
    return lh < m.line_height ? m.line_height : lh;
}

/* The shaped line, cached by text. Returns NULL on failure. */
static const ExShaped *shaped(ExEditor *e, ExRenderer *r, const char *text, size_t len) {
    int px = line_px(e, r), i, victim = 0;
    unsigned long oldest = (unsigned long)-1;
    CachedLine *c;
    e->tick++;
    for (i = 0; i < CACHE_LINES; i++) {
        CachedLine *l = &e->cache[i];
        if (l->valid && l->len == len && l->px == px && l->font == ex_font_id(e->font) && memcmp(l->text, text, len) == 0) {
            l->used = e->tick;
            return &l->sh;
        }
        if (!l->valid) { victim = i; oldest = 0; }
        else if (l->used < oldest) { oldest = l->used; victim = i; }
    }
    c = &e->cache[victim];
    if (c->valid) { free(c->text); ex_shaped_free(&c->sh); c->valid = 0; }
    c->text = malloc(len + 1);
    if (!c->text) return NULL;
    memcpy(c->text, text, len);
    c->text[len] = 0;
    c->len = len;
    c->px = px;
    c->font = ex_font_id(e->font);
    if (ex_shape(ex_renderer_fonts(r), e->font, px, text, len, &c->sh) < 0) { free(c->text); return NULL; }
    c->valid = 1;
    c->used = e->tick;
    return &c->sh;
}

/* ---- Markdown tokens and line layouts (source mode and live preview) ---- */

/* Brings the tokens up to date with the document: the edits since the last call become one edit for the
 * incremental update (or the whole text is tokenized when the log does not reach back far enough). */
static void ensure_md(ExEditor *e) {
    ExDocEdit edits[EX_DOC_EDIT_LOG];
    int n;
    char *text;
    size_t len = 0, lo_off = 0, hi_old = 0, hi_new = 0, i;
    unsigned long version;
    if (e->mode == EX_EDITOR_PLAIN) return;
    version = ex_doc_version(e->doc);
    if (e->md && e->md_valid && e->md_doc == e->doc && e->md_version == version) return;
    text = ex_buffer_text(buf(e), &len);
    if (!text) return;
    if (!e->md) e->md = ex_md_create();
    n = (e->md && e->md_valid && e->md_doc == e->doc) ? ex_doc_edits_since(e->doc, e->md_version, edits, EX_DOC_EDIT_LOG) : -1;
    if (n > 0) {
        for (i = 0; i < (size_t)n; i++) {                       /* combine the edits into one that covers them all */
            size_t o = edits[i].offset, rm = edits[i].removed, ins = edits[i].inserted;
            if (i == 0) { lo_off = o; hi_old = o + rm; hi_new = o + ins; }
            else {
                size_t nlo = o < lo_off ? o : lo_off, cur_end = o + rm, nhi = cur_end > hi_new ? cur_end : hi_new;
                hi_old += nhi - hi_new;
                lo_off = nlo;
                hi_new = nhi - rm + ins;
            }
        }
        ex_md_update(e->md, text, len, lo_off, hi_old - lo_off, hi_new - lo_off);
    } else if (n == 0) {
        /* nothing changed in the text */
    } else {
        ex_md_tokenize(e->md, text, len);
    }
    free(text);
    e->md_doc = e->doc;
    e->md_version = version;
    e->md_valid = 1;
}

/* The layout of one line, or NULL in plain mode. The caller frees it. */
static LLine *layout_of(ExEditor *e, ExRenderer *r, size_t line, size_t *ls_out, size_t *le_out) {
    LEnv env;
    size_t len, s, en, n;
    char *t;
    LLine *l;
    if (e->mode == EX_EDITOR_PLAIN || !e->font) return NULL;
    ensure_md(e);
    t = line_text(e, line, &len);
    if (!t) return NULL;
    line_range(e, line, &s, &en, &n);
    env.r = r; env.size = e->size; env.live = e->mode == EX_EDITOR_LIVE; env.md = e->md; env.doc = e->doc; env.line = t; env.ls = s; env.le = en;
    l = ll_build(&env);
    free(t);              /* the runs point into the text: keep a copy that lives as long as the layout */
    if (ls_out) *ls_out = s;
    if (le_out) *le_out = en;
    return l;
}


/* ---- folding ---- */

static long find_folded(const ExEditor *e, size_t hs) {
    size_t i;
    for (i = 0; i < e->nfolded; i++) if (e->folded[i] == hs) return (long)i;
    return -1;
}

static void add_folded(ExEditor *e, size_t hs) {
    if (find_folded(e, hs) >= 0) return;
    if (e->nfolded == e->capfolded) {
        size_t nc = e->capfolded ? e->capfolded * 2 : 16;
        size_t *p = realloc(e->folded, nc * sizeof *p);
        if (!p) return;
        e->folded = p;
        e->capfolded = nc;
    }
    e->folded[e->nfolded++] = hs;
}

static void remove_folded(ExEditor *e, size_t i) { e->folded[i] = e->folded[--e->nfolded]; }

static int cmp_hid(const void *x, const void *y) {
    const size_t *a = x, *b = y;
    return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
}

/* The hidden line ranges (sorted, not overlapping) from the folded regions. */
static void rebuild_hidden(ExEditor *e) {
    size_t i, n = 0;
    size_t (*tmp)[2] = malloc((e->nfolded ? e->nfolded : 1) * sizeof *tmp);
    free(e->hid);
    e->hid = NULL;
    e->nhid = 0;
    if (!tmp) return;
    for (i = 0; i < e->nfolded; i++) {
        long k = ex_folds_find_hs(&e->folds, e->folded[i]);
        if (k < 0) continue;
        tmp[n][0] = e->folds.r[k].first;
        tmp[n][1] = e->folds.r[k].last;
        n++;
    }
    qsort(tmp, n, sizeof *tmp, cmp_hid);
    e->hid = malloc((n ? n : 1) * sizeof *e->hid);
    if (e->hid) {
        for (i = 0; i < n; i++) {
            if (e->nhid && tmp[i][0] <= e->hid[e->nhid - 1].last + 1) {       /* merge nested and touching ranges */
                if (tmp[i][1] > e->hid[e->nhid - 1].last) e->hid[e->nhid - 1].last = tmp[i][1];
            } else {
                e->hid[e->nhid].first = tmp[i][0];
                e->hid[e->nhid].last = tmp[i][1];
                e->nhid++;
            }
        }
    }
    free(tmp);
}

static int line_hidden(const ExEditor *e, size_t line) {
    size_t i;
    for (i = 0; i < e->nhid; i++) {
        if (line < e->hid[i].first) return 0;
        if (line <= e->hid[i].last) return 1;
    }
    return 0;
}

/* The screen row of a line: the lines before it minus the hidden ones (a hidden line has the row of its header). */
static size_t row_of_line(const ExEditor *e, size_t line) {
    size_t i, hidden = 0;
    for (i = 0; i < e->nhid; i++) {
        if (e->hid[i].last < line) hidden += e->hid[i].last - e->hid[i].first + 1;
        else if (e->hid[i].first <= line) return e->hid[i].first - 1 - hidden;
        else break;
    }
    return line - hidden;
}

static size_t line_of_row(const ExEditor *e, size_t row) {
    size_t i, line = row;
    for (i = 0; i < e->nhid; i++) {
        if (line >= e->hid[i].first) line += e->hid[i].last - e->hid[i].first + 1;
        else break;
    }
    return line;
}

static size_t row_count(const ExEditor *e) {
    size_t i, nl = line_count(e);
    for (i = 0; i < e->nhid; i++) nl -= e->hid[i].last - e->hid[i].first + 1;
    return nl;
}

/* Brings the folds up to date with the text: the folded offsets follow the edits, only regions that still exist
 * stay folded, and a caret inside hidden text opens the fold that hides it. */
static void fold_sync(ExEditor *e) {
    unsigned long version;
    size_t i;
    int changed = 0;
    if (e->mode == EX_EDITOR_PLAIN || (!e->fold_headings && !e->fold_lists)) {
        if (e->nfolded || e->nhid || e->folds.n) { e->nfolded = 0; rebuild_hidden(e); e->folds.n = 0; e->folds_valid = 0; }
        return;
    }
    ensure_md(e);
    version = ex_doc_version(e->doc);
    if (e->folds_valid && e->folds_version == version && e->md_valid) goto reveal;
    if (e->nfolded && e->folded_version != version) {                      /* move the folded offsets along with the edits */
        ExDocEdit edits[EX_DOC_EDIT_LOG];
        int n = ex_doc_edits_since(e->doc, e->folded_version, edits, EX_DOC_EDIT_LOG);
        if (n < 0) e->nfolded = 0;
        else {
            int k;
            for (k = 0; k < n; k++)
                for (i = 0; i < e->nfolded; i++) {
                    size_t p = e->folded[i], o = edits[k].offset, rm = edits[k].removed, ins = edits[k].inserted;
                    if (p <= o) continue;
                    e->folded[i] = p >= o + rm ? p - rm + ins : o;
                }
        }
    }
    e->folded_version = version;
    ex_folds_compute(&e->folds, e->md, buf(e), e->fold_headings, e->fold_lists);
    e->folds_version = version;
    e->folds_valid = 1;
    for (i = 0; i < e->nfolded;) {                                         /* only regions that still exist stay folded */
        if (ex_folds_find_hs(&e->folds, e->folded[i]) < 0) { remove_folded(e, i); continue; }
        i++;
    }
    changed = 1;
reveal:
    if (changed) rebuild_hidden(e);
    {
        size_t k, ns = ex_doc_sel_count(e->doc);
        int opened = 0;
        for (k = 0; k < ns && e->nhid; k++) {
            ExSel s = ex_doc_sel(e->doc, k);
            size_t line;
            if (s.anchor != s.head) continue;
            line = ex_buffer_line_of_offset(buf(e), s.head);
            if (!line_hidden(e, line)) continue;
            for (i = 0; i < e->nfolded;) {
                long f = ex_folds_find_hs(&e->folds, e->folded[i]);
                if (f >= 0 && e->folds.r[f].first <= line && line <= e->folds.r[f].last) { remove_folded(e, i); opened = 1; continue; }
                i++;
            }
        }
        if (opened) rebuild_hidden(e);
    }
}

/* Where a caret goes that would land in hidden text: back to the end of the header (moving left) or on to the next shown line. */
static size_t skip_hidden(ExEditor *e, size_t off, int dir) {
    size_t line, i;
    if (!e->nhid) return off;
    line = ex_buffer_line_of_offset(buf(e), off);
    for (i = 0; i < e->nhid; i++) {
        if (line < e->hid[i].first) break;
        if (line <= e->hid[i].last) {
            if (dir > 0) {
                size_t nl = e->hid[i].last + 1;
                return nl < line_count(e) ? ex_buffer_line_start(buf(e), nl) : ex_buffer_size(buf(e));
            } else {
                size_t st, en, nx;
                line_range(e, e->hid[i].first - 1, &st, &en, &nx);
                return en;
            }
        }
    }
    return off;
}

void ex_editor_set_folding(ExEditor *e, int headings, int lists) {
    headings = headings != 0;
    lists = lists != 0;
    if (e->fold_headings == headings && e->fold_lists == lists) return;
    e->fold_headings = headings;
    e->fold_lists = lists;
    e->folds_valid = 0;
}

/* If the primary caret is in text that is now hidden, it moves to the end of the header line. */
static void pull_caret_out(ExEditor *e) {
    ExSel s = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    size_t line = ex_buffer_line_of_offset(buf(e), s.head);
    if (line_hidden(e, line)) ex_doc_set_caret(e->doc, skip_hidden(e, s.head, -1));
}

void ex_editor_fold(ExEditor *e, ExFoldCmd cmd) {
    size_t i, maxd = 0, mind = (size_t)-1;
    fold_sync(e);
    if (e->mode == EX_EDITOR_PLAIN) return;
    switch (cmd) {
    case EX_FOLD_ALL:
        for (i = 0; i < e->folds.n; i++) add_folded(e, e->folds.r[i].hs);
        break;
    case EX_FOLD_UNFOLD_ALL:
        e->nfolded = 0;
        break;
    case EX_FOLD_MORE: {                                  /* fold the deepest regions that are shown and still open */
        for (i = 0; i < e->folds.n; i++) {
            const ExFoldRange *f = &e->folds.r[i];
            if (line_hidden(e, f->hdr) || find_folded(e, f->hs) >= 0) continue;
            if (f->depth >= maxd) maxd = f->depth;
        }
        for (i = 0; i < e->folds.n; i++) {
            const ExFoldRange *f = &e->folds.r[i];
            if (line_hidden(e, f->hdr) || find_folded(e, f->hs) >= 0) continue;
            if (f->depth == maxd) add_folded(e, f->hs);
        }
        break;
    }
    case EX_FOLD_LESS: {                                  /* open the shallowest folded regions */
        for (i = 0; i < e->nfolded; i++) {
            long k = ex_folds_find_hs(&e->folds, e->folded[i]);
            if (k >= 0 && e->folds.r[k].depth < mind) mind = e->folds.r[k].depth;
        }
        for (i = 0; i < e->nfolded;) {
            long k = ex_folds_find_hs(&e->folds, e->folded[i]);
            if (k >= 0 && e->folds.r[k].depth == mind) { remove_folded(e, i); continue; }
            i++;
        }
        break;
    }
    case EX_FOLD_TOGGLE_LINE: {
        ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        size_t line = ex_buffer_line_of_offset(buf(e), p.head);
        long k = ex_folds_find_hdr(&e->folds, line);
        if (k < 0) {                                       /* not on a header: the innermost region around the line */
            for (i = 0; i < e->folds.n; i++)
                if (e->folds.r[i].hdr < line && line <= e->folds.r[i].last) k = (long)i;
        }
        if (k >= 0) {
            long f = find_folded(e, e->folds.r[k].hs);
            if (f >= 0) remove_folded(e, (size_t)f); else add_folded(e, e->folds.r[k].hs);
        }
        break;
    }
    }
    rebuild_hidden(e);
    pull_caret_out(e);
}

size_t ex_editor_fold_count(ExEditor *e) { fold_sync(e); return e->nfolded; }
size_t ex_editor_region_count(ExEditor *e) { fold_sync(e); return e->folds.n; }
int ex_editor_line_hidden(ExEditor *e, size_t line) { fold_sync(e); return line_hidden(e, line); }
size_t ex_editor_row_count(ExEditor *e) { fold_sync(e); return row_count(e); }
size_t ex_editor_row_of_line(ExEditor *e, size_t line) { fold_sync(e); return row_of_line(e, line); }
int ex_editor_toggle_fold_at_line(ExEditor *e, size_t line) {
    long k;
    fold_sync(e);
    k = ex_folds_find_hdr(&e->folds, line);
    if (k < 0) return 0;
    {
        long f = find_folded(e, e->folds.r[k].hs);
        if (f >= 0) remove_folded(e, (size_t)f); else add_folded(e, e->folds.r[k].hs);
    }
    rebuild_hidden(e);
    pull_caret_out(e);
    return 1;
}

static float x_of(ExEditor *e, ExRenderer *r, size_t line, size_t offset) {
    size_t len, s, en, n;
    char *t;
    if (e->mode != EX_EDITOR_PLAIN) {
        LLine *l = layout_of(e, r, line, &s, &en);
        float x = 0;
        if (l) { if (offset < s) offset = s; if (offset > en) offset = en; x = ll_x_of(l, offset); ll_free(l); }
        return x;
    }
    t = line_text(e, line, &len);
    float x = 0.0f;
    const ExShaped *sh;
    line_range(e, line, &s, &en, &n);
    if (offset < s) offset = s;
    if (offset > en) offset = en;
    if (t && (sh = shaped(e, r, t, len)) != NULL) x = ex_shaped_x_for_offset(sh, offset - s, len);
    free(t);
    return x;
}

static size_t offset_at_x(ExEditor *e, ExRenderer *r, size_t line, float x) {
    size_t len, s, en, n, off = 0;
    char *t;
    if (e->mode != EX_EDITOR_PLAIN) {
        LLine *l = layout_of(e, r, line, &s, &en);
        size_t o = s;
        if (l) { o = ll_offset_at(l, x, s, en); ll_free(l); }
        return o;
    }
    t = line_text(e, line, &len);
    const ExShaped *sh;
    line_range(e, line, &s, &en, &n);
    if (t && (sh = shaped(e, r, t, len)) != NULL) off = ex_shaped_offset_for_x(sh, x, len);
    free(t);
    return s + off;
}

/* ---- selections and movement ---- */

static size_t lo(ExSel s) { return s.anchor < s.head ? s.anchor : s.head; }
static size_t hi(ExSel s) { return s.anchor < s.head ? s.head : s.anchor; }

static size_t next_boundary(ExEditor *e, size_t off) {
    size_t line = ex_buffer_line_of_offset(buf(e), off), s, en, n, len;
    char *t;
    size_t r;
    line_range(e, line, &s, &en, &n);
    if (off >= en) return off < ex_buffer_size(buf(e)) ? n : off; /* over the line break */
    t = line_text(e, line, &len);
    if (!t) return off;
    r = s + ex_grapheme_next(t, len, off - s);
    free(t);
    return r;
}

static size_t prev_boundary(ExEditor *e, size_t off) {
    size_t line, s, en, n, len;
    char *t;
    size_t r;
    if (off == 0) return 0;
    line = ex_buffer_line_of_offset(buf(e), off);
    line_range(e, line, &s, &en, &n);
    if (off == s) { /* at a line start: step back over the previous line's break */
        size_t ps, pe, pn;
        line_range(e, line - 1, &ps, &pe, &pn);
        return pe;
    }
    t = line_text(e, line, &len);
    if (!t) return off;
    r = s + ex_grapheme_prev(t, len, off - s);
    free(t);
    return r;
}

static int is_word_cp(uint32_t cp) {
    hb_unicode_general_category_t c = hb_unicode_general_category(hb_unicode_funcs_get_default(), cp);
    return cp == '_' || (c >= HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER && c <= HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER) ||
           c == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER || c == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
           c == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK;
}

/* Word boundaries within a line's text: dir > 0 forward, < 0 backward. */
static size_t word_move(ExEditor *e, size_t off, int dir) {
    size_t size = ex_buffer_size(buf(e));
    int seen_word = 0;
    for (;;) {
        size_t nb = dir > 0 ? next_boundary(e, off) : prev_boundary(e, off);
        char tmp[8];
        size_t from = dir > 0 ? off : nb, got, used = 0;
        uint32_t cp;
        if (nb == off) return off;
        got = ex_buffer_read(buf(e), from, tmp, sizeof tmp);
        cp = got ? ex_utf8_decode(tmp, got, &used) : ' ';
        if (cp == '\n') { if (seen_word) return off; off = nb; if (dir > 0) return off; continue; }
        if (is_word_cp(cp)) seen_word = 1;
        else if (seen_word) return off;
        off = nb;
        if (off == 0 || off >= size) { if (seen_word || dir < 0 || off >= size) return off; }
    }
}

static void sync_goal(ExEditor *e, size_t n) {
    if (e->ngoal != n) {
        float *g = realloc(e->goal, (n ? n : 1) * sizeof *g);
        size_t i;
        if (!g) return;
        e->goal = g;
        for (i = 0; i < n; i++) g[i] = -1.0f;
        e->ngoal = n;
    }
}

static void ensure_visible(ExEditor *e, ExRenderer *r) {
    ExSel p;
    size_t line;
    int lh;
    float top, bottom;
    if (e->vh <= 0 || !e->font) return;
    fold_sync(e);
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    line = ex_buffer_line_of_offset(buf(e), p.head);
    lh = line_height_px(e, r);
    top = (float)row_of_line(e, line) * (float)lh;
    bottom = top + (float)lh;
    if (top < e->scroll_y) e->scroll_y = top;
    else if (bottom > e->scroll_y + e->vh) e->scroll_y = bottom - e->vh;
    if (e->scroll_y < 0) e->scroll_y = 0;
}

/* Replaces the selections; keep_anchor extends instead of collapsing. */
static void set_heads(ExEditor *e, ExRenderer *r, const size_t *heads, int keep_anchor, int vertical) {
    size_t n = ex_doc_sel_count(e->doc), i;
    ExSel *s = malloc((n ? n : 1) * sizeof *s);
    float *saved = NULL;
    if (!s) return;
    if (vertical && e->ngoal == n) { saved = malloc(n * sizeof *saved); if (saved) memcpy(saved, e->goal, n * sizeof *saved); }
    for (i = 0; i < n; i++) {
        ExSel old = ex_doc_sel(e->doc, i);
        s[i].head = heads[i];
        s[i].anchor = keep_anchor ? old.anchor : heads[i];
    }
    ex_doc_set_selections(e->doc, s, n);
    sync_goal(e, ex_doc_sel_count(e->doc));
    if (saved && e->ngoal == n) memcpy(e->goal, saved, n * sizeof *saved);
    free(saved);
    free(s);
    e->last_vertical = vertical;
    ensure_visible(e, r);
}

static void move_horizontal(ExEditor *e, ExRenderer *r, int dir, int word, int shift) {
    size_t n = ex_doc_sel_count(e->doc), i;
    size_t *heads = malloc((n ? n : 1) * sizeof *heads);
    if (!heads) return;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        if (!shift && s.anchor != s.head) heads[i] = dir < 0 ? lo(s) : hi(s); /* collapse to the edge */
        else heads[i] = word ? word_move(e, s.head, dir) : (dir < 0 ? prev_boundary(e, s.head) : next_boundary(e, s.head));
        heads[i] = skip_hidden(e, heads[i], dir);
    }
    set_heads(e, r, heads, shift, 0);
    free(heads);
}

static void move_vertical(ExEditor *e, ExRenderer *r, int lines, int shift) {
    size_t n = ex_doc_sel_count(e->doc), i, rows;
    size_t *heads = malloc((n ? n : 1) * sizeof *heads);
    if (!heads) return;
    fold_sync(e);
    rows = row_count(e);
    sync_goal(e, n);
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), target;
        float x = (e->last_vertical && e->goal && e->goal[i] >= 0) ? e->goal[i] : x_of(e, r, line, s.head);
        long t = (long)row_of_line(e, line) + lines;                 /* moves by shown rows: hidden lines are skipped */
        if (e->goal) e->goal[i] = x;
        if (t < 0) { heads[i] = 0; if (e->goal) e->goal[i] = -1; continue; }
        if ((size_t)t >= rows) {
            size_t last = line_of_row(e, rows - 1), st, en, nx;
            if (last + 1 >= line_count(e)) heads[i] = ex_buffer_size(buf(e));
            else { line_range(e, last, &st, &en, &nx); heads[i] = en; }
            if (e->goal) e->goal[i] = -1;
            continue;
        }
        target = line_of_row(e, (size_t)t);
        heads[i] = offset_at_x(e, r, target, x);
    }
    set_heads(e, r, heads, shift, 1);
    free(heads);
}

static void move_line_edge(ExEditor *e, ExRenderer *r, int to_end, int shift) {
    size_t n = ex_doc_sel_count(e->doc), i;
    size_t *heads = malloc((n ? n : 1) * sizeof *heads);
    if (!heads) return;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), st, en, nx;
        line_range(e, line, &st, &en, &nx);
        heads[i] = to_end ? en : st;
    }
    set_heads(e, r, heads, shift, 0);
    free(heads);
}

static void move_doc_edge(ExEditor *e, ExRenderer *r, int to_end, int shift) {
    size_t heads[1];
    fold_sync(e);
    heads[0] = to_end ? ex_buffer_size(buf(e)) : 0;
    if (to_end) heads[0] = skip_hidden(e, heads[0], -1);                 /* a folded end of the text: the end of its header */
    /* one caret at the edge: keep it simple */
    if (!shift) { ex_doc_set_caret(e->doc, heads[0]); sync_goal(e, 1); ensure_visible(e, r); return; }
    {
        ExSel s = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        s.head = heads[0];
        ex_doc_set_selections(e->doc, &s, 1);
        sync_goal(e, 1);
        ensure_visible(e, r);
    }
}

/* ---- editing ---- */

typedef struct Range { size_t a, b; } Range;

static int cmp_range(const void *x, const void *y) {
    const Range *p = x, *q = y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    return p->b < q->b ? -1 : p->b > q->b;
}

static void delete_ranges(ExEditor *e, ExRenderer *r, Range *rg, size_t n) {
    size_t i, m = 0;
    ExChange *ch;
    if (n == 0) return;
    qsort(rg, n, sizeof *rg, cmp_range);
    for (i = 0; i < n; i++) { /* union of overlapping ranges */
        if (rg[i].a == rg[i].b) continue;
        if (m > 0 && rg[i].a <= rg[m - 1].b) { if (rg[i].b > rg[m - 1].b) rg[m - 1].b = rg[i].b; }
        else rg[m++] = rg[i];
    }
    if (m == 0) return;
    ch = malloc(m * sizeof *ch);
    if (!ch) return;
    for (i = 0; i < m; i++) { ch[i].offset = rg[i].a; ch[i].remove_len = rg[i].b - rg[i].a; ch[i].text = ""; ch[i].text_len = 0; }
    ex_doc_apply(e->doc, ch, m, 0, NULL, 0);
    free(ch);
    e->last_vertical = 0;
    ensure_visible(e, r);
}

static void backspace_or_delete(ExEditor *e, ExRenderer *r, int forward, int word) {
    size_t n = ex_doc_sel_count(e->doc), i;
    if (e->readonly) return;
    Range *rg = malloc((n ? n : 1) * sizeof *rg);
    if (!rg) return;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        if (s.anchor != s.head) { rg[i].a = lo(s); rg[i].b = hi(s); }
        else if (forward) { rg[i].a = s.head; rg[i].b = word ? word_move(e, s.head, 1) : next_boundary(e, s.head); }
        else { rg[i].b = s.head; rg[i].a = word ? word_move(e, s.head, -1) : prev_boundary(e, s.head); }
    }
    delete_ranges(e, r, rg, n);
    free(rg);
}

void ex_editor_text_input(ExEditor *e, const char *text, size_t len) {
    if (len == 0 || e->readonly) return;
    e->composition[0] = 0;
    ex_doc_replace_selections(e->doc, text, len, 1);
    e->last_vertical = 0;
}

void ex_editor_set_composition(ExEditor *e, const char *utf8) {
    size_t n = strlen(utf8);
    if (n >= sizeof e->composition) n = sizeof e->composition - 1;
    memcpy(e->composition, utf8, n);
    e->composition[n] = 0;
}

char *ex_editor_selected_text(ExEditor *e, size_t *len) {
    size_t n = ex_doc_sel_count(e->doc), i, total = 0, used = 0;
    char *out;
    for (i = 0; i < n; i++) { ExSel s = ex_doc_sel(e->doc, i); total += hi(s) - lo(s) + 1; }
    out = malloc(total + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        if (hi(s) == lo(s)) continue;
        if (used) out[used++] = '\n';
        used += ex_buffer_read(buf(e), lo(s), out + used, hi(s) - lo(s));
    }
    out[used] = 0;
    if (len) *len = used;
    return out;
}

static void paste(ExEditor *e) {
    char *text;
    if (e->readonly) return;
    text = e->clip.get ? e->clip.get(e->clip.user) : NULL;
    size_t n = ex_doc_sel_count(e->doc), lines = 1, i, len;
    if (!text) return;
    len = strlen(text);
    for (i = 0; i < len; i++) if (text[i] == '\n') lines++;
    if (lines == n && n > 1 && text[len - 1] != '\n') { /* one line for each cursor */
        ExChange *ch = malloc(n * sizeof *ch);
        char *p = text;
        if (ch) {
            for (i = 0; i < n; i++) {
                ExSel s = ex_doc_sel(e->doc, i);
                char *nl = strchr(p, '\n');
                ch[i].offset = lo(s);
                ch[i].remove_len = hi(s) - lo(s);
                ch[i].text = p;
                ch[i].text_len = nl ? (size_t)(nl - p) : strlen(p);
                p = nl ? nl + 1 : p + strlen(p);
            }
            ex_doc_apply(e->doc, ch, n, 0, NULL, 0);
            free(ch);
        }
    } else {
        ex_doc_replace_selections(e->doc, text, len, 0);
    }
    free(text);
}

static void add_cursor_vertical(ExEditor *e, ExRenderer *r, int dir) {
    size_t n = ex_doc_sel_count(e->doc), nl = line_count(e);
    ExSel s = ex_doc_sel(e->doc, dir < 0 ? 0 : n - 1);
    size_t line = ex_buffer_line_of_offset(buf(e), s.head), target;
    float x = x_of(e, r, line, s.head);
    if (dir < 0 ? line == 0 : line + 1 >= nl) return;
    target = dir < 0 ? line - 1 : line + 1;
    ex_doc_add_cursor(e->doc, offset_at_x(e, r, target, x));
    sync_goal(e, ex_doc_sel_count(e->doc));
    ensure_visible(e, r);
}

void ex_editor_key(ExEditor *e, ExRenderer *r, ExKey key, int mods) {
    int shift = (mods & EX_MOD_SHIFT) != 0, prim = (mods & EX_MOD_PRIMARY) != 0, alt = (mods & EX_MOD_ALT) != 0;
    if (!e->font) return;
    switch (key) {
    case EX_KEY_LEFT: move_horizontal(e, r, -1, prim || alt, shift); break;
    case EX_KEY_RIGHT: move_horizontal(e, r, 1, prim || alt, shift); break;
    case EX_KEY_UP:
        if (prim && alt) add_cursor_vertical(e, r, -1); else move_vertical(e, r, -1, shift);
        break;
    case EX_KEY_DOWN:
        if (prim && alt) add_cursor_vertical(e, r, 1); else move_vertical(e, r, 1, shift);
        break;
    case EX_KEY_PAGE_UP:
    case EX_KEY_PAGE_DOWN: {
        int lh = line_height_px(e, r), page = lh > 0 ? (int)(e->vh / (float)lh) - 1 : 10;
        if (page < 1) page = 1;
        move_vertical(e, r, key == EX_KEY_PAGE_UP ? -page : page, shift);
        break;
    }
    case EX_KEY_HOME: if (prim) move_doc_edge(e, r, 0, shift); else move_line_edge(e, r, 0, shift); break;
    case EX_KEY_END: if (prim) move_doc_edge(e, r, 1, shift); else move_line_edge(e, r, 1, shift); break;
    case EX_KEY_BACKSPACE: backspace_or_delete(e, r, 0, prim || alt); break;
    case EX_KEY_DELETE: backspace_or_delete(e, r, 1, prim || alt); break;
    case EX_KEY_ENTER: ex_editor_text_input(e, "\n", 1); ensure_visible(e, r); break;
    case EX_KEY_TAB: ex_editor_text_input(e, "\t", 1); break;
    case EX_KEY_SPACE: break; /* typed through text input */
    case EX_KEY_ESCAPE: {
        ExSel s = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        s.anchor = s.head;
        ex_doc_set_selections(e->doc, &s, 1);
        sync_goal(e, 1);
        break;
    }
    case EX_KEY_A: if (prim) { ExSel s; s.anchor = 0; s.head = ex_buffer_size(buf(e)); ex_doc_set_selections(e->doc, &s, 1); sync_goal(e, 1); } break;
    case EX_KEY_Z: if (prim && !e->readonly) { if (shift) ex_doc_redo(e->doc); else ex_doc_undo(e->doc); ensure_visible(e, r); } break;
    case EX_KEY_Y: if (prim && !e->readonly) { ex_doc_redo(e->doc); ensure_visible(e, r); } break;
    case EX_KEY_C:
    case EX_KEY_X:
        if (prim) {
            size_t len = 0;
            char *t = ex_editor_selected_text(e, &len);
            if (t && len > 0 && e->clip.set) {
                e->clip.set(e->clip.user, t);
                if (key == EX_KEY_X && !e->readonly) backspace_or_delete(e, r, 0, 0);
            }
            free(t);
        }
        break;
    case EX_KEY_V: if (prim) { paste(e); ensure_visible(e, r); } break;
    default: break;
    }
    sync_goal(e, ex_doc_sel_count(e->doc));
}

/* ---- mouse ---- */

static size_t point_to_offset(ExEditor *e, ExRenderer *r, float x, float y) {
    int lh = line_height_px(e, r);
    long row = (long)floorf((y - e->vy + e->scroll_y) / (float)(lh > 0 ? lh : 1));
    size_t rows;
    fold_sync(e);
    rows = row_count(e);
    if (row < 0) row = 0;
    if ((size_t)row >= rows) row = (long)rows - 1;
    return offset_at_x(e, r, line_of_row(e, (size_t)row), x - e->vx - (float)pad_px(r));
}

static void word_bounds(ExEditor *e, size_t off, size_t *a, size_t *b) {
    size_t s = off, t = off;
    for (;;) { size_t p = prev_boundary(e, s); char c[8]; size_t used = 0, got; uint32_t cp;
        if (p == s) break;
        got = ex_buffer_read(buf(e), p, c, sizeof c); cp = got ? ex_utf8_decode(c, got, &used) : ' ';
        if (!is_word_cp(cp)) break;
        s = p; }
    for (;;) { size_t nx = next_boundary(e, t); char c[8]; size_t used = 0, got; uint32_t cp;
        if (nx == t) break;
        got = ex_buffer_read(buf(e), t, c, sizeof c); cp = got ? ex_utf8_decode(c, got, &used) : ' ';
        if (!is_word_cp(cp)) break;
        t = nx; }
    if (s == t) { t = next_boundary(e, off); s = off; } /* not on a word: select the character */
    *a = s; *b = t;
}

void ex_editor_mouse(ExEditor *e, ExRenderer *r, ExMouse kind, float x, float y, int mods, int clicks) {
    size_t off;
    if (!e->font) return;
    if (kind == EX_MOUSE_DOWN && x >= e->vx && x < e->vx + (float)gutter_px(r) && e->mode != EX_EDITOR_PLAIN) {   /* the gutter: fold or open */
        size_t row = (size_t)floorf((y - e->vy + e->scroll_y) / (float)line_height_px(e, r)), line;
        fold_sync(e);
        if (y >= e->vy && row < row_count(e)) {
            line = line_of_row(e, row);
            if (ex_editor_toggle_fold_at_line(e, line)) return;
        }
    }
    off = point_to_offset(e, r, x, y);
    if (kind == EX_MOUSE_DOWN) {
        ExSel s;
        e->dragging = 1;
        e->drag_anchor = off;
        if (clicks >= 3) {
            size_t line = ex_buffer_line_of_offset(buf(e), off), st, en, nx;
            line_range(e, line, &st, &en, &nx);
            s.anchor = st; s.head = nx;
        } else if (clicks == 2) {
            word_bounds(e, off, &s.anchor, &s.head);
        } else if (mods & EX_MOD_SHIFT) {
            ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
            s.anchor = p.anchor; s.head = off;
        } else {
            s.anchor = s.head = off;
        }
        if (mods & EX_MOD_ALT) { /* add a cursor, keep the others */
            size_t n = ex_doc_sel_count(e->doc), i;
            ExSel *all = malloc((n + 1) * sizeof *all);
            if (all) {
                for (i = 0; i < n; i++) all[i] = ex_doc_sel(e->doc, i);
                all[n] = s;
                ex_doc_set_selections(e->doc, all, n + 1);
                free(all);
            }
        } else {
            ex_doc_set_selections(e->doc, &s, 1);
        }
        sync_goal(e, ex_doc_sel_count(e->doc));
    } else if (kind == EX_MOUSE_DRAG && e->dragging) {
        size_t n = ex_doc_sel_count(e->doc), i;
        ExSel *all = malloc(n * sizeof *all);
        if (all) {
            for (i = 0; i < n; i++) all[i] = ex_doc_sel(e->doc, i);
            all[ex_doc_primary(e->doc)].anchor = e->drag_anchor;
            all[ex_doc_primary(e->doc)].head = off;
            ex_doc_set_selections(e->doc, all, n);
            free(all);
        }
    } else if (kind == EX_MOUSE_UP) {
        e->dragging = 0;
    }
    ensure_visible(e, r);
}

void ex_editor_get_state(const ExEditor *e, int *line, int *column, float *scroll_y) {
    ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    size_t l = ex_buffer_line_of_offset(ex_doc_buffer(e->doc), p.head);
    if (line) *line = (int)l;
    if (column) *column = (int)(p.head - ex_buffer_line_start(ex_doc_buffer(e->doc), l));
    if (scroll_y) *scroll_y = e->scroll_y;
}

void ex_editor_set_state(ExEditor *e, int line, int column, float scroll_y) {
    const ExBuffer *b = ex_doc_buffer(e->doc);
    size_t lines = ex_buffer_line_count(b), l = line < 0 ? 0 : (size_t)line, start, end, off;
    if (l >= lines) l = lines - 1;
    start = ex_buffer_line_start(b, l);
    end = l + 1 < lines ? ex_buffer_line_start(b, l + 1) : ex_buffer_size(b);
    off = start + (column < 0 ? 0 : (size_t)column);
    if (off > end) off = end;
    /* never between the bytes of one character: step back to the start of the grapheme */
    {
        size_t len, cand;
        char *t = line_text(e, l, &len);
        if (t) {
            size_t rel = off - start;
            if (rel > len) rel = len;
            cand = 0;
            while (cand < len) { size_t nx = ex_grapheme_next(t, len, cand); if (nx > rel || nx <= cand) break; cand = nx; }
            off = start + (rel == len ? len : cand);
            free(t);
        }
    }
    ex_doc_set_caret(e->doc, off);
    e->scroll_y = scroll_y < 0 ? 0 : scroll_y;
}

void ex_editor_drop_text(ExEditor *e, ExRenderer *r, float x, float y, const char *utf8, size_t len) {
    if (e->readonly || !utf8 || len == 0) return;
    ex_editor_mouse(e, r, EX_MOUSE_DOWN, x, y, 0, 1);
    ex_editor_mouse(e, r, EX_MOUSE_UP, x, y, 0, 1);
    ex_editor_text_input(e, utf8, len);
}

void ex_editor_scroll(ExEditor *e, float dy) {
    e->scroll_y += dy;
    if (e->scroll_y < 0) e->scroll_y = 0;
}

/* ---- drawing ---- */

void ex_editor_caret_rect(ExEditor *e, ExRenderer *r, float *x, float *y, float *w, float *h) {
    ExSel p;
    size_t line;
    int lh = line_height_px(e, r);
    float cx;
    fold_sync(e);
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    line = ex_buffer_line_of_offset(buf(e), p.head);
    cx = e->vx + (float)pad_px(r) + x_of(e, r, line, p.head);
    if (x) *x = cx;
    if (y) *y = e->vy + (float)row_of_line(e, line) * (float)lh - e->scroll_y;
    if (w) *w = (float)ex_px(r, 1.5f);
    if (h) *h = (float)lh;
}

void ex_editor_set_mode(ExEditor *e, ExEditorMode mode) { e->mode = mode; e->md_valid = 0; clear_cache(e); }
ExEditorMode ex_editor_mode(const ExEditor *e) { return e->mode; }

size_t ex_editor_line_display(ExEditor *e, ExRenderer *r, size_t line, char *out, size_t cap) {
    size_t n = 0, len, s, en, nx;
    LLine *l = layout_of(e, r, line, NULL, NULL);
    if (l) { n = ll_display(l, out, cap); ll_free(l); return n; }
    {
        char *t = line_text(e, line, &len);
        if (!t) { if (cap) out[0] = 0; return 0; }
        line_range(e, line, &s, &en, &nx);
        n = len < cap ? len : (cap ? cap - 1 : 0);
        if (cap) { memcpy(out, t, n); out[n] = 0; }
        free(t);
        return n;
    }
}

const struct ExMd *ex_editor_tokens(ExEditor *e) {
    if (e->mode == EX_EDITOR_PLAIN) return NULL;
    ensure_md(e);
    return e->md;
}

/* Draws one line in source mode or live preview from its layout. */
static void draw_layout_line(ExEditor *e, ExRenderer *r, const ExTheme *t, size_t line, size_t nl, float y, int lh, const ExFontMetrics *fm,
                             int focused, int blink_on, ExColor sel_c, ExColor cur_c, ExColor comp_c) {
    size_t st, en, nx, si, nsel = ex_doc_sel_count(e->doc), i;
    LLine *ll = layout_of(e, r, line, &st, &en);
    float left = e->vx + (float)pad_px(r);
    int base_ascent = fm->ascent;
    if (!ll) return;
    line_range(e, line, &st, &en, &nx);
    if (ll->callout) {                                   /* a tint behind a callout's lines */
        ExColor c = ex_theme_color(t, (ExColorToken)(EX_COLOR_CALLOUT_NOTE + (ll->callout - 1 < EX_CALLOUT_COUNT ? ll->callout - 1 : 0)));
        c.a = 34;
        ex_fill_rect(r, e->vx, y, e->vw, (float)lh, c);
    } else if (ll->code) {
        ex_fill_rect(r, e->vx, y, e->vw, (float)lh, ex_theme_color(t, EX_COLOR_CODE_BACKGROUND));
    }
    /* selections behind the text */
    for (si = 0; si < nsel; si++) {
        ExSel s = ex_doc_sel(e->doc, si);
        size_t a = lo(s), b = hi(s), la, lb;
        float x0, x1;
        if (a == b || b <= st || (a > en && a >= nx)) continue;
        la = a > st ? a : st;
        lb = b < en ? b : en;
        x0 = ll_x_of(ll, la);
        x1 = ll_x_of(ll, lb);
        if (x1 < x0) { float tmp = x0; x0 = x1; x1 = tmp; }
        if (b > en && line + 1 < nl) x1 += (float)ex_px(r, 6);
        ex_fill_rect(r, left + x0, y, x1 - x0, (float)lh, sel_c);
    }
    if (ll->rule) {
        ExColor c = ex_theme_color(t, EX_COLOR_BORDER);
        ex_fill_rect(r, left, y + (float)lh / 2.0f, e->vw - (float)pad_px(r) - (float)ex_px(r, 8), (float)ex_px(r, 1.5f), c);
    }
    for (i = 0; i < ll->n; i++) {
        LRun *run = &ll->runs[i];
        float x = left + run->x;
        if (run->kind == LRUN_HIDDEN) continue;
        if (run->bg >= 0 && run->w > 0) ex_fill_rect(r, x, y, run->w, (float)lh, ex_theme_color(t, (ExColorToken)run->bg));
        if (run->kind == LRUN_TEXT || run->kind == LRUN_SUBST) {
            ExFontMetrics m;
            ExColor col = ex_theme_color(t, (ExColorToken)run->fg);
            float ty = y + (float)(lh - fm->line_height) / 2.0f;
            if (ex_text_metrics(r, run->font, run->size, &m) == 0) ty += (float)(base_ascent - m.ascent);
            ex_text_draw(r, run->font, run->size, x, ty, run->text, run->tlen, col);
            if (run->flags & LF_UNDERLINE) ex_fill_rect(r, x, y + (float)(lh + fm->line_height) / 2.0f - (float)ex_px(r, 1), run->w, (float)ex_px(r, 1), col);
            if (run->flags & LF_STRIKE) ex_fill_rect(r, x, y + (float)lh / 2.0f, run->w, (float)ex_px(r, 1), col);
        } else if (run->kind == LRUN_BAR) {
            ex_fill_rect(r, x + run->w * 0.3f, y, (float)ex_px(r, 2), (float)lh, ex_theme_color(t, (ExColorToken)run->fg));
        } else if (run->kind == LRUN_CHECKBOX) {
            float side = (float)fm->line_height * 0.7f, bx = x + (run->w - side) / 2.0f, by = y + ((float)lh - side) / 2.0f, th = (float)ex_px(r, 1.5f);
            ExColor col = ex_theme_color(t, (ExColorToken)run->fg);
            ex_fill_rect(r, bx, by, side, th, col);
            ex_fill_rect(r, bx, by + side - th, side, th, col);
            ex_fill_rect(r, bx, by, th, side, col);
            ex_fill_rect(r, bx + side - th, by, th, side, col);
            if (run->checked) ex_fill_rect(r, bx + side * 0.25f, by + side * 0.25f, side * 0.5f, side * 0.5f, col);
        }
    }
    /* carets and the IME composition */
    for (si = 0; si < nsel; si++) {
        ExSel s = ex_doc_sel(e->doc, si);
        float cx;
        if (s.head < st || s.head > en || !(s.head < nx || line + 1 == nl)) continue;
        cx = left + ll_x_of(ll, s.head);
        if (si == ex_doc_primary(e->doc) && e->composition[0]) {
            float ty = y + (float)(lh - fm->line_height) / 2.0f;
            float w = ex_text_draw(r, e->font, e->size, cx, ty, e->composition, strlen(e->composition), comp_c);
            ex_fill_rect(r, cx, y + (float)lh - (float)ex_px(r, 2), w, (float)ex_px(r, 1), comp_c);
            cx += w;
        }
        if (focused && blink_on) ex_fill_rect(r, cx, y, (float)ex_px(r, 1.5f), (float)lh, cur_c);
    }
    ll_free(ll);
}

/* The fold marker of a header line: a triangle in the gutter (pointing right when folded, down when open; open ones only
 * show on the caret's line), and three dots after the text of a folded header. */
static void draw_fold_marker(ExEditor *e, ExRenderer *r, const ExTheme *t, size_t line, float y, int lh, int caret_line) {
    long k = ex_folds_find_hdr(&e->folds, line);
    int folded;
    float g = (float)gutter_px(r), side = g * 0.5f, cx, cy;
    ExColor c;
    if (k < 0) return;
    folded = find_folded(e, e->folds.r[k].hs) >= 0;
    if (!folded && !caret_line) return;
    c = ex_theme_color(t, folded ? EX_COLOR_TEXT : EX_COLOR_TEXT_MUTED);
    cx = e->vx + g * 0.5f;
    cy = y + (float)lh / 2.0f;
    if (folded) ex_fill_triangle(r, cx - side * 0.35f, cy - side * 0.5f, cx - side * 0.35f, cy + side * 0.5f, cx + side * 0.5f, cy, c);
    else ex_fill_triangle(r, cx - side * 0.5f, cy - side * 0.3f, cx + side * 0.5f, cy - side * 0.3f, cx, cy + side * 0.4f, c);
    if (folded) {
        size_t st, en, nx;
        float x;
        ExFontMetrics fm;
        line_range(e, line, &st, &en, &nx);
        x = e->vx + (float)pad_px(r) + x_of(e, r, line, en) + (float)ex_px(r, 6);
        if (ex_font_metrics(e->font, line_px(e, r), &fm) == 0)
            ex_text_draw(r, e->font, e->size, x, y + (float)(lh - fm.line_height) / 2.0f, "...", 3, ex_theme_color(t, EX_COLOR_TEXT_MUTED));
    }
}

void ex_editor_draw(ExEditor *e, ExRenderer *r, const ExTheme *t, int focused, int blink_on) {
    int lh, px, pad;
    size_t nl = line_count(e), first, last, line, row, rows, caret_line, nsel = ex_doc_sel_count(e->doc), si;
    ExFontMetrics fm;
    ExColor text = ex_theme_color(t, EX_COLOR_TEXT), sel_c = ex_theme_color(t, EX_COLOR_SELECTION);
    ExColor cur_c = ex_theme_color(t, EX_COLOR_CURSOR), comp_c = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    if (!e->font) return;
    lh = line_height_px(e, r);
    px = line_px(e, r);
    pad = pad_px(r);
    if (ex_font_metrics(e->font, px, &fm)) return;
    ex_set_clip(r, (int)e->vx, (int)e->vy, (int)e->vw, (int)e->vh);
    fold_sync(e);
    rows = row_count(e);
    first = (size_t)(e->scroll_y / (float)lh);
    last = first + (size_t)(e->vh / (float)lh) + 1;
    if (last >= rows) last = rows - 1;
    caret_line = ex_buffer_line_of_offset(buf(e), ex_doc_sel(e->doc, ex_doc_primary(e->doc)).head);
    for (row = first; row <= last && row < rows; row++) {
        size_t len, st, en, nx;
        char *txt;
        const ExShaped *sh;
        float y = e->vy + (float)row * (float)lh - e->scroll_y;
        float ty = y + (float)(lh - fm.line_height) / 2.0f;
        line = line_of_row(e, row);
        if (e->mode != EX_EDITOR_PLAIN) { draw_layout_line(e, r, t, line, nl, y, lh, &fm, focused, blink_on, sel_c, cur_c, comp_c); draw_fold_marker(e, r, t, line, y, lh, line == caret_line); continue; }
        txt = line_text(e, line, &len);
        if (!txt) continue;
        line_range(e, line, &st, &en, &nx);
        sh = shaped(e, r, txt, len);
        /* selections behind the text */
        for (si = 0; si < nsel; si++) {
            ExSel s = ex_doc_sel(e->doc, si);
            size_t a = lo(s), b = hi(s), la, lb;
            float x0, x1;
            if (a == b || b <= st || (a > en && a >= nx)) continue;
            la = a > st ? a : st;
            lb = b < en ? b : en;
            x0 = sh ? ex_shaped_x_for_offset(sh, la - st, len) : 0;
            x1 = sh ? ex_shaped_x_for_offset(sh, lb - st, len) : 0;
            if (x1 < x0) { float tmp = x0; x0 = x1; x1 = tmp; }
            if (b > en && line + 1 < nl) x1 += (float)ex_px(r, 6); /* the selected line break */
            ex_fill_rect(r, e->vx + (float)pad + x0, y, x1 - x0, (float)lh, sel_c);
        }
        ex_text_draw(r, e->font, e->size, e->vx + (float)pad, ty, txt, len, text);
        /* carets and the IME composition */
        for (si = 0; si < nsel; si++) {
            ExSel s = ex_doc_sel(e->doc, si);
            float cx;
            if (s.head < st || s.head > en || !(s.head < nx || line + 1 == nl)) continue;
            cx = e->vx + (float)pad + (sh ? ex_shaped_x_for_offset(sh, s.head - st, len) : 0.0f);
            if (si == ex_doc_primary(e->doc) && e->composition[0]) {
                float w = ex_text_draw(r, e->font, e->size, cx, ty, e->composition, strlen(e->composition), comp_c);
                ex_fill_rect(r, cx, y + (float)lh - (float)ex_px(r, 2), w, (float)ex_px(r, 1), comp_c);
                cx += w;
            }
            if (focused && blink_on) ex_fill_rect(r, cx, y, (float)ex_px(r, 1.5f), (float)lh, cur_c);
        }
        free(txt);
    }
    ex_set_clip(r, 0, 0, -1, -1);
}
