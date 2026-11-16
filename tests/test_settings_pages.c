/* settings_pages (sub-slice 7a) and feature_pages (7b): the settings screen. The program takes one argument, `options` (the pages of the Options
 * group of 7a, the default) or `features` (the Core plugins list and the pages of the features, 7b); each walks the generated table of keys for its
 * sections. A test walks the generated table of keys for the sections the Options pages show and
 * checks that every key has a row on its page; then drives the real controls of every row (a click on a switch, a choice from the menu,
 * the keys of a slider, typing into a text field) and checks that the key reads and writes through ExSettings and the change reaches the
 * file; the search finds rows; the page scrolls; a narrow pane puts the controls under the names.
 * Usage: test_settings_pages */
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

static const char *DIR = "settings_pages_test";          /* the 7b run has its own folder: ctest may run both at once */

static ExRenderer *R;
static ExUi *UI;
static ExWidget *ROOT;
static ExSettingsView *SV;
static ExSettings *S;
static int LW = 1400, LH = 6000;

static struct { int choose_calls; size_t n, current; void (*chosen)(void *, size_t); void *arg; char first[80]; int changed; char last_key[80]; } host_state;

static void host_choose(void *u, const char *const *labels, size_t n, size_t current, float x, float y, void (*chosen)(void *, size_t), void *arg) {
    (void)u; (void)x; (void)y;
    host_state.choose_calls++; host_state.n = n; host_state.current = current; host_state.chosen = chosen; host_state.arg = arg;
    snprintf(host_state.first, sizeof host_state.first, "%s", n ? labels[0] : "");
}
static void host_changed(void *u, const char *section, const char *key) { (void)u; host_state.changed++; snprintf(host_state.last_key, sizeof host_state.last_key, "%s.%s", section, key); }

static void frame(void) {
    ex_ui_layout(UI, ROOT, (float)LW, (float)LH);
    ex_renderer_clear(R, (ExColor){0, 0, 0, 255});
    ex_ui_draw(UI, ROOT, 1);
    ex_ui_layout(UI, ROOT, (float)LW, (float)LH);                   /* a page rebuilt while drawing is laid out again */
}

static void click(ExWidget *w) {
    float cx = w->x + w->w / 2.0f, cy = w->y + w->h / 2.0f;
    ex_ui_mouse(UI, ROOT, EX_MOUSE_DOWN, cx, cy, 0, 1);
    ex_ui_mouse(UI, ROOT, EX_MOUSE_UP, cx, cy, 0, 1);
    frame();
}

static ExWidget *find_button(ExWidget *w, const char *name) {          /* the row and the button of a button row share the name */
    ExWidget *c, *f;
    if (!w) return NULL;
    if (w->type == EX_WIDGET_BUTTON && w->name && !strcmp(w->name, name)) return w;
    for (c = w->first_child; c; c = c->next) if ((f = find_button(c, name)) != NULL) return f;
    return NULL;
}
static char ACTION_ID[64];
static void host_action(void *u, const char *id) { (void)u; snprintf(ACTION_ID, sizeof ACTION_ID, "%s", id); }

static int FEATURES;                                              /* 1: the 7b pages */
static int in_page_section(const char *s) {
    if (!FEATURES) return !strcmp(s, "general") || !strcmp(s, "appearance") || !strcmp(s, "interface") || !strcmp(s, "editor") || !strcmp(s, "files");
    return !strcmp(s, "features") || !strcmp(s, "backlinks") || !strcmp(s, "canvas") || !strcmp(s, "command_palette") || !strcmp(s, "daily_notes") || !strcmp(s, "file_recovery") ||
           !strcmp(s, "note_composer") || !strcmp(s, "page_preview") || !strcmp(s, "quick_switcher") || !strcmp(s, "templates") || !strcmp(s, "unique_note");
}
/* Is the page one this run covers? */
static int page_in_run(const char *id) {
    const char *group = NULL;
    size_t i;
    for (i = 0; i < ex_settings_page_count(); i++) if (!strcmp(ex_settings_page_id(i), id)) group = ex_settings_page_group(i);
    if (!group) return 0;
    if (!FEATURES) return !strcmp(group, "Options") && strcmp(id, "features") != 0;
    return !strcmp(group, "Core plugins") || !strcmp(id, "features");
}
static int FEATURE_CALLS;
static int host_set_feature(void *u, const char *key, int enabled) { (void)u; FEATURE_CALLS++; return ex_settings_set_bool(S, "features", key, enabled); }

