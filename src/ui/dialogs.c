/* The dialogs (quick switcher, command palette, prompt, choice, confirmation) and the page preview. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_vault.h"
#include "panels_internal.h"

static float px_to_units(ExPanels *p, float px) { return px / (float)ex_theme_size(ex_ui_theme(p->ui), EX_SIZE_SPACING); }

void dialog_close(ExPanels *p) {
    if (p->overlay) { ex_ui_release(p->ui, p->overlay); ex_widget_free(p->overlay); p->overlay = NULL; }
    ex_doc_free(p->dialog_doc);
    p->dialog_doc = NULL; p->dialog_field = p->dialog_list = p->dialog_msg = NULL;
    p->dkind = D_NONE;
    free(p->palette); p->palette = NULL; p->npalette = 0;
}

void dialogs_free(ExPanels *p) { dialog_close(p); preview_hide(p); }

/* The frame: a full-window box that centers the dialog near the top. */
static ExWidget *frame(ExPanels *p, const char *title, float width_px) {
    ExWidget *root = ex_widget_box(p->ui, 1, title), *box, *t;
    root->width = ex_size_grow(); root->height = ex_size_grow();
    root->align_x = 1; root->pad_y = px_to_units(p, 90);
    box = ex_widget_box(p->ui, 1, title);
    box->width = ex_size_px(p->ui, width_px);
    box->bg_token = EX_COLOR_SURFACE; box->border_token = EX_COLOR_BORDER; box->radius_token = EX_SIZE_RADIUS;
    box->pad_x = 1.5f; box->pad_y = 1.5f; box->gap = 1;
    t = ex_widget_label(p->ui, title);
    t->bold = 1; t->text_token = EX_COLOR_TEXT;
    ex_widget_add(box, t);
    ex_widget_add(root, box);
    snprintf(p->dialog_title, sizeof p->dialog_title, "%s", title);
    p->overlay = root;
    return box;
}

static ExWidget *dlg_buttons(ExPanels *p, ExWidget *box) {
    ExWidget *row = ex_widget_box(p->ui, 0, "Buttons");
    row->width = ex_size_grow(); row->gap = 1; row->align_x = 2;
    ex_widget_add(box, row);
    return row;
}

/* ---- prompt ---- */

static int prompt_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExPanels *p = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ESCAPE) { dialog_close(p); return 1; }
    if (key == EX_KEY_ENTER) {
        char *t = field_text_of(p->dialog_doc);
        void (*cb)(ExPanels *, const char *, void *) = p->on_prompt;
        void *arg = p->dialog_arg;
        dialog_close(p);
        if (cb && t) cb(p, t, arg);
        free(t);
        return 1;
    }
    return 0;
}

static void on_cancel(ExWidget *w, void *u) { (void)w; dialog_close(u); }
static void on_ok(ExWidget *w, void *u) { (void)w; prompt_key(NULL, EX_KEY_ENTER, 0, u); }

void dialog_prompt(ExPanels *p, const char *title, const char *label, const char *initial, void (*cb)(ExPanels *, const char *, void *), void *arg) {
    ExWidget *box, *lab, *row;
    dialog_close(p);
    preview_hide(p);
    box = frame(p, title, 480);
    lab = ex_widget_label(p->ui, label);
    lab->text_token = EX_COLOR_TEXT_MUTED;
    ex_widget_add(box, lab);
    p->dialog_doc = ex_doc_create("", 0);
    set_doc_text(p->dialog_doc, initial);
    p->dialog_field = make_field(p, p->dialog_doc, label, prompt_key, p);
    ex_widget_add(box, p->dialog_field);
    row = dlg_buttons(p, box);
    ex_widget_add(row, make_button(p, "Cancel", "Cancel", on_cancel));
    ex_widget_add(row, make_button(p, "OK", "OK", on_ok));
    p->dkind = D_PROMPT; p->on_prompt = cb; p->dialog_arg = arg;
    ex_ui_set_focus(p->ui, p->dialog_field);
}

