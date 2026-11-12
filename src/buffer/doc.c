#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"

/* One text change as it was applied, with what it replaced, so it can be undone. */
typedef struct Rec {
    size_t offset;
    char *removed; size_t removed_len;
    char *inserted; size_t inserted_len;
} Rec;

typedef struct Node {
    size_t id, parent;
    size_t redo;                 /* id of the child redo follows; 0 if none (the root is never a child) */
    size_t nkids;
    Rec *recs; size_t nrecs;     /* in the order they were applied */
    ExSel *before; size_t nbefore;
    ExSel *after; size_t nafter;
    int sealed;                  /* typing may no longer extend this step */
    int typing;                  /* a single plain insertion made with coalesce */
} Node;

struct ExDoc {
    ExBuffer *buf;
    ExSel *sel; size_t nsel, capsel;
    size_t primary;
    Node *nodes; size_t nnodes, capnodes;
    size_t cur;
    ExEditFn listener; void *listener_user;
};

/* ---- helpers ---- */

static char *dup_bytes(const char *p, size_t n) {
    char *q = malloc(n ? n : 1);
    if (q && n) memcpy(q, p, n);
    return q;
}

static ExSel *dup_sels(const ExSel *s, size_t n) {
    ExSel *q = malloc((n ? n : 1) * sizeof *q);
    if (q && n) memcpy(q, s, n * sizeof *q);
    return q;
}

static size_t lo_of(ExSel s) { return s.anchor < s.head ? s.anchor : s.head; }
static size_t hi_of(ExSel s) { return s.anchor < s.head ? s.head : s.anchor; }

static int cmp_sel(const void *a, const void *b) {
    size_t x = lo_of(*(const ExSel *)a), y = lo_of(*(const ExSel *)b);
    if (x != y) return x < y ? -1 : 1;
    x = hi_of(*(const ExSel *)a); y = hi_of(*(const ExSel *)b);
    return x < y ? -1 : x > y;
}

/* Sort, clamp and merge selections in place; returns the new count. *primary is
 * an offset used to find the primary selection afterwards. */
static size_t normalize(ExSel *s, size_t n, size_t size, size_t primary_off, size_t *primary_idx) {
    size_t i, out = 0;
    for (i = 0; i < n; i++) {
        if (s[i].anchor > size) s[i].anchor = size;
        if (s[i].head > size) s[i].head = size;
    }
    qsort(s, n, sizeof *s, cmp_sel);
    for (i = 0; i < n; i++) {
        if (out > 0) {
            ExSel *p = &s[out - 1];
            /* overlapping, or two carets at one place: merge (touching ranges stay apart) */
            int caret_same = lo_of(s[i]) == hi_of(s[i]) && lo_of(*p) == hi_of(*p) && lo_of(s[i]) == lo_of(*p);
            int inside = lo_of(s[i]) < hi_of(*p) || (lo_of(s[i]) == hi_of(*p) && (lo_of(s[i]) == hi_of(s[i]) || lo_of(*p) == hi_of(*p)));
            if (caret_same || inside) {
                size_t lo = lo_of(*p), hi = hi_of(*p);
                if (hi_of(s[i]) > hi) hi = hi_of(s[i]);
                if (p->anchor <= p->head) { p->anchor = lo; p->head = hi; } else { p->anchor = hi; p->head = lo; }
                continue;
            }
        }
        s[out++] = s[i];
    }
    if (primary_idx) {
        size_t k;
        *primary_idx = 0;
        for (k = 0; k < out; k++)
            if (primary_off >= lo_of(s[k]) && primary_off <= hi_of(s[k])) { *primary_idx = k; break; }
    }
    return out;
}

static int ensure_sel(ExDoc *d, size_t n) {
    if (n > d->capsel) {
        size_t nc = d->capsel ? d->capsel : 4;
        ExSel *ns;
        while (nc < n) nc *= 2;
        ns = realloc(d->sel, nc * sizeof *ns);
        if (!ns) return -1;
        d->sel = ns;
        d->capsel = nc;
    }
    return 0;
}

