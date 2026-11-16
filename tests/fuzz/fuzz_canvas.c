/* libFuzzer target for JSON Canvas: any bytes in. Reading must not crash; what is read can be written, and what is written reads back to the
 * same canvas (writing it again gives the same text); editing (adding, raising and removing cards and edges) leaves a canvas that still writes
 * and reads. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_canvas.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ExCanvas *c = ex_canvas_parse((const char *)data, size, NULL, 0), *d;
    char *out, *again;
    size_t n = 0, m = 0, i;
    double x0, y0, x1, y1, ax, ay, bx, by;
    int s1, s2;
    if (!c) return 0;
    out = ex_canvas_write(c, &n);
    if (!out) { ex_canvas_free(c); return 0; }
    d = ex_canvas_parse(out, n, NULL, 0);
    if (!d) abort();                                              /* what is written must read back */
    again = ex_canvas_write(d, &m);
    if (!again || m != n || memcmp(again, out, n)) abort();       /* and write the same text */
    free(again); free(out);
    ex_canvas_free(d);
    /* the geometry on whatever came in */
    (void)ex_canvas_bounds(c, &x0, &y0, &x1, &y1);
    (void)ex_canvas_node_at(c, 0, 0);
    for (i = 0; i < c->ne && i < 64; i++) (void)ex_canvas_edge_ends(c, &c->edges[i], &ax, &ay, &s1, &bx, &by, &s2);
    for (i = 0; i < c->n && i < 64; i++) { size_t mem[8]; (void)ex_canvas_group_members(c, i, mem, 8); }
    /* edits */
    {
        int a = ex_canvas_add_node(c, EX_CN_TEXT, 1, 2, 3, 4), b = ex_canvas_add_node(c, EX_CN_GROUP, 0, 0, 100, 100);
        if (a >= 0 && b >= 0) {
            ex_canvas_set_string(c, &c->nodes[a].text, "edited \"text\"\n");
            ex_canvas_add_edge(c, (size_t)a, EX_CS_RIGHT, (size_t)b, EX_CS_NONE);
            ex_canvas_raise_node(c, (size_t)a);
        }
        if (c->n > 2) ex_canvas_remove_node(c, c->n / 2);
        if (c->ne > 0) ex_canvas_remove_edge(c, 0);
    }
    out = ex_canvas_write(c, &n);
    if (!out) abort();
    d = ex_canvas_parse(out, n, NULL, 0);
    if (!d) abort();
    ex_canvas_free(d); free(out);
    ex_canvas_free(c);
    return 0;
}
