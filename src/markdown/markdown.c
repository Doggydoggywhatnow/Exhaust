/* The block tokenizer, the document of top-level blocks and the incremental update. The inline
 * constructs are in md_inline.c. See docs/formats.md for the kinds and the rules. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"
#include "md_internal.h"

/* ---- names and callout types ---- */

static const struct { const char *name; ExCalloutType type; } k_callouts[] = {
    {"note", EX_CALLOUT_NOTE}, {"abstract", EX_CALLOUT_ABSTRACT}, {"summary", EX_CALLOUT_ABSTRACT}, {"tldr", EX_CALLOUT_ABSTRACT}, {"info", EX_CALLOUT_INFO},
    {"todo", EX_CALLOUT_TODO}, {"tip", EX_CALLOUT_TIP}, {"hint", EX_CALLOUT_TIP}, {"important", EX_CALLOUT_TIP}, {"success", EX_CALLOUT_SUCCESS},
    {"check", EX_CALLOUT_SUCCESS}, {"done", EX_CALLOUT_SUCCESS}, {"question", EX_CALLOUT_QUESTION}, {"help", EX_CALLOUT_QUESTION}, {"faq", EX_CALLOUT_QUESTION},
    {"warning", EX_CALLOUT_WARNING}, {"caution", EX_CALLOUT_WARNING}, {"attention", EX_CALLOUT_WARNING}, {"failure", EX_CALLOUT_FAILURE},
    {"fail", EX_CALLOUT_FAILURE}, {"missing", EX_CALLOUT_FAILURE}, {"danger", EX_CALLOUT_DANGER}, {"error", EX_CALLOUT_DANGER}, {"bug", EX_CALLOUT_BUG},
    {"example", EX_CALLOUT_EXAMPLE}, {"quote", EX_CALLOUT_QUOTE}, {"cite", EX_CALLOUT_QUOTE}};

ExCalloutType ex_callout_type(const char *name, size_t len, int *known) {
    size_t i, k;
    for (i = 0; i < sizeof k_callouts / sizeof k_callouts[0]; i++) {
        const char *n = k_callouts[i].name;
        if (strlen(n) != len) continue;
        for (k = 0; k < len && tolower((unsigned char)name[k]) == n[k]; k++) {}
        if (k == len) { if (known) *known = 1; return k_callouts[i].type; }
    }
    if (known) *known = 0;
    return EX_CALLOUT_NOTE;
}

const char *ex_callout_name(ExCalloutType t) {
    static const char *const n[EX_CALLOUT_COUNT] = {"note", "abstract", "info", "todo", "tip", "success", "question", "warning", "failure", "danger", "bug", "example", "quote"};
    return t >= 0 && t < EX_CALLOUT_COUNT ? n[t] : "note";
}

const char *ex_md_kind_name(ExMdKind k) {
    static const char *const n[EX_MD_KIND_COUNT] = {
        "", "frontmatter", "heading", "paragraph", "blockquote", "callout", "list", "list_item", "task", "code_block", "code_info", "code_body", "math_block",
        "table", "table_row", "table_cell", "hr", "footnote_def", "comment_block", "html_block", "callout_type", "callout_title", "emphasis", "strong", "strike",
        "highlight", "code_span", "math_inline", "link", "image", "link_text", "link_dest", "link_title", "wikilink", "embed", "link_target", "link_heading",
        "link_block", "link_alias", "autolink", "footnote_ref", "inline_footnote", "tag", "block_id", "comment_inline", "html_inline", "escape", "hard_break", "marker"};
    return k > 0 && k < EX_MD_KIND_COUNT ? n[k] : "";
}

/* ---- spans of the block being parsed ---- */

void md_emit(Ctx *c, ExMdKind kind, size_t start, size_t end, int flags, int level) {
    if (end < start) end = start;
    if (c->nsp == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 128;
        RawSpan *ns = realloc(c->sp, nc * sizeof *ns);
        if (!ns) { c->oom = 1; return; }
        c->sp = ns;
        c->cap = nc;
    }
    c->sp[c->nsp].kind = (uint8_t)kind; c->sp[c->nsp].flags = (uint8_t)flags; c->sp[c->nsp].level = (uint16_t)level;
    c->sp[c->nsp].start = start; c->sp[c->nsp].end = end; c->sp[c->nsp].seq = c->seq++;
    c->nsp++;
}

#define EMIT(k, s, e) md_emit(c, k, s, e, 0, 0)

/* ---- lines ---- */

typedef struct Src {
    VLine *v;
    size_t n, cap;
    int lazy;                 /* lines are read from the text as they are asked for (the top level) */
    size_t pos;               /* start of the next unread physical line */
    int doc_start;            /* line 0 is the first line of the note */
} Src;

static int src_has(Ctx *c, Src *s, size_t i) {
    while (s->lazy && i >= s->n && s->pos < c->len) {
        const char *nl = memchr(c->t + s->pos, '\n', c->len - s->pos);
        size_t e = nl ? (size_t)(nl - c->t) : c->len, next = nl ? e + 1 : c->len, ce = e;
        if (s->n == s->cap) {
            size_t nc = s->cap ? s->cap * 2 : 64;
            VLine *nv = realloc(s->v, nc * sizeof *nv);
            if (!nv) { c->oom = 1; return 0; }
            s->v = nv;
            s->cap = nc;
        }
        if (ce > s->pos && c->t[ce - 1] == '\r') ce--;
        s->v[s->n].s = s->pos;
        s->v[s->n].e = ce;
        s->n++;
        s->pos = next;
    }
    return i < s->n;
}

static int ws(char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; }

static int is_blank(const Ctx *c, VLine l) {
    size_t k;
    for (k = l.s; k < l.e; k++) if (!ws(c->t[k])) return 0;
    return 1;
}

/* The first non-blank byte of a line and the columns of indentation before it (a tab goes to the next multiple of 4). */
static size_t indent(const Ctx *c, VLine l, size_t *cols) {
    size_t k = l.s, col = 0;
    while (k < l.e && (c->t[k] == ' ' || c->t[k] == '\t')) { col += c->t[k] == '\t' ? 4 - col % 4 : 1; k++; }
    if (cols) *cols = col;
    return k;
}

static size_t trim_end(const Ctx *c, size_t s, size_t e) { while (e > s && ws(c->t[e - 1])) e--; return e; }

/* The line without the first `cols` columns of indentation (a tab that crosses the edge is taken whole). */
static VLine strip_cols(const Ctx *c, VLine l, size_t cols) {
    size_t k = l.s, col = 0;
    while (k < l.e && col < cols && (c->t[k] == ' ' || c->t[k] == '\t')) { col += c->t[k] == '\t' ? 4 - col % 4 : 1; k++; }
    l.s = k;
    return l;
}

/* ---- the block starts ---- */

typedef struct Atx { int level; size_t hs, cs, ce, cl_s, cl_e; } Atx;

