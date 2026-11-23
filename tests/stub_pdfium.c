/* A stand-in for PDFium (6c-iv tests): the part of its C API that Exhaust uses, over a made-up document of three pages. A file that does not begin with `%PDF`
 * is refused. Page 1 (612 by 792 points) has a link to page 3, a link to https://example.org/, a launch action and a link with no target; page 2 is 400 by 300;
 * the text of page n is "Page n of the sample. The quick brown fox." with a 10-point box for each character. The outline is "Chapter 1" (page 1) with the
 * child "Section 1.1" (page 2), then "Chapter 2" (page 3). Rendering fills the bitmap with white and a grey block whose width is the page number in tenths.
 * The library counts the calls it should never get (form fill, JavaScript, saving): there are no such functions here at all, so a program that needs one
 * fails to load the library. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fpdfview.h"
#include "fpdf_doc.h"
#include "fpdf_text.h"

#define NPAGES 3
typedef struct Doc { int ok; } Doc;
typedef struct Page { int index; } Page;
typedef struct Text { int index; uint16_t chars[64]; int n; } Text;
typedef struct Sch { Text *t; uint16_t what[64]; int nwhat; unsigned long flags; int pos; int found, count; } Sch;
typedef struct Mark { int id; } Mark;
typedef struct Bitmap { int w, h, stride; uint8_t *buf; } Bitmap;

static Mark MARKS[3] = { { 1 }, { 2 }, { 3 } };       /* Chapter 1, Section 1.1, Chapter 2 */
static int DEST_PAGE[8];                              /* dest handles are pointers into this array: the page index */
static int ACTION[4];

static void page_text(int index, Text *t) {
    static const char tail[] = " of the sample. The quick brown fox.";
    char buf[64];
    int i, n = 0;
    buf[n++] = 'P'; buf[n++] = 'a'; buf[n++] = 'g'; buf[n++] = 'e'; buf[n++] = ' '; buf[n++] = (char)('1' + index);
    for (i = 0; tail[i] && n < 63; i++) buf[n++] = tail[i];
    for (i = 0; i < n; i++) t->chars[i] = (uint16_t)(unsigned char)buf[i];
    t->n = n; t->index = index;
}

FPDF_EXPORT void FPDF_CALLCONV FPDF_InitLibraryWithConfig(const FPDF_LIBRARY_CONFIG *c) { (void)c; }
FPDF_EXPORT void FPDF_CALLCONV FPDF_DestroyLibrary() {}
FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV FPDF_LoadMemDocument(const void *d, int size, FPDF_BYTESTRING pw) {
    Doc *doc;
    (void)pw;
    if (size < 8 || memcmp(d, "%PDF", 4) != 0) return NULL;
    doc = calloc(1, sizeof *doc); doc->ok = 1;
    return (FPDF_DOCUMENT)doc;
}
FPDF_EXPORT void FPDF_CALLCONV FPDF_CloseDocument(FPDF_DOCUMENT d) { free(d); }
FPDF_EXPORT int FPDF_CALLCONV FPDF_GetPageCount(FPDF_DOCUMENT d) { (void)d; return NPAGES; }
FPDF_EXPORT FPDF_PAGE FPDF_CALLCONV FPDF_LoadPage(FPDF_DOCUMENT d, int i) { Page *p; (void)d; if (i < 0 || i >= NPAGES) return NULL; p = calloc(1, sizeof *p); p->index = i; return (FPDF_PAGE)p; }
FPDF_EXPORT void FPDF_CALLCONV FPDF_ClosePage(FPDF_PAGE p) { free(p); }
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDF_GetPageSizeByIndexF(FPDF_DOCUMENT d, int i, FS_SIZEF *s) { (void)d; if (i < 0 || i >= NPAGES) return 0; s->width = i == 1 ? 400.0f : 612.0f; s->height = i == 1 ? 300.0f : 792.0f; return 1; }
FPDF_EXPORT FPDF_BITMAP FPDF_CALLCONV FPDFBitmap_CreateEx(int w, int h, int fmt, void *scan, int stride) { Bitmap *b; (void)fmt; if (!scan) return NULL; b = calloc(1, sizeof *b); b->w = w; b->h = h; b->stride = stride; b->buf = scan; return (FPDF_BITMAP)b; }
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDFBitmap_FillRect(FPDF_BITMAP bm, int l, int t, int w, int h, FPDF_DWORD color) {
    Bitmap *b = (Bitmap *)bm;
    int x, y;
    for (y = t; y < t + h && y < b->h; y++) for (x = l; x < l + w && x < b->w; x++) { uint8_t *p = b->buf + (size_t)y * (size_t)b->stride + (size_t)x * 4; p[0] = (uint8_t)color; p[1] = (uint8_t)(color >> 8); p[2] = (uint8_t)(color >> 16); p[3] = (uint8_t)(color >> 24); }
    return 1;
}
FPDF_EXPORT void *FPDF_CALLCONV FPDFBitmap_GetBuffer(FPDF_BITMAP bm) { return ((Bitmap *)bm)->buf; }
FPDF_EXPORT int FPDF_CALLCONV FPDFBitmap_GetStride(FPDF_BITMAP bm) { return ((Bitmap *)bm)->stride; }
FPDF_EXPORT void FPDF_CALLCONV FPDFBitmap_Destroy(FPDF_BITMAP bm) { free(bm); }
FPDF_EXPORT void FPDF_CALLCONV FPDF_RenderPageBitmap(FPDF_BITMAP bm, FPDF_PAGE page, int sx, int sy, int w, int h, int rotate, int flags) {
    Bitmap *b = (Bitmap *)bm;
    int x, y, block = w * (((Page *)page)->index + 1) / 10;
    (void)rotate; (void)flags; (void)sx; (void)sy;
    for (y = 0; y < h / 4 && y < b->h; y++) for (x = 0; x < block && x < b->w; x++) { uint8_t *p = b->buf + (size_t)y * (size_t)b->stride + (size_t)x * 4; p[0] = 0x80; p[1] = 0x80; p[2] = 0x80; p[3] = 255; }
}

