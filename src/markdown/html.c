#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"
#include "exhaust_text.h"

#define MAX_DEPTH 64

static const char *const k_names[EX_HTML_TAG_COUNT] = {
    "", "a", "abbr", "b", "blockquote", "br", "caption", "code", "del", "details", "div", "em", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "i", "img", "ins",
    "kbd", "li", "mark", "ol", "p", "pre", "q", "s", "samp", "small", "span", "strong", "sub", "summary", "sup", "table", "tbody", "td", "tfoot", "th", "thead",
    "tr", "u", "ul", "var"};

const char *ex_html_tag_name(ExHtmlTag t) { return t > 0 && t < EX_HTML_TAG_COUNT ? k_names[t] : ""; }

ExHtmlTag ex_html_tag_from_name(const char *name, size_t len) {
    int t;
    for (t = 1; t < EX_HTML_TAG_COUNT; t++) {
        const char *n = k_names[t];
        size_t i;
        if (strlen(n) != len) continue;
        for (i = 0; i < len && tolower((unsigned char)name[i]) == n[i]; i++) {}
        if (i == len) return (ExHtmlTag)t;
    }
    return EX_HTML_TEXT;
}

int ex_html_is_block_tag(ExHtmlTag t) {
    switch (t) {
    case EX_HTML_BLOCKQUOTE: case EX_HTML_CAPTION: case EX_HTML_DETAILS: case EX_HTML_DIV: case EX_HTML_H1: case EX_HTML_H2: case EX_HTML_H3: case EX_HTML_H4:
    case EX_HTML_H5: case EX_HTML_H6: case EX_HTML_HR: case EX_HTML_LI: case EX_HTML_OL: case EX_HTML_P: case EX_HTML_PRE: case EX_HTML_SUMMARY: case EX_HTML_TABLE:
    case EX_HTML_TBODY: case EX_HTML_TD: case EX_HTML_TFOOT: case EX_HTML_TH: case EX_HTML_THEAD: case EX_HTML_TR: case EX_HTML_UL: return 1;
    default: return 0;
    }
}

static int is_void(ExHtmlTag t) { return t == EX_HTML_BR || t == EX_HTML_HR || t == EX_HTML_IMG; }

/* ---- storage ---- */

typedef struct Chunk { struct Chunk *next; size_t used, cap; char data[1]; } Chunk;

struct ExHtml {
    ExHtmlNode *root;
    Chunk *chunks;
    ExHtmlNode **all; size_t nall, capall;
};

static void *arena_alloc(ExHtml *h, size_t n) {
    Chunk *c = h->chunks;
    void *p;
    n = (n + 15) & ~(size_t)15;
    if (!c || c->used + n > c->cap) {
        size_t cap = n > 8192 ? n : 8192;
        Chunk *nc = malloc(sizeof *nc + cap);
        if (!nc) return NULL;
        nc->next = h->chunks; nc->used = 0; nc->cap = cap;
        h->chunks = nc;
        c = nc;
    }
    p = c->data + c->used;
    c->used += n;
    return p;
}

static char *arena_str(ExHtml *h, const char *s, size_t n) {
    char *p = arena_alloc(h, n + 1);
    if (p) { memcpy(p, s, n); p[n] = 0; }
    return p;
}

static ExHtmlNode *new_node(ExHtml *h, ExHtmlTag tag) {
    ExHtmlNode *n = arena_alloc(h, sizeof *n);
    if (!n) return NULL;
    memset(n, 0, sizeof *n);
    n->tag = tag;
    if (h->nall == h->capall) {
        size_t nc = h->capall ? h->capall * 2 : 64;
        ExHtmlNode **na = realloc(h->all, nc * sizeof *na);
        if (!na) return NULL;
        h->all = na;
        h->capall = nc;
    }
    h->all[h->nall++] = n;
    return n;
}

static int add_kid(ExHtmlNode *parent, ExHtmlNode *kid) {
    ExHtmlNode **nk = realloc(parent->kids, (parent->nkids + 1) * sizeof *nk);
    if (!nk) return -1;
    parent->kids = nk;
    parent->kids[parent->nkids++] = kid;
    kid->parent = parent;
    return 0;
}

void ex_html_free(ExHtml *h) {
    size_t i;
    Chunk *c, *next;
    if (!h) return;
    for (i = 0; i < h->nall; i++) { free(h->all[i]->kids); free(h->all[i]->attrs); }
    free(h->all);
    for (c = h->chunks; c; c = next) { next = c->next; free(c); }
    free(h);
}

const ExHtmlNode *ex_html_root(const ExHtml *h) { return h->root; }

