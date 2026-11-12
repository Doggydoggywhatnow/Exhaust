#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_commands.h"
#include "exhaust_fontdb.h"
#include "exhaust_input.h"
#include "exhaust_notes.h"
#include "exhaust_panes.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_session.h"
#include "exhaust_settings.h"
#include "exhaust_theme.h"
#include "exhaust_ui.h"
#include "exhaust_vault.h"
#include "exhaust_vaultmgr.h"
#include "exhaust_vaultscreen.h"
#include "exhaust_worker.h"
#include "exhaust_workspace.h"

/* The system font scan runs on a worker thread and hands its result to the UI
 * thread as a message; the queue wakes the UI loop with an SDL event. */
enum { MSG_FONTS_READY = 1 };
static Uint32 g_wake_event;
static volatile sig_atomic_t g_signal; /* SIGTERM or SIGHUP arrived: flush and leave */

static void on_signal(int sig) { g_signal = sig; }

static void wake_ui(void *unused) {
    SDL_Event e;
    (void)unused;
    SDL_zero(e);
    e.type = g_wake_event;
    SDL_PushEvent(&e);
}

static void worker_job(ExWorker *self, const ExMessage *job, void *unused) {
    (void)unused;
    if (job->type == MSG_FONTS_READY)
        ex_worker_send(self, MSG_FONTS_READY, ex_fontdb_scan(NULL, 0, NULL, NULL), 0, NULL);
}

typedef enum Banner { BANNER_NONE, BANNER_RECOVERED, BANNER_CONFLICT, BANNER_ERROR } Banner;

typedef struct TabNote { ExId tab; ExNote *note; } TabNote;

typedef struct App {
    ExRenderer *r;
    ExUi *ui;
    ExTheme theme;
    ExWidget *root, *editor, *tree, *status;     /* editor: the editor of the active tab, or NULL */
    ExWidget *banner, *banner_text, *banner_a, *banner_b, *banner_c;
    ExWidget *left_sb, *right_sb;
    ExDoc *demo_doc;           /* shown when there is no vault */
    ExDoc *diff_doc;           /* the conflict diff, while it is shown */
    ExDoc *shown_before_diff;
    ExVault *vault;
    ExNotes *notes;
    ExNote *note;              /* the note of the active tab */
    ExNote *banner_note;       /* the note the banner was last computed for */
    ExWorkspace *ws;
    ExPanes *panes;
    ExSession *session;
    ExSettings *settings;
    ExFeatures *features;
    ExCommands *commands;
    ExWidget *left_content;
    float zoom_now;
    TabNote *tabnotes; size_t ntabnotes, captabnotes;
    ExId focus_tab;            /* the tab whose editor should get the focus when it exists */
    Banner banner_kind;
    int blink_on;
    float zoom;
} App;

static void sync_ui(App *a);

static void relayout_and_draw(App *a) {
    int w, h;
    sync_ui(a);
    ex_renderer_size(a->r, &w, &h);
    ex_ui_layout(a->ui, a->root, (float)w, (float)h);
    ex_renderer_clear(a->r, ex_theme_color(&a->theme, EX_COLOR_BACKGROUND));
    ex_ui_draw(a->ui, a->root, a->blink_on);
    ex_renderer_present(a->r);
}

static void set_label(ExWidget *w, const char *text) {
    ex_widget_set_text(w, text);
    free(w->name);
    w->name = malloc(strlen(text) + 1);
    if (w->name) strcpy(w->name, text);
}

/* ---- the banner above the panes ---- */

static void show_banner(App *a, Banner kind, const char *text, const char *a_label, const char *b_label, const char *c_label) {
    a->banner_kind = kind;
    a->banner->hidden = kind == BANNER_NONE;
    if (kind == BANNER_NONE) return;
    set_label(a->banner_text, text);
    a->banner_a->hidden = !a_label; if (a_label) set_label(a->banner_a, a_label);
    a->banner_b->hidden = !b_label; if (b_label) set_label(a->banner_b, b_label);
    a->banner_c->hidden = !c_label; if (c_label) set_label(a->banner_c, c_label);
}

static void set_status(App *a, const char *text) { if (a->status) ex_widget_set_text(a->status, text); }

static void show_conflict_banner(App *a) {
    show_banner(a, BANNER_CONFLICT, "This note was changed on disk while you have unsaved edits. Both versions are kept. Which one should be saved?",
                "Keep my version", "Use the version on disk", "Show differences");
}

/* The banner follows the note of the active tab. */
static void banner_for_note(App *a, ExNote *n) {
    a->banner_note = n;
    if (n && ex_note_has_conflict(n)) show_conflict_banner(a);
    else if (n && ex_note_error(n)) show_banner(a, BANNER_ERROR, ex_note_error(n), NULL, NULL, NULL);
    else show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
}

/* The editing mode a tab asks for ("live" is live preview, "source" is source mode; "reading" waits for sub-slice 3c). */
static ExEditorMode editor_mode_for(const char *mode) { return mode && strcmp(mode, "source") == 0 ? EX_EDITOR_SOURCE : EX_EDITOR_LIVE; }

static void stop_showing_diff(App *a) {
    if (a->diff_doc) {
        if (a->editor && a->editor->editor) {
            ex_editor_set_doc(a->editor->editor, a->shown_before_diff);
            ex_editor_set_readonly(a->editor->editor, a->note == NULL);
            {
                ExId tab = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
                const ExTab *tt = tab ? ex_ws_tab(a->ws, tab) : NULL;
                ex_editor_set_mode(a->editor->editor, editor_mode_for(tt ? tt->mode : NULL));
            }
        }
        ex_doc_free(a->diff_doc);
        a->diff_doc = NULL;
        a->shown_before_diff = NULL;
    }
}

