/* Table editing (sub-slice 5f): the table.* commands, working on the Markdown table under the caret as one text edit each (one undo
 * step) that touches only the table's lines. See ExTableCmd in exhaust_editor.h and the "Table editing" part of docs/formats.md for the rules.
 *
 * A table is a header row, a delimiter row (cells of dashes with optional colons) with as many cells as the header, and the rows after it,
 * up to a blank line, a line without an unescaped pipe, or the start of another block (a quote, a fence, a heading). A table inside a
 * fenced code block is not a table. Cells are split at pipes that are not escaped; the outer pipes are optional.
 *
 * Column commands write every line of the table again with tidy column widths (the whole table is what they edit); row commands add,
 * copy, swap or remove whole lines and write only a new row tidily; the align commands change only the delimiter row's cell. A cell's
 * text is copied unchanged. A row with more cells than the header keeps them: the table is given that many columns. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_editor.h"

typedef struct Text { char *t; size_t len; size_t *ls; size_t nl; } Text;

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
static size_t line_of(const Text *x, size_t off) {
    size_t lo = 0, hi = x->nl;
    while (hi - lo > 1) { size_t m = (lo + hi) / 2; if (x->ls[m] <= off) lo = m; else hi = m; }
    return lo;
}
/* The end of a line's content: before the '\n' and before a '\r' that goes with it. */
static size_t cend(const Text *x, size_t line) {
    size_t e = line + 1 < x->nl ? x->ls[line + 1] - 1 : x->len;
    if (e > x->ls[line] && x->t[e - 1] == '\r') e--;
    return e;
}
static int is_ws(char c) { return c == ' ' || c == '\t'; }

/* ---- cells ---- */

typedef struct Cell { size_t s, e; size_t rs, re; } Cell;      /* s..e: the text without the spaces around it; rs..re: everything between two pipes */
typedef struct Row { size_t line; Cell *c; size_t n; size_t indent_end; } Row;

static int has_pipe(const Text *x, size_t line) {
    size_t i = x->ls[line], e = cend(x, line);
    while (i < e) {
        if (x->t[i] == '\\' && i + 1 < e) { i += 2; continue; }
        if (x->t[i] == '|') return 1;
        i++;
    }
    return 0;
}

static int row_parse(const Text *x, size_t line, Row *r) {
    size_t ls = x->ls[line], le = cend(x, line), i = ls, cap = 8, n = 0, pipes = 0, first = 1;
    Cell *c = malloc(cap * sizeof *c);
    if (!c) return 0;
    while (i < le && is_ws(x->t[i])) i++;
    r->indent_end = i;
    r->line = line;
    if (i < le && x->t[i] == '|') { i++; pipes = 1; }
    for (;;) {
        size_t cs = i, j = i, s, e;
        while (j < le && x->t[j] != '|') { if (x->t[j] == '\\' && j + 1 < le) j++; j++; }
        s = cs; e = j;
        while (s < e && is_ws(x->t[s])) s++;
        while (e > s && is_ws(x->t[e - 1])) e--;
        if (n == cap) { Cell *p = realloc(c, (cap *= 2) * sizeof *c); if (!p) { free(c); return 0; } c = p; }
        c[n].s = s; c[n].e = e; c[n].rs = first ? ls : cs; c[n].re = j;
        n++;
        first = 0;
        if (j >= le) break;
        i = j + 1;
        pipes = 1;
    }
    /* a blank last cell after a pipe is the closing pipe's, not a cell */
    if (pipes && n && c[n - 1].s == c[n - 1].e && c[n - 1].re == le) n--;
    r->c = c; r->n = n;
    return 1;
}
static void row_free(Row *r) { free(r->c); r->c = NULL; }

/* A line that can belong to a table: not blank, has an unescaped pipe, and does not start another block. */
static int pipe_line(const Text *x, size_t line) {
    size_t i = x->ls[line], e = cend(x, line);
    while (i < e && is_ws(x->t[i])) i++;
    if (i >= e) return 0;
    if (x->t[i] == '>') return 0;
    if (e - i >= 3 && ((x->t[i] == '`' && x->t[i + 1] == '`' && x->t[i + 2] == '`') || (x->t[i] == '~' && x->t[i + 1] == '~' && x->t[i + 2] == '~'))) return 0;
    if (x->t[i] == '#') { size_t k = i; while (k < e && x->t[k] == '#') k++; if (k - i <= 6 && (k == e || is_ws(x->t[k]))) return 0; }
    return has_pipe(x, line);
}

