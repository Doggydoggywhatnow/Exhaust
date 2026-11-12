/* The text formatting commands (see ExFormat in exhaust_editor.h). They only use the document's public API. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_editor.h"

typedef struct Chg { size_t off, rm; char *text; size_t tlen; } Chg;
typedef struct ChgList { Chg *c; size_t n, cap; int oom; } ChgList;

static void cl_add(ChgList *l, size_t off, size_t rm, const char *text, size_t tlen) {
    if (l->oom) return;
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        Chg *p = realloc(l->c, nc * sizeof *p);
        if (!p) { l->oom = 1; return; }
        l->c = p;
        l->cap = nc;
    }
    l->c[l->n].off = off;
    l->c[l->n].rm = rm;
    l->c[l->n].tlen = tlen;
    l->c[l->n].text = malloc(tlen + 1);
    if (!l->c[l->n].text) { l->oom = 1; return; }
    memcpy(l->c[l->n].text, text, tlen);
    l->c[l->n].text[tlen] = 0;
    l->n++;
}

static void cl_free(ChgList *l) {
    size_t i;
    for (i = 0; i < l->n; i++) free(l->c[i].text);
    free(l->c);
}

/* Offset after the changes (ascending, not overlapping). A position inside a removed range goes to the end of its replacement. */
static size_t map_off(const ChgList *l, size_t off) {
    size_t i;
    long shift = 0;
    for (i = 0; i < l->n; i++) {
        const Chg *c = &l->c[i];
        if (off < c->off + (c->rm ? 1 : 0) && off <= c->off) break;                /* before (or at the start of) the change */
        if (off >= c->off + c->rm) { shift += (long)c->tlen - (long)c->rm; continue; }
        return (size_t)((long)c->off + shift + (long)c->tlen);                      /* inside the removed text */
    }
    return (size_t)((long)off + shift);
}

static int apply(ExDoc *d, ChgList *l, const ExSel *after, size_t nafter) {
    ExChange *ch;
    size_t i;
    int rc;
    if (l->oom || l->n == 0) return 0;
    ch = malloc(l->n * sizeof *ch);
    if (!ch) return 0;
    for (i = 0; i < l->n; i++) { ch[i].offset = l->c[i].off; ch[i].remove_len = l->c[i].rm; ch[i].text = l->c[i].text; ch[i].text_len = l->c[i].tlen; }
    rc = ex_doc_apply(d, ch, l->n, 0, after, nafter);
    free(ch);
    return rc == 0;
}

/* Applies the changes and moves every selection through them. */
static int apply_mapped(ExDoc *d, ChgList *l) {
    size_t n = ex_doc_sel_count(d), i;
    ExSel *sel = malloc((n ? n : 1) * sizeof *sel);
    int ok;
    if (!sel) return 0;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(d, i);
        sel[i].anchor = map_off(l, s.anchor);
        sel[i].head = map_off(l, s.head);
    }
    ok = apply(d, l, sel, n);
    free(sel);
    return ok;
}

/* ---- lines ---- */

typedef struct Text { char *t; size_t len; size_t *ls; size_t nl; } Text;   /* the whole text and the start of every line */

static int text_load(Text *x, ExDoc *d) {
    size_t i, n = 1;
    x->t = ex_buffer_text(ex_doc_buffer(d), &x->len);
    if (!x->t) return 0;
    for (i = 0; i < x->len; i++) if (x->t[i] == '\n') n++;
    x->ls = malloc(n * sizeof *x->ls);
    if (!x->ls) { free(x->t); return 0; }
    x->nl = 0;
    x->ls[x->nl++] = 0;
    for (i = 0; i < x->len; i++) if (x->t[i] == '\n') x->ls[x->nl++] = i + 1;
    return 1;
}
static void text_free(Text *x) { free(x->t); free(x->ls); }
static size_t line_end(const Text *x, size_t line) { return line + 1 < x->nl ? x->ls[line + 1] - 1 : x->len; }          /* before the '\n' */
static size_t line_of(const Text *x, size_t off) {
    size_t lo = 0, hi = x->nl;
    while (hi - lo > 1) { size_t m = (lo + hi) / 2; if (x->ls[m] <= off) lo = m; else hi = m; }
    return lo;
}
/* The content of a line without a trailing \r (CRLF text keeps its \r). */
static size_t line_content_end(const Text *x, size_t line) {
    size_t e = line_end(x, line);
    if (e > x->ls[line] && x->t[e - 1] == '\r') e--;
    return e;
}

