/* stubs_disabled (sub-slice 8c): the features that are not in 0.1.0 exist where the README places them, disabled and labelled, and do nothing.
 *  - Vim key bindings (Editor), Language and Community themes (Appearance), Community plugins and Web clipper (Plugins), Sync, Shared vaults and
 *    Publish (Sync), the switches Sync and Publish (Core plugins), Command line and Program icon: each control is disabled, its row says it is not in
 *    this version, a click calls no callback of the host and changes no setting; a stub switch cannot be turned on through the settings API either.
 *  - Every row of kind information or stub on any page is held to the same rule, so a new stub cannot be added unlabelled.
 *  - No network: the source tree has no call that opens a connection (a scan of src/ and include/ for the system and library calls).
 *  - extras/clipper/ exists and says it is a stub.
 * Usage: test_stubs SOURCE_DIR */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "exhaust_settingsview.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT;
static ExSettingsView *SV;
static ExSettings *S;
static int calls;                                                  /* every callback of the host */
static int LW = 1400, LH = 6000;

static void host_choose(void *u, const char *const *l, size_t n, size_t c, float x, float y, void (*f)(void *, size_t), void *a) { (void)u; (void)l; (void)n; (void)c; (void)x; (void)y; (void)f; (void)a; calls++; }
static void host_changed(void *u, const char *s, const char *k) { (void)u; (void)s; (void)k; calls++; }
static int host_feature(void *u, const char *k, int e) { (void)u; (void)k; (void)e; calls++; return 0; }
static void host_action(void *u, const char *id) { (void)u; (void)id; calls++; }

static void frame(void) {
    ex_ui_layout(UI, ROOT, (float)LW, (float)LH);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    ex_ui_layout(UI, ROOT, (float)LW, (float)LH);
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
static int has_label(ExWidget *w, const char *text) {
    ExWidget *c;
    if (!w) return 0;
    if (w->type == EX_WIDGET_LABEL && w->text && !strcmp(w->text, text)) return 1;
    for (c = w->first_child; c; c = c->next) if (has_label(c, text)) return 1;
    return 0;
}
static int icontains(const char *h, const char *n) {
    size_t i, l = strlen(n);
    for (; *h; h++) { for (i = 0; i < l && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i]); i++) ; if (i == l) return 1; }
    return 0;
}
/* The words that tell a person it is not there yet. */
static int says_not_available(const char *text) { return icontains(text, "not in this version") || icontains(text, "(later)") || icontains(text, "not available yet"); }

static int row_is_stub(const ExSettingsRow *r) {
    const ExSettingDef *d = r->key ? ex_settings_def(r->section, r->key) : NULL;
    if (r->kind == EX_ROW_STUB) return 1;
    if (r->kind == EX_ROW_INFO) return strcmp(r->name, "Version") != 0;     /* the version row's button is a planned manual check, not a feature of the README */
    return d && d->stub;
}

/* One stub row: shown, disabled, labelled, and a click does nothing. */
static void check_row(const ExSettingsRow *r) {
    ExWidget *ctl;
    char name[300];
    int before = calls;
    CHECK(ex_settingsview_show(SV, r->page) == 0);
    frame();
    CHECK(has_label(ex_settingsview_widget(SV), r->name));
    CHECK(says_not_available(r->desc) || (r->key && ex_settings_def(r->section, r->key) && ex_settings_def(r->section, r->key)->stub));
    if (r->kind == EX_ROW_INFO) snprintf(name, sizeof name, "%s", r->choices ? r->choices : "");
    else snprintf(name, sizeof name, "%s: not in this version", r->name);
    ctl = find_button(ex_settingsview_widget(SV), name);
    if (!ctl) { fprintf(stderr, "no control named \"%s\" on page %s\n", name, r->page); failures++; return; }
    CHECK(ctl->disabled);
    click(ctl);
    ex_ui_key(UI, EX_KEY_ENTER, 0);
    frame();
    CHECK(calls == before);                                            /* no callback: nothing was asked of the application */
    if (r->key) CHECK(!ex_settings_get_bool(S, r->section, r->key));     /* and the setting is still off */
}

static const struct { const char *page, *name; } WANTED[] = {
    { "editor", "Vim key bindings" }, { "appearance", "Language" }, { "appearance", "Community themes" }, { "plugins", "Community plugins" }, { "plugins", "Web clipper" },
    { "sync", "Sync" }, { "sync", "Shared vaults" }, { "sync", "Publish" }, { "general", "Command line" }, { "appearance", "Program icon" }, { "keychain", "Saved secrets" },
};

