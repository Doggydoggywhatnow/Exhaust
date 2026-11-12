/* html: the HTML subset keeps the documented tags and attributes and nothing else: scripts, styles,
 * event handlers, unsafe URLs and unknown tags are removed, character references are decoded, broken
 * markup is repaired, and the cleaned result is stable (cleaning it again changes nothing). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *clean(const char *in) {
    ExHtml *h = ex_html_parse(in, strlen(in));
    char *out = ex_html_serialize(h);
    ex_html_free(h);
    return out;
}

static void expect(const char *in, const char *want, int line) {
    char *got = clean(in), *again;
    if (strcmp(got, want) != 0) { fprintf(stderr, "line %d:\n input: %s\n  got: %s\n want: %s\n", line, in, got, want); failures++; }
    again = clean(got);                                                      /* cleaning is idempotent */
    if (strcmp(again, got) != 0) { fprintf(stderr, "line %d: not idempotent:\n  once: %s\n twice: %s\n", line, got, again); failures++; }
    free(got); free(again);
}
#define EXPECT(in, want) expect(in, want, __LINE__)

int main(int argc, char **argv) {
    int a;
    /* what is kept */
    EXPECT("<b>bold</b> and <i>italic</i>", "<b>bold</b> and <i>italic</i>");
    EXPECT("<p>one</p><p>two</p>", "<p>one</p><p>two</p>");
    EXPECT("<a href=\"https://example.com/a?b=1&amp;c=2\" title='T'>link</a>", "<a href=\"https://example.com/a?b=1&amp;c=2\" title=\"T\">link</a>");
    EXPECT("<img src=\"pic.png\" alt=\"A\" width=100 height=\"50\">", "<img src=\"pic.png\" alt=\"A\" width=\"100\" height=\"50\">");
    EXPECT("<table><thead><tr><th>H</th></tr></thead><tbody><tr><td colspan=\"2\" align=\"right\">c</td></tr></tbody></table>",
           "<table><thead><tr><th>H</th></tr></thead><tbody><tr><td colspan=\"2\" align=\"right\">c</td></tr></tbody></table>");
    EXPECT("<details open><summary>S</summary>body</details>", "<details open=\"\"><summary>S</summary>body</details>");
    EXPECT("line<br>break<br/>again<hr>", "line<br>break<br>again<hr>");
    EXPECT("<H1 ID=top>Title</H1>", "<h1 id=\"top\">Title</h1>");
    /* what is removed */
    EXPECT("<script>alert(1)</script>after", "after");
    EXPECT("<SCRIPT type=x>var a = '</p>';</SCRIPT>after", "after");
    EXPECT("<style>p { color: red }</style><p>text</p>", "<p>text</p>");
    EXPECT("<p onclick=\"evil()\" style=\"color:red\" class=\"x\">t</p>", "<p>t</p>");
    EXPECT("<a href=\"javascript:alert(1)\">x</a>", "<a>x</a>");
    EXPECT("<a href=\" JaVaScRiPt:alert(1)\">x</a>", "<a>x</a>");
    EXPECT("<a href=\"data:text/html,<b>x</b>\">x</a>", "<a>x</a>");
    EXPECT("<img src=\"data:image/png;base64,AAAA\">", "<img>");
    EXPECT("<a href=\"vbscript:x\">x</a><a href=\"file:///etc/passwd\">y</a>", "<a>x</a><a>y</a>");
    EXPECT("<a href=\"mailto:me@example.com\">m</a><a href=\"notes/page.md\">r</a><a href=\"#frag\">f</a>", "<a href=\"mailto:me@example.com\">m</a><a href=\"notes/page.md\">r</a><a href=\"#frag\">f</a>");
    EXPECT("<iframe src=\"https://x\">fallback</iframe><object>o</object><form><input></form>", "fallbacko");
    EXPECT("<font color=red>coloured</font> <blink>x</blink>", "coloured x");
    EXPECT("<!-- a comment -->text<!-- another", "text");
    EXPECT("<img width=\"100px\" height=\"-5\" src=\"a.png\">", "<img src=\"a.png\">");
    EXPECT("<td colspan=\"x\">", "<td></td>");                           /* an element is kept where it stands; the bad attribute is not */
    EXPECT("<table><tr><td colspan=\"x\">c</td></tr></table>", "<table><tr><td>c</td></tr></table>");
    /* character references */
    EXPECT("a &amp; b &lt;c&gt; &quot;q&quot; &#65;&#x42; &nbsp;&copy; &unknown; &#0; &#xD800;", "a &amp; b &lt;c&gt; \"q\" AB \xC2\xA0\xC2\xA9 &amp;unknown; &amp;#0; &amp;#xD800;");
    /* repairing */
    EXPECT("<b>unclosed <i>nested", "<b>unclosed <i>nested</i></b>");
    EXPECT("</b>stray close</i>", "stray close");
    EXPECT("<ul><li>one<li>two</ul>", "<ul><li>one</li><li>two</li></ul>");
    EXPECT("<p>one<p>two", "<p>one</p><p>two</p>");
    EXPECT("1 < 2 and 3 > 2", "1 &lt; 2 and 3 &gt; 2");
    EXPECT("<b <i>x</b>", "<b>x</b>");
    EXPECT("<a href=\"x>y\">q</a>", "<a href=\"x&gt;y\">q</a>");
    /* depth is limited */
    {
        char deep[4096];
        size_t i, n = 0;
        ExHtml *h;
        char *text;
        for (i = 0; i < 300; i++) { memcpy(deep + n, "<div>", 5); n += 5; }
        memcpy(deep + n, "end", 4);
        h = ex_html_parse(deep, strlen(deep));
        CHECK(h != NULL);
        text = ex_html_text(h);
        CHECK(strcmp(text, "end") == 0);
        free(text);
        ex_html_free(h);
    }
    /* the API */
    CHECK(ex_html_tag_from_name("DIV", 3) == EX_HTML_DIV && ex_html_tag_from_name("script", 6) == EX_HTML_TEXT && ex_html_tag_from_name("h7", 2) == EX_HTML_TEXT);
    CHECK(strcmp(ex_html_tag_name(EX_HTML_SUMMARY), "summary") == 0 && ex_html_is_block_tag(EX_HTML_P) && !ex_html_is_block_tag(EX_HTML_B));
    CHECK(ex_html_url_allowed("https://a", 9) && ex_html_url_allowed("relative/x", 10) && !ex_html_url_allowed("javascript:x", 12) && !ex_html_url_allowed("ftp://x", 7));
    {
        const char *src = "<p>a <b>b</b> <a href=\"https://x\">c</a></p>";
        ExHtml *h = ex_html_parse(src, strlen(src));
        const ExHtmlNode *p = ex_html_root(h)->kids[0];
        char *text = ex_html_text(h);
        CHECK(p->tag == EX_HTML_P && p->nkids == 4 && strcmp(text, "a b c") == 0);
        CHECK(ex_html_attr(p->kids[3], "href") != NULL && strcmp(ex_html_attr(p->kids[3], "href"), "https://x") == 0 && ex_html_attr(p->kids[3], "src") == NULL);
        free(text);
        ex_html_free(h);
    }
    /* inputs the fuzzer once crashed on: cleaning must be stable (the nesting limit used to break that) */
    for (a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        char buf[8192];
        size_t n;
        char *once, *twice;
        ExHtml *h, *h2;
        CHECK(f != NULL);
        if (!f) continue;
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        h = ex_html_parse(buf, n);
        once = ex_html_serialize(h);
        h2 = ex_html_parse(once, strlen(once));
        twice = ex_html_serialize(h2);
        CHECK(strcmp(once, twice) == 0);
        free(once); free(twice);
        ex_html_free(h); ex_html_free(h2);
    }
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("html: ok");
    return 0;
}
