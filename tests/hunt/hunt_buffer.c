/* hunt_buffer: the piece table and the document (selections, undo tree, edit stream) against plain-array models.
 *
 * "Typing is sacred" (AGENTS.md): a lost or reordered byte here is the worst bug the program can have. This test
 *   A. runs the raw buffer against a flat array through random inserts and deletes, including every boundary
 *      (offset 0, offset == size, empty text, range ends that overflow a size_t, NUL bytes) and checks the
 *      line index (line_count, line_start, line_of_offset) and ex_buffer_read after every step;
 *   B. runs the document against snapshots: after any mix of typing, multi-cursor edits, undo, redo, jumps through
 *      the undo tree, bad edits and selection changes, the text must equal the model, the selections must stay
 *      sorted and inside the text, the edit listener's stream replayed on a copy must reproduce the text, and
 *      ex_doc_edits_since must agree with the size change;
 *   C. checks the documented typing-coalescing and undo-branch rules;
 *   D. one large text, to catch size arithmetic that only fails past 2^16 or 2^31 lines of work.
 * Link: exhaust_buffer. */
#include "hunt.h"

#include "exhaust_buffer.h"

/* ---- the model: a flat array ---- */
typedef struct Model { char *t; size_t n, cap; } Model;

static void m_reserve(Model *m, size_t need) {
    if (need > m->cap) { m->cap = need * 2 + 64; m->t = (char *)realloc(m->t, m->cap); if (!m->t) exit(2); }
}
static void m_set(Model *m, const char *s, size_t n) { m_reserve(m, n); if (n) memcpy(m->t, s, n); m->n = n; }
static void m_insert(Model *m, size_t at, const char *s, size_t n) {
    m_reserve(m, m->n + n);
    memmove(m->t + at + n, m->t + at, m->n - at);
    if (n) memcpy(m->t + at, s, n);
    m->n += n;
}
static void m_delete(Model *m, size_t at, size_t n) { memmove(m->t + at, m->t + at + n, m->n - at - n); m->n -= n; }
static size_t m_lines(const Model *m) { size_t i, l = 1; for (i = 0; i < m->n; i++) if (m->t[i] == '\n') l++; return l; }
static size_t m_line_start(const Model *m, size_t line) {
    size_t i, l = 0;
    if (line == 0) return 0;
    for (i = 0; i < m->n; i++) if (m->t[i] == '\n' && ++l == line) return i + 1;
    return m->n;
}
static size_t m_line_of(const Model *m, size_t off) { size_t i, l = 0; for (i = 0; i < off && i < m->n; i++) if (m->t[i] == '\n') l++; return l; }

/* The whole buffer compared with the model, and the line index on a sample of lines. */
static void compare_buffer(const ExBuffer *b, const Model *m, int full_lines) {
    size_t n = 0, lines, i;
    char *t = ex_buffer_text(b, &n);
    HUNT_CHECK(t != NULL);
    HUNT_EQ_INT(ex_buffer_size(b), m->n);
    if (t) {
        HUNT_EQ_BYTES(t, n, m->t, m->n);
        HUNT_CHECK(t[n] == 0);              /* "NUL terminated" */
    }
    free(t);
    lines = m_lines(m);
    HUNT_EQ_INT(ex_buffer_line_count(b), lines);
    if (full_lines) {
        for (i = 0; i <= lines; i++) HUNT_CHECKF(ex_buffer_line_start(b, i) == m_line_start(m, i), "line %lu", (unsigned long)i);
        for (i = 0; i <= m->n; i++) HUNT_CHECKF(ex_buffer_line_of_offset(b, i) == m_line_of(m, i), "offset %lu", (unsigned long)i);
    } else {
        int k;
        for (k = 0; k < 6; k++) {
            size_t l = hunt_rnd_size(lines + 1), o = hunt_rnd_size(m->n + 1);
            HUNT_CHECKF(ex_buffer_line_start(b, l) == m_line_start(m, l), "line %lu", (unsigned long)l);
            HUNT_CHECKF(ex_buffer_line_of_offset(b, o) == m_line_of(m, o), "offset %lu", (unsigned long)o);
        }
    }
}

