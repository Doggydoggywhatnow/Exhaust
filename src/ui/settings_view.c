/* The settings screen (sub-slice 7a). See include/exhaust_settingsview.h and "Settings screen" in docs/formats.md. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"
#include "exhaust_commands.h"
#include "exhaust_settingsview.h"

typedef struct RowCtl RowCtl;

struct ExSettingsView {
    ExUi *ui;
    ExSettings *settings;
    ExSettingsHost host;
    ExWidget *root, *nav_col, *search_f, *scroll, *col;
    ExWidget *page_btn[32], *nav_list;
    ExDoc *search_doc;
    unsigned long search_ver;
    char page[32];                   /* the page shown (also while a search shows rows of every page) */
    char query[200];
    RowCtl *rows;                    /* the controls of the rows shown, a list */
    size_t shown;
    ExWidget *last_focus;            /* the widget that had the focus at the last tick (a text field is written when it loses it) */
    ExWidget *seen_focus;            /* the widget the page last scrolled to */
    int compact;                     /* a narrow pane: the controls go under the names, the list of pages is narrower */
    /* the Hotkeys page (7c): the commands that pass the filter, and the window of them that has widgets (the list is long: only what is near the view is built) */
    int *hk_idx; size_t hk_n;
    size_t hk_first, hk_count;
    ExWidget *hk_rows, *hk_filter_f, *hk_custom_btn;
    ExDoc *hk_doc;
    unsigned long hk_doc_ver, hk_ver;
    int hk_custom_only;
    long hk_capturing;               /* the command (index in the command table) a shortcut is being captured for, or -1 */
    ExWidget *hk_capture_btn;
    char hk_filter[160];
    /* the Theme page (7d) */
    struct TokCtl *toks;
    ExWidget *th_select, *th_warn;
    char th_warn_text[700];
};

struct RowCtl {
    RowCtl *next;
    ExSettingsView *sv;
    const ExSettingsRow *row;
    ExWidget *ctl;                   /* the switch, button, slider or text field */
    ExWidget *value;                 /* a number's value label */
    ExDoc *doc;                      /* a text field's document */
    char shown_text[200];            /* what the control shows now, to see when it must change */
    int editing;                     /* the text field has been typed in since it was last written */
};

static int ieq_ci(const char *a, const char *b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; return *a == *b; }
static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void set_name(ExWidget *w, const char *name) { char *n = dup_str(name); if (n) { free(w->name); w->name = n; } }

/* ---- text fields ---- */

static char *field_text(ExDoc *d) {
    char *t = ex_buffer_text(ex_doc_buffer(d), NULL), *nl;
    if (!t) return NULL;
    nl = strpbrk(t, "\r\n");
    if (nl) *nl = 0;
    return t;
}

static void set_field(ExDoc *d, const char *text) {
    const ExBuffer *b = ex_doc_buffer(d);
    ExChange c;
    ExSel all;
    c.offset = 0; c.remove_len = ex_buffer_size(b); c.text = text; c.text_len = strlen(text);
    ex_doc_apply(d, &c, 1, 0, NULL, 0);
    ex_doc_seal(d);
    all.anchor = all.head = strlen(text);                                  /* the caret at the end: no highlighted text in a field you have not clicked */
    ex_doc_set_selections(d, &all, 1);
}

/* ---- reading and writing a row's value ---- */

static void wrote(ExSettingsView *sv, const ExSettingsRow *r) { if (sv->host.changed) sv->host.changed(sv->host.user, r->section, r->key); }

static size_t choice_index(const ExSettingsRow *r, const char *value) {
    const ExSettingDef *def = ex_settings_def(r->section, r->key);
    const char *p = def ? def->enum_values : NULL;
    size_t i = 0;
    while (p && *p) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (strlen(value) == len && !strncmp(value, p, len)) return i;
        i++;
        p = bar ? bar + 1 : p + len;
    }
    return 0;
}

static size_t split(const char *s, char sep, const char **starts, size_t *lens, size_t cap) {
    size_t n = 0;
    while (s && *s && n < cap) {
        const char *e = strchr(s, sep);
        starts[n] = s; lens[n] = e ? (size_t)(e - s) : strlen(s);
        n++;
        s = e ? e + 1 : s + strlen(s);
    }
    return n;
}

static void choice_label(const ExSettingsRow *r, size_t index, char *out, size_t cap) {
    const char *st[16];
    size_t ln[16], n = split(r->choices, '|', st, ln, 16);
    if (index >= n) index = 0;
    snprintf(out, cap, "%.*s", n ? (int)ln[index] : 0, n ? st[index] : "");
}

static void choice_value(const ExSettingsRow *r, size_t index, char *out, size_t cap) {
    const ExSettingDef *def = ex_settings_def(r->section, r->key);
    const char *st[16];
    size_t ln[16], n = split(def ? def->enum_values : NULL, '|', st, ln, 16);
    if (index >= n) index = 0;
    snprintf(out, cap, "%.*s", n ? (int)ln[index] : 0, n ? st[index] : "");
}

/* What the control of a row should show, as text, and (for a switch) its state in the name. */
static void shown_value(const RowCtl *c, char *out, size_t cap) {
    const ExSettingsRow *r = c->row;
    const char *v = ex_settings_get(c->sv->settings, r->section, r->key);
    char buf[100];
    if (!v) v = "";
    switch (r->kind) {
    case EX_ROW_TOGGLE: snprintf(out, cap, "%s", ex_settings_get_bool(c->sv->settings, r->section, r->key) ? "On" : "Off"); break;
    case EX_ROW_SELECT: choice_label(r, choice_index(r, v), buf, sizeof buf); snprintf(out, cap, "%s", buf); break;
    default: snprintf(out, cap, "%s", v); break;
    }
}

static int is_stub_toggle(const ExSettingsRow *r) { const ExSettingDef *d = r->key ? ex_settings_def(r->section, r->key) : NULL; return r->kind == EX_ROW_TOGGLE && d && d->stub; }

static void apply_shown(RowCtl *c) {
    const ExSettingsRow *r = c->row;
    char text[200], name[320];
    if (!r->key || r->kind == EX_ROW_STUB || r->kind == EX_ROW_INFO || is_stub_toggle(r)) return;                /* nothing of the setting is shown */
    shown_value(c, text, sizeof text);
    if (!strcmp(text, c->shown_text)) return;
    snprintf(c->shown_text, sizeof c->shown_text, "%s", text);
    switch (r->kind) {
    case EX_ROW_TOGGLE:
        ex_widget_set_text(c->ctl, text);
        c->ctl->bg_token = ex_settings_get_bool(c->sv->settings, r->section, r->key) ? EX_COLOR_ACCENT : EX_COLOR_BACKGROUND;
        snprintf(name, sizeof name, "%s: %s", r->name, text[1] == 'n' ? "on" : "off");
        set_name(c->ctl, name);
        break;
    case EX_ROW_SELECT:
        snprintf(name, sizeof name, "%s: %s", r->name, text);
        ex_widget_set_text(c->ctl, text);
        set_name(c->ctl, name);
        break;
    case EX_ROW_NUMBER: {
        char v[24];
        ex_slider_set_value(c->ctl, (float)ex_settings_get_int(c->sv->settings, r->section, r->key));
        snprintf(v, sizeof v, "%ld", ex_settings_get_int(c->sv->settings, r->section, r->key));
        if (c->value) ex_widget_set_text(c->value, v);
        break;
    }
    case EX_ROW_TEXT: case EX_ROW_COLOR:
        if (c->doc && !c->editing && c->sv->last_focus != c->ctl) { set_field(c->doc, text); }
        break;
    default: break;
    }
}

/* ---- the controls' callbacks ---- */