static int match_atx(const Ctx *c, VLine l, Atx *a) {
    size_t cols, p = indent(c, l, &cols), k, e;
    if (cols >= 4) return 0;
    for (k = p; k < l.e && c->t[k] == '#'; k++) {}
    a->level = (int)(k - p);
    if (a->level < 1 || a->level > 6) return 0;
    if (k < l.e && c->t[k] != ' ' && c->t[k] != '\t') return 0;
    a->hs = p;
    while (k < l.e && (c->t[k] == ' ' || c->t[k] == '\t')) k++;
    a->cs = k;
    a->ce = trim_end(c, k, l.e);
    a->cl_s = a->cl_e = 0;
    e = a->ce;
    while (e > a->cs && c->t[e - 1] == '#') e--;
    if (e < a->ce && (e == a->cs || c->t[e - 1] == ' ' || c->t[e - 1] == '\t')) { a->cl_s = e; a->cl_e = a->ce; a->ce = trim_end(c, a->cs, e); }
    return 1;
}

typedef struct Fence { char ch; size_t fs, len, is, ie; } Fence;

static int match_fence(const Ctx *c, VLine l, Fence *f) {
    size_t cols, p = indent(c, l, &cols), k;
    char ch;
    if (cols >= 4 || p >= l.e || (c->t[p] != '`' && c->t[p] != '~')) return 0;
    ch = c->t[p];
    for (k = p; k < l.e && c->t[k] == ch; k++) {}
    if (k - p < 3) return 0;
    f->ch = ch; f->fs = p; f->len = k - p;
    while (k < l.e && ws(c->t[k])) k++;
    f->is = k;
    f->ie = trim_end(c, k, l.e);
    if (ch == '`') { size_t q; for (q = f->is; q < f->ie; q++) if (c->t[q] == '`') return 0; }
    return 1;
}

static int closes_fence(const Ctx *c, VLine l, const Fence *f, size_t *cs, size_t *clen) {
    size_t cols, p = indent(c, l, &cols), k;
    if (cols >= 4 || p >= l.e || c->t[p] != f->ch) return 0;
    for (k = p; k < l.e && c->t[k] == f->ch; k++) {}
    if (k - p < f->len) return 0;
    if (trim_end(c, k, l.e) != k) return 0;
    *cs = p; *clen = k - p;
    return 1;
}

static int match_hr(const Ctx *c, VLine l) {
    size_t cols, p = indent(c, l, &cols), k, n = 0;
    char ch;
    if (cols >= 4 || p >= l.e) return 0;
    ch = c->t[p];
    if (ch != '*' && ch != '-' && ch != '_') return 0;
    for (k = p; k < l.e; k++) { if (c->t[k] == ch) n++; else if (!ws(c->t[k])) return 0; }
    return n >= 3;
}

static int match_setext(const Ctx *c, VLine l, int *level, size_t *s, size_t *e) {
    size_t cols, p = indent(c, l, &cols), k;
    if (cols >= 4 || p >= l.e || (c->t[p] != '=' && c->t[p] != '-')) return 0;
    for (k = p; k < l.e && c->t[k] == c->t[p]; k++) {}
    if (trim_end(c, k, l.e) != k) return 0;
    *level = c->t[p] == '=' ? 1 : 2;
    *s = p; *e = k;
    return 1;
}

typedef struct LM { size_t ms, me, cs, col; int ordered, empty; char delim; unsigned long num; } LM;

static int match_list(const Ctx *c, VLine l, LM *m) {
    size_t cols, p = indent(c, l, &cols), k, col, gap = 0;
    if (cols >= 4 || p >= l.e) return 0;
    m->ordered = 0; m->num = 0;
    if (c->t[p] == '-' || c->t[p] == '*' || c->t[p] == '+') { k = p + 1; m->delim = c->t[p]; }
    else if (isdigit((unsigned char)c->t[p])) {
        for (k = p; k < l.e && isdigit((unsigned char)c->t[k]) && k - p < 9; k++) m->num = m->num * 10 + (unsigned long)(c->t[k] - '0');
        if (k >= l.e || (c->t[k] != '.' && c->t[k] != ')') || (k < l.e && isdigit((unsigned char)c->t[k]))) return 0;
        m->ordered = 1; m->delim = c->t[k]; k++;
    } else return 0;
    if (k < l.e && c->t[k] != ' ' && c->t[k] != '\t') return 0;
    m->ms = p; m->me = k;
    col = cols + (k - p);
    {
        size_t q = k, gc = col;
        while (q < l.e && (c->t[q] == ' ' || c->t[q] == '\t')) { gc += c->t[q] == '\t' ? 4 - gc % 4 : 1; q++; }
        gap = gc - col;
        if (q >= l.e || trim_end(c, q, l.e) == q) { m->empty = 1; m->cs = l.e; m->col = col + 1; return 1; }
        m->empty = 0;
        if (gap > 4) { m->cs = k + 1; m->col = col + 1; }       /* more than four spaces: indented code inside the item */
        else { m->cs = q; m->col = col + gap; }
    }
    return 1;
}

typedef struct Fn { size_t fs, id_s, id_e, cs; } Fn;

static int match_footnote_def(const Ctx *c, VLine l, Fn *f) {
    size_t cols, p = indent(c, l, &cols), k;
    if (cols >= 4 || p + 3 >= l.e || c->t[p] != '[' || c->t[p + 1] != '^') return 0;
    for (k = p + 2; k < l.e && c->t[k] != ']' && !ws(c->t[k]) && c->t[k] != '['; k++) {}
    if (k >= l.e || c->t[k] != ']' || k == p + 2 || k + 1 >= l.e || c->t[k + 1] != ':') return 0;
    f->fs = p; f->id_s = p + 2; f->id_e = k;
    k += 2;
    while (k < l.e && ws(c->t[k])) k++;
    f->cs = k;
    return 1;
}

static int match_math_open(const Ctx *c, VLine l) {
    size_t cols, p = indent(c, l, &cols);
    return cols < 4 && p + 1 < l.e && c->t[p] == '$' && c->t[p + 1] == '$';
}

static int match_comment_open(const Ctx *c, VLine l, size_t *p_out, int *closed_here) {
    size_t cols, p = indent(c, l, &cols), k;
    if (cols >= 4 || p + 1 >= l.e || c->t[p] != '%' || c->t[p + 1] != '%') return 0;
    *p_out = p;
    *closed_here = 0;
    for (k = p + 2; k + 1 < l.e; k++) if (c->t[k] == '%' && c->t[k + 1] == '%') { *closed_here = 1; break; }
    return 1;
}

static int ieq(const char *a, size_t n, const char *b) {
    size_t i;
    if (strlen(b) != n) return 0;
    for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != b[i]) return 0;
    return 1;
}

static const char *const k_block_tags[] = {"address", "article", "aside", "blockquote", "body", "caption", "center", "col", "colgroup", "dd", "details", "dialog", "dir", "div", "dl", "dt",
    "fieldset", "figcaption", "figure", "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr", "html", "iframe", "legend", "li",
    "link", "main", "menu", "nav", "noframes", "ol", "optgroup", "option", "p", "param", "section", "source", "summary", "table", "tbody", "td", "tfoot", "th", "thead",
    "title", "tr", "track", "ul"};

