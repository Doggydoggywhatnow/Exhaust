/* shell_ui (sub-slice 7f): the shell of the window. Zoom levels and the steps between them, the size of a new window, which buttons the ribbon
 * shows (the list, with unknown ids kept), the ribbon bar itself (buttons, hiding, the graph button needing its feature, a press runs its command),
 * the status bar items (mode, save state, polling notice, message), and high contrast: the built-in high contrast theme, with the theme file and
 * the accent colour not applied. Works in ./shell_ui_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_ribbon.h"
#include "exhaust_shell.h"
#include "exhaust_statusbar.h"
#include "exhaust_themes.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "shell_ui_test/vault/.exhaust"
#define G "shell_ui_test/global"

static void test_zoom_and_size(void) {
    size_t i;
    CHECK(ex_zoom_level_count() == 14 && ex_zoom_level(0) == 50 && ex_zoom_level(5) == 100 && ex_zoom_level(13) == 400 && ex_zoom_level(14) == 0);
    for (i = 1; i < ex_zoom_level_count(); i++) CHECK(ex_zoom_level(i) > ex_zoom_level(i - 1));
    CHECK(ex_zoom_clamp(10) == 50 && ex_zoom_clamp(50) == 50 && ex_zoom_clamp(123) == 123 && ex_zoom_clamp(400) == 400 && ex_zoom_clamp(9000) == 400 && ex_zoom_clamp(-5) == 50);
    /* from a level: the next level; from between two: the next one in that direction */
    CHECK(ex_zoom_step(100, 1) == 110 && ex_zoom_step(100, -1) == 90 && ex_zoom_step(110, 1) == 125 && ex_zoom_step(50, -1) == 50 && ex_zoom_step(400, 1) == 400);
    CHECK(ex_zoom_step(105, 1) == 110 && ex_zoom_step(105, -1) == 100 && ex_zoom_step(111, -1) == 110 && ex_zoom_step(399, 1) == 400 && ex_zoom_step(51, -1) == 50);
    CHECK(ex_zoom_step(250, 0) == 100 && ex_zoom_step(7, 1) == 67 && ex_zoom_step(7000, -1) == 300);       /* a value out of range counts as its limit */
    /* walking up and down visits every level and comes back */
    {   int z = 50, up = 0;
        while (ex_zoom_step(z, 1) != z) { z = ex_zoom_step(z, 1); up++; }
        CHECK(z == 400 && up == 13);
        while (ex_zoom_step(z, -1) != z) { z = ex_zoom_step(z, -1); up--; }
        CHECK(z == 50 && up == 0); }
    CHECK(ex_window_size_clamp(100) == 400 && ex_window_size_clamp(1100) == 1100 && ex_window_size_clamp(99999) == 10000 && ex_window_size_clamp(400) == 400);
}