static void on_toggle(ExWidget *w, void *u) {
    RowCtl *c = u;
    (void)w;
    int now = !ex_settings_get_bool(c->sv->settings, c->row->section, c->row->key), rc;
    if (!strcmp(c->row->section, "features") && c->sv->host.set_feature) rc = c->sv->host.set_feature(c->sv->host.user, c->row->key, now);   /* the feature registry turns the feature on or off and writes the key */
    else rc = ex_settings_set_bool(c->sv->settings, c->row->section, c->row->key, now);
    if (rc == 0) wrote(c->sv, c->row);
    apply_shown(c);
}

static void picked(void *arg, size_t index) {
    RowCtl *c = arg;
    char v[64];
    choice_value(c->row, index, v, sizeof v);
    if (ex_settings_set(c->sv->settings, c->row->section, c->row->key, v) == 0) wrote(c->sv, c->row);
    apply_shown(c);
}

static void on_select(ExWidget *w, void *u) {
    RowCtl *c = u;
    const char *st[16];
    char labels[16][80];
    const char *ptrs[16];
    size_t ln[16], n = split(c->row->choices, '|', st, ln, 16), i;
    if (!c->sv->host.choose || n == 0) return;
    for (i = 0; i < n; i++) { snprintf(labels[i], sizeof labels[i], "%.*s", (int)ln[i], st[i]); ptrs[i] = labels[i]; }
    c->sv->host.choose(c->sv->host.user, ptrs, n, choice_index(c->row, ex_settings_get(c->sv->settings, c->row->section, c->row->key)), w->x, w->y + w->h, picked, c);
}

static void on_slider(ExWidget *w, float v, void *u) {
    RowCtl *c = u;
    char t[24];
    (void)w;
    if (ex_settings_set_int(c->sv->settings, c->row->section, c->row->key, (long)v) == 0) wrote(c->sv, c->row);
    snprintf(t, sizeof t, "%ld", (long)v);
    if (c->value) ex_widget_set_text(c->value, t);
    snprintf(c->shown_text, sizeof c->shown_text, "%s", t);
}

/* Writes a text field's text. A value the key refuses is put back. */
static void commit_field(RowCtl *c) {
    char *t;
    const char *cur;
    if (!c->doc) return;
    c->editing = 0;
    t = field_text(c->doc);
    if (!t) return;
    cur = ex_settings_get(c->sv->settings, c->row->section, c->row->key);
    if (!strcmp(t, cur ? cur : "")) { free(t); return; }               /* nothing was changed */
    if (ex_settings_set(c->sv->settings, c->row->section, c->row->key, t) == 0) wrote(c->sv, c->row);
    c->shown_text[0] = 0;                                              /* show what the setting is now: the value, or the old one if it was refused */
    free(t);
    {
        const char *now = ex_settings_get(c->sv->settings, c->row->section, c->row->key);
        set_field(c->doc, now ? now : "");
    }
    apply_shown(c);
}

static int field_key(ExWidget *w, ExKey key, int mods, void *u) {
    RowCtl *c = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { commit_field(c); return 1; }
    if (key == EX_KEY_ESCAPE) {                                         /* give the old value back */
        const char *now = ex_settings_get(c->sv->settings, c->row->section, c->row->key);
        c->editing = 0;
        set_field(c->doc, now ? now : "");
        return 1;
    }
    return 0;
}

static void on_stub(ExWidget *w, void *u) { (void)w; (void)u; }
/* A button row (EX_ROW_ACTION): `choices` is "Label|id"; the application runs the action named by the id. */
static void on_action(ExWidget *w, void *u) {
    RowCtl *c = u;
    const char *bar = c->row->choices ? strchr(c->row->choices, '|') : NULL;
    (void)w;
    if (bar && c->sv->host.action) {
        char id[64];
        snprintf(id, sizeof id, "%s", bar + 1);
        c->sv->host.action(c->sv->host.user, id);
    }
}

/* ---- building a page ---- */

static void free_toks(ExSettingsView *sv);
static void free_rows(ExSettingsView *sv) {
    RowCtl *c = sv->rows, *next;
    free_toks(sv);
    for (; c; c = next) { next = c->next; ex_doc_free(c->doc); free(c); }
    sv->rows = NULL;
    sv->shown = 0;
    free(sv->hk_idx); sv->hk_idx = NULL; sv->hk_n = 0;
    ex_doc_free(sv->hk_doc); sv->hk_doc = NULL;
    sv->hk_rows = sv->hk_filter_f = sv->hk_custom_btn = sv->hk_capture_btn = NULL;
    sv->hk_capturing = -1;
}

static void clear_content(ExSettingsView *sv) {
    ExWidget *c;
    ex_ui_release(sv->ui, sv->col);
    while ((c = sv->col->first_child) != NULL) { ex_widget_remove(sv->col, c); ex_widget_free(c); }
    free_rows(sv);
    sv->last_focus = NULL;
    sv->seen_focus = NULL;
}

static ExWidget *label(ExSettingsView *sv, const char *text, int token, float scale, int bold) {
    ExWidget *l = ex_widget_label(sv->ui, text);
    if (!l) return NULL;
    l->text_token = token; l->text_scale = scale; l->bold = bold; l->wrap = 1; l->width = ex_size_grow();
    return l;
}

static ExWidget *control_for(ExSettingsView *sv, RowCtl *c, ExWidget **holder) {
    const ExSettingsRow *r = c->row;
    const ExSettingDef *def = r->key ? ex_settings_def(r->section, r->key) : NULL;
    ExWidget *ctl = NULL, *box = NULL;
    switch (r->kind) {
    case EX_ROW_TOGGLE:
        if (def && def->stub) goto stub;
        ctl = ex_widget_button(sv->ui, "Off", on_toggle, c);
        if (ctl) { ctl->pad_x = 1.0f; ctl->pad_y = 0.4f; ctl->width = ex_size_token(EX_SIZE_SPACING, 5.0f); }
        break;
    case EX_ROW_SELECT:
        ctl = ex_widget_button(sv->ui, "", on_select, c);
        if (ctl) { ctl->pad_x = 1.0f; ctl->pad_y = 0.4f; ctl->width = ex_size_token(EX_SIZE_SPACING, 16.0f); ctl->bg_token = EX_COLOR_BACKGROUND; }
        break;
    case EX_ROW_NUMBER: {
        ExWidget *vl;
        ctl = ex_widget_slider(sv->ui, r->name, r->min, r->max, r->step, r->min, on_slider, c);
        vl = ex_widget_label(sv->ui, "0");
        box = ex_widget_box(sv->ui, 0, r->name);
        if (!ctl || !vl || !box) { return NULL; }
        ctl->width = ex_size_token(EX_SIZE_SPACING, 14.0f);
        vl->text_token = EX_COLOR_TEXT_MUTED; vl->width = ex_size_token(EX_SIZE_SPACING, 5.0f);
        box->gap = 0.5f; box->align_y = 1;
        ex_widget_add(box, ctl); ex_widget_add(box, vl);
        c->value = vl;
        *holder = box;
        return ctl;
    }
    case EX_ROW_TEXT: case EX_ROW_COLOR: {
        ExWidget *f;
        c->doc = ex_doc_create("", 0);
        if (!c->doc) return NULL;
        f = ex_widget_editor(sv->ui, c->doc, r->name);
        if (!f) return NULL;
        f->bg_token = EX_COLOR_BACKGROUND; f->border_token = EX_COLOR_BORDER; f->radius_token = EX_SIZE_RADIUS;
        f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f);
        f->width = ex_size_token(EX_SIZE_SPACING, 22.0f);
        f->on_key = field_key; f->on_key_user = c;
        f->no_wheel = 1;
        ctl = f;
        break;
    }
    case EX_ROW_STUB:
    stub:
        ctl = ex_widget_button(sv->ui, "Not in this version", on_stub, c);
        if (ctl) { ctl->pad_x = 1.0f; ctl->pad_y = 0.4f; ctl->disabled = 1; }
        break;
    case EX_ROW_ACTION: {
        char label[80];
        const char *bar = r->choices ? strchr(r->choices, '|') : NULL;
        snprintf(label, sizeof label, "%.*s", bar ? (int)(bar - r->choices) : 40, r->choices ? r->choices : "");
        ctl = ex_widget_button(sv->ui, label, on_action, c);
        if (ctl) { ctl->pad_x = 1.0f; ctl->pad_y = 0.4f; }
        break;
    }
    case EX_ROW_INFO:
        ctl = ex_widget_button(sv->ui, r->choices ? r->choices : "", on_stub, c);
        if (ctl) { ctl->pad_x = 1.0f; ctl->pad_y = 0.4f; ctl->disabled = 1; }
        break;
    }
    *holder = ctl;
    return ctl;
}

