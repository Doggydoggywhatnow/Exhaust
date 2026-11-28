/* hunt_markdown: the Markdown tokenizer (include/exhaust_markdown.h, docs/formats.md "Markdown tokens and the HTML subset").
 *
 * The editor draws from these spans and the index takes links, tags and tasks from them, so a wrong span is a wrong link in the graph and a wrong
 * rename. tests/test_tokenizer*.c check one sample note and random edits; this test adds
 *   A. span invariants (inside the text, parents contain children, visit order) on random and hostile text, text with NULs, broken UTF-8 and CRLF;
 *   B. the documented rules, one tiny note for each (tags, block ids, inline math, list starts, tables, callouts, wikilinks across lines...);
 *   C. EVERY single-byte edit (insert from a set of syntax characters, delete, replace) at EVERY offset of several sample notes: the incremental
 *      update must equal a full tokenization;
 *   D. text written to make a tokenizer slow (a hundred thousand `*`, `[`, backticks, `<`, nesting, wide tables): it must finish, and its
 *      spans must still satisfy the invariants;
 *   E. the same note with LF and with CRLF must give the same kinds in the same order.
 * Link: exhaust_markdown. */
#include "hunt.h"

#include "exhaust_markdown.h"

typedef struct Flat { ExMdSpan *v; size_t n, cap; } Flat;

static int collect(const ExMdSpan *sp, void *u) {
    Flat *f = (Flat *)u;
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 256; f->v = (ExMdSpan *)realloc(f->v, f->cap * sizeof *f->v); if (!f->v) exit(2); }
    f->v[f->n++] = *sp;
    return 0;
}
static void flatten(const ExMd *m, size_t len, Flat *f) { f->n = 0; ex_md_visit(m, 0, len + 1, collect, f); }

static int same_span(const ExMdSpan *a, const ExMdSpan *b) {
    return a->kind == b->kind && a->flags == b->flags && a->level == b->level && a->start == b->start && a->end == b->end && a->parent_kind == b->parent_kind &&
           (a->parent_kind == 0 || (a->parent_start == b->parent_start && a->parent_end == b->parent_end));
}

static const char *describe(const ExMdSpan *s) {
    static char buf[4][160];
    static int k;
    char *o = buf[k++ & 3];
    snprintf(o, 160, "%s %lu-%lu L%u F%u in %s %lu-%lu", ex_md_kind_name(s->kind), (unsigned long)s->start, (unsigned long)s->end, s->level, s->flags, ex_md_kind_name(s->parent_kind), (unsigned long)s->parent_start, (unsigned long)s->parent_end);
    return o;
}

/* ---- A. invariants ---- */

static void check_invariants(const ExMd *m, const char *text, size_t len, const char *what) {
    Flat f = {0};
    size_t i, nb, prev_end = 0;
    (void)text;
    flatten(m, len, &f);
    for (i = 0; i < f.n; i++) {
        const ExMdSpan *s = &f.v[i];
        if (!((int)s->kind >= 1 && (int)s->kind < EX_MD_KIND_COUNT)) { HUNT_CHECKF(0, "%s: span %lu has kind %d", what, (unsigned long)i, (int)s->kind); continue; }
        if (!(s->start <= s->end && s->end <= len)) { HUNT_CHECKF(0, "%s: span %lu (%s) is outside the %lu byte text", what, (unsigned long)i, describe(s), (unsigned long)len); continue; }
        if (s->parent_kind != 0) {
            if (!(s->parent_start <= s->start && s->end <= s->parent_end && s->parent_end <= len)) HUNT_CHECKF(0, "%s: span %lu (%s) is not inside its parent", what, (unsigned long)i, describe(s));
        }
        if (i > 0 && f.v[i - 1].start > s->start) HUNT_CHECKF(0, "%s: span %lu (%s) starts before the one before it (%s)", what, (unsigned long)i, describe(s), describe(&f.v[i - 1]));
        if (s->kind == EX_MD_CALLOUT) HUNT_CHECK(s->level < EX_CALLOUT_COUNT && s->flags <= EX_CALLOUT_FOLD_CLOSED);
        if (s->kind == EX_MD_HEADING) HUNT_CHECKF(s->level >= 1 && s->level <= 6, "%s: heading level %u", what, s->level);
        if (s->kind == EX_MD_TASK) HUNT_CHECK(s->flags <= 2);
    }
    HUNT_SUSPECT(ex_md_span_count(m) == f.n, "ex_md_span_count should equal the number of spans a whole-text visit returns");
    nb = ex_md_block_count(m);
    for (i = 0; i < nb; i++) {
        size_t a = 0, b = 0;
        ex_md_block_range(m, i, &a, &b);
        if (!(a <= b && b <= len && a >= prev_end)) HUNT_CHECKF(0, "%s: block %lu is %lu-%lu after %lu in a text of %lu bytes", what, (unsigned long)i, (unsigned long)a, (unsigned long)b, (unsigned long)prev_end, (unsigned long)len);
        prev_end = b > prev_end ? b : prev_end;
    }
    free(f.v);
}

/* A windowed visit returns exactly the spans that overlap the window, and stops when asked. */
static int count_cb(const ExMdSpan *s, void *u) { (void)s; (*(size_t *)u)++; return 0; }
static int stop_cb(const ExMdSpan *s, void *u) { (void)s; (*(size_t *)u)++; return 1; }

