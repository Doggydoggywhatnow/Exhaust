/* theme_editor (sub-slice 7d): the Theme page of the settings screen with a real theme worker: a token typed into a field is written to the user's own
 * theme file (the built-in theme is never touched), the theme in use becomes that file, the screen shows the new value and swatch once the worker has built
 * it, a refused value is put back, the contrast warning appears and goes, a theme is chosen from the menu, and a snippet is switched on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "exhaust_settingsview.h"
#include "exhaust_themes.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "theme_editor_test"
#define W 900
#define H 3000

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT;
static ExSettingsView *SV;
static ExSettings *S;
static ExThemes *TH;
static ExTheme CUR;
static void (*CHOSEN)(void *, size_t);
static void *CHOSEN_ARG;
static size_t CHOICES;

static const ExTheme *current(void *u) { (void)u; return &CUR; }
static void choose(void *u, const char *const *labels, size_t n, size_t cur, float x, float y, void (*chosen)(void *, size_t), void *arg) { (void)u; (void)labels; (void)cur; (void)x; (void)y; CHOSEN = chosen; CHOSEN_ARG = arg; CHOICES = n; }

/* what the application does: hand the worker the settings, take a newer theme */
static void sync_theme(void) {
    ExTheme next;
    ex_themes_set_inputs(TH, ex_settings_get(S, "appearance", "color_scheme"), 0, ex_settings_get(S, "appearance", "theme"), ex_settings_get(S, "appearance", "accent_color"), ex_settings_get(S, "appearance", "snippets_enabled"));
    if (ex_themes_take(TH, &next, NULL)) { ex_theme_free(&CUR); CUR = next; }
}
static void changed(void *u, const char *s, const char *k) { (void)u; (void)s; (void)k; sync_theme(); }

static void frame(void) {
    sync_theme();
    ex_ui_layout(UI, ROOT, (float)W, (float)H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    ex_ui_layout(UI, ROOT, (float)W, (float)H);
}
static void frames_until(int (*done)(void), int ms) {
    Uint64 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < (Uint64)ms) { frame(); if (done && done()) return; SDL_Delay(10); }
}

static ExWidget *editor_named(ExWidget *w, const char *name) {
    for (; w; w = w->next) {
        ExWidget *hit;
        if (w->type == EX_WIDGET_EDITOR && w->name && !strcmp(w->name, name)) return w;
        hit = editor_named(w->first_child, name);
        if (hit) return hit;
    }
    return NULL;
}

static void type_into(const char *name, const char *text) {
    ExWidget *ed = editor_named(ROOT, name);
    CHECK(ed != NULL);
    if (!ed) return;
    ex_ui_set_focus(UI, ed);
    ex_ui_key(UI, EX_KEY_A, EX_MOD_PRIMARY);
    ex_ui_key(UI, EX_KEY_BACKSPACE, 0);
    if (text[0]) ex_ui_text_input(UI, text, strlen(text));
    ex_ui_key(UI, EX_KEY_ENTER, 0);
    frame();
}
static int bg_is(unsigned r, unsigned g, unsigned b) { ExColor c = CUR.colors[EX_COLOR_BACKGROUND]; return c.r == r && c.g == g && c.b == b; }
static int bg_is_101010(void) { return bg_is(0x10, 0x10, 0x10); }
static int has_warning_text(void) { ExWidget *w = ex_widget_find_name(ROOT, "Theme in use"); (void)w; char warn[4][160]; return ex_theme_contrast_warnings(&CUR, warn, 4) > 0; }

