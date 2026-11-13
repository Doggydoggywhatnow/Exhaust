/* libFuzzer target for the frontmatter: any bytes in. Reading the properties must not crash; editing a property must
 * leave every byte outside its lines alone, and the result must still read. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_extract.h"
#include "exhaust_props.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *text = (const char *)data;
    ExExtract *x = ex_extract(text, size), *y;
    if (!x) return 0;
    if (x->nprops && x->front_valid) {
        const ExXProp *p = &x->props[0];
        size_t n = 0;
        char *name = malloc(strlen(p->name) + 1), *out;
        if (name) memcpy(name, p->name, strlen(p->name) + 1);
        if (!name) { ex_extract_free(x); return 0; }
        out = ex_prop_text_remove(text, size, name, &n);
        if (out) {
            if (n != size - (p->end - p->start) || memcmp(out, text, p->start) || memcmp(out + p->start, text + p->end, size - p->end)) abort();
            y = ex_extract(out, n);
            if (!y) abort();
            ex_extract_free(y);
            free(out);
        }
        out = ex_prop_text_set(text, size, name, EX_PROP_TEXT, "value: with # chars", &n);
        if (out) {
            y = ex_extract(out, n);
            if (!y) abort();
            if (y->front_valid) {
                size_t i, found = 0;
                for (i = 0; i < y->nprops; i++) if (!strcmp(y->props[i].name, name)) { found = 1; if (strcmp(y->props[i].value, "value: with # chars")) abort(); }
                if (!found) abort();
            }
            ex_extract_free(y);
            free(out);
        }
        free(name);
    }
    ex_extract_free(x);
    return 0;
}
