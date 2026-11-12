/* editing_findbar: the find and replace bar on a note: incremental search, the count, next and previous with wrap-around,
 * the case, word and regular expression switches, replace and replace all (one undo step), errors in patterns, and that
 * the bar never changes the text unless a replace is asked for. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_findbar.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *text_of(ExDoc *d) { return ex_buffer_text(ex_doc_buffer(d), NULL); }
static size_t sel_lo(ExDoc *d) { ExSel s = ex_doc_sel(d, 0); return s.anchor < s.head ? s.anchor : s.head; }
static size_t sel_hi(ExDoc *d) { ExSel s = ex_doc_sel(d, 0); return s.anchor < s.head ? s.head : s.anchor; }

int main(void) {
    const char *src = "The cat sat. the Cat ran.\nA catalog of cats.\n";
    ExRenderer *r;
    ExTheme th;
    ExUi *ui;
    ExDoc *d;
    ExWidget *w;
    ExFindBar *fb;
    char *t;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("findbar", 800, 600);
    if (!r) return 1;
    ex_theme_init_builtin(&th, EX_THEME_DARK);
    ui = ex_ui_create(r, &th, 16.0f);
    d = ex_doc_create(src, strlen(src));
    w = ex_widget_editor(ui, d, "note");
    ex_editor_set_mode(w->editor, EX_EDITOR_SOURCE);
    fb = ex_findbar_create(ui);
    CHECK(fb != NULL);
    CHECK(!ex_findbar_is_open(fb));
    ex_findbar_open(fb, w, 1);
    CHECK(ex_findbar_is_open(fb) && !ex_findbar_widget(fb)->hidden);

    /* incremental search: case does not matter by default */
    ex_findbar_set_pattern(fb, "cat");
    ex_findbar_update(fb);
    CHECK(ex_findbar_match_count(fb) == 4);                         /* cat, Cat, cat(alog), cat(s) */
    CHECK(sel_lo(d) == 4 && sel_hi(d) == 7);                        /* the first match is selected */
    ex_findbar_next(fb, 1);
    CHECK(sel_lo(d) == 17 && sel_hi(d) == 20);
    ex_findbar_next(fb, 1); ex_findbar_next(fb, 1);
    CHECK(sel_lo(d) == 39);                                         /* the last "cats" */
    ex_findbar_next(fb, 1);
    CHECK(sel_lo(d) == 4);                                          /* wraps */
    ex_findbar_next(fb, -1);
    CHECK(sel_lo(d) == 39);                                         /* and back */
    CHECK(strstr(ex_findbar_status(fb), "of 4") != NULL);

    /* switches */
    ex_findbar_set_option(fb, EX_FINDBAR_CASE, 1);
    ex_findbar_update(fb);
    CHECK(ex_findbar_match_count(fb) == 3);
    ex_findbar_set_option(fb, EX_FINDBAR_WORD, 1);
    ex_findbar_update(fb);
    CHECK(ex_findbar_match_count(fb) == 1);                         /* only "cat" as a whole word, same case */
    ex_findbar_set_option(fb, EX_FINDBAR_WORD, 0);
    ex_findbar_set_option(fb, EX_FINDBAR_CASE, 0);
    ex_findbar_set_option(fb, EX_FINDBAR_REGEX, 1);
    ex_findbar_set_pattern(fb, "c[a-z]+s?\\b");
    ex_findbar_update(fb);
    CHECK(ex_findbar_match_count(fb) >= 4);
    ex_findbar_set_pattern(fb, "(unclosed");
    ex_findbar_update(fb);
    CHECK(ex_findbar_match_count(fb) == 0 && ex_findbar_status(fb)[0] != 0 && strcmp(ex_findbar_status(fb), "No results") != 0);
    t = text_of(d);
    CHECK(strcmp(t, src) == 0);                                     /* nothing so far changed the text */
    free(t);

    /* replace: the selected match, then the next */
    ex_findbar_set_option(fb, EX_FINDBAR_REGEX, 0);
    ex_findbar_set_pattern(fb, "cat");
    ex_findbar_set_replacement(fb, "dog");
    ex_findbar_update(fb);
    ex_editor_select(w->editor, r, 4, 7);
    CHECK(ex_findbar_replace(fb) == 1);
    t = text_of(d);
    CHECK(strncmp(t, "The dog sat. the Cat ran.", 25) == 0);
    free(t);
    CHECK(sel_lo(d) == 17);                                         /* moved on to the next match */
    CHECK(ex_findbar_replace(fb) == 1);
    t = text_of(d);
    CHECK(strncmp(t, "The dog sat. the dog ran.", 25) == 0);
    free(t);

    /* replace all with groups: one undo brings everything back */
    ex_findbar_set_option(fb, EX_FINDBAR_REGEX, 1);
    ex_findbar_set_pattern(fb, "(c)at(\\w*)");
    ex_findbar_set_replacement(fb, "$1$2-$2");
    ex_findbar_update(fb);
    CHECK(ex_findbar_replace_all(fb) == 2);                         /* "catalog" and "cats" */
    t = text_of(d);
    CHECK(strstr(t, "A calog-alog of cs-s.") != NULL);
    free(t);
    ex_doc_undo(d);
    t = text_of(d);
    CHECK(strstr(t, "A catalog of cats.") != NULL);
    free(t);

    /* a read-only editor is never changed */
    ex_editor_set_readonly(w->editor, 1);
    CHECK(ex_findbar_replace_all(fb) == 0);
    t = text_of(d);
    CHECK(strstr(t, "A catalog of cats.") != NULL);
    free(t);
    ex_editor_set_readonly(w->editor, 0);

    /* the selection becomes the pattern when the bar opens; Escape closes and gives the focus back */
    ex_findbar_close(fb);
    CHECK(!ex_findbar_is_open(fb) && ex_findbar_widget(fb)->hidden && ex_ui_focus(ui) == w);
    ex_editor_select(w->editor, r, 2, 3);                             /* "e" */
    ex_findbar_set_option(fb, EX_FINDBAR_REGEX, 0);
    ex_findbar_open(fb, w, 0);
    CHECK(ex_findbar_match_count(fb) >= 1);
    CHECK(ex_findbar_widget(fb)->first_child && ex_findbar_widget(fb)->first_child->next->hidden);   /* find only: no replace row */

    ex_findbar_free(fb);
    ex_widget_free(w);
    ex_doc_free(d);
    ex_ui_free(ui);
    ex_renderer_destroy(r);
    printf("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