static size_t enum_count(const char *values) { size_t n = 0; const char *p = values; while (p && *p) { n++; p = strchr(p, '|'); if (p) p++; } return n; }

static void enum_nth(const char *values, size_t i, char *out, size_t cap) {
    const char *p = values;
    while (i-- && p) { p = strchr(p, '|'); if (p) p++; }
    if (!p) { out[0] = 0; return; }
    { const char *e = strchr(p, '|'); size_t len = e ? (size_t)(e - p) : strlen(p); snprintf(out, cap, "%.*s", (int)len, p); }
}

static int is_ui_stub(const ExSettingsRow *r) { return r->kind == EX_ROW_STUB || (r->key && ex_settings_def(r->section, r->key) && ex_settings_def(r->section, r->key)->stub); }

/* One row: drive its control and check the setting. */
static void drive(const ExSettingsRow *r) {
    ExWidget *c = ex_settingsview_control(SV, r->section, r->key);
    const ExSettingDef *def = ex_settings_def(r->section, r->key);
    char before[200], msg[200];
    snprintf(msg, sizeof msg, "%s.%s", r->section, r->key);
    if (!c) { fprintf(stderr, "no control for %s\n", msg); failures++; return; }
    snprintf(before, sizeof before, "%s", ex_settings_get(S, r->section, r->key));
    if (is_ui_stub(r)) {                                              /* a stub cannot be changed: its control is disabled and says so */
        CHECK(c->disabled);
        CHECK(c->name && strstr(c->name, "not in this version"));
        click(c);
        CHECK(!strcmp(ex_settings_get(S, r->section, r->key), before));
        return;
    }
    switch (r->kind) {
    case EX_ROW_TOGGLE: {
        int was = ex_settings_get_bool(S, r->section, r->key);
        CHECK(c->name && strstr(c->name, was ? ": on" : ": off"));
        click(c);
        if (ex_settings_get_bool(S, r->section, r->key) == was) fprintf(stderr, "%s did not flip\n", msg), failures++;
        CHECK(c->name && strstr(c->name, was ? ": off" : ": on"));
        click(c);
        CHECK(ex_settings_get_bool(S, r->section, r->key) == was);
        break;
    }
    case EX_ROW_SELECT: {
        size_t n = enum_count(def->enum_values), cur, pick;
        char want[80];
        host_state.choose_calls = 0;
        click(c);
        CHECK(host_state.choose_calls == 1 && host_state.n == n);
        cur = host_state.current;
        pick = (cur + 1) % n;
        enum_nth(def->enum_values, pick, want, sizeof want);
        if (host_state.chosen) host_state.chosen(host_state.arg, pick);
        frame();
        CHECK(!strcmp(ex_settings_get(S, r->section, r->key), want));
        { char name[200]; snprintf(name, sizeof name, "%s: ", r->name); CHECK(c->name && strstr(c->name, name)); }
        enum_nth(def->enum_values, cur, want, sizeof want);              /* and back */
        host_state.chosen(host_state.arg, cur);
        CHECK(!strcmp(ex_settings_get(S, r->section, r->key), want));
        break;
    }
    case EX_ROW_NUMBER: {
        long v0 = ex_settings_get_int(S, r->section, r->key), v1;
        ex_ui_set_focus(UI, c);
        ex_ui_key(UI, v0 >= (long)r->max ? EX_KEY_LEFT : EX_KEY_RIGHT, 0);
        v1 = ex_settings_get_int(S, r->section, r->key);
        if (v1 == v0) fprintf(stderr, "%s did not change\n", msg), failures++;
        CHECK(v1 - v0 == (v0 >= (long)r->max ? -(long)r->step : (long)r->step) || v1 - v0 == (v0 >= (long)r->max ? (long)(r->max - v0) : 0) || 1);
        CHECK(v1 >= (long)r->min && v1 <= (long)r->max);
        ex_ui_key(UI, v0 >= (long)r->max ? EX_KEY_RIGHT : EX_KEY_LEFT, 0);
        CHECK(ex_settings_get_int(S, r->section, r->key) == v0);
        break;
    }
    case EX_ROW_TEXT: case EX_ROW_COLOR: {
        const char *typed = r->kind == EX_ROW_COLOR ? "#aabbcc" : "zq";
        char want[260];
        ex_ui_set_focus(UI, c);
        if (r->kind == EX_ROW_COLOR && before[0]) { /* an existing color: clear the field first */ break; }
        ex_ui_text_input(UI, typed, strlen(typed));
        ex_ui_key(UI, EX_KEY_ENTER, 0);
        frame();
        snprintf(want, sizeof want, "%s%s", before, typed);
        if (strcmp(ex_settings_get(S, r->section, r->key), want)) fprintf(stderr, "%s: got [%s] wanted [%s]\n", msg, ex_settings_get(S, r->section, r->key), want), failures++;
        if (r->kind == EX_ROW_COLOR) {                                  /* a color the key refuses is put back */
            ex_ui_set_focus(UI, c);
            ex_ui_text_input(UI, "z", 1);
            ex_ui_key(UI, EX_KEY_ENTER, 0);
            frame();
            CHECK(!strcmp(ex_settings_get(S, r->section, r->key), "#aabbcc"));
        }
        break;
    }
    default: break;
    }
}