/* The lines the selections touch, ascending and without repeats; a selection that ends at the start of a line does not touch it. */
static size_t touched_lines(const Text *x, ExDoc *d, size_t **out) {
    size_t n = ex_doc_sel_count(d), i, m = 0, cap = 0, *v = NULL;
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(d, i);
        size_t a = s.anchor < s.head ? s.anchor : s.head, b = s.anchor < s.head ? s.head : s.anchor, la = line_of(x, a), lb = line_of(x, b), k;
        if (b > a && x->ls[lb] == b && lb > la) lb--;
        for (k = la; k <= lb; k++) {
            if (m && v[m - 1] >= k) continue;
            if (m == cap) { size_t *p = realloc(v, (cap = cap ? cap * 2 : 16) * sizeof *p); if (!p) { free(v); return 0; } v = p; }
            v[m++] = k;
        }
    }
    *out = v;
    return m;
}

static size_t skip_ws(const char *t, size_t n) { size_t i = 0; while (i < n && (t[i] == ' ' || t[i] == '\t')) i++; return i; }
static int is_blank(const char *t, size_t n) { return skip_ws(t, n) == n; }

/* A bullet or number marker after the indentation: returns the length of the marker with its space (0 if none); *numbered says which. */
static size_t list_marker(const char *t, size_t n, size_t ws, int *numbered) {
    size_t i = ws;
    *numbered = 0;
    if (i + 1 < n + 1 && i < n && (t[i] == '-' || t[i] == '*' || t[i] == '+') && (i + 1 == n || t[i + 1] == ' ')) return (i + 1 < n ? 2 : 1);
    while (i < n && isdigit((unsigned char)t[i])) i++;
    if (i > ws && i < n && (t[i] == '.' || t[i] == ')') && (i + 1 == n || t[i + 1] == ' ')) { *numbered = 1; return i + 1 - ws + (i + 1 < n ? 1 : 0); }
    return 0;
}

/* "[ ] " or "[x] " right at p: returns 4 (or 3 at the end of the line) and the status character. */
static size_t checkbox_at(const char *t, size_t n, size_t p, char *status) {
    if (p + 2 < n + 0 + 1 && p + 2 < n && t[p] == '[' && t[p + 2] == ']' && (p + 3 == n || t[p + 3] == ' ')) { *status = t[p + 1]; return p + 3 == n ? 3 : 4; }
    return 0;
}

static int heading_len(const char *t, size_t n, int *level) {
    size_t i = 0;
    while (i < n && t[i] == '#') i++;
    if (i >= 1 && i <= 6 && (i == n || t[i] == ' ')) { *level = (int)i; return (int)(i == n ? i : i + 1); }
    *level = 0;
    return 0;
}

typedef int (*PrefixFn)(const char *t, size_t n, size_t idx, int ctx, size_t *rm, char *out, size_t cap);

static int line_command(ExDoc *d, PrefixFn fn, int ctx, int skip_blank) {
    Text x;
    size_t *lines, nlines, i;
    ChgList l = { NULL, 0, 0, 0 };
    int ok = 0;
    size_t idx = 0;
    if (!text_load(&x, d)) return 0;
    nlines = touched_lines(&x, d, &lines);
    for (i = 0; i < nlines; i++) {
        size_t ln = lines[i], st = x.ls[ln], n = line_content_end(&x, ln) - st, rm;
        char out[64];
        int len;
        if (skip_blank && is_blank(x.t + st, n)) continue;
        len = fn(x.t + st, n, idx++, ctx, &rm, out, sizeof out);
        if (len < 0) continue;
        if (len == 0 && rm == 0) continue;
        cl_add(&l, st, rm, out, (size_t)len);
    }
    ok = apply_mapped(d, &l);
    cl_free(&l);
    free(lines);
    text_free(&x);
    return ok;
}

