/* base_view (6b-iii): the Bases widget drawn and used through the real UI. A base in a document is read, run (the application's job is played by this test:
 * the widget asks for a run, the test runs the view over made-up files and hands the result back), and drawn as a table, list, cards, kanban or map;
 * only the rows in view are drawn; the view menu's actions (add a view, change a setting) are written into the document as one edit each, so Undo
 * takes them back; a base that cannot be read is shown as a message and never written; an answer to an earlier run is dropped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_baseview.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 1000
#define H 700

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *BW;
static ExDoc *DOC;
static ExBaseData *DATA;
static size_t NFILES = 40;

typedef struct Got { unsigned long req; size_t view; int runs; char text[4000]; int opens, messages; char open[300], msg[300]; } Got;
static Got got;

static unsigned long run_cb(void *u, const char *text, size_t len, size_t view, const char *path) {
    Got *g = u;
    (void)path;
    g->runs++; g->req++; g->view = view;
    snprintf(g->text, sizeof g->text, "%.*s", (int)(len < sizeof g->text - 1 ? len : sizeof g->text - 1), text);
    return g->req;
}
static void open_cb(void *u, const char *path, int nt) { Got *g = u; (void)nt; g->opens++; snprintf(g->open, sizeof g->open, "%s", path); }
static void message_cb(void *u, const char *m) { Got *g = u; g->messages++; snprintf(g->msg, sizeof g->msg, "%s", m); }

static ExBaseData *make_data(size_t n) {
    ExBaseData *d = calloc(1, sizeof *d);
    size_t i;
    static const char *status[] = { "Planned", "In progress", "Done" };
    for (i = 0; i < n; i++) {
        char path[80], val[40];
        ExBaseFile *f;
        snprintf(path, sizeof path, "Projects/Note %04zu.md", i);
        f = ex_base_data_add(d, path, 1, 1.7e12, 1.7e12 + (double)i * 1000.0, 500.0 + (double)i);
        ex_base_file_prop(f, "status", EXB_TEXT, status[i % 3]);
        snprintf(val, sizeof val, "%zu", i % 17);
        ex_base_file_prop(f, "price", EXB_NUMBER, val);
        snprintf(val, sizeof val, "%d\n%d", (int)(i % 50) - 25, (int)(i % 90) - 45);
        ex_base_file_prop(f, "coordinates", EXB_LIST, val);
        ex_base_file_link(f, "Note 0000");
    }
    return d;
}

/* plays the service thread: runs the last request that has not been answered */
static unsigned long answered;
static void serve(void) {
    while (got.req > answered) {
        ExBase *b;
        ExBaseResult *res = calloc(1, sizeof *res);
        char err[100];
        unsigned long req = got.req;
        answered = req;
        b = ex_base_parse(got.text, strlen(got.text), err, sizeof err);
        if (b && ex_base_run(b, got.view, DATA, "x.base", 1.7e12, 0, res) == 0) ex_baseview_set_result(BW, req, res, NULL);
        else { free(res); ex_baseview_set_result(BW, req, NULL, "could not run"); }
        ex_base_free(b);
    }
}

static void frame(void) {
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}
/* frames until every run asked for has been answered and drawn */
static void settle(void) { int i; for (i = 0; i < 6; i++) { frame(); serve(); } frame(); }

static char *doc_text(void) { return ex_buffer_text(ex_doc_buffer(DOC), NULL); }
static void setup(const char *text) {
    ExBaseViewHandlers h;
    memset(&h, 0, sizeof h);
    h.user = &got; h.run = run_cb; h.open_file = open_cb; h.message = message_cb;
    memset(&got, 0, sizeof got); answered = 0;
    if (BW) { ex_ui_release(UI, BW); ex_widget_remove(ROOT, BW); ex_widget_free(BW); BW = NULL; }
    if (DOC) ex_doc_free(DOC);
    DOC = ex_doc_create(text, strlen(text));
    BW = ex_widget_base(UI, DOC, "Projects.base", &h);
    ex_widget_add(ROOT, BW);
    settle();
}

static void set_doc(const char *text) {
    char *old = doc_text();
    ExChange ch;
    ch.offset = 0; ch.remove_len = strlen(old); ch.text = text; ch.text_len = strlen(text);
    ex_doc_apply(DOC, &ch, 1, 0, NULL, 0);
    free(old);
    settle();
}

static const char *TABLE =
    "views:\n  - type: table\n    name: All\n    order: [file.name, status, price]\n    summaries:\n      price: Sum\n";