static void test_ribbon_list(void) {
    char *s;
    size_t i;
    CHECK(ex_ribbon_item_count() == 9);
    CHECK(ex_ribbon_item(0)->top == 1 && ex_ribbon_item(3)->top == 1 && ex_ribbon_item(4)->top == 0 && ex_ribbon_item(9) == NULL);
    for (i = 0; i < 9; i++) CHECK(ex_ribbon_shown("", ex_ribbon_item(i)->id) && ex_ribbon_shown(NULL, ex_ribbon_item(i)->id));       /* empty: all */
    CHECK(ex_ribbon_shown("app.open-settings", "app.open-settings") && !ex_ribbon_shown("app.open-settings", "app.change-theme"));
    CHECK(ex_ribbon_shown(" app.change-theme ,app.open-settings", "app.change-theme"));                                              /* spaces around ids do not matter */
    CHECK(!ex_ribbon_shown("none", "app.open-settings") && !ex_ribbon_shown("none, bogus", "app.open-settings"));
    /* hiding one from "all" lists the other eight, in the order of the bar */
    s = ex_ribbon_set("", "app.change-theme", 0);
    CHECK(s && strstr(s, "app.open-settings") && !strstr(s, "app.change-theme") && !ex_ribbon_shown(s, "app.change-theme") && ex_ribbon_shown(s, "app.open-settings") && ex_ribbon_shown(s, ex_ribbon_item(0)->id));
    CHECK(strncmp(s, ex_ribbon_item(0)->id, strlen(ex_ribbon_item(0)->id)) == 0);
    {   char *back = ex_ribbon_set(s, "app.change-theme", 1);                                                                       /* showing it again: all, which is the empty value */
        CHECK(back && back[0] == 0); free(back); }
    free(s);
    /* hiding all: none; showing one of them lists only it */
    s = ex_ribbon_set("", ex_ribbon_item(0)->id, 0);
    for (i = 1; i < 9 && s; i++) { char *n = ex_ribbon_set(s, ex_ribbon_item(i)->id, 0); free(s); s = n; }
    CHECK(s && !strcmp(s, "none"));
    for (i = 0; i < 9; i++) CHECK(!ex_ribbon_shown(s, ex_ribbon_item(i)->id));
    {   char *one = ex_ribbon_set(s, "app.open-settings", 1);
        CHECK(one && !strcmp(one, "app.open-settings") && ex_ribbon_shown(one, "app.open-settings") && !ex_ribbon_shown(one, "app.change-theme")); free(one); }
    free(s);
    /* ids this version does not know stay, also when everything is shown or hidden */
    s = ex_ribbon_set("future.thing, app.open-settings", "app.open-settings", 1);
    CHECK(s && strstr(s, "future.thing") && ex_ribbon_shown(s, "app.open-settings"));
    {   char *all = ex_ribbon_set("", "app.open-settings", 1);
        CHECK(all && all[0] == 0); free(all); }
    {   size_t k; char *cur = ex_ribbon_set("future.thing", "app.open-settings", 1);
        for (k = 0; k < 9 && cur; k++) { char *n = ex_ribbon_set(cur, ex_ribbon_item(k)->id, 1); free(cur); cur = n; }
        CHECK(cur && strstr(cur, "future.thing"));                                                                                   /* all nine shown and the unknown kept */
        for (k = 0; k < 9; k++) CHECK(ex_ribbon_shown(cur, ex_ribbon_item(k)->id));
        free(cur); }
    free(s);
    s = ex_ribbon_set("future.thing", ex_ribbon_item(0)->id, 0);                                                                     /* a list with only unknown ids: hidden one stays hidden */
    CHECK(s && strstr(s, "future.thing") && !ex_ribbon_shown(s, ex_ribbon_item(0)->id));
    free(s);
    CHECK(ex_high_contrast_resolve("on", 0) == 1 && ex_high_contrast_resolve("off", 1) == 0 && ex_high_contrast_resolve("system", 1) == 1 && ex_high_contrast_resolve("system", 0) == 0
          && ex_high_contrast_resolve("", 1) == 0 && ex_high_contrast_resolve(NULL, 1) == 0 && ex_high_contrast_resolve("bogus", 1) == 0);
}

static char g_ran[200]; static int g_runs;
static void ribbon_run(void *u, const char *id) { (void)u; snprintf(g_ran, sizeof g_ran, "%s", id); g_runs++; }