/* Where the new prefix goes: after the indentation for list commands, at the very start for headings and quotes. */
static int heading_fn(const char *t, size_t n, size_t idx, int level, size_t *rm, char *out, size_t cap) {
    int cur, hl = heading_len(t, n, &cur);
    (void)idx; (void)cap;
    *rm = (size_t)hl;
    if (level == cur) level = 0;                                    /* the same level again: off */
    if (level == 0) return 0;
    memset(out, '#', (size_t)level);
    out[level] = ' ';
    return level + 1;
}
static int heading_cycle_fn(const char *t, size_t n, size_t idx, int ctx, size_t *rm, char *out, size_t cap) {
    int cur, next;
    (void)ctx;
    heading_len(t, n, &cur);
    next = cur >= 6 ? 0 : cur + 1;
    return heading_fn(t, n, idx, next, rm, out, cap);
}

static int bullet_fn(const char *t, size_t n, size_t idx, int remove, size_t *rm, char *out, size_t cap) {
    size_t ws = skip_ws(t, n);
    int num;
    size_t m = list_marker(t, n, ws, &num);
    (void)idx; (void)cap;
    if (remove) { *rm = m; return 0; }                              /* only called for lines that are bullets */
    if (m && !num) return -1;
    *rm = m;
    memcpy(out, "- ", 2);
    {   /* the marker goes where the indentation ends: the prefix change starts at the line start, so repeat the indentation */
        size_t k = ws < 40 ? ws : 40;
        memmove(out + k, out, 2);
        memcpy(out, t, k);
        *rm += ws;
        return (int)(k + 2);
    }
}

static int numbered_fn(const char *t, size_t n, size_t idx, int remove, size_t *rm, char *out, size_t cap) {
    size_t ws = skip_ws(t, n), k = ws < 40 ? ws : 40;
    int num;
    size_t m = list_marker(t, n, ws, &num);
    (void)cap;
    if (remove) { *rm = ws + m; memcpy(out, t, k); return (int)k; }
    *rm = ws + m;
    memcpy(out, t, k);
    return (int)k + snprintf(out + k, 20, "%zu. ", idx + 1);
}

static int quote_fn(const char *t, size_t n, size_t idx, int remove, size_t *rm, char *out, size_t cap) {
    (void)idx; (void)cap;
    if (remove) { *rm = n > 1 && t[1] == ' ' ? 2 : 1; return 0; }
    *rm = 0;
    memcpy(out, "> ", 2);
    return 2;
}

/* checkbox: ctx 0 = toggle the status, 1 = cycle plain, bullet, checkbox */
static int checkbox_fn(const char *t, size_t n, size_t idx, int ctx, size_t *rm, char *out, size_t cap) {
    size_t ws = skip_ws(t, n), k = ws < 40 ? ws : 40;
    int num;
    size_t m = list_marker(t, n, ws, &num);
    char st = 0;
    size_t cb = m ? checkbox_at(t, n, ws + m, &st) : 0;
    (void)idx; (void)cap;
    memcpy(out, t, k);
    if (cb && ctx == 1) { *rm = ws + m + cb; return (int)k; }                      /* checkbox: back to plain text */
    if (cb) {                                                                       /* flip done and open */
        *rm = ws + m + cb;
        memcpy(out + k, t + ws, m);
        memcpy(out + k + m, st == ' ' ? "[x] " : "[ ] ", cb);
        return (int)(k + m + cb);
    }
    if (m) { *rm = ws + m; memcpy(out + k, t + ws, m); memcpy(out + k + m, "[ ] ", 4); return (int)(k + m + 4); }
    *rm = ws;
    if (ctx == 1) { memcpy(out + k, "- ", 2); return (int)k + 2; }               /* plain text: a bullet */
    memcpy(out + k, "- [ ] ", 6);
    return (int)k + 6;
}

static int indent_fn(const char *t, size_t n, size_t idx, int unit_len, size_t *rm, char *out, size_t cap) {
    size_t ws = skip_ws(t, n);
    int num;
    (void)idx; (void)cap;
    *rm = 0;
    if (!list_marker(t, n, ws, &num)) return -1;                        /* only list items indent */
    if (unit_len > 0) { memset(out, ' ', (size_t)unit_len); return unit_len; }   /* the caller passes spaces or (negative) a tab */
    out[0] = '\t';
    return 1;
}