/* 0 not an HTML block; 1 ends at the closing tag (pre, script, style, textarea); 2 a comment; 3 ends at a blank line. */
static int match_html_block(const Ctx *c, VLine l, char name[16]) {
    size_t cols, p = indent(c, l, &cols), k, ns, i;
    int closing;
    if (cols >= 4 || p + 1 >= l.e || c->t[p] != '<') return 0;
    if (p + 3 < l.e && memcmp(c->t + p, "<!--", 4) == 0) return 2;
    closing = c->t[p + 1] == '/';
    ns = p + 1 + (size_t)closing;
    for (k = ns; k < l.e && (isalnum((unsigned char)c->t[k]) || c->t[k] == '-') ; k++) {}
    if (k == ns || k - ns >= 16 || !isalpha((unsigned char)c->t[ns])) return 0;
    if (k < l.e && c->t[k] != ' ' && c->t[k] != '\t' && c->t[k] != '>' && !(c->t[k] == '/' && k + 1 < l.e && c->t[k + 1] == '>')) return 0;
    for (i = 0; i < k - ns; i++) name[i] = (char)tolower((unsigned char)c->t[ns + i]);
    name[k - ns] = 0;
    if (!closing && (ieq(c->t + ns, k - ns, "pre") || ieq(c->t + ns, k - ns, "script") || ieq(c->t + ns, k - ns, "style") || ieq(c->t + ns, k - ns, "textarea"))) return 1;
    for (i = 0; i < sizeof k_block_tags / sizeof k_block_tags[0]; i++) if (strcmp(k_block_tags[i], name) == 0) return 3;
    return 0;
}

/* Does this line start a block that ends the paragraph before it (so it is not a continuation line)? With
 * `strict` (a paragraph being continued) a list item interrupts only if it is not empty and, when ordered, starts
 * at 1; otherwise (the line did not match the container's prefix, so it is lazy continuation at best) any list
 * item starts a new item. */
static int starts_block(const Ctx *c, VLine l, int depth, int strict) {
    LM m;
    Atx a;
    Fence f;
    Fn fn;
    char name[16];
    size_t cols, p;
    if (match_atx(c, l, &a) || match_fence(c, l, &f) || match_hr(c, l) || match_footnote_def(c, l, &fn)) return 1;
    p = indent(c, l, &cols);
    if (cols < 4 && p < l.e && c->t[p] == '>' && depth < MD_MAX_DEPTH) return 1;
    if (depth < MD_MAX_DEPTH && match_list(c, l, &m) && (!strict || (!m.empty && (!m.ordered || m.num == 1)))) return 1;
    if (match_html_block(c, l, name)) return 1;
    if (cols < 4 && p + 1 < l.e && c->t[p] == '$' && c->t[p + 1] == '$' && trim_end(c, p + 2, l.e) == p + 2) return 1;
    return 0;
}

/* Would the block parser give this line to something that is not a table, an indented code block or a paragraph?
 * (It tries these in a fixed order; a table is only recognized on a line that would start a paragraph.) */
static int starts_non_paragraph(const Ctx *c, VLine l, int depth) {
    LM m;
    Atx a;
    Fence f;
    Fn fn;
    char name[16];
    size_t cols, p, cp;
    int closed;
    if (match_atx(c, l, &a) || match_fence(c, l, &f) || match_hr(c, l) || match_footnote_def(c, l, &fn) || match_math_open(c, l)) return 1;
    p = indent(c, l, &cols);
    if (cols < 4 && p < l.e && c->t[p] == '>' && depth < MD_MAX_DEPTH) return 1;
    if (depth < MD_MAX_DEPTH && match_list(c, l, &m)) return 1;
    if (match_comment_open(c, l, &cp, &closed) && !closed) return 1;
    return match_html_block(c, l, name) != 0;
}

/* ---- tables ---- */

#define MAX_CELLS 256

typedef struct Cells { size_t cs[MAX_CELLS], ce[MAX_CELLS], pipes[MAX_CELLS + 1]; size_t n, npipes; } Cells;

/* Splits a table row at unescaped bars. */
static void split_cells(const Ctx *c, VLine l, Cells *out) {
    size_t p = indent(c, l, NULL), e = trim_end(c, p, l.e), k, cs;
    out->n = out->npipes = 0;
    if (p < e && c->t[p] == '|') { out->pipes[out->npipes++] = p; p++; }
    cs = p;
    for (k = p; k <= e; k++) {
        if (k < e && c->t[k] == '\\') { k++; continue; }
        if (k == e || c->t[k] == '|') {
            size_t a = cs, b = k;
            if (k == e && a >= b) break;                                   /* nothing after the last bar */
            while (a < b && ws(c->t[a])) a++;
            while (b > a && ws(c->t[b - 1])) b--;
            if (out->n < MAX_CELLS) { out->cs[out->n] = a; out->ce[out->n] = b; out->n++; }
            if (k < e && out->npipes < MAX_CELLS) out->pipes[out->npipes++] = k;
            cs = k + 1;
        }
    }
}

/* A delimiter row: cells of dashes with optional colons. Fills the alignment of each column. */
static int match_delim_row(const Ctx *c, VLine l, int *align, size_t *ncols) {
    Cells cells;
    size_t i, k;
    split_cells(c, l, &cells);
    if (cells.n == 0) return 0;
    for (i = 0; i < cells.n; i++) {
        size_t a = cells.cs[i], b = cells.ce[i];
        int left = 0, right = 0;
        if (b <= a) return 0;
        if (c->t[a] == ':') { left = 1; a++; }
        if (b > a && c->t[b - 1] == ':') { right = 1; b--; }
        if (b <= a) return 0;
        for (k = a; k < b; k++) if (c->t[k] != '-') return 0;
        align[i] = left && right ? 2 : left ? 1 : right ? 3 : 0;
    }
    *ncols = cells.n;
    return 1;
}

static int row_has_pipe(const Ctx *c, VLine l) { size_t k; for (k = l.s; k < l.e; k++) { if (c->t[k] == '\\') { k++; continue; } if (c->t[k] == '|') return 1; } return 0; }

/* Does a table start at line i? */
static int table_at(Ctx *c, Src *s, size_t i, int *align, size_t *ncols) {
    Cells head;
    size_t n;
    size_t cols;
    if (!src_has(c, s, i) || !src_has(c, s, i + 1) || !row_has_pipe(c, s->v[i])) return 0;
    indent(c, s->v[i], &cols);
    if (cols >= 4) return 0;                                                  /* an indented line is code, never a table header */
    indent(c, s->v[i + 1], &cols);
    if (cols >= 4) return 0;
    if (is_blank(c, s->v[i + 1]) || !match_delim_row(c, s->v[i + 1], align, &n)) return 0;
    split_cells(c, s->v[i], &head);
    if (head.n != n) return 0;
    *ncols = n;
    return 1;
}

