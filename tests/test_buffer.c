/* buffer: the piece table against a plain array over 100,000 random operations
 * (fixed seed), then the document: selections, multiple cursors, undo and redo,
 * the undo tree against full-text snapshots. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint64_t rng_state = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)((rng_state >> 11) % n);
}

static const char *PIECES[] = {"a", "b", "\n", "hello", " ", "\xC3\xA9", "\xE4\xBD\xA0", "\xF0\x9F\x98\x80", "line one\nline two\n", "x\ny", ""};
#define NPIECES (sizeof PIECES / sizeof PIECES[0])

static size_t random_text(char *out, size_t max) {
    size_t n = 0, parts = 1 + rnd(4);
    while (parts--) {
        const char *p = PIECES[rnd(NPIECES)];
        size_t l = strlen(p);
        if (n + l > max) break;
        memcpy(out + n, p, l);
        n += l;
    }
    return n;
}

/* ---- reference model for the raw buffer ---- */

typedef struct Ref { char *t; size_t n, cap; } Ref;

static void ref_insert(Ref *r, size_t off, const char *s, size_t len) {
    if (r->n + len > r->cap) { r->cap = (r->n + len) * 2 + 16; r->t = realloc(r->t, r->cap); }
    memmove(r->t + off + len, r->t + off, r->n - off);
    memcpy(r->t + off, s, len);
    r->n += len;
}
static void ref_delete(Ref *r, size_t off, size_t len) {
    memmove(r->t + off, r->t + off + len, r->n - off - len);
    r->n -= len;
}
static size_t ref_line_start(const Ref *r, size_t line) {
    size_t i, l = 0;
    if (line == 0) return 0;
    for (i = 0; i < r->n; i++)
        if (r->t[i] == '\n' && ++l == line) return i + 1;
    return r->n;
}
static size_t ref_line_of(const Ref *r, size_t off) {
    size_t i, l = 0;
    for (i = 0; i < off && i < r->n; i++) if (r->t[i] == '\n') l++;
    return l;
}
static size_t ref_lines(const Ref *r) { return ref_line_of(r, r->n) + 1; }

static int same(const ExBuffer *b, const Ref *r) {
    size_t n = 0;
    char *t = ex_buffer_text(b, &n);
    int ok = t && n == r->n && (n == 0 || memcmp(t, r->t, n) == 0);
    free(t);
    return ok && ex_buffer_size(b) == r->n;
}

