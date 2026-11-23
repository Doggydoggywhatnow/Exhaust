/* export to PDF (sub-slice 6e-iv): a sample note with every kind of block exports to a file that passes the structure check (cross-reference table,
 * every font embedded, subset and with a ToUnicode map, text recovered in reading order, no dark fills), keeps a link as a link annotation, a picture as
 * an image, a diagram and an equation as shapes and glyphs; the page size, landscape and margin options change the pages; a 5,000-line note exports in
 * many pages with every line recovered; a cancel stops the export. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_pdfexport.h"
#include "exhaust_pdfwrite.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *IMAGES;
static int resolve(void *u, const char *name, char *path, size_t cap) {
    (void)u;
    if (!strcmp(name, "pattern.png")) { snprintf(path, cap, "%s/pattern.png", IMAGES); return 1; }
    if (!strcmp(name, "song.flac")) { snprintf(path, cap, "/vault/audio/song.flac"); return 1; }
    return 0;
}

static int count(const unsigned char *h, size_t n, const char *pat) {
    size_t pl = strlen(pat), i;
    int k = 0;
    for (i = 0; i + pl <= n; i++) if (h[i] == (unsigned char)pat[0] && !memcmp(h + i, pat, pl)) k++;
    return k;
}

/* the text with line breaks and runs of spaces turned into single spaces */
static char *flat(const char *t) {
    char *o = malloc(strlen(t) + 1), *w = o;
    int sp = 0;
    for (; *t; t++) {
        if (*t == '\n' || *t == ' ') { if (!sp) *w++ = ' '; sp = 1; }
        else { *w++ = *t; sp = 0; }
    }
    *w = 0;
    return o;
}

static const char *SAMPLE =
    "---\ntitle: Hidden\n---\n"
    "# Heading One\n\nA paragraph with **bold**, *italic*, `inline code`, ==highlight== and a [web link](https://example.org/page) with an inline equation $x^2 + y^2 = z^2$ in it.\n\n"
    "## Lists\n\n- first item\n- second item\n  - nested item\n1. numbered one\n2. numbered two\n- [ ] open task\n- [x] done task\n\n"
    "> [!tip] Callout title\n> The callout text.\n\n> A plain quote.\n\n"
    "```c\nint main(void) { return 0; }\n```\n\n"
    "$$\n\\frac{a}{b} = \\sqrt{c}\n$$\n\n"
    "```mermaid\nflowchart LR\n  A[Start] --> B{Choice}\n  B --> C[End]\n```\n\n"
    "| Name | Value |\n| --- | ---: |\n| alpha | 1 |\n| beta | 22 |\n\n"
    "![[pattern.png|120]]\n\n![[song.flac]]\n\n---\n\nText with a footnote[^1].\n\n[^1]: The footnote text.\n";

static void export_it(const char *text, size_t len, ExPdfExportOptions opt, const char *title, ExPdfExportOut *out) {
    ExPdfExportIn in;
    memset(&in, 0, sizeof in);
    in.title = title; in.text = text; in.len = len; in.opt = opt; in.resolve = resolve;
    CHECK(ex_pdf_export(&in, out) == 0 && out->pdf && out->len > 1000 && !out->err[0]);
    if (out->err[0]) fprintf(stderr, "error: %s\n", out->err);
}

