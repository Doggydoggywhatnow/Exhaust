/* font subsetting (sub-slice 6e-iv): a font is cut down to the characters a document uses with HarfBuzz's hb-subset. FreeType must open the result, find
 * the kept characters and none of the others, and the file must be much smaller. Done for a TrueType font (Atkinson Hyperlegible) and for an OpenType font
 * with CFF outlines (STIX Two Math), the two kinds the export embeds. Glyph ids are kept, so the ids the page's text uses stay valid. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_fontsubset.h"
#include "exhaust_platform.h"
#include "exhaust_text.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void one(const char *path, const char *keep, const char *absent) {
    size_t len = 0, slen = 0, i;
    unsigned char *data = (unsigned char *)ex_platform_read_file(path, &len), *sub;
    ExFontLib *lib = ex_fontlib_create();
    ExFont *orig, *cut;
    uint32_t glyphs[16], uni[16];
    size_t ng = 0, nu = 0;
    CHECK(data && len > 1000 && lib);
    if (!data || !lib) { free(data); ex_fontlib_destroy(lib); return; }
    orig = ex_font_open_memory(lib, data, len);
    CHECK(orig != NULL);
    for (i = 0; keep[i]; i++) { uni[nu++] = (unsigned char)keep[i]; glyphs[ng++] = ex_font_glyph_index(orig, (unsigned char)keep[i]); }
    for (i = 0; i < ng; i++) CHECK(glyphs[i] != 0);
    sub = ex_font_subset(data, len, glyphs, ng, uni, nu, &slen);
    CHECK(sub != NULL && slen > 100 && slen < len / 4);
    if (sub) {
        cut = ex_font_open_memory(lib, sub, slen);
        CHECK(cut != NULL);
        if (cut) {
            for (i = 0; keep[i]; i++) {
                uint32_t g = ex_font_glyph_index(cut, (unsigned char)keep[i]);
                CHECK(g != 0 && g == glyphs[i]);                                   /* the same glyph id as in the whole font */
                CHECK(ex_font_advance(cut, g, 1000) == ex_font_advance(orig, g, 1000));
            }
            for (i = 0; absent[i]; i++) CHECK(ex_font_glyph_index(cut, (unsigned char)absent[i]) == 0);
        }
        /* a subset made from glyph ids alone keeps those glyphs usable by id (the PDF writer addresses glyphs by id) */
        free(sub);
        sub = ex_font_subset(data, len, glyphs, ng, NULL, 0, &slen);
        CHECK(sub != NULL && slen < len / 4);
        free(sub);
    }
    CHECK(ex_font_subset(data, 10, glyphs, ng, NULL, 0, &slen) == NULL);              /* not a font */
    ex_fontlib_destroy(lib);
    free(data);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "third_party/fonts";
    char p[600];
    snprintf(p, sizeof p, "%s/atkinson-hyperlegible/AtkinsonHyperlegible-Regular.ttf", dir);
    one(p, "A", "BCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    snprintf(p, sizeof p, "%s/stix-two-math/STIXTwoMath-Regular.otf", dir);
    one(p, "x+", "yz=-ABC");
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("font_subset ok\n");
    return 0;
}
