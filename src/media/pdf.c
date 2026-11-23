/* PDFium at run time and the document service: see include/exhaust_pdf.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fpdfview.h"
#include "fpdf_doc.h"
#include "fpdf_text.h"
#include "exhaust_pdf.h"

/* ---- the library ---- */

typedef struct Api {
    SDL_SharedObject *lib;
    void (FPDF_CALLCONV *init_library_with_config)(const FPDF_LIBRARY_CONFIG *);
    void (FPDF_CALLCONV *destroy_library)(void);
    FPDF_DOCUMENT (FPDF_CALLCONV *load_mem_document)(const void *, int, FPDF_BYTESTRING);
    void (FPDF_CALLCONV *close_document)(FPDF_DOCUMENT);
    int (FPDF_CALLCONV *get_page_count)(FPDF_DOCUMENT);
    FPDF_PAGE (FPDF_CALLCONV *load_page)(FPDF_DOCUMENT, int);
    void (FPDF_CALLCONV *close_page)(FPDF_PAGE);
    FPDF_BOOL (FPDF_CALLCONV *get_page_size_by_index_f)(FPDF_DOCUMENT, int, FS_SIZEF *);
    FPDF_BITMAP (FPDF_CALLCONV *bitmap_create_ex)(int, int, int, void *, int);
    FPDF_BOOL (FPDF_CALLCONV *bitmap_fill_rect)(FPDF_BITMAP, int, int, int, int, FPDF_DWORD);
    void *(FPDF_CALLCONV *bitmap_get_buffer)(FPDF_BITMAP);
    int (FPDF_CALLCONV *bitmap_get_stride)(FPDF_BITMAP);
    void (FPDF_CALLCONV *bitmap_destroy)(FPDF_BITMAP);
    void (FPDF_CALLCONV *render_page_bitmap)(FPDF_BITMAP, FPDF_PAGE, int, int, int, int, int, int);
    FPDF_TEXTPAGE (FPDF_CALLCONV *text_load_page)(FPDF_PAGE);
    void (FPDF_CALLCONV *text_close_page)(FPDF_TEXTPAGE);
    int (FPDF_CALLCONV *text_count_chars)(FPDF_TEXTPAGE);
    unsigned int (FPDF_CALLCONV *text_get_unicode)(FPDF_TEXTPAGE, int);
    FPDF_BOOL (FPDF_CALLCONV *text_get_char_box)(FPDF_TEXTPAGE, int, double *, double *, double *, double *);
    FPDF_SCHHANDLE (FPDF_CALLCONV *text_find_start)(FPDF_TEXTPAGE, FPDF_WIDESTRING, unsigned long, int);
    FPDF_BOOL (FPDF_CALLCONV *text_find_next)(FPDF_SCHHANDLE);
    int (FPDF_CALLCONV *text_get_sch_result_index)(FPDF_SCHHANDLE);
    int (FPDF_CALLCONV *text_get_sch_count)(FPDF_SCHHANDLE);
    void (FPDF_CALLCONV *text_find_close)(FPDF_SCHHANDLE);
    FPDF_BOOKMARK (FPDF_CALLCONV *bookmark_get_first_child)(FPDF_DOCUMENT, FPDF_BOOKMARK);
    FPDF_BOOKMARK (FPDF_CALLCONV *bookmark_get_next_sibling)(FPDF_DOCUMENT, FPDF_BOOKMARK);
    unsigned long (FPDF_CALLCONV *bookmark_get_title)(FPDF_BOOKMARK, void *, unsigned long);
    FPDF_DEST (FPDF_CALLCONV *bookmark_get_dest)(FPDF_DOCUMENT, FPDF_BOOKMARK);
    int (FPDF_CALLCONV *dest_get_dest_page_index)(FPDF_DOCUMENT, FPDF_DEST);
    FPDF_BOOL (FPDF_CALLCONV *link_enumerate)(FPDF_PAGE, int *, FPDF_LINK *);
    FPDF_BOOL (FPDF_CALLCONV *link_get_annot_rect)(FPDF_LINK, FS_RECTF *);
    FPDF_ACTION (FPDF_CALLCONV *link_get_action)(FPDF_LINK);
    FPDF_DEST (FPDF_CALLCONV *link_get_dest)(FPDF_DOCUMENT, FPDF_LINK);
    unsigned long (FPDF_CALLCONV *action_get_type)(FPDF_ACTION);
    unsigned long (FPDF_CALLCONV *action_get_uri_path)(FPDF_DOCUMENT, FPDF_ACTION, void *, unsigned long);
    FPDF_DEST (FPDF_CALLCONV *action_get_dest)(FPDF_DOCUMENT, FPDF_ACTION);
    unsigned long (FPDF_CALLCONV *get_meta_text)(FPDF_DOCUMENT, FPDF_BYTESTRING, void *, unsigned long);
} Api;

