/* editing_find: plain and regular-expression find and replace on a sample: case, whole words, Unicode, groups in
 * replacements, replace all, searching backwards, empty matches, error messages and the guard against patterns
 * that would run for ever. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_find.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *SAMPLE =
    "The quick brown fox\njumps over the lazy dog.\nThe end of THE story: the-end, theory, bathe.\n"
    "Price: $5 (five) [x] a.b*c\r\nCaf\xC3\xA9 caf\xC3\xA9 CAF\xC3\xA9\n2024-05-17 and 1999-12-31\n";

static ExFind *mk(const char *pat, int regex, int match_case, int whole) {
    ExFindOptions o;
    char err[200];
    ExFind *f;
    o.regex = regex; o.match_case = match_case; o.whole_word = whole;
    f = ex_find_compile(pat, strlen(pat), &o, err, sizeof err);
    return f;
}

static size_t count(const char *pat, int regex, int match_case, int whole) {
    ExFind *f = mk(pat, regex, match_case, whole);
    size_t n;
    if (!f) return (size_t)-1;
    n = ex_find_count(f, SAMPLE, strlen(SAMPLE), 1000);
    ex_find_free(f);
    return n;
}

static char *replace_all(const char *pat, int regex, int match_case, int whole, const char *repl, size_t *nedits) {
    ExFind *f = mk(pat, regex, match_case, whole);
    ExFindEdit *ed = NULL;
    size_t n, i, len = strlen(SAMPLE), pos = 0, outn = 0;
    char *out = malloc(len * 4 + 1000);
    if (!f) { free(out); return NULL; }
    n = ex_find_replace_all(f, SAMPLE, len, repl, strlen(repl), &ed);
    for (i = 0; i < n; i++) {
        memcpy(out + outn, SAMPLE + pos, ed[i].offset - pos); outn += ed[i].offset - pos;
        memcpy(out + outn, ed[i].text, ed[i].text_len); outn += ed[i].text_len;
        pos = ed[i].offset + ed[i].remove_len;
    }
    memcpy(out + outn, SAMPLE + pos, len - pos); outn += len - pos;
    out[outn] = 0;
    if (nedits) *nedits = n;
    ex_find_edits_free(ed, n);
    ex_find_free(f);
    return out;
}

int main(void) {
    ExFind *f;
    size_t s, e, n;
    ExFindOptions o;
    char err[200], *r;

    /* plain: case, whole words */
    CHECK(count("the", 0, 1, 0) == 4);              /* the lazy, the-end, theory, bathe (case sensitive) */
    CHECK(count("the", 0, 0, 0) == 7);              /* and The, The, THE without regard to case */
    CHECK(count("the", 0, 0, 1) == 5);              /* whole words: The, the, The, THE, the (in "the-end"); not theory, bathe */
    CHECK(count("the", 0, 1, 1) == 2);              /* whole word and case sensitive: "the lazy" and "the-end" */
    /* plain text takes characters literally */
    CHECK(count("a.b*c", 0, 1, 0) == 1);
    CHECK(count("$5 (five) [x]", 0, 1, 0) == 1);
    CHECK(count("a.b*c", 1, 1, 0) == 0);            /* as a regex, "a.b*c" does not match "a.b*c" ... (a, any, b*, c) */
    /* Unicode case folding */
    CHECK(count("caf\xC3\xA9", 0, 0, 0) == 3);
    CHECK(count("caf\xC3\xA9", 0, 1, 0) == 1);                       /* only the middle one is all lower case */
    CHECK(count("cafe", 0, 0, 0) == 0);             /* no accent folding */
    CHECK(count("\xC3\xA9", 0, 0, 1) == 0);         /* é inside a word is not a whole word */

    /* regex */
    CHECK(count("[0-9]{4}-[0-9]{2}-[0-9]{2}", 1, 1, 0) == 2);
    CHECK(count("\\bthe\\b", 1, 0, 0) == 5);
    CHECK(count("^The", 1, 1, 0) == 2);             /* multiline anchors */
    CHECK(count("end$", 1, 1, 0) == 0);
    CHECK(count("dog\\.$", 1, 1, 0) == 1);
    CHECK(count("b\\*c$", 1, 1, 0) == 1);           /* the line ends with \r\n: $ matches before the \r (ANYCRLF) */
    CHECK(count("(?i)QUICK", 1, 1, 0) == 1);
    CHECK(count("\\p{Lu}\\p{Ll}+", 1, 1, 0) >= 3);  /* Unicode properties */
    CHECK(count("fox\\njumps", 1, 1, 0) == 1);      /* a pattern can span lines */

    /* next and previous */
    f = mk("the", 0, 0, 1);
    CHECK(ex_find_next(f, SAMPLE, strlen(SAMPLE), 0, &s, &e) == EX_FIND_MATCH && s == 0 && e == 3);
    CHECK(ex_find_next(f, SAMPLE, strlen(SAMPLE), 1, &s, &e) == EX_FIND_MATCH && s == 31);
    CHECK(ex_find_next(f, SAMPLE, strlen(SAMPLE), strlen(SAMPLE), &s, &e) == EX_FIND_NONE);
    CHECK(ex_find_prev(f, SAMPLE, strlen(SAMPLE), 31, &s, &e) == EX_FIND_MATCH && s == 0);
    CHECK(ex_find_prev(f, SAMPLE, strlen(SAMPLE), 0, &s, &e) == EX_FIND_NONE);
    ex_find_free(f);

    /* replace all, plain */
    r = replace_all("fox", 0, 1, 0, "cat", &n);
    CHECK(r && n == 1 && strstr(r, "The quick brown cat\n") == r);
    free(r);
    r = replace_all("the", 0, 0, 1, "a", &n);
    CHECK(r && n == 5 && strncmp(r, "a quick brown fox", 17) == 0 && strstr(r, "a end of a story: a-end, theory, bathe.") != NULL);
    free(r);
    r = replace_all("$5", 0, 1, 0, "$1 \\n", &n);                      /* plain mode: the replacement is literal, `$1 \n` stays */
    CHECK(r && n == 1 && strstr(r, "Price: $1 \\n (five)") != NULL);
    free(r);
    /* replace all, regex with groups, escapes and no-ops */
    r = replace_all("([0-9]{4})-([0-9]{2})-([0-9]{2})", 1, 1, 0, "$3.$2.$1", &n);
    CHECK(r && n == 2 && strstr(r, "17.05.2024 and 31.12.1999") != NULL);
    free(r);
    r = replace_all("(?<w>quick) (brown)", 1, 1, 0, "${2}\\t$1 $$ \\$ $9 \\\\", &n);
    CHECK(r && n == 1 && strstr(r, "brown\tquick $ $  \\ fox") != NULL);
    free(r);
    r = replace_all("fox", 1, 1, 0, "fox", &n);                        /* replacing with the same text changes nothing */
    CHECK(r && n == 0 && strcmp(r, SAMPLE) == 0);
    free(r);
    r = replace_all("^", 1, 1, 0, "> ", &n);                          /* empty matches: a quote marker on every line */
    CHECK(r && n == 6 && strncmp(r, "> The quick", 11) == 0 && strstr(r, "\n> jumps") != NULL);
    free(r);
    r = replace_all("\\r?\\n", 1, 1, 0, "\n", &n);                      /* CRLF to LF */
    CHECK(r && strstr(r, "\r") == NULL);
    free(r);
    r = replace_all("caf\xC3\xA9", 0, 0, 0, "tea", &n);
    CHECK(r && n == 3 && strstr(r, "tea tea tea\n") != NULL);
    free(r);

    /* errors and limits */
    o.regex = 1; o.match_case = 1; o.whole_word = 0;
    CHECK(ex_find_compile("(unclosed", 9, &o, err, sizeof err) == NULL && err[0]);
    CHECK(ex_find_compile("[z-a]", 5, &o, err, sizeof err) == NULL && err[0]);
    CHECK(ex_find_compile("", 0, &o, err, sizeof err) == NULL);
    {
        char *big = malloc(200001), *pat = "(a+)+$";
        ExFind *g;
        memset(big, 'a', 200000);
        big[200000] = 'b';
        big[200001 - 1] = 'b';
        g = ex_find_compile(pat, strlen(pat), &o, err, sizeof err);
        CHECK(g != NULL);
        if (g) {
            size_t a, b;
            ExFindResult res = ex_find_next(g, big, 200001, 0, &a, &b);       /* catastrophic: must give up, not hang */
            CHECK(res == EX_FIND_LIMIT || res == EX_FIND_NONE);
            ex_find_free(g);
        }
        free(big);
    }
    {
        ExFind *g = mk("a", 0, 1, 0);
        size_t a, b;
        CHECK(ex_find_next(g, "a\xFF" "a", 3, 0, &a, &b) == EX_FIND_BAD_TEXT);   /* not UTF-8: no match, no crash */
        ex_find_free(g);
    }

    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
