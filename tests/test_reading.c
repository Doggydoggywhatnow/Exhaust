/* reading_mode: the reading-mode model of the sample note matches a golden dump and the explicit
 * expectations below (callouts, wikilinks, embeds, highlights, tags, footnotes, comments, block IDs,
 * tasks, tables, code, math and the HTML subset), and the pieces the model leaves out stay out.
 * Folding is tested in the second half. Usage: test_reading <sample-note.md> <golden.txt> [--write-golden] [--dump] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_markdown.h"
#include "exhaust_platform.h"
#include "exhaust_reading.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static char *dump_of(const char *md) {
    ExReading *r = ex_reading_build(md, strlen(md));
    char *d = ex_reading_dump(r);
    ex_reading_free(r);
    return d;
}

static void expect_dump(const char *md, const char *want, int line) {
    char *got = dump_of(md);
    if (strcmp(got, want) != 0) fprintf(stderr, "line %d:\n  source: %s\n  got:  %s  want: %s", line, md, got, want), failures++;
    free(got);
}
#define EXPECT(md, want) expect_dump(md, want, __LINE__)

int main(int argc, char **argv) {
    size_t len = 0, glen = 0;
    char *text, *dump, *plain;
    ExReading *r;
    int write_golden = argc > 3 && strcmp(argv[3], "--write-golden") == 0, show = argc > 3 && strcmp(argv[3], "--dump") == 0;
    if (argc < 3) { fprintf(stderr, "usage: test_reading SAMPLE GOLDEN\n"); return 2; }
    text = ex_platform_read_file(argv[1], &len);
    CHECK(text != NULL);
    if (!text) return 1;
    r = ex_reading_build(text, len);
    dump = ex_reading_dump(r);
    plain = ex_reading_plain_text(r);
    if (show) fputs(dump, stdout);

    /* the golden dump */
    if (write_golden) {
        FILE *f = fopen(argv[2], "wb");
        if (f) { fwrite(dump, 1, strlen(dump), f); fclose(f); }
        fprintf(stderr, "golden written\n");
    } else {
        char *golden = ex_platform_read_file(argv[2], &glen);
        CHECK(golden != NULL);
        if (golden) {
            if (strcmp(golden, dump) != 0) {
                size_t i = 0;
                while (golden[i] && dump[i] && golden[i] == dump[i]) i++;
                fprintf(stderr, "dump differs from the golden file at byte %zu:\n--- got ---\n%.300s\n--- golden ---\n%.300s\n", i, dump + (i > 80 ? i - 80 : 0), golden + (i > 80 ? i - 80 : 0));
                failures++;
            }
            free(golden);
        }
    }

    /* what the sample note must read as */
    CHECK(!has(dump, "title: Every element"));                                          /* front matter is not part of the text */
    CHECK(has(dump, "H1 Heading one") && has(dump, "H2 Heading two") && has(dump, "H6 Heading six") && has(dump, "H1 Setext heading"));
    CHECK(has(dump, "*emphasis*") && has(dump, "**strong**") && has(dump, "~~strike~~") && has(dump, "==highlight==") && has(dump, "`code span`"));
    CHECK(has(dump, "[[Wikilink]]") && has(dump, "[[Page|alias]]") && has(dump, "[[Page#Heading]]") && has(dump, "[[Page#^block]]"));
    CHECK(has(dump, "![[embed.png|100]]") && has(dump, "[text](https://example.com)") && has(dump, "![img: alt -> image.png]"));
    CHECK(has(dump, "[https://auto.link](https://auto.link)") || has(dump, "https://auto.link"));
    CHECK(has(dump, "[^1]") && has(dump, "FOOTNOTES") && has(dump, "1. P The footnote text"));
    CHECK(has(dump, "#tag") && has(dump, "#nested/tag-two") && !has(dump, "#123]"));
    CHECK(!has(dump, "hidden *text*") && !has(dump, "%%") && !has(dump, "a multi-line"));    /* comments are not shown */
    CHECK(!has(dump, "^block-id") && !has(dump, "block-id"));                                  /* block IDs are not shown */
    CHECK(has(dump, "> P A quote") && has(dump, "> > P nested quote"));
    CHECK(has(dump, "[!note] TITLE **A callout title**") && has(dump, "[!warning-] TITLE **Closed and foldable**") && has(dump, "[!abstract+] TITLE **Open foldable alias**") && has(dump, "[!note] TITLE **Unknown type**"));
    CHECK(has(dump, "[!note] P Callout body with **strong**") && has(dump, "[!note] \xE2\x80\xA2 P list in callout"));
    CHECK(has(dump, "\xE2\x80\xA2 P bullet one") && has(dump, "\xE2\x80\xA2 [ ] P open task") && has(dump, "\xE2\x80\xA2 [x] P done task") && has(dump, "\xE2\x80\xA2 [x] P other task"));
    CHECK(has(dump, "1. P ordered one") && has(dump, "2. P ordered two"));
    CHECK(has(dump, "CODE python: def f():\\n    return 1") && has(dump, "CODE: tilde fence") && has(dump, "CODE: indented code\\nmore code"));
    CHECK(has(dump, "MATH: \\int_0^1 x\\,dx") && has(dump, "$a^2 + b^2 = c^2$") && has(dump, "$x = y$"));
    CHECK(has(dump, "ROW(header) |:l Left |:c Center |:r Right") && has(dump, "ROW |:l a |:c **b** |:r [[c|alias]]") && has(dump, "ROW |:l d |:c e"));
    CHECK(has(dump, "\nHR\n"));
    CHECK(has(dump, "P An HTML block"));                                                       /* the HTML block is reduced to its text */
    CHECK(has(dump, "**html**") && !has(dump, "<b>"));                                         /* inline tags of the subset set the style */
    CHECK(has(plain, "Heading one\n") && has(plain, "\xE2\x80\xA2 bullet one\n") && has(plain, "Footnotes\n"));

    /* small cases */
    EXPECT("plain text\n", "P plain text\n");
    EXPECT("# T\n\ntext\n", "H1 T\nP text\n");
    EXPECT("a ==hl== b\n", "P a ==hl== b\n");
    EXPECT("`==code==` and ==x==\n", "P `==code==` and ==x==\n");                              /* not inside code */
    EXPECT("%%gone%% kept\n", "P kept\n");
    EXPECT("line one %%a\nstill comment\nend%% line two\n", "P line one  line two\n");
    EXPECT("text ^id\n", "P text\n");
    EXPECT("see [[Note#Section]] and [[Note|shown]]\n", "P see [[Note#Section]] and [[Note|shown]]\n");
    EXPECT("> [!info]\n> body\n", "[!info] TITLE **info**\n[!info] P body\n");
    EXPECT("> [!bug]- Closed\n", "[!bug-] TITLE **Closed**\n");
    EXPECT("> [!tldr] T\n> > [!tip] Inner\n> > text\n", "[!abstract] TITLE **T**\n[!abstract] [!tip] TITLE **Inner**\n[!abstract] [!tip] P text\n");
    EXPECT("> not a callout [!note]\n", "> P not a callout [!note]\n");
    EXPECT("1. a\n2. b\n   - c\n", "1. P a\n2. P b\n  \xE2\x80\xA2 P c\n");
    EXPECT("- [ ] a\n- [x] b\n", "\xE2\x80\xA2 [ ] P a\n\xE2\x80\xA2 [x] P b\n");
    EXPECT("text[^a] and [^b]\n\n[^a]: first\n[^b]: second\n    more\n", "P text[^1] and [^2]\nFOOTNOTES\n1. P first\n2. P second more\n");
    EXPECT("[^x]\n", "P [^1]\nFOOTNOTES\n1. P (no text)\n");
    EXPECT("```\n==not highlighted== %%not a comment%%\n```\n", "CODE: ==not highlighted== %%not a comment%%\n");
    EXPECT("&amp; &lt;b&gt; &#65;\n", "P & <b> A\n");
    EXPECT("a <i>it</i> <mark>m</mark> <sub>s</sub>\n", "P a *it* ==m== <sub>s</sub>\n");
    EXPECT("line one  \nline two\n", "P line one\\nline two\n");
    EXPECT("", "");
    EXPECT("---\ntitle: x\n---\nbody\n", "P body\n");
    EXPECT("---\nnot front matter\n", "HR\nP not front matter\n");
    EXPECT("$$\nx\n$$\n", "MATH: x\n");
    EXPECT("a^[inline note] b\n", "P a[^1] b\nFOOTNOTES\n1. P inline note\n");
    EXPECT("![[img.png]]\n", "P ![[img.png]]\n");
    EXPECT("a #tag. b #1 c#d\n", "P a #tag. b #1 c#d\n");
    EXPECT("| a |\n|---|\n| b |\n", "ROW(header) | a\nROW | b\n");

    ex_reading_free(r);
    free(dump); free(plain); free(text);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("reading_mode: ok");
    return 0;
}
