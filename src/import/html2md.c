/* HTML to Markdown for the HTML importer (sub-slice 6e-v). A forgiving single pass over the tags with an explicit stack (no recursion, so deeply nested
 * input cannot overflow the call stack); see include/exhaust_import.h. */
#include "exhaust_html.h"
#include "exhaust_import.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 200
#define MAX_OUT (64u * 1024u * 1024u)
#define MAX_TABLE_ROWS 2000
#define MAX_TABLE_COLS 64

typedef struct Buf { char *p; size_t n, cap; int oom; } Buf;

static void bput(Buf *b, const char *s, size_t n) {
    if (b->oom || n == 0) return;
    if (b->n + n + 1 > MAX_OUT) { b->oom = 1; return; }
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        char *np;
        while (nc < b->n + n + 1) nc *= 2;
        np = realloc(b->p, nc);
        if (!np) { b->oom = 1; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void bc(Buf *b, char c) { bput(b, &c, 1); }
static void bs(Buf *b, const char *s) { bput(b, s, strlen(s)); }

enum { T_OTHER, T_P, T_H, T_UL, T_OL, T_LI, T_BQ, T_PRE, T_CODE, T_A, T_STRONG, T_EM, T_DEL, T_MARK, T_TABLE, T_TR, T_TD, T_SUPSUB, T_BLOCK };

typedef struct Frame {
    int kind;
    int count;           /* list item counter (ol) */
    int ordered;         /* li: the list is ordered */
    int number;          /* li: its number */
    int marker_pending;  /* li: the marker is not yet written */
    char *href;          /* a: malloc'ed */
    size_t href_text_at; /* a: where its text starts in the output */
    Buf *href_buf;       /* a: the buffer that holds it (the page, or a table cell: the link may close in another one) */
    int level;           /* heading */
    int header_cell;
} Frame;

typedef struct Cell { char *text; } Cell;

typedef struct St {
    Buf out;
    Buf title, h1;
    Buf cell;
    int in_cell;
    Frame st[MAX_DEPTH];
    int depth;
    int last_space;      /* the last character written is a space or the start of a line */
    int at_line_start;
    int pre;             /* inside <pre> */
    int got_title, in_h1, h1_done;
    char *lang;
    Cell *rows[MAX_TABLE_ROWS]; int rowcols[MAX_TABLE_ROWS]; int nrows; int header_first;
    int row_has_th[MAX_TABLE_ROWS];
    int cur_cols;
    size_t last_break_at; int broke;
} St;

static Buf *dst(St *s) { return s->in_cell ? &s->cell : &s->out; }

/* the indentation and quote marks in front of a new line of the current container */
static void line_prefix(St *s, Buf *b) {
    int i, last_li = -1;
    for (i = 0; i < s->depth; i++) if (s->st[i].kind == T_LI) last_li = i;
    for (i = 0; i < s->depth; i++) {
        Frame *f = &s->st[i];
        if (f->kind == T_BQ) bs(b, "> ");
        else if (f->kind == T_LI) {
            char m[24];
            if (f->marker_pending && i == last_li) {
                if (f->ordered) snprintf(m, sizeof m, "%d. ", f->number); else snprintf(m, sizeof m, "- ");
                bs(b, m);
                f->marker_pending = 0;
            } else {
                size_t w = f->ordered ? (size_t)snprintf(m, sizeof m, "%d. ", f->number) : 2;
                while (w--) bc(b, ' ');
            }
        }
    }
}

static void newline(St *s) {
    Buf *b = dst(s);
    if (s->in_cell) { if (b->n && b->p[b->n - 1] != ' ') bc(b, ' '); s->last_space = 1; return; }
    while (b->n && (b->p[b->n - 1] == ' ' && b->n > 1 && b->p[b->n - 2] != ' ') && 0) b->n--;
    if (b->n && b->p[b->n - 1] != '\n') bc(b, '\n');
    s->at_line_start = 1; s->last_space = 1;
}

/* a blank line between blocks (not inside a table cell); inside a quote the blank line carries the quote marks */
static void block_break(St *s) {
    Buf *b = dst(s);
    if (s->in_cell) return;
    if (b->n == 0) { s->at_line_start = 1; return; }
    newline(s);
    if (!(s->broke && s->last_break_at == b->n)) {
        int i, any = 0;
        for (i = 0; i < s->depth; i++) if (s->st[i].kind == T_BQ) { if (any) bc(b, ' '); bc(b, '>'); any = 1; }
        bc(b, '\n');
        s->broke = 1; s->last_break_at = b->n;
    }
    s->at_line_start = 1; s->last_space = 1;
}

static void begin_line(St *s) {
    Buf *b = dst(s);
    if (!s->at_line_start || s->in_cell) return;
    line_prefix(s, b);
    s->at_line_start = 0;
}

static void put_text(St *s, const char *t, size_t n) {
    Buf *b = dst(s);
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (s->in_h1 && !s->h1_done) {
            if (c == '\n' || c == '\r' || c == '\t' || c == ' ') { if (s->h1.n && s->h1.p[s->h1.n - 1] != ' ') bc(&s->h1, ' '); }
            else bc(&s->h1, (char)c);
        }
        if (s->pre) {
            if (c == '\r') continue;
            if (s->at_line_start && c != '\n') begin_line(s);
            bc(b, (char)c);
            s->at_line_start = c == '\n';
            if (c == '\n' && s->depth) { /* the next line of a code block in a quote or list */ }
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xC2) {
            if (c == 0xC2 && !(i + 1 < n && (unsigned char)t[i + 1] == 0xA0)) goto plain;
            if (c == 0xC2) i++;                                         /* a no-break space is a space */
            if (!s->last_space) { begin_line(s); bc(b, ' '); s->last_space = 1; }
            continue;
        }
    plain:
        {
            int line_start = s->at_line_start && !s->in_cell;
            begin_line(s);
            if (c == '\\' || c == '*' || c == '_' || c == '`' || c == '[' || c == ']') bc(b, '\\');
            else if (c == '|' && s->in_cell) bc(b, '\\');
            else if (c == '<' && i + 1 < n && (isalpha((unsigned char)t[i + 1]) || t[i + 1] == '/')) bc(b, '\\');
            else if (line_start) {              /* the first character of a line must not start a heading, a quote, a list or a rule */
                if (c == '#' || c == '>' || c == '+' || ((c == '-' || c == '=') && i + 1 < n && (t[i + 1] == ' ' || t[i + 1] == c))) bc(b, '\\');
                else if (c >= '0' && c <= '9') {
                    size_t k = i;
                    while (k < n && t[k] >= '0' && t[k] <= '9') k++;
                    if (k < n && (t[k] == '.' || t[k] == ')')) { bput(b, t + i, k - i); bc(b, '\\'); i = k; c = (unsigned char)t[k]; }
                }
            }
        }
        bc(b, (char)c);
        s->last_space = 0;
    }
}

static const struct { const char *n; const char *u; } NAMED[] = {
    {"amp","&"},{"lt","<"},{"gt",">"},{"quot","\""},{"apos","'"},{"nbsp"," "},{"copy","\xC2\xA9"},{"reg","\xC2\xAE"},{"trade","\xE2\x84\xA2"},{"hellip","\xE2\x80\xA6"},
    {"mdash","\xE2\x80\x94"},{"ndash","\xE2\x80\x93"},{"lsquo","\xE2\x80\x98"},{"rsquo","\xE2\x80\x99"},{"ldquo","\xE2\x80\x9C"},{"rdquo","\xE2\x80\x9D"},
    {"euro","\xE2\x82\xAC"},{"pound","\xC2\xA3"},{"yen","\xC2\xA5"},{"cent","\xC2\xA2"},{"deg","\xC2\xB0"},{"plusmn","\xC2\xB1"},{"times","\xC3\x97"},
    {"divide","\xC3\xB7"},{"laquo","\xC2\xAB"},{"raquo","\xC2\xBB"},{"bull","\xE2\x80\xA2"},{"middot","\xC2\xB7"},{"larr","\xE2\x86\x90"},{"rarr","\xE2\x86\x92"},
};

/* decodes the entity at t[i] ('&'); writes UTF-8 to out (at most 8 bytes); returns the bytes consumed, or 0 */
static size_t entity(const char *t, size_t n, size_t i, char *out, size_t *olen) {
    size_t j = i + 1, k;
    if (j >= n) return 0;
    if (t[j] == '#') {
        unsigned long v = 0;
        int hex = 0, digits = 0;
        j++;
        if (j < n && (t[j] == 'x' || t[j] == 'X')) { hex = 1; j++; }
        while (j < n && digits < 8 && (hex ? isxdigit((unsigned char)t[j]) : isdigit((unsigned char)t[j]))) { v = v * (hex ? 16 : 10) + (unsigned long)(isdigit((unsigned char)t[j]) ? t[j] - '0' : (tolower((unsigned char)t[j]) - 'a' + 10)); j++; digits++; }
        if (!digits || j >= n || t[j] != ';') return 0;
        if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
        if (v < 0x80) { out[0] = (char)v; *olen = 1; }
        else if (v < 0x800) { out[0] = (char)(0xC0 | (v >> 6)); out[1] = (char)(0x80 | (v & 0x3f)); *olen = 2; }
        else if (v < 0x10000) { out[0] = (char)(0xE0 | (v >> 12)); out[1] = (char)(0x80 | ((v >> 6) & 0x3f)); out[2] = (char)(0x80 | (v & 0x3f)); *olen = 3; }
        else { out[0] = (char)(0xF0 | (v >> 18)); out[1] = (char)(0x80 | ((v >> 12) & 0x3f)); out[2] = (char)(0x80 | ((v >> 6) & 0x3f)); out[3] = (char)(0x80 | (v & 0x3f)); *olen = 4; }
        return j + 1 - i;
    }
    for (k = 0; k < sizeof NAMED / sizeof *NAMED; k++) {
        size_t nl = strlen(NAMED[k].n);
        if (j + nl < n && !strncmp(t + j, NAMED[k].n, nl) && t[j + nl] == ';') { *olen = strlen(NAMED[k].u); memcpy(out, NAMED[k].u, *olen); return nl + 2; }
    }
    return 0;
}

static void text_run(St *s, const char *t, size_t n) {
    size_t i = 0, start = 0;
    while (i < n) {
        if (t[i] == '&') {
            char o[8]; size_t ol = 0, used = entity(t, n, i, o, &ol);
            if (used) { put_text(s, t + start, i - start); put_text(s, o, ol); i += used; start = i; continue; }
        }
        i++;
    }
    put_text(s, t + start, n - start);
}

typedef struct Attr { char name[24]; char *value; } Attr;

static void free_attrs(Attr *a, int n) { int i; for (i = 0; i < n; i++) free(a[i].value); }

static char *attr_of(Attr *a, int n, const char *name) { int i; for (i = 0; i < n; i++) if (!strcmp(a[i].name, name)) return a[i].value; return NULL; }

/* the text with entities decoded (malloc'ed) */
static char *decode(const char *t, size_t n) {
    Buf b = {0};
    size_t i = 0;
    while (i < n) {
        if (t[i] == '&') { char o[8]; size_t ol = 0, used = entity(t, n, i, o, &ol); if (used) { bput(&b, o, ol); i += used; continue; } }
        bc(&b, t[i]); i++;
    }
    if (!b.p) { b.p = malloc(1); if (b.p) b.p[0] = 0; }
    return b.p;
}

static int tag_kind(const char *n) {
    static const struct { const char *n; int k; } T[] = {
        {"p", T_P}, {"h1", T_H}, {"h2", T_H}, {"h3", T_H}, {"h4", T_H}, {"h5", T_H}, {"h6", T_H}, {"ul", T_UL}, {"ol", T_OL}, {"li", T_LI}, {"blockquote", T_BQ},
        {"pre", T_PRE}, {"code", T_CODE}, {"kbd", T_CODE}, {"samp", T_CODE}, {"tt", T_CODE}, {"a", T_A}, {"strong", T_STRONG}, {"b", T_STRONG}, {"em", T_EM}, {"i", T_EM}, {"cite", T_EM},
        {"del", T_DEL}, {"s", T_DEL}, {"strike", T_DEL}, {"mark", T_MARK}, {"table", T_TABLE}, {"tr", T_TR}, {"td", T_TD}, {"th", T_TD}, {"sup", T_SUPSUB}, {"sub", T_SUPSUB},
        {"div", T_BLOCK}, {"section", T_BLOCK}, {"article", T_BLOCK}, {"header", T_BLOCK}, {"footer", T_BLOCK}, {"main", T_BLOCK}, {"nav", T_BLOCK}, {"aside", T_BLOCK},
        {"figure", T_BLOCK}, {"figcaption", T_BLOCK}, {"details", T_BLOCK}, {"summary", T_BLOCK}, {"address", T_BLOCK}, {"dl", T_BLOCK}, {"dt", T_BLOCK}, {"dd", T_BLOCK},
        {"form", T_BLOCK}, {"fieldset", T_BLOCK}, {"center", T_BLOCK}, {"body", T_OTHER},
    };
    size_t i;
    for (i = 0; i < sizeof T / sizeof *T; i++) if (!strcmp(T[i].n, n)) return T[i].k;
    return T_OTHER;
}

static void marker(St *s, const char *m) { begin_line(s); bs(dst(s), m); s->last_space = 1; }   /* an opener: a space right after it is dropped */

static void trim_trailing_space_then(St *s, const char *m) {
    Buf *b = dst(s);
    size_t sp = 0;
    while (b->n && b->p[b->n - 1] == ' ') { b->n--; sp++; }
    if (b->p) b->p[b->n] = 0;
    bs(b, m);
    while (sp--) bc(b, ' ');
    s->last_space = 0;
}

static void flush_table(St *s) {
    int r, c, cols = 0;
    Buf *b = &s->out;
    if (s->nrows == 0) { if (s->rows[0]) { for (c = 0; c < s->rowcols[0]; c++) free(s->rows[0][c].text); free(s->rows[0]); s->rows[0] = NULL; s->rowcols[0] = 0; } return; }
    for (r = 0; r < s->nrows; r++) if (s->rowcols[r] > cols) cols = s->rowcols[r];
    block_break(s);
    for (r = 0; r < s->nrows; r++) {
        begin_line(s);
        bc(b, '|');
        for (c = 0; c < cols; c++) { bc(b, ' '); if (c < s->rowcols[r] && s->rows[r][c].text) bs(b, s->rows[r][c].text); bs(b, " |"); }
        bc(b, '\n'); s->at_line_start = 1;
        if (r == 0) { begin_line(s); bc(b, '|'); for (c = 0; c < cols; c++) bs(b, " --- |"); bc(b, '\n'); s->at_line_start = 1; }
    }
    for (r = 0; r < s->nrows; r++) { for (c = 0; c < s->rowcols[r]; c++) free(s->rows[r][c].text); free(s->rows[r]); s->rows[r] = NULL; }
    if (s->nrows < MAX_TABLE_ROWS && s->rows[s->nrows]) { for (c = 0; c < s->rowcols[s->nrows]; c++) free(s->rows[s->nrows][c].text); free(s->rows[s->nrows]); s->rows[s->nrows] = NULL; s->rowcols[s->nrows] = 0; }
    s->nrows = 0;
    s->last_space = 1;
}

static void open_tag(St *s, const char *name, Attr *a, int na, int self_close) {
    int k = tag_kind(name), lvl = 0;
    Frame *f;
    if (!strcmp(name, "br")) { Buf *b = dst(s); if (s->in_cell) bs(b, " "); else { bs(b, "  "); bc(b, '\n'); s->at_line_start = 1; } s->last_space = 1; return; }
    if (!strcmp(name, "hr")) { block_break(s); begin_line(s); bs(dst(s), "---"); newline(s); block_break(s); return; }
    if (!strcmp(name, "img")) {
        char *src = attr_of(a, na, "src"), *alt = attr_of(a, na, "alt"), *altd = decode(alt ? alt : "", alt ? strlen(alt) : 0), *srcd = decode(src ? src : "", src ? strlen(src) : 0);
        if (srcd && *srcd && altd && ex_html_url_allowed(srcd, strlen(srcd))) {      /* only http, https, mailto, relative and # addresses: a page's javascript: or data: address does not come into a note */
            size_t i;
            begin_line(s);
            bs(dst(s), "![");
            for (i = 0; altd[i]; i++) { if (altd[i] == ']' || altd[i] == '[' || altd[i] == '\\') bc(dst(s), '\\'); bc(dst(s), altd[i] == '\n' ? ' ' : altd[i]); }
            bs(dst(s), "](");
            for (i = 0; srcd[i]; i++) { if (srcd[i] == ' ') bs(dst(s), "%20"); else if (srcd[i] == ')') bs(dst(s), "%29"); else if (srcd[i] == '(') bs(dst(s), "%28"); else bc(dst(s), srcd[i]); }
            bc(dst(s), ')');
            s->last_space = 0;
        }
        free(altd); free(srcd);
        return;
    }
    if (!strcmp(name, "script") || !strcmp(name, "style")) return;
    if (k == T_OTHER || self_close || s->depth >= MAX_DEPTH) return;
    if (k == T_H) lvl = name[1] - '0';
    if (k == T_P || k == T_H || k == T_BQ || k == T_UL || k == T_OL || k == T_PRE || k == T_BLOCK || k == T_TABLE) {
        if (k != T_UL && k != T_OL) block_break(s);
        else if (s->depth == 0 || s->st[s->depth - 1].kind != T_LI) block_break(s);
        else newline(s);
    }
    f = &s->st[s->depth];
    memset(f, 0, sizeof *f);
    f->kind = k; f->level = lvl;
    if (k == T_OL) f->count = 0;
    if (k == T_LI) {
        const Frame *parent = s->depth ? &s->st[s->depth - 1] : NULL;
        Frame *pp = s->depth ? &s->st[s->depth - 1] : NULL;
        f->ordered = parent && parent->kind == T_OL;
        if (pp && (pp->kind == T_OL || pp->kind == T_UL)) pp->count++;
        f->number = pp ? pp->count : 1;
        f->marker_pending = 1;
        newline(s);
    }
    if (k == T_H) { begin_line(s); { char h[8]; int i; for (i = 0; i < lvl; i++) h[i] = '#'; h[lvl] = ' '; h[lvl + 1] = 0; bs(dst(s), h); } s->last_space = 1; if (lvl == 1 && !s->h1_done && !s->in_h1) { s->in_h1 = 1; s->h1.n = 0; } }
    if (k == T_PRE) {
        char *lang = NULL;
        begin_line(s); bs(dst(s), "```"); newline(s);
        s->pre = 1; s->last_space = 1;
        (void)lang;
    }
    if (k == T_CODE && !s->pre) { marker(s, "`"); }
    if (k == T_CODE && s->pre) { char *c = attr_of(a, na, "class"); if (c) { char *p = strstr(c, "language-"); if (p) { size_t i = 0; char lang[40]; p += 9; while (p[i] && p[i] != ' ' && i < sizeof lang - 1) { lang[i] = p[i]; i++; } lang[i] = 0; if (i) { Buf *b = &s->out; size_t at = b->n; if (at >= 4 && !memcmp(b->p + at - 4, "```\n", 4)) { b->n -= 1; bs(b, lang); bc(b, '\n'); } } } } }
    if (k == T_STRONG) marker(s, "**");
    if (k == T_EM) marker(s, "*");
    if (k == T_DEL) marker(s, "~~");
    if (k == T_MARK) marker(s, "==");
    if (k == T_SUPSUB) { begin_line(s); bs(dst(s), name[1] == 'u' && name[2] == 'p' ? "<sup>" : "<sub>"); s->last_space = 1; }
    if (k == T_A) {
        char *h = attr_of(a, na, "href");
        f->href = NULL;
        if (h && *h) f->href = decode(h, strlen(h));
        if (f->href && !ex_html_url_allowed(f->href, strlen(f->href))) { free(f->href); f->href = NULL; }          /* the text stays, without the link */
        if (f->href) {
            begin_line(s); bc(dst(s), '[');
            f->href_buf = dst(s);
            f->href_text_at = dst(s)->n;
            s->last_space = 1;
        }
    }
    if (k == T_BQ) { s->at_line_start = 1; }
    if (k == T_TR) { s->cur_cols = 0; if (s->nrows < MAX_TABLE_ROWS && s->rows[s->nrows]) { int cc; for (cc = 0; cc < s->rowcols[s->nrows]; cc++) free(s->rows[s->nrows][cc].text); free(s->rows[s->nrows]); s->rows[s->nrows] = NULL; }
        if (s->nrows < MAX_TABLE_ROWS) { s->rows[s->nrows] = calloc(MAX_TABLE_COLS, sizeof(Cell)); s->rowcols[s->nrows] = 0; s->row_has_th[s->nrows] = 0; } }
    if (k == T_TD) {
        s->in_cell = 1; s->cell.n = 0; if (s->cell.p) s->cell.p[0] = 0; s->last_space = 1; s->at_line_start = 0;
        f->header_cell = name[1] == 'h';
    }
    s->depth++;
}

static void close_frame(St *s, int i, const char *name) {
    Frame *f = &s->st[i];
    (void)name;
    switch (f->kind) {
    case T_P: case T_BLOCK: case T_BQ: block_break(s); break;
    case T_H:
        if (s->in_h1) { s->in_h1 = 0; s->h1_done = 1; }
        newline(s); block_break(s);
        break;
    case T_UL: case T_OL: newline(s); if (!(i > 0 && s->st[i - 1].kind == T_LI)) { /* a blank line after the outermost list */ } break;
    case T_LI: newline(s); break;
    case T_PRE: {
        Buf *b = &s->out;
        s->pre = 0;
        if (b->n && b->p[b->n - 1] != '\n') bc(b, '\n');
        s->at_line_start = 1;
        line_prefix(s, b);
        bs(b, "```\n");
        s->at_line_start = 1; s->last_space = 1;
        block_break(s);
        break;
    }
    case T_CODE: if (!s->pre) trim_trailing_space_then(s, "`"); break;
    case T_STRONG: trim_trailing_space_then(s, "**"); break;
    case T_EM: trim_trailing_space_then(s, "*"); break;
    case T_DEL: trim_trailing_space_then(s, "~~"); break;
    case T_MARK: trim_trailing_space_then(s, "=="); break;
    case T_SUPSUB: bs(dst(s), name[1] == 'u' && name[2] == 'p' ? "</sup>" : "</sub>"); break;
    case T_A:
        if (f->href && f->href_buf == dst(s) && f->href_buf->n >= f->href_text_at && f->href_text_at > 0) {     /* the same buffer it was opened in, and not emptied since */
            Buf *b = dst(s);
            size_t k;
            if (b->n > f->href_text_at) {
                while (b->n && b->p[b->n - 1] == ' ' && b->n > f->href_text_at) b->n--;
                bs(b, "](");
                for (k = 0; f->href[k]; k++) { if (f->href[k] == ' ') bs(b, "%20"); else if (f->href[k] == ')') bs(b, "%29"); else if (f->href[k] == '(') bs(b, "%28"); else bc(b, f->href[k]); }
                bc(b, ')');
            } else { b->n--; b->p[b->n] = 0; }                                                    /* a link with no text: the bracket goes */
            s->last_space = 0;
        }
        break;
    case T_TD: {
        Buf *b = &s->cell;
        while (b->n && b->p[b->n - 1] == ' ') b->n--;
        if (s->nrows < MAX_TABLE_ROWS && s->cur_cols < MAX_TABLE_COLS && s->rows[s->nrows]) {
            char *t = malloc(b->n + 1);
            if (t) { memcpy(t, b->p ? b->p : "", b->n); t[b->n] = 0; }
            s->rows[s->nrows][s->cur_cols].text = t;
            s->cur_cols++; s->rowcols[s->nrows] = s->cur_cols;
            if (f->header_cell) s->row_has_th[s->nrows] = 1;
        }
        s->in_cell = 0; s->last_space = 1;
        break;
    }
    case T_TR: if (s->nrows < MAX_TABLE_ROWS && s->rows[s->nrows]) { if (s->rowcols[s->nrows] > 0) s->nrows++; else { free(s->rows[s->nrows]); s->rows[s->nrows] = NULL; } } break;
    case T_TABLE: flush_table(s); block_break(s); break;
    }
    free(f->href); f->href = NULL;
}

static void end_tag(St *s, const char *name) {
    int k = tag_kind(name), i;
    if (k == T_OTHER) return;
    for (i = s->depth - 1; i >= 0; i--) if (s->st[i].kind == k && (k != T_H || 1)) break;
    if (i < 0) return;
    while (s->depth > i) { s->depth--; close_frame(s, s->depth, name); }
}

char *ex_html_to_markdown(const char *html, size_t len, char **title, size_t *out_len) {
    St *s = calloc(1, sizeof *s);
    size_t i = 0;
    char *res;
    if (title) *title = NULL;
    if (out_len) *out_len = 0;
    if (!s) return NULL;
    s->at_line_start = 1; s->last_space = 1;
    while (i < len && !s->out.oom) {
        if (html[i] == '<' && i + 1 < len) {
            char c = html[i + 1];
            if (c == '!' && i + 3 < len && !memcmp(html + i, "<!--", 4)) {
                const char *e = NULL;
                size_t k;
                for (k = i + 4; k + 2 < len; k++) if (html[k] == '-' && html[k + 1] == '-' && html[k + 2] == '>') { e = html + k + 3; break; }
                i = e ? (size_t)(e - html) : len;
                continue;
            }
            if (c == '!' || c == '?') { while (i < len && html[i] != '>') i++; i++; continue; }
            if (isalpha((unsigned char)c) || c == '/') {
                size_t k = i + 1;
                int closing = 0, self_close = 0, na = 0;
                char name[24];
                size_t nl = 0;
                Attr attrs[12];
                if (html[k] == '/') { closing = 1; k++; }
                while (k < len && (isalnum((unsigned char)html[k]) || html[k] == '-' || html[k] == ':') && nl < sizeof name - 1) name[nl++] = (char)tolower((unsigned char)html[k++]);
                name[nl] = 0;
                if (nl == 0) { text_run(s, html + i, 1); i++; continue; }
                while (k < len && html[k] != '>') {            /* attributes */
                    while (k < len && (isspace((unsigned char)html[k]) || html[k] == '/')) { if (html[k] == '/') self_close = 1; k++; }
                    if (k >= len || html[k] == '>') break;
                    self_close = 0;
                    {
                        char an[24];
                        size_t al = 0;
                        char *val = NULL;
                        while (k < len && !isspace((unsigned char)html[k]) && html[k] != '=' && html[k] != '>' && html[k] != '/') { if (al < sizeof an - 1) an[al++] = (char)tolower((unsigned char)html[k]); k++; }
                        an[al] = 0;
                        while (k < len && isspace((unsigned char)html[k])) k++;
                        if (k < len && html[k] == '=') {
                            size_t vs, ve;
                            k++;
                            while (k < len && isspace((unsigned char)html[k])) k++;
                            if (k < len && (html[k] == '"' || html[k] == '\'')) { char q = html[k++]; vs = k; while (k < len && html[k] != q) k++; ve = k; if (k < len) k++; }
                            else { vs = k; while (k < len && !isspace((unsigned char)html[k]) && html[k] != '>') k++; ve = k; }
                            val = malloc(ve - vs + 1);
                            if (val) { memcpy(val, html + vs, ve - vs); val[ve - vs] = 0; }
                        }
                        if (na < 12 && al) { memcpy(attrs[na].name, an, al + 1); attrs[na].value = val; na++; } else free(val);
                    }
                }
                if (k < len) k++;                                  /* the '>' */
                if (closing) end_tag(s, name);
                else {
                    if (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "title") || !strcmp(name, "textarea")) {
                        size_t e = k, cl = strlen(name) + 2;
                        while (e + cl <= len) {
                            if (html[e] == '<' && html[e + 1] == '/') { size_t q; int same = 1; for (q = 0; q < cl - 2; q++) if (tolower((unsigned char)html[e + 2 + q]) != name[q]) { same = 0; break; } if (same) break; }
                            e++;
                        }
                        if (e + cl > len) e = len;
                        if (!strcmp(name, "title") && !s->got_title) { char *t = decode(html + k, e - k); if (t) { bs(&s->title, t); free(t); s->got_title = 1; } }
                        k = e;
                        while (k < len && html[k] != '>') k++;
                        if (k < len) k++;
                    } else open_tag(s, name, attrs, na, self_close);
                }
                free_attrs(attrs, na);
                i = k;
                continue;
            }
        }
        {   /* text up to the next tag */
            size_t e = i + 1;
            while (e < len && html[e] != '<') e++;
            text_run(s, html + i, e - i);
            i = e;
        }
    }
    while (s->depth > 0) { s->depth--; close_frame(s, s->depth, ""); }
    flush_table(s);
    {   /* a few clean-ups: no more than one blank line, no space at the end of a line except a hard break, trimmed ends */
        Buf *o = &s->out;
        size_t r, w = 0;
        int blanks = 0;
        for (r = 0; r < o->n; r++) {
            if (o->p[r] == '\n') {
                size_t lw = w;
                while (lw > 0 && o->p[lw - 1] == ' ') lw--;
                if (w - lw >= 2 && lw > 0 && o->p[lw - 1] != '\n') { o->p[lw] = ' '; o->p[lw + 1] = ' '; w = lw + 2; } else w = lw;
                if (w > 0 && o->p[w - 1] == '\n') { blanks++; if (blanks >= 2) continue; } else blanks = 0;
                o->p[w++] = '\n';
                if (w >= 2 && o->p[w - 2] == '\n') blanks = 1;
            } else { o->p[w++] = o->p[r]; blanks = 0; }
        }
        if (o->p) {
            for (;;) {                                           /* trailing blank lines, also those that only hold quote marks */
                size_t e = w, ls;
                while (e > 0 && (o->p[e - 1] == '\n' || o->p[e - 1] == ' ')) e--;
                ls = e;
                while (ls > 0 && o->p[ls - 1] != '\n') ls--;
                if (e > ls && e - ls <= 8) { size_t q; int only = 1; for (q = ls; q < e; q++) if (o->p[q] != '>' && o->p[q] != ' ') only = 0; if (only) { w = ls; continue; } }
                w = e;
                break;
            }
            o->n = w; o->p[w] = 0;
            if (w) bc(o, '\n');                                  /* through bput: the buffer may be exactly full (found by the fuzz target in CI, run 45185859148) */
        }
        {   size_t lead = 0; while (o->p && lead < o->n && (o->p[lead] == '\n' || o->p[lead] == ' ')) lead++; if (lead) { memmove(o->p, o->p + lead, o->n - lead + 1); o->n -= lead; } }
    }
    if (title) {
        Buf *t = s->title.n ? &s->title : &s->h1;
        if (t->n) {
            size_t a = 0, b = t->n;
            while (a < b && isspace((unsigned char)t->p[a])) a++;
            while (b > a && isspace((unsigned char)t->p[b - 1])) b--;
            if (b > a) { char *x = malloc(b - a + 1); if (x) { memcpy(x, t->p + a, b - a); x[b - a] = 0; *title = x; } }
        }
    }
    if (s->out.oom || !s->out.p) { res = malloc(1); if (res) res[0] = 0; if (out_len) *out_len = 0; }
    else { res = s->out.p; if (out_len) *out_len = s->out.n; s->out.p = NULL; }
    free(s->out.p); free(s->title.p); free(s->h1.p); free(s->cell.p); free(s->lang);
    { int r2, c2; for (r2 = 0; r2 <= s->nrows && r2 < MAX_TABLE_ROWS; r2++) { if (!s->rows[r2]) continue; for (c2 = 0; c2 < s->rowcols[r2]; c2++) free(s->rows[r2][c2].text); free(s->rows[r2]); } }
    { int d; for (d = 0; d < s->depth; d++) free(s->st[d].href); }
    free(s);
    return res;
}