static void test_raw_buffer(void) {
    ExBuffer *b = ex_buffer_create("first\nsecond\nthird", 18);
    Ref ref = {0};
    int op;
    char tmp[256], out[256];
    size_t peak = 0;

    ref_insert(&ref, 0, "first\nsecond\nthird", 18);
    CHECK(same(b, &ref) && ex_buffer_line_count(b) == 3);
    CHECK(ex_buffer_line_start(b, 0) == 0 && ex_buffer_line_start(b, 1) == 6 && ex_buffer_line_start(b, 2) == 13);
    CHECK(ex_buffer_line_start(b, 3) == 18 && ex_buffer_line_of_offset(b, 12) == 1 && ex_buffer_line_of_offset(b, 13) == 2);
    CHECK(ex_buffer_insert(b, 99, "x", 1) == -1 && ex_buffer_delete(b, 10, 99) == -1 && ex_buffer_size(b) == 18);
    CHECK(ex_buffer_insert(b, 0, "", 0) == 0 && ex_buffer_delete(b, 5, 0) == 0 && ex_buffer_size(b) == 18);

    for (op = 0; op < 100000; op++) {
        uint32_t kind = rnd(100);
        if (kind < 52 || (ref.n < 10 && kind < 80)) {
            size_t len = random_text(tmp, 80), off = rnd((uint32_t)ref.n + 1);
            if (ref.n > 40000) continue;
            CHECK(ex_buffer_insert(b, off, tmp, len) == 0);
            ref_insert(&ref, off, tmp, len);
        } else if (kind < 80) {
            size_t off = rnd((uint32_t)ref.n + 1), len = rnd(25);
            if (off + len > ref.n) len = ref.n - off;
            CHECK(ex_buffer_delete(b, off, len) == 0);
            ref_delete(&ref, off, len);
        } else if (kind < 90) {
            size_t off = rnd((uint32_t)ref.n + 4), len = rnd(200), got = ex_buffer_read(b, off, out, len);
            size_t want = off >= ref.n ? 0 : (len > ref.n - off ? ref.n - off : len);
            CHECK(got == want && (got == 0 || memcmp(out, ref.t + off, got) == 0));
        } else {
            size_t line = rnd((uint32_t)ref_lines(&ref) + 2), off = rnd((uint32_t)ref.n + 1);
            if (op % 10 == 0) { /* these references are linear scans, so only check some of the time */
                CHECK(ex_buffer_line_start(b, line) == ref_line_start(&ref, line));
                CHECK(ex_buffer_line_of_offset(b, off) == ref_line_of(&ref, off));
                CHECK(ex_buffer_line_count(b) == ref_lines(&ref));
            }
        }
        if (ref.n > peak) peak = ref.n;
        if (op % 2500 == 0) CHECK(same(b, &ref));
    }
    CHECK(same(b, &ref));
    fprintf(stderr, "raw buffer: %zu bytes (peak %zu) in %zu pieces after 100000 operations\n", ref.n, peak, ex_buffer_piece_count(b));
    CHECK(peak > 5000);
    free(ref.t);
    ex_buffer_free(b);
}

/* ---- document ---- */

static char *doc_text(const ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }
static int doc_is(const ExDoc *d, const char *want) { char *t = doc_text(d); int ok = t && strcmp(t, want) == 0; free(t); return ok; }

static void test_selections(void) {
    ExDoc *d = ex_doc_create("hello world", 11);
    ExSel s[4];
    ExSel got;

    CHECK(ex_doc_sel_count(d) == 1 && ex_doc_sel(d, 0).anchor == 0 && ex_doc_sel(d, 0).head == 0);
    /* sorted, clamped, merged */
    s[0].anchor = 8; s[0].head = 8;
    s[1].anchor = 2; s[1].head = 2;
    s[2].anchor = 2; s[2].head = 2;      /* the same caret again */
    s[3].anchor = 50; s[3].head = 50;    /* beyond the end */
    ex_doc_set_selections(d, s, 4);
    CHECK(ex_doc_sel_count(d) == 3);
    CHECK(ex_doc_sel(d, 0).head == 2 && ex_doc_sel(d, 1).head == 8 && ex_doc_sel(d, 2).head == 11);
    /* overlapping ranges merge; direction of the first is kept */
    s[0].anchor = 6; s[0].head = 2;      /* backwards 2..6 */
    s[1].anchor = 4; s[1].head = 9;
    ex_doc_set_selections(d, s, 2);
    CHECK(ex_doc_sel_count(d) == 1);
    got = ex_doc_sel(d, 0);
    CHECK(got.anchor == 9 && got.head == 2);
    /* touching ranges stay separate; a caret on the edge of a range joins it */
    s[0].anchor = 0; s[0].head = 3;
    s[1].anchor = 3; s[1].head = 6;
    ex_doc_set_selections(d, s, 2);
    CHECK(ex_doc_sel_count(d) == 2);
    s[1].anchor = s[1].head = 3;
    ex_doc_set_selections(d, s, 2);
    CHECK(ex_doc_sel_count(d) == 1);
    ex_doc_set_caret(d, 5);
    ex_doc_add_cursor(d, 0);
    ex_doc_add_cursor(d, 11);
    CHECK(ex_doc_sel_count(d) == 3 && ex_doc_sel(d, ex_doc_primary(d)).head == 11);
    ex_doc_free(d);
}

