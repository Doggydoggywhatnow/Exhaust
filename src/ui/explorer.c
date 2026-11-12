/* The explorer sidebar: toolbar, file tree and the vault button (layout of the
 * screenshots; icons come later, for now the buttons carry short labels). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ui.h"

typedef struct Explorer {
    ExVault *vault;
    ExWidget *tree;
    void (*on_open)(ExWidget *w, const char *rel, void *user);
    void *user;
    void (*on_new)(ExWidget *w, const char *parent_rel, void *user);
    void *new_user;
    void (*on_vault)(ExWidget *w, void *user);   /* the vault button at the bottom */
    void *vault_user;
} Explorer;

static void free_explorer(void *p) { free(p); }

/* The folder new items go into: the selected folder, or the folder of the selected file. */
static char *target_folder(Explorer *e) {
    const ExNode *n = ex_tree_selected(e->tree);
    const char *rel = "";
    char *copy, *slash;
    if (n) {
        if (n->is_dir) rel = n->rel;
        else if (n->parent) rel = n->parent->rel;
    }
    copy = malloc(strlen(rel) + 1);
    if (copy) strcpy(copy, rel);
    (void)slash;
    return copy;
}

static void new_item(ExWidget *button, void *user, int folder) {
    Explorer *e = user;
    char *dir = target_folder(e), *rel = NULL;
    int k;
    (void)button;
    if (!dir) return;
    if (!folder && e->on_new) { e->on_new(e->tree, dir, e->new_user); free(dir); return; }   /* the note is not created until something is typed */
    for (k = 1; k < 1000; k++) {
        char name[64];
        if (folder) snprintf(name, sizeof name, k == 1 ? "Untitled folder" : "Untitled folder %d", k);
        else snprintf(name, sizeof name, k == 1 ? "Untitled" : "Untitled %d", k);
        if ((folder ? ex_vault_create_folder(e->vault, dir, name, &rel) : ex_vault_create_note(e->vault, dir, name, &rel)) == EX_VAULT_OK) break;
        rel = NULL;
    }
    if (rel) {
        ex_tree_select_path(e->tree, rel);
        if (!folder && e->on_open) e->on_open(e->tree, rel, e->user);
        free(rel);
    }
    free(dir);
}

static void on_vault_button(ExWidget *w, void *u) {
    Explorer *e = u;
    if (e->on_vault) e->on_vault(w, e->vault_user);
}

static void on_new_note(ExWidget *w, void *u) { new_item(w, u, 0); }
static void on_new_folder(ExWidget *w, void *u) { new_item(w, u, 1); }

static void on_toggle_all(ExWidget *w, void *u) {
    Explorer *e = u;
    size_t i, rows = ex_vault_row_count(e->vault);
    int any_closed = 0;
    (void)w;
    for (i = 0; i < rows; i++) { const ExNode *n = ex_vault_row(e->vault, i, NULL); if (n && n->is_dir && !n->expanded) any_closed = 1; }
    if (any_closed) ex_vault_expand_all(e->vault); else ex_vault_collapse_all(e->vault);
}

static void on_sort(ExWidget *w, void *u) {
    Explorer *e = u;
    ExVaultOptions o;
    (void)w;
    ex_vault_get_options(e->vault, &o);
    /* name A-Z, name Z-A, modified new-old, modified old-new, created new-old, created old-new */
    if (o.sort_by == EX_SORT_NAME && !o.sort_descending) o.sort_descending = 1;
    else if (o.sort_by == EX_SORT_NAME) { o.sort_by = EX_SORT_MODIFIED; o.sort_descending = 1; }
    else if (o.sort_by == EX_SORT_MODIFIED && o.sort_descending) o.sort_descending = 0;
    else if (o.sort_by == EX_SORT_MODIFIED) { o.sort_by = EX_SORT_CREATED; o.sort_descending = 1; }
    else if (o.sort_descending) o.sort_descending = 0;
    else { o.sort_by = EX_SORT_NAME; o.sort_descending = 0; }
    ex_vault_set_options(e->vault, &o); /* only what is shown changes; no file is touched */
}

ExWidget *ex_explorer_sidebar(ExUi *ui, ExVault *vault, void (*on_open)(ExWidget *w, const char *rel, void *user), void *user,
                              ExWidget **tree_out) {
    Explorer *e = calloc(1, sizeof *e);
    ExWidget *side, *bar, *vault_row, *vault_button, *b;
    char label[200];
    if (!e) return NULL;
    side = ex_widget_box(ui, 1, "File explorer");
    side->width = ex_size_token(EX_SIZE_SPACING, 32.5f); /* 260 px at 100% */
    side->height = ex_size_grow();
    side->bg_token = EX_COLOR_BACKGROUND_ALT;
    side->owned = e;
    side->free_owned = free_explorer;
    e->vault = vault;
    e->on_open = on_open;
    e->user = user;

    bar = ex_widget_box(ui, 0, "Explorer toolbar");
    bar->width = ex_size_grow();
    bar->pad_x = 0.5f; bar->pad_y = 0.5f; bar->gap = 0.5f;
    b = ex_widget_button(ui, "New note", on_new_note, e);
    b->text_scale = 0.8f; b->pad_x = 0.75f; b->pad_y = 0.4f; ex_widget_add(bar, b);
    b = ex_widget_button(ui, "New folder", on_new_folder, e);
    b->text_scale = 0.8f; b->pad_x = 0.75f; b->pad_y = 0.4f; ex_widget_add(bar, b);
    b = ex_widget_button(ui, "Sort", on_sort, e);
    b->text_scale = 0.8f; b->pad_x = 0.75f; b->pad_y = 0.4f; ex_widget_add(bar, b);
    b = ex_widget_button(ui, "Expand all", on_toggle_all, e);
    b->text_scale = 0.8f; b->pad_x = 0.75f; b->pad_y = 0.4f; ex_widget_add(bar, b);
    ex_widget_add(side, bar);

    e->tree = ex_widget_tree(ui, vault, "Files", on_open, user);
    ex_widget_add(side, e->tree);

    vault_row = ex_widget_box(ui, 0, "Vault");
    vault_row->width = ex_size_grow();
    vault_row->pad_x = 0.75f; vault_row->pad_y = 0.5f;
    snprintf(label, sizeof label, "%s", ex_vault_name(vault));
    vault_button = ex_widget_button(ui, label, on_vault_button, e);   /* opens the vault manager (the application decides how) */
    vault_button->bg_token = -1; vault_button->border_token = -1;
    vault_button->name = realloc(vault_button->name, strlen(label) + 32);
    if (vault_button->name) snprintf(vault_button->name, strlen(label) + 32, "Vault: %s", label);
    vault_button->text_scale = 0.9f;
    ex_widget_add(vault_row, vault_button);
    ex_widget_add(side, vault_row);
    if (tree_out) *tree_out = e->tree;
    return side;
}

void ex_explorer_set_new_note_handler(ExWidget *sidebar, void (*fn)(ExWidget *w, const char *parent_rel, void *user), void *user) {
    Explorer *e = sidebar ? sidebar->owned : NULL;
    if (!e) return;
    e->on_new = fn;
    e->new_user = user;
}

void ex_explorer_set_vault_handler(ExWidget *sidebar, void (*fn)(ExWidget *w, void *user), void *user) {
    Explorer *e = sidebar ? sidebar->owned : NULL;
    if (!e) return;
    e->on_vault = fn;
    e->vault_user = user;
}