static void test_ribbon_and_status(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExRibbon *rb;
    ExRibbonHooks h;
    ExStatusBar *sb;
    size_t i;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create("shell ui", 800, 600);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);

    h.user = NULL; h.run = ribbon_run;
    rb = ex_ribbon_create(ui, &h);
    CHECK(rb && ex_ribbon_widget(rb));
    for (i = 0; i < 9; i++) CHECK(ex_ribbon_button(rb, ex_ribbon_item(i)->id) != NULL);
    CHECK(ex_ribbon_button(rb, "nothing.here") == NULL);
    ex_ribbon_apply(rb, 1, "", 1, 1);
    CHECK(ex_ribbon_visible(rb) && ex_ribbon_button_visible(rb, "app.open-settings") && ex_ribbon_button_visible(rb, "graph-view.open-graph-view"));
    ex_ribbon_apply(rb, 1, "", 0, 1);                                                                       /* the graph feature is off: its button goes, the others stay */
    CHECK(!ex_ribbon_button_visible(rb, "graph-view.open-graph-view") && ex_ribbon_button_visible(rb, "app.open-settings"));
    ex_ribbon_apply(rb, 1, "app.open-settings, graph-view.open-graph-view", 1, 1);
    CHECK(ex_ribbon_button_visible(rb, "app.open-settings") && ex_ribbon_button_visible(rb, "graph-view.open-graph-view") && !ex_ribbon_button_visible(rb, "app.change-theme") && !ex_ribbon_button_visible(rb, ex_ribbon_item(0)->id));
    ex_ribbon_apply(rb, 0, "", 1, 1);                                                                       /* show_ribbon off: nothing */
    CHECK(!ex_ribbon_visible(rb) && ex_ribbon_widget(rb)->hidden && !ex_ribbon_button_visible(rb, "app.open-settings"));
    ex_ribbon_apply(rb, 1, "none", 1, 1);
    for (i = 0; i < 9; i++) CHECK(!ex_ribbon_button_visible(rb, ex_ribbon_item(i)->id));
    ex_ribbon_apply(rb, 1, "", 1, 1);
    /* a press runs the command of the button; the canvas button works since 6a and follows its feature switch */
    {   ExWidget *b = ex_ribbon_button(rb, "app.change-theme");
        g_runs = 0;
        if (b && b->on_click) b->on_click(b, b->on_click_user);
        CHECK(g_runs == 1 && !strcmp(g_ran, "app.change-theme"));
        b = ex_ribbon_button(rb, "command-palette.open-command-palette");
        if (b && b->on_click) b->on_click(b, b->on_click_user);
        CHECK(g_runs == 2 && !strcmp(g_ran, "command-palette.open-command-palette"));
        b = ex_ribbon_button(rb, "canvas.create-new-canvas");
        CHECK(b && !b->disabled && b->on_click);
        if (b && b->on_click) b->on_click(b, b->on_click_user);
        CHECK(g_runs == 3 && !strcmp(g_ran, "canvas.create-new-canvas"));
        ex_ribbon_apply(rb, 1, "", 1, 0);                                                                  /* the canvas feature is off: its button goes */
        CHECK(!ex_ribbon_button_visible(rb, "canvas.create-new-canvas") && ex_ribbon_button_visible(rb, "graph-view.open-graph-view"));
        ex_ribbon_apply(rb, 1, "", 1, 1); }
    ex_widget_free(ex_ribbon_widget(rb));
    ex_ribbon_free(rb);

    sb = ex_statusbar_create(ui);
    CHECK(sb && ex_statusbar_widget(sb));
    CHECK(!ex_statusbar_item_visible(sb, "mode") && !ex_statusbar_item_visible(sb, "save") && !ex_statusbar_item_visible(sb, "polling") && !ex_statusbar_item_visible(sb, "words"));
    ex_statusbar_message(sb, "A message");
    CHECK(!strcmp(ex_statusbar_text(sb, "message"), "A message"));
    ex_statusbar_set_mode(sb, "live", 1);   CHECK(!strcmp(ex_statusbar_text(sb, "mode"), "Live preview"));
    ex_statusbar_set_mode(sb, "source", 1); CHECK(!strcmp(ex_statusbar_text(sb, "mode"), "Source mode"));
    ex_statusbar_set_mode(sb, "reading", 1); CHECK(!strcmp(ex_statusbar_text(sb, "mode"), "Reading view"));
    ex_statusbar_set_mode(sb, "source", 0); CHECK(!ex_statusbar_item_visible(sb, "mode"));                  /* the setting is off */
    ex_statusbar_set_mode(sb, NULL, 1);     CHECK(!ex_statusbar_item_visible(sb, "mode"));                  /* no note */
    ex_statusbar_set_save(sb, EX_SAVE_SAVED);   CHECK(!strcmp(ex_statusbar_text(sb, "save"), "Saved"));
    ex_statusbar_set_save(sb, EX_SAVE_EDITING); CHECK(!strcmp(ex_statusbar_text(sb, "save"), "Editing"));
    ex_statusbar_set_save(sb, EX_SAVE_FAILED);  CHECK(!strcmp(ex_statusbar_text(sb, "save"), "Not saved") && ex_statusbar_words(sb) != NULL);
    ex_statusbar_set_save(sb, EX_SAVE_NONE);    CHECK(!ex_statusbar_item_visible(sb, "save"));
    ex_statusbar_set_polling(sb, 1); CHECK(ex_statusbar_item_visible(sb, "polling") && strstr(ex_statusbar_text(sb, "polling"), "polling"));
    ex_statusbar_set_polling(sb, 0); CHECK(!ex_statusbar_item_visible(sb, "polling"));
    /* the bar lays out and draws with every item and a message far longer than the room */
    {   ExWidget *root = ex_widget_box(ui, 1, "root"), *spacer = ex_widget_box(ui, 1, "spacer");
        char longmsg[2000];
        memset(longmsg, 'x', sizeof longmsg - 1); longmsg[sizeof longmsg - 1] = 0;
        root->width = ex_size_grow(); root->height = ex_size_grow(); spacer->width = ex_size_grow(); spacer->height = ex_size_grow();
        ex_widget_add(root, spacer);
        ex_widget_add(root, ex_statusbar_widget(sb));
        ex_statusbar_message(sb, longmsg);
        ex_statusbar_set_mode(sb, "live", 1); ex_statusbar_set_save(sb, EX_SAVE_EDITING); ex_statusbar_set_polling(sb, 1);
        ex_widget_set_text(ex_statusbar_words(sb), "12 words, 80 characters"); ex_statusbar_words(sb)->hidden = 0;
        ex_ui_layout(ui, root, 400.0f, 300.0f);
        ex_renderer_clear(r, (ExColor){0, 0, 0, 255});
        ex_ui_draw(ui, root, 1);
        ex_ui_layout(ui, root, 400.0f, 300.0f);
        {   ExWidget *m = ex_statusbar_message_label(sb), *wd = ex_statusbar_words(sb);
            CHECK(m && wd && wd->x + wd->w <= 400.5f && m->x + m->w <= wd->x + 0.5f);                       /* the items stay in the window and the message ends before them */
            CHECK(ex_statusbar_widget(sb)->y + ex_statusbar_widget(sb)->h <= 300.5f); }
        ex_widget_free(root); }
    ex_statusbar_free(sb);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
}

