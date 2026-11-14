#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hb.h>

#include "editor_fold.h"
#include "editor_layout.h"
#include "exhaust_editor.h"
#include "exhaust_find.h"
#include "exhaust_markdown.h"
#include "exhaust_platform.h"
#include "exhaust_slash.h"
#include "exhaust_spell.h"
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
    ExFormatOptions fmt;
    ExSlashMenu *slash;           /* not owned */
    int slash_open;
    size_t slash_start;           /* offset of the "/" that opened the menu */
    ExLinkSuggestFn link_fn; void *link_user;
    ExEditorLinkClickFn link_click; void *link_click_user;
    int link_open, link_md; size_t link_start;       /* the offset after the "[[" that opened the list */
    ExLinkSuggestion link_items[8]; size_t link_n, link_sel;
    float sp_x, sp_y, sp_w, sp_rowh;   /* the popup as last drawn, for clicks */
    size_t sp_first, sp_rows;
    ExSpell *spell;               /* not owned */
    struct { unsigned long h; char w[32]; signed char ok; } scache[256];   /* results for words seen lately: 1 correct, 0 wrong */
    struct ExFind *find;         /* matches to highlight (not owned) */
    struct { size_t s, e; } *hl;  /* the matches in the lines drawn last */
    size_t nhl, caphl;
    int rect;                    /* a rectangular selection is being dragged from (rect_x, rect_y) */
    float rect_x, rect_y;
    struct { size_t first, last; } *hid;
    size_t nhid;
    /* soft wrap: a long line is shown on several rows, cut where the viewport is full */
    int wrap;
    uint32_t *nseg; size_t wn;   /* the rows each line takes; 0: not measured yet (an estimate stands in) */
    size_t *vpre; int vpre_ok;   /* vpre[i]: the first row of line i (a hidden line takes none), vpre[wn]: all rows */
    const ExDoc *wrap_doc; unsigned long wrap_ver; float wrap_w; int wrap_px; uint32_t wrap_font; ExEditorMode wrap_mode;
    size_t wrap_caret;
    float avgw;                  /* an average character width, for lines not measured yet */
    size_t last_rows;            /* the rows at the last measure (for the owner that sizes the editor to its text) */
};

static void wrap_free_all(struct ExEditor *e);

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
    e->fmt.use_tabs = 1;
    e->fmt.indent_width = 4;
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
    free(e->nseg); free(e->vpre);
    free(e->hl);
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
    wrap_free_all(e);
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
    e->vpre_ok = 0;
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

/* ---- soft wrap ----
 * A line is laid out once, as a single long row (live preview or plain). Wrapping cuts that row into segments at blanks (or at a
 * cluster when there is no blank) so that each fits the viewport; a segment is drawn by sliding the row to its start and clipping,
 * so carets, selections and clicks use the line's own x positions minus the x where the segment starts. Rows are numbered from the
 * top: a line is first row vpre[line], and takes nseg[line] rows. Lines far from the screen are only estimated until they are
 * measured (the view keeps its place when they are). */

typedef struct WrapLine { size_t *start; float *x; size_t n, cap; } WrapLine;   /* segment starts (offsets) and their x in the row */

static int wrap_active(const ExEditor *e) { return e->wrap && e->vw > 0.0f && e->font != NULL; }

static float wrap_avail(const ExEditor *e, ExRenderer *r) {
    float a = e->vw - (float)pad_px(r) - (float)ex_px(r, 8), lo = 1.0f;       /* down to one character a row: a segment always takes at least one cluster (wrap_line) */
    return a < lo ? lo : a;
}

typedef struct Geom { LLine *ll; const ExShaped *sh; char *t; size_t s, en, len; } Geom;

static int geom_open(ExEditor *e, ExRenderer *r, size_t line, Geom *g) {
    size_t nx;
    memset(g, 0, sizeof *g);
    line_range(e, line, &g->s, &g->en, &nx);
    g->t = line_text(e, line, &g->len);
    if (!g->t) return -1;
    if (e->mode != EX_EDITOR_PLAIN) g->ll = layout_of(e, r, line, NULL, NULL);
    else g->sh = shaped(e, r, g->t, g->len);
    if (!g->ll && !g->sh) { free(g->t); g->t = NULL; return -1; }
    return 0;
}
static void geom_close(Geom *g) { if (g->ll) ll_free(g->ll); free(g->t); }
static float geom_x(const Geom *g, size_t off) {
    if (off < g->s) off = g->s;
    if (off > g->en) off = g->en;
    return g->ll ? ll_x_of(g->ll, off) : ex_shaped_x_for_offset(g->sh, off - g->s, g->len);
}
static size_t geom_off(const Geom *g, float x) {
    return g->ll ? ll_offset_at(g->ll, x, g->s, g->en) : g->s + ex_shaped_offset_for_x(g->sh, x, g->len);
}

static void wl_free(WrapLine *w) { free(w->start); free(w->x); memset(w, 0, sizeof *w); }
static int wl_push(WrapLine *w, size_t s, float x) {
    if (w->n == w->cap) {
        size_t nc = w->cap ? w->cap * 2 : 4, *a = realloc(w->start, nc * sizeof *a);
        float *b;
        if (!a) return -1;
        w->start = a;
        b = realloc(w->x, nc * sizeof *b);
        if (!b) return -1;
        w->x = b; w->cap = nc;
    }
    w->start[w->n] = s; w->x[w->n] = x; w->n++;
    return 0;
}

/* The segments of one line. Always at least one (the whole line when wrapping is off). */
static void wrap_line(ExEditor *e, ExRenderer *r, size_t line, WrapLine *w) {
    Geom g;
    float avail, x0 = 0.0f;
    size_t cur, guard;
    memset(w, 0, sizeof *w);
    if (!wrap_active(e) || geom_open(e, r, line, &g) != 0) { size_t st, en, nx; line_range(e, line, &st, &en, &nx); wl_push(w, st, 0.0f); return; }
    avail = wrap_avail(e, r);
    cur = g.s;
    wl_push(w, cur, 0.0f);
    for (guard = 0; guard < 200000; guard++) {
        float limit = x0 + avail;
        size_t o, b, k;
        if (geom_x(&g, g.en) <= limit + 0.5f) break;
        o = geom_off(&g, limit);
        b = o > g.en ? g.en : o;
        k = b;
        while (k > cur && g.t[k - 1 - g.s] != ' ' && g.t[k - 1 - g.s] != '\t') k--;
        if (k > cur) b = k;                                           /* after the last blank that fits */
        else while (b > cur + 1 && geom_x(&g, b) - x0 > avail + 0.5f) b = g.s + ex_grapheme_prev(g.t, g.len, b - g.s);   /* no blank: the last cluster that fits */
        if (b >= g.en) b = g.s + ex_grapheme_prev(g.t, g.len, g.en - g.s);
        if (b <= cur) b = g.s + ex_grapheme_next(g.t, g.len, cur - g.s);
        if (b >= g.en || b <= cur) break;
        if (wl_push(w, b, geom_x(&g, b)) != 0) break;
        cur = b;
        x0 = geom_x(&g, b);
    }
    geom_close(&g);
}

static size_t est_rows(const ExEditor *e, size_t line) {
    size_t s = ex_buffer_line_start(buf(e), line), n = ex_buffer_line_start(buf(e), line + 1), len = n > s ? n - s : 0, rows;
    float w = (float)len * e->avgw * 1.04f;
    rows = (size_t)ceilf(w / (e->wrap_w > 1.0f ? e->wrap_w : 1.0f));
    return rows < 1 ? 1 : rows;
}

static void wrap_free_all(ExEditor *e) { free(e->nseg); free(e->vpre); e->nseg = NULL; e->vpre = NULL; e->wn = 0; e->vpre_ok = 0; }

static void wrap_reset(ExEditor *e, size_t n) {
    free(e->nseg); free(e->vpre);
    e->nseg = calloc(n + 1, sizeof *e->nseg);
    e->vpre = calloc(n + 2, sizeof *e->vpre);
    e->wn = e->nseg && e->vpre ? n : 0;
    e->vpre_ok = 0;
}

static void wrap_prefix(ExEditor *e) {
    size_t i, j = 0, acc = 0;
    for (i = 0; i < e->wn; i++) {
        int hidden;
        e->vpre[i] = acc;
        while (j < e->nhid && e->hid[j].last < i) j++;
        hidden = j < e->nhid && e->hid[j].first <= i;
        if (!hidden) acc += e->nseg[i] ? e->nseg[i] : (uint32_t)est_rows(e, i);
    }
    e->vpre[e->wn] = acc;
    e->vpre_ok = 1;
}

