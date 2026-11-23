/* the PDF writer (sub-slice 6e-iv): a document with two pages, two fonts, a shape, a picture with transparency and a link is written and passes the
 * structure check (cross-reference table, every font embedded, subset and with a ToUnicode map, the text recovered through the maps, no dark fills),
 * and the check refuses files that were damaged. With the environment variable EXHAUST_PDF_OUT the file is also kept for looking at. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_pdfwrite.h"
#include "exhaust_platform.h"
#include "exhaust_text.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct Fnt { ExFont *f; } Fnt;
static float adv(void *u, uint32_t g) { return ex_font_advance(((Fnt *)u)->f, g, 1000); }

static void run(ExFont *f, int pf, float size, float x, float y, const char *s, ExPdfDoc *d, ExPdfColor col) {
    uint32_t g[64], cp[64];
    size_t n = 0;
    for (; *s && n < 64; s++) { cp[n] = (unsigned char)*s; g[n] = ex_font_glyph_index(f, (unsigned char)*s); n++; }
    ex_pdfw_text(d, pf, size, x, y, g, cp, n, col);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "third_party/fonts";
    char p[600];
    size_t len1 = 0, len2 = 0, plen = 0, i;
    unsigned char *d1, *d2, *pdf = NULL;
    ExFontLib *lib = ex_fontlib_create();
    ExFont *a, *m;
    Fnt fa, fm;
    ExPdfDoc *doc = ex_pdfw_create();
    ExPdfColor ink = {0.1f, 0.1f, 0.1f}, panel = {0.95f, 0.95f, 0.95f}, dark = {0.05f, 0.05f, 0.05f};
    ExPdfCheck chk;
    uint8_t px[4 * 4 * 4];
    int fa_id, fm_id, im;
    snprintf(p, sizeof p, "%s/atkinson-hyperlegible/AtkinsonHyperlegible-Regular.ttf", dir);
    d1 = (unsigned char *)ex_platform_read_file(p, &len1);
    snprintf(p, sizeof p, "%s/atkinson-hyperlegible-mono/AtkinsonHyperlegibleMono-Regular.ttf", dir);
    d2 = (unsigned char *)ex_platform_read_file(p, &len2);
    CHECK(d1 && d2 && lib && doc);
    if (!d1 || !d2 || !lib || !doc) return 1;
    a = ex_font_open_memory(lib, d1, len1); m = ex_font_open_memory(lib, d2, len2);
    fa.f = a; fm.f = m;
    fa_id = ex_pdfw_font_add(doc, "AtkinsonHyperlegible", d1, len1, adv, &fa, 950, 250, 0, 0);
    fm_id = ex_pdfw_font_add(doc, "AtkinsonHyperlegibleMono", d2, len2, adv, &fm, 950, 250, 1, 0);
    CHECK(fa_id == 0 && fm_id == 1);
    for (i = 0; i < 16; i++) { px[4 * i] = (uint8_t)(i * 16); px[4 * i + 1] = 100; px[4 * i + 2] = (uint8_t)(255 - i * 16); px[4 * i + 3] = (uint8_t)(i < 8 ? 255 : 128); }
    im = ex_pdfw_image_add(doc, px, 4, 4);
    CHECK(im == 0);
    CHECK(ex_pdfw_page_begin(doc, 595.0f, 842.0f) == 0);
    ex_pdfw_set_title(doc, "Sample \xC3\xA9 title");
    ex_pdfw_rect(doc, 40, 40, 200, 30, 4, &panel, NULL, 0);
    run(a, fa_id, 12, 48, 60, "Hello PDF world", doc, ink);
    run(a, fa_id, 12, 48, 90, "second line (with parens) \\ and a backslash", doc, ink);
    run(m, fm_id, 10, 48, 120, "mono text 123", doc, ink);
    ex_pdfw_image_draw(doc, im, 40, 150, 80, 80);
    ex_pdfw_link(doc, 48, 50, 100, 14, "https://example.org/a(b)c d");
    ex_pdfw_ellipse(doc, 200, 200, 20, 10, &panel, &ink, 1);
    { float tri[6] = {300, 300, 340, 300, 320, 330}; ex_pdfw_poly(doc, tri, 3, 1, NULL, &ink, 1, 1); }
    CHECK(ex_pdfw_page_begin(doc, 595.0f, 842.0f) == 1);
    ex_pdfw_clip_begin(doc, 40, 40, 100, 30);
    run(a, fa_id, 12, 48, 60, "Page two", doc, ink);
    ex_pdfw_clip_end(doc);
    CHECK(ex_pdfw_page_count(doc) == 2);
    CHECK(ex_pdfw_finish(doc, &pdf, &plen) == 0 && pdf && plen > 1000);
    if (!pdf) return 1;
    {   const char *out = getenv("EXHAUST_PDF_OUT");
        if (out && *out) ex_platform_write_file_atomic(out, pdf, plen); }
    CHECK(ex_pdfw_check(pdf, plen, &chk) == 0 && chk.ok);
    if (!chk.ok) fprintf(stderr, "check: %s\n", chk.err);
    CHECK(chk.pages == 2 && chk.fonts == 2 && chk.fonts_embedded == 2 && chk.fonts_subset == 2 && chk.fonts_tounicode == 2);
    CHECK(chk.images == 1 && chk.dark_fills == 0);
    CHECK(chk.text && !strcmp(chk.text, "Hello PDF world\nsecond line (with parens) \\ and a backslash\nmono text 123\nPage two\n"));
    if (chk.text && strcmp(chk.text, "Hello PDF world\nsecond line (with parens) \\ and a backslash\nmono text 123\nPage two\n")) fprintf(stderr, "text: [%s]\n", chk.text);
    ex_pdfw_check_free(&chk);
    CHECK(plen < len1 / 2);                                                   /* the fonts were cut down: smaller than one of them whole */
    {   /* damaged files are refused */
        unsigned char *bad = malloc(plen);
        memcpy(bad, pdf, plen);
        bad[plen - 8] ^= 0x01;                                                /* the startxref offset */
        CHECK(ex_pdfw_check(bad, plen, &chk) != 0 && !chk.ok);
        ex_pdfw_check_free(&chk);
        memcpy(bad, pdf, plen);
        CHECK(ex_pdfw_check(bad, plen - 20, &chk) != 0);
        ex_pdfw_check_free(&chk);
        memcpy(bad, pdf, plen);
        {   size_t k;
            for (k = 0; k + 12 < plen; k++) if (!memcmp(bad + k, "/Type /Pages", 12)) { bad[k + 1] = 'X'; break; }
            CHECK(ex_pdfw_check(bad, plen, &chk) != 0);
            ex_pdfw_check_free(&chk); }
        free(bad);
    }
    {   /* a dark rectangle is reported */
        ExPdfDoc *d2c = ex_pdfw_create();
        ExPdfCheck c2;
        unsigned char *o2; size_t l2;
        ex_pdfw_page_begin(d2c, 100, 100);
        ex_pdfw_rect(d2c, 0, 0, 100, 100, 0, &dark, NULL, 0);
        CHECK(ex_pdfw_finish(d2c, &o2, &l2) == 0);
        CHECK(ex_pdfw_check(o2, l2, &c2) == 0 && c2.dark_fills == 1);
        ex_pdfw_check_free(&c2); free(o2); ex_pdfw_free(d2c);
    }
    free(pdf);
    ex_pdfw_free(doc);
    ex_fontlib_destroy(lib);
    free(d1); free(d2);
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("pdf_writer ok\n");
    return 0;
}
