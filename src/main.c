#include <limits.h>
#include <signal.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_commands.h"
#include "exhaust_datefmt.h"
#include "exhaust_templates.h"
#include "exhaust_compose.h"
#include "exhaust_slides.h"
#include "exhaust_canvasview.h"
#include "exhaust_baseview.h"
#include "exhaust_imageview.h"
#include "exhaust_audioview.h"
#include "exhaust_mediaview.h"
#include "exhaust_pdfview.h"
#include "exhaust_reading.h"
#include "exhaust_graphview.h"
#include "exhaust_history.h"
#include "exhaust_layouts.h"
#include "exhaust_fontdb.h"
#include "exhaust_input.h"
#include "exhaust_notes.h"
#include "exhaust_panes.h"
#include "exhaust_a11y_adapter.h"
#include "exhaust_popwin.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_session.h"
#include "exhaust_ribbon.h"
#include "exhaust_shell.h"
#include "exhaust_statusbar.h"
#include "exhaust_settings.h"
#include "exhaust_hotkeys.h"
#include "exhaust_themes.h"
#include "exhaust_settingsview.h"
#include "exhaust_theme.h"
#include "exhaust_findbar.h"
#include "exhaust_slash.h"
#include "exhaust_spell.h"
#include "exhaust_ui.h"
#include "exhaust_index.h"
#include "exhaust_graphpanel.h"
#include "exhaust_panels.h"
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

typedef enum Banner { BANNER_NONE, BANNER_RECOVERED, BANNER_CONFLICT, BANNER_ERROR, BANNER_HISTORY } Banner;

typedef struct TabNote { ExId tab; ExNote *note; } TabNote;

typedef struct App {
    char **tpl_abs; size_t tpl_n;      /* the templates offered by the last Insert template (6e-i) */
    char ex_source[600], merge_from[600], merge_into[600]; char *ex_content; size_t ex_content_len, ex_start, ex_end; int ex_newline;   /* the extraction and the merge in flight (6e-ii) */
    ExRenderer *r;
    ExUi *ui;
    ExTheme theme;
    ExWidget *root, *editor, *tree, *status;     /* editor: the editor of the active tab, or NULL */
    ExFindBar *findbar;
    ExWidget *title;               /* the inline title of the active tab (a child of its editor), or NULL */
    ExGraphModel *gm;              /* the graph's data and layout, while a graph tab exists */
    ExGraphModel *gm_local;        /* the local graph's (5c), while a local graph tab exists */
    char pdf_goto_path[700]; int pdf_goto_page, pdf_goto_s, pdf_goto_e, pdf_goto_set;   /* a link to a PDF page or selection, while the tab opens (6c-v) */
    char audio_embed_path[700];    /* an audio embed that was pressed: plays when its file is loaded */
    char pending_url[600];         /* a web address a PDF link asked to open, while the user is asked (6c-iv) */
    ExMpv *mpv;                    /* the libmpv player, one at a time (6c-iii) */
    char mpv_path[600];
    ExPlayer *player;              /* the one audio output of the window (6c-ii) */
    ExImageCache *icache;          /* the textures of pictures: image tabs, canvas image cards (6c-i) */
    unsigned long base_gen_seen;   /* the index generation the open bases were run at (6b-iii) */
    ExCanvasCache *ccache;         /* the start of the notes that canvas cards show (6a) */
    ExGraphOptions gopts;          /* the graph's settings (5b): filters, groups, forces, display; kept in the session's [graph] section */
    char *graph_note;              /* the note last open in a tab: drawn in the focus color (the graph's own tab is not a note) */
    ExRibbon *ribbon;                        /* the ribbon (7f) */
    ExWidget *ribbon_line, *rb_graph, *rb_mode, *rb_left, *rb_right;   /* the line beside it, and the buttons the program keeps track of */
    ExStatusBar *sbar;                       /* the status bar (7f) */
    /* pop-out windows (7g): r, ui, root and panes are those of the window the code is working for (the main window unless an event or a frame of a pop-out is being handled) */
    ExPopWin *pops[8]; size_t npops;
    ExPopWin *cur_pop;                       /* the pop-out window the code is working for, or NULL */
    ExRenderer *main_r; ExUi *main_ui; ExWidget *main_root; ExPanes *main_panes; ExPanels *saved_panels;
    ExFontDb *fontdb;                        /* the main window's font database, shared with the pop-outs */
    ExA11yAdapter *a11y;                     /* the main window's screen reader interface (7h) */
    char sb_mode[16]; int sb_save, sb_poll;  /* what the bar shows now, so it is only touched when something changes */
    void (*sel_chosen)(void *, size_t);      /* the choice a settings select is waiting for (7a) */
    void *sel_arg;
    int main_collapsed, main_prev_width; ExSide main_collapsed_side;   /* app.toggle-main-text-view: the main text view is a handle, and what to give back */
    Uint64 title_warn_until;       /* the bubble under the title that names the characters a file name cannot have */
    ExSlashMenu *slash;
    ExSpell *spell;
    ExWidget *wordcount;          /* the word count at the right of the status line */
    unsigned long wc_version;     /* what the count was computed for */
    const void *wc_doc;
    unsigned wc_tick;
    size_t wc_sel;
    char spell_orig[64];          /* the misspelled word the replace command is cycling suggestions for */
    char spell_sug[8][EX_SPELL_WORD_MAX];
    size_t spell_nsug, spell_idx;
    ExWidget *banner, *banner_text, *banner_a, *banner_b, *banner_c;
    ExWidget *left_sb, *right_sb;
    ExDoc *demo_doc;           /* shown when there is no vault */
    ExDoc *diff_doc;           /* the conflict diff, while it is shown */
    ExDoc *shown_before_diff;
    ExWidget *diff_editor;     /* the editor the diff is shown in (the active one when it was opened) */
    ExEditorMode diff_mode;    /* the mode that editor had */
    ExLayouts *layouts;                      /* saved workspace layouts, workspaces.ini (7e) */
    char pending_layout[64];                 /* the name a question about a layout is about */
    char *hist_old; size_t hist_old_len;     /* the earlier version being compared with the note, and its note and label (7e) */
    char hist_rel[512], hist_label[200];
    ExVault *vault;
    ExNotes *notes;
    ExNote *note;              /* the note of the active tab */
    ExNote *banner_note;       /* the note the banner was last computed for */
    ExWorkspace *ws;
    ExPanes *panes;
    ExSession *session;
    ExSettings *settings;
    ExHotkeys *hotkeys;                      /* hotkeys.ini and the shortcuts (7c) */
    ExThemes *themes;                        /* the theme files and snippets, built on a worker thread (7d) */
    int sys_hc;                              /* does the system ask for high contrast: -1 not asked yet (7f) */
    void (*cap_cb)(void *, const char *);    /* the settings screen is waiting for a key press to make a shortcut of (7c) */
    void *cap_arg;
    Uint64 swallow_text_until;               /* the text event that follows a captured key press is not typed anywhere */
    ExFeatures *features;
    ExCommands *commands;
    ExWidget *left_content;
    ExPanels *panels;
    char *sel_path; size_t sel_offset;   /* put the caret here once the note is showing */
    TabNote *tabnotes; size_t ntabnotes, captabnotes;
    ExId focus_tab;            /* the tab whose editor should get the focus when it exists */
    ExId menu_group;           /* the pane whose three-dot menu is open (5d) */
    int right_tabs, had_side_tabs;   /* the right sidebar shows its tabs, not its panels; it had tabs the frame before (5e) */
    Banner banner_kind;
    int blink_on;
} App;

static char *dup_text(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void sync_ui(App *a);
static ExUi *ui_of(App *a, const ExWidget *w);
static const char *host_active_path(void *u);

static void set_cursor_shape(int shape) {
    static SDL_Cursor *arrow, *resize;
    static int current = -1;
    if (shape == current) return;
    if (!arrow) arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!resize) resize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    if (shape == 1 && resize) SDL_SetCursor(resize); else if (arrow) SDL_SetCursor(arrow);
    current = shape;
}

static unsigned long g_drawn_revision;
static void host_open_note(void *u, const char *path, size_t offset, int new_tab);

static void relayout_and_draw(App *a) {
    int w, h;
    static int log_frames = -1;
    Uint64 t0 = SDL_GetPerformanceCounter(), t1, t2, t3;
    if (log_frames < 0) log_frames = getenv("EXHAUST_FRAME_LOG") != NULL;   /* a developer's aid: the time of each frame, on stderr */
    sync_ui(a);
    ex_renderer_size(a->r, &w, &h);
    t1 = SDL_GetPerformanceCounter();
    ex_ui_layout(a->ui, a->root, (float)w, (float)h);
    t2 = SDL_GetPerformanceCounter();
    ex_renderer_clear(a->r, ex_theme_color(&a->theme, EX_COLOR_BACKGROUND));
    ex_ui_draw(a->ui, a->root, a->blink_on);
    t3 = SDL_GetPerformanceCounter();
    if (a->panels && ex_panels_overlay(a->panels)) {                  /* a dialog or the page preview, over the window */
        ExWidget *ov = ex_panels_overlay(a->panels);
        ex_ui_layout(a->ui, ov, (float)w, (float)h);
        ex_ui_draw(a->ui, ov, a->blink_on);
    }
    if (a->title && !a->title->hidden && SDL_GetTicks() < a->title_warn_until) {        /* the bubble under the title */
        static const char msg[] = "File name cannot contain any of these characters: \\ / : # ^ [ ] |";
        ExFont *f = ex_ui_font(a->ui, EX_FONT_TOKEN_INTERFACE, 0);
        float tw = ex_text_measure(a->r, f, 16.0f * 0.9f, msg, strlen(msg)), pad = (float)ex_px(a->r, 12);
        ExFontMetrics fm;
        if (ex_text_metrics(a->r, f, 16.0f * 0.9f, &fm) == 0) {
            float bx = a->title->x + (float)ex_px(a->r, 24), by = a->title->y + a->title->h + (float)ex_px(a->r, 6), bw = tw + 2 * pad, bh = (float)fm.line_height + pad;
            ex_fill_triangle(a->r, bx + (float)ex_px(a->r, 14), by - (float)ex_px(a->r, 6), bx + (float)ex_px(a->r, 8), by + 1, bx + (float)ex_px(a->r, 20), by + 1, ex_theme_color(&a->theme, EX_COLOR_ERROR));
            ex_fill_round_rect(a->r, bx, by, bw, bh, (float)ex_px(a->r, 8), ex_theme_color(&a->theme, EX_COLOR_ERROR));
            ex_text_draw(a->r, f, 16.0f * 0.9f, bx + pad, by + pad / 2, msg, strlen(msg), ex_theme_color(&a->theme, EX_COLOR_BACKGROUND));
        }
    }
    {   /* a developer's aid: EXHAUST_SHOT=file.ppm writes the sixth frame to the file and ends the program */
        static int frames_drawn;
        const char *shot = getenv("EXHAUST_SHOT");
        if (shot && ++frames_drawn == 6) {
            int sw, sh;
            uint8_t *px = ex_renderer_read_rgb(a->r, &sw, &sh);
            FILE *f = px ? fopen(shot, "wb") : NULL;
            if (f) { fprintf(f, "P6\n%d %d\n255\n", sw, sh); fwrite(px, 1, (size_t)sw * (size_t)sh * 3, f); fclose(f); }
            free(px);
            exit(0);
        }
    }
    ex_renderer_present(a->r);
    if (log_frames) {
        double f = 1000.0 / (double)SDL_GetPerformanceFrequency();
        static int slow_only = -1;
        if (slow_only < 0) { const char *v = getenv("EXHAUST_FRAME_LOG"); slow_only = v && !strcmp(v, "slow"); }      /* EXHAUST_FRAME_LOG=slow: only the frames over the 16 ms budget (without the wait for the screen) */
        if (slow_only && (double)(t3 - t0) * f < 16.0) return;
        fprintf(stderr, "frame: sync %.1f ms, layout %.1f ms, draw %.1f ms, present %.1f ms\n", (double)(t1 - t0) * f, (double)(t2 - t1) * f, (double)(t3 - t2) * f, (double)(SDL_GetPerformanceCounter() - t3) * f);
    }
}

/* ---- a developer's aid: EXHAUST_SCRIPT=file feeds scripted input to the real program (see tools/run_script.sh) ----
 * One command a line: wait MS | move X Y | down X Y | up X Y | rdown X Y | text STRING | key enter|escape|down|tab | shot FILE.ppm | quit.
 * The commands become SDL events, so they take the same path as a person's input. */
typedef struct Script { char *text; char *next; Uint64 until; int button_down; int active; int window; } Script;     /* window: 0 the main window, n the n-th pop-out (7g) */
static Script g_script;

static void script_start(void) {
    const char *f = getenv("EXHAUST_SCRIPT");
    if (!f) return;
    g_script.text = ex_platform_read_file(f, NULL);
    g_script.next = g_script.text;
    g_script.active = g_script.text != NULL;
}

static void script_shot(App *a, const char *file);

static Uint32 script_window_id(App *a) {
    int n = g_script.window;
    return n > 0 && (size_t)n <= a->npops ? ex_popwin_sdl_id(a->pops[n - 1]) : 0;
}

/* The events of the script carry the id of the window they are for, as a person's input does. */
static void script_push(App *a, SDL_Event *e) {
    Uint32 id = script_window_id(a);
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION: e->motion.windowID = id; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: e->button.windowID = id; break;
    case SDL_EVENT_MOUSE_WHEEL: e->wheel.windowID = id; break;
    case SDL_EVENT_TEXT_INPUT: e->text.windowID = id; break;
    case SDL_EVENT_KEY_DOWN: e->key.windowID = id; break;
    default: break;
    }
    SDL_PushEvent(e);
}

