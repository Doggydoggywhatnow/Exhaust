/* The HTML subset (docs/formats.md, "Markdown tokens and the HTML subset"). HTML in a note is parsed
 * here, never by a browser: only the documented tags and attributes are kept, character references are
 * decoded, unsafe URLs are removed, and anything else is dropped. The result is a small tree that
 * live preview, reading mode and the PDF export draw themselves. */
#ifndef EXHAUST_HTML_H
#define EXHAUST_HTML_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExHtmlTag {
    EX_HTML_TEXT = 0,
    EX_HTML_A, EX_HTML_ABBR, EX_HTML_B, EX_HTML_BLOCKQUOTE, EX_HTML_BR, EX_HTML_CAPTION, EX_HTML_CODE, EX_HTML_DEL, EX_HTML_DETAILS,
    EX_HTML_DIV, EX_HTML_EM, EX_HTML_H1, EX_HTML_H2, EX_HTML_H3, EX_HTML_H4, EX_HTML_H5, EX_HTML_H6, EX_HTML_HR, EX_HTML_I, EX_HTML_IMG,
    EX_HTML_INS, EX_HTML_KBD, EX_HTML_LI, EX_HTML_MARK, EX_HTML_OL, EX_HTML_P, EX_HTML_PRE, EX_HTML_Q, EX_HTML_S, EX_HTML_SAMP,
    EX_HTML_SMALL, EX_HTML_SPAN, EX_HTML_STRONG, EX_HTML_SUB, EX_HTML_SUMMARY, EX_HTML_SUP, EX_HTML_TABLE, EX_HTML_TBODY, EX_HTML_TD,
    EX_HTML_TFOOT, EX_HTML_TH, EX_HTML_THEAD, EX_HTML_TR, EX_HTML_U, EX_HTML_UL, EX_HTML_VAR,
    EX_HTML_TAG_COUNT
} ExHtmlTag;

typedef struct ExHtmlAttr { const char *name, *value; } ExHtmlAttr;   /* names are lowercase; values decoded */

typedef struct ExHtmlNode {
    ExHtmlTag tag;                    /* EX_HTML_TEXT: `text` holds the decoded text */
    const char *text;
    ExHtmlAttr *attrs; size_t nattrs;
    struct ExHtmlNode **kids; size_t nkids;
    struct ExHtmlNode *parent;
} ExHtmlNode;

typedef struct ExHtml ExHtml;

/* Parses a fragment. Never fails on bad input (it is cleaned up); NULL only when out of memory. */
ExHtml *ex_html_parse(const char *s, size_t len);
void ex_html_free(ExHtml *h);
const ExHtmlNode *ex_html_root(const ExHtml *h);       /* a node of tag EX_HTML_DIV-less container: tag 0 and text NULL, whose kids are the fragment */
const char *ex_html_tag_name(ExHtmlTag t);             /* "a", "div", ... */
/* The tag for a name (any case), or EX_HTML_TEXT (0) if it is not in the subset. */
ExHtmlTag ex_html_tag_from_name(const char *name, size_t len);
int ex_html_is_block_tag(ExHtmlTag t);
/* The value of an attribute, or NULL. */
const char *ex_html_attr(const ExHtmlNode *n, const char *name);
/* Is this URL one the subset allows (http, https, mailto, relative or a #fragment)? */
int ex_html_url_allowed(const char *url, size_t len);
/* Text with the character references decoded (&amp;, &#65;, ...); malloc'ed. */
char *ex_html_decode(const char *s, size_t len);
/* The text of the whole tree, tags removed (for search and tests). */
char *ex_html_text(const ExHtml *h);
/* The cleaned fragment written back as HTML (for tests and the clipboard). malloc'ed. */
char *ex_html_serialize(const ExHtml *h);

#ifdef __cplusplus
}
#endif
#endif