static void emit_row(Ctx *c, VLine l, int flags, const int *align, size_t ncols) {
    Cells cells;
    size_t i, p = indent(c, l, NULL), e = trim_end(c, p, l.e);
    split_cells(c, l, &cells);
    md_emit(c, EX_MD_TABLE_ROW, p, e, flags, 0);
    for (i = 0; i < cells.npipes; i++) EMIT(EX_MD_MARKER, cells.pipes[i], cells.pipes[i] + 1);
    for (i = 0; i < cells.n; i++) {
        VLine cell;
        md_emit(c, EX_MD_TABLE_CELL, cells.cs[i], cells.ce[i], (flags & 2) ? 0 : (i < ncols ? align[i] : 0), (int)i);      /* cells past the delimiter row's columns have no alignment */
        if (!(flags & 2) && cells.ce[i] > cells.cs[i]) { cell.s = cells.cs[i]; cell.e = cells.ce[i]; md_inline(c, &cell, 1); }
    }
}

/* ---- the block parser ---- */

static size_t parse_block(Ctx *c, Src *s, size_t i, int depth);

static void parse_all(Ctx *c, Src *s, int depth) {
    size_t i = 0;
    while (src_has(c, s, i)) {
        if (is_blank(c, s->v[i])) { i++; continue; }
        i = parse_block(c, s, i, depth);
    }
}

/* Parses lines (already stripped of their container's prefix) as blocks. */
static void parse_lines(Ctx *c, const VLine *lines, size_t n, int depth) {
    Src s;
    VLine *copy;
    if (n == 0) return;
    copy = malloc(n * sizeof *copy);
    if (!copy) { c->oom = 1; return; }
    memcpy(copy, lines, n * sizeof *copy);
    memset(&s, 0, sizeof s);
    s.v = copy; s.n = s.cap = n;
    parse_all(c, &s, depth);
    free(copy);
}

static int push_line(VLine **v, size_t *n, size_t *cap, VLine l) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        VLine *nv = realloc(*v, nc * sizeof **v);
        if (!nv) return -1;
        *v = nv;
        *cap = nc;
    }
    (*v)[(*n)++] = l;
    return 0;
}

/* Collects the lines of a list item or a footnote definition that follows the first line at index i:
 * indented lines (stripped of `col` columns), blank lines between them, and lazy continuation lines.
 * Returns the index after the item; the blank lines after the last content line are not part of it. */
static size_t collect_item(Ctx *c, Src *s, size_t i, size_t col, VLine first, VLine **out, size_t *nout, int depth) {
    VLine *v = NULL;
    size_t n = 0, cap = 0, j = i + 1, last_content = i;      /* last_content: index of the last non-blank line taken */
    int in_fence = 0, para = !is_blank(c, first);
    Fence fence = {0}, tmp;
    size_t cs, clen;
    if (push_line(&v, &n, &cap, first) != 0) { c->oom = 1; goto done; }
    if (match_fence(c, first, &fence)) in_fence = 1;
    while (src_has(c, s, j)) {
        VLine l = s->v[j];
        size_t cols;
        if (is_blank(c, l)) {
            size_t k = j;                                    /* blank lines count only if an indented line follows */
            while (src_has(c, s, k) && is_blank(c, s->v[k])) k++;
            if (!src_has(c, s, k)) break;
            indent(c, s->v[k], &cols);
            if (cols < col) break;
            while (j < k) { VLine b = s->v[j]; b.s = b.e; if (push_line(&v, &n, &cap, b) != 0) { c->oom = 1; goto done; } j++; }
            para = 0;
            continue;
        }
        indent(c, l, &cols);
        if (cols >= col) {
            VLine st = strip_cols(c, l, col);
            if (in_fence) { if (closes_fence(c, st, &fence, &cs, &clen)) in_fence = 0; }
            else if (match_fence(c, st, &tmp)) { in_fence = 1; fence = tmp; }
            para = !in_fence;
            if (push_line(&v, &n, &cap, st) != 0) { c->oom = 1; goto done; }
            last_content = j;
            j++;
            continue;
        }
        if (para && !in_fence && !starts_block(c, l, depth, 0)) {          /* a lazy continuation line */
            if (push_line(&v, &n, &cap, l) != 0) { c->oom = 1; goto done; }
            last_content = j;
            j++;
            continue;
        }
        break;
    }
    while (n > 1 && j > last_content + 1 && is_blank(c, v[n - 1]) ) { n--; j--; }     /* trailing blank lines are not part of the item */
    j = last_content + 1;
    if (n > last_content - i + 1) n = last_content - i + 1;
done:
    *out = v;
    *nout = n;
    return j;
}

static size_t parse_paragraph(Ctx *c, Src *s, size_t i, int depth) {
    VLine *lines = NULL;
    size_t n = 0, cap = 0, j = i;
    int level = 0;
    size_t us = 0, ue = 0;
    int align[MAX_CELLS];
    size_t ncols;
    while (src_has(c, s, j)) {
        VLine l = s->v[j];
        if (j > i) {
            if (is_blank(c, l)) break;
            if (match_setext(c, l, &level, &us, &ue)) break;
            if (starts_block(c, l, depth, 1) || (!starts_non_paragraph(c, l, depth) && table_at(c, s, j, align, &ncols))) break;
            level = 0;
        }
        {
            VLine t = l;
            t.s = indent(c, l, NULL);
            if (push_line(&lines, &n, &cap, t) != 0) { c->oom = 1; free(lines); return j + 1; }
        }
        j++;
    }
    if (level && src_has(c, s, j)) {                                            /* a setext heading: the underline is the last line */
        size_t hs = lines[0].s, he = trim_end(c, lines[n - 1].s, lines[n - 1].e);
        md_emit(c, EX_MD_HEADING, hs, ue, 0, level);
        EMIT(EX_MD_MARKER, us, ue);
        lines[n - 1].e = he;
        md_inline(c, lines, n);
        j++;
    } else {
        size_t pe = trim_end(c, lines[n - 1].s, lines[n - 1].e);
        md_emit(c, EX_MD_PARAGRAPH, lines[0].s, pe, 0, 0);
        lines[n - 1].e = pe;
        md_inline(c, lines, n);
    }
    free(lines);
    return j;
}

static size_t parse_fenced(Ctx *c, Src *s, size_t i) {
    Fence f;
    size_t j = i + 1, cs = 0, clen = 0, end;
    int closed = 0;
    match_fence(c, s->v[i], &f);
    while (src_has(c, s, j)) {
        if (closes_fence(c, s->v[j], &f, &cs, &clen)) { closed = 1; break; }
        j++;
    }
    end = closed ? cs + clen : (j > i + 1 ? s->v[j - 1].e : f.ie > f.fs + f.len ? f.ie : f.fs + f.len);
    if (!closed && j > i + 1) end = s->v[j - 1].e;
    md_emit(c, EX_MD_CODE_BLOCK, f.fs, end, 0, 0);
    EMIT(EX_MD_MARKER, f.fs, f.fs + f.len);
    if (f.ie > f.is) EMIT(EX_MD_CODE_INFO, f.is, f.ie);
    if (j > i + 1) EMIT(EX_MD_CODE_BODY, s->v[i + 1].s, s->v[j - 1].e);
    if (closed) EMIT(EX_MD_MARKER, cs, cs + clen);
    return closed ? j + 1 : j;
}

