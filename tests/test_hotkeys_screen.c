/* hotkeys_screen (sub-slice 7c): the Hotkeys page of the settings screen: the rows of the commands with their shortcuts as chips, adding a
 * shortcut (the + button waits for a key press, the application hands it back), removing one, the conflict colour, reset, the filter, the
 * "customized only" switch, and that only the rows near the view have widgets (the list has 200 rows). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_commands.h"
#include "exhaust_hotkeys.h"
#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "exhaust_settingsview.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "hotkeys_screen_test"
#define W 900
#define H 700

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT, *SCROLL;
static ExSettingsView *SV;
static ExHotkeys *HK;
static void (*CAP_DONE)(void *, const char *);
static void *CAP_ARG;
static int CAP_CALLS;

static void host_capture(void *u, void (*done)(void *, const char *), void *arg) { (void)u; CAP_DONE = done; CAP_ARG = arg; CAP_CALLS++; }

static void frame(void) {
    ex_ui_layout(UI, ROOT, (float)W, (float)H);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    ex_ui_layout(UI, ROOT, (float)W, (float)H);
}
static void frames(int n) { while (n-- > 0) frame(); }

static void click(ExWidget *w) {
    float cx = w->x + w->w / 2.0f, cy = w->y + w->h / 2.0f;
    ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, cx, cy, 0, 1);
    ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, cx, cy, 0, 1);
    frames(3);
}

static ExWidget *named(const char *name) { return ex_widget_find_name(ROOT, name); }
static size_t count_rows(ExWidget *w) {                       /* the row boxes: a box with the name of a command */
    size_t n = 0, i;
    for (; w; w = w->next) {
        for (i = 0; i < ex_command_table_count; i++) if (w->name && w->type == EX_WIDGET_BOX && !strcmp(w->name, ex_command_table[i].name)) { n++; break; }
        n += count_rows(w->first_child);
    }
    return n;
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
static void filter(const char *text) {
    ExWidget *ed = editor_named(ROOT, "Filter the commands");
    if (!ed) { fprintf(stderr, "no filter field\n"); failures++; return; }
    ex_ui_set_focus(UI, ed);
    ex_ui_key(UI, EX_KEY_A, EX_MOD_PRIMARY);                      /* select all, then replace */
    ex_ui_key(UI, EX_KEY_BACKSPACE, 0);
    if (text[0]) ex_ui_text_input(UI, text, strlen(text));
    frames(3);
}