static void script_step(App *a) {
    char *line, *end;
    SDL_Event e;
    float x = 0, y = 0;
    if (!g_script.active || SDL_GetTicks() < g_script.until) return;
    if (!g_script.next || !*g_script.next) { g_script.active = 0; return; }
    line = g_script.next;
    end = strchr(line, '\n');
    if (end) { *end = 0; g_script.next = end + 1; } else g_script.next = line + strlen(line);
    memset(&e, 0, sizeof e);
    if (!strncmp(line, "wait ", 5)) g_script.until = SDL_GetTicks() + (Uint64)atoi(line + 5);
    else if (!strncmp(line, "move ", 5) && sscanf(line + 5, "%f %f", &x, &y) == 2) {
        e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = x; e.motion.y = y; e.motion.state = g_script.button_down ? SDL_BUTTON_LMASK : 0;
        script_push(a, &e);
    } else if ((!strncmp(line, "down ", 5) || !strncmp(line, "rdown ", 6)) && sscanf(line + (line[0] == 'r' ? 6 : 5), "%f %f", &x, &y) == 2) {
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = line[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y; e.button.clicks = 1;
        g_script.button_down = line[0] != 'r';
        script_push(a, &e);
    } else if (!strncmp(line, "up ", 3) && sscanf(line + 3, "%f %f", &x, &y) == 2) {
        e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
        g_script.button_down = 0;
        script_push(a, &e);
    } else if (!strncmp(line, "wheel ", 6) && sscanf(line + 6, "%f %f %f", &x, &y, &e.wheel.y) == 3) {      /* wheel X Y DY: DY < 0 scrolls down */
        e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.mouse_x = x; e.wheel.mouse_y = y;
        script_push(a, &e);
    } else if (!strncmp(line, "text ", 5)) {
        static char typed[256];
        snprintf(typed, sizeof typed, "%s", line + 5);
        e.type = SDL_EVENT_TEXT_INPUT; e.text.text = typed;
        script_push(a, &e);
    } else if (!strncmp(line, "key ", 4)) {
        /* key NAME, or key MODS+NAME with the mods ctrl, shift, alt, gui and mod (Ctrl, or Cmd on a Mac) joined by +: ctrl+alt+j, mod+comma, shift+tab. A name is enter, escape, up, down,
         * left, right, tab, space, comma, period, or one letter or digit. */
        const char *spec = line + 4, *name = spec, *plus;
        SDL_Keymod mod = 0;
        SDL_Keycode k = SDLK_UNKNOWN;
        while ((plus = strchr(name, '+')) != NULL) {
            size_t n = (size_t)(plus - name);
            if (n == 4 && !strncmp(name, "ctrl", 4)) mod |= SDL_KMOD_CTRL;
            else if (n == 3 && !strncmp(name, "mod", 3)) {                                 /* the primary key: Ctrl, or Cmd on a Mac */
#ifdef __APPLE__
                mod |= SDL_KMOD_GUI;
#else
                mod |= SDL_KMOD_CTRL;
#endif
            }
            else if (n == 5 && !strncmp(name, "shift", 5)) mod |= SDL_KMOD_SHIFT;
            else if (n == 3 && !strncmp(name, "alt", 3)) mod |= SDL_KMOD_ALT;
            else if (n == 3 && !strncmp(name, "gui", 3)) mod |= SDL_KMOD_GUI;
            name = plus + 1;
        }
        k = !strcmp(name, "enter") ? SDLK_RETURN : !strcmp(name, "escape") ? SDLK_ESCAPE : !strcmp(name, "down") ? SDLK_DOWN : !strcmp(name, "up") ? SDLK_UP :
            !strcmp(name, "left") ? SDLK_LEFT : !strcmp(name, "right") ? SDLK_RIGHT : !strcmp(name, "tab") ? SDLK_TAB : !strcmp(name, "space") ? SDLK_SPACE :
            !strcmp(name, "comma") ? SDLK_COMMA : !strcmp(name, "period") ? SDLK_PERIOD : (strlen(name) == 1 ? (SDL_Keycode)name[0] : SDLK_UNKNOWN);
        e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.mod = mod; e.key.down = true;
        script_push(a, &e);
    } else if (!strncmp(line, "window ", 7)) {                                   /* window N: the next events and shots are for the main window (0) or the N-th pop-out (7g) */
        g_script.window = atoi(line + 7);
    } else if (!strncmp(line, "closewindow ", 12)) {                             /* closewindow N: the close button of the N-th pop-out */
        SDL_Event c;
        memset(&c, 0, sizeof c);
        c.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED; c.window.windowID = a->npops >= (size_t)atoi(line + 12) && atoi(line + 12) > 0 ? ex_popwin_sdl_id(a->pops[atoi(line + 12) - 1]) : 0;
        SDL_PushEvent(&c);
    } else if (!strncmp(line, "shot ", 5)) {
        script_shot(a, line + 5);
    } else if (!strcmp(line, "quit")) { SDL_Event q; memset(&q, 0, sizeof q); q.type = SDL_EVENT_QUIT; SDL_PushEvent(&q); }
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
static int is_reading(const char *mode) { return mode && strcmp(mode, "reading") == 0; }

static void stop_showing_diff(App *a) {
    if (a->diff_doc) {
        if (a->diff_editor && a->diff_editor->editor) {
            ex_editor_set_doc(a->diff_editor->editor, a->shown_before_diff);
            ex_editor_set_readonly(a->diff_editor->editor, a->note == NULL);
            ex_editor_set_mode(a->diff_editor->editor, a->diff_mode);
        }
        ex_doc_free(a->diff_doc);
        a->diff_doc = NULL;
        a->shown_before_diff = NULL;
        a->diff_editor = NULL;
    }
}

/* Shows text (a diff) in the editor of the active note, read only and as it is, until stop_showing_diff. */
static void show_diff_doc(App *a, const char *text) {
    if (!a->note || !a->editor || !a->editor->editor) return;
    stop_showing_diff(a);
    a->diff_doc = ex_doc_create(text, strlen(text));
    a->shown_before_diff = ex_note_doc(a->note);
    a->diff_editor = a->editor;
    a->diff_mode = ex_editor_mode(a->editor->editor);
    ex_editor_set_doc(a->editor->editor, a->diff_doc);
    ex_editor_set_readonly(a->editor->editor, 1);
    ex_editor_set_mode(a->editor->editor, EX_EDITOR_PLAIN);              /* a diff is shown as it is, not as Markdown */
}

static int host_replace_text(void *u, const char *path, const char *text, size_t len);

static void banner_a_click(ExWidget *w, void *user) { /* "Keep my version", or "OK" */
    App *a = user;
    (void)w;
    stop_showing_diff(a);
    if (a->banner_kind == BANNER_HISTORY) {                         /* Restore this version (7e) */
        if (a->note && a->hist_old && !strcmp(ex_note_path(a->note), a->hist_rel)) {
            ex_notes_snapshot_now(a->notes, a->note);              /* the text as it is now is kept first, so the restore can be undone from the history too */
            if (host_replace_text(a, a->hist_rel, a->hist_old, a->hist_old_len) == 0) set_status(a, "Restored the earlier version. Undo (Ctrl+Z) brings back the text you had; it is also in the history.");
            else set_status(a, "The earlier version could not be put back.");
        }
        free(a->hist_old); a->hist_old = NULL;
    }
    if (a->banner_kind == BANNER_CONFLICT && a->note) { ex_note_resolve_conflict(a->note, EX_KEEP_MINE); set_status(a, "Kept your version; the other one was copied to the recovery folder."); }
    show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
}

static void banner_b_click(ExWidget *w, void *user) { /* "Use the version on disk" */
    App *a = user;
    (void)w;
    stop_showing_diff(a);
    if (a->banner_kind == BANNER_HISTORY) { free(a->hist_old); a->hist_old = NULL; }       /* Back to the note */
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
    show_diff_doc(a, diff[0] ? diff : "(the two versions have the same text)\n");
    free(diff);
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

/* ---- the graph view (sub-slice 5a) ---- */

static void host_open_note(void *u, const char *path, size_t offset, int new_tab);
static void follow_note_link(App *a, const char *target, int create);
static void set_status(App *a, const char *text);

/* A click on a node: a note opens, a name that is not a note yet is made (written when something is typed), other files are a later slice. */
static void graph_open(void *u, const char *path, int kind, int new_tab) {
    App *a = u;
    if (kind == EX_GRAPH_NOTE) host_open_note(a, path, (size_t)-1, new_tab);
    else if (kind == EX_GRAPH_UNRESOLVED) follow_note_link(a, path, 1);
    else set_status(a, "Images and other files open in a tab in a later sub-slice.");
}

static ExGraphModel *ensure_gm(App *a) {
    if (!a->gm) { a->gm = ex_graphmodel_create(); if (a->gm) ex_graphmodel_set_options(a->gm, &a->gopts); }
    return a->gm;
}

static ExGraphModel *ensure_gm_local(App *a) {
    if (!a->gm_local) { a->gm_local = ex_graphmodel_create(); if (a->gm_local) { ex_graphmodel_set_local(a->gm_local, 1); ex_graphmodel_set_options(a->gm_local, &a->gopts); } }
    return a->gm_local;
}

static void host_local_graph_data(void *u, ExGraph *g) {
    App *a = u;
    if (ensure_gm_local(a)) ex_graphmodel_set_graph(a->gm_local, g);
    else { ex_graph_free(g); free(g); }
}

static void host_graph_data(void *u, ExGraph *g) {
    App *a = u;
    if (ensure_gm(a)) ex_graphmodel_set_graph(a->gm, g);
    else { ex_graph_free(g); free(g); }
}

/* A right click on a node: the menu a note has in the file explorer, or Create note for a name nothing answers to yet. */
static void graph_context(void *u, const char *path, int kind, float x, float y) {
    App *a = u;
    if (!a->panels) return;
    if (kind == EX_GRAPH_NOTE) ex_panels_file_menu_at(a->panels, path, x, y);
    else if (kind == EX_GRAPH_UNRESOLVED) ex_panels_unresolved_menu_at(a->panels, path, x, y);
}

static void graph_set_key(void *u, const char *key, const char *value) { ex_ws_extra_set(((App *)u)->ws, "graph", key, value); }
static const char *graph_get_key(void *u, const char *key) { return ex_ws_extra_get(((App *)u)->ws, "graph", key); }

/* The gear panel changed something: remember it in the session and, when it is a filter or a group, have the graph read again with it. */
static void graph_changed(void *u, const ExGraphOptions *o, int filters) {
    App *a = u;
    a->gopts = *o;
    ex_graphopts_save(o, graph_set_key, a);
    if (a->gm) ex_graphmodel_set_options(a->gm, o);                       /* both graphs share the settings */
    if (a->gm_local) ex_graphmodel_set_options(a->gm_local, o);
    if (filters && a->panels) ex_panels_graph_options(a->panels, o);
}

/* The keyboard asks for the page preview of a node: the panels show it as they do for the pointer with Ctrl held, at the node's place. */
static void graph_preview(void *u, const char *path, int kind, float x, float y) {
    App *a = u;
    (void)path; (void)kind;
    if (a->panels && a->root) ex_panels_pointer(a->panels, a->root, x, y, path != NULL);
}

static ExWidget *make_local_graph(App *a) {
    ExGraphHandlers h;
    ExWidget *gw;
    memset(&h, 0, sizeof h);
    h.user = a; h.open = graph_open; h.context = graph_context; h.preview = graph_preview;
    if (!ensure_gm_local(a)) return info_label(a, "The graph cannot be shown: out of memory.");
    gw = ex_widget_graph(a->ui, a->gm_local, "Local graph", &h);
    if (gw) ex_graphpanel_create(a->ui, gw, a->gm_local, 1, graph_changed, a);
    return gw;
}

static ExWidget *make_graph(App *a) {
    ExGraphHandlers h;
    ExWidget *gw;
    memset(&h, 0, sizeof h);
    h.user = a; h.open = graph_open; h.context = graph_context; h.preview = graph_preview;
    if (!ensure_gm(a)) return info_label(a, "The graph cannot be shown: out of memory.");
    gw = ex_widget_graph(a->ui, a->gm, "Graph view", &h);
    if (gw) ex_graphpanel_create(a->ui, gw, a->gm, 0, graph_changed, a);
    return gw;
}

/* What a tab shows. Notes open here (not when the session is restored), so a note that is not on
 * screen costs nothing, and a missing file only shows a message: its tab stays in the session. */
/* A note of the vault by its name (case-insensitive, without the extension); the link rules of 4a will replace this. */
static void open_path(App *a, ExId group, const char *rel);
static const ExNode *find_file_node(const ExNode *n, const char *text);
static void open_file_target(App *a, const char *path, const char *frag);
static int strcasecmp_ascii(const char *a, const char *b);
static const ExNode *find_note_node(const ExNode *n, const char *name, size_t len) {
    size_t i;
    if (!n) return NULL;
    if (!n->is_dir) {
        const char *base = strrchr(n->rel, '/');
        size_t bl;
        base = base ? base + 1 : n->rel;
        bl = strlen(base);
        if (bl > 3 && strcmp(base + bl - 3, ".md") == 0) bl -= 3;
        if (bl == len) { size_t k; for (k = 0; k < len && tolower((unsigned char)base[k]) == tolower((unsigned char)name[k]); k++) {} if (k == len) return n; }
        return NULL;
    }
    for (i = 0; i < n->nkids; i++) { const ExNode *f = find_note_node(n->kids[i], name, len); if (f) return f; }
    return NULL;
}

/* Follows a link to a note. A note that does not exist is made only when the user asked for it with Ctrl (Command on a Mac) and a
 * click: nothing creates a note by itself. */
static void follow_note_link(App *a, const char *target, int create) {
    char msg[500], name[300], path[700];
    size_t len = strcspn(target, "#");
    const ExNode *n;
    size_t i, o = 0;
    if (!a->vault || !len) return;
    {   /* a link to a picture, audio, video, PDF or canvas (`[[f.pdf#page=3&selection=120,148]]`) opens that file (6c-v) */
        char file[700], frag[200];
        ex_embed_split(target, file, sizeof file, NULL, 0, frag, sizeof frag);
        if (ex_embed_media_kind(file)) {
            const ExNode *fn = find_file_node(ex_vault_root(a->vault), file);
            if (fn) { open_file_target(a, fn->rel, frag); return; }
        }
    }
    n = find_note_node(ex_vault_root(a->vault), target, len);
    if (n) { host_open_note(a, n->rel, (size_t)-1, 0); return; }
    for (i = 0; i < len && o + 1 < sizeof name; i++) {                       /* a Markdown link's address: %20 and the extension */
        if (target[i] == '%' && i + 2 < len + 0 && isxdigit((unsigned char)target[i + 1]) && isxdigit((unsigned char)target[i + 2])) { char h[3] = {target[i + 1], target[i + 2], 0}; name[o++] = (char)strtol(h, NULL, 16); i += 2; }
        else name[o++] = target[i];
    }
    name[o] = 0;
    if (o > 3 && !strcmp(name + o - 3, ".md")) name[o -= 3] = 0;
    {
        const char *slash = strrchr(name, '/');
        if (!(slash ? slash[1] : name[0]) || !ex_name_valid(slash ? slash + 1 : name)) { snprintf(msg, sizeof msg, "\"%s\" is not a name a note can have.", name); set_status(a, msg); return; }
    }
    if (!create) {
        snprintf(msg, sizeof msg, "The note \"%s\" does not exist yet. Ctrl+click the link (Command on a Mac) to create it.", name);
        set_status(a, msg);
        return;
    }
    snprintf(path, sizeof path, "%s.md", name);
    host_open_note(a, path, (size_t)-1, 0);                                  /* made now, written when something is typed */
    snprintf(msg, sizeof msg, "New note \"%s\". It is written when you type something.", name);
    set_status(a, msg);
}

static void on_editor_link_click(void *user, const char *target, const char *heading, const char *block) {
    (void)heading; (void)block;
    if (target && target[0]) follow_note_link(user, target, 1);
}

static void pdf_apply_goto(App *a, const char *path);
static void embed_click(App *a, const char *target);
static int embed_resolve(void *u, const char *name, char *path, size_t cap);
static int embed_audio_state(void *u, const char *path);

static void on_reading_link(ExRSpanKind kind, const char *target, void *user) {
    App *a = user;
    char msg[400];
    if (kind == EX_RSK_EMBED) { embed_click(a, target); return; }
    if (kind == EX_RSK_LINK_EXTERNAL && target && (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0 || strncmp(target, "mailto:", 7) == 0)) {
        if (!SDL_OpenURL(target)) { snprintf(msg, sizeof msg, "Could not open %s", target); set_status(a, msg); }
    } else if (kind == EX_RSK_LINK_INTERNAL && target && a->vault) {
        follow_note_link(a, target, (ex_mods_from_sdl() & EX_MOD_PRIMARY) != 0);
    } else if (kind == EX_RSK_TAG && target) {
        if (a->panels) { char q[300]; snprintf(q, sizeof q, "tag:#%s", target[0] == '#' ? target + 1 : target); ex_panels_search_for(a->panels, q); }
    } else if (kind == EX_RSK_LINK_EXTERNAL) set_status(a, "Only web and e-mail links are opened.");
}

static void make_title(App *a, ExWidget *ed, ExId tab);

/* ---- the settings screen (sub-slice 7a) ---- */

static void settings_menu_chosen(ExPanels *p, int id, void *arg) {
    App *a = arg;
    void (*fn)(void *, size_t) = a->sel_chosen;
    void *fa = a->sel_arg;
    (void)p;
    a->sel_chosen = NULL; a->sel_arg = NULL;
    if (fn && id >= 0) fn(fa, (size_t)id);
}

static void settings_choose(void *u, const char *const *labels, size_t n, size_t current, float x, float y, void (*chosen)(void *, size_t), void *arg) {
    App *a = u;
    ExMenuItem items[16];
    size_t i;
    (void)current;
    if (!a->panels || n == 0 || n > 16) return;
    for (i = 0; i < n; i++) { ExMenuItem it = {labels[i], 0, (int)i, 0, 0}; items[i] = it; }
    a->sel_chosen = chosen; a->sel_arg = arg;
    ex_panels_popup(a->panels, x, y, items, n, settings_menu_chosen, a);
}

static void settings_capture(void *u, void (*done)(void *, const char *), void *arg) { App *a = u; a->cap_cb = done; a->cap_arg = arg; }   /* the next key press goes to the Hotkeys page (7c) */
static int settings_set_feature(void *u, const char *key, int enabled) { return ex_features_set(((App *)u)->features, key, enabled); }

static int theme_sync(App *a);
static void apply_history(App *a);
static void apply_zoom(App *a);
static void apply_ribbon(App *a);
static void settings_action(void *u, const char *id);
static void settings_changed(void *u, const char *section, const char *key) {
    App *a = u;
    if (!strcmp(section, "interface") && !strcmp(key, "zoom_percent")) apply_zoom(a);
    if (!strcmp(section, "interface") && (!strcmp(key, "show_ribbon") || !strcmp(key, "ribbon_items"))) apply_ribbon(a);
    if (!strcmp(section, "file_recovery")) apply_history(a);                   /* the interval or the days kept (7e) */
    if (!strcmp(section, "appearance")) theme_sync(a);                         /* the colour scheme, the theme, the accent colour or the snippets changed */
    (void)key;
}
static const ExTheme *current_theme_cb(void *u);

static ExWidget *make_settings(App *a) {
    ExSettingsHost h;
    ExSettingsView *sv;
    memset(&h, 0, sizeof h);
    h.user = a; h.choose = settings_choose; h.changed = settings_changed; h.version = exhaust_version(); h.set_feature = settings_set_feature; h.hotkeys = a->hotkeys; h.capture = settings_capture; h.themes = a->themes; h.current_theme = current_theme_cb; h.action = settings_action;
    sv = ex_settingsview_create(a->ui, a->settings, &h);
    return sv ? ex_settingsview_widget(sv) : info_label(a, "The settings cannot be shown.");
}

/* ---- the canvas view (6a) ---- */

static void on_open_file(ExWidget *w, const char *rel, void *user);

static void canvas_want_text(void *u, const char *key) { App *a = u; if (a->panels) ex_panels_file_text(a->panels, key); }
static void host_file_text(void *u, const char *key, const char *text) { App *a = u; ex_canvascache_set(a->ccache, key, text); }
static void canvas_open_file(void *u, const char *path, const char *sub, int new_tab) { (void)sub; host_open_note(u, path, (size_t)-1, new_tab); }
static void canvas_open_url(void *u, const char *url) { on_reading_link(EX_RSK_LINK_EXTERNAL, url, u); }
static void canvas_message(void *u, const char *text) { set_status(u, text); }
static int host_file_op(void *u, int what, const char *path, const char *arg);
static void pdf_load(void *u, const char *path) { App *a = u; if (a->panels) ex_panels_load_pdf(a->panels, path); }
static void pdf_url_answer(int button, void *arg) { App *a = arg; if (button == 1 && a->pending_url[0]) on_reading_link(EX_RSK_LINK_EXTERNAL, a->pending_url, a); a->pending_url[0] = 0; }
/* A link in a PDF to a web address: shown, and opened only when the user says so, through the path of an external link in a note. */
static void pdf_open_url(void *u, const char *url) {
    App *a = u;
    char msg[800];
    if (!a->panels) return;
    snprintf(a->pending_url, sizeof a->pending_url, "%s", url);
    snprintf(msg, sizeof msg, "This PDF links to a web address:\n\n%s\n\nOpen it in your browser?", url);
    ex_panels_ask_confirm(a->panels, "Open a web link?", msg, "Open", "Cancel", NULL, pdf_url_answer, a);
}
static void pdf_clipboard(void *u, const char *text) { (void)u; SDL_SetClipboardText(text); }
static void pdf_link_name(void *u, const char *path, char *out, size_t cap) {
    App *a = u;
    const char *base = strrchr(path, '/');
    (void)a;
    base = base ? base + 1 : path;
    snprintf(out, cap, "%s", base);                                                   /* the file name; the path is written only when the name is not unique (the link rules of 4a) */
}
static void host_pdf_result(void *u, const char *path, ExPdf *doc, const char *err) {
    App *a = u;
    size_t i, nt = a->ws ? ex_ws_tab_count(a->ws) : 0;
    for (i = 0; a->panes && i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c = t ? ex_panes_content(a->panes, t->id) : NULL;
        if (c && c->type == EX_WIDGET_PDF && !ex_pdfview_has_doc(c) && !strcmp(ex_pdfview_path(c), path)) { ex_pdfview_set_doc(c, doc, err); pdf_apply_goto(a, path); return; }
    }
    ex_pdf_close_async(doc);                                                          /* its tab was closed while it opened */
}
static void media_load(void *u, const char *path) {
    App *a = u;
    const char *dot = strrchr(path, '.');
    int video = !(dot && (!strcmp(dot, ".m4a") || !strcmp(dot, ".M4A")));
    if (a->panels) ex_panels_load_media(a->panels, path, video);
}
static ExMpv *media_current(void *u, const char *path) { App *a = u; return a->mpv && !strcmp(a->mpv_path, path) ? a->mpv : NULL; }
static void host_media_result(void *u, const char *path, ExMpv *m, const char *err) {
    App *a = u;
    size_t i, nt = a->ws ? ex_ws_tab_count(a->ws) : 0;
    if (m) {
        if (a->mpv) ex_mpv_close_async(a->mpv);
        a->mpv = m; snprintf(a->mpv_path, sizeof a->mpv_path, "%s", path);
        if (a->player) ex_player_pause(a->player);                                    /* one sound at a time */
    }
    for (i = 0; a->panes && i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c = t ? ex_panes_content(a->panes, t->id) : NULL;
        if (c && c->type == EX_WIDGET_MEDIA && !strcmp(ex_mediaview_path(c), path)) ex_mediaview_loaded(c, m ? NULL : err);
    }
}
static void audio_load(void *u, const char *path) { App *a = u; if (a->panels && a->player) ex_panels_load_audio(a->panels, path, a->player); }
static void audio_open_external(void *u, const char *path) { App *a = u; if (a->panels) host_file_op(a, EX_FILEOP_OPEN_DEFAULT, path, NULL); }
static void host_audio_result(void *u, const char *path, ExAudioDecoder *d, const char *err) {
    App *a = u;
    size_t i, nt = a->ws ? ex_ws_tab_count(a->ws) : 0;
    if (d) { ex_player_set_path(a->player, path); if (ex_player_attach(a->player, d) != 0) { err = "No sound output."; d = NULL; } else { ex_player_set_path(a->player, path); if (!strcmp(a->audio_embed_path, path)) { ex_player_play(a->player); a->audio_embed_path[0] = 0; } } }
    for (i = 0; a->panes && i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c = t ? ex_panes_content(a->panes, t->id) : NULL;
        if (c && c->type == EX_WIDGET_AUDIO && !strcmp(ex_audioview_path(c), path)) ex_audioview_loaded(c, d ? NULL : err);
    }
}
static void image_want(void *u, const char *path, int svg_width) { App *a = u; if (a->panels) ex_panels_load_image(a->panels, path, svg_width); }
static void host_image_result(void *u, const char *path, int svg_width, ExImage *img, const char *err) { App *a = u; ex_imagecache_result(a->icache, path, svg_width, img, err); }

static int same_text_nocase(const char *a, const char *b, size_t n) { size_t i; for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0; return 1; }

/* A file of the vault by its path or its name (with or without .md), any case: the first one found. */
static const ExNode *find_file_node(const ExNode *n, const char *text) {
    size_t i;
    if (!n) return NULL;
    if (!n->is_dir) {
        const char *base = strrchr(n->rel, '/');
        size_t bl, tl = strlen(text);
        base = base ? base + 1 : n->rel;
        bl = strlen(base);
        if (strlen(n->rel) == tl && same_text_nocase(n->rel, text, tl)) return n;
        if (bl == tl && same_text_nocase(base, text, tl)) return n;
        if (bl > 3 && !strcmp(base + bl - 3, ".md") && bl - 3 == tl && same_text_nocase(base, text, tl)) return n;
        return NULL;
    }
    for (i = 0; i < n->nkids; i++) { const ExNode *f = find_file_node(n->kids[i], text); if (f) return f; }
    return NULL;
}
static int canvas_resolve(void *u, const char *text, char *path, size_t cap) {
    App *a = u;
    const ExNode *n;
    if (!a->vault || !text || !*text) return 0;
    n = find_file_node(ex_vault_root(a->vault), text);
    if (!n) return 0;
    snprintf(path, cap, "%s", n->rel);
    return 1;
}


/* ---- embeds in reading mode, and links to a PDF page or selection (6c-v) ---- */

static int embed_resolve(void *u, const char *name, char *path, size_t cap) { return canvas_resolve(u, name, path, cap); }
static int embed_audio_state(void *u, const char *path) {
    App *a = u;
    if (!a->player || strcmp(ex_player_path(a->player), path) != 0 || ex_player_state(a->player) == EX_PLAYER_IDLE) return 0;
    return ex_player_state(a->player) == EX_PLAYER_PLAYING ? 2 : 1;
}

/* the PDF tab at this path shows a page and selects text (now when it has its document, else when it gets it) */
static void pdf_apply_goto(App *a, const char *path) {
    size_t i, nt = a->ws ? ex_ws_tab_count(a->ws) : 0;
    if (!a->pdf_goto_set || strcmp(a->pdf_goto_path, path)) return;
    for (i = 0; a->panes && i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c = t ? ex_panes_content(a->panes, t->id) : NULL;
        if (c && c->type == EX_WIDGET_PDF && !strcmp(ex_pdfview_path(c), path) && ex_pdfview_has_doc(c)) { ex_pdfview_goto(c, a->pdf_goto_page, a->pdf_goto_s, a->pdf_goto_e); a->pdf_goto_set = 0; return; }
    }
}

/* Opens a file of the vault that is not a note, by the kind of file; frag is what followed the # of the link. */
static void open_file_target(App *a, const char *path, const char *frag) {
    if (ex_file_kind(path) == EX_KIND_PDF && frag && frag[0]) {
        int pg = 0, s = 0, e = 0;
        if (ex_pdf_parse_fragment(frag, &pg, &s, &e)) {
            snprintf(a->pdf_goto_path, sizeof a->pdf_goto_path, "%s", path);
            a->pdf_goto_page = pg; a->pdf_goto_s = s; a->pdf_goto_e = e; a->pdf_goto_set = 1;
        }
    }
    open_path(a, 0, path);
    if (a->pdf_goto_set) pdf_apply_goto(a, path);
}

static void embed_click(App *a, const char *target) {
    char path[700], frag[200];
    const char *dot;
    int builtin_audio;
    ex_embed_split(target, path, sizeof path, NULL, 0, frag, sizeof frag);
    dot = strrchr(path, '.');
    builtin_audio = ex_file_kind(path) == EX_KIND_AUDIO && dot && strcasecmp_ascii(dot, ".m4a") && strcasecmp_ascii(dot, ".webm") && strcasecmp_ascii(dot, ".3gp");
    if (builtin_audio && a->player && a->panels) {                                   /* an audio embed plays here, with the one player */
        if (!strcmp(ex_player_path(a->player), path) && ex_player_state(a->player) != EX_PLAYER_IDLE) { ex_player_toggle(a->player); return; }
        snprintf(a->audio_embed_path, sizeof a->audio_embed_path, "%s", path);
        ex_panels_load_audio(a->panels, path, a->player);
        return;
    }
    open_file_target(a, path, frag);
}

/* Convert to file: the text of a card becomes a note beside the canvas, named by its first line. */
static int canvas_create_note(void *u, const char *text, char *path, size_t cap) {
    App *a = u;
    ExId t = a->ws ? ex_ws_group_active(a->ws, ex_ws_active_group(a->ws)) : 0;
    const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
    char parent[600] = "", name[100];
    size_t i, o = 0, len = strlen(text);
    ExNote *n;
    ExChange ch;
    if (!a->notes || !a->vault) return 0;
    if (tab && tab->path[0] && strrchr(tab->path, '/')) snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(tab->path, '/') - tab->path), tab->path);
    for (i = 0; i < len && text[i] != '\n' && o + 1 < sizeof name; i++) {                 /* the first line, without the marks a heading or a list has and the characters a name cannot have */
        char ch2 = text[i];
        if (!o && (ch2 == '#' || ch2 == '-' || ch2 == '*' || ch2 == ' ' || ch2 == '>')) continue;
        if (ch2 == '/' || ch2 == '\\' || ch2 == ':' || ch2 == '#' || ch2 == '^' || ch2 == '[' || ch2 == ']' || ch2 == '|' || ch2 == '%' || ch2 == '*' || ch2 == '?' || ch2 == '"' || ch2 == '<' || ch2 == '>') continue;
        name[o++] = ch2;
    }
    while (o > 0 && (name[o - 1] == ' ' || name[o - 1] == '.')) o--;
    name[o] = 0;
    if (!o || !ex_name_valid(name)) snprintf(name, sizeof name, "Untitled");
    n = ex_notes_new(a->notes, parent, name);
    if (!n) return 0;
    ch.offset = 0; ch.remove_len = 0; ch.text = text; ch.text_len = len;
    if (len) ex_doc_apply(ex_note_doc(n), &ch, 1, 0, NULL, 0);
    snprintf(path, cap, "%s", ex_note_path(n));
    ex_notes_close(a->notes, n);                                                       /* writes it now (the user asked for it) */
    if (a->vault) ex_vault_rescan(a->vault);
    return 1;
}

static void canvas_view_key(char *out, size_t cap, const char *path) {
    size_t o = 0;
    const char *p;
    o += (size_t)snprintf(out, cap, "view.");
    for (p = path; *p && o + 4 < cap; p++) { if (*p == '=' || *p == '%') o += (size_t)snprintf(out + o, cap - o, "%%%02X", (unsigned char)*p); else out[o++] = *p; }
    out[o] = 0;
}

static ExWidget *make_canvas(App *a, ExNote *n, ExOpenResult res, const ExTab *t) {
    ExCanvasHandlers h;
    ExWidget *cw;
    char key[700];
    const char *saved;
    memset(&h, 0, sizeof h);
    h.user = a; h.panels = a->panels; h.settings = a->settings; h.cache = a->ccache; h.images = a->icache;
    h.want_text = canvas_want_text; h.open_file = canvas_open_file; h.open_url = canvas_open_url; h.resolve = canvas_resolve; h.create_note = canvas_create_note; h.message = canvas_message;
    cw = ex_widget_canvas(a->ui, ex_note_doc(n), t->path, &h);
    if (!cw) return NULL;
    canvas_view_key(key, sizeof key, t->path);
    saved = ex_ws_extra_get(a->ws, "canvas", key);
    if (saved && *saved) ex_canvasview_set_view(cw, saved);
    if (!note_for_tab(a, t->id)) remember_tab_note(a, t->id, n);
    if (res == EX_OPEN_RECOVERED) show_banner(a, BANNER_RECOVERED, "Exhaust closed unexpectedly. The changes you made to this canvas were restored from the recovery journal.", "OK", NULL, NULL);
    a->banner_note = n;
    return cw;
}

/* Every few frames: the view of the canvases that are open goes to the session (the centre and zoom, as text). */
static void canvas_save_views(App *a) {
    size_t i, nt = ex_ws_tab_count(a->ws);
    if (!a->panes) return;
    for (i = 0; i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c;
        char key[700], now[80];
        const char *was;
        if (!t || !t->view || strcmp(t->view, "canvas") || !t->path[0]) continue;
        c = ex_panes_content(a->panes, t->id);
        if (!c || c->type != EX_WIDGET_CANVAS) continue;
        ex_canvasview_get_view(c, now, sizeof now);
        if (!now[0]) continue;
        canvas_view_key(key, sizeof key, t->path);
        was = ex_ws_extra_get(a->ws, "canvas", key);
        if (!was || strcmp(was, now)) ex_ws_extra_set(a->ws, "canvas", key, now);
    }
}

/* ---- the Bases view (6b-iii) ---- */

static unsigned long base_run(void *u, const char *text, size_t len, size_t view, const char *this_path) { App *a = u; return a->panels ? ex_panels_run_base(a->panels, text, len, view, this_path) : 0; }
static void base_open_file(void *u, const char *path, int new_tab) { host_open_note(u, path, (size_t)-1, new_tab); }
static void base_message(void *u, const char *text) { set_status(u, text); }

/* Export as CSV: a save dialog comes with the PDF export (6e); until then the file is written beside the base, under a name that is not taken. */
static int base_export_file(void *u, const char *name, const char *data, size_t len) {
    App *a = u;
    ExId t = a->ws ? ex_ws_group_active(a->ws, ex_ws_active_group(a->ws)) : 0;
    const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
    char rel[1100], full[1700], parent[600] = "", stem[300], msg[1300];
    const char *dot;
    ExSaveResult sr;
    int n;
    if (!a->vault || !tab) return -1;
    if (strrchr(tab->path, '/')) snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(tab->path, '/') - tab->path), tab->path);
    dot = strrchr(name, '.');
    snprintf(stem, sizeof stem, "%.*s", (int)(dot ? (size_t)(dot - name) : strlen(name)), name);
    for (n = 0; n < 1000; n++) {
        char fn[400];
        if (n) snprintf(fn, sizeof fn, "%s %d.csv", stem, n); else snprintf(fn, sizeof fn, "%s.csv", stem);
        snprintf(rel, sizeof rel, "%s%s%s", parent, parent[0] ? "/" : "", fn);
        snprintf(full, sizeof full, "%s/%s", ex_vault_path(a->vault), rel);
        if (!ex_platform_file_exists(full)) break;
    }
    if (n >= 1000) return -1;
    if (ex_platform_save_file(full, data, len, &sr) != 0) return -1;
    ex_vault_rescan(a->vault);
    snprintf(msg, sizeof msg, "Exported %s.", rel);
    set_status(a, msg);
    return 0;
}

static void host_base_result(void *u, unsigned long req, ExBaseResult *result, const char *error) {
    App *a = u;
    size_t i, nt = a->ws ? ex_ws_tab_count(a->ws) : 0;
    for (i = 0; a->panes && i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c = t ? ex_panes_content(a->panes, t->id) : NULL;
        if (c && c->type == EX_WIDGET_BASE && ex_baseview_owns_request(c, req)) { ex_baseview_set_result(c, req, result, error); return; }
    }
    ex_baseresult_free_heap(result);                                         /* its tab was closed while it ran */
}

static ExWidget *make_base(App *a, ExNote *n, ExOpenResult res, const ExTab *t) {
    ExBaseViewHandlers h;
    ExWidget *bw;
    memset(&h, 0, sizeof h);
    h.user = a; h.panels = a->panels; h.run = base_run; h.open_file = base_open_file; h.open_url = canvas_open_url; h.message = base_message; h.export_file = base_export_file;
    bw = ex_widget_base(a->ui, ex_note_doc(n), t->path, &h);
    if (!bw) return NULL;
    if (!note_for_tab(a, t->id)) remember_tab_note(a, t->id, n);
    if (res == EX_OPEN_RECOVERED) show_banner(a, BANNER_RECOVERED, "Exhaust closed unexpectedly. The changes you made to this base were restored from the recovery journal.", "OK", NULL, NULL);
    a->banner_note = n;
    return bw;
}

/* Every frame: when the index changed, the bases that are open run again; every pane's bases, not only the active one. */
static void bases_follow_index(App *a) {
    size_t i, nt;
    unsigned long g;
    if (!a->panels || !a->panes) return;
    g = ex_panels_index_generation(a->panels);
    if (g == a->base_gen_seen) return;
    a->base_gen_seen = g;
    nt = ex_ws_tab_count(a->ws);
    for (i = 0; i < nt; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *c = t && t->view && !strcmp(t->view, "base") ? ex_panes_content(a->panes, t->id) : NULL;
        if (c && c->type == EX_WIDGET_BASE) ex_baseview_invalidate(c);
        else if (t && t->view && !strcmp(t->view, "note") && is_reading(t->mode)) {              /* a note shown in reading mode: its query blocks, bases and embeds are read again (6c-v) */
            ExNote *n = note_for_tab(a, t->id);
            ExWidget *rw = ex_panes_content(a->panes, t->id);
            if (n && rw && rw->type == EX_WIDGET_READING) {
                size_t tl = 0;
                char *txt = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), &tl);
                if (txt) { ex_panels_expand_queries(a->panels, t->path, txt, tl); free(txt); }
            }
        }
    }
}

/* The canvas widget of the active tab, or NULL. */
static ExWidget *active_canvas(App *a) {
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    ExWidget *c = t && a->panes ? ex_panes_content(a->panes, t) : NULL;
    return c && c->type == EX_WIDGET_CANVAS ? c : NULL;
}
static ExWidget *active_base(App *a) {
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    ExWidget *c = t && a->panes ? ex_panes_content(a->panes, t) : NULL;
    return c && c->type == EX_WIDGET_BASE ? c : NULL;
}
static void format_cmd(App *a, ExFormat f);
static int has_base(void *u) { return active_base(u) != NULL; }
static void cmd_base_add_view(void *u) { ExWidget *b = active_base(u); if (b) ex_baseview_add_view(b, "table"); }
static void cmd_base_switch_view(void *u) { ExWidget *b = active_base(u); if (b) ex_baseview_view_menu(b); }
static void cmd_base_copy(void *u) { ExWidget *b = active_base(u); if (b) ex_baseview_copy(b); }
static void cmd_base_insert(void *u) { format_cmd(u, EX_FMT_BASE); }
/* ---- daily notes and templates (6e-i) ---- */
static void daily_config(App *a, ExDailyConfig *c) {
    c->date_format = ex_settings_get(a->settings, "daily_notes", "date_format");
    c->folder = ex_settings_get(a->settings, "daily_notes", "folder");
    c->template_path = ex_settings_get(a->settings, "daily_notes", "template");
    c->tz_min = ex_platform_tz_offset_minutes();
}
/* shift 0: today; 1 and -1: the day after and before the daily note that is open (or today, when it is not one). The note is made when it is not there:
 * this is an action of the user, and the file is never replaced. */
static void open_daily(App *a, int shift) {
    ExDailyConfig c;
    double now = (double)ex_platform_unix_time() * 1000.0, day = now;
    char rel[700];
    int rc;
    if (!a->vault || !a->notes) return;
    daily_config(a, &c);
    if (shift) {
        ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
        const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
        double cur;
        if (tab && tab->path[0] && ex_daily_date_of(&c, tab->path, &cur) == 0) day = cur;
        day = ex_daily_shift(&c, day, shift);
    }
    rc = ex_daily_ensure(a->vault, &c, day, now, ex_settings_get(a->settings, "templates", "time_format"), rel, sizeof rel);
    if (rc < 0) { set_status(a, "The daily note could not be made: check the date format and the folder in the Daily notes settings."); return; }
    ex_panels_tree_changed(a->panels);
    open_path(a, 0, rel);
}
static void cmd_daily_today(void *u) { open_daily(u, 0); }
static void cmd_daily_next(void *u) { open_daily(u, 1); }
static void cmd_daily_prev(void *u) { open_daily(u, -1); }