/* The lines changed by the edits since the last look: the rows of the lines after them stay as they were. */
static void wrap_remap(ExEditor *e, size_t n, unsigned long ver) {
    ExDocEdit edits[EX_DOC_EDIT_LOG];
    int k = ex_doc_edits_since(e->doc, e->wrap_ver, edits, EX_DOC_EDIT_LOG);
    size_t i, lo_off = 0, hi_old = 0, hi_new = 0, old_n = e->wn, size = ex_buffer_size(buf(e)), l0, l1_new, l1_old, tail;
    uint32_t *fresh;
    if (k == 0) return;
    if (k < 0 || old_n == 0) { wrap_reset(e, n); return; }
    for (i = 0; i < (size_t)k; i++) {
        size_t o = edits[i].offset, rm = edits[i].removed, ins = edits[i].inserted;
        if (i == 0) { lo_off = o; hi_old = o + rm; hi_new = o + ins; }
        else {
            size_t nlo = o < lo_off ? o : lo_off, cur_end = o + rm, nhi = cur_end > hi_new ? cur_end : hi_new;
            hi_old += nhi - hi_new;
            lo_off = nlo;
            hi_new = nhi - rm + ins;
        }
    }
    (void)hi_old;
    l0 = ex_buffer_line_of_offset(buf(e), lo_off > size ? size : lo_off);
    l1_new = ex_buffer_line_of_offset(buf(e), hi_new > size ? size : hi_new);
    {   /* the same lines in the old numbering: the tail after them is unchanged */
        long lo_old = (long)l1_new + (long)old_n - (long)n;
        if (l1_new + 1 > n || lo_old < (long)l0 || lo_old >= (long)old_n) { wrap_reset(e, n); return; }
        l1_old = (size_t)lo_old;
    }
    tail = n - (l1_new + 1);
    if (tail != old_n - (l1_old + 1)) { wrap_reset(e, n); return; }
    fresh = calloc(n + 1, sizeof *fresh);
    if (!fresh) { wrap_reset(e, n); return; }
    memcpy(fresh, e->nseg, l0 * sizeof *fresh);                                  /* before: as they were; l0..l1_new: measured again */
    memcpy(fresh + l1_new + 1, e->nseg + l1_old + 1, tail * sizeof *fresh);
    free(e->nseg);
    e->nseg = fresh;
    free(e->vpre);
    e->vpre = calloc(n + 2, sizeof *e->vpre);
    e->wn = e->vpre ? n : 0;
    e->vpre_ok = 0;
    (void)ver;
}

static size_t vrow_total(ExEditor *e) { return wrap_active(e) && e->vpre ? e->vpre[e->wn] : row_count(e); }
static size_t vrow_of_line(ExEditor *e, size_t line) { return wrap_active(e) && e->vpre && line <= e->wn ? e->vpre[line] : row_of_line(e, line); }

/* The line that has the row, and which of its rows it is. */
static size_t line_of_vrow(ExEditor *e, size_t vrow, size_t *seg) {
    size_t lo, hi, total;
    if (!wrap_active(e) || !e->vpre || e->wn == 0) { if (seg) *seg = 0; return line_of_row(e, vrow); }
    total = e->vpre[e->wn];
    if (total == 0) { if (seg) *seg = 0; return 0; }
    if (vrow >= total) vrow = total - 1;
    lo = 0; hi = e->wn;                                  /* the first line whose rows end after vrow */
    while (lo + 1 < hi) { size_t mid = (lo + hi) / 2; if (e->vpre[mid] <= vrow) lo = mid; else hi = mid; }
    while (lo + 1 < e->wn && e->vpre[lo + 1] <= vrow) lo++;       /* skip the hidden lines that share the number */
    if (seg) *seg = vrow - e->vpre[lo];
    return lo;
}

/* Measures what the screen needs, and keeps the place the view is at when the measures change the rows above it. */
static void wrap_sync(ExEditor *e, ExRenderer *r) {
    size_t n, i, pass;
    float avail;
    int px, lh;
    unsigned long ver;
    if (!wrap_active(e)) return;
    n = line_count(e);
    avail = wrap_avail(e, r); px = line_px(e, r); ver = ex_doc_version(e->doc);
    if (e->avgw <= 0.0f || e->wrap_px != px) {
        static const char sample[] = "The quick brown fox jumps over the lazy dog";
        e->avgw = ex_text_measure(r, e->font, e->size, sample, sizeof sample - 1) / (float)(sizeof sample - 1);
        if (e->avgw <= 0.0f) e->avgw = (float)px * 0.5f;
    }
    if (!e->nseg || e->wn == 0 || e->wrap_doc != e->doc || e->wrap_w != avail || e->wrap_px != px || e->wrap_font != ex_font_id(e->font) || e->wrap_mode != e->mode) {
        wrap_reset(e, n);
    } else if (ver != e->wrap_ver) wrap_remap(e, n, ver);
    if (e->wn != n) wrap_reset(e, n);
    if (!e->nseg) return;
    e->wrap_doc = e->doc; e->wrap_ver = ver; e->wrap_w = avail; e->wrap_px = px; e->wrap_font = ex_font_id(e->font); e->wrap_mode = e->mode;
    lh = line_height_px(e, r);
    {
        size_t caret = ex_buffer_line_of_offset(buf(e), ex_doc_sel(e->doc, ex_doc_primary(e->doc)).head);
        if (e->mode == EX_EDITOR_LIVE) { if (caret < e->wn) e->nseg[caret] = 0; if (e->wrap_caret < e->wn) e->nseg[e->wrap_caret] = 0; }   /* the syntax shows where the caret is */
        e->wrap_caret = caret;
    }
    for (pass = 0; pass < 3; pass++) {
        size_t top, bottom, l0, l1, anchor, changed = 0;
        float within;
        if (!e->vpre_ok) wrap_prefix(e);
        top = (size_t)(e->scroll_y > 0 ? e->scroll_y / (float)lh : 0);
        bottom = (size_t)((e->scroll_y + e->vh) / (float)lh) + 1;
        l0 = line_of_vrow(e, top, NULL);
        l1 = line_of_vrow(e, bottom, NULL);
        anchor = l0;
        within = e->scroll_y - (float)e->vpre[anchor] * (float)lh;
        l0 = l0 > 30 ? l0 - 30 : 0;
        l1 = l1 + 30 < e->wn ? l1 + 30 : e->wn - 1;
        {   /* the caret line is measured too: the view follows the caret */
            size_t caret = e->wrap_caret;
            if (caret < e->wn && e->nseg[caret] == 0) { WrapLine w; wrap_line(e, r, caret, &w); e->nseg[caret] = (uint32_t)w.n; wl_free(&w); changed++; }
        }
        for (i = l0; i <= l1 && i < e->wn; i++) {
            WrapLine w;
            if (e->nseg[i] || line_hidden(e, i)) continue;
            wrap_line(e, r, i, &w);
            e->nseg[i] = (uint32_t)w.n;
            wl_free(&w);
            changed++;
        }
        if (!changed) break;
        wrap_prefix(e);
        e->scroll_y = (float)e->vpre[anchor] * (float)lh + within;
        if (e->scroll_y < 0) e->scroll_y = 0;
    }
    e->last_rows = e->vpre_ok ? e->vpre[e->wn] : 0;
}

/* Rows geometry for the places that work in rows: call before using vrow_*. */
static void geometry(ExEditor *e, ExRenderer *r) { fold_sync(e); wrap_sync(e, r); }

/* Where the row of an offset is: the line, which of its segments, and the segments. The caller frees w. */
static size_t seg_of(ExEditor *e, ExRenderer *r, size_t line, size_t off, WrapLine *w) {
    size_t k = 0;
    wrap_line(e, r, line, w);
    while (k + 1 < w->n && off >= w->start[k + 1]) k++;
    return k;
}

