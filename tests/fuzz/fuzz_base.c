/* libFuzzer target for `.base` files: any bytes in. Reading must not crash; what reads can be written and what is written reads back to the same text;
 * every view of it runs over a small set of files (filters, formulas, sort, groups and summaries are whatever the bytes say) without a crash, and the exports run. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base.h"

static ExBaseData *make_data(void) {
    ExBaseData *d = calloc(1, sizeof *d);
    ExBaseFile *f;
    size_t i;
    for (i = 0; i < 12; i++) {
        char p[40], v[16];
        snprintf(p, sizeof p, "Folder %zu/Note %zu.md", i % 3, i);
        f = ex_base_data_add(d, p, 1, 1.7e12, 1.7e12 + (double)i * 1000.0, (double)(i * 100));
        snprintf(v, sizeof v, "%zu", i * 7 % 13);
        ex_base_file_prop(f, "price", EXB_NUMBER, v); ex_base_file_prop(f, "status", EXB_TEXT, i % 2 ? "todo" : "done"); ex_base_file_prop(f, "age", EXB_NUMBER, v);
        ex_base_file_prop(f, "read", EXB_DATE, "2025-01-15"); ex_base_file_prop(f, "author", EXB_LINKS, "[[Note 1]]\n[[Note 2]]");
        if (i % 2) ex_base_file_tag(f, "#tag");
        if (i % 3 == 0) ex_base_file_link(f, "Note 4");
    }
    return d;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ExBase *b = ex_base_parse((const char *)data, size, NULL, 0), *again;
    char *out, *out2;
    size_t n = 0, m = 0, v;
    ExBaseData *d;
    if (!b) return 0;
    out = ex_base_write(b, &n);
    if (!out) { ex_base_free(b); return 0; }
    again = ex_base_parse(out, n, NULL, 0);
    if (!again) abort();                                            /* what is written must read back */
    out2 = ex_base_write(again, &m);
    if (!out2 || m != n || memcmp(out, out2, n)) abort();
    free(out); free(out2);
    ex_base_free(again);
    d = make_data();
    for (v = 0; v < ex_base_view_count(b) && v < 8; v++) {
        ExBaseResult r;
        if (ex_base_run(b, v, d, "Folder 0/Note 0.md", 1.75e12, 0, &r) == 0) {
            char *csv = ex_base_result_csv(&r, NULL), *md = ex_base_result_markdown(&r, NULL);
            size_t i, j;
            for (i = 0; i < r.nrows && i < 20; i++) for (j = 0; j < r.ncols; j++) (void)ex_base_cell_text(&r, i, j);
            free(csv); free(md);
            ex_base_result_free(&r);
        }
    }
    ex_base_data_free(d); free(d);
    ex_base_free(b);
    return 0;
}