static Api g_api;
static SDL_Mutex *g_lock;                  /* guards loading and the one-time library initialisation */
static char g_err[300];
static int g_inited;

static void *sym(const char *name) { return (void *)SDL_LoadFunction(g_api.lib, name); }

#define R(field, name, type) do { a.field = (type)sym(name); if (!a.field) missing = name; } while (0)

static int try_load(const char *path) {
    SDL_SharedObject *lib = SDL_LoadObject(path);
    Api a;
    const char *missing = NULL;
    if (!lib) return 0;
    memset(&a, 0, sizeof a);
    a.lib = lib; g_api.lib = lib;
    R(init_library_with_config, "FPDF_InitLibraryWithConfig", void (FPDF_CALLCONV *)(const FPDF_LIBRARY_CONFIG *));
    R(destroy_library, "FPDF_DestroyLibrary", void (FPDF_CALLCONV *)(void));
    R(load_mem_document, "FPDF_LoadMemDocument", FPDF_DOCUMENT (FPDF_CALLCONV *)(const void *, int, FPDF_BYTESTRING));
    R(close_document, "FPDF_CloseDocument", void (FPDF_CALLCONV *)(FPDF_DOCUMENT));
    R(get_page_count, "FPDF_GetPageCount", int (FPDF_CALLCONV *)(FPDF_DOCUMENT));
    R(load_page, "FPDF_LoadPage", FPDF_PAGE (FPDF_CALLCONV *)(FPDF_DOCUMENT, int));
    R(close_page, "FPDF_ClosePage", void (FPDF_CALLCONV *)(FPDF_PAGE));
    R(get_page_size_by_index_f, "FPDF_GetPageSizeByIndexF", FPDF_BOOL (FPDF_CALLCONV *)(FPDF_DOCUMENT, int, FS_SIZEF *));
    R(bitmap_create_ex, "FPDFBitmap_CreateEx", FPDF_BITMAP (FPDF_CALLCONV *)(int, int, int, void *, int));
    R(bitmap_fill_rect, "FPDFBitmap_FillRect", FPDF_BOOL (FPDF_CALLCONV *)(FPDF_BITMAP, int, int, int, int, FPDF_DWORD));
    R(bitmap_get_buffer, "FPDFBitmap_GetBuffer", void *(FPDF_CALLCONV *)(FPDF_BITMAP));
    R(bitmap_get_stride, "FPDFBitmap_GetStride", int (FPDF_CALLCONV *)(FPDF_BITMAP));
    R(bitmap_destroy, "FPDFBitmap_Destroy", void (FPDF_CALLCONV *)(FPDF_BITMAP));
    R(render_page_bitmap, "FPDF_RenderPageBitmap", void (FPDF_CALLCONV *)(FPDF_BITMAP, FPDF_PAGE, int, int, int, int, int, int));
    R(text_load_page, "FPDFText_LoadPage", FPDF_TEXTPAGE (FPDF_CALLCONV *)(FPDF_PAGE));
    R(text_close_page, "FPDFText_ClosePage", void (FPDF_CALLCONV *)(FPDF_TEXTPAGE));
    R(text_count_chars, "FPDFText_CountChars", int (FPDF_CALLCONV *)(FPDF_TEXTPAGE));
    R(text_get_unicode, "FPDFText_GetUnicode", unsigned int (FPDF_CALLCONV *)(FPDF_TEXTPAGE, int));
    R(text_get_char_box, "FPDFText_GetCharBox", FPDF_BOOL (FPDF_CALLCONV *)(FPDF_TEXTPAGE, int, double *, double *, double *, double *));
    R(text_find_start, "FPDFText_FindStart", FPDF_SCHHANDLE (FPDF_CALLCONV *)(FPDF_TEXTPAGE, FPDF_WIDESTRING, unsigned long, int));
    R(text_find_next, "FPDFText_FindNext", FPDF_BOOL (FPDF_CALLCONV *)(FPDF_SCHHANDLE));
    R(text_get_sch_result_index, "FPDFText_GetSchResultIndex", int (FPDF_CALLCONV *)(FPDF_SCHHANDLE));
    R(text_get_sch_count, "FPDFText_GetSchCount", int (FPDF_CALLCONV *)(FPDF_SCHHANDLE));
    R(text_find_close, "FPDFText_FindClose", void (FPDF_CALLCONV *)(FPDF_SCHHANDLE));
    R(bookmark_get_first_child, "FPDFBookmark_GetFirstChild", FPDF_BOOKMARK (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_BOOKMARK));
    R(bookmark_get_next_sibling, "FPDFBookmark_GetNextSibling", FPDF_BOOKMARK (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_BOOKMARK));
    R(bookmark_get_title, "FPDFBookmark_GetTitle", unsigned long (FPDF_CALLCONV *)(FPDF_BOOKMARK, void *, unsigned long));
    R(bookmark_get_dest, "FPDFBookmark_GetDest", FPDF_DEST (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_BOOKMARK));
    R(dest_get_dest_page_index, "FPDFDest_GetDestPageIndex", int (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_DEST));
    R(link_enumerate, "FPDFLink_Enumerate", FPDF_BOOL (FPDF_CALLCONV *)(FPDF_PAGE, int *, FPDF_LINK *));
    R(link_get_annot_rect, "FPDFLink_GetAnnotRect", FPDF_BOOL (FPDF_CALLCONV *)(FPDF_LINK, FS_RECTF *));
    R(link_get_action, "FPDFLink_GetAction", FPDF_ACTION (FPDF_CALLCONV *)(FPDF_LINK));
    R(link_get_dest, "FPDFLink_GetDest", FPDF_DEST (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_LINK));
    R(action_get_type, "FPDFAction_GetType", unsigned long (FPDF_CALLCONV *)(FPDF_ACTION));
    R(action_get_uri_path, "FPDFAction_GetURIPath", unsigned long (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_ACTION, void *, unsigned long));
    R(action_get_dest, "FPDFAction_GetDest", FPDF_DEST (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_ACTION));
    R(get_meta_text, "FPDF_GetMetaText", unsigned long (FPDF_CALLCONV *)(FPDF_DOCUMENT, FPDF_BYTESTRING, void *, unsigned long));
    if (missing) {
        SDL_UnloadObject(lib); memset(&g_api, 0, sizeof g_api);
        snprintf(g_err, sizeof g_err, "%s lacks %s: it is not a PDFium Exhaust can use", path, missing);
        return 0;
    }
    g_api = a;
    return 1;
}