/* ex_buffer_read at random windows, including ones that run off the end. */
static void compare_reads(const ExBuffer *b, const Model *m) {
    int k;
    for (k = 0; k < 8; k++) {
        size_t off = hunt_rnd_size(m->n + 3), want = hunt_rnd_size(40);
        char out[64], guard = 0x5A;
        size_t got, expect;
        memset(out, guard, sizeof out);
        got = ex_buffer_read(b, off, out, want);
        expect = off >= m->n ? 0 : (want < m->n - off ? want : m->n - off);
        HUNT_CHECKF(got == expect, "read(off=%lu, n=%lu) returned %lu, want %lu (size %lu)", (unsigned long)off, (unsigned long)want, (unsigned long)got, (unsigned long)expect, (unsigned long)m->n);
        if (got == expect && got) HUNT_CHECK(memcmp(out, m->t + off, got) == 0);
        HUNT_CHECK(out[got < sizeof out ? got : sizeof out - 1] == guard || got >= sizeof out - 1);   /* nothing written past what was returned */
    }
}

static const char *const NASTY[] = { "", "a", "\n", "\n\n\n", "\0\0", "x\0y", "\xC3\xA9", "\xF0\x9F\x98\x80", "line one\nline two\n", "\r\n", "0123456789abcdefghijklmnopqrstuvwxyz" };