/* The offset in a row for an x measured from the left edge of the text; it stays inside the row. */
static size_t offset_in_row(ExEditor *e, ExRenderer *r, size_t line, const WrapLine *w, size_t seg, float x) {
    size_t off = offset_at_x(e, r, line, x + w->x[seg]);
    if (off < w->start[seg]) off = w->start[seg];
    if (seg + 1 < w->n && off >= w->start[seg + 1]) off = prev_boundary(e, w->start[seg + 1]);
    return off;
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
    size_t line, seg = 0;
    int lh;
    float top, bottom;
    if (e->vh <= 0 || !e->font) return;
    geometry(e, r);
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    line = ex_buffer_line_of_offset(buf(e), p.head);
    lh = line_height_px(e, r);
    if (wrap_active(e)) { WrapLine w; seg = seg_of(e, r, line, p.head, &w); wl_free(&w); }
    top = (float)(vrow_of_line(e, line) + seg) * (float)lh;
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
    geometry(e, r);
    rows = vrow_total(e);
    sync_goal(e, n);
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), seg, tseg, target;
        WrapLine w, tw;
        float x;
        long t;
        seg = seg_of(e, r, line, s.head, &w);
        x = (e->last_vertical && e->goal && e->goal[i] >= 0) ? e->goal[i] : x_of(e, r, line, s.head) - w.x[seg];
        t = (long)(vrow_of_line(e, line) + seg) + lines;                 /* moves by rows on the screen: hidden lines are skipped, wrapped lines count each row */
        wl_free(&w);
        if (e->goal) e->goal[i] = x;
        if (t < 0) { heads[i] = 0; if (e->goal) e->goal[i] = -1; continue; }
        if ((size_t)t >= rows) {
            size_t last = line_of_vrow(e, rows - 1, NULL), st, en, nx;
            if (last + 1 >= line_count(e)) heads[i] = ex_buffer_size(buf(e));
            else { line_range(e, last, &st, &en, &nx); heads[i] = en; }
            if (e->goal) e->goal[i] = -1;
            continue;
        }
        target = line_of_vrow(e, (size_t)t, &tseg);
        wrap_line(e, r, target, &tw);
        if (tseg >= tw.n) tseg = tw.n - 1;
        heads[i] = offset_in_row(e, r, target, &tw, tseg, x);
        wl_free(&tw);
    }
    set_heads(e, r, heads, shift, 1);
    free(heads);
}

static void move_line_edge(ExEditor *e, ExRenderer *r, int to_end, int shift) {
    size_t n = ex_doc_sel_count(e->doc), i;
    size_t *heads = malloc((n ? n : 1) * sizeof *heads);
    if (!heads) return;
    geometry(e, r);
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), st, en, nx, seg = 0;
        WrapLine w;
        line_range(e, line, &st, &en, &nx);
        seg = seg_of(e, r, line, s.head, &w);                          /* a wrapped line has an end and a start for each row */
        if (to_end) heads[i] = seg + 1 < w.n ? prev_boundary(e, w.start[seg + 1]) : en;
        else heads[i] = w.start[seg];
        wl_free(&w);
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

/* ---- link suggestions ---- */

void ex_editor_set_link_click(ExEditor *e, ExEditorLinkClickFn fn, void *user) { e->link_click = fn; e->link_click_user = user; }

void ex_editor_set_link_suggest(ExEditor *e, ExLinkSuggestFn fn, void *user) {
    if (e->link_fn == fn && e->link_user == user) return;       /* called every frame: it must not close an open list */
    e->link_fn = fn; e->link_user = user; e->link_open = 0;
}
int ex_editor_link_suggest_open(const ExEditor *e) { return e->link_fn && e->link_open; }

static void link_refresh(ExEditor *e) {
    ExSel p;
    char q[258];
    size_t n, off = 0;
    if (!e->link_fn || !e->link_open) return;
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    if (ex_doc_sel_count(e->doc) != 1 || p.anchor != p.head || p.head < e->link_start || p.head - e->link_start >= sizeof q) { e->link_open = 0; return; }
    if (e->link_md) q[off++] = '\x02';
    n = ex_buffer_read(buf(e), e->link_start, q + off, p.head - e->link_start);
    q[off + n] = 0;
    if (strpbrk(q + off, e->link_md ? ")\r\n" : "]|\r\n")) { e->link_open = 0; return; }
    e->link_n = e->link_fn(e->link_user, q, e->link_items, 8);
    if (e->link_sel >= e->link_n) e->link_sel = 0;
}

static void link_accept(ExEditor *e, ExRenderer *r) {
    ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    ExChange c;
    ExSel after;
    char ahead[2] = {0, 0}, text[260];
    size_t k;
    if (e->link_sel >= e->link_n) { e->link_open = 0; return; }
    k = ex_buffer_read(buf(e), p.head, ahead, 2);
    if (e->link_md) snprintf(text, sizeof text, "%s%s", e->link_items[e->link_sel].insert, (k >= 1 && ahead[0] == ')') ? "" : ")");
    else snprintf(text, sizeof text, "%s%s", e->link_items[e->link_sel].insert, (k == 2 && ahead[0] == ']' && ahead[1] == ']') ? "" : "]]");
    c.offset = e->link_start; c.remove_len = p.head - e->link_start; c.text = text; c.text_len = strlen(text);
    after.anchor = after.head = e->link_start + strlen(e->link_items[e->link_sel].insert) + (e->link_md ? 1 : 2);
    e->link_open = 0;
    ex_doc_apply(e->doc, &c, 1, 0, &after, 1);
    ensure_visible(e, r);
}

/* ---- slash menu ---- */

void ex_editor_set_slash(ExEditor *e, ExSlashMenu *menu) { if (e->slash == menu) return; e->slash = menu; e->slash_open = 0; }
int ex_editor_slash_open(const ExEditor *e) { return e->slash && e->slash_open; }

/* The menu follows the text after the slash; it closes when the caret leaves it, when there are several cursors or
 * a selection, or when a space or line break is typed. */
static void slash_refresh(ExEditor *e) {
    ExSel p;
    char q[128], c;
    size_t n;
    if (!e->slash || !e->slash_open) return;
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    if (ex_doc_sel_count(e->doc) != 1 || p.anchor != p.head || p.head <= e->slash_start || p.head - e->slash_start > sizeof q) { e->slash_open = 0; return; }
    if (ex_buffer_read(buf(e), e->slash_start, &c, 1) != 1 || c != '/') { e->slash_open = 0; return; }
    n = ex_buffer_read(buf(e), e->slash_start + 1, q, p.head - e->slash_start - 1);
    q[n] = 0;
    if (strpbrk(q, " \t\r\n")) { e->slash_open = 0; return; }
    ex_slash_set_query(e->slash, q);
}

static void slash_accept(ExEditor *e, ExRenderer *r) {
    ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    const ExSlashItem *it = ex_slash_item(e->slash, ex_slash_selected(e->slash));
    ExChange c;
    ExSel after;
    if (!it || !it->available) return;                       /* a command that cannot run stays in the list, disabled */
    c.offset = e->slash_start; c.remove_len = p.head - e->slash_start; c.text = ""; c.text_len = 0;
    after.anchor = after.head = e->slash_start;
    e->slash_open = 0;
    ex_doc_apply(e->doc, &c, 1, 0, &after, 1);               /* the "/..." is removed, then the command works on the text as it was */
    ex_slash_run(e->slash);
    ensure_visible(e, r);
}

void ex_editor_text_input(ExEditor *e, const char *text, size_t len) {
    if (len == 0 || e->readonly) return;
    e->composition[0] = 0;
    ex_doc_replace_selections(e->doc, text, len, 1);
    e->last_vertical = 0;
    if (e->link_fn) {
        ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        char before;
        if (!e->link_open && len == 1 && text[0] == '[' && ex_doc_sel_count(e->doc) == 1 && p.head >= 2 && ex_buffer_read(buf(e), p.head - 2, &before, 1) == 1 && before == '[') {
            e->link_open = 1; e->link_md = 0; e->link_start = p.head; e->link_sel = 0; e->link_n = 0;
        } else if (!e->link_open && len == 1 && text[0] == '(' && ex_doc_sel_count(e->doc) == 1 && p.head >= 2 && ex_buffer_read(buf(e), p.head - 2, &before, 1) == 1 && before == ']') {
            e->link_open = 1; e->link_md = 1; e->link_start = p.head; e->link_sel = 0; e->link_n = 0;   /* "](" : a Markdown link's address */
        }
        link_refresh(e);
    }
    if (e->slash) {
        ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        if (!e->slash_open && len == 1 && text[0] == '/' && ex_doc_sel_count(e->doc) == 1 && p.head >= 1) {   /* at the start of a line or after a blank */
            char before;
            if (p.head == 1 || (ex_buffer_read(buf(e), p.head - 2, &before, 1) == 1 && (before == ' ' || before == '\t' || before == '\n'))) {
                e->slash_open = 1;
                e->slash_start = p.head - 1;
                ex_slash_set_query(e->slash, "");
            }
        } else slash_refresh(e);
    }
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

/* The text of the lines the selections are on (with their line breaks), one range per line, merged; used by copy, cut
 * and delete for a caret with nothing selected. */
static size_t line_ranges(ExEditor *e, Range *rg) {
    size_t n = ex_doc_sel_count(e->doc), i;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), st, en, nx;
        line_range(e, line, &st, &en, &nx);
        rg[i].a = st;
        rg[i].b = nx;
        if (nx == en && st > 0) rg[i].a = st - 1;                      /* the last line has no break of its own: take the one before it */
    }
    return n;
}

static int nothing_selected(ExEditor *e) {
    size_t n = ex_doc_sel_count(e->doc), i;
    for (i = 0; i < n; i++) { ExSel s = ex_doc_sel(e->doc, i); if (s.anchor != s.head) return 0; }
    return 1;
}

