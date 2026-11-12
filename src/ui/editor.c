#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <hb.h>

#include "exhaust_editor.h"
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
    return e;
}

static void clear_cache(ExEditor *e) {
    int i;
    for (i = 0; i < CACHE_LINES; i++)
        if (e->cache[i].valid) { free(e->cache[i].text); ex_shaped_free(&e->cache[i].sh); e->cache[i].valid = 0; }
}

void ex_editor_free(ExEditor *e) {
    if (!e) return;
    clear_cache(e);
    free(e->goal);
    free(e);
}

ExDoc *ex_editor_doc(ExEditor *e) { return e->doc; }
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

static float x_of(ExEditor *e, ExRenderer *r, size_t line, size_t offset) {
    size_t len, s, en, n;
    char *t = line_text(e, line, &len);
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
    char *t = line_text(e, line, &len);
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
    p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    line = ex_buffer_line_of_offset(buf(e), p.head);
    lh = line_height_px(e, r);
    top = (float)line * (float)lh;
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
    }
    set_heads(e, r, heads, shift, 0);
    free(heads);
}

static void move_vertical(ExEditor *e, ExRenderer *r, int lines, int shift) {
    size_t n = ex_doc_sel_count(e->doc), i, nl = line_count(e);
    size_t *heads = malloc((n ? n : 1) * sizeof *heads);
    if (!heads) return;
    sync_goal(e, n);
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(e->doc, i);
        size_t line = ex_buffer_line_of_offset(buf(e), s.head), target;
        float x = (e->last_vertical && e->goal && e->goal[i] >= 0) ? e->goal[i] : x_of(e, r, line, s.head);
        long t = (long)line + lines;
        if (e->goal) e->goal[i] = x;
        if (t < 0) { heads[i] = 0; if (e->goal) e->goal[i] = -1; continue; }
        if ((size_t)t >= nl) { heads[i] = ex_buffer_size(buf(e)); if (e->goal) e->goal[i] = -1; continue; }
        target = (size_t)t;
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
    heads[0] = to_end ? ex_buffer_size(buf(e)) : 0;
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
    if (len == 0) return;
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
    char *text = e->clip.get ? e->clip.get(e->clip.user) : NULL;
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
    case EX_KEY_ESCAPE: {
        ExSel s = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
        s.anchor = s.head;
        ex_doc_set_selections(e->doc, &s, 1);
        sync_goal(e, 1);
        break;
    }
    case EX_KEY_A: if (prim) { ExSel s; s.anchor = 0; s.head = ex_buffer_size(buf(e)); ex_doc_set_selections(e->doc, &s, 1); sync_goal(e, 1); } break;
    case EX_KEY_Z: if (prim) { if (shift) ex_doc_redo(e->doc); else ex_doc_undo(e->doc); ensure_visible(e, r); } break;
    case EX_KEY_Y: if (prim) { ex_doc_redo(e->doc); ensure_visible(e, r); } break;
    case EX_KEY_C:
    case EX_KEY_X:
        if (prim) {
            size_t len = 0;
            char *t = ex_editor_selected_text(e, &len);
            if (t && len > 0 && e->clip.set) {
                e->clip.set(e->clip.user, t);
                if (key == EX_KEY_X) backspace_or_delete(e, r, 0, 0);
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
    long line = (long)floorf((y - e->vy + e->scroll_y) / (float)(lh > 0 ? lh : 1));
    size_t nl = line_count(e);
    if (line < 0) line = 0;
    if ((size_t)line >= nl) line = (long)nl - 1;
    return offset_at_x(e, r, (size_t)line, x - e->vx - (float)ex_px(r, 8));
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

void ex_editor_scroll(ExEditor *e, float dy) {
    e->scroll_y += dy;
    if (e->scroll_y < 0) e->scroll_y = 0;
}

/* ---- drawing ---- */

void ex_editor_caret_rect(ExEditor *e, ExRenderer *r, float *x, float *y, float *w, float *h) {
    ExSel p = ex_doc_sel(e->doc, ex_doc_primary(e->doc));
    size_t line = ex_buffer_line_of_offset(buf(e), p.head);
    int lh = line_height_px(e, r);
    float cx = e->vx + (float)ex_px(r, 8) + x_of(e, r, line, p.head);
    if (x) *x = cx;
    if (y) *y = e->vy + (float)line * (float)lh - e->scroll_y;
    if (w) *w = (float)ex_px(r, 1.5f);
    if (h) *h = (float)lh;
}

void ex_editor_draw(ExEditor *e, ExRenderer *r, const ExTheme *t, int focused, int blink_on) {
    int lh, px, pad;
    size_t nl = line_count(e), first, last, line, nsel = ex_doc_sel_count(e->doc), si;
    ExFontMetrics fm;
    ExColor text = ex_theme_color(t, EX_COLOR_TEXT), sel_c = ex_theme_color(t, EX_COLOR_SELECTION);
    ExColor cur_c = ex_theme_color(t, EX_COLOR_CURSOR), comp_c = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    if (!e->font) return;
    lh = line_height_px(e, r);
    px = line_px(e, r);
    pad = ex_px(r, 8);
    if (ex_font_metrics(e->font, px, &fm)) return;
    ex_set_clip(r, (int)e->vx, (int)e->vy, (int)e->vw, (int)e->vh);
    first = (size_t)(e->scroll_y / (float)lh);
    last = first + (size_t)(e->vh / (float)lh) + 1;
    if (last >= nl) last = nl - 1;
    for (line = first; line <= last && line < nl; line++) {
        size_t len, st, en, nx;
        char *txt = line_text(e, line, &len);
        const ExShaped *sh;
        float y = e->vy + (float)line * (float)lh - e->scroll_y;
        float ty = y + (float)(lh - fm.line_height) / 2.0f;
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
