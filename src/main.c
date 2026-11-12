#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_fontdb.h"
#include "exhaust_input.h"
#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_theme.h"
#include "exhaust_ui.h"
#include "exhaust_vault.h"
#include "exhaust_worker.h"

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

typedef struct App {
    ExRenderer *r;
    ExUi *ui;
    ExTheme theme;
    ExWidget *root, *editor, *tree, *status;
    ExWidget *banner, *banner_text, *banner_a, *banner_b, *banner_c;
    ExDoc *demo_doc;           /* shown when no note is open */
    ExDoc *diff_doc;           /* the conflict diff, while it is shown */
    ExVault *vault;
    ExNotes *notes;
    ExNote *note;
    Banner banner_kind;
    int blink_on;
    float zoom;
} App;

static void relayout_and_draw(App *a) {
    int w, h;
    ex_renderer_size(a->r, &w, &h);
    ex_ui_layout(a->ui, a->root, (float)w, (float)h);
    ex_renderer_clear(a->r, ex_theme_color(&a->theme, EX_COLOR_BACKGROUND));
    ex_ui_draw(a->ui, a->root, a->blink_on);
    ex_renderer_present(a->r);
}

/* ---- the banner above the editor ---- */

static void show_banner(App *a, Banner kind, const char *text, const char *a_label, const char *b_label, const char *c_label) {
    a->banner_kind = kind;
    a->banner->hidden = kind == BANNER_NONE;
    if (kind == BANNER_NONE) return;
    ex_widget_set_text(a->banner_text, text);
    free(a->banner_text->name);
    a->banner_text->name = malloc(strlen(text) + 1);
    if (a->banner_text->name) strcpy(a->banner_text->name, text);
    a->banner_a->hidden = !a_label; if (a_label) { ex_widget_set_text(a->banner_a, a_label); free(a->banner_a->name); a->banner_a->name = malloc(strlen(a_label) + 1); if (a->banner_a->name) strcpy(a->banner_a->name, a_label); }
    a->banner_b->hidden = !b_label; if (b_label) { ex_widget_set_text(a->banner_b, b_label); free(a->banner_b->name); a->banner_b->name = malloc(strlen(b_label) + 1); if (a->banner_b->name) strcpy(a->banner_b->name, b_label); }
    a->banner_c->hidden = !c_label; if (c_label) { ex_widget_set_text(a->banner_c, c_label); free(a->banner_c->name); a->banner_c->name = malloc(strlen(c_label) + 1); if (a->banner_c->name) strcpy(a->banner_c->name, c_label); }
}

static void set_status(App *a, const char *text) { if (a->status) ex_widget_set_text(a->status, text); }

static void show_note(App *a, ExNote *n) {
    a->note = n;
    ex_editor_set_doc(a->editor->editor, n ? ex_note_doc(n) : a->demo_doc);
    ex_editor_set_readonly(a->editor->editor, n == NULL);
    ex_ui_set_focus(a->ui, a->editor);
}