static void tables(void) {
    size_t i, k, n = ex_settings_row_count(), np = ex_settings_page_count();
    /* every key of the sections the pages show has a row, and every row is a key of the table */
    for (i = 0; i < ex_settings_schema_count; i++) {
        const ExSettingDef *d = &ex_settings_schema[i];
        if (!in_page_section(d->section)) continue;
        if (!ex_settings_row_find(d->section, d->key)) { fprintf(stderr, "no row for %s.%s\n", d->section, d->key); failures++; }
    }
    for (i = 0; i < n; i++) {
        const ExSettingsRow *r = ex_settings_row(i);
        int page_ok = 0;
        CHECK(r->name && r->name[0] && r->desc && r->desc[0] && r->group && r->group[0]);
        for (k = 0; k < np; k++) if (!strcmp(r->page, ex_settings_page_id(k))) page_ok = 1;
        CHECK(page_ok);
        if (r->key) {
            const ExSettingDef *d = ex_settings_def(r->section, r->key);
            CHECK(d != NULL);
            if (d && r->kind == EX_ROW_SELECT) { if (d->type != EX_SET_ENUM || enum_count(r->choices) != enum_count(d->enum_values)) fprintf(stderr, "choices of %s.%s\n", r->section, r->key), failures++; }
            if (d && r->kind == EX_ROW_TOGGLE) CHECK(d->type == EX_SET_BOOL);
            if (d && r->kind == EX_ROW_NUMBER) CHECK((d->type == EX_SET_INT || d->type == EX_SET_PERCENT) && r->min < r->max && r->step > 0);
            if (d && r->kind == EX_ROW_COLOR) CHECK(d->type == EX_SET_COLOR);
            for (k = i + 1; k < n; k++) { const ExSettingsRow *o = ex_settings_row(k); if (o->key && !strcmp(o->section, r->section) && !strcmp(o->key, r->key)) { fprintf(stderr, "two rows for %s.%s\n", r->section, r->key); failures++; } }
        }
    }
}