static void copy_or_cut(ExEditor *e, ExRenderer *r, int cut) {
    size_t len = 0, n = ex_doc_sel_count(e->doc), i;
    char *t;
    if (nothing_selected(e)) {                                          /* copy or cut the line (the paragraph) the caret is in */
        Range *rg = malloc((n ? n : 1) * sizeof *rg);
        size_t total = 0, used = 0;
        if (!rg) return;
        n = line_ranges(e, rg);
        for (i = 0; i < n; i++) total += rg[i].b - rg[i].a;
        t = malloc(total + n + 1);
        if (t) {
            for (i = 0; i < n; i++) {
                size_t got = ex_buffer_read(buf(e), rg[i].a, t + used, rg[i].b - rg[i].a);
                used += got;
            }
            t[used] = 0;
            if (used && e->clip.set) e->clip.set(e->clip.user, t);
            free(t);
        }
        if (cut && !e->readonly) delete_ranges(e, r, rg, n);
        free(rg);
        return;
    }
    t = ex_editor_selected_text(e, &len);
    if (t && len > 0 && e->clip.set) {
        e->clip.set(e->clip.user, t);
        if (cut && !e->readonly) backspace_or_delete(e, r, 0, 0);
    }
    free(t);
}

static void delete_to_line_edge(ExEditor *e, ExRenderer *r, int to_end) {
    size_t n = ex_doc_sel_count(e->doc), i;
    Range *rg;
    if (e->readonly) return;
    rg = malloc((n ? n : 1) * sizeof *rg);
    if (!rg) return;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), st, en, nx;
        line_range(e, line, &st, &en, &nx);
        if (s.anchor != s.head) { rg[i].a = lo(s); rg[i].b = hi(s); }
        else if (to_end) { rg[i].a = s.head; rg[i].b = en; if (s.head == en && nx > en) rg[i].b = nx; }   /* at the end: the line break */
        else { rg[i].a = st; rg[i].b = s.head; if (s.head == st && st > 0) rg[i].a = st - 1; }
    }
    delete_ranges(e, r, rg, n);
    free(rg);
}

static void delete_lines(ExEditor *e, ExRenderer *r) {
    size_t n = ex_doc_sel_count(e->doc);
    Range *rg;
    if (e->readonly || !nothing_selected(e)) return;
    rg = malloc((n ? n : 1) * sizeof *rg);
    if (!rg) return;
    n = line_ranges(e, rg);
    delete_ranges(e, r, rg, n);
    free(rg);
}

void ex_editor_set_indent(ExEditor *e, int use_tabs, int indent_width) { e->fmt.use_tabs = use_tabs != 0; e->fmt.indent_width = indent_width < 1 ? 1 : indent_width > 16 ? 16 : indent_width; }
void ex_editor_format(ExEditor *e, ExFormat f) {
    if (e->readonly) return;
    ex_doc_format(e->doc, f, &e->fmt);
    e->last_vertical = 0;
}

void ex_editor_perform(ExEditor *e, ExRenderer *r, ExEditAction act, int extend) {
    switch (act) {
    case EX_ACT_LEFT: move_horizontal(e, r, -1, 0, extend); break;
    case EX_ACT_RIGHT: move_horizontal(e, r, 1, 0, extend); break;
    case EX_ACT_WORD_LEFT: move_horizontal(e, r, -1, 1, extend); break;
    case EX_ACT_WORD_RIGHT: move_horizontal(e, r, 1, 1, extend); break;
    case EX_ACT_UP: move_vertical(e, r, -1, extend); break;
    case EX_ACT_DOWN: move_vertical(e, r, 1, extend); break;
    case EX_ACT_ADD_CURSOR_ABOVE: add_cursor_vertical(e, r, -1); break;
    case EX_ACT_ADD_CURSOR_BELOW: add_cursor_vertical(e, r, 1); break;
    case EX_ACT_PAGE_UP:
    case EX_ACT_PAGE_DOWN: {
        int lh = line_height_px(e, r), page = lh > 0 ? (int)(e->vh / (float)lh) - 1 : 10;
        if (page < 1) page = 1;
        move_vertical(e, r, act == EX_ACT_PAGE_UP ? -page : page, extend);
        break;
    }
    case EX_ACT_LINE_START: move_line_edge(e, r, 0, extend); break;
    case EX_ACT_LINE_END: move_line_edge(e, r, 1, extend); break;
    case EX_ACT_DOC_START: move_doc_edge(e, r, 0, extend); break;
    case EX_ACT_DOC_END: move_doc_edge(e, r, 1, extend); break;
    case EX_ACT_DELETE_BACK: backspace_or_delete(e, r, 0, 0); break;
    case EX_ACT_DELETE_FORWARD: backspace_or_delete(e, r, 1, 0); break;
    case EX_ACT_DELETE_WORD_BACK: backspace_or_delete(e, r, 0, 1); break;
    case EX_ACT_DELETE_WORD_FORWARD: backspace_or_delete(e, r, 1, 1); break;
    case EX_ACT_DELETE_TO_LINE_START: delete_to_line_edge(e, r, 0); break;
    case EX_ACT_DELETE_TO_LINE_END: delete_to_line_edge(e, r, 1); break;
    case EX_ACT_DELETE_LINE: delete_lines(e, r); break;
    case EX_ACT_NEWLINE: ex_editor_text_input(e, "\n", 1); ensure_visible(e, r); break;
    case EX_ACT_TAB: ex_editor_text_input(e, "\t", 1); break;
    case EX_ACT_SIMPLIFY: {
        ExSel s = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        s.anchor = s.head;
        ex_doc_set_selections(e->doc, &s, 1);
        sync_goal(e, 1);
        break;
    }
    case EX_ACT_SELECT_ALL: { ExSel s; s.anchor = 0; s.head = ex_buffer_size(buf(e)); ex_doc_set_selections(e->doc, &s, 1); sync_goal(e, 1); break; }
    case EX_ACT_UNDO: if (!e->readonly) { ex_doc_undo(e->doc); ensure_visible(e, r); } break;
    case EX_ACT_REDO: if (!e->readonly) { ex_doc_redo(e->doc); ensure_visible(e, r); } break;
    case EX_ACT_COPY: copy_or_cut(e, r, 0); break;
    case EX_ACT_CUT: copy_or_cut(e, r, 1); break;
    case EX_ACT_PASTE:
    case EX_ACT_PASTE_PLAIN: paste(e); ensure_visible(e, r); break;      /* the clipboard is plain text here, so both paste the same */
    case EX_ACT_BOLD: case EX_ACT_ITALIC: ex_editor_format(e, act == EX_ACT_BOLD ? EX_FMT_BOLD : EX_FMT_ITALIC); ensure_visible(e, r); break;
    default: break;
    }
    sync_goal(e, ex_doc_sel_count(e->doc));
}

void ex_editor_key(ExEditor *e, ExRenderer *r, ExKey key, int mods) {
    ExEditAction act;
    int extend;
    if (!e->font) return;
    if (e->link_fn && e->link_open) {
        if (key == EX_KEY_UP && e->link_n) { e->link_sel = (e->link_sel + e->link_n - 1) % e->link_n; return; }
        if (key == EX_KEY_DOWN && e->link_n) { e->link_sel = (e->link_sel + 1) % e->link_n; return; }
        if ((key == EX_KEY_ENTER || key == EX_KEY_TAB) && e->link_n) { link_accept(e, r); return; }
        if (key == EX_KEY_ESCAPE) { e->link_open = 0; return; }
    }
    if (e->slash && e->slash_open) {
        if (key == EX_KEY_UP || key == EX_KEY_DOWN) { ex_slash_move(e->slash, key == EX_KEY_UP ? -1 : 1); return; }
        if (key == EX_KEY_ENTER || key == EX_KEY_TAB) { slash_accept(e, r); return; }
        if (key == EX_KEY_ESCAPE) { e->slash_open = 0; return; }
    }
#ifdef __APPLE__
    if (!ex_edit_action_for(key, mods, 1, &act, &extend)) return;
#else
    if (!ex_edit_action_for(key, mods, 0, &act, &extend)) return;
#endif
    ex_editor_perform(e, r, act, extend);
    slash_refresh(e);
    link_refresh(e);
}

/* ---- mouse ---- */

