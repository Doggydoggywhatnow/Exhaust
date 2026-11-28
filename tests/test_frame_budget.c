/* frame_budget (sub-slice 4d): the interface stays responsive. On a large note (6000 lines of live-preview Markdown) typing,
 * Enter in the middle of the text, paging, resizing the window and a full frame stay within budget: an input handler within
 * 16 ms (the project's rule that the interface thread never waits longer), a whole frame (layout and drawing with the
 * software renderer, the slowest there is) within 60 ms. The 95th percentile is checked, so one slow moment of a busy
 * machine does not fail the test; sanitizer builds are given ten times the time. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_canvasview.h"
#include "exhaust_baseview.h"
#include "exhaust_panes.h"
#include "exhaust_popwin.h"
#include "exhaust_ui.h"

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif
#ifndef SANITIZED
#define SANITIZED 0
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* The renderer queues its drawing commands and runs them when something needs them (a texture update, a present). A test that never
 * presents would see that queued work, all of it, land on whichever frame first updates the glyph atlas: that was the "first edit in a
 * big note freezes for 270 ms" of ISSUES 9, which was not an edit cost at all. Running the queue at the end of each frame charges
 * every frame for its own drawing, as a real frame is. */
static void finish_frame(ExRenderer *r) { SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(r))); }

static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double p95(double *v, size_t n) { qsort(v, n, sizeof *v, cmp_d); return v[(n * 95) / 100 >= n ? n - 1 : (n * 95) / 100]; }

static void report(const char *what, double *v, size_t n, double limit) {
    size_t k;
    double p;
    if (getenv("FRAME_BUDGET_VERBOSE")) for (k = 0; k < n; k++) if (v[k] > 4.0) printf("  %s #%zu: %.1f ms\n", what, k, v[k]);
    p = p95(v, n);
    printf("%s: 95th percentile %.2f ms (limit %.0f ms), worst %.2f ms\n", what, p, limit, v[n - 1]);
    if (p >= limit) { fprintf(stderr, "%s is over budget\n", what); failures++; }
}

static ExDoc *g_big_doc;
/* The content of the tab in the second window: an editor on the same big text as the first window's (7g). */
static ExWidget *big_content(ExPanes *p, const ExTab *tab, void *user) {
    ExWidget *e;
    (void)tab; (void)user;
    e = ex_widget_editor(ex_panes_ui(p), g_big_doc, "note");
    if (!e) return NULL;
    e->width = ex_size_grow(); e->height = ex_size_grow();
    ex_editor_set_mode(e->editor, EX_EDITOR_LIVE);
    ex_editor_set_wrap(e->editor, 1);
    return e;
}

