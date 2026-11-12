/* keyboard_nav: every interactive widget can be reached and operated with the keyboard
 * and shows a visible focus ring. A scripted Tab sequence visits the widgets in the
 * golden focus order, Shift+Tab goes back, and Enter or Space presses each button.
 * Usage: test_keyboard_nav <focus-order-golden-file> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int clicks[8];
static void on_click(ExWidget *w, void *u) { (void)w; clicks[(int)(size_t)u]++; }

static const char *role_name(const ExWidget *w) {
    return w->type == EX_WIDGET_BUTTON ? "button" : w->type == EX_WIDGET_EDITOR ? "text-input" : w->type == EX_WIDGET_LABEL ? "label" : "group";
}

/* "role: name" of the focused widget, or "none" */
static void describe(const ExUi *ui, char *out, size_t cap) {
    const ExWidget *f = ex_ui_focus(ui);
    if (!f) snprintf(out, cap, "none");
    else snprintf(out, cap, "%s: %s", role_name(f), f->name ? f->name : "");
}

static void walk_check_focusable(const ExWidget *w) {
    for (; w; w = w->next) {
        if (w->type == EX_WIDGET_BUTTON || w->type == EX_WIDGET_EDITOR) CHECK(w->focusable); /* nothing interactive is mouse-only */
        walk_check_focusable(w->first_child);
    }
}

static int near(int a, int b, int tol) { return abs(a - b) <= tol; }

/* Is the focus ring (the accent color) drawn on the left edge of the widget? */
static int ring_visible(ExUi *ui, ExRenderer *r, ExWidget *root, const ExWidget *w, ExColor accent, ExColor bg) {
    uint8_t *px;
    int iw = 0, ih = 0, hit = 0, y, x;
    ex_renderer_clear(r, bg);
    ex_ui_draw(ui, root, 1);
    px = ex_renderer_read_rgb(r, &iw, &ih);
    if (!px) return 0;
    for (y = (int)(w->y + w->h / 2) - 1; y <= (int)(w->y + w->h / 2) + 1 && !hit; y++)
        for (x = (int)w->x; x < (int)w->x + 4; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)iw + (size_t)x) * 3;
            if (near(p[0], accent.r, 6) && near(p[1], accent.g, 6) && near(p[2], accent.b, 6)) hit = 1;
        }
    free(px);
    return hit;
}