FPDF_EXPORT FPDF_TEXTPAGE FPDF_CALLCONV FPDFText_LoadPage(FPDF_PAGE p) { Text *t = calloc(1, sizeof *t); page_text(((Page *)p)->index, t); return (FPDF_TEXTPAGE)t; }
FPDF_EXPORT void FPDF_CALLCONV FPDFText_ClosePage(FPDF_TEXTPAGE t) { free(t); }
FPDF_EXPORT int FPDF_CALLCONV FPDFText_CountChars(FPDF_TEXTPAGE t) { return ((Text *)t)->n; }
FPDF_EXPORT unsigned int FPDF_CALLCONV FPDFText_GetUnicode(FPDF_TEXTPAGE t, int i) { Text *x = (Text *)t; return i >= 0 && i < x->n ? x->chars[i] : 0; }
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDFText_GetCharBox(FPDF_TEXTPAGE t, int i, double *l, double *r, double *b, double *tp) { Text *x = (Text *)t; if (i < 0 || i >= x->n) return 0; *l = 10.0 * i; *r = 10.0 * i + 9.0; *b = 700.0; *tp = 712.0; return 1; }
FPDF_EXPORT FPDF_SCHHANDLE FPDF_CALLCONV FPDFText_FindStart(FPDF_TEXTPAGE t, FPDF_WIDESTRING what, unsigned long flags, int start) {
    Sch *s = calloc(1, sizeof *s);
    const uint16_t *w = (const uint16_t *)what;
    s->t = (Text *)t; s->flags = flags; s->pos = start;
    while (w[s->nwhat] && s->nwhat < 63) { s->what[s->nwhat] = w[s->nwhat]; s->nwhat++; }
    return (FPDF_SCHHANDLE)s;
}
static uint16_t fold(uint16_t c, unsigned long flags) { return (flags & FPDF_MATCHCASE) || c < 'A' || c > 'Z' ? c : (uint16_t)(c + 32); }
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDFText_FindNext(FPDF_SCHHANDLE h) {
    Sch *s = (Sch *)h;
    int i, k;
    for (i = s->pos; i + s->nwhat <= s->t->n; i++) {
        for (k = 0; k < s->nwhat; k++) if (fold(s->t->chars[i + k], s->flags) != fold(s->what[k], s->flags)) break;
        if (k == s->nwhat && s->nwhat > 0) { s->found = i; s->count = s->nwhat; s->pos = i + 1; return 1; }
    }
    return 0;
}
FPDF_EXPORT int FPDF_CALLCONV FPDFText_GetSchResultIndex(FPDF_SCHHANDLE h) { return ((Sch *)h)->found; }
FPDF_EXPORT int FPDF_CALLCONV FPDFText_GetSchCount(FPDF_SCHHANDLE h) { return ((Sch *)h)->count; }
FPDF_EXPORT void FPDF_CALLCONV FPDFText_FindClose(FPDF_SCHHANDLE h) { free(h); }

