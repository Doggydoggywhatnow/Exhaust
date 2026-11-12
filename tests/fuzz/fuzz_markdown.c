/* libFuzzer target for the Markdown tokenizer: any bytes in. The spans must lie inside the text, be
 * sorted, and nest properly; and an edit (taken from the first bytes of the input) applied through the
 * incremental update must give exactly the spans of a full tokenization. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_markdown.h"

typedef struct Check { size_t len, prev_start; int bad; size_t n; } Check;

static int check_span(const ExMdSpan *s, void *u) {
    Check *c = u;
    if (s->start > s->end || s->end > c->len) c->bad = 1;
    if (s->start < c->prev_start) c->bad = 1;                                   /* sorted by start */
    if (s->parent_kind) {
        if (s->start < s->parent_start || s->end > s->parent_end) c->bad = 1;     /* inside its parent */
    }
    c->prev_start = s->start;
    c->n++;
    return 0;
}

typedef struct Cmp { const ExMdSpan *v; size_t n, i; int bad; } Cmp;

static int collect_cmp(const ExMdSpan *s, void *u) {
    Cmp *c = u;
    if (c->i >= c->n) { c->bad = 1; return 1; }
    if (s->kind != c->v[c->i].kind || s->start != c->v[c->i].start || s->end != c->v[c->i].end || s->level != c->v[c->i].level || s->flags != c->v[c->i].flags) c->bad = 1;
    c->i++;
    return 0;
}

typedef struct Store { ExMdSpan *v; size_t n, cap; } Store;
static int store_cb(const ExMdSpan *s, void *u) {
    Store *st = u;
    if (st->n == st->cap) { st->cap = st->cap ? st->cap * 2 : 128; st->v = realloc(st->v, st->cap * sizeof *st->v); }
    st->v[st->n++] = *s;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ExMd *m, *full;
    Check c;
    char *text, *edited;
    size_t body, off, rem, ins, elen;
    Store st = {0};
    Cmp cmp;
    if (size < 3) return 0;
    body = size - 3;
    text = malloc(body + 1);
    if (!text) return 0;
    memcpy(text, data + 3, body);
    text[body] = 0;
    m = ex_md_create();
    ex_md_tokenize(m, text, body);
    memset(&c, 0, sizeof c);
    c.len = body;
    ex_md_visit(m, 0, body + 1, check_span, &c);
    if (c.bad) abort();
    /* an edit: offset, bytes removed and one byte inserted come from the first three bytes */
    off = body ? data[0] % (body + 1) : 0;
    rem = body - off ? data[1] % (body - off + 1) : 0;
    ins = data[2] % 3;                                                          /* 0, 1 or 2 bytes of "\n#" */
    elen = body - rem + ins;
    edited = malloc(elen + 1);
    if (!edited) { ex_md_free(m); free(text); return 0; }
    memcpy(edited, text, off);
    memcpy(edited + off, "\n#", ins);
    memcpy(edited + off + ins, text + off + rem, body - off - rem);
    edited[elen] = 0;
    ex_md_update(m, edited, elen, off, rem, ins);
    full = ex_md_create();
    ex_md_tokenize(full, edited, elen);
    ex_md_visit(full, 0, elen + 1, store_cb, &st);
    cmp.v = st.v; cmp.n = st.n; cmp.i = 0; cmp.bad = 0;
    ex_md_visit(m, 0, elen + 1, collect_cmp, &cmp);
    if (cmp.bad || cmp.i != cmp.n) abort();
    free(st.v);
    ex_md_free(full);
    ex_md_free(m);
    free(text);
    free(edited);
    return 0;
}