static ExTplContext template_context(App *a, char *title, size_t cap) {
    ExTplContext c;
    memset(&c, 0, sizeof c);
    title[0] = 0;
    if (a->note) {
        const char *p = ex_note_path(a->note), *slash = p ? strrchr(p, '/') : NULL, *name = slash ? slash + 1 : (p ? p : "");
        size_t n = strlen(name);
        if (n > 3 && !strcmp(name + n - 3, ".md")) n -= 3;
        snprintf(title, cap, "%.*s", (int)n, name);
    }
    c.title = title;
    c.now_ms = (double)ex_platform_unix_time() * 1000.0;
    c.tz_min = ex_platform_tz_offset_minutes();
    c.date_format = ex_settings_get(a->settings, "templates", "date_format");
    c.time_format = ex_settings_get(a->settings, "templates", "time_format");
    return c;
}
static void insert_text_at_carets(App *a, const char *text) {
    ExEditor *ed = a->editor ? a->editor->editor : NULL;
    if (ed) ex_doc_replace_selections(ex_editor_doc(ed), text, strlen(text), 0);
}
static void cmd_tpl_date(void *u) {
    App *a = u;
    char title[300], out[300];
    ExTplContext c = template_context(a, title, sizeof title);
    ex_date_format(out, sizeof out, c.now_ms, c.tz_min, c.date_format && *c.date_format ? c.date_format : "YYYY-MM-DD");
    insert_text_at_carets(a, out);
}
static void cmd_tpl_time(void *u) {
    App *a = u;
    char title[300], out[300];
    ExTplContext c = template_context(a, title, sizeof title);
    ex_date_format(out, sizeof out, c.now_ms, c.tz_min, c.time_format && *c.time_format ? c.time_format : "HH:mm");
    insert_text_at_carets(a, out);
}
typedef struct TplList { char **abs; size_t n, cap; } TplList;
/* the notes under a folder of the vault, from the tree held in memory (no disk is read) */
static void tpl_collect(const ExVault *v, const ExNode *dir, TplList *l, int depth) {
    size_t i;
    if (!dir || depth > 8) return;
    for (i = 0; i < dir->nkids; i++) {
        const ExNode *k = dir->kids[i];
        if (k->is_dir) { tpl_collect(v, k, l, depth + 1); continue; }
        if (k->kind != EX_KIND_MARKDOWN) continue;
        if (l->n == l->cap) { size_t nc = l->cap ? l->cap * 2 : 16; char **t = realloc(l->abs, nc * sizeof *t); if (!t) return; l->abs = t; l->cap = nc; }
        l->abs[l->n] = ex_vault_abs_path(v, k->rel);
        if (l->abs[l->n]) l->n++;
    }
}
static int tpl_cmp(const void *x, const void *y) { return strcmp(*(char *const *)x, *(char *const *)y); }
static void tpl_choose(size_t index, void *arg) {
    App *a = arg;
    ExEditor *ed = a->editor ? a->editor->editor : NULL;
    ExDoc *doc;
    size_t tl = 0, nl = 0, cursor;
    char *tpl, *note;
    ExTplContext c;
    ExTplPlan p;
    char title[300];
    ExChange ch[2];
    size_t nch = 0, caret;
    if (!ed || index >= a->tpl_n) return;
    tpl = ex_platform_read_file(a->tpl_abs[index], &tl);
    if (!tpl) { set_status(a, "The template could not be read."); return; }
    doc = ex_editor_doc(ed);
    note = ex_buffer_text(ex_doc_buffer(doc), &nl);
    cursor = ex_doc_sel(doc, ex_doc_primary(doc)).head;
    c = template_context(a, title, sizeof title);
    if (note && ex_tpl_plan(note, nl, tpl, tl, &c, cursor, &p) == 0) {
        size_t shift = 0;
        if (p.has_front) { ch[nch].offset = p.front_off; ch[nch].remove_len = p.front_del; ch[nch].text = p.front_text; ch[nch].text_len = p.front_len; nch++; shift = p.front_len - p.front_del; }
        ch[nch].offset = p.body_off; ch[nch].remove_len = 0; ch[nch].text = p.body_text; ch[nch].text_len = p.body_len; nch++;
        caret = p.body_off + shift + p.body_len;
        {
            ExSel sel; sel.anchor = sel.head = caret;
            ex_doc_apply(doc, ch, nch, 0, &sel, 1);     /* one undo step: the properties and the text */
        }
        ex_tpl_plan_free(&p);
    }
    free(note); free(tpl);
}
/* templates.insert-template: the notes of the template folder, to pick from. */
static void convert_answer(int button, void *arg) {
    App *a = arg;
    if (button == 1) ex_panels_convert_frontmatter(a->panels);
}

static void cmd_format_convert(void *arg) {
    App *a = arg;
    ex_panels_ask_confirm(a->panels, "Convert frontmatter",
        "This rewrites the property keys alias, tag and cssclass to aliases, tags and cssclasses in every note that has them. "
        "Only the key names change, but the notes are edited on disk: back up your vault first. A note that already has the new key is left alone.",
        "Convert", "Cancel", NULL, convert_answer, a);
}

static void cmd_tpl_insert(void *u) {
    App *a = u;
    const char *folder = ex_settings_get(a->settings, "templates", "folder");
    char *abs;
    TplList l;
    char **labels;
    size_t i;
    if (!a->vault || !a->editor) return;
    memset(&l, 0, sizeof l);
    abs = ex_vault_abs_path(a->vault, folder ? folder : "");
    if (!abs) return;
    tpl_collect(a->vault, folder && *folder ? ex_vault_find(a->vault, folder) : ex_vault_root(a->vault), &l, 0);
    if (l.n == 0) { set_status(a, folder && *folder ? "There are no templates in the template folder (Templates settings)." : "Choose a template folder in the Templates settings first."); free(abs); free(l.abs); return; }
    qsort(l.abs, l.n, sizeof *l.abs, tpl_cmp);
    for (i = 0; i < a->tpl_n; i++) free(a->tpl_abs[i]);
    free(a->tpl_abs);
    a->tpl_abs = l.abs; a->tpl_n = l.n;
    labels = malloc(l.n * sizeof *labels);
    if (labels) {
        size_t base = strlen(abs);
        for (i = 0; i < l.n; i++) {
            const char *p = l.abs[i] + (strlen(l.abs[i]) > base ? base : 0);
            size_t n;
            while (*p == '/') p++;
            n = strlen(p);
            labels[i] = malloc(n + 1);
            if (labels[i]) { memcpy(labels[i], p, n + 1); if (n > 3) labels[i][n - 3] = 0; }
        }
        ex_panels_ask_choice(a->panels, "Insert template", (const char *const *)labels, l.n, tpl_choose, a);
        for (i = 0; i < l.n; i++) free(labels[i]);
        free(labels);
    }
    free(abs);
}

/* ---- unique notes and the note composer (6e-ii) ---- */
static int make_unique(App *a, char *rel, size_t cap) {
    ExUniqueConfig c;
    if (!a->vault) return -1;
    c.prefix_format = ex_settings_get(a->settings, "unique_note", "prefix_format");
    c.folder = ex_settings_get(a->settings, "unique_note", "folder");
    c.template_path = ex_settings_get(a->settings, "unique_note", "template");
    c.tz_min = ex_platform_tz_offset_minutes();
    return ex_unique_create(a->vault, &c, (double)ex_platform_unix_time() * 1000.0, ex_settings_get(a->settings, "templates", "date_format"), ex_settings_get(a->settings, "templates", "time_format"), rel, cap);
}
static void cmd_unique_new(void *u) {
    App *a = u;
    char rel[700];
    if (make_unique(a, rel, sizeof rel) < 0) { set_status(a, "The unique note could not be made: check the folder and the prefix format in the Unique note creator settings."); return; }
    ex_panels_tree_changed(a->panels);
    open_path(a, 0, rel);
}
/* unique-note-creator.add-unique-internal-link: makes the note and puts a link to it at the cursor; the note is not opened. */
static void cmd_unique_link(void *u) {
    App *a = u;
    char rel[700], link[760];
    const char *slash, *name;
    size_t n;
    if (make_unique(a, rel, sizeof rel) < 0) { set_status(a, "The unique note could not be made: check the folder and the prefix format in the Unique note creator settings."); return; }
    slash = strrchr(rel, '/'); name = slash ? slash + 1 : rel; n = strlen(name);
    if (n > 3 && !strcmp(name + n - 3, ".md")) n -= 3;
    snprintf(link, sizeof link, "[[%.*s]]", (int)n, name);
    insert_text_at_carets(a, link);
    ex_panels_tree_changed(a->panels);
}

static ExComposeConfig composer_config(App *a, char **tpl) {
    ExComposeConfig c;
    const char *after = ex_settings_get(a->settings, "note_composer", "text_after_extract"), *tp = ex_settings_get(a->settings, "note_composer", "template");
    memset(&c, 0, sizeof c);
    c.after_extract = after && !strcmp(after, "embed") ? EX_AFTER_EMBED : (after && !strcmp(after, "none") ? EX_AFTER_NONE : EX_AFTER_LINK);
    c.now_ms = (double)ex_platform_unix_time() * 1000.0; c.tz_min = ex_platform_tz_offset_minutes();
    c.date_format = ex_settings_get(a->settings, "templates", "date_format"); c.time_format = ex_settings_get(a->settings, "templates", "time_format");
    *tpl = NULL;
    if (tp && *tp && a->vault) {
        char rel[700], *abs;
        size_t n = strlen(tp), tl = 0;
        snprintf(rel, sizeof rel, "%.600s%s", tp, n >= 3 && !strcmp(tp + n - 3, ".md") ? "" : ".md");
        abs = ex_vault_abs_path(a->vault, rel);
        if (abs) { *tpl = ex_platform_read_file(abs, &tl); free(abs); }
        if (*tpl) { c.template_text = *tpl; c.template_len = tl; }
    }
    return c;
}

static void extract_done(const char *name, void *arg) {
    App *a = arg;
    ExEditor *ed = a->editor ? a->editor->editor : NULL;
    ExComposeConfig cfg;
    char *tpl, *rel = NULL, *rep = NULL;
    int rc;
    if (!ed || !a->vault || !a->ex_content || !name) return;
    cfg = composer_config(a, &tpl);
    rc = ex_compose_extract(a->vault, a->ex_source, a->ex_content, a->ex_content_len, name, &cfg, &rel, &rep);
    if (rc == 0) {
        ExChange ch;
        ExSel sel;
        char *text = malloc(strlen(rep) + 2);
        if (text) {
            snprintf(text, strlen(rep) + 2, "%s%s", rep, a->ex_newline && rep[0] ? "\n" : "");
            ch.offset = a->ex_start; ch.remove_len = a->ex_end - a->ex_start; ch.text = text; ch.text_len = strlen(text);
            sel.anchor = sel.head = a->ex_start + ch.text_len;
            ex_doc_apply(ex_editor_doc(ed), &ch, 1, 0, &sel, 1);
            free(text);
        }
        ex_panels_tree_changed(a->panels);
        { char msg[400]; snprintf(msg, sizeof msg, "Moved the text into \"%.150s\".", name); set_status(a, msg); }
    } else if (rc == EX_VAULT_EXISTS) set_status(a, "A note with that name already exists: nothing was changed.");
    else if (rc == EX_VAULT_INVALID) set_status(a, "That is not a name a note can have (no # ^ [ ] | : / \\ ).");
    else set_status(a, "The new note could not be made.");
    free(rel); free(rep); free(tpl);
    free(a->ex_content); a->ex_content = NULL;
}
static void begin_extract(App *a, size_t start, size_t end, size_t content_start, int whole_section, const char *suggest_from, size_t suggest_len) {
    ExEditor *ed = a->editor ? a->editor->editor : NULL;
    ExDoc *doc;
    size_t nl = 0;
    char *text, name[100];
    if (!ed || !a->note || !a->vault || end <= start) return;
    doc = ex_editor_doc(ed);
    text = ex_buffer_text(ex_doc_buffer(doc), &nl);
    if (!text) return;
    free(a->ex_content);
    a->ex_content_len = end - content_start;
    a->ex_content = malloc(a->ex_content_len + 1);
    if (!a->ex_content) { free(text); return; }
    memcpy(a->ex_content, text + content_start, a->ex_content_len); a->ex_content[a->ex_content_len] = 0;
    a->ex_start = start; a->ex_end = end;
    a->ex_newline = whole_section && end > start && text[end - 1] == '\n';
    snprintf(a->ex_source, sizeof a->ex_source, "%s", ex_note_path(a->note));
    ex_compose_suggest_name(suggest_from ? suggest_from : text + content_start, suggest_from ? suggest_len : a->ex_content_len, name, sizeof name);
    free(text);
    ex_panels_ask_text(a->panels, "Extract into a new note", "Name of the new note", name, extract_done, a);
}
static void cmd_extract_selection(void *u) {
    App *a = u;
    ExEditor *ed = a->editor ? a->editor->editor : NULL;
    ExSel s;
    size_t lo, hi;
    if (!ed) return;
    s = ex_doc_sel(ex_editor_doc(ed), ex_doc_primary(ex_editor_doc(ed)));
    lo = s.anchor < s.head ? s.anchor : s.head; hi = s.anchor < s.head ? s.head : s.anchor;
    if (hi == lo) { set_status(a, "Select the text to move into a new note first."); return; }
    begin_extract(a, lo, hi, lo, 0, NULL, 0);
}
static void cmd_extract_heading(void *u) {
    App *a = u;
    ExEditor *ed = a->editor ? a->editor->editor : NULL;
    ExDoc *doc;
    size_t nl = 0, s0, e0, cursor, cs;
    char *text, heading[200];
    if (!ed) return;
    doc = ex_editor_doc(ed);
    text = ex_buffer_text(ex_doc_buffer(doc), &nl);
    if (!text) return;
    cursor = ex_doc_sel(doc, ex_doc_primary(doc)).head;
    if (ex_compose_heading_span(text, nl, cursor, &s0, &e0, heading, sizeof heading) != 0) { free(text); set_status(a, "Put the cursor under a heading first."); return; }
    cs = s0;
    while (cs < e0 && text[cs] != '\n') cs++;
    if (cs < e0) cs++;                      /* the section without its heading line is what moves */
    free(text);
    begin_extract(a, s0, e0, cs, 1, heading, strlen(heading));
}

static int strcasecmp_ci(const char *x, const char *y) { for (; *x && *y; x++, y++) { char a = *x, b = *y; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); if (a != b) return 1; } return *x || *y; }
static int strncasecmp_ci(const char *x, const char *y, size_t n) { size_t i; for (i = 0; i < n; i++) { char a = x[i], b = y[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); if (a != b) return 1; if (!a) break; } return 0; }
static int tree_find(const ExNode *n, const char *want, int want_is_path, const char **found, int *count) {
    size_t i;
    for (i = 0; i < n->nkids; i++) {
        const ExNode *k = n->kids[i];
        if (k->is_dir) { tree_find(k, want, want_is_path, found, count); continue; }
        if (k->kind != EX_KIND_MARKDOWN) continue;
        {
            size_t nl = strlen(k->name), rl = strlen(k->rel);
            const char *base = k->name;
            size_t bl = nl > 3 ? nl - 3 : nl;
            int hit;
            if (want_is_path) hit = (rl == strlen(want) && !strcasecmp_ci(k->rel, want)) || (rl > 3 && rl - 3 == strlen(want) && !strncasecmp_ci(k->rel, want, rl - 3));
            else hit = bl == strlen(want) && !strncasecmp_ci(base, want, bl);
            if (hit) { *found = k->rel; (*count)++; }
        }
    }
    return 0;
}
static void merge_confirmed(int button, void *arg) {
    App *a = arg;
    char *tpl;
    ExComposeConfig cfg;
    if (button != 1) return;
    cfg = composer_config(a, &tpl);
    ex_panels_merge_notes(a->panels, a->merge_from, a->merge_into, cfg.template_text, cfg.template_len);
    free(tpl);
}
static void merge_target_done(const char *text, void *arg) {
    App *a = arg;
    const char *found = NULL, *cur = a->note ? ex_note_path(a->note) : NULL;
    int count = 0;
    char want[600];
    size_t n;
    if (!text || !a->vault || !cur) return;
    snprintf(want, sizeof want, "%s", text);
    n = strlen(want);
    while (n && want[n - 1] == ' ') want[--n] = 0;
    if (n > 3 && !strcmp(want + n - 3, ".md")) want[n -= 3] = 0;
    if (!n) return;
    tree_find(ex_vault_root(a->vault), want, strchr(want, '/') != NULL, &found, &count);
    if (count == 0) { set_status(a, "No note has that name."); return; }
    if (count > 1) { set_status(a, "More than one note has that name: type its folder too (Folder/Name)."); return; }
    if (!strcmp(found, cur)) { set_status(a, "A note cannot be merged into itself."); return; }
    snprintf(a->merge_from, sizeof a->merge_from, "%s", cur);
    snprintf(a->merge_into, sizeof a->merge_into, "%s", found);
    if (ex_settings_get_bool(a->settings, "note_composer", "confirm_merge")) {
        char msg[1500];
        snprintf(msg, sizeof msg, "\"%.200s\" will be added to the end of \"%.200s\", every link to it will point there, and \"%.200s\" will be deleted (the trash setting of Files and links decides where it goes).", cur, found, cur);
        ex_panels_ask_confirm(a->panels, "Merge notes", msg, "Merge", "Cancel", NULL, merge_confirmed, a);
    } else merge_confirmed(1, a);
}
static void cmd_merge(void *u) {
    App *a = u;
    if (!a->note || !a->vault) return;
    ex_panels_ask_text(a->panels, "Merge into another note", "Merge this note into (a note's name, or Folder/Name)", "", merge_target_done, a);
}

/* ---- slides (6e-iii) ---- */
typedef struct SlideShow { App *a; ExId tab; char *text; size_t len; ExSlide *sl; size_t n, idx; ExWidget *w; } SlideShow;
static void slideshow_free(void *p) { SlideShow *s = p; if (s) { free(s->text); ex_slides_free(s->sl); free(s); } }
static void slide_go(SlideShow *s, size_t idx) {
    char msg[200];
    if (s->n == 0) { ex_reading_set_text(s->w, "", 0); set_status(s->a, "This note has nothing to present: put --- on a line of its own, with a blank line before it, between slides."); return; }
    if (idx >= s->n) idx = s->n - 1;
    s->idx = idx;
    ex_reading_set_text(s->w, s->text + s->sl[idx].start, s->sl[idx].end - s->sl[idx].start);
    ex_reading_set_scroll(s->w, 0);
    snprintf(msg, sizeof msg, "Slide %zu of %zu. Right, Space or Page Down: next. Left or Page Up: back. Home and End: first and last. Escape: end.", idx + 1, s->n);
    set_status(s->a, msg);
}
static int slide_key(ExWidget *w, ExKey key, int mods, void *user) {
    SlideShow *s = user;
    (void)w; (void)mods;
    switch (key) {
    case EX_KEY_RIGHT: case EX_KEY_PAGE_DOWN: case EX_KEY_SPACE: case EX_KEY_ENTER: if (s->idx + 1 < s->n) slide_go(s, s->idx + 1); return 1;
    case EX_KEY_LEFT: case EX_KEY_PAGE_UP: case EX_KEY_BACKSPACE: if (s->idx > 0) slide_go(s, s->idx - 1); return 1;
    case EX_KEY_HOME: slide_go(s, 0); return 1;
    case EX_KEY_END: slide_go(s, s->n ? s->n - 1 : 0); return 1;
    case EX_KEY_ESCAPE: ex_ws_close_tab(s->a->ws, s->tab); return 1;
    default: return 0;
    }
}
static ExWidget *make_slides(App *a, ExNote *n, const ExTab *t) {
    SlideShow *s = calloc(1, sizeof *s);
    ExWidget *rv;
    if (!s) return NULL;
    s->a = a; s->tab = t->id;
    s->text = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), &s->len);
    if (!s->text) { free(s); return NULL; }
    if (ex_slides_split(s->text, s->len, &s->sl, &s->n) != 0) { slideshow_free(s); return NULL; }
    rv = ex_widget_reading(a->ui, "", 0, t->path);
    if (!rv) { slideshow_free(s); return NULL; }
    s->w = rv;
    ex_reading_set_page_scale(rv, 1.8f);
    ex_reading_set_link_handler(rv, on_reading_link, a);
    { ExReadingMedia rm; memset(&rm, 0, sizeof rm); rm.user = a; rm.images = a->icache; rm.resolve = embed_resolve; rm.audio_state = embed_audio_state; ex_reading_set_media(rv, &rm); }
    rv->on_key = slide_key; rv->on_key_user = s;
    rv->owned = s; rv->free_owned = slideshow_free;
    if (!note_for_tab(a, t->id)) remember_tab_note(a, t->id, n);
    slide_go(s, 0);
    return rv;
}
/* slides.start-presentation: the open note, as slides in a tab of its own (the note's own tab stays). */
static void cmd_slides_start(void *u) {
    App *a = u;
    ExId g = ex_ws_active_group(a->ws), t = ex_ws_group_active(a->ws, g);
    const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
    ExId nt;
    if (!tab || !tab->path[0] || ex_file_kind(tab->path) != EX_KIND_MARKDOWN) { set_status(a, "Open a note first: Start presentation shows the open note as slides."); return; }
    nt = ex_ws_new_tab(a->ws, g, tab->path, "slides");
    if (nt) a->focus_tab = nt;
}

/* bases.create-new-base: a new base beside the open note or in the vault folder (the canvas setting), with one table view; written at once. */
static void cmd_new_base(void *u) {
    App *a = u;
    char parent[600] = "";
    ExNote *n;
    static const char text[] = "views:\n  - type: table\n    name: Table\n";
    ExChange ch;
    if (!a->notes || !a->vault) return;
    {
        ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
        const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
        if (tab && tab->path[0] && strrchr(tab->path, '/')) snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(tab->path, '/') - tab->path), tab->path);
    }
    n = ex_notes_new_file(a->notes, parent, "Untitled", "base");
    if (!n) return;
    ch.offset = 0; ch.remove_len = 0; ch.text = text; ch.text_len = sizeof text - 1;
    ex_doc_apply(ex_note_doc(n), &ch, 1, 0, NULL, 0);
    open_path(a, 0, ex_note_path(n));
}
/* bases.add-item: a new note beside the base (the view's filters are not applied to it in 0.1.0). */
static void cmd_base_add_item(void *u) {
    App *a = u;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
    char parent[600] = "";
    ExNote *n;
    if (!a->notes || !a->vault) return;
    if (tab && tab->path[0] && strrchr(tab->path, '/')) snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(tab->path, '/') - tab->path), tab->path);
    n = ex_notes_new(a->notes, parent, "Untitled");
    if (n) open_path(a, 0, ex_note_path(n));
}
static int has_canvas(void *u) { return active_canvas(u) != NULL; }
static int has_canvas_selection(void *u) { ExWidget *c = active_canvas(u); return c && ex_canvasview_has_selection(c); }
static void cmd_canvas_add_text(void *u) { ExWidget *c = active_canvas(u); if (c) ex_canvasview_add_text(c, -1, -1); }
static void cmd_canvas_add_note(void *u) { ExWidget *c = active_canvas(u); if (c) ex_canvasview_add_file_prompt(c, -1, -1); }
static void cmd_canvas_add_group(void *u) { ExWidget *c = active_canvas(u); if (c) ex_canvasview_add_group(c, -1, -1); }
static void cmd_canvas_fit(void *u) { ExWidget *c = active_canvas(u); if (c) ex_canvasview_fit(c, 0); }
static void cmd_canvas_jump(void *u) { ExWidget *c = active_canvas(u); if (c) ex_canvasview_jump_to_group_prompt(c); }
static void cmd_canvas_convert(void *u) {
    App *a = u;
    ExWidget *c = active_canvas(a);
    if (c && ex_canvasview_convert_selected(c) != 0) set_status(a, "Select one text card to convert it to a file.");
}

/* canvas.create-new-canvas: a new canvas, beside the open note or in the vault folder (the setting), written when something is added to it. */
static void cmd_new_canvas(void *u) {
    App *a = u;
    char parent[600] = "";
    ExNote *n;
    const char *where = ex_settings_get(a->settings, "canvas", "new_file_location");
    if (!a->notes || !a->vault) return;
    if (where && !strcmp(where, "same_folder")) {
        ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
        const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
        if (tab && tab->path[0] && strrchr(tab->path, '/')) snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(tab->path, '/') - tab->path), tab->path);
    }
    n = ex_notes_new_file(a->notes, parent, "Untitled", "canvas");
    if (n) open_path(a, 0, ex_note_path(n));
}

static ExWidget *make_content(ExPanes *p, const ExTab *t, void *user) {
    App *a = user;
    char msg[600];
    ExOpenResult res = EX_OPEN_OK;
    ExNote *n;
    ExWidget *ed;
    (void)p;
    if (strcmp(t->view, "settings") == 0) return make_settings(a);
    if (!a->vault) {
        ed = ex_widget_editor(a->ui, a->demo_doc, "Welcome");
        return ed;
    }
    if (strcmp(t->view, "graph") == 0) return make_graph(a);
    if (strcmp(t->view, "localgraph") == 0) return make_local_graph(a);
    if (!t->path[0]) return info_label(a, "An empty tab. Open a note from the file explorer, or create one with New note.");
    if (!strcmp(t->view, "pdf")) { ExPdfViewHandlers ph; memset(&ph, 0, sizeof ph); ph.user = a; ph.load = pdf_load; ph.open_external = audio_open_external; ph.open_url = pdf_open_url; ph.set_clipboard = pdf_clipboard; ph.message = base_message; ph.link_name = pdf_link_name; return ex_widget_pdfview(a->ui, t->path, &ph); }
    if (!strcmp(t->view, "media")) { ExMediaViewHandlers mh; memset(&mh, 0, sizeof mh); mh.user = a; mh.load = media_load; mh.current = media_current; mh.open_external = audio_open_external; return ex_widget_mediaview(a->ui, t->path, &mh); }
    if (!strcmp(t->view, "audio")) { ExAudioViewHandlers ah; memset(&ah, 0, sizeof ah); ah.user = a; ah.load = audio_load; ah.open_external = audio_open_external; return ex_widget_audioview(a->ui, a->player, t->path, &ah); }
    if (!strcmp(t->view, "image")) return ex_widget_imageview(a->ui, a->icache, t->path);                  /* a picture: read and decoded on the service thread (6c-i) */
    if (!a->notes) return info_label(a, "Notes cannot be edited because the recovery journal could not be opened.");
    n = note_for_tab(a, t->id);                                                 /* switching between reading and editing keeps the note open */
    if (!n) n = ex_notes_open(a->notes, t->path, &res);
    if (!n) {
        snprintf(msg, sizeof msg, res == EX_OPEN_INVALID_UTF8 ? "%s is not a UTF-8 text file, so Exhaust will not open or change it."
                                 : res == EX_OPEN_NOT_FOUND ? "%s does not exist any more. Close this tab, or restore the file." : "%s could not be opened.", t->path);
        return info_label(a, msg);
    }
    if (!strcmp(t->view, "base")) return make_base(a, n, res, t);              /* a .base file: its views over the vault (6b-iii) */
    if (!strcmp(t->view, "slides")) return make_slides(a, n, t);               /* a note as slides, one at a time (6e-iii) */
    if (!strcmp(t->view, "canvas")) return make_canvas(a, n, res, t);          /* a .canvas file: its text is JSON, drawn as a board (6a) */
    if (is_reading(t->mode)) {                                                  /* reading mode: the note as formatted pages */
        size_t tl = 0;
        char *txt = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), &tl);
        ExWidget *rv = txt ? ex_widget_reading(a->ui, txt, tl, t->path) : NULL;
        if (rv) {
            make_title(a, rv, t->id);
            ex_reading_set_link_handler(rv, on_reading_link, a);
            { ExReadingMedia rm; memset(&rm, 0, sizeof rm); rm.user = a; rm.images = a->icache; rm.resolve = embed_resolve; rm.audio_state = embed_audio_state; ex_reading_set_media(rv, &rm); }
            if (a->panels) ex_panels_expand_queries(a->panels, t->path, txt, tl);
            if (!note_for_tab(a, t->id)) remember_tab_note(a, t->id, n);
            free(txt);
            return rv;
        }
        free(txt);
    }
    ed = ex_widget_editor(a->ui, ex_note_doc(n), t->path);
    if (!ed) return NULL;
    ex_editor_set_wrap(ed->editor, 1);                                          /* long lines continue on the next row, at the width of the editor */
    ex_editor_set_state(ed->editor, t->cursor_line, t->cursor_column, t->scroll_y);
    ex_editor_set_mode(ed->editor, editor_mode_for(t->mode));
    make_title(a, ed, t->id);
    if (!note_for_tab(a, t->id)) remember_tab_note(a, t->id, n);
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
    if (a->findbar) ex_findbar_target_closed(a->findbar, content);
    if (a->diff_doc && a->diff_editor == content) { show_banner(a, BANNER_NONE, "", NULL, NULL, NULL); stop_showing_diff(a); }     /* the editor that shows a diff goes away: its own text comes back first */
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

