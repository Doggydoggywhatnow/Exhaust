#include <stdlib.h>
#include <string.h>

#include "exhaust_extract.h"
#include "exhaust_nav.h"

int ex_outline(const char *text, size_t len, ExOutlineItem **out, size_t *n) {
    ExExtract *x = ex_extract(text, len);
    ExOutlineItem *v;
    size_t i;
    *out = NULL; *n = 0;
    if (!x) return -1;
    v = calloc(x->nheadings + 1, sizeof *v);
    if (!v) { ex_extract_free(x); return -1; }
    for (i = 0; i < x->nheadings; i++) {
        const ExXHeading *h = &x->headings[i];
        size_t tl = strlen(h->text);
        v[i].level = h->level; v[i].start = h->start; v[i].end = h->end; v[i].section_end = h->section_end; v[i].line = h->line;
        v[i].text = malloc(tl + 1);
        if (v[i].text) memcpy(v[i].text, h->text, tl + 1);
    }
    *out = v; *n = x->nheadings;
    ex_extract_free(x);
    return 0;
}

void ex_outline_free(ExOutlineItem *items, size_t n) { size_t i; for (i = 0; i < n; i++) free(items[i].text); free(items); }

char *ex_outline_move(const char *text, size_t len, size_t from, size_t to, size_t *out_len) {
    ExOutlineItem *items;
    size_t n, bs, be, pos, rl, o = 0, bl;
    char *r, *res;
    int block_has_nl, r_has_nl;
    if (ex_outline(text, len, &items, &n) != 0) return NULL;
    if (from >= n || to > n || to == from) { ex_outline_free(items, n); return NULL; }
    bs = items[from].start; be = items[from].section_end;
    pos = to < n ? items[to].start : len;
    if (pos >= bs && pos <= be) { ex_outline_free(items, n); return NULL; }   /* into its own sections, or where it already is */
    ex_outline_free(items, n);
    /* the text without the section, and the place in it */
    rl = len - (be - bs);
    r = malloc(rl + 1);
    if (!r) return NULL;
    memcpy(r, text, bs);
    memcpy(r + bs, text + be, len - be);
    r[rl] = 0;
    if (pos >= be) pos -= be - bs;
    block_has_nl = be > bs && text[be - 1] == '\n';
    r_has_nl = rl > 0 && r[rl - 1] == '\n';
    bl = be - bs;
    res = malloc(rl + bl + 4);
    if (!res) { free(r); return NULL; }
    memcpy(res, r, pos); o = pos;
    if (pos == rl && rl > 0 && !r_has_nl) res[o++] = '\n';                  /* to the end of a note that does not end with a line break */
    memcpy(res + o, text + bs, bl); o += bl;
    if (!block_has_nl) res[o++] = '\n';                                     /* the block came from the end of the note */
    memcpy(res + o, r + pos, rl - pos); o += rl - pos;
    /* the end of the note keeps the way it ended */
    {
        int orig_nl = len > 0 && text[len - 1] == '\n';
        if (!orig_nl && o > 0 && res[o - 1] == '\n') o--;
    }
    res[o] = 0;
    free(r);
    if (out_len) *out_len = o;
    return res;
}