int ex_pdf_load(const char *path, char *err, size_t errcap) {
    int ok = 0;
    if (!g_lock) g_lock = SDL_CreateMutex();
    SDL_LockMutex(g_lock);
    if (g_api.lib) ok = 1;
    else {
        const char *base = SDL_GetBasePath(), *env = getenv("EXHAUST_PDFIUM_LIB");
        static const char *const names[] = {
#if defined(_WIN32)
            "pdfium.dll"
#elif defined(__APPLE__)
            "libpdfium.dylib"
#else
            "libpdfium.so"
#endif
        };
        size_t i;
        g_err[0] = 0;
        if (path && *path) ok = try_load(path);
        else if (env && *env) ok = try_load(env);
        else for (i = 0; i < sizeof names / sizeof *names && !ok; i++) {
            char full[900];
            if (base) { snprintf(full, sizeof full, "%saddons/%s", base, names[i]); ok = try_load(full); }
            if (!ok) ok = try_load(names[i]);
        }
        if (!ok && !g_err[0]) snprintf(g_err, sizeof g_err, "PDFium is not installed (the PDF add-on)");
        if (ok && !g_inited) {
            FPDF_LIBRARY_CONFIG cfg;
            memset(&cfg, 0, sizeof cfg);
            cfg.version = 2;                                        /* no font paths of our own, no isolate */
            g_api.init_library_with_config(&cfg);
            g_inited = 1;
        }
    }
    SDL_UnlockMutex(g_lock);
    if (!ok && err && errcap) snprintf(err, errcap, "%s", g_err);
    return ok;
}
int ex_pdf_loaded(void) { return g_api.lib != NULL; }
void ex_pdf_unload(void) {
    if (!g_api.lib) return;
    if (g_inited) { g_api.destroy_library(); g_inited = 0; }
    SDL_UnloadObject(g_api.lib);
    memset(&g_api, 0, sizeof g_api);
}

