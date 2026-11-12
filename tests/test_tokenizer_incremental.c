/* tokenizer_incremental: after random edits the incremental update gives exactly the spans a full
 * tokenization of the new text gives, and the range it reports covers every change: spans before it are
 * untouched and spans after it only moved. Usage: test_tokenizer_incremental <sample-note.md> */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_markdown.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint64_t g_rng = 88172645463325252ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static size_t below(size_t n) { return n ? (size_t)(rnd() % n) : 0; }

typedef struct Flat { ExMdSpan *v; size_t n, cap; } Flat;

static int collect(const ExMdSpan *sp, void *u) {
    Flat *f = u;
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 256; f->v = realloc(f->v, f->cap * sizeof *f->v); }
    f->v[f->n++] = *sp;
    return 0;
}

static void flatten(const ExMd *m, size_t len, Flat *f) {
    f->n = 0;
    ex_md_visit(m, 0, len + 1, collect, f);
}

static int same(const ExMdSpan *a, const ExMdSpan *b, ptrdiff_t shift) {
    return a->kind == b->kind && a->flags == b->flags && a->level == b->level && (ptrdiff_t)a->start + shift == (ptrdiff_t)b->start && (ptrdiff_t)a->end + shift == (ptrdiff_t)b->end &&
           a->parent_kind == b->parent_kind && (a->parent_kind == 0 || ((ptrdiff_t)a->parent_start + shift == (ptrdiff_t)b->parent_start && (ptrdiff_t)a->parent_end + shift == (ptrdiff_t)b->parent_end));
}

static void describe(const ExMdSpan *s, char *out, size_t cap) { snprintf(out, cap, "%s %zu-%zu L%u F%u in %s %zu-%zu", ex_md_kind_name(s->kind), s->start, s->end, s->level, s->flags, ex_md_kind_name(s->parent_kind), s->parent_start, s->parent_end); }

static const char *const k_fragments[] = {
    "# Heading\n", "## Two ##\n", "Plain text with *em* and **strong** and `code` and [[link]] and #tag.\n", "- item\n", "  - nested\n", "1. one\n", "2. two\n", "- [ ] task\n", "- [x] done\n",
    "> quote\n", "> [!note] Title\n", "> body\n", ">\n", "```\n", "```js\n", "code line\n", "~~~\n", "    indented\n", "|a|b|\n", "|-|:-:|\n", "|1|2|\n", "---\n", "===\n", "***\n",
    "$$\n", "x^2\n", "%%\n", "hidden\n", "<div>\n", "</div>\n", "<p>para</p>\n", "[^a]: footnote\n", "    more\n", "\n", "\n", "\n", "text ^id\n", "![[img.png]]\n", "[t](u)\n", "==hl== ~~st~~\n",
    "a\n", "b  \n", "line one\n", "line two\n", "https://x.y/z\n", "$a$ and $$b$$\n", "\\*escaped\\*\n", "<!-- c -->\n", "- \n", "1) x\n", "10. ten\n"};
#define NFRAG (sizeof k_fragments / sizeof k_fragments[0])
static const char k_alphabet[] = "#*_`>-[]()|~=$%^!<>:+\\\t\"' \n\n\n1.ab";

static char *random_doc(size_t *len) {
    size_t n = 1 + below(40), i, cap = 4096, used = 0;
    char *d = malloc(cap);
    for (i = 0; i < n; i++) {
        const char *f = k_fragments[below(NFRAG)];
        size_t fl = strlen(f);
        if (used + fl + 1 > cap) { cap *= 2; d = realloc(d, cap); }
        memcpy(d + used, f, fl);
        used += fl;
    }
    if (below(4) == 0 && used > 0) used--;                       /* no final line break */
    d[used] = 0;
    *len = used;
    return d;
}

