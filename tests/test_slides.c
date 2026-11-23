/* slides (6e-iii): a note splits into slides at the lines that hold ---; the properties, code fences, math blocks and comments are not looked into; a --- that
 * underlines a text is a heading and not a separator; blank slides are left out; the ranges point into the note. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_slides.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* expect: the slides separated by a byte 0x01, "" for none */
static void run(int line, const char *text, const char *expect) {
    ExSlide *s = NULL;
    size_t n = 0, i, pos = 0;
    char *got = malloc(strlen(text) * 2 + 8);
    size_t o = 0;
    if (ex_slides_split(text, strlen(text), &s, &n) != 0) { fprintf(stderr, "line %d: split failed\n", line); failures++; free(got); return; }
    for (i = 0; i < n; i++) {
        if (i) got[o++] = 1;
        if (s[i].end > strlen(text) || s[i].start > s[i].end || (i && s[i].start < s[i - 1].end)) { fprintf(stderr, "line %d: a range is not inside the text or not in order\n", line); failures++; }
        else { memcpy(got + o, text + s[i].start, s[i].end - s[i].start); o += s[i].end - s[i].start; }
        pos = s[i].end;
    }
    (void)pos;
    got[o] = 0;
    if (strcmp(got, expect)) {
        size_t k;
        fprintf(stderr, "line %d: slides differ\n  got  [", line);
        for (k = 0; got[k]; k++) fputs(got[k] == 1 ? " | " : (got[k] == '\n' ? "\\n" : (got[k] == '\r' ? "\\r" : (const char[]){got[k], 0})), stderr);
        fprintf(stderr, "]\n  want [");
        for (k = 0; expect[k]; k++) fputs(expect[k] == 1 ? " | " : (expect[k] == '\n' ? "\\n" : (expect[k] == '\r' ? "\\r" : (const char[]){expect[k], 0})), stderr);
        fprintf(stderr, "]\n");
        failures++;
    }
    ex_slides_free(s);
    free(got);
}
#define RUN(t, e) run(__LINE__, t, e)

int main(void) {
    RUN("one", "one");
    RUN("", "");
    RUN("\n\n  \n", "");
    RUN("a\n\n---\n\nb", "a\001b");
    RUN("a\n---\nb", "a\n---\nb");                                          /* underlines a text: a heading, not a separator */
    RUN("# T\n---\nb\n\n---\nc", "# T\n---\nb\001c");
    RUN("---\na: 1\n---\nbody\n\n---\nnext", "body\001next");               /* the properties are not a slide */
    RUN("---\na: 1\n...\nbody\n\n---\nnext", "body\001next");
    RUN("\n---\n\nx", "x");                                                 /* a separator at the start: no empty first slide */
    RUN("a\n\n---\n\n---\n\nb\n\n---\n", "a\001b");                          /* empty slides and a separator at the end */
    RUN("a\n\n----\n\nb", "a\001b");                                        /* three or more hyphens */
    RUN("a\n\n--\n\nb", "a\n\n--\n\nb");
    RUN("a\n\n***\n\nb", "a\n\n***\n\nb");
    RUN("a\n\n- - -\n\nb", "a\n\n- - -\n\nb");
    RUN("a\n\n--- text\n\nb", "a\n\n--- text\n\nb");
    RUN("a\n\n---   \n\nb", "a\001b");
    RUN("a\n\n   ---\n\nb", "a\001b");                                       /* up to three spaces of indent */
    RUN("a\n\n    ---\n\nb", "a\n\n    ---\n\nb");                           /* four: code */
    RUN("```\n---\n```\n\n---\nx", "```\n---\n```\001x");                    /* fenced code */
    RUN("~~~\n\n---\n~~~\n\n---\nx", "~~~\n\n---\n~~~\001x");
    RUN("````\n\n---\n```\n\n---\n````\n\n---\nx", "````\n\n---\n```\n\n---\n````\001x");   /* a longer fence closes only with as many */
    RUN("$$\n\n---\n\n$$\n\n---\nm", "$$\n\n---\n\n$$\001m");                /* math */
    RUN("<!--\n\n---\n-->\n\n---\nc", "<!--\n\n---\n-->\001c");              /* a comment */
    RUN("a\r\n\r\n---\r\n\r\nb\r\n", "a\001b");                              /* Windows line breaks */
    RUN("\n\n a \n\n---\n  \nb\n\n\n", " a \001b");                          /* blank lines go, spaces of a line stay */
    RUN("---\nno close\n\nx", "no close\n\nx");                              /* an opening line with no closing one: a separator at the start */
    RUN("one\n\n---\n\ntwo\n\n---\n\nthree\n\n---\n\nfour", "one\001two\001three\001four");
    RUN("- a\n- b\n\n---\n\n> quote\n\n---\n\n| t |\n|---|\n| x |\n", "- a\n- b\001> quote\001| t |\n|---|\n| x |");    /* a table's |---| is not a line of hyphens */
    {   /* many slides: ranges in order and inside the text */
        char *big = malloc(40000), *want = malloc(40000);
        size_t len = 0, wl = 0;
        int i;
        for (i = 0; i < 2000; i++) {
            len += (size_t)snprintf(big + len, 40000 - len, "slide %d\n\n---\n\n", i);
            wl += (size_t)snprintf(want + wl, 40000 - wl, "%sslide %d", i ? "\001" : "", i);
        }
        run(__LINE__, big, want);
        free(big); free(want);
    }
    {   /* NULL and a length that stops in the middle of a line */
        ExSlide *s = NULL;
        size_t n = 7;
        CHECK(ex_slides_split(NULL, 5, &s, &n) == 0 && n == 0);
        ex_slides_free(s);
        CHECK(ex_slides_split("a\n\n---\n\nbcd", 9, &s, &n) == 0 && n == 2 && s[1].end - s[1].start == 1);     /* only the first 9 bytes: "a\n\n---\n\nb" */
        ex_slides_free(s);
    }
    if (failures) fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