/* ---- a document and its thread ---- */

typedef struct Req { ExPdfKind kind; unsigned long ticket; int page, w, h, match_case; char *query; } Req;

struct ExPdf {
    uint8_t *bytes; size_t len;
    FPDF_DOCUMENT doc;
    ExPdfInfo info;
    SDL_Thread *thread;
    SDL_Mutex *lock;
    SDL_Condition *cond;
    int stop;
    Req *q; size_t nq, capq;                    /* waiting requests */
    ExPdfResult *out; size_t nout, capout;      /* finished results */
    unsigned long next_ticket;
    SDL_AtomicInt cancel_search;
    SDL_Semaphore *opened; int open_ok; char open_err[200];
};

static void push_result(ExPdf *d, ExPdfResult *r) {
    SDL_LockMutex(d->lock);
    if (d->nout == d->capout) {
        size_t nc = d->capout ? d->capout * 2 : 16;
        ExPdfResult *n = realloc(d->out, nc * sizeof *n);
        if (!n) { SDL_UnlockMutex(d->lock); ex_pdf_result_free(r); return; }
        d->out = n; d->capout = nc;
    }
    d->out[d->nout++] = *r;
    SDL_UnlockMutex(d->lock);
}

static void utf16_to_utf8(const uint16_t *s, size_t n, char *out, size_t cap) {
    size_t i, o = 0;
    for (i = 0; i < n && s[i] && o + 4 < cap; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
        else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 63)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
    }
    out[o] = 0;
}

static uint16_t *utf8_to_utf16(const char *s) {
    size_t n = strlen(s), i = 0, o = 0;
    uint16_t *w = malloc((n + 1) * 2 + 2);
    if (!w) return NULL;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = c;
        size_t l = 1;
        if (c >= 0xF0 && i + 3 < n) { cp = ((uint32_t)(c & 7) << 18) | ((uint32_t)(s[i + 1] & 63) << 12) | ((uint32_t)(s[i + 2] & 63) << 6) | (uint32_t)(s[i + 3] & 63); l = 4; }
        else if (c >= 0xE0 && i + 2 < n) { cp = ((uint32_t)(c & 15) << 12) | ((uint32_t)(s[i + 1] & 63) << 6) | (uint32_t)(s[i + 2] & 63); l = 3; }
        else if (c >= 0xC0 && i + 1 < n) { cp = ((uint32_t)(c & 31) << 6) | (uint32_t)(s[i + 1] & 63); l = 2; }
        i += l;
        if (cp >= 0x10000) { cp -= 0x10000; w[o++] = (uint16_t)(0xD800 + (cp >> 10)); w[o++] = (uint16_t)(0xDC00 + (cp & 1023)); }
        else w[o++] = (uint16_t)cp;
    }
    w[o] = 0;
    return w;
}

static void read_outline(ExPdf *d, FPDF_BOOKMARK parent, int depth, size_t *cap) {
    FPDF_BOOKMARK b = g_api.bookmark_get_first_child(d->doc, parent);
    int guard = 0;
    while (b && guard++ < 5000 && depth < 16) {
        uint16_t title[256];
        unsigned long n = g_api.bookmark_get_title(b, title, sizeof title);
        FPDF_DEST dest = g_api.bookmark_get_dest(d->doc, b);
        ExPdfToc t;
        char utf8[1024];
        memset(title + (n / 2 < 256 ? n / 2 : 255), 0, 2);
        utf16_to_utf8(title, 255, utf8, sizeof utf8);
        t.title = malloc(strlen(utf8) + 1); if (t.title) memcpy(t.title, utf8, strlen(utf8) + 1); t.depth = depth;
        t.page = dest ? g_api.dest_get_dest_page_index(d->doc, dest) : -1;
        if (d->info.ntoc == *cap) { size_t nc = *cap ? *cap * 2 : 32; ExPdfToc *nt = realloc(d->info.toc, nc * sizeof *nt); if (!nt) { free(t.title); return; } d->info.toc = nt; *cap = nc; }
        d->info.toc[d->info.ntoc++] = t;
        read_outline(d, b, depth + 1, cap);
        b = g_api.bookmark_get_next_sibling(d->doc, b);
    }
}