static void check_visit_windows(const ExMd *m, size_t len) {
    Flat all = {0}, win = {0};
    int k;
    flatten(m, len, &all);
    for (k = 0; k < 12 && len; k++) {
        size_t a = hunt_rnd_size(len + 1), b = a + hunt_rnd_size(len - a + 1), i, expect = 0, got = 0;
        const size_t b_asked = b;
        if (b == a) b = a + 1;                                        /* ex_md_visit: an empty window asks for the position a, as [a, a + 1) (exhaust_markdown.h) */
        win.n = 0;
        ex_md_visit(m, a, b_asked, collect, &win);
        for (i = 0; i < win.n; i++) {
            const ExMdSpan *s = &win.v[i];
            HUNT_CHECKF((s->end > a && s->start < b) || (s->start == s->end && s->start >= a && s->start <= b), "visit(%lu,%lu) returned a span that does not overlap it: %s", (unsigned long)a, (unsigned long)b, describe(s));
            if (i > 0) HUNT_CHECK(win.v[i - 1].start <= s->start);
        }
        for (i = 0; i < all.n; i++) if (all.v[i].end > a && all.v[i].start < b) expect++;
        for (i = 0; i < win.n; i++) if (win.v[i].end > a && win.v[i].start < b) got++;
        HUNT_CHECKF(got == expect, "visit(%lu,%lu) returned %lu spans that overlap, a whole visit has %lu", (unsigned long)a, (unsigned long)b, (unsigned long)got, (unsigned long)expect);
    }
    if (all.n) {
        size_t calls = 0;
        ex_md_visit(m, 0, len + 1, stop_cb, &calls);
        HUNT_EQ_INT(calls, 1);
        calls = 0;
        ex_md_visit(m, len + 5, len + 10, count_cb, &calls);          /* a window past the end */
        HUNT_CHECKF(calls == 0, "a window past the end returned %lu spans", (unsigned long)calls);
        calls = 0;
        ex_md_visit(m, 5, 5, count_cb, &calls);                       /* an empty window must not crash */
    }
    free(all.v); free(win.v);
}

static void test_invariants(void) {
    long rounds = hunt_iters(1500), r;
    ExMd *m = ex_md_create(), *fresh;
    for (r = 0; r < rounds; r++) {
        char text[1500];
        size_t len;
        char *exact;
        Flat a = {0}, b = {0};
        size_t i;
        int mode = (int)hunt_rnd(4);
        len = mode == 0 ? hunt_random_valid_text(text, sizeof text, 60) : mode == 1 ? hunt_random_text(text, sizeof text, 60) : hunt_random_text(text, sizeof text, 12);
        if (mode == 3 && len) { size_t k; for (k = 0; k < len; k++) if (hunt_chance(4) && text[k] == '\n') text[k] = '\r'; }       /* lone CRs and CRLF mixes */
        exact = hunt_dup(text, len);
        hunt_stepf("markdown invariants %ld", r);
        hunt_set_input(exact, len, "note text");
        ex_md_tokenize(m, exact, len);
        check_invariants(m, exact, len, "random");
        check_visit_windows(m, len);
        /* tokenizing into an object that held another text gives the same as a fresh one */
        fresh = ex_md_create();
        ex_md_tokenize(fresh, exact, len);
        flatten(m, len, &a); flatten(fresh, len, &b);
        HUNT_CHECKF(a.n == b.n, "a reused tokenizer gives %lu spans, a fresh one %lu", (unsigned long)a.n, (unsigned long)b.n);
        for (i = 0; i < a.n && i < b.n; i++) if (!same_span(&a.v[i], &b.v[i])) { HUNT_CHECKF(0, "a reused tokenizer differs at span %lu: %s vs %s", (unsigned long)i, describe(&a.v[i]), describe(&b.v[i])); break; }
        ex_md_free(fresh);
        free(a.v); free(b.v);
        hunt_clear_input();
        free(exact);
    }
    ex_md_tokenize(m, "", 0);
    check_invariants(m, "", 0, "empty");
    ex_md_tokenize(m, NULL, 0);
    check_invariants(m, "", 0, "NULL empty");
    ex_md_free(m);

    hunt_step("kind and callout names");
    {
        int k;
        for (k = 1; k < EX_MD_KIND_COUNT; k++) {
            const char *n = ex_md_kind_name((ExMdKind)k);
            HUNT_CHECKF(n && *n, "kind %d has no name", k);
            if (n) { int j; for (j = 1; j < k; j++) { const char *o = ex_md_kind_name((ExMdKind)j); HUNT_CHECKF(!o || strcmp(o, n) != 0, "kinds %d and %d are both called %s", j, k, n); } }
        }
        for (k = 0; k < EX_CALLOUT_COUNT; k++) HUNT_CHECK(ex_callout_name((ExCalloutType)k) && *ex_callout_name((ExCalloutType)k));
    }
    hunt_step("callout aliases (docs/formats.md, Callouts)");
    {
        struct { const char *name; ExCalloutType t; } al[] = {
            { "note", EX_CALLOUT_NOTE }, { "abstract", EX_CALLOUT_ABSTRACT }, { "summary", EX_CALLOUT_ABSTRACT }, { "tldr", EX_CALLOUT_ABSTRACT }, { "info", EX_CALLOUT_INFO }, { "todo", EX_CALLOUT_TODO },
            { "tip", EX_CALLOUT_TIP }, { "hint", EX_CALLOUT_TIP }, { "important", EX_CALLOUT_TIP }, { "success", EX_CALLOUT_SUCCESS }, { "check", EX_CALLOUT_SUCCESS }, { "done", EX_CALLOUT_SUCCESS },
            { "question", EX_CALLOUT_QUESTION }, { "help", EX_CALLOUT_QUESTION }, { "faq", EX_CALLOUT_QUESTION }, { "warning", EX_CALLOUT_WARNING }, { "caution", EX_CALLOUT_WARNING }, { "attention", EX_CALLOUT_WARNING },
            { "failure", EX_CALLOUT_FAILURE }, { "fail", EX_CALLOUT_FAILURE }, { "missing", EX_CALLOUT_FAILURE }, { "danger", EX_CALLOUT_DANGER }, { "error", EX_CALLOUT_DANGER }, { "bug", EX_CALLOUT_BUG },
            { "example", EX_CALLOUT_EXAMPLE }, { "quote", EX_CALLOUT_QUOTE }, { "cite", EX_CALLOUT_QUOTE },
            { "NOTE", EX_CALLOUT_NOTE }, { "WaRnInG", EX_CALLOUT_WARNING }, { "TLDR", EX_CALLOUT_ABSTRACT }, { "Cite", EX_CALLOUT_QUOTE },
        };
        size_t i;
        int known = 0;
        for (i = 0; i < sizeof al / sizeof *al; i++) {
            ExCalloutType t = ex_callout_type(al[i].name, strlen(al[i].name), &known);
            HUNT_SPECF(t == al[i].t && known == 1, "docs/formats.md, Callouts", "callout name %s: type %d known %d, want %d", al[i].name, (int)t, known, (int)al[i].t);
        }
        HUNT_SPEC(ex_callout_type("nonsense", 8, &known) == EX_CALLOUT_NOTE && known == 0, "docs/formats.md, Callouts: an unknown type is note");
        HUNT_CHECK(ex_callout_type("", 0, &known) == EX_CALLOUT_NOTE);
        HUNT_CHECK(ex_callout_type("tip", 3, NULL) == EX_CALLOUT_TIP);                       /* known may be NULL */
        HUNT_CHECK(ex_callout_type("tipx", 3, &known) == EX_CALLOUT_TIP);                    /* the length counts, not the NUL */
        HUNT_CHECK(ex_callout_type("ti", 2, &known) == EX_CALLOUT_NOTE && known == 0);
    }
}