int main(void) {
    ExTheme theme;
    ExBaseViewStats st;
    char *before, *after;
    const ExBaseResult *res;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("base view", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    DATA = make_data(NFILES);

    /* ---- a table: run once, drawn, the result kept ---- */
    setup(TABLE);
    ex_baseview_stats(BW, &st);
    CHECK(st.runs == 1 && st.results == 1 && !st.failed && !st.loading);
    CHECK(st.layout == 0 && st.rows == NFILES && st.cols == 3);
    CHECK(st.items_drawn > 3 && st.items_drawn <= NFILES);
    res = ex_baseview_result(BW);
    CHECK(res && res->nrows == NFILES && !strcmp(res->cols[0].name, "File name"));
    CHECK(res && res->cols[2].summary_name != NULL);
    frame(); frame();
    ex_baseview_stats(BW, &st);
    CHECK(st.runs == 1);                                                           /* drawing again does not run it again */

    /* ---- only the rows in view are drawn; scrolling shows others and stays inside the content ---- */
    {
        size_t first_drawn;
        NFILES = 3000;
        ex_base_data_free(DATA); free(DATA);
        DATA = make_data(NFILES);
        ex_baseview_invalidate(BW);
        SDL_Delay(750);                                                           /* a change of the vault is run again at most about once a second */
        settle();
        ex_baseview_stats(BW, &st);
        first_drawn = st.items_drawn;
        CHECK(st.rows == 3000 && first_drawn > 5 && first_drawn < 60);
        ex_baseview_wheel(BW, 0, -20, 500, 300);
        frame();
        ex_baseview_stats(BW, &st);
        CHECK(st.scroll_y > 100.0f && st.items_drawn < 60 && st.items_drawn > 5);
        ex_baseview_wheel(BW, 0, -100000, 500, 300);
        frame();
        ex_baseview_stats(BW, &st);
        CHECK(st.scroll_y > 1000.0f && st.items_drawn > 3);                      /* clamped at the end, the last rows are drawn */
        ex_baseview_wheel(BW, 0, 1000000, 500, 300);
        frame();
        ex_baseview_stats(BW, &st);
        CHECK(st.scroll_y == 0.0f);
    }

    /* ---- the widget's own work in a frame does not grow with the number of rows ---- */
    {
        double total = 0;
        int i;
        for (i = 0; i < 10; i++) { frame(); ex_baseview_stats(BW, &st); total += st.own_ms; }
        CHECK(total / 10.0 < 25.0);                                                /* 3000 rows: a screenful, not all of them (unoptimized build; the limit is far above the work) */
    }
    NFILES = 40; ex_base_data_free(DATA); free(DATA); DATA = make_data(NFILES);

    /* ---- the copy is a Markdown table ---- */
    setup(TABLE);
    {
        char *t = ex_baseview_copy_text(BW);
        CHECK(t && t[0] == '|' && strstr(t, "File name") && strstr(t, "Note 0001"));
        free(t);
        t = ex_baseview_csv(BW, NULL);
        CHECK(t && strstr(t, "File name,") && strstr(t, "Note 0002"));
        free(t);
    }

    /* ---- adding a view is one edit of the document; Undo takes it back; the new layout is shown ---- */
    before = doc_text();
    ex_baseview_add_view(BW, "list");
    settle();
    after = doc_text();
    CHECK(strcmp(before, after) != 0 && strstr(after, "type: list") && strstr(after, "type: table") && strstr(after, "price: Sum"));
    ex_baseview_stats(BW, &st);
    CHECK(st.layout == 1 && st.rows == NFILES && ex_baseview_current_view(BW) == 1);
    ex_doc_undo(DOC);
    settle();
    free(after);
    after = doc_text();
    CHECK(strcmp(before, after) == 0);                                              /* the very same text */
    ex_baseview_stats(BW, &st);
    CHECK(st.layout == 0);
    free(before); free(after);

    /* ---- the layouts: each is drawn from the same result ---- */
    {
        static const char *docs[] = {
            "views:\n  - type: list\n    name: L\n    order: [file.name, status]\n    markers: number\n",
            "views:\n  - type: cards\n    name: C\n    order: [file.name, status, price]\n    cardSize: 200\n",
            "views:\n  - type: kanban\n    name: K\n    groupBy:\n      property: note.status\n      direction: ASC\n    order: [file.name, price]\n",
            "views:\n  - type: map\n    name: M\n    order: [file.name, coordinates]\n    coordinates: note.coordinates\n",
            "views:\n  - type: table\n    name: G\n    groupBy:\n      property: note.status\n      direction: ASC\n    order: [file.name, status]\n",
        };
        static const int layouts[] = { 1, 2, 3, 4, 0 };
        size_t i;
        for (i = 0; i < sizeof docs / sizeof *docs; i++) {
            setup(docs[i]);
            ex_baseview_stats(BW, &st);
            CHECK(st.layout == layouts[i] && st.rows == NFILES && !st.failed);
            if (!(st.items_drawn > 0)) fprintf(stderr, "layout %zu drew nothing (rows %zu cols %zu)\n", i, st.rows, st.cols);
            CHECK(st.items_drawn > 0);
            if (i == 2) CHECK(st.groups == 3);
            if (i == 4) CHECK(st.groups == 3);
        }
        /* a group of the grouped table collapses with a click on its heading and opens again */
        ex_baseview_stats(BW, &st);
        {
            size_t before_drawn = st.items_drawn;
            float x = BW->x + 60.0f, y = BW->y + 90.0f, yy;
            int found = 0;
            /* the headings are drawn under the column header: find one by clicking down the first rows */
            for (yy = y; yy < y + 400 && !found; yy += 4.0f) {
                ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, x, yy, 0, 1); ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, x, yy, 0, 1);
                frame();
                ex_baseview_stats(BW, &st);
                if (st.items_drawn != before_drawn) found = 1;
            }
            CHECK(found);
        }
    }

    /* ---- a link cell opens its file with a click; the right place is found by trying down the rows ---- */
    setup("views:\n  - type: table\n    name: T\n    order: [file.name, status]\n");
    {
        float y;
        for (y = BW->y + 40.0f; y < BW->y + 200.0f && !got.opens; y += 3.0f) {
            ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, BW->x + 60.0f, y, 0, 1); ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, BW->x + 60.0f, y, 0, 1);
        }
        CHECK(got.opens == 1 && strstr(got.open, "Projects/Note ") != NULL);
    }

    /* ---- a base that cannot be read is a message, never written ---- */
    {
        char *bad_before;
        setup("views: [unclosed\n  - : :");
        ex_baseview_stats(BW, &st);
        CHECK(st.failed && st.layout == -1);
        bad_before = doc_text();
        frame(); frame();
        ex_baseview_commit(BW);
        after = doc_text();
        CHECK(!strcmp(bad_before, after));
        free(bad_before); free(after);
        CHECK(ex_baseview_base(BW) == NULL);
    }

    /* ---- an answer to an earlier run is dropped ---- */
    setup(TABLE);
    {
        ExBaseResult *stale = calloc(1, sizeof *stale);
        ExBase *b = ex_base_parse(TABLE, strlen(TABLE), NULL, 0);
        unsigned long runs_before;
        ex_baseview_stats(BW, &st);
        runs_before = st.results;
        CHECK(b && ex_base_run(b, 0, DATA, "x.base", 1.7e12, 0, stale) == 0);
        ex_baseview_set_result(BW, 9999, stale, NULL);                              /* nobody asked for this one: freed, not shown */
        ex_baseview_stats(BW, &st);
        CHECK(st.results == runs_before);
        ex_base_free(b);
    }

    /* ---- a change of the document by other means is read again and the view is run again ---- */
    setup(TABLE);
    ex_baseview_stats(BW, &st);
    {
        unsigned long runs0 = st.runs;
        set_doc("views:\n  - type: table\n    name: All\n    limit: 5\n    order: [file.name]\n");
        ex_baseview_stats(BW, &st);
        CHECK(st.runs > runs0 && st.rows == 5 && st.cols == 1);
    }

    /* ---- the source: Edit source shows the YAML in an editor; typing there changes the document (and the view after a moment) ---- */
    setup(TABLE);
    ex_baseview_toggle_source(BW);
    frame();
    ex_baseview_stats(BW, &st);
    CHECK(st.source && ex_baseview_editing_source(BW));
    ex_baseview_toggle_source(BW);
    frame();
    ex_baseview_stats(BW, &st);
    CHECK(!st.source);

    /* ---- the draw does not write the document: no edit unless the user did something ---- */
    setup(TABLE);
    before = doc_text();
    {
        int i;
        for (i = 0; i < 5; i++) { frame(); serve(); }
    }
    after = doc_text();
    CHECK(!strcmp(before, after));
    free(before); free(after);

    ex_ui_release(UI, BW);
    ex_widget_remove(ROOT, BW);
    ex_widget_free(BW);
    ex_doc_free(DOC);
    ex_base_data_free(DATA); free(DATA);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("base_view: ok\n");
    return 0;
}
