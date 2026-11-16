/* canvas_view (6a): the canvas widget drawn and used through the real UI. A canvas in a document is drawn (only what is in view, content only above the zoom
 * threshold), a card is moved, resized, added, edited, deleted, joined to another with an edge and pasted, each as one edit of the document (so Undo takes
 * it back to the very same text), changes made to the document by other means are read again, and a file that is not a canvas is shown as a message and
 * never written.
 * Usage: test_canvas_view <sample.canvas> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_canvasview.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define W 1000
#define H 700

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *CW;
static ExDoc *DOC;
static ExCanvasCache *CACHE;

typedef struct Got { int wants, opens, messages; char want[300], open[300], msg[300]; int creates; } Got;
static Got got;
static void want_text(void *u, const char *path) { Got *g = u; g->wants++; snprintf(g->want, sizeof g->want, "%s", path); }
static void open_file(void *u, const char *path, const char *sub, int nt) { Got *g = u; (void)sub; (void)nt; g->opens++; snprintf(g->open, sizeof g->open, "%s", path); }
static void message(void *u, const char *m) { Got *g = u; g->messages++; snprintf(g->msg, sizeof g->msg, "%s", m); }
static int create_note(void *u, const char *text, char *path, size_t cap) { Got *g = u; (void)text; g->creates++; snprintf(path, cap, "Converted.md"); return 1; }

static void frame(void) {
    ex_ui_layout(UI, ROOT, W, H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    SDL_FlushRenderer(SDL_GetRenderer(ex_renderer_window(R)));
}

static char *doc_text(void) { return ex_buffer_text(ex_doc_buffer(DOC), NULL); }
static ExCanvas *doc_canvas(void) { char *t = doc_text(); ExCanvas *c = ex_canvas_parse(t, strlen(t), NULL, 0); free(t); return c; }

static void setup(const char *text, const ExCanvasHandlers *extra) {
    ExCanvasHandlers h;
    memset(&h, 0, sizeof h);
    h.user = &got; h.want_text = want_text; h.open_file = open_file; h.message = message; h.create_note = create_note; h.cache = CACHE;
    (void)extra;
    memset(&got, 0, sizeof got);
    if (CW) { ex_ui_release(UI, CW); ex_widget_remove(ROOT, CW); ex_widget_free(CW); CW = NULL; }
    if (DOC) ex_doc_free(DOC);
    DOC = ex_doc_create(text, strlen(text));
    CW = ex_widget_canvas(UI, DOC, "Board.canvas", &h);
    ex_widget_add(ROOT, CW);
    ex_canvascache_invalidate(CACHE, "");
    frame();
}

static void press(float x, float y) { ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, x, y, 0, 1); }
static void drag(float x, float y) { ex_ui_mouse(UI, ROOT, EX_MOUSE_DRAG, x, y, 0, 1); }
static void release(float x, float y) { ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, x, y, 0, 1); }
static void click(float x, float y) { press(x, y); release(x, y); frame(); }
static void drag_from_to(float x0, float y0, float x1, float y1) {
    int i;
    press(x0, y0);
    for (i = 1; i <= 6; i++) { drag(x0 + (x1 - x0) * (float)i / 6.0f, y0 + (y1 - y0) * (float)i / 6.0f); frame(); }
    release(x1, y1);
    frame();
}
static void center(size_t card, float *x, float *y) { float cx, cy, cw, ch; CHECK(ex_canvasview_card_rect(CW, card, &cx, &cy, &cw, &ch)); *x = cx + cw / 2; *y = cy + ch / 2; }
static void key(ExKey k, int mods) { ex_ui_key(UI, k, mods); frame(); }
static void typed(const char *s) { ex_ui_text_input(UI, s, strlen(s)); frame(); }
static void zoom_one(void) { CW->on_text(CW, "1", 1, CW->on_text_user); frame(); }
static void fit(void) { CW->on_text(CW, "0", 1, CW->on_text_user); frame(); }
static int find_text_card(const ExCanvas *c, const char *needle) { size_t i; for (i = 0; i < c->n; i++) if (c->nodes[i].text && strstr(c->nodes[i].text, needle)) return (int)i; return -1; }

/* a point in the view that no card, group border or edge is at */
static int empty_point(float *x, float *y) {
    float px, py;
    for (px = 60; px < W - 60; px += 41) for (py = 60; py < H - 60; py += 37) {
        ExCanvasViewStats st;
        size_t i;
        int hit = 0;
        for (i = 0; i < ex_canvasview_model(CW)->n && !hit; i++) {
            float cx, cy, cw, ch;
            if (ex_canvasview_card_rect(CW, i, &cx, &cy, &cw, &ch) && px > cx - 30 && px < cx + cw + 30 && py > cy - 40 && py < cy + ch + 30) hit = 1;
        }
        (void)st;
        if (!hit) { *x = px; *y = py; return 1; }
    }
    return 0;
}