static void run(ExBuiltinTheme which, const char *golden_path) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc = ex_doc_create("note", 4);
    ExWidget *root, *editor, *button = NULL;
    char got[64], *want, *line, *next;
    char seq[8][96];
    size_t n = 0, i;
    ExColor accent, bg;

    ex_theme_init_builtin(&theme, which);
    accent = ex_theme_color(&theme, EX_COLOR_ACCENT);
    bg = ex_theme_color(&theme, EX_COLOR_BACKGROUND);
    r = ex_renderer_create("keyboard", 960, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_sample_window(ui, doc, on_click, (void *)(size_t)0, &editor);
    ex_ui_layout(ui, root, 960, 600);
    walk_check_focusable(root);

    /* ---- the golden Tab order, one entry per line ---- */
    want = ex_platform_read_file(golden_path, NULL);
    CHECK(want != NULL);
    CHECK(ex_ui_focus(ui) == NULL);
    for (i = 0; i < 4; i++) { /* press Tab, record, and leave the editor when we get there */
        ex_ui_key(ui, EX_KEY_TAB, 0);
        describe(ui, got, sizeof got);
        if (n < 8) snprintf(seq[n++], sizeof seq[0], "%s", got);
        if (ex_ui_focus(ui) == editor) {
            char *before = ex_buffer_text(ex_doc_buffer(doc), NULL);
            ex_ui_key(ui, EX_KEY_TAB, 0);                       /* in an editor Tab types a tab */
            {
                char *after = ex_buffer_text(ex_doc_buffer(doc), NULL);
                CHECK(strlen(after) == strlen(before) + 1 && ex_ui_focus(ui) == editor);
                free(after);
            }
            free(before);
            ex_ui_key(ui, EX_KEY_Z, EX_MOD_PRIMARY);            /* take the tab back out */
            ex_ui_key(ui, EX_KEY_ESCAPE, 0);                    /* Escape, then Tab, leaves */
        }
    }
    /* the sequence wraps: after the last widget comes the first */
    if (want) {
        size_t k = 0;
        for (line = want; *line && k < n; line = next, k++) {
            next = strchr(line, '\n');
            if (next) { *next++ = 0; } else next = line + strlen(line);
            if (strlen(line) && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
            if (!*line) { k--; continue; }
            if (strcmp(seq[k], line) != 0) { fprintf(stderr, "focus order step %zu: got [%s], golden [%s]\n", k, seq[k], line); failures++; }
        }
        CHECK(k >= 2 && strcmp(seq[0], seq[2]) == 0 && strcmp(seq[1], seq[3]) == 0);   /* wrapped around */
    }
    free(want);

    /* ---- Shift+Tab goes back; Ctrl+Tab leaves an editor at once ---- */
    ex_ui_set_focus(ui, editor);
    ex_ui_key(ui, EX_KEY_TAB, EX_MOD_PRIMARY);
    describe(ui, got, sizeof got);
    CHECK(strncmp(got, "button", 6) == 0);                      /* wrapped to the first widget */
    ex_ui_key(ui, EX_KEY_TAB, EX_MOD_SHIFT);
    CHECK(ex_ui_focus(ui) == editor);                           /* Shift+Tab: back to the last */
    ex_ui_key(ui, EX_KEY_TAB, EX_MOD_PRIMARY | EX_MOD_SHIFT);
    describe(ui, got, sizeof got);
    CHECK(strncmp(got, "button", 6) == 0);
    button = ex_ui_focus(ui);
    CHECK(button && button->type == EX_WIDGET_BUTTON);

    /* ---- Enter and Space press the focused button, and only it ---- */
    ex_ui_key(ui, EX_KEY_ENTER, 0);
    CHECK(clicks[0] == 1);
    ex_ui_key(ui, EX_KEY_SPACE, 0);
    CHECK(clicks[0] == 2);
    ex_ui_set_focus(ui, editor);
    ex_ui_key(ui, EX_KEY_SPACE, 0);                             /* in the editor space is typed text, not a press */
    CHECK(clicks[0] == 2);
    ex_ui_key(ui, EX_KEY_ENTER, 0);                             /* and Enter types a line break */
    {
        char *t = ex_buffer_text(ex_doc_buffer(doc), NULL);
        CHECK(t && strchr(t, '\n') != NULL);
        free(t);
    }

    /* ---- a visible focus ring on every focusable widget, in this theme ---- */
    ex_ui_layout(ui, root, 960, 600);
    ex_ui_set_focus(ui, button);
    CHECK(ring_visible(ui, r, root, button, accent, bg));
    ex_ui_set_focus(ui, editor);
    CHECK(ring_visible(ui, r, root, editor, accent, bg));
    ex_ui_set_focus(ui, NULL);
    CHECK(!ring_visible(ui, r, root, editor, accent, bg));      /* the ring follows the focus */

    ex_widget_free(root);
    ex_doc_free(doc);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
}

/* A larger tree: three buttons, a label and a box between them, an editor. */
static void test_larger_tree(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc = ex_doc_create("", 0);
    ExWidget *root, *row, *b1, *b2, *b3, *lab, *ed;
    char got[96];
    const char *want[] = {"button: One", "button: Two", "text-input: Body", "button: Three", "button: One"};
    size_t i;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_LIGHT);
    r = ex_renderer_create("tree", 640, 480);
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "Window");
    root->role = EX_ROLE_WINDOW;
    root->width = ex_size_grow(); root->height = ex_size_grow();
    row = ex_widget_box(ui, 0, "Row");
    b1 = ex_widget_button(ui, "One", on_click, (void *)(size_t)1);
    b2 = ex_widget_button(ui, "Two", on_click, (void *)(size_t)2);
    lab = ex_widget_label(ui, "Not interactive");
    ed = ex_widget_editor(ui, doc, "Body");
    b3 = ex_widget_button(ui, "Three", on_click, (void *)(size_t)3);
    ex_widget_add(row, b1); ex_widget_add(row, b2); ex_widget_add(row, lab);
    ex_widget_add(root, row); ex_widget_add(root, ed); ex_widget_add(root, b3);
    ex_ui_layout(ui, root, 640, 480);
    {
        /* Tab, Tab, Tab, then Ctrl+Tab to leave the editor, then Tab: the label and boxes are skipped */
        static const int mods[5] = {0, 0, 0, EX_MOD_PRIMARY, 0};
        for (i = 0; i < 5; i++) {
            ex_ui_key(ui, EX_KEY_TAB, mods[i]);
            describe(ui, got, sizeof got);
            if (strcmp(got, want[i]) != 0) fprintf(stderr, "step %zu: got [%s] want [%s]\n", i, got, want[i]);
            CHECK(strcmp(got, want[i]) == 0);
        }
    }
    /* the label is never focused; Enter and Space press the buttons that have focus */
    ex_ui_set_focus(ui, b2);
    ex_ui_key(ui, EX_KEY_ENTER, 0);
    ex_ui_set_focus(ui, b3);
    ex_ui_key(ui, EX_KEY_SPACE, 0);
    CHECK(clicks[2] == 1 && clicks[3] == 1 && clicks[1] == 0);
    ex_widget_free(root);
    ex_doc_free(doc);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
}

int main(int argc, char **argv) {
    const char *golden = argc > 1 ? argv[1] : "tests/golden/focus_order.txt";
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    run(EX_THEME_DARK, golden);
    memset(clicks, 0, sizeof clicks);
    run(EX_THEME_LIGHT, golden);
    memset(clicks, 0, sizeof clicks);
    run(EX_THEME_HC_DARK, golden);
    memset(clicks, 0, sizeof clicks);
    test_larger_tree();
    SDL_Quit();
    return failures ? 1 : 0;
}