static void banner_a_click(ExWidget *w, void *user) { /* "Keep my version", or "OK" */
    App *a = user;
    (void)w;
    stop_showing_diff(a);
    if (a->banner_kind == BANNER_CONFLICT && a->note) { ex_note_resolve_conflict(a->note, EX_KEEP_MINE); set_status(a, "Kept your version; the other one was copied to the recovery folder."); }
    show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
}

static void banner_b_click(ExWidget *w, void *user) { /* "Use the version on disk" */
    App *a = user;
    (void)w;
    stop_showing_diff(a);
    if (a->banner_kind == BANNER_CONFLICT && a->note) { ex_note_resolve_conflict(a->note, EX_USE_DISK); set_status(a, "Loaded the version from disk; your text was copied to the recovery folder (and Undo brings it back)."); }
    show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
}

static void banner_c_click(ExWidget *w, void *user) { /* "Show differences" */
    App *a = user;
    char *diff;
    (void)w;
    if (!a->note || a->banner_kind != BANNER_CONFLICT || !a->editor) return;
    if (a->diff_doc) { stop_showing_diff(a); return; }
    diff = ex_note_conflict_diff(a->note);
    if (!diff) return;
    a->diff_doc = ex_doc_create(diff[0] ? diff : "(the two versions have the same text)\n", diff[0] ? strlen(diff) : 38);
    free(diff);
    a->shown_before_diff = ex_note_doc(a->note);
    ex_editor_set_doc(a->editor->editor, a->diff_doc);
    ex_editor_set_readonly(a->editor->editor, 1);
    ex_editor_set_mode(a->editor->editor, EX_EDITOR_PLAIN);              /* a diff is shown as it is, not as Markdown */
}

/* ---- tabs and notes ---- */

static ExNote *note_for_tab(App *a, ExId tab) {
    size_t i;
    for (i = 0; i < a->ntabnotes; i++) if (a->tabnotes[i].tab == tab) return a->tabnotes[i].note;
    return NULL;
}

static void remember_tab_note(App *a, ExId tab, ExNote *n) {
    if (a->ntabnotes == a->captabnotes) {
        size_t nc = a->captabnotes ? a->captabnotes * 2 : 8;
        TabNote *nt = realloc(a->tabnotes, nc * sizeof *nt);
        if (!nt) return;
        a->tabnotes = nt;
        a->captabnotes = nc;
    }
    a->tabnotes[a->ntabnotes].tab = tab;
    a->tabnotes[a->ntabnotes].note = n;
    a->ntabnotes++;
}

static ExWidget *info_label(App *a, const char *text) {
    ExWidget *l = ex_widget_label(a->ui, text);
    if (l) { l->text_token = EX_COLOR_TEXT_MUTED; l->pad_x = 2; l->pad_y = 2; }
    return l;
}

/* What a tab shows. Notes open here (not when the session is restored), so a note that is not on
 * screen costs nothing, and a missing file only shows a message: its tab stays in the session. */
static ExWidget *make_content(ExPanes *p, const ExTab *t, void *user) {
    App *a = user;
    char msg[600];
    ExOpenResult res = EX_OPEN_OK;
    ExNote *n;
    ExWidget *ed;
    (void)p;
    if (!a->vault) {
        ed = ex_widget_editor(a->ui, a->demo_doc, "Welcome");
        return ed;
    }
    if (strcmp(t->view, "graph") == 0) return info_label(a, "The graph view arrives in a later sub-slice.");
    if (!t->path[0]) return info_label(a, "An empty tab. Open a note from the file explorer, or create one with New note.");
    if (!a->notes) return info_label(a, "Notes cannot be edited because the recovery journal could not be opened.");
    n = ex_notes_open(a->notes, t->path, &res);
    if (!n) {
        snprintf(msg, sizeof msg, res == EX_OPEN_INVALID_UTF8 ? "%s is not a UTF-8 text file, so Exhaust will not open or change it."
                                 : res == EX_OPEN_NOT_FOUND ? "%s does not exist any more. Close this tab, or restore the file." : "%s could not be opened.", t->path);
        return info_label(a, msg);
    }
    ed = ex_widget_editor(a->ui, ex_note_doc(n), t->path);
    if (!ed) return NULL;
    ex_editor_set_state(ed->editor, t->cursor_line, t->cursor_column, t->scroll_y);
    ex_editor_set_mode(ed->editor, editor_mode_for(t->mode));
    remember_tab_note(a, t->id, n);
    if (res == EX_OPEN_RECOVERED) show_banner(a, BANNER_RECOVERED, "Exhaust closed unexpectedly. The text you typed was restored from the recovery journal.", "OK", NULL, NULL);
    else if (res == EX_OPEN_CONFLICT && ex_note_has_conflict(n)) show_conflict_banner(a);
    a->banner_note = n;
    snprintf(msg, sizeof msg, "%s%s", ex_note_path(n), ex_note_exists_on_disk(n) ? "" : " (new: it is written when you type something)");
    set_status(a, msg);
    return ed;
}

static void content_closed(ExPanes *p, ExId tab, ExWidget *content, void *user) {
    App *a = user;
    size_t i;
    ExNote *n = NULL;
    (void)p;
    for (i = 0; i < a->ntabnotes; i++) if (a->tabnotes[i].tab == tab) { n = a->tabnotes[i].note; a->tabnotes[i] = a->tabnotes[--a->ntabnotes]; break; }
    if (!n) return;
    if (a->editor == content) a->editor = NULL;
    for (i = 0; i < a->ntabnotes; i++) if (a->tabnotes[i].note == n) return;      /* another tab still shows it */
    if (a->note == n) a->note = NULL;
    if (a->banner_note == n) { a->banner_note = NULL; show_banner(a, BANNER_NONE, "", NULL, NULL, NULL); }
    stop_showing_diff(a);
    ex_notes_close(a->notes, n);                                                    /* saves it first */
}

static void tab_shown(ExPanes *p, ExId tab, void *user) { App *a = user; (void)p; a->focus_tab = tab; }