static int unindent_fn(const char *t, size_t n, size_t idx, int width, size_t *rm, char *out, size_t cap) {
    size_t ws = skip_ws(t, n), k = 0;
    int num;
    (void)idx; (void)out; (void)cap;
    if (!list_marker(t, n, ws, &num) || ws == 0) return -1;
    if (t[0] == '\t') k = 1;
    else while (k < ws && k < (size_t)width && t[k] == ' ') k++;
    *rm = k;
    return 0;
}

/* ---- inline wrapping ---- */

static size_t run_left(const char *t, size_t pos, char c) { size_t k = 0; while (k < pos && t[pos - 1 - k] == c) k++; return k; }
static size_t run_right(const char *t, size_t len, size_t pos, char c) { size_t k = 0; while (pos + k < len && t[pos + k] == c) k++; return k; }

/* Which way a toggle goes for [a, b): 1 take the markers off outside the selection, 2 take off inside it, 0 wrap. *mlen is the length to remove on each side. */
static int wrap_state(const char *t, size_t len, size_t a, size_t b, const char *m, size_t ml, size_t *rmlen) {
    char c = m[0];
    *rmlen = ml;
    if (c == '*') {                                                      /* * and ** share a character: look at the whole run */
        size_t l = run_left(t, a, '*'), r = run_right(t, len, b, '*'), run = l < r ? l : r;
        if (ml == 1 && run % 2 == 1) return 1;
        if (ml == 2 && run >= 2) return 1;
        if (b - a >= 2 * ml) {
            size_t li = run_right(t, len, a, '*'), ri = run_left(t, b, '*'), in = li < ri ? li : ri;
            if (b - a > 2 * in && ((ml == 1 && in % 2 == 1) || (ml == 2 && in >= 2))) return 2;
        }
        return 0;
    }
    if (a >= ml && b + ml <= len && memcmp(t + a - ml, m, ml) == 0 && memcmp(t + b, m, ml) == 0) return 1;
    if (b - a >= 2 * ml && memcmp(t + a, m, ml) == 0 && memcmp(t + b - ml, m, ml) == 0) return 2;
    return 0;
}

static int inline_toggle(ExDoc *d, const char *m) {
    Text x;
    size_t ml = strlen(m), n = ex_doc_sel_count(d), i;
    long shift = 0;
    ChgList l = { NULL, 0, 0, 0 };
    ExSel *after;
    int ok;
    if (!text_load(&x, d)) return 0;
    after = malloc((n ? n : 1) * sizeof *after);
    if (!after) { text_free(&x); return 0; }
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(d, i);
        size_t a = s.anchor < s.head ? s.anchor : s.head, b = s.anchor < s.head ? s.head : s.anchor, rl;
        int st = wrap_state(x.t, x.len, a, b, m, ml, &rl);
        if (a == b) {
            if (st == 1) {                                               /* the caret between a pair of markers: take them out */
                size_t take = ml;
                if (m[0] == '*' && ml == 1 && run_left(x.t, a, '*') >= 2) st = 0;     /* "**|**" is bold, not italic: add */
                if (st == 1) { cl_add(&l, a - take, take, "", 0); cl_add(&l, a, take, "", 0); after[i].anchor = after[i].head = (size_t)((long)a - (long)take + shift); shift -= 2 * (long)take; continue; }
            }
            {
                char buf[16];
                memcpy(buf, m, ml); memcpy(buf + ml, m, ml);
                cl_add(&l, a, 0, buf, 2 * ml);
                after[i].anchor = after[i].head = (size_t)((long)a + (long)ml + shift);
                shift += 2 * (long)ml;
            }
            continue;
        }
        if (st == 1) {
            cl_add(&l, a - ml, ml, "", 0); cl_add(&l, b, ml, "", 0);
            after[i].anchor = (size_t)((long)a - (long)ml + shift); after[i].head = (size_t)((long)b - (long)ml + shift);
            shift -= 2 * (long)ml;
        } else if (st == 2) {
            cl_add(&l, a, ml, "", 0); cl_add(&l, b - ml, ml, "", 0);
            after[i].anchor = (size_t)((long)a + shift); after[i].head = (size_t)((long)b - 2 * (long)ml + shift);
            shift -= 2 * (long)ml;
        } else {
            cl_add(&l, a, 0, m, ml); cl_add(&l, b, 0, m, ml);
            after[i].anchor = (size_t)((long)a + (long)ml + shift); after[i].head = (size_t)((long)b + (long)ml + shift);
            shift += 2 * (long)ml;
        }
        if (s.anchor > s.head) { size_t t = after[i].anchor; after[i].anchor = after[i].head; after[i].head = t; }
    }
    ok = apply(d, &l, after, n);
    cl_free(&l);
    free(after);
    text_free(&x);
    return ok;
}