const char *ex_html_attr(const ExHtmlNode *n, const char *name) {
    size_t i;
    for (i = 0; i < n->nattrs; i++) if (strcmp(n->attrs[i].name, name) == 0) return n->attrs[i].value;
    return NULL;
}

/* ---- character references ---- */

static const struct { const char *name; unsigned cp; } k_entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE}, {"hellip", 0x2026},
    {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"bull", 0x2022}, {"middot", 0xB7}, {"trade", 0x2122}, {"deg", 0xB0},
    {"plusmn", 0xB1}, {"times", 0xD7}, {"divide", 0xF7}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5}, {"cent", 0xA2}, {"sect", 0xA7}, {"para", 0xB6},
    {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"hearts", 0x2665}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C},
    {"rdquo", 0x201D}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"shy", 0xAD}, {"check", 0x2713}};

/* Decodes the reference at s[i] == '&'. Returns the bytes consumed (0: not a reference) and the code point. */
static size_t decode_ref(const char *s, size_t len, size_t i, unsigned *cp) {
    size_t j = i + 1;
    if (j < len && s[j] == '#') {
        unsigned long v = 0;
        int hex = 0;
        size_t digits = 0;
        j++;
        if (j < len && (s[j] == 'x' || s[j] == 'X')) { hex = 1; j++; }
        while (j < len && digits < 8 && (hex ? isxdigit((unsigned char)s[j]) : isdigit((unsigned char)s[j]))) {
            int d = isdigit((unsigned char)s[j]) ? s[j] - '0' : (tolower((unsigned char)s[j]) - 'a' + 10);
            v = v * (hex ? 16 : 10) + (unsigned long)d;
            j++; digits++;
        }
        if (digits == 0 || v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
        if (j < len && s[j] == ';') j++;
        *cp = (unsigned)v;
        return j - i;
    } else {
        size_t k, start = j;
        while (j < len && j - start < 10 && isalnum((unsigned char)s[j])) j++;
        if (j >= len || s[j] != ';' || j == start) return 0;
        for (k = 0; k < sizeof k_entities / sizeof k_entities[0]; k++)
            if (strlen(k_entities[k].name) == j - start && memcmp(k_entities[k].name, s + start, j - start) == 0) { *cp = k_entities[k].cp; return j + 1 - i; }
        return 0;
    }
}

/* The text with references decoded, into a malloc'ed buffer. */
static char *decode(const char *s, size_t len) {
    char *out = malloc(len * 4 + 1), *o = out;
    size_t i = 0;
    if (!out) return NULL;
    while (i < len) {
        unsigned cp;
        size_t used;
        if (s[i] == '&' && (used = decode_ref(s, len, i, &cp)) != 0) { o += ex_utf8_encode(cp, o); i += used; }
        else *o++ = s[i++];
    }
    *o = 0;
    return out;
}

/* ---- urls ---- */

int ex_html_url_allowed(const char *url, size_t len) {
    size_t i = 0, k;
    char scheme[16];
    while (i < len && (unsigned char)url[i] <= ' ') i++;                     /* leading spaces and controls are ignored by browsers: so are they here */
    for (k = i; k < len; k++) if ((unsigned char)url[k] < ' ' && url[k] != '\t') return 0;
    for (k = i; k < len && url[k] != ':' && url[k] != '/' && url[k] != '?' && url[k] != '#'; k++) {}
    if (k < len && url[k] == ':') {                                          /* a scheme */
        size_t n = k - i;
        if (n == 0 || n >= sizeof scheme) return 0;
        for (n = 0; n < k - i; n++) scheme[n] = (char)tolower((unsigned char)url[i + n]);
        scheme[k - i] = 0;
        return strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0 || strcmp(scheme, "mailto") == 0;
    }
    return 1;
}

/* ---- parsing ---- */

typedef struct P {
    ExHtml *h;
    ExHtmlNode *stack[MAX_DEPTH + 1];
    int depth;                           /* stack[0] is the root */
    unsigned skipped[EX_HTML_TAG_COUNT]; /* elements too deep to open: their end tags must not close a real element */
} P;

static ExHtmlNode *cur(P *p) { return p->stack[p->depth]; }

static void add_text(P *p, const char *s, size_t len) {
    ExHtmlNode *parent = cur(p), *last;
    char *dec, *joined;
    size_t dl;
    if (len == 0) return;
    dec = decode(s, len);
    if (!dec) return;
    dl = strlen(dec);
    last = parent->nkids ? parent->kids[parent->nkids - 1] : NULL;
    if (last && last->tag == EX_HTML_TEXT) {                                 /* adjacent text is one node */
        size_t ol = strlen(last->text);
        joined = arena_alloc(p->h, ol + dl + 1);
        if (joined) { memcpy(joined, last->text, ol); memcpy(joined + ol, dec, dl + 1); last->text = joined; }
    } else {
        ExHtmlNode *n = new_node(p->h, EX_HTML_TEXT);
        if (n) { n->text = arena_str(p->h, dec, dl); add_kid(parent, n); }
    }
    free(dec);
}

static int attr_allowed(ExHtmlTag tag, const char *name) {
    if (!strcmp(name, "title") || !strcmp(name, "id")) return 1;
    if (!strcmp(name, "href")) return tag == EX_HTML_A;
    if (!strcmp(name, "src") || !strcmp(name, "alt") || !strcmp(name, "width") || !strcmp(name, "height")) return tag == EX_HTML_IMG;
    if (!strcmp(name, "colspan") || !strcmp(name, "rowspan")) return tag == EX_HTML_TD || tag == EX_HTML_TH;
    if (!strcmp(name, "align")) return tag == EX_HTML_P || tag == EX_HTML_DIV || tag == EX_HTML_TD || tag == EX_HTML_TH || tag == EX_HTML_TR || tag == EX_HTML_THEAD ||
                                       tag == EX_HTML_TBODY || tag == EX_HTML_TFOOT || tag == EX_HTML_TABLE || tag == EX_HTML_CAPTION || (tag >= EX_HTML_H1 && tag <= EX_HTML_H6);
    if (!strcmp(name, "open")) return tag == EX_HTML_DETAILS;
    return 0;
}

static int value_ok(const char *name, const char *value) {
    size_t i, n = strlen(value);
    if (!strcmp(name, "href") || !strcmp(name, "src")) return ex_html_url_allowed(value, n);
    if (!strcmp(name, "width") || !strcmp(name, "height") || !strcmp(name, "colspan") || !strcmp(name, "rowspan")) {
        if (n == 0 || n > 5) return 0;
        for (i = 0; i < n; i++) if (!isdigit((unsigned char)value[i])) return 0;
        return 1;
    }
    if (!strcmp(name, "align")) return !strcmp(value, "left") || !strcmp(value, "center") || !strcmp(value, "right");
    if (!strcmp(name, "id")) {
        if (n == 0 || n > 100) return 0;
        for (i = 0; i < n; i++) if (!isalnum((unsigned char)value[i]) && !strchr("_:.-", value[i])) return 0;
        return 1;
    }
    return 1;
}

/* Parses the attributes of a tag in s[i..end) into the node. */
static void parse_attrs(P *p, ExHtmlNode *node, const char *s, size_t i, size_t end) {
    while (i < end) {
        size_t ns, ne, vs = 0, ve = 0;
        char name[24];
        char *value = NULL;
        while (i < end && (isspace((unsigned char)s[i]) || s[i] == '/')) i++;
        if (i >= end) break;
        ns = i;
        while (i < end && !isspace((unsigned char)s[i]) && s[i] != '=' && s[i] != '/' && s[i] != '>') i++;
        ne = i;
        if (ne == ns) { i++; continue; }
        while (i < end && isspace((unsigned char)s[i])) i++;
        if (i < end && s[i] == '=') {
            i++;
            while (i < end && isspace((unsigned char)s[i])) i++;
            if (i < end && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                vs = i;
                while (i < end && s[i] != q) i++;
                ve = i;
                if (i < end) i++;
            } else {
                vs = i;
                while (i < end && !isspace((unsigned char)s[i]) && s[i] != '>') i++;
                ve = i;
            }
            value = decode(s + vs, ve - vs);
        } else value = decode("", 0);                                        /* an attribute without a value, like open */
        if (!value) continue;
        if (ne - ns < sizeof name) {
            size_t k;
            for (k = 0; k < ne - ns; k++) name[k] = (char)tolower((unsigned char)s[ns + k]);
            name[ne - ns] = 0;
            if (attr_allowed(node->tag, name) && value_ok(name, value) && !ex_html_attr(node, name)) {
                ExHtmlAttr *na = realloc(node->attrs, (node->nattrs + 1) * sizeof *na);
                if (na) {
                    node->attrs = na;
                    na[node->nattrs].name = arena_str(p->h, name, strlen(name));
                    na[node->nattrs].value = arena_str(p->h, value, strlen(value));
                    node->nattrs++;
                }
            }
        }
        free(value);
    }
}

static void close_to(P *p, int depth) { if (depth >= 0 && depth < p->depth) p->depth = depth; }

/* Finds the nearest open element with this tag, stopping at a boundary tag; returns its depth or -1. */
static int find_open(P *p, ExHtmlTag tag, ExHtmlTag b1, ExHtmlTag b2, ExHtmlTag b3) {
    int d;
    for (d = p->depth; d >= 1; d--) {
        ExHtmlTag t = p->stack[d]->tag;
        if (t == tag) return d;
        if (t == b1 || t == b2 || t == b3) return -1;
    }
    return -1;
}

static void implied_closes(P *p, ExHtmlTag tag) {
    int d;
    switch (tag) {
    case EX_HTML_LI:  d = find_open(p, EX_HTML_LI, EX_HTML_UL, EX_HTML_OL, EX_HTML_LI); if (d > 0) close_to(p, d - 1); break;
    case EX_HTML_TD: case EX_HTML_TH:
        d = find_open(p, EX_HTML_TD, EX_HTML_TR, EX_HTML_TABLE, EX_HTML_TABLE); if (d > 0) close_to(p, d - 1);
        d = find_open(p, EX_HTML_TH, EX_HTML_TR, EX_HTML_TABLE, EX_HTML_TABLE); if (d > 0) close_to(p, d - 1);
        break;
    case EX_HTML_TR:
        d = find_open(p, EX_HTML_TR, EX_HTML_TABLE, EX_HTML_TBODY, EX_HTML_THEAD); if (d > 0) close_to(p, d - 1);
        break;
    case EX_HTML_THEAD: case EX_HTML_TBODY: case EX_HTML_TFOOT:
        d = find_open(p, EX_HTML_THEAD, EX_HTML_TABLE, EX_HTML_TABLE, EX_HTML_TABLE); if (d > 0) close_to(p, d - 1);
        d = find_open(p, EX_HTML_TBODY, EX_HTML_TABLE, EX_HTML_TABLE, EX_HTML_TABLE); if (d > 0) close_to(p, d - 1);
        d = find_open(p, EX_HTML_TFOOT, EX_HTML_TABLE, EX_HTML_TABLE, EX_HTML_TABLE); if (d > 0) close_to(p, d - 1);
        break;
    default:
        if (ex_html_is_block_tag(tag) && cur(p)->tag == EX_HTML_P) close_to(p, p->depth - 1);     /* a block closes an open paragraph */
        break;
    }
}

static int ieq(const char *a, size_t alen, const char *b) {
    size_t i, n = strlen(b);
    if (alen != n) return 0;
    for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != b[i]) return 0;
    return 1;
}