static size_t point_to_offset(ExEditor *e, ExRenderer *r, float x, float y) {
    int lh = line_height_px(e, r);
    long row = (long)floorf((y - e->vy + e->scroll_y) / (float)(lh > 0 ? lh : 1));
    size_t rows, line, seg, off;
    WrapLine w;
    geometry(e, r);
    rows = vrow_total(e);
    if (row < 0) row = 0;
    if ((size_t)row >= rows) row = (long)rows - 1;
    line = line_of_vrow(e, (size_t)row, &seg);
    wrap_line(e, r, line, &w);
    if (seg >= w.n) seg = w.n - 1;
    off = offset_in_row(e, r, line, &w, seg, x - e->vx - (float)pad_px(r));
    wl_free(&w);
    return off;
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

void ex_editor_rect_select(ExEditor *e, ExRenderer *r, float ax, float ay, float x, float y) {
    int lh = line_height_px(e, r);
    long ra, rb, k;
    size_t rows, n = 0;
    float xa = ax - e->vx - (float)pad_px(r), xb = x - e->vx - (float)pad_px(r), left, right;
    ExSel *sel;
    if (!e->font || lh <= 0) return;
    geometry(e, r);
    rows = vrow_total(e);
    ra = (long)floorf((ay - e->vy + e->scroll_y) / (float)lh);
    rb = (long)floorf((y - e->vy + e->scroll_y) / (float)lh);
    if (ra < 0) ra = 0;
    if (rb < 0) rb = 0;
    if ((size_t)ra >= rows) ra = (long)rows - 1;
    if ((size_t)rb >= rows) rb = (long)rows - 1;
    left = xa < xb ? xa : xb;
    right = xa < xb ? xb : xa;
    sel = malloc((size_t)(ra > rb ? ra - rb : rb - ra) * sizeof *sel + sizeof *sel);
    if (!sel) return;
    for (k = ra < rb ? ra : rb; k <= (ra > rb ? ra : rb); k++) {
        size_t seg, line = line_of_vrow(e, (size_t)k, &seg), o0, o1;
        WrapLine w;
        wrap_line(e, r, line, &w);
        if (seg >= w.n) seg = w.n - 1;
        o0 = offset_in_row(e, r, line, &w, seg, left); o1 = offset_in_row(e, r, line, &w, seg, right);
        wl_free(&w);
        sel[n].anchor = xa <= xb ? o0 : o1;
        sel[n].head = xa <= xb ? o1 : o0;
        n++;
    }
    ex_doc_set_selections(e->doc, sel, n);
    sync_goal(e, ex_doc_sel_count(e->doc));
    free(sel);
}

void ex_editor_mouse(ExEditor *e, ExRenderer *r, ExMouse kind, float x, float y, int mods, int clicks) {
    size_t off;
    if (!e->font) return;
    if (e->link_fn && e->link_open) {
        if (kind == EX_MOUSE_DOWN && e->link_n && x >= e->sp_x && x < e->sp_x + e->sp_w && y >= e->sp_y && e->sp_rowh > 0 && y < e->sp_y + e->sp_rowh * (float)e->link_n) {
            e->link_sel = (size_t)((y - e->sp_y) / e->sp_rowh);
            link_accept(e, r);
            return;
        }
        if (kind == EX_MOUSE_DOWN) e->link_open = 0;
    }
    if (e->slash && e->slash_open) {
        if (kind == EX_MOUSE_DOWN && x >= e->sp_x && x < e->sp_x + e->sp_w && y >= e->sp_y && e->sp_rowh > 0 && y < e->sp_y + e->sp_rowh * (float)e->sp_rows) {
            size_t row = e->sp_first + (size_t)((y - e->sp_y) / e->sp_rowh);
            ex_slash_select(e->slash, row);
            slash_accept(e, r);
            return;
        }
        if (kind == EX_MOUSE_DOWN) e->slash_open = 0;                       /* a click elsewhere closes it and is then an ordinary click */
    }
    if (kind == EX_MOUSE_DOWN && (mods & EX_MOD_PRIMARY) && e->link_click) {              /* Ctrl or Command and click on a link */
        char target[300], heading[200], block[100];
        if (ex_editor_link_at(e, r, x, y, target, sizeof target, heading, sizeof heading, block, sizeof block)) { e->link_click(e->link_click_user, target, heading, block); return; }
    }
    if (kind == EX_MOUSE_DOWN && x >= e->vx && x < e->vx + (float)gutter_px(r) && e->mode != EX_EDITOR_PLAIN) {   /* the gutter: fold or open */
        size_t row = (size_t)floorf((y - e->vy + e->scroll_y) / (float)line_height_px(e, r)), line;
        geometry(e, r);
        if (y >= e->vy && row < vrow_total(e)) {
            line = line_of_vrow(e, row, NULL);
            if (ex_editor_toggle_fold_at_line(e, line)) return;
        }
    }
    if (kind == EX_MOUSE_DOWN && (mods & EX_MOD_ALT) && (mods & EX_MOD_SHIFT)) { e->rect = 1; e->rect_x = x; e->rect_y = y; e->dragging = 0; ex_editor_rect_select(e, r, x, y, x, y); return; }
    if (e->rect) {
        if (kind == EX_MOUSE_DRAG) ex_editor_rect_select(e, r, e->rect_x, e->rect_y, x, y);
        else if (kind == EX_MOUSE_UP) e->rect = 0;
        ensure_visible(e, r);
        return;
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
    size_t line, seg = 0;
    int lh = line_height_px(e, r);
    float cx;
    WrapLine wl;
    geometry(e, r);
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    line = ex_buffer_line_of_offset(buf(e), p.head);
    seg = seg_of(e, r, line, p.head, &wl);
    cx = e->vx + (float)pad_px(r) + x_of(e, r, line, p.head) - wl.x[seg];
    wl_free(&wl);
    if (x) *x = cx;
    if (y) *y = e->vy + (float)(vrow_of_line(e, line) + seg) * (float)lh - e->scroll_y;
    if (w) *w = (float)ex_px(r, 1.5f);
    if (h) *h = (float)lh;
}

void ex_editor_set_wrap(ExEditor *e, int on) { if ((on != 0) != e->wrap) { e->wrap = on != 0; wrap_free_all(e); } }

/* The height of all the rows of the text, for a field that grows with what is typed into it (the title). Valid once the editor has been
 * laid out and drawn at its width: it answers with the rows of the last measure until then. */
float ex_editor_content_height(ExEditor *e, ExRenderer *r) {
    int lh;
    if (!e->font || e->vw <= 0.0f) return 0.0f;
    lh = line_height_px(e, r);
    geometry(e, r);
    return (float)(vrow_total(e) ? vrow_total(e) : 1) * (float)lh;
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

typedef struct LinkProbe { size_t off, start; int have; size_t ts, te, hs, he, bs, be; int ht, hh, hb; } LinkProbe;
static int link_probe(const ExMdSpan *sp, void *user) {
    LinkProbe *lp = user;
    if (!lp->have && (sp->kind == EX_MD_WIKILINK || sp->kind == EX_MD_EMBED || sp->kind == EX_MD_LINK || sp->kind == EX_MD_IMAGE) && lp->off >= sp->start && lp->off < sp->end) { lp->have = 1; lp->start = sp->start; }
    else if (lp->have && sp->parent_start == lp->start) {
        if ((sp->kind == EX_MD_LINK_TARGET || sp->kind == EX_MD_LINK_DEST) && !lp->ht) { lp->ht = 1; lp->ts = sp->start; lp->te = sp->end; }
        else if (sp->kind == EX_MD_LINK_HEADING) { lp->hh = 1; lp->hs = sp->start; lp->he = sp->end; }
        else if (sp->kind == EX_MD_LINK_BLOCK) { lp->hb = 1; lp->bs = sp->start; lp->be = sp->end; }
    }
    return 0;
}

int ex_editor_link_at(ExEditor *e, ExRenderer *r, float x, float y, char *target, size_t tcap, char *heading, size_t hcap, char *block, size_t bcap) {
    LinkProbe lp;
    size_t len;
    memset(&lp, 0, sizeof lp);
    if (!e->font || e->mode == EX_EDITOR_PLAIN || x < e->vx || y < e->vy || x >= e->vx + e->vw || y >= e->vy + e->vh) return 0;
    ensure_md(e);
    lp.off = point_to_offset(e, r, x, y);
    len = ex_buffer_size(buf(e));
    ex_md_visit(e->md, lp.off > 0 ? lp.off - 1 : 0, lp.off + 1 < len ? lp.off + 1 : len, link_probe, &lp);
    if (!lp.have || !lp.ht) return 0;
    {
        size_t n = lp.te - lp.ts, k;
        if (n >= tcap) n = tcap - 1;
        k = ex_buffer_read(buf(e), lp.ts, target, n); target[k] = 0;
        heading[0] = 0; block[0] = 0;
        if (lp.hh) { n = lp.he - lp.hs; if (n >= hcap) n = hcap - 1; k = ex_buffer_read(buf(e), lp.hs, heading, n); heading[k] = 0; }
        if (lp.hb) { n = lp.be - lp.bs; if (n >= bcap) n = bcap - 1; k = ex_buffer_read(buf(e), lp.bs, block, n); block[k] = 0; }
    }
    return target[0] != 0 && !strstr(target, "://");
}

static size_t misspelled_in_line(ExEditor *e, size_t st, size_t en, size_t out_s[], size_t out_e[], size_t max);

/* Draws one line in source mode or live preview from its layout. */
static void draw_layout_line(ExEditor *e, ExRenderer *r, const ExTheme *t, size_t line, size_t nl, float y, int lh, const ExFontMetrics *fm,
                             int focused, int blink_on, ExColor sel_c, ExColor cur_c, ExColor comp_c, const WrapLine *wl, size_t seg) {
    size_t st, en, nx, si, nsel = ex_doc_sel_count(e->doc), i, ss, se;
    LLine *ll = layout_of(e, r, line, &st, &en);
    float text_left = e->vx + (float)pad_px(r), left, segx = wl->x[seg];
    int base_ascent = fm->ascent, last = seg + 1 == wl->n, wrapped = wl->n > 1;
    if (!ll) return;
    line_range(e, line, &st, &en, &nx);
    ss = wl->start[seg];                                  /* this row shows the bytes ss..se of the line */
    se = last ? en : wl->start[seg + 1];
    left = text_left - segx;
    if (ll->callout) {                                   /* a tint behind a callout's lines */
        ExColor c = ex_theme_color(t, (ExColorToken)(EX_COLOR_CALLOUT_NOTE + (ll->callout - 1 < EX_CALLOUT_COUNT ? ll->callout - 1 : 0)));
        c.a = 34;
        ex_fill_rect(r, e->vx, y, e->vw, (float)lh, c);
    } else if (ll->code) {
        ex_fill_rect(r, e->vx, y, e->vw, (float)lh, ex_theme_color(t, EX_COLOR_CODE_BACKGROUND));
    }
    if (wrapped) {                                        /* the row shows its part of the line: nothing of the other rows reaches it */
        float cy0 = y > e->vy ? y : e->vy, cy1 = y + (float)lh < e->vy + e->vh ? y + (float)lh : e->vy + e->vh;
        if (cy1 > cy0) ex_set_clip(r, (int)text_left, (int)cy0, (int)ceilf(wrap_avail(e, r) + (float)ex_px(r, 2)), (int)ceilf(cy1 - cy0));
    }
    for (si = 0; si < e->nhl; si++) {                   /* find matches behind the selection */
        size_t ms = e->hl[si].s > ss ? e->hl[si].s : ss, me = e->hl[si].e < se ? e->hl[si].e : se;
        if (me <= ms) continue;
        ex_fill_rect(r, left + ll_x_of(ll, ms), y, ll_x_of(ll, me) - ll_x_of(ll, ms), (float)lh, ex_theme_color(t, EX_COLOR_HIGHLIGHT));
    }
    /* selections behind the text */
    for (si = 0; si < nsel; si++) {
        ExSel s = ex_doc_sel(e->doc, si);
        size_t a = lo(s), b = hi(s), la, lb;
        float x0, x1;
        if (a == b || b <= ss || (last ? (a > en && a >= nx) : a >= se)) continue;
        la = a > ss ? a : ss;
        lb = b < se ? b : se;
        x0 = ll_x_of(ll, la);
        x1 = ll_x_of(ll, lb);
        if (x1 < x0) { float tmp = x0; x0 = x1; x1 = tmp; }
        if (b > en && line + 1 < nl && last) x1 += (float)ex_px(r, 6);
        ex_fill_rect(r, left + x0, y, x1 - x0, (float)lh, sel_c);
    }
    if (ll->rule) {
        ExColor c = ex_theme_color(t, EX_COLOR_BORDER);
        ex_fill_rect(r, left, y + (float)lh / 2.0f, e->vw - (float)pad_px(r) - (float)ex_px(r, 8), (float)ex_px(r, 1.5f), c);
    }
    for (i = 0; i < ll->n; i++) {
        LRun *run = &ll->runs[i];
        float x = left + run->x, rw = run->w;
        const char *rtext = run->text;
        size_t rlen = run->tlen;
        if (run->kind == LRUN_HIDDEN) continue;
        if (wrapped && (run->e <= ss || run->s >= se)) continue;        /* a run of another row */
        if (wrapped && run->kind == LRUN_TEXT && run->shaped && run->tlen == run->e - run->s && (run->s < ss || run->e > se)) {
            size_t a = run->s > ss ? run->s : ss, b = run->e < se ? run->e : se;        /* the part of a long run that is in this row */
            float xa = ex_shaped_x_for_offset(&run->sh, a - run->s, run->tlen), xb = ex_shaped_x_for_offset(&run->sh, b - run->s, run->tlen);
            if (xb < xa) { float tmp = xa; xa = xb; xb = tmp; }
            x = left + run->x + xa; rw = xb - xa; rtext = run->text + (a - run->s); rlen = b - a;
        }
        if (run->bg >= 0 && rw > 0) ex_fill_rect(r, x, y, rw, (float)lh, ex_theme_color(t, (ExColorToken)run->bg));
        if (run->kind == LRUN_TEXT || run->kind == LRUN_SUBST) {
            ExFontMetrics m;
            ExColor col = ex_theme_color(t, (ExColorToken)run->fg);
            float ty = y + (float)(lh - fm->line_height) / 2.0f;
            if (ex_text_metrics(r, run->font, run->size, &m) == 0) ty += (float)(base_ascent - m.ascent);
            ex_text_draw(r, run->font, run->size, x, ty, rtext, rlen, col);
            if (run->flags & LF_UNDERLINE) ex_fill_rect(r, x, y + (float)(lh + fm->line_height) / 2.0f - (float)ex_px(r, 1), rw, (float)ex_px(r, 1), col);
            if (run->flags & LF_STRIKE) ex_fill_rect(r, x, y + (float)lh / 2.0f, rw, (float)ex_px(r, 1), col);
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
    if (e->spell) {                                                      /* wavy lines under unknown words */
        size_t ms[64], me[64], k, nm = misspelled_in_line(e, st, en, ms, me, 64);
        ExColor ec = ex_theme_color(t, EX_COLOR_ERROR);
        ExSel pr = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        for (k = 0; k < nm; k++) {
            float x0, x1, x, by, step = (float)ex_px(r, 2);
            int odd = 0;
            if (focused && pr.head >= ms[k] && pr.head <= me[k]) continue;   /* the word being typed is left alone */
            x0 = left + ll_x_of(ll, ms[k]);
            x1 = left + ll_x_of(ll, me[k]);
            by = y + (float)(lh + fm->line_height) / 2.0f - (float)ex_px(r, 2);
            for (x = x0; x < x1; x += step, odd ^= 1) ex_fill_rect(r, x, by + (odd ? (float)ex_px(r, 1.5f) : 0.0f), x + step > x1 ? x1 - x : step, (float)ex_px(r, 1), ec);
        }
    }
    /* carets and the IME composition */
    for (si = 0; si < nsel; si++) {
        ExSel s = ex_doc_sel(e->doc, si);
        float cx;
        if (s.head < ss) continue;
        if (last) { if (s.head > en || !(s.head < nx || line + 1 == nl)) continue; }
        else if (s.head >= se) continue;
        cx = left + ll_x_of(ll, s.head);
        if (si == ex_doc_primary(e->doc) && e->composition[0]) {
            float ty = y + (float)(lh - fm->line_height) / 2.0f;
            float w = ex_text_draw(r, e->font, e->size, cx, ty, e->composition, strlen(e->composition), comp_c);
            ex_fill_rect(r, cx, y + (float)lh - (float)ex_px(r, 2), w, (float)ex_px(r, 1), comp_c);
            cx += w;
        }
        if (focused && blink_on) ex_fill_rect(r, cx, y, (float)ex_px(r, 1.5f), (float)lh, cur_c);
    }
    if (wrapped) ex_set_clip(r, (int)e->vx, (int)e->vy, (int)e->vw, (int)e->vh);
    ll_free(ll);
}

void ex_editor_set_find(ExEditor *e, struct ExFind *find) { e->find = find; }

void ex_editor_select(ExEditor *e, ExRenderer *r, size_t a, size_t b) {
    ExSel s;
    size_t size = ex_buffer_size(buf(e));
    s.anchor = a > size ? size : a;
    s.head = b > size ? size : b;
    ex_doc_set_selections(e->doc, &s, 1);
    sync_goal(e, 1);
    ensure_visible(e, r);
}

/* The matches of the find pattern in the lines first..last (screen lines), by byte range, for drawing. */
static void collect_matches(ExEditor *e, size_t first_line, size_t last_line) {
    size_t from, to, len, pos, s, en;
    char *text;
    e->nhl = 0;
    if (!e->find || first_line >= line_count(e)) return;
    if (last_line >= line_count(e)) last_line = line_count(e) - 1;
    from = ex_buffer_line_start(buf(e), first_line);
    to = last_line + 1 < line_count(e) ? ex_buffer_line_start(buf(e), last_line + 1) : ex_buffer_size(buf(e));
    text = malloc(to - from + 1);
    if (!text) return;
    len = ex_buffer_read(buf(e), from, text, to - from);
    pos = 0;
    while (pos <= len && ex_find_next(e->find, text, len, pos, &s, &en) == EX_FIND_MATCH && e->nhl < 2000) {
        if (e->nhl == e->caphl) {
            size_t nc = e->caphl ? e->caphl * 2 : 64;
            void *p = realloc(e->hl, nc * sizeof *e->hl);
            if (!p) break;
            e->hl = p;
            e->caphl = nc;
        }
        if (en > s) { e->hl[e->nhl].s = from + s; e->hl[e->nhl].e = from + en; e->nhl++; }
        pos = en > s ? en : s + 1;
    }
    free(text);
}

/* ---- spell check ---- */

void ex_editor_set_spell(ExEditor *e, ExSpell *spell) { if (e->spell != spell) { e->spell = spell; memset(e->scache, 0, sizeof e->scache); } }
void ex_editor_spell_changed(ExEditor *e) { memset(e->scache, 0, sizeof e->scache); }

static int word_ok(ExEditor *e, const char *w, size_t n) {
    unsigned long h = 1469598103934665603ul;
    size_t i;
    if (n >= 32) return ex_spell_check(e->spell, w, n);
    for (i = 0; i < n; i++) { h ^= (unsigned char)w[i]; h *= 1099511628211ul; }
    i = h & 255;
    if (e->scache[i].ok != 0 && e->scache[i].h == h && strlen(e->scache[i].w) == n && memcmp(e->scache[i].w, w, n) == 0) return e->scache[i].ok > 0;
    e->scache[i].h = h;
    memcpy(e->scache[i].w, w, n);
    e->scache[i].w[n] = 0;
    e->scache[i].ok = ex_spell_check(e->spell, w, n) ? 1 : -1;
    return e->scache[i].ok > 0;
}

typedef struct SkipCtx { size_t s[64], e[64]; size_t n; } SkipCtx;
static int skip_span(const ExMdSpan *sp, void *user) {
    SkipCtx *c = user;
    switch (sp->kind) {
    case EX_MD_FRONTMATTER: case EX_MD_CODE_BLOCK: case EX_MD_CODE_SPAN: case EX_MD_MATH_BLOCK: case EX_MD_MATH_INLINE: case EX_MD_LINK_DEST:
    case EX_MD_AUTOLINK: case EX_MD_TAG: case EX_MD_LINK_TARGET: case EX_MD_LINK_BLOCK: case EX_MD_LINK_HEADING: case EX_MD_EMBED: case EX_MD_IMAGE:
    case EX_MD_HTML_INLINE: case EX_MD_HTML_BLOCK: case EX_MD_BLOCK_ID: case EX_MD_FOOTNOTE_REF: case EX_MD_COMMENT_INLINE: case EX_MD_COMMENT_BLOCK:
        if (c->n < 64) { c->s[c->n] = sp->start; c->e[c->n] = sp->end; c->n++; }
        break;
    default: break;
    }
    return 0;
}

/* The misspelled words of a line as byte ranges (offsets in the document), skipping what is not prose. */
static size_t misspelled_in_line(ExEditor *e, size_t st, size_t en, size_t out_s[], size_t out_e[], size_t max) {
    char tb[1024];
    size_t len, pos = 0, ws, we, n = 0, i;
    SkipCtx sk;
    if (!e->spell || !e->md || en <= st) return 0;
    len = ex_buffer_read(buf(e), st, tb, en - st < sizeof tb ? en - st : sizeof tb);
    sk.n = 0;
    ex_md_visit(e->md, st, en, skip_span, &sk);
    while (n < max && ex_words_next(tb, len, &pos, &ws, &we)) {
        int skip = 0;
        for (i = 0; i < sk.n; i++) if (st + ws < sk.e[i] && st + we > sk.s[i]) { skip = 1; break; }
        if (skip || we - ws < 2) continue;
        if (!word_ok(e, tb + ws, we - ws)) { out_s[n] = st + ws; out_e[n] = st + we; n++; }
    }
    return n;
}

int ex_editor_word_at_caret(ExEditor *e, size_t *start, size_t *end) {
    ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    size_t line = ex_buffer_line_of_offset(buf(e), p.head), st, en, nx, len, pos = 0, ws, we;
    char tb[1024];
    line_range(e, line, &st, &en, &nx);
    len = ex_buffer_read(buf(e), st, tb, en - st < sizeof tb ? en - st : sizeof tb);
    while (ex_words_next(tb, len, &pos, &ws, &we))
        if (st + ws <= p.head && p.head <= st + we) { *start = st + ws; *end = st + we; return 1; }
    return 0;
}

/* The fold marker of a header line: a triangle in the gutter (pointing right when folded, down when open; open ones only
 * show on the caret's line), and three dots after the text of a folded header. */
static void draw_fold_marker(ExEditor *e, ExRenderer *r, const ExTheme *t, size_t line, float y, int lh, int caret_line, const WrapLine *wl, size_t seg) {
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
    if (seg == 0) {
        if (folded) ex_fill_triangle(r, cx - side * 0.35f, cy - side * 0.5f, cx - side * 0.35f, cy + side * 0.5f, cx + side * 0.5f, cy, c);
        else ex_fill_triangle(r, cx - side * 0.5f, cy - side * 0.3f, cx + side * 0.5f, cy - side * 0.3f, cx, cy + side * 0.4f, c);
    }
    if (folded && seg + 1 == wl->n) {
        size_t st, en, nx;
        float x;
        ExFontMetrics fm;
        line_range(e, line, &st, &en, &nx);
        x = e->vx + (float)pad_px(r) + x_of(e, r, line, en) - wl->x[seg] + (float)ex_px(r, 6);
        if (ex_font_metrics(e->font, line_px(e, r), &fm) == 0)
            ex_text_draw(r, e->font, e->size, x, y + (float)(lh - fm.line_height) / 2.0f, "...", 3, ex_theme_color(t, EX_COLOR_TEXT_MUTED));
    }
}

void ex_editor_draw(ExEditor *e, ExRenderer *r, const ExTheme *t, int focused, int blink_on) {
    int lh, px, pad;
    size_t nl = line_count(e), first, last, line, row, rows, caret_line, nsel = ex_doc_sel_count(e->doc), si;
    size_t cached_line = (size_t)-1;
    WrapLine wl;
    ExFontMetrics fm;
    ExColor text = ex_theme_color(t, EX_COLOR_TEXT), sel_c = ex_theme_color(t, EX_COLOR_SELECTION);
    ExColor cur_c = ex_theme_color(t, EX_COLOR_CURSOR), comp_c = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    if (!e->font) return;
    lh = line_height_px(e, r);
    px = line_px(e, r);
    pad = pad_px(r);
    if (ex_font_metrics(e->font, px, &fm)) return;
    memset(&wl, 0, sizeof wl);
    ex_set_clip(r, (int)e->vx, (int)e->vy, (int)e->vw, (int)e->vh);
    geometry(e, r);
    rows = vrow_total(e);
    first = (size_t)(e->scroll_y / (float)lh);
    last = first + (size_t)(e->vh / (float)lh) + 1;
    if (last >= rows) last = rows - 1;
    caret_line = ex_buffer_line_of_offset(buf(e), ex_doc_sel(e->doc, ex_doc_primary(e->doc)).head);
    if (e->find && rows) collect_matches(e, line_of_vrow(e, first, NULL), line_of_vrow(e, last < rows ? last : rows - 1, NULL));
    else e->nhl = 0;
    for (row = first; row <= last && row < rows; row++) {
        size_t len, st, en, nx, seg, ss, se;
        int lastseg;
        char *txt;
        const ExShaped *sh;
        float y = e->vy + (float)row * (float)lh - e->scroll_y;
        float ty = y + (float)(lh - fm.line_height) / 2.0f, segx, x0pad;
        line = line_of_vrow(e, row, &seg);
        if (line != cached_line) { wl_free(&wl); wrap_line(e, r, line, &wl); cached_line = line; }
        if (seg >= wl.n) seg = wl.n - 1;
        if (e->mode != EX_EDITOR_PLAIN) { draw_layout_line(e, r, t, line, nl, y, lh, &fm, focused, blink_on, sel_c, cur_c, comp_c, &wl, seg); draw_fold_marker(e, r, t, line, y, lh, line == caret_line, &wl, seg); continue; }
        txt = line_text(e, line, &len);
        if (!txt) continue;
        line_range(e, line, &st, &en, &nx);
        sh = shaped(e, r, txt, len);
        ss = wl.start[seg]; lastseg = seg + 1 == wl.n; se = lastseg ? en : wl.start[seg + 1];
        segx = wl.x[seg];
        x0pad = e->vx + (float)pad - segx;                 /* where x = 0 of the line is for this row */
        for (si = 0; si < e->nhl; si++) {                 /* find matches behind the selection */
            size_t ms = e->hl[si].s > ss ? e->hl[si].s : ss, me = e->hl[si].e < se ? e->hl[si].e : se;
            if (me <= ms || !sh) continue;
            ex_fill_rect(r, x0pad + ex_shaped_x_for_offset(sh, ms - st, len), y, ex_shaped_x_for_offset(sh, me - st, len) - ex_shaped_x_for_offset(sh, ms - st, len), (float)lh, ex_theme_color(t, EX_COLOR_HIGHLIGHT));
        }
        /* selections behind the text */
        for (si = 0; si < nsel; si++) {
            ExSel s = ex_doc_sel(e->doc, si);
            size_t a = lo(s), b = hi(s), la, lb;
            float x0, x1;
            if (a == b || b <= ss || (lastseg ? (a > en && a >= nx) : a >= se)) continue;
            la = a > ss ? a : ss;
            lb = b < se ? b : se;
            x0 = sh ? ex_shaped_x_for_offset(sh, la - st, len) : 0;
            x1 = sh ? ex_shaped_x_for_offset(sh, lb - st, len) : 0;
            if (x1 < x0) { float tmp = x0; x0 = x1; x1 = tmp; }
            if (b > en && line + 1 < nl && lastseg) x1 += (float)ex_px(r, 6); /* the selected line break */
            ex_fill_rect(r, x0pad + x0, y, x1 - x0, (float)lh, sel_c);
        }
        ex_text_draw(r, e->font, e->size, e->vx + (float)pad, ty, txt + (ss - st), se - ss, text);      /* the part of the line in this row */
        /* carets and the IME composition */
        for (si = 0; si < nsel; si++) {
            ExSel s = ex_doc_sel(e->doc, si);
            float cx;
            if (s.head < ss) continue;
            if (lastseg) { if (s.head > en || !(s.head < nx || line + 1 == nl)) continue; }
            else if (s.head >= se) continue;
            cx = x0pad + (sh ? ex_shaped_x_for_offset(sh, s.head - st, len) : 0.0f);
            if (si == ex_doc_primary(e->doc) && e->composition[0]) {
                float w = ex_text_draw(r, e->font, e->size, cx, ty, e->composition, strlen(e->composition), comp_c);
                ex_fill_rect(r, cx, y + (float)lh - (float)ex_px(r, 2), w, (float)ex_px(r, 1), comp_c);
                cx += w;
            }
            if (focused && blink_on) ex_fill_rect(r, cx, y, (float)ex_px(r, 1.5f), (float)lh, cur_c);
        }
        free(txt);
    }
    wl_free(&wl);
    ex_set_clip(r, (int)e->vx, (int)e->vy, (int)e->vw, (int)e->vh);
    if (e->link_fn && e->link_open) {                                   /* the link suggestions under the caret */
        size_t count = e->link_n, k;
        float rowh = (float)lh, w = (float)ex_px(r, 360) * (e->size / 16.0f), h = rowh * (float)(count ? count : 1) + (float)ex_px(r, 8), cx, cy, py, pxl;
        ExColor bg = ex_theme_color(t, EX_COLOR_SURFACE), border = ex_theme_color(t, EX_COLOR_BORDER);
        ex_editor_caret_rect(e, r, &cx, &cy, NULL, NULL);
        pxl = cx;
        if (pxl + w > e->vx + e->vw) pxl = e->vx + e->vw - w;
        if (pxl < e->vx) pxl = e->vx;
        py = cy + (float)lh + 2.0f;
        if (py + h > e->vy + e->vh && cy - h - 2.0f >= e->vy) py = cy - h - 2.0f;
        e->sp_x = pxl; e->sp_y = py + (float)ex_px(r, 4); e->sp_w = w; e->sp_rowh = rowh;
        ex_fill_round_rect(r, pxl, py, w, h, (float)ex_px(r, 6), border);
        ex_fill_round_rect(r, pxl + 1, py + 1, w - 2, h - 2, (float)ex_px(r, 5), bg);
        if (!count) ex_text_draw(r, e->font, e->size, pxl + (float)ex_px(r, 8), py + (float)ex_px(r, 4) + (rowh - (float)fm.line_height) / 2.0f, "No matching notes", 17, ex_theme_color(t, EX_COLOR_TEXT_FAINT));
        for (k = 0; k < count; k++) {
            char lab[400], det[200];
            float ry = py + (float)ex_px(r, 4) + rowh * (float)k, inner = w - 2.0f * (float)ex_px(r, 8), dw, lw;
            ex_text_fit(r, e->font, e->size * 0.85f, e->link_items[k].detail, strlen(e->link_items[k].detail), inner * 0.4f, det, sizeof det);     /* the detail takes at most 40% of the row */
            dw = det[0] ? ex_text_measure(r, e->font, e->size * 0.85f, det, strlen(det)) : 0;
            lw = inner - (dw > 0 ? dw + (float)ex_px(r, 8) : 0);
            ex_text_fit(r, e->font, e->size, e->link_items[k].label, strlen(e->link_items[k].label), lw, lab, sizeof lab);                      /* a long name ends in "..." inside the popup */
            if (k == e->link_sel) ex_fill_rect(r, pxl + 2, ry, w - 4, rowh, ex_theme_color(t, EX_COLOR_SELECTION));
            ex_text_draw(r, e->font, e->size, pxl + (float)ex_px(r, 8), ry + (rowh - (float)fm.line_height) / 2.0f, lab, strlen(lab), ex_theme_color(t, EX_COLOR_TEXT));
            if (dw > 0) ex_text_draw(r, e->font, e->size * 0.85f, pxl + w - dw - (float)ex_px(r, 8), ry + (rowh - (float)fm.line_height * 0.85f) / 2.0f, det, strlen(det), ex_theme_color(t, EX_COLOR_TEXT_MUTED));
        }
    }
    if (e->slash && e->slash_open) {                                    /* the slash menu under the caret */
        size_t count = ex_slash_count(e->slash), rows = count ? (count < 8 ? count : 8) : 1, sel = ex_slash_selected(e->slash), first = 0, k;
        float rowh = (float)lh, w = (float)ex_px(r, 300) * (e->size / 16.0f), h = rowh * (float)rows + (float)ex_px(r, 8), cx, cy, py, pxl;
        ExColor bg = ex_theme_color(t, EX_COLOR_SURFACE), border = ex_theme_color(t, EX_COLOR_BORDER);
        ex_editor_caret_rect(e, r, &cx, &cy, NULL, NULL);
        pxl = cx;
        if (pxl + w > e->vx + e->vw) pxl = e->vx + e->vw - w;
        if (pxl < e->vx) pxl = e->vx;
        py = cy + (float)lh + 2.0f;
        if (py + h > e->vy + e->vh && cy - h - 2.0f >= e->vy) py = cy - h - 2.0f;
        if (sel >= rows) first = sel - rows + 1;
        e->sp_x = pxl; e->sp_y = py + (float)ex_px(r, 4); e->sp_w = w; e->sp_rowh = rowh; e->sp_first = first; e->sp_rows = count ? rows : 0;
        ex_fill_round_rect(r, pxl, py, w, h, (float)ex_px(r, 6), border);
        ex_fill_round_rect(r, pxl + 1, py + 1, w - 2, h - 2, (float)ex_px(r, 5), bg);
        if (!count) ex_text_draw(r, e->font, e->size, pxl + (float)ex_px(r, 8), py + (float)ex_px(r, 4) + (rowh - (float)fm.line_height) / 2.0f, "No matching commands", 20, ex_theme_color(t, EX_COLOR_TEXT_FAINT));
        for (k = 0; k < rows && first + k < count; k++) {
            const ExSlashItem *it = ex_slash_item(e->slash, first + k);
            float ry = py + (float)ex_px(r, 4) + rowh * (float)k;
            if (first + k == sel) ex_fill_rect(r, pxl + 2, ry, w - 4, rowh, ex_theme_color(t, EX_COLOR_SELECTION));
            ex_text_draw(r, e->font, e->size, pxl + (float)ex_px(r, 8), ry + (rowh - (float)fm.line_height) / 2.0f, it->name, strlen(it->name), ex_theme_color(t, it->available ? EX_COLOR_TEXT : EX_COLOR_TEXT_FAINT));
        }
    }
    ex_set_clip(r, 0, 0, -1, -1);
}