/* 1 if the line (already parsed) is a delimiter row: every cell is dashes with an optional colon at either end. */
static int is_delim(const Text *x, const Row *r) {
    size_t k;
    if (r->n == 0) return 0;
    for (k = 0; k < r->n; k++) {
        size_t s = r->c[k].s, e = r->c[k].e, dashes = 0;
        if (s < e && x->t[s] == ':') s++;
        while (s < e && x->t[s] == '-') { s++; dashes++; }
        if (s < e && x->t[s] == ':') s++;
        if (dashes == 0 || s != e) return 0;
    }
    return 1;
}

/* Is this line inside a fenced code block? Scans from the top of the note (lines up to but not including `line`). */
static int in_fence(const Text *x, size_t line) {
    size_t l;
    char fc = 0;
    size_t flen = 0;
    for (l = 0; l < line; l++) {
        size_t i = x->ls[l], e = cend(x, l), k, ind = 0;
        while (i < e && x->t[i] == ' ' && ind < 4) { i++; ind++; }
        if (ind >= 4 || i >= e) continue;
        if (x->t[i] == '`' || x->t[i] == '~') {
            char c = x->t[i];
            k = i;
            while (k < e && x->t[k] == c) k++;
            if (k - i >= 3) {
                if (!fc) { fc = c; flen = k - i; }
                else if (c == fc && k - i >= flen) {
                    size_t m = k;
                    while (m < e && is_ws(x->t[m])) m++;
                    if (m == e) { fc = 0; flen = 0; }
                }
            }
        }
    }
    return fc != 0;
}

/* ---- the table under the caret ---- */

typedef struct Tbl {
    Text x;
    size_t first, last, caret, cline, crow;     /* lines of the table, the caret and its line, the caret's row (0 header, 1 delimiter, 2 first body row) */
    Row *rows;                                  /* one per line of the table */
    size_t nrows, ncols, col;                   /* columns (the most any row has) and the caret's */
    int *align;                                 /* per column: 0 none, 1 left, 2 center, 3 right */
    size_t *w;                                  /* per column: the width its cells and delimiter have now (at least 3) */
} Tbl;

static void tbl_free(Tbl *t) {
    size_t i;
    for (i = 0; i < t->nrows; i++) row_free(&t->rows[i]);
    free(t->rows); free(t->align); free(t->w);
    text_free(&t->x);
    memset(t, 0, sizeof *t);
}

static size_t cps(const char *p, size_t n) { size_t i, k = 0; for (i = 0; i < n; i++) if (((unsigned char)p[i] & 0xC0) != 0x80) k++; return k; }

static int align_of(const Text *x, const Cell *c) {
    int l = c->s < c->e && x->t[c->s] == ':', r = c->e > c->s && x->t[c->e - 1] == ':' && (c->e - c->s > 1 || !l);
    return l && r ? 2 : l ? 1 : r ? 3 : 0;
}

