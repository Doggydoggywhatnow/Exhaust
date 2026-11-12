#include <stdlib.h>
#include <string.h>

#include "editor_fold.h"

void ex_folds_free(ExFolds *f) { free(f->r); f->r = NULL; f->n = f->cap = 0; }

typedef struct Cand { size_t hdr_off; size_t end; unsigned level; int heading; } Cand;
typedef struct Collect { Cand *c; size_t n, cap; int headings, lists; int oom; } Collect;

static int collect_span(const ExMdSpan *s, void *user) {
    Collect *c = user;
    int is_h = s->kind == EX_MD_HEADING && s->parent_kind == 0 && c->headings;
    int is_l = s->kind == EX_MD_LIST_ITEM && c->lists;
    if (!is_h && !is_l) return 0;
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 64;
        Cand *p = realloc(c->c, nc * sizeof *p);
        if (!p) { c->oom = 1; return 1; }
        c->c = p;
        c->cap = nc;
    }
    c->c[c->n].hdr_off = s->start;
    c->c[c->n].end = s->end;
    c->c[c->n].level = s->level;
    c->c[c->n].heading = is_h;
    c->n++;
    return 0;
}

static int cmp_range(const void *x, const void *y) {
    const ExFoldRange *a = x, *b = y;
    if (a->hdr != b->hdr) return a->hdr < b->hdr ? -1 : 1;
    return a->last > b->last ? -1 : a->last < b->last ? 1 : 0;
}

int ex_folds_compute(ExFolds *f, const ExMd *md, const ExBuffer *b, int headings, int lists) {
    Collect c;
    size_t i, j, size = ex_buffer_size(b), nl = ex_buffer_line_count(b);
    size_t *stack = NULL, sp = 0;
    memset(&c, 0, sizeof c);
    c.headings = headings;
    c.lists = lists;
    f->n = 0;
    if (!md || (!headings && !lists)) return 0;
    ex_md_visit(md, 0, size, collect_span, &c);
    if (c.oom) { free(c.c); return -1; }
    if (c.n > f->cap) {
        ExFoldRange *p = realloc(f->r, c.n * sizeof *p);
        if (!p) { free(c.c); return -1; }
        f->r = p;
        f->cap = c.n;
    }
    for (i = 0; i < c.n; i++) {
        size_t hdr = ex_buffer_line_of_offset(b, c.c[i].hdr_off), last;
        if (c.c[i].heading) {
            size_t he = c.c[i].end;                              /* a setext heading has two lines: the header is the last */
            char ch;
            if (he > c.c[i].hdr_off && ex_buffer_read(b, he - 1, &ch, 1) == 1 && ch == '\n') he--;
            hdr = ex_buffer_line_of_offset(b, he);
            last = nl - 1;                                       /* up to the next heading of the same or a higher level */
            for (j = i + 1; j < c.n; j++)
                if (c.c[j].heading && c.c[j].level <= c.c[i].level) { last = ex_buffer_line_of_offset(b, c.c[j].hdr_off); last = last ? last - 1 : 0; break; }
        } else {
            size_t end = c.c[i].end;
            char ch;
            if (end > c.c[i].hdr_off && ex_buffer_read(b, end - 1, &ch, 1) == 1 && ch == '\n') end--;
            last = ex_buffer_line_of_offset(b, end);
        }
        if (last <= hdr) continue;                               /* nothing below the header line */
        {
            ExFoldRange *r = &f->r[f->n++];
            r->hdr = hdr;
            r->first = hdr + 1;
            r->last = last;
            r->hs = ex_buffer_line_start(b, hdr + 1);
            r->he = last + 1 < nl ? ex_buffer_line_start(b, last + 1) : size;
            r->depth = 0;
            r->heading = c.c[i].heading;
        }
    }
    free(c.c);
    qsort(f->r, f->n, sizeof *f->r, cmp_range);
    stack = malloc((f->n ? f->n : 1) * sizeof *stack);
    if (!stack) return -1;
    for (i = 0; i < f->n; i++) {                                 /* depth: how many regions enclose this header line */
        while (sp && f->r[stack[sp - 1]].last < f->r[i].hdr) sp--;
        f->r[i].depth = (unsigned)sp;
        stack[sp++] = i;
    }
    free(stack);
    return 0;
}

long ex_folds_find_hs(const ExFolds *f, size_t hs) {
    size_t lo = 0, hi = f->n;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (f->r[m].hs == hs) return (long)m;
        if (f->r[m].hs < hs) lo = m + 1; else hi = m;
    }
    return -1;
}

long ex_folds_find_hdr(const ExFolds *f, size_t hdr) {
    size_t lo = 0, hi = f->n;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (f->r[m].hdr == hdr) return (long)m;
        if (f->r[m].hdr < hdr) lo = m + 1; else hi = m;
    }
    return -1;
}