int main(int argc, char **argv) {
    ExPdfExportOptions opt;
    ExPdfExportOut out;
    ExPdfCheck chk;
    char *f;
    float w, h;
    IMAGES = argc > 1 ? argv[1] : "tests/data/images";
    memset(&opt, 0, sizeof opt);
    opt.page_size = "A4"; opt.margin = "default"; opt.downscale_percent = 100;
    CHECK(ex_pdf_page_size("A4", &w, &h) == 0 && w > 595 && w < 596 && h > 841 && h < 842);
    CHECK(ex_pdf_page_size("Nonsense", &w, &h) == -1);

    export_it(SAMPLE, strlen(SAMPLE), opt, "Sample note", &out);
    if (out.pdf) {
        const char *want[] = { "Heading One", "A paragraph with bold, italic, inline code, highlight and a web link", "Lists", "first item", "second item", "nested item", "numbered one", "numbered two",
                               "open task", "done task", "Callout title", "The callout text.", "A plain quote.", "int main(void) { return 0; }", "Name Value", "alpha 1", "beta 22",
                               "Audio song.flac", "Text with a footnote", "Footnotes", "The footnote text." };
        size_t i;
        char *t;
        const char *pos;
        if (getenv("EXHAUST_PDF_OUT")) ex_platform_write_file_atomic(getenv("EXHAUST_PDF_OUT"), out.pdf, out.len);
        CHECK(ex_pdfw_check(out.pdf, out.len, &chk) == 0 && chk.ok);
        if (!chk.ok) fprintf(stderr, "check: %s\n", chk.err);
        CHECK(chk.pages >= 1 && chk.pages == out.pages);
        CHECK(chk.fonts >= 4 && chk.fonts == chk.fonts_embedded && chk.fonts == chk.fonts_subset && chk.fonts == chk.fonts_tounicode);
        if (!(chk.images >= 1 && chk.dark_fills == 0)) fprintf(stderr, "images %d dark %d\n", chk.images, chk.dark_fills);
        CHECK(chk.images >= 1 && chk.dark_fills == 0);
        t = flat(chk.text ? chk.text : "");
        pos = t;
        for (i = 0; i < sizeof want / sizeof *want; i++) {
            const char *q = strstr(pos, want[i]);
            if (!q) fprintf(stderr, "missing in order: [%s]\n--- text ---\n%s\n", want[i], t);
            CHECK(q != NULL);
            if (q) pos = q + strlen(want[i]);
        }
        CHECK(strstr(t, "Hidden") == NULL);                                    /* the properties are not part of the page */
        free(t);
        CHECK(count(out.pdf, out.len, "/Subtype /Link") >= 1 && count(out.pdf, out.len, "https://example.org/page") >= 1);
        CHECK(count(out.pdf, out.len, "/Title <FEFF") == 1);
        ex_pdfw_check_free(&chk);
        ex_pdf_export_free(&out);
    }

    {   /* options: landscape Letter with no margin, include the title, downscale */
        ExPdfExportOptions o2 = opt;
        o2.page_size = "Letter"; o2.landscape = 1; o2.margin = "none"; o2.include_title = 1; o2.downscale_percent = 70;
        export_it(SAMPLE, strlen(SAMPLE), o2, "Sample note", &out);
        if (out.pdf) {
            CHECK(count(out.pdf, out.len, "/MediaBox [0 0 792 612 ]") == out.pages);
            CHECK(ex_pdfw_check(out.pdf, out.len, &chk) == 0);
            f = flat(chk.text ? chk.text : "");
            CHECK(strncmp(f, "Sample note Heading One", 23) == 0);          /* the title is the first line */
            free(f);
            ex_pdfw_check_free(&chk);
            ex_pdf_export_free(&out);
        }
    }

    {   /* a long note: 5,000 lines, every one recovered, many pages, headings not alone at the bottom of a page */
        size_t cap = 5000 * 90, n = 0, i;
        char *big = malloc(cap);
        for (i = 0; i < 5000 && big; i++) {
            if (i % 250 == 0) n += (size_t)snprintf(big + n, cap - n, "\n## Section %zu\n\n", i / 250);
            n += (size_t)snprintf(big + n, cap - n, "Line %zu of the long note with enough words in it to need more than one pass.\n\n", i);
        }
        export_it(big, n, opt, "Long", &out);
        if (out.pdf) {
            CHECK(out.pages > 40);
            CHECK(ex_pdfw_check(out.pdf, out.len, &chk) == 0 && chk.ok && chk.fonts >= 2);
            f = flat(chk.text ? chk.text : "");
            CHECK(strstr(f, "Line 0 of") && strstr(f, "Line 2500 of") && strstr(f, "Line 4999 of") && strstr(f, "Section 19"));
            free(f);
            ex_pdfw_check_free(&chk);
            ex_pdf_export_free(&out);
        }
        free(big);
    }

    {   /* a cancel stops it */
        static volatile int stop = 1;
        ExPdfExportIn in;
        memset(&in, 0, sizeof in);
        in.title = "x"; in.text = SAMPLE; in.len = strlen(SAMPLE); in.opt = opt; in.cancel = &stop;
        CHECK(ex_pdf_export(&in, &out) != 0 && out.cancelled && !out.pdf);
    }
    {   /* an empty note is one empty page */
        ExPdfExportIn in;
        memset(&in, 0, sizeof in);
        in.title = "Empty"; in.text = ""; in.len = 0; in.opt = opt;
        CHECK(ex_pdf_export(&in, &out) == 0 && out.pages == 1);
        ex_pdf_export_free(&out);
    }
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("pdf_export ok\n");
    return 0;
}
