#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exhaust_vaultscreen.h"

struct ExVaultScreen {
    ExUi *ui;
    ExVaultManager *mgr;
    ExVaultScreenHooks hooks;
    void *user;
    char *version;
    ExWidget *root;
    ExDoc *name_doc;            /* "My vault": the name for a new vault */
    ExDoc *rename_doc;          /* while renaming */
    ExWidget *name_field, *rename_field;
    char menu_id[40];           /* the vault whose menu is open, or "" */
    char rename_target[40];     /* the vault being renamed, or "" */
    char transfer_from[40];     /* the vault whose settings are being copied (choosing the target), or "" */
    char message[600];
    unsigned long built_revision;
    int dirty;
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

static void set_name(ExWidget *w, const char *name) { free(w->name); w->name = dup_str(name); }

const char *ex_vaultscreen_message(const ExVaultScreen *s) { return s->message; }

static void say(ExVaultScreen *s, const char *text) { snprintf(s->message, sizeof s->message, "%s", text); s->dirty = 1; }

/* The text of a one-line field: up to the first line break. */
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
    all.anchor = 0; all.head = strlen(text);                 /* selected, so typing replaces it */
    ex_doc_set_selections(d, &all, 1);
}

/* ---- actions ---- */

static void do_create(ExVaultScreen *s) {
    char *name = field_text(s->name_doc);
    if (!name) return;
    if (!ex_vault_folder_name_valid(name)) { say(s, ex_vm_message(EX_VM_INVALID_NAME)); free(name); return; }
    free(name);
    if (s->hooks.pick_folder) s->hooks.pick_folder(s->user, EX_PICK_CREATE_PARENT, NULL);
}

static void do_open_folder(ExVaultScreen *s) { if (s->hooks.pick_folder) s->hooks.pick_folder(s->user, EX_PICK_OPEN_FOLDER, NULL); }

static void on_create_click(ExWidget *w, void *u) { (void)w; do_create(u); }
static void on_open_click(ExWidget *w, void *u) { (void)w; do_open_folder(u); }

static int vault_at(ExVaultScreen *s, uint32_t tag, ExVaultInfo *out) { return tag > 0 && ex_vaultmgr_get(s->mgr, tag - 1, out) == 0; }

static void open_this(ExVaultScreen *s, const ExVaultInfo *v) {
    ex_vaultmgr_touch(s->mgr, v->id, (int64_t)time(NULL));
    if (s->hooks.open_vault) s->hooks.open_vault(s->user, v->path);
}

static void on_vault_open(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    char path[1024];
    if (!vault_at(s, w->tag, &v)) return;
    snprintf(path, sizeof path, "%s", v.path);
    ex_vaultmgr_touch(s->mgr, v.id, (int64_t)time(NULL));
    if (s->hooks.open_vault) s->hooks.open_vault(s->user, path);
}

static void on_menu_toggle(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    if (!vault_at(s, w->tag, &v)) return;
    if (strcmp(s->menu_id, v.id) == 0) s->menu_id[0] = 0; else snprintf(s->menu_id, sizeof s->menu_id, "%s", v.id);
    s->dirty = 1;
}

static void on_rename_start(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    if (!vault_at(s, w->tag, &v)) return;
    snprintf(s->rename_target, sizeof s->rename_target, "%s", v.id);
    set_field(s->rename_doc, v.name);
    s->menu_id[0] = 0;
    s->dirty = 1;
}

static void apply_rename(ExVaultScreen *s) {
    char *name = field_text(s->rename_doc);
    ExVmResult r;
    if (!name || !s->rename_target[0]) { free(name); return; }
    r = ex_vaultmgr_rename(s->mgr, s->rename_target, name);
    free(name);
    if (r == EX_VM_OK) { s->rename_target[0] = 0; say(s, "Renamed. Only the name in this list changed; the folder keeps its name."); }
    else say(s, ex_vm_message(r));
}

static void on_rename_ok(ExWidget *w, void *u) { (void)w; apply_rename(u); }
static void on_rename_cancel(ExWidget *w, void *u) { ExVaultScreen *s = u; (void)w; s->rename_target[0] = 0; s->dirty = 1; }