static int strcasecmp_ascii(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b; if (x != y) return x < y ? -1 : 1; }
    return *a ? 1 : *b ? -1 : 0;
}

static void open_path(App *a, ExId group, const char *rel) {
    ExId t = ex_ws_find_tab(a->ws, rel);
    if (t) { ex_ws_activate(a->ws, t); a->focus_tab = t; return; }
    if (ex_file_kind(rel) == EX_KIND_CANVAS) { t = ex_ws_new_tab(a->ws, group, rel, "canvas"); a->focus_tab = t; return; }
    if (ex_file_kind(rel) == EX_KIND_IMAGE) { t = ex_ws_new_tab(a->ws, group, rel, "image"); a->focus_tab = t; return; }
    if (ex_file_kind(rel) == EX_KIND_AUDIO) {                                                  /* m4a, webm and 3gp are libmpv's; the rest the built-in decoders' */
        const char *dot = strrchr(rel, '.');
        int mpv = dot && (!strcasecmp_ascii(dot, ".m4a") || !strcasecmp_ascii(dot, ".webm") || !strcasecmp_ascii(dot, ".3gp"));
        t = ex_ws_new_tab(a->ws, group, rel, mpv ? "media" : "audio"); a->focus_tab = t; return;
    }
    if (ex_file_kind(rel) == EX_KIND_PDF) { t = ex_ws_new_tab(a->ws, group, rel, "pdf"); a->focus_tab = t; return; }
    if (ex_file_kind(rel) == EX_KIND_VIDEO) { t = ex_ws_new_tab(a->ws, group, rel, "media"); a->focus_tab = t; return; }
    if (ex_file_kind(rel) == EX_KIND_BASE && (!a->features || ex_features_enabled(a->features, "bases"))) { t = ex_ws_new_tab(a->ws, group, rel, "base"); a->focus_tab = t; return; }
    t = ex_ws_new_tab(a->ws, group, rel, "note");
    if (t && strcmp(ex_settings_get(a->settings, "editor", "default_view"), "reading") == 0) ex_ws_set_tab_mode(a->ws, t, "reading");
    else if (t && strcmp(ex_settings_get(a->settings, "editor", "default_mode"), "source") == 0) ex_ws_set_tab_mode(a->ws, t, "source");
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
    if (ex_file_kind(rel) != EX_KIND_MARKDOWN && ex_file_kind(rel) != EX_KIND_BASE && ex_file_kind(rel) != EX_KIND_CANVAS && ex_file_kind(rel) != EX_KIND_IMAGE && ex_file_kind(rel) != EX_KIND_AUDIO && ex_file_kind(rel) != EX_KIND_VIDEO && ex_file_kind(rel) != EX_KIND_PDF) {
        char msg[400];
        snprintf(msg, sizeof msg, "%s cannot be shown.", rel);
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

static int high_contrast_now(App *a);
static void on_toggle_theme(ExWidget *w, void *user) {
    App *a = user;
    ExTheme next;
    int to_dark = !a->theme.is_dark;
    (void)w;
    if (ex_theme_init_builtin(&next, high_contrast_now(a) ? (to_dark ? EX_THEME_HC_DARK : EX_THEME_HC_LIGHT) : (to_dark ? EX_THEME_DARK : EX_THEME_LIGHT)) == 0) {
        ex_theme_free(&a->theme);
        a->theme = next;
        ex_ui_set_theme(a->ui, &a->theme);
        ex_settings_set(a->settings, "appearance", "color_scheme", to_dark ? "dark" : "light");   /* written at once */
        {   /* the vault manager has no vault, so it reads the setting kept with the program: both windows must agree */
            char *cfg = ex_platform_config_dir();
            ExSettings *global = ex_settings_open(NULL, cfg);
            if (global) { ex_settings_set(global, "appearance", "color_scheme", to_dark ? "dark" : "light"); ex_settings_close(global); }
            free(cfg);
        }
    }
}

/* The theme from the settings, built by the worker (7d): hands it what the settings say now and takes a newer theme when one is ready. Returns 1 when the
 * theme on screen changed (the caller draws again). */
/* Is high contrast on: the setting, or the system's when the setting says system. The system is asked once (and again after the setting was changed). */
static int high_contrast_now(App *a) {
    const char *hc = ex_settings_get(a->settings, "appearance", "high_contrast");
    if (hc && !strcmp(hc, "system")) { if (a->sys_hc < 0) a->sys_hc = ex_platform_high_contrast(); /* ui-ok: one short question to the system, when the user chose it */ }
    else a->sys_hc = -1;
    return ex_high_contrast_resolve(hc, a->sys_hc > 0);
}

static int theme_sync(App *a) {
    ExTheme next;
    const char *scheme, *theme, *accent, *snippets;
    if (!a->themes || !a->settings) return 0;
    scheme = ex_settings_get(a->settings, "appearance", "color_scheme");
    theme = ex_settings_get(a->settings, "appearance", "theme");
    accent = ex_settings_get(a->settings, "appearance", "accent_color");
    snippets = ex_settings_get(a->settings, "appearance", "snippets_enabled");
    ex_themes_set_high_contrast(a->themes, high_contrast_now(a));
    ex_themes_set_inputs(a->themes, scheme ? scheme : "dark", SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT, theme ? theme : "", accent ? accent : "", snippets ? snippets : "");
    if (!ex_themes_take(a->themes, &next, NULL)) return 0;
    ex_theme_free(&a->theme);
    a->theme = next;
    ex_ui_set_theme(a->ui, &a->theme);
    return 1;
}

static const ExTheme *current_theme_cb(void *u) { return &((App *)u)->theme; }

static void toggle_side(App *a, ExSide side) {
    ex_ws_set_sidebar(a->ws, side, !ex_ws_sidebar_visible(a->ws, side), ex_ws_sidebar_width(a->ws, side));
}

/* ---- commands ---- */

static void cmd_toggle_left(void *u) { toggle_side(u, EX_SIDE_LEFT); }
static void cmd_toggle_right(void *u) { toggle_side(u, EX_SIDE_RIGHT); }
/* Collapse the main text view to its handle by widening a sidebar over it (the right one when it is shown, else the left), and give the width back. */
static void cmd_toggle_main_view(void *u) {
    App *a = u;
    if (a->main_collapsed) {
        ex_ws_set_sidebar(a->ws, a->main_collapsed_side, 1, a->main_prev_width);
        a->main_collapsed = 0;
    } else {
        ExSide side = ex_ws_sidebar_visible(a->ws, EX_SIDE_RIGHT) ? EX_SIDE_RIGHT : EX_SIDE_LEFT;
        a->main_collapsed_side = side;
        a->main_prev_width = ex_sidebar_fill(side == EX_SIDE_RIGHT ? a->right_sb : a->left_sb);
        a->main_collapsed = 1;
    }
}
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
static void capture_tab_state(App *a);
static ExId new_popout(App *a, ExId tab);
static void enter_tab_window(App *a, ExId tab);
static void leave_pop(App *a);
static void cmd_toggle_reading(void *u) {
    App *a = u;
    ExId tab = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    const ExTab *t = tab ? ex_ws_tab(a->ws, tab) : NULL;
    const char *next;
    ExWidget *w;
    if (!t || !note_for_tab(a, tab) || a->diff_doc) return;
    if (is_reading(t->mode)) next = strcmp(ex_settings_get(a->settings, "editor", "default_mode"), "source") == 0 ? "source" : "live";
    else next = "reading";
    capture_tab_state(a);
    ex_ws_set_tab_mode(a->ws, tab, next);
    t = ex_ws_tab(a->ws, tab);
    enter_tab_window(a, tab);                                       /* the new content is made for the window that shows the tab (7g) */
    w = make_content(a->panes, t, a);
    if (w) {
        if (a->findbar) ex_findbar_target_closed(a->findbar, ex_panes_content(a->panes, tab));
        ex_panes_replace_content(a->panes, tab, w);
        a->focus_tab = tab;
    }
    leave_pop(a);
}

/* ---- the pane header: history and the pane menu (sub-slice 5d) ---- */

static int host_file_op(void *u, int what, const char *path, const char *arg);
static void move_tab_to_sidebar(App *a, ExId tab);
static void move_tab_to_main(App *a, ExId tab);

/* A place of a pane's history still exists: a note must still be a file of the vault; a graph always does. */
static int place_exists(const ExPlace *pl, void *u) {
    App *a = u;
    char abs[1400];
    ExPathInfo info;
    if (strcmp(pl->view, "note") != 0) return 1;
    if (!a->vault) return 0;
    snprintf(abs, sizeof abs, "%s/%s", ex_vault_path(a->vault), pl->path);
    return ex_platform_path_info(abs, &info) == 0;
}

/* Shows a place in a pane: the tab the pane already has for it comes to the front; otherwise a tab is made, with the cursor and scroll the
 * pane had when it left the place. */
static void show_place(App *a, ExId group, const char *path, const char *view, int line, int column, float scroll) {
    size_t i;
    ExId t = 0;
    for (i = 0; i < ex_ws_group_tab_count(a->ws, group); i++) {
        const ExTab *tab = ex_ws_tab(a->ws, ex_ws_group_tab(a->ws, group, i));
        if (tab && !strcmp(tab->path, path) && !strcmp(tab->view, view)) { t = tab->id; break; }
    }
    if (!t) {
        t = ex_ws_new_tab(a->ws, group, path, view);
        if (t && !strcmp(view, "note")) {
            if (strcmp(ex_settings_get(a->settings, "editor", "default_view"), "reading") == 0) ex_ws_set_tab_mode(a->ws, t, "reading");
            else if (strcmp(ex_settings_get(a->settings, "editor", "default_mode"), "source") == 0) ex_ws_set_tab_mode(a->ws, t, "source");
            ex_ws_set_tab_state(a->ws, t, line, column, scroll);
        }
    }
    if (t) { ex_ws_set_active_group(a->ws, group); ex_ws_activate(a->ws, t); a->focus_tab = t; }
}

static void pane_navigate(ExPanes *p, ExId group, int dir, void *u) {
    App *a = u;
    const ExPlace *pl;
    char path[700], view[40];
    int line, column;
    float scroll;
    (void)p;
    pl = ex_ws_history_go_valid(a->ws, group, dir, place_exists, a);
    if (!pl) return;
    snprintf(path, sizeof path, "%s", pl->path); snprintf(view, sizeof view, "%s", pl->view);   /* copies: opening a tab changes the workspace */
    line = pl->line; column = pl->column; scroll = pl->scroll;
    show_place(a, group, path, view, line, column, scroll);
}
static void cmd_nav_back(void *u) { App *a = u; pane_navigate(a->panes, ex_ws_active_group(a->ws), -1, a); }
static void cmd_nav_forward(void *u) { App *a = u; pane_navigate(a->panes, ex_ws_active_group(a->ws), 1, a); }

/* What each pane shows is a place in its history: once a frame, the active tab of every pane is recorded (the same place again changes
 * nothing). A tab with no file and no view of its own (a new tab) is not a place. */
static void record_places(App *a) {
    size_t i;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        if (!t || !t->view || (!t->path[0] && !strcmp(t->view, "empty"))) continue;
        if (ex_ws_group_active(a->ws, t->group) == t->id) ex_ws_history_visit(a->ws, t->group, t->path, t->view);
    }
}

static void pane_toggle_reading(ExPanes *p, ExId group, void *u) {
    App *a = u;
    (void)p;
    ex_ws_set_active_group(a->ws, group);
    ex_commands_run(a->commands, "app.toggle-reading-view");
}

enum { PM_RIGHT = 1, PM_WINDOW, PM_COPY, PM_SHOW, PM_BOOKMARK, PM_CLOSE, PM_SIDE, PM_MAIN };

static void reveal_in_explorer(App *a, const char *path) {
    if (!a->tree || !path[0]) return;
    ex_tree_select_path(a->tree, path);
    if (a->panels) ex_panels_show(a->panels, "files");
}

static void pane_menu_chosen(ExPanels *pn, int id, void *arg) {
    App *a = arg;
    const ExTab *t = ex_ws_tab(a->ws, ex_ws_group_active(a->ws, a->menu_group));
    char path[700], view[40];
    (void)pn;
    if (!t) return;
    snprintf(path, sizeof path, "%s", t->path); snprintf(view, sizeof view, "%s", t->view);
    switch (id) {
    case PM_RIGHT: {
        ExId ng = ex_ws_split(a->ws, a->menu_group, EX_SPLIT_HORIZONTAL, 1, 0);
        if (ng) show_place(a, ng, path, view, t->cursor_line, t->cursor_column, t->scroll_y);
        break;
    }
    case PM_WINDOW: new_popout(a, t->id); break;                                       /* a window of its own (7g) */
    case PM_COPY: if (path[0]) { host_file_op(a, EX_FILEOP_COPY_REL, path, NULL); set_status(a, "Path copied."); } break;
    case PM_SHOW: reveal_in_explorer(a, path); break;
    case PM_BOOKMARK: if (a->panels && path[0]) ex_panels_bookmark_note(a->panels, path); break;
    case PM_CLOSE: ex_ws_close_tab(a->ws, t->id); break;
    case PM_SIDE: move_tab_to_sidebar(a, t->id); break;
    case PM_MAIN: move_tab_to_main(a, t->id); break;
    }
}

/* The three-dot menu of a pane: the entries of the note menu that make sense for a pane. */
static void pane_menu(ExPanes *p, ExId group, float x, float y, void *u) {
    App *a = u;
    const ExTab *t = ex_ws_tab(a->ws, ex_ws_group_active(a->ws, group));
    ExMenuItem items[9];
    size_t n = 0;
    int is_note = t && !strcmp(t->view, "note");
    (void)p;
    if (!t || !a->panels) return;
    a->menu_group = group;
    ex_ws_set_active_group(a->ws, group);
    if (group != ex_ws_side_group(a->ws, EX_SIDE_RIGHT)) { ExMenuItem it = {"Open to the right", EX_ICON_NEW_NOTE, PM_RIGHT, 0, 0}; items[n++] = it; }       /* the sidebar's group is not split */
    { ExMenuItem it = {"Open in new window", EX_ICON_EXTERNAL, PM_WINDOW, 0, 0}; items[n++] = it; }
    if (is_note) {
        { ExMenuItem it = {"Copy path", EX_ICON_CLIPBOARD, PM_COPY, 0, 1}; items[n++] = it; }
        { ExMenuItem it = {"Show in the explorer", EX_ICON_FILES, PM_SHOW, 0, 0}; items[n++] = it; }
        { ExMenuItem it = {"Bookmark", EX_ICON_BOOKMARK, PM_BOOKMARK, 0, 0}; items[n++] = it; }
    }
    if (group == ex_ws_side_group(a->ws, EX_SIDE_RIGHT)) { ExMenuItem it = {"Move to main area", EX_ICON_MOVE, PM_MAIN, 0, 1}; items[n++] = it; }
    else { ExMenuItem it = {"Move to right sidebar", EX_ICON_SIDEBAR_RIGHT, PM_SIDE, 0, 1}; items[n++] = it; }
    { ExMenuItem it = {"Close", EX_ICON_TRASH, PM_CLOSE, 0, 1}; items[n++] = it; }
    ex_panels_popup(a->panels, x, y, items, n, pane_menu_chosen, a);
}

/* ---- tabs in the right sidebar (sub-slice 5e) ---- */

/* A reading view shows the text of its note when it was built; an edit made in another place (the same note in an editor in the main area or in
 * the right sidebar) must show in it. Once a frame the version of each reading tab's note is compared with what the view shows; when it has
 * changed and stayed the same for 200 ms (so typing does not rebuild the page for every key) the view takes the new text. */
typedef struct ReadingSeen { ExId tab; unsigned long shown, seen; Uint64 seen_at; } ReadingSeen;
static ReadingSeen g_reading_seen[16];

static void refresh_reading_views(App *a) {
    size_t i, k;
    Uint64 now = SDL_GetTicks();
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExNote *n;
        ExWidget *w;
        ReadingSeen *rs = NULL;
        unsigned long ver;
        if (!t || !t->view || strcmp(t->view, "note") != 0 || !is_reading(t->mode)) continue;
        n = note_for_tab(a, t->id);
        w = ex_panes_content(a->panes, t->id);
        if (!n || !w || w->type != EX_WIDGET_READING || !w->reading) continue;
        ver = ex_doc_version(ex_note_doc(n));
        for (k = 0; k < 16; k++) if (g_reading_seen[k].tab == t->id) { rs = &g_reading_seen[k]; break; }
        if (!rs) {
            for (k = 0; k < 16 && g_reading_seen[k].tab; k++) {}
            if (k == 16) continue;
            rs = &g_reading_seen[k];
            rs->tab = t->id; rs->shown = rs->seen = ver; rs->seen_at = now;
            continue;
        }
        if (ver != rs->seen) { rs->seen = ver; rs->seen_at = now; }
        if (ver != rs->shown && now - rs->seen_at >= 200) {
            size_t tl = 0;
            char *txt = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), &tl);
            if (txt) { ex_reading_set_text(w, txt, tl); if (a->panels) ex_panels_expand_queries(a->panels, t->path, txt, tl); free(txt); }
            rs->shown = ver;
        }
    }
}


/* The first group of the main window (a place to move a tab back to). */
static ExId first_main_group(App *a) {
    ExId n = ex_ws_window_root(a->ws, ex_ws_main_window(a->ws));
    while (n && ex_ws_node_kind(a->ws, n) == EX_NODE_SPLIT && ex_ws_split_count(a->ws, n) > 0) n = ex_ws_split_child(a->ws, n, 0);
    return n;
}

static void move_tab_to_sidebar(App *a, ExId tab) {
    ExId sg = ex_ws_side_group_ensure(a->ws, EX_SIDE_RIGHT);
    const ExTab *t = ex_ws_tab(a->ws, tab);
    if (!sg || !t || t->group == sg) return;
    if (a->findbar) ex_findbar_target_closed(a->findbar, ex_panes_content(a->panes, tab));
    ex_ws_move_tab(a->ws, tab, sg, (size_t)-1);
    ex_ws_set_sidebar(a->ws, EX_SIDE_RIGHT, 1, ex_ws_sidebar_width(a->ws, EX_SIDE_RIGHT));      /* shown, if it was hidden */
    a->right_tabs = 1;
    a->focus_tab = tab;
}

static void move_tab_to_main(App *a, ExId tab) {
    ExId sg = ex_ws_side_group(a->ws, EX_SIDE_RIGHT), mg = first_main_group(a);
    const ExTab *t = ex_ws_tab(a->ws, tab);
    if (!sg || !t || t->group != sg || !mg) return;
    ex_ws_move_tab(a->ws, tab, mg, (size_t)-1);
    a->focus_tab = tab;
}

static void cmd_move_to_sidebar(void *u) { App *a = u; move_tab_to_sidebar(a, ex_ws_group_active(a->ws, ex_ws_active_group(a->ws))); }
static void cmd_move_to_main(void *u) { App *a = u; move_tab_to_main(a, ex_ws_group_active(a->ws, ex_ws_active_group(a->ws))); }

/* Once a frame: the sidebar shows its tabs when it has some (a tab just moved there brings them forward) and the panels when the user
 * pressed a view button of its strip; the panels' view is hidden while the tabs show, and the strip stays above them. */
static void sync_right_sidebar(App *a) {
    ExId sg = ex_ws_side_group(a->ws, EX_SIDE_RIGHT);
    int has = sg && ex_ws_group_tab_count(a->ws, sg) > 0, to_tabs = 0, showing;
    ExAxis fit = {EX_SIZING_FIT, 0, 0};
    ExWidget *pr = a->panels ? ex_panels_right(a->panels) : NULL, *sr = ex_panes_side_root(a->panes);
    if (a->panels && ex_panels_take_right_click(a->panels, &to_tabs)) a->right_tabs = to_tabs;
    if (has && !a->had_side_tabs) a->right_tabs = 1;
    a->had_side_tabs = has;
    showing = has && a->right_tabs;
    if (a->panels) ex_panels_set_side_tabs(a->panels, has, showing);
    if (pr) pr->height = showing ? fit : ex_size_grow();
    if (sr) sr->hidden = !showing;
}
static int has_note_tab(void *u) { App *a = u; ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws)); return t && note_for_tab(a, t) != NULL; }
static void fold_cmd(App *a, ExFoldCmd cmd) { if (a->editor && a->editor->editor) ex_editor_fold(a->editor->editor, cmd); }
static void cmd_fold_all(void *u) { fold_cmd(u, EX_FOLD_ALL); }
static void cmd_unfold_all(void *u) { fold_cmd(u, EX_FOLD_UNFOLD_ALL); }
static void cmd_fold_more(void *u) { fold_cmd(u, EX_FOLD_MORE); }
static void cmd_fold_less(void *u) { fold_cmd(u, EX_FOLD_LESS); }
static void cmd_fold_toggle(void *u) { fold_cmd(u, EX_FOLD_TOGGLE_LINE); }
static void cmd_find(void *u) { App *a = u; if (a->findbar && a->editor) ex_findbar_open(a->findbar, a->editor, 0); }
static void cmd_find_replace(void *u) { App *a = u; if (a->findbar && a->editor) ex_findbar_open(a->findbar, a->editor, 1); }
static void format_cmd(App *a, ExFormat f) { if (a->editor && a->editor->editor) ex_editor_format(a->editor->editor, f); }
static void cmd_fmt_toggle_bold(void *u) { format_cmd(u, EX_FMT_BOLD); }
static void cmd_fmt_toggle_italic(void *u) { format_cmd(u, EX_FMT_ITALIC); }
static void cmd_fmt_toggle_strikethrough(void *u) { format_cmd(u, EX_FMT_STRIKE); }
static void cmd_fmt_toggle_highlight(void *u) { format_cmd(u, EX_FMT_HIGHLIGHT); }
static void cmd_fmt_toggle_code(void *u) { format_cmd(u, EX_FMT_CODE); }
static void cmd_fmt_toggle_inline_math(void *u) { format_cmd(u, EX_FMT_INLINE_MATH); }
static void cmd_fmt_toggle_comment(void *u) { format_cmd(u, EX_FMT_COMMENT); }
static void cmd_fmt_toggle_heading(void *u) { format_cmd(u, EX_FMT_HEADING_CYCLE); }
static void cmd_fmt_set_as_heading_1(void *u) { format_cmd(u, EX_FMT_HEADING_1); }
static void cmd_fmt_set_as_heading_2(void *u) { format_cmd(u, EX_FMT_HEADING_2); }
static void cmd_fmt_set_as_heading_3(void *u) { format_cmd(u, EX_FMT_HEADING_3); }
static void cmd_fmt_set_as_heading_4(void *u) { format_cmd(u, EX_FMT_HEADING_4); }
static void cmd_fmt_set_as_heading_5(void *u) { format_cmd(u, EX_FMT_HEADING_5); }
static void cmd_fmt_set_as_heading_6(void *u) { format_cmd(u, EX_FMT_HEADING_6); }
static void cmd_fmt_remove_heading(void *u) { format_cmd(u, EX_FMT_REMOVE_HEADING); }
static void cmd_fmt_toggle_bullet_list(void *u) { format_cmd(u, EX_FMT_BULLET); }
static void cmd_fmt_toggle_numbered_list(void *u) { format_cmd(u, EX_FMT_NUMBERED); }
static void cmd_fmt_toggle_blockquote(void *u) { format_cmd(u, EX_FMT_QUOTE); }
static void cmd_fmt_toggle_checkbox_status(void *u) { format_cmd(u, EX_FMT_CHECKBOX_STATUS); }
static void cmd_fmt_cycle_bullet_checkbox(void *u) { format_cmd(u, EX_FMT_CYCLE_BULLET_CHECKBOX); }
static void cmd_fmt_indent_list_item(void *u) { format_cmd(u, EX_FMT_INDENT); }
static void cmd_fmt_unindent_list_item(void *u) { format_cmd(u, EX_FMT_UNINDENT); }
static void cmd_fmt_move_line_up(void *u) { format_cmd(u, EX_FMT_MOVE_LINE_UP); }
static void cmd_fmt_move_line_down(void *u) { format_cmd(u, EX_FMT_MOVE_LINE_DOWN); }
static void cmd_fmt_delete_paragraph(void *u) { format_cmd(u, EX_FMT_DELETE_PARAGRAPH); }
static void cmd_fmt_insert_horizontal_rule(void *u) { format_cmd(u, EX_FMT_HR); }
static void cmd_fmt_insert_code_block(void *u) { format_cmd(u, EX_FMT_CODE_BLOCK); }
static void cmd_fmt_insert_callout(void *u) { format_cmd(u, EX_FMT_CALLOUT); }
static void cmd_fmt_insert_math_block(void *u) { format_cmd(u, EX_FMT_MATH_BLOCK); }
static void cmd_fmt_insert_table(void *u) { format_cmd(u, EX_FMT_TABLE); }
/* The table commands (5f): each is available only where the caret is in a table and the command applies there. */
static void table_cmd(App *a, ExTableCmd c) { if (a->editor && a->editor->editor) ex_editor_table(a->editor->editor, c); }
static int table_can(App *a, ExTableCmd c) { return a->editor && a->editor->editor && ex_editor_table_can(a->editor->editor, c); }
static void cmd_tbl_add_col_left(void *u) { table_cmd(u, EX_TBL_COL_LEFT); }
static int can_tbl_add_col_left(void *u) { return table_can(u, EX_TBL_COL_LEFT); }
static void cmd_tbl_add_col_right(void *u) { table_cmd(u, EX_TBL_COL_RIGHT); }
static int can_tbl_add_col_right(void *u) { return table_can(u, EX_TBL_COL_RIGHT); }
static void cmd_tbl_add_row_above(void *u) { table_cmd(u, EX_TBL_ROW_ABOVE); }
static int can_tbl_add_row_above(void *u) { return table_can(u, EX_TBL_ROW_ABOVE); }
static void cmd_tbl_add_row_below(void *u) { table_cmd(u, EX_TBL_ROW_BELOW); }
static int can_tbl_add_row_below(void *u) { return table_can(u, EX_TBL_ROW_BELOW); }
static void cmd_tbl_align_center(void *u) { table_cmd(u, EX_TBL_ALIGN_CENTER); }
static int can_tbl_align_center(void *u) { return table_can(u, EX_TBL_ALIGN_CENTER); }
static void cmd_tbl_align_left(void *u) { table_cmd(u, EX_TBL_ALIGN_LEFT); }
static int can_tbl_align_left(void *u) { return table_can(u, EX_TBL_ALIGN_LEFT); }
static void cmd_tbl_align_right(void *u) { table_cmd(u, EX_TBL_ALIGN_RIGHT); }
static int can_tbl_align_right(void *u) { return table_can(u, EX_TBL_ALIGN_RIGHT); }
static void cmd_tbl_delete_col(void *u) { table_cmd(u, EX_TBL_DEL_COL); }
static int can_tbl_delete_col(void *u) { return table_can(u, EX_TBL_DEL_COL); }
static void cmd_tbl_delete_row(void *u) { table_cmd(u, EX_TBL_DEL_ROW); }
static int can_tbl_delete_row(void *u) { return table_can(u, EX_TBL_DEL_ROW); }
static void cmd_tbl_dup_col(void *u) { table_cmd(u, EX_TBL_DUP_COL); }
static int can_tbl_dup_col(void *u) { return table_can(u, EX_TBL_DUP_COL); }
static void cmd_tbl_dup_row(void *u) { table_cmd(u, EX_TBL_DUP_ROW); }
static int can_tbl_dup_row(void *u) { return table_can(u, EX_TBL_DUP_ROW); }
static void cmd_tbl_move_col_left(void *u) { table_cmd(u, EX_TBL_MOVE_COL_LEFT); }
static int can_tbl_move_col_left(void *u) { return table_can(u, EX_TBL_MOVE_COL_LEFT); }
static void cmd_tbl_move_col_right(void *u) { table_cmd(u, EX_TBL_MOVE_COL_RIGHT); }
static int can_tbl_move_col_right(void *u) { return table_can(u, EX_TBL_MOVE_COL_RIGHT); }
static void cmd_tbl_move_row_down(void *u) { table_cmd(u, EX_TBL_MOVE_ROW_DOWN); }
static int can_tbl_move_row_down(void *u) { return table_can(u, EX_TBL_MOVE_ROW_DOWN); }
static void cmd_tbl_move_row_up(void *u) { table_cmd(u, EX_TBL_MOVE_ROW_UP); }
static int can_tbl_move_row_up(void *u) { return table_can(u, EX_TBL_MOVE_ROW_UP); }
static void cmd_fmt_insert_footnote(void *u) { format_cmd(u, EX_FMT_FOOTNOTE); }
static void cmd_fmt_insert_markdown_link(void *u) { format_cmd(u, EX_FMT_MD_LINK); }
static void cmd_fmt_add_internal_link(void *u) { format_cmd(u, EX_FMT_INTERNAL_LINK); }
static void cmd_fmt_add_embed(void *u) { format_cmd(u, EX_FMT_EMBED); }
static void cmd_fmt_add_tag(void *u) { format_cmd(u, EX_FMT_TAG); }
static void cmd_fmt_clear_formatting(void *u) { format_cmd(u, EX_FMT_CLEAR); }
static void cmd_cursor_above(void *u) { App *a = u; if (a->editor && a->editor->editor) ex_editor_perform(a->editor->editor, a->r, EX_ACT_ADD_CURSOR_ABOVE, 0); }
static void cmd_cursor_below(void *u) { App *a = u; if (a->editor && a->editor->editor) ex_editor_perform(a->editor->editor, a->r, EX_ACT_ADD_CURSOR_BELOW, 0); }
/* ---- spell check and word count ---- */