/* One edit of a text; returns the new text (malloc'ed) and its pieces. */
static char *edit(const char *t, size_t len, size_t *new_len, size_t *off, size_t *removed, size_t *inserted) {
    size_t o = below(len + 1), r = 0, ins = 0, i;
    char piece[8], *nt;
    switch (below(4)) {
    case 0: ins = 1 + below(3); for (i = 0; i < ins; i++) piece[i] = k_alphabet[below(sizeof k_alphabet - 1)]; break;
    case 1: r = below(len - o < 8 ? len - o + 1 : 8); break;
    case 2: r = below(len - o < 4 ? len - o + 1 : 4); ins = 1 + below(2); for (i = 0; i < ins; i++) piece[i] = k_alphabet[below(sizeof k_alphabet - 1)]; break;
    default: {   /* insert a whole fragment, or delete a whole line */
        if (below(2)) {
            const char *f = k_fragments[below(NFRAG)];
            nt = malloc(len + strlen(f) + 1);
            memcpy(nt, t, o); memcpy(nt + o, f, strlen(f)); memcpy(nt + o + strlen(f), t + o, len - o);
            *new_len = len + strlen(f); nt[*new_len] = 0; *off = o; *removed = 0; *inserted = strlen(f);
            return nt;
        } else {
            size_t s = o, e = o;
            while (s > 0 && t[s - 1] != '\n') s--;
            while (e < len && t[e] != '\n') e++;
            if (e < len) e++;
            o = s; r = e - s;
        }
    }
    }
    nt = malloc(len - r + ins + 1);
    memcpy(nt, t, o);
    memcpy(nt + o, piece, ins);
    memcpy(nt + o + ins, t + o + r, len - o - r);
    *new_len = len - r + ins;
    nt[*new_len] = 0;
    *off = o; *removed = r; *inserted = ins;
    return nt;
}

static int check_step(ExMd *inc, const char *old_text, size_t old_len, const char *new_text, size_t new_len, size_t off, size_t removed, size_t inserted, Flat *before, int *shown) {
    ExMd *full = ex_md_create();
    Flat fa = {0}, fb = {0};
    ExMdRange range;
    size_t i, a_old = 0, a_new = 0, b_old, b_new;
    ptrdiff_t delta = (ptrdiff_t)inserted - (ptrdiff_t)removed;
    int ok = 1;
    (void)old_len;
    range = ex_md_update(inc, new_text, new_len, off, removed, inserted);
    ex_md_tokenize(full, new_text, new_len);
    flatten(inc, new_len, &fa);
    flatten(full, new_len, &fb);
    if (fa.n != fb.n) ok = 0;
    for (i = 0; ok && i < fa.n; i++) if (!same(&fb.v[i], &fa.v[i], 0)) ok = 0;
    if (!ok && !*shown) {
        char x[200], y[200];
        *shown = 1;
        fprintf(stderr, "incremental differs from full: edit at %zu removed %zu inserted %zu; %zu vs %zu spans\n", off, removed, inserted, fa.n, fb.n);
        for (i = 0; i < fa.n && i < fb.n; i++) if (!same(&fb.v[i], &fa.v[i], 0)) { describe(&fa.v[i], x, sizeof x); describe(&fb.v[i], y, sizeof y); fprintf(stderr, "  first difference at span %zu:\n    incremental %s\n    full        %s\n", i, x, y); break; }
        fprintf(stderr, "--- old text ---\n%s\n--- new text ---\n%s\n------------\n", old_text, new_text);
    }
    /* the reported range covers the changes: before it nothing changed, after it spans only moved */
    if (ok) {
        while (a_old < before->n && before->v[a_old].start < range.start) a_old++;
        while (a_new < fa.n && fa.v[a_new].start < range.start) a_new++;
        if (a_old != a_new) ok = 0;
        for (i = 0; ok && i < a_old; i++) if (!same(&before->v[i], &fa.v[i], 0)) ok = 0;
        {
            size_t old_end = (size_t)((ptrdiff_t)range.end - delta);
            b_old = 0; b_new = 0;
            if (range.end >= new_len) { b_old = before->n; b_new = fa.n; }       /* tokenized to the end: nothing after the range to compare */
            if ((ptrdiff_t)range.end - delta < 0) old_end = 0;
            while (b_old < before->n && before->v[b_old].start < old_end && range.end < new_len) b_old++;
            while (b_new < fa.n && fa.v[b_new].start < range.end && range.end < new_len) b_new++;
            if (before->n - b_old != fa.n - b_new) ok = 0;
            for (i = 0; ok && i < before->n - b_old; i++) if (!same(&before->v[b_old + i], &fa.v[b_new + i], delta)) ok = 0;
        }
        if (!ok && !*shown) { *shown = 1; fprintf(stderr, "the reported range [%zu,%zu) misses a change (edit at %zu, removed %zu, inserted %zu)\n--- old text ---\n%s\n--- new text ---\n%s\n------------\n", range.start, range.end, off, removed, inserted, old_text, new_text); }
    }
    free(fa.v); free(fb.v);
    ex_md_free(full);
    return ok;
}

