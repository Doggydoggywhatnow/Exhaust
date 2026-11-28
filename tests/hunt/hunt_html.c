/* hunt_html: the HTML subset parser and cleaner (include/exhaust_html.h, docs/formats.md "HTML subset").
 *
 * HTML in a note is untrusted text: a note can come from a sync folder, a shared vault, an importer or a clipboard. The parser is the only thing between
 * that text and the reading view, the PDF export and the clipboard, and the documentation makes promises a test can hold it to:
 * only the listed tags and attributes survive, `style`, `class` and `on...` are dropped, only http, https, mailto, relative and #fragment URLs are kept,
 * script and style lose their content, comments are dropped, references are decoded, nesting past 64 is flattened, an unclosed tag is closed at the end
 * of its parent, and "cleaning is stable: cleaning the cleaned text changes nothing". This test
 *   A. checks the URL filter against a table of tricks (case, spaces, control characters inside the scheme, entities that spell `javascript:`);
 *   B. checks character references (overflow, surrogates, zero, no semicolon, double encoding);
 *   C. feeds a corpus of hostile fragments and random tag soup, and walks every tree it returns for the documented guarantees;
 *   D. checks that the cleaner is stable (parse, write, parse, write gives the same text twice);
 *   E. measures time on input made to be slow (deep nesting, a million `<`, thousands of attributes, one huge attribute).
 * Link: exhaust_markdown. */
#include "hunt.h"

#include "exhaust_html.h"

/* ---- A. URLs ---- */

static void test_urls(void) {
    struct { const char *url; int ok; int suspect; } u[] = {
        { "http://example.com", 1, 0 }, { "https://example.com/a?b=c#d", 1, 0 }, { "HTTP://EXAMPLE.COM", 1, 0 }, { "HtTpS://x", 1, 0 }, { "mailto:a@b.c", 1, 0 }, { "MAILTO:a@b.c", 1, 0 }, { "#fragment", 1, 0 }, { "#", 1, 0 },
        { "relative/path.md", 1, 0 }, { "./x", 1, 0 }, { "../x", 1, 0 }, { "x.png", 1, 0 }, { "/absolute/path", 1, 0 }, { "a b.md", 1, 0 }, { "?query=1", 1, 0 }, { "", 1, 1 }, { "dir/with:colon.md", 1, 1 },
        { "javascript:alert(1)", 0, 0 }, { "JAVASCRIPT:alert(1)", 0, 0 }, { "JaVaScRiPt:alert(1)", 0, 0 }, { " javascript:alert(1)", 0, 0 }, { "\tjavascript:alert(1)", 0, 0 }, { "\njavascript:alert(1)", 0, 0 }, { "\x01javascript:alert(1)", 0, 0 },
        { "java\tscript:alert(1)", 0, 0 }, { "java\nscript:alert(1)", 0, 0 }, { "java\rscript:alert(1)", 0, 0 }, { "java\x01script:alert(1)", 0, 1 }, { "jav&#x09;ascript:x", 0, 0 }, { "&#106;avascript:x", 0, 0 }, { "javascript&colon;x", 0, 1 },
        { "vbscript:msgbox(1)", 0, 0 }, { "data:text/html,<script>alert(1)</script>", 0, 0 }, { "DATA:image/png;base64,AAAA", 0, 0 }, { "file:///etc/passwd", 0, 0 }, { "FILE:///C:/Windows", 0, 0 }, { "ftp://x/y", 0, 0 }, { "tel:123", 0, 0 },
        { "ssh://x", 0, 0 }, { "smb://host/share", 0, 0 }, { "ms-msdt:/id", 0, 0 }, { "x-man-page://ls", 0, 0 }, { "about:blank", 0, 0 }, { "blob:http://x/1", 0, 0 }, { "view-source:http://x", 0, 0 }, { "chrome://settings", 0, 0 },
        { "exhaust://open?vault=x", 0, 1 }, { "http\x01://x", 0, 1 }, { "http ://x", 0, 1 }, { "//evil.example/x", 1, 1 }, { "\\\\host\\share", 1, 1 }, { "\xE2\x80\xAEjavascript:x", 0, 1 }, { "javascript\xE2\x80\x8B:x", 0, 1 },
        { "https:\\\\evil.example", 1, 1 }, { "mailto:", 1, 0 }, { "http:", 1, 1 }, { "javascript", 1, 0 }, { "javascript.md", 1, 0 }, { "javascript/x", 1, 0 }, { "a:b", 0, 1 }, { ":x", 1, 1 },
    };
    size_t i;
    hunt_step("urls: the allow list");
    for (i = 0; i < sizeof u / sizeof *u; i++) {
        int got = ex_html_url_allowed(u[i].url, strlen(u[i].url)) != 0;
        if (u[i].suspect) { HUNT_SUSPECT(got == u[i].ok, "an unusual URL: read this one by hand"); if (got != u[i].ok) hunt_note_("url_allowed(\"%s\") = %d", u[i].url, got); }
        else HUNT_SPECF(got == u[i].ok, "docs/formats.md, HTML subset: href and src (only http, https, mailto, a relative path or a # fragment; never javascript: or data:)", "url_allowed(\"%s\") = %d, want %d", u[i].url, got, u[i].ok);
    }
    HUNT_CHECK(ex_html_url_allowed(NULL, 0) == 0 || ex_html_url_allowed(NULL, 0) == 1);
    /* the length is honoured: text after it is not part of the URL */
    HUNT_CHECK(ex_html_url_allowed("https://ok javascript:x", 10));
    HUNT_CHECK(!ex_html_url_allowed("javascript:x", 12));
    HUNT_CHECK(!ex_html_url_allowed("javascript:x https://ok", 12));
    {
        /* a NUL inside the length */
        static const char nul[] = "http://ok\0javascript:x";
        (void)ex_html_url_allowed(nul, sizeof nul - 1);
    }
    hunt_step("urls: through the whole parser");
    {
        const char *html[] = { "<a href=\"javascript:alert(1)\">x</a>", "<a href=\"&#106;avascript:alert(1)\">x</a>", "<a href=\"jav&#x09;ascript:alert(1)\">x</a>", "<a href='JAVASCRIPT:x'>x</a>", "<a href=javascript:x>x</a>",
            "<img src=\"data:image/png;base64,AAAA\">", "<a href=\"  javascript:x\">x</a>", "<a href=\"file:///etc/passwd\">x</a>", "<img src=\"javascript:x\" alt=\"y\">", "<a href=\"vbscript:x\">x</a>" };
        size_t k;
        for (k = 0; k < sizeof html / sizeof *html; k++) {
            ExHtml *h = ex_html_parse(html[k], strlen(html[k]));
            char *out = h ? ex_html_serialize(h) : NULL;
            HUNT_CHECKF(out != NULL, "no output for %s", html[k]);
            if (out) {
                HUNT_SPECF(!strstr(out, "javascript") && !strstr(out, "JAVASCRIPT") && !strstr(out, "data:") && !strstr(out, "file:") && !strstr(out, "vbscript"), "docs/formats.md, HTML subset: never javascript: or data:", "%s was cleaned to %s", html[k], out);
                free(out);
            }
            ex_html_free(h);
        }
    }
}

