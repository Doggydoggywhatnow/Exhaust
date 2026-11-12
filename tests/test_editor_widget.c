/* editor_widget: scripted input types, selects, undoes and redoes multilingual
 * text with several cursors under both token sets (dark and light); the widget
 * tree lays out with Clay from theme sizes; and the drawn pixels use the theme's
 * colors. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* An in-memory clipboard, so the test does not touch the real one. */
static char g_clip[1024];
static char *clip_get(void *u) {
    char *c;
    (void)u;
    if (!g_clip[0]) return NULL;
    c = malloc(strlen(g_clip) + 1);
    if (c) strcpy(c, g_clip);
    return c;
}
static void clip_set(void *u, const char *t) { (void)u; snprintf(g_clip, sizeof g_clip, "%s", t); }

static char *text_of(ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }
static int doc_is(ExDoc *d, const char *want) { char *t = text_of(d); int ok = t && strcmp(t, want) == 0; if (!ok) fprintf(stderr, "doc is [%s], wanted [%s]\n", t ? t : "?", want); free(t); return ok; }

static void type(ExUi *ui, const char *s) { ex_ui_text_input(ui, s, strlen(s)); }

static int clicked;
static void on_click(ExWidget *w, void *u) { (void)w; (void)u; clicked++; }

static void pixel(const uint8_t *px, int w, int x, int y, int *r, int *g, int *b) {
    const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 3;
    *r = p[0]; *g = p[1]; *b = p[2];
}

static int near(int a, int b, int tol) { return abs(a - b) <= tol; }

/* blend of src over dst with src's alpha */
static int mix(int src, int dst, int alpha) { return (src * alpha + dst * (255 - alpha) + 127) / 255; }