int main(void) {
    ExTheme theme0;
    ExSettingsHost h;
    char *text;
    size_t n;
    ex_platform_remove_tree(DIR);
    ex_platform_make_dirs(DIR "/vault/.exhaust/snippets");
    { const char *sn = "@tokens {\n  colors.link: #00ff00;\n}\n"; ex_platform_write_file_atomic(DIR "/vault/.exhaust/snippets/loud.style", sn, strlen(sn)); }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme0, EX_THEME_DARK) == 0);
    R = ex_renderer_create("theme editor", W, 600);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme0, 16.0f);
    S = ex_settings_open(DIR "/vault/.exhaust", NULL);
    ex_settings_set(S, "appearance", "color_scheme", "dark");
    TH = ex_themes_create(DIR "/vault/.exhaust", NULL);
    CUR = theme0;                                                  /* (not freed: built-in tables are plain) */
    memset(&CUR.fonts, 0, sizeof CUR.fonts);
    memset(&h, 0, sizeof h);
    h.themes = TH; h.current_theme = current; h.choose = choose; h.changed = changed;
    SV = ex_settingsview_create(UI, S, &h);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    ex_widget_add(ROOT, ex_settingsview_widget(SV));
    CHECK(ex_settingsview_show(SV, "theme") == 0);
    frames_until(NULL, 600);

    /* a token typed into a field: the built-in theme is left alone, Custom is made and chosen, the screen follows the worker */
    CHECK(!bg_is_101010());
    type_into("background", "#101010");
    frames_until(bg_is_101010, 5000);
    CHECK(bg_is_101010());
    CHECK(!strcmp(ex_settings_get(S, "appearance", "theme"), "Custom"));
    text = ex_platform_read_file(DIR "/vault/.exhaust/themes/custom.theme", &n);
    CHECK(text && strstr(text, "name = Custom") && strstr(text, "background = #101010"));
    free(text);
    {   /* the field shows the value, and the swatch is that colour */
        ExWidget *ed = editor_named(ROOT, "background");
        ExWidget *row = ed ? ed->parent : NULL, *left = row ? row->first_child : NULL, *sw = left ? left->first_child : NULL;
        CHECK(sw && sw->bg_custom && sw->bg_color.r == 0x10 && sw->bg_color.g == 0x10 && sw->bg_color.b == 0x10);
    }
    /* a value the token refuses is put back */
    type_into("background", "not a colour");
    frames_until(NULL, 300);
    text = ex_platform_read_file(DIR "/vault/.exhaust/themes/custom.theme", &n);
    CHECK(text && strstr(text, "background = #101010") && !strstr(text, "not a colour"));
    free(text);
    CHECK(bg_is_101010());

    /* the contrast warning: text the colour of the background */
    CHECK(!has_warning_text());
    type_into("text", "#101018");
    frames_until(has_warning_text, 5000);
    CHECK(has_warning_text());
    type_into("text", "#f0f0f0");
    frames_until(NULL, 1200);
    CHECK(!has_warning_text());

    /* a size and a font */
    type_into("radius", "2");
    frames_until(NULL, 800);
    CHECK(CUR.sizes[EX_SIZE_RADIUS] == 2);
    type_into("text font", "Some Font");
    frames_until(NULL, 800);
    CHECK(CUR.fonts[EX_FONT_TOKEN_TEXT] && !strcmp(CUR.fonts[EX_FONT_TOKEN_TEXT], "Some Font"));
    type_into("radius", "-3");                                     /* refused: stays 2 */
    frames_until(NULL, 400);
    CHECK(CUR.sizes[EX_SIZE_RADIUS] == 2);

    /* choosing a theme from the menu: the built-in one is the first; choosing it sets `theme` to exhaust */
    {
        ExWidget *b = ex_widget_find_name(ROOT, "Choose the theme");
        float cx, cy;
        CHECK(b != NULL && b->text && !strcmp(b->text, "Custom"));
        if (b) {
            cx = b->x + b->w / 2.0f; cy = b->y + b->h / 2.0f;
            CHOSEN = NULL;
            ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, cx, cy, 0, 1);
            ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, cx, cy, 0, 1);
            CHECK(CHOSEN != NULL && CHOICES >= 2);
            if (CHOSEN) { CHOSEN(CHOSEN_ARG, 0); CHECK(!strcmp(ex_settings_get(S, "appearance", "theme"), "exhaust")); CHOSEN(CHOSEN_ARG, 1); CHECK(!strcmp(ex_settings_get(S, "appearance", "theme"), "Custom")); }
        }
        frames_until(NULL, 800);
        CHECK(CUR.sizes[EX_SIZE_RADIUS] == 2);
    }

    /* a snippet: found by the worker, switched on from the page */
    {
        Uint64 t0 = SDL_GetTicks();
        ExWidget *sn = NULL;
        while (SDL_GetTicks() - t0 < 5000 && !sn) {                  /* the folders are listed every two seconds: the page is built again to show the snippet */
            ex_settingsview_show(SV, "theme");
            frame();
            sn = ex_widget_find_name(ROOT, "Snippet loud");
            if (!sn) SDL_Delay(100);
        }
        CHECK(sn != NULL);
        if (sn) {
            float cx = sn->x + sn->w / 2.0f, cy = sn->y + sn->h / 2.0f;
            ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, cx, cy, 0, 1);
            ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, cx, cy, 0, 1);
            CHECK(!strcmp(ex_settings_get(S, "appearance", "snippets_enabled"), "loud"));
            frames_until(NULL, 1500);
            CHECK(CUR.colors[EX_COLOR_LINK].g == 0xff && CUR.colors[EX_COLOR_LINK].r == 0);
        }
    }

    ex_settingsview_widget(SV);
    ex_widget_free(ROOT);
    ex_themes_free(TH);
    ex_settings_close(S);
    ex_ui_free(UI); ex_theme_free(&theme0); ex_renderer_destroy(R);
    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "theme_editor: %d failure(s)\n", failures); return 1; }
    printf("theme_editor: ok\n");
    return 0;
}