static void do_render(ExPdf *d, const Req *q) {
    FPDF_PAGE page;
    FPDF_BITMAP bm;
    ExPdfResult r;
    uint8_t *buf;
    int w = q->w, h = q->h, stride, y;
    if (q->page < 0 || q->page >= d->info.pages || w < 1 || h < 1 || w > 8000 || h > 8000) return;
    page = g_api.load_page(d->doc, q->page);
    if (!page) return;
    buf = calloc((size_t)w * (size_t)h, 4);
    if (!buf) { g_api.close_page(page); return; }
    bm = g_api.bitmap_create_ex(w, h, FPDFBitmap_BGRA, buf, w * 4);                  /* PDFium draws straight into our buffer */
    if (!bm) { free(buf); g_api.close_page(page); return; }
    g_api.bitmap_fill_rect(bm, 0, 0, w, h, 0xFFFFFFFFu);
    g_api.render_page_bitmap(bm, page, 0, 0, w, h, 0, 0);                            /* no annotations, no form widgets: flags 0 */
    stride = g_api.bitmap_get_stride(bm);
    (void)stride;
    g_api.bitmap_destroy(bm);
    g_api.close_page(page);
    for (y = 0; y < w * h; y++) { uint8_t *p = buf + (size_t)y * 4, t = p[0]; p[0] = p[2]; p[2] = t; p[3] = 255; }       /* BGRA to RGBA, opaque */
    memset(&r, 0, sizeof r);
    r.kind = EX_PDF_RENDER; r.ticket = q->ticket; r.page = q->page; r.rgba = buf; r.w = w; r.h = h;
    push_result(d, &r);
}

static void do_text(ExPdf *d, const Req *q) {
    FPDF_PAGE page;
    FPDF_TEXTPAGE tp;
    ExPdfResult r;
    int i, n;
    if (q->page < 0 || q->page >= d->info.pages) return;
    page = g_api.load_page(d->doc, q->page);
    if (!page) return;
    tp = g_api.text_load_page(page);
    memset(&r, 0, sizeof r);
    r.kind = EX_PDF_TEXT; r.ticket = q->ticket; r.page = q->page;
    n = tp ? g_api.text_count_chars(tp) : 0;
    if (n < 0) n = 0;
    if (n > 2000000) n = 2000000;
    r.chars = calloc((size_t)n + 1, sizeof *r.chars);
    r.boxes = calloc((size_t)n * 4 + 4, sizeof *r.boxes);
    if (r.chars && r.boxes) {
        for (i = 0; i < n; i++) {
            double l = 0, rr = 0, b = 0, t = 0;
            r.chars[i] = g_api.text_get_unicode(tp, i);
            if (g_api.text_get_char_box(tp, i, &l, &rr, &b, &t)) { r.boxes[i * 4] = (float)l; r.boxes[i * 4 + 1] = (float)b; r.boxes[i * 4 + 2] = (float)rr; r.boxes[i * 4 + 3] = (float)t; }
        }
        r.nchars = (size_t)n;
        push_result(d, &r);
    } else { free(r.chars); free(r.boxes); }
    if (tp) g_api.text_close_page(tp);
    g_api.close_page(page);
}