/* ---- insertions around or in place of the selections ---- */

/* Replaces each selection s with pre + s + post. The selection afterwards is the old text (inner), or a caret at `caret_in_pre` bytes
 * into the empty case when the selection is empty and caret_pos >= 0 (counted from the start of the replacement). */
static int surround(ExDoc *d, const char *pre, const char *post, int empty_caret) {
    Text x;
    size_t n = ex_doc_sel_count(d), i, pl = strlen(pre), ql = strlen(post);
    long shift = 0;
    ChgList l = { NULL, 0, 0, 0 };
    ExSel *after;
    int ok;
    if (!text_load(&x, d)) return 0;
    after = malloc((n ? n : 1) * sizeof *after);
    if (!after) { text_free(&x); return 0; }
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(d, i);
        size_t a = s.anchor < s.head ? s.anchor : s.head, b = s.anchor < s.head ? s.head : s.anchor;
        cl_add(&l, a, 0, pre, pl);
        cl_add(&l, b, 0, post, ql);
        if (a == b && empty_caret >= 0) after[i].anchor = after[i].head = (size_t)((long)a + shift + empty_caret);
        else { after[i].anchor = (size_t)((long)a + shift + (long)pl); after[i].head = (size_t)((long)b + shift + (long)pl); }
        shift += (long)pl + (long)ql;
    }
    ok = apply(d, &l, after, n);
    cl_free(&l);
    free(after);
    text_free(&x);
    return ok;
}

