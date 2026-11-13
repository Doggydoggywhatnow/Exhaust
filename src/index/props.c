/* The properties of a note: the frontmatter read with libyaml. The note is never rewritten from what is read here: every
 * property remembers the lines it occupies so an edit changes only those lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yaml.h>

#include "exhaust_extract.h"
#include "extract_internal.h"

static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (!p) return NULL; memcpy(p, s, n); p[n] = 0; return p; }

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int looks_number(const char *s) {
    size_t i = 0;
    int digits = 0, dot = 0;
    if (s[i] == '-' || s[i] == '+') i++;
    for (; s[i]; i++) {
        if (is_digit(s[i])) digits++;
        else if (s[i] == '.' && !dot) dot = 1;
        else return 0;
    }
    return digits > 0;
}

static int looks_date(const char *s) {   /* YYYY-MM-DD */
    return strlen(s) == 10 && is_digit(s[0]) && is_digit(s[1]) && is_digit(s[2]) && is_digit(s[3]) && s[4] == '-' && is_digit(s[5]) && is_digit(s[6]) && s[7] == '-' && is_digit(s[8]) && is_digit(s[9]);
}

static int looks_datetime(const char *s) {   /* YYYY-MM-DDTHH:MM[:SS[.fff]][Z|+hh:mm] or with a space */
    size_t n = strlen(s);
    if (n < 16) return 0;
    if (!(is_digit(s[0]) && is_digit(s[1]) && is_digit(s[2]) && is_digit(s[3]) && s[4] == '-' && is_digit(s[5]) && is_digit(s[6]) && s[7] == '-' && is_digit(s[8]) && is_digit(s[9]))) return 0;
    if (s[10] != 'T' && s[10] != ' ') return 0;
    return is_digit(s[11]) && is_digit(s[12]) && s[13] == ':' && is_digit(s[14]) && is_digit(s[15]);
}

int ex_prop_type_of_text(const char *s, int quoted) {
    if (quoted) return strstr(s, "[[") ? EX_PROP_LINKS : EX_PROP_TEXT;
    if (!strcmp(s, "true") || !strcmp(s, "false")) return EX_PROP_CHECKBOX;
    if (looks_number(s)) return EX_PROP_NUMBER;
    if (looks_date(s)) return EX_PROP_DATE;
    if (looks_datetime(s)) return EX_PROP_DATETIME;
    if (strstr(s, "[[")) return EX_PROP_LINKS;
    return EX_PROP_TEXT;
}

typedef struct B { char *v; size_t n, cap; } B;
static void badd(B *b, const char *s, size_t n) {
    if (b->n + n + 2 > b->cap) { size_t nc = (b->n + n + 2) * 2; char *nv = realloc(b->v, nc); if (!nv) return; b->v = nv; b->cap = nc; }
    memcpy(b->v + b->n, s, n);
    b->n += n;
    b->v[b->n] = 0;
}

static int add_prop(ExExtract *x, size_t *cap, const char *name, size_t nlen, uint32_t line) {
    ExXProp *p;
    size_t i;
    for (i = 0; i < x->nprops; i++) if (strlen(x->props[i].name) == nlen && !memcmp(x->props[i].name, name, nlen)) return -1;   /* a key twice: not valid */
    if (x->nprops == *cap) { size_t nc = *cap ? *cap * 2 : 8; ExXProp *nv = realloc(x->props, nc * sizeof *nv); if (!nv) return -1; x->props = nv; *cap = nc; }
    p = &x->props[x->nprops++];
    memset(p, 0, sizeof *p);
    p->name = dupn(name, nlen);
    p->line = line;
    return p->name ? 0 : -1;
}

