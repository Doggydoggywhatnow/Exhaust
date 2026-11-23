/* pdf_loader (6c-iv): PDFium loaded at run time and the document service. Without the library everything says so and nothing fails; with a stand-in
 * library (tests/stub_pdfium.c) a document opens on its own thread and reports its pages, sizes, title and outline; pages are rendered, their text and links
 * read, and the document searched, all by requests whose results are polled; a damaged file, a page out of range and a request after close are harmless.
 * Usage: test_pdf <path of the stand-in library> <a file that starts with %PDF> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_pdf.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint8_t *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*len + 1);
    if (b && fread(b, 1, *len, f) != *len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

/* waits for a result of the kind (up to 2 seconds); the caller frees it */
static int wait_result(ExPdf *d, ExPdfKind kind, ExPdfResult *out) {
    int i;
    for (i = 0; i < 2000; i++) {
        ExPdfResult r;
        while (ex_pdf_poll(d, &r)) { if (r.kind == kind) { *out = r; return 1; } ex_pdf_result_free(&r); }
        SDL_Delay(1);
    }
    return 0;
}

int main(int argc, char **argv) {
    char err[300] = "";
    size_t len = 0;
    uint8_t *file;
    ExPdf *d;
    const ExPdfInfo *info;
    ExPdfResult r;
    int total_hits, i;
    const char *text = "Page 1 of the sample. The quick brown fox.";
    if (argc < 3) return 2;

    /* ---- without the library ---- */
    CHECK(!ex_pdf_loaded());
    CHECK(ex_pdf_load("/nonexistent/libpdfium.so", err, sizeof err) == 0 && err[0]);
    file = read_all(argv[2], &len);
    CHECK(file != NULL);
    CHECK(ex_pdf_open(file, len, err, sizeof err) == NULL && err[0]);                          /* the bytes are freed by the call */
    ex_pdf_close(NULL); ex_pdf_close_async(NULL);
    CHECK(ex_pdf_request_render(NULL, 0, 10, 10) == 0 && ex_pdf_poll(NULL, &r) == 0);

    /* ---- with the stand-in ---- */
    CHECK(ex_pdf_load(argv[1], err, sizeof err) == 1 && ex_pdf_loaded());
    {   /* a file that is not a PDF */
        uint8_t *junk = malloc(40);
        memset(junk, 'x', 40);
        CHECK(ex_pdf_open(junk, 40, err, sizeof err) == NULL && err[0]);
    }
    file = read_all(argv[2], &len);
    d = ex_pdf_open(file, len, err, sizeof err);
    CHECK(d != NULL);
    if (!d) { fprintf(stderr, "open: %s\n", err); return 1; }
    info = ex_pdf_info(d);
    CHECK(info->pages == 3 && info->w[0] == 612.0 && info->h[0] == 792.0 && info->w[1] == 400.0 && info->h[1] == 300.0);
    CHECK(!strcmp(info->title, "Sample document"));
    CHECK(info->ntoc == 3 && !strcmp(info->toc[0].title, "Chapter 1") && info->toc[0].page == 0 && info->toc[0].depth == 0);
    CHECK(!strcmp(info->toc[1].title, "Section 1.1") && info->toc[1].page == 1 && info->toc[1].depth == 1);
    CHECK(!strcmp(info->toc[2].title, "Chapter 2") && info->toc[2].page == 2 && info->toc[2].depth == 0);

    /* ---- render ---- */
    CHECK(ex_pdf_request_render(d, 0, 306, 396) != 0);
    CHECK(wait_result(d, EX_PDF_RENDER, &r) && r.page == 0 && r.w == 306 && r.h == 396 && r.rgba != NULL);
    if (r.rgba) {
        CHECK(r.rgba[3] == 255 && r.rgba[(size_t)(400 / 4) * 0 + 0] == 0x80);                  /* the grey block at the top left, made opaque */
        CHECK(r.rgba[((size_t)200 * 306 + 100) * 4] == 255 && r.rgba[((size_t)200 * 306 + 100) * 4 + 3] == 255);   /* white elsewhere */
        ex_pdf_result_free(&r);
    }
    CHECK(ex_pdf_request_render(d, 7, 100, 100) != 0);                                         /* a page that is not there: no result, no crash */
    CHECK(ex_pdf_request_render(d, 1, 0, 100) != 0);                                           /* a size that is not one */
    CHECK(ex_pdf_request_render(d, 2, 9000, 100) != 0);
    CHECK(ex_pdf_request_render(d, 1, 200, 150) != 0);
    CHECK(wait_result(d, EX_PDF_RENDER, &r) && r.page == 1 && r.w == 200 && r.h == 150);
    ex_pdf_result_free(&r);

    /* ---- text and boxes ---- */
    CHECK(ex_pdf_request_text(d, 0) != 0);
    CHECK(wait_result(d, EX_PDF_TEXT, &r) && r.page == 0 && r.nchars == strlen(text));
    if (r.chars) {
        CHECK(r.chars[0] == 'P' && r.chars[5] == '1' && r.chars[r.nchars - 1] == '.');
        CHECK(r.boxes[5 * 4] == 50.0f && r.boxes[5 * 4 + 1] == 700.0f && r.boxes[5 * 4 + 2] == 59.0f && r.boxes[5 * 4 + 3] == 712.0f);
        ex_pdf_result_free(&r);
    }

    /* ---- links: the address is reported, a launch action and a link with no target are dropped ---- */
    CHECK(ex_pdf_request_links(d, 0) != 0);
    CHECK(wait_result(d, EX_PDF_LINKS, &r) && r.page == 0 && r.nlinks == 2);
    if (r.nlinks == 2) {
        CHECK(r.links[0].page == 2 && r.links[0].uri[0] == 0);
        CHECK(r.links[1].page == -1 && !strcmp(r.links[1].uri, "https://example.org/"));
        CHECK(r.links[0].l == 10.0f && r.links[0].r == 110.0f);
    }
    ex_pdf_result_free(&r);

    /* ---- search: every page has one "quick"; the case option; the last piece says it is over ---- */
    CHECK(ex_pdf_request_search(d, "QUICK", 0) != 0);
    total_hits = 0;
    for (i = 0; i < 2000; i++) {
        ExPdfResult s;
        int done = 0;
        while (ex_pdf_poll(d, &s)) {
            if (s.kind == EX_PDF_SEARCH) {
                size_t k;
                for (k = 0; k < s.nhits; k++) CHECK(s.hits[k].count == 5 && s.hits[k].start == (int)(strstr(text, "quick") - text));
                total_hits += (int)s.nhits;
                if (s.done) done = 1;
            }
            ex_pdf_result_free(&s);
        }
        if (done) break;
        SDL_Delay(1);
    }
    CHECK(total_hits == 3);
    CHECK(ex_pdf_request_search(d, "QUICK", 1) != 0);                                          /* match case: nothing */
    total_hits = 0;
    for (i = 0; i < 2000; i++) {
        ExPdfResult s;
        int done = 0;
        while (ex_pdf_poll(d, &s)) { if (s.kind == EX_PDF_SEARCH) { total_hits += (int)s.nhits; if (s.done) done = 1; } ex_pdf_result_free(&s); }
        if (done) break;
        SDL_Delay(1);
    }
    CHECK(total_hits == 0);
    CHECK(ex_pdf_request_search(d, "", 0) == 0);                                               /* nothing to look for */

    /* ---- closing with work waiting ---- */
    for (i = 0; i < 20; i++) ex_pdf_request_render(d, i % 3, 300 + i, 400);
    ex_pdf_request_search(d, "fox", 0);
    ex_pdf_close(d);
    d = ex_pdf_open(read_all(argv[2], &len), len, err, sizeof err);
    CHECK(d != NULL);
    ex_pdf_request_render(d, 0, 100, 100);
    ex_pdf_close_async(d);                                                                    /* on a thread of its own */
    SDL_Delay(100);
    ex_pdf_unload();
    CHECK(!ex_pdf_loaded());
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("pdf_loader: ok\n");
    return 0;
}