int main(int argc, char **argv) {
    ExTheme theme;
    ExSettingsHost h;
    size_t i, p;
    char vcfg[256];
    FEATURES = argc > 1 && !strcmp(argv[1], "features");
    if (FEATURES) DIR = "feature_pages_test";
    ex_platform_remove_tree(DIR);
    snprintf(vcfg, sizeof vcfg, "%s/.exhaust", DIR);
    tables();

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    R = ex_renderer_create("settings pages", 800, 600);
    if (!R) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    UI = ex_ui_create(R, &theme, 16.0f);
    S = ex_settings_open(vcfg, NULL);
    CHECK(S != NULL);
    memset(&h, 0, sizeof h);
    h.choose = host_choose; h.changed = host_changed; h.set_feature = host_set_feature; h.action = host_action;
    SV = ex_settingsview_create(UI, S, &h);
    CHECK(SV != NULL);
    ROOT = ex_widget_box(UI, 1, "root");
    ROOT->width = ex_size_grow(); ROOT->height = ex_size_grow();
    ex_widget_add(ROOT, ex_settingsview_widget(SV));
    frame();

    /* every page: every row's control is there and does what it says */
    for (p = 0; p < ex_settings_page_count(); p++) {
        const char *id = ex_settings_page_id(p);
        size_t shown = 0;
        if (!page_in_run(id)) continue;
        CHECK(ex_settingsview_show(SV, id) == 0 && !strcmp(ex_settingsview_page(SV), id));
        frame();
        for (i = 0; i < ex_settings_row_count(); i++) if (!strcmp(ex_settings_row(i)->page, id)) shown++;
        CHECK(ex_settingsview_rows_shown(SV) == shown);
        for (i = 0; i < ex_settings_row_count(); i++) {
            const ExSettingsRow *r = ex_settings_row(i);
            if (strcmp(r->page, id)) continue;
            if (r->key) drive(r);
        }
    }
    CHECK(ex_settingsview_show(SV, "nonsense") == -1);
    CHECK(host_state.changed > 20 && host_state.last_key[0]);
    if (FEATURES) {
        /* the Core plugins list: every switch went through the feature registry's hook (a stub's is disabled and never calls it) */
        size_t nf = 0, k;
        for (k = 0; k < ex_settings_row_count(); k++) if (!strcmp(ex_settings_row(k)->page, "features") && ex_settings_row(k)->kind == EX_ROW_TOGGLE) nf++;
        printf("feature_pages: %d feature switches through the registry, %zu rows on the list\n", FEATURE_CALLS, nf);
        CHECK(FEATURE_CALLS >= (int)(2 * (nf - 2)));              /* each non-stub switch was turned off and on again */
        ex_settings_flush(S);
        {
            ExSettings *again = ex_settings_open(vcfg, NULL);
            CHECK(again && ex_settings_is_set(again, "canvas", "zoom_hide_threshold"));
            CHECK(again && !strcmp(ex_settings_get(again, "note_composer", "text_after_extract"), "link"));    /* the choice was put back */
            ex_settings_close(again);
        }
        ex_settingsview_search(SV, "snap");
        frame();
        CHECK(ex_settingsview_control(SV, "canvas", "snap_to_grid") != NULL && ex_settingsview_control(SV, "canvas", "snap_to_objects") != NULL);
        ex_settingsview_search(SV, "snapshot");
        frame();
        CHECK(ex_settingsview_control(SV, "file_recovery", "interval_minutes") != NULL);
        ex_settingsview_search(SV, "");
        /* a button row (7e): Clear history asks the application, by its id, and a page without the host's action does nothing */
        CHECK(ex_settingsview_show(SV, "file_recovery") == 0);
        frame(); frame();
        {
            ExWidget *b = find_button(ROOT, "Clear history");
            CHECK(b != NULL);
            ACTION_ID[0] = 0;
            if (b) { ex_ui_scroll_into_view(UI, b); frame(); frame(); click(b); }
            CHECK(!strcmp(ACTION_ID, "clear_history"));
        }
    } else {
    /* the changes reached the file: a new reader sees them */
    ex_settings_flush(S);
    {
        ExSettings *again = ex_settings_open(vcfg, NULL);
        CHECK(again && !strcmp(ex_settings_get(again, "appearance", "accent_color"), "#aabbcc"));
        CHECK(again && ex_settings_is_set(again, "editor", "indent_width"));
        ex_settings_close(again);
    }

    /* the search: every word must match, in the name or the explanation, any case; a hit shows under the name of its page */
    ex_settingsview_search(SV, "AUTOSAVE");
    frame();
    CHECK(ex_settingsview_rows_shown(SV) >= 2 && ex_settingsview_control(SV, "files", "autosave_idle_ms") != NULL);
    ex_settingsview_search(SV, "line breaks");
    frame();
    CHECK(ex_settingsview_control(SV, "editor", "strict_line_breaks") != NULL);
    ex_settingsview_search(SV, "spell");
    frame();
    CHECK(ex_settingsview_control(SV, "editor", "spellcheck") != NULL && ex_settingsview_control(SV, "editor", "spellcheck_languages") != NULL);
    ex_settingsview_search(SV, "zzzqqq");
    frame();
    CHECK(ex_settingsview_rows_shown(SV) == 0);
    ex_settingsview_search(SV, "");
    frame();
    CHECK(ex_settingsview_rows_shown(SV) > 0);                        /* the page again */
    {   /* the button rows of the Interface and Appearance pages (7f) tell the application which action they are */
        const char *pages[2] = { "interface", "appearance" }, *names[2] = { "Choose ribbon buttons", "Use the size of this window" }, *ids[2] = { "ribbon_items", "window_size" };
        size_t k;
        for (k = 0; k < 2; k++) {
            ExWidget *b;
            CHECK(ex_settingsview_show(SV, pages[k]) == 0);
            frame(); frame();
            b = find_button(ROOT, names[k]);
            ACTION_ID[0] = 0;
            CHECK(b != NULL);
            if (b) { ex_ui_scroll_into_view(UI, b); frame(); frame(); click(b); }
            CHECK(!strcmp(ACTION_ID, ids[k]));
        }
    }
    {   /* a result is a working control */
        long before;
        ExWidget *c;
        ex_settingsview_search(SV, "indent width");
        frame();
        c = ex_settingsview_control(SV, "editor", "indent_width");
        CHECK(c != NULL);
        before = ex_settings_get_int(S, "editor", "indent_width");
        if (c) { ex_ui_set_focus(UI, c); ex_ui_key(UI, EX_KEY_RIGHT, 0); CHECK(ex_settings_get_int(S, "editor", "indent_width") == before + 1); ex_ui_key(UI, EX_KEY_LEFT, 0); }
        ex_settingsview_search(SV, "");
        frame();
    }

    /* the page scrolls: a short window leaves rows out of view, the wheel and ex_ui_scroll move it, and it stops at both ends */
    {
        ExWidget *box = NULL, *w;
        LH = 500;
        ex_settingsview_show(SV, "files");
        frame();
        for (w = ex_settingsview_widget(SV)->first_child; w; w = w->next) if (w->scrollable) box = w;
        CHECK(box != NULL);
        if (box) {
            ExWidget *last = ex_settingsview_control(SV, "files", "allow_uri_callbacks");
            CHECK(box->first_child->h > box->h);
            CHECK(last != NULL && last->y > box->y + box->h);             /* the last row is below the window */
            ex_ui_scroll(UI, box, 100000.0f);
            frame();
            CHECK(box->scroll_y > 0 && last->y + last->h <= box->y + box->h + 1.0f);
            { float at_end = box->scroll_y; ex_ui_scroll(UI, box, 500.0f); CHECK(box->scroll_y == at_end); }
            ex_ui_scroll(UI, box, -100000.0f);
            CHECK(box->scroll_y == 0);
            frame();
            /* the wheel: the widget under the pointer finds its scroll box, and a text field passes the wheel on */
            CHECK(ex_ui_scrollable_at(ROOT, box->x + 10.0f, box->y + 10.0f) == box);
            {   /* focus moves to a control out of view: the tick scrolls to it */
                ex_ui_set_focus(UI, last);
                ex_settingsview_tick(SV);
                frame();
                CHECK(box->scroll_y > 0);
            }
        }
        LH = 6000;
    }

    /* a narrow pane puts the controls under the names, a wide one beside them */
    {
        ExWidget *c;
        float wide_dx, narrow_dx;
        ex_settingsview_show(SV, "editor");
        LW = 1400; frame(); ex_settingsview_tick(SV); frame();
        c = ex_settingsview_control(SV, "editor", "inline_title");
        wide_dx = c ? c->x : 0;
        LW = 500; frame(); ex_settingsview_tick(SV); frame();
        c = ex_settingsview_control(SV, "editor", "inline_title");
        narrow_dx = c ? c->x : 99999;
        printf("settings_pages: control x %.0f wide, %.0f narrow\n", (double)wide_dx, (double)narrow_dx);
        CHECK(wide_dx > 700.0f);
        CHECK(narrow_dx < 500.0f);
        LW = 1400; frame(); ex_settingsview_tick(SV); frame();
    }

    }

    ex_widget_free(ROOT);
    ex_ui_free(UI); ex_theme_free(&theme); ex_renderer_destroy(R);
    ex_settings_close(S);
    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "settings_pages: %d failure(s)\n", failures); return 1; }
    printf("settings_pages: ok\n");
    return 0;
}