static void new_empty_tab(ExPanes *p, ExId group, void *user) {
    App *a = user;
    ExId t = ex_ws_new_tab(a->ws, group, "", "empty");
    (void)p;
    a->focus_tab = t;
}

static void open_path(App *a, ExId group, const char *rel) {
    ExId t = ex_ws_find_tab(a->ws, rel);
    if (t) { ex_ws_activate(a->ws, t); a->focus_tab = t; return; }
    t = ex_ws_new_tab(a->ws, group, rel, "note");
    if (t && strcmp(ex_settings_get(a->settings, "editor", "default_mode"), "source") == 0) ex_ws_set_tab_mode(a->ws, t, "source");
    a->focus_tab = t;
}

static int drop_file(ExPanes *p, ExId group, const char *path, void *user) {
    App *a = user;
    const char *root = a->vault ? ex_vault_path(a->vault) : NULL;
    size_t rl = root ? strlen(root) : 0;
    (void)p;
    if (root && strncmp(path, root, rl) == 0 && (path[rl] == '/' || path[rl] == '\\') && path[rl + 1]) {
        char *rel = malloc(strlen(path + rl + 1) + 1), *c;
        if (!rel) return 0;
        strcpy(rel, path + rl + 1);
        for (c = rel; *c; c++) if (*c == '\\') *c = '/';
        if (ex_file_kind(rel) == EX_KIND_MARKDOWN) { open_path(a, group, rel); free(rel); return 1; }
        free(rel);
    }
    set_status(a, "Dropped files from outside the vault are imported by the importers (a later sub-slice); nothing was copied.");
    return 0;
}

static void on_open_file(ExWidget *w, const char *rel, void *user) {
    App *a = user;
    (void)w;
    if (ex_file_kind(rel) != EX_KIND_MARKDOWN && ex_file_kind(rel) != EX_KIND_BASE && ex_file_kind(rel) != EX_KIND_CANVAS) {
        char msg[400];
        snprintf(msg, sizeof msg, "%s cannot be shown yet (images, audio, video and PDF viewers come in sub-slice 6c).", rel);
        set_status(a, msg);
        return;
    }
    open_path(a, 0, rel);
}

static void on_new_note(ExWidget *w, const char *parent_rel, void *user) {
    App *a = user;
    ExNote *n;
    (void)w;
    if (!a->notes) return;
    n = ex_notes_new(a->notes, parent_rel, "Untitled");
    if (n) open_path(a, 0, ex_note_path(n));
}

static void on_toggle_theme(ExWidget *w, void *user) {
    App *a = user;
    ExTheme next;
    int to_dark = !a->theme.is_dark;
    (void)w;
    if (ex_theme_init_builtin(&next, to_dark ? EX_THEME_DARK : EX_THEME_LIGHT) == 0) {
        ex_theme_free(&a->theme);
        a->theme = next;
        ex_ui_set_theme(a->ui, &a->theme);
        ex_settings_set(a->settings, "appearance", "color_scheme", to_dark ? "dark" : "light");   /* written at once */
    }
}

static void toggle_side(App *a, ExSide side) {
    ex_ws_set_sidebar(a->ws, side, !ex_ws_sidebar_visible(a->ws, side), ex_ws_sidebar_width(a->ws, side));
}
static void on_toggle_left(ExWidget *w, void *user) { (void)w; ex_commands_run(((App *)user)->commands, "app.toggle-left-sidebar"); }
static void on_toggle_mode_button(ExWidget *w, void *user) { (void)w; ex_commands_run(((App *)user)->commands, "app.toggle-live-preview-source-mode"); }
static void on_toggle_right(ExWidget *w, void *user) { (void)w; ex_commands_run(((App *)user)->commands, "app.toggle-right-sidebar"); }

/* ---- commands ---- */

static void cmd_toggle_left(void *u) { toggle_side(u, EX_SIDE_LEFT); }
static void cmd_toggle_right(void *u) { toggle_side(u, EX_SIDE_RIGHT); }
static void cmd_change_theme(void *u) { on_toggle_theme(NULL, u); }   /* until the picker exists: switches light and dark */
static void spawn_self(const char *arg);
static void cmd_change_vault(void *u) { (void)u; spawn_self("--vaults"); }
static void cmd_toggle_mode(void *u) {
    App *a = u;
    ExId tab = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    const ExTab *t = tab ? ex_ws_tab(a->ws, tab) : NULL;
    const char *next;
    if (!t) return;
    next = strcmp(t->mode, "source") == 0 ? "live" : "source";
    ex_ws_set_tab_mode(a->ws, tab, next);
    if (a->editor && a->editor->editor && !a->diff_doc) ex_editor_set_mode(a->editor->editor, editor_mode_for(next));
}
static int has_editor(void *u) { return ((App *)u)->editor != NULL; }
static void cmd_new_note(void *u) { on_new_note(NULL, "", u); }
static int has_notes(void *u) { return ((App *)u)->notes != NULL && ((App *)u)->vault != NULL; }
static void cmd_close_tab(void *u) {
    App *a = u;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    if (t) ex_ws_close_tab(a->ws, t);
}
static int has_tab(void *u) { App *a = u; return ex_ws_group_active(a->ws, ex_ws_active_group(a->ws)) != 0; }

static void on_files_feature(const char *id, int enabled, void *user) {
    App *a = user;
    (void)id;
    if (a->left_content) a->left_content->hidden = !enabled;
}

static void register_features_and_commands(App *a) {
    a->features = ex_features_create(a->settings);
    ex_features_register(a->features, "files", "Files", on_files_feature, a);
    on_files_feature("files", ex_features_enabled(a->features, "files"), a);
    a->commands = ex_commands_create(a->features);
    ex_commands_bind(a->commands, "app.toggle-left-sidebar", cmd_toggle_left, NULL, a);
    ex_commands_bind(a->commands, "app.toggle-right-sidebar", cmd_toggle_right, NULL, a);
    ex_commands_bind(a->commands, "app.change-theme", cmd_change_theme, NULL, a);
    ex_commands_bind(a->commands, "app.change-vault", cmd_change_vault, NULL, a);
    ex_commands_bind(a->commands, "app.toggle-live-preview-source-mode", cmd_toggle_mode, has_editor, a);
    ex_commands_bind(a->commands, "app.create-new-note", cmd_new_note, has_notes, a);
    ex_commands_bind(a->commands, "app.close-current-tab", cmd_close_tab, has_tab, a);
}