static void run_doc(const char *start_text, size_t start_len, int steps, int *shown, size_t *max_blocks, size_t *total_blocks) {
    ExMd *m = ex_md_create();
    char *text = malloc(start_len + 1);
    size_t len = start_len, s;
    Flat cur = {0};
    memcpy(text, start_text, start_len);
    text[len] = 0;
    ex_md_tokenize(m, text, len);
    for (s = 0; s < (size_t)steps; s++) {
        size_t nlen, off, removed, inserted;
        char *nt = edit(text, len, &nlen, &off, &removed, &inserted);
        int ok;
        flatten(m, len, &cur);
        ok = check_step(m, text, len, nt, nlen, off, removed, inserted, &cur, shown);
        CHECK(ok);
        if (ex_md_last_retokenized(m) > *max_blocks) *max_blocks = ex_md_last_retokenized(m);
        *total_blocks += ex_md_last_retokenized(m);
        free(text);
        text = nt; len = nlen;
        if (!ok) break;
    }
    free(cur.v);
    free(text);
    ex_md_free(m);
}

int main(int argc, char **argv) {
    size_t len = 0, i, max_blocks = 0, total = 0;
    char *sample;
    int shown = 0;
    size_t docs = argc > 2 ? (size_t)atol(argv[2]) : 300, seed = argc > 3 ? (size_t)atol(argv[3]) : 0;
    if (argc < 2) { fprintf(stderr, "usage: test_tokenizer_incremental SAMPLE [DOCUMENTS [SEED]]\n"); return 2; }
    g_rng ^= seed * 0x9E3779B97F4A7C15ull;
    sample = ex_platform_read_file(argv[1], &len);
    CHECK(sample != NULL);
    if (!sample) return 1;
    /* the sample note and random documents built from fragments that exercise every block start */
    run_doc(sample, len, 400, &shown, &max_blocks, &total);
    for (i = 0; i < docs; i++) {
        size_t dl;
        char *d = random_doc(&dl);
        run_doc(d, dl, 40, &shown, &max_blocks, &total);
        free(d);
    }
    run_doc("", 0, 60, &shown, &max_blocks, &total);                    /* from nothing */
    /* a large note: edits retokenize a few blocks, not the note */
    {
        size_t reps = 150, bl = len * reps + reps * 8, pos = 0, steps_blocks_max = 0, steps_total = 0, small_steps = 0;
        char *big = malloc(bl + 1);
        ExMd *m = ex_md_create();
        size_t blocks, step;
        for (i = 0; i < reps; i++) { memcpy(big + pos, sample, len); pos += len; memcpy(big + pos, "\n\n\n", 3); pos += 3; }
        big[pos] = 0;
        ex_md_tokenize(m, big, pos);
        blocks = ex_md_block_count(m);
        for (step = 0; step < 300; step++) {
            size_t nlen, off, removed, inserted;
            char *nt = edit(big, pos, &nlen, &off, &removed, &inserted);
            ExMd *full = ex_md_create();
            ex_md_update(m, nt, nlen, off, removed, inserted);
            ex_md_tokenize(full, nt, nlen);
            CHECK(ex_md_span_count(full) == ex_md_span_count(m) && ex_md_block_count(full) == ex_md_block_count(m));
            if (ex_md_last_retokenized(m) > steps_blocks_max) steps_blocks_max = ex_md_last_retokenized(m);
            steps_total += ex_md_last_retokenized(m);
            if (ex_md_last_retokenized(m) <= 12) small_steps++;
            ex_md_free(full);
            free(big);
            big = nt; pos = nlen;
        }
        fprintf(stderr, "large note: %zu blocks; an edit retokenized %.1f blocks on average, %zu at most\n", blocks, (double)steps_total / 300.0, steps_blocks_max);
        CHECK(small_steps >= 270);          /* nine edits in ten retokenize a few blocks, not the note (an edit that opens an unclosed fence legitimately changes everything after it) */
        ex_md_free(m);
        free(big);
    }
    fprintf(stderr, "small documents: an edit retokenized %zu blocks at most\n", max_blocks);
    (void)total;
    free(sample);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("tokenizer_incremental: ok");
    return 0;
}