int main(void) {
    ExTheme theme;
    ExSettings *S;
    ExSettingsHost h;
    ExWidget *w;
    char got[8][48];
    size_t i, built;

    ex_platform_remove_tree(DIR);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("hotkeys screen", W, H);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    S = ex_settings_open(DIR "/.exhaust", NULL);
    HK = ex_hotkeys_open(DIR "/.exhaust", 0);
    memset(&h, 0, sizeof h);
    h.hotkeys = HK; h.capture = host_capture;
    SV = ex_settingsview_create(UI, S, &h);
    CHECK(SV != NULL);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    ex_widget_add(ROOT, ex_settingsview_widget(SV));
    frames(2);
    for (w = ex_settingsview_widget(SV)->first_child; w; w = w->next) if (w->scrollable && w->name && !strcmp(w->name, "Settings page")) SCROLL = w;
    CHECK(SCROLL != NULL);
    CHECK(ex_settingsview_show(SV, "hotkeys") == 0);
    frames(3);

    /* the list is long and only the rows near the view are built; scrolling builds the ones that come into view */
    built = count_rows(ROOT);
    printf("hotkeys_screen: %zu rows built of %zu commands\n", built, ex_command_table_count);
    CHECK(built > 5 && built < ex_command_table_count / 2);
    CHECK(named("Add alias") != NULL && named("Toggle numbered list") == NULL);
    ex_ui_scroll(UI, SCROLL, 1000000.0f);
    frames(4);
    CHECK(named("Add alias") == NULL && named("Toggle numbered list") != NULL);       /* the last rows came in, the first went */
    CHECK(count_rows(ROOT) < ex_command_table_count / 2);
    ex_ui_scroll(UI, SCROLL, -1000000.0f);
    frames(4);
    CHECK(named("Add alias") != NULL);

    /* the filter: by name, by id and by a shortcut; the shortcuts are the defaults of the table */
    filter("open settings");
    CHECK(named("Open settings") != NULL && count_rows(ROOT) < 5);
    CHECK(named("Remove the shortcut Ctrl+, of Open settings") != NULL);
    filter("ctrl+,");
    CHECK(named("Open settings") != NULL);                       /* the same by its shortcut */
    filter("open settings");

    /* adding: the + button waits for a key press and the application hands it back */
    w = named("Add a shortcut to Open settings");
    CHECK(w != NULL);
    CAP_CALLS = 0;
    if (w) click(w);
    CHECK(CAP_CALLS == 1 && CAP_DONE != NULL);
    CHECK(w && w->text && strstr(w->text, "Press keys"));
    CAP_DONE(CAP_ARG, "Ctrl+Alt+S");
    frames(3);
    CHECK(ex_hotkeys_get(HK, "app.open-settings", got, 8) == 2);
    CHECK(named("Remove the shortcut Ctrl+Alt+S of Open settings") != NULL);
    CHECK(named("Restore the default shortcuts of Open settings") != NULL);       /* it is a change now */
    /* a captured Escape (NULL) changes nothing */
    w = named("Add a shortcut to Open settings");
    if (w) click(w);
    CAP_DONE(CAP_ARG, NULL);
    frames(3);
    CHECK(ex_hotkeys_get(HK, "app.open-settings", got, 8) == 2);
    CHECK(named("Add a shortcut to Open settings") && named("Add a shortcut to Open settings")->text && !strstr(named("Add a shortcut to Open settings")->text, "Press"));

    /* a conflict: both commands show it */
    w = named("Add a shortcut to Open settings");
    if (w) click(w);
    CAP_DONE(CAP_ARG, "Ctrl+N");                                  /* Create new note's default */
    frames(3);
    w = named("Remove the shortcut Ctrl+N of Open settings; it conflicts with Create new note");
    CHECK(w != NULL && w->bg_token == EX_COLOR_ERROR);
    filter("create new note");
    w = named("Remove the shortcut Ctrl+N of Create new note; it conflicts with Open settings");
    CHECK(w != NULL && w->bg_token == EX_COLOR_ERROR);
    filter("open settings");

    /* removing and resetting */
    w = named("Remove the shortcut Ctrl+N of Open settings; it conflicts with Create new note");
    if (w) click(w);
    CHECK(named("Remove the shortcut Ctrl+N of Open settings; it conflicts with Create new note") == NULL);
    w = named("Remove the shortcut Ctrl+, of Open settings");
    if (w) click(w);
    CHECK(ex_hotkeys_get(HK, "app.open-settings", got, 8) == 1 && !strcmp(got[0], "Ctrl+Alt+S"));
    w = named("Restore the default shortcuts of Open settings");
    if (w) click(w);
    CHECK(ex_hotkeys_get(HK, "app.open-settings", got, 8) == 1 && !strcmp(got[0], "Ctrl+,"));
    CHECK(named("Restore the default shortcuts of Open settings") == NULL);

    /* customized only */
    w = named("Add a shortcut to Open settings");
    if (w) click(w);
    CAP_DONE(CAP_ARG, "Ctrl+Alt+S");
    frames(3);
    filter("");
    w = named("Customized only: off");
    CHECK(w != NULL);
    if (w) click(w);
    CHECK(named("Customized only: on") != NULL);
    CHECK(named("Open settings") != NULL && named("Add alias") == NULL && count_rows(ROOT) == 1);
    w = named("Customized only: on");
    if (w) click(w);
    CHECK(named("Add alias") != NULL);

    /* the change is in the file */
    ex_hotkeys_flush(HK);
    {
        size_t n;
        char *t = ex_platform_read_file(DIR "/.exhaust/hotkeys.ini", &n);
        CHECK(t != NULL && strstr(t, "app.open-settings = Ctrl+,, Ctrl+Alt+S") != NULL);
        free(t);
    }
    (void)i;
    ex_widget_free(ROOT);
    ex_ui_free(UI); ex_theme_free(&theme); ex_renderer_destroy(R);
    ex_hotkeys_close(HK);
    ex_settings_close(S);
    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "hotkeys_screen: %d failure(s)\n", failures); return 1; }
    printf("hotkeys_screen: ok\n");
    return 0;
}
