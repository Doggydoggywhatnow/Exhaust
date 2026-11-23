/* libFuzzer target for the Mermaid parsers and layouts (6d-ii, 6d-iii): any bytes as the text of a ```mermaid block. Must not crash, hang, read past the
 * input or leak; a diagram that cannot be drawn has an error and no items. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "exhaust_diagram.h"
#include "exhaust_text.h"

static ExFontLib *g_lib;
static ExFont *g_font;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ExDiagram *d;
    char dump[4096];
    if (!g_lib) {
        g_lib = ex_fontlib_create();
        g_font = g_lib ? ex_font_builtin(g_lib, EX_FONT_UI_REGULAR) : NULL;
    }
    if (!g_font) abort();
    d = ex_diagram_parse(g_font, (const char *)data, size);
    if (!d) abort();
    ex_diagram_dump(d, dump, sizeof dump);
    ex_diagram_free(d);
    return 0;
}