static ExWidget *build_row(ExSettingsView *sv, const ExSettingsRow *r) {
    ExWidget *row = ex_widget_box(sv->ui, sv->compact, r->name), *left = ex_widget_box(sv->ui, 1, r->name), *holder = NULL, *ctl, *nm, *ds;
    RowCtl *c = calloc(1, sizeof *c);
    char desc[400];
    if (!row || !left || !c) { free(c); return row; }
    c->sv = sv; c->row = r;
    nm = label(sv, r->name, EX_COLOR_TEXT, 1.0f, 0);
    snprintf(desc, sizeof desc, "%s", r->desc);
    if (r->kind == EX_ROW_INFO && !strcmp(r->name, "Version")) snprintf(desc, sizeof desc, "Version %s. %s", sv->host.version ? sv->host.version : "unknown", r->desc);
    ds = label(sv, desc, EX_COLOR_TEXT_MUTED, 0.9f, 0);
    ctl = control_for(sv, c, &holder);
    if (!nm || !ds || !ctl || !holder) { ex_doc_free(c->doc); free(c); if (nm) ex_widget_free(nm); if (ds) ex_widget_free(ds); return row; }
    c->ctl = ctl;
    row->width = ex_size_grow(); row->gap = sv->compact ? 0.5f : 1.5f; row->align_y = sv->compact ? 0 : 1; row->pad_y = 0.75f;
    left->width = ex_size_grow(); left->gap = 0.15f;
    ex_widget_add(left, nm); ex_widget_add(left, ds);
    ex_widget_add(row, left); ex_widget_add(row, holder);
    if (r->kind == EX_ROW_TOGGLE || r->kind == EX_ROW_SELECT || r->kind == EX_ROW_NUMBER || r->kind == EX_ROW_TEXT || r->kind == EX_ROW_COLOR) {
        if (r->kind == EX_ROW_NUMBER) set_name(ctl, r->name);
    } else set_name(ctl, r->kind == EX_ROW_INFO ? r->choices : r->name);
    if (r->kind == EX_ROW_STUB || (r->kind == EX_ROW_TOGGLE && ex_settings_def(r->section, r->key) && ex_settings_def(r->section, r->key)->stub)) {
        char name[200];
        snprintf(name, sizeof name, "%s: not in this version", r->name);
        set_name(ctl, name);
    }
    c->shown_text[0] = 1; c->shown_text[1] = 0;                        /* never equal to what is shown: the first apply writes it */
    c->next = sv->rows; sv->rows = c;
    sv->shown++;
    if (r->kind != EX_ROW_STUB && r->kind != EX_ROW_INFO && !(r->kind == EX_ROW_TOGGLE && ex_settings_def(r->section, r->key) && ex_settings_def(r->section, r->key)->stub)) apply_shown(c);
    return row;
}

static ExWidget *card(ExSettingsView *sv) {
    ExWidget *b = ex_widget_box(sv->ui, 1, "");
    if (!b) return NULL;
    b->width = ex_size_grow(); b->bg_token = EX_COLOR_SURFACE; b->radius_token = EX_SIZE_RADIUS_LARGE; b->pad_x = 1.5f; b->pad_y = 0.5f; b->gap = 0.0f;
    return b;
}

static ExWidget *divider(ExSettingsView *sv) {
    ExWidget *d = ex_widget_box(sv->ui, 0, "");
    if (!d) return NULL;
    d->width = ex_size_grow(); d->height = ex_size_px(sv->ui, 1.0f); d->bg_token = EX_COLOR_BORDER;
    return d;
}



/* ---- the Theme page (7d) ---- */

typedef struct TokCtl { struct TokCtl *next; ExSettingsView *sv; char key[48]; ExDoc *doc; ExWidget *field, *swatch; char shown[80], theme_val[80], committed[80]; int editing; } TokCtl;

static void free_toks(ExSettingsView *sv) {
    TokCtl *c = sv->toks, *next;
    for (; c; c = next) { next = c->next; ex_doc_free(c->doc); free(c); }
    sv->toks = NULL; sv->th_select = sv->th_warn = NULL;
}

static const char *size_names[] = { "radius", "radius_large", "spacing", "border_width", "ribbon_width", "tab_height", "line_height" };
static const char *font_names[] = { "interface", "text", "mono" };

/* The value of a token in the theme on screen, as the text of the field. */
static void tok_value(const ExSettingsView *sv, const char *key, char *out, size_t cap) {
    const ExTheme *t = sv->host.current_theme ? sv->host.current_theme(sv->host.user) : NULL;
    out[0] = 0;
    if (!t) return;
    if (!strncmp(key, "colors.", 7)) {
        int id = ex_color_token_by_name(key + 7);
        if (id >= 0) { ExColor c = t->colors[id]; if (c.a == 255) snprintf(out, cap, "#%02x%02x%02x", c.r, c.g, c.b); else snprintf(out, cap, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a); }
    } else if (!strncmp(key, "sizes.", 6)) {
        size_t k;
        for (k = 0; k < sizeof size_names / sizeof *size_names; k++) if (!strcmp(size_names[k], key + 6)) snprintf(out, cap, "%d", t->sizes[k]);
    } else if (!strncmp(key, "fonts.", 6)) {
        size_t k;
        for (k = 0; k < sizeof font_names / sizeof *font_names; k++) if (!strcmp(font_names[k], key + 6)) {
            char sk[24];
            snprintf(sk, sizeof sk, "font_%s", font_names[k]);
            snprintf(out, cap, "%s", t->fonts[k] ? t->fonts[k] : (ex_settings_get(sv->settings, "appearance", sk) ? ex_settings_get(sv->settings, "appearance", sk) : ""));
        }
    }
}

static void tok_commit(TokCtl *c) {
    ExSettingsView *sv = c->sv;
    char *t, *name;
    const char *cur_theme = ex_settings_get(sv->settings, "appearance", "theme");
    const ExTheme *shown_theme = sv->host.current_theme ? sv->host.current_theme(sv->host.user) : NULL;
    const char *scheme = shown_theme && !shown_theme->is_dark ? "light" : "dark";                 /* a new Custom theme has the base of the theme on screen */
    c->editing = 0;
    if (!sv->host.themes) return;
    t = field_text(c->doc);
    if (!t) return;
    if (strcmp(t, c->shown) != 0) {
        snprintf(c->committed, sizeof c->committed, "%s", t);
        name = ex_themes_set_token(sv->host.themes, cur_theme ? cur_theme : "exhaust", scheme ? scheme : "dark", 0, c->key, t);
        if (name) {
            if (!cur_theme || strcmp(cur_theme, name) != 0) ex_settings_set(sv->settings, "appearance", "theme", name);
            if (sv->host.changed) sv->host.changed(sv->host.user, "appearance", "theme");
            free(name);
        } else { set_field(c->doc, c->shown); }                                   /* a value the token refuses is put back */
    }
    free(t);
}

static int tok_key(ExWidget *w, ExKey key, int mods, void *u) {
    TokCtl *c = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { tok_commit(c); return 1; }
    if (key == EX_KEY_ESCAPE) { set_field(c->doc, c->shown); c->editing = 0; return 1; }
    return 0;
}

