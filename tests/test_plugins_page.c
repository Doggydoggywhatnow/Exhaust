/* plugins_page (sub-slice 8b): the Plugins page of the settings. A real runtime on a temporary vault with two plugins; the page lists them,
 * asks before it turns one on (the permissions are in the question), does nothing on Cancel, turns it on on Turn on, shows the state the runtime
 * reports and turns it off again; a plugin with a broken manifest is listed with the reason; the master switch stops everything.
 * Usage: test_plugins_page */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_plugins.h"
#include "exhaust_settings.h"
#include "exhaust_settingsview.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *V = "plugins_page_test";
static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT;
static ExSettingsView *SV;
static ExSettings *S;
static ExPlugins *P;

static void write_str(const char *rel, const char *s) {
    char path[600], d[600], *sl;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    snprintf(d, sizeof d, "%s", path);
    sl = strrchr(d, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(d); }
    f = fopen(path, "wb");
    if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
}

static int ASKS; static char ASK_TITLE[200], ASK_MESSAGE[800], ASK_YES[40];
static void (*ASK_DONE)(void *, int); static void *ASK_ARG;
static void host_confirm(void *u, const char *title, const char *message, const char *yes, void (*done)(void *arg, int yes), void *arg) {
    (void)u;
    ASKS++; snprintf(ASK_TITLE, sizeof ASK_TITLE, "%s", title); snprintf(ASK_MESSAGE, sizeof ASK_MESSAGE, "%s", message); snprintf(ASK_YES, sizeof ASK_YES, "%s", yes);
    ASK_DONE = done; ASK_ARG = arg;
}
static void host_choose(void *u, const char *const *l, size_t n, size_t c, float x, float y, void (*f)(void *, size_t), void *a) { (void)u; (void)l; (void)n; (void)c; (void)x; (void)y; (void)f; (void)a; }

static void frame(void) {
    ex_ui_layout(UI, ROOT, 1400.0f, 3000.0f);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    ex_ui_layout(UI, ROOT, 1400.0f, 3000.0f);
}
static void click(ExWidget *w) {
    float cx = w->x + w->w / 2.0f, cy = w->y + w->h / 2.0f;
    ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, cx, cy, 0, 1);
    ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, cx, cy, 0, 1);
    frame();
}
static ExWidget *find_button(ExWidget *w, const char *name) {
    ExWidget *c, *f;
    if (!w) return NULL;
    if (w->type == EX_WIDGET_BUTTON && w->name && !strcmp(w->name, name)) return w;
    for (c = w->first_child; c; c = c->next) if ((f = find_button(c, name)) != NULL) return f;
    return NULL;
}
static int has_label(ExWidget *w, const char *needle) {
    ExWidget *c;
    if (!w) return 0;
    if (w->type == EX_WIDGET_LABEL && w->text && strstr(w->text, needle)) return 1;
    for (c = w->first_child; c; c = c->next) if (has_label(c, needle)) return 1;
    return 0;
}
/* Run the runtime and the page until the state is what a test wants (the plugin thread is separate). */
static int until(int (*ok)(void)) {
    int i;
    for (i = 0; i < 400; i++) {
        ex_plugins_update(P);
        ex_settingsview_tick(SV);
        frame();
        if (!ex_plugins_busy(P) && ok()) return 1;
        SDL_Delay(5);                                       /* a test may wait: tools/check_ui_blocking.sh looks at src/ui only */
    }
    return 0;
}
static int status_is(const char *id, const char *prefix) {
    ExPluginInfo all[16];
    size_t n = ex_plugins_snapshot(P, all, 16), i;
    for (i = 0; i < n; i++) if (!strcmp(all[i].id, id)) return !strncmp(all[i].status, prefix, strlen(prefix));
    return 0;
}
static int ok_off(void) { return status_is("hello", "Off"); }
static int ok_running(void) { return status_is("hello", "Running") && find_button(ex_settingsview_widget(SV), "Turn off: Hello") != NULL; }
static int has_label(ExWidget *w, const char *needle);
static int ok_waiting(void) { return status_is("hello", "On, waiting") && has_label(ex_settingsview_widget(SV), "plugins are not allowed"); }
static int ok_stopped(void) { return status_is("hello", "Off") && find_button(ex_settingsview_widget(SV), "Turn on: Hello") != NULL; }
static int ok_listed(void) { return find_button(ex_settingsview_widget(SV), "Turn on: Hello") != NULL; }

