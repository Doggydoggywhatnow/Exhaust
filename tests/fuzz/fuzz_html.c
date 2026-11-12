/* libFuzzer target for the HTML subset: any bytes in. The result must contain only documented tags and
 * attributes with safe values, and cleaning the cleaned text must change nothing. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"

static int allowed_attr(const char *n) {
    static const char *const ok[] = {"href", "src", "alt", "title", "width", "height", "colspan", "rowspan", "align", "open", "id"};
    size_t i;
    for (i = 0; i < sizeof ok / sizeof ok[0]; i++) if (strcmp(n, ok[i]) == 0) return 1;
    return 0;
}

static void walk(const ExHtmlNode *n, int depth) {
    size_t i;
    if (depth > 70) abort();                                                    /* the nesting limit */
    if (n->tag >= EX_HTML_TAG_COUNT) abort();
    for (i = 0; i < n->nattrs; i++) {
        if (!allowed_attr(n->attrs[i].name)) abort();
        if ((strcmp(n->attrs[i].name, "href") == 0 || strcmp(n->attrs[i].name, "src") == 0) && !ex_html_url_allowed(n->attrs[i].value, strlen(n->attrs[i].value))) abort();
    }
    for (i = 0; i < n->nkids; i++) { if (n->kids[i]->parent != n) abort(); walk(n->kids[i], depth + 1); }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ExHtml *h = ex_html_parse((const char *)data, size), *h2;
    char *once, *twice;
    if (!h) return 0;
    walk(ex_html_root(h), 0);
    once = ex_html_serialize(h);
    if (!once) { ex_html_free(h); return 0; }
    h2 = ex_html_parse(once, strlen(once));
    twice = h2 ? ex_html_serialize(h2) : NULL;
    if (twice && strcmp(once, twice) != 0) abort();                              /* cleaning is stable */
    free(once); free(twice);
    ex_html_free(h2);
    ex_html_free(h);
    return 0;
}
