/* editing_slash_editor: the slash menu in the editor: "/" at the start of a line or after a space opens it, typing narrows it,
 * Up and Down move, Enter runs the command and removes the "/..." text, Escape or a space closes it, a "/" inside a word
 * (a path, a fraction) does nothing, and a click on an item runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_render.h"
#include "exhaust_slash.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int bold_ran, italic_ran;
static ExDoc *g_doc;
static void run_bold(void *u) { (void)u; bold_ran++; ex_doc_replace_selections(g_doc, "**", 2, 0); }
static void run_italic(void *u) { (void)u; italic_ran++; }

static char *text_of(ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }
static void type(ExEditor *e, const char *s) { for (; *s; s++) ex_editor_text_input(e, s, 1); }          /* one character at a time, as typed */

int main(int argc, char **argv) {
    ExRenderer *r;
    ExTheme th;
    ExCommands *cmds = ex_commands_create(NULL);
    ExSlashMenu *m;
    ExDoc *d;
    ExEditor *e;
    char *t;
    float cx, cy, ch;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("slash", 800, 600);
    if (!r) return 1;
    ex_theme_init_builtin(&th, EX_THEME_DARK);
    ex_commands_bind(cmds, "editor.toggle-bold", run_bold, NULL, NULL);
    ex_commands_bind(cmds, "editor.toggle-italic", run_italic, NULL, NULL);
    m = ex_slash_create(cmds);
    d = ex_doc_create("", 0);
    g_doc = d;
    e = ex_editor_create(d);
    ex_editor_set_font(e, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
    ex_editor_set_viewport(e, 0, 0, 800, 600);
    ex_editor_set_mode(e, EX_EDITOR_SOURCE);

    if (argc > 2 && strcmp(argv[1], "--ppm") == 0) {                   /* for looking at: the popup after "/ta" in a short note */
        int w, h;
        uint8_t *px;
        FILE *f = fopen(argv[2], "wb");
        ex_editor_set_slash(e, m);
        type(e, "First line of the note\nSecond line\n/ta");
        ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
        ex_editor_draw(e, r, &th, 1, 1);
        px = ex_renderer_read_rgb(r, &w, &h);
        if (f && px) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(px, 1, (size_t)w * (size_t)h * 3, f); }
        if (f) fclose(f);
        free(px);
        return 0;
    }
    /* without a menu a slash is just a character */
    type(e, "/");
    CHECK(!ex_editor_slash_open(e));
    ex_editor_set_slash(e, m);
    ex_doc_set_caret(d, 0);
    ex_editor_key(e, r, EX_KEY_DELETE, 0);

    /* at the start of the note */
    type(e, "/");
    CHECK(ex_editor_slash_open(e) && ex_slash_count(m) == 73);
    type(e, "bol");
    CHECK(strcmp(ex_slash_query(m), "bol") == 0 && ex_slash_count(m) >= 1 && strcmp(ex_slash_item(m, 0)->id, "editor.toggle-bold") == 0);
    ex_editor_key(e, r, EX_KEY_BACKSPACE, 0);
    CHECK(strcmp(ex_slash_query(m), "bo") == 0);
    type(e, "l");
    ex_editor_key(e, r, EX_KEY_ENTER, 0);                              /* runs the command; the "/bol" is gone first */
    CHECK(!ex_editor_slash_open(e) && bold_ran == 1);
    t = text_of(d);
    CHECK(strcmp(t, "**") == 0);
    free(t);

    /* a space closes it and stays a space; Escape closes it and leaves the text */
    ex_doc_set_caret(d, 2);
    type(e, "\n/");
    CHECK(ex_editor_slash_open(e));
    type(e, "x");
    ex_editor_key(e, r, EX_KEY_ESCAPE, 0);
    CHECK(!ex_editor_slash_open(e));
    t = text_of(d);
    CHECK(strcmp(t, "**\n/x") == 0);
    free(t);
    type(e, " ");
    type(e, "/");                                                      /* after a space: opens */
    CHECK(ex_editor_slash_open(e));
    type(e, " ");
    CHECK(!ex_editor_slash_open(e));
    type(e, "a/");                                                     /* inside a word: no */
    CHECK(!ex_editor_slash_open(e));

    /* moving, and a command that is not bound is listed but does not run (the text stays) */
    ex_doc_set_caret(d, ex_buffer_size(ex_doc_buffer(d)));
    type(e, "\n/");
    CHECK(ex_editor_slash_open(e));
    ex_editor_key(e, r, EX_KEY_DOWN, 0);
    CHECK(ex_slash_selected(m) == 1);
    ex_editor_key(e, r, EX_KEY_UP, 0);
    ex_editor_key(e, r, EX_KEY_UP, 0);
    CHECK(ex_slash_selected(m) == ex_slash_count(m) - 1);
    {
        size_t before = ex_buffer_size(ex_doc_buffer(d));
        ex_editor_key(e, r, EX_KEY_ENTER, 0);                          /* the last command is not bound */
        CHECK(ex_editor_slash_open(e) && ex_buffer_size(ex_doc_buffer(d)) == before);
    }
    type(e, "ital");
    ex_editor_key(e, r, EX_KEY_TAB, 0);
    CHECK(!ex_editor_slash_open(e) && italic_ran == 1);

    /* the caret leaving the "/..." closes the menu; a click on an item runs it */
    ex_doc_set_caret(d, ex_buffer_size(ex_doc_buffer(d)));
    type(e, "\n/bold");
    CHECK(ex_editor_slash_open(e));
    ex_editor_key(e, r, EX_KEY_LEFT, 0);
    CHECK(ex_editor_slash_open(e));                                    /* still inside the typed text */
    ex_editor_key(e, r, EX_KEY_HOME, 0);
    CHECK(!ex_editor_slash_open(e));
    ex_doc_set_caret(d, ex_buffer_size(ex_doc_buffer(d)));
    type(e, "\n/bold");
    ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
    ex_editor_draw(e, r, &th, 1, 1);
    ex_editor_caret_rect(e, r, &cx, &cy, NULL, &ch);
    ex_editor_mouse(e, r, EX_MOUSE_DOWN, cx + 20, cy + ch + 2 + 8 + ch / 2, 0, 1);      /* the first row of the popup */
    CHECK(!ex_editor_slash_open(e) && bold_ran == 2);

    ex_editor_free(e);
    ex_doc_free(d);
    ex_slash_free(m);
    ex_commands_free(cmds);
    ex_renderer_destroy(r);
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