static unsigned long base_budget_run(void *u, const char *text, size_t len, size_t view, const char *path) { unsigned long *n = u; (void)text; (void)len; (void)view; (void)path; return ++*n; }

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc;
    ExWidget *root, *ed;
    char *text;
    size_t i, len = 0, cap = 6000 * 90;
    double key_limit = SANITIZED ? 160 : 16, frame_limit = SANITIZED ? 600 : 60;
    double keys[200], frames[200];
    size_t nk = 0, nf = 0;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    text = malloc(cap);
    for (i = 0; i < 6000; i++) {
        switch (i % 6) {
        case 0: len += (size_t)snprintf(text + len, cap - len, "## Section %zu\n", i / 6); break;
        case 1: len += (size_t)snprintf(text + len, cap - len, "A paragraph with **bold**, *italic*, `code` and a [[Link %zu]] to another note.\n", i); break;
        case 2: len += (size_t)snprintf(text + len, cap - len, "- a list item with a [markdown link](Note %zu.md)\n", i); break;
        case 3: len += (size_t)snprintf(text + len, cap - len, "- [ ] a task #tag%zu\n", i % 40); break;
        case 4: len += (size_t)snprintf(text + len, cap - len, "> a quotation that is long enough to be a real line of text, with some words in it\n"); break;
        default: len += (size_t)snprintf(text + len, cap - len, "\n"); break;
        }
    }
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("frame budget", 1000, 700);
    ui = ex_ui_create(r, &theme, 16.0f);
    doc = ex_doc_create(text, len);
    root = ex_widget_box(ui, 1, "root");
    root->width = ex_size_grow(); root->height = ex_size_grow();
    ed = ex_widget_editor(ui, doc, "note");
    ed->width = ex_size_grow(); ed->height = ex_size_grow();
    ex_editor_set_mode(ed->editor, EX_EDITOR_LIVE);
    ex_editor_set_wrap(ed->editor, 1);                         /* as the note editors are: long lines wrap */
    ex_widget_add(root, ed);
    ex_ui_set_focus(ui, ed);
    ex_ui_layout(ui, root, 1000, 700);
    ex_ui_draw(ui, root, 1);                                  /* the first frame loads fonts and builds the tokens */
    ex_doc_set_caret(doc, len / 2);

    /* typing: the handler, then the frame */
    for (i = 0; i < 60; i++) {
        double t0 = now_ms(), t1;
        ex_ui_text_input(ui, "x", 1);
        t1 = now_ms();
        if (nk < 200) keys[nk++] = t1 - t0;
        ex_ui_layout(ui, root, 1000, 700); ex_ui_draw(ui, root, 1); finish_frame(r);
        { double ft = now_ms() - t1; if (getenv("FRAME_BUDGET_VERBOSE") && ft > 20) printf("  slow frame after step %zu: %.1f ms (handler %.1f)\n", i, ft, keys[nk - 1]); if (nf < 200) frames[nf++] = ft; }
    }
    /* Enter in the middle of the text, and paging */
    for (i = 0; i < 40; i++) {
        double t0 = now_ms(), t1;
        ex_ui_key(ui, i % 4 == 3 ? EX_KEY_PAGE_DOWN : EX_KEY_ENTER, 0);
        t1 = now_ms();
        if (nk < 200) keys[nk++] = t1 - t0;
        ex_ui_layout(ui, root, 1000, 700); ex_ui_draw(ui, root, 1); finish_frame(r);
        { double ft = now_ms() - t1; if (getenv("FRAME_BUDGET_VERBOSE") && ft > 20) printf("  slow frame after step %zu: %.1f ms (handler %.1f)\n", i, ft, keys[nk - 1]); if (nf < 200) frames[nf++] = ft; }
    }
    /* resizing the window: layout and drawing at many sizes */
    for (i = 0; i < 40; i++) {
        double t0 = now_ms();
        float w = 700.0f + (float)((i * 37) % 300), h = 450.0f + (float)((i * 53) % 250);
        ex_ui_layout(ui, root, w, h); ex_ui_draw(ui, root, 1); finish_frame(r);
        if (nf < 200) frames[nf++] = now_ms() - t0;
    }
    report("an input handler (typing, Enter, Page Down)", keys, nk, key_limit);
    report("a frame (layout and drawing)", frames, nf, frame_limit);
    {   /* rendered blocks (live preview): a note of 6000 lines with a table every 8 lines, an equation every 40 and properties: about 750 blocks. Typing between blocks,
         * paging and resizing must cost what the plain note cost: the blocks are collected once for each edit, measured near the window only, and drawn from views kept by what they say */
        ExDoc *bdoc;
        ExWidget *bed, *broot;
        char *bt = malloc(900000);
        size_t bl = 0, j, bk = 0, bfr = 0;
        double bkeys[200], bframes[200];
        bl += (size_t)snprintf(bt + bl, 900000 - bl, "---\ntags: [a, b]\n---\n");
        for (j = 0; j < 6000; j++) {
            switch (j % 8) {
            case 0: bl += (size_t)snprintf(bt + bl, 900000 - bl, "| a | b |\n"); break;
            case 1: bl += (size_t)snprintf(bt + bl, 900000 - bl, "|---|---|\n"); break;
            case 2: bl += (size_t)snprintf(bt + bl, 900000 - bl, "| %zu | two |\n", j); break;
            case 3: bl += (size_t)snprintf(bt + bl, 900000 - bl, "\n"); break;
            case 4: bl += (size_t)snprintf(bt + bl, 900000 - bl, "A paragraph with **bold** and a [[Link %zu]] in it, long enough to need some room.\n", j); break;
            case 5: bl += (size_t)snprintf(bt + bl, 900000 - bl, j % 40 == 5 ? "$$\nx^2 + %zu\n$$\n" : "text %zu\n", j); break;
            default: bl += (size_t)snprintf(bt + bl, 900000 - bl, "\n"); break;
            }
        }
        bdoc = ex_doc_create(bt, bl);
        broot = ex_widget_box(ui, 1, "broot");
        broot->width = ex_size_grow(); broot->height = ex_size_grow();
        bed = ex_widget_editor(ui, bdoc, "bnote");
        bed->width = ex_size_grow(); bed->height = ex_size_grow();
        ex_editor_set_mode(bed->editor, EX_EDITOR_LIVE);
        ex_editor_set_wrap(bed->editor, 1);
        ex_editor_set_blocks(bed->editor, 1, 0);
        ex_widget_add(broot, bed);
        ex_ui_set_focus(ui, bed);
        ex_ui_layout(ui, broot, 1000, 700);
        ex_ui_draw(ui, broot, 1);
        ex_doc_set_caret(bdoc, bl / 2);
        CHECK(ex_editor_block_count(bed->editor) > 700);
        for (j = 0; j < 60; j++) {
            double t0 = now_ms(), t1;
            ex_ui_text_input(ui, "x", 1);
            t1 = now_ms();
            if (bk < 200) bkeys[bk++] = t1 - t0;
            ex_ui_layout(ui, broot, 1000, 700); ex_ui_draw(ui, broot, 1); finish_frame(r);
            if (bfr < 200) bframes[bfr++] = now_ms() - t1;
        }
        for (j = 0; j < 20; j++) {
            double t0 = now_ms(), t1;
            ex_ui_key(ui, EX_KEY_PAGE_DOWN, 0);
            t1 = now_ms();
            if (bk < 200) bkeys[bk++] = t1 - t0;
            ex_ui_layout(ui, broot, 1000, 700); ex_ui_draw(ui, broot, 1); finish_frame(r);
            if (bfr < 200) bframes[bfr++] = now_ms() - t1;
        }
        for (j = 0; j < 20; j++) {
            double t0 = now_ms();
            ex_ui_layout(ui, broot, 700.0f + (float)((j * 37) % 300), 450.0f + (float)((j * 53) % 250)); ex_ui_draw(ui, broot, 1); finish_frame(r);
            if (bfr < 200) bframes[bfr++] = now_ms() - t0;
        }
        CHECK(ex_editor_blocks_drawn(bed->editor) > 100);
        report("an input handler in a note of 750 drawn blocks", bkeys, bk, key_limit);
        report("a frame of a note of 750 drawn blocks", bframes, bfr, frame_limit);
        ex_ui_set_focus(ui, ed);
        ex_widget_free(broot);
        ex_doc_free(bdoc);
        free(bt);
    }
    {   /* the pane headers (5d): four panes with six tabs each and a history, brought up to date, laid out and drawn every frame */
        ExWorkspace *ws = ex_ws_create();
        ExPanesHooks hooks;
        ExPanes *panes;
        ExWidget *win;
        ExId g = ex_ws_active_group(ws);
        double hf[120];
        size_t nh = 0;
        int gi, ti;
        memset(&hooks, 0, sizeof hooks);
        panes = ex_panes_create(ui, ws, &hooks, NULL);
        win = ex_widget_box(ui, 0, "window");
        win->width = ex_size_grow(); win->height = ex_size_grow();
        ex_widget_add(win, ex_panes_root(panes));
        for (gi = 0; gi < 4; gi++) {
            for (ti = 0; ti < 6; ti++) {
                char p[64];
                snprintf(p, sizeof p, "Folder %d/A fairly long note name number %d.md", gi, ti);
                ex_ws_new_tab(ws, g, p, "note");
                ex_ws_history_visit(ws, g, p, "note");
            }
            if (gi < 3) g = ex_ws_split(ws, g, EX_SPLIT_HORIZONTAL, 1, 0);
        }
        {                                                                       /* and the right sidebar's group of tabs next to them (5e) */
            ExId sg = ex_ws_side_group_ensure(ws, EX_SIDE_RIGHT);
            ExWidget *sb = ex_widget_box(ui, 1, "sidebar");
            sb->width = ex_size_px(ui, 300); sb->height = ex_size_grow();
            ex_widget_add(sb, ex_panes_side_root(panes));
            ex_widget_add(win, sb);
            for (ti = 0; ti < 6; ti++) { char p[64]; snprintf(p, sizeof p, "Side/A note in the sidebar number %d.md", ti); ex_ws_new_tab(ws, sg, p, "note"); ex_ws_history_visit(ws, sg, p, "note"); }
        }
        for (i = 0; i < 120; i++) {
            double t0 = now_ms();
            ex_panes_update(panes);
            ex_ui_layout(ui, win, 1000, 700); ex_ui_draw(ui, win, 1); finish_frame(r);
            if (i % 10 == 9) ex_ws_history_visit(ws, ex_ws_active_group(ws), "Folder 0/Another place.md", "note");   /* the history changes now and then */
            if (nh < 120) hf[nh++] = now_ms() - t0;
        }
        report("a frame with four pane headers and the sidebar's tabs", hf, nh, frame_limit);
        ex_ui_release(ui, win);
        ex_widget_remove(win, ex_panes_root(panes));
        ex_widget_remove(win->last_child, ex_panes_side_root(panes));
        ex_widget_free(win);
        ex_panes_free(panes);
        ex_ws_free(ws);
    }
    {   /* a big canvas (6a): 5,000 cards and 8,000 edges, drawn fitted (outlines) and at natural size (text), panned and zoomed; nothing in a frame grows with the whole canvas but the test of which cards are in view */
        size_t nn = 5000, ne = 8000, cap2 = nn * 200 + ne * 150 + 100, o = 0, k;
        char *js = malloc(cap2);
        ExDoc *cdoc;
        ExWidget *cw, *croot;
        ExCanvasHandlers ch;
        ExCanvasCache *cc = ex_canvascache_create();
        double cf[160], chd[160], flush_total = 0.0;
        size_t ncf = 0, nchd = 0;
        ExCanvasViewStats st;
        o += (size_t)snprintf(js + o, cap2 - o, "{\n\t\"nodes\":[\n");
        for (k = 0; k < nn; k++)
            o += (size_t)snprintf(js + o, cap2 - o, "\t\t{\"id\":\"n%zu\",\"type\":\"%s\",\"%s\":\"%s %zu with some words that wrap onto more than one line\",\"x\":%zu,\"y\":%zu,\"width\":240,\"height\":120}%s\n",
                                  k, k % 5 == 4 ? "file" : "text", k % 5 == 4 ? "file" : "text", k % 5 == 4 ? "Notes/Note" : "# Card", k, (k % 100) * 300, (k / 100) * 180, k + 1 < nn ? "," : "");
        o += (size_t)snprintf(js + o, cap2 - o, "\t],\n\t\"edges\":[\n");
        for (k = 0; k < ne; k++)
            o += (size_t)snprintf(js + o, cap2 - o, "\t\t{\"id\":\"e%zu\",\"fromNode\":\"n%zu\",\"toNode\":\"n%zu\",\"label\":\"%s\"}%s\n", k, (k * 7) % nn, (k * 7 + 1 + k % 3 * 99) % nn, k % 4 == 0 ? "link" : "", k + 1 < ne ? "," : "");
        o += (size_t)snprintf(js + o, cap2 - o, "\t]\n}");
        cdoc = ex_doc_create(js, o);
        memset(&ch, 0, sizeof ch);
        ch.cache = cc;
        croot = ex_widget_box(ui, 1, "canvas root");
        croot->width = ex_size_grow(); croot->height = ex_size_grow();
        cw = ex_widget_canvas(ui, cdoc, "Big.canvas", &ch);
        ex_widget_add(croot, cw);
        ex_ui_layout(ui, croot, 1000, 700); ex_ui_draw(ui, croot, 1); finish_frame(r);
        ex_canvasview_stats(cw, &st);
        CHECK(st.cards == nn && st.edges == ne && st.content_hidden && st.cards_drawn > 100);
        /* The software renderer draws the queued triangles when the frame is flushed, and in the unoptimized build that is slow and says nothing about a real
         * renderer: what is limited is the canvas's own work (building the frame and handing it over) and the number of triangles; the flush is printed. */
        for (i = 0; i < 40; i++) {                                                              /* fitted: every card an outline, 8,000 edges */
            double t0;
            ex_ui_layout(ui, croot, 1000, 700); ex_ui_draw(ui, croot, 1);
            ex_canvasview_stats(cw, &st);
            if (ncf < 160) cf[ncf++] = st.own_ms;
            t0 = now_ms(); finish_frame(r); flush_total += now_ms() - t0;
        }
        ex_canvasview_stats(cw, &st);
        CHECK(st.triangles > 1000 && st.triangles < 200000);                                    /* the level of detail keeps a far view cheap */
        report("the canvas's own work for a frame of 5,000 cards, fitted", cf, ncf, frame_limit);
        printf("  (the software renderer took %.1f ms a frame to draw %zu triangles)\n", flush_total / 40.0, st.triangles);
        cw->on_text(cw, "1", 1, cw->on_text_user);                                               /* natural size: text, a few dozen cards in view */
        ncf = 0;
        for (i = 0; i < 60; i++) {
            double t0 = now_ms(), t1;
            ex_canvasview_wheel(cw, (i % 2) ? 1.0f : -1.0f, 500.0f, 350.0f);                     /* pan down and up */
            t1 = now_ms();
            if (nchd < 160) chd[nchd++] = t1 - t0;
            ex_ui_layout(ui, croot, 1000, 700); ex_ui_draw(ui, croot, 1);
            ex_canvasview_stats(cw, &st);
            if (ncf < 160) cf[ncf++] = st.own_ms;
            finish_frame(r);
        }
        ex_canvasview_stats(cw, &st);
        CHECK(!st.content_hidden && st.texts_laid_out > 0 && st.texts_laid_out < 200 && st.cards_drawn < 200);        /* only what is in view is laid out */
        report("the canvas's own work for a frame of 5,000 cards at natural size", cf, ncf, frame_limit);
        report("an input handler of the canvas (the wheel)", chd, nchd, key_limit);
        { ExCanvasViewStats a, b; ex_ui_layout(ui, croot, 1000, 700); ex_ui_draw(ui, croot, 1); ex_canvasview_stats(cw, &a); ex_ui_layout(ui, croot, 1000, 700); ex_ui_draw(ui, croot, 1); ex_canvasview_stats(cw, &b); CHECK(a.geom_grows == b.geom_grows); }
        ex_ui_release(ui, croot);
        ex_widget_free(croot);
        ex_doc_free(cdoc);
        ex_canvascache_free(cc);
        free(js);
    }
    {   /* a big base (6b-iii): 20,000 files as a table, a list, cards and a kanban board, scrolled; a frame draws the rows in view, not the 20,000 (the run is the service thread's work and is not timed here) */
        static const char *layouts[] = { "table", "list", "cards", "kanban" };
        ExBaseData *bd = calloc(1, sizeof *bd);
        size_t nb = 20000, li;
        for (i = 0; i < nb; i++) {
            char pth[80], v2[40];
            ExBaseFile *f;
            snprintf(pth, sizeof pth, "Big/Note %05zu.md", i);
            f = ex_base_data_add(bd, pth, 1, 1.7e12, 1.7e12 + (double)i, 100.0);
            ex_base_file_prop(f, "status", EXB_TEXT, i % 3 == 0 ? "Planned" : i % 3 == 1 ? "Doing" : "Done");
            snprintf(v2, sizeof v2, "%zu", i % 97);
            ex_base_file_prop(f, "price", EXB_NUMBER, v2);
        }
        for (li = 0; li < 4; li++) {
            char yaml[500], name[100];
            ExDoc *bdoc;
            ExWidget *bw, *broot;
            ExBaseViewHandlers bh;
            ExBase *bs;
            ExBaseResult *br = calloc(1, sizeof *br);
            unsigned long reqs = 0;
            double bf[160];
            size_t nbf = 0;
            ExBaseViewStats bst;
            snprintf(yaml, sizeof yaml, "views:\n  - type: %s\n    name: Big\n%s    order: [file.name, status, price]\n    summaries:\n      price: Sum\n", layouts[li],
                     li == 0 || li == 1 ? "" : "    groupBy:\n      property: note.status\n      direction: ASC\n");
            bdoc = ex_doc_create(yaml, strlen(yaml));
            memset(&bh, 0, sizeof bh);
            bh.user = &reqs; bh.run = base_budget_run;
            broot = ex_widget_box(ui, 1, "base root");
            broot->width = ex_size_grow(); broot->height = ex_size_grow();
            bw = ex_widget_base(ui, bdoc, "Big.base", &bh);
            ex_widget_add(broot, bw);
            ex_ui_layout(ui, broot, 1000, 700); ex_ui_draw(ui, broot, 1);                 /* the first frame asks for the run */
            bs = ex_base_parse(yaml, strlen(yaml), NULL, 0);
            CHECK(bs && reqs == 1 && ex_base_run(bs, 0, bd, "Big.base", 1.7e12, 0, br) == 0);
            ex_baseview_set_result(bw, 1, br, NULL);
            for (i = 0; i < 60; i++) {
                if (i % 2) ex_baseview_wheel(bw, 0, -3.0f, 500.0f, 350.0f); else ex_baseview_wheel(bw, 0, 2.0f, 500.0f, 350.0f);
                ex_ui_layout(ui, broot, 1000, 700); ex_ui_draw(ui, broot, 1);
                ex_baseview_stats(bw, &bst);
                if (nbf < 160) bf[nbf++] = bst.own_ms;
                finish_frame(r);
            }
            ex_baseview_stats(bw, &bst);
            CHECK(bst.rows == nb && bst.items_drawn > 0 && bst.items_drawn < 400 && !bst.failed);
            snprintf(name, sizeof name, "the base view's own work for a frame of 20,000 rows (%s)", layouts[li]);
            report(name, bf, nbf, frame_limit);
            ex_base_free(bs);
            ex_ui_release(ui, broot);
            ex_widget_free(broot);
            ex_doc_free(bdoc);
        }
        ex_base_data_free(bd); free(bd);
    }
    {   /* two windows (7g): the same big note in the first window and in a pop-out window of its own; every pass handles a key press, draws the first window and then the second (one frame each) */
        ExWorkspace *ws2 = ex_ws_create();
        ExId tab = ex_ws_new_tab(ws2, 0, "Big.md", "note"), win = ex_ws_popout(ws2, tab);
        ExPanesHooks hooks;
        ExPopWin *pw;
        double pass[120], second[120], handler[120];
        size_t np = 0, before_size = 0;
        memset(&hooks, 0, sizeof hooks);
        hooks.make_content = big_content;
        g_big_doc = doc;
        pw = win ? ex_popwin_create(ws2, win, &theme, 16.0f, 1.0f, &hooks, NULL, INT_MIN, INT_MIN, 800, 600, "second window") : NULL;
        CHECK(pw != NULL);
        if (pw) {
            ex_ui_set_focus(ui, ed);
            before_size = ex_buffer_size(ex_doc_buffer(doc));
            ex_popwin_frame(pw, 1);                                                      /* the first frame loads fonts and builds the content */
            for (i = 0; i < 120; i++) {
                double t0 = now_ms(), t1, t2, t3;
                ex_ui_text_input(ui, i % 7 == 6 ? "\n" : "y", 1);
                t1 = now_ms();
                ex_ui_layout(ui, root, 1000, 700); ex_ui_draw(ui, root, 1); finish_frame(r);
                t2 = now_ms();
                ex_popwin_frame(pw, 1);
                finish_frame(ex_popwin_renderer(pw));
                t3 = now_ms();
                handler[np] = t1 - t0; second[np] = t3 - t2; pass[np] = t3 - t0;
                np++;
            }
            CHECK(ex_buffer_size(ex_doc_buffer(doc)) >= before_size + 100);                        /* the keys really went into the text both windows show */
            report("an input handler with two windows open", handler, np, key_limit);
            report("the frame of the second window", second, np, frame_limit);
            report("a pass: a key, the first window and the second", pass, np, frame_limit * 2);
            ex_popwin_free(pw);
        }
        ex_ws_free(ws2);
    }
    ex_widget_free(root);
    ex_doc_free(doc);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    free(text);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