static void do_links(ExPdf *d, const Req *q) {
    FPDF_PAGE page;
    ExPdfResult r;
    int pos = 0, guard = 0;
    FPDF_LINK link;
    size_t cap = 0;
    if (q->page < 0 || q->page >= d->info.pages) return;
    page = g_api.load_page(d->doc, q->page);
    if (!page) return;
    memset(&r, 0, sizeof r);
    r.kind = EX_PDF_LINKS; r.ticket = q->ticket; r.page = q->page;
    while (g_api.link_enumerate(page, &pos, &link) && guard++ < 10000) {
        FS_RECTF rc;
        ExPdfLink L;
        FPDF_ACTION act;
        FPDF_DEST dest = NULL;
        unsigned long type;
        memset(&L, 0, sizeof L);
        L.page = -1;
        if (!g_api.link_get_annot_rect(link, &rc)) continue;
        L.l = rc.left; L.b = rc.bottom; L.r = rc.right; L.t = rc.top;
        act = g_api.link_get_action(link);
        type = act ? g_api.action_get_type(act) : 0;
        if (type == PDFACTION_URI) {
            char tmp[512];
            unsigned long n = g_api.action_get_uri_path(d->doc, act, tmp, sizeof tmp - 1);
            if (n > 0 && n < sizeof tmp) { tmp[n] = 0; snprintf(L.uri, sizeof L.uri, "%s", tmp); }
        } else if (type == PDFACTION_GOTO) dest = g_api.action_get_dest(d->doc, act);
        else if (!act) dest = g_api.link_get_dest(d->doc, link);
        /* a launch, a remote or embedded goto and anything else: no target, so a click does nothing */
        if (dest) L.page = g_api.dest_get_dest_page_index(d->doc, dest);
        if (L.page < 0 && !L.uri[0]) continue;
        if (r.nlinks == cap) { size_t nc = cap ? cap * 2 : 16; ExPdfLink *nl = realloc(r.links, nc * sizeof *nl); if (!nl) break; r.links = nl; cap = nc; }
        r.links[r.nlinks++] = L;
    }
    g_api.close_page(page);
    push_result(d, &r);
}

static void do_search(ExPdf *d, const Req *q) {
    uint16_t *w = utf8_to_utf16(q->query ? q->query : "");
    int p;
    if (!w || !w[0]) { free(w); return; }
    SDL_SetAtomicInt(&d->cancel_search, 0);
    for (p = 0; p < d->info.pages; p++) {
        FPDF_PAGE page;
        FPDF_TEXTPAGE tp;
        FPDF_SCHHANDLE h;
        ExPdfResult r;
        size_t cap = 0;
        if (SDL_GetAtomicInt(&d->cancel_search)) break;
        page = g_api.load_page(d->doc, p);
        if (!page) continue;
        tp = g_api.text_load_page(page);
        memset(&r, 0, sizeof r);
        r.kind = EX_PDF_SEARCH; r.ticket = q->ticket; r.page = p;
        h = tp ? g_api.text_find_start(tp, w, q->match_case ? FPDF_MATCHCASE : 0, 0) : NULL;
        if (h) {
            int guard = 0;
            while (g_api.text_find_next(h) && guard++ < 100000) {
                if (r.nhits == cap) { size_t nc = cap ? cap * 2 : 16; ExPdfHit *nh = realloc(r.hits, nc * sizeof *nh); if (!nh) break; r.hits = nh; cap = nc; }
                r.hits[r.nhits].page = p; r.hits[r.nhits].start = g_api.text_get_sch_result_index(h); r.hits[r.nhits].count = g_api.text_get_sch_count(h); r.nhits++;
            }
            g_api.text_find_close(h);
        }
        if (tp) g_api.text_close_page(tp);
        g_api.close_page(page);
        if (r.nhits) { r.done = 0; push_result(d, &r); } else free(r.hits);
    }
    free(w);
    {   /* the last piece says the search is over */
        ExPdfResult r;
        memset(&r, 0, sizeof r);
        r.kind = EX_PDF_SEARCH; r.ticket = q->ticket; r.page = -1; r.done = 1;
        push_result(d, &r);
    }
}