static void stop_showing_diff(App *a) {
    if (a->diff_doc) {
        show_note(a, a->note);
        ex_doc_free(a->diff_doc);
        a->diff_doc = NULL;
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
    if (!a->note || a->banner_kind != BANNER_CONFLICT) return;
    if (a->diff_doc) { stop_showing_diff(a); return; }
    diff = ex_note_conflict_diff(a->note);
    if (!diff) return;
    a->diff_doc = ex_doc_create(diff[0] ? diff : "(the two versions have the same text)\n", diff[0] ? strlen(diff) : 38);
    free(diff);
    ex_editor_set_doc(a->editor->editor, a->diff_doc);
    ex_editor_set_readonly(a->editor->editor, 1);
}

/* ---- opening notes ---- */

static void open_note(App *a, ExNote *n, ExOpenResult res) {
    char msg[600];
    stop_showing_diff(a);
    show_note(a, n);
    if (res == EX_OPEN_RECOVERED) {
        show_banner(a, BANNER_RECOVERED, "Exhaust closed unexpectedly. The text you typed was restored from the recovery journal.", "OK", NULL, NULL);
    } else if (res == EX_OPEN_CONFLICT && ex_note_has_conflict(n)) {
        show_banner(a, BANNER_CONFLICT, "This note was changed on disk after your unsaved edits. Both versions are kept. Which one should be saved?", "Keep my version", "Use the version on disk", "Show differences");
    } else if (ex_note_error(n)) {
        show_banner(a, BANNER_ERROR, ex_note_error(n), NULL, NULL, NULL);
    } else {
        show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
    }
    snprintf(msg, sizeof msg, "%s%s", ex_note_path(n), ex_note_exists_on_disk(n) ? "" : " (new: it is written when you type something)");
    set_status(a, msg);
}

static void on_open_file(ExWidget *w, const char *rel, void *user) {
    App *a = user;
    ExOpenResult res;
    ExNote *n;
    (void)w;
    if (ex_file_kind(rel) != EX_KIND_MARKDOWN && ex_file_kind(rel) != EX_KIND_BASE && ex_file_kind(rel) != EX_KIND_CANVAS) {
        char msg[400];
        snprintf(msg, sizeof msg, "%s cannot be shown yet (images, audio, video and PDF viewers come in sub-slice 6c).", rel);
        set_status(a, msg);
        return;
    }
    if (a->note && ex_note_dirty(a->note)) ex_note_save_now(a->note); /* switching notes saves the one we leave */
    n = ex_notes_open(a->notes, rel, &res);
    if (!n) {
        char msg[400];
        snprintf(msg, sizeof msg, res == EX_OPEN_INVALID_UTF8 ? "%s is not a UTF-8 text file, so Exhaust will not open or change it."
                                                               : "%s could not be opened.", rel);
        show_banner(a, BANNER_ERROR, msg, NULL, NULL, NULL);
        return;
    }
    open_note(a, n, res);
}

static void on_new_note(ExWidget *w, const char *parent_rel, void *user) {
    App *a = user;
    ExNote *n;
    (void)w;
    if (a->note && ex_note_dirty(a->note)) ex_note_save_now(a->note);
    n = ex_notes_new(a->notes, parent_rel, "Untitled");
    if (n) open_note(a, n, EX_OPEN_OK);
}

static void on_toggle_theme(ExWidget *w, void *user) {
    App *a = user;
    ExTheme next;
    (void)w;
    if (ex_theme_init_builtin(&next, a->theme.is_dark ? EX_THEME_LIGHT : EX_THEME_DARK) == 0) {
        ex_theme_free(&a->theme);
        a->theme = next;
        ex_ui_set_theme(a->ui, &a->theme);
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
            if (ev.note == a->note)
                show_banner(a, BANNER_CONFLICT, "This note was changed on disk while you have unsaved edits. Both versions are kept. Which one should be saved?",
                            "Keep my version", "Use the version on disk", "Show differences");
            break;
        case EX_NOTE_EVENT_TREE_CHANGED:
            ex_vault_rescan(a->vault);
            break;
        }
    }
}