static size_t parse_indented_code(Ctx *c, Src *s, size_t i) {
    size_t j = i, last = i;
    size_t cols;
    while (src_has(c, s, j)) {
        if (is_blank(c, s->v[j])) { j++; continue; }
        indent(c, s->v[j], &cols);
        if (cols < 4) break;
        last = j;
        j++;
    }
    {
        VLine first = strip_cols(c, s->v[i], 4), lastl = s->v[last];
        md_emit(c, EX_MD_CODE_BLOCK, first.s, lastl.e, 1, 0);
        EMIT(EX_MD_CODE_BODY, first.s, lastl.e);
    }
    return last + 1;
}

static size_t parse_math(Ctx *c, Src *s, size_t i) {
    VLine l = s->v[i];
    size_t p = indent(c, l, NULL), k, j = i + 1;
    for (k = p + 2; k + 1 < l.e; k++) if (c->t[k] == '$' && c->t[k + 1] == '$') break;
    if (k + 1 < l.e) {                                                    /* one line: $$ x $$ */
        md_emit(c, EX_MD_MATH_BLOCK, p, trim_end(c, p, l.e), 0, 0);
        EMIT(EX_MD_MARKER, p, p + 2);
        EMIT(EX_MD_MARKER, k, k + 2);
        return i + 1;
    }
    while (src_has(c, s, j)) {
        VLine m = s->v[j];
        for (k = m.s; k + 1 < m.e; k++) if (c->t[k] == '$' && c->t[k + 1] == '$') break;
        if (k + 1 < m.e) {
            md_emit(c, EX_MD_MATH_BLOCK, p, trim_end(c, k + 2, m.e), 0, 0);
            EMIT(EX_MD_MARKER, p, p + 2);
            EMIT(EX_MD_MARKER, k, k + 2);
            return j + 1;
        }
        j++;
    }
    md_emit(c, EX_MD_MATH_BLOCK, p, s->v[j - 1].e, 0, 0);               /* never closed: to the end */
    EMIT(EX_MD_MARKER, p, p + 2);
    return j;
}

static size_t parse_comment(Ctx *c, Src *s, size_t i, size_t p) {
    size_t j = i + 1, k;
    while (src_has(c, s, j)) {
        VLine m = s->v[j];
        for (k = m.s; k + 1 < m.e; k++) if (c->t[k] == '%' && c->t[k + 1] == '%') break;
        if (k + 1 < m.e) {
            md_emit(c, EX_MD_COMMENT_BLOCK, p, trim_end(c, k + 2, m.e), 0, 0);
            EMIT(EX_MD_MARKER, p, p + 2);
            EMIT(EX_MD_MARKER, k, k + 2);
            return j + 1;
        }
        j++;
    }
    md_emit(c, EX_MD_COMMENT_BLOCK, p, s->v[j - 1].e, 0, 0);
    EMIT(EX_MD_MARKER, p, p + 2);
    return j;
}

static size_t parse_html(Ctx *c, Src *s, size_t i, int kind, const char *name) {
    size_t j = i, p = indent(c, s->v[i], NULL), nl = strlen(name);
    while (src_has(c, s, j)) {
        VLine l = s->v[j];
        size_t k;
        if (kind == 3 && j > i && is_blank(c, l)) break;
        if (kind == 2) {
            int closed = 0;
            for (k = l.s; k + 2 < l.e + 0 && !closed; k++) if (memcmp(c->t + k, "-->", 3) == 0) closed = 1;
            if (closed || (l.e >= l.s + 3 && memcmp(c->t + l.e - 3, "-->", 3) == 0)) { j++; break; }
        } else if (kind == 1) {
            int closed = 0;
            for (k = l.s; k + 2 + nl <= l.e; k++)
                if (c->t[k] == '<' && c->t[k + 1] == '/' && ieq(c->t + k + 2, nl, name)) { closed = 1; break; }
            if (closed) { j++; break; }
        }
        j++;
    }
    md_emit(c, EX_MD_HTML_BLOCK, p, s->v[j - 1].e, 0, 0);
    return j;
}

static size_t parse_table(Ctx *c, Src *s, size_t i, const int *align, size_t ncols) {
    size_t j = i + 2, start = indent(c, s->v[i], NULL), end;
    while (src_has(c, s, j)) {
        VLine l = s->v[j];
        Atx a;
        Fence f;
        size_t cols, p;
        if (is_blank(c, l) || match_atx(c, l, &a) || match_fence(c, l, &f)) break;
        p = indent(c, l, &cols);
        if (cols < 4 && p < l.e && c->t[p] == '>') break;
        j++;
    }
    end = trim_end(c, start, s->v[j - 1].e);
    md_emit(c, EX_MD_TABLE, start, end, 0, 0);
    emit_row(c, s->v[i], 1, align, ncols);
    emit_row(c, s->v[i + 1], 2, align, ncols);
    {
        size_t r;
        for (r = i + 2; r < j; r++) emit_row(c, s->v[r], 0, align, ncols);
    }
    return j;
}