static ExWidget *tok_row(ExSettingsView *sv, const char *key, const char *label_text, int swatch) {
    ExWidget *row = ex_widget_box(sv->ui, sv->compact, label_text), *nm = label(sv, label_text, EX_COLOR_TEXT, 1.0f, 0), *left = ex_widget_box(sv->ui, 0, ""), *f;
    TokCtl *c = calloc(1, sizeof *c);
    if (!row || !nm || !left || !c) { free(c); return row; }
    c->sv = sv;
    snprintf(c->key, sizeof c->key, "%s", key);
    c->doc = ex_doc_create("", 0);
    f = c->doc ? ex_widget_editor(sv->ui, c->doc, label_text) : NULL;
    if (!f) { ex_doc_free(c->doc); free(c); return row; }
    f->bg_token = EX_COLOR_BACKGROUND; f->border_token = EX_COLOR_BORDER; f->radius_token = EX_SIZE_RADIUS;
    f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f); f->width = ex_size_token(EX_SIZE_SPACING, 16.0f); f->no_wheel = 1;
    f->on_key = tok_key; f->on_key_user = c;
    c->field = f;
    row->width = ex_size_grow(); row->gap = sv->compact ? 0.5f : 1.0f; row->align_y = sv->compact ? 0 : 1; row->pad_y = 0.4f;
    left->width = ex_size_grow(); left->gap = 0.75f; left->align_y = 1;
    if (swatch) {
        ExWidget *sw = ex_widget_box(sv->ui, 0, "");
        if (sw) { sw->width = ex_size_token(EX_SIZE_SPACING, 3.0f); sw->height = ex_size_token(EX_SIZE_SPACING, 3.0f); sw->radius_token = EX_SIZE_RADIUS; sw->border_token = EX_COLOR_BORDER; sw->bg_custom = 1; c->swatch = sw; ex_widget_add(left, sw); }
    }
    ex_widget_add(left, nm);
    ex_widget_add(row, left); ex_widget_add(row, f);
    c->shown[0] = 1; c->shown[1] = 0; c->theme_val[0] = 1; c->theme_val[1] = 0;
    c->next = sv->toks; sv->toks = c;
    return row;
}

static void on_snippet(ExWidget *w, void *u) {
    ExSettingsView *sv = u;
    const char *list = ex_settings_get(sv->settings, "appearance", "snippets_enabled");
    char *name = w->owned, out[400], cur[400], *p;
    int on = 0;
    size_t o = 0;
    if (!name) return;
    snprintf(cur, sizeof cur, "%s", list ? list : "");
    out[0] = 0;
    for (p = cur; p && *p;) {
        char *comma = strchr(p, ','), *item = p;
        if (comma) *comma = 0;
        while (*item == ' ') item++;
        { size_t n = strlen(item); while (n && item[n - 1] == ' ') item[--n] = 0; }
        p = comma ? comma + 1 : NULL;
        if (!*item) continue;
        if (!strcmp(item, name)) { on = 1; continue; }
        o += (size_t)snprintf(out + o, sizeof out - o, "%s%s", o ? "," : "", item);
    }
    if (!on) snprintf(out + o, sizeof out - o, "%s%s", o ? "," : "", name);
    if (ex_settings_set(sv->settings, "appearance", "snippets_enabled", out) == 0 && sv->host.changed) sv->host.changed(sv->host.user, "appearance", "snippets_enabled");
    sv->th_warn_text[0] = 0;
    ex_widget_set_text(w, on ? "Off" : "On");
    w->bg_token = on ? EX_COLOR_BACKGROUND : EX_COLOR_ACCENT;
}

static void theme_picked(void *arg, size_t index) {
    ExSettingsView *sv = arg;
    char *name = sv->host.themes ? ex_themes_name(sv->host.themes, index) : NULL;
    if (!name) return;
    if (ex_settings_set(sv->settings, "appearance", "theme", index == 0 ? "exhaust" : name) == 0 && sv->host.changed) sv->host.changed(sv->host.user, "appearance", "theme");
    free(name);
}

static void on_theme_select(ExWidget *w, void *u) {
    ExSettingsView *sv = u;
    size_t n = ex_themes_count(sv->host.themes), i, cur = 0;
    char labels[24][80];
    const char *ptrs[24];
    const char *now = ex_settings_get(sv->settings, "appearance", "theme");
    if (!sv->host.choose || n == 0) return;
    if (n > 24) n = 24;
    for (i = 0; i < n; i++) {
        char *name = ex_themes_name(sv->host.themes, i);
        snprintf(labels[i], sizeof labels[i], "%s", name ? name : "");
        if (i > 0 && name && now && ieq_ci(now, name)) cur = i;
        free(name);
        ptrs[i] = labels[i];
    }
    sv->host.choose(sv->host.user, ptrs, n, cur, w->x, w->y + w->h, theme_picked, sv);
}

static void build_theme_page(ExSettingsView *sv) {
    ExWidget *intro, *cd, *h;
    size_t i, n;
    char buf[100];
    if (!sv->host.themes) { ExWidget *l = label(sv, "Themes are not available here.", EX_COLOR_TEXT_MUTED, 1.0f, 0); if (l) ex_widget_add(sv->col, l); return; }
    intro = label(sv, "Pick a theme, switch style snippets on, or change any token below: the change is written to your own theme file and shows at once. The built-in themes are never changed.", EX_COLOR_TEXT_MUTED, 0.9f, 0);
    if (intro) ex_widget_add(sv->col, intro);
    /* the theme in use and the contrast check */
    h = label(sv, "Theme in use", EX_COLOR_TEXT, 1.15f, 1);
    if (h) ex_widget_add(sv->col, h);
    cd = card(sv);
    if (cd) {
        ExWidget *row = ex_widget_box(sv->ui, sv->compact, "Theme in use"), *left = ex_widget_box(sv->ui, 1, ""), *nm = label(sv, "Theme", EX_COLOR_TEXT, 1.0f, 0);
        sv->th_select = ex_widget_button(sv->ui, "", on_theme_select, sv);
        sv->th_warn = label(sv, "", EX_COLOR_TEXT_MUTED, 0.9f, 0);
        if (row && left && nm && sv->th_select && sv->th_warn) {
            row->width = ex_size_grow(); row->gap = sv->compact ? 0.5f : 1.5f; row->align_y = sv->compact ? 0 : 1; row->pad_y = 0.75f;
            left->width = ex_size_grow(); left->gap = 0.15f;
            sv->th_select->pad_x = 1.0f; sv->th_select->pad_y = 0.4f; sv->th_select->width = ex_size_token(EX_SIZE_SPACING, 16.0f); sv->th_select->bg_token = EX_COLOR_BACKGROUND;
            ex_widget_add(left, nm); ex_widget_add(left, sv->th_warn);
            ex_widget_add(row, left); ex_widget_add(row, sv->th_select);
            ex_widget_add(cd, row);
        }
        ex_widget_add(sv->col, cd);
    }
    /* the snippets */
    n = ex_themes_snippet_count(sv->host.themes);
    h = label(sv, "Style snippets", EX_COLOR_TEXT, 1.15f, 1);
    if (h) ex_widget_add(sv->col, h);
    cd = card(sv);
    if (cd && n == 0) { ExWidget *l = label(sv, "No snippets yet. Put files that end in .style in the snippets folder of the vault's .exhaust folder.", EX_COLOR_TEXT_MUTED, 0.9f, 0); if (l) ex_widget_add(cd, l); }
    for (i = 0; cd && i < n; i++) {
        char *name = ex_themes_snippet_name(sv->host.themes, i);
        ExWidget *row = ex_widget_box(sv->ui, 0, name ? name : ""), *nm = name ? label(sv, name, EX_COLOR_TEXT, 1.0f, 0) : NULL, *b;
        const char *list = ex_settings_get(sv->settings, "appearance", "snippets_enabled");
        int on = 0;
        char *p, cur[400];
        if (!name || !row || !nm) { free(name); continue; }
        snprintf(cur, sizeof cur, "%s", list ? list : "");
        for (p = cur; p && *p;) { char *comma = strchr(p, ','), *item = p; if (comma) *comma = 0; while (*item == ' ') item++; { size_t k = strlen(item); while (k && item[k - 1] == ' ') item[--k] = 0; } p = comma ? comma + 1 : NULL; if (!strcmp(item, name)) on = 1; }
        b = ex_widget_button(sv->ui, on ? "On" : "Off", on_snippet, sv);
        if (b) {
            snprintf(buf, sizeof buf, "Snippet %s", name);
            set_name(b, buf);
            b->owned = name; b->free_owned = free; name = NULL;
            b->pad_x = 1.0f; b->pad_y = 0.4f; b->width = ex_size_token(EX_SIZE_SPACING, 5.0f); b->bg_token = on ? EX_COLOR_ACCENT : EX_COLOR_BACKGROUND;
            nm->width = ex_size_grow(); row->width = ex_size_grow(); row->align_y = 1; row->pad_y = 0.4f; row->gap = 1.0f;
            ex_widget_add(row, nm); ex_widget_add(row, b);
            ex_widget_add(cd, row);
        }
        free(name);
    }
    if (cd) ex_widget_add(sv->col, cd);
    /* every token */
    h = label(sv, "Colors", EX_COLOR_TEXT, 1.15f, 1);
    if (h) ex_widget_add(sv->col, h);
    cd = card(sv);
    for (i = 0; cd && i < EX_COLOR_COUNT; i++) {
        char key[64];
        ExWidget *r;
        snprintf(key, sizeof key, "colors.%s", ex_color_token_name((ExColorToken)i));
        r = tok_row(sv, key, ex_color_token_name((ExColorToken)i), 1);
        if (r) ex_widget_add(cd, r);
    }
    if (cd) ex_widget_add(sv->col, cd);
    h = label(sv, "Fonts and sizes", EX_COLOR_TEXT, 1.15f, 1);
    if (h) ex_widget_add(sv->col, h);
    cd = card(sv);
    for (i = 0; cd && i < sizeof font_names / sizeof *font_names; i++) { char key[48], nmb[48]; ExWidget *r; snprintf(key, sizeof key, "fonts.%s", font_names[i]); snprintf(nmb, sizeof nmb, "%s font", font_names[i]); r = tok_row(sv, key, nmb, 0); if (r) ex_widget_add(cd, r); }
    for (i = 0; cd && i < sizeof size_names / sizeof *size_names; i++) { char key[48]; ExWidget *r; snprintf(key, sizeof key, "sizes.%s", size_names[i]); r = tok_row(sv, key, size_names[i], 0); if (r) ex_widget_add(cd, r); }
    if (cd) ex_widget_add(sv->col, cd);
}