/* Does a key press match a shortcut text from the command table ("Ctrl+N", "Ctrl+Shift+T")? Only
 * letters, digits and single punctuation marks for now; the hotkeys screen (sub-slice 7c) replaces this. */
static int shortcut_matches(const char *keys, const SDL_KeyboardEvent *k) {
    int ctrl = 0, shift = 0, alt = 0;
    const char *p = keys, *last;
    SDL_Keymod m = k->mod;
    for (;;) {
        const char *plus = strchr(p, '+');
        size_t n = plus && plus[1] ? (size_t)(plus - p) : 0;
        if (!n) break;
        if (n == 4 && !strncmp(p, "Ctrl", 4)) ctrl = 1;
        else if (n == 3 && !strncmp(p, "Mod", 3)) ctrl = 1;
        else if (n == 5 && !strncmp(p, "Shift", 5)) shift = 1;
        else if (n == 3 && !strncmp(p, "Alt", 3)) alt = 1;
        else return 0;
        p = plus + 1;
    }
    last = p;
    if (strlen(last) != 1) return 0;
#ifdef __APPLE__
    if (((m & SDL_KMOD_GUI) != 0) != ctrl) return 0;
#else
    if (((m & SDL_KMOD_CTRL) != 0) != ctrl) return 0;
#endif
    if (((m & SDL_KMOD_SHIFT) != 0) != shift || ((m & SDL_KMOD_ALT) != 0) != alt) return 0;
    {
        int c = (unsigned char)last[0];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        return (int)k->key == c;
    }
}

static int run_shortcut(App *a, const SDL_KeyboardEvent *k) {
    size_t i;
    for (i = 0; i < ex_commands_count(a->commands); i++) {
        const ExCommandDef *d = ex_commands_at(a->commands, i);
        if (d->default_keys[0] && ex_commands_available(a->commands, d->id) && shortcut_matches(d->default_keys, k)) {
            ex_commands_run(a->commands, d->id);
            return 1;
        }
    }
    return 0;
}

/* Which tab, note and editor are active; called before every layout. */
static void sync_ui(App *a) {
    ExId g, t;
    ExWidget *c;
    ex_sidebar_sync(a->left_sb);
    ex_sidebar_sync(a->right_sb);
    ex_panes_update(a->panes);
    g = ex_ws_active_group(a->ws);
    t = ex_ws_group_active(a->ws, g);
    c = t ? ex_panes_content(a->panes, t) : NULL;
    a->editor = c && c->type == EX_WIDGET_EDITOR ? c : NULL;
    a->note = t ? note_for_tab(a, t) : NULL;
    if (a->note != a->banner_note && !a->diff_doc) banner_for_note(a, a->note);
    if (a->focus_tab && a->focus_tab == t && a->editor) { ex_ui_set_focus(a->ui, a->editor); a->focus_tab = 0; }
    else if (a->focus_tab && !ex_ws_tab(a->ws, a->focus_tab)) a->focus_tab = 0;
}

/* Cursor and scroll positions of the tabs that are showing a note go into the workspace (and so the session file). */
static void capture_tab_state(App *a) {
    size_t i;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        ExId t = ex_ws_tab_at(a->ws, i);
        ExWidget *c = ex_panes_content(a->panes, t);
        int line, col;
        float sc;
        if (!c || c->type != EX_WIDGET_EDITOR || !c->editor || (c == a->editor && a->diff_doc)) continue;
        if (!note_for_tab(a, t)) continue;
        ex_editor_get_state(c->editor, &line, &col, &sc);
        ex_ws_set_tab_state(a->ws, t, line, col, sc);
    }
}

/* Notes events: saves, failures, outside changes. */
static void handle_note_events(App *a) {
    ExNoteEvent ev;
    while (ex_notes_poll_event(a->notes, &ev)) {
        switch (ev.kind) {
        case EX_NOTE_EVENT_SAVED:
            if (ev.note == a->note && a->banner_kind == BANNER_ERROR) show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
            break;
        case EX_NOTE_EVENT_SAVE_FAILED:
            if (ev.note == a->note) show_banner(a, BANNER_ERROR, ev.message, NULL, NULL, NULL); /* stays until a save works */
            break;
        case EX_NOTE_EVENT_RELOADED:
            if (ev.note == a->note) set_status(a, "This note was changed outside Exhaust; the new text is shown (Undo goes back).");
            break;
        case EX_NOTE_EVENT_CONFLICT:
            if (ev.note == a->note) show_conflict_banner(a);
            break;
        case EX_NOTE_EVENT_TREE_CHANGED:
            ex_vault_rescan(a->vault);
            break;
        }
    }
}

static ExWidget *ribbon_button(App *a, const char *text, const char *name, ExClickFn fn) {
    ExWidget *b = ex_widget_button(a->ui, text, fn, a);
    if (!b) return NULL;
    free(b->name);
    b->name = malloc(strlen(name) + 1);
    if (b->name) strcpy(b->name, name);
    return b;
}

/* ---- the vault manager window and opening vaults ---- */

static const char *g_exe = "exhaust";
static char *g_first_folder;           /* the vault used last, when started without an argument */
static Uint32 g_picked_event;

/* Starts another Exhaust: a vault opens in its own window, the manager is its own small window. */
static void spawn_self(const char *arg) {
    const char *args[3];
    SDL_Process *p;
    args[0] = g_exe; args[1] = arg; args[2] = NULL;
    p = SDL_CreateProcess(args, false);
    if (p) SDL_DestroyProcess(p);                 /* does not stop it */
    else fprintf(stderr, "exhaust: cannot start %s: %s\n", g_exe, SDL_GetError());
}