/* ---- B. the documented rules ---- */

static size_t count_kind(const char *text, ExMdKind k, ExMdSpan *first) {
    ExMd *m = ex_md_create();
    Flat f = {0};
    size_t i, n = 0, len = strlen(text);
    ex_md_tokenize(m, text, len);
    flatten(m, len, &f);
    for (i = 0; i < f.n; i++) if (f.v[i].kind == k) { if (n == 0 && first) *first = f.v[i]; n++; }
    free(f.v);
    ex_md_free(m);
    return n;
}

#define RULE(text, kind, want, ref) do { size_t got_ = count_kind((text), (kind), NULL); hunt_.checks++; \
    if (got_ != (size_t)(want)) { hunt_fail_("SPEC", __FILE__, __LINE__, "count of " #kind " in the note below", "documented in %s", (ref)); hunt_note_("note %s: %lu x %s, want %d", #text, (unsigned long)got_, ex_md_kind_name(kind), (int)(want)); } } while (0)

static void test_rules(void) {
    const char *D = "docs/formats.md, Markdown tokens: ";
    char ref[200];
    ExMdSpan sp;

    hunt_step("rules: tags");
    snprintf(ref, sizeof ref, "%stags", D);
    RULE("#tag", EX_MD_TAG, 1, ref);
    RULE("word #tag word", EX_MD_TAG, 1, ref);
    RULE("(#tag)", EX_MD_TAG, 1, ref);
    RULE("[#tag]", EX_MD_TAG, 1, ref);
    RULE("#a/b-c_d", EX_MD_TAG, 1, ref);
    RULE("#123", EX_MD_TAG, 0, ref);
    RULE("#2024-01", EX_MD_TAG, 1, ref);                  /* contains a character that is not a digit */
    RULE("#1a", EX_MD_TAG, 1, ref);
    RULE("a#b", EX_MD_TAG, 0, ref);
    RULE("http://x.y/z#frag", EX_MD_TAG, 0, ref);
    RULE("`#tag`", EX_MD_TAG, 0, ref);
    RULE("```\n#tag\n```", EX_MD_TAG, 0, ref);
    RULE("# not a tag\n", EX_MD_TAG, 0, ref);
    RULE("#a #b #c", EX_MD_TAG, 3, ref);
    RULE("#a\n#b", EX_MD_TAG, 2, ref);
    RULE("##tag", EX_MD_TAG, 0, ref);                     /* a second # is not a tag start */
    RULE("#-", EX_MD_TAG, 1, ref);                        /* the rule: letters, digits, _ - / and a character that is not a digit; "-" is one */
    RULE("#\xC3\xA9t\xC3\xA9", EX_MD_TAG, 1, ref);        /* letters outside ASCII are letters */
    if (count_kind("see #tag.", EX_MD_TAG, &sp) == 1) HUNT_SUSPECT(sp.end - sp.start == 4, "a tag stops before the full stop that ends the sentence");
    if (count_kind("#a/b", EX_MD_TAG, &sp) == 1) HUNT_CHECK(sp.start == 0 && sp.end == 4);

    hunt_step("rules: headings");
    RULE("# a", EX_MD_HEADING, 1, "CommonMark, ATX headings");
    RULE("###### a", EX_MD_HEADING, 1, "docs/formats.md: heading (level 1 to 6)");
    RULE("####### a", EX_MD_HEADING, 0, "docs/formats.md: heading (level 1 to 6)");
    RULE("#a", EX_MD_HEADING, 0, "CommonMark: a heading needs a space after the #");
    RULE("a\n=", EX_MD_HEADING, 1, "docs/formats.md: setext underlines");
    RULE("a\n---", EX_MD_HEADING, 1, "docs/formats.md: setext underlines");
    RULE("    # code", EX_MD_HEADING, 0, "CommonMark: four spaces make code");
    RULE("# a #\n", EX_MD_HEADING, 1, "CommonMark: closing sequence");
    if (count_kind("###### six", EX_MD_HEADING, &sp) == 1) HUNT_CHECK(sp.level == 6);
    if (count_kind("## two", EX_MD_HEADING, &sp) == 1) HUNT_CHECK(sp.level == 2);

    hunt_step("rules: block ids");
    snprintf(ref, sizeof ref, "%sblock ids", D);
    RULE("text ^abc", EX_MD_BLOCK_ID, 1, ref);
    RULE("text^abc", EX_MD_BLOCK_ID, 0, ref);
    RULE("^abc", EX_MD_BLOCK_ID, 1, ref);
    RULE("text ^abc more", EX_MD_BLOCK_ID, 0, ref);
    RULE("text ^a-b-1", EX_MD_BLOCK_ID, 1, ref);
    RULE("text ^a_b", EX_MD_BLOCK_ID, 0, ref);
    RULE("text ^", EX_MD_BLOCK_ID, 0, ref);
    RULE("- item ^id1\n- other", EX_MD_BLOCK_ID, 1, ref);
    RULE("`code ^id`", EX_MD_BLOCK_ID, 0, ref);

    hunt_step("rules: inline math");
    snprintf(ref, sizeof ref, "%sinline math", D);
    RULE("$a$", EX_MD_MATH_INLINE, 1, ref);
    RULE("$ a$", EX_MD_MATH_INLINE, 0, ref);
    RULE("$a $", EX_MD_MATH_INLINE, 0, ref);
    RULE("$a$1", EX_MD_MATH_INLINE, 0, ref);
    RULE("it costs $5 and $6 today", EX_MD_MATH_INLINE, 0, ref);
    RULE("a $$x$$ b", EX_MD_MATH_INLINE, 1, ref);          /* inside a paragraph; on a line of its own, $$x$$ is a math block (next rule) */
    RULE("$$x$$", EX_MD_MATH_BLOCK, 1, "docs/formats.md: math_block");
    RULE("$a$ and $b$", EX_MD_MATH_INLINE, 2, ref);
    RULE("`$a$`", EX_MD_MATH_INLINE, 0, ref);
    RULE("$a\n\nb$", EX_MD_MATH_INLINE, 0, "CommonMark: nothing crosses a blank line");
    RULE("$$\nx^2\n$$", EX_MD_MATH_BLOCK, 1, "docs/formats.md: math_block");
    /* an unclosed $$ is a math block to the end of the note: nothing documents the other choice (docs/ISSUES.md) */

    hunt_step("rules: lists and tables");
    snprintf(ref, sizeof ref, "%slists", D);
    RULE("para\n2. x", EX_MD_LIST, 0, ref);              /* an ordered list that interrupts a paragraph must start at 1 */
    RULE("para\n1. x", EX_MD_LIST, 1, ref);
    RULE("para\n- x", EX_MD_LIST, 1, ref);
    RULE("2. x", EX_MD_LIST, 1, ref);
    RULE("- a\n- b\n\n- c", EX_MD_LIST, 1, "CommonMark: a loose list is one list");
    RULE("- a\n  - b\n    - c", EX_MD_LIST_ITEM, 3, ref);
    RULE("- [ ] a", EX_MD_TASK, 1, "docs/formats.md: task");
    RULE("- [x] a", EX_MD_TASK, 1, "docs/formats.md: task");
    RULE("- [-] a", EX_MD_TASK, 1, "docs/formats.md: task (any other character)");
    RULE("- [] a", EX_MD_TASK, 0, "a task needs a character between the brackets");
    RULE("[ ] a", EX_MD_TASK, 0, "a task is a list item");
    if (count_kind("- [x] a", EX_MD_TASK, &sp) == 1) HUNT_CHECK(sp.flags == 1);
    if (count_kind("- [ ] a", EX_MD_TASK, &sp) == 1) HUNT_CHECK(sp.flags == 0);
    if (count_kind("- [-] a", EX_MD_TASK, &sp) == 1) HUNT_CHECK(sp.flags == 2);
    RULE("|a|b|\n|-|-|\n|1|2|", EX_MD_TABLE, 1, "docs/formats.md: table");
    RULE("|a|b|\n|-|\n|1|2|", EX_MD_TABLE, 0, "docs/formats.md: a table needs a delimiter row with as many columns as the header");
    RULE("|a|b|\n|-|-|-|\n", EX_MD_TABLE, 0, "docs/formats.md: as many columns as the header");
    RULE("|a|b|\n|-|-|", EX_MD_TABLE_ROW, 2, "docs/formats.md: table_row (header and delimiter rows)");
    RULE("|a\\|b|c|\n|-|-|\n", EX_MD_TABLE_CELL, 4, "an escaped pipe does not split a cell");
    RULE("a|b\n-|-\n1|2", EX_MD_TABLE, 1, "tables without outer pipes");
    if (count_kind("|a|b|\n|:-|-:|\n", EX_MD_TABLE_CELL, &sp) >= 2) HUNT_CHECK(sp.kind == EX_MD_TABLE_CELL);

    hunt_step("rules: links and wikilinks");
    snprintf(ref, sizeof ref, "%slinks do not cross a blank line, a wikilink does not cross a line break", D);
    RULE("[[a]]", EX_MD_WIKILINK, 1, ref);
    RULE("[[a|b]]", EX_MD_WIKILINK, 1, ref);
    RULE("[[a#h]]", EX_MD_LINK_HEADING, 1, ref);
    RULE("[[a#^blk]]", EX_MD_LINK_BLOCK, 1, ref);
    RULE("[[a\nb]]", EX_MD_WIKILINK, 0, ref);
    RULE("[[a]", EX_MD_WIKILINK, 0, ref);
    RULE("![[a]]", EX_MD_EMBED, 1, ref);
    RULE("![[a]]", EX_MD_WIKILINK, 0, ref);
    RULE("[a](b)", EX_MD_LINK, 1, ref);
    RULE("[a\nb](c)", EX_MD_LINK, 1, ref);
    RULE("[a\n\nb](c)", EX_MD_LINK, 0, ref);
    RULE("![a](b.png)", EX_MD_IMAGE, 1, ref);
    RULE("[a](<b c.md>)", EX_MD_LINK, 1, "CommonMark: angle-bracket destinations");
    RULE("`[[a]]`", EX_MD_WIKILINK, 0, "links inside code are text");
    RULE("```\n[[a]]\n```", EX_MD_WIKILINK, 0, "links inside code are text");
    RULE("\\[[a]]", EX_MD_WIKILINK, 0, "a backslash escapes the first bracket");
    RULE("[[a]] [[b]] [[c]]", EX_MD_WIKILINK, 3, ref);
    RULE("[[a|b|c]]", EX_MD_WIKILINK, 1, ref);
    RULE("<https://example.com>", EX_MD_AUTOLINK, 1, "docs/formats.md: autolink");
    RULE("[^1]", EX_MD_FOOTNOTE_REF, 1, "docs/formats.md: footnote_ref");
    RULE("[^1]: text", EX_MD_FOOTNOTE_DEF, 1, "docs/formats.md: footnote_def");
    RULE("a^[inline note]", EX_MD_INLINE_FOOTNOTE, 1, "docs/formats.md: inline_footnote");
    if (count_kind("[[Page#Heading]]", EX_MD_LINK_TARGET, &sp) == 1) HUNT_CHECK(sp.end - sp.start == 4);

    hunt_step("rules: callouts, comments, code and front matter");
    RULE("> [!note]\n> x", EX_MD_CALLOUT, 1, "docs/formats.md: Callouts");
    RULE("> [!note] Title\n> x", EX_MD_CALLOUT_TITLE, 1, "docs/formats.md: callout_title");
    RULE("> [!note]\n> x", EX_MD_CALLOUT_TYPE, 1, "docs/formats.md: callout_type");
    RULE("> not a callout\n> x", EX_MD_CALLOUT, 0, "docs/formats.md: Callouts");
    RULE("> [!note]\n> > [!tip]\n> > x", EX_MD_CALLOUT, 2, "docs/formats.md: Callouts nest");
    if (count_kind("> [!tip]- t\n> x", EX_MD_CALLOUT, &sp) == 1) HUNT_CHECK(sp.level == EX_CALLOUT_TIP && sp.flags == EX_CALLOUT_FOLD_CLOSED);
    if (count_kind("> [!tip]+ t\n> x", EX_MD_CALLOUT, &sp) == 1) HUNT_CHECK(sp.flags == EX_CALLOUT_FOLD_OPEN);
    if (count_kind("> [!WARNING] t\n> x", EX_MD_CALLOUT, &sp) == 1) HUNT_CHECK(sp.level == EX_CALLOUT_WARNING && sp.flags == EX_CALLOUT_NOT_FOLDABLE);
    if (count_kind("> [!nonsense]\n> x", EX_MD_CALLOUT, &sp) == 1) HUNT_CHECK(sp.level == EX_CALLOUT_NOTE);
    RULE("%%a%%", EX_MD_COMMENT_INLINE, 1, "docs/formats.md: comment_inline");
    RULE("%%\na\n%%", EX_MD_COMMENT_BLOCK, 1, "docs/formats.md: comment_block");
    RULE("==a==", EX_MD_HIGHLIGHT, 1, "docs/formats.md: highlight");
    RULE("~~a~~", EX_MD_STRIKE, 1, "docs/formats.md: strike");
    RULE("**a**", EX_MD_STRONG, 1, "docs/formats.md: strong");
    RULE("\\*a\\*", EX_MD_EMPHASIS, 0, "an escaped star is not emphasis");
    RULE("\\*a\\*", EX_MD_ESCAPE, 2, "docs/formats.md: escape");
    RULE("```js\ncode\n```", EX_MD_CODE_BLOCK, 1, "docs/formats.md: code_block");
    RULE("```js\ncode\n```", EX_MD_CODE_INFO, 1, "docs/formats.md: code_info");
    RULE("    code", EX_MD_CODE_BLOCK, 1, "docs/formats.md: code_block flags 1 indented");
    RULE("```\nnever closed", EX_MD_CODE_BLOCK, 1, "CommonMark: an unclosed fence runs to the end");
    RULE("~~~\ncode\n~~~", EX_MD_CODE_BLOCK, 1, "CommonMark: tilde fences");
    RULE("````\n```\ninner\n```\n````", EX_MD_CODE_BLOCK, 1, "CommonMark: a longer fence holds a shorter one");
    RULE("---\na: 1\n---\nbody", EX_MD_FRONTMATTER, 1, "docs/formats.md: frontmatter");
    RULE("---\na: 1\n...\nbody", EX_MD_FRONTMATTER, 1, "docs/formats.md: frontmatter (ended by --- or ...)");
    RULE("\n---\na: 1\n---\nbody", EX_MD_FRONTMATTER, 0, "docs/formats.md: a --- block on the first line");
    RULE("text\n---\na: 1\n---\n", EX_MD_FRONTMATTER, 0, "docs/formats.md: a --- block on the first line");
    RULE("***", EX_MD_HR, 1, "CommonMark");
    RULE("a\\\nb", EX_MD_HARD_BREAK, 1, "CommonMark: a backslash at the end of a line is a hard break");
    RULE("a  \nb", EX_MD_HARD_BREAK, 1, "docs/formats.md: hard_break");
    RULE("<div>x</div>", EX_MD_HTML_BLOCK, 1, "docs/formats.md: html_block");
    RULE("a <b>x</b> c", EX_MD_HTML_INLINE, 2, "docs/formats.md: html_inline");
    RULE("<!-- c -->", EX_MD_HTML_BLOCK, 1, "CommonMark: an HTML comment is an HTML block");
}

/* ---- C. every single-byte edit ---- */

static const char *const SAMPLES[] = {
    "---\ntitle: x\ntags: [a]\n---\n# Head\n\nA *b* **c** `d` [[e]] #tag ^id\n\n- [ ] t\n  - n\n\n> [!note]- T\n> body\n\n```js\ncode\n```\n\n|a|b|\n|-|-|\n|1|2|\n",
    "para one\nstill one\n\n1. a\n2. b\n\n$$\nx\n$$\n\n%%c%%\n\n[^1]: f\n\n<div>\nh\n</div>\n\n***\n\nSetext\n===\n",
    "> a\n> > b\n> c\n\n- x\n\n\n- y\n    code?\n\n    indented\n\n~~~\nt\n~~~\n![[i.png]] [l](u 't') <http://a> $m$ ==h== ~~s~~\n",
};
static const char EDIT_CHARS[] = "\n #*_`>-[]()|~=$%^!<:+\\1.a\t";

static void check_update_equals_full(ExMd *inc, const char *old_text, size_t old_len, size_t off, size_t removed, const char *ins, size_t ins_len) {
    size_t new_len = old_len - removed + ins_len;
    char *nt = (char *)malloc(new_len + 1);
    ExMd *full = ex_md_create();
    Flat a = {0}, b = {0};
    size_t i;
    ExMdRange rg;
    memcpy(nt, old_text, off);
    memcpy(nt + off, ins, ins_len);
    memcpy(nt + off + ins_len, old_text + off + removed, old_len - off - removed);
    nt[new_len] = 0;
    hunt_set_input(nt, new_len, "the text after the edit");
    rg = ex_md_update(inc, nt, new_len, off, removed, ins_len);
    ex_md_tokenize(full, nt, new_len);
    flatten(inc, new_len, &a); flatten(full, new_len, &b);
    if (a.n != b.n) HUNT_CHECKF(0, "after an edit at %lu (-%lu +%lu) the update has %lu spans, a full tokenization %lu", (unsigned long)off, (unsigned long)removed, (unsigned long)ins_len, (unsigned long)a.n, (unsigned long)b.n);
    else for (i = 0; i < a.n; i++) if (!same_span(&a.v[i], &b.v[i])) { HUNT_CHECKF(0, "after an edit at %lu (-%lu +%lu) span %lu differs: update %s, full %s", (unsigned long)off, (unsigned long)removed, (unsigned long)ins_len, (unsigned long)i, describe(&a.v[i]), describe(&b.v[i])); break; }
    HUNT_CHECK(rg.start <= rg.end && rg.end <= new_len);
    HUNT_CHECK(ex_md_block_count(inc) == ex_md_block_count(full));
    free(a.v); free(b.v);
    ex_md_free(full);
    hunt_clear_input();
    free(nt);
}

static void test_every_edit(void) {
    size_t s;
    for (s = 0; s < sizeof SAMPLES / sizeof *SAMPLES; s++) {
        const char *text = SAMPLES[s];
        size_t len = strlen(text), off, c;
        for (off = 0; off <= len; off++) {
            hunt_stepf("every edit: sample %lu offset %lu", (unsigned long)s, (unsigned long)off);
            for (c = 0; c < sizeof EDIT_CHARS - 1; c += (hunt_.scale_den > 1 ? 4 : 1)) {
                ExMd *inc = ex_md_create();
                char ch = EDIT_CHARS[c];
                ex_md_tokenize(inc, text, len);
                check_update_equals_full(inc, text, len, off, 0, &ch, 1);                           /* insert */
                if (off < len) {
                    ex_md_free(inc); inc = ex_md_create(); ex_md_tokenize(inc, text, len);
                    check_update_equals_full(inc, text, len, off, 1, &ch, 1);                       /* replace */
                }
                ex_md_free(inc);
            }
            if (off < len) {
                ExMd *inc = ex_md_create();
                ex_md_tokenize(inc, text, len);
                check_update_equals_full(inc, text, len, off, 1, "", 0);                            /* delete */
                ex_md_free(inc);
                inc = ex_md_create(); ex_md_tokenize(inc, text, len);
                check_update_equals_full(inc, text, len, off, len - off > 7 ? 7 : len - off, "", 0);  /* delete a run */
                ex_md_free(inc);
            }
        }
    }
    /* a chain of edits on one tokenizer (the real use: the editor keeps one) */
    {
        long rounds = hunt_iters(400), r;
        ExMd *inc = ex_md_create();
        char *cur = hunt_strdup(SAMPLES[0]);
        size_t len = strlen(cur);
        ex_md_tokenize(inc, cur, len);
        for (r = 0; r < rounds; r++) {
            size_t off = hunt_rnd_size(len + 1), rem = hunt_chance(40) ? hunt_rnd_size(len - off + 1 > 5 ? 5 : len - off + 1) : 0;
            char ins[24];
            size_t il = hunt_chance(15) ? 0 : hunt_random_valid_text(ins, sizeof ins, 3), nl;
            char *nt;
            if (len > 3000) { rem = len - off > 200 ? 200 : len - off; il = 0; }
            hunt_stepf("edit chain %ld", r);
            nl = len - rem + il;
            nt = (char *)malloc(nl + 1);
            memcpy(nt, cur, off); memcpy(nt + off, ins, il); memcpy(nt + off + il, cur + off + rem, len - off - rem); nt[nl] = 0;
            {
                ExMd *full = ex_md_create();
                Flat a = {0}, b = {0};
                size_t i;
                ex_md_update(inc, nt, nl, off, rem, il);
                ex_md_tokenize(full, nt, nl);
                flatten(inc, nl, &a); flatten(full, nl, &b);
                HUNT_CHECKF(a.n == b.n, "edit chain %ld: %lu spans against %lu", r, (unsigned long)a.n, (unsigned long)b.n);
                for (i = 0; i < a.n && i < b.n; i++) if (!same_span(&a.v[i], &b.v[i])) { HUNT_CHECKF(0, "edit chain %ld: span %lu differs: %s vs %s", r, (unsigned long)i, describe(&a.v[i]), describe(&b.v[i])); break; }
                free(a.v); free(b.v);
                ex_md_free(full);
            }
            free(cur); cur = nt; len = nl;
        }
        free(cur);
        ex_md_free(inc);
    }
}

/* ---- D. text that is slow to tokenize ---- */

typedef struct Hostile { const char *name; char *text; size_t len; } Hostile;

static char *repeat(const char *unit, size_t times, size_t *len) {
    size_t u = strlen(unit), i;
    char *s = (char *)malloc(u * times + 1);
    for (i = 0; i < times; i++) memcpy(s + i * u, unit, u);
    s[u * times] = 0;
    *len = u * times;
    return s;
}

static void time_one(const char *name, const char *text, size_t len, double limit_s) {
    ExMd *m = ex_md_create();
    clock_t t0;
    double s;
    hunt_stepf("hostile: %s (%lu bytes)", name, (unsigned long)len);
    t0 = clock();
    ex_md_tokenize(m, text, len);
    s = hunt_cpu_since(t0);
    if (s > hunt_time_limit(limit_s)) { HUNT_SUSPECT(0, "tokenizing took longer than the limit: the cost grows faster than the size of the note (a note can be megabytes; the editor tokenizes on the UI thread)"); hunt_note_("%s: %.2f s for %lu bytes (limit %.1f s)", name, s, (unsigned long)len, hunt_time_limit(limit_s)); }
    check_invariants(m, text, len, name);
    /* and an edit in the middle, the way typing does it */
    if (len > 10) {
        char *nt = (char *)malloc(len + 2);
        memcpy(nt, text, len / 2); nt[len / 2] = 'x'; memcpy(nt + len / 2 + 1, text + len / 2, len - len / 2); nt[len + 1] = 0;
        t0 = clock();
        ex_md_update(m, nt, len + 1, len / 2, 0, 1);
        s = hunt_cpu_since(t0);
        if (s > hunt_time_limit(limit_s)) { HUNT_SUSPECT(0, "one typed character took longer than the limit to update"); hunt_note_("%s: %.2f s for one character at the middle of %lu bytes", name, s, (unsigned long)len); }
        check_invariants(m, nt, len + 1, name);
        free(nt);
    }
    ex_md_free(m);
}

static void test_hostile(void) {
    size_t len, i, n;
    char *t;
    struct { const char *name; const char *unit; size_t times; } rep[] = {
        { "stars", "*", 80000 }, { "underscores", "_", 80000 }, { "star-letter pairs", "*a", 40000 }, { "open brackets", "[", 80000 }, { "wikilink openers", "[[", 40000 },
        { "image openers", "![", 40000 }, { "link openers with text", "[a](", 20000 }, { "backticks", "`", 80000 }, { "backtick runs of one", "`a", 40000 }, { "less-thans", "<", 80000 }, { "tag openers", "<a ", 30000 },
        { "quote markers on one line", "> ", 40000 }, { "list markers on one line", "- ", 40000 }, { "pipes", "|", 80000 }, { "dollars", "$", 80000 }, { "dollar-letter", "$a ", 30000 },
        { "comment markers", "%%", 40000 }, { "caret brackets", "^[", 30000 }, { "backslashes", "\\", 80000 }, { "hashes", "# ", 40000 }, { "equal signs", "=", 80000 }, { "tildes", "~", 80000 },
        { "footnote refs", "[^a]", 20000 }, { "one very long line", "word ", 100000 }, { "empty lines", "\n", 200000 }, { "short lines", "x\n", 100000 }, { "html comment openers", "<!--", 30000 },
        { "closing html", "</div>", 30000 }, { "ampersands", "&#x41", 30000 }, { "autolink starts", "<http://a", 20000 }, { "bare urls", "http://a.b/c ", 20000 }, { "block ids", " ^a", 40000 },
        { "setext candidates", "a\n=\n", 30000 }, { "hr candidates", "- - -\n", 20000 }, { "table row", "|a|b|\n", 20000 }, { "ordered markers", "1. ", 30000 }, { "carets and brackets", "[^", 30000 },
    };
    for (i = 0; i < sizeof rep / sizeof *rep; i++) {
        t = repeat(rep[i].unit, (size_t)((double)rep[i].times * (hunt_.scale_den > 1 ? 0.3 : 1.0)) + 1, &len);
        time_one(rep[i].name, t, len, 2.0);
        free(t);
    }

    /* nesting: each line is indented one more, or quoted one more */
    {
        size_t depth = 1500;
        char *buf = (char *)malloc(depth * depth / 2 + depth * 8 + 64), *p;
        p = buf;
        for (i = 0; i < depth; i++) { size_t k; for (k = 0; k < i && k < 600; k++) *p++ = ' '; p += sprintf(p, "- item %lu\n", (unsigned long)i); }
        *p = 0;
        time_one("deep list nesting", buf, (size_t)(p - buf), 3.0);
        p = buf;
        for (i = 0; i < depth; i++) { size_t k; for (k = 0; k <= i % 400; k++) { *p++ = '>'; *p++ = ' '; } p += sprintf(p, "q%lu\n", (unsigned long)i); }
        *p = 0;
        time_one("deep quote nesting", buf, (size_t)(p - buf), 3.0);
        p = buf;
        for (i = 0; i < depth; i++) p += sprintf(p, "> [!note]\n");
        *p = 0;
        time_one("many callouts in one quote", buf, (size_t)(p - buf), 3.0);
        free(buf);
    }
    /* a wide and tall table */
    {
        size_t cols = 400, rows = 400, r, c;
        char *buf = (char *)malloc(rows * cols * 4 + 4 * cols + 64), *p = buf;
        for (r = 0; r < rows + 2; r++) { for (c = 0; c < cols; c++) { *p++ = '|'; if (r == 1) { *p++ = '-'; } else { *p++ = 'x'; } } *p++ = '|'; *p++ = '\n'; }
        *p = 0;
        time_one("a table of 400 by 400 cells", buf, (size_t)(p - buf), 3.0);
        free(buf);
    }
    /* long unclosed constructs followed by plenty of text */
    {
        const char *openers[] = { "[[", "![[", "**", "`", "$$", "```\n", "<div>", "%%", "[a](", "==", "~~" };
        for (i = 0; i < sizeof openers / sizeof *openers; i++) {
            char unit[64];
            snprintf(unit, sizeof unit, "%s", openers[i]);
            t = repeat("text and more text and some more text\n", 3000, &n);
            {
                char *w = (char *)malloc(n + strlen(unit) + 1);
                memcpy(w, unit, strlen(unit)); memcpy(w + strlen(unit), t, n + 1);
                time_one(openers[i], w, n + strlen(unit), 2.0);
                free(w);
            }
            free(t);
        }
    }
    /* many blocks, then one edit at the top: everything after moves, nothing is read again */
    {
        size_t blocks = 40000;
        char *buf = (char *)malloc(blocks * 12 + 16), *p = buf;
        ExMd *m = ex_md_create();
        clock_t t0;
        double s;
        for (i = 0; i < blocks; i++) p += sprintf(p, "para %lu\n\n", (unsigned long)i);
        *p = 0;
        len = (size_t)(p - buf);
        ex_md_tokenize(m, buf, len);
        {
            char *nt = (char *)malloc(len + 3);
            nt[0] = 'Z'; memcpy(nt + 1, buf, len + 1);
            hunt_step("edit at the top of 40000 blocks");
            t0 = clock();
            ex_md_update(m, nt, len + 1, 0, 0, 1);
            s = hunt_cpu_since(t0);
            HUNT_SUSPECT(s < hunt_time_limit(0.25), "one character at the top of a long note took more than 250 ms: the update moves every later span one by one on the UI thread");
            if (s >= hunt_time_limit(0.25)) hunt_note_("%.3f s, %lu blocks retokenized", s, (unsigned long)ex_md_last_retokenized(m));
            HUNT_SUSPECT(ex_md_last_retokenized(m) <= 4, "one typed character should re-read only the blocks around it");
            free(nt);
        }
        ex_md_free(m);
        free(buf);
    }
}

/* ---- E. LF and CRLF ---- */

static void kinds_of(const char *text, size_t len, int **out, size_t *n) {
    ExMd *m = ex_md_create();
    Flat f = {0};
    size_t i, k = 0;
    int *v;
    ex_md_tokenize(m, text, len);
    flatten(m, len, &f);
    v = (int *)malloc((f.n + 1) * sizeof *v);
    for (i = 0; i < f.n; i++) v[k++] = (int)f.v[i].kind * 1000 + f.v[i].level * 4 + f.v[i].flags;
    *out = v; *n = k;
    free(f.v);
    ex_md_free(m);
}

static void test_crlf(void) {
    size_t s;
    for (s = 0; s < sizeof SAMPLES / sizeof *SAMPLES; s++) {
        const char *lf = SAMPLES[s];
        size_t len = strlen(lf), i, j = 0, n1, n2;
        char *crlf = (char *)malloc(len * 2 + 1);
        int *k1, *k2;
        for (i = 0; i < len; i++) { if (lf[i] == '\n') crlf[j++] = '\r'; crlf[j++] = lf[i]; }
        crlf[j] = 0;
        hunt_stepf("crlf sample %lu", (unsigned long)s);
        kinds_of(lf, len, &k1, &n1);
        kinds_of(crlf, j, &k2, &n2);
        HUNT_SUSPECT(n1 == n2, "the same note with CRLF line ends should give the same number of spans");
        if (n1 != n2) hunt_note_("sample %lu: %lu spans with LF, %lu with CRLF", (unsigned long)s, (unsigned long)n1, (unsigned long)n2);
        for (i = 0; i < n1 && i < n2; i++) if (k1[i] != k2[i]) { HUNT_SUSPECT(0, "the same note with CRLF line ends gives a different span at the same position"); hunt_note_("sample %lu span %lu: %d vs %d", (unsigned long)s, (unsigned long)i, k1[i], k2[i]); break; }
        free(k1); free(k2); free(crlf);
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_markdown", argc, argv);
    test_invariants();
    test_rules();
    test_every_edit();
    test_hostile();
    test_crlf();
    return hunt_end();
}