static int contains_call(const char *text) {
    static const char *const bad[] = { "socket(", "getaddrinfo", "gethostbyname", "WSAStartup", "WinHttp", "InternetOpen", "NSURLSession", "curl_easy", "SDL_net", "connect(" };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof *bad; i++) {
        const char *p = strstr(text, bad[i]);
        while (p) {
            if (strcmp(bad[i], "connect(") != 0 || p == text || !(isalnum((unsigned char)p[-1]) || p[-1] == '_')) return 1;     /* a bare connect( call, not sqlite3_connect( or the like */
            p = strstr(p + 1, bad[i]);
        }
    }
    return 0;
}
static int SCANNED;
static SDL_EnumerationResult scan_cb(void *user, const char *dirname, const char *fname) {
    char path[1500];
    SDL_PathInfo info;
    size_t l;
    (void)user;
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) { char sub[1600]; snprintf(sub, sizeof sub, "%s/", path); SDL_EnumerateDirectory(sub, scan_cb, NULL); return SDL_ENUM_CONTINUE; }
    l = strlen(path);
    if (l > 2 && (!strcmp(path + l - 2, ".c") || !strcmp(path + l - 2, ".h") || !strcmp(path + l - 2, ".m"))) {
        size_t n = 0;
        char *text = ex_platform_read_file(path, &n);
        SCANNED++;
        if (text && contains_call(text)) { fprintf(stderr, "a network call in %s\n", path); failures++; }
        free(text);
    }
    return SDL_ENUM_CONTINUE;
}
static void scan_tree(const char *root) {
    char dir[1300];
    snprintf(dir, sizeof dir, "%s/src/", root); SDL_EnumerateDirectory(dir, scan_cb, NULL);
    snprintf(dir, sizeof dir, "%s/include/", root); SDL_EnumerateDirectory(dir, scan_cb, NULL);
    CHECK(SCANNED > 100);                                              /* the scan really looked at the sources */
}

int main(int argc, char **argv) {
    ExTheme theme;
    ExSettingsHost h;
    size_t i, k, found = 0;
    char *t;
    char path[1200];
    if (argc < 2) { fprintf(stderr, "usage: test_stubs SOURCE_DIR\n"); return 2; }
    ex_platform_remove_tree("stubs_test");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("stubs", 800, 600);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    S = ex_settings_open("stubs_test/.exhaust", NULL);
    CHECK(S != NULL);
    memset(&h, 0, sizeof h);
    h.choose = host_choose; h.changed = host_changed; h.set_feature = host_feature; h.action = host_action;
    SV = ex_settingsview_create(UI, S, &h);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    ex_widget_add(ROOT, ex_settingsview_widget(SV));
    frame();

    /* what the README places must be on a page */
    for (k = 0; k < sizeof WANTED / sizeof *WANTED; k++) {
        int ok = 0;
        for (i = 0; i < ex_settings_row_count(); i++) {
            const ExSettingsRow *r = ex_settings_row(i);
            if (!strcmp(r->page, WANTED[k].page) && !strcmp(r->name, WANTED[k].name)) ok = 1;
        }
        if (!ok) { fprintf(stderr, "no row \"%s\" on page %s\n", WANTED[k].name, WANTED[k].page); failures++; }
    }
    /* every stub row of every page */
    for (i = 0; i < ex_settings_row_count(); i++) {
        const ExSettingsRow *r = ex_settings_row(i);
        if (!row_is_stub(r)) continue;
        if (!strcmp(r->page, "features")) continue;                    /* the switches: below */
        check_row(r);
        found++;
    }
    CHECK(found >= 11);

    /* the switches of the Core plugins list: Sync and Publish (and the audio recorder) cannot be turned on, in the view or through the settings API */
    {
        static const char *const keys[] = { "sync", "publish" };
        for (k = 0; k < 2; k++) {
            ExWidget *c;
            CHECK(ex_settingsview_show(SV, "features") == 0);
            frame();
            c = ex_settingsview_control(SV, "features", keys[k]);
            CHECK(c != NULL && c->disabled && c->name && strstr(c->name, "not in this version"));
            if (c) { int before = calls; click(c); CHECK(calls == before); }
            CHECK(ex_settings_set_bool(S, "features", keys[k], 1) == -1);
            CHECK(!ex_settings_get_bool(S, "features", keys[k]));
        }
    }
    /* Vim mode: the key exists, is a stub, and cannot be turned on */
    CHECK(ex_settings_def("editor", "vim_mode") && ex_settings_def("editor", "vim_mode")->stub);
    CHECK(ex_settings_set_bool(S, "editor", "vim_mode", 1) == -1 && !ex_settings_get_bool(S, "editor", "vim_mode"));

    /* the web clipper's folder says it is a stub */
    snprintf(path, sizeof path, "%s/extras/clipper/README.md", argv[1]);
    t = ex_platform_read_file(path, NULL);
    CHECK(t && icontains(t, "stub") && icontains(t, "not built"));
    free(t);

    /* the program makes no network call */
    scan_tree(argv[1]);

    ex_settings_close(S);
    ex_platform_remove_tree("stubs_test");
    if (failures) { fprintf(stderr, "stubs_disabled: %d failures\n", failures); return 1; }
    printf("stubs_disabled ok (%zu rows)\n", found);
    return 0;
}
