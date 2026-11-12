/* vault_manager: the model (create, open a folder, rename, move with its recovery data, remove) and
 * the screen driven by scripted mouse and keyboard input with the folder picker mocked. Nothing
 * outside the folders it creates is touched, and no note is ever changed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_vaultscreen.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define BASE "vault_manager_test"
#define REG BASE "/vaults.ini"
#define DATA BASE "/data"

static int exists(const char *p) { ExPathInfo i; return ex_platform_path_info(p, &i) == 0; }
static void write_text(const char *p, const char *t) { CHECK(ex_platform_write_file_atomic(p, t, strlen(t)) == 0); }

static char *abs_path(const char *rel) { char *r = ex_platform_realpath(rel); return r; }

static void test_model(void) {
    ExVaultManager *m;
    const char *id = NULL, *id2 = NULL;
    ExVaultInfo v;
    char *a, *b, path[512], old_data[512], new_data[512], oldid[64];
    ex_platform_remove_tree(BASE);
    ex_platform_make_dirs(BASE "/parent/Existing");
    ex_platform_make_dirs(BASE "/parent2");
    write_text(BASE "/parent/Existing/note.md", "hello\n");

    m = ex_vaultmgr_create(REG, DATA);
    CHECK(m && ex_vaultmgr_count(m) == 0 && ex_vaultmgr_last(m) == NULL);

    /* names */
    CHECK(ex_vault_folder_name_valid("My vault") && ex_vault_folder_name_valid("Notes 2024") && ex_vault_folder_name_valid("a.b"));
    CHECK(!ex_vault_folder_name_valid("") && !ex_vault_folder_name_valid("a/b") && !ex_vault_folder_name_valid("a\\b") && !ex_vault_folder_name_valid(".hidden"));
    CHECK(!ex_vault_folder_name_valid("trailing ") && !ex_vault_folder_name_valid("dot.") && !ex_vault_folder_name_valid("CON") && !ex_vault_folder_name_valid("nul.txt"));
    CHECK(!ex_vault_folder_name_valid("a:b") && !ex_vault_folder_name_valid("what?") && !ex_vault_folder_name_valid("tab\there"));

    /* create: a new folder, never an existing one */
    CHECK(ex_vaultmgr_create_vault(m, BASE "/parent", "a/b", NULL) == EX_VM_INVALID_NAME && !exists(BASE "/parent/a"));
    CHECK(ex_vaultmgr_create_vault(m, BASE "/parent", "Existing", NULL) == EX_VM_EXISTS && ex_vaultmgr_count(m) == 0);
    CHECK(ex_vaultmgr_create_vault(m, BASE "/nowhere", "X", NULL) == EX_VM_NOT_FOUND);
    CHECK(ex_vaultmgr_create_vault(m, BASE "/parent/Existing/note.md", "X", NULL) == EX_VM_NOT_A_FOLDER);
    CHECK(ex_vaultmgr_create_vault(m, BASE "/parent", "Fresh", &id) == EX_VM_OK && id && exists(BASE "/parent/Fresh"));
    CHECK(ex_vaultmgr_count(m) == 1);
    {
        char *rd = ex_platform_realpath(BASE "/parent/Fresh");
        CHECK(ex_vaultmgr_get(m, 0, &v) == 0 && strcmp(v.name, "Fresh") == 0 && v.exists && strcmp(v.path, rd) == 0);
        free(rd);
    }

    /* open an existing folder: nothing in it changes */
    CHECK(ex_vaultmgr_open_folder(m, BASE "/parent/Existing", &id2) == EX_VM_OK && id2);
    CHECK(ex_vaultmgr_count(m) == 2);
    CHECK(ex_vaultmgr_open_folder(m, BASE "/parent/Existing", NULL) == EX_VM_OK && ex_vaultmgr_count(m) == 2);   /* the same folder twice is one vault */
    CHECK(ex_vaultmgr_open_folder(m, BASE "/nowhere", NULL) == EX_VM_NOT_FOUND && ex_vaultmgr_open_folder(m, BASE "/parent/Existing/note.md", NULL) == EX_VM_NOT_A_FOLDER);
    {
        char *t = ex_platform_read_file(BASE "/parent/Existing/note.md", NULL);
        CHECK(t && strcmp(t, "hello\n") == 0 && !exists(BASE "/parent/Existing/.exhaust"));
        free(t);
    }

    /* touch orders the list by last opened and remembers the last one */
    snprintf(oldid, sizeof oldid, "%s", id2);
    CHECK(ex_vaultmgr_touch(m, id2, 1000) == EX_VM_OK);
    {
        char fresh[64];
        ex_vaultmgr_get(m, 0, &v);
        CHECK(strcmp(v.id, oldid) == 0 && strcmp(ex_vaultmgr_last(m), oldid) == 0);
        ex_vaultmgr_get(m, 1, &v);
        snprintf(fresh, sizeof fresh, "%s", v.id);
        CHECK(ex_vaultmgr_touch(m, fresh, 2000) == EX_VM_OK);
        ex_vaultmgr_get(m, 0, &v);
        CHECK(strcmp(v.id, fresh) == 0);
    }

    /* rename: only the name in the list */
    CHECK(ex_vaultmgr_rename(m, oldid, "") == EX_VM_INVALID_NAME && ex_vaultmgr_rename(m, "nope", "X") == EX_VM_NOT_FOUND);
    CHECK(ex_vaultmgr_rename(m, oldid, "Work notes") == EX_VM_OK);
    {
        size_t i;
        int ok = 0;
        for (i = 0; i < 2; i++) { ex_vaultmgr_get(m, i, &v); if (!strcmp(v.id, oldid)) ok = !strcmp(v.name, "Work notes"); }
        CHECK(ok && exists(BASE "/parent/Existing/note.md"));                       /* the folder keeps its name */
    }

    /* move: the folder and its recovery data follow; the id changes with the path */
    snprintf(old_data, sizeof old_data, DATA "/vaults/%s/journal", oldid);
    ex_platform_make_dirs(old_data);
    strcat(old_data, "/1.jrn");
    write_text(old_data, "journal bytes");
    snprintf(old_data, sizeof old_data, DATA "/vaults/%s/session.ini", oldid);
    write_text(old_data, "[session]\nversion = 1\n");
    CHECK(ex_vaultmgr_move(m, oldid, BASE "/parent/Existing", NULL) == EX_VM_INTO_ITSELF);
    CHECK(ex_vaultmgr_move(m, oldid, BASE "/nowhere", NULL) == EX_VM_NOT_FOUND);
    ex_platform_make_dirs(BASE "/parent2/Existing");                                      /* something is in the way */
    CHECK(ex_vaultmgr_move(m, oldid, BASE "/parent2", NULL) == EX_VM_EXISTS && exists(BASE "/parent/Existing/note.md"));
    ex_platform_remove_tree(BASE "/parent2/Existing");
    CHECK(ex_vaultmgr_move(m, oldid, BASE "/parent2", &id2) == EX_VM_OK && id2 && strcmp(id2, oldid) != 0);
    CHECK(!exists(BASE "/parent/Existing") && exists(BASE "/parent2/Existing/note.md"));
    CHECK(ex_vaultmgr_count(m) == 2);
    snprintf(new_data, sizeof new_data, DATA "/vaults/%s/journal/1.jrn", id2);
    {
        char *j = ex_platform_read_file(new_data, NULL);
        CHECK(j && strcmp(j, "journal bytes") == 0);
        free(j);
        snprintf(new_data, sizeof new_data, DATA "/vaults/%s/session.ini", id2);
        CHECK(exists(new_data));
        snprintf(old_data, sizeof old_data, DATA "/vaults/%s", oldid);
        CHECK(!exists(old_data));
    }
    {
        size_t i;
        int ok = 0;
        for (i = 0; i < 2; i++) { ex_vaultmgr_get(m, i, &v); if (!strcmp(v.id, id2)) ok = !strcmp(v.name, "Work notes"); }
        CHECK(ok);                                                                         /* the name was kept */
    }

    /* transfer settings: the target's own files are never replaced, only added to */
    {
        const char *a_id = NULL, *b_id = NULL;
        char a_copy[64], b_copy[64];
        ExTransferResult tr;
        char *t;
        ex_platform_make_dirs(BASE "/tv/Source/.exhaust/themes");
        ex_platform_make_dirs(BASE "/tv/Source/.exhaust/snippets");
        ex_platform_make_dirs(BASE "/tv/Target/.exhaust/themes");
        write_text(BASE "/tv/Source/.exhaust/settings.ini", "# source\n[editor]\nvim_mode = true\nindent_width = 2\n[appearance]\nfont_size = 20\n");
        write_text(BASE "/tv/Source/.exhaust/hotkeys.ini", "[hotkeys]\napp.close-current-tab = Ctrl+Q\n");
        write_text(BASE "/tv/Source/.exhaust/themes/mine.theme", "[theme]\nname = Mine\n");
        write_text(BASE "/tv/Source/.exhaust/themes/shared.theme", "[theme]\nname = Source version\n");
        write_text(BASE "/tv/Source/.exhaust/snippets/wide.style", "@tokens { }\n");
        write_text(BASE "/tv/Target/.exhaust/settings.ini", "# target\n[editor]\nindent_width = 8  # mine\n[future]\nx = 1\n");
        write_text(BASE "/tv/Target/.exhaust/themes/shared.theme", "[theme]\nname = Target version\n");
        CHECK(ex_vaultmgr_open_folder(m, BASE "/tv/Source", &a_id) == EX_VM_OK);
        snprintf(a_copy, sizeof a_copy, "%s", a_id);
        CHECK(ex_vaultmgr_open_folder(m, BASE "/tv/Target", &b_id) == EX_VM_OK);
        snprintf(b_copy, sizeof b_copy, "%s", b_id);
        CHECK(ex_vaultmgr_transfer_settings(m, a_copy, a_copy, NULL) == EX_VM_SAME_VAULT);
        CHECK(ex_vaultmgr_transfer_settings(m, a_copy, "nope", NULL) == EX_VM_NOT_FOUND);
        CHECK(ex_vaultmgr_transfer_settings(m, a_copy, b_copy, &tr) == EX_VM_OK);
        CHECK(tr.files_copied == 2 && tr.keys_added >= 3 && tr.skipped >= 2);          /* mine.theme and wide.style; vim_mode, font_size, the hotkey; indent_width and shared.theme kept */
        t = ex_platform_read_file(BASE "/tv/Target/.exhaust/settings.ini", NULL);
        CHECK(t && strstr(t, "# target\n[editor]\nindent_width = 8  # mine\n") == t);        /* the target's own text, comment and all, is untouched */
        CHECK(t && strstr(t, "vim_mode = true") && strstr(t, "font_size = 20") && strstr(t, "[future]\nx = 1\n"));
        CHECK(t && !strstr(t, "indent_width = 2"));
        free(t);
        t = ex_platform_read_file(BASE "/tv/Target/.exhaust/hotkeys.ini", NULL);
        CHECK(t && strstr(t, "app.close-current-tab = Ctrl+Q"));
        free(t);
        t = ex_platform_read_file(BASE "/tv/Target/.exhaust/themes/shared.theme", NULL);
        CHECK(t && strstr(t, "Target version"));
        free(t);
        CHECK(exists(BASE "/tv/Target/.exhaust/themes/mine.theme") && exists(BASE "/tv/Target/.exhaust/snippets/wide.style"));
        t = ex_platform_read_file(BASE "/tv/Source/.exhaust/settings.ini", NULL);
        CHECK(t && strstr(t, "# source\n[editor]\nvim_mode = true\nindent_width = 2\n[appearance]\nfont_size = 20\n") == t);   /* the source is only read */
        free(t);
        /* doing it again changes nothing */
        CHECK(ex_vaultmgr_transfer_settings(m, a_copy, b_copy, &tr) == EX_VM_OK && tr.files_copied == 0 && tr.keys_added == 0);
        ex_vaultmgr_remove(m, a_copy);
        ex_vaultmgr_remove(m, b_copy);
    }

    /* remove: the list only */
    a = abs_path(BASE "/parent2/Existing");
    {
        char gone[64];
        snprintf(gone, sizeof gone, "%s", id2);                       /* the id points into the list, which changes */
        CHECK(ex_vaultmgr_remove(m, id2) == EX_VM_OK && ex_vaultmgr_count(m) == 1 && ex_vaultmgr_remove(m, gone) == EX_VM_NOT_FOUND);
    }
    CHECK(exists(BASE "/parent2/Existing/note.md"));
    free(a);

    /* a vault whose folder disappears is shown as missing, not dropped */
    ex_platform_remove_tree(BASE "/parent/Fresh");
    ex_vaultmgr_get(m, 0, &v);
    CHECK(!v.exists);

    /* the list is in the file and a new manager reads it */
    ex_vaultmgr_free(m);
    m = ex_vaultmgr_create(REG, DATA);
    CHECK(m && ex_vaultmgr_count(m) == 1);
    b = ex_platform_read_file(REG, NULL);
    CHECK(b && strstr(b, "[vault."));
    free(b);
    (void)path;
    ex_vaultmgr_free(m);
}