static int block_command(ExDoc *d, ExFormat f) {
    Text x;
    ExSel p = ex_doc_sel(d, ex_doc_primary(d));
    size_t a = p.anchor < p.head ? p.anchor : p.head, b = p.anchor < p.head ? p.head : p.anchor;
    ChgList l = { NULL, 0, 0, 0 };
    ExSel after;
    int ok = 0;
    if (!text_load(&x, d)) return 0;
    {
        size_t la = line_of(&x, a), lb = line_of(&x, b);
        size_t st = x.ls[la], en = line_end(&x, lb);
        const char *sel = x.t + a;
        size_t sl = b - a;
        char *buf = NULL;
        if (b > a && x.ls[lb] == b && lb > la) { lb--; en = line_end(&x, lb); }
        switch (f) {
        case EX_FMT_HR: {
            size_t pos = line_end(&x, la);
            if (a == b && is_blank(x.t + st, pos - st)) { cl_add(&l, st, pos - st, "---", 3); after.anchor = after.head = st + 3; }
            else { cl_add(&l, pos, 0, "\n\n---", 5); after.anchor = after.head = pos + 5; }
            break;
        }
        case EX_FMT_CODE_BLOCK:
        case EX_FMT_MATH_BLOCK: {
            const char *o = f == EX_FMT_CODE_BLOCK ? "```\n" : "$$\n", *c = f == EX_FMT_CODE_BLOCK ? "\n```" : "\n$$";
            size_t ol = strlen(o), cl2 = strlen(c);
            buf = malloc(ol + sl + cl2 + 1);
            if (!buf) break;
            memcpy(buf, o, ol); memcpy(buf + ol, sel, sl); memcpy(buf + ol + sl, c, cl2);
            cl_add(&l, a, b - a, buf, ol + sl + cl2);
            after.anchor = a + ol; after.head = a + ol + sl;
            break;
        }
        case EX_FMT_CALLOUT: {                                           /* the selected lines (or the caret's line) become a callout */
            size_t k, nl2 = 0;
            const char *src = x.t + st;
            size_t srcl = en - st;
            buf = malloc(srcl * 2 + 64);
            if (!buf) break;
            memcpy(buf, "> [!note]\n", 10);
            nl2 = 10;
            if (srcl == 0 || (srcl && is_blank(src, srcl))) { memcpy(buf + nl2, "> ", 2); nl2 += 2; }
            else {
                memcpy(buf + nl2, "> ", 2); nl2 += 2;
                for (k = 0; k < srcl; k++) { buf[nl2++] = src[k]; if (src[k] == '\n') { memcpy(buf + nl2, "> ", 2); nl2 += 2; } }
            }
            cl_add(&l, st, srcl, buf, nl2);
            after.anchor = after.head = st + 12;                       /* in the callout's first line of text */
            break;
        }
        case EX_FMT_TABLE: {
            const char *tb = "| | |\n|---|---|\n| | |";
            size_t pos = a == b ? a : b, tl = strlen(tb);
            int at_start = pos == x.ls[line_of(&x, pos)];
            buf = malloc(tl + 3);
            if (!buf) break;
            buf[0] = 0;
            if (!at_start) strcpy(buf, "\n");
            strcat(buf, tb);
            cl_add(&l, a, b - a, buf, strlen(buf));
            after.anchor = after.head = a + (at_start ? 0 : 1) + 2;
            break;
        }
        case EX_FMT_FOOTNOTE: {                                          /* [^n] at the caret, its definition at the end of the note */
            long maxn = 0;
            size_t i;
            char ref[32], def[48];
            const char *sep = x.len >= 2 && x.t[x.len - 1] == '\n' && x.t[x.len - 2] == '\n' ? "" : x.len && x.t[x.len - 1] == '\n' ? "\n" : x.len ? "\n\n" : "";
            for (i = 0; i + 3 < x.len; i++)
                if (x.t[i] == '[' && x.t[i + 1] == '^') { long v = 0; size_t j = i + 2; while (j < x.len && isdigit((unsigned char)x.t[j]) && v < 100000) v = v * 10 + (x.t[j++] - '0'); if (j > i + 2 && x.t[j] == ']' && v > maxn) maxn = v; }
            snprintf(ref, sizeof ref, "[^%ld]", maxn + 1);
            snprintf(def, sizeof def, "%s[^%ld]: ", sep, maxn + 1);
            if (b >= x.len) {                                            /* the caret is at the very end: one insertion */
                char both[96];
                snprintf(both, sizeof both, "%s%s", ref, def);
                cl_add(&l, a, b - a, both, strlen(both));
                after.anchor = after.head = a + strlen(both);
            } else {
                cl_add(&l, a, b - a, ref, strlen(ref));
                cl_add(&l, x.len, 0, def, strlen(def));
                after.anchor = after.head = x.len - (b - a) + strlen(ref) + strlen(def);
            }
            break;
        }
        default: break;
        }
        free(buf);
    }
    ok = apply(d, &l, &after, 1);
    cl_free(&l);
    text_free(&x);
    return ok;
}

static int move_lines(ExDoc *d, int down) {
    Text x;
    size_t n = ex_doc_sel_count(d), i, first = (size_t)-1, last = 0, *lines, nlines;
    int ok = 0;
    ChgList l = { NULL, 0, 0, 0 };
    if (!text_load(&x, d)) return 0;
    nlines = touched_lines(&x, d, &lines);
    if (!nlines) { text_free(&x); return 0; }
    first = lines[0]; last = lines[nlines - 1];
    free(lines);
    if ((!down && first == 0) || (down && last + 1 >= x.nl)) { text_free(&x); return 0; }
    {
        size_t other = down ? last + 1 : first - 1;
        size_t bst = x.ls[first], ben = line_end(&x, last), ost = x.ls[other], oen = line_end(&x, other);
        size_t bl = ben - bst, ol = oen - ost, from = down ? bst : ost, to = down ? oen : ben, w = 0;
        char *buf = malloc(bl + ol + 2);
        ExSel *sel = malloc((n ? n : 1) * sizeof *sel);
        if (!buf || !sel) { free(buf); free(sel); text_free(&x); return 0; }
        if (down) { memcpy(buf, x.t + ost, ol); buf[ol] = '\n'; memcpy(buf + ol + 1, x.t + bst, bl); w = ol + 1 + bl; }
        else { memcpy(buf, x.t + bst, bl); buf[bl] = '\n'; memcpy(buf + bl + 1, x.t + ost, ol); w = bl + 1 + ol; }
        cl_add(&l, from, to - from, buf, w);
        for (i = 0; i < n; i++) {
            ExSel s = ex_doc_sel(d, i);
            long delta = down ? (long)ol + 1 : -((long)ol + 1);
            sel[i].anchor = (size_t)((long)s.anchor + delta);
            sel[i].head = (size_t)((long)s.head + delta);
        }
        ok = apply(d, &l, sel, n);
        free(buf);
        free(sel);
    }
    cl_free(&l);
    text_free(&x);
    return ok;
}