static void test_raw_buffer(void) {
    ExBuffer *b = ex_buffer_create(NULL, 0);
    Model m = {0};
    long ops = hunt_iters(30000), i;
    char *probe;
    size_t big;

    hunt_step("raw buffer: empty buffer");
    HUNT_CHECK(b != NULL);
    compare_buffer(b, &m, 1);
    HUNT_EQ_INT(ex_buffer_delete(b, 0, 0), 0);
    HUNT_EQ_INT(ex_buffer_insert(b, 0, "", 0), 0);
    HUNT_CHECK(ex_buffer_insert(b, 1, "x", 1) != 0);          /* past the end */
    HUNT_CHECK(ex_buffer_delete(b, 0, 1) != 0);
    HUNT_CHECK(ex_buffer_delete(b, 1, 0) != 0 || ex_buffer_size(b) == 0);
    compare_buffer(b, &m, 1);
    ex_buffer_free(b);

    hunt_step("raw buffer: create with a length shorter than the string, and with NUL bytes");
    b = ex_buffer_create("abc\0def", 7);
    HUNT_EQ_INT(ex_buffer_size(b), 7);
    probe = ex_buffer_text(b, &big);
    HUNT_CHECK(probe && big == 7 && memcmp(probe, "abc\0def", 7) == 0);
    free(probe);
    ex_buffer_free(b);
    b = ex_buffer_create("abcdef", 3);
    HUNT_EQ_INT(ex_buffer_size(b), 3);
    ex_buffer_free(b);
    b = ex_buffer_create("abc", 0);
    HUNT_EQ_INT(ex_buffer_size(b), 0);
    ex_buffer_free(b);

    hunt_step("raw buffer: random operations");
    b = ex_buffer_create("first\nsecond\nthird", 18);
    m_set(&m, "first\nsecond\nthird", 18);
    compare_buffer(b, &m, 1);
    for (i = 0; i < ops; i++) {
        int kind = (int)hunt_rnd(100);
        hunt_stepf("raw buffer op %ld (kind %d, size %lu)", i, kind, (unsigned long)m.n);
        if (kind < 45 || m.n == 0) {
            const char *p = NASTY[hunt_rnd(sizeof NASTY / sizeof *NASTY)];
            size_t l = !strcmp(p, "\0\0") ? 2 : !strcmp(p, "x\0y") ? 3 : strlen(p), at;
            switch (hunt_rnd(6)) { case 0: at = 0; break; case 1: at = m.n; break; default: at = hunt_rnd_size(m.n + 1); }
            if (m.n > 6000) continue;
            HUNT_EQ_INT(ex_buffer_insert(b, at, p, l), 0);
            m_insert(&m, at, p, l);
        } else if (kind < 80) {
            size_t at = hunt_rnd_size(m.n), l = hunt_chance(10) ? m.n - at : 1 + hunt_rnd_size(m.n - at > 20 ? 20 : m.n - at);
            HUNT_EQ_INT(ex_buffer_delete(b, at, l), 0);
            m_delete(&m, at, l);
        } else if (kind < 88) {
            /* bad ranges must fail and change nothing */
            size_t before = m.n;
            switch (hunt_rnd(5)) {
            case 0: HUNT_CHECK(ex_buffer_insert(b, m.n + 1 + hunt_rnd_size(50), "z", 1) != 0); break;
            case 1: HUNT_CHECK(ex_buffer_delete(b, m.n, 1) != 0); break;
            case 2: HUNT_CHECK(ex_buffer_delete(b, 0, m.n + 1) != 0); break;
            case 3: HUNT_CHECK(ex_buffer_delete(b, 1, (size_t)-1) != 0); break;       /* offset + len overflows */
            default: HUNT_CHECK(ex_buffer_delete(b, (size_t)-1, 2) != 0); break;
            }
            HUNT_EQ_INT(ex_buffer_size(b), before);
        } else {
            /* a replace in two steps in the middle of a long run of pieces: delete then insert at the same place */
            size_t at = hunt_rnd_size(m.n + 1), l = hunt_rnd_size(m.n - at > 5 ? 5 : m.n - at + 1);
            if (l > m.n - at) l = m.n - at;
            HUNT_EQ_INT(ex_buffer_delete(b, at, l), 0);
            m_delete(&m, at, l);
            HUNT_EQ_INT(ex_buffer_insert(b, at, "<>", 2), 0);
            m_insert(&m, at, "<>", 2);
        }
        compare_buffer(b, &m, (i % 97) == 0);
        compare_reads(b, &m);
        HUNT_SUSPECT(ex_buffer_piece_count(b) <= (size_t)(2 * i + 8), "the piece count should not outgrow twice the number of operations");
    }
    compare_buffer(b, &m, 1);
    ex_buffer_free(b);
    free(m.t);

    hunt_step("raw buffer: typing at the end one byte at a time");
    b = ex_buffer_create(NULL, 0);
    for (i = 0; i < 20000; i++) { char c = (char)('a' + i % 26); HUNT_EQ_INT(ex_buffer_insert(b, (size_t)i, &c, 1), 0); }
    HUNT_EQ_INT(ex_buffer_size(b), 20000);
    HUNT_SUSPECT(ex_buffer_piece_count(b) < 200, "20000 single-byte appends should coalesce into a few pieces (a piece per keystroke makes every line lookup slower)");
    ex_buffer_free(b);

    hunt_step("raw buffer: typing at the front one byte at a time");
    b = ex_buffer_create(NULL, 0);
    for (i = 0; i < 5000; i++) HUNT_EQ_INT(ex_buffer_insert(b, 0, "x", 1), 0);
    HUNT_EQ_INT(ex_buffer_size(b), 5000);
    ex_buffer_free(b);
}

/* ---- B. the document ---- */

typedef struct Replica { char *t; size_t n, cap; unsigned long edits; int bad; } Replica;

static void replica_edit(void *user, size_t offset, size_t removed, const char *inserted, size_t inserted_len) {
    Replica *r = (Replica *)user;
    r->edits++;
    if (offset > r->n || removed > r->n - offset) { r->bad++; return; }     /* an edit that does not fit the text it was told about */
    if (r->n - removed + inserted_len > r->cap) { r->cap = (r->n + inserted_len) * 2 + 64; r->t = (char *)realloc(r->t, r->cap); if (!r->t) exit(2); }
    memmove(r->t + offset + inserted_len, r->t + offset + removed, r->n - offset - removed);
    if (inserted_len) memcpy(r->t + offset, inserted, inserted_len);
    r->n = r->n - removed + inserted_len;
}

static size_t lo_(ExSel s) { return s.anchor < s.head ? s.anchor : s.head; }
static size_t hi_(ExSel s) { return s.anchor < s.head ? s.head : s.anchor; }