/* Once a frame while the page is shown: the name of the theme in use, the warnings, and the fields that follow the theme on screen. */
static void theme_tick(ExSettingsView *sv, ExWidget *focus) {
    TokCtl *c;
    const ExTheme *t;
    if (!sv->th_select || !sv->host.themes) return;
    t = sv->host.current_theme ? sv->host.current_theme(sv->host.user) : NULL;
    {
        const char *now = ex_settings_get(sv->settings, "appearance", "theme");
        char shown[96];
        size_t i, n = ex_themes_count(sv->host.themes);
        snprintf(shown, sizeof shown, "Exhaust");
        for (i = 1; i < n; i++) { char *name = ex_themes_name(sv->host.themes, i); if (name && now && ieq_ci(now, name)) snprintf(shown, sizeof shown, "%s", name); free(name); }
        if (!sv->th_select->text || strcmp(sv->th_select->text, shown)) { ex_widget_set_text(sv->th_select, shown); set_name(sv->th_select, "Choose the theme"); }
    }
    if (t) {
        char w[5][160], text[700];
        size_t n = ex_theme_contrast_warnings(t, w, 5), i, o = 0;
        text[0] = 0;
        if (n == 0) snprintf(text, sizeof text, "No contrast problems found.");
        for (i = 0; i < n && o < sizeof text; i++) o += (size_t)snprintf(text + o, sizeof text - o, "%s%s", i ? " " : "", w[i]);
        if (strcmp(text, sv->th_warn_text)) { snprintf(sv->th_warn_text, sizeof sv->th_warn_text, "%s", text); ex_widget_set_text(sv->th_warn, text); }
    }
    for (c = sv->toks; c; c = c->next) {
        char now[80];
        char *typed;
        tok_value(sv, c->key, now, sizeof now);
        if (focus == c->field) {                                           /* being typed in: noticed, written when it is left or on Enter */
            typed = field_text(c->doc);
            c->editing = typed && strcmp(typed, c->shown) && strcmp(typed, c->committed);
            free(typed);
        } else if (sv->last_focus == c->field && c->editing) { tok_commit(c); c->editing = 0; }
        if (strcmp(now, c->theme_val)) {                                   /* the theme on screen changed: the field shows its value, unless something else is being typed */
            snprintf(c->theme_val, sizeof c->theme_val, "%s", now);
            snprintf(c->shown, sizeof c->shown, "%s", now);
            c->committed[0] = 0;
            if (!c->editing) set_field(c->doc, now);
        }
        if (c->swatch) { ExColor col; if (ex_parse_color(c->shown, &col) == 0) c->swatch->bg_color = col; }
    }
}

/* ---- the Hotkeys page (7c) ---- */

#define HK_ROW 1.5f                                                      /* a row is this many tab heights: fixed, so the rows that are not built can be a spacer */
#define HK_WINDOW_STEP 8                                                  /* the built window moves by this many rows */

typedef struct HkCtl { ExSettingsView *sv; size_t cmd; char shortcut[48]; int kind; } HkCtl;   /* kind: 0 a chip (removes it), 1 the + button, 2 reset */

static float hk_row_px(const ExSettingsView *sv) {
    return (float)ex_theme_size(ex_ui_theme(sv->ui), EX_SIZE_TAB_HEIGHT) * ex_renderer_scale(ex_ui_renderer(sv->ui)) * HK_ROW;
}

static int hk_matches(const ExSettingsView *sv, size_t cmd) {
    const ExCommandDef *d = &ex_command_table[cmd];
    char hay[512], sc[4][48];
    size_t i, n, o;
    if (sv->hk_custom_only && !ex_hotkeys_is_custom(sv->host.hotkeys, d->id)) return 0;
    if (!sv->hk_filter[0]) return 1;
    o = (size_t)snprintf(hay, sizeof hay, "%s %s", d->name, d->id);
    n = ex_hotkeys_get(sv->host.hotkeys, d->id, sc, 4);
    for (i = 0; i < n && o < sizeof hay; i++) o += (size_t)snprintf(hay + o, sizeof hay - o, " %s", sc[i]);
    for (i = 0; hay[i]; i++) hay[i] = (char)tolower((unsigned char)hay[i]);
    {   /* every word of the filter must be in the name, the id or a shortcut */
        const char *q = sv->hk_filter;
        while (*q) {
            char word[80];
            size_t k = 0;
            while (*q == ' ') q++;
            while (*q && *q != ' ' && k + 1 < sizeof word) word[k++] = (char)tolower((unsigned char)*q++);
            word[k] = 0;
            if (k && !strstr(hay, word)) return 0;
        }
    }
    return 1;
}

static void hk_make_list(ExSettingsView *sv) {
    size_t i, n = 0;
    free(sv->hk_idx);
    sv->hk_idx = malloc((ex_command_table_count + 1) * sizeof *sv->hk_idx);
    sv->hk_n = 0;
    if (!sv->hk_idx || !sv->host.hotkeys) return;
    for (i = 0; i < ex_command_table_count; i++) if (hk_matches(sv, i)) sv->hk_idx[n++] = (int)i;
    sv->hk_n = n;
}

static void hk_captured(void *arg, const char *shortcut);

static void on_hk(ExWidget *w, void *u) {
    HkCtl *c = u;
    ExSettingsView *sv = c->sv;
    const char *id = ex_command_table[c->cmd].id;
    (void)w;
    if (c->kind == 0) { ex_hotkeys_remove(sv->host.hotkeys, id, c->shortcut); }
    else if (c->kind == 2) { ex_hotkeys_reset(sv->host.hotkeys, id); }
    else if (sv->host.capture) {                                             /* wait for the next key press */
        sv->hk_capturing = (long)c->cmd;
        ex_widget_set_text(w, "Press keys");
        sv->hk_capture_btn = w;
        sv->host.capture(sv->host.user, hk_captured, sv);
        return;
    }
    sv->hk_ver = (unsigned long)-1;                                          /* the tick builds the rows again */
}