FPDF_EXPORT FPDF_BOOKMARK FPDF_CALLCONV FPDFBookmark_GetFirstChild(FPDF_DOCUMENT d, FPDF_BOOKMARK parent) {
    (void)d;
    if (!parent) return (FPDF_BOOKMARK)&MARKS[0];
    if (((Mark *)parent)->id == 1) return (FPDF_BOOKMARK)&MARKS[1];
    return NULL;
}
FPDF_EXPORT FPDF_BOOKMARK FPDF_CALLCONV FPDFBookmark_GetNextSibling(FPDF_DOCUMENT d, FPDF_BOOKMARK b) { (void)d; return ((Mark *)b)->id == 1 ? (FPDF_BOOKMARK)&MARKS[2] : NULL; }
FPDF_EXPORT unsigned long FPDF_CALLCONV FPDFBookmark_GetTitle(FPDF_BOOKMARK b, void *buf, unsigned long len) {
    static const char *names[] = { "Chapter 1", "Section 1.1", "Chapter 2" };
    const char *n = names[((Mark *)b)->id - 1];
    unsigned long i, l = (unsigned long)strlen(n);
    uint16_t *w = buf;
    if (buf && len >= (l + 1) * 2) for (i = 0; i <= l; i++) w[i] = (uint16_t)(unsigned char)n[i];
    return (l + 1) * 2;
}
FPDF_EXPORT FPDF_DEST FPDF_CALLCONV FPDFBookmark_GetDest(FPDF_DOCUMENT d, FPDF_BOOKMARK b) { static const int page[3] = { 0, 1, 2 }; (void)d; DEST_PAGE[page[((Mark *)b)->id - 1]] = page[((Mark *)b)->id - 1]; return (FPDF_DEST)&DEST_PAGE[page[((Mark *)b)->id - 1]]; }
FPDF_EXPORT int FPDF_CALLCONV FPDFDest_GetDestPageIndex(FPDF_DOCUMENT d, FPDF_DEST dest) { (void)d; return (int)((int *)dest - DEST_PAGE); }

/* the links of page 1: an internal one (to page 3), an address, a launch action, and one without a target */
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDFLink_Enumerate(FPDF_PAGE page, int *pos, FPDF_LINK *link) {
    static int L[4] = { 0, 1, 2, 3 };
    if (((Page *)page)->index != 0 || *pos >= 4) return 0;
    *link = (FPDF_LINK)&L[*pos];
    (*pos)++;
    return 1;
}
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV FPDFLink_GetAnnotRect(FPDF_LINK l, FS_RECTF *r) { int i = *(int *)l; r->left = 10.0f; r->right = 110.0f; r->bottom = 600.0f - 40.0f * (float)i; r->top = r->bottom + 20.0f; return 1; }
FPDF_EXPORT FPDF_ACTION FPDF_CALLCONV FPDFLink_GetAction(FPDF_LINK l) { int i = *(int *)l; if (i == 0) { ACTION[0] = 1; return (FPDF_ACTION)&ACTION[0]; } if (i == 1) { ACTION[1] = 3; return (FPDF_ACTION)&ACTION[1]; } if (i == 2) { ACTION[2] = 4; return (FPDF_ACTION)&ACTION[2]; } return NULL; }
FPDF_EXPORT FPDF_DEST FPDF_CALLCONV FPDFLink_GetDest(FPDF_DOCUMENT d, FPDF_LINK l) { (void)d; (void)l; return NULL; }
FPDF_EXPORT unsigned long FPDF_CALLCONV FPDFAction_GetType(FPDF_ACTION a) { return (unsigned long)*(int *)a; }
FPDF_EXPORT unsigned long FPDF_CALLCONV FPDFAction_GetURIPath(FPDF_DOCUMENT d, FPDF_ACTION a, void *buf, unsigned long len) { static const char u[] = "https://example.org/"; (void)d; (void)a; if (buf && len >= sizeof u) memcpy(buf, u, sizeof u); return sizeof u; }
FPDF_EXPORT FPDF_DEST FPDF_CALLCONV FPDFAction_GetDest(FPDF_DOCUMENT d, FPDF_ACTION a) { (void)d; (void)a; DEST_PAGE[2] = 2; return (FPDF_DEST)&DEST_PAGE[2]; }
FPDF_EXPORT unsigned long FPDF_CALLCONV FPDF_GetMetaText(FPDF_DOCUMENT d, FPDF_BYTESTRING tag, void *buf, unsigned long len) {
    static const char t[] = "Sample document";
    unsigned long i;
    uint16_t *w = buf;
    (void)d;
    if (strcmp(tag, "Title") != 0) return 2;
    if (buf && len >= sizeof t * 2) for (i = 0; i < sizeof t; i++) w[i] = (uint16_t)(unsigned char)t[i];
    return sizeof t * 2;
}