static void check_sels(const ExDoc *d, size_t size) {
    size_t n = ex_doc_sel_count(d), i;
    HUNT_CHECK(n >= 1);
    HUNT_CHECK(ex_doc_primary(d) < n || n == 0);
    for (i = 0; i < n; i++) {
        ExSel s = ex_doc_sel(d, i);
        HUNT_CHECKF(s.anchor <= size && s.head <= size, "selection %lu is %lu..%lu in a text of %lu bytes", (unsigned long)i, (unsigned long)s.anchor, (unsigned long)s.head, (unsigned long)size);
        if (i > 0) {
            ExSel p = ex_doc_sel(d, i - 1);
            HUNT_CHECKF(lo_(s) >= hi_(p), "selections %lu and %lu overlap or are out of order", (unsigned long)(i - 1), (unsigned long)i);
            HUNT_CHECKF(!(lo_(s) == hi_(s) && lo_(p) == hi_(p) && lo_(s) == lo_(p)), "two carets at the same offset were not merged");
        }
    }
}

static void check_doc(const ExDoc *d, const Model *m, const Replica *r, const char *what) {
    size_t n = 0;
    char *t = ex_buffer_text(ex_doc_buffer(d), &n);
    HUNT_CHECKF(n == m->n && (n == 0 || (t && memcmp(t, m->t, n) == 0)), "the text differs from the model after %s (%lu bytes against %lu)", what, (unsigned long)n, (unsigned long)m->n);
    HUNT_CHECKF(r->n == n && (n == 0 || (t && memcmp(t, r->t, n) == 0)), "the edit listener's stream does not rebuild the text after %s (%lu bytes against %lu)", what, (unsigned long)r->n, (unsigned long)n);
    HUNT_CHECKF(r->bad == 0, "the edit listener was told an edit outside the text, after %s", what);
    free(t);
    check_sels(d, n);
}

typedef struct Snap { char *t; size_t n; } Snap;

static void snap_store(Snap **snaps, size_t *nsnaps, size_t node, const char *t, size_t n) {
    if (node >= *nsnaps) { *snaps = (Snap *)realloc(*snaps, (node + 16) * sizeof **snaps); memset(*snaps + *nsnaps, 0, (node + 16 - *nsnaps) * sizeof **snaps); *nsnaps = node + 16; }
    free((*snaps)[node].t);
    (*snaps)[node].t = hunt_dup(t, n);
    (*snaps)[node].n = n;
}