static void store_sels(ExDoc *d, const ExSel *s, size_t n, size_t primary_off) {
    if (n == 0) { ExSel c = {0, 0}; s = &c; n = 1; if (ensure_sel(d, 1)) return; d->sel[0] = c; d->nsel = 1; d->primary = 0; return; }
    if (ensure_sel(d, n)) return;
    if (s != d->sel) memmove(d->sel, s, n * sizeof *s);
    d->nsel = normalize(d->sel, n, ex_buffer_size(d->buf), primary_off, &d->primary);
}

/* ---- creation ---- */

ExDoc *ex_doc_create(const char *text, size_t len) {
    ExDoc *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->buf = ex_buffer_create(text, len);
    d->nodes = calloc(8, sizeof *d->nodes);
    d->sel = calloc(4, sizeof *d->sel);
    if (!d->buf || !d->nodes || !d->sel) { ex_doc_free(d); return NULL; }
    d->capnodes = 8;
    d->capsel = 4;
    d->nsel = 1;
    d->nnodes = 1; /* node 0: the original text */
    d->nodes[0].sealed = 1;
    return d;
}

void ex_doc_free(ExDoc *d) {
    size_t i, k;
    if (!d) return;
    for (i = 0; i < d->nnodes; i++) {
        Node *n = &d->nodes[i];
        for (k = 0; k < n->nrecs; k++) { free(n->recs[k].removed); free(n->recs[k].inserted); }
        free(n->recs); free(n->before); free(n->after);
    }
    free(d->nodes);
    free(d->sel);
    ex_buffer_free(d->buf);
    free(d);
}

const ExBuffer *ex_doc_buffer(const ExDoc *d) { return d->buf; }
void ex_doc_set_edit_listener(ExDoc *d, ExEditFn fn, void *user) { d->listener = fn; d->listener_user = user; }

/* ---- selections ---- */

size_t ex_doc_sel_count(const ExDoc *d) { return d->nsel; }
ExSel ex_doc_sel(const ExDoc *d, size_t i) { return d->sel[i < d->nsel ? i : d->nsel - 1]; }
size_t ex_doc_primary(const ExDoc *d) { return d->primary; }

void ex_doc_set_selections(ExDoc *d, const ExSel *sels, size_t n) {
    ExSel *copy;
    size_t poff;
    if (n == 0) { ex_doc_set_caret(d, 0); return; }
    copy = dup_sels(sels, n);
    if (!copy) return;
    poff = copy[n - 1].head; /* the last one given is the primary */
    store_sels(d, copy, n, poff);
    free(copy);
    ex_doc_seal(d);
}

void ex_doc_set_caret(ExDoc *d, size_t offset) {
    ExSel c;
    c.anchor = c.head = offset;
    ex_doc_set_selections(d, &c, 1);
}

void ex_doc_add_cursor(ExDoc *d, size_t offset) {
    ExSel *copy = malloc((d->nsel + 1) * sizeof *copy);
    if (!copy) return;
    memcpy(copy, d->sel, d->nsel * sizeof *copy);
    copy[d->nsel].anchor = copy[d->nsel].head = offset;
    store_sels(d, copy, d->nsel + 1, offset);
    free(copy);
    ex_doc_seal(d);
}

void ex_doc_seal(ExDoc *d) { d->nodes[d->cur].sealed = 1; }

/* ---- applying changes ---- */

static void emit(ExDoc *d, size_t offset, size_t removed, const char *ins, size_t ins_len) {
    if (d->listener) d->listener(d->listener_user, offset, removed, ins, ins_len);
}

static int apply_rec_forward(ExDoc *d, const Rec *r) {
    if (ex_buffer_delete(d->buf, r->offset, r->removed_len)) return -1;
    if (ex_buffer_insert(d->buf, r->offset, r->inserted, r->inserted_len)) return -1;
    emit(d, r->offset, r->removed_len, r->inserted, r->inserted_len);
    return 0;
}