static void test_editing(void) {
    ExDoc *d = ex_doc_create("one two three", 13);
    ExSel s[3];
    ExChange bad[2];

    /* typing replaces the selection; carets follow */
    s[0].anchor = 0; s[0].head = 3;
    ex_doc_set_selections(d, s, 1);
    CHECK(ex_doc_replace_selections(d, "1", 1, 0) == 0 && doc_is(d, "1 two three"));
    CHECK(ex_doc_sel(d, 0).head == 1 && ex_doc_sel(d, 0).anchor == 1);
    CHECK(ex_doc_undo(d) == 1 && doc_is(d, "one two three"));
    CHECK(ex_doc_sel(d, 0).anchor == 0 && ex_doc_sel(d, 0).head == 3); /* the selection comes back */
    CHECK(ex_doc_redo(d) == 1 && doc_is(d, "1 two three") && ex_doc_redo(d) == 0);

    /* several cursors type at once and undo as one step */
    ex_doc_set_caret(d, 0);
    ex_doc_add_cursor(d, 2);
    ex_doc_add_cursor(d, 6);
    CHECK(ex_doc_sel_count(d) == 3);
    CHECK(ex_doc_replace_selections(d, "-", 1, 0) == 0 && doc_is(d, "-1 -two -three"));
    CHECK(ex_doc_sel(d, 0).head == 1 && ex_doc_sel(d, 1).head == 4 && ex_doc_sel(d, 2).head == 9);
    CHECK(ex_doc_undo(d) == 1 && doc_is(d, "1 two three") && ex_doc_sel_count(d) == 3);
    CHECK(ex_doc_redo(d) == 1 && doc_is(d, "-1 -two -three"));
    /* deleting with several selections */
    s[0].anchor = 0; s[0].head = 1;
    s[1].anchor = 3; s[1].head = 4;
    s[2].anchor = 8; s[2].head = 9;
    ex_doc_set_selections(d, s, 3);
    CHECK(ex_doc_replace_selections(d, "", 0, 0) == 0 && doc_is(d, "1 two three"));

    /* changes that are out of order, overlapping or out of range are refused and change nothing */
    bad[0].offset = 4; bad[0].remove_len = 0; bad[0].text = "x"; bad[0].text_len = 1;
    bad[1].offset = 2; bad[1].remove_len = 0; bad[1].text = "y"; bad[1].text_len = 1;
    {
        char *before = doc_text(d);
        size_t nodes = ex_doc_node_count(d);
        CHECK(ex_doc_apply(d, bad, 2, 0, NULL, 0) == -1);
        bad[0].offset = 1; bad[0].remove_len = 5; bad[1].offset = 3;
        CHECK(ex_doc_apply(d, bad, 2, 0, NULL, 0) == -1);
        bad[0].offset = 999; bad[0].remove_len = 0;
        CHECK(ex_doc_apply(d, bad, 1, 0, NULL, 0) == -1);
        CHECK(doc_is(d, before) && ex_doc_node_count(d) == nodes);
        free(before);
    }
    ex_doc_free(d);
}

static void test_typing_coalesce(void) {
    ExDoc *d = ex_doc_create("", 0);
    const char *word = "hello";
    int i;
    for (i = 0; i < 5; i++) CHECK(ex_doc_replace_selections(d, word + i, 1, 1) == 0);
    CHECK(doc_is(d, "hello") && ex_doc_node_count(d) == 2);   /* one undo step for the whole word */
    ex_doc_seal(d);                                          /* the cursor moved */
    CHECK(ex_doc_replace_selections(d, "!", 1, 1) == 0 && ex_doc_node_count(d) == 3);
    CHECK(ex_doc_undo(d) == 1 && doc_is(d, "hello"));
    CHECK(ex_doc_undo(d) == 1 && doc_is(d, "") && ex_doc_undo(d) == 0);
    CHECK(ex_doc_redo(d) == 1 && doc_is(d, "hello"));
    /* a caret jump between keystrokes starts a new step even without sealing */
    ex_doc_set_caret(d, 0);
    CHECK(ex_doc_replace_selections(d, ">", 1, 1) == 0 && ex_doc_replace_selections(d, ">", 1, 1) == 0);
    CHECK(doc_is(d, ">>hello") && ex_doc_undo(d) == 1 && doc_is(d, "hello"));
    ex_doc_free(d);
}

