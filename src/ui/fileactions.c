/* The actions on files and folders of the explorer (sub-slice 4d): the menu (right click on a row, or the More button), and
 * what each entry does. Renaming and moving go through the link-safe flow of panels.c; the rest is done by the application
 * through ExPanelsHost.file_op, because it owns the tabs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "panels_internal.h"

enum { A_OPEN_TAB, A_NEW_NOTE, A_NEW_FOLDER, A_RENAME, A_MOVE, A_DELETE, A_COPY_REL, A_COPY_ABS, A_DEFAULT, A_SHOW, A_COUNT };
static const char *const LABELS[A_COUNT] = {
    "Open in new tab", "New note in this folder", "New folder in this folder", "Rename...", "Move to folder...", "Delete",
    "Copy path from the vault", "Copy path from the system root", "Open in default app", "Show in file manager"
};

static int op(ExPanels *p, int what, const char *path, const char *arg) {
    return p->in.host.file_op ? p->in.host.file_op(p->in.host.user, what, path, arg) : -1;
}

static void say(ExPanels *p, const char *text) { if (p->in.host.message) p->in.host.message(p->in.host.user, text); }

/* "Untitled", "Untitled 2" ... that does not exist in folder. */
static void free_name(ExPanels *p, const char *folder, const char *base, const char *ext, char *out, size_t cap) {
    int k;
    for (k = 1; k < 1000; k++) {
        char rel[700], abs[1400];
        ExPathInfo info;
        if (k == 1) snprintf(rel, sizeof rel, "%s%s%s%s", folder, folder[0] ? "/" : "", base, ext);
        else snprintf(rel, sizeof rel, "%s%s%s %d%s", folder, folder[0] ? "/" : "", base, k, ext);
        snprintf(abs, sizeof abs, "%s/%s", p->root, rel);
        if (ex_platform_path_info(abs, &info) != 0) { snprintf(out, cap, "%s", rel); return; }
    }
    snprintf(out, cap, "%s%s%s%s", folder, folder[0] ? "/" : "", base, ext);
}

static void delete_confirmed(ExPanels *p, int button, void *arg) {
    const char *path = arg;
    if (button != 1) return;
    if (op(p, EX_FILEOP_DELETE, path, NULL) == 0) {
        say(p, "Deleted.");
        ex_panels_tree_changed(p);
    } else say(p, "The file could not be deleted.");
}

static void do_delete(ExPanels *p, const char *given) {
    const char *path;
    const char *mode = ex_settings_get(p->in.settings, "files", "trash_mode");
    char msg[700];
    free(p->dialog_arg_text);                                           /* the dialog outlives the caller's string */
    p->dialog_arg_text = dup_s(given);
    path = p->dialog_arg_text;
    if (!path) return;
    if (!ex_settings_get_bool(p->in.settings, "files", "confirm_delete")) { delete_confirmed(p, 1, (void *)path); return; }
    snprintf(msg, sizeof msg, "Delete \"%s\"?\n%s", path,
             mode && !strcmp(mode, "permanent") ? "It is deleted for good and cannot be brought back." :
             mode && !strcmp(mode, "vault_trash") ? "It is moved to the .trash folder of the vault." : "It is moved to the system's trash.");
    dialog_confirm(p, "Delete", msg, "Delete", "Cancel", NULL, delete_confirmed, (void *)path);
}

static void new_folder_done(ExPanels *p, const char *name, void *arg) {
    if (!name[0]) return;
    if (op(p, EX_FILEOP_NEW_FOLDER, arg, name) == 0) ex_panels_tree_changed(p);
    else say(p, "That folder could not be created.");
}