static int apply_rec_backward(ExDoc *d, const Rec *r) {
    if (ex_buffer_delete(d->buf, r->offset, r->inserted_len)) return -1;
    if (ex_buffer_insert(d->buf, r->offset, r->removed, r->removed_len)) return -1;
    emit(d, r->offset, r->inserted_len, r->removed, r->removed_len);
    return 0;
}

static Node *add_node(ExDoc *d) {
    if (d->nnodes == d->capnodes) {
        Node *nn = realloc(d->nodes, d->capnodes * 2 * sizeof *nn);
        if (!nn) return NULL;
        memset(nn + d->capnodes, 0, d->capnodes * sizeof *nn);
        d->nodes = nn;
        d->capnodes *= 2;
    }
    return &d->nodes[d->nnodes++];
}

int ex_doc_apply(ExDoc *d, const ExChange *ch, size_t n, int coalesce, const ExSel *sel_after, size_t nsel_after) {
    size_t i, size = ex_buffer_size(d->buf), prev_end = 0;
    Rec *recs;
    Node *cur = &d->nodes[d->cur], *node;
    ExSel *after = NULL, *before;
    size_t nafter, delta = 0, poff = 0, applied = 0;

    if (n == 0) return 0;
    for (i = 0; i < n; i++) {
        if (ch[i].offset < prev_end || ch[i].offset > size || ch[i].remove_len > size - ch[i].offset) return -1;
        prev_end = ch[i].offset + ch[i].remove_len;
    }

    /* selections after the edit */
    if (sel_after) {
        after = dup_sels(sel_after, nsel_after);
        nafter = nsel_after;
    } else {
        after = malloc(n * sizeof *after);
        nafter = n;
        for (i = 0; after && i < n; i++) {
            size_t pos = ch[i].offset + delta + ch[i].text_len;
            after[i].anchor = after[i].head = pos;
            delta += ch[i].text_len - ch[i].remove_len; /* size_t wraps correctly for shrinking */
        }
    }
    if (!after) return -1;

    /* extend the previous typing step if this continues it */
    if (coalesce && n == 1 && ch[0].remove_len == 0 && ch[0].text_len > 0 && !cur->sealed && cur->typing &&
        d->cur != 0 && cur->nkids == 0 && cur->nrecs == 1 &&
        cur->recs[0].offset + cur->recs[0].inserted_len == ch[0].offset) {
        Rec *r = &cur->recs[0];
        char *grown = realloc(r->inserted, r->inserted_len + ch[0].text_len);
        ExSel *na;
        if (!grown) { free(after); return -1; }
        r->inserted = grown;
        if (ex_buffer_insert(d->buf, ch[0].offset, ch[0].text, ch[0].text_len)) { free(after); return -1; }
        memcpy(r->inserted + r->inserted_len, ch[0].text, ch[0].text_len);
        r->inserted_len += ch[0].text_len;
        emit(d, ch[0].offset, 0, ch[0].text, ch[0].text_len);
        na = after;
        free(cur->after);
        cur->after = na;
        cur->nafter = nafter;
        poff = after[nafter - 1].head;
        store_sels(d, after, nafter, poff);
        return 0;
    }

    recs = calloc(n, sizeof *recs);
    before = dup_sels(d->sel, d->nsel);
    node = recs && before ? add_node(d) : NULL;
    if (!node) { free(recs); free(before); free(after); return -1; }

    /* apply back to front so earlier offsets stay valid */
    for (i = n; i-- > 0;) {
        Rec *r = &recs[applied];
        r->offset = ch[i].offset;
        r->removed_len = ch[i].remove_len;
        r->removed = malloc(r->removed_len ? r->removed_len : 1);
        r->inserted_len = ch[i].text_len;
        r->inserted = dup_bytes(ch[i].text, ch[i].text_len);
        if (!r->removed || !r->inserted) goto fail;
        ex_buffer_read(d->buf, r->offset, r->removed, r->removed_len);
        if (apply_rec_forward(d, r)) goto fail;
        applied++;
    }

    memset(node, 0, sizeof *node);
    node->id = d->nnodes - 1;
    node->parent = d->cur;
    node->recs = recs;
    node->nrecs = n;
    node->before = before;
    node->nbefore = d->nsel;
    node->after = after;
    node->nafter = nafter;
    node->typing = coalesce && n == 1 && ch[0].remove_len == 0;
    cur = &d->nodes[d->cur]; /* the array may have moved */
    cur->nkids++;
    cur->redo = node->id;
    d->cur = node->id;
    poff = after[nafter - 1].head;
    store_sels(d, after, nafter, poff);
    return 0;
fail:
    /* the text must never be left half edited: undo what was applied, newest first */
    while (applied > 0) {
        applied--;
        (void)apply_rec_backward(d, &recs[applied]);
    }
    for (i = 0; i < n; i++) { free(recs[i].removed); free(recs[i].inserted); }
    free(recs); free(before); free(after);
    d->nnodes--;
    return -1;
}