/* The undo tree against full-text snapshots of every node. */
static void test_undo_tree(void) {
    enum { MAXN = 6000 };
    ExDoc *d = ex_doc_create("start\n", 6);
    char **snap = calloc(MAXN, sizeof *snap);
    size_t *redo = calloc(MAXN, sizeof *redo), *parent = calloc(MAXN, sizeof *parent);
    size_t cur = 0, nodes = 1, i;
    int op;
    snap[0] = malloc(7);
    memcpy(snap[0], "start\n", 7);

    for (op = 0; op < 20000; op++) {
        uint32_t kind = rnd(100);
        if (kind < 55 && nodes < MAXN) {
            char t[64];
            size_t len = random_text(t, 20), sz = ex_buffer_size(ex_doc_buffer(d)), k, nsel = 1 + rnd(3);
            ExSel s[3];
            char *before;
            if (sz > 1500) len = 0;
            for (k = 0; k < nsel; k++) {
                size_t a = rnd((uint32_t)sz + 1), b = rnd(4) ? a : (a + rnd(5) > sz ? sz : a + rnd(5));
                s[k].anchor = a; s[k].head = b;
            }
            ex_doc_set_selections(d, s, nsel);
            before = doc_text(d);
            if (ex_doc_replace_selections(d, t, len, 0) == 0) {
                char *now = doc_text(d);
                if (ex_doc_node_count(d) == nodes + 1) {       /* a no-op edit still makes a node */
                    snap[nodes] = now;
                    parent[nodes] = cur;
                    redo[cur] = nodes;
                    cur = nodes++;
                } else free(now);
            }
            free(before);
        } else if (kind < 75) {
            int moved = ex_doc_undo(d);
            CHECK(moved == (cur != 0));
            if (cur != 0) { redo[parent[cur]] = cur; cur = parent[cur]; }
        } else if (kind < 90) {
            int moved = ex_doc_redo(d);
            CHECK(moved == (redo[cur] != 0));
            if (redo[cur] != 0) cur = redo[cur];
        } else {
            size_t target = rnd((uint32_t)nodes);
            /* the model: undo to the common ancestor, redo down, setting redo pointers on the way */
            size_t path[MAXN], np = 0, a = target;
            for (;; a = parent[a]) { path[np++] = a; if (a == 0) break; }
            for (;;) {
                size_t k;
                int on = 0;
                for (k = 0; k < np; k++) if (path[k] == cur) { on = 1; break; }
                if (on) break;
                redo[parent[cur]] = cur;
                cur = parent[cur];
            }
            {
                size_t k = 0;
                while (path[k] != cur) k++;
                while (k > 0) { redo[cur] = path[k - 1]; cur = path[k - 1]; k--; }
            }
            CHECK(ex_doc_goto_node(d, target) == 0);
        }
        CHECK(ex_doc_current_node(d) == cur);
        if (op % 97 == 0) CHECK(doc_is(d, snap[cur]));
        CHECK(ex_doc_node_count(d) == nodes);
    }
    CHECK(doc_is(d, snap[cur]));
    for (i = 0; i < nodes; i++) CHECK(ex_doc_node_parent(d, i) == (i ? parent[i] : 0));
    fprintf(stderr, "undo tree: %zu nodes\n", nodes);
    for (i = 0; i < nodes; i++) free(snap[i]);
    free(snap); free(redo); free(parent);
    ex_doc_free(d);
}

int main(void) {
    test_raw_buffer();
    test_selections();
    test_editing();
    test_typing_coalesce();
    test_undo_tree();
    return failures ? 1 : 0;
}