static void test_document(void) {
    char init[400];
    size_t initn = hunt_random_valid_text(init, sizeof init, 12);
    ExDoc *d = ex_doc_create(init, initn);
    Model m = {0};
    Replica rep = {0};
    Snap *snaps = NULL;
    size_t nsnaps = 0, i;
    long ops = hunt_iters(8000), k;

    m_set(&m, init, initn);
    rep.t = (char *)malloc(initn + 64); rep.cap = initn + 64;
    memcpy(rep.t, init, initn); rep.n = initn;
    ex_doc_set_edit_listener(d, replica_edit, &rep);
    snap_store(&snaps, &nsnaps, 0, m.t, m.n);
    check_doc(d, &m, &rep, "creation");

    for (k = 0; k < ops; k++) {
        unsigned long v0 = ex_doc_version(d), e0 = rep.edits;
        size_t nodes0 = ex_doc_node_count(d), size0 = m.n;
        int kind = (int)hunt_rnd(100);
        const char *what = "?";
        hunt_stepf("document op %ld kind %d (text %lu bytes, %lu nodes)", k, kind, (unsigned long)m.n, (unsigned long)nodes0);

        if (kind < 22) {
            /* typing at every cursor (coalesced or not) */
            char piece[16];
            size_t pl = hunt_random_valid_text(piece, 8, 2);
            size_t ns = ex_doc_sel_count(d), j;
            long shift = 0;
            ExSel *before = (ExSel *)malloc(ns * sizeof *before);
            Model next = {0};
            what = "replace_selections";
            if (m.n > 3000) { free(before); continue; }
            for (j = 0; j < ns; j++) before[j] = ex_doc_sel(d, j);
            m_set(&next, m.t, m.n);
            for (j = 0; j < ns; j++) {
                size_t lo = lo_(before[j]), hi = hi_(before[j]);
                m_delete(&next, (size_t)((long)lo + shift), hi - lo);
                m_insert(&next, (size_t)((long)lo + shift), piece, pl);
                shift += (long)pl - (long)(hi - lo);
            }
            HUNT_EQ_INT(ex_doc_replace_selections(d, piece, pl, hunt_chance(60)), 0);
            m_set(&m, next.t, next.n);
            if (pl > 0 && ex_doc_sel_count(d) == ns) {
                long sh = 0;
                for (j = 0; j < ns; j++) {
                    size_t lo = lo_(before[j]), hi = hi_(before[j]), want = (size_t)((long)lo + sh + (long)pl);
                    ExSel s = ex_doc_sel(d, j);
                    HUNT_CHECKF(s.anchor == s.head && s.head == want, "caret %lu after typing is at %lu, want %lu", (unsigned long)j, (unsigned long)s.head, (unsigned long)want);
                    sh += (long)pl - (long)(hi - lo);
                }
            }
            free(next.t); free(before);
        } else if (kind < 36) {
            /* a valid multi-change edit */
            ExChange ch[6];
            char texts[6][8];
            size_t nc = 1 + hunt_rnd_size(5), at = 0, j;
            Model next = {0};
            long shift = 0;
            what = "apply (valid)";
            if (m.n > 3000) continue;
            m_set(&next, m.t, m.n);
            for (j = 0; j < nc; j++) {
                size_t room = m.n - at, off, rl, tl;
                if (at > m.n) break;
                off = at + hunt_rnd_size(room + 1);
                rl = hunt_rnd_size(m.n - off + 1 > 6 ? 6 : m.n - off + 1);
                tl = hunt_random_valid_text(texts[j], sizeof texts[j], 2);
                ch[j].offset = off; ch[j].remove_len = rl; ch[j].text = texts[j]; ch[j].text_len = tl;
                m_delete(&next, (size_t)((long)off + shift), rl);
                m_insert(&next, (size_t)((long)off + shift), texts[j], tl);
                shift += (long)tl - (long)rl;
                at = off + rl;
            }
            nc = j;
            HUNT_EQ_INT(ex_doc_apply(d, ch, nc, hunt_chance(30), NULL, 0), 0);
            m_set(&m, next.t, next.n);
            free(next.t);
        } else if (kind < 46) {
            /* an invalid edit: nothing may change */
            ExChange ch[3];
            size_t nodes = ex_doc_node_count(d);
            int rc;
            what = "apply (invalid)";
            ch[0].text = "ZZ"; ch[0].text_len = 2; ch[1] = ch[0]; ch[2] = ch[0];
            switch (hunt_rnd(5)) {
            case 0: ch[0].offset = m.n + 1; ch[0].remove_len = 0; rc = ex_doc_apply(d, ch, 1, 0, NULL, 0); break;
            case 1: ch[0].offset = 0; ch[0].remove_len = m.n + 1; rc = ex_doc_apply(d, ch, 1, 0, NULL, 0); break;
            case 2: ch[0].offset = 2; ch[0].remove_len = 3; ch[1].offset = 3; ch[1].remove_len = 0; rc = m.n >= 5 ? ex_doc_apply(d, ch, 2, 0, NULL, 0) : -1; break;   /* overlap */
            case 3: ch[0].offset = 4; ch[0].remove_len = 0; ch[1].offset = 1; ch[1].remove_len = 0; rc = m.n >= 4 ? ex_doc_apply(d, ch, 2, 0, NULL, 0) : -1; break;     /* unsorted */
            default: ch[0].offset = 1; ch[0].remove_len = (size_t)-1; rc = ex_doc_apply(d, ch, 1, 0, NULL, 0); break;                                                  /* overflow */
            }
            HUNT_CHECK(rc != 0);
            HUNT_EQ_INT(ex_doc_node_count(d), nodes);
        } else if (kind < 60) {
            what = "undo";
            ex_doc_undo(d);
            { Snap *s = &snaps[ex_doc_current_node(d)]; m_set(&m, s->t, s->n); }
        } else if (kind < 68) {
            what = "redo";
            ex_doc_redo(d);
            { Snap *s = &snaps[ex_doc_current_node(d)]; m_set(&m, s->t, s->n); }
        } else if (kind < 74) {
            size_t target = hunt_rnd_size(ex_doc_node_count(d));
            what = "goto_node";
            HUNT_EQ_INT(ex_doc_goto_node(d, target), 0);
            HUNT_EQ_INT(ex_doc_goto_node(d, target), 0);            /* going to the node we are on is harmless */
            HUNT_EQ_INT(ex_doc_current_node(d), target);
            { Snap *s = &snaps[target]; m_set(&m, s->t, s->n); }
        } else if (kind < 78) {
            what = "goto_node (bad)";
            HUNT_CHECK(ex_doc_goto_node(d, ex_doc_node_count(d) + 5) != 0);
        } else if (kind < 84) {
            what = "set_caret";
            ex_doc_set_caret(d, hunt_rnd_size(m.n + 1));
            HUNT_EQ_INT(ex_doc_sel_count(d), 1);
        } else if (kind < 88) {
            what = "set_caret past the end";
            ex_doc_set_caret(d, m.n + 1 + hunt_rnd_size(100));
            HUNT_SPEC(ex_doc_sel(d, 0).head <= m.n, "include/exhaust_buffer.h: selections are kept within the text");
        } else if (kind < 94) {
            what = "add_cursor";
            ex_doc_add_cursor(d, hunt_rnd_size(m.n + 1));
        } else if (kind < 98) {
            ExSel s[4];
            size_t j, ns = 1 + hunt_rnd_size(4);
            what = "set_selections";
            for (j = 0; j < ns; j++) { s[j].anchor = hunt_rnd_size(m.n + 1); s[j].head = hunt_rnd_size(m.n + 1); }
            ex_doc_set_selections(d, s, ns);
        } else {
            what = "seal";
            ex_doc_seal(d);
        }

        /* the text, the replica of the edit stream, the selections */
        check_doc(d, &m, &rep, what);

        /* the undo tree bookkeeping: the snapshot of the node we are on is the text */
        snap_store(&snaps, &nsnaps, ex_doc_current_node(d), m.t, m.n);
        HUNT_CHECK(ex_doc_current_node(d) < ex_doc_node_count(d));

        /* the version and the edit log */
        if (rep.edits != e0) {
            ExDocEdit log[16];
            int got = ex_doc_edits_since(d, v0, log, 16);
            HUNT_CHECKF(ex_doc_version(d) > v0, "the version did not grow after %lu edits (%s)", rep.edits - e0, what);
            if (rep.edits - e0 <= 16) {
                long delta = 0;
                int j;
                HUNT_CHECKF(got >= 0, "ex_doc_edits_since returned %d for %lu edits (%s)", got, rep.edits - e0, what);
                for (j = 0; j < got && j < 16; j++) delta += (long)log[j].inserted - (long)log[j].removed;
                if (got >= 0) {
                    HUNT_CHECKF(delta == (long)m.n - (long)size0, "the edit log says the text changed by %ld bytes, it changed by %ld (%s)", delta, (long)m.n - (long)size0, what);
                    HUNT_SUSPECT((unsigned long)got == rep.edits - e0, "the edit log has one entry per edit the listener saw");
                }
            }
        } else {
            HUNT_SUSPECT(ex_doc_version(d) == v0, "the version should not grow when the text did not change");
        }
        if (k % 500 == 0 && ex_doc_node_count(d) > 1) {
            /* every node must still reproduce its snapshot, however the tree was walked */
            size_t cur = ex_doc_current_node(d), nn = ex_doc_node_count(d);
            for (i = 0; i < nn; i++) {
                size_t sz = 0;
                char *t;
                if (i >= nsnaps || !snaps[i].t) continue;
                ex_doc_goto_node(d, i);
                t = ex_buffer_text(ex_doc_buffer(d), &sz);
                HUNT_CHECKF(sz == snaps[i].n && (sz == 0 || memcmp(t, snaps[i].t, sz) == 0), "undo tree node %lu does not reproduce the text it had", (unsigned long)i);
                free(t);
            }
            ex_doc_goto_node(d, cur);
            m_set(&m, snaps[cur].t, snaps[cur].n);
            check_doc(d, &m, &rep, "the walk through every node");
        }
    }
    for (i = 0; i < nsnaps; i++) free(snaps[i].t);
    free(snaps);
    ex_doc_free(d);
    free(m.t);
    free(rep.t);
}