static size_t parse_quote(Ctx *c, Src *s, size_t i, int depth) {
    VLine *inner = NULL;
    size_t n = 0, cap = 0, j = i, first = indent(c, s->v[i], NULL), end = 0, k;
    int in_fence = 0, para = 0;
    Fence fence = {0}, tmp;
    size_t cs, clen;
    size_t *gts = NULL, ngt = 0, capgt = 0;               /* the '>' of each line, for the markers */
    while (src_has(c, s, j)) {
        VLine l = s->v[j], st;
        size_t cols, p = indent(c, l, &cols);
        if (cols < 4 && p < l.e && c->t[p] == '>') {
            size_t q = p + 1;
            if (ngt == capgt) { size_t nc = capgt ? capgt * 2 : 16; size_t *ng = realloc(gts, nc * sizeof *ng); if (!ng) { c->oom = 1; break; } gts = ng; capgt = nc; }
            gts[ngt++] = p;
            if (q < l.e && (c->t[q] == ' ' || c->t[q] == '\t')) q++;
            st = l; st.s = q;
            if (in_fence) { if (closes_fence(c, st, &fence, &cs, &clen)) in_fence = 0; }
            else if (match_fence(c, st, &tmp)) { in_fence = 1; fence = tmp; }
            para = !is_blank(c, st) && !in_fence;
            if (push_line(&inner, &n, &cap, st) != 0) { c->oom = 1; break; }
            end = l.e;
            j++;
            continue;
        }
        if (para && !in_fence && !is_blank(c, l) && !starts_block(c, l, depth, 0)) {                 /* lazy continuation */
            VLine lazy = l;
            lazy.s = p;
            if (push_line(&inner, &n, &cap, lazy) != 0) { c->oom = 1; break; }
            end = l.e;
            j++;
            continue;
        }
        break;
    }
    if (n == 0) { free(inner); free(gts); return i + 1; }
    /* a callout: the first line is [!type] with an optional + or - and a title */
    {
        VLine f0 = inner[0];
        size_t p = f0.s, ts, te, fold_at = 0, title_s, title_e;
        int is_callout = 0, fold = 0;
        if (p + 3 < f0.e && c->t[p] == '[' && c->t[p + 1] == '!') {
            ts = p + 2;
            for (te = ts; te < f0.e && c->t[te] != ']' && !ws(c->t[te]); te++) {}
            if (te < f0.e && c->t[te] == ']' && te > ts) {
                k = te + 1;
                if (k < f0.e && (c->t[k] == '+' || c->t[k] == '-')) { fold = c->t[k] == '+' ? EX_CALLOUT_FOLD_OPEN : EX_CALLOUT_FOLD_CLOSED; fold_at = k; k++; }
                if (k >= f0.e || ws(c->t[k])) {
                    while (k < f0.e && ws(c->t[k])) k++;
                    title_s = k;
                    title_e = trim_end(c, k, f0.e);
                    is_callout = 1;
                    {
                        int known;
                        ExCalloutType ct = ex_callout_type(c->t + ts, te - ts, &known);
                        md_emit(c, EX_MD_CALLOUT, first, end, fold, (int)ct);
                        for (k = 0; k < ngt; k++) EMIT(EX_MD_MARKER, gts[k], gts[k] + 1);
                        EMIT(EX_MD_MARKER, p, ts);
                        EMIT(EX_MD_CALLOUT_TYPE, ts, te);
                        EMIT(EX_MD_MARKER, te, te + 1);
                        if (fold) EMIT(EX_MD_MARKER, fold_at, fold_at + 1);
                        if (title_e > title_s) {
                            VLine tl;
                            EMIT(EX_MD_CALLOUT_TITLE, title_s, title_e);
                            tl.s = title_s; tl.e = title_e;
                            md_inline(c, &tl, 1);
                        }
                    }
                }
            }
        }
        if (is_callout) parse_lines(c, inner + 1, n - 1, depth + 1);
        else {
            md_emit(c, EX_MD_BLOCKQUOTE, first, end, 0, 0);
            for (k = 0; k < ngt; k++) EMIT(EX_MD_MARKER, gts[k], gts[k] + 1);
            parse_lines(c, inner, n, depth + 1);
        }
    }
    free(inner);
    free(gts);
    return j;
}

static size_t parse_list(Ctx *c, Src *s, size_t i, int depth) {
    LM first, m;
    size_t j = i, list_end = 0, list_start = 0;
    size_t list_idx;
    int started = 0;
    if (!match_list(c, s->v[i], &first)) return i + 1;
    list_start = first.ms;
    list_idx = c->nsp;
    md_emit(c, EX_MD_LIST, list_start, list_start, 0, first.ordered ? 2 : 1);      /* the end is set when the last item is known */
    while (src_has(c, s, j)) {
        VLine l = s->v[j], *inner = NULL;
        size_t n = 0, next, item_end, k;
        int task = 0;
        size_t task_s = 0;
        if (!match_list(c, l, &m) || m.ordered != first.ordered || m.delim != first.delim) {
            if (started) break;
            break;
        }
        /* the content of the first line, after a task marker if there is one */
        {
            VLine f0 = l;
            f0.s = m.cs;
            if (!m.empty && m.cs + 2 < l.e && c->t[m.cs] == '[' && c->t[m.cs + 2] == ']' && (m.cs + 3 >= l.e || ws(c->t[m.cs + 3])) && c->t[m.cs + 1] != '[' ) {
                char st = c->t[m.cs + 1];
                task = st == ' ' ? 1 : (st == 'x' || st == 'X') ? 2 : 3;      /* 1 open, 2 done, 3 other */
                task_s = m.cs;
                f0.s = m.cs + 3;
                while (f0.s < l.e && ws(c->t[f0.s])) f0.s++;
            }
            next = collect_item(c, s, j, m.col, f0, &inner, &n, depth);
        }
        item_end = n ? (inner[n - 1].e > inner[n - 1].s ? inner[n - 1].e : (n > 1 ? inner[n - 2].e : l.e)) : l.e;
        for (k = n; k-- > 0;) { if (!is_blank(c, inner[k])) { item_end = trim_end(c, inner[k].s, inner[k].e); break; } }
        if (item_end < m.me) item_end = l.e;
        md_emit(c, EX_MD_LIST_ITEM, m.ms, item_end, 0, m.ordered ? 2 : 1);
        EMIT(EX_MD_MARKER, m.ms, m.me);
        if (task) md_emit(c, EX_MD_TASK, task_s, task_s + 3, task - 1, 0);
        if (task) EMIT(EX_MD_MARKER, task_s, task_s + 3);
        if (depth + 1 < MD_MAX_DEPTH) parse_lines(c, inner, n, depth + 1);
        else if (n) md_inline(c, inner, n);
        free(inner);
        list_end = item_end;
        started = 1;
        j = next;
        /* the list goes on if the next non-blank line is another item of the same kind */
        {
            size_t k2 = j;
            while (src_has(c, s, k2) && is_blank(c, s->v[k2])) k2++;
            if (!src_has(c, s, k2)) break;
            { LM nm; if (!(match_list(c, s->v[k2], &nm) && nm.ordered == first.ordered && nm.delim == first.delim && !match_hr(c, s->v[k2]))) break; }
            j = k2;
        }
    }
    c->sp[list_idx].end = list_end > list_start ? list_end : list_start;
    return j > i ? j : i + 1;
}

static size_t parse_footnote(Ctx *c, Src *s, size_t i, int depth) {
    Fn f;
    VLine first = s->v[i], *inner = NULL;
    size_t n = 0, next, end = 0, k;
    if (!match_footnote_def(c, first, &f)) return i + 1;
    first.s = f.cs;
    next = collect_item(c, s, i, 4, first, &inner, &n, depth);
    end = first.e;
    for (k = n; k-- > 0;) if (!is_blank(c, inner[k])) { end = trim_end(c, inner[k].s, inner[k].e); break; }
    if (end < f.cs) end = f.cs;
    md_emit(c, EX_MD_FOOTNOTE_DEF, f.fs, end, 0, 0);
    EMIT(EX_MD_MARKER, f.fs, f.fs + 2);
    EMIT(EX_MD_MARKER, f.id_e, f.id_e + 2);
    parse_lines(c, inner, n, depth + 1);
    free(inner);
    return next;
}

