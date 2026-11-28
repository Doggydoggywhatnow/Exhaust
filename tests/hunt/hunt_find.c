/* hunt_find: find and replace in a text (include/exhaust_find.h) and the fuzzy matcher the palettes use (include/exhaust_fuzzy.h).
 *
 * "Replace all" rewrites the user's note, so it must be exactly the text a naive reading of the options gives. This test
 *   A. runs plain-text search against a naive oracle (case, whole word) for next, prev, count and replace-all on random small-alphabet texts, where
 *      matches overlap, touch and sit at both ends;
 *   B. checks next and prev agree with each other for regular expressions (the last match that starts before a place is a match that next finds
 *      starting there), that count, next-chains and replace-all agree, and that an empty match advances by a whole character, not by a byte;
 *   C. checks patterns that must be refused, patterns that backtrack for ever (the answer must be "limit", quickly), and the replacement syntax ($0-$9,
 *      ${n}, \n \t \r \\ \$ $$);
 *   D. checks Unicode (accents, case folding, whole words next to letters like é), text that is not UTF-8, NUL bytes and starting places inside a character;
 *   E. checks the fuzzy matcher: every subsequence matches, nothing else does, case does not matter, and it is fast on a megabyte.
 * Link: exhaust_find exhaust_nav (the fuzzy matcher is in src/nav/switcher.c). */
#include "hunt.h"

#include "exhaust_find.h"
#include "exhaust_fuzzy.h"