/* Finds the table the caret of the primary selection is in. Returns 1 and fills t (to be freed with tbl_free); 0 if there is none. */
static int tbl_load(ExDoc *d, Tbl *t) {
    size_t a, b, k, n = ex_doc_sel_count(d), pi = ex_doc_primary(d), line, delim = 0, c, r;
    int found = 0;
    ExSel s;
    memset(t, 0, sizeof *t);
    if (pi >= n) pi = 0;
    s = ex_doc_sel(d, pi);
    if (!text_load(&t->x, d)) return 0;
    t->caret = s.head;
    line = line_of(&t->x, s.head);
    t->cline = line;
    if (!pipe_line(&t->x, line)) goto no;
    a = line;
    while (a > 0 && pipe_line(&t->x, a - 1)) a--;
    b = line;
    while (b + 1 < t->x.nl && pipe_line(&t->x, b + 1)) b++;
    for (k = a + 1; k <= b; k++) {
        Row rh, rd;
        int ok = 0;
        if (!row_parse(&t->x, k - 1, &rh)) goto no;
        if (!row_parse(&t->x, k, &rd)) { row_free(&rh); goto no; }
        ok = is_delim(&t->x, &rd) && rh.n == rd.n;
        row_free(&rh); row_free(&rd);
        if (ok) { delim = k; found = 1; break; }
    }
    if (!found || line < delim - 1) goto no;
    t->first = delim - 1;
    t->last = b;
    if (in_fence(&t->x, t->first)) goto no;
    t->nrows = t->last - t->first + 1;
    t->rows = calloc(t->nrows, sizeof *t->rows);
    if (!t->rows) goto no;
    for (r = 0; r < t->nrows; r++) {
        if (!row_parse(&t->x, t->first + r, &t->rows[r])) { t->nrows = r; goto no; }
        if (t->rows[r].n > t->ncols) t->ncols = t->rows[r].n;
    }
    t->align = calloc(t->ncols ? t->ncols : 1, sizeof *t->align);
    t->w = calloc(t->ncols ? t->ncols : 1, sizeof *t->w);
    if (!t->align || !t->w) goto no;
    for (c = 0; c < t->ncols; c++) {
        t->w[c] = 3;
        if (c < t->rows[1].n) t->align[c] = align_of(&t->x, &t->rows[1].c[c]);
        for (r = 0; r < t->nrows; r++) {
            size_t wc;
            if (c >= t->rows[r].n) continue;
            wc = cps(t->x.t + t->rows[r].c[c].s, t->rows[r].c[c].e - t->rows[r].c[c].s);
            if (wc > t->w[c]) t->w[c] = wc;
        }
    }
    t->crow = line - t->first;
    {
        const Row *cr = &t->rows[t->crow];
        t->col = cr->n ? cr->n - 1 : 0;
        for (c = 0; c < cr->n; c++) if (t->caret <= cr->c[c].re) { t->col = c; break; }
        if (t->col >= t->ncols) t->col = t->ncols - 1;
    }
    return 1;
no:
    tbl_free(t);
    return 0;
}

/* ---- building text ---- */