static int SDLCALL doc_thread(void *arg) {
    ExPdf *d = arg;
    int i;
    size_t tocap = 0;
    d->doc = g_api.load_mem_document(d->bytes, (int)(d->len > 0x7fffffff ? 0x7fffffff : d->len), NULL);
    if (!d->doc) { snprintf(d->open_err, sizeof d->open_err, "this PDF is damaged, encrypted or in a form PDFium cannot read"); d->open_ok = 0; SDL_SignalSemaphore(d->opened); return 0; }
    d->info.pages = g_api.get_page_count(d->doc);
    if (d->info.pages < 1 || d->info.pages > 100000) { g_api.close_document(d->doc); d->doc = NULL; snprintf(d->open_err, sizeof d->open_err, "this PDF has no pages"); d->open_ok = 0; SDL_SignalSemaphore(d->opened); return 0; }
    d->info.w = calloc((size_t)d->info.pages, sizeof *d->info.w); d->info.h = calloc((size_t)d->info.pages, sizeof *d->info.h);
    for (i = 0; d->info.w && d->info.h && i < d->info.pages; i++) {
        FS_SIZEF sz;
        sz.width = 612.0f; sz.height = 792.0f;
        g_api.get_page_size_by_index_f(d->doc, i, &sz);
        d->info.w[i] = sz.width > 1.0f ? sz.width : 612.0; d->info.h[i] = sz.height > 1.0f ? sz.height : 792.0;
    }
    read_outline(d, NULL, 0, &tocap);
    {
        uint16_t title[256];
        unsigned long n = g_api.get_meta_text(d->doc, "Title", title, sizeof title);
        if (n > 2) { memset(title + (n / 2 < 256 ? n / 2 : 255), 0, 2); utf16_to_utf8(title, 255, d->info.title, sizeof d->info.title); }
    }
    d->open_ok = d->info.w && d->info.h;
    SDL_SignalSemaphore(d->opened);
    while (d->open_ok) {
        Req q;
        SDL_LockMutex(d->lock);
        while (!d->stop && d->nq == 0) SDL_WaitCondition(d->cond, d->lock);
        if (d->stop) { SDL_UnlockMutex(d->lock); break; }
        q = d->q[0];
        memmove(d->q, d->q + 1, (d->nq - 1) * sizeof *d->q);
        d->nq--;
        SDL_UnlockMutex(d->lock);
        if (q.kind == EX_PDF_RENDER) do_render(d, &q);
        else if (q.kind == EX_PDF_TEXT) do_text(d, &q);
        else if (q.kind == EX_PDF_LINKS) do_links(d, &q);
        else if (q.kind == EX_PDF_SEARCH) do_search(d, &q);
        free(q.query);
    }
    g_api.close_document(d->doc);
    d->doc = NULL;
    return 0;
}

ExPdf *ex_pdf_open(uint8_t *bytes, size_t len, char *err, size_t errcap) {
    ExPdf *d;
    if (!ex_pdf_loaded() && !ex_pdf_load(NULL, err, errcap)) { free(bytes); return NULL; }
    if (!bytes || len < 8) { free(bytes); if (err && errcap) snprintf(err, errcap, "this is not a PDF file"); return NULL; }
    d = calloc(1, sizeof *d);
    if (!d) { free(bytes); if (err && errcap) snprintf(err, errcap, "out of memory"); return NULL; }
    d->bytes = bytes; d->len = len;
    d->lock = SDL_CreateMutex(); d->cond = SDL_CreateCondition(); d->opened = SDL_CreateSemaphore(0);
    d->next_ticket = 1;
    d->thread = SDL_CreateThread(doc_thread, "exhaust-pdf", d);
    if (!d->thread) { if (err && errcap) snprintf(err, errcap, "the PDF thread could not start"); SDL_DestroyMutex(d->lock); SDL_DestroyCondition(d->cond); SDL_DestroySemaphore(d->opened); free(d->bytes); free(d); return NULL; }
    SDL_WaitSemaphore(d->opened);
    if (!d->open_ok) {
        if (err && errcap) snprintf(err, errcap, "%s", d->open_err);
        SDL_WaitThread(d->thread, NULL);
        SDL_DestroyMutex(d->lock); SDL_DestroyCondition(d->cond); SDL_DestroySemaphore(d->opened);
        free(d->info.w); free(d->info.h); free(d->bytes); free(d);
        return NULL;
    }
    return d;
}

const ExPdfInfo *ex_pdf_info(const ExPdf *d) { return d ? &d->info : NULL; }

void ex_pdf_result_free(ExPdfResult *r) {
    if (!r) return;
    free(r->rgba); free(r->chars); free(r->boxes); free(r->links); free(r->hits);
    memset(r, 0, sizeof *r);
}