static char *custom_words_path(App *a) {
    char *p;
    if (!a->vault) return NULL;
    p = malloc(strlen(ex_vault_path(a->vault)) + 48);
    if (p) sprintf(p, "%s/.exhaust/dictionaries/custom.txt", ex_vault_path(a->vault));
    return p;
}

static void load_custom_words(App *a) {
    char *path = custom_words_path(a), *text;
    size_t len = 0, i = 0;
    if (!path || !a->spell) { free(path); return; }
    text = ex_platform_read_file(path, &len);
    free(path);
    if (!text) return;
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n' && text[e] != '\r') e++;
        if (e > i) ex_spell_add_word(a->spell, text + i, e - i);
        i = e + 1;
    }
    free(text);
}

/* The user's own words are kept in .exhaust/dictionaries/custom.txt, one on each line, so they travel with the vault. */
static void remember_custom_word(App *a, const char *w, size_t n) {
    char *path = custom_words_path(a), *old, *dir, *slash;
    size_t olen = 0;
    char *all;
    if (!path) return;
    old = ex_platform_read_file(path, &olen);
    dir = malloc(strlen(path) + 1);
    if (dir) memcpy(dir, path, strlen(path) + 1);
    slash = dir ? strrchr(dir, '/') : NULL;
    if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    all = malloc(olen + n + 2);
    if (all) {
        if (old) memcpy(all, old, olen);
        if (olen && all[olen - 1] != '\n') all[olen++] = '\n';
        memcpy(all + olen, w, n);
        all[olen + n] = '\n';
        ex_platform_write_file_atomic(path, all, olen + n + 1);
        free(all);
    }
    free(old);
    free(dir);
    free(path);
}

static void spell_changed_everywhere(App *a) {
    size_t i;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        ExWidget *w = ex_panes_content(a->panes, ex_ws_tab_at(a->ws, i));
        if (w && w->type == EX_WIDGET_EDITOR && w->editor) ex_editor_spell_changed(w->editor);
    }
}

static void cmd_toggle_spellcheck(void *u) {
    App *a = u;
    ex_settings_set_bool(a->settings, "editor", "spellcheck", !ex_settings_get_bool(a->settings, "editor", "spellcheck"));
    set_status(a, ex_settings_get_bool(a->settings, "editor", "spellcheck") ? "Spell check is on." : "Spell check is off.");
}

static void cmd_spell_add(void *u) {
    App *a = u;
    size_t ws, we;
    char w[EX_SPELL_WORD_MAX], msg[160];
    if (!a->spell || !a->editor || !a->editor->editor || !ex_editor_word_at_caret(a->editor->editor, &ws, &we) || we - ws >= sizeof w) { set_status(a, "Put the cursor in a word first."); return; }
    ex_buffer_read(ex_doc_buffer(ex_editor_doc(a->editor->editor)), ws, w, we - ws);
    w[we - ws] = 0;
    if (ex_spell_add_word(a->spell, w, we - ws) == 0) {
        remember_custom_word(a, w, we - ws);
        spell_changed_everywhere(a);
        snprintf(msg, sizeof msg, "Added \"%s\" to your dictionary (.exhaust/dictionaries/custom.txt).", w);
        set_status(a, msg);
    }
}

static void cmd_spell_replace(void *u) {
    App *a = u;
    ExEditor *e = a->editor && a->editor->editor ? a->editor->editor : NULL;
    size_t ws, we;
    char w[EX_SPELL_WORD_MAX], msg[400];
    ExChange c;
    ExSel after;
    if (!a->spell || !e || ex_editor_readonly(e) || !ex_editor_word_at_caret(e, &ws, &we) || we - ws >= sizeof w) { set_status(a, "Put the cursor in a misspelled word first."); return; }
    ex_buffer_read(ex_doc_buffer(ex_editor_doc(e)), ws, w, we - ws);
    w[we - ws] = 0;
    if (a->spell_nsug && a->spell_idx < a->spell_nsug && strcmp(w, a->spell_sug[a->spell_idx]) == 0) {
        a->spell_idx = (a->spell_idx + 1) % a->spell_nsug;                   /* the word is the last suggestion: the next one */
    } else {
        a->spell_nsug = ex_spell_suggest(a->spell, w, we - ws, a->spell_sug, 8);
        a->spell_idx = 0;
        if (ex_spell_check(a->spell, w, we - ws)) { a->spell_nsug = 0; set_status(a, "This word is spelled correctly."); return; }
        if (!a->spell_nsug) { set_status(a, "No suggestions for this word."); return; }
    }
    c.offset = ws; c.remove_len = we - ws; c.text = a->spell_sug[a->spell_idx]; c.text_len = strlen(c.text);
    after.anchor = after.head = ws + c.text_len;
    ex_doc_apply(ex_editor_doc(e), &c, 1, 0, &after, 1);
    {
        size_t i, n = 0;
        n += (size_t)snprintf(msg, sizeof msg, "Suggestion %zu of %zu:", a->spell_idx + 1, a->spell_nsug);
        for (i = 0; i < a->spell_nsug && n < sizeof msg - 80; i++) n += (size_t)snprintf(msg + n, sizeof msg - n, " %s%s", i == a->spell_idx ? "> " : "", a->spell_sug[i]);
        set_status(a, msg);
    }
}

/* The word count of the note being edited (or of the selection), at most about three times a second and only when something changed. */
static void update_wordcount(App *a) {
    ExEditor *e = a->editor ? a->editor->editor : NULL;
    unsigned now = (unsigned)SDL_GetTicks();
    char *text, buf[96];
    size_t len = 0;
    ExWordCount wc;
    ExSel sel;
    ExDoc *d;
    if (!a->wordcount) return;
    if (a->wordcount->hidden) return;
    if (!e) { if (a->wc_doc) { ex_widget_set_text(a->wordcount, ""); a->wc_doc = NULL; } return; }
    d = ex_editor_doc(e);
    sel = ex_doc_sel(d, ex_doc_primary(d));
    if (a->wc_doc == d && a->wc_version == ex_doc_version(d) && a->wc_sel == (sel.anchor != sel.head ? ((size_t)(sel.anchor ^ (sel.head << 1)) | 1u) : 0)) return;
    if (a->wc_doc == d && now - a->wc_tick < 300) return;
    a->wc_tick = now;
    text = ex_buffer_text(ex_doc_buffer(d), &len);
    if (!text) return;
    wc = ex_wordcount(text, len, ex_editor_tokens(e));
    if (sel.anchor != sel.head) {
        size_t lo = sel.anchor < sel.head ? sel.anchor : sel.head, hi = sel.anchor < sel.head ? sel.head : sel.anchor;
        ExWordCount ws = ex_wordcount(text + lo, hi - lo, NULL);
        snprintf(buf, sizeof buf, "%zu of %zu words, %zu characters", ws.words, wc.words, wc.characters);
    } else snprintf(buf, sizeof buf, "%zu words, %zu characters", wc.words, wc.characters);
    free(text);
    ex_widget_set_text(a->wordcount, buf);
    a->wc_doc = d;
    a->wc_version = ex_doc_version(d);
    a->wc_sel = sel.anchor != sel.head ? ((size_t)(sel.anchor ^ (sel.head << 1)) | 1u) : 0;
}

static void on_wordcount_feature(const char *id, int enabled, void *user) {
    App *a = user;
    (void)id;
    if (a->wordcount) { a->wordcount->hidden = !enabled; a->wc_doc = NULL; }
}

static int has_editor(void *u) { return ((App *)u)->editor != NULL; }
static void cmd_new_note(void *u) { on_new_note(NULL, "", u); }
static int has_notes(void *u) { return ((App *)u)->notes != NULL && ((App *)u)->vault != NULL; }
static void cmd_close_tab(void *u) {
    App *a = u;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    if (t) ex_ws_close_tab(a->ws, t);
}
static int has_side_tab(void *u) { App *a = u; ExId sg = ex_ws_side_group(a->ws, EX_SIDE_RIGHT); return sg && ex_ws_active_group(a->ws) == sg && ex_ws_group_active(a->ws, sg) != 0; }
static int has_tab(void *u) { App *a = u; return ex_ws_group_active(a->ws, ex_ws_active_group(a->ws)) != 0; }

static void on_files_feature(const char *id, int enabled, void *user) {
    App *a = user;
    (void)id;
    if (a->left_content) a->left_content->hidden = !enabled;
}

static void apply_ribbon(App *a);
static void on_graph_feature(const char *id, int enabled, void *user) {
    App *a = user;
    (void)id; (void)enabled;
    apply_ribbon(a);                                                        /* the graph button needs the feature */
}

/* graph-view.open-graph-view: the graph tab, in the active group; one that is already open comes to the front. */
static void cmd_open_graph(void *u) {
    App *a = u;
    size_t i;
    ExId t = 0;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        const ExTab *tab = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        if (tab && tab->view && !strcmp(tab->view, "graph")) { t = tab->id; break; }
    }
    if (t) ex_ws_activate(a->ws, t);
    else t = ex_ws_new_tab(a->ws, ex_ws_active_group(a->ws), "", "graph");
    a->focus_tab = t;
}

/* app.open-settings (7a): the settings screen in a tab of the active group; an open one comes to the front. */
static void cmd_open_settings(void *u) {
    App *a = u;
    size_t i;
    ExId t = 0;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        const ExTab *tab = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        if (tab && tab->view && !strcmp(tab->view, "settings")) { t = tab->id; break; }
    }
    if (t) ex_ws_activate(a->ws, t);
    else t = ex_ws_new_tab(a->ws, ex_ws_active_group(a->ws), "", "settings");
    a->focus_tab = t;
}

/* graph-view.open-local-graph: the local graph tab (the open note and the notes around it), in the active group; an open one comes to the front. */
static void cmd_open_local_graph(void *u) {
    App *a = u;
    size_t i;
    ExId t = 0;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        const ExTab *tab = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        if (tab && tab->view && !strcmp(tab->view, "localgraph")) { t = tab->id; break; }
    }
    if (t) ex_ws_activate(a->ws, t);
    else t = ex_ws_new_tab(a->ws, ex_ws_active_group(a->ws), "", "localgraph");
    a->focus_tab = t;
}

/* The graph widget of the active tab, if it shows a graph (global or local). */
static ExWidget *active_graph(App *a) {
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    ExWidget *c = t && a->panes ? ex_panes_content(a->panes, t) : NULL;
    return c && c->type == EX_WIDGET_GRAPH ? c : NULL;
}
static int has_graph(void *u) { return active_graph(u) != NULL; }
/* graph-view.start-graph-timelapse-animation: the notes appear in the order they were created (the wand button does the same). */
static void cmd_graph_animation(void *u) { ExWidget *g = active_graph(u); if (g) ex_graphview_animation_toggle(g); }


/* ---- the ribbon and the status bar (7f) ---- */

static void ribbon_run(void *user, const char *command_id) { ex_commands_run(((App *)user)->commands, command_id); }

/* The ribbon from the settings: the bar at all, and which buttons. */
static void apply_ribbon(App *a) {
    int show;
    if (!a->ribbon || !a->settings) return;
    show = ex_settings_get_bool(a->settings, "interface", "show_ribbon");
    ex_ribbon_apply(a->ribbon, show, ex_settings_get(a->settings, "interface", "ribbon_items"), a->features ? ex_features_enabled(a->features, "graph") : 1, a->features ? ex_features_enabled(a->features, "canvas") : 1);
    if (a->ribbon_line) a->ribbon_line->hidden = !show;
}

/* Once a frame: the items of the status bar that follow the active note. Touches a label only when its value changed. */
static void update_statusbar(App *a) {
    const char *mode = NULL;
    int save = EX_SAVE_NONE, poll;
    if (!a->sbar) return;
    if (a->note) {
        ExId tab = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
        const ExTab *t = tab ? ex_ws_tab(a->ws, tab) : NULL;
        mode = t && t->mode && t->mode[0] ? t->mode : "live";
        save = ex_note_error(a->note) ? EX_SAVE_FAILED : !ex_note_exists_on_disk(a->note) && !ex_note_dirty(a->note) ? EX_SAVE_NONE : ex_note_dirty(a->note) ? EX_SAVE_EDITING : EX_SAVE_SAVED;
    }
    {
        char key[16];
        int on = ex_settings_get_bool(a->settings, "editor", "show_mode_in_status_bar");
        snprintf(key, sizeof key, "%s%s", on ? "+" : "-", mode ? mode : "");
        if (strcmp(key, a->sb_mode)) { snprintf(a->sb_mode, sizeof a->sb_mode, "%s", key); ex_statusbar_set_mode(a->sbar, mode, on); }
    }
    if (save != a->sb_save) { a->sb_save = save; ex_statusbar_set_save(a->sbar, (ExSaveState)save); }
    poll = a->notes ? ex_notes_watcher_polling(a->notes) : 0;
    if (poll != a->sb_poll) { a->sb_poll = poll; ex_statusbar_set_polling(a->sbar, poll); }
}

/* The ribbon chooser (the Interface page's button): a list of the buttons with Shown or Hidden before each; choosing one flips it and the list opens again. */
static void ribbon_choose(App *a, size_t selected);
static void ribbon_flip(size_t index, void *arg) {
    App *a = arg;
    const ExRibbonItem *it = ex_ribbon_item(index);
    char *next;
    if (!it) return;
    next = ex_ribbon_set(ex_settings_get(a->settings, "interface", "ribbon_items"), it->id, !ex_ribbon_shown(ex_settings_get(a->settings, "interface", "ribbon_items"), it->id));
    if (next) { ex_settings_set(a->settings, "interface", "ribbon_items", next); free(next); }
    apply_ribbon(a);
    ribbon_choose(a, index);
}
static void ribbon_choose(App *a, size_t selected) {
    size_t n = ex_ribbon_item_count(), i;
    static char text[16][110];
    const char *labels[16];
    const char *list = ex_settings_get(a->settings, "interface", "ribbon_items");
    if (!a->panels || n > 16) return;
    for (i = 0; i < n; i++) {
        snprintf(text[i], sizeof text[i], "%s: %s", ex_ribbon_shown(list, ex_ribbon_item(i)->id) ? "Shown" : "Hidden", ex_ribbon_item(i)->name);
        labels[i] = text[i];
    }
    ex_panels_ask_choice_at(a->panels, "Ribbon items (choose one to show or hide it)", labels, n, selected, ribbon_flip, a);
}

/* ---- zoom (7f) ---- */

/* The interface zoom from the settings: at once, whoever changed the setting (a command, the slider on the Appearance page, the file). */
static void apply_zoom(App *a) {
    size_t i;
    float z = (float)ex_zoom_clamp(ex_settings_get_int(a->settings, "interface", "zoom_percent")) / 100.0f;
    ex_renderer_set_zoom(a->main_r ? a->main_r : a->r, z);
    for (i = 0; i < a->npops; i++) ex_renderer_set_zoom(ex_popwin_renderer(a->pops[i]), z);
}
static void set_zoom(App *a, int percent) {
    ex_settings_set_int(a->settings, "interface", "zoom_percent", percent);        /* written at once */
    apply_zoom(a);
}
static void cmd_zoom_in(void *u) { App *a = u; set_zoom(a, ex_zoom_step(ex_settings_get_int(a->settings, "interface", "zoom_percent"), 1)); }
static void cmd_zoom_out(void *u) { App *a = u; set_zoom(a, ex_zoom_step(ex_settings_get_int(a->settings, "interface", "zoom_percent"), -1)); }
static void cmd_zoom_reset(void *u) { set_zoom(u, 100); }

/* ---- workspaces (7e): saved layouts ---- */

static int has_layouts(void *u) { App *a = u; return a->layouts != NULL && a->vault != NULL && a->panels != NULL; }
static int has_saved_layouts(void *u) { App *a = u; return has_layouts(u) && ex_layouts_count(a->layouts) > 0; }

static void layout_save_do(App *a) {
    char msg[200];
    capture_tab_state(a);                                                    /* the cursors and scroll positions as they are now */
    if (ex_layouts_save(a->layouts, a->pending_layout, a->ws, ex_platform_unix_time()) == 0) { snprintf(msg, sizeof msg, "Saved the layout \"%s\".", a->pending_layout); set_status(a, msg); }
    else set_status(a, "That layout could not be saved.");
}
static void layout_replace_answer(int button, void *arg) { if (button == 1) layout_save_do(arg); }
static void layout_named(const char *text, void *arg) {
    App *a = arg;
    char name[64];
    size_t b = 0, e = strlen(text);
    while (text[b] == ' ') b++;
    while (e > b && text[e - 1] == ' ') e--;
    if (e - b >= sizeof name) e = b + sizeof name - 1;
    memcpy(name, text + b, e - b); name[e - b] = 0;
    if (!ex_layouts_name_valid(name)) { set_status(a, "A layout's name is 1 to 60 characters and has no [ ] or / in it."); return; }
    snprintf(a->pending_layout, sizeof a->pending_layout, "%s", name);
    if (ex_layouts_find(a->layouts, name)) {
        char msg[200];
        snprintf(a->pending_layout, sizeof a->pending_layout, "%s", ex_layouts_find(a->layouts, name));
        snprintf(msg, sizeof msg, "A layout named \"%s\" is saved already. Replace it with the layout on screen?", a->pending_layout);
        ex_panels_ask_confirm(a->panels, "Replace the layout", msg, "Replace", "Cancel", NULL, layout_replace_answer, a);
    } else layout_save_do(a);
}
static void cmd_layout_save(void *u) { App *a = u; ex_panels_ask_text(a->panels, "Save workspace layout", "Name of the layout", "", layout_named, a); }

static int layout_note_exists(void *u, const char *path) {
    App *a = u;
    char *abs = malloc(strlen(ex_vault_path(a->vault)) + strlen(path) + 2);
    int ok;
    if (!abs) return 1;
    sprintf(abs, "%s/%s", ex_vault_path(a->vault), path);
    ok = ex_platform_file_exists(abs);                                         /* ui-ok: one stat for each open tab, on the user's own command */
    free(abs);
    return ok;
}

static void layout_load_index(size_t index, void *arg) {
    App *a = arg;
    const char *name = ex_layouts_name(a->layouts, index);
    char msg[200];
    if (!name) return;
    snprintf(msg, sizeof msg, "%s", name);
    stop_showing_diff(a);
    if (ex_layouts_load(a->layouts, msg, a->ws, layout_note_exists, a) == 0) {
        snprintf(msg, sizeof msg, "Loaded the layout \"%s\".", name);
        set_status(a, msg);
    } else set_status(a, "That layout could not be read. The tabs stay as they are.");
}
static void cmd_layout_load(void *u) {
    App *a = u;
    size_t n = ex_layouts_count(a->layouts), i;
    const char *labels[64];
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) labels[i] = ex_layouts_name(a->layouts, i);
    ex_panels_ask_choice(a->panels, "Load workspace layout", labels, n, layout_load_index, a);
}

static void layout_manage_answer(int button, void *arg) {
    App *a = arg;
    char msg[200];
    if (button == 2) layout_save_do(a);                                         /* Replace with the current layout */
    else if (button == 3) {
        if (ex_layouts_delete(a->layouts, a->pending_layout) == 0) { snprintf(msg, sizeof msg, "Deleted the layout \"%s\".", a->pending_layout); set_status(a, msg); }
    }
}
static void layout_manage_index(size_t index, void *arg) {
    App *a = arg;
    const char *name = ex_layouts_name(a->layouts, index);
    char msg[300], when[40];
    int64_t t;
    if (!name) return;
    snprintf(a->pending_layout, sizeof a->pending_layout, "%s", name);
    t = ex_layouts_saved_time(a->layouts, name);
    if (t > 0) ex_history_format_time(t, when, sizeof when); else snprintf(when, sizeof when, "an unknown time");
    snprintf(msg, sizeof msg, "The layout \"%s\" was saved on %s.", a->pending_layout, when);
    ex_panels_ask_confirm(a->panels, "Manage workspace layouts", msg, "Cancel", "Replace with the current layout", "Delete", layout_manage_answer, a);
}
static void cmd_layout_manage(void *u) {
    App *a = u;
    size_t n = ex_layouts_count(a->layouts), i;
    const char *labels[64];
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) labels[i] = ex_layouts_name(a->layouts, i);
    ex_panels_ask_choice(a->panels, "Manage workspace layouts", labels, n, layout_manage_index, a);
}

/* ---- file recovery (7e) ---- */

static void apply_history(App *a) {
    if (!a->notes || !a->features) return;
    ex_notes_set_history(a->notes, ex_features_enabled(a->features, "file_recovery"), (int)ex_settings_get_int(a->settings, "file_recovery", "interval_minutes"), (int)ex_settings_get_int(a->settings, "file_recovery", "history_days"));
}
static void on_recovery_feature(const char *id, int enabled, void *user) { (void)id; (void)enabled; apply_history(user); }

static void cmd_history(void *u) {
    App *a = u;
    if (a->panels && a->note && ex_notes_history_dir(a->notes)) ex_panels_history_open(a->panels, ex_notes_history_dir(a->notes), ex_note_path(a->note));
}

/* The user picked an earlier version: show what differs from the note, with a banner to restore it. */
static void host_history_version(void *u, const char *rel, const char *label, const char *old_text, size_t len, const char *diff) {
    App *a = u;
    char when[24], msg[400];
    if (!a->note || !a->editor || strcmp(ex_note_path(a->note), rel) != 0) { set_status(a, "Open the note again to compare it with that version."); return; }
    stop_showing_diff(a);
    free(a->hist_old);
    a->hist_old = malloc(len + 1);
    if (!a->hist_old) return;
    memcpy(a->hist_old, old_text, len); a->hist_old[len] = 0; a->hist_old_len = len;
    snprintf(a->hist_rel, sizeof a->hist_rel, "%s", rel);
    snprintf(a->hist_label, sizeof a->hist_label, "%s", label);
    snprintf(when, sizeof when, "%.19s", label);
    show_diff_doc(a, diff && diff[0] ? diff : "(this version has the same text as the note now)\n");
    a->banner_note = a->note;
    snprintf(msg, sizeof msg, "Comparing with the version of %s. Lines with - are only in that version, lines with + only in the note now.", when);
    show_banner(a, BANNER_HISTORY, msg, "Restore this version", "Back to the note", NULL);
}

static void clear_history_answer(int button, void *arg) {
    App *a = arg;
    if (button == 1 && a->panels && a->notes) ex_panels_history_clear(a->panels, ex_notes_history_dir(a->notes));
}
/* The File recovery page's buttons (7e) and the other actions of the settings screen. */
static void settings_action(void *u, const char *id) {
    App *a = u;
    if (!strcmp(id, "ribbon_items")) { ribbon_choose(a, 0); return; }
    if (!strcmp(id, "window_size")) {
        int w = 0, h = 0;
        char msg[120];
        SDL_GetWindowSize(ex_renderer_window(a->r), &w, &h);
        ex_settings_set_int(a->settings, "appearance", "window_width", ex_window_size_clamp(w));
        ex_settings_set_int(a->settings, "appearance", "window_height", ex_window_size_clamp(h));
        snprintf(msg, sizeof msg, "New windows will start at %d by %d.", ex_window_size_clamp(w), ex_window_size_clamp(h));
        set_status(a, msg);
        return;
    }
    if (!strcmp(id, "clear_history") && a->panels && a->notes)
        ex_panels_ask_confirm(a->panels, "Clear the history", "Delete every snapshot of every note?\nThe copies kept when a note was changed on disk and in Exhaust at the same time stay.", "Delete", "Cancel", NULL, clear_history_answer, a);
}