static void on_move_start(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    char id[40];
    if (!vault_at(s, w->tag, &v)) return;
    snprintf(id, sizeof id, "%s", v.id);
    s->menu_id[0] = 0;
    s->dirty = 1;
    if (s->hooks.pick_folder) s->hooks.pick_folder(s->user, EX_PICK_MOVE_TARGET, id);
}

static void on_transfer_start(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    if (!vault_at(s, w->tag, &v)) return;
    snprintf(s->transfer_from, sizeof s->transfer_from, "%s", v.id);
    s->menu_id[0] = 0;
    say(s, "Choose the vault to copy these settings to. Settings it already has are kept as they are.");
}

static void on_transfer_cancel(ExWidget *w, void *u) { ExVaultScreen *s = u; (void)w; s->transfer_from[0] = 0; say(s, ""); }

static void on_transfer_here(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    ExTransferResult res;
    ExVmResult r;
    char msg[300], to[48];
    if (!vault_at(s, w->tag, &v)) return;
    snprintf(to, sizeof to, "%s", v.id);
    r = ex_vaultmgr_transfer_settings(s->mgr, s->transfer_from, to, &res);
    s->transfer_from[0] = 0;
    if (r == EX_VM_OK) {
        snprintf(msg, sizeof msg, "Copied settings: %d added, %d already set and kept, %d theme and snippet files copied.", res.keys_added, res.skipped, res.files_copied);
        say(s, msg);
    } else say(s, ex_vm_message(r));
}

static void on_remove(ExWidget *w, void *u) {
    ExVaultScreen *s = u;
    ExVaultInfo v;
    char msg[500], name[200];
    if (!vault_at(s, w->tag, &v)) return;
    snprintf(name, sizeof name, "%s", v.name);
    if (ex_vaultmgr_remove(s->mgr, v.id) == EX_VM_OK) {
        snprintf(msg, sizeof msg, "Removed %s from the list. Its files were not touched.", name);
        s->menu_id[0] = 0;
        say(s, msg);
    }
}

void ex_vaultscreen_folder_chosen(ExVaultScreen *s, ExPickPurpose purpose, const char *vault_id, const char *path) {
    ExVmResult r;
    const char *id = NULL;
    if (!path) return;                                       /* cancelled */
    if (purpose == EX_PICK_OPEN_FOLDER) {
        r = ex_vaultmgr_open_folder(s->mgr, path, &id);
        if (r == EX_VM_OK && id) {
            ExVaultInfo v;
            size_t i;
            for (i = 0; i < ex_vaultmgr_count(s->mgr); i++) if (ex_vaultmgr_get(s->mgr, i, &v) == 0 && strcmp(v.id, id) == 0) { open_this(s, &v); break; }
        } else say(s, ex_vm_message(r));
    } else if (purpose == EX_PICK_CREATE_PARENT) {
        char *name = field_text(s->name_doc);
        r = name ? ex_vaultmgr_create_vault(s->mgr, path, name, &id) : EX_VM_INVALID_NAME;
        free(name);
        if (r == EX_VM_OK && id) {
            ExVaultInfo v;
            size_t i;
            for (i = 0; i < ex_vaultmgr_count(s->mgr); i++) if (ex_vaultmgr_get(s->mgr, i, &v) == 0 && strcmp(v.id, id) == 0) { open_this(s, &v); break; }
        } else say(s, ex_vm_message(r));
    } else if (purpose == EX_PICK_MOVE_TARGET && vault_id) {
        r = ex_vaultmgr_move(s->mgr, vault_id, path, NULL);
        say(s, r == EX_VM_OK ? "Moved. Its recovery data and session moved with it." : ex_vm_message(r));
    }
    s->dirty = 1;
}

/* ---- keys in the fields ---- */

static int name_key(ExWidget *w, ExKey key, int mods, void *u) {
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { do_create(u); return 1; }
    return 0;
}

static int rename_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExVaultScreen *s = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { apply_rename(s); return 1; }
    if (key == EX_KEY_ESCAPE) { s->rename_target[0] = 0; s->dirty = 1; return 1; }
    return 0;
}

/* ---- building ---- */

static ExWidget *label(ExVaultScreen *s, const char *text, int muted, float scale) {
    ExWidget *l = ex_widget_label(s->ui, text);
    if (!l) return NULL;
    if (muted) l->text_token = EX_COLOR_TEXT_MUTED;
    l->text_scale = scale;
    return l;
}