/* ---- choice ---- */

static void choice_activate(ExWidget *w, size_t row, void *u) {
    ExPanels *p = u;
    void (*cb)(ExPanels *, size_t, const char *, void *) = p->on_choice;
    void *arg = p->dialog_arg;
    const ExListRow *r = ex_list_row(w, row);
    char key[100];
    snprintf(key, sizeof key, "%s", r && r->key ? r->key : "");
    dialog_close(p);
    if (cb) cb(p, row, key, arg);
}

void dialog_choice(ExPanels *p, const char *title, const char **labels, size_t n, size_t selected, void (*cb)(ExPanels *, size_t, const char *, void *), void *arg) {
    ExWidget *box, *row;
    ExListHandlers h;
    size_t i;
    dialog_close(p);
    preview_hide(p);
    box = frame(p, title, 360);
    p->dialog_list = ex_widget_list(p->ui, title);
    p->dialog_list->height = ex_size_px(p->ui, 26.0f * (float)(n < 10 ? n : 10) + 8);
    for (i = 0; i < n; i++) { ExListRow r; memset(&r, 0, sizeof r); r.text = labels[i]; r.key = labels[i]; ex_list_add(p->dialog_list, &r); }
    ex_list_select(p->dialog_list, selected);
    memset(&h, 0, sizeof h); h.activate = choice_activate; h.user = p; h.single_click = 1;
    ex_list_set_handlers(p->dialog_list, &h);
    ex_widget_add(box, p->dialog_list);
    row = dlg_buttons(p, box);
    ex_widget_add(row, make_button(p, "Cancel", "Cancel", on_cancel));
    p->dkind = D_CHOICE; p->on_choice = cb; p->dialog_arg = arg;
    ex_ui_set_focus(p->ui, p->dialog_list);
}

/* ---- confirmation ---- */

static void confirm_click(ExWidget *w, void *u) {
    ExPanels *p = u;
    int which = (int)(intptr_t)w->tag;
    void (*cb)(ExPanels *, int, void *) = p->on_confirm;
    void *arg = p->dialog_arg;
    dialog_close(p);
    if (cb) cb(p, which, arg);
}

void dialog_confirm(ExPanels *p, const char *title, const char *message, const char *b1, const char *b2, const char *b3, void (*cb)(ExPanels *, int, void *), void *arg) {
    ExWidget *box, *row, *b;
    const char *labels[3];
    char *copy = dup_s(message), *line, *next;
    int i;
    dialog_close(p);
    preview_hide(p);
    box = frame(p, title, 520);
    for (line = copy; line && *line; line = next) {
        ExWidget *l;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        l = ex_widget_label(p->ui, line);
        l->text_token = EX_COLOR_TEXT;
        ex_widget_add(box, l);
    }
    free(copy);
    row = dlg_buttons(p, box);
    labels[0] = b1; labels[1] = b2; labels[2] = b3;
    for (i = 0; i < 3; i++) {
        if (!labels[i]) continue;
        b = make_button(p, labels[i], labels[i], confirm_click);
        b->tag = (uint32_t)i + 1;
        ex_widget_add(row, b);
        if (i == 0) ex_ui_set_focus(p->ui, b);
    }
    p->dkind = D_CONFIRM; p->on_confirm = cb; p->dialog_arg = arg;
}

/* ---- the quick switcher ---- */

static void switcher_open_selected(ExPanels *p, int force_create) {
    ExWidget *l = p->dialog_list;
    const ExListRow *r = l ? ex_list_row(l, ex_list_selected(l)) : NULL;
    char *q = field_text_of(p->dialog_doc);
    ExSwitchAction act;
    int show_existing_only = ex_settings_get_bool(p->in.settings, "quick_switcher", "show_existing_only");
    int ok = -1;
    if (!q) return;
    if (force_create || (r && r->key && !strcmp(r->key, "\x01create"))) ok = ex_switcher_enter(p->snapshot, q, NULL, show_existing_only && !force_create, &act);
    else if (r && r->id >= 0 && (size_t)r->id < p->nsw) ok = ex_switcher_enter(p->snapshot, q, &p->sw_items[r->id], show_existing_only, &act);
    else if (!r) ok = ex_switcher_enter(p->snapshot, q, NULL, show_existing_only, &act);
    free(q);
    if (ok != 0) { if (p->in.host.message) p->in.host.message(p->in.host.user, "That is not a valid note name."); return; }
    dialog_close(p);
    panels_open_note(p, act.path, (size_t)-1, 0);
}