int main(void) {
    ExTheme theme;
    ExSettingsHost h;
    ExPluginHost ph;
    ExWidget *page, *b;
    char vcfg[256];
    ex_platform_remove_tree(V);
    snprintf(vcfg, sizeof vcfg, "%s/.exhaust", V);
    write_str(".exhaust/plugins/hello/manifest.ini", "[plugin]\nid = hello\nname = Hello\nversion = 1.2.0\napi = 1\nauthor = Ada\ndescription = Says hello\npermissions = ui.notice, vault.read\n");
    write_str(".exhaust/plugins/hello/main.lua", "-- nothing to do\n");
    write_str(".exhaust/plugins/broken/manifest.ini", "[plugin]\nid = broken\nname = Broken\nversion = 1\napi = 1\n");
    write_str(".exhaust/plugins/broken/main.lua", "");

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("plugins page", 800, 600);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    S = ex_settings_open(vcfg, NULL);
    memset(&ph, 0, sizeof ph);
    P = ex_plugins_create(V, NULL, &ph, NULL, NULL);
    CHECK(P != NULL);
    memset(&h, 0, sizeof h);
    h.choose = host_choose; h.plugins = P; h.confirm = host_confirm;
    SV = ex_settingsview_create(UI, S, &h);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    ex_widget_add(ROOT, ex_settingsview_widget(SV));
    frame();
    CHECK(ex_settingsview_show(SV, "plugins") == 0);
    page = ex_settingsview_widget(SV);
    ex_plugins_rescan(P);
    CHECK(until(ok_listed));

    /* the master switch is a row; the plugins are cards under it */
    CHECK(ex_settingsview_control(SV, "plugins", "allow") != NULL);
    CHECK(has_label(page, "Hello  1.2.0") && has_label(page, "Permissions: ui.notice, vault.read") && has_label(page, "Ada"));
    CHECK(has_label(page, "broken"));                                    /* the folder with an unusable manifest: its name, and the reason */
    CHECK(find_button(page, "Turn on: broken") == NULL);

    /* turning on asks, with the permissions in the question; Cancel changes nothing */
    CHECK(ex_settings_get_bool(S, "plugins", "allow") == 0);
    ex_settings_set_bool(S, "plugins", "allow", 1);
    ex_plugins_set_allowed(P, 1);
    b = find_button(page, "Turn on: Hello");
    CHECK(b != NULL);
    if (b) click(b);
    CHECK(ASKS == 1 && strstr(ASK_TITLE, "Hello") && strstr(ASK_MESSAGE, "ui.notice, vault.read") && !strcmp(ASK_YES, "Turn on"));
    if (ASK_DONE) ASK_DONE(ASK_ARG, 0);
    ASK_DONE = NULL;
    CHECK(until(ok_off));
    CHECK(find_button(ex_settingsview_widget(SV), "Turn on: Hello") != NULL);

    /* yes: it runs, and the card says so */
    b = find_button(ex_settingsview_widget(SV), "Turn on: Hello");
    if (b) click(b);
    CHECK(ASKS == 2);
    if (ASK_DONE) ASK_DONE(ASK_ARG, 1);
    ASK_DONE = NULL;
    CHECK(until(ok_running));
    CHECK(has_label(ex_settingsview_widget(SV), "Running"));

    /* the master switch off: the plugin waits, and the card says why */
    ex_plugins_set_allowed(P, 0);
    CHECK(until(ok_waiting));
    CHECK(has_label(ex_settingsview_widget(SV), "plugins are not allowed"));
    ex_plugins_set_allowed(P, 1);
    CHECK(until(ok_running));

    /* Turn off needs no question */
    b = find_button(ex_settingsview_widget(SV), "Turn off: Hello");
    CHECK(b != NULL);
    if (b) click(b);
    CHECK(ASKS == 2);
    CHECK(until(ok_stopped));

    ex_plugins_free(P);
    ex_settingsview_widget(SV);
    ex_settings_close(S);
    ex_platform_remove_tree(V);
    if (failures) { fprintf(stderr, "plugins_page: %d failures\n", failures); return 1; }
    printf("plugins_page ok\n");
    return 0;
}