static void hk_captured(void *arg, const char *shortcut) {
    ExSettingsView *sv = arg;
    if (sv->hk_capturing >= 0 && shortcut && sv->host.hotkeys) ex_hotkeys_add(sv->host.hotkeys, ex_command_table[sv->hk_capturing].id, shortcut);
    sv->hk_capturing = -1;
    sv->hk_capture_btn = NULL;
    sv->hk_ver = (unsigned long)-1;
}

static ExWidget *hk_button(ExSettingsView *sv, size_t cmd, int kind, const char *shortcut, const char *text, const char *name) {
    ExWidget *b;
    HkCtl *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->sv = sv; c->cmd = cmd; c->kind = kind;
    if (shortcut) snprintf(c->shortcut, sizeof c->shortcut, "%s", shortcut);
    b = ex_widget_button(sv->ui, text, on_hk, c);
    if (!b) { free(c); return NULL; }
    set_name(b, name);
    b->owned = c; b->free_owned = free;
    b->pad_x = 0.75f; b->pad_y = 0.15f;
    return b;
}

static ExWidget *hk_row(ExSettingsView *sv, size_t cmd) {
    const ExCommandDef *d = &ex_command_table[cmd];
    ExWidget *row = ex_widget_box(sv->ui, 0, d->name), *nbox = ex_widget_box(sv->ui, 0, ""), *nm = ex_widget_label(sv->ui, d->name), *b;
    char sc[8][48], name[300];
    const char *others[3];
    size_t n, i;
    if (!row || !nbox || !nm) return row;
    row->width = ex_size_grow(); row->height = ex_size_token(EX_SIZE_TAB_HEIGHT, HK_ROW); row->align_y = 1; row->gap = 0.5f;
    nbox->width = ex_size_grow(); nbox->clip = 1; nbox->align_y = 1;                     /* a clipped box does not pass the width of a long name up */
    nm->text_token = EX_COLOR_TEXT;
    ex_widget_add(nbox, nm);
    ex_widget_add(row, nbox);
    n = ex_hotkeys_get(sv->host.hotkeys, d->id, sc, 8);
    if (n == 0) { ExWidget *bl = ex_widget_label(sv->ui, "Blank"); if (bl) { bl->text_token = EX_COLOR_TEXT_FAINT; ex_widget_add(row, bl); } }
    for (i = 0; i < n; i++) {
        size_t nc = ex_hotkeys_conflicts(sv->host.hotkeys, d->id, sc[i], others, 3);
        char text[80];
        snprintf(text, sizeof text, "%s  x", sc[i]);
        snprintf(name, sizeof name, "Remove the shortcut %s of %s", sc[i], d->name);
        if (nc) {   /* name the first other command that has it */
            size_t k;
            for (k = 0; k < ex_command_table_count; k++) if (!strcmp(ex_command_table[k].id, others[0])) { snprintf(name, sizeof name, "Remove the shortcut %s of %s; it conflicts with %s", sc[i], d->name, ex_command_table[k].name); break; }
        }
        b = hk_button(sv, cmd, 0, sc[i], text, name);
        if (b) { b->bg_token = nc ? EX_COLOR_ERROR : EX_COLOR_SURFACE_HOVER; ex_widget_add(row, b); }
    }
    snprintf(name, sizeof name, "Add a shortcut to %s", d->name);
    b = hk_button(sv, cmd, 1, NULL, "+", name);
    if (b) { b->bg_token = EX_COLOR_SURFACE; ex_widget_add(row, b); }
    if (ex_hotkeys_is_custom(sv->host.hotkeys, d->id)) {
        snprintf(name, sizeof name, "Restore the default shortcuts of %s", d->name);
        b = hk_button(sv, cmd, 2, NULL, "Reset", name);
        if (b) { b->bg_token = EX_COLOR_SURFACE; ex_widget_add(row, b); }
    }
    return row;
}

/* Builds the rows of the window [hk_first, hk_first + hk_count) between two spacers that stand for the rows that are not built. */
static void hk_build_rows(ExSettingsView *sv) {
    ExWidget *c, *sp;
    size_t i, last;
    ex_ui_release(sv->ui, sv->hk_rows);
    while ((c = sv->hk_rows->first_child) != NULL) { ex_widget_remove(sv->hk_rows, c); ex_widget_free(c); }
    sv->hk_capture_btn = NULL;
    if (sv->hk_first > sv->hk_n) sv->hk_first = 0;
    last = sv->hk_first + sv->hk_count;
    if (last > sv->hk_n) last = sv->hk_n;
    sp = ex_widget_box(sv->ui, 1, "");
    if (sp) { sp->width = ex_size_grow(); sp->height = ex_size_token(EX_SIZE_TAB_HEIGHT, HK_ROW * (float)sv->hk_first); ex_widget_add(sv->hk_rows, sp); }
    for (i = sv->hk_first; i < last; i++) { ExWidget *r = hk_row(sv, (size_t)sv->hk_idx[i]); if (r) ex_widget_add(sv->hk_rows, r); }
    sp = ex_widget_box(sv->ui, 1, "");
    if (sp) { sp->width = ex_size_grow(); sp->height = ex_size_token(EX_SIZE_TAB_HEIGHT, HK_ROW * (float)(sv->hk_n - last)); ex_widget_add(sv->hk_rows, sp); }
    sv->shown = last - sv->hk_first;
    sv->hk_ver = ex_hotkeys_version(sv->host.hotkeys);
}

static void on_hk_custom(ExWidget *w, void *u) {
    ExSettingsView *sv = u;
    sv->hk_custom_only = !sv->hk_custom_only;
    ex_widget_set_text(w, sv->hk_custom_only ? "On" : "Off");
    set_name(w, sv->hk_custom_only ? "Customized only: on" : "Customized only: off");
    w->bg_token = sv->hk_custom_only ? EX_COLOR_ACCENT : EX_COLOR_BACKGROUND;
    hk_make_list(sv);
    sv->hk_first = 0;
    hk_build_rows(sv);
}

static void build_hotkeys_page(ExSettingsView *sv) {
    ExWidget *head, *intro;
    if (!sv->host.hotkeys) {
        ExWidget *l = label(sv, "The shortcuts are not available here.", EX_COLOR_TEXT_MUTED, 1.0f, 0);
        if (l) ex_widget_add(sv->col, l);
        return;
    }
    intro = label(sv, "Every command can have shortcuts. Press + and then the keys you want. A shortcut that two commands share is marked in red: the first command that can run does.", EX_COLOR_TEXT_MUTED, 0.9f, 0);
    if (intro) ex_widget_add(sv->col, intro);
    sv->hk_doc = ex_doc_create("", 0);
    head = ex_widget_box(sv->ui, 0, "Filter the commands");
    sv->hk_filter_f = sv->hk_doc ? ex_widget_editor(sv->ui, sv->hk_doc, "Filter the commands") : NULL;
    sv->hk_custom_btn = ex_widget_button(sv->ui, "Off", on_hk_custom, sv);
    sv->hk_rows = ex_widget_box(sv->ui, 1, "Commands and their shortcuts");
    if (!head || !sv->hk_filter_f || !sv->hk_custom_btn || !sv->hk_rows) return;
    head->width = ex_size_grow(); head->gap = 1.0f; head->align_y = 1;
    sv->hk_filter_f->bg_token = EX_COLOR_BACKGROUND; sv->hk_filter_f->border_token = EX_COLOR_BORDER; sv->hk_filter_f->radius_token = EX_SIZE_RADIUS;
    sv->hk_filter_f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f); sv->hk_filter_f->width = ex_size_grow(); sv->hk_filter_f->no_wheel = 1;
    sv->hk_custom_btn->pad_x = 1.0f; sv->hk_custom_btn->pad_y = 0.4f; sv->hk_custom_btn->width = ex_size_token(EX_SIZE_SPACING, 5.0f); sv->hk_custom_btn->bg_token = EX_COLOR_BACKGROUND;
    set_name(sv->hk_custom_btn, "Customized only: off");
    {
        ExWidget *cl = ex_widget_label(sv->ui, "Customized only");
        if (cl) { cl->text_token = EX_COLOR_TEXT_MUTED; ex_widget_add(head, sv->hk_filter_f); ex_widget_add(head, cl); }
    }
    ex_widget_add(head, sv->hk_custom_btn);
    sv->hk_rows->width = ex_size_grow();
    ex_widget_add(sv->col, head);
    ex_widget_add(sv->col, sv->hk_rows);
    sv->hk_filter[0] = 0; sv->hk_custom_only = 0; sv->hk_first = 0; sv->hk_count = 40; sv->hk_doc_ver = ex_doc_version(sv->hk_doc);
    hk_make_list(sv);
    hk_build_rows(sv);
}