int main(int argc, char **argv) {
    ExTheme theme;
    char *sample;
    size_t slen = 0;
    ExCanvasViewStats st;
    char *before, *after;
    ExCanvas *c;
    float x, y, x2, y2;
    unsigned long ver;
    if (argc < 2) return 2;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("canvas view", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    CACHE = ex_canvascache_create();
    sample = ex_platform_read_file(argv[1], &slen);
    CHECK(sample != NULL);
    if (!sample) return 1;

    /* ---- drawing: the sample is read, fitted and drawn; below the zoom threshold cards are outlines, at zoom 1 they have text ---- */
    setup(sample, NULL);
    ex_canvasview_stats(CW, &st);
    CHECK(!st.broken && st.cards == 7 && st.edges == 4);
    CHECK(st.cards_drawn >= 6 && st.edges_drawn == 4 && st.triangles > 100);
    CHECK(st.zoom < 1.0f && st.content_hidden && st.texts_laid_out == 0);
    CHECK(got.wants == 0);                                                          /* nothing reads a note while the cards are outlines */
    zoom_one();
    ex_canvasview_stats(CW, &st);
    CHECK(st.zoom == 1.0f && !st.content_hidden && st.texts_laid_out > 0);
    CHECK(got.wants >= 1);                                                          /* a file card in view asks for its note's text, once */
    {
        int asked = got.wants;
        frame(); frame();
        CHECK(got.wants == asked);
        ex_canvascache_set(CACHE, got.want, "Excerpt of the note\nsecond line");
        frame();
        CHECK(got.wants == asked);
    }
    {   /* a stable frame builds no new geometry buffers */
        ExCanvasViewStats a, b;
        frame(); ex_canvasview_stats(CW, &a);
        frame(); ex_canvasview_stats(CW, &b);
        CHECK(a.geom_grows == b.geom_grows);
    }
    fit();
    ex_canvasview_stats(CW, &st);
    CHECK(st.zoom < 1.0f && st.content_hidden);

    /* ---- moving a card: one edit, the other cards untouched, Undo gives back the same text ---- */
    before = doc_text();
    c = doc_canvas();
    {
        int a = find_text_card(c, "Ideas");
        double ox, oy;
        float zoom;
        CHECK(a >= 0);
        ox = c->nodes[a].x; oy = c->nodes[a].y;
        zoom = (ex_canvasview_stats(CW, &st), st.zoom);
        center((size_t)a, &x, &y);
        ver = ex_doc_version(DOC);
        drag_from_to(x, y, x + 140.0f, y + 60.0f);
        after = doc_text();
        CHECK(strcmp(before, after) != 0 && ex_doc_version(DOC) > ver);
        ex_canvas_free(c);
        c = ex_canvas_parse(after, strlen(after), NULL, 0);
        CHECK(c && c->nodes[a].x > ox + 100.0 / zoom * 0.5 && c->nodes[a].y > oy + 20.0 / zoom * 0.5);       /* it moved about as far as the pointer did (snapped to a grid) */
        CHECK(c && fmod(c->nodes[a].x, 20.0) == 0.0 && fmod(c->nodes[a].y, 20.0) == 0.0);
        CHECK(c && c->nodes[0].x == -480 && c->nodes[2].x == -60 && c->n == 7 && c->ne == 4);                 /* the others are where they were */
        CHECK(c && c->nodes[6].type && !strcmp(c->nodes[6].type, "sticker"));                                  /* and the unknown card is still there */
        ex_canvas_free(c);
        key(EX_KEY_Z, EX_MOD_PRIMARY);
        free(after);
        after = doc_text();
        CHECK(!strcmp(before, after));                                                                         /* Undo: the very same text */
        ex_canvasview_stats(CW, &st);
        CHECK(st.reads >= 2);
        key(EX_KEY_Y, EX_MOD_PRIMARY);                                                                         /* Redo */
        free(after);
        after = doc_text();
        CHECK(strcmp(before, after) != 0);
        key(EX_KEY_Z, EX_MOD_PRIMARY);
        free(after);
    }
    free(before);
    c = NULL;

    /* ---- a click selects, a click on empty space clears; the group moves its cards with it ---- */
    fit();
    center(1, &x, &y);
    click(x, y);
    ex_canvasview_stats(CW, &st);
    CHECK(st.selected_cards == 1);
    if (empty_point(&x2, &y2)) { click(x2, y2); ex_canvasview_stats(CW, &st); CHECK(st.selected_cards == 0); }
    before = doc_text();
    {
        float cx, cy, cw, ch;
        CHECK(ex_canvasview_card_rect(CW, 0, &cx, &cy, &cw, &ch));                                              /* the group: pressed on its border */
        drag_from_to(cx + cw / 2, cy + 2, cx + cw / 2 + 80, cy + 2 + 40);
        after = doc_text();
        c = ex_canvas_parse(after, strlen(after), NULL, 0);
        CHECK(c && c->nodes[0].x != -480 && c->nodes[1].x != -440 && c->nodes[2].x != -60 && c->nodes[3].x != -440);     /* the cards inside moved with it */
        CHECK(c && c->nodes[5].x == 420 && c->nodes[6].x == 420);                                                         /* the ones outside did not */
        { double dx0 = c ? c->nodes[0].x + 480 : 0, dx1 = c ? c->nodes[1].x + 440 : 1; CHECK(dx0 == dx1); }              /* by the same amount */
        ex_canvas_free(c); free(after);
    }
    key(EX_KEY_Z, EX_MOD_PRIMARY);
    after = doc_text();
    CHECK(!strcmp(before, after));
    free(before); free(after);

    /* ---- resizing: the border of the selected card ---- */
    zoom_one();
    {
        int a;
        float cx, cy, cw, ch;
        double ow;
        c = doc_canvas();
        a = find_text_card(c, "Ideas");
        ow = c->nodes[a].w;
        ex_canvas_free(c);
        ex_canvasview_stats(CW, &st);
        /* bring the card into view and select it */
        CW->on_text(CW, "0", 1, CW->on_text_user);
        center((size_t)a, &x, &y);
        click(x, y);
        zoom_one();
        before = doc_text();
        CHECK(ex_canvasview_card_rect(CW, (size_t)a, &cx, &cy, &cw, &ch));
        if (cx + cw + 100 < W && cy + ch / 2 > 0 && cy + ch / 2 < H) {
            drag_from_to(cx + cw, cy + ch / 2, cx + cw + 100, cy + ch / 2);
            c = doc_canvas();
            CHECK(c && c->nodes[a].w >= ow + 60 && c->nodes[a].h == 120 && c->nodes[a].x == -440);
            ex_canvas_free(c);
            key(EX_KEY_Z, EX_MOD_PRIMARY);
            after = doc_text();
            CHECK(!strcmp(before, after));
            free(after);
        } else { fprintf(stderr, "(the card is not in view: resize not tried)\n"); failures++; }
        free(before);
    }

    /* ---- an edge from the dot at the side of a card to another card ---- */
    setup("{\n\t\"nodes\":[\n\t\t{\"id\":\"a\",\"type\":\"text\",\"text\":\"one\",\"x\":0,\"y\":0,\"width\":200,\"height\":100},\n\t\t{\"id\":\"b\",\"type\":\"text\",\"text\":\"two\",\"x\":400,\"y\":0,\"width\":200,\"height\":100}\n\t],\n\t\"edges\":[]\n}", NULL);
    zoom_one();
    {
        float ax, ay, aw, ah, bx, by, bw, bh;
        CHECK(ex_canvasview_card_rect(CW, 0, &ax, &ay, &aw, &ah) && ex_canvasview_card_rect(CW, 1, &bx, &by, &bw, &bh));
        click(ax + aw / 2, ay + ah / 2);                                                      /* select a */
        before = doc_text();
        drag_from_to(ax + aw + 12.0f, ay + ah / 2, bx + bw / 2, by + bh / 2);                    /* from the dot at its right side onto b */
        c = doc_canvas();
        CHECK(c && c->ne == 1 && !strcmp(c->edges[0].from, "a") && !strcmp(c->edges[0].to, "b") && c->edges[0].from_side == EX_CS_RIGHT && c->edges[0].to_side == EX_CS_LEFT);
        ex_canvas_free(c);
        ex_canvasview_stats(CW, &st);
        CHECK(st.edges == 1 && st.edges_drawn == 1 && st.selected_edges == 1 && st.selected_cards == 0);
        key(EX_KEY_DELETE, 0);                                                                    /* the selected edge goes; the cards stay */
        c = doc_canvas();
        CHECK(c && c->ne == 0 && c->n == 2);
        ex_canvas_free(c);
        key(EX_KEY_Z, EX_MOD_PRIMARY);
        key(EX_KEY_Z, EX_MOD_PRIMARY);
        after = doc_text();
        CHECK(!strcmp(before, after));
        free(before); free(after);
    }

    /* ---- deleting a card takes its edges; copy and paste; select all ---- */
    setup("{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"one\",\"x\":0,\"y\":0,\"width\":200,\"height\":100},{\"id\":\"b\",\"type\":\"text\",\"text\":\"two\",\"x\":400,\"y\":0,\"width\":200,\"height\":100},{\"id\":\"c\",\"type\":\"file\",\"file\":\"X.md\",\"x\":0,\"y\":200,\"width\":200,\"height\":100}],\"edges\":[{\"id\":\"e1\",\"fromNode\":\"a\",\"toNode\":\"b\"},{\"id\":\"e2\",\"fromNode\":\"b\",\"toNode\":\"c\",\"label\":\"to c\"}]}", NULL);
    zoom_one();
    before = doc_text();
    center(1, &x, &y);
    click(x, y);
    key(EX_KEY_DELETE, 0);
    c = doc_canvas();
    CHECK(c && c->n == 2 && c->ne == 0);
    ex_canvas_free(c);
    key(EX_KEY_Z, EX_MOD_PRIMARY);
    after = doc_text();
    CHECK(!strcmp(before, after));
    free(before); free(after);
    key(EX_KEY_A, EX_MOD_PRIMARY);
    ex_canvasview_stats(CW, &st);
    CHECK(st.selected_cards == 3);
    key(EX_KEY_C, EX_MOD_PRIMARY);
    key(EX_KEY_V, EX_MOD_PRIMARY);
    c = doc_canvas();
    CHECK(c && c->n == 6 && c->ne == 4);                                                          /* the three cards and the two edges between them, again, with new ids */
    if (c && c->n == 6) {
        size_t i, j;
        for (i = 0; i < c->n; i++) for (j = i + 1; j < c->n; j++) CHECK(strcmp(c->nodes[i].id, c->nodes[j].id) != 0);
        CHECK(c->nodes[3].kind == EX_CN_TEXT && !strcmp(c->nodes[3].text, "one") && c->nodes[5].kind == EX_CN_FILE && !strcmp(c->nodes[5].file, "X.md"));
        CHECK(c->edges[3].label && !strcmp(c->edges[3].label, "to c") && strcmp(c->edges[2].from, "a") != 0);
    }
    ex_canvas_free(c);
    ex_canvasview_stats(CW, &st);
    CHECK(st.selected_cards == 3);                                                                 /* the pasted cards are selected */
    key(EX_KEY_Z, EX_MOD_PRIMARY);
    c = doc_canvas();
    CHECK(c && c->n == 3 && c->ne == 2);
    ex_canvas_free(c);

    /* ---- Convert to file ---- */
    center(0, &x, &y);
    click(x, y);
    CHECK(ex_canvasview_convert_selected(CW) == 0 && got.creates == 1);
    c = doc_canvas();
    CHECK(c && c->nodes[0].kind == EX_CN_FILE && !strcmp(c->nodes[0].file, "Converted.md") && !c->nodes[0].text);
    ex_canvas_free(c);
    key(EX_KEY_Z, EX_MOD_PRIMARY);

    /* ---- a double click on empty space makes a text card and edits it: what is typed is in the card when editing ends ---- */
    setup("{\"nodes\":[{\"id\":\"a\",\"type\":\"text\",\"text\":\"one\",\"x\":0,\"y\":0,\"width\":200,\"height\":100}],\"edges\":[]}", NULL);
    zoom_one();
    before = doc_text();
    CHECK(empty_point(&x, &y));
    press(x, y); release(x, y);
    press(x, y); release(x, y);
    frame();
    ex_canvasview_stats(CW, &st);
    CHECK(st.cards == 2 && st.editing);
    typed("New *card*");
    key(EX_KEY_ESCAPE, 0);
    frame();
    ex_canvasview_stats(CW, &st);
    CHECK(!st.editing);
    c = doc_canvas();
    CHECK(c && c->n == 2 && c->nodes[1].kind == EX_CN_TEXT && c->nodes[1].text && !strcmp(c->nodes[1].text, "New *card*"));
    ex_canvas_free(c);
    /* editing an existing card: double click, add text, click outside */
    center(0, &x, &y);
    press(x, y); release(x, y);
    press(x, y); release(x, y);
    frame();
    ex_canvasview_stats(CW, &st);
    CHECK(st.editing);
    typed("!");
    if (empty_point(&x2, &y2)) { click(x2, y2); }
    ex_canvasview_stats(CW, &st);
    CHECK(!st.editing);
    c = doc_canvas();
    CHECK(c && c->nodes[0].text && !strcmp(c->nodes[0].text, "one!"));
    ex_canvas_free(c);
    free(before);

    /* ---- a change of the document by other means is read again, and the selection stays ---- */
    center(0, &x, &y);
    click(x, y);
    {
        char *t = doc_text();
        ExChange ch;
        const char *edit = "two";
        char *at = strstr(t, "one!");
        ExCanvasViewStats s1;
        CHECK(at != NULL);
        ex_canvasview_stats(CW, &s1);
        ch.offset = (size_t)(at - t); ch.remove_len = 4; ch.text = edit; ch.text_len = 3;
        ex_doc_apply(DOC, &ch, 1, 0, NULL, 0);
        frame();
        ex_canvasview_stats(CW, &st);
        CHECK(st.reads > s1.reads && st.selected_cards == 1 && !strcmp(ex_canvasview_model(CW)->nodes[0].text, "two"));
        free(t);
    }

    /* ---- the wheel pans (or zooms with Ctrl); zooming keeps the point under the pointer ---- */
    {
        ExCanvasViewStats a, b;
        float zx = 300, zy = 200;
        double X0, Y0;
        ex_canvasview_stats(CW, &a);
        ex_canvasview_wheel(CW, 1.0f, zx, zy);
        frame();
        ex_canvasview_stats(CW, &b);
        CHECK(b.cy < a.cy && b.cx == a.cx && b.zoom == a.zoom);                                    /* up: the canvas moves down, the view up */
        SDL_SetModState(SDL_KMOD_LCTRL);
        {
            float ox, oy, ow, oh, nx, ny, nw, nh;
            CHECK(ex_canvasview_card_rect(CW, 0, &ox, &oy, &ow, &oh));
            X0 = (zx - ox) / ow; Y0 = (zy - oy) / oh;
            ex_canvasview_wheel(CW, 2.0f, zx, zy);
            frame();
            CHECK(ex_canvasview_card_rect(CW, 0, &nx, &ny, &nw, &nh));
            CHECK(nw > ow * 1.2f);
            CHECK(fabs((zx - nx) / nw - X0) < 0.01 && fabs((zy - ny) / nh - Y0) < 0.01);       /* the same point of the card is under the pointer */
        }
        SDL_SetModState(SDL_KMOD_NONE);
    }

    /* ---- a view as text, for the session ---- */
    {
        char buf[80];
        ExCanvasViewStats s1, s2;
        ex_canvasview_get_view(CW, buf, sizeof buf);
        CHECK(buf[0] != 0);
        ex_canvasview_stats(CW, &s1);
        CHECK(ex_canvasview_set_view(CW, "120 -40 0.5") == 0);
        frame();
        ex_canvasview_stats(CW, &s2);
        CHECK(s2.cx == 120.0f && s2.cy == -40.0f && s2.zoom == 0.5f);
        CHECK(ex_canvasview_set_view(CW, "x y z") != 0 && ex_canvasview_set_view(CW, "0 0 9999") != 0 && ex_canvasview_set_view(CW, "0 0 0") != 0 && ex_canvasview_set_view(CW, "nan 0 1") != 0);
    }

    /* ---- an empty document is a new canvas: the first card writes the whole file ---- */
    setup("", NULL);
    ex_canvasview_stats(CW, &st);
    CHECK(!st.broken && st.cards == 0);
    before = doc_text();
    CHECK(before[0] == 0);
    free(before);
    CHECK(ex_canvasview_add_text(CW, -1, -1) == 0);
    key(EX_KEY_ESCAPE, 0);
    c = doc_canvas();
    CHECK(c && c->n == 1 && c->nodes[0].kind == EX_CN_TEXT && c->has_edges);
    ex_canvas_free(c);

    /* ---- a file that is not a canvas is shown as a message and is never written ---- */
    setup("{\"nodes\": [ this is not json", NULL);
    ex_canvasview_stats(CW, &st);
    CHECK(st.broken && st.cards == 0);
    ver = ex_doc_version(DOC);
    click(300, 300);
    press(300, 300); release(300, 300);
    press(300, 300); release(300, 300);
    key(EX_KEY_A, EX_MOD_PRIMARY);
    key(EX_KEY_DELETE, 0);
    key(EX_KEY_V, EX_MOD_PRIMARY);
    CHECK(ex_canvasview_add_text(CW, -1, -1) == -1 && ex_canvasview_add_group(CW, 10, 10) == -1 && ex_canvasview_add_file(CW, "A.md", 0, 0) == -1);
    ex_canvasview_commit(CW);
    CHECK(ex_doc_version(DOC) == ver);
    before = doc_text();
    CHECK(!strcmp(before, "{\"nodes\": [ this is not json"));
    free(before);

    /* ---- a canvas of other keys: they are kept through an edit ---- */
    setup(sample, NULL);
    {
        int a;
        c = doc_canvas();
        a = find_text_card(c, "Outside the group");
        ex_canvas_free(c);
        fit();
        center((size_t)a, &x, &y);
        drag_from_to(x, y, x + 60, y + 60);
        before = doc_text();
        CHECK(strstr(before, "\"zIndex\":7,\"meta\":{\"tags\":[\"a\",\"b\"],\"ratio\":1.50,\"big\":12345678901234567890,\"none\":null}") != NULL);
        CHECK(strstr(before, "\"style\":\"dashed\"") != NULL && strstr(before, "\"metadata\":{\"version\":\"1.0-1.0\",\"frontmatter\":{\"a\":1}}") != NULL);
        CHECK(strstr(before, "\"emoji\":\"\xF0\x9F\x99\x82\",\"size\":2.5e1") != NULL);
        free(before);
    }

    free(sample);
    ex_canvascache_free(CACHE);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