typedef struct Sb { char *p; size_t n, cap; int oom; } Sb;
static void sb_put(Sb *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        char *p;
        while (nc < b->n + n + 1) nc *= 2;
        p = realloc(b->p, nc);
        if (!p) { b->oom = 1; return; }
        b->p = p; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void sb_fill(Sb *b, char c, size_t n) { while (n--) sb_put(b, &c, 1); }

typedef struct CellV { const char *p; size_t n; } CellV;

/* One line of the table: the indentation, then | cell | cell |, each cell padded to its column's width; the delimiter row is dashes with
 * the colons of the alignment. *pos gets, for each column, where its text starts. */
static void render_row(Sb *b, const char *indent, size_t ilen, size_t ncols, const size_t *w, const int *align, const CellV *cv, int delim, size_t *pos) {
    size_t c;
    sb_put(b, indent, ilen);
    sb_put(b, "|", 1);
    for (c = 0; c < ncols; c++) {
        sb_put(b, " ", 1);
        if (delim) {
            size_t n = w[c], base = b->n;
            if (pos) pos[c] = b->n;
            sb_fill(b, '-', n);
            if (!b->oom) {
                if (align[c] == 1 || align[c] == 2) b->p[base] = ':';
                if (align[c] == 3 || align[c] == 2) b->p[base + n - 1] = ':';
            }
        } else {
            size_t used = cps(cv[c].p, cv[c].n), pad = w[c] > used ? w[c] - used : 0, left = align[c] == 3 ? pad : align[c] == 2 ? pad / 2 : 0;
            sb_fill(b, ' ', left);
            if (pos) pos[c] = b->n;
            sb_put(b, cv[c].p, cv[c].n);
            sb_fill(b, ' ', pad - left);
        }
        sb_put(b, " |", 2);
    }
}

static size_t map1(size_t off, size_t start, size_t rm, size_t tlen) {
    if (off <= start) return off;
    if (off >= start + rm) return off - rm + tlen;
    return start + tlen;
}

static int commit(ExDoc *d, size_t start, size_t rm, const char *text, size_t tlen, size_t caret) {
    ExChange ch;
    ExSel after;
    ch.offset = start; ch.remove_len = rm; ch.text = text; ch.text_len = tlen;
    after.anchor = after.head = caret;
    return ex_doc_apply(d, &ch, 1, 0, &after, 1) == 0;
}

/* ---- the commands ---- */

static int can_row(const Tbl *t, ExTableCmd c) {
    size_t r = t->crow;
    switch (c) {
    case EX_TBL_ROW_ABOVE: return r >= 2;
    case EX_TBL_ROW_BELOW: return 1;
    case EX_TBL_DEL_ROW: case EX_TBL_DUP_ROW: return r >= 2;
    case EX_TBL_MOVE_ROW_UP: return r >= 3;
    case EX_TBL_MOVE_ROW_DOWN: return r >= 2 && t->cline < t->last;
    default: return 0;
    }
}

static int can_cmd(const Tbl *t, ExTableCmd c) {
    switch (c) {
    case EX_TBL_COL_LEFT: case EX_TBL_COL_RIGHT: return 1;
    case EX_TBL_ALIGN_LEFT: case EX_TBL_ALIGN_CENTER: case EX_TBL_ALIGN_RIGHT: return t->col < t->rows[1].n;
    case EX_TBL_DEL_COL: return t->ncols > 1;
    case EX_TBL_DUP_COL: return 1;
    case EX_TBL_MOVE_COL_LEFT: return t->col > 0;
    case EX_TBL_MOVE_COL_RIGHT: return t->col + 1 < t->ncols;
    default: return can_row(t, c);
    }
}

/* The line break to put between two lines of the table: the one the delimiter row ends with. */
static void eol_of(const Tbl *t, const char **eol, size_t *elen) {
    size_t ce = cend(&t->x, t->first + 1), end = t->first + 2 < t->x.nl ? t->x.ls[t->first + 2] : t->x.len;
    if (end > ce && end - ce <= 2) { *eol = t->x.t + ce; *elen = end - ce; }
    else { size_t ce0 = cend(&t->x, t->first), e0 = t->x.ls[t->first + 1]; *eol = t->x.t + ce0; *elen = e0 - ce0; }
}

static int do_columns(ExDoc *d, Tbl *t, ExTableCmd c) {
    size_t nc, *src, k, r, newcol, *pos = NULL, crow = t->crow;
    int *align = NULL, rc = 0;
    size_t *w = NULL;
    Sb sb = {0};
    CellV *cv = NULL;
    const char *indent = t->x.t + t->x.ls[t->first];
    size_t ilen = t->rows[0].indent_end - t->x.ls[t->first];
    switch (c) {
    case EX_TBL_COL_LEFT: case EX_TBL_COL_RIGHT: case EX_TBL_DUP_COL: nc = t->ncols + 1; break;
    case EX_TBL_DEL_COL: nc = t->ncols - 1; break;
    default: nc = t->ncols; break;
    }
    src = malloc((nc + 1) * sizeof *src);
    align = calloc(nc + 1, sizeof *align);
    w = calloc(nc + 1, sizeof *w);
    pos = calloc(nc + 1, sizeof *pos);
    cv = calloc(nc + 1, sizeof *cv);
    if (!src || !align || !w || !pos || !cv) goto out;
    #define NONE ((size_t)-1)
    for (k = 0; k < nc; k++) src[k] = k;
    newcol = t->col;
    switch (c) {
    case EX_TBL_COL_LEFT: for (k = t->col; k < nc; k++) src[k] = k == t->col ? NONE : k - 1; newcol = t->col; break;
    case EX_TBL_COL_RIGHT: for (k = t->col + 1; k < nc; k++) src[k] = k == t->col + 1 ? NONE : k - 1; newcol = t->col + 1; break;
    case EX_TBL_DUP_COL: for (k = t->col + 1; k < nc; k++) src[k] = k - 1; newcol = t->col + 1; break;
    case EX_TBL_DEL_COL: for (k = t->col; k < nc; k++) src[k] = k + 1; newcol = t->col < nc ? t->col : nc - 1; break;
    case EX_TBL_MOVE_COL_LEFT: src[t->col - 1] = t->col; src[t->col] = t->col - 1; newcol = t->col - 1; break;
    case EX_TBL_MOVE_COL_RIGHT: src[t->col + 1] = t->col; src[t->col] = t->col + 1; newcol = t->col + 1; break;
    default: goto out;
    }
    for (k = 0; k < nc; k++) {
        align[k] = src[k] == NONE ? 0 : t->align[src[k]];
        w[k] = 3;
        for (r = 0; r < t->nrows; r++) {
            const Row *row = &t->rows[r];
            size_t wc;
            if (r == 1 || src[k] == NONE || src[k] >= row->n) continue;
            wc = cps(t->x.t + row->c[src[k]].s, row->c[src[k]].e - row->c[src[k]].s);
            if (wc > w[k]) w[k] = wc;
        }
    }
    for (r = 0; r < t->nrows; r++) {
        const Row *row = &t->rows[r];
        size_t line = t->first + r, sepb = cend(&t->x, line), sepe = line + 1 < t->x.nl && line < t->last ? t->x.ls[line + 1] : sepb;
        for (k = 0; k < nc; k++) {
            if (src[k] == NONE || src[k] >= row->n) { cv[k].p = ""; cv[k].n = 0; }
            else { cv[k].p = t->x.t + row->c[src[k]].s; cv[k].n = row->c[src[k]].e - row->c[src[k]].s; }
        }
        render_row(&sb, indent, ilen, nc, w, align, cv, r == 1, r == crow ? pos : NULL);
        if (line < t->last) sb_put(&sb, t->x.t + sepb, sepe - sepb);
    }
    if (sb.oom) goto out;
    {
        size_t start = t->x.ls[t->first], end = cend(&t->x, t->last), caret = start + pos[newcol];
        if (end - start == sb.n && memcmp(t->x.t + start, sb.p, sb.n) == 0) goto out;
        rc = commit(d, start, end - start, sb.p, sb.n, caret);
    }
    #undef NONE
out:
    free(src); free(align); free(w); free(pos); free(cv); free(sb.p);
    return rc;
}

static int do_align(ExDoc *d, Tbl *t, ExTableCmd c) {
    const Cell *cell = &t->rows[1].c[t->col];
    size_t n = cell->e - cell->s, w = n < 3 ? 3 : n, base = 0;
    int a = c == EX_TBL_ALIGN_LEFT ? 1 : c == EX_TBL_ALIGN_CENTER ? 2 : 3;
    char *buf = malloc(w + 1);
    int rc = 0;
    if (!buf) return 0;
    memset(buf, '-', w);
    if (a == 1 || a == 2) buf[base] = ':';
    if (a == 3 || a == 2) buf[w - 1] = ':';
    if (n == w && memcmp(t->x.t + cell->s, buf, w) == 0) goto out;
    rc = commit(d, cell->s, n, buf, w, map1(t->caret, cell->s, n, w));
out:
    free(buf);
    return rc;
}

static int do_rows(ExDoc *d, Tbl *t, ExTableCmd c) {
    const char *eol;
    size_t elen, ls = t->x.ls[t->cline], ce = cend(&t->x, t->cline), r = t->crow, k;
    int rc = 0;
    eol_of(t, &eol, &elen);
    if (c == EX_TBL_ROW_ABOVE || c == EX_TBL_ROW_BELOW) {
        Sb sb = {0};
        size_t *pos = calloc(t->ncols + 1, sizeof *pos), at, caret;
        CellV *cv = calloc(t->ncols + 1, sizeof *cv);
        const char *indent = t->x.t + t->x.ls[t->first];
        size_t ilen = t->rows[0].indent_end - t->x.ls[t->first];
        if (!pos || !cv) { free(pos); free(cv); return 0; }
        for (k = 0; k < t->ncols; k++) { cv[k].p = ""; cv[k].n = 0; }
        if (c == EX_TBL_ROW_ABOVE) {
            render_row(&sb, indent, ilen, t->ncols, t->w, t->align, cv, 0, pos);
            caret = ls + pos[t->col];
            sb_put(&sb, eol, elen);
            if (!sb.oom) rc = commit(d, ls, 0, sb.p, sb.n, caret);
        } else {
            size_t line = r < 2 ? t->first + 1 : t->cline;      /* below the header goes under the delimiter row */
            at = cend(&t->x, line);
            sb_put(&sb, eol, elen);
            render_row(&sb, indent, ilen, t->ncols, t->w, t->align, cv, 0, pos);
            caret = at + pos[t->col];
            if (!sb.oom) rc = commit(d, at, 0, sb.p, sb.n, caret);
        }
        free(sb.p); free(pos); free(cv);
        return rc;
    }
    if (c == EX_TBL_DUP_ROW) {
        Sb sb = {0};
        sb_put(&sb, eol, elen);
        sb_put(&sb, t->x.t + ls, ce - ls);
        if (!sb.oom) rc = commit(d, ce, 0, sb.p, sb.n, ce + elen + (t->caret > ce ? ce - ls : t->caret - ls));
        free(sb.p);
        return rc;
    }
    if (c == EX_TBL_DEL_ROW) {
        size_t start, rm, tcol = t->col, land;
        if (t->cline < t->last) { start = ls; rm = t->x.ls[t->cline + 1] - ls; land = start; }
        else { start = cend(&t->x, t->cline - 1); rm = ce - start; land = t->x.ls[t->cline - 1]; }
        {
            /* the caret goes to the same column of the row that takes this one's place (the one above, for the last row) */
            const Row *nr = t->cline < t->last ? &t->rows[r + 1] : &t->rows[r - 1];
            size_t col = tcol < nr->n ? tcol : (nr->n ? nr->n - 1 : 0), off = nr->n ? nr->c[col].s : land;
            return commit(d, start, rm, "", 0, map1(off, start, rm, 0));
        }
    }
    if (c == EX_TBL_MOVE_ROW_UP || c == EX_TBL_MOVE_ROW_DOWN) {
        size_t la = c == EX_TBL_MOVE_ROW_UP ? t->cline - 1 : t->cline, lb = la + 1;       /* swap the lines la and lb */
        size_t as = t->x.ls[la], ae = cend(&t->x, la), bs = t->x.ls[lb], be = cend(&t->x, lb), sepn = bs - ae;
        Sb sb = {0};
        size_t within = t->caret > ce ? ce - ls : t->caret - ls, caret;
        sb_put(&sb, t->x.t + bs, be - bs);
        sb_put(&sb, t->x.t + ae, sepn);
        sb_put(&sb, t->x.t + as, ae - as);
        if (c == EX_TBL_MOVE_ROW_UP) caret = as + within;
        else caret = as + (be - bs) + sepn + within;
        if (!sb.oom) rc = commit(d, as, be - as, sb.p, sb.n, caret);
        free(sb.p);
        return rc;
    }
    return 0;
}

int ex_doc_table_can(ExDoc *d, ExTableCmd c) {
    Tbl t;
    size_t n = ex_doc_sel_count(d), pi = ex_doc_primary(d), line, ls, le;
    char buf[512];
    ExSel s;
    int ok;
    if ((int)c < 0 || c >= EX_TBL_COUNT) return 0;
    if (pi >= n) pi = 0;
    s = ex_doc_sel(d, pi);
    /* a quick no for the usual case (the caret is not on a line with a pipe), without copying the note */
    line = ex_buffer_line_of_offset(ex_doc_buffer(d), s.head);
    ls = ex_buffer_line_start(ex_doc_buffer(d), line);
    le = ex_buffer_line_start(ex_doc_buffer(d), line + 1);
    {
        size_t got = ex_buffer_read(ex_doc_buffer(d), ls, buf, le - ls < sizeof buf ? le - ls : sizeof buf), i;
        int pipe = 0;
        for (i = 0; i < got; i++) if (buf[i] == '|') { pipe = 1; break; }
        if (!pipe && le - ls <= sizeof buf) return 0;
    }
    if (!tbl_load(d, &t)) return 0;
    ok = can_cmd(&t, c);
    tbl_free(&t);
    return ok;
}

int ex_doc_table(ExDoc *d, ExTableCmd c) {
    Tbl t;
    int rc;
    if ((int)c < 0 || c >= EX_TBL_COUNT) return 0;
    if (!tbl_load(d, &t)) return 0;
    if (!can_cmd(&t, c)) { tbl_free(&t); return 0; }
    switch (c) {
    case EX_TBL_ALIGN_LEFT: case EX_TBL_ALIGN_CENTER: case EX_TBL_ALIGN_RIGHT: rc = do_align(d, &t, c); break;
    case EX_TBL_ROW_ABOVE: case EX_TBL_ROW_BELOW: case EX_TBL_DEL_ROW: case EX_TBL_DUP_ROW: case EX_TBL_MOVE_ROW_UP: case EX_TBL_MOVE_ROW_DOWN:
        rc = do_rows(d, &t, c);
        break;
    default: rc = do_columns(d, &t, c); break;
    }
    tbl_free(&t);
    return rc;
}