/* ---- C. documented behavior ---- */

static char *doc_text(const ExDoc *d) { size_t n; return ex_buffer_text(ex_doc_buffer(d), &n); }

static void test_typing_rules(void) {
    ExDoc *d = ex_doc_create("", 0);
    char *t;
    size_t i;

    hunt_step("typing: coalescing");
    for (i = 0; i < 10; i++) { char c = (char)('a' + i); ex_doc_replace_selections(d, &c, 1, 1); }
    t = doc_text(d);
    HUNT_EQ_STR(t, "abcdefghij");
    free(t);
    HUNT_SPEC(ex_doc_node_count(d) == 2, "include/exhaust_buffer.h: a single insertion that continues the previous one extends that undo step");
    ex_doc_undo(d);
    t = doc_text(d);
    HUNT_EQ_STR(t, "");
    free(t);
    HUNT_EQ_INT(ex_doc_redo(d), 1);
    t = doc_text(d);
    HUNT_EQ_STR(t, "abcdefghij");
    free(t);
    HUNT_EQ_INT(ex_doc_redo(d), 0);
    HUNT_EQ_INT(ex_doc_undo(d), 1);
    HUNT_EQ_INT(ex_doc_undo(d), 0);                  /* at the root */

    hunt_step("typing: moving the caret ends the step");
    ex_doc_redo(d);
    ex_doc_set_caret(d, 3);
    ex_doc_replace_selections(d, "X", 1, 1);
    t = doc_text(d);
    HUNT_EQ_STR(t, "abcXdefghij");
    free(t);
    HUNT_CHECK(ex_doc_node_count(d) == 3);
    ex_doc_undo(d);
    t = doc_text(d);
    HUNT_EQ_STR(t, "abcdefghij");
    free(t);

    hunt_step("typing: an insertion after an undo does not extend the undone step");
    ex_doc_undo(d);          /* back at the root: "" */
    ex_doc_replace_selections(d, "Q", 1, 1);
    t = doc_text(d);
    HUNT_EQ_STR(t, "Q");
    free(t);
    ex_doc_undo(d);
    t = doc_text(d);
    HUNT_EQ_STR(t, "");
    free(t);
    ex_doc_redo(d);
    t = doc_text(d);
    HUNT_SPEC(t && !strcmp(t, "Q"), "include/exhaust_buffer.h: redo follows the child that was current most recently");
    free(t);
    HUNT_CHECK(ex_doc_node_count(d) >= 4);          /* both branches still exist */

    hunt_step("typing: a delete is not joined with typing");
    ex_doc_free(d);
    d = ex_doc_create("hello", 5);
    ex_doc_set_caret(d, 5);
    ex_doc_replace_selections(d, "!", 1, 1);
    { ExChange c; c.offset = 0; c.remove_len = 1; c.text = ""; c.text_len = 0; ex_doc_apply(d, &c, 1, 1, NULL, 0); }
    t = doc_text(d);
    HUNT_EQ_STR(t, "ello!");
    free(t);
    ex_doc_undo(d);
    t = doc_text(d);
    HUNT_EQ_STR(t, "hello!");
    free(t);
    ex_doc_free(d);

    hunt_step("selections: multiple cursors merge");
    d = ex_doc_create("0123456789", 10);
    ex_doc_set_caret(d, 4);
    ex_doc_add_cursor(d, 4);
    HUNT_EQ_INT(ex_doc_sel_count(d), 1);                       /* two carets at one place are one */
    ex_doc_add_cursor(d, 7);
    ex_doc_add_cursor(d, 1);
    HUNT_EQ_INT(ex_doc_sel_count(d), 3);
    HUNT_CHECK(ex_doc_sel(d, 0).head == 1 && ex_doc_sel(d, 1).head == 4 && ex_doc_sel(d, 2).head == 7);   /* sorted */
    ex_doc_replace_selections(d, "-", 1, 0);
    t = doc_text(d);
    HUNT_EQ_STR(t, "0-123-456-789");
    free(t);
    {
        ExSel s[2] = { { 2, 6 }, { 4, 9 } };            /* overlapping selections merge into one */
        ex_doc_set_selections(d, s, 2);
        HUNT_EQ_INT(ex_doc_sel_count(d), 1);
        HUNT_CHECK(lo_(ex_doc_sel(d, 0)) == 2 && hi_(ex_doc_sel(d, 0)) == 9);
    }
    {
        ExSel s[2] = { { 9, 2 }, { 0, 0 } };            /* a backwards selection keeps its direction */
        ex_doc_set_selections(d, s, 2);
        HUNT_CHECK(ex_doc_sel_count(d) == 2);
    }
    ex_doc_set_selections(d, NULL, 0);                      /* no selection at all is one caret, never zero */
    HUNT_EQ_INT(ex_doc_sel_count(d), 1);
    ex_doc_free(d);

    hunt_step("apply: no changes and an empty change");
    d = ex_doc_create("abc", 3);
    HUNT_EQ_INT(ex_doc_apply(d, NULL, 0, 0, NULL, 0), 0);
    { ExChange c; c.offset = 1; c.remove_len = 0; c.text = ""; c.text_len = 0; HUNT_EQ_INT(ex_doc_apply(d, &c, 1, 0, NULL, 0), 0); }
    HUNT_SUSPECT(ex_doc_node_count(d) == 1, "an edit that changes nothing should not add an undo step (the user would press Ctrl+Z and see nothing happen)");
    ex_doc_free(d);
}

