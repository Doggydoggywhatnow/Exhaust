/* The PDF tab (6c-iv): a PDF document (include/exhaust_pdf.h) as a column of pages. Nothing here calls PDFium: pages are drawn from textures the document's own
 * thread rendered, and text, links and search results arrive as results of its requests. Only the pages near the window are rendered and kept.
 *
 * Toolbar: Contents (the outline), Fit width, Fit page, zoom out and in, the page number, Search. Mouse: the wheel scrolls (Ctrl+wheel zooms about the pointer);
 * a press and drag on text selects it; a click on a link inside the document goes there; a click on a link to an address asks the application (which asks the
 * user) and does nothing else; a click on the outline goes to its page. Keys: arrows, Page Up, Page Down, Home, End scroll; + - 0 zoom; Ctrl+C copies the
 * selection; Ctrl+F opens the search (typing searches the whole document; Enter and Shift+Enter move between the hits; Escape closes it; the two switches
 * "Aa" (match case) and "All" (highlight all hits) are clickable). Ctrl+Shift+C copies a link to the selection, `[[file.pdf#page=3&selection=120,148]]`: the
 * page and the first character and the one after the last of the text on it; opening such a link goes to the page and selects that text.
 *
 * The document is opened on a worker (the `load` handler) and handed over with ex_pdfview_set_doc; the widget owns it from then on and closes it when it goes. */
#ifndef EXHAUST_PDFVIEW_H
#define EXHAUST_PDFVIEW_H
#include "exhaust_pdf.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExPdfViewHandlers {
    void *user;
    void (*load)(void *user, const char *path);                      /* asks for the file to be read and opened (a worker, then ex_pdfview_set_doc) */
    void (*open_external)(void *user, const char *path);             /* the file in the system's own program */
    void (*open_url)(void *user, const char *url);                   /* a link to an address was clicked: the application asks the user before it opens anything */
    void (*set_clipboard)(void *user, const char *utf8);
    void (*message)(void *user, const char *text);
    /* the name to put in a link to this file (the shortest that is unique in the vault); fills out */
    void (*link_name)(void *user, const char *path, char *out, size_t cap);
} ExPdfViewHandlers;

ExWidget *ex_widget_pdfview(ExUi *ui, const char *path, const ExPdfViewHandlers *h);
void ex_pdfview_draw(ExUi *ui, ExWidget *w);
void ex_pdfview_wheel(ExWidget *w, float dy, int ctrl, float x, float y);
/* The application's answer to `load`: the document (the widget owns it) or the reason it could not be opened. */
void ex_pdfview_set_doc(ExWidget *w, ExPdf *doc, const char *error);
const char *ex_pdfview_path(const ExWidget *w);
int ex_pdfview_has_doc(const ExWidget *w);
/* Goes to a page (0-based) and, when sel_end > sel_start, selects those characters of it. */
void ex_pdfview_goto(ExWidget *w, int page, int sel_start, int sel_end);
/* The page number and selection in a link: "page=3&selection=120,148" (what follows the # of a link). Returns 1 when a page was found. */
int ex_pdf_parse_fragment(const char *frag, int *page, int *sel_start, int *sel_end);
int ex_pdfview_copy_selection(ExWidget *w);               /* the selected text to the clipboard: 1, or 0 when nothing is selected */
int ex_pdfview_copy_link(ExWidget *w);                    /* a link to the selection (or, with none, to the page) to the clipboard */

typedef struct ExPdfViewStats {
    int loading, failed, has_doc;
    int pages, current_page;
    float scale;                      /* screen pixels per point as drawn */
    int mode;                         /* 0 fit width, 1 fit page, 2 custom */
    int toc_open, search_open;
    size_t hits, hit_index;
    int sel_page, sel_start, sel_end;
    size_t textures;                  /* pages with a texture kept */
    unsigned long uploads;
    float scroll;
    float toc_x, toc_y, fitw_x, fitw_y, fitp_x, fitp_y, zin_x, zin_y, search_x, search_y;   /* where the toolbar's buttons are */
    float page0_x, page0_y, page0_w, page0_h;                                              /* the first page's box on screen */
    double own_ms;
} ExPdfViewStats;
void ex_pdfview_stats(const ExWidget *w, ExPdfViewStats *out);
/* A character's box on screen (for the tests); 0 when its page's text is not here yet. */
int ex_pdfview_char_rect(const ExWidget *w, int page, int index, float *x, float *y, float *wd, float *ht);
/* The first link of a page's box on screen (for the tests); 0 when the links are not here yet. */
int ex_pdfview_link_rect(const ExWidget *w, int page, size_t index, float *x, float *y, float *wd, float *ht);
/* Types text into the search box, as the keyboard does (for the tests). */
void ex_pdfview_search(ExWidget *w, const char *utf8);

#ifdef __cplusplus
}
#endif
#endif