static int build_with(ExThemes *t, int hc, const char *scheme, const char *theme, const char *accent, const char *snippets, ExTheme *out) {
    Uint64 t0 = SDL_GetTicks();
    ex_themes_set_high_contrast(t, hc);
    ex_themes_set_inputs(t, scheme, 0, theme, accent, snippets);
    while (SDL_GetTicks() - t0 < 10000) { if (ex_themes_take(t, out, NULL)) return 1; SDL_Delay(5); }
    return 0;
}

static void test_high_contrast(void) {
    ExThemes *t;
    ExTheme off, hc, hcl, builtin_hc, builtin_hcl, builtin_dark;
    const char *rose = "[theme]\nname = Rose\nbase = light\n[colors]\nbackground = #ffe0e6\naccent = #123456\n";
    size_t n;
    ex_platform_remove_tree("shell_ui_test");
    ex_platform_make_dirs(V "/themes");
    ex_platform_write_file_atomic(V "/themes/rose.theme", rose, strlen(rose));
    CHECK(ex_theme_init_builtin(&builtin_hc, EX_THEME_HC_DARK) == 0 && ex_theme_init_builtin(&builtin_hcl, EX_THEME_HC_LIGHT) == 0 && ex_theme_init_builtin(&builtin_dark, EX_THEME_DARK) == 0);
    t = ex_themes_create(V, G);
    CHECK(t != NULL);
    if (!t) return;
    /* off: the theme file and the accent colour apply */
    CHECK(build_with(t, 0, "dark", "Rose", "#abcdef", "", &off));
    CHECK(off.colors[EX_COLOR_BACKGROUND].r == 0xff && off.colors[EX_COLOR_BACKGROUND].g == 0xe0 && off.colors[EX_COLOR_ACCENT].r == 0xab);
    /* on: the built-in high contrast theme of the scheme, nothing from the file or the accent */
    CHECK(build_with(t, 1, "dark", "Rose", "#abcdef", "", &hc));
    CHECK(hc.is_dark && !memcmp(&hc.colors[EX_COLOR_BACKGROUND], &builtin_hc.colors[EX_COLOR_BACKGROUND], sizeof(ExColor)) && !memcmp(&hc.colors[EX_COLOR_TEXT], &builtin_hc.colors[EX_COLOR_TEXT], sizeof(ExColor)));
    CHECK(!memcmp(&hc.colors[EX_COLOR_ACCENT], &builtin_hc.colors[EX_COLOR_ACCENT], sizeof(ExColor)));
    CHECK(hc.colors[EX_COLOR_BACKGROUND].r == 0 && hc.colors[EX_COLOR_BACKGROUND].g == 0 && hc.colors[EX_COLOR_BACKGROUND].b == 0 && hc.colors[EX_COLOR_TEXT].r == 255);
    CHECK(memcmp(&hc.colors[EX_COLOR_BACKGROUND], &builtin_dark.colors[EX_COLOR_BACKGROUND], sizeof(ExColor)) != 0 || memcmp(&hc.colors[EX_COLOR_TEXT], &builtin_dark.colors[EX_COLOR_TEXT], sizeof(ExColor)) != 0);
    {   char warn[8][160]; n = ex_theme_contrast_warnings(&hc, warn, 8); CHECK(n == 0); }              /* the high contrast theme passes the contrast check */
    /* the light scheme gives the light one */
    CHECK(build_with(t, 1, "light", "", "", "", &hcl));
    CHECK(!hcl.is_dark && !memcmp(&hcl.colors[EX_COLOR_BACKGROUND], &builtin_hcl.colors[EX_COLOR_BACKGROUND], sizeof(ExColor)) && hcl.colors[EX_COLOR_BACKGROUND].r == 255);
    /* turned off again, the theme file is back */
    {   ExTheme again;
        CHECK(build_with(t, 0, "dark", "Rose", "#abcdef", "", &again));
        CHECK(again.colors[EX_COLOR_BACKGROUND].r == 0xff && again.colors[EX_COLOR_ACCENT].r == 0xab);
        ex_theme_free(&again); }
    ex_theme_free(&off); ex_theme_free(&hc); ex_theme_free(&hcl);
    ex_theme_free(&builtin_hc); ex_theme_free(&builtin_hcl); ex_theme_free(&builtin_dark);
    ex_themes_free(t);
}

int main(void) {
    test_zoom_and_size();
    test_ribbon_list();
    test_ribbon_and_status();
    test_high_contrast();
    ex_platform_remove_tree("shell_ui_test");
    if (failures) { fprintf(stderr, "shell_ui: %d failed\n", failures); return 1; }
    printf("shell_ui: ok\n");
    return 0;
}