static int is_raw_text_tag(const char *name, size_t len) { return ieq(name, len, "script") || ieq(name, len, "style"); }

/* The end of a tag that starts at s[i] == '<': the index of its '>' (quotes in attributes are respected), or len. */
static size_t tag_end(const char *s, size_t len, size_t i) {
    char q = 0;
    for (i++; i < len; i++) {
        if (q) { if (s[i] == q) q = 0; }
        else if (s[i] == '"' || s[i] == '\'') q = s[i];
        else if (s[i] == '>') return i;
    }
    return len;
}

ExHtml *ex_html_parse(const char *s, size_t len) {
    ExHtml *h = calloc(1, sizeof *h);
    P p;
    size_t i = 0, text_start = 0;
    if (!h) return NULL;
    h->root = new_node(h, EX_HTML_TEXT);                                    /* the container: tag 0 and no text */
    if (!h->root) { ex_html_free(h); return NULL; }
    memset(&p, 0, sizeof p);
    p.h = h;
    p.stack[0] = h->root;
    while (i < len) {
        size_t e, ns, ne;
        int closing;
        if (s[i] != '<') { i++; continue; }
        if (i + 3 < len && memcmp(s + i, "<!--", 4) == 0) {                /* a comment is dropped */
            size_t k;
            add_text(&p, s + text_start, i - text_start);
            for (k = i + 4; k + 2 < len && memcmp(s + k, "-->", 3) != 0; k++) {}
            i = k + 2 < len ? k + 3 : len;
            text_start = i;
            continue;
        }
        if (i + 1 < len && (s[i + 1] == '!' || s[i + 1] == '?')) {          /* a doctype or processing instruction */
            add_text(&p, s + text_start, i - text_start);
            e = tag_end(s, len, i);
            i = e < len ? e + 1 : len;
            text_start = i;
            continue;
        }
        closing = i + 1 < len && s[i + 1] == '/';
        ns = i + 1 + (size_t)closing;
        if (ns >= len || !isalpha((unsigned char)s[ns])) { i++; continue; }  /* a lone < is text */
        for (ne = ns; ne < len && (isalnum((unsigned char)s[ne]) || s[ne] == '-'); ne++) {}
        e = tag_end(s, len, i);
        add_text(&p, s + text_start, i - text_start);
        {
            ExHtmlTag tag = ex_html_tag_from_name(s + ns, ne - ns);
            if (!closing && is_raw_text_tag(s + ns, ne - ns)) {            /* script and style: the content is dropped with the tags */
                const char *raw = ieq(s + ns, ne - ns, "script") ? "script" : "style";
                size_t k = e < len ? e + 1 : len, rl = strlen(raw);
                while (k < len) {                                            /* up to the matching end tag */
                    if (s[k] == '<' && k + 1 < len && s[k + 1] == '/' && k + 2 + rl <= len && ieq(s + k + 2, rl, raw)) break;
                    k++;
                }
                e = k < len ? tag_end(s, len, k) : len;
            } else if (tag != EX_HTML_TEXT) {
                if (closing) {
                    if (p.skipped[tag] > 0) p.skipped[tag]--;                 /* the end tag of an element that was too deep to open */
                    else {
                        int d = find_open(&p, tag, EX_HTML_TEXT, EX_HTML_TEXT, EX_HTML_TEXT);
                        if (d > 0) close_to(&p, d - 1);
                    }
                } else {
                    ExHtmlNode *n;
                    implied_closes(&p, tag);
                    n = new_node(h, tag);
                    if (n) {
                        int self_closing = e > i && e <= len && e > 0 && s[e - 1] == '/';
                        parse_attrs(&p, n, s, ne, e < len ? e : len);
                        add_kid(cur(&p), n);
                        if (!is_void(tag) && !self_closing) {
                            if (p.depth < MAX_DEPTH) p.stack[++p.depth] = n;
                            else p.skipped[tag]++;                              /* deeper than the limit: not opened, its content stays in its parent */
                        }
                    }
                }
            }                                                               /* a tag not in the subset is dropped; its text stays */
        }
        i = e < len ? e + 1 : len;
        text_start = i;
    }
    add_text(&p, s + text_start, len - text_start);
    return h;
}