/* Once a frame while the page is shown: the filter, the shortcuts changing, and the window of built rows following the scroll. */
static void hk_tick(ExSettingsView *sv) {
    float px, off, vis;
    size_t first_vis, want_first, want_count;
    int changed = 0;
    if (!sv->hk_rows || !sv->host.hotkeys) return;
    if (ex_doc_version(sv->hk_doc) != sv->hk_doc_ver) {
        char *t = field_text(sv->hk_doc);
        sv->hk_doc_ver = ex_doc_version(sv->hk_doc);
        if (t) { snprintf(sv->hk_filter, sizeof sv->hk_filter, "%s", t); free(t); }
        hk_make_list(sv);
        sv->hk_first = 0;
        changed = 1;
    }
    px = hk_row_px(sv);
    off = sv->scroll->y - sv->hk_rows->y;                                  /* how far the top of the list is above the top of the view */
    vis = sv->scroll->h;
    first_vis = off > 0 && px > 0 ? (size_t)(off / px) : 0;
    want_first = first_vis > HK_WINDOW_STEP ? ((first_vis - HK_WINDOW_STEP) / HK_WINDOW_STEP) * HK_WINDOW_STEP : 0;
    want_count = (px > 0 ? (size_t)(vis / px) + 1 : 20) + 3 * HK_WINDOW_STEP;
    if (want_first != sv->hk_first || want_count > sv->hk_count) { sv->hk_first = want_first; if (want_count > sv->hk_count) sv->hk_count = want_count; changed = 1; }
    if (sv->hk_ver != ex_hotkeys_version(sv->host.hotkeys)) { hk_make_list(sv); changed = 1; }
    if (changed) hk_build_rows(sv);
}

static int words_match(const ExSettingsRow *r, const char *query) {
    char hay[800];
    size_t i = 0, n;
    snprintf(hay, sizeof hay, "%s %s %s", r->name, r->desc, r->key ? r->key : "");
    for (n = 0; hay[n]; n++) hay[n] = (char)tolower((unsigned char)hay[n]);
    while (query[i]) {                                                   /* every word of the query must be somewhere in the row (no strtok_r: it is not on Windows) */
        char word[80];
        size_t k = 0;
        while (query[i] == ' ' || query[i] == '\t') i++;
        while (query[i] && query[i] != ' ' && query[i] != '\t' && k + 1 < sizeof word) word[k++] = (char)tolower((unsigned char)query[i++]);
        word[k] = 0;
        if (k && !strstr(hay, word)) return 0;
    }
    return 1;
}

static const char *page_name_of(const char *id) {
    size_t i;
    for (i = 0; i < ex_settings_page_count(); i++) if (!strcmp(ex_settings_page_id(i), id)) return ex_settings_page_name(i);
    return id;
}

static void select_nav(ExSettingsView *sv) {
    size_t i;
    for (i = 0; i < ex_settings_page_count() && i < 32; i++)
        if (sv->page_btn[i]) sv->page_btn[i]->selected = sv->query[0] == 0 && !strcmp(ex_settings_page_id(i), sv->page);
}

static void rebuild(ExSettingsView *sv) {
    size_t i, nrows = ex_settings_row_count();
    const char *group = NULL, *page = NULL;
    ExWidget *cd = NULL;
    int first_in_card = 1;
    clear_content(sv);
    if (!sv->query[0] && !strcmp(sv->page, "hotkeys")) { build_hotkeys_page(sv); sv->scroll->scroll_y = 0; select_nav(sv); return; }
    if (!sv->query[0] && !strcmp(sv->page, "theme")) { build_theme_page(sv); sv->scroll->scroll_y = 0; select_nav(sv); return; }
    for (i = 0; i < nrows; i++) {
        const ExSettingsRow *r = ex_settings_row(i);
        ExWidget *row;
        if (sv->query[0]) { if (!words_match(r, sv->query)) continue; }
        else if (strcmp(r->page, sv->page)) continue;
        if (!group || strcmp(group, r->group) || (sv->query[0] && (!page || strcmp(page, r->page)))) {      /* a new card under a heading */
            ExWidget *h;
            char title[120];
            if (sv->query[0]) snprintf(title, sizeof title, "%s: %s", page_name_of(r->page), r->group); else snprintf(title, sizeof title, "%s", r->group);
            h = label(sv, title, EX_COLOR_TEXT, 1.15f, 1);
            if (h) { h->pad_y = 0.0f; ex_widget_add(sv->col, h); }
            cd = card(sv);
            if (cd) ex_widget_add(sv->col, cd);
            group = r->group; page = r->page; first_in_card = 1;
        }
        if (!cd) continue;
        if (!first_in_card) { ExWidget *d = divider(sv); if (d) ex_widget_add(cd, d); }
        row = build_row(sv, r);
        if (row) ex_widget_add(cd, row);
        first_in_card = 0;
    }
    if (sv->query[0] && sv->shown == 0) {
        ExWidget *l = label(sv, "No setting matches.", EX_COLOR_TEXT_MUTED, 1.0f, 0);
        if (l) ex_widget_add(sv->col, l);
    }
    sv->scroll->scroll_y = 0;
    select_nav(sv);
}

int ex_settingsview_show(ExSettingsView *sv, const char *page) {
    size_t i;
    if (!sv || !page) return -1;
    for (i = 0; i < ex_settings_page_count(); i++) if (!strcmp(ex_settings_page_id(i), page)) break;
    if (i == ex_settings_page_count()) return -1;
    snprintf(sv->page, sizeof sv->page, "%s", page);
    if (sv->query[0]) { sv->query[0] = 0; set_field(sv->search_doc, ""); sv->search_ver = ex_doc_version(sv->search_doc); }
    rebuild(sv);
    return 0;
}

const char *ex_settingsview_page(const ExSettingsView *sv) { return sv ? sv->page : NULL; }

void ex_settingsview_search(ExSettingsView *sv, const char *query) {
    if (!sv) return;
    set_field(sv->search_doc, query ? query : "");
    sv->search_ver = (unsigned long)-1;                                  /* the tick notices the change */
    ex_settingsview_tick(sv);
}

size_t ex_settingsview_rows_shown(const ExSettingsView *sv) { return sv ? sv->shown : 0; }

ExWidget *ex_settingsview_control(const ExSettingsView *sv, const char *section, const char *key) {
    RowCtl *c;
    if (!sv) return NULL;
    for (c = sv->rows; c; c = c->next) if (c->row->key && !strcmp(c->row->section, section) && !strcmp(c->row->key, key)) return c->ctl;
    return NULL;
}

/* ---- the frame ---- */

