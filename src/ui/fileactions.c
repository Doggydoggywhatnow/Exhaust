/* The actions on files and folders of the explorer (sub-slice 4d): the menu (right click on a row, or the More button), and
 * what each entry does. Renaming and moving go through the link-safe flow of panels.c; the rest is done by the application
 * through ExPanelsHost.file_op, because it owns the tabs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "panels_internal.h"

enum { A_OPEN_TAB, A_NEW_NOTE, A_NEW_FOLDER, A_RENAME, A_MOVE, A_DELETE, A_COPY_REL, A_COPY_ABS, A_DEFAULT, A_SHOW, A_COPY_FILE, A_BOOKMARK, A_COPY_PATH_MENU, A_COUNT };

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

static void action_done(ExPanels *p, int a, void *arg) {
    char path[700], rel[700];
    (void)rel;
    snprintf(path, sizeof path, "%s", (const char *)arg);                      /* a copy: the actions below replace dialog_arg_text, which arg points into */
    switch (a) {
    case A_OPEN_TAB: panels_open_note(p, path, (size_t)-1, 1); break;
    case A_NEW_NOTE: {
        free_name(p, path, "Untitled", ".md", rel, sizeof rel);
        panels_open_note(p, rel, (size_t)-1, 1);
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
    case A_COPY_FILE: if (op(p, EX_FILEOP_COPY_FILE, path, NULL) == 0) { say(p, "A copy was made."); ex_panels_tree_changed(p); } else say(p, "The copy could not be made."); break;
    case A_BOOKMARK: {
        ExBookmark b;
        memset(&b, 0, sizeof b);
        b.type = EX_BM_NOTE; b.path = path;
        ex_bookmarks_add(p->bookmarks, &b, 0);
        views_bookmarks_changed(p);
        say(p, "Note bookmarked.");
        break;
    }
    }
}

/* "Copy path": its own small menu next to the first one. */
static void copy_path_menu(ExPanels *p, float x, float y, const char *path) {
    static const PopupItem items[] = {
        {"Path from the vault folder", EX_ICON_CLIPBOARD, A_COPY_REL, 0, 0, 0},
        {"Path from the system root", EX_ICON_CLIPBOARD, A_COPY_ABS, 0, 0, 0},
    };
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(path);
    dialog_popup(p, x, y, items, 2, action_done, p->dialog_arg_text);
}

static float g_menu_x, g_menu_y;
static void menu_chosen(ExPanels *p, int a, void *arg) {
    if (a == A_COPY_PATH_MENU) { char path[700]; snprintf(path, sizeof path, "%s", (const char *)arg); copy_path_menu(p, g_menu_x + 262.0f, g_menu_y + 120.0f, path); return; }
    action_done(p, a, arg);
}

/* The menu for a file or folder of the vault ("" is the vault folder itself), at a window position. */
void ex_panels_file_menu_at(ExPanels *p, const char *path, float x, float y) {
    PopupItem items[16];
    size_t n = 0;
    char abs[1400];
    ExPathInfo info;
    int is_dir, is_root = !path[0];
    snprintf(abs, sizeof abs, "%s/%s", p->root, path);
    is_dir = is_root || (ex_platform_path_info(abs, &info) == 0 && info.is_dir);
#define ITEM(l, ic, id, dg, sep, sub) do { PopupItem it_ = {l, ic, id, dg, sep, sub}; items[n++] = it_; } while (0)
    if (!is_dir) ITEM("Open in new tab", EX_ICON_NEW_NOTE, A_OPEN_TAB, 0, 0, 0);
    if (is_dir) { ITEM("New note in this folder", EX_ICON_NEW_NOTE, A_NEW_NOTE, 0, 0, 0); ITEM("New folder in this folder", EX_ICON_NEW_FOLDER, A_NEW_FOLDER, 0, 0, 0); }
    if (!is_root) {
        if (!is_dir) ITEM("Make a copy", EX_ICON_COPY, A_COPY_FILE, 0, 1, 0);
        ITEM("Move to folder...", EX_ICON_MOVE, A_MOVE, 0, is_dir ? 1 : 0, 0);
        if (!is_dir) ITEM("Bookmark", EX_ICON_BOOKMARK, A_BOOKMARK, 0, 0, 0);
    }
    ITEM("Copy path", EX_ICON_CLIPBOARD, A_COPY_PATH_MENU, 0, 1, 1);
    if (!is_dir) ITEM("Open in default app", EX_ICON_EXTERNAL, A_DEFAULT, 0, 1, 0);
    ITEM("Show in file manager", EX_ICON_EXTERNAL, A_SHOW, 0, is_dir ? 1 : 0, 0);
    if (!is_root) { ITEM("Rename...", EX_ICON_MODE, A_RENAME, 0, 1, 0); ITEM("Delete", EX_ICON_TRASH, A_DELETE, 1, 0, 0); }
#undef ITEM
    g_menu_x = x; g_menu_y = y;
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(path);
    dialog_popup(p, x, y, items, n, menu_chosen, p->dialog_arg_text);
}

void ex_panels_file_menu(ExPanels *p, const char *path) { ex_panels_file_menu_at(p, path, 120.0f, 120.0f); }

/* The context menu: a right click at window position (x, y). Returns 1 if it opened one. */
int ex_panels_context_menu(ExPanels *p, ExWidget *root, float x, float y) {
    ExWidget *w = ex_ui_widget_at(root, x, y);
    const ExNode *n;
    while (w && w->type != EX_WIDGET_TREE) w = w->parent;
    if (!w || ex_panels_modal(p)) return 0;
    n = ex_tree_node_at(p->ui, w, y, 1);
    ex_panels_file_menu_at(p, n ? n->rel : "", x, y);
    return 1;
}

/* The More button of the toolbar: the menu of the selected row, or of the vault, below the button. */
static void on_more(ExWidget *tree, const char *rel, float x, float y, void *user) { (void)tree; ex_panels_file_menu_at(user, rel ? rel : "", x, y); }

/* A row dragged onto a folder: the same flow as Move to folder (the link update included). */
static void on_drop(ExWidget *tree, const char *from, const char *to_folder, void *user) {
    ExPanels *p = user;
    (void)tree;
    panels_move_done(p, to_folder, (void *)from);
}

/* Double click on a folder's name: its name is edited where it is; Enter makes the rename (the links are updated as for any rename). */
static void folder_renamed(ExPanels *p, const char *text, void *arg) {
    const char *path = arg, *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
    if (!strcmp(text, base)) return;
    ex_panels_rename_to(p, path, text);
}

static int on_tree_rename(ExWidget *tree, const char *rel, float x, float y, float width, float height, void *user) {
    ExPanels *p = user;
    const char *slash = strrchr(rel, '/'), *base = slash ? slash + 1 : rel;
    (void)tree;
    if (!ex_settings_get_bool(p->in.settings, "files", "rename_folder_on_double_click") || ex_panels_modal(p)) return 0;
    free(p->dialog_arg_text);
    p->dialog_arg_text = dup_s(rel);
    dialog_inline(p, x, y, width, height, base, folder_renamed, p->dialog_arg_text);
    return 1;
}

void fileactions_init(ExPanels *p) {
    ExWidget *c;
    if (!p->in.files) return;
    ex_explorer_set_menu_handler(p->in.files, on_more, p);
    for (c = p->in.files->first_child; c; c = c->next)
        if (c->type == EX_WIDGET_TREE) { ex_tree_set_drop_handler(c, on_drop, p); ex_tree_set_rename_handler(c, on_tree_rename, p); }
}