void ex_pdf_close(ExPdf *d) {
    size_t i;
    if (!d) return;
    SDL_SetAtomicInt(&d->cancel_search, 1);
    SDL_LockMutex(d->lock);
    d->stop = 1;
    SDL_SignalCondition(d->cond);
    SDL_UnlockMutex(d->lock);
    SDL_WaitThread(d->thread, NULL);
    for (i = 0; i < d->nq; i++) free(d->q[i].query);
    free(d->q);
    for (i = 0; i < d->nout; i++) ex_pdf_result_free(&d->out[i]);
    free(d->out);
    for (i = 0; i < d->info.ntoc; i++) free(d->info.toc[i].title);
    free(d->info.toc); free(d->info.w); free(d->info.h);
    SDL_DestroyMutex(d->lock); SDL_DestroyCondition(d->cond); SDL_DestroySemaphore(d->opened);
    free(d->bytes);
    free(d);
}
static int SDLCALL closer(void *arg) { ex_pdf_close((ExPdf *)arg); return 0; }
void ex_pdf_close_async(ExPdf *d) {
    SDL_Thread *t;
    if (!d) return;
    t = SDL_CreateThread(closer, "exhaust-pdf-close", d);
    if (t) SDL_DetachThread(t); else ex_pdf_close(d);
}

/* A request replaces a waiting one of the same kind and page (a search replaces a search). */
static unsigned long post(ExPdf *d, Req *r) {
    size_t i;
    unsigned long t;
    if (!d) return 0;
    SDL_LockMutex(d->lock);
    if (d->stop) { SDL_UnlockMutex(d->lock); free(r->query); return 0; }
    r->ticket = t = d->next_ticket++;
    for (i = 0; i < d->nq; i++)
        if (d->q[i].kind == r->kind && (r->kind == EX_PDF_SEARCH || d->q[i].page == r->page)) { free(d->q[i].query); d->q[i] = *r; SDL_SignalCondition(d->cond); SDL_UnlockMutex(d->lock); return t; }
    if (d->nq == d->capq) { size_t nc = d->capq ? d->capq * 2 : 16; Req *n = realloc(d->q, nc * sizeof *n); if (!n) { SDL_UnlockMutex(d->lock); free(r->query); return 0; } d->q = n; d->capq = nc; }
    d->q[d->nq++] = *r;
    SDL_SignalCondition(d->cond);
    SDL_UnlockMutex(d->lock);
    return t;
}
unsigned long ex_pdf_request_render(ExPdf *d, int page, int w, int h) { Req r; memset(&r, 0, sizeof r); r.kind = EX_PDF_RENDER; r.page = page; r.w = w; r.h = h; return post(d, &r); }
unsigned long ex_pdf_request_text(ExPdf *d, int page) { Req r; memset(&r, 0, sizeof r); r.kind = EX_PDF_TEXT; r.page = page; return post(d, &r); }
unsigned long ex_pdf_request_links(ExPdf *d, int page) { Req r; memset(&r, 0, sizeof r); r.kind = EX_PDF_LINKS; r.page = page; return post(d, &r); }
unsigned long ex_pdf_request_search(ExPdf *d, const char *utf8, int match_case) {
    Req r;
    size_t n = utf8 ? strlen(utf8) : 0;
    if (!d || !n) return 0;
    memset(&r, 0, sizeof r);
    r.kind = EX_PDF_SEARCH; r.page = -1; r.match_case = match_case;
    r.query = malloc(n + 1);
    if (!r.query) return 0;
    memcpy(r.query, utf8, n + 1);
    SDL_SetAtomicInt(&d->cancel_search, 1);                                  /* a search that is running stops at its next page */
    return post(d, &r);
}
void ex_pdf_cancel_search(ExPdf *d) { if (d) SDL_SetAtomicInt(&d->cancel_search, 1); }

int ex_pdf_poll(ExPdf *d, ExPdfResult *out) {
    int got = 0;
    if (!d) return 0;
    SDL_LockMutex(d->lock);
    if (d->nout) { *out = d->out[0]; memmove(d->out, d->out + 1, (d->nout - 1) * sizeof *d->out); d->nout--; got = 1; }
    SDL_UnlockMutex(d->lock);
    return got;
}
