/* PDF (sub-slice 6c-iv): PDFium loaded at run time, and a document service.
 *
 * PDFium is BSD-3/Apache-2.0 and is never linked: it is opened with SDL_LoadObject from the add-on folder `addons` next to the program, from the path in
 * the environment variable EXHAUST_PDFIUM_LIB (tests), or by the system's name (`libpdfium.so`, `libpdfium.dylib`, `pdfium.dll`). Only three headers of its
 * public API are vendored. Without it `ex_pdf_open` says so and the interface offers "Open externally".
 *
 * PDFium may be used by one thread at a time, so a document owns **one thread** and every call into PDFium happens there: the interface thread posts
 * requests (render a page at a size, read a page's text with the box of every character, list a page's links, search the whole document) and polls the
 * results; it never calls into PDFium. A request for the same thing as one still waiting replaces it, so scrolling fast does not queue pages nobody sees.
 *
 * Privacy. PDFium is expected to be built without JavaScript (no V8) and without XFA; Exhaust never starts its form-fill environment, so no script, form
 * submission or launch action can run, and it calls none of PDFium's network, file or print functions. A link to a web address is only *reported* (a result of
 * `ex_pdf_request_links`); what to do with it is the application's, which asks the user first. The library is initialised with the configuration of
 * version 2 and no isolate. The document is read from memory; PDFium is given no path. */
#ifndef EXHAUST_PDF_H
#define EXHAUST_PDF_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ex_pdf_load(const char *path, char *err, size_t errcap);   /* 1 loaded (also when it already was), 0 with the reason */
int ex_pdf_loaded(void);
void ex_pdf_unload(void);                                       /* the tests */

typedef struct ExPdf ExPdf;
typedef struct ExPdfToc { char *title; int page; int depth; } ExPdfToc;      /* page is 0-based, -1 when the entry goes nowhere */
typedef struct ExPdfInfo {
    int pages;
    double *w, *h;                       /* the size of each page in points (1/72 inch), after its rotation */
    ExPdfToc *toc; size_t ntoc;          /* the outline (bookmarks), in order, with their depth */
    char title[256];                     /* the document's title, UTF-8, or empty */
} ExPdfInfo;

/* Opens a document (the decoder owns the bytes, also when it fails). Blocks until it is open: call it on a worker. NULL with the reason in err. */
ExPdf *ex_pdf_open(uint8_t *bytes, size_t len, char *err, size_t errcap);
const ExPdfInfo *ex_pdf_info(const ExPdf *d);
void ex_pdf_close(ExPdf *d);                                    /* stops the thread and frees everything */
void ex_pdf_close_async(ExPdf *d);                              /* the same on a thread of its own */

typedef enum ExPdfKind { EX_PDF_RENDER = 1, EX_PDF_TEXT, EX_PDF_LINKS, EX_PDF_SEARCH } ExPdfKind;
typedef struct ExPdfLink {
    float l, b, r, t;                    /* the box in page points, origin at the bottom left */
    int page;                            /* a link inside the document: the 0-based page, else -1 */
    char uri[512];                       /* a link to an address: the address (never opened by this module), else empty */
} ExPdfLink;
typedef struct ExPdfHit { int page; int start; int count; } ExPdfHit;      /* characters of the page's text */
typedef struct ExPdfResult {
    ExPdfKind kind;
    unsigned long ticket;
    int page;
    uint8_t *rgba; int w, h;                                    /* RENDER: w * h * 4 bytes, opaque, straight alpha */
    uint32_t *chars; float *boxes; size_t nchars;               /* TEXT: the page's characters (Unicode scalar values) and a box (l, b, r, t in points) for each */
    ExPdfLink *links; size_t nlinks;                            /* LINKS */
    ExPdfHit *hits; size_t nhits; int done;                     /* SEARCH: the hits found in some pages (the results of one request arrive in pieces); done: the last piece */
} ExPdfResult;

/* Requests. Each returns a ticket the result carries (0 when the document is closing). pixel sizes are at most 8000 on a side. */
unsigned long ex_pdf_request_render(ExPdf *d, int page, int w, int h);
unsigned long ex_pdf_request_text(ExPdf *d, int page);
unsigned long ex_pdf_request_links(ExPdf *d, int page);
unsigned long ex_pdf_request_search(ExPdf *d, const char *utf8, int match_case);
void ex_pdf_cancel_search(ExPdf *d);
/* The next finished result, or 0. The caller frees it with ex_pdf_result_free. */
int ex_pdf_poll(ExPdf *d, ExPdfResult *out);
void ex_pdf_result_free(ExPdfResult *r);

#ifdef __cplusplus
}
#endif
#endif