/* ---- the screen ---- */

typedef struct Ctx {
    ExVaultScreen *screen;
    char next_folder[512];       /* what the mock "dialog" answers with; "" cancels */
    int picks, opened;
    char opened_path[512];
    ExPickPurpose last_purpose;
} Ctx;

static void mock_pick(void *user, ExPickPurpose purpose, const char *vault_id) {
    Ctx *c = user;
    c->picks++;
    c->last_purpose = purpose;
    ex_vaultscreen_folder_chosen(c->screen, purpose, vault_id, c->next_folder[0] ? c->next_folder : NULL);
}
static void mock_open(void *user, const char *folder) { Ctx *c = user; c->opened++; snprintf(c->opened_path, sizeof c->opened_path, "%s", folder); }

static ExWidget *find(ExWidget *w, const char *name) {
    for (; w; w = w->next) {
        ExWidget *f;
        if (w->name && strcmp(w->name, name) == 0) return w;
        f = find(w->first_child, name);
        if (f) return f;
    }
    return NULL;
}
/* The button with this accessible name (a card row has the same name as its button). */
static ExWidget *find_button(ExWidget *w, const char *name) {
    for (; w; w = w->next) {
        ExWidget *f;
        if (w->type == EX_WIDGET_BUTTON && w->name && strcmp(w->name, name) == 0) return w;
        f = find_button(w->first_child, name);
        if (f) return f;
    }
    return NULL;
}
static ExWidget *find_prefix(ExWidget *w, const char *prefix) {
    for (; w; w = w->next) {
        ExWidget *f;
        if (w->name && strncmp(w->name, prefix, strlen(prefix)) == 0) return w;
        f = find_prefix(w->first_child, prefix);
        if (f) return f;
    }
    return NULL;
}
static void frame(ExUi *ui, ExVaultScreen *s) { ex_vaultscreen_update(s); ex_ui_layout(ui, ex_vaultscreen_root(s), 1100, 700); }
static void click(ExUi *ui, ExWidget *root, const ExWidget *w) {
    float x = w->x + w->w / 2, y = w->y + w->h / 2;
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, y, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, x, y, 0, 1);
}
static void type(ExUi *ui, const char *text) { ex_ui_text_input(ui, text, strlen(text)); }