/* ---- D. one large text ---- */
static void test_large(void) {
    size_t lines = 150000, i, n;
    char *text, *p;
    ExBuffer *b;
    Model m = {0};
    text = (char *)malloc(lines * 12 + 1);
    for (i = 0, p = text; i < lines; i++) p += sprintf(p, "line %06lu\n", (unsigned long)i);
    n = (size_t)(p - text);
    hunt_step("large: create 150000 lines");
    b = ex_buffer_create(text, n);
    HUNT_EQ_INT(ex_buffer_line_count(b), lines + 1);
    for (i = 0; i < 2000; i++) {
        size_t l = hunt_rnd_size(lines + 1), s = ex_buffer_line_start(b, l);
        HUNT_CHECKF(s == (l < lines ? l * 12 : n), "line_start(%lu)", (unsigned long)l);
        if (l < lines) HUNT_CHECKF(ex_buffer_line_of_offset(b, l * 12 + hunt_rnd_size(12)) == l, "line_of_offset in line %lu", (unsigned long)l);
    }
    hunt_step("large: edits spread over the text keep every later line right");
    m.t = text; m.n = n; m.cap = lines * 12 + 1;
    for (i = 0; i < 300; i++) {
        size_t at = hunt_rnd_size(m.n + 1);
        if (hunt_chance(50)) { ex_buffer_insert(b, at, "\n\n", 2); m_insert(&m, at, "\n\n", 2); }
        else if (m.n > at + 40) { size_t l = hunt_rnd_size(40); ex_buffer_delete(b, at, l); m_delete(&m, at, l); }
    }
    HUNT_EQ_INT(ex_buffer_size(b), m.n);
    HUNT_EQ_INT(ex_buffer_line_count(b), m_lines(&m));
    {
        char *t = ex_buffer_text(b, &n);
        HUNT_CHECK(n == m.n && t && memcmp(t, m.t, n) == 0);
        free(t);
    }
    for (i = 0; i < 300; i++) {
        size_t l = hunt_rnd_size(m_lines(&m) + 1);
        HUNT_CHECK(ex_buffer_line_start(b, l) == m_line_start(&m, l));
    }
    ex_buffer_free(b);
    free(m.t);
}

int main(int argc, char **argv) {
    hunt_begin("hunt_buffer", argc, argv);
    test_raw_buffer();
    test_document();
    test_typing_rules();
    test_large();
    return hunt_end();
}
