/* libFuzzer target for the math layout (6d-i): any bytes as the text between the dollar signs, in both styles. Must not crash, hang or read past the
 * input, and always gives a layout (the source itself when it cannot be parsed). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "exhaust_math.h"
#include "exhaust_text.h"

static ExFontLib *g_lib;
static ExFont *g_font;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int d;
    if (!g_lib) {
        g_lib = ex_fontlib_create();
        g_font = g_lib ? ex_font_builtin(g_lib, EX_FONT_MATH) : NULL;
    }
    if (!g_font) abort();
    for (d = 0; d < 2; d++) {
        ExMath *m = ex_math_layout(g_font, (const char *)data, size, d);
        if (!m) abort();
        ex_math_free(m);
    }
    return 0;
}