static void register_features_and_commands(App *a) {
    a->features = ex_features_create(a->settings);
    ex_features_register(a->features, "files", "Files", on_files_feature, a);
    ex_features_register(a->features, "graph", "Graph view", on_graph_feature, a);
    ex_features_register(a->features, "canvas", "Canvas", on_graph_feature, a);
    ex_features_register(a->features, "bases", "Bases", NULL, a);
    on_files_feature("files", ex_features_enabled(a->features, "files"), a);
    ex_features_register(a->features, "slash_commands", "Slash commands", NULL, a);
    ex_features_register(a->features, "word_count", "Word count", on_wordcount_feature, a);
    ex_features_register(a->features, "file_recovery", "File recovery", on_recovery_feature, a);
    a->commands = ex_commands_create(a->features);
    ex_commands_bind(a->commands, "graph-view.open-graph-view", cmd_open_graph, NULL, a);
    ex_commands_bind(a->commands, "daily-notes.open-today-s-daily-note", cmd_daily_today, has_notes, a);
    ex_commands_bind(a->commands, "daily-notes.open-next-daily-note", cmd_daily_next, has_notes, a);
    ex_commands_bind(a->commands, "daily-notes.open-previous-daily-note", cmd_daily_prev, has_notes, a);
    ex_commands_bind(a->commands, "templates.insert-current-date", cmd_tpl_date, has_editor, a);
    ex_commands_bind(a->commands, "templates.insert-current-time", cmd_tpl_time, has_editor, a);
    ex_commands_bind(a->commands, "templates.insert-template", cmd_tpl_insert, has_editor, a);
    ex_commands_bind(a->commands, "slides.start-presentation", cmd_slides_start, has_notes, a);
    ex_commands_bind(a->commands, "format-converter.frontmatter-migration", cmd_format_convert, has_notes, a);
    ex_commands_bind(a->commands, "unique-note-creator.create-new-unique-note", cmd_unique_new, has_notes, a);
    ex_commands_bind(a->commands, "unique-note-creator.add-unique-internal-link", cmd_unique_link, has_editor, a);
    ex_commands_bind(a->commands, "note-composer.extract-current-selection", cmd_extract_selection, has_editor, a);
    ex_commands_bind(a->commands, "note-composer.extract-this-heading", cmd_extract_heading, has_editor, a);
    ex_commands_bind(a->commands, "note-composer.merge-current-file-with-another-file", cmd_merge, has_editor, a);
    ex_commands_bind(a->commands, "app.open-settings", cmd_open_settings, NULL, a);
    ex_commands_bind(a->commands, "bases.create-new-base", cmd_new_base, has_notes, a);
    ex_commands_bind(a->commands, "bases.add-view", cmd_base_add_view, has_base, a);
    ex_commands_bind(a->commands, "bases.switch-view", cmd_base_switch_view, has_base, a);
    ex_commands_bind(a->commands, "bases.copy-table-to-clipboard", cmd_base_copy, has_base, a);
    ex_commands_bind(a->commands, "bases.add-item", cmd_base_add_item, has_base, a);
    ex_commands_bind(a->commands, "bases.insert-new-base", cmd_base_insert, has_editor, a);
    ex_commands_bind(a->commands, "canvas.create-new-canvas", cmd_new_canvas, has_notes, a);
    ex_commands_bind(a->commands, "canvas.add-text-card", cmd_canvas_add_text, has_canvas, a);
    ex_commands_bind(a->commands, "canvas.add-note-card", cmd_canvas_add_note, has_canvas, a);
    ex_commands_bind(a->commands, "canvas.add-group", cmd_canvas_add_group, has_canvas, a);
    ex_commands_bind(a->commands, "canvas.zoom-to-fit", cmd_canvas_fit, has_canvas, a);
    ex_commands_bind(a->commands, "canvas.jump-to-group", cmd_canvas_jump, has_canvas, a);
    ex_commands_bind(a->commands, "canvas.convert-to-file", cmd_canvas_convert, has_canvas_selection, a);
    ex_commands_bind(a->commands, "graph-view.open-local-graph", cmd_open_local_graph, has_notes, a);
    ex_commands_bind(a->commands, "graph-view.start-graph-timelapse-animation", cmd_graph_animation, has_graph, a);
    ex_commands_bind(a->commands, "app.toggle-left-sidebar", cmd_toggle_left, NULL, a);
    ex_commands_bind(a->commands, "app.toggle-right-sidebar", cmd_toggle_right, NULL, a);
    ex_commands_bind(a->commands, "app.toggle-main-text-view", cmd_toggle_main_view, NULL, a);
    ex_commands_bind(a->commands, "app.change-theme", cmd_change_theme, NULL, a);
    ex_commands_bind(a->commands, "app.change-vault", cmd_change_vault, NULL, a);
    ex_commands_bind(a->commands, "app.toggle-live-preview-source-mode", cmd_toggle_mode, has_editor, a);
    ex_commands_bind(a->commands, "editor.fold-all-headings-and-lists", cmd_fold_all, has_editor, a);
    ex_commands_bind(a->commands, "editor.unfold-all-headings-and-lists", cmd_unfold_all, has_editor, a);
    ex_commands_bind(a->commands, "editor.fold-more", cmd_fold_more, has_editor, a);
    ex_commands_bind(a->commands, "editor.fold-less", cmd_fold_less, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-fold-on-the-current-line", cmd_fold_toggle, has_editor, a);
    ex_commands_bind(a->commands, "app.search-current-file", cmd_find, has_editor, a);
    ex_commands_bind(a->commands, "app.search-and-replace-in-current-file", cmd_find_replace, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-bold", cmd_fmt_toggle_bold, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-italic", cmd_fmt_toggle_italic, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-strikethrough", cmd_fmt_toggle_strikethrough, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-highlight", cmd_fmt_toggle_highlight, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-code", cmd_fmt_toggle_code, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-inline-math", cmd_fmt_toggle_inline_math, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-comment", cmd_fmt_toggle_comment, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-heading", cmd_fmt_toggle_heading, has_editor, a);
    ex_commands_bind(a->commands, "editor.set-as-heading-1", cmd_fmt_set_as_heading_1, has_editor, a);
    ex_commands_bind(a->commands, "editor.set-as-heading-2", cmd_fmt_set_as_heading_2, has_editor, a);
    ex_commands_bind(a->commands, "editor.set-as-heading-3", cmd_fmt_set_as_heading_3, has_editor, a);
    ex_commands_bind(a->commands, "editor.set-as-heading-4", cmd_fmt_set_as_heading_4, has_editor, a);
    ex_commands_bind(a->commands, "editor.set-as-heading-5", cmd_fmt_set_as_heading_5, has_editor, a);
    ex_commands_bind(a->commands, "editor.set-as-heading-6", cmd_fmt_set_as_heading_6, has_editor, a);
    ex_commands_bind(a->commands, "editor.remove-heading", cmd_fmt_remove_heading, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-bullet-list", cmd_fmt_toggle_bullet_list, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-numbered-list", cmd_fmt_toggle_numbered_list, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-blockquote", cmd_fmt_toggle_blockquote, has_editor, a);
    ex_commands_bind(a->commands, "editor.toggle-checkbox-status", cmd_fmt_toggle_checkbox_status, has_editor, a);
    ex_commands_bind(a->commands, "editor.cycle-bullet-checkbox", cmd_fmt_cycle_bullet_checkbox, has_editor, a);
    ex_commands_bind(a->commands, "editor.indent-list-item", cmd_fmt_indent_list_item, has_editor, a);
    ex_commands_bind(a->commands, "editor.unindent-list-item", cmd_fmt_unindent_list_item, has_editor, a);
    ex_commands_bind(a->commands, "editor.move-line-up", cmd_fmt_move_line_up, has_editor, a);
    ex_commands_bind(a->commands, "editor.move-line-down", cmd_fmt_move_line_down, has_editor, a);
    ex_commands_bind(a->commands, "editor.delete-paragraph", cmd_fmt_delete_paragraph, has_editor, a);
    ex_commands_bind(a->commands, "editor.insert-horizontal-rule", cmd_fmt_insert_horizontal_rule, has_editor, a);
    ex_commands_bind(a->commands, "editor.insert-code-block", cmd_fmt_insert_code_block, has_editor, a);
    ex_commands_bind(a->commands, "editor.insert-callout", cmd_fmt_insert_callout, has_editor, a);
    ex_commands_bind(a->commands, "editor.insert-math-block", cmd_fmt_insert_math_block, has_editor, a);
    ex_commands_bind(a->commands, "editor.insert-table", cmd_fmt_insert_table, has_editor, a);
    ex_commands_bind(a->commands, "table.add-column-to-the-left", cmd_tbl_add_col_left, can_tbl_add_col_left, a);
    ex_commands_bind(a->commands, "table.add-column-to-the-right", cmd_tbl_add_col_right, can_tbl_add_col_right, a);
    ex_commands_bind(a->commands, "table.add-row-above", cmd_tbl_add_row_above, can_tbl_add_row_above, a);
    ex_commands_bind(a->commands, "table.add-row-below", cmd_tbl_add_row_below, can_tbl_add_row_below, a);
    ex_commands_bind(a->commands, "table.align-center", cmd_tbl_align_center, can_tbl_align_center, a);
    ex_commands_bind(a->commands, "table.align-left", cmd_tbl_align_left, can_tbl_align_left, a);
    ex_commands_bind(a->commands, "table.align-right", cmd_tbl_align_right, can_tbl_align_right, a);
    ex_commands_bind(a->commands, "table.delete-column", cmd_tbl_delete_col, can_tbl_delete_col, a);
    ex_commands_bind(a->commands, "table.delete-row", cmd_tbl_delete_row, can_tbl_delete_row, a);
    ex_commands_bind(a->commands, "table.duplicate-column", cmd_tbl_dup_col, can_tbl_dup_col, a);
    ex_commands_bind(a->commands, "table.duplicate-row", cmd_tbl_dup_row, can_tbl_dup_row, a);
    ex_commands_bind(a->commands, "table.move-column-left", cmd_tbl_move_col_left, can_tbl_move_col_left, a);
    ex_commands_bind(a->commands, "table.move-column-right", cmd_tbl_move_col_right, can_tbl_move_col_right, a);
    ex_commands_bind(a->commands, "table.move-row-down", cmd_tbl_move_row_down, can_tbl_move_row_down, a);
    ex_commands_bind(a->commands, "table.move-row-up", cmd_tbl_move_row_up, can_tbl_move_row_up, a);
    ex_commands_bind(a->commands, "editor.insert-footnote", cmd_fmt_insert_footnote, has_editor, a);
    ex_commands_bind(a->commands, "editor.insert-markdown-link", cmd_fmt_insert_markdown_link, has_editor, a);
    ex_commands_bind(a->commands, "editor.add-internal-link", cmd_fmt_add_internal_link, has_editor, a);
    ex_commands_bind(a->commands, "editor.add-embed", cmd_fmt_add_embed, has_editor, a);
    ex_commands_bind(a->commands, "editor.add-tag", cmd_fmt_add_tag, has_editor, a);
    ex_commands_bind(a->commands, "editor.clear-formatting", cmd_fmt_clear_formatting, has_editor, a);
    ex_commands_bind(a->commands, "editor.add-cursor-above", cmd_cursor_above, has_editor, a);
    ex_commands_bind(a->commands, "editor.add-cursor-below", cmd_cursor_below, has_editor, a);
    a->slash = ex_slash_create(a->commands);
    ex_commands_bind(a->commands, "app.toggle-spellcheck", cmd_toggle_spellcheck, NULL, a);
    ex_commands_bind(a->commands, "editor.spelling-add-word-to-dictionary", cmd_spell_add, has_editor, a);
    ex_commands_bind(a->commands, "editor.spelling-replace-with-suggestion", cmd_spell_replace, has_editor, a);
    ex_commands_bind(a->commands, "app.toggle-reading-view", cmd_toggle_reading, has_note_tab, a);
    ex_commands_bind(a->commands, "workspace.move-to-right-sidebar", cmd_move_to_sidebar, has_tab, a);
    ex_commands_bind(a->commands, "workspace.move-to-main-area", cmd_move_to_main, has_side_tab, a);
    ex_commands_bind(a->commands, "app.navigate-back", cmd_nav_back, NULL, a);
    ex_commands_bind(a->commands, "app.navigate-forward", cmd_nav_forward, NULL, a);
    ex_commands_bind(a->commands, "app.create-new-note", cmd_new_note, has_notes, a);
    ex_commands_bind(a->commands, "app.close-current-tab", cmd_close_tab, has_tab, a);
    ex_commands_bind(a->commands, "workspaces.save-workspace-layout", cmd_layout_save, has_layouts, a);
    ex_commands_bind(a->commands, "workspaces.load-workspace-layout", cmd_layout_load, has_saved_layouts, a);
    ex_commands_bind(a->commands, "workspaces.manage-workspace-layouts", cmd_layout_manage, has_saved_layouts, a);
    ex_commands_bind(a->commands, "file-recovery.open-local-history", cmd_history, has_note_tab, a);
    ex_commands_bind(a->commands, "app.zoom-in", cmd_zoom_in, NULL, a);
    ex_commands_bind(a->commands, "app.zoom-out", cmd_zoom_out, NULL, a);
    ex_commands_bind(a->commands, "app.reset-zoom", cmd_zoom_reset, NULL, a);
}

/* A key press as a chord of the hotkeys module (7c). Printable keys are their lower case character; a key that is only a modifier gives 0. */
static int chord_from_sdl(const SDL_KeyboardEvent *k, ExChord *c) {
    SDL_Keycode key = k->key;
    int code = 0;
    c->mods = 0;
    if (k->mod & SDL_KMOD_CTRL) c->mods |= EX_HK_CTRL;
    if (k->mod & SDL_KMOD_SHIFT) c->mods |= EX_HK_SHIFT;
    if (k->mod & SDL_KMOD_ALT) c->mods |= EX_HK_ALT;
    if (k->mod & SDL_KMOD_GUI) c->mods |= EX_HK_META;
    if (key >= SDLK_A && key <= SDLK_Z) code = (int)key;                      /* SDL's letter codes are lower case ascii */
    else if (key >= SDLK_0 && key <= SDLK_9) code = (int)key;
    else if (key >= SDLK_F1 && key <= SDLK_F12) code = EX_HK_F1 + (int)(key - SDLK_F1);
    else switch (key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: code = EX_HK_ENTER; break;
    case SDLK_TAB: code = EX_HK_TAB; break;
    case SDLK_ESCAPE: code = EX_HK_ESCAPE; break;
    case SDLK_SPACE: code = EX_HK_SPACE; break;
    case SDLK_BACKSPACE: code = EX_HK_BACKSPACE; break;
    case SDLK_DELETE: code = EX_HK_DELETE; break;
    case SDLK_INSERT: code = EX_HK_INSERT; break;
    case SDLK_HOME: code = EX_HK_HOME; break;
    case SDLK_END: code = EX_HK_END; break;
    case SDLK_PAGEUP: code = EX_HK_PAGEUP; break;
    case SDLK_PAGEDOWN: code = EX_HK_PAGEDOWN; break;
    case SDLK_LEFT: code = EX_HK_LEFT; break;
    case SDLK_RIGHT: code = EX_HK_RIGHT; break;
    case SDLK_UP: code = EX_HK_UP; break;
    case SDLK_DOWN: code = EX_HK_DOWN; break;
    case SDLK_COMMA: case SDLK_PERIOD: case SDLK_SLASH: case SDLK_SEMICOLON: case SDLK_APOSTROPHE: case SDLK_LEFTBRACKET: case SDLK_RIGHTBRACKET:
    case SDLK_BACKSLASH: case SDLK_MINUS: case SDLK_EQUALS: case SDLK_GRAVE: code = (int)key; break;
    case SDLK_PLUS: code = '+'; break;
    default: code = 0; break;
    }
    c->key = code;
    return code != 0;
}

/* Waiting for a key press for the hotkeys screen: Escape with no modifier cancels, a modifier alone is waited out. Returns 1 if the press was taken. */
static int capture_key(App *a, const SDL_KeyboardEvent *k) {
    ExChord c;
    char text[48];
    void (*fn)(void *, const char *) = a->cap_cb;
    void *arg = a->cap_arg;
    if (!fn) return 0;
    if (!chord_from_sdl(k, &c)) return 1;                                      /* Ctrl, Shift... on their own: keep waiting */
    a->cap_cb = NULL; a->cap_arg = NULL;
    a->swallow_text_until = SDL_GetTicks() + 80;
    if (c.key == EX_HK_ESCAPE && c.mods == 0) fn(arg, NULL);
    else if (ex_chord_format(&c, text, sizeof text)) fn(arg, text);
    else fn(arg, NULL);
    return 1;
}

static int run_shortcut(App *a, const SDL_KeyboardEvent *k) {
    ExChord c;
    const char *ids[16];
    size_t i, n;
    if (!a->hotkeys || !chord_from_sdl(k, &c)) return 0;
    n = ex_hotkeys_find(a->hotkeys, &c, ids, 16);
    for (i = 0; i < n; i++) {                                                  /* the first command of the shortcut that can run now */
        if (ex_commands_available(a->commands, ids[i])) { ex_commands_run(a->commands, ids[i]); return 1; }
    }
    return 0;
}

/* ---- the inline title (issue 8): the note's name above its text; changing it renames the file ---- */

static void note_name_of(const char *path, char *out, size_t cap) {
    const char *b = strrchr(path, '/');
    size_t n;
    b = b ? b + 1 : path;
    n = strlen(b);
    if (n > 3 && !strcmp(b + n - 3, ".md")) n -= 3;
    if (n >= cap) n = cap - 1;
    memcpy(out, b, n); out[n] = 0;
}

/* One title for each note editor (ISSUES 11): a child of the editor widget, so it scrolls with the text and every pane has its own. */
typedef struct Title { App *a; ExDoc *doc; ExWidget *w, *ed; ExId tab; char path[512]; int was_focused; } Title;

static void set_title_text(Title *t, const char *text) {
    size_t n = strlen(text), have = ex_buffer_size(ex_doc_buffer(t->doc));
    ExChange c;
    c.offset = 0; c.remove_len = have; c.text = text; c.text_len = n;
    ex_doc_apply(t->doc, &c, 1, 0, NULL, 0);
    ex_doc_set_caret(t->doc, n);
}

static void title_revert(Title *t) {
    char name[300];
    if (!t->path[0]) return;
    note_name_of(t->path, name, sizeof name);
    set_title_text(t, name);
}

/* Enter in the title: the note is renamed (a note that is not on disk yet is started again under the new name). */
static void title_commit(Title *t, int keep_focus) {
    App *a = t->a;
    size_t len = 0;
    char *typed = ex_buffer_text(ex_doc_buffer(t->doc), &len), path[512];
    ExNote *note = note_for_tab(a, t->tab);
    if (!typed || !t->path[0] || !a->panels) { free(typed); return; }
    snprintf(path, sizeof path, "%s", t->path);
    if (note && !ex_note_exists_on_disk(note) && ex_buffer_size(ex_doc_buffer(ex_note_doc(note))) == 0) {
        char to[700], name[300];
        const char *slash = strrchr(path, '/');
        size_t n = strlen(typed);
        while (n > 0 && typed[n - 1] == ' ') typed[--n] = 0;
        snprintf(name, sizeof name, "%s", typed);
        if (!name[0] || !ex_name_valid(name)) { set_status(a, "That is not a valid note name (no  # ^ [ ] | : and not empty)."); title_revert(t); free(typed); return; }
        snprintf(to, sizeof to, "%.*s%s%s.md", slash ? (int)(slash - path) : 0, path, slash ? "/" : "", name);
        if (strcmp(to, path)) {
            ExPathInfo info;
            char abs[1400];
            snprintf(abs, sizeof abs, "%s/%s", ex_vault_path(a->vault), to);
            if (ex_platform_path_info(abs, &info) == 0) { set_status(a, "A note with that name already exists."); title_revert(t); free(typed); return; }
            ex_ws_close_tab(a->ws, t->tab);                                      /* t is freed with the tab's content: nothing of it is used below */
            host_open_note(a, to, (size_t)-1, 0);
            free(typed);
            return;
        }
    } else if (ex_panels_rename_to(a->panels, path, typed) != 0) title_revert(t);
    free(typed);
    if (keep_focus && t->ed) ex_ui_set_focus(a->ui, t->ed);
}

#define NAME_FORBIDDEN "\\/:#^[]|"

/* The title is a file name: the characters a name cannot have are not typed (a bubble says which), the rest of a paste goes in. */
static int title_text(ExWidget *w, const char *utf8, size_t len, void *user) {
    Title *t = user;
    char clean[512];
    size_t i, o = 0;
    int dropped = 0;
    for (i = 0; i < len && o + 1 < sizeof clean; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 0x20 || c == 0x7f || (c < 0x80 && strchr(NAME_FORBIDDEN, c))) { dropped = 1; continue; }
        clean[o++] = (char)c;
    }
    if (!dropped) return 0;
    if (o) ex_editor_text_input(w->editor, clean, o);
    t->a->title_warn_until = SDL_GetTicks() + 3000;
    return 1;
}

static int title_key(ExWidget *w, ExKey key, int mods, void *user) {
    Title *t = user;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { title_commit(t, 1); return 1; }
    if (key == EX_KEY_ESCAPE) { title_revert(t); if (t->ed) ex_ui_set_focus(t->a->ui, t->ed); return 1; }
    if (key == EX_KEY_DOWN) { if (t->ed) ex_ui_set_focus(t->a->ui, t->ed); return 1; }
    return 0;
}

static void title_free(void *p) { Title *t = p; ex_doc_free(t->doc); free(t); }

/* The title widget of a note editor: large and bold, wrapping, no frame; it grows with its rows. */
static void make_title(App *a, ExWidget *ed, ExId tab) {
    Title *t = calloc(1, sizeof *t);
    if (!t) return;
    t->a = a; t->ed = ed; t->tab = tab;
    t->doc = ex_doc_create("", 0);
    t->w = t->doc ? ex_widget_editor(a->ui, t->doc, "Note title") : NULL;
    if (!t->w) { if (t->doc) ex_doc_free(t->doc); free(t); return; }
    ex_editor_set_mode(t->w->editor, EX_EDITOR_PLAIN);
    ex_editor_set_wrap(t->w->editor, 1);
    t->w->text_scale = 2.0f; t->w->bold = 1;
    t->w->width = ex_size_grow(); t->w->height = ex_size_token(EX_SIZE_SPACING, 7.0f);
    t->w->on_key = title_key; t->w->on_key_user = t;
    t->w->on_text = title_text; t->w->on_text_user = t;
    t->w->hidden = 1;
    t->w->clip = 0;                                  /* it is clipped to the editor it sits in, which draws it */
    t->w->owned = t; t->w->free_owned = title_free;
    ex_widget_add(ed, t->w);
}

/* Shows the title of every open note editor (not while it is being typed in). Leaving a title with a changed name renames the note, as Enter does. */
static void sync_title(App *a) {
    size_t i;
    ExId active = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        ExId tid = ex_ws_tab_at(a->ws, i);
        ExWidget *c = ex_panes_content(a->panes, tid);
        Title *t = c && (c->type == EX_WIDGET_EDITOR || c->type == EX_WIDGET_READING) && c->first_child ? c->first_child->owned : NULL;
        const ExTab *tab = ex_ws_tab(a->ws, tid);
        ExNote *note = note_for_tab(a, tid);
        int focused, show;
        if (!t || !tab) continue;
        focused = ex_ui_focus(a->ui) == t->w;
        if (t->was_focused && !focused && t->path[0]) {
            size_t len = 0;
            char *typed = ex_buffer_text(ex_doc_buffer(t->doc), &len), name[300];
            note_name_of(t->path, name, sizeof name);
            if (typed && strcmp(typed, name)) { free(typed); title_commit(t, 0); continue; }   /* clicked elsewhere or switched away: the rename is made */
            free(typed);
        }
        t->was_focused = focused;
        show = note && tab->path[0] && ex_file_kind(tab->path) == EX_KIND_MARKDOWN && ex_settings_get_bool(a->settings, "editor", "inline_title");
        t->w->hidden = !show;
        if (!show) { t->path[0] = 0; continue; }
        {   /* the field is as tall as the rows of the title (one frame behind the width: it is measured at the width of the last layout) */
            float h = ex_editor_content_height(t->w->editor, a->r), scale = ex_renderer_scale(a->r), one = (float)ex_px(a->r, 56);
            if (h < one) h = one;
            t->w->height = ex_size_px(a->ui, (h + (float)ex_px(a->r, 8)) / (scale > 0 ? scale : 1.0f));
        }
        if (strcmp(t->path, tab->path) || t->path[0] == 0) {
            snprintf(t->path, sizeof t->path, "%s", tab->path);
            title_revert(t);
            if (!ex_note_exists_on_disk(note) && tid == active) ex_ui_set_focus(a->ui, t->w);   /* a new note: the first thing typed is its name */
        }
    }
}