static int add_link(ExExtract *x, size_t *cap, const char *text, size_t s, size_t e, uint32_t line) {
    /* text[s..e) is "[[...]]" */
    ExXLink *l;
    const char *in = text + s + 2;
    size_t n = e - s - 4, bar, hash, i;
    if (x->nlinks == *cap) { size_t nc = *cap ? *cap * 2 : 8; ExXLink *nv = realloc(x->links, nc * sizeof *nv); if (!nv) return -1; x->links = nv; *cap = nc; }
    l = &x->links[x->nlinks++];
    memset(l, 0, sizeof *l);
    l->kind = EX_LINK_PROP; l->start = s; l->end = e; l->line = line;
    for (bar = 0; bar < n && in[bar] != '|'; bar++) {}
    for (hash = 0; hash < bar && in[hash] != '#'; hash++) {}
    l->target = dupn(in, hash);
    if (hash < bar) {
        if (hash + 1 < bar && in[hash + 1] == '^') l->block = dupn(in + hash + 2, bar - hash - 2); else l->heading = dupn(in + hash + 1, bar - hash - 1);
    }
    if (bar < n) l->alias = dupn(in + bar + 1, n - bar - 1);
    for (i = 0; i < 1; i++) {}
    return l->target ? 0 : -1;
}

static int add_tag(ExExtract *x, size_t *cap, const char *s, size_t n, size_t at, uint32_t line) {
    ExXTag *t;
    while (n && *s == '#') { s++; n--; }
    while (n && (s[n - 1] == ' ')) n--;
    if (!n) return 0;
    if (x->ntags == *cap) { size_t nc = *cap ? *cap * 2 : 8; ExXTag *nv = realloc(x->tags, nc * sizeof *nv); if (!nv) return -1; x->tags = nv; *cap = nc; }
    t = &x->tags[x->ntags++];
    memset(t, 0, sizeof *t);
    t->tag = dupn(s, n); t->start = at; t->line = line; t->in_props = 1;
    return t->tag ? 0 : -1;
}