static void on_switch_activate(ExWidget *w, size_t row, void *u) { (void)w; (void)row; switcher_open_selected(u, 0); }

static int switcher_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExPanels *p = u;
    size_t sel = ex_list_selected(p->dialog_list), n = ex_list_count(p->dialog_list);
    (void)w;
    if (key == EX_KEY_ESCAPE) { dialog_close(p); return 1; }
    if (key == EX_KEY_UP) { if (sel > 0) ex_list_select(p->dialog_list, sel - 1); return 1; }
    if (key == EX_KEY_DOWN) { if (sel + 1 < n) ex_list_select(p->dialog_list, sel + 1); return 1; }
    if (key == EX_KEY_ENTER) { switcher_open_selected(p, (mods & EX_MOD_SHIFT) != 0); return 1; }
    return 0;
}

static unsigned long g_dialog_seen;

void dialog_refresh(ExPanels *p) {
    char *q;
    if (!p->dialog_doc || (p->dkind != D_SWITCHER && p->dkind != D_PALETTE)) return;
    if (ex_doc_version(p->dialog_doc) == g_dialog_seen && p->dialog_list && ex_list_count(p->dialog_list) > 0) return;
    g_dialog_seen = ex_doc_version(p->dialog_doc);
    q = field_text_of(p->dialog_doc);
    if (!q) return;
    ex_list_clear(p->dialog_list);
    if (p->dkind == D_SWITCHER) {
        size_t i;
        int exact = 0;
        p->nsw = p->snapshot ? ex_switcher_query(p->snapshot, q, (const char *const *)p->recent, p->nrecent, 24, p->sw_items) : 0;
        for (i = 0; i < p->nsw; i++) {
            ExListRow r;
            char title[300], detail[320];
            const char *slash = strrchr(p->sw_items[i].path, '/');
            memset(&r, 0, sizeof r);
            note_title(p->sw_items[i].path, title, sizeof title);
            if (p->sw_items[i].via_alias) { snprintf(detail, sizeof detail, "alias of %s", title); r.text = p->sw_items[i].label; }
            else if (p->sw_items[i].is_note && p->sw_items[i].label != NULL && strchr(p->sw_items[i].label, '/')) { snprintf(detail, sizeof detail, "path"); r.text = p->sw_items[i].path; }
            else { snprintf(detail, sizeof detail, "%.*s", slash ? (int)(slash - p->sw_items[i].path) : 0, p->sw_items[i].path); r.text = p->sw_items[i].is_note ? title : p->sw_items[i].path; }
            r.detail = detail[0] ? detail : NULL;
            r.key = p->sw_items[i].path; r.id = (int64_t)i;
            if (!strcmp(r.text, q)) exact = 1;
            ex_list_add(p->dialog_list, &r);
        }
        if (q[0] && !exact && !ex_settings_get_bool(p->in.settings, "quick_switcher", "show_existing_only")) {
            ExListRow r;
            char label[300];
            snprintf(label, sizeof label, "Create \"%s\"", q);
            memset(&r, 0, sizeof r);
            r.text = label; r.key = "\x01create"; r.id = -1; r.muted = 1;
            ex_list_add(p->dialog_list, &r);
        }
        if (!p->snapshot) ex_list_set_empty_text(p->dialog_list, "Reading the vault's names...");
        else ex_list_set_empty_text(p->dialog_list, q[0] ? "No matching notes" : "No recent notes. Type to find one.");
    } else {
        size_t i;
        const char *list = ex_settings_get(p->in.settings, "command_palette", "pinned");
        char *copy = dup_s(list ? list : ""), *tok, *save;
        const char *pins[64];
        size_t np = 0;
        free(p->palette); p->palette = NULL;
        for (tok = copy; tok && np < 64; tok = save) {
            save = strchr(tok, ',');
            if (save) *save++ = 0;
            while (*tok == ' ') tok++;
            if (*tok) pins[np++] = tok;
        }
        p->npalette = ex_palette_query(p->in.commands, q, pins, np, &p->palette);
        for (i = 0; i < p->npalette; i++) {
            ExListRow r;
            char detail[120];
            memset(&r, 0, sizeof r);
            snprintf(detail, sizeof detail, "%s%s%s", p->palette[i].pinned ? "pinned  " : "", p->palette[i].keys, "");
            r.text = p->palette[i].name; r.detail = detail[0] ? detail : NULL; r.key = p->palette[i].id; r.muted = !p->palette[i].available; r.id = (int64_t)i;
            ex_list_add(p->dialog_list, &r);
        }
        free(copy);
        ex_list_set_empty_text(p->dialog_list, "No matching commands");
    }
    ex_list_select(p->dialog_list, 0);
    free(q);
}