/* Which tab, note and editor are active; called before every layout. */
static void sync_ui(App *a) {
    ExId g, t;
    ExWidget *c;
    bases_follow_index(a);
    ex_imagecache_pump(a->icache, a->main_r);
    ex_sidebar_sync(a->left_sb);
    ex_sidebar_sync(a->right_sb);
    {   /* the main text view squeezed to less than a character's width is hidden; the two divider strips next to it are its handle (GitHub issue 2) */
        ExWidget *note = ex_panes_root(a->panes) ? ex_panes_root(a->panes)->parent : NULL;
        if (note) {
            int ww, wh;
            float scale = ex_renderer_scale(a->r), room;
            ex_renderer_size(a->r, &ww, &wh);
            room = (float)ww - (float)ex_theme_size(&a->theme, EX_SIZE_RIBBON_WIDTH) * scale - (a->left_sb->hidden ? 0.0f : a->left_sb->w) - (a->right_sb->hidden ? 0.0f : a->right_sb->w)
                   - 12.0f * scale;
            note->hidden = room < 20.0f * scale && !a->left_sb->hidden && !a->right_sb->hidden ? 1 : 0;
        }
    }
    ex_panes_update(a->panes);
    record_places(a);
    sync_right_sidebar(a);
    refresh_reading_views(a);
    ex_panes_set_header_visible(a->panes, ex_settings_get_bool(a->settings, "interface", "show_tab_title_bar"));
    {                                                                                /* the graph: read while a graph tab exists, updated once a frame */
        size_t i, graphs = 0;
        for (i = 0; i < ex_ws_tab_count(a->ws); i++) { const ExTab *gt = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i)); if (gt && gt->view && !strcmp(gt->view, "graph")) graphs++; }
        if (a->panels) ex_panels_graph_wanted(a->panels, graphs > 0);
        if (graphs == 0 && a->gm) { ex_graphmodel_free(a->gm); a->gm = NULL; }
        {   const char *ap = host_active_path(a);
            if (ap && (!a->graph_note || strcmp(ap, a->graph_note))) { free(a->graph_note); a->graph_note = dup_text(ap); }
        }
        if (a->gm) { ex_graphmodel_set_active(a->gm, a->graph_note); ex_graphmodel_update(a->gm); }
    }
    {                                                                                /* the local graph (5c): around the note last open in a tab */
        size_t i, locals = 0;
        for (i = 0; i < ex_ws_tab_count(a->ws); i++) { const ExTab *gt = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i)); if (gt && gt->view && !strcmp(gt->view, "localgraph")) locals++; }
        if (a->panels) ex_panels_local_graph(a->panels, locals > 0, a->graph_note);
        if (locals == 0 && a->gm_local) { ex_graphmodel_free(a->gm_local); a->gm_local = NULL; }
        if (a->gm_local) { ex_graphmodel_set_active(a->gm_local, a->graph_note); ex_graphmodel_update(a->gm_local); }
    }
    g = ex_ws_active_group(a->ws);
    t = ex_ws_group_active(a->ws, g);
    c = t ? ex_panes_content(a->panes, t) : NULL;
    a->editor = c && c->type == EX_WIDGET_EDITOR ? c : NULL;
    {                                                                                /* the fold and indent settings apply to every editor */
        int tabs = ex_settings_get_bool(a->settings, "editor", "indent_with_tabs"), width = ex_settings_get_int(a->settings, "editor", "indent_width");
        int fh = ex_settings_get_bool(a->settings, "editor", "fold_heading"), fi = ex_settings_get_bool(a->settings, "editor", "fold_indent");
        int slash_on = ex_features_enabled(a->features, "slash_commands"), spell_on = ex_settings_get_bool(a->settings, "editor", "spellcheck");
        size_t i;
        for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
            ExWidget *w = ex_panes_content(a->panes, ex_ws_tab_at(a->ws, i));
            if (w && w->type == EX_WIDGET_EDITOR && w->editor) { ex_editor_set_folding(w->editor, fh, fi); ex_editor_set_indent(w->editor, tabs, width); ex_editor_set_slash(w->editor, slash_on ? a->slash : NULL); ex_editor_set_spell(w->editor, spell_on ? a->spell : NULL); if (a->panels) ex_editor_set_link_suggest(w->editor, ex_panels_suggest_links, a->panels); ex_editor_set_link_click(w->editor, on_editor_link_click, a); }
        }
    }
    a->note = t ? note_for_tab(a, t) : NULL;
    a->title = c && (c->type == EX_WIDGET_EDITOR || c->type == EX_WIDGET_READING) && c->first_child ? c->first_child : NULL;
    sync_title(a);
    if (a->rb_left) a->rb_left->selected = ex_ws_sidebar_visible(a->ws, EX_SIDE_LEFT);          /* pressed while the sidebar shows */
    if (a->rb_right) a->rb_right->selected = ex_ws_sidebar_visible(a->ws, EX_SIDE_RIGHT);
    if (a->rb_mode) { const ExTab *rt = t ? ex_ws_tab(a->ws, t) : NULL; a->rb_mode->selected = rt && rt->mode && !strcmp(rt->mode, "source"); }
    if (a->sel_path && a->note && a->editor && !strcmp(ex_note_path(a->note), a->sel_path)) {      /* a link or a search result opened this note at a place */
        ex_editor_select(a->editor->editor, a->r, a->sel_offset, a->sel_offset);
        ex_ui_set_focus(ui_of(a, a->editor), a->editor);
        free(a->sel_path); a->sel_path = NULL;
    } else if (a->sel_path && a->note && !a->editor && !strcmp(ex_note_path(a->note), a->sel_path)) { free(a->sel_path); a->sel_path = NULL; }
    update_wordcount(a);
    update_statusbar(a);
    if (a->findbar && ex_findbar_is_open(a->findbar)) { if (a->editor) ex_findbar_set_target(a->findbar, a->editor); ex_findbar_update(a->findbar); }
    if (a->diff_doc && a->banner_kind == BANNER_HISTORY && a->note != a->banner_note) { stop_showing_diff(a); free(a->hist_old); a->hist_old = NULL; banner_for_note(a, a->note); }   /* another tab: the comparison ends */
    if (a->note != a->banner_note && !a->diff_doc) banner_for_note(a, a->note);
    if (a->focus_tab && a->focus_tab == t && c && c->on_key == slide_key) { ex_ui_set_focus(ui_of(a, c), c); a->focus_tab = 0; }       /* the slides take the keyboard (6e-iii) */
    else if (a->focus_tab && a->focus_tab == t && c && c->type == EX_WIDGET_CANVAS) { ex_ui_set_focus(ui_of(a, c), c); a->focus_tab = 0; }       /* a canvas takes the keyboard when its tab is shown (6a) */
    else if (a->focus_tab && a->focus_tab == t && a->editor) { if (!(a->title && !a->title->hidden && a->note && !ex_note_exists_on_disk(a->note))) ex_ui_set_focus(ui_of(a, a->editor), a->editor); a->focus_tab = 0; }   /* a new note keeps the focus on its title */
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
    canvas_save_views(a);                                                    /* the view of every open canvas (6a) */
}

/* ---- pop-out windows (7g) ---- */

/* The panes' callbacks, for the main window and for a pop-out (a pop-out has no pane menu yet: it would open in the main window). */
static ExPopWin *find_pop(App *a, ExId window);
/* A new pop-out window gets the size of a new window (7f) and a place a little off from the main window's. */
static ExId new_popout(App *a, ExId tab) {
    int x = 0, y = 0;
    ExId w = ex_ws_popout(a->ws, tab);
    if (!w || find_pop(a, w)) return w;
    SDL_GetWindowPosition(ex_renderer_window(a->main_r), &x, &y);
    ex_ws_window_set_rect(a->ws, w, x + 40, y + 40, ex_window_size_clamp(ex_settings_get_int(a->settings, "appearance", "window_width")), ex_window_size_clamp(ex_settings_get_int(a->settings, "appearance", "window_height")), 0);
    return w;
}
static void pane_popout(ExPanes *p, ExId tab, void *u) { (void)p; new_popout(u, tab); }   /* a tab was dragged out of the window: it has a window of its own */

static void fill_pane_hooks(App *a, ExPanesHooks *h, int popout) {
    memset(h, 0, sizeof *h);
    h->make_content = make_content;
    h->content_closed = content_closed;
    h->tab_shown = tab_shown;
    h->new_tab = a->vault ? new_empty_tab : NULL;
    h->drop_file = drop_file;
    h->popout = pane_popout;
    h->navigate = pane_navigate; h->toggle_reading = pane_toggle_reading;
    h->pane_menu = popout ? NULL : pane_menu;
}

static ExPopWin *find_pop(App *a, ExId window) {
    size_t i;
    for (i = 0; i < a->npops; i++) if (ex_popwin_window(a->pops[i]) == window) return a->pops[i];
    return NULL;
}

static ExPopWin *pop_by_sdl_id(App *a, Uint32 id) {
    size_t i;
    for (i = 0; id && i < a->npops; i++) if (ex_popwin_sdl_id(a->pops[i]) == id) return a->pops[i];
    return NULL;
}

/* The pop-out window an event is for, or NULL for the main window (and for events of no window). */
static ExPopWin *pop_for_event(App *a, const SDL_Event *e) {
    Uint32 id = 0;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: id = e->key.windowID; break;
    case SDL_EVENT_TEXT_INPUT: id = e->text.windowID; break;
    case SDL_EVENT_TEXT_EDITING: id = e->edit.windowID; break;
    case SDL_EVENT_MOUSE_MOTION: id = e->motion.windowID; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: id = e->button.windowID; break;
    case SDL_EVENT_MOUSE_WHEEL: id = e->wheel.windowID; break;
    case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_TEXT: id = e->drop.windowID; break;
    default: if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST) id = e->window.windowID; break;
    }
    return pop_by_sdl_id(a, id);
}

/* The code that handles an event or draws a frame works on a->r, a->ui, a->root and a->panes. For a pop-out they are its own, and the panels (a dialog or
 * a preview that would open in the main window) are left out until leave_pop. */
static void leave_pop(App *a) {
    if (!a->cur_pop) return;
    a->r = a->main_r; a->ui = a->main_ui; a->root = a->main_root; a->panes = a->main_panes;
    a->panels = a->saved_panels; a->saved_panels = NULL;
    a->cur_pop = NULL;
}
static void enter_pop(App *a, ExPopWin *pw) {
    if (a->cur_pop == pw) return;
    leave_pop(a);
    a->saved_panels = a->panels; a->panels = NULL;
    a->r = ex_popwin_renderer(pw); a->ui = ex_popwin_ui(pw); a->root = ex_popwin_root(pw); a->panes = ex_popwin_panes(pw);
    a->cur_pop = pw;
}

/* The pop-out window a widget is in, or NULL when it is in the main window. */
static ExPopWin *pop_of_widget(App *a, const ExWidget *w) {
    size_t i;
    while (w && w->parent) w = w->parent;
    for (i = 0; w && i < a->npops; i++) if (ex_popwin_root(a->pops[i]) == w) return a->pops[i];
    return NULL;
}
static ExUi *ui_of(App *a, const ExWidget *w) { ExPopWin *pw = pop_of_widget(a, w); return pw ? ex_popwin_ui(pw) : a->main_ui; }

static void enter_tab_window(App *a, ExId tab) {
    const ExTab *t = ex_ws_tab(a->ws, tab);
    ExPopWin *pw = t ? find_pop(a, ex_ws_window_of_node(a->ws, t->group)) : NULL;
    if (pw) enter_pop(a, pw);
}

static void create_pop(App *a, ExId window) {
    ExPanesHooks h;
    ExPopWin *pw;
    int x = 0, y = 0, w = 0, hh = 0, max = 0;
    size_t i;
    if (a->npops >= 8) return;
    ex_ws_window_get_rect(a->ws, window, &x, &y, &w, &hh, &max);
    if (w <= 0 || hh <= 0) {                                                       /* a window that has no size of its own: the size of a new window (7f) */
        w = ex_window_size_clamp(ex_settings_get_int(a->settings, "appearance", "window_width"));
        hh = ex_window_size_clamp(ex_settings_get_int(a->settings, "appearance", "window_height"));
        x = y = INT_MIN;
    }
    fill_pane_hooks(a, &h, 1);
    pw = ex_popwin_create(a->ws, window, &a->theme, ex_ui_base_size(a->main_ui), (float)ex_zoom_clamp(ex_settings_get_int(a->settings, "interface", "zoom_percent")) / 100.0f, &h, a, x, y, w, hh, "Exhaust");
    if (!pw) return;
    a->pops[a->npops++] = pw;
    ex_panes_link(a->main_panes, ex_popwin_panes(pw), 1);
    ex_panes_link(ex_popwin_panes(pw), a->main_panes, 1);
    for (i = 0; i + 1 < a->npops; i++) { ex_panes_link(ex_popwin_panes(a->pops[i]), ex_popwin_panes(pw), 1); ex_panes_link(ex_popwin_panes(pw), ex_popwin_panes(a->pops[i]), 1); }
    if (a->fontdb) ex_popwin_set_font_db(pw, a->fontdb);
    ex_text_input_start(ex_renderer_window(ex_popwin_renderer(pw)));
}

static void destroy_pop(App *a, ExPopWin *pw) {
    size_t i, k;
    if (a->cur_pop == pw) leave_pop(a);
    for (i = 0; i < a->npops; i++) if (a->pops[i] == pw) break;
    if (i == a->npops) return;
    for (k = 0; k < a->npops; k++) if (k != i) ex_panes_link(ex_popwin_panes(a->pops[k]), ex_popwin_panes(pw), 0);
    ex_panes_link(a->main_panes, ex_popwin_panes(pw), 0);
    if (a->editor && pop_of_widget(a, a->editor) == pw) a->editor = NULL;
    if (a->title && pop_of_widget(a, a->title) == pw) a->title = NULL;
    a->pops[i] = a->pops[--a->npops];
    ex_popwin_free(pw);
}

/* Once a pass: a window for every pop-out of the workspace, none for one that is gone, and the place and size of each in the workspace (and so the session). */
static void sync_windows(App *a) {
    size_t i;
    for (i = 0; i < ex_ws_window_count(a->ws); i++) {
        ExId w = ex_ws_window_at(a->ws, i);
        if (w != ex_ws_main_window(a->ws) && !find_pop(a, w)) create_pop(a, w);
    }
    for (i = 0; i < a->npops;) {
        ExPopWin *pw = a->pops[i];
        int found = 0, x, y, w, h, ox, oy, ow, oh, om;
        size_t k;
        for (k = 0; k < ex_ws_window_count(a->ws); k++) if (ex_ws_window_at(a->ws, k) == ex_popwin_window(pw)) found = 1;
        if (!found) { destroy_pop(a, pw); continue; }                              /* i now holds the one that took its place */
        ex_popwin_get_rect(pw, &x, &y, &w, &h);
        ex_ws_window_get_rect(a->ws, ex_popwin_window(pw), &ox, &oy, &ow, &oh, &om);
        if (x != ox || y != oy || w != ow || h != oh) ex_ws_window_set_rect(a->ws, ex_popwin_window(pw), x, y, w, h, 0);
        i++;
    }
}

/* One frame of every pop-out window. */
static void draw_pops(App *a) {
    size_t i;
    for (i = 0; i < a->npops; i++) {
        enter_pop(a, a->pops[i]);
        ex_panes_set_header_visible(a->panes, ex_settings_get_bool(a->settings, "interface", "show_tab_title_bar"));
        ex_popwin_frame(a->pops[i], a->blink_on);
        leave_pop(a);
    }
}

static void script_shot(App *a, const char *file) {
    int sw, sh;
    uint8_t *px;
    FILE *f;
    ExPopWin *pw = g_script.window > 0 && (size_t)g_script.window <= a->npops ? a->pops[g_script.window - 1] : NULL;
    ExRenderer *r;
    if (g_script.window > 0 && !pw) return;                                   /* a window that is not there: no picture (not the main window's) */
    if (pw) { enter_pop(a, pw); ex_popwin_frame(pw, a->blink_on); r = ex_popwin_renderer(pw); }
    else { relayout_and_draw(a); r = a->r; }
    px = ex_renderer_read_rgb(r, &sw, &sh);
    f = px ? fopen(file, "wb") : NULL;
    if (f) { fprintf(f, "P6\n%d %d\n255\n", sw, sh); fwrite(px, 1, (size_t)sw * (size_t)sh * 3, f); fclose(f); }
    free(px);
    leave_pop(a);
}

/* The screen reader interface of every window (7h): the actions the operating system asked for are done, and while a screen reader is attached the tree
 * is brought up to date from the widgets a few times a second. */
static void a11y_window(ExA11yAdapter *ad, ExUi *ui, ExWidget *root, SDL_Window *win) {
    ExA11yActionReq req;
    if (!ad) return;
    while (ex_a11y_adapter_poll(ad, &req)) ex_ui_a11y_action(ui, root, req.id, req.action);
    if (ex_a11y_adapter_wants_update(ad, (uint64_t)SDL_GetTicks())) {
        int x = 0, y = 0, w = 0, h = 0;
        ex_ui_a11y_sync(ui, root, ex_a11y_adapter_tree(ad));
        SDL_GetWindowPosition(win, &x, &y);
        SDL_GetWindowSize(win, &w, &h);
        ex_a11y_adapter_set_window_bounds(ad, x, y, w, h);
        {   /* a headless window (the dummy video driver) has no window manager to focus it: it counts as focused */
            const char *drv = SDL_GetCurrentVideoDriver();
            int f = (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0 || (drv && !strcmp(drv, "dummy"));
            ex_a11y_adapter_set_focus(ad, f);
        }
        ex_a11y_adapter_commit(ad);
    }
}

static void a11y_pass(App *a) {
    size_t i;
    a11y_window(a->a11y, a->main_ui, a->main_root, ex_renderer_window(a->main_r));
    for (i = 0; i < a->npops; i++) a11y_window(ex_popwin_a11y(a->pops[i]), ex_popwin_ui(a->pops[i]), ex_popwin_root(a->pops[i]), ex_renderer_window(ex_popwin_renderer(a->pops[i])));
}

/* Notes events: saves, failures, outside changes. */
static void handle_note_events(App *a) {
    ExNoteEvent ev;
    while (ex_notes_poll_event(a->notes, &ev)) {
        switch (ev.kind) {
        case EX_NOTE_EVENT_SAVED:
            if (a->vault && !ex_vault_find(a->vault, ev.rel)) {                      /* the first write of a new note: the explorer shows it now */
                ex_vault_rescan(a->vault);
                if (a->tree) ex_tree_select_path(a->tree, ev.rel);
            }
            if (a->panels) ex_panels_file_changed(a->panels, ev.rel);
            ex_canvascache_invalidate(a->ccache, ev.rel);                             /* a canvas card that shows this note asks for its text again */
            if (ev.note == a->note && a->banner_kind == BANNER_ERROR) show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
            break;
        case EX_NOTE_EVENT_SAVE_FAILED:
            if (ev.note == a->note) show_banner(a, BANNER_ERROR, ev.message, NULL, NULL, NULL); /* stays until a save works */
            break;
        case EX_NOTE_EVENT_RELOADED:
            if (a->panels) ex_panels_file_changed(a->panels, ev.rel);
            ex_canvascache_invalidate(a->ccache, ev.rel);
            if (ev.note == a->note) set_status(a, "This note was changed outside Exhaust; the new text is shown (Undo goes back).");
            break;
        case EX_NOTE_EVENT_CONFLICT:
            if (ev.note == a->note) show_conflict_banner(a);
            break;
        case EX_NOTE_EVENT_TREE_CHANGED:
            ex_vault_rescan(a->vault);
            if (a->panels) ex_panels_tree_changed(a->panels);
            ex_imagecache_invalidate(a->icache, "");                                 /* a picture may have changed: every open one is read again */
            break;
        }
    }
}


/* ---- what the panels ask of the application ---- */

static const char *host_active_path(void *u) {
    App *a = u;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    ExNote *n = t ? note_for_tab(a, t) : NULL;
    return n ? ex_note_path(n) : NULL;
}

static char *host_active_text(void *u, size_t *len) {
    App *a = u;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    ExNote *n = t ? note_for_tab(a, t) : NULL;
    return n ? ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), len) : NULL;
}

static ExNote *open_note_object(App *a, const char *path) {
    size_t i;
    for (i = 0; i < a->ntabnotes; i++) if (!strcmp(ex_note_path(a->tabnotes[i].note), path)) return a->tabnotes[i].note;
    return NULL;
}

static char *host_read_text(void *u, const char *path, size_t *len) {
    App *a = u;
    ExNote *n = open_note_object(a, path);
    if (n) return ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), len);
    return ex_index_read_note(ex_vault_path(a->vault), path, len);
}

static int host_replace_text(void *u, const char *path, const char *text, size_t len) {
    App *a = u;
    ExNote *n = open_note_object(a, path);
    ExDoc *d;
    size_t old_len = 0, pre = 0, suf = 0;
    char *old;
    ExChange c;
    if (!n) return -1;
    d = ex_note_doc(n);
    old = ex_buffer_text(ex_doc_buffer(d), &old_len);
    if (!old) return -1;
    while (pre < old_len && pre < len && old[pre] == text[pre]) pre++;
    while (suf < old_len - pre && suf < len - pre && old[old_len - 1 - suf] == text[len - 1 - suf]) suf++;
    c.offset = pre; c.remove_len = old_len - pre - suf; c.text = text + pre; c.text_len = len - pre - suf;
    free(old);
    if (c.remove_len == 0 && c.text_len == 0) return 0;
    ex_doc_seal(d);
    return ex_doc_apply(d, &c, 1, 0, NULL, 0) == 0 ? 0 : -1;       /* one undo step */
}

static void host_select_range(void *u, size_t from, size_t to) {
    App *a = u;
    if (a->editor && a->editor->editor) { ex_editor_select(a->editor->editor, a->r, from, to); ex_ui_set_focus(ui_of(a, a->editor), a->editor); }
}

static unsigned long host_active_version(void *u) {
    App *a = u;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    ExNote *n = t ? note_for_tab(a, t) : NULL;
    return n ? ex_doc_version(ex_note_doc(n)) : 0;
}

static size_t host_caret_offset(void *u) {
    App *a = u;
    ExDoc *d;
    if (!a->editor || !a->editor->editor) return (size_t)-1;
    d = ex_editor_doc(a->editor->editor);
    return ex_doc_sel(d, ex_doc_primary(d)).head;
}

static void host_query_results(void *u, const char *path, const char *expanded) {
    App *a = u;
    size_t i;
    for (i = 0; i < ex_ws_tab_count(a->ws); i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        ExWidget *w;
        if (!t || strcmp(t->path, path) || !is_reading(t->mode)) continue;
        w = ex_panes_content(a->panes, ex_ws_tab_at(a->ws, i));
        if (w && w->type == EX_WIDGET_READING) ex_reading_set_text(w, expanded, strlen(expanded));
    }
}

static void host_message(void *u, const char *text) { set_status(u, text); }
static void host_flush(void *u) { App *a = u; ExWidget *cv = a->panes ? active_canvas(a) : NULL; if (cv) ex_canvasview_commit(cv); { ExWidget *bv = active_base(a); if (bv) ex_baseview_commit(bv); } if (a->notes) ex_notes_flush(a->notes); }

static void host_open_note(void *u, const char *path, size_t offset, int new_tab) {
    App *a = u;
    ExPathInfo info;
    char abs[1400];
    (void)new_tab;
    if (!a->vault) return;
    snprintf(abs, sizeof abs, "%s/%s", ex_vault_path(a->vault), path);
    if (ex_platform_path_info(abs, &info) != 0 && !open_note_object(a, path)) {            /* a note that does not exist yet: made now, written when typed in */
        char parent[600] = "", name[300];
        const char *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
        size_t n = strlen(base);
        ExNote *nn;
        if (slash) snprintf(parent, sizeof parent, "%.*s", (int)(slash - path), path);
        snprintf(name, sizeof name, "%s", base);
        n = strlen(name);
        if (n > 3 && !strcmp(name + n - 3, ".md")) name[n - 3] = 0;
        nn = a->notes ? ex_notes_new(a->notes, parent, name) : NULL;
        if (nn) open_path(a, 0, ex_note_path(nn));
        return;
    }
    if (ex_file_kind(path) != EX_KIND_MARKDOWN && ex_file_kind(path) != EX_KIND_BASE && ex_file_kind(path) != EX_KIND_CANVAS && ex_file_kind(path) != EX_KIND_IMAGE && ex_file_kind(path) != EX_KIND_AUDIO && ex_file_kind(path) != EX_KIND_VIDEO && ex_file_kind(path) != EX_KIND_PDF) { on_open_file(NULL, path, a); return; }
    open_path(a, 0, path);
    free(a->sel_path);
    a->sel_path = offset != (size_t)-1 ? dup_text(path) : NULL;
    a->sel_offset = offset;
}

/* Moves or renames a note or folder. Tabs that show it are closed first (the note is saved where it was), then opened again
 * under the new path. */
static int host_move_file(void *u, const char *from, const char *to) {
    App *a = u;
    char *out = NULL;
    ExVaultResult rc = EX_VAULT_OK;
    size_t i, nt = ex_ws_tab_count(a->ws), fl = strlen(from), k = 0;
    struct { ExId group; char path[512], mode[16]; int active; } moved[64];
    char to_parent[512], from_parent[512], to_name[300];
    const char *ts = strrchr(to, '/'), *fs = strrchr(from, '/');
    snprintf(to_parent, sizeof to_parent, "%.*s", ts ? (int)(ts - to) : 0, to);
    snprintf(from_parent, sizeof from_parent, "%.*s", fs ? (int)(fs - from) : 0, from);
    snprintf(to_name, sizeof to_name, "%s", ts ? ts + 1 : to);
    for (i = 0; i < nt && k < 64; i++) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i));
        if (!t || (strcmp(t->path, from) && !(!strncmp(t->path, from, fl) && t->path[fl] == '/'))) continue;
        moved[k].group = ex_ws_node_parent(a->ws, ex_ws_tab_at(a->ws, i)) ? ex_ws_node_parent(a->ws, ex_ws_tab_at(a->ws, i)) : 0;
        snprintf(moved[k].path, sizeof moved[k].path, "%s%s", to, t->path + fl);
        snprintf(moved[k].mode, sizeof moved[k].mode, "%s", t->mode);
        moved[k].active = ex_ws_group_active(a->ws, moved[k].group) == ex_ws_tab_at(a->ws, i);
        k++;
    }
    for (i = nt; i > 0; i--) {
        const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i - 1));
        if (t && (!strcmp(t->path, from) || (!strncmp(t->path, from, fl) && t->path[fl] == '/'))) ex_ws_close_tab(a->ws, ex_ws_tab_at(a->ws, i - 1));
    }
    ex_panes_update(a->panes);                                     /* the notes of those tabs are saved and closed now */
    if (a->notes) ex_notes_flush(a->notes);
    if (to_parent[0]) {
        char abs[1200];
        snprintf(abs, sizeof abs, "%s/%s", ex_vault_path(a->vault), to_parent);
        ex_platform_make_dirs(abs);
        ex_vault_rescan(a->vault);
    }
    if (strcmp(from_parent, to_parent)) {
        const char *base = fs ? fs + 1 : from;
        if (strcmp(base, to_name)) rc = ex_vault_rename(a->vault, from, to_name, &out);
        if (rc == EX_VAULT_OK) {
            char *src = out ? out : dup_text(from);
            rc = ex_vault_move(a->vault, src, to_parent, NULL);
            if (src != out) free(src);
        }
    } else rc = ex_vault_rename(a->vault, from, to_name, NULL);
    free(out);
    ex_vault_rescan(a->vault);
    for (i = 0; i < k; i++) {
        ExId t = ex_ws_new_tab(a->ws, moved[i].group, rc == EX_VAULT_OK ? moved[i].path : "", "note");
        if (t && moved[i].mode[0]) ex_ws_set_tab_mode(a->ws, t, moved[i].mode);
        if (t && moved[i].active) ex_ws_activate(a->ws, t);
    }
    if (rc != EX_VAULT_OK && k) {                                  /* it did not move: the tabs come back where they were */
        for (i = 0; i < k; i++) { size_t pl = strlen(moved[i].path), tl = strlen(to); if (pl >= tl) { char back[600]; snprintf(back, sizeof back, "%s%s", from, moved[i].path + tl); open_path(a, moved[i].group, back); } }
    }
    return rc == EX_VAULT_OK ? 0 : -1;
}