/* ---- text and serialization ---- */

typedef struct Out { char *p; size_t n, cap; } Out;

static void out_put(Out *o, const char *s, size_t n) {
    if (o->n + n + 1 > o->cap) {
        size_t nc = (o->n + n + 1) * 2;
        char *np = realloc(o->p, nc);
        if (!np) return;
        o->p = np; o->cap = nc;
    }
    memcpy(o->p + o->n, s, n);
    o->n += n;
    o->p[o->n] = 0;
}
static void out_str(Out *o, const char *s) { out_put(o, s, strlen(s)); }

static void out_escaped(Out *o, const char *s, int attr) {
    for (; *s; s++) {
        if (*s == '&') out_str(o, "&amp;");
        else if (*s == '<') out_str(o, "&lt;");
        else if (*s == '>') out_str(o, "&gt;");
        else if (attr && *s == '"') out_str(o, "&quot;");
        else out_put(o, s, 1);
    }
}

static void text_of(const ExHtmlNode *n, Out *o) {
    size_t i;
    if (n->tag == EX_HTML_TEXT && n->text) out_str(o, n->text);
    for (i = 0; i < n->nkids; i++) text_of(n->kids[i], o);
}

char *ex_html_text(const ExHtml *h) {
    Out o = {0};
    text_of(h->root, &o);
    if (!o.p) { o.p = malloc(1); if (o.p) o.p[0] = 0; }
    return o.p;
}

static void ser(const ExHtmlNode *n, Out *o) {
    size_t i;
    if (n->tag == EX_HTML_TEXT) {
        if (n->text) out_escaped(o, n->text, 0);
        for (i = 0; i < n->nkids; i++) ser(n->kids[i], o);
        return;
    }
    out_str(o, "<"); out_str(o, ex_html_tag_name(n->tag));
    for (i = 0; i < n->nattrs; i++) { out_str(o, " "); out_str(o, n->attrs[i].name); out_str(o, "=\""); out_escaped(o, n->attrs[i].value, 1); out_str(o, "\""); }
    out_str(o, ">");
    if (is_void(n->tag)) return;
    for (i = 0; i < n->nkids; i++) ser(n->kids[i], o);
    out_str(o, "</"); out_str(o, ex_html_tag_name(n->tag)); out_str(o, ">");
}

char *ex_html_serialize(const ExHtml *h) {
    Out o = {0};
    ser(h->root, &o);
    if (!o.p) { o.p = malloc(1); if (o.p) o.p[0] = 0; }
    return o.p;
}