static ExWidget *button(ExVaultScreen *s, const char *text, const char *name, ExClickFn fn, uint32_t tag, int primary) {
    ExWidget *b = ex_widget_button(s->ui, text, fn, s);
    if (!b) return NULL;
    if (name) set_name(b, name);
    b->tag = tag;
    if (primary) { b->bg_token = EX_COLOR_ACCENT; b->text_token = EX_COLOR_BACKGROUND; }
    return b;
}

static ExWidget *field(ExVaultScreen *s, ExDoc *doc, const char *name, int (*key)(ExWidget *, ExKey, int, void *)) {
    ExWidget *f = ex_widget_editor(s->ui, doc, name);
    if (!f) return NULL;
    f->bg_token = EX_COLOR_BACKGROUND;
    f->border_token = EX_COLOR_BORDER;
    f->radius_token = EX_SIZE_RADIUS;
    f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f);
    f->width = ex_size_grow();
    f->on_key = key;
    f->on_key_user = s;
    return f;
}

static ExWidget *card_row(ExVaultScreen *s, const char *title, const char *desc, ExWidget *action, int last) {
    ExWidget *row = ex_widget_box(s->ui, 0, title), *text = ex_widget_box(s->ui, 1, ""), *t;
    row->width = ex_size_grow();
    row->pad_x = 1.5f; row->pad_y = 1.25f; row->gap = 1;
    if (!last) row->border_token = EX_COLOR_BORDER;
    text->width = ex_size_grow();
    t = label(s, title, 0, 1.0f); if (t) ex_widget_add(text, t);
    t = label(s, desc, 1, 0.9f); if (t) ex_widget_add(text, t);
    ex_widget_add(row, text);
    if (action) ex_widget_add(row, action);
    return row;
}

/* Long text is shortened (the full text stays in the accessible name) so a row never grows wider than its column. */
static void shorten(const char *text, size_t max_bytes, int keep_end, char *out, size_t cap) {
    size_t n = strlen(text), start, len;
    if (n <= max_bytes || max_bytes < 4) { snprintf(out, cap, "%s", text); return; }
    if (keep_end) {                                          /* ...tail of a path */
        start = n - (max_bytes - 3);
        while (start < n && ((unsigned char)text[start] & 0xC0) == 0x80) start++;     /* not inside a character */
        snprintf(out, cap, "\xE2\x80\xA6%s", text + start);
    } else {                                                 /* head of a name... */
        len = max_bytes - 3;
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) len--;
        snprintf(out, cap, "%.*s\xE2\x80\xA6", (int)len, text);
    }
}