int ex_doc_replace_selections(ExDoc *d, const char *text, size_t len, int coalesce) {
    ExChange *ch = malloc(d->nsel * sizeof *ch);
    size_t i;
    int rc;
    if (!ch) return -1;
    for (i = 0; i < d->nsel; i++) {
        ch[i].offset = lo_of(d->sel[i]);
        ch[i].remove_len = hi_of(d->sel[i]) - lo_of(d->sel[i]);
        ch[i].text = text;
        ch[i].text_len = len;
    }
    rc = ex_doc_apply(d, ch, d->nsel, coalesce, NULL, 0);
    free(ch);
    return rc;
}

/* ---- undo tree ---- */

size_t ex_doc_node_count(const ExDoc *d) { return d->nnodes; }
size_t ex_doc_current_node(const ExDoc *d) { return d->cur; }
size_t ex_doc_node_parent(const ExDoc *d, size_t node) { return node < d->nnodes ? d->nodes[node].parent : 0; }

int ex_doc_undo(ExDoc *d) {
    Node *n = &d->nodes[d->cur];
    size_t i;
    if (d->cur == 0) return 0;
    for (i = n->nrecs; i-- > 0;)
        if (apply_rec_backward(d, &n->recs[i])) return -1;
    d->nodes[n->parent].redo = n->id;
    d->cur = n->parent;
    store_sels(d, n->before, n->nbefore, n->before[n->nbefore - 1].head);
    return 1;
}

int ex_doc_redo(ExDoc *d) {
    Node *p = &d->nodes[d->cur], *n;
    size_t i;
    if (p->nkids == 0 || p->redo == 0) return 0;
    n = &d->nodes[p->redo];
    for (i = 0; i < n->nrecs; i++)
        if (apply_rec_forward(d, &n->recs[i])) return -1;
    d->cur = n->id;
    store_sels(d, n->after, n->nafter, n->after[n->nafter - 1].head);
    return 1;
}

int ex_doc_goto_node(ExDoc *d, size_t node) {
    size_t a, depth_target = 0, k;
    size_t *path;
    if (node >= d->nnodes) return -1;
    /* mark the ancestors of the target, then undo until the current node is one of them */
    path = malloc((d->nnodes + 1) * sizeof *path);
    if (!path) return -1;
    for (a = node;; a = d->nodes[a].parent) {
        path[depth_target++] = a;
        if (a == 0) break;
    }
    for (;;) {
        int on_path = 0;
        for (k = 0; k < depth_target; k++) if (path[k] == d->cur) { on_path = 1; break; }
        if (on_path) break;
        if (ex_doc_undo(d) < 0) { free(path); return -1; }
    }
    /* redo down the path from the current node to the target */
    for (k = 0; k < depth_target && path[k] != d->cur; k++) {}
    while (k > 0) {
        d->nodes[d->cur].redo = path[k - 1];
        if (ex_doc_redo(d) < 0) { free(path); return -1; }
        k--;
    }
    free(path);
    return 0;
}