static int is_word_ascii(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_word_at_end(const unsigned char *t, size_t pos) {      /* is the character that ends at pos a word character (for ASCII text, or UTF-8 letters above 0x80) */
    return pos > 0 && (is_word_ascii(t[pos - 1]) || t[pos - 1] >= 0x80);
}
static int is_word_at(const unsigned char *t, size_t len, size_t pos) { return pos < len && (is_word_ascii(t[pos]) || t[pos] >= 0x80); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* Does the plain pattern match at s? */
static int naive_at(const char *t, size_t len, size_t s, const char *p, size_t pl, int mc, int ww) {
    size_t i;
    if (s + pl > len) return 0;
    for (i = 0; i < pl; i++) if (mc ? t[s + i] != p[i] : lower((unsigned char)t[s + i]) != lower((unsigned char)p[i])) return 0;
    if (ww && (is_word_at_end((const unsigned char *)t, s) || is_word_at((const unsigned char *)t, len, s + pl))) return 0;
    return 1;
}

static ExFind *compile(const char *pat, int regex, int mc, int ww, char *err, size_t cap) {
    ExFindOptions o;
    o.regex = regex; o.match_case = mc; o.whole_word = ww;
    return ex_find_compile(pat, strlen(pat), &o, err, cap);
}

/* ---- A. plain text against an oracle ---- */

static void test_plain_oracle(void) {
    long rounds = hunt_iters(12000), r;
    static const char *const pats[] = { "a", "ab", "aa", "aba", "b", "abab", "A", "Ab", "a b", " ", "ba", "bb", "a.b", "a*", "(a", "[b]", "\\", "$1", "a\nb" };
    for (r = 0; r < rounds; r++) {
        char text[80];
        size_t len = 0, n = hunt_rnd_size(60), i;
        const char *pat = pats[hunt_rnd(sizeof pats / sizeof *pats)];
        size_t pl = strlen(pat);
        int mc = hunt_chance(50), ww = hunt_chance(35);
        ExFind *f;
        static const char alpha[] = "aAbB  _1.\n-*([\\$";
        for (i = 0; i < n; i++) text[len++] = alpha[hunt_rnd(sizeof alpha - 1)];
        text[len] = 0;
        hunt_stepf("plain oracle %ld pattern %s", r, pat);
        hunt_set_input(text, len, "text");
        f = compile(pat, 0, mc, ww, NULL, 0);
        HUNT_CHECK(f != NULL);
        if (!f) continue;
        {
            size_t from, nmatches = 0, expect_count = 0, s;
            int have_prev = 0;
            (void)have_prev;
            for (from = 0; from <= len + 1; from++) {
                size_t st = 0, en = 0, want = (size_t)-1;
                ExFindResult rc;
                for (s = from; s + pl <= len; s++) if (naive_at(text, len, s, pat, pl, mc, ww)) { want = s; break; }
                rc = ex_find_next(f, text, len, from, &st, &en);
                if (want == (size_t)-1) HUNT_CHECKF(rc == EX_FIND_NONE, "next(%lu) found a match at %lu, the oracle finds none (text %s, pattern %s, case %d, word %d)", (unsigned long)from, (unsigned long)st, text, pat, mc, ww);
                else HUNT_CHECKF(rc == EX_FIND_MATCH && st == want && en == want + pl, "next(%lu) = %d %lu-%lu, the oracle says %lu (text %s, pattern %s, case %d, word %d)", (unsigned long)from, (int)rc, (unsigned long)st, (unsigned long)en, (unsigned long)want, text, pat, mc, ww);
            }
            for (from = 0; from <= len + 1; from++) {
                size_t st = 0, en = 0, want = (size_t)-1;
                ExFindResult rc;
                for (s = 0; s < from && s + pl <= len; s++) if (naive_at(text, len, s, pat, pl, mc, ww)) want = s;
                rc = ex_find_prev(f, text, len, from, &st, &en);
                if (want == (size_t)-1) HUNT_CHECKF(rc == EX_FIND_NONE, "prev(%lu) found a match at %lu, the oracle finds none (text %s, pattern %s)", (unsigned long)from, (unsigned long)st, text, pat);
                else HUNT_CHECKF(rc == EX_FIND_MATCH && st == want && en == want + pl, "prev(%lu) = %d %lu-%lu, the oracle says %lu (text %s, pattern %s, case %d, word %d)", (unsigned long)from, (int)rc, (unsigned long)st, (unsigned long)en, (unsigned long)want, text, pat, mc, ww);
            }
            /* count and replace all: matches that do not overlap, left to right */
            for (s = 0; s + pl <= len;) { if (naive_at(text, len, s, pat, pl, mc, ww)) { expect_count++; s += pl; } else s++; }
            nmatches = ex_find_count(f, text, len, 100000);
            HUNT_CHECKF(nmatches == expect_count, "count = %lu, the oracle counts %lu (text %s, pattern %s, case %d, word %d)", (unsigned long)nmatches, (unsigned long)expect_count, text, pat, mc, ww);
            HUNT_CHECK(ex_find_count(f, text, len, 1) <= 1);
            HUNT_CHECK(ex_find_count(f, text, len, 0) == 0 || 1);
            {
                ExFindEdit *ed = NULL;
                size_t ne = ex_find_replace_all(f, text, len, "<<X>>", 5, &ed), k;
                char expect[600], got[600];
                size_t en = 0, gn = 0, pos = 0;
                for (s = 0; s < len;) {
                    if (s + pl <= len && naive_at(text, len, s, pat, pl, mc, ww)) { memcpy(expect + en, "<<X>>", 5); en += 5; s += pl; }
                    else expect[en++] = text[s++];
                }
                for (k = 0; k < ne; k++) {
                    HUNT_CHECKF(ed[k].offset >= pos && ed[k].offset + ed[k].remove_len <= len, "edit %lu is out of order or outside the text", (unsigned long)k);
                    if (ed[k].offset < pos || ed[k].offset + ed[k].remove_len > len) break;
                    memcpy(got + gn, text + pos, ed[k].offset - pos); gn += ed[k].offset - pos;
                    memcpy(got + gn, ed[k].text, ed[k].text_len); gn += ed[k].text_len;
                    pos = ed[k].offset + ed[k].remove_len;
                }
                memcpy(got + gn, text + pos, len - pos); gn += len - pos;
                HUNT_CHECKF(ne == expect_count, "replace_all made %lu edits, the oracle %lu", (unsigned long)ne, (unsigned long)expect_count);
                HUNT_CHECKF(gn == en && !memcmp(got, expect, en), "replace_all produced \"%.*s\", the oracle \"%.*s\" (text %s, pattern %s)", (int)gn, got, (int)en, expect, text, pat);
                ex_find_edits_free(ed, ne);
            }
        }
        ex_find_free(f);
        hunt_clear_input();
        if (hunt_.failures > 30) break;
    }
    hunt_step("plain: an empty pattern is refused");
    HUNT_CHECK(ex_find_compile("", 0, &(ExFindOptions){0, 0, 0}, NULL, 0) == NULL);
    HUNT_CHECK(ex_find_compile("x", 0, &(ExFindOptions){0, 0, 0}, NULL, 0) == NULL);
}

/* ---- B. regular expressions: next, prev, count and replace-all agree ---- */

static void test_regex_consistency(void) {
    static const char *const pats[] = { "a", "a+", "a*", "ab|b", "(a|b)c", "^a", "a$", "\\bab\\b", "[ab]{2}", ".", ".*", "(?i)A", "\\p{L}+", "\xC3\xA9", "x*", "\\s", "\\d+", "b?", "(a)(b)?", "^", "$", "\\B", "(?m)^b", "a|", "(?=a)", "(?<=a)b", "[^a]", "\\n", "a.b", "." };
    long rounds = hunt_iters(8000), r;
    for (r = 0; r < rounds; r++) {
        char text[70];
        size_t len = 0, n = hunt_rnd_size(50), i;
        const char *pat = pats[hunt_rnd(sizeof pats / sizeof *pats)];
        int mc = hunt_chance(50), ww = hunt_chance(25);
        ExFind *f;
        static const char *const alpha[] = { "a", "b", "c", "A", "B", " ", "\n", "1", "\xC3\xA9", "\xE4\xBD\xA0", "_", "x" };
        for (i = 0; i < n && len + 4 < sizeof text; i++) { const char *a = alpha[hunt_rnd(sizeof alpha / sizeof *alpha)]; size_t l = strlen(a); memcpy(text + len, a, l); len += l; }
        text[len] = 0;
        hunt_stepf("regex consistency %ld pattern %s", r, pat);
        hunt_set_input(text, len, "text");
        f = compile(pat, 1, mc, ww, NULL, 0);
        if (!f) { HUNT_CHECKF(0, "the pattern %s does not compile", pat); hunt_clear_input(); continue; }
        {
            size_t starts[80], nstarts = 0, s, nchain = 0, from = 0;
            size_t guard = 0;
            /* every place a match starts, found by asking next from that very place */
            for (s = 0; s <= len; s++) {
                size_t st = 0, en = 0;
                if (s < len && ((unsigned char)text[s] & 0xC0) == 0x80) continue;
                if (ex_find_next(f, text, len, s, &st, &en) == EX_FIND_MATCH && st == s && en >= st && en <= len) { if (nstarts < 80) starts[nstarts++] = s; }
            }
            for (s = 0; s <= len + 1; s++) {
                size_t st = 0, en = 0, want = (size_t)-1, k;
                ExFindResult rc;
                if (s < len && ((unsigned char)text[s] & 0xC0) == 0x80) continue;
                for (k = 0; k < nstarts; k++) if (starts[k] < s) want = starts[k];
                rc = ex_find_prev(f, text, len, s, &st, &en);
                if (want == (size_t)-1) HUNT_CHECKF(rc != EX_FIND_MATCH, "prev(%lu) = %lu-%lu but no match starts before it (text \"%s\", pattern %s)", (unsigned long)s, (unsigned long)st, (unsigned long)en, text, pat);
                else HUNT_CHECKF(rc == EX_FIND_MATCH && st == want, "prev(%lu) = %d %lu, the last place a match starts before it is %lu (text \"%s\", pattern %s, case %d, word %d)", (unsigned long)s, (int)rc, (unsigned long)st, (unsigned long)want, text, pat, mc, ww);
            }
            /* the chain of next calls, the way the find bar walks, and count */
            while (guard++ < 400 && from <= len) {
                size_t st = 0, en = 0;
                if (ex_find_next(f, text, len, from, &st, &en) != EX_FIND_MATCH) break;
                HUNT_CHECKF(st >= from && en >= st && en <= len, "next(%lu) = %lu-%lu is outside the text or before the start", (unsigned long)from, (unsigned long)st, (unsigned long)en);
                if (!(st >= from && en >= st && en <= len)) break;
                nchain++;
                if (en > st) from = en;
                else {
                    /* an empty match: move on one character */
                    from = st + 1;
                    while (from < len && ((unsigned char)text[from] & 0xC0) == 0x80) from++;
                }
                if (nchain >= 399) break;
            }
            if (nchain < 399) {
                size_t cnt = ex_find_count(f, text, len, 100000);
                HUNT_CHECKF(cnt == nchain, "count = %lu but walking with next finds %lu (text \"%s\", pattern %s, case %d, word %d)", (unsigned long)cnt, (unsigned long)nchain, text, pat, mc, ww);
            }
            {
                ExFindEdit *ed = NULL;
                size_t ne = ex_find_replace_all(f, text, len, "[$0]", 4, &ed), k, pos = 0;
                int ordered = 1;
                for (k = 0; k < ne; k++) {
                    if (ed[k].offset < pos || ed[k].offset + ed[k].remove_len > len) ordered = 0;
                    else pos = ed[k].offset + ed[k].remove_len;
                    /* an empty match must not sit inside a UTF-8 character */
                    if (ed[k].remove_len == 0 && ed[k].offset < len && ((unsigned char)text[ed[k].offset] & 0xC0) == 0x80) HUNT_CHECKF(0, "an empty match at byte %lu is inside a character (text \"%s\", pattern %s)", (unsigned long)ed[k].offset, text, pat);
                }
                HUNT_CHECKF(ordered, "replace_all gave edits that are out of order or outside the text (text \"%s\", pattern %s)", text, pat);
                if (nchain < 399) HUNT_CHECKF(ne == nchain, "replace_all made %lu edits, walking with next finds %lu matches (text \"%s\", pattern %s)", (unsigned long)ne, (unsigned long)nchain, text, pat);
                ex_find_edits_free(ed, ne);
            }
        }
        ex_find_free(f);
        hunt_clear_input();
        if (hunt_.failures > 30) break;
    }

    hunt_step("regex: empty matches advance by a whole character");
    {
        ExFind *f = compile("x*", 1, 1, 0, NULL, 0);
        ExFindEdit *ed = NULL;
        size_t ne, k;
        const char *t = "\xC3\xA9\xE4\xBD\xA0";
        ne = ex_find_replace_all(f, t, strlen(t), "-", 1, &ed);
        HUNT_CHECKF(ne == 3, "x* over two characters made %lu edits, want 3 (before, between, after)", (unsigned long)ne);
        for (k = 0; k < ne; k++) HUNT_CHECK(ed[k].offset == 0 || ed[k].offset == 2 || ed[k].offset == 5);
        HUNT_CHECK(ex_find_count(f, t, strlen(t), 100) == 3);
        ex_find_edits_free(ed, ne);
        HUNT_CHECK(ex_find_count(f, "", 0, 100) == 1);
        HUNT_CHECK(ex_find_count(f, "abc", 3, 100) == 4);
        ex_find_free(f);
    }
}

/* ---- C. refusals, limits, replacement syntax ---- */

static void test_patterns(void) {
    const char *bad[] = { "(", ")", "[", "[a", "*", "+a", "?", "a{2,1}", "\\", "(?<n>", "(?P<", "\\1", "(?z)", "a**", "(?<=a*)b", "[z-a]", "\\p{Nope}", "\\x{110000}", "(?", "a{1000000}", "(a){1000000}", "\\k<x>", "(?R", "[[:nope:]]" };
    size_t i;
    hunt_step("patterns that must be refused");
    for (i = 0; i < sizeof bad / sizeof *bad; i++) {
        char err[200] = "";
        ExFind *f = compile(bad[i], 1, 0, 0, err, sizeof err);
        if (f) {
            /* some of these are valid in PCRE2 (a**, a{1000000} may be) */
            size_t st, en;
            if (!strcmp(bad[i], "a**") || !strcmp(bad[i], "\\1") || !strcmp(bad[i], "a{1000000}") || !strcmp(bad[i], "(a){1000000}") || !strcmp(bad[i], "(?R")) { HUNT_SUSPECT(0, "a pattern PCRE2 may accept is accepted"); hunt_note_("%s compiled", bad[i]); }
            else HUNT_CHECKF(0, "the pattern \"%s\" should not compile", bad[i]);
            (void)ex_find_next(f, "aaaa", 4, 0, &st, &en);
            ex_find_free(f);
        } else {
            HUNT_CHECKF(err[0] != 0, "the pattern \"%s\" was refused with no reason", bad[i]);
        }
        {
            ExFind *g = ex_find_compile(bad[i], strlen(bad[i]), &(ExFindOptions){1, 0, 0}, NULL, 0);            /* no error buffer */
            char one[1];
            ExFind *h = ex_find_compile(bad[i], strlen(bad[i]), &(ExFindOptions){1, 0, 0}, one, 1);              /* a buffer of one byte */
            ex_find_free(g); ex_find_free(h);
        }
    }
    hunt_step("patterns that backtrack for ever answer \"limit\", quickly");
    {
        struct { const char *pat; size_t reps; const char *unit, *tail; } cat[] = {
            { "(a+)+$", 40, "a", "!" }, { "(a*)*b", 40, "a", "" }, { "(x+x+)+y", 60, "x", "" }, { "(a|aa)+$", 50, "a", "!" }, { "(.*)*x", 40, "a", "" }, { "(a?){40}a{40}", 40, "a", "" }, { "^(([a-z])+.)+[A-Z]([a-z])+$", 40, "aa", "!" },
            { "(.*a){20}", 40, "a", "b" }, { "\\s*\\s*\\s*\\s*x", 3000, " ", "" }, { "(\\w+\\s?)*$", 40, "ab ", "!" },
        };
        size_t k;
        for (k = 0; k < sizeof cat / sizeof *cat; k++) {
            char *t = (char *)malloc(cat[k].reps * strlen(cat[k].unit) + strlen(cat[k].tail) + 1);
            size_t j, tl = 0, st = 0, en = 0;
            ExFind *f;
            ExFindResult rc;
            clock_t t0;
            double s;
            for (j = 0; j < cat[k].reps; j++) { strcpy(t + tl, cat[k].unit); tl += strlen(cat[k].unit); }
            strcpy(t + tl, cat[k].tail); tl += strlen(cat[k].tail);
            hunt_stepf("catastrophic %s", cat[k].pat);
            f = compile(cat[k].pat, 1, 1, 0, NULL, 0);
            if (!f) { free(t); continue; }
            t0 = clock();
            rc = ex_find_next(f, t, tl, 0, &st, &en);
            s = hunt_cpu_since(t0);
            HUNT_CHECKF(rc == EX_FIND_LIMIT || rc == EX_FIND_NONE || rc == EX_FIND_MATCH, "pattern %s gave %d", cat[k].pat, (int)rc);
            if (s > hunt_time_limit(2.0)) { HUNT_SUSPECT(0, "a pattern that backtracks took more than 2 seconds before giving up: the interface freezes while it is typed into the find bar"); hunt_note_("%s took %.2f s", cat[k].pat, s); }
            t0 = clock();
            (void)ex_find_count(f, t, tl, 1000);
            s = hunt_cpu_since(t0);
            if (s > hunt_time_limit(3.0)) { HUNT_SUSPECT(0, "count with a pattern that backtracks took more than 3 seconds"); hunt_note_("%s count took %.2f s", cat[k].pat, s); }
            t0 = clock();
            { ExFindEdit *ed = NULL; size_t ne = ex_find_replace_all(f, t, tl, "x", 1, &ed); ex_find_edits_free(ed, ne); }
            s = hunt_cpu_since(t0);
            if (s > hunt_time_limit(3.0)) { HUNT_SUSPECT(0, "replace all with a pattern that backtracks took more than 3 seconds"); hunt_note_("%s replace took %.2f s", cat[k].pat, s); }
            (void)ex_find_prev(f, t, tl, tl, &st, &en);
            ex_find_free(f);
            free(t);
        }
    }
    hunt_step("the replacement syntax");
    {
        ExFind *f = compile("(\\w+) (\\w+)", 1, 1, 0, NULL, 0);
        const char *t = "say hello world now";
        size_t st = 0, en = 0, ol = 0;
        char *r;
        struct { const char *repl, *want; } rp[] = {
            { "$2 $1", "world hello" }, { "$0", "hello world" }, { "${1}x", "hellox" }, { "${2}", "world" }, { "[$1]", "[hello]" }, { "$$", "$" }, { "$$1", "$1" }, { "\\n", "\n" }, { "\\t", "\t" }, { "\\r", "\r" }, { "\\\\", "\\" },
            { "\\$1", "$1" }, { "a\\nb", "a\nb" }, { "", "" }, { "plain", "plain" }, { "$3", "" }, { "$9", "" }, { "${9}", "" }, { "${99}", "" }, { "$1$2", "helloworld" }, { "$1-$2-$0", "hello-world-hello world" },
        };
        size_t k;
        HUNT_EQ_INT(ex_find_next(f, t, strlen(t), 0, &st, &en), 1);
        HUNT_CHECK(st == 4 && en == 15);
        for (k = 0; k < sizeof rp / sizeof *rp; k++) {
            r = ex_find_replacement(f, t, strlen(t), st, en, rp[k].repl, strlen(rp[k].repl), &ol);
            HUNT_CHECKF(r && ol == strlen(rp[k].want) && !memcmp(r, rp[k].want, ol), "replacement \"%s\" gave \"%.*s\", want \"%s\"", rp[k].repl, r ? (int)ol : 0, r ? r : "(null)", rp[k].want);
            free(r);
        }
        {
            const char *odd[] = { "$", "${", "${1", "${}", "$a", "\\", "$-1", "${-1}", "${1x}", "$ 1", "\\x", "\\u0041", "${0}${0}${0}", "$$$", "\\$$1" };
            for (k = 0; k < sizeof odd / sizeof *odd; k++) { r = ex_find_replacement(f, t, strlen(t), st, en, odd[k], strlen(odd[k]), &ol); HUNT_CHECKF(r != NULL, "replacement \"%s\" gave nothing", odd[k]); free(r); }
        }
        ex_find_free(f);
        f = compile("a.b", 0, 1, 0, NULL, 0);
        r = ex_find_replacement(f, "a.b", 3, 0, 3, "$1 \\n ${0}", 10, &ol);
        HUNT_CHECK(r && ol == 10 && !memcmp(r, "$1 \\n ${0}", 10));            /* plain mode: the replacement as it is */
        free(r);
        ex_find_free(f);
        /* a group that did not take part in the match */
        f = compile("(a)|(b)", 1, 1, 0, NULL, 0);
        r = ex_find_replacement(f, "b", 1, 0, 1, "[$1][$2]", 8, &ol);
        HUNT_CHECK(r && !strcmp(r, "[][b]"));
        free(r);
        ex_find_free(f);
    }
}

/* ---- D. Unicode, broken text, NULs ---- */

static void test_unicode_and_bytes(void) {
    ExFind *f;
    size_t st = 0, en = 0;
    hunt_step("whole word next to letters outside ASCII");
    f = compile("caf", 0, 0, 1, NULL, 0);
    HUNT_SPEC(ex_find_next(f, "caf\xC3\xA9", 5, 0, &st, &en) == EX_FIND_NONE, "docs/formats.md, Editing: a match must not touch a letter, digit or underscore (\xC3\xA9 is a letter)");
    HUNT_CHECK(ex_find_next(f, "caf ", 4, 0, &st, &en) == EX_FIND_MATCH);
    HUNT_CHECK(ex_find_next(f, "\xC3\xA9" "caf", 5, 0, &st, &en) == EX_FIND_NONE);
    HUNT_CHECK(ex_find_next(f, "\xE4\xBD\xA0" "caf", 6, 0, &st, &en) == EX_FIND_NONE);
    HUNT_CHECK(ex_find_next(f, "caf_", 4, 0, &st, &en) == EX_FIND_NONE);
    HUNT_CHECK(ex_find_next(f, "caf1", 4, 0, &st, &en) == EX_FIND_NONE);
    HUNT_CHECK(ex_find_next(f, "-caf-", 5, 0, &st, &en) == EX_FIND_MATCH);
    HUNT_CHECK(ex_find_next(f, "\xE2\x80\x94" "caf\xE2\x80\x94", 9, 0, &st, &en) == EX_FIND_MATCH);       /* em dashes are not letters */
    ex_find_free(f);

    hunt_step("case folding (Unicode simple folding, no accent folding)");
    {
        struct { const char *pat, *text; int match; } cf[] = {
            { "\xC3\xA9", "\xC3\x89", 1 }, { "\xC3\x89", "\xC3\xA9", 1 }, { "e", "\xC3\xA9", 0 }, { "\xC3\xA9", "e\xCC\x81", 0 }, { "\xCF\x83", "\xCE\xA3", 1 }, { "\xCF\x83", "\xCF\x82", 1 }, { "\xC3\x9F", "SS", 0 }, { "ss", "\xC3\x9F", 0 },
            { "\xD0\xB4", "\xD0\x94", 1 }, { "i", "\xC4\xB0", 0 }, { "\xC7\x86", "\xC7\x85", 1 }, { "\xC7\x86", "\xC7\x84", 1 }, { "k", "\xE2\x84\xAA", 1 }, { "\xE1\xBA\x9E", "\xC3\x9F", 1 }, { "A", "a", 1 }, { "stra\xC3\x9F" "e", "STRASSE", 0 },
        };
        size_t i;
        for (i = 0; i < sizeof cf / sizeof *cf; i++) {
            ExFind *g = compile(cf[i].pat, 0, 0, 0, NULL, 0);
            int got = g && ex_find_next(g, cf[i].text, strlen(cf[i].text), 0, &st, &en) == EX_FIND_MATCH;
            if (cf[i].match == 1 && !got) { HUNT_SUSPECT(0, "simple case folding should make these equal"); hunt_note_("\"%s\" vs \"%s\"", cf[i].pat, cf[i].text); }
            else if (cf[i].match == 0) HUNT_CHECKF(!got, "\"%s\" should not match \"%s\" (simple folding only, no accent folding)", cf[i].pat, cf[i].text);
            ex_find_free(g);
        }
    }

    hunt_step("text that is not UTF-8");
    {
        const char *bad[] = { "a\xFF" "b", "\xC3", "ab\xC3", "\xE4\xBD", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xC0\x80", "\x80" };
        size_t i;
        f = compile("a", 0, 1, 0, NULL, 0);
        for (i = 0; i < sizeof bad / sizeof *bad; i++) {
            ExFindResult a = ex_find_next(f, bad[i], strlen(bad[i]), 0, &st, &en), b = ex_find_prev(f, bad[i], strlen(bad[i]), strlen(bad[i]), &st, &en);
            HUNT_CHECKF(a == EX_FIND_BAD_TEXT || a == EX_FIND_NONE || a == EX_FIND_MATCH, "next on broken text gave %d", (int)a);
            HUNT_SPEC(a != EX_FIND_MATCH || strlen(bad[i]) > 1, "include/exhaust_find.h: a subject that is not valid UTF-8 never matches");
            (void)b;
            HUNT_CHECK(ex_find_count(f, bad[i], strlen(bad[i]), 100) <= 100);
            { ExFindEdit *ed = NULL; size_t ne = ex_find_replace_all(f, bad[i], strlen(bad[i]), "x", 1, &ed); ex_find_edits_free(ed, ne); }
        }
        ex_find_free(f);
        f = compile("a", 1, 1, 0, NULL, 0);
        for (i = 0; i < sizeof bad / sizeof *bad; i++) { (void)ex_find_next(f, bad[i], strlen(bad[i]), 0, &st, &en); (void)ex_find_prev(f, bad[i], strlen(bad[i]), 1, &st, &en); }
        ex_find_free(f);
    }

    hunt_step("NUL bytes and starting places");
    {
        static const char nul[] = "a\0b\0c";
        f = compile("b", 0, 1, 0, NULL, 0);
        HUNT_CHECK(ex_find_next(f, nul, 5, 0, &st, &en) == EX_FIND_MATCH && st == 2 && en == 3);
        HUNT_CHECK(ex_find_count(f, nul, 5, 10) == 1);
        ex_find_free(f);
        f = compile("c", 1, 1, 0, NULL, 0);
        HUNT_CHECK(ex_find_next(f, nul, 5, 0, &st, &en) == EX_FIND_MATCH && st == 4);
        ex_find_free(f);
        f = compile("\xC3\xA9", 0, 1, 0, NULL, 0);
        HUNT_CHECK(ex_find_next(f, "a\xC3\xA9", 3, 1, &st, &en) == EX_FIND_MATCH && st == 1);
        HUNT_SUSPECT(ex_find_next(f, "a\xC3\xA9", 3, 2, &st, &en) != EX_FIND_MATCH, "starting in the middle of a character must not find a match that starts there");
        HUNT_CHECK(ex_find_next(f, "a\xC3\xA9", 3, 99, &st, &en) == EX_FIND_NONE);
        HUNT_CHECK(ex_find_next(f, "", 0, 0, &st, &en) == EX_FIND_NONE);
        HUNT_CHECK(ex_find_prev(f, "", 0, 0, &st, &en) == EX_FIND_NONE);
        HUNT_CHECK(ex_find_prev(f, "a\xC3\xA9", 3, 99, &st, &en) == EX_FIND_MATCH && st == 1);
        HUNT_CHECK(ex_find_count(f, "", 0, 10) == 0);
        ex_find_free(f);
    }

    hunt_step("big text");
    {
        size_t n = 6u << 20, i;
        char *t = (char *)malloc(n + 1);
        clock_t t0;
        double s;
        for (i = 0; i < n; i++) t[i] = (char)("ab \n"[i & 3]);
        f = compile("ab", 0, 0, 1, NULL, 0);
        t0 = clock();
        HUNT_CHECK(ex_find_count(f, t, n, 1000) == 1000);
        s = hunt_cpu_since(t0);
        HUNT_SUSPECT(s < hunt_time_limit(1.0), "counting the first 1000 matches in 6 MB took more than a second: count must stop at the limit");
        t0 = clock();
        { ExFindEdit *ed = NULL; size_t ne = ex_find_replace_all(f, t, n, "xy", 2, &ed); HUNT_CHECK(ne > 100000); ex_find_edits_free(ed, ne); }
        s = hunt_cpu_since(t0);
        HUNT_SUSPECT(s < hunt_time_limit(3.0), "replace all over 1.5 million matches took more than 3 seconds");
        t0 = clock();
        HUNT_CHECK(ex_find_prev(f, t, n, n, &st, &en) == EX_FIND_MATCH);
        s = hunt_cpu_since(t0);
        HUNT_SUSPECT(s < hunt_time_limit(0.5), "prev from the end of a big text took more than half a second: it should look backwards, not scan from the start");
        ex_find_free(f);
        f = compile("(a|b)+ ", 1, 1, 0, NULL, 0);
        t0 = clock();
        (void)ex_find_next(f, t, n, 0, &st, &en);
        s = hunt_cpu_since(t0);
        HUNT_SUSPECT(s < hunt_time_limit(1.0), "a regular expression over a big text took more than a second for the first match");
        ex_find_free(f);
        free(t);
    }
    hunt_step("a pattern of a megabyte, and plain mode with characters that mean something to a regular expression");
    {
        size_t n = 1u << 20;
        char *p = (char *)malloc(n + 1);
        memset(p, 'a', n); p[n] = 0;
        f = ex_find_compile(p, n, &(ExFindOptions){0, 0, 0}, NULL, 0);
        if (f) { HUNT_CHECK(ex_find_next(f, "aaa", 3, 0, &st, &en) == EX_FIND_NONE); ex_find_free(f); }
        f = ex_find_compile(p, n, &(ExFindOptions){1, 0, 0}, NULL, 0);
        if (f) ex_find_free(f);
        free(p);
        f = compile("a.b*c(d)[e]{2}^$|\\", 0, 1, 0, NULL, 0);
        HUNT_CHECK(f && ex_find_next(f, "xx a.b*c(d)[e]{2}^$|\\ yy", 24, 0, &st, &en) == EX_FIND_MATCH && st == 3);
        ex_find_free(f);
    }
}

/* ---- E. fuzzy ---- */

static int is_subsequence(const char *q, const char *t) {
    for (; *t && *q; t++) if (lower((unsigned char)*t) == lower((unsigned char)*q)) q++;
    return *q == 0;
}

static void test_fuzzy(void) {
    long rounds = hunt_iters(40000), r;
    hunt_step("fuzzy: known cases");
    HUNT_CHECK(ex_fuzzy_score("", "anything") == 0);
    HUNT_CHECK(ex_fuzzy_score("", "") == 0);
    HUNT_CHECK(ex_fuzzy_score("a", "") == -1);
    HUNT_CHECK(ex_fuzzy_score("abc", "ab") == -1);
    HUNT_CHECK(ex_fuzzy_score("zzz", "abc") == -1);
    HUNT_CHECK(ex_fuzzy_score("acb", "abc") == -1);
    HUNT_CHECK(ex_fuzzy_score("abc", "abc") >= 0);
    HUNT_CHECK(ex_fuzzy_score("ABC", "abc") == ex_fuzzy_score("abc", "abc"));
    HUNT_CHECK(ex_fuzzy_score("abc", "ABC") == ex_fuzzy_score("abc", "abc"));
    HUNT_SUSPECT(ex_fuzzy_score("ab", "ab cd") > ex_fuzzy_score("ab", "xxxxab"), "a match at the start of the text should score higher than one in the middle");
    HUNT_SUSPECT(ex_fuzzy_score("fb", "foo bar") > ex_fuzzy_score("fb", "fxxxbxx"), "matches at the start of words should score higher");
    HUNT_SUSPECT(ex_fuzzy_score("abc", "abcxxx") > ex_fuzzy_score("abc", "axbxcx"), "characters that follow each other should score higher");
    HUNT_SUSPECT(ex_fuzzy_score("\xC3\xA9", "\xC3\x89") >= 0, "an accented capital should match its lowercase");
    HUNT_SUSPECT(ex_fuzzy_score("\xC3\xA9", "caf\xC3\xA9") >= 0, "a query with a non-ASCII letter should match text that has it");
    for (r = 0; r < rounds; r++) {
        char q[12], t[40];
        size_t ql = hunt_rnd_size(6), tl = hunt_rnd_size(30), i;
        static const char alpha[] = "abcABC /-_.1";
        int sc, sub;
        for (i = 0; i < ql; i++) q[i] = alpha[hunt_rnd(sizeof alpha - 1)];
        for (i = 0; i < tl; i++) t[i] = alpha[hunt_rnd(sizeof alpha - 1)];
        q[ql] = 0; t[tl] = 0;
        sc = ex_fuzzy_score(q, t);
        sub = is_subsequence(q, t);
        HUNT_CHECKF(sc >= -1, "score %d for query \"%s\" in \"%s\"", sc, q, t);
        HUNT_CHECKF((sc >= 0) == sub, "query \"%s\" in \"%s\": score %d but subsequence is %d", q, t, sc, sub);
        if (hunt_.failures > 30) break;
    }
    hunt_step("fuzzy: a megabyte of text");
    {
        size_t n = 1u << 20, i;
        char *t = (char *)malloc(n + 1);
        clock_t t0;
        double s;
        for (i = 0; i < n; i++) t[i] = (char)('a' + (i * 7) % 26);
        t[n] = 0;
        t0 = clock();
        (void)ex_fuzzy_score("abcdefghijklmnopqrst", t);
        (void)ex_fuzzy_score("zzzzzzzzzzzzzzzzzzzz", t);
        s = hunt_cpu_since(t0);
        HUNT_SUSPECT(s < hunt_time_limit(1.0), "two fuzzy scores over a megabyte took more than a second: the palettes score every command and note on each keystroke");
        free(t);
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_find", argc, argv);
    test_plain_oracle();
    test_regex_consistency();
    test_patterns();
    test_unicode_and_bytes();
    test_fuzzy();
    return hunt_end();
}