static ExWidget *build_list(ExVaultScreen *s) {
    ExWidget *col = ex_widget_box(s->ui, 1, "Vaults"), *head;
    size_t i, n = ex_vaultmgr_count(s->mgr);
    col->width = ex_size_token(EX_SIZE_SPACING, 40.0f);
    col->height = ex_size_grow();
    col->bg_token = EX_COLOR_BACKGROUND_ALT;
    col->pad_x = 1.0f; col->pad_y = 1.5f; col->gap = 0.5f;
    head = label(s, n ? "Your vaults" : "No vaults yet", 1, 0.85f);
    if (head) ex_widget_add(col, head);
    if (s->transfer_from[0]) {
        ExWidget *c = button(s, "Cancel", "Cancel copying settings", on_transfer_cancel, 0, 0);
        if (c) ex_widget_add(col, c);
    }
    for (i = 0; i < n; i++) {
        ExVaultInfo v;
        ExWidget *row, *text, *t, *menu;
        char nm[300];
        if (ex_vaultmgr_get(s->mgr, i, &v) != 0) continue;
        row = ex_widget_box(s->ui, 0, "Vault");
        row->width = ex_size_grow(); row->gap = 0.5f; row->pad_y = 0.5f;
        text = ex_widget_box(s->ui, 1, "");
        text->width = ex_size_grow();
        if (strcmp(s->rename_target, v.id) == 0) {
            ExWidget *bar = ex_widget_box(s->ui, 0, "Rename"), *ok, *cancel;
            bar->width = ex_size_grow(); bar->gap = 0.5f;
            s->rename_field = field(s, s->rename_doc, "New name for the vault", rename_key);
            if (s->rename_field) ex_widget_add(bar, s->rename_field);
            ok = button(s, "OK", "Rename the vault", on_rename_ok, (uint32_t)(i + 1), 1);
            cancel = button(s, "Cancel", "Cancel renaming", on_rename_cancel, (uint32_t)(i + 1), 0);
            if (ok) ex_widget_add(bar, ok);
            if (cancel) ex_widget_add(bar, cancel);
            ex_widget_add(text, bar);
        } else {
            char shown[80];
            shorten(v.name, 30, 0, shown, sizeof shown);
            t = label(s, shown, 0, 1.1f);
            if (t) { snprintf(nm, sizeof nm, "Vault %s", v.name); set_name(t, nm); ex_widget_add(text, t); }
        }
        {
            char shown[96];
            shorten(v.path, v.exists ? 34 : 22, 1, shown, sizeof shown);
            snprintf(nm, sizeof nm, "%s%s", shown, v.exists ? "" : "  (folder missing)");
            t = label(s, nm, 1, 0.85f);
            if (t) { char full[1100]; snprintf(full, sizeof full, "Folder %s%s", v.path, v.exists ? "" : " (missing)"); set_name(t, full); ex_widget_add(text, t); }
        }
        ex_widget_add(row, text);
        if (s->transfer_from[0]) {                            /* choosing where the settings go */
            if (strcmp(s->transfer_from, v.id) != 0 && v.exists) {
                ExWidget *here = button(s, "Copy here", NULL, on_transfer_here, (uint32_t)(i + 1), 1);
                if (here) { snprintf(nm, sizeof nm, "Copy settings to vault %s", v.name); set_name(here, nm); ex_widget_add(row, here); }
            }
            ex_widget_add(col, row);
            continue;
        }
        if (v.exists) {
            ExWidget *open = button(s, "Open", NULL, on_vault_open, (uint32_t)(i + 1), 0);
            if (open) { snprintf(nm, sizeof nm, "Open vault %s", v.name); set_name(open, nm); ex_widget_add(row, open); }
        }
        menu = button(s, "\xE2\x8B\xAE", NULL, on_menu_toggle, (uint32_t)(i + 1), 0);      /* a vertical ellipsis */
        if (menu) {
            snprintf(nm, sizeof nm, "Menu for vault %s", v.name);
            set_name(menu, nm);
            menu->bg_token = -1; menu->border_token = -1; menu->pad_x = 0.75f;
            ex_widget_add(row, menu);
        }
        ex_widget_add(col, row);
        if (strcmp(s->menu_id, v.id) == 0) {                  /* the menu: shown inline under its vault */
            ExWidget *m = ex_widget_box(s->ui, 0, "Vault menu"), *b;
            m->width = ex_size_grow(); m->gap = 0.5f; m->pad_x = 0.5f;
            b = button(s, "Rename", "Rename this vault in the list", on_rename_start, (uint32_t)(i + 1), 0); if (b) ex_widget_add(m, b);
            b = button(s, "Move", "Move this vault's folder", on_move_start, (uint32_t)(i + 1), 0); if (b && v.exists) ex_widget_add(m, b); else ex_widget_free(b);
            b = button(s, "Copy settings to...", "Copy this vault's settings to another vault", on_transfer_start, (uint32_t)(i + 1), 0); if (b && v.exists && n > 1) ex_widget_add(m, b); else ex_widget_free(b);
            b = button(s, "Remove from list", "Remove this vault from the list", on_remove, (uint32_t)(i + 1), 0); if (b) ex_widget_add(m, b);
            ex_widget_add(col, m);
        }
    }
    return col;
}

