/* libFuzzer target for the importers' parsers: the first byte picks one (HTML to Markdown, CSV, ZIP), the rest is the input. Nothing may crash, read or
 * write out of bounds, or run out of control (nesting, field and entry sizes are limited); an extracted ZIP entry is always inside its limit, and
 * the Markdown made from HTML is always NUL-terminated text. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_import.h"

static int count_rows(const char *const *f, const size_t *lens, size_t n, size_t row, void *user) {
    size_t i;
    (void)row;
    for (i = 0; i < n; i++) if (f[i][lens[i]] != 0) abort();                 /* every field is NUL-terminated */
    *(size_t *)user += n;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    switch (data[0] % 3) {
    case 0: {
        char *title = NULL, *md;
        size_t n = 0;
        md = ex_html_to_markdown((const char *)data + 1, size - 1, &title, &n);
        if (md && md[n] != 0) abort();
        free(md); free(title);
        break;
    }
    case 1: {
        size_t cells = 0;
        ex_csv_parse((const char *)data + 1, size - 1, count_rows, &cells);
        break;
    }
    default: {
        ExZip *z = ex_zip_open(data + 1, size - 1);
        if (z) {
            size_t i, n = ex_zip_count(z);
            for (i = 0; i < n && i < 50; i++) {
                size_t len = 0;
                unsigned char *e = ex_zip_extract(z, i, &len);
                if (e && e[len] != 0) abort();
                free(e);
            }
            ex_zip_close(z);
        }
        break;
    }
    }
    return 0;
}
