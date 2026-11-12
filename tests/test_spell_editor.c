/* spellcheck_editor: the editor draws a wavy line under words the checker does not know, and nowhere else: not under correct words,
 * code, links' addresses or tags, not under the word the caret is in while typing, and not at all when spell check is off. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_render.h"
#include "exhaust_spell.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* Pixels of the error color in the rows of one line (lines are line-height rows apart from the top). */
static size_t error_pixels(ExRenderer *r, const ExTheme *th, int line, int lh) {
    int w, h, x, y;
    uint8_t *px = ex_renderer_read_rgb(r, &w, &h);
    ExColor ec = ex_theme_color(th, EX_COLOR_ERROR);
    size_t n = 0;
    if (!px) return 0;
    for (y = line * lh; y < (line + 1) * lh && y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *p = px + ((size_t)y * (size_t)w + (size_t)x) * 3;
            if (p[0] == ec.r && p[1] == ec.g && p[2] == ec.b) n++;
        }
    free(px);
    return n;
}

int main(void) {
    const char *src = "The cat sat\nthe teh word\n`teh` code\n[link](http://teh.example/teh) and #teh tag\nSee [[Teh page|alias]] now\n";
    ExRenderer *r;
    ExTheme th;
    ExSpell *sp = ex_spell_create_en_us();
    ExDoc *d;
    ExEditor *e;
    int lh;
    ExFontMetrics fm;
    ExFont *font;
    size_t a, b;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("spell", 600, 400);
    if (!r || !sp) return 1;
    ex_theme_init_builtin(&th, EX_THEME_DARK);
    d = ex_doc_create(src, strlen(src));
    e = ex_editor_create(d);
    font = ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR);
    ex_editor_set_font(e, font, 16.0f, 150);
    ex_editor_set_viewport(e, 0, 0, 600, 400);
    ex_editor_set_mode(e, EX_EDITOR_SOURCE);
    ex_font_metrics(font, ex_px(r, 16.0f), &fm);
    lh = (fm.line_height * 150 + 50) / 100;
    if (lh < fm.line_height) lh = fm.line_height;

#define DRAW(focused) do { ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND)); ex_editor_draw(e, r, &th, focused, 0); } while (0)

    DRAW(1);
    CHECK(error_pixels(r, &th, 1, lh) == 0);                         /* off: nothing is drawn */
    ex_editor_set_spell(e, sp);
    ex_doc_set_caret(d, 0);
    DRAW(1);
    CHECK(error_pixels(r, &th, 0, lh) == 0);                         /* correct words */
    CHECK(error_pixels(r, &th, 1, lh) > 0);                          /* "teh" */
    CHECK(error_pixels(r, &th, 2, lh) == 0);                         /* inside code */
    CHECK(error_pixels(r, &th, 3, lh) == 0);                         /* a link's address and a tag */
    CHECK(error_pixels(r, &th, 4, lh) == 0);                         /* the target of a wikilink, but not its alias... "Teh page" is a target */
    /* the word the caret is in is left alone while typing, and marked when the editor is not focused */
    ex_doc_set_caret(d, 17);                                         /* inside "teh" on the second line */
    DRAW(1);
    CHECK(error_pixels(r, &th, 1, lh) == 0);
    DRAW(0);
    CHECK(error_pixels(r, &th, 1, lh) > 0);
    CHECK(ex_editor_word_at_caret(e, &a, &b) && b - a == 3 && a == 16);
    /* words added to the dictionary stop being marked */
    ex_editor_spell_changed(e);
    ex_spell_add_word(sp, "teh", 3);
    ex_editor_spell_changed(e);
    DRAW(0);
    CHECK(error_pixels(r, &th, 1, lh) == 0);
    ex_editor_set_spell(e, NULL);
    DRAW(0);
    CHECK(error_pixels(r, &th, 1, lh) == 0);

    ex_editor_free(e);
    ex_doc_free(d);
    ex_spell_free(sp);
    ex_renderer_destroy(r);
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
