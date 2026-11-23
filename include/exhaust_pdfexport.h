/* Export of a note to PDF (sub-slice 6e-iv): the note's reading model is laid out page by page and written with Exhaust's own PDF writer
 * (exhaust_pdfwrite.h). Always light-styled (the built-in Exhaust Light theme), never the user's theme. Runs on a thread of its own: it owns its fonts and
 * caches and touches nothing of the interface. See docs/formats.md (Export to PDF). */
#ifndef EXHAUST_PDFEXPORT_H
#define EXHAUST_PDFEXPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExPdfExportOptions {
    const char *page_size;      /* A3, A4, A5, Legal, Letter or Tabloid (anything else is A4) */
    int landscape;
    const char *margin;         /* "default", "minimal" or "none" */
    int downscale_percent;      /* 10 to 200; 100 is the normal size */
    int include_title;          /* the file name as a heading above the note */
} ExPdfExportOptions;

typedef struct ExPdfExportIn {
    const char *title;          /* the note's name without the extension: the PDF's title and, with include_title, the heading */
    const char *text; size_t len;   /* the note's text with its embedded notes and bases shown (what reading mode draws) */
    ExPdfExportOptions opt;
    /* A file of the vault by the name written in the note (`![[photo.png]]`): 1 with its full path in `path`. May be NULL: pictures then show as their names. */
    int (*resolve)(void *user, const char *name, char *path, size_t cap);
    void *user;
    /* Called from the export thread: fraction in thousandths (0 to 1000). May be NULL. */
    void (*progress)(void *user, int permille, int page);
    /* Checked often; the export stops with "Cancelled" when it becomes nonzero. May be NULL. */
    const volatile int *cancel;
} ExPdfExportIn;

typedef struct ExPdfExportOut {
    unsigned char *pdf; size_t len;    /* malloc'ed */
    int pages;
    char err[200];                     /* empty on success */
    int cancelled;
} ExPdfExportOut;

/* 0 on success. Any thread (it makes its own font library). */
int ex_pdf_export(const ExPdfExportIn *in, ExPdfExportOut *out);
void ex_pdf_export_free(ExPdfExportOut *out);

/* The page size in points for a name (portrait): returns 0 and sets w, h; -1 for an unknown name. */
int ex_pdf_page_size(const char *name, float *w, float *h);

#ifdef __cplusplus
}
#endif
#endif