/* ---- B. character references ---- */

static int valid_utf8_nonul(const char *s, size_t n) {
    const unsigned char *p = (const unsigned char *)s, *e = p + n;
    while (p < e) {
        if (*p == 0) return 0;
        if (*p < 0x80) p++;
        else if (*p >= 0xC2 && *p <= 0xDF && p + 1 < e && (p[1] & 0xC0) == 0x80) p += 2;
        else if (*p == 0xE0 && p + 2 < e && p[1] >= 0xA0 && p[1] <= 0xBF && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p >= 0xE1 && *p <= 0xEC && p + 2 < e && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p == 0xED && p + 2 < e && p[1] >= 0x80 && p[1] <= 0x9F && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p >= 0xEE && *p <= 0xEF && p + 2 < e && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p == 0xF0 && p + 3 < e && p[1] >= 0x90 && p[1] <= 0xBF && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else if (*p >= 0xF1 && *p <= 0xF3 && p + 3 < e && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else if (*p == 0xF4 && p + 3 < e && p[1] >= 0x80 && p[1] <= 0x8F && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else return 0;
    }
    return 1;
}

static void test_entities(void) {
    struct { const char *in, *want; } known[] = {
        { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }, { "&#65;", "A" }, { "&#x41;", "A" }, { "&#X41;", "A" }, { "&#x1F600;", "\xF0\x9F\x98\x80" }, { "&#128512;", "\xF0\x9F\x98\x80" },
        { "&nbsp;", "\xC2\xA0" }, { "&copy;", "\xC2\xA9" }, { "&hellip;", "\xE2\x80\xA6" }, { "&mdash;", "\xE2\x80\x94" }, { "&amp;lt;", "&lt;" }, { "&amp;amp;", "&amp;" }, { "a&amp;b", "a&b" }, { "&", "&" }, { "&;", "&;" },
        { "&#;", "&#;" }, { "&#x;", "&#x;" }, { "&unknownentity;", "&unknownentity;" }, { "&amp", "&amp" }, { "x &lt y", "x &lt y" }, { "&#65", "&#65" }, { "&&amp;", "&&" }, { "&#38;lt;", "&lt;" }, { "&AMP;", "&AMP;" }, { "plain", "plain" }, { "", "" },
    };
    size_t i;
    hunt_step("entities: known cases");
    for (i = 0; i < sizeof known / sizeof *known; i++) {
        char *d = ex_html_decode(known[i].in, strlen(known[i].in));
        if (strcmp(known[i].in, "&amp") == 0 || strcmp(known[i].in, "&AMP;") == 0 || strcmp(known[i].in, "&#65") == 0 || strcmp(known[i].in, "x &lt y") == 0) {
            HUNT_SUSPECT(d && !strcmp(d, known[i].want), "a reference without a semicolon or in capitals: browsers decode some of these; either answer is defensible but it must be the same everywhere (reading view, export, index)");
            if (d && strcmp(d, known[i].want)) hunt_note_("\"%s\" decoded to \"%s\"", known[i].in, d);
        } else HUNT_CHECKF(d && !strcmp(d, known[i].want), "\"%s\" decoded to \"%s\", want \"%s\"", known[i].in, d ? d : "(null)", known[i].want);
        free(d);
    }
    hunt_step("entities: numbers that are not characters");
    {
        const char *bad[] = { "&#0;", "&#x0;", "&#xD800;", "&#xDFFF;", "&#x110000;", "&#1114112;", "&#99999999999999999999;", "&#xFFFFFFFFFFFFFFFFFF;", "&#-1;", "&#x-1;", "&#1.5;", "&#x1g;", "&#0000000000000000000065;", "&#xFFFE;", "&#xFFFF;", "&#1;", "&#x7F;", "&#128;", "&#x80;" };
        size_t k;
        for (k = 0; k < sizeof bad / sizeof *bad; k++) {
            char in[80], *d;
            size_t dl;
            snprintf(in, sizeof in, "a%sb", bad[k]);
            d = ex_html_decode(in, strlen(in));
            HUNT_CHECKF(d != NULL, "no result for %s", in);
            if (!d) continue;
            dl = strlen(d);
            HUNT_CHECKF(valid_utf8_nonul(d, dl), "%s decoded to something that is not valid UTF-8 (or holds a NUL)", in);
            HUNT_CHECKF(d[0] == 'a' && dl >= 1 && d[dl - 1] == 'b', "%s: the text around the reference was damaged (\"%s\")", in, d);
            free(d);
        }
        {
            char *d = ex_html_decode("a&#65;b", 3);        /* the length cuts the reference */
            HUNT_CHECK(d && !strcmp(d, "a&#"));
            free(d);
            d = ex_html_decode("&amp;&amp;", 5);
            HUNT_CHECK(d && !strcmp(d, "&"));
            free(d);
        }
    }
    hunt_step("entities: random text decodes to valid UTF-8 and never grows more than 4 bytes per input byte");
    {
        long rounds = hunt_iters(8000), r;
        for (r = 0; r < rounds; r++) {
            char text[300];
            size_t tl = hunt_random_valid_text(text, sizeof text, 20), dl;
            char *d = ex_html_decode(text, tl);
            HUNT_CHECK(d != NULL);
            if (d) { dl = strlen(d); HUNT_CHECKF(valid_utf8_nonul(d, dl), "random text decoded to invalid UTF-8"); HUNT_CHECK(dl <= tl * 4 + 4); free(d); }
        }
    }
    hunt_step("tag names");
    {
        const char *names[] = { "a", "abbr", "b", "blockquote", "br", "caption", "code", "del", "details", "div", "em", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "i", "img", "ins", "kbd", "li", "mark", "ol", "p", "pre", "q", "s", "samp",
            "small", "span", "strong", "sub", "summary", "sup", "table", "tbody", "td", "tfoot", "th", "thead", "tr", "u", "ul", "var" };
        const char *not_names[] = { "script", "style", "iframe", "object", "embed", "form", "font", "center", "marquee", "svg", "math", "input", "button", "link", "meta", "base", "textarea", "video", "audio", "canvas", "h7", "h0", "ab", "aa", "" };
        size_t k;
        for (k = 0; k < sizeof names / sizeof *names; k++) {
            char up[16];
            size_t j;
            ExHtmlTag t = ex_html_tag_from_name(names[k], strlen(names[k]));
            HUNT_CHECKF(t != EX_HTML_TEXT, "<%s> is in the subset", names[k]);
            HUNT_CHECKF(t == EX_HTML_TEXT || (ex_html_tag_name(t) && !strcmp(ex_html_tag_name(t), names[k])), "tag %s has the name %s", names[k], ex_html_tag_name(t) ? ex_html_tag_name(t) : "(null)");
            for (j = 0; names[k][j]; j++) up[j] = (char)(names[k][j] >= 'a' && names[k][j] <= 'z' ? names[k][j] - 32 : names[k][j]);
            up[j] = 0;
            HUNT_CHECKF(ex_html_tag_from_name(up, strlen(up)) == t, "<%s> in capitals is another tag", up);
            HUNT_CHECK(ex_html_tag_from_name(names[k], strlen(names[k]) - 1) != t || strlen(names[k]) == 1 || ex_html_tag_from_name(names[k], strlen(names[k]) - 1) != EX_HTML_TEXT);
        }
        for (k = 0; k < sizeof not_names / sizeof *not_names; k++) HUNT_CHECKF(ex_html_tag_from_name(not_names[k], strlen(not_names[k])) == EX_HTML_TEXT, "<%s> is not in the subset", not_names[k]);
        HUNT_CHECK(ex_html_is_block_tag(EX_HTML_DIV) && ex_html_is_block_tag(EX_HTML_P) && ex_html_is_block_tag(EX_HTML_H1) && ex_html_is_block_tag(EX_HTML_UL) && ex_html_is_block_tag(EX_HTML_TABLE) && ex_html_is_block_tag(EX_HTML_BLOCKQUOTE));
        HUNT_CHECK(!ex_html_is_block_tag(EX_HTML_SPAN) && !ex_html_is_block_tag(EX_HTML_B) && !ex_html_is_block_tag(EX_HTML_A) && !ex_html_is_block_tag(EX_HTML_EM) && !ex_html_is_block_tag(EX_HTML_TEXT));
        for (k = 1; k < EX_HTML_TAG_COUNT; k++) HUNT_CHECK(ex_html_tag_name((ExHtmlTag)k) && *ex_html_tag_name((ExHtmlTag)k));
    }
}

/* ---- C. trees ---- */

static const char *const ALLOWED_ATTRS[] = { "href", "src", "alt", "title", "width", "height", "colspan", "rowspan", "align", "open", "id" };

static int is_whole_number(const char *s) { size_t i; if (!*s) return 0; for (i = 0; s[i]; i++) if (s[i] < '0' || s[i] > '9') return 0; return 1; }

static size_t g_max_depth, g_nodes;

static void walk(const ExHtmlNode *n, size_t depth, const char *what, const char *src) {
    size_t i, k;
    g_nodes++;
    if (depth > g_max_depth) g_max_depth = depth;
    if (n->tag == EX_HTML_TEXT) {
        HUNT_CHECKF(n->nkids == 0, "%s: a text node has children", what);
        if (n->text) HUNT_CHECKF(valid_utf8_nonul(n->text, strlen(n->text)) , "%s: text that is not valid UTF-8", what);
    } else {
        HUNT_CHECKF((int)n->tag > 0 && (int)n->tag < EX_HTML_TAG_COUNT, "%s: a node with tag %d", what, (int)n->tag);
        for (i = 0; i < n->nattrs; i++) {
            const char *name = n->attrs[i].name, *val = n->attrs[i].value;
            int ok = 0;
            HUNT_CHECKF(name && val, "%s: an attribute with no name or value", what);
            if (!name || !val) continue;
            for (k = 0; k < sizeof ALLOWED_ATTRS / sizeof *ALLOWED_ATTRS; k++) if (!strcmp(name, ALLOWED_ATTRS[k])) ok = 1;
            HUNT_SPECF(ok, "docs/formats.md, HTML subset: every other attribute, including style, class and the on... handlers, is dropped", "%s: attribute \"%s\" survived on <%s>", what, name, ex_html_tag_name(n->tag));
            if (!strcmp(name, "href") || !strcmp(name, "src")) HUNT_SPECF(ex_html_url_allowed(val, strlen(val)), "docs/formats.md, HTML subset: only http, https, mailto, relative and # URLs survive", "%s: %s=\"%s\" survived", what, name, val);
            if (!strcmp(name, "width") || !strcmp(name, "height") || !strcmp(name, "colspan") || !strcmp(name, "rowspan")) HUNT_SPECF(is_whole_number(val), "docs/formats.md, HTML subset: width, height, colspan and rowspan are whole numbers", "%s: %s=\"%s\"", what, name, val);
            if (!strcmp(name, "align")) HUNT_SPECF(!strcmp(val, "left") || !strcmp(val, "center") || !strcmp(val, "right"), "docs/formats.md, HTML subset: align is left, center or right", "%s: align=\"%s\"", what, val);
            for (k = i + 1; k < n->nattrs; k++) HUNT_CHECKF(strcmp(name, n->attrs[k].name) != 0, "%s: the attribute %s appears twice on <%s>", what, name, ex_html_tag_name(n->tag));
            HUNT_CHECKF(valid_utf8_nonul(val, strlen(val)), "%s: attribute value that is not valid UTF-8", what);
        }
        if (n->tag == EX_HTML_BR || n->tag == EX_HTML_HR || n->tag == EX_HTML_IMG) HUNT_CHECKF(n->nkids == 0, "%s: a void element has children", what);
    }
    for (i = 0; i < n->nkids; i++) {
        HUNT_CHECKF(n->kids[i]->parent == n, "%s: a child whose parent pointer is not its parent", what);
        if (depth < 200) walk(n->kids[i], depth + 1, what, src);
    }
    (void)src;
}

static void check_tree(const char *html, size_t len, const char *what, int forbid_script_text) {
    ExHtml *h = ex_html_parse(html, len);
    const ExHtmlNode *root;
    char *out, *text;
    HUNT_CHECKF(h != NULL, "%s: no tree", what);
    if (!h) return;
    root = ex_html_root(h);
    HUNT_CHECK(root && root->tag == EX_HTML_TEXT && root->text == NULL);
    g_max_depth = 0; g_nodes = 0;
    if (root) walk(root, 0, what, html);
    HUNT_SPECF(g_max_depth <= 66, "docs/formats.md, HTML subset: nesting deeper than 64 levels is flattened", "%s: the tree is %lu levels deep", what, (unsigned long)g_max_depth);
    text = ex_html_text(h);
    HUNT_CHECKF(text != NULL, "%s: no text", what);
    if (text && forbid_script_text) HUNT_SPECF(!strstr(text, "ALERT_PAYLOAD"), "docs/formats.md, HTML subset: script and style lose their content as well", "%s: the content of a script or style came out as text: %.80s", what, text);
    out = ex_html_serialize(h);
    HUNT_CHECKF(out != NULL, "%s: nothing to write", what);
    if (out) {
        /* the cleaned text read again gives the same cleaned text */
        ExHtml *h2 = ex_html_parse(out, strlen(out));
        char *out2 = h2 ? ex_html_serialize(h2) : NULL;
        HUNT_SPECF(out2 && !strcmp(out, out2), "docs/formats.md, HTML subset: cleaning is stable: cleaning the cleaned text changes nothing", "%s:\n    first:  %.200s\n    second: %.200s", what, out, out2 ? out2 : "(null)");
        if (out2) {
            if (!strstr(out2, "<script") && !strstr(out2, "<style")) HUNT_CHECKF(!strstr(out, "<script") && !strstr(out, "<style") && !strstr(out, "<iframe") && !strstr(out, "<object") && !strstr(out, "<embed") && !strstr(out, "<form"), "%s: a tag outside the subset was written: %.100s", what, out);
        }
        HUNT_CHECKF(!strstr(out, " onerror") && !strstr(out, " onclick") && !strstr(out, " onload") && !strstr(out, " style=") && !strstr(out, " class="), "%s: a dropped attribute was written: %.100s", what, out);
        free(out2); ex_html_free(h2);
        free(out);
    }
    free(text);
    ex_html_free(h);
}

static void test_trees(void) {
    const char *corpus[] = {
        "<p>plain</p>", "text only", "", "<", ">", "<>", "</>", "<a", "<a ", "<a href", "<a href=", "<a href=\"", "<a href=\"x", "<a href=\"x\"", "<!--", "<!-- unterminated", "<!-- a -- b -->", "<!--->", "<!---->",
        "<script>ALERT_PAYLOAD</script>", "<script>ALERT_PAYLOAD", "<SCRIPT>ALERT_PAYLOAD</SCRIPT>", "<script >ALERT_PAYLOAD</script >", "<script/>ALERT_PAYLOAD", "<scr<script>ipt>ALERT_PAYLOAD</scr</script>ipt>", "<script><!--ALERT_PAYLOAD--></script>",
        "<style>ALERT_PAYLOAD{}</style>", "<style>", "<STYLE>ALERT_PAYLOAD", "<svg><script>ALERT_PAYLOAD</script></svg>", "<math><script>ALERT_PAYLOAD</script></math>", "<script src=x></script>after", "<script>a</script><b>after</b>",
        "<iframe src=\"http://x\">frame text</iframe>", "<object data=x>object text</object>", "<embed src=x>", "<form action=x><input name=a><button>go</button></form>", "<font color=red>f</font>", "<center>c</center>", "<unknown>kept text</unknown>",
        "<img src=x onerror=alert(1)>", "<img src=\"x\" onerror=\"alert(1)\" style=\"x:y\" class=\"c\" alt=\"a\">", "<IMG SRC=x ONERROR=y>", "<a href=x onclick=y>t</a>", "<p style=\"color:red\" class=a id=b title=c>t</p>", "<p ID=A TITLE=B>t</p>",
        "<a href=\"a\" href=\"b\">dup</a>", "<a href='single'>s</a>", "<a href=unquoted>u</a>", "<a href>novalue</a>", "<a href=\"\">empty</a>", "<a hRef=\"x\">case</a>", "<a\nhref=\"x\"\n>newline</a>", "<a/href=x>slash</a>", "<a\thref=\"x\">tab</a>",
        "<img width=100 height=50>", "<img width=\"1e3\" height=\"-5\">", "<img width=\"10px\">", "<img width=\"99999999999999999999\">", "<td colspan=2 rowspan=\"3\">c</td>", "<td colspan=x>c</td>", "<p align=center>c</p>", "<p align=justify>c</p>", "<p align=\"CENTER\">c</p>",
        "<details open><summary>s</summary>d</details>", "<details open=\"open\">d</details>", "<details open=false>d</details>",
        "<b><i>x</b></i>", "<b><i>x</b>y</i>", "<b>unclosed", "<p>one<p>two<p>three", "<ul><li>a<li>b</ul>", "<li>outside", "<table><tr><td>a<td>b<tr><td>c</table>", "<table><td>no tr</table>", "<div><p>a</div>b</p>", "</div>stray close", "</b></i></p>",
        "<a href=x><a href=y>nested anchors</a></a>", "<p><div>block in inline</div></p>", "<h1><h2>x</h2></h1>", "<br/><br /><br>", "<hr/>", "<img/>", "<br>text</br>", "<hr>text</hr>", "<p/>", "<div/>after",
        "<!DOCTYPE html><html><head><title>t</title></head><body>b</body></html>", "<?xml version=\"1.0\"?>", "<![CDATA[ALERT_PAYLOAD]]>", "<!ELEMENT x>", "<![if IE]>x<![endif]>",
        "a < b and c > d", "1<2", "<3", "x<y>z", "a&lt;b", "&lt;script&gt;ALERT_PAYLOAD&lt;/script&gt;", "&lt;b&gt;bold&lt;/b&gt;", "&#60;script&#62;x&#60;/script&#62;",
        "<a href=\"&#x6A;avascript:x\">x</a>", "<a href=\"&amp;#106;avascript:x\">x</a>", "<a href=\"java&#10;script:x\">x</a>", "<img src=\"&#x64;ata:image/png;base64,AAAA\">",
        "<b>\xC3\xA9</b><i>\xF0\x9F\x98\x80</i>", "<p>\xFF\xFE</p>", "<p>\xC3</p>", "<p title=\"\xFF\">x</p>", "<a\x01href=x>c</a>", "<a href=\"a\x01b\">c</a>", "<p>a\0b</p>",
    };
    size_t i;
    hunt_step("trees: the corpus");
    for (i = 0; i < sizeof corpus / sizeof *corpus; i++) {
        size_t l = strlen(corpus[i]);
        char msg[64];
        snprintf(msg, sizeof msg, "corpus %lu", (unsigned long)i);
        hunt_stepf("html corpus %lu", (unsigned long)i);
        hunt_set_input(corpus[i], l, "html");
        check_tree(corpus[i], l, msg, 1);
        hunt_clear_input();
    }
    { static const char nulhtml[] = "<p>a\0b</p><script>ALERT_PAYLOAD\0</script>"; check_tree(nulhtml, sizeof nulhtml - 1, "NUL bytes in the text", 1); }

    hunt_step("trees: documented behaviors");
    {
        struct { const char *html, *text_want; } tw[] = {
            { "<p>a</p><script>x</script><p>b</p>", "ab" }, { "<b>bold</b> and <i>it</i>", "bold and it" }, { "<!-- c -->shown", "shown" }, { "<iframe>inside</iframe>", "inside" }, { "<unknown>kept</unknown>", "kept" },
            { "a&amp;b", "a&b" }, { "<style>p{}</style>text", "text" }, { "<a href=x>link</a>", "link" },
        };
        size_t k;
        for (k = 0; k < sizeof tw / sizeof *tw; k++) {
            ExHtml *h = ex_html_parse(tw[k].html, strlen(tw[k].html));
            char *t = h ? ex_html_text(h) : NULL;
            HUNT_SPECF(t && strstr(t, tw[k].text_want) != NULL, "docs/formats.md, HTML subset", "%s gave the text \"%s\", want \"%s\" in it", tw[k].html, t ? t : "(null)", tw[k].text_want);
            free(t); ex_html_free(h);
        }
        {
            ExHtml *h = ex_html_parse("<p id=a>x</p>", 13);
            const ExHtmlNode *root = ex_html_root(h), *p = root && root->nkids ? root->kids[0] : NULL;
            HUNT_CHECK(p && p->tag == EX_HTML_P && ex_html_attr(p, "id") && !strcmp(ex_html_attr(p, "id"), "a") && ex_html_attr(p, "nope") == NULL);
            ex_html_free(h);
        }
        {
            ExHtml *h = ex_html_parse("<b>unclosed <i>deeper", 21);
            char *w = h ? ex_html_serialize(h) : NULL;
            HUNT_SPECF(w && strstr(w, "</b>") && strstr(w, "</i>"), "docs/formats.md, HTML subset: a tag that is not closed is closed at the end of its parent", "wrote %s", w ? w : "(null)");
            free(w); ex_html_free(h);
        }
        {
            ExHtml *h = ex_html_parse("<p>ab</p>", 5);               /* the length cuts the input: "<p>ab" */
            char *t = h ? ex_html_text(h) : NULL;
            HUNT_CHECK(t && !strcmp(t, "ab"));
            free(t); ex_html_free(h);
        }
    }

    hunt_step("trees: random tag soup");
    {
        static const char *const bits[] = { "<p>", "</p>", "<b>", "</b>", "<i>", "</i>", "<a href=\"", "http://x\"", "javascript:x\"", ">", "<", "/", "=", "\"", "'", " ", "\n", "text", "&amp;", "&#x41;", "&#0;", "&", "<script>", "</script>", "<style>", "</style>",
            "<!--", "-->", "<div>", "</div>", "<span ", "class=a ", "onclick=x ", "style=\"x\" ", "<table>", "<tr>", "<td colspan=2>", "</td>", "<br>", "<img src=x ", "alt=\"y\">", "<li>", "<ul>", "</ul>", "<h1>", "</h1>", "<details open>", "<pre>", "</pre>", "<code>", "\xC3\xA9", "\xF0\x9F\x98\x80", "\xFF", "<?", "?>", "<![", "]]>", "<!DOCTYPE " };
        long rounds = hunt_iters(8000), r;
        for (r = 0; r < rounds; r++) {
            char text[500];
            size_t n = 0, parts = 1 + hunt_rnd_size(25);
            while (parts--) { const char *b = bits[hunt_rnd(sizeof bits / sizeof *bits)]; size_t l = strlen(b); if (n + l + 1 > sizeof text) break; memcpy(text + n, b, l); n += l; }
            text[n] = 0;
            hunt_stepf("html soup %ld", r);
            hunt_set_input(text, n, "html");
            check_tree(text, n, "random tag soup", 0);
            hunt_clear_input();
            if (hunt_.failures > 40) break;
        }
    }
    hunt_step("trees: random mutations of a page");
    {
        const char *seed = "<div class=\"x\"><h1>Title</h1><p align=\"center\" style=\"c\">Para with <a href=\"https://example.com/?a=1&amp;b=2\">link</a>, <b>bold</b>, <i>it<u>al</u></i> &amp; &#x41;.</p><table><thead><tr><th colspan=2>H</th></tr></thead><tbody><tr><td>1</td><td>2</td></tr></tbody></table><img src=\"a.png\" width=\"100\" alt=\"A\"><script>ALERT_PAYLOAD</script><!-- c --><details open><summary>S</summary>D</details></div>";
        size_t sl = strlen(seed);
        long rounds = hunt_iters(8000), r;
        for (r = 0; r < rounds; r++) {
            unsigned char *buf = (unsigned char *)malloc(sl * 2 + 64);
            size_t l;
            memcpy(buf, seed, sl);
            l = hunt_mutate(buf, sl, sl * 2 + 32);
            hunt_stepf("html mutation %ld", r);
            hunt_set_input(buf, l, "mutated html");
            check_tree((const char *)buf, l, "mutated page", 0);
            hunt_clear_input();
            free(buf);
            if (hunt_.failures > 40) break;
        }
    }
}

/* ---- E. slow input ---- */

static void timed(const char *name, const char *html, size_t len, double limit) {
    clock_t t0 = clock();
    ExHtml *h;
    char *out, *text;
    double s;
    hunt_stepf("html timing: %s (%lu bytes)", name, (unsigned long)len);
    h = ex_html_parse(html, len);
    out = h ? ex_html_serialize(h) : NULL;
    text = h ? ex_html_text(h) : NULL;
    s = hunt_cpu_since(t0);
    HUNT_CHECKF(h != NULL, "%s: no tree", name);
    if (s > hunt_time_limit(limit)) { HUNT_SUSPECT(0, "parsing, writing and extracting text took longer than the limit"); hunt_note_("%s: %.2f s for %lu bytes (limit %.1f s)", name, s, (unsigned long)len, hunt_time_limit(limit)); }
    if (h) { g_max_depth = 0; g_nodes = 0; walk(ex_html_root(h), 0, name, html); HUNT_SPECF(g_max_depth <= 66, "docs/formats.md, HTML subset: nesting deeper than 64 levels is flattened", "%s: %lu levels", name, (unsigned long)g_max_depth); }
    free(out); free(text); ex_html_free(h);
}

static char *rep(const char *unit, size_t n, const char *pre, const char *post, size_t *len) {
    size_t u = strlen(unit), p = strlen(pre), q = strlen(post), i;
    char *s = (char *)malloc(p + u * n + q + 1);
    memcpy(s, pre, p);
    for (i = 0; i < n; i++) memcpy(s + p + i * u, unit, u);
    memcpy(s + p + u * n, post, q + 1);
    *len = p + u * n + q;
    return s;
}

static void test_slow(void) {
    size_t n, i;
    char *s;
    struct { const char *name, *unit; size_t times; } r[] = {
        { "100000 open divs", "<div>", 100000 }, { "100000 open spans", "<span>", 100000 }, { "100000 open bold", "<b>", 100000 }, { "50000 nested lists", "<ul><li>", 50000 }, { "50000 nested tables", "<table><tr><td>", 50000 },
        { "a million less-thans", "<", 1000000 }, { "300000 unclosed tags", "<a ", 300000 }, { "200000 broken comments", "<!-", 200000 }, { "200000 ampersands", "&", 200000 }, { "100000 character references", "&#x41;", 100000 },
        { "100000 unknown references", "&abcdefg;", 100000 }, { "200000 unclosed quotes", "<a href=\"", 200000 }, { "100000 br", "<br>", 100000 }, { "100000 paragraphs", "<p>", 100000 }, { "100000 closing tags", "</b>", 100000 },
        { "100000 mis-nested pairs", "<b><i></b></i>", 25000 }, { "100000 images", "<img src=x>", 100000 }, { "200000 script openers", "<script>", 200000 }, { "100000 style openers", "<style>x", 100000 },
        { "100000 definitions", "<!DOCTYPE ", 100000 }, { "200000 cdata openers", "<![CDATA[", 200000 },
    };
    for (i = 0; i < sizeof r / sizeof *r; i++) {
        size_t times = (size_t)((double)r[i].times * (hunt_.scale_den > 1 ? 0.3 : 1.0)) + 1;
        s = rep(r[i].unit, times, "", "", &n);
        timed(r[i].name, s, n, 3.0);
        free(s);
    }
    {
        size_t attrs = 20000, p = 0, k;
        s = (char *)malloc(attrs * 24 + 64);
        p += (size_t)sprintf(s, "<a ");
        for (k = 0; k < attrs; k++) p += (size_t)sprintf(s + p, "a%lu=1 ", (unsigned long)k);
        p += (size_t)sprintf(s + p, "href=x>t</a>");
        timed("20000 attributes on one tag", s, p, 3.0);
        free(s);
        p = 0;
        s = (char *)malloc(attrs * 24 + 64);
        p += (size_t)sprintf(s, "<a ");
        for (k = 0; k < attrs; k++) p += (size_t)sprintf(s + p, "href=%lu ", (unsigned long)k);
        p += (size_t)sprintf(s + p, ">t</a>");
        timed("20000 repeated href attributes", s, p, 3.0);
        free(s);
    }
    {
        size_t big = 8u << 20;
        s = (char *)malloc(big + 64);
        memcpy(s, "<a href=\"", 9); memset(s + 9, 'x', big); memcpy(s + 9 + big, "\">t</a>", 8);
        timed("an 8 MB attribute", s, 9 + big + 7, 3.0);
        free(s);
        s = (char *)malloc(big + 64);
        memcpy(s, "<p>", 3); memset(s + 3, 'y', big); memcpy(s + 3 + big, "</p>", 5);
        timed("an 8 MB paragraph", s, 3 + big + 4, 3.0);
        free(s);
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_html", argc, argv);
    test_urls();
    test_entities();
    test_trees();
    test_slow();
    return hunt_end();
}