static int delete_paragraph(ExDoc *d) {
    Text x;
    ExSel p = ex_doc_sel(d, ex_doc_primary(d));
    size_t ln, a, b, from, to;
    ChgList l = { NULL, 0, 0, 0 };
    ExSel after;
    int ok;
    if (!text_load(&x, d)) return 0;
    ln = line_of(&x, p.head);
    a = b = ln;
    if (!is_blank(x.t + x.ls[ln], line_end(&x, ln) - x.ls[ln])) {
        while (a > 0 && !is_blank(x.t + x.ls[a - 1], line_end(&x, a - 1) - x.ls[a - 1])) a--;
        while (b + 1 < x.nl && !is_blank(x.t + x.ls[b + 1], line_end(&x, b + 1) - x.ls[b + 1])) b++;
    }
    from = x.ls[a];
    to = b + 1 < x.nl ? x.ls[b + 1] : x.len;
    if (b + 1 < x.nl && is_blank(x.t + x.ls[b + 1], line_end(&x, b + 1) - x.ls[b + 1]) && b + 2 < x.nl) to = x.ls[b + 2];   /* and the blank line after it */
    else if (b + 1 >= x.nl && a > 0) from--;                                 /* the last paragraph: take the break before it */
    cl_add(&l, from, to - from, "", 0);
    after.anchor = after.head = from;
    ok = apply(d, &l, &after, 1);
    cl_free(&l);
    text_free(&x);
    return ok;
}

static int clear_formatting(ExDoc *d) {
    Text x;
    size_t n = ex_doc_sel_count(d), i;
    ChgList l = { NULL, 0, 0, 0 };
    ExSel *after = malloc((n ? n : 1) * sizeof *after);
    long shift = 0;
    int ok;
    if (!after || !text_load(&x, d)) { free(after); return 0; }
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(d, i);
        size_t a = s.anchor < s.head ? s.anchor : s.head, b = s.anchor < s.head ? s.head : s.anchor, la, lb, k, w = 0;
        char *buf;
        if (a == b) { la = line_of(&x, a); a = x.ls[la]; b = line_end(&x, la); s.anchor = a; s.head = b; }     /* nothing selected: the line */
        la = line_of(&x, a);
        lb = line_of(&x, b);
        buf = malloc(b - a + 1);
        if (!buf) continue;
        for (k = a; k < b; ) {
            size_t ln = line_of(&x, k);
            if (k == x.ls[ln]) {                                      /* a line start inside the range: the line's prefixes go */
                int lv, hl = heading_len(x.t + k, line_end(&x, ln) - k, &lv);
                size_t ws = skip_ws(x.t + k, line_end(&x, ln) - k), m;
                int num;
                if (hl) { k += (size_t)hl; continue; }
                if (x.t[k] == '>') { k += x.t[k + 1] == ' ' ? 2 : 1; continue; }
                m = list_marker(x.t + k, line_end(&x, ln) - k, ws, &num);
                if (m) { size_t cb; char stc; k += ws + m; cb = checkbox_at(x.t + k, line_end(&x, ln) - k, 0, &stc); k += cb; continue; }
            }
            if (k + 1 < b && ((x.t[k] == '*' && x.t[k + 1] == '*') || (x.t[k] == '_' && x.t[k + 1] == '_') || (x.t[k] == '~' && x.t[k + 1] == '~') || (x.t[k] == '=' && x.t[k + 1] == '=') || (x.t[k] == '%' && x.t[k + 1] == '%'))) { k += 2; continue; }
            if (x.t[k] == '*' || x.t[k] == '`') { k++; continue; }
            buf[w++] = x.t[k++];
        }
        (void)lb; (void)la;
        if (w != b - a || memcmp(buf, x.t + a, w) != 0) cl_add(&l, a, b - a, buf, w);
        after[i].anchor = (size_t)((long)a + shift);
        after[i].head = (size_t)((long)a + (long)w + shift);
        shift += (long)w - (long)(b - a);
        free(buf);
    }
    ok = apply(d, &l, after, n);
    cl_free(&l);
    free(after);
    text_free(&x);
    return ok;
}