typedef struct PickRequest { ExPickPurpose purpose; char vault_id[48]; } PickRequest;

static void SDLCALL on_folder_dialog(void *userdata, const char *const *filelist, int filter) {
    PickRequest *req = userdata;
    (void)filter;
    if (filelist && filelist[0]) {                /* cancelled or failed: nothing to do */
        SDL_Event e;
        char *path = malloc(strlen(filelist[0]) + 1), *id = req->vault_id[0] ? malloc(strlen(req->vault_id) + 1) : NULL;
        if (path) {
            strcpy(path, filelist[0]);
            if (id) strcpy(id, req->vault_id);
            SDL_zero(e);
            e.type = g_picked_event;
            e.user.code = (Sint32)req->purpose;
            e.user.data1 = path;
            e.user.data2 = id;
            if (!SDL_PushEvent(&e)) { free(path); free(id); }
        }
    }
    free(req);
}

typedef struct ManagerApp { SDL_Window *window; int quit; } ManagerApp;

static void manager_pick(void *user, ExPickPurpose purpose, const char *vault_id) {
    ManagerApp *m = user;
    PickRequest *req = calloc(1, sizeof *req);
    if (!req) return;
    req->purpose = purpose;
    if (vault_id) snprintf(req->vault_id, sizeof req->vault_id, "%s", vault_id);
    SDL_ShowOpenFolderDialog(on_folder_dialog, req, m->window, NULL, false);
}

static void manager_open(void *user, const char *folder) {
    ManagerApp *m = user;
    spawn_self(folder);
    m->quit = 1;                                  /* the manager has done its job */
}

static int run_vault_manager(void) {
    ExRenderer *r;
    ExTheme theme;
    ExUi *ui;
    ExVaultManager *mgr;
    ExVaultScreen *screen;
    ExVaultScreenHooks hooks;
    ManagerApp ma;
    ExSettings *settings;
    SDL_Event e;
    int clicks = 0;
    Uint64 last_click = 0;
    char *cfg = ex_platform_config_dir();
    int dark = 1;

    settings = ex_settings_open(NULL, cfg);
    free(cfg);
    r = ex_renderer_create("Exhaust: vaults", 960, 600);
    if (!r) { fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error()); ex_settings_close(settings); return 1; }
    if (settings) {
        const char *scheme = ex_settings_get(settings, "appearance", "color_scheme");
        dark = strcmp(scheme, "light") != 0;
        if (strcmp(scheme, "system") == 0) dark = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
    }
    if (ex_theme_init_builtin(&theme, dark ? EX_THEME_DARK : EX_THEME_LIGHT) != 0) return 1;
    ex_renderer_set_display_scale(r, ex_renderer_detect_display_scale(r));
    ui = ex_ui_create(r, &theme, 16.0f);
    mgr = ex_vaultmgr_create(NULL, NULL);
    memset(&ma, 0, sizeof ma);
    ma.window = ex_renderer_window(r);
    memset(&hooks, 0, sizeof hooks);
    hooks.pick_folder = manager_pick;
    hooks.open_vault = manager_open;
    screen = mgr ? ex_vaultscreen_create(ui, mgr, &hooks, &ma, exhaust_version()) : NULL;
    if (!screen) { fprintf(stderr, "exhaust: cannot read the list of vaults\n"); return 1; }
    g_picked_event = SDL_RegisterEvents(1);
    ex_text_input_start(ex_renderer_window(r));
    while (!ma.quit) {
        ExTextEvent te;
        ExKey key;
        int mods, w, h;
        if (SDL_WaitEventTimeout(&e, 100)) {
            if (e.type == SDL_EVENT_QUIT) break;
            if (e.type == g_picked_event) {
                ex_vaultscreen_folder_chosen(screen, (ExPickPurpose)e.user.code, (const char *)e.user.data2, (const char *)e.user.data1);
                free(e.user.data1);
                free(e.user.data2);
            } else if (ex_text_event_decode(&e, &te)) {
                if (te.kind == EX_TEXT_COMMIT) ex_ui_text_input(ui, te.text, strlen(te.text));
            } else if (ex_key_from_sdl(&e, &key, &mods)) {
                ex_ui_key(ui, key, mods);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_MOUSE_MOTION) {
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(r));
                float x = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x) * density, y = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y) * density;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    Uint64 now = SDL_GetTicks();
                    clicks = now - last_click < 400 ? clicks + 1 : 1;
                    last_click = now;
                    ex_ui_mouse(ui, ex_vaultscreen_root(screen), EX_MOUSE_DOWN, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) ex_ui_mouse(ui, ex_vaultscreen_root(screen), EX_MOUSE_UP, x, y, ex_mods_from_sdl(), clicks);
                else if (e.motion.state & SDL_BUTTON_LMASK) ex_ui_mouse(ui, ex_vaultscreen_root(screen), EX_MOUSE_DRAG, x, y, ex_mods_from_sdl(), clicks);
            } else if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                ex_renderer_set_display_scale(r, ex_renderer_detect_display_scale(r));
            }
        }
        ex_vaultscreen_update(screen);
        ex_renderer_size(r, &w, &h);
        ex_ui_layout(ui, ex_vaultscreen_root(screen), (float)w, (float)h);
        ex_renderer_clear(r, ex_theme_color(&theme, EX_COLOR_BACKGROUND));
        ex_ui_draw(ui, ex_vaultscreen_root(screen), 1);
        ex_renderer_present(r);
    }
    ex_vaultscreen_free(screen);
    ex_vaultmgr_free(mgr);
    ex_settings_close(settings);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
    return 0;
}

static void on_vault_button(ExWidget *w, void *user) { (void)w; (void)user; spawn_self("--vaults"); }

