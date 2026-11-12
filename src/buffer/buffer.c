/* Piece table kept in a treap: every node is a piece (a span of the original text
 * or of the add buffer) and carries the byte and newline totals of its subtree,
 * so offset, line and edit lookups are O(log n). Newlines inside a span are
 * counted by bisecting sorted newline positions kept for each source buffer. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"

typedef struct Node {
    struct Node *l, *r;
    uint32_t prio;
    unsigned char src;       /* 0 original, 1 add buffer */
    size_t start, len;       /* span within the source */
    size_t nl;               /* newlines in the span */
    size_t size, nls, count; /* subtree: bytes, newlines, nodes */
} Node;

typedef struct Source {
    char *data;
    size_t len, cap;
    size_t *nl;              /* positions of '\n', ascending */
    size_t nnl, capnl;
} Source;

struct ExBuffer {
    Source src[2];
    Node *root;
    uint64_t rng;
};

static uint32_t next_prio(ExBuffer *b) {
    b->rng ^= b->rng << 13; b->rng ^= b->rng >> 7; b->rng ^= b->rng << 17; /* xorshift64 */
    return (uint32_t)(b->rng >> 16);
}

/* newlines at positions in [a, b) of a source */
static size_t count_nl(const Source *s, size_t a, size_t b) {
    size_t lo = 0, hi = s->nnl, x, y;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (s->nl[m] < a) lo = m + 1; else hi = m; }
    x = lo;
    hi = s->nnl;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (s->nl[m] < b) lo = m + 1; else hi = m; }
    y = lo;
    return y - x;
}

/* index into src->nl of the first newline at position >= a */
static size_t nl_index(const Source *s, size_t a) {
    size_t lo = 0, hi = s->nnl;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (s->nl[m] < a) lo = m + 1; else hi = m; }
    return lo;
}

static int src_append(Source *s, const char *text, size_t len) {
    size_t i;
    if (s->len + len > s->cap) {
        size_t nc = s->cap ? s->cap : 256;
        char *nd;
        while (nc < s->len + len) nc *= 2;
        nd = realloc(s->data, nc);
        if (!nd) return -1;
        s->data = nd;
        s->cap = nc;
    }
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (s->nnl == s->capnl) {
                size_t nc = s->capnl ? s->capnl * 2 : 64;
                size_t *nn = realloc(s->nl, nc * sizeof *nn);
                if (!nn) return -1;
                s->nl = nn;
                s->capnl = nc;
            }
            s->nl[s->nnl++] = s->len + i;
        }
    }
    if (len) memcpy(s->data + s->len, text, len);
    s->len += len;
    return 0;
}

static void pull(Node *n) {
    n->size = n->len + (n->l ? n->l->size : 0) + (n->r ? n->r->size : 0);
    n->nls = n->nl + (n->l ? n->l->nls : 0) + (n->r ? n->r->nls : 0);
    n->count = 1 + (n->l ? n->l->count : 0) + (n->r ? n->r->count : 0);
}

static Node *new_node(ExBuffer *b, int src, size_t start, size_t len) {
    Node *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->prio = next_prio(b);
    n->src = (unsigned char)src;
    n->start = start;
    n->len = len;
    n->nl = count_nl(&b->src[src], start, start + len);
    pull(n);
    return n;
}

static void free_tree(Node *n) {
    while (n) {
        Node *r = n->r;
        free_tree(n->l);
        free(n);
        n = r;
    }
}

static Node *merge(Node *a, Node *b) {
    if (!a) return b;
    if (!b) return a;
    if (a->prio > b->prio) { a->r = merge(a->r, b); pull(a); return a; }
    b->l = merge(a, b->l);
    pull(b);
    return b;
}

/* Splits t into the first k bytes and the rest. A piece that straddles k is cut in two. */
static int split(ExBuffer *bf, Node *t, size_t k, Node **L, Node **R) {
    size_t lsz;
    if (!t) { *L = *R = NULL; return 0; }
    lsz = t->l ? t->l->size : 0;
    if (k <= lsz) {
        Node *a, *b;
        if (split(bf, t->l, k, &a, &b)) return -1;
        t->l = b;
        pull(t);
        *L = a;
        *R = t;
        return 0;
    }
    if (k >= lsz + t->len) {
        Node *a, *b;
        if (split(bf, t->r, k - lsz - t->len, &a, &b)) return -1;
        t->r = a;
        pull(t);
        *L = t;
        *R = b;
        return 0;
    }
    {   /* k falls inside this piece: it keeps the head; a new node takes the tail */
        size_t cut = k - lsz;
        Node *tail = new_node(bf, t->src, t->start + cut, t->len - cut);
        Node *rr = t->r;
        if (!tail) return -1;
        t->len = cut;
        t->nl = count_nl(&bf->src[t->src], t->start, t->start + cut);
        t->r = NULL;
        pull(t);
        tail->r = rr;
        pull(tail);
        /* the tail has a random priority; restore the heap order by merging */
        {
            Node *right = tail->r;
            tail->r = NULL;
            pull(tail);
            *L = t;
            *R = merge(tail, right);
        }
        return 0;
    }
}