static size_t parse_block(Ctx *c, Src *s, size_t i, int depth) {
    VLine l = s->v[i];
    Atx a;
    char name[16];
    int kind, align[MAX_CELLS];
    size_t ncols, p, cols, next;

    if (s->doc_start && i == 0) {                                          /* front matter: --- ... --- on the first line */
        size_t q = indent(c, l, &cols);
        if (cols == 0 && q + 3 == trim_end(c, q, l.e) && memcmp(c->t + q, "---", 3) == 0) {
            size_t j = 1;
            while (src_has(c, s, j)) {
                VLine m = s->v[j];
                size_t e = trim_end(c, m.s, m.e);
                if ((e == m.s + 3 && (memcmp(c->t + m.s, "---", 3) == 0 || memcmp(c->t + m.s, "...", 3) == 0))) {
                    md_emit(c, EX_MD_FRONTMATTER, q, e, 0, 0);
                    EMIT(EX_MD_MARKER, q, q + 3);
                    EMIT(EX_MD_MARKER, m.s, m.s + 3);
                    return j + 1;
                }
                j++;
            }
        }
    }
    if (match_fence(c, l, &(Fence){0})) return parse_fenced(c, s, i);
    if (match_atx(c, l, &a)) {
        md_emit(c, EX_MD_HEADING, a.hs, a.cl_e ? a.cl_e : a.ce > a.cs ? a.ce : a.hs + (size_t)a.level, 0, a.level);
        EMIT(EX_MD_MARKER, a.hs, a.ce > a.cs ? a.cs : a.hs + (size_t)a.level);        /* the # characters and the space after them */
        if (a.cl_e) EMIT(EX_MD_MARKER, a.cl_s, a.cl_e);
        if (a.ce > a.cs) { VLine t; t.s = a.cs; t.e = a.ce; md_inline(c, &t, 1); }
        return i + 1;
    }
    if (match_hr(c, l)) {
        size_t q = indent(c, l, NULL), e = trim_end(c, q, l.e);
        md_emit(c, EX_MD_HR, q, e, 0, 0);
        EMIT(EX_MD_MARKER, q, e);
        return i + 1;
    }
    p = indent(c, l, &cols);
    if (cols < 4 && p < l.e && c->t[p] == '>' && depth < MD_MAX_DEPTH) return parse_quote(c, s, i, depth);
    if (depth < MD_MAX_DEPTH) { LM m; if (match_list(c, l, &m)) return parse_list(c, s, i, depth); }
    { Fn f; if (match_footnote_def(c, l, &f)) return parse_footnote(c, s, i, depth); }
    if (match_math_open(c, l)) return parse_math(c, s, i);
    { size_t cp; int closed; if (match_comment_open(c, l, &cp, &closed) && !closed) return parse_comment(c, s, i, cp); }
    kind = match_html_block(c, l, name);
    if (kind) return parse_html(c, s, i, kind, name);
    if (cols >= 4) return parse_indented_code(c, s, i);
    if (table_at(c, s, i, align, &ncols)) return parse_table(c, s, i, align, ncols);
    next = parse_paragraph(c, s, i, depth);
    return next > i ? next : i + 1;
}

/* ---- the document ---- */

typedef struct Span {
    uint8_t kind, flags;
    uint16_t level;
    uint32_t start, end;                 /* relative to the block */
    int32_t parent;                      /* index in the block's spans, or -1 */
} Span;

typedef struct TopBlock { size_t start, end; Span *spans; uint32_t n; } TopBlock;

struct ExMd {
    TopBlock *b;
    size_t n, cap;
    size_t nspans;
    size_t last_retok;
    Ctx ctx;
};

ExMd *ex_md_create(void) { return calloc(1, sizeof(ExMd)); }

static void free_block(TopBlock *b) { free(b->spans); b->spans = NULL; }

void ex_md_free(ExMd *m) {
    size_t i;
    if (!m) return;
    for (i = 0; i < m->n; i++) free_block(&m->b[i]);
    free(m->b);
    free(m->ctx.sp);
    free(m);
}

size_t ex_md_block_count(const ExMd *m) { return m->n; }
void ex_md_block_range(const ExMd *m, size_t i, size_t *start, size_t *end) { if (i < m->n) { if (start) *start = m->b[i].start; if (end) *end = m->b[i].end; } }
size_t ex_md_span_count(const ExMd *m) { return m->nspans; }
size_t ex_md_last_retokenized(const ExMd *m) { return m->last_retok; }

static int cmp_raw(const void *pa, const void *pb) {
    const RawSpan *a = pa, *b = pb;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    if (a->end != b->end) return a->end > b->end ? -1 : 1;
    return a->seq < b->seq ? -1 : a->seq > b->seq;
}

/* Turns the spans collected for one top-level block into its stored form. */
static int finish_block(Ctx *c, size_t start, size_t end, TopBlock *out) {
    size_t i;
    int32_t *stack = NULL;
    size_t sp = 0;
    Span *sp_out;
    qsort(c->sp, c->nsp, sizeof *c->sp, cmp_raw);
    sp_out = malloc((c->nsp ? c->nsp : 1) * sizeof *sp_out);
    stack = malloc((c->nsp ? c->nsp : 1) * sizeof *stack);
    if (!sp_out || !stack) { free(sp_out); free(stack); return -1; }
    for (i = 0; i < c->nsp; i++) {
        const RawSpan *r = &c->sp[i];
        while (sp > 0 && c->sp[stack[sp - 1]].end <= r->start) sp--;
        while (sp > 0 && r->end > c->sp[stack[sp - 1]].end) sp--;              /* never nest across a boundary */
        sp_out[i].kind = r->kind; sp_out[i].flags = r->flags; sp_out[i].level = r->level;
        sp_out[i].start = (uint32_t)(r->start - start); sp_out[i].end = (uint32_t)(r->end - start);
        sp_out[i].parent = sp ? stack[sp - 1] : -1;
        stack[sp++] = (int32_t)i;
    }
    free(stack);
    out->start = start; out->end = end; out->spans = sp_out; out->n = (uint32_t)c->nsp;
    c->nsp = 0;
    return 0;
}

/* Tokenizes top-level blocks from the line at `pos0`. With old != NULL (an update), stops when a new block
 * starts where an old block (at index >= from_old, shifted by delta) started: *sync_old is its index and
 * *sync_pos its start. Otherwise runs to the end of the text. */
typedef struct NewBlocks { TopBlock *v; size_t n, cap; } NewBlocks;

static int nb_push(NewBlocks *nb, const TopBlock *b) {
    if (nb->n == nb->cap) {
        size_t nc = nb->cap ? nb->cap * 2 : 64;
        TopBlock *nv = realloc(nb->v, nc * sizeof *nv);
        if (!nv) return -1;
        nb->v = nv;
        nb->cap = nc;
    }
    nb->v[nb->n++] = *b;
    return 0;
}