static ExWidget *build_right(ExVaultScreen *s) {
    ExWidget *col = ex_widget_box(s->ui, 1, "Welcome"), *card, *t, *bar, *b, *stub;
    col->width = ex_size_grow(); col->height = ex_size_grow();
    col->pad_x = 4; col->pad_y = 4; col->gap = 1.5f;
    t = label(s, "Exhaust", 0, 2.4f); if (t) { t->bold = 1; ex_widget_add(col, t); }
    if (s->version) { char v[80]; snprintf(v, sizeof v, "Version %s", s->version); t = label(s, v, 1, 1.0f); if (t) ex_widget_add(col, t); }
    card = ex_widget_box(s->ui, 1, "Open or create a vault");
    card->width = ex_size_grow(); card->bg_token = EX_COLOR_SURFACE; card->radius_token = EX_SIZE_RADIUS_LARGE; card->pad_x = 0.5f; card->pad_y = 0.5f;
    /* create: a name field and the button */
    bar = ex_widget_box(s->ui, 0, "Create");
    bar->gap = 0.75f;
    s->name_field = field(s, s->name_doc, "Name of the new vault", name_key);
    if (s->name_field) { s->name_field->width = ex_size_token(EX_SIZE_SPACING, 20.0f); ex_widget_add(bar, s->name_field); }
    b = button(s, "Create", "Create new vault", on_create_click, 0, 1);
    if (b) ex_widget_add(bar, b);
    ex_widget_add(card, card_row(s, "Create new vault", "Create a new vault in a folder you choose.", bar, 0));
    b = button(s, "Open", "Open folder as vault", on_open_click, 0, 0);
    ex_widget_add(card, card_row(s, "Open folder as vault", "Choose an existing folder of Markdown files.", b, 0));
    /* the synced vault stub: visible, labelled, not operable */
    stub = ex_widget_label(s->ui, "Not available yet");
    if (stub) { stub->text_token = EX_COLOR_TEXT_FAINT; set_name(stub, "Open synced vault: not available yet"); }
    ex_widget_add(card, card_row(s, "Open synced vault (optional, later)", "Set up a synced vault from a remote vault. Sync is a later, optional feature.", stub, 1));
    ex_widget_add(col, card);
    t = label(s, s->message, 0, 0.95f);
    if (t) { set_name(t, s->message[0] ? s->message : "Status"); t->text_token = EX_COLOR_TEXT_MUTED; ex_widget_add(col, t); }
    return col;
}

static void clear_children(ExVaultScreen *s) {
    ExWidget *c, *next;
    for (c = s->root->first_child; c; c = next) {
        next = c->next;
        ex_ui_release(s->ui, c);
        ex_widget_remove(s->root, c);
        ex_widget_free(c);
    }
    s->name_field = s->rename_field = NULL;
}

void ex_vaultscreen_update(ExVaultScreen *s) {
    ExWidget *prev_focus = ex_ui_focus(s->ui);
    int had_name = prev_focus && prev_focus == s->name_field, had_rename = prev_focus && prev_focus == s->rename_field;
    if (!s->dirty && s->built_revision == ex_vaultmgr_revision(s->mgr) && s->root->first_child) return;
    clear_children(s);
    ex_widget_add(s->root, build_list(s));
    ex_widget_add(s->root, build_right(s));
    s->built_revision = ex_vaultmgr_revision(s->mgr);
    s->dirty = 0;
    if (s->rename_target[0] && s->rename_field && !had_rename) ex_ui_set_focus(s->ui, s->rename_field);    /* typing replaces the name at once */
    else if (had_name && s->name_field) ex_ui_set_focus(s->ui, s->name_field);
}

ExVaultScreen *ex_vaultscreen_create(ExUi *ui, ExVaultManager *mgr, const ExVaultScreenHooks *hooks, void *user, const char *version) {
    ExVaultScreen *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->ui = ui; s->mgr = mgr; s->user = user;
    if (hooks) s->hooks = *hooks;
    s->version = version ? dup_str(version) : NULL;
    s->name_doc = ex_doc_create("My vault", 8);
    s->rename_doc = ex_doc_create("", 0);
    s->root = ex_widget_box(ui, 0, "Vault manager");
    if (!s->name_doc || !s->rename_doc || !s->root) { ex_vaultscreen_free(s); return NULL; }
    s->root->width = ex_size_grow(); s->root->height = ex_size_grow();
    s->root->bg_token = EX_COLOR_BACKGROUND;
    s->root->role = EX_ROLE_WINDOW;
    s->built_revision = (unsigned long)-1;
    return s;
}

ExWidget *ex_vaultscreen_root(ExVaultScreen *s) { return s->root; }

void ex_vaultscreen_free(ExVaultScreen *s) {
    if (!s) return;
    if (s->root && !s->root->parent) {
        ex_ui_release(s->ui, s->root);                       /* the focus, the pressed widget and the like must not point into the freed tree */
        ex_widget_free(s->root);
    }
    ex_doc_free(s->name_doc);
    ex_doc_free(s->rename_doc);
    free(s->version);
    free(s);
}