int ex_doc_format(ExDoc *d, ExFormat f, const ExFormatOptions *opt) {
    ExFormatOptions o = { 1, 4 };
    if (opt) o = *opt;
    switch (f) {
    case EX_FMT_BOLD: return inline_toggle(d, "**");
    case EX_FMT_ITALIC: return inline_toggle(d, "*");
    case EX_FMT_STRIKE: return inline_toggle(d, "~~");
    case EX_FMT_HIGHLIGHT: return inline_toggle(d, "==");
    case EX_FMT_CODE: return inline_toggle(d, "`");
    case EX_FMT_INLINE_MATH: return inline_toggle(d, "$");
    case EX_FMT_COMMENT: return inline_toggle(d, "%%");
    case EX_FMT_HEADING_CYCLE: return line_command(d, heading_cycle_fn, 0, 1);
    case EX_FMT_HEADING_1: case EX_FMT_HEADING_2: case EX_FMT_HEADING_3:
    case EX_FMT_HEADING_4: case EX_FMT_HEADING_5: case EX_FMT_HEADING_6:
        return line_command(d, heading_fn, (int)(f - EX_FMT_HEADING_1) + 1, 1);
    case EX_FMT_REMOVE_HEADING: return line_command(d, heading_fn, 0, 1);
    case EX_FMT_BULLET: case EX_FMT_NUMBERED: case EX_FMT_QUOTE: {
        /* off when every (non-blank) line already has it */
        Text x;
        size_t *lines, nl2, i;
        int all = 1, any = 0, rc;
        if (!text_load(&x, d)) return 0;
        nl2 = touched_lines(&x, d, &lines);
        for (i = 0; i < nl2; i++) {
            size_t st = x.ls[lines[i]], len = line_content_end(&x, lines[i]) - st, ws = skip_ws(x.t + st, len);
            int num;
            size_t m = list_marker(x.t + st, len, ws, &num);
            if (f != EX_FMT_QUOTE && is_blank(x.t + st, len)) continue;
            any = 1;
            if (f == EX_FMT_BULLET && !(m && !num)) all = 0;
            if (f == EX_FMT_NUMBERED && !(m && num)) all = 0;
            if (f == EX_FMT_QUOTE && !(len && x.t[st] == '>')) all = 0;
        }
        free(lines);
        text_free(&x);
        rc = line_command(d, f == EX_FMT_BULLET ? bullet_fn : f == EX_FMT_NUMBERED ? numbered_fn : quote_fn, any && all, f != EX_FMT_QUOTE);
        return rc;
    }
    case EX_FMT_CHECKBOX_STATUS: return line_command(d, checkbox_fn, 0, 1);
    case EX_FMT_CYCLE_BULLET_CHECKBOX: return line_command(d, checkbox_fn, 1, 1);
    case EX_FMT_INDENT: return line_command(d, indent_fn, o.use_tabs ? 0 : o.indent_width, 1);
    case EX_FMT_UNINDENT: return line_command(d, unindent_fn, o.indent_width, 1);
    case EX_FMT_MOVE_LINE_UP: return move_lines(d, 0);
    case EX_FMT_MOVE_LINE_DOWN: return move_lines(d, 1);
    case EX_FMT_DELETE_PARAGRAPH: return delete_paragraph(d);
    case EX_FMT_HR: case EX_FMT_CODE_BLOCK: case EX_FMT_CALLOUT: case EX_FMT_MATH_BLOCK: case EX_FMT_TABLE: case EX_FMT_FOOTNOTE:
        return block_command(d, f);
    case EX_FMT_MD_LINK: return surround(d, "[", "]()", 1);
    case EX_FMT_INTERNAL_LINK: return surround(d, "[[", "]]", 2);
    case EX_FMT_EMBED: return surround(d, "![[", "]]", 3);
    case EX_FMT_TAG: return surround(d, "#", "", 1);
    case EX_FMT_CLEAR: return clear_formatting(d);
    default: return 0;
    }
}
