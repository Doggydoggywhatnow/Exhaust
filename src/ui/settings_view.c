/* The settings screen (sub-slice 7a). See include/exhaust_settingsview.h and "Settings screen" in docs/formats.md. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"
#include "exhaust_settingsview.h"

typedef struct RowCtl RowCtl;

struct ExSettingsView {
    ExUi *ui;
    ExSettings *settings;
    ExSettingsHost host;
    ExWidget *root, *nav_col, *search_f, *scroll, *col;
    ExWidget *page_btn[16];
    ExDoc *search_doc;
    unsigned long search_ver;
    char page[32];                   /* the page shown (also while a search shows rows of every page) */
    char query[200];
    RowCtl *rows;                    /* the controls of the rows shown, a list */
    size_t shown;
    ExWidget *last_focus;            /* the widget that had the focus at the last tick (a text field is written when it loses it) */
    ExWidget *seen_focus;            /* the widget the page last scrolled to */
    int compact;                     /* a narrow pane: the controls go under the names, the list of pages is narrower */
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
    if (ex_settings_set_bool(c->sv->settings, c->row->section, c->row->key, !ex_settings_get_bool(c->sv->settings, c->row->section, c->row->key)) == 0) wrote(c->sv, c->row);
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

/* ---- building a page ---- */

static void free_rows(ExSettingsView *sv) {
    RowCtl *c = sv->rows, *next;
    for (; c; c = next) { next = c->next; ex_doc_free(c->doc); free(c); }
    sv->rows = NULL;
    sv->shown = 0;
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
    for (i = 0; i < ex_settings_page_count() && i < 16; i++)
        if (sv->page_btn[i]) sv->page_btn[i]->selected = sv->query[0] == 0 && !strcmp(ex_settings_page_id(i), sv->page);
}

static void rebuild(ExSettingsView *sv) {
    size_t i, nrows = ex_settings_row_count();
    const char *group = NULL, *page = NULL;
    ExWidget *cd = NULL;
    int first_in_card = 1;
    clear_content(sv);
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
            sv->nav_col->width = ex_size_token(EX_SIZE_SPACING, compact ? 22.0f : 24.0f);
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
    sv->last_focus = focus;
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
    for (i = 0; i < ex_settings_page_count() && i < 16; i++) if (sv->page_btn[i] == w) { ex_settingsview_show(sv, ex_settings_page_id(i)); return; }
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
    ExWidget *group_label, *search_label;
    size_t i;
    static const int icons[6] = { EX_ICON_GEAR, EX_ICON_PALETTE, EX_ICON_MODE, EX_ICON_PROPERTIES, EX_ICON_FILES, EX_ICON_CLIPBOARD };
    if (!sv) return NULL;
    sv->ui = ui; sv->settings = settings;
    if (host) sv->host = *host;
    sv->search_doc = ex_doc_create("", 0);
    sv->root = ex_widget_box(ui, 0, "Settings");
    sv->nav_col = ex_widget_box(ui, 1, "Settings pages");
    sv->scroll = ex_widget_scrollbox(ui, "Settings page");
    sv->col = ex_widget_box(ui, 1, "Settings content");
    search_label = ex_widget_label(ui, "Search");
    group_label = ex_widget_label(ui, "Options");
    sv->search_f = sv->search_doc ? ex_widget_editor(ui, sv->search_doc, "Search settings") : NULL;
    if (!sv->root || !sv->nav_col || !sv->scroll || !sv->col || !search_label || !group_label || !sv->search_f) { free_view(sv); return NULL; }
    sv->root->width = ex_size_percent(1.0f); sv->root->height = ex_size_percent(1.0f);   /* not grow: a child of the clipped pane that grows takes the size of its content (Clay) */
    sv->root->bg_token = EX_COLOR_BACKGROUND;
    sv->nav_col->width = ex_size_token(EX_SIZE_SPACING, 24.0f); sv->nav_col->height = ex_size_grow(); sv->nav_col->bg_token = EX_COLOR_SURFACE;
    sv->nav_col->pad_x = 1.0f; sv->nav_col->pad_y = 1.0f; sv->nav_col->gap = 0.25f;
    search_label->text_token = EX_COLOR_TEXT_MUTED; search_label->text_scale = 0.85f;
    sv->search_f->bg_token = EX_COLOR_BACKGROUND; sv->search_f->border_token = EX_COLOR_BORDER; sv->search_f->radius_token = EX_SIZE_RADIUS;
    sv->search_f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f); sv->search_f->width = ex_size_grow(); sv->search_f->no_wheel = 1;
    group_label->bold = 1; group_label->text_token = EX_COLOR_TEXT_MUTED; group_label->pad_y = 0.5f;
    ex_widget_add(sv->nav_col, search_label); ex_widget_add(sv->nav_col, sv->search_f); ex_widget_add(sv->nav_col, group_label);
    for (i = 0; i < ex_settings_page_count() && i < 16; i++) {
        ExWidget *b = ex_widget_button(ui, ex_settings_page_name(i), on_page, sv);
        if (!b) continue;
        b->menu_item = 1; b->width = ex_size_grow(); b->bg_token = EX_COLOR_SURFACE; b->border_token = -1; b->icon = i < 6 ? icons[i] : 0;
        sv->page_btn[i] = b;
        ex_widget_add(sv->nav_col, b);
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