void ex_extract_props(ExExtract *x, const char *text, size_t len) {
    size_t cs, ls, e = x->front_end, i, pcap = 0, lcap = x->nlinks, tcap = x->ntags;
    size_t scan_byte = 0, scan_char = 0;          /* a position in the content and the character count libyaml reports for it */
    yaml_parser_t parser;
    yaml_event_t ev;
    int depth = 0, want_key = 1, in_list = 0, ok = 1, quoted_val = 0, done = 0, any_tags = 0;
    B items = {0};
    if (x->front_end <= x->front_start || x->front_end > len) return;
    cs = x->front_start;
    while (cs < e && text[cs] != '\n') cs++;
    if (cs < e) cs++;
    ls = e;
    while (ls > cs && text[ls - 1] != '\n') ls--;
    if (ls < cs) ls = cs;
    if (!yaml_parser_initialize(&parser)) return;
    yaml_parser_set_input_string(&parser, (const unsigned char *)text + cs, ls - cs);
    while (!done) {
        if (!yaml_parser_parse(&parser, &ev)) { ok = 0; break; }
        switch (ev.type) {
        case YAML_MAPPING_START_EVENT:
            if (depth == 0) { depth = 1; want_key = 1; if (ev.data.mapping_start.style == YAML_FLOW_MAPPING_STYLE) ok = 0; }
            else { if (depth == 1 && !want_key && x->nprops) { x->props[x->nprops - 1].type = EX_PROP_TEXT; } depth++; }
            break;
        case YAML_SEQUENCE_START_EVENT:
            if (depth == 0) ok = 0;
            else {
                if (depth == 1 && !want_key && x->nprops) { x->props[x->nprops - 1].type = EX_PROP_LIST; in_list = 1; items.n = 0; if (items.v) items.v[0] = 0; }
                depth++;
            }
            break;
        case YAML_SEQUENCE_END_EVENT: case YAML_MAPPING_END_EVENT:
            depth--;
            if (depth == 0) done = 1;
            else if (depth == 1) {
                if (in_list && x->nprops) { x->props[x->nprops - 1].value = items.v ? dupn(items.v, items.n) : dupn("", 0); in_list = 0; }
                want_key = 1;
            }
            break;
        case YAML_SCALAR_EVENT:
            if (depth == 0) { ok = 0; break; }
            if (depth == 1) {
                const char *v = (const char *)ev.data.scalar.value;
                size_t vl = ev.data.scalar.length;
                if (want_key) {
                    if (ev.start_mark.column != 0) ok = 0;                          /* only keys at the start of a line can be edited as lines */
                    else {
                        while (scan_char < ev.start_mark.index && cs + scan_byte < ls) {      /* libyaml counts characters, not bytes */
                            size_t b = 1;
                            while (cs + scan_byte + b < ls && ((unsigned char)text[cs + scan_byte + b] & 0xC0) == 0x80) b++;
                            scan_byte += b; scan_char++;
                        }
                        if (scan_char != ev.start_mark.index) ok = 0;
                        else if (add_prop(x, &pcap, v, vl, ex_extract_line_of(x, cs + scan_byte)) != 0) ok = 0;
                        else x->props[x->nprops - 1].start = cs + scan_byte;
                    }
                    want_key = 0;
                } else if (x->nprops) {
                    ExXProp *p = &x->props[x->nprops - 1];
                    quoted_val = ev.data.scalar.style != YAML_PLAIN_SCALAR_STYLE;
                    p->value = dupn(v, vl);
                    p->type = p->value ? ex_prop_type_of_text(p->value, quoted_val) : EX_PROP_TEXT;
                    if (!p->value) ok = 0;
                    want_key = 1;
                }
            } else if (in_list && depth >= 2) {
                if (items.n) badd(&items, "\n", 1);
                badd(&items, (const char *)ev.data.scalar.value, ev.data.scalar.length);
            }
            break;
        case YAML_NO_EVENT: ok = 0; break;
        default: break;
        }
        yaml_event_delete(&ev);
        if (!ok) break;
    }
    yaml_parser_delete(&parser);
    if (!ok) {                                                    /* not valid YAML: no properties, the text is left alone */
        for (i = 0; i < x->nprops; i++) { free(x->props[i].name); free(x->props[i].value); }
        x->nprops = 0;
        free(items.v);
        return;
    }
    x->front_valid = 1;
    /* the extent of each property: from its line to the line of the next one */
    for (i = 0; i < x->nprops; i++) {
        ExXProp *p = &x->props[i];
        p->end = i + 1 < x->nprops ? x->props[i + 1].start : ls;
        if (!p->value) p->value = dupn("", 0);
        if (p->type == EX_PROP_LIST) { if (p->value[0] == 0 || strstr(p->value, "[[")) { /* an empty list is still a list */ } }
        /* links written in the property */
        {
            size_t k = p->start;
            int has_link = 0;
            while (k + 4 <= p->end) {
                if (text[k] == '[' && text[k + 1] == '[') {
                    size_t j = k + 2;
                    while (j + 1 < p->end && text[j] != '\n' && !(text[j] == ']' && text[j + 1] == ']')) j++;
                    if (j + 1 < p->end && text[j] == ']' && j > k + 2) {
                        uint32_t ln = ex_extract_line_of(x, k);
                        add_link(x, &lcap, text, k, j + 2, ln);
                        has_link = 1;
                        k = j + 2;
                        continue;
                    }
                }
                k++;
            }
            if (has_link && p->type != EX_PROP_LIST) p->type = EX_PROP_LINKS;
        }
        /* tags: a list or a text of names separated by commas or spaces */
        if (!strcmp(p->name, "tags") || !strcmp(p->name, "tag")) {
            const char *v = p->value;
            size_t a = 0, n = strlen(v);
            any_tags = 1;
            while (a < n) {
                size_t b = a;
                while (b < n && v[b] != '\n' && v[b] != ',' && v[b] != ' ') b++;
                if (b > a) add_tag(x, &tcap, v + a, b - a, p->start, p->line);
                a = b + 1;
            }
        }
    }
    (void)any_tags;
    free(items.v);
}
