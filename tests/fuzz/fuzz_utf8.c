/* libFuzzer target for the UTF-8 decoder and the grapheme-cluster segmenter: any
 * bytes in, and the invariants of both must hold (progress, bounds, re-encoding). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "exhaust_text.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *s = (const char *)data;
    size_t i = 0, b = 0, prev;
    char out[4];

    while (i < size) {           /* decoding always moves forward and stays inside the input */
        size_t before = i;
        uint32_t cp = ex_utf8_decode(s, size, &i);
        if (i <= before || i > size) abort();
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) abort();
        if (ex_utf8_encode(cp, out) < 1) abort();
    }
    while (b < size) {           /* cluster boundaries are strictly increasing and end at the length */
        prev = b;
        b = ex_grapheme_next(s, size, b);
        if (b <= prev || b > size) abort();
    }
    if (size > 0 && ex_grapheme_prev(s, size, size) >= size) abort();
    return 0;
}