/* The explorer menu's file operations (sub-slice 4d). */
static int host_file_op(void *u, int what, const char *path, const char *arg) {
    App *a = u;
    char *abs;
    int rc = -1;
    if (!a->vault) return -1;
    switch (what) {
    case EX_FILEOP_DELETE: {
        size_t i, fl = strlen(path);
        if (!path[0]) return -1;
        for (i = ex_ws_tab_count(a->ws); i > 0; i--) {              /* the tabs that show it close; their notes are saved first */
            const ExTab *t = ex_ws_tab(a->ws, ex_ws_tab_at(a->ws, i - 1));
            if (t && (!strcmp(t->path, path) || (!strncmp(t->path, path, fl) && t->path[fl] == '/'))) ex_ws_close_tab(a->ws, ex_ws_tab_at(a->ws, i - 1));
        }
        ex_panes_update(a->panes);
        if (a->notes) ex_notes_flush(a->notes);
        rc = ex_vault_delete(a->vault, path) == EX_VAULT_OK ? 0 : -1;
        ex_vault_rescan(a->vault);
        if (rc == 0 && a->panels) ex_panels_file_removed(a->panels, path);
        break;
    }
    case EX_FILEOP_NEW_FOLDER: {
        char *out = NULL;
        rc = arg && ex_vault_create_folder(a->vault, path, arg, &out) == EX_VAULT_OK ? 0 : -1;
        if (rc == 0 && out) ex_tree_select_path(a->tree, out);
        free(out);
        break;
    }
    case EX_FILEOP_COPY_REL: rc = ex_vault_copy_path(a->vault, path, 0); break;
    case EX_FILEOP_COPY_ABS: rc = ex_vault_copy_path(a->vault, path, 1); break;
    case EX_FILEOP_OPEN_DEFAULT: rc = ex_vault_open_default(a->vault, path); break;
    case EX_FILEOP_COPY_FILE: {                                        /* "Make a copy": Name 1.md, Name 2.md ... next to the note */
        char *src = ex_vault_abs_path(a->vault, path), dst[1500];
        size_t len = 0, k;
        const char *dot = strrchr(path, '.');
        char *bytes = src ? ex_platform_read_file(src, &len) : NULL;
        if (a->notes) ex_notes_flush(a->notes);
        for (k = 1; bytes && k < 1000; k++) {
            ExPathInfo info;
            snprintf(dst, sizeof dst, "%s/%.*s %zu%s", ex_vault_path(a->vault), (int)(dot ? (size_t)(dot - path) : strlen(path)), path, k, dot ? dot : "");
            if (ex_platform_path_info(dst, &info) == 0) continue;
            rc = ex_platform_write_file_atomic(dst, bytes, len) == 0 ? 0 : -1;
            break;
        }
        free(bytes); free(src);
        ex_vault_rescan(a->vault);
        break;
    }
    case EX_FILEOP_SHOW: abs = ex_vault_abs_path(a->vault, path); if (abs) { rc = ex_platform_show_in_file_manager(abs); free(abs); } break;
    }
    return rc;
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
    ExA11yAdapter *a11y;

    settings = ex_settings_open(NULL, cfg);
    free(cfg);
    r = ex_renderer_create_hidden("Exhaust: vaults", 960, 600);
    if (!r) { fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error()); ex_settings_close(settings); return 1; }
    a11y = ex_a11y_adapter_create(ex_renderer_window(r));                  /* the screen reader interface of this window too (7h) */
    ex_renderer_show(r);
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
        a11y_window(a11y, ui, ex_vaultscreen_root(screen), ex_renderer_window(r));
        ex_renderer_size(r, &w, &h);
        ex_ui_layout(ui, ex_vaultscreen_root(screen), (float)w, (float)h);
        ex_renderer_clear(r, ex_theme_color(&theme, EX_COLOR_BACKGROUND));
        ex_ui_draw(ui, ex_vaultscreen_root(screen), 1);
        ex_renderer_present(r);
    }
    ex_vaultscreen_free(screen);
    ex_a11y_adapter_free(a11y);
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
    ex_platform_match_cursor_size();               /* before SDL starts: the pointer is as large as the desktop's (ISSUES 21) */
    SDL_Event e;
    ExQueue *results;
    ExWorker *worker;
    ExMessage job;
    ExWidget *row, *col, *ribbon, *status, *left_content;
    ExPanesHooks hooks;
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
#ifdef __APPLE__
        app.hotkeys = ex_hotkeys_open(vcfg ? vcfg : cfg, 1);                    /* hotkeys.ini next to settings.ini: the vault's, or the program's with no vault */
#else
        app.hotkeys = ex_hotkeys_open(vcfg ? vcfg : cfg, 0);
#endif
        app.themes = ex_themes_create(vcfg ? vcfg : NULL, cfg);                  /* the worker lists and reads the theme files (7d) */
        free(vcfg);
        free(cfg);
        if (!app.settings) { fprintf(stderr, "exhaust: cannot read the settings\n"); return 1; }
    }
    app.r = ex_renderer_create_hidden("Exhaust", ex_window_size_clamp(ex_settings_get_int(app.settings, "appearance", "window_width")), ex_window_size_clamp(ex_settings_get_int(app.settings, "appearance", "window_height")));
    if (!app.r) {
        fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error());
        return 1;
    }
    app.a11y = ex_a11y_adapter_create(ex_renderer_window(app.r));          /* the screen reader interface: attached before the window is shown */
    ex_renderer_show(app.r);
    {
        const char *scheme = ex_settings_get(app.settings, "appearance", "color_scheme");
        char *global_scheme = NULL;
        int dark;
        if (!ex_settings_is_set(app.settings, "appearance", "color_scheme")) {      /* a vault that never chose follows the program's setting, as the vault manager does */
            char *cfg = ex_platform_config_dir();
            ExSettings *global = ex_settings_open(NULL, cfg);
            if (global) { const char *g = ex_settings_get(global, "appearance", "color_scheme"); global_scheme = g ? dup_text(g) : NULL; ex_settings_close(global); }
            free(cfg);
            if (global_scheme) scheme = global_scheme;
        }
        dark = strcmp(scheme, "light") != 0;
        if (strcmp(scheme, "system") == 0) dark = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
        free(global_scheme);
        app.sys_hc = -1;
        if (ex_theme_init_builtin(&app.theme, high_contrast_now(&app) ? (dark ? EX_THEME_HC_DARK : EX_THEME_HC_LIGHT) : (dark ? EX_THEME_DARK : EX_THEME_LIGHT)) != 0) return 1;
    }
    ex_renderer_set_display_scale(app.r, ex_renderer_detect_display_scale(app.r));
    {
        long fs = ex_settings_get_int(app.settings, "appearance", "font_size");
        long zp = ex_settings_get_int(app.settings, "interface", "zoom_percent");
        app.ui = ex_ui_create(app.r, &app.theme, (float)(fs < 8 ? 8 : fs > 48 ? 48 : fs));
        ex_renderer_set_zoom(app.r, (float)ex_zoom_clamp(zp) / 100.0f);
    }
    app.demo_doc = ex_doc_create(intro, strlen(intro));
    app.ws = ex_ws_create();
    app.ccache = ex_canvascache_create();
    app.icache = ex_imagecache_create(0);
    app.player = ex_player_create();
    ex_imagecache_set_loader(app.icache, image_want, &app);

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
        {   /* default_file (7a): `empty` and `note` start from a clean layout instead of the last session's tabs (the session is still saved when Exhaust closes) */
            const char *df = ex_settings_get(app.settings, "files", "default_file");
            int fresh = df && (!strcmp(df, "empty") || !strcmp(df, "note"));
            loaded = app.session && !fresh ? ex_session_load(app.session, app.ws) : 0;
        }
        (void)loaded;
    } else {
        ex_ws_new_tab(app.ws, 0, "", "welcome");   /* no vault: one tab with the welcome text */
    }

    register_features_and_commands(&app);
    apply_history(&app);                                                    /* file recovery: on or off, the interval and the days (7e) */
    if (app.vault) {                                                        /* saved workspace layouts: read now, written by a worker */
        char *lp = malloc(strlen(ex_vault_path(app.vault)) + 32);
        if (lp) { sprintf(lp, "%s/.exhaust/workspaces.ini", ex_vault_path(app.vault)); app.layouts = ex_layouts_open(lp); free(lp); }
    }
    app.spell = ex_spell_create_en_us();                                    /* English (US), built in; the words you added come from the vault */
    load_custom_words(&app);
    fill_pane_hooks(&app, &hooks, 0);
    app.panes = ex_panes_create(app.ui, app.ws, &hooks, &app);
    app.main_r = app.r; app.main_ui = app.ui; app.main_panes = app.panes;

    /* window: ribbon | left sidebar | banner above the panes | right sidebar, and a status line */
    app.root = ex_widget_box(app.ui, 1, "Exhaust");
    app.root->role = EX_ROLE_WINDOW;
    app.root->width = ex_size_grow(); app.root->height = ex_size_grow();
    app.root->bg_token = EX_COLOR_BACKGROUND;
    app.main_root = app.root;
    row = ex_widget_box(app.ui, 0, "workspace");
    row->width = ex_size_grow(); row->height = ex_size_grow();
    {   /* the ribbon: the actions at the top, the view switches at the bottom (src/ui/ribbon.c); the buttons are commands, and the settings choose which show (7f) */
        ExRibbonHooks rh;
        rh.user = &app; rh.run = ribbon_run;
        app.ribbon = ex_ribbon_create(app.ui, &rh);
        ribbon = ex_ribbon_widget(app.ribbon);
        app.rb_graph = ex_ribbon_button(app.ribbon, "graph-view.open-graph-view");
        app.rb_mode = ex_ribbon_button(app.ribbon, "app.toggle-live-preview-source-mode");
        app.rb_left = ex_ribbon_button(app.ribbon, "app.toggle-left-sidebar");
        app.rb_right = ex_ribbon_button(app.ribbon, "app.toggle-right-sidebar");
    }
    ex_widget_add(row, ribbon);
    {   /* a thin line between the ribbon and the file explorer */
        ExWidget *line = ex_widget_box(app.ui, 1, "Ribbon separator");
        line->width = ex_size_px(app.ui, 1); line->height = ex_size_grow(); line->bg_token = EX_COLOR_BORDER;
        ex_widget_add(row, line);
        app.ribbon_line = line;
    }

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
    on_graph_feature("graph", ex_features_enabled(app.features, "graph"), &app);
    if (app.vault) {
        ExPanelsInit pi;
        memset(&pi, 0, sizeof pi);
        pi.ui = app.ui; pi.vault_root = ex_vault_path(app.vault); pi.vault_id = ex_vault_id(app.vault);
        pi.settings = app.settings; pi.features = app.features; pi.commands = app.commands; pi.ws = app.ws; pi.files = left_content;
        pi.host.user = &app;
        pi.host.open_note = host_open_note; pi.host.active_path = host_active_path; pi.host.active_text = host_active_text; pi.host.read_text = host_read_text;
        pi.host.replace_text = host_replace_text; pi.host.select_range = host_select_range; pi.host.move_file = host_move_file; pi.host.file_op = host_file_op; pi.host.message = host_message;
        pi.host.history_version = host_history_version; pi.host.file_text = host_file_text; pi.host.image_result = host_image_result; pi.host.audio_result = host_audio_result; pi.host.media_result = host_media_result; pi.host.pdf_result = host_pdf_result; pi.host.base_result = host_base_result; pi.host.graph_data = host_graph_data; pi.host.local_graph_data = host_local_graph_data; pi.host.flush_notes = host_flush; pi.host.query_results = host_query_results; pi.host.active_version = host_active_version; pi.host.caret_offset = host_caret_offset;
        pi.wake = wake_ui;
        app.panels = ex_panels_create(&pi);
        if (app.panels) {
            ex_panels_register(app.panels);
            ex_graphopts_load(&app.gopts, graph_get_key, &app);                  /* the graph's settings from the session */
            ex_panels_graph_options(app.panels, &app.gopts);
            left_content = ex_panels_left(app.panels);
            left_content->width = ex_size_grow(); left_content->height = ex_size_grow(); left_content->bg_token = EX_COLOR_BACKGROUND_ALT;
        }
    }
    app.left_sb = ex_sidebar_create(app.ui, app.ws, EX_SIDE_LEFT, left_content, "Left sidebar");
    ex_widget_add(row, ex_sidebar_edge_create(app.left_sb));       /* the handle of a hidden left sidebar */
    ex_widget_add(row, app.left_sb);
    ex_widget_add(row, ex_sidebar_divider_create(app.left_sb));

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
    app.banner_text->wrap = 1;                                  /* a long notice goes onto more lines instead of under the buttons */
    app.banner_a = ex_widget_button(app.ui, "", banner_a_click, &app);
    app.banner_b = ex_widget_button(app.ui, "", banner_b_click, &app);
    app.banner_c = ex_widget_button(app.ui, "", banner_c_click, &app);
    ex_widget_add(app.banner, app.banner_text);
    ex_widget_add(app.banner, app.banner_a);
    ex_widget_add(app.banner, app.banner_b);
    ex_widget_add(app.banner, app.banner_c);
    ex_widget_add(col, app.banner);
    app.findbar = ex_findbar_create(app.ui);
    if (app.findbar) ex_widget_add(col, ex_findbar_widget(app.findbar));
    ex_widget_add(col, ex_panes_root(app.panes));
    if (app.panels) ex_widget_add(col, ex_panels_bottom(app.panels));
    ex_widget_add(row, col);

    {
        ExWidget *right_content = app.panels ? ex_panels_right(app.panels) : info_label(&app, "Open a vault to see backlinks, the outline and tags.");
        if (right_content) { right_content->bg_token = EX_COLOR_BACKGROUND_ALT; right_content->width = ex_size_grow(); right_content->height = ex_size_grow(); }
        {                                                                          /* the panels, and under them the group of tabs of the sidebar (5e) */
            ExWidget *stack = ex_widget_box(app.ui, 1, "Right sidebar content"), *sr = ex_panes_side_root(app.panes);
            if (stack && right_content && sr) {
                stack->width = ex_size_grow(); stack->height = ex_size_grow(); stack->bg_token = EX_COLOR_BACKGROUND_ALT;
                ex_widget_add(stack, right_content);
                sr->hidden = 1;
                ex_widget_add(stack, sr);
                right_content = stack;
            }
        }
        app.right_sb = ex_sidebar_create(app.ui, app.ws, EX_SIDE_RIGHT, right_content, "Right sidebar");
        ex_widget_add(row, ex_sidebar_divider_create(app.right_sb));
        ex_widget_add(row, app.right_sb);
        ex_widget_add(row, ex_sidebar_edge_create(app.right_sb));  /* the handle of a hidden right sidebar, at the window's edge */
    }
    app.sbar = ex_statusbar_create(app.ui);                                 /* the status bar (src/ui/statusbar.c, 7f) */
    status = ex_statusbar_message_label(app.sbar);
    app.wordcount = ex_statusbar_words(app.sbar);
    ex_statusbar_message(app.sbar, app.vault ? "Typing, selecting, several cursors (Ctrl+Alt+Up or Down, Alt+click), undo and redo work. Ctrl+Plus and Ctrl+Minus zoom."
                                             : "No vault is open. Start with:  exhaust FOLDER");
    ex_widget_add(app.root, row);
    ex_widget_add(app.root, ex_statusbar_widget(app.sbar));
    on_wordcount_feature("word_count", ex_features_enabled(app.features, "word_count"), &app);
    app.status = status;
    apply_ribbon(&app);

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
    script_start();
    if (getenv("EXHAUST_OPEN") && app.vault) open_path(&app, 0, getenv("EXHAUST_OPEN"));       /* a developer's aid: open this note at start */
    else if (app.vault && !strcmp(ex_settings_get(app.settings, "files", "default_file"), "note")) {   /* default_file = note (7a): the chosen note, or the empty tab if it is not there */
        const char *rel = ex_settings_get(app.settings, "files", "default_file_note");
        char full[2048];
        snprintf(full, sizeof full, "%s/%s", ex_vault_path(app.vault), rel ? rel : "");
        if (rel && rel[0] && ex_platform_file_exists(full)) open_path(&app, 0, rel);
    }
    sync_windows(&app);                                                                        /* the pop-out windows of the last session (7g) */
    relayout_and_draw(&app);
    draw_pops(&app);
    while (running) {
        ExTextEvent te;
        ExKey key;
        int mods;
        script_step(&app);
        int animating = (app.gm && ex_graphmodel_animating(app.gm)) || (app.gm_local && ex_graphmodel_animating(app.gm_local)) || ex_graphview_animations_running() > 0 || ex_imagecache_busy(app.icache) || (app.player && ex_player_state(app.player) == EX_PLAYER_PLAYING) || (app.mpv && ex_mpv_state(app.mpv) == EX_MPV_PLAYING);        /* the graph's layout is still moving the nodes: a frame every 16 ms */
        int timed_out = !SDL_WaitEventTimeout(&e, animating ? 16 : 100); /* wakes ten times a second for autosave and the watcher */
        static int idle_ticks;
        if (g_signal) running = 0;
        int woke = timed_out, redraw = !timed_out;     /* an idle wake-up draws only when the caret blinks: nothing else changed */
        /* Every event that is waiting is handled before the next frame: a frame waits for the screen (vsync), so one frame
         * for each of the hundreds of mouse-motion and resize events a second made the interface fall seconds behind. */
        for (;;) {
        ExPopWin *tw = timed_out ? NULL : pop_for_event(&app, &e);         /* the pop-out window this event is for (7g) */
        if (tw && e.type != SDL_EVENT_KEY_DOWN) enter_pop(&app, tw);         /* a shortcut works on the program, the rest of a key press goes to the window's own focus */
        if (e.type == g_wake_event && !timed_out) woke = 1;
        if (timed_out && animating) {
            redraw = 1;
        } else if (timed_out) {
            if (++idle_ticks % 5 == 0) { app.blink_on = !app.blink_on; redraw = 1; }   /* the caret blinks about every half second */
            if (theme_sync(&app)) redraw = 1;                                          /* a theme file changed, or the settings did (7d) */
        } else if (e.type == g_wake_event) {
            ExMessage m;
            while (ex_queue_pop(results, &m))
                if (m.type == MSG_FONTS_READY) {
                    size_t pi;
                    app.fontdb = (ExFontDb *)m.payload;
                    ex_renderer_set_font_db(app.main_r, app.fontdb);
                    for (pi = 0; pi < app.npops; pi++) ex_popwin_set_font_db(app.pops[pi], app.fontdb);      /* the pop-outs use the same database, which the main window owns */
                }
        } else if (e.type == SDL_EVENT_KEY_DOWN && app.cap_cb && capture_key(&app, &e.key)) {
            redraw = 1;
        } else if (e.type == SDL_EVENT_KEY_DOWN && !(app.panels && ex_panels_modal(app.panels)) && run_shortcut(&app, &e.key)) {
            app.blink_on = 1;
        } else if (e.type == SDL_EVENT_TEXT_INPUT && SDL_GetTicks() < app.swallow_text_until) {
            /* the text a captured key press made: not typed anywhere */
        } else if (ex_text_event_decode(&e, &te)) {
            app.blink_on = 1;
            if (te.kind == EX_TEXT_COMMIT) ex_ui_text_input(app.ui, te.text, strlen(te.text));
            else if (app.editor && app.editor->editor) ex_editor_set_composition(app.editor->editor, te.text);
        } else if (ex_key_from_sdl(&e, &key, &mods)) {
            app.blink_on = 1;
            if (tw) enter_pop(&app, tw);
            ex_ui_key(app.ui, key, mods);
        } else {
            app.blink_on = 1;
            switch (e.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (tw) { ExId wid = ex_popwin_window(tw); leave_pop(&app); ex_ws_close_window(app.ws, wid); }   /* the close button of a pop-out: its tabs go back to the main window */
                else running = 0;                                                                              /* of the main window: the program ends */
                break;
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
                ExWidget *input_root = app.panels && ex_panels_modal(app.panels) ? ex_panels_overlay(app.panels) : app.root;
                if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.button == SDL_BUTTON_RIGHT) {   /* the context menu of a file or folder */
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && app.panels && input_root == app.root) {
                        ExWidget *gw = ex_ui_widget_at(app.root, x, y), *cv = gw;
                        while (gw && gw->type != EX_WIDGET_GRAPH) gw = gw->parent;
                        while (cv && cv->type != EX_WIDGET_CANVAS && cv->type != EX_WIDGET_BASE) cv = cv->parent;
                        ex_panels_pointer(app.panels, app.root, -1, -1, 0);
                        if (gw) ex_graphview_context(gw, x, y); else if (cv && cv->type == EX_WIDGET_BASE) ex_baseview_context(cv, x, y); else if (cv) ex_canvasview_context(cv, x, y); else ex_panels_context_menu(app.panels, app.root, x, y);
                    }
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    Uint64 now = SDL_GetTicks();
                    clicks = now - last_click < 400 ? clicks + 1 : 1;
                    last_click = now;
                    if (app.panels) ex_panels_pointer(app.panels, app.root, -1, -1, 0);       /* a click puts the page preview away */
                    if (input_root != app.root && ex_ui_widget_at(input_root, x, y) == input_root) { ex_panels_close_dialog(app.panels); break; }   /* outside the dialog */
                    ex_ui_mouse(app.ui, input_root, EX_MOUSE_DOWN, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    ex_ui_mouse(app.ui, input_root, EX_MOUSE_UP, x, y, ex_mods_from_sdl(), clicks);
                } else if (e.motion.state & SDL_BUTTON_LMASK) {
                    ex_ui_mouse(app.ui, input_root, EX_MOUSE_DRAG, x, y, ex_mods_from_sdl(), clicks);
                } else {
                    ex_ui_pointer_moved(app.ui, input_root, x, y);                         /* a graph view under the pointer follows the node it is over */
                    if (app.panels) ex_panels_pointer(app.panels, app.root, x, y, (ex_mods_from_sdl() & EX_MOD_PRIMARY) != 0);
                }
                set_cursor_shape(ex_ui_cursor_at(app.ui, app.root, x, y));       /* the resize pointer over a sidebar's edge */
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(app.r));
                ExWidget *wheel_root = app.panels && ex_panels_modal(app.panels) ? ex_panels_overlay(app.panels) : app.root;
                ExWidget *under = ex_ui_widget_at(wheel_root, e.wheel.mouse_x * density, e.wheel.mouse_y * density);
                while (under && !under->scrollable && (under->no_wheel || (under->type != EX_WIDGET_EDITOR && under->type != EX_WIDGET_READING && under->type != EX_WIDGET_LIST && under->type != EX_WIDGET_TREE && under->type != EX_WIDGET_GRAPH && under->type != EX_WIDGET_CANVAS && under->type != EX_WIDGET_BASE && under->type != EX_WIDGET_IMAGE && under->type != EX_WIDGET_PDF))) under = under->parent;   /* the editor, reading view or list under the pointer, not only the active one */
                if (under && under->parent && (under->parent->type == EX_WIDGET_EDITOR || under->parent->type == EX_WIDGET_READING)) under = under->parent;   /* over the title: the note scrolls */
                if (!under && wheel_root == app.root && app.editor && pop_of_widget(&app, app.editor) == app.cur_pop) under = app.editor;
                if (under && under->scrollable) ex_ui_scroll(app.ui, under, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));                       /* a scroll box: the settings screens (7a) */
                else if (under && under->type == EX_WIDGET_GRAPH) ex_graphview_wheel(under, e.wheel.y, e.wheel.mouse_x * density, e.wheel.mouse_y * density);   /* zoom about the pointer */
                else if (under && under->type == EX_WIDGET_CANVAS) ex_canvasview_wheel(under, e.wheel.y, e.wheel.mouse_x * density, e.wheel.mouse_y * density);   /* pan, or zoom (the setting) */
                else if (under && under->type == EX_WIDGET_PDF) ex_pdfview_wheel(under, e.wheel.y, (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, e.wheel.mouse_x * density, e.wheel.mouse_y * density);
                else if (under && under->type == EX_WIDGET_IMAGE) ex_imageview_wheel(under, e.wheel.y, e.wheel.mouse_x * density, e.wheel.mouse_y * density);
                else if (under && under->type == EX_WIDGET_BASE) ex_baseview_wheel(under, e.wheel.x, e.wheel.y, e.wheel.mouse_x * density, e.wheel.mouse_y * density);
                else if (under && under->type == EX_WIDGET_TREE) ex_tree_scroll(app.ui, under, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                else if (under && under->type == EX_WIDGET_LIST) ex_list_scroll(under, -e.wheel.y * 3.0f * ex_list_row_height(under));
                else if (under && under->type == EX_WIDGET_READING) ex_reading_scroll(under, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                else if (under && under->editor) ex_editor_scroll(under->editor, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                break;
            }
            default: break;
            }
        }
        leave_pop(&app);
        if (timed_out || !running || !SDL_PollEvent(&e)) break;
        }
        if (app.notes) { ex_notes_tick(app.notes); handle_note_events(&app); }
        if (woke) {
            capture_tab_state(&app);
            if (app.session) ex_session_tick(app.session, app.ws, (int64_t)SDL_GetTicks());
        }
        sync_windows(&app);                                                                    /* the windows of the workspace (7g) */
        a11y_pass(&app);                                                                       /* the screen reader interface of each window (7h) */
        if (app.panels) ex_panels_update(app.panels);
        if (redraw || app.title_warn_until || (app.panels && ex_panels_overlay(app.panels)) || ex_ws_revision(app.ws) != g_drawn_revision) {
            g_drawn_revision = ex_ws_revision(app.ws);
            if (app.title_warn_until && SDL_GetTicks() >= app.title_warn_until) app.title_warn_until = 0;     /* drawn once more without the bubble */
            relayout_and_draw(&app);
            draw_pops(&app);                                                                   /* one frame of each pop-out window (7g) */
        }
        if (app.editor && app.editor->editor) {
            float cx, cy, cw, ch;
            ExPopWin *home = pop_of_widget(&app, app.editor);
            ExRenderer *er = home ? ex_popwin_renderer(home) : app.main_r;
            ex_editor_caret_rect(app.editor->editor, er, &cx, &cy, &cw, &ch);
            ex_text_input_set_area(ex_renderer_window(er), (int)cx, (int)cy, 1, (int)ch);
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
    while (app.npops) destroy_pop(&app, app.pops[app.npops - 1]);
    ex_a11y_adapter_free(app.a11y);                 /* the pop-out windows go first: they share the main window's font database */
    ex_graphmodel_free(app.gm);
    ex_graphmodel_free(app.gm_local);
    ex_canvascache_free(app.ccache);
    ex_imagecache_free(app.icache);
    ex_player_free(app.player);
    if (app.mpv) ex_mpv_close(app.mpv);
    free(app.graph_note);
    ex_slash_free(app.slash);
    ex_spell_free(app.spell);
    ex_panes_free(app.panes);                                          /* contents are detached and closed; their notes are closed too (content_closed still tells the find bar) */
    ex_findbar_free(app.findbar);
    app.findbar = NULL;
    if (app.panels) {
        ex_panels_free(app.panels);
        ex_widget_free(app.left_content);                              /* the explorer: the panels only held it */
        app.left_content = NULL;
    }
    free(app.sel_path);
    ex_notes_destroy(app.notes);
    ex_widget_free(app.root);
    free(app.tabnotes);
    ex_doc_free(app.demo_doc);
    ex_ws_free(app.ws);
    ex_commands_free(app.commands);
    ex_features_free(app.features);
    ex_themes_free(app.themes);
    ex_layouts_close(app.layouts);                 /* waits for the layout writes that are queued */
    free(app.hist_old);
    ex_hotkeys_close(app.hotkeys);                 /* waits for the shortcut writes that are queued */
    ex_settings_close(app.settings);               /* waits for the settings writes that are queued */
    ex_vault_close(app.vault);
    free(g_first_folder);
    ex_ui_free(app.ui);
    ex_theme_free(&app.theme);
    ex_renderer_destroy(app.r);
    return 0;
}