static void action_done(ExPanels *p, size_t index, const char *key, void *arg) {
    char path[700], rel[700];                        /* a copy: the actions below replace dialog_arg_text, which arg points into */
    int a, is_dir = 0;
    (void)index;
    snprintf(path, sizeof path, "%s", (const char *)arg);
    for (a = 0; a < A_COUNT; a++) if (!strcmp(key, LABELS[a])) break;
    if (a == A_COUNT) return;
    {
        char abs[1400];
        ExPathInfo info;
        snprintf(abs, sizeof abs, "%s/%s", p->root, path);
        is_dir = !path[0] || (ex_platform_path_info(abs, &info) == 0 && info.is_dir);
    }
    switch (a) {
    case A_OPEN_TAB: panels_open_note(p, path, (size_t)-1, 1); break;
    case A_NEW_NOTE: {
        free_name(p, path, "Untitled", ".md", rel, sizeof rel);
        panels_open_note(p, rel, (size_t)-1, 1);
        ex_panels_tree_changed(p);
        break;
    }
    case A_NEW_FOLDER: {
        free(p->dialog_arg_text);
        p->dialog_arg_text = dup_s(path);
        dialog_prompt(p, "New folder", "Name of the folder", "", new_folder_done, p->dialog_arg_text);
        break;
    }
    case A_RENAME: ex_panels_rename_file(p, path); break;
    case A_MOVE: ex_panels_move_file_prompt(p, path); break;
    case A_DELETE: do_delete(p, path); break;
    case A_COPY_REL: op(p, EX_FILEOP_COPY_REL, path, NULL); say(p, "Path copied."); break;
    case A_COPY_ABS: op(p, EX_FILEOP_COPY_ABS, path, NULL); say(p, "Path copied."); break;
    case A_DEFAULT: if (op(p, EX_FILEOP_OPEN_DEFAULT, path, NULL) != 0) say(p, "It could not be opened."); break;
    case A_SHOW: if (op(p, EX_FILEOP_SHOW, path, NULL) != 0) say(p, "The file manager could not be opened."); break;
    }
    (void)is_dir;
}

/* The menu for a file or folder of the vault ("" is the vault folder itself). */
void ex_panels_file_menu(ExPanels *p, const char *path) {
    const char *items[A_COUNT];
    size_t n = 0;
    char abs[1400], title[320];
    ExPathInfo info;
    int is_dir, is_root = !path[0];
    snprintf(abs, sizeof abs, "%s/%s", p->root, path);
    is_dir = is_root || (ex_platform_path_info(abs, &info) == 0 && info.is_dir);
    if (!is_dir) items[n++] = LABELS[A_OPEN_TAB];
    if (is_dir) { items[n++] = LABELS[A_NEW_NOTE]; items[n++] = LABELS[A_NEW_FOLDER]; }
    if (!is_root) { items[n++] = LABELS[A_RENAME]; items[n++] = LABELS[A_MOVE]; items[n++] = LABELS[A_DELETE]; items[n++] = LABELS[A_COPY_REL]; }
    items[n++] = LABELS[A_COPY_ABS];
    if (!is_dir) items[n++] = LABELS[A_DEFAULT];
    items[n++] = LABELS[A_SHOW];
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(path);
    snprintf(title, sizeof title, "%s", is_root ? "Vault" : path);
    dialog_choice(p, title, items, n, 0, action_done, p->dialog_arg_text);
}

/* The context menu: a right click at window position (x, y). Returns 1 if it opened one. */
int ex_panels_context_menu(ExPanels *p, ExWidget *root, float x, float y) {
    ExWidget *w = ex_ui_widget_at(root, x, y);
    const ExNode *n;
    while (w && w->type != EX_WIDGET_TREE) w = w->parent;
    if (!w || ex_panels_modal(p)) return 0;
    n = ex_tree_node_at(p->ui, w, y, 1);
    ex_panels_file_menu(p, n ? n->rel : "");
    return 1;
}

/* The More button of the toolbar: the menu of the selected row, or of the vault. */
static void on_more(ExWidget *tree, const char *rel, void *user) { (void)tree; ex_panels_file_menu(user, rel ? rel : ""); }

/* A row dragged onto a folder: the same flow as Move to folder (the link update included). */
static void on_drop(ExWidget *tree, const char *from, const char *to_folder, void *user) {
    ExPanels *p = user;
    (void)tree;
    panels_move_done(p, to_folder, (void *)from);
}

void fileactions_init(ExPanels *p) {
    ExWidget *c;
    if (!p->in.files) return;
    ex_explorer_set_menu_handler(p->in.files, on_more, p);
    for (c = p->in.files->first_child; c; c = c->next)
        if (c->type == EX_WIDGET_TREE) ex_tree_set_drop_handler(c, on_drop, p);
}