void dialog_switcher(ExPanels *p) {
    ExWidget *box, *hint;
    ExListHandlers h;
    dialog_close(p);
    preview_hide(p);
    box = frame(p, "Quick switcher", 600);
    p->dialog_doc = ex_doc_create("", 0);
    p->dialog_field = make_field(p, p->dialog_doc, "Find or create a note", switcher_key, p);
    ex_widget_add(box, p->dialog_field);
    p->dialog_list = ex_widget_list(p->ui, "Notes");
    p->dialog_list->height = ex_size_px(p->ui, 300);
    memset(&h, 0, sizeof h); h.activate = on_switch_activate; h.user = p; h.single_click = 1;
    ex_list_set_handlers(p->dialog_list, &h);
    ex_widget_add(box, p->dialog_list);
    hint = ex_widget_label(p->ui, "Enter opens (or creates the note you typed), Shift+Enter creates, Esc closes");
    hint->text_token = EX_COLOR_TEXT_MUTED; hint->text_scale = 0.8f;
    ex_widget_add(box, hint);
    p->dkind = D_SWITCHER; g_dialog_seen = (unsigned long)-1;
    ex_ui_set_focus(p->ui, p->dialog_field);
    dialog_refresh(p);
}

/* ---- the command palette ---- */

static void palette_run(ExPanels *p) {
    const ExListRow *r = ex_list_row(p->dialog_list, ex_list_selected(p->dialog_list));
    char id[200];
    if (!r || !r->key) return;
    if (r->muted) { if (p->in.host.message) p->in.host.message(p->in.host.user, "That command cannot run now."); return; }
    snprintf(id, sizeof id, "%s", r->key);
    dialog_close(p);
    ex_commands_run(p->in.commands, id);
}

static void palette_toggle_pin(ExPanels *p) {
    const ExListRow *r = ex_list_row(p->dialog_list, ex_list_selected(p->dialog_list));
    const char *cur = ex_settings_get(p->in.settings, "command_palette", "pinned");
    char *copy, *out, *tok, *save;
    size_t n;
    int found = 0;
    if (!r || !r->key) return;
    copy = dup_s(cur ? cur : "");
    out = malloc(strlen(copy) + strlen(r->key) + 4);
    if (!copy || !out) { free(copy); free(out); return; }
    out[0] = 0; n = 0;
    for (tok = copy; tok; tok = save) {
        save = strchr(tok, ',');
        if (save) *save++ = 0;
        while (*tok == ' ') tok++;
        if (!*tok) continue;
        if (!strcmp(tok, r->key)) { found = 1; continue; }
        n += (size_t)sprintf(out + n, "%s%s", n ? "," : "", tok);
    }
    if (!found) sprintf(out + n, "%s%s", n ? "," : "", r->key);
    ex_settings_set(p->in.settings, "command_palette", "pinned", out);
    free(copy); free(out);
    g_dialog_seen = (unsigned long)-1;
    dialog_refresh(p);
}