int main(int argc, char **argv) {
    App app;
    SDL_Event e;
    ExQueue *results;
    ExWorker *worker;
    ExMessage job;
    ExWidget *row, *col, *ribbon, *status, *left_content;
    ExPanesHooks hooks;
    float zoom = 1.0f;
    int running = 1, clicks = 0;
    Uint64 last_click = 0;
    const char *intro = "Welcome to Exhaust.\n\nOpen a folder of notes with:  exhaust FOLDER\n\nEnglish, \xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A (Arabic), "
                        "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA (Hebrew), \xE4\xBD\xA0\xE5\xA5\xBD (Chinese), \xF0\x9F\x98\x80.\n";

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("Exhaust %s\n", exhaust_version());
        return 0;
    }
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: exhaust [FOLDER | --vaults]\n  FOLDER    open this folder of notes as a vault (it is remembered)\n  --vaults  show the vault manager\n  (no argument) open the vault used last, or show the vault manager\n  --version\n");
        return 0;
    }
    if (argc > 0 && argv[0] && argv[0][0]) g_exe = argv[0];
    {   /* which vault: the folder given, else the one used last; none known (or --vaults): the vault manager */
        char *last_path = NULL;
        if (argc > 1 && strcmp(argv[1], "--vaults") == 0) return run_vault_manager();
        if (argc == 1) {
            ExRegistry *reg = ex_registry_load(NULL);
            if (reg) {
                const char *last = ex_registry_last(reg);
                ExRegistryEntry en;
                size_t i;
                for (i = 0; last && i < ex_registry_count(reg); i++)
                    if (ex_registry_get(reg, i, &en) == 0 && strcmp(en.id, last) == 0) {
                        ExPathInfo info;
                        if (ex_platform_path_info(en.path, &info) == 0 && info.is_dir) { last_path = malloc(strlen(en.path) + 1); if (last_path) strcpy(last_path, en.path); }
                    }
                ex_registry_free(reg);
            }
            if (!last_path) return run_vault_manager();
            g_first_folder = last_path;
        }
    }
    memset(&app, 0, sizeof app);
    app.zoom = 1.0f;
    app.blink_on = 1;
    {
        const char *folder = argc > 1 ? argv[1] : g_first_folder;
        if (folder) {
            app.vault = ex_vault_open(folder);
            if (!app.vault) fprintf(stderr, "exhaust: cannot open %s as a vault\n", folder);
        }
    }
    {   /* settings: the vault's .exhaust folder over the global file; reading creates nothing */
        char *cfg = ex_platform_config_dir(), *vcfg = NULL;
        if (app.vault) {
            vcfg = malloc(strlen(ex_vault_path(app.vault)) + 16);
            if (vcfg) sprintf(vcfg, "%s/.exhaust", ex_vault_path(app.vault));
        }
        app.settings = ex_settings_open(vcfg, cfg);
        free(vcfg);
        free(cfg);
        if (!app.settings) { fprintf(stderr, "exhaust: cannot read the settings\n"); return 1; }
    }
    app.r = ex_renderer_create("Exhaust", (int)ex_settings_get_int(app.settings, "appearance", "window_width"), (int)ex_settings_get_int(app.settings, "appearance", "window_height"));
    if (!app.r) {
        fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error());
        return 1;
    }
    {
        const char *scheme = ex_settings_get(app.settings, "appearance", "color_scheme");
        int dark = strcmp(scheme, "light") != 0;
        if (strcmp(scheme, "system") == 0) dark = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
        if (ex_theme_init_builtin(&app.theme, dark ? EX_THEME_DARK : EX_THEME_LIGHT) != 0) return 1;
    }
    ex_renderer_set_display_scale(app.r, ex_renderer_detect_display_scale(app.r));
    {
        long fs = ex_settings_get_int(app.settings, "appearance", "font_size");
        long zp = ex_settings_get_int(app.settings, "interface", "zoom_percent");
        app.ui = ex_ui_create(app.r, &app.theme, (float)(fs < 8 ? 8 : fs > 48 ? 48 : fs));
        zoom = (float)(zp < 50 ? 50 : zp > 400 ? 400 : zp) / 100.0f;
        ex_renderer_set_zoom(app.r, zoom);
    }
    app.demo_doc = ex_doc_create(intro, strlen(intro));
    app.ws = ex_ws_create();

    if (app.vault) {
        ExRegistry *reg = ex_registry_load(NULL);
        ExNotesConfig nc;
        int loaded;
        if (reg) { /* remember the vault, and which one was opened last */
            const char *id = ex_registry_add(reg, ex_vault_path(app.vault));
            if (id) ex_registry_touch(reg, id, (int64_t)time(NULL));
            ex_registry_save(reg);
            ex_registry_free(reg);
        }
        memset(&nc, 0, sizeof nc);
        nc.autosave_idle_ms = (int)ex_settings_get_int(app.settings, "files", "autosave_idle_ms");
        nc.autosave_max_ms = (int)ex_settings_get_int(app.settings, "files", "autosave_max_ms");
        nc.journal_flush_ms = (int)ex_settings_get_int(app.settings, "files", "journal_flush_ms");
        ex_settings_apply_vault(app.settings, app.vault);
        app.notes = ex_notes_create(app.vault, &nc);
        app.session = ex_session_create(app.vault, NULL, 300);
        loaded = app.session ? ex_session_load(app.session, app.ws) : 0;
        (void)loaded;
        /* Pop-out windows cannot be opened as operating-system windows yet: their tabs come back to the main window
         * (as they would when such a window is closed), so no tab is ever out of reach. */
        while (ex_ws_window_count(app.ws) > 1) {
            ExId w = ex_ws_window_at(app.ws, 0) == ex_ws_main_window(app.ws) ? ex_ws_window_at(app.ws, 1) : ex_ws_window_at(app.ws, 0);
            if (ex_ws_close_window(app.ws, w) != 0) break;
        }
    } else {
        ex_ws_new_tab(app.ws, 0, "", "welcome");   /* no vault: one tab with the welcome text */
    }

    register_features_and_commands(&app);
    memset(&hooks, 0, sizeof hooks);
    hooks.make_content = make_content;
    hooks.content_closed = content_closed;
    hooks.tab_shown = tab_shown;
    hooks.new_tab = app.vault ? new_empty_tab : NULL;
    hooks.drop_file = drop_file;
    app.panes = ex_panes_create(app.ui, app.ws, &hooks, &app);

    /* window: ribbon | left sidebar | banner above the panes | right sidebar, and a status line */
    app.root = ex_widget_box(app.ui, 1, "Exhaust");
    app.root->role = EX_ROLE_WINDOW;
    app.root->width = ex_size_grow(); app.root->height = ex_size_grow();
    app.root->bg_token = EX_COLOR_BACKGROUND;
    row = ex_widget_box(app.ui, 0, "workspace");
    row->width = ex_size_grow(); row->height = ex_size_grow();
    ribbon = ex_widget_box(app.ui, 1, "ribbon");
    ribbon->width = ex_size_token(EX_SIZE_RIBBON_WIDTH, 1.0f);
    ribbon->height = ex_size_grow();
    ribbon->bg_token = EX_COLOR_BACKGROUND_ALT;
    ribbon->pad_x = 0.5f; ribbon->pad_y = 1.0f; ribbon->gap = 0.5f;
    ex_widget_add(ribbon, ribbon_button(&app, "Aa", "Switch light and dark theme", on_toggle_theme));
    ex_widget_add(ribbon, ribbon_button(&app, "\xC2\xAB", "Show or hide the left sidebar", on_toggle_left));
    ex_widget_add(ribbon, ribbon_button(&app, "\xC2\xBB", "Show or hide the right sidebar", on_toggle_right));
    ex_widget_add(ribbon, ribbon_button(&app, "Md", "Switch between live preview and source mode", on_toggle_mode_button));
    ex_widget_add(row, ribbon);

    left_content = app.vault ? ex_explorer_sidebar(app.ui, app.vault, on_open_file, &app, &app.tree) : info_label(&app, "No vault is open.");
    if (left_content) {
        left_content->width = ex_size_grow();
        left_content->height = ex_size_grow();
        left_content->bg_token = EX_COLOR_BACKGROUND_ALT;
    }
    if (app.vault && left_content) ex_explorer_set_new_note_handler(left_content, on_new_note, &app);
    app.left_content = left_content;
    if (app.vault && left_content) ex_explorer_set_vault_handler(left_content, on_vault_button, &app);
    on_files_feature("files", ex_features_enabled(app.features, "files"), &app);
    app.left_sb = ex_sidebar_create(app.ui, app.ws, EX_SIDE_LEFT, left_content, "Left sidebar");
    ex_widget_add(row, app.left_sb);

    col = ex_widget_box(app.ui, 1, "Note");
    col->width = ex_size_grow(); col->height = ex_size_grow();
    app.banner = ex_widget_box(app.ui, 0, "Notice");
    app.banner->width = ex_size_grow();
    app.banner->bg_token = EX_COLOR_SURFACE;
    app.banner->border_token = EX_COLOR_WARNING;
    app.banner->pad_x = 1; app.banner->pad_y = 0.5f; app.banner->gap = 1;
    app.banner->hidden = 1;
    app.banner_text = ex_widget_label(app.ui, "");
    app.banner_text->width = ex_size_grow();
    app.banner_a = ex_widget_button(app.ui, "", banner_a_click, &app);
    app.banner_b = ex_widget_button(app.ui, "", banner_b_click, &app);
    app.banner_c = ex_widget_button(app.ui, "", banner_c_click, &app);
    ex_widget_add(app.banner, app.banner_text);
    ex_widget_add(app.banner, app.banner_a);
    ex_widget_add(app.banner, app.banner_b);
    ex_widget_add(app.banner, app.banner_c);
    ex_widget_add(col, app.banner);
    ex_widget_add(col, ex_panes_root(app.panes));
    ex_widget_add(row, col);

    {
        ExWidget *right_content = info_label(&app, "Backlinks, outline and tags arrive in later sub-slices.");
        if (right_content) { right_content->bg_token = EX_COLOR_BACKGROUND_ALT; right_content->width = ex_size_grow(); right_content->height = ex_size_grow(); }
        app.right_sb = ex_sidebar_create(app.ui, app.ws, EX_SIDE_RIGHT, right_content, "Right sidebar");
        ex_widget_add(row, app.right_sb);
    }
    status = ex_widget_label(app.ui, app.vault ? "Typing, selecting, several cursors (Ctrl+Alt+Up or Down, Alt+click), undo and redo work. Ctrl+Plus and Ctrl+Minus zoom."
                                                : "No vault is open. Start with:  exhaust FOLDER");
    status->text_token = EX_COLOR_TEXT_MUTED;
    status->text_scale = 0.85f;
    status->width = ex_size_grow();
    status->pad_x = 1.0f; status->pad_y = 0.5f;
    status->bg_token = EX_COLOR_BACKGROUND_ALT;
    ex_widget_add(app.root, row);
    ex_widget_add(app.root, status);
    app.status = status;

    if (app.vault && !app.notes) set_status(&app, "Warning: the recovery journal could not be opened, so notes cannot be edited safely.");
    else if (app.notes && ex_notes_unsaved_count(app.notes) > 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "%zu note(s) have text restored from the recovery journal: open them to see it.", ex_notes_unsaved_count(app.notes));
        set_status(&app, msg);
    }
    app.focus_tab = ex_ws_group_active(app.ws, ex_ws_active_group(app.ws));

    signal(SIGTERM, on_signal);