static void parse_top(ExMd *m, size_t pos0, const ExMd *old_view, size_t from_old, ptrdiff_t delta, NewBlocks *nb, size_t *sync_old, size_t *sync_pos) {
    Ctx *c = &m->ctx;
    Src s;
    size_t i = 0, oi = from_old;
    memset(&s, 0, sizeof s);
    s.lazy = 1;
    s.pos = pos0;
    s.doc_start = pos0 == 0;
    *sync_old = (size_t)-1;
    *sync_pos = c->len;
    for (;;) {
        size_t next, start, end;
        TopBlock tb;
        while (src_has(c, &s, i) && is_blank(c, s.v[i])) i++;
        if (!src_has(c, &s, i)) break;
        start = s.v[i].s;
        if (old_view && start > 0) {                  /* the first block of a note is special (front matter), so it is never reused */
            while (oi < old_view->n && (ptrdiff_t)old_view->b[oi].start + delta < (ptrdiff_t)start) oi++;
            if (oi < old_view->n && old_view->b[oi].start > 0 && (ptrdiff_t)old_view->b[oi].start + delta == (ptrdiff_t)start) { *sync_old = oi; *sync_pos = start; break; }
        }
        c->nsp = 0;
        next = parse_block(c, &s, i, 0);
        if (next <= i) next = i + 1;
        end = s.v[next - 1].e;
        if (end < start) end = start;
        if (c->oom || finish_block(c, start, end, &tb) != 0 || nb_push(nb, &tb) != 0) { c->oom = 1; break; }
        i = next;
    }
    free(s.v);
}

void ex_md_tokenize(ExMd *m, const char *text, size_t len) {
    NewBlocks nb = {0};
    size_t so, sp, i;
    for (i = 0; i < m->n; i++) free_block(&m->b[i]);
    free(m->b);
    m->b = NULL; m->n = m->cap = 0; m->nspans = 0;
    m->ctx.t = text; m->ctx.len = len; m->ctx.oom = 0;
    parse_top(m, 0, NULL, 0, 0, &nb, &so, &sp);
    m->b = nb.v; m->n = nb.n; m->cap = nb.cap;
    for (i = 0; i < m->n; i++) m->nspans += m->b[i].n;
    m->last_retok = m->n;
}

static size_t line_start_of(const char *t, size_t pos) { while (pos > 0 && t[pos - 1] != '\n') pos--; return pos; }

ExMdRange ex_md_update(ExMd *m, const char *text, size_t len, size_t offset, size_t removed, size_t inserted) {
    ExMdRange range;
    NewBlocks nb = {0};
    size_t r0, r, lo, hi, pos0, cand, sync_old, sync_pos, keep_from, i, old_n = m->n;
    ptrdiff_t delta = (ptrdiff_t)inserted - (ptrdiff_t)removed;
    ExMd view;
    TopBlock *merged;
    size_t nm;
    if (m->n == 0 || offset > len) { ex_md_tokenize(m, text, len); range.start = 0; range.end = len; return range; }
    /* Front matter depends on the whole note: a first line of --- is front matter only if a closing line follows
     * somewhere, so an edit anywhere can change the first block. When the note starts that way and the first
     * block is not front matter, tokenize everything (a rare case: an unclosed --- at the top). */
    if (len >= 3 && memcmp(text, "---", 3) == 0 && (len == 3 || text[3] == '\n' || text[3] == '\r' || text[3] == ' ' || text[3] == '\t')) {
        int fm = m->b[0].start == 0 && m->b[0].n > 0 && m->b[0].spans[0].kind == EX_MD_FRONTMATTER;
        if (!fm) { ex_md_tokenize(m, text, len); range.start = 0; range.end = len; return range; }
    }
    /* the first block that ends at or after the edit, and the one before it: its extent may depend on what follows */
    lo = 0; hi = m->n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (m->b[mid].end >= offset) hi = mid; else lo = mid + 1; }
    r0 = lo;
    r = r0 > 0 ? r0 - 1 : 0;
    cand = r < m->n ? m->b[r].start : offset;
    if (offset < cand) cand = offset;
    pos0 = line_start_of(text, cand);
    /* the first old block that starts after the edit: where a new block may line up with an old one */
    lo = r; hi = m->n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (m->b[mid].start >= offset + removed) hi = mid; else lo = mid + 1; }
    keep_from = lo;
    view = *m;
    m->ctx.t = text; m->ctx.len = len; m->ctx.oom = 0;
    parse_top(m, pos0, &view, keep_from, delta, &nb, &sync_old, &sync_pos);
    m->last_retok = nb.n;
    if (m->ctx.oom) { free(nb.v); ex_md_tokenize(m, text, len); range.start = 0; range.end = len; return range; }
    /* blocks [r, sync_old) are replaced by nb; the blocks from sync_old on only move */
    {
        size_t tail_from = sync_old == (size_t)-1 ? old_n : sync_old;
        nm = r + nb.n + (old_n - tail_from);
        merged = malloc((nm ? nm : 1) * sizeof *merged);
        if (!merged) { free(nb.v); ex_md_tokenize(m, text, len); range.start = 0; range.end = len; return range; }
        memcpy(merged, m->b, r * sizeof *merged);
        memcpy(merged + r, nb.v, nb.n * sizeof *merged);
        for (i = r; i < tail_from; i++) { m->nspans -= m->b[i].n; free_block(&m->b[i]); }
        for (i = 0; i < nb.n; i++) m->nspans += nb.v[i].n;
        for (i = tail_from; i < old_n; i++) {
            TopBlock t = m->b[i];
            t.start = (size_t)((ptrdiff_t)t.start + delta);
            t.end = (size_t)((ptrdiff_t)t.end + delta);
            merged[r + nb.n + (i - tail_from)] = t;
        }
        free(m->b);
        free(nb.v);
        m->b = merged; m->n = nm; m->cap = nm;
    }
    range.start = pos0;
    range.end = sync_pos;
    return range;
}

void ex_md_visit(const ExMd *m, size_t from, size_t to, ExMdVisit fn, void *user) {
    size_t lo = 0, hi = m->n, bi, to_eff = to > from ? to : from + 1;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (m->b[mid].end >= from) hi = mid; else lo = mid + 1; }
    for (bi = lo; bi < m->n && m->b[bi].start < to_eff; bi++) {
        const TopBlock *b = &m->b[bi];
        uint32_t k;
        for (k = 0; k < b->n; k++) {
            const Span *sp = &b->spans[k];
            ExMdSpan out;
            size_t s0 = b->start + sp->start, e0 = b->start + sp->end;
            if (s0 >= to_eff) break;                                            /* sorted by start: the rest begin later */
            if (e0 <= from && !(e0 == s0 && s0 >= from)) continue;
            out.kind = (ExMdKind)sp->kind; out.flags = sp->flags; out.level = sp->level; out.start = s0; out.end = e0;
            if (sp->parent >= 0) {
                const Span *pr = &b->spans[sp->parent];
                out.parent_kind = (ExMdKind)pr->kind; out.parent_start = b->start + pr->start; out.parent_end = b->start + pr->end;
            } else { out.parent_kind = (ExMdKind)0; out.parent_start = out.parent_end = 0; }
            if (fn(&out, user)) return;
        }
    }
}