void ex_settingsview_tick(ExSettingsView *sv) {
    RowCtl *c;
    ExWidget *focus = ex_ui_focus(sv->ui);
    unsigned long ver = ex_doc_version(sv->search_doc);
    /* a narrow pane puts the controls under the names (the widths of the last layout) */
    {
        float unit = (float)ex_theme_size(ex_ui_theme(sv->ui), EX_SIZE_SPACING) * ex_renderer_scale(ex_ui_renderer(sv->ui));
        int compact = sv->root->w > 0 && unit > 0 && sv->root->w < 64.0f * unit;
        if (compact != sv->compact) {
            sv->compact = compact;
            sv->nav_col->width = ex_size_token(EX_SIZE_SPACING, compact ? 25.0f : 27.0f);
            sv->col->pad_x = compact ? 1.5f : 3.0f;
            rebuild(sv);
            focus = ex_ui_focus(sv->ui);
        }
    }
    /* the search box */
    if (ver != sv->search_ver) {
        char *t = field_text(sv->search_doc);
        sv->search_ver = ver;
        if (t) {
            char trimmed[200];
            size_t b = 0, e;
            snprintf(trimmed, sizeof trimmed, "%s", t);
            while (trimmed[b] == ' ') b++;
            e = strlen(trimmed);
            while (e > b && trimmed[e - 1] == ' ') e--;
            trimmed[e] = 0;
            if (strcmp(trimmed + b, sv->query)) { snprintf(sv->query, sizeof sv->query, "%s", trimmed + b); rebuild(sv); focus = ex_ui_focus(sv->ui); }
            free(t);
        }
    }
    /* a text field that was typed in is written when it loses the focus; what is typed is noticed by its version */
    for (c = sv->rows; c; c = c->next) {
        if (c->doc) {
            if (focus == c->ctl) { char *t = field_text(c->doc); const char *now = ex_settings_get(sv->settings, c->row->section, c->row->key); if (t && strcmp(t, now ? now : "")) c->editing = 1; free(t); }
            else if (sv->last_focus == c->ctl) commit_field(c);
        }
        apply_shown(c);
    }
    theme_tick(sv, focus);
    sv->last_focus = focus;
    hk_tick(sv);
    focus = ex_ui_focus(sv->ui);                                            /* hk_tick may have freed the button that had it */
    /* the focus moved to a control that is out of view: scroll to it */
    if (focus != sv->seen_focus) {
        sv->seen_focus = focus;
        if (focus && focus->y > 0) ex_ui_scroll_into_view(sv->ui, focus);
    }
}

static void tick_cb(ExWidget *w, void *u) { (void)w; ex_settingsview_tick(u); }

static void on_page(ExWidget *w, void *u) {
    ExSettingsView *sv = u;
    size_t i;
    for (i = 0; i < ex_settings_page_count() && i < 32; i++) if (sv->page_btn[i] == w) { ex_settingsview_show(sv, ex_settings_page_id(i)); return; }
}

static void free_view(void *p) {
    ExSettingsView *sv = p;
    if (!sv) return;
    free_rows(sv);
    ex_doc_free(sv->search_doc);
    free(sv);
}

ExWidget *ex_settingsview_widget(ExSettingsView *sv) { return sv ? sv->root : NULL; }

ExSettingsView *ex_settingsview_create(ExUi *ui, ExSettings *settings, const ExSettingsHost *host) {
    ExSettingsView *sv = calloc(1, sizeof *sv);
    ExWidget *search_label;
    size_t i;
    if (!sv) return NULL;
    sv->ui = ui; sv->settings = settings;
    if (host) sv->host = *host;
    sv->search_doc = ex_doc_create("", 0);
    sv->root = ex_widget_box(ui, 0, "Settings");
    sv->nav_col = ex_widget_box(ui, 1, "Settings pages");
    sv->scroll = ex_widget_scrollbox(ui, "Settings page");
    sv->col = ex_widget_box(ui, 1, "Settings content");
    search_label = ex_widget_label(ui, "Search");
    sv->search_f = sv->search_doc ? ex_widget_editor(ui, sv->search_doc, "Search settings") : NULL;
    if (!sv->root || !sv->nav_col || !sv->scroll || !sv->col || !search_label || !sv->search_f) { free_view(sv); return NULL; }
    sv->root->width = ex_size_percent(1.0f); sv->root->height = ex_size_percent(1.0f);   /* not grow: a child of the clipped pane that grows takes the size of its content (Clay) */
    sv->root->bg_token = EX_COLOR_BACKGROUND;
    sv->nav_col->width = ex_size_token(EX_SIZE_SPACING, 27.0f); sv->nav_col->height = ex_size_grow(); sv->nav_col->bg_token = EX_COLOR_SURFACE;
    sv->nav_col->pad_x = 1.0f; sv->nav_col->pad_y = 1.0f; sv->nav_col->gap = 0.25f;
    search_label->text_token = EX_COLOR_TEXT_MUTED; search_label->text_scale = 0.85f;
    sv->search_f->bg_token = EX_COLOR_BACKGROUND; sv->search_f->border_token = EX_COLOR_BORDER; sv->search_f->radius_token = EX_SIZE_RADIUS;
    sv->search_f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f); sv->search_f->width = ex_size_grow(); sv->search_f->no_wheel = 1;
    ex_widget_add(sv->nav_col, search_label); ex_widget_add(sv->nav_col, sv->search_f);
    {   /* the list of pages scrolls when the window is short */
        ExWidget *nav_scroll = ex_widget_scrollbox(ui, "Settings page list");
        sv->nav_list = ex_widget_box(ui, 1, "Settings pages list");
        if (!nav_scroll || !sv->nav_list) { free_view(sv); return NULL; }
        nav_scroll->width = ex_size_grow(); nav_scroll->height = ex_size_grow();
        sv->nav_list->width = ex_size_grow(); sv->nav_list->gap = 0.25f;
        ex_widget_add(nav_scroll, sv->nav_list);
        ex_widget_add(sv->nav_col, nav_scroll);
        for (i = 0; i < ex_settings_page_count() && i < 32; i++) {
            ExWidget *b;
            if (i == 0 || strcmp(ex_settings_page_group(i), ex_settings_page_group(i - 1))) {
                ExWidget *gl = ex_widget_label(ui, ex_settings_page_group(i));
                if (gl) { gl->bold = 1; gl->text_token = EX_COLOR_TEXT_MUTED; gl->pad_y = 0.5f; ex_widget_add(sv->nav_list, gl); }
            }
            b = ex_widget_button(ui, ex_settings_page_name(i), on_page, sv);
            if (!b) continue;
            b->menu_item = 1; b->width = ex_size_grow(); b->bg_token = EX_COLOR_SURFACE; b->border_token = -1;
            b->icon = !strcmp(ex_settings_page_id(i), "general") ? EX_ICON_GEAR : !strcmp(ex_settings_page_id(i), "appearance") ? EX_ICON_PALETTE : !strcmp(ex_settings_page_id(i), "interface") ? EX_ICON_MODE :
                      !strcmp(ex_settings_page_id(i), "editor") ? EX_ICON_PROPERTIES : !strcmp(ex_settings_page_id(i), "files") ? EX_ICON_FILES : !strcmp(ex_settings_page_id(i), "keychain") ? EX_ICON_CLIPBOARD :
                      !strcmp(ex_settings_page_id(i), "features") ? EX_ICON_OUTLINE : 0;
            sv->page_btn[i] = b;
            ex_widget_add(sv->nav_list, b);
        }
    }
    sv->scroll->width = ex_size_grow(); sv->scroll->height = ex_size_grow();
    sv->col->width = ex_size_grow(); sv->col->pad_x = 3.0f; sv->col->pad_y = 2.0f; sv->col->gap = 1.0f;
    ex_widget_add(sv->scroll, sv->col);
    ex_widget_add(sv->root, sv->nav_col); ex_widget_add(sv->root, sv->scroll);
    sv->root->owned = sv; sv->root->free_owned = free_view;
    sv->root->on_tick = tick_cb; sv->root->on_tick_user = sv;
    snprintf(sv->page, sizeof sv->page, "general");
    sv->search_ver = ex_doc_version(sv->search_doc);
    rebuild(sv);
    return sv;
}