static int palette_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExPanels *p = u;
    size_t sel = ex_list_selected(p->dialog_list), n = ex_list_count(p->dialog_list);
    (void)w;
    if (key == EX_KEY_ESCAPE) { dialog_close(p); return 1; }
    if (key == EX_KEY_UP) { if (sel > 0) ex_list_select(p->dialog_list, sel - 1); return 1; }
    if (key == EX_KEY_DOWN) { if (sel + 1 < n) ex_list_select(p->dialog_list, sel + 1); return 1; }
    if (key == EX_KEY_ENTER) { palette_run(p); return 1; }
    if (key == EX_KEY_SPACE && (mods & EX_MOD_PRIMARY)) { palette_toggle_pin(p); return 1; }
    return 0;
}

static void on_palette_activate(ExWidget *w, size_t row, void *u) { (void)w; (void)row; palette_run(u); }

void dialog_palette(ExPanels *p) {
    ExWidget *box, *hint;
    ExListHandlers h;
    dialog_close(p);
    preview_hide(p);
    box = frame(p, "Command palette", 640);
    p->dialog_doc = ex_doc_create("", 0);
    p->dialog_field = make_field(p, p->dialog_doc, "Type a command", palette_key, p);
    ex_widget_add(box, p->dialog_field);
    p->dialog_list = ex_widget_list(p->ui, "Commands");
    p->dialog_list->height = ex_size_px(p->ui, 340);
    memset(&h, 0, sizeof h); h.activate = on_palette_activate; h.user = p; h.single_click = 1;
    ex_list_set_handlers(p->dialog_list, &h);
    ex_widget_add(box, p->dialog_list);
    hint = ex_widget_label(p->ui, "Enter runs the command, Ctrl+Space pins or unpins it, Esc closes");
    hint->text_token = EX_COLOR_TEXT_MUTED; hint->text_scale = 0.8f;
    ex_widget_add(box, hint);
    p->dkind = D_PALETTE; g_dialog_seen = (unsigned long)-1;
    ex_ui_set_focus(p->ui, p->dialog_field);
    dialog_refresh(p);
}

/* ---- the page preview ---- */

void preview_hide(ExPanels *p) {
    if (p->preview) { ex_ui_release(p->ui, p->preview); ex_widget_free(p->preview); p->preview = NULL; }
    p->prev_shown = 0; p->prev_pending = 0; p->prev_target[0] = 0;
}

void preview_result(ExPanels *p, QRes *r) {
    ExWidget *box, *view;
    if (r->req != p->prev_req || !r->ok || !r->text || !p->prev_pending) return;
    if (p->preview) { ex_ui_release(p->ui, p->preview); ex_widget_free(p->preview); p->preview = NULL; }
    p->preview = ex_widget_box(p->ui, 1, "Page preview");
    p->preview->width = ex_size_grow(); p->preview->height = ex_size_grow();
    p->preview->pad_x = px_to_units(p, p->prev_x + 12); p->preview->pad_y = px_to_units(p, p->prev_y + 18);
    box = ex_widget_box(p->ui, 1, "Page preview");
    box->width = ex_size_px(p->ui, 460); box->height = ex_size_px(p->ui, 280);
    box->bg_token = EX_COLOR_SURFACE; box->border_token = EX_COLOR_BORDER; box->radius_token = EX_SIZE_RADIUS; box->pad_x = 1; box->pad_y = 0.5f; box->clip = 1;
    view = ex_widget_reading(p->ui, r->text, strlen(r->text), "Page preview");
    if (view) { view->width = ex_size_grow(); view->height = ex_size_grow(); view->focusable = 0; ex_widget_add(box, view); }
    ex_widget_add(p->preview, box);
    p->prev_shown = 1;
}