static void run_theme(ExBuiltinTheme which, float scale) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc;
    ExWidget *root, *row, *ribbon, *editor, *status, *button;
    ExClipboard clip = {clip_get, clip_set, NULL};
    int W = (int)(640 * scale), H = (int)(360 * scale);
    ExColor bg, sel, cur, fg;

    clicked = 0;

    CHECK(ex_theme_init_builtin(&theme, which) == 0);
    bg = ex_theme_color(&theme, EX_COLOR_BACKGROUND);
    sel = ex_theme_color(&theme, EX_COLOR_SELECTION);
    cur = ex_theme_color(&theme, EX_COLOR_CURSOR);
    fg = ex_theme_color(&theme, EX_COLOR_TEXT);
    r = ex_renderer_create("editor", W, H);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ex_renderer_set_display_scale(r, scale);
    ui = ex_ui_create(r, &theme, 16.0f);
    doc = ex_doc_create("", 0);

    /* ---- widget tree: [ribbon | editor] over a status row ---- */
    root = ex_widget_box(ui, 1, "window");
    root->role = EX_ROLE_WINDOW;
    root->width = ex_size_grow();
    root->height = ex_size_grow();
    root->bg_token = EX_COLOR_BACKGROUND;
    row = ex_widget_box(ui, 0, "main");
    row->width = ex_size_grow();
    row->height = ex_size_grow();
    ribbon = ex_widget_box(ui, 1, "ribbon");
    ribbon->width = ex_size_token(EX_SIZE_RIBBON_WIDTH, 1.0f);
    ribbon->height = ex_size_grow();
    ribbon->bg_token = EX_COLOR_BACKGROUND_ALT;
    button = ex_widget_button(ui, "Go", on_click, NULL);
    editor = ex_widget_editor(ui, doc, "note");
    ex_editor_set_clipboard(editor->editor, &clip);
    status = ex_widget_label(ui, "Ready");
    status->text_token = EX_COLOR_TEXT_MUTED;
    ex_widget_add(ribbon, button);
    ex_widget_add(row, ribbon);
    ex_widget_add(row, editor);
    ex_widget_add(root, row);
    ex_widget_add(root, status);
    CHECK(ex_widget_count(root) == 6);

    ex_ui_layout(ui, root, (float)W, (float)H);
    CHECK(near((int)ribbon->w, ex_px(r, (float)ex_theme_size(&theme, EX_SIZE_RIBBON_WIDTH)), 1));
    CHECK(near((int)(editor->x), (int)ribbon->w, 1) && near((int)(editor->w), W - (int)ribbon->w, 1));
    CHECK(status->y > editor->y && near((int)(status->y + status->h), H, 1) && editor->h > H / 2);
    CHECK(button->w > 0 && button->x >= ribbon->x && button->x + button->w <= ribbon->x + ribbon->w + 1);
    CHECK(ex_ui_widget_at(root, editor->x + 5, editor->y + 5) == editor);
    CHECK(ex_ui_widget_at(root, button->x + 2, button->y + 2) == button);
    CHECK(ex_ui_widget_at(root, -5, 3) == NULL);

    /* button: press and release inside calls back once; release outside does not */
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, button->x + 3, button->y + 3, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, button->x + 3, button->y + 3, 0, 1);
    CHECK(clicked == 1);
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, button->x + 3, button->y + 3, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, editor->x + 50, editor->y + 50, 0, 1);
    CHECK(clicked == 1);

    /* focus the editor by clicking in it */
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, editor->x + 40, editor->y + 10, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, editor->x + 40, editor->y + 10, 0, 1);
    CHECK(ex_ui_focus(ui) == editor);

    /* ---- typing multilingual text ---- */
    type(ui, "Hello ");
    type(ui, "\xD0\xBC\xD0\xB8\xD1\x80 ");                      /* мир */
    type(ui, "\xE4\xBD\xA0\xE5\xA5\xBD ");                      /* 你好 */
    type(ui, "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D ");              /* שלום */
    type(ui, "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 ");      /* مرحبا */
    type(ui, "e\xCC\x81 \xF0\x9F\x98\x80");                      /* é (combining) and an emoji */
    CHECK(doc_is(doc, "Hello \xD0\xBC\xD0\xB8\xD1\x80 \xE4\xBD\xA0\xE5\xA5\xBD \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
                      "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 e\xCC\x81 \xF0\x9F\x98\x80"));
    CHECK(ex_doc_node_count(doc) == 2); /* typing coalesced into one undo step */

    /* backspace removes whole clusters: the emoji, then the space, then e + combining accent as one */
    ex_ui_key(ui, EX_KEY_BACKSPACE, 0);                          /* the emoji */
    CHECK(doc_is(doc, "Hello \xD0\xBC\xD0\xB8\xD1\x80 \xE4\xBD\xA0\xE5\xA5\xBD \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
                      "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 e\xCC\x81 "));
    ex_ui_key(ui, EX_KEY_BACKSPACE, 0);                          /* the space */
    ex_ui_key(ui, EX_KEY_BACKSPACE, 0);                          /* "e" with its combining accent, as one */
    CHECK(doc_is(doc, "Hello \xD0\xBC\xD0\xB8\xD1\x80 \xE4\xBD\xA0\xE5\xA5\xBD \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
                      "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "));
    /* undo brings the deleted clusters back, one step per key press; redo repeats them */
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "Hello \xD0\xBC\xD0\xB8\xD1\x80 \xE4\xBD\xA0\xE5\xA5\xBD \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
                      "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 e\xCC\x81 \xF0\x9F\x98\x80"));
    ex_ui_key(ui, EX_KEY_Y, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY | EX_MOD_SHIFT);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY | EX_MOD_SHIFT);
    CHECK(doc_is(doc, "Hello \xD0\xBC\xD0\xB8\xD1\x80 \xE4\xBD\xA0\xE5\xA5\xBD \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
                      "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "));
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);                    /* the typing step */
    CHECK(doc_is(doc, ""));
    ex_ui_key(ui, EX_KEY_Y, EX_MOD_PRIMARY);
    CHECK(ex_buffer_size(ex_doc_buffer(doc)) > 20);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);

    /* ---- selecting: shift+arrows, word, line, select all ---- */
    type(ui, "alpha beta\ngamma delta\nepsilon\n");
    CHECK(doc_is(doc, "alpha beta\ngamma delta\nepsilon\n"));
    ex_ui_key(ui, EX_KEY_HOME, EX_MOD_PRIMARY);                  /* start of the document */
    CHECK(ex_doc_sel(doc, 0).head == 0);
    ex_ui_key(ui, EX_KEY_RIGHT, EX_MOD_SHIFT);
    ex_ui_key(ui, EX_KEY_RIGHT, EX_MOD_SHIFT);
    CHECK(ex_doc_sel(doc, 0).anchor == 0 && ex_doc_sel(doc, 0).head == 2);
    ex_ui_key(ui, EX_KEY_RIGHT, EX_MOD_PRIMARY | EX_MOD_SHIFT);  /* to the end of the word */
    CHECK(ex_doc_sel(doc, 0).head == 5);
    ex_ui_key(ui, EX_KEY_DOWN, EX_MOD_SHIFT);                    /* extend down a line */
    CHECK(ex_doc_sel(doc, 0).anchor == 0 && ex_doc_sel(doc, 0).head > 11);
    ex_ui_text_input(ui, "X", 1);                               /* typing replaces the selection */
    {   /* Shift+Down lands at the same x on the next line (the font is proportional, so near the same column) */
        char *now = text_of(doc);
        size_t n = strlen(now);
        CHECK(now[0] == 'X' && n > 16 && n < 22 && strcmp(now + n - 9, "\nepsilon\n") == 0 && strstr(now, "delta") != NULL);
        free(now);
    }
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "alpha beta\ngamma delta\nepsilon\n"));
    CHECK(ex_doc_sel(doc, 0).anchor != ex_doc_sel(doc, 0).head);  /* the selection is restored by undo */
    ex_ui_key(ui, EX_KEY_A, EX_MOD_PRIMARY);
    CHECK(ex_doc_sel(doc, 0).anchor == 0 && ex_doc_sel(doc, 0).head == ex_buffer_size(ex_doc_buffer(doc)));
    ex_ui_key(ui, EX_KEY_C, EX_MOD_PRIMARY);
    CHECK(strcmp(g_clip, "alpha beta\ngamma delta\nepsilon\n") == 0);
    ex_ui_key(ui, EX_KEY_DELETE, 0);
    CHECK(doc_is(doc, ""));
    ex_ui_key(ui, EX_KEY_V, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "alpha beta\ngamma delta\nepsilon\n"));

    /* ---- several cursors ---- */
    ex_ui_key(ui, EX_KEY_HOME, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_DOWN, EX_MOD_PRIMARY | EX_MOD_ALT);
    ex_ui_key(ui, EX_KEY_DOWN, EX_MOD_PRIMARY | EX_MOD_ALT);
    CHECK(ex_doc_sel_count(doc) == 3);
    type(ui, "- ");
    CHECK(doc_is(doc, "- alpha beta\n- gamma delta\n- epsilon\n"));
    ex_ui_key(ui, EX_KEY_END, 0);                                /* every cursor to its line end */
    type(ui, "!");
    CHECK(doc_is(doc, "- alpha beta!\n- gamma delta!\n- epsilon!\n"));
    ex_ui_key(ui, EX_KEY_BACKSPACE, 0);
    CHECK(doc_is(doc, "- alpha beta\n- gamma delta\n- epsilon\n"));
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);                     /* the backspace, all three cursors at once */
    CHECK(doc_is(doc, "- alpha beta!\n- gamma delta!\n- epsilon!\n") && ex_doc_sel_count(doc) == 3);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "alpha beta\ngamma delta\nepsilon\n"));
    ex_ui_key(ui, EX_KEY_Y, EX_MOD_PRIMARY);
    ex_ui_key(ui, EX_KEY_Y, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "- alpha beta!\n- gamma delta!\n- epsilon!\n"));
    ex_ui_key(ui, EX_KEY_Y, EX_MOD_PRIMARY);                     /* and the backspace again */
    CHECK(doc_is(doc, "- alpha beta\n- gamma delta\n- epsilon\n"));
    ex_ui_key(ui, EX_KEY_ESCAPE, 0);                             /* collapses to one caret */
    CHECK(ex_doc_sel_count(doc) == 1);
    ex_doc_set_caret(doc, 0);

    /* paste one line per cursor */
    snprintf(g_clip, sizeof g_clip, "A\nB\nC");
    {
        ExSel s[3];
        s[0].anchor = s[0].head = 0;
        s[1].anchor = s[1].head = 13;
        s[2].anchor = s[2].head = 27;
        ex_doc_set_selections(doc, s, 3);
    }
    ex_ui_key(ui, EX_KEY_V, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "A- alpha beta\nB- gamma delta\nC- epsilon\n"));
    ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);
    CHECK(doc_is(doc, "- alpha beta\n- gamma delta\n- epsilon\n"));

    /* ---- clicking places carets; double click selects a word; alt+click adds a cursor ---- */
    ex_doc_set_caret(doc, 8); /* "- alpha| beta!": after "- alpha" */
    {
        float cx, cy, cw, ch;
        ex_ui_layout(ui, root, (float)W, (float)H);
        ex_editor_caret_rect(editor->editor, r, &cx, &cy, &cw, &ch);
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, cx - 2.0f * scale, cy + ch / 2, 0, 1);
        ex_ui_mouse(ui, root, EX_MOUSE_UP, cx - 2.0f * scale, cy + ch / 2, 0, 1);
        CHECK(ex_doc_sel_count(doc) == 1 && ex_doc_sel(doc, 0).head == 8);
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, cx - 6.0f * scale, cy + ch / 2, 0, 2);
        ex_ui_mouse(ui, root, EX_MOUSE_UP, cx - 6.0f * scale, cy + ch / 2, 0, 2);
        CHECK(ex_doc_sel(doc, 0).anchor == 2 && ex_doc_sel(doc, 0).head == 7); /* "alpha" */
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, cx, cy + ch / 2 + ch, EX_MOD_ALT, 1);
        ex_ui_mouse(ui, root, EX_MOUSE_UP, cx, cy + ch / 2 + ch, EX_MOD_ALT, 1);
        CHECK(ex_doc_sel_count(doc) == 2);
    }

    /* ---- drawing: the pixels use the theme's colors ---- */
    {
        ExSel s[2];
        uint8_t *px;
        int w = 0, h = 0, pr, pg, pb, count = 0, x, y, found_sel = 0, found_caret = 0;
        float cx, cy, cw, chh;
        /* one selected word on line 2, a caret at the end of line 3 */
        s[0].anchor = 15; s[0].head = 20;
        s[1].anchor = s[1].head = 36;
        CHECK(doc_is(doc, "- alpha beta\n- gamma delta\n- epsilon\n"));
        ex_doc_set_selections(doc, s, 2);
        ex_ui_layout(ui, root, (float)W, (float)H);
        ex_renderer_clear(r, bg);
        ex_ui_draw(ui, root, 1);
        px = ex_renderer_read_rgb(r, &w, &h);
        CHECK(px && w == W && h == H);
        if (px) {
            /* an empty corner of the editor is the theme background */
            pixel(px, w, (int)(editor->x + editor->w - 4), (int)(editor->y + editor->h - 4), &pr, &pg, &pb);
            CHECK(pr == bg.r && pg == bg.g && pb == bg.b);
            /* the ribbon is the alternate background */
            {
                ExColor alt = ex_theme_color(&theme, EX_COLOR_BACKGROUND_ALT);
                pixel(px, w, (int)(ribbon->x + ribbon->w - 3), (int)(ribbon->y + ribbon->h - 3), &pr, &pg, &pb);
                CHECK(pr == alt.r && pg == alt.g && pb == alt.b);
            }
            /* selection: some pixel on the selected line is the selection color blended over the background */
            ex_editor_caret_rect(editor->editor, r, &cx, &cy, &cw, &chh);  /* the caret is on line 3 */
            for (x = (int)editor->x; x < (int)(editor->x + editor->w) && !found_sel; x++) {
                int lh = (int)((cy - editor->y) / 2.0f + 0.5f);
                y = (int)editor->y + lh + 2;       /* just inside the second line, above the glyphs */
                pixel(px, w, x, y, &pr, &pg, &pb);
                if (near(pr, mix(sel.r, bg.r, sel.a), 3) && near(pg, mix(sel.g, bg.g, sel.a), 3) && near(pb, mix(sel.b, bg.b, sel.a), 3)) found_sel = 1;
            }
            CHECK(found_sel);
            /* text: pixels close to the text color exist in the editor */
            for (y = (int)editor->y; y < (int)(editor->y + 3 * ex_px(r, 16) * 2); y++)
                for (x = (int)editor->x; x < (int)(editor->x + 200 * scale); x++) {
                    pixel(px, w, x, y, &pr, &pg, &pb);
                    if (near(pr, fg.r, 12) && near(pg, fg.g, 12) && near(pb, fg.b, 12)) count++;
                }
            CHECK(count > 100);
            /* caret: a column of cursor-colored pixels at the primary caret (the last selection) */
            ex_editor_caret_rect(editor->editor, r, &cx, &cy, &cw, &chh);
            for (x = (int)cx - 1; x <= (int)(cx + cw + 1) && !found_caret; x++) {
                pixel(px, w, x, (int)(cy + chh / 2), &pr, &pg, &pb);
                if (near(pr, cur.r, 2) && near(pg, cur.g, 2) && near(pb, cur.b, 2)) found_caret = 1;
            }
            CHECK(found_caret);
            free(px);
        }
        /* an unfocused editor draws no caret, and blink off hides it */
        ex_ui_set_focus(ui, NULL);
        ex_renderer_clear(r, bg);
        ex_ui_draw(ui, root, 1);
        px = ex_renderer_read_rgb(r, &w, &h);
        if (px) {
            int hit = 0;
            for (x = (int)cx - 1; x <= (int)(cx + cw + 1); x++) {
                pixel(px, w, x, (int)(cy + chh / 2), &pr, &pg, &pb);
                if (near(pr, cur.r, 2) && near(pg, cur.g, 2) && near(pb, cur.b, 2)) hit = 1;
            }
            CHECK(!hit || (cur.r == bg.r && cur.g == bg.g && cur.b == bg.b));
            free(px);
        }
    }

    ex_widget_free(root);
    ex_doc_free(doc);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
}

int main(void) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    run_theme(EX_THEME_DARK, 1.0f);
    run_theme(EX_THEME_LIGHT, 1.0f);
    run_theme(EX_THEME_DARK, 2.0f);       /* HiDPI: sizes scale with the display */
    run_theme(EX_THEME_HC_LIGHT, 1.5f);
    SDL_Quit();
    return failures ? 1 : 0;
}