static void test_screen(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExVaultManager *m;
    ExVaultScreen *s;
    ExVaultScreenHooks hooks;
    Ctx ctx;
    ExWidget *root, *w;
    ExVaultInfo v;
    char *real;
    ex_platform_remove_tree(BASE);
    ex_platform_make_dirs(BASE "/parent/Notes");
    ex_platform_make_dirs(BASE "/parent2");
    write_text(BASE "/parent/Notes/one.md", "one\n");

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("vault manager", 1100, 700);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); failures++; return; }
    ui = ex_ui_create(r, &theme, 16.0f);
    m = ex_vaultmgr_create(REG, DATA);
    memset(&ctx, 0, sizeof ctx);
    memset(&hooks, 0, sizeof hooks);
    hooks.pick_folder = mock_pick;
    hooks.open_vault = mock_open;
    s = ex_vaultscreen_create(ui, m, &hooks, &ctx, "0.1.0");
    ctx.screen = s;
    root = ex_vaultscreen_root(s);
    frame(ui, s);

    /* the screen: the three rows, the stub is labelled and cannot be operated */
    CHECK(find(root, "Create new vault") && find(root, "Open folder as vault") && find(root, "Name of the new vault"));
    w = find_prefix(root, "Open synced vault");
    CHECK(w != NULL && strstr(w->name, "(optional, later)") != NULL);
    CHECK(find_button(root, "Open synced vault: not available yet") == NULL);    /* nothing there can be pressed */
    {
        ExWidget *note = find(root, "Open synced vault: not available yet");
        CHECK(note != NULL && note->type == EX_WIDGET_LABEL && !note->focusable);
    }

    /* Open: choose an existing folder */
    snprintf(ctx.next_folder, sizeof ctx.next_folder, "%s", BASE "/parent/Notes");
    w = find_button(root, "Open folder as vault");
    CHECK(w != NULL);
    click(ui, root, w);
    CHECK(ctx.picks == 1 && ctx.last_purpose == EX_PICK_OPEN_FOLDER);
    CHECK(ex_vaultmgr_count(m) == 1 && ctx.opened == 1);
    real = ex_platform_realpath(BASE "/parent/Notes");
    CHECK(real && strcmp(ctx.opened_path, real) == 0);
    free(real);
    frame(ui, s);
    CHECK(find(root, "Vault Notes") != NULL && find(root, "Open vault Notes") != NULL);
    {
        char *t = ex_platform_read_file(BASE "/parent/Notes/one.md", NULL);
        CHECK(t && strcmp(t, "one\n") == 0 && !exists(BASE "/parent/Notes/.exhaust"));          /* opening it changed nothing in it */
        free(t);
    }

    /* cancelling the dialog does nothing */
    ctx.next_folder[0] = 0;
    ctx.opened = 0;
    click(ui, root, find_button(root, "Open folder as vault"));
    CHECK(ex_vaultmgr_count(m) == 1 && ctx.opened == 0);

    /* Create: type a name, press Create (or Enter in the field), choose the parent */
    snprintf(ctx.next_folder, sizeof ctx.next_folder, "%s", BASE "/parent2");
    frame(ui, s);
    w = find(root, "Name of the new vault");
    CHECK(w && w->type == EX_WIDGET_EDITOR);
    click(ui, root, w);
    ex_ui_key(ui, EX_KEY_A, EX_MOD_PRIMARY);                    /* select the suggested name */
    type(ui, "Journal");
    ctx.opened = 0; ctx.picks = 0;
    ex_ui_key(ui, EX_KEY_ENTER, 0);                              /* Enter in the field is the same as the button */
    CHECK(ctx.picks == 1 && ctx.last_purpose == EX_PICK_CREATE_PARENT);
    CHECK(exists(BASE "/parent2/Journal") && ex_vaultmgr_count(m) == 2 && ctx.opened == 1);
    frame(ui, s);
    CHECK(find(root, "Vault Journal") != NULL);
    /* a bad name is refused before the dialog and nothing is created */
    w = find(root, "Name of the new vault");
    click(ui, root, w);
    ex_ui_key(ui, EX_KEY_A, EX_MOD_PRIMARY);
    type(ui, "bad/name");
    ctx.picks = 0;
    ex_ui_key(ui, EX_KEY_ENTER, 0);
    CHECK(ctx.picks == 0 && strstr(ex_vaultscreen_message(s), "cannot be used") != NULL && !exists(BASE "/parent2/bad"));
    /* an existing name is refused after the dialog, with the message */
    frame(ui, s);
    w = find(root, "Name of the new vault");
    click(ui, root, w);
    ex_ui_key(ui, EX_KEY_A, EX_MOD_PRIMARY);
    type(ui, "Journal");
    ex_ui_key(ui, EX_KEY_ENTER, 0);
    CHECK(ex_vaultmgr_count(m) == 2 && strstr(ex_vaultscreen_message(s), "already there") != NULL);

    /* Rename from the menu: type the new name and press Enter */
    frame(ui, s);
    w = find(root, "Menu for vault Journal");
    CHECK(w != NULL);
    click(ui, root, w);
    frame(ui, s);
    w = find(root, "Rename this vault in the list");
    CHECK(w != NULL);
    click(ui, root, w);
    frame(ui, s);
    CHECK(find(root, "New name for the vault") != NULL);
    type(ui, "Daily journal");                                   /* the old name was selected, so this replaces it */
    ex_ui_key(ui, EX_KEY_ENTER, 0);
    frame(ui, s);
    {
        size_t i; int ok = 0;
        for (i = 0; i < ex_vaultmgr_count(m); i++) { ex_vaultmgr_get(m, i, &v); if (!strcmp(v.name, "Daily journal")) ok = 1; }
        CHECK(ok && find(root, "Vault Daily journal") != NULL && exists(BASE "/parent2/Journal"));
    }

    /* Move from the menu */
    ex_platform_make_dirs(BASE "/parent3");
    snprintf(ctx.next_folder, sizeof ctx.next_folder, "%s", BASE "/parent3");
    w = find(root, "Menu for vault Daily journal");
    click(ui, root, w);
    frame(ui, s);
    w = find(root, "Move this vault's folder");
    CHECK(w != NULL);
    click(ui, root, w);
    CHECK(ctx.last_purpose == EX_PICK_MOVE_TARGET && exists(BASE "/parent3/Journal") && !exists(BASE "/parent2/Journal"));
    frame(ui, s);

    /* Copy settings to another vault from the menu */
    ex_platform_make_dirs(BASE "/parent/Notes/.exhaust");
    write_text(BASE "/parent/Notes/.exhaust/settings.ini", "[editor]\nline_numbers = true\n");
    w = find(root, "Menu for vault Notes");
    click(ui, root, w);
    frame(ui, s);
    w = find(root, "Copy this vault's settings to another vault");
    CHECK(w != NULL);
    click(ui, root, w);
    frame(ui, s);
    CHECK(find_button(root, "Cancel copying settings") != NULL);
    w = find_button(root, "Copy settings to vault Daily journal");
    CHECK(w != NULL && find_button(root, "Copy settings to vault Notes") == NULL);      /* not to itself */
    click(ui, root, w);
    frame(ui, s);
    CHECK(strstr(ex_vaultscreen_message(s), "Copied settings: 1 added") != NULL);
    {
        char *t = ex_platform_read_file(BASE "/parent3/Journal/.exhaust/settings.ini", NULL);
        CHECK(t && strstr(t, "line_numbers = true"));
        free(t);
    }
    CHECK(find_button(root, "Cancel copying settings") == NULL);

    /* Remove from the list: the folder stays */
    w = find(root, "Menu for vault Daily journal");
    click(ui, root, w);
    frame(ui, s);
    w = find(root, "Remove this vault from the list");
    CHECK(w != NULL);
    click(ui, root, w);
    frame(ui, s);
    CHECK(ex_vaultmgr_count(m) == 1 && exists(BASE "/parent3/Journal") && find(root, "Vault Daily journal") == NULL);
    CHECK(strstr(ex_vaultscreen_message(s), "files were not touched") != NULL);

    /* the Escape key leaves renaming without a change */
    w = find(root, "Menu for vault Notes");
    click(ui, root, w);
    frame(ui, s);
    click(ui, root, find(root, "Rename this vault in the list"));
    frame(ui, s);
    type(ui, "Changed");
    ex_ui_key(ui, EX_KEY_ESCAPE, 0);
    frame(ui, s);
    ex_vaultmgr_get(m, 0, &v);
    CHECK(strcmp(v.name, "Notes") == 0 && find(root, "New name for the vault") == NULL);

    ex_vaultscreen_free(s);
    ex_vaultmgr_free(m);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
}

int main(void) {
    test_model();
    test_screen();
    ex_platform_remove_tree(BASE);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("vault_manager: ok");
    return 0;
}