ExBuffer *ex_buffer_create(const char *text, size_t len) {
    ExBuffer *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->rng = 0x9E3779B97F4A7C15ull;
    if (len && src_append(&b->src[0], text, len)) { ex_buffer_free(b); return NULL; }
    if (len) {
        b->root = new_node(b, 0, 0, len);
        if (!b->root) { ex_buffer_free(b); return NULL; }
    }
    return b;
}

void ex_buffer_free(ExBuffer *b) {
    if (!b) return;
    free_tree(b->root);
    free(b->src[0].data); free(b->src[0].nl);
    free(b->src[1].data); free(b->src[1].nl);
    free(b);
}

size_t ex_buffer_size(const ExBuffer *b) { return b->root ? b->root->size : 0; }
size_t ex_buffer_line_count(const ExBuffer *b) { return 1 + (b->root ? b->root->nls : 0); }
size_t ex_buffer_piece_count(const ExBuffer *b) { return b->root ? b->root->count : 0; }

static size_t read_rec(const ExBuffer *b, const Node *t, size_t off, char *out, size_t n) {
    size_t done = 0, lsz;
    if (!t || n == 0) return 0;
    lsz = t->l ? t->l->size : 0;
    if (off < lsz) {
        done = read_rec(b, t->l, off, out, n);
        n -= done;
        off = 0;
    } else {
        off -= lsz;
    }
    if (n == 0) return done;
    if (off < t->len) {
        size_t take = t->len - off;
        if (take > n) take = n;
        memcpy(out + done, b->src[t->src].data + t->start + off, take);
        done += take;
        n -= take;
        off = 0;
    } else {
        off -= t->len;
    }
    if (n == 0) return done;
    return done + read_rec(b, t->r, off, out + done, n);
}

size_t ex_buffer_read(const ExBuffer *b, size_t offset, char *out, size_t n) {
    size_t size = ex_buffer_size(b);
    if (offset >= size) return 0;
    if (n > size - offset) n = size - offset;
    return read_rec(b, b->root, offset, out, n);
}

char *ex_buffer_text(const ExBuffer *b, size_t *len) {
    size_t n = ex_buffer_size(b);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    n = ex_buffer_read(b, 0, out, n);
    out[n] = 0;
    if (len) *len = n;
    return out;
}

int ex_buffer_insert(ExBuffer *b, size_t offset, const char *text, size_t len) {
    Node *L, *R, *mid;
    size_t start;
    if (offset > ex_buffer_size(b)) return -1;
    if (len == 0) return 0;
    start = b->src[1].len;
    if (src_append(&b->src[1], text, len)) return -1;
    mid = new_node(b, 1, start, len);
    if (!mid) return -1;
    if (split(b, b->root, offset, &L, &R)) { free(mid); return -1; }
    b->root = merge(merge(L, mid), R);
    return 0;
}

int ex_buffer_delete(ExBuffer *b, size_t offset, size_t len) {
    Node *L, *M, *R;
    if (offset > ex_buffer_size(b) || len > ex_buffer_size(b) - offset) return -1;
    if (len == 0) return 0;
    if (split(b, b->root, offset, &L, &R)) return -1;
    if (split(b, R, len, &M, &R)) { b->root = merge(L, R); return -1; }
    free_tree(M);
    b->root = merge(L, R);
    return 0;
}

size_t ex_buffer_line_start(const ExBuffer *b, size_t line) {
    const Node *t = b->root;
    size_t base = 0, need = line; /* position after the need-th newline */
    if (line == 0) return 0;
    if (!b->root || line > b->root->nls) return ex_buffer_size(b);
    while (t) {
        size_t lnls = t->l ? t->l->nls : 0, lsz = t->l ? t->l->size : 0;
        if (need <= lnls) { t = t->l; continue; }
        need -= lnls;
        base += lsz;
        if (need <= t->nl) {
            const Source *s = &b->src[t->src];
            size_t idx = nl_index(s, t->start) + need - 1;
            return base + (s->nl[idx] - t->start) + 1;
        }
        need -= t->nl;
        base += t->len;
        t = t->r;
    }
    return ex_buffer_size(b);
}

size_t ex_buffer_line_of_offset(const ExBuffer *b, size_t offset) {
    const Node *t = b->root;
    size_t lines = 0;
    while (t) {
        size_t lsz = t->l ? t->l->size : 0;
        if (offset < lsz) { t = t->l; continue; }
        if (t->l) lines += t->l->nls;
        offset -= lsz;
        if (offset < t->len) {
            return lines + count_nl(&b->src[t->src], t->start, t->start + offset);
        }
        lines += t->nl;
        offset -= t->len;
        t = t->r;
    }
    return lines;
}
