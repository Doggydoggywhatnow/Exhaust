/* Typing helpers (docs/formats.md, "Typing helpers: smart lists and auto-pairing"): what Enter does in a list or a quote, and the
 * pairing of brackets and Markdown marks. They work on a document and one selection, so tests drive them without a window; the editor
 * decides when they apply (not in plain fields, not with several cursors, only for a key that types one character) and tells them whether
 * the caret is in text where Markdown has no meaning (`in_literal`). Every function returns 1 if it changed the text or the caret and 0
 * if the key should type as usual. Each change is one undo step of its own. */
#include <stdio.h>
#include <string.h>

#include "exhaust_editor.h"

#define LINE_CAP 8192

static int is_blank(char c) { return c == ' ' || c == '\t'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
/* A byte of a word: ASCII letters and digits, and every byte of a multi-byte character. */
static int is_word(unsigned char c) { return is_digit((char)c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }

static size_t lo_of(ExSel s) { return s.anchor < s.head ? s.anchor : s.head; }
static size_t hi_of(ExSel s) { return s.anchor < s.head ? s.head : s.anchor; }

/* ---- Enter ---- */

typedef struct Prefix {
    size_t quote_end;      /* after the quote marks and the one blank that follows the last of them; 0 if the line is not in a quote */
    size_t indent;         /* blanks between quote_end and the marker */
    size_t mstart, mend;   /* the list marker, mend after its last character */
    size_t box;            /* where the task box starts, when task is set */
    size_t content;        /* where the text of the item (or of the quoted line) starts */
    size_t min_col;        /* the first column an Enter with a caret counts from: content, or just past the marker when nothing follows it */
    int list, task, number;
    unsigned long n;       /* the number of a numbered item */
    char delim;            /* '.' or ')' for a numbered item */
} Prefix;

static void prefix_parse(const char *ln, size_t n, Prefix *p) {
    size_t i = 0, j;
    memset(p, 0, sizeof *p);
    while (i < n && is_blank(ln[i])) i++;
    if (i < n && ln[i] == '>') {
        while (i < n && ln[i] == '>') {
            i++;
            if (i < n && ln[i] == ' ') i++;
            else break;                                    /* ">text": no blank, and no second mark after it */
        }
        p->quote_end = i;
    } else i = 0;
    j = i;
    while (j < n && is_blank(ln[j])) j++;
    p->indent = j - p->quote_end;
    p->mstart = j;
    if (j + 1 < n && (ln[j] == '-' || ln[j] == '*' || ln[j] == '+') && is_blank(ln[j + 1])) {
        p->list = 1; p->mend = j + 1;
    } else {
        size_t k = j;
        unsigned long v = 0;
        while (k < n && is_digit(ln[k]) && k - j < 9) { v = v * 10 + (unsigned long)(ln[k] - '0'); k++; }
        if (k > j && k + 1 < n && (ln[k] == '.' || ln[k] == ')') && is_blank(ln[k + 1])) {
            p->list = 1; p->number = 1; p->n = v; p->delim = ln[k]; p->mend = k + 1;
        }
    }
    if (!p->list) { p->content = p->min_col = p->quote_end; return; }
    j = p->mend;
    while (j < n && is_blank(ln[j])) j++;
    if (j + 3 <= n && ln[j] == '[' && (ln[j + 1] == ' ' || ln[j + 1] == 'x' || ln[j + 1] == 'X') && ln[j + 2] == ']' && (j + 3 == n || is_blank(ln[j + 3]))) {
        p->task = 1; p->box = j;
        j += 3;
        while (j < n && is_blank(ln[j])) j++;
    }
    p->content = j;
    p->min_col = j;
    if (j >= n) { size_t m = (p->task ? p->box + 3 : p->mend) + 1; p->min_col = m < n ? m : n; }
}

int ex_doc_smart_enter(ExDoc *d, int in_literal) {
    const ExBuffer *b = ex_doc_buffer(d);
    ExSel s, after;
    size_t lo, hi, line, ls, le, n, i, w;
    char ln[LINE_CAP], nw[LINE_CAP + 64], last;
    int truncated = 0, empty;
    Prefix p;
    ExChange c;
    if (in_literal || ex_doc_sel_count(d) != 1) return 0;
    s = ex_doc_sel(d, 0);
    lo = lo_of(s); hi = hi_of(s);
    line = ex_buffer_line_of_offset(b, lo);
    ls = ex_buffer_line_start(b, line);
    le = line + 1 < ex_buffer_line_count(b) ? ex_buffer_line_start(b, line + 1) - 1 : ex_buffer_size(b);
    if (le > ls && ex_buffer_read(b, le - 1, &last, 1) == 1 && last == '\r') le--;
    n = le - ls;
    if (n > LINE_CAP - 1) { n = LINE_CAP - 1; truncated = 1; }
    n = ex_buffer_read(b, ls, ln, n);
    ln[n] = 0;
    prefix_parse(ln, n, &p);
    if (!p.list && !p.quote_end) return 0;
    if (lo - ls < p.min_col) return 0;                              /* the caret is inside the marker: a plain line break */
    i = p.content;
    while (i < n && is_blank(ln[i])) i++;
    empty = !truncated && i >= n;
    if (empty && lo == hi) {
        /* nothing after the prefix: leave the list or the quote */
        if (p.list && p.indent > 0) {
            size_t k = 0, end = p.quote_end + p.indent;
            if (ln[end - 1] == '\t') k = 1;
            else while (k < 4 && k < p.indent && ln[end - 1 - k] == ' ') k++;
            c.offset = ls + end - k; c.remove_len = k;
            after.anchor = after.head = lo - k;
        } else if (p.list) {
            c.offset = ls + p.quote_end; c.remove_len = le - (ls + p.quote_end);
            after.anchor = after.head = ls + p.quote_end;
        } else {
            c.offset = ls; c.remove_len = le - ls;
            after.anchor = after.head = ls;
        }
        c.text = ""; c.text_len = 0;
        return ex_doc_apply(d, &c, 1, 0, &after, 1) == 0;
    }
    /* the next line starts with the same prefix */
    w = 0;
    nw[w++] = '\n';
    if (!p.list) {
        memcpy(nw + w, ln, p.quote_end); w += p.quote_end;
    } else {
        memcpy(nw + w, ln, p.mstart); w += p.mstart;                       /* the blanks and quote marks before the marker */
        if (p.number) w += (size_t)snprintf(nw + w, 32, "%lu%c", p.n + 1, p.delim);
        else nw[w++] = ln[p.mstart];
        for (i = p.mend; i < (p.task ? p.box : p.content); i++) nw[w++] = ln[i];   /* the blanks after the marker, as they were */
        if (p.task) {
            nw[w++] = '['; nw[w++] = ' '; nw[w++] = ']';
            for (i = p.box + 3; i < p.content; i++) nw[w++] = ln[i];
        }
    }
    c.offset = lo; c.remove_len = hi - lo; c.text = nw; c.text_len = w;
    after.anchor = after.head = lo + w;
    return ex_doc_apply(d, &c, 1, 0, &after, 1) == 0;
}

/* ---- typing a bracket, a quote or a Markdown mark ---- */

static char closer_of(char c) {
    switch (c) { case '(': return ')'; case '[': return ']'; case '{': return '}'; default: return c; }
}
static int next_ok(int next) { return next == 0 || next == '\n' || next == '\r' || is_blank((char)next) || next == ')' || next == ']' || next == '}'; }

static int wrap(ExDoc *d, ExSel s, char open, char close) {
    size_t lo = lo_of(s), hi = hi_of(s);
    ExChange ch[2];
    ExSel after;
    ch[0].offset = lo; ch[0].remove_len = 0; ch[0].text = &open; ch[0].text_len = 1;
    ch[1].offset = hi; ch[1].remove_len = 0; ch[1].text = &close; ch[1].text_len = 1;
    if (s.anchor <= s.head) { after.anchor = lo + 1; after.head = hi + 1; }
    else { after.anchor = hi + 1; after.head = lo + 1; }
    return ex_doc_apply(d, ch, 2, 0, &after, 1) == 0;
}

static int insert_pair(ExDoc *d, size_t at, char open, char close) {
    char t[2];
    ExChange c;
    ExSel after;
    t[0] = open; t[1] = close;
    c.offset = at; c.remove_len = 0; c.text = t; c.text_len = 2;
    after.anchor = after.head = at + 1;
    return ex_doc_apply(d, &c, 1, 0, &after, 1) == 0;
}

int ex_doc_smart_type(ExDoc *d, const char *text, size_t len, const ExSmartOptions *o, int in_literal) {
    const ExBuffer *b = ex_doc_buffer(d);
    ExSel s;
    size_t lo, hi;
    char c, prev = 0, nx = 0;
    int have_prev, have_next, sel, bracket, md;
    if (len != 1 || !o || (!o->brackets && !o->markdown) || ex_doc_sel_count(d) != 1) return 0;
    c = text[0];
    bracket = c == '(' || c == '[' || c == '{' || c == '"' || c == ')' || c == ']' || c == '}';
    md = c == '*' || c == '_' || c == '~' || c == '=' || c == '$' || c == '`';
    if (!bracket && !md) return 0;
    s = ex_doc_sel(d, 0);
    lo = lo_of(s); hi = hi_of(s);
    sel = lo != hi;
    have_prev = lo > 0 && ex_buffer_read(b, lo - 1, &prev, 1) == 1;
    have_next = hi < ex_buffer_size(b) && ex_buffer_read(b, hi, &nx, 1) == 1;
    if (bracket) {
        if (!o->brackets) return 0;
        if (c == ')' || c == ']' || c == '}' || c == '"') {
            if (!sel && have_next && nx == c) { ex_doc_set_caret(d, lo + 1); ex_doc_seal(d); return 1; }     /* type over the closing character */
        }
        if (c == '(' || c == '[' || c == '{' || c == '"') {
            if (sel) return wrap(d, s, c, closer_of(c));
            if (!next_ok(have_next ? (unsigned char)nx : 0)) return 0;
            if (c == '"' && have_prev && (is_word((unsigned char)prev) || prev == '"')) return 0;
            return insert_pair(d, lo, c, closer_of(c));
        }
        return 0;
    }
    if (o->markdown && !in_literal) {
        if (sel) return wrap(d, s, c, c);
        if (c == '`') {
            if (have_next && nx == '`') { ex_doc_set_caret(d, lo + 1); ex_doc_seal(d); return 1; }
            if (have_prev && (prev == '`' || is_word((unsigned char)prev))) return 0;
            if (!next_ok(have_next ? (unsigned char)nx : 0)) return 0;
            return insert_pair(d, lo, c, c);
        }
    }
    return 0;
}

int ex_doc_smart_backspace(ExDoc *d, const ExSmartOptions *o, int in_literal) {
    const ExBuffer *b = ex_doc_buffer(d);
    ExSel s, after;
    size_t at;
    char pv, nx;
    ExChange c;
    int ok = 0;
    if (!o || (!o->brackets && !o->markdown) || ex_doc_sel_count(d) != 1) return 0;
    s = ex_doc_sel(d, 0);
    if (s.anchor != s.head || s.head == 0) return 0;
    at = s.head;
    if (ex_buffer_read(b, at - 1, &pv, 1) != 1 || ex_buffer_read(b, at, &nx, 1) != 1) return 0;
    if (o->brackets && ((pv == '(' && nx == ')') || (pv == '[' && nx == ']') || (pv == '{' && nx == '}') || (pv == '"' && nx == '"'))) ok = 1;
    if (o->markdown && !in_literal && pv == '`' && nx == '`') ok = 1;
    if (!ok) return 0;
    c.offset = at - 1; c.remove_len = 2; c.text = ""; c.text_len = 0;
    after.anchor = after.head = at - 1;
    return ex_doc_apply(d, &c, 1, 0, &after, 1) == 0;
}