int main(int argc, char **argv) {
    App app;
    SDL_Event e;
    ExQueue *results;
    ExWorker *worker;
    ExMessage job;
    ExWidget *row, *col;
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
        printf("Usage: exhaust [FOLDER]\n  FOLDER  open this folder of notes as a vault (it is remembered)\n  --version\n");
        return 0;
    }
    memset(&app, 0, sizeof app);
    app.zoom = 1.0f;
    app.blink_on = 1;
    app.r = ex_renderer_create("Exhaust", 960, 600);
    if (!app.r) {
        fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error());
        return 1;
    }
    if (ex_theme_init_builtin(&app.theme, EX_THEME_DARK) != 0) return 1;
    ex_renderer_set_display_scale(app.r, ex_renderer_detect_display_scale(app.r));
    app.ui = ex_ui_create(app.r, &app.theme, 16.0f);
    app.demo_doc = ex_doc_create(intro, strlen(intro));
    app.root = ex_sample_window(app.ui, app.demo_doc, on_toggle_theme, &app, &app.editor);
    app.status = app.root->last_child;
    row = app.root->first_child;

    /* the banner: a message with up to three buttons, above the editor */
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
    ex_widget_remove(row, app.editor);
    ex_widget_add(col, app.banner);
    ex_widget_add(col, app.editor);
    ex_widget_add(row, col);

    if (argc > 1) {
        app.vault = ex_vault_open(argv[1]);
        if (!app.vault) fprintf(stderr, "exhaust: cannot open %s as a vault\n", argv[1]);
    }
    if (app.vault) {
        ExRegistry *reg = ex_registry_load(NULL);
        ExNotesConfig nc;
        ExWidget *side = ex_explorer_sidebar(app.ui, app.vault, on_open_file, &app, &app.tree);
        if (reg) { /* remember the vault, and which one was opened last */
            const char *id = ex_registry_add(reg, ex_vault_path(app.vault));
            if (id) ex_registry_touch(reg, id, (int64_t)time(NULL));
            ex_registry_save(reg);
            ex_registry_free(reg);
        }
        if (side) {
            ex_widget_insert_before(row, side, col);
            ex_explorer_set_new_note_handler(side, on_new_note, &app);
        }
        memset(&nc, 0, sizeof nc);
        app.notes = ex_notes_create(app.vault, &nc);
        if (!app.notes) set_status(&app, "Warning: the recovery journal could not be opened, so notes cannot be edited safely. They are shown read-only.");
        else if (ex_notes_unsaved_count(app.notes) > 0) {
            char msg[200];
            snprintf(msg, sizeof msg, "%zu note(s) have text restored from the recovery journal: open them to see it.", ex_notes_unsaved_count(app.notes));
            set_status(&app, msg);
        }
        ex_editor_set_readonly(app.editor->editor, 1); /* until a note is open */
    }

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
        } else if (ex_text_event_decode(&e, &te)) {
            app.blink_on = 1;
            if (te.kind == EX_TEXT_COMMIT) ex_ui_text_input(app.ui, te.text, strlen(te.text));
            else if (app.editor->editor) ex_editor_set_composition(app.editor->editor, te.text);
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
            case SDL_EVENT_MOUSE_WHEEL:
                if (app.editor->editor) ex_editor_scroll(app.editor->editor, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ex_mods_from_sdl() & EX_MOD_PRIMARY) {
                    if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS) zoom *= 1.1f;
                    else if (e.key.key == SDLK_MINUS) zoom /= 1.1f;
                    else if (e.key.key == SDLK_0) zoom = 1.0f;
                    ex_renderer_set_zoom(app.r, zoom);
                }
                break;
            default: break;
            }
        }
        if (app.notes) { ex_notes_tick(app.notes); handle_note_events(&app); }
        if (app.editor->editor) {
            float cx, cy, cw, ch;
            relayout_and_draw(&app);
            ex_editor_caret_rect(app.editor->editor, app.r, &cx, &cy, &cw, &ch);
            ex_text_input_set_area(ex_renderer_window(app.r), (int)cx, (int)cy, 1, (int)ch);
        }
    }
    /* leaving (the window was closed, or SIGTERM, SIGHUP or the end of the session): everything is saved first */
    if (app.notes) {
        int unsaved = ex_notes_flush(app.notes);
        if (unsaved) fprintf(stderr, "exhaust: %d note(s) could not be written; their text is kept in the recovery journal.\n", unsaved);
    }
    ex_worker_stop(worker);
    {
        ExMessage m;
        while (ex_queue_pop(results, &m))
            if (m.type == MSG_FONTS_READY) ex_fontdb_free((ExFontDb *)m.payload);
    }
    ex_queue_destroy(results);
    stop_showing_diff(&app);
    ex_notes_destroy(app.notes);
    ex_widget_free(app.root);
    ex_doc_free(app.demo_doc);
    ex_vault_close(app.vault);
    ex_ui_free(app.ui);
    ex_theme_free(&app.theme);
    ex_renderer_destroy(app.r);
    return 0;
}