#ifdef SIGHUP
    signal(SIGHUP, on_signal);
#endif
    g_wake_event = SDL_RegisterEvents(1);
    results = ex_queue_create(wake_ui, NULL);
    worker = ex_worker_start("font scan", 1, worker_job, NULL, results);
    memset(&job, 0, sizeof job);
    job.type = MSG_FONTS_READY;
    if (worker) ex_worker_post(worker, &job);

    ex_text_input_start(ex_renderer_window(app.r));
    relayout_and_draw(&app);
    while (running) {
        ExTextEvent te;
        ExKey key;
        int mods;
        int timed_out = !SDL_WaitEventTimeout(&e, 100); /* wakes ten times a second for autosave and the watcher */
        static int idle_ticks;
        if (g_signal) running = 0;
        if (timed_out) {
            if (++idle_ticks % 5 == 0) app.blink_on = !app.blink_on;   /* the caret blinks about every half second */
        } else if (e.type == g_wake_event) {
            ExMessage m;
            while (ex_queue_pop(results, &m))
                if (m.type == MSG_FONTS_READY) ex_renderer_set_font_db(app.r, (ExFontDb *)m.payload);
        } else if (e.type == SDL_EVENT_KEY_DOWN && run_shortcut(&app, &e.key)) {
            app.blink_on = 1;
        } else if (ex_text_event_decode(&e, &te)) {
            app.blink_on = 1;
            if (te.kind == EX_TEXT_COMMIT) ex_ui_text_input(app.ui, te.text, strlen(te.text));
            else if (app.editor && app.editor->editor) ex_editor_set_composition(app.editor->editor, te.text);
        } else if (ex_key_from_sdl(&e, &key, &mods)) {
            app.blink_on = 1;
            ex_ui_key(app.ui, key, mods);
        } else {
            app.blink_on = 1;
            switch (e.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (app.notes) ex_notes_flush(app.notes); /* saving on focus loss */
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                ex_renderer_set_display_scale(app.r, ex_renderer_detect_display_scale(app.r));
                break;
            case SDL_EVENT_DROP_FILE:
            case SDL_EVENT_DROP_TEXT: {
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(app.r));
                if (e.drop.data) ex_ui_drop(app.ui, app.root, e.type == SDL_EVENT_DROP_FILE, e.drop.data, e.drop.x * density, e.drop.y * density);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION: {
                float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
                float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(app.r));
                x *= density; y *= density; /* window coordinates to output pixels */
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    Uint64 now = SDL_GetTicks();
                    clicks = now - last_click < 400 ? clicks + 1 : 1;
                    last_click = now;
                    ex_ui_mouse(app.ui, app.root, EX_MOUSE_DOWN, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    ex_ui_mouse(app.ui, app.root, EX_MOUSE_UP, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.motion.state & SDL_BUTTON_LMASK) {
                    ex_ui_mouse(app.ui, app.root, EX_MOUSE_DRAG, x, y, ex_mods_from_sdl(), clicks);
                }
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(app.r));
                ExWidget *under = ex_ui_widget_at(app.root, e.wheel.mouse_x * density, e.wheel.mouse_y * density);
                while (under && under->type != EX_WIDGET_EDITOR) under = under->parent;   /* the editor under the pointer, not only the active one */
                if (!under) under = app.editor;
                if (under && under->editor) ex_editor_scroll(under->editor, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                if (ex_mods_from_sdl() & EX_MOD_PRIMARY) {
                    float before = zoom;
                    if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS) zoom *= 1.1f;
                    else if (e.key.key == SDLK_MINUS) zoom /= 1.1f;
                    else if (e.key.key == SDLK_0) zoom = 1.0f;
                    if (zoom < 0.5f) zoom = 0.5f;
                    if (zoom > 4.0f) zoom = 4.0f;
                    if (zoom != before) {
                        ex_renderer_set_zoom(app.r, zoom);
                        ex_settings_set_int(app.settings, "interface", "zoom_percent", (long)(zoom * 100.0f + 0.5f));   /* written at once */
                    }
                }
                break;
            default: break;
            }
        }
        if (app.notes) { ex_notes_tick(app.notes); handle_note_events(&app); }
        if (timed_out || e.type == g_wake_event) {
            capture_tab_state(&app);
            if (app.session) ex_session_tick(app.session, app.ws, (int64_t)SDL_GetTicks());
        }
        relayout_and_draw(&app);
        if (app.editor && app.editor->editor) {
            float cx, cy, cw, ch;
            ex_editor_caret_rect(app.editor->editor, app.r, &cx, &cy, &cw, &ch);
            ex_text_input_set_area(ex_renderer_window(app.r), (int)cx, (int)cy, 1, (int)ch);
        }
    }
    /* leaving (the window was closed, or SIGTERM, SIGHUP or the end of the session): everything is saved first */
    if (app.notes) {
        int unsaved = ex_notes_flush(app.notes);
        if (unsaved) fprintf(stderr, "exhaust: %d note(s) could not be written; their text is kept in the recovery journal.\n", unsaved);
    }
    capture_tab_state(&app);
    if (app.session) ex_session_destroy(app.session, app.ws);       /* the last layout is on disk before anything is torn down */
    app.session = NULL;
    ex_worker_stop(worker);
    {
        ExMessage m;
        while (ex_queue_pop(results, &m))
            if (m.type == MSG_FONTS_READY) ex_fontdb_free((ExFontDb *)m.payload);
    }
    ex_queue_destroy(results);
    stop_showing_diff(&app);
    ex_panes_free(app.panes);                                          /* contents are detached and closed; their notes are closed too */
    ex_notes_destroy(app.notes);
    ex_widget_free(app.root);
    free(app.tabnotes);
    ex_doc_free(app.demo_doc);
    ex_ws_free(app.ws);
    ex_commands_free(app.commands);
    ex_features_free(app.features);
    ex_settings_close(app.settings);               /* waits for the settings writes that are queued */
    ex_vault_close(app.vault);
    free(g_first_folder);
    ex_ui_free(app.ui);
    ex_theme_free(&app.theme);
    ex_renderer_destroy(app.r);
    return 0;
}
