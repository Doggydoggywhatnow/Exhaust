/* libFuzzer target for the Bases expression engine: any bytes in. Parsing must not crash; whatever parses is run against a host that answers some questions
 * and not others, its value is turned into text and into its repr, and the same text run twice gives the same repr (no clock or random in it). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base_expr.h"

static int note_prop(void *u, ExArena *a, const char *name, ExValue *out) {
    (void)u;
    if (!strcmp(name, "price")) { *out = ex_v_num(12.5); return 1; }
    if (!strcmp(name, "title")) { *out = ex_v_str(a, "Some Title"); return 1; }
    if (!strcmp(name, "x")) { ExValue l[2]; l[0] = ex_v_num(1); l[1] = ex_v_str(a, "two"); *out = ex_v_list(a, l, 2); return 1; }
    return 0;
}
static int file_field(void *u, ExArena *a, const char *path, const char *field, ExValue *out) {
    (void)u; (void)path;
    if (!strcmp(field, "tags")) { ExValue t[2]; t[0] = ex_v_str(a, "#a"); t[1] = ex_v_str(a, "#b/c"); *out = ex_v_list(a, t, 2); return 1; }
    if (!strcmp(field, "name")) { *out = ex_v_str(a, "Name.md"); return 1; }
    if (!strcmp(field, "links")) { ExValue t[1]; t[0] = ex_v_link(a, "Note", NULL, NULL); *out = ex_v_list(a, t, 1); return 1; }
    if (!strcmp(field, "properties")) { const char *k[1] = { "role" }; ExValue v = ex_v_str(a, "r"); *out = ex_v_object(a, k, &v, 1); return 1; }
    return 0;
}
static int resolve(void *u, const char *from, const char *text, char *path, size_t cap) { (void)u; (void)from; if (!strcmp(text, "Note")) { strncpy(path, "Note.md", cap); path[cap - 1] = 0; return 1; } return 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *text = malloc(size + 1);
    ExArena *a = ex_arena_new();
    ExExprHost h;
    ExExpr *e;
    ExValue v, w;
    char *r1, *r2, *t;
    if (!text || !a) { free(text); ex_arena_free(a); return 0; }
    memcpy(text, data, size);
    text[size] = 0;
    memset(&h, 0, sizeof h);
    h.note_prop = note_prop; h.file_field = file_field; h.resolve = resolve;
    h.file_path = "Notes/Name.md"; h.this_path = "Note.md"; h.now_ms = 1748354709123.0;
    e = ex_expr_parse(text, NULL, 0, NULL);
    if (e) {
        v = ex_expr_eval(e, a, &h, NULL);
        r1 = ex_value_repr(a, v);
        t = ex_value_to_string(a, &h, v);
        (void)t;
        w = ex_expr_eval(e, a, &h, NULL);
        r2 = ex_value_repr(a, w);
        if (!strstr(text, "random") && strcmp(r1, r2)) abort();        /* the same expression gives the same value (random() is meant to give another: CI found "random()" in 60 seconds, ISSUES 84) */
        ex_value_compare(v, w);
        ex_value_truthy(v);
        ex_expr_free(e);
    }
    ex_arena_free(a);
    free(text);
    return 0;
}
