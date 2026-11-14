#include <signal.h>
#include <ctype.h>
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
#include "exhaust_findbar.h"
#include "exhaust_slash.h"
#include "exhaust_spell.h"
#include "exhaust_ui.h"
#include "exhaust_index.h"
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

typedef enum Banner { BANNER_NONE, BANNER_RECOVERED, BANNER_CONFLICT, BANNER_ERROR } Banner;

typedef struct TabNote { ExId tab; ExNote *note; } TabNote;

typedef struct App {
    ExRenderer *r;
    ExUi *ui;
    ExTheme theme;
    ExWidget *root, *editor, *tree, *status;     /* editor: the editor of the active tab, or NULL */
    ExFindBar *findbar;
    ExDoc *title_doc;              /* the inline title: the name of the note, above its text */
    ExWidget *title;
    ExWidget *rb_switcher, *rb_graph, *rb_canvas, *rb_palette, *rb_mode, *rb_left, *rb_right, *rb_theme;   /* the ribbon */
    char title_for[512];           /* the path the title shows */
    int title_was_focused;
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
    ExPanels *panels;
    char *sel_path; size_t sel_offset;   /* put the caret here once the note is showing */
    float zoom_now;
    TabNote *tabnotes; size_t ntabnotes, captabnotes;
    ExId focus_tab;            /* the tab whose editor should get the focus when it exists */
    Banner banner_kind;
    int blink_on;
    float zoom;
} App;

static char *dup_text(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void sync_ui(App *a);

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
typedef struct Script { char *text; char *next; Uint64 until; int button_down; int active; } Script;
static Script g_script;

static void script_start(void) {
    const char *f = getenv("EXHAUST_SCRIPT");
    if (!f) return;
    g_script.text = ex_platform_read_file(f, NULL);
    g_script.next = g_script.text;
    g_script.active = g_script.text != NULL;
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
        SDL_PushEvent(&e);
    } else if ((!strncmp(line, "down ", 5) || !strncmp(line, "rdown ", 6)) && sscanf(line + (line[0] == 'r' ? 6 : 5), "%f %f", &x, &y) == 2) {
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = line[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y; e.button.clicks = 1;
        g_script.button_down = line[0] != 'r';
        SDL_PushEvent(&e);
    } else if (!strncmp(line, "up ", 3) && sscanf(line + 3, "%f %f", &x, &y) == 2) {
        e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
        g_script.button_down = 0;
        SDL_PushEvent(&e);
    } else if (!strncmp(line, "text ", 5)) {
        static char typed[256];
        snprintf(typed, sizeof typed, "%s", line + 5);
        e.type = SDL_EVENT_TEXT_INPUT; e.text.text = typed;
        SDL_PushEvent(&e);
    } else if (!strncmp(line, "key ", 4)) {
        SDL_Keycode k = !strcmp(line + 4, "enter") ? SDLK_RETURN : !strcmp(line + 4, "escape") ? SDLK_ESCAPE : !strcmp(line + 4, "down") ? SDLK_DOWN : !strcmp(line + 4, "tab") ? SDLK_TAB : SDLK_UNKNOWN;
        e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.down = true;
        SDL_PushEvent(&e);
    } else if (!strncmp(line, "shot ", 5)) {
        int sw, sh;
        uint8_t *px;
        FILE *f;
        relayout_and_draw(a);
        px = ex_renderer_read_rgb(a->r, &sw, &sh);
        f = px ? fopen(line + 5, "wb") : NULL;
        if (f) { fprintf(f, "P6\n%d %d\n255\n", sw, sh); fwrite(px, 1, (size_t)sw * (size_t)sh * 3, f); fclose(f); }
        free(px);
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
/* A note of the vault by its name (case-insensitive, without the extension); the link rules of 4a will replace this. */
static void open_path(App *a, ExId group, const char *rel);
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

static void on_reading_link(ExRSpanKind kind, const char *target, void *user) {
    App *a = user;
    char msg[400];
    if (kind == EX_RSK_LINK_EXTERNAL && target && (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0 || strncmp(target, "mailto:", 7) == 0)) {
        if (!SDL_OpenURL(target)) { snprintf(msg, sizeof msg, "Could not open %s", target); set_status(a, msg); }
    } else if (kind == EX_RSK_LINK_INTERNAL && target && a->vault) {
        follow_note_link(a, target, (ex_mods_from_sdl() & EX_MOD_PRIMARY) != 0);
    } else if (kind == EX_RSK_TAG && target) {
        if (a->panels) { char q[300]; snprintf(q, sizeof q, "tag:#%s", target[0] == '#' ? target + 1 : target); ex_panels_search_for(a->panels, q); }
    } else if (kind == EX_RSK_LINK_EXTERNAL) set_status(a, "Only web and e-mail links are opened.");
}

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
    n = note_for_tab(a, t->id);                                                 /* switching between reading and editing keeps the note open */
    if (!n) n = ex_notes_open(a->notes, t->path, &res);
    if (!n) {
        snprintf(msg, sizeof msg, res == EX_OPEN_INVALID_UTF8 ? "%s is not a UTF-8 text file, so Exhaust will not open or change it."
                                 : res == EX_OPEN_NOT_FOUND ? "%s does not exist any more. Close this tab, or restore the file." : "%s could not be opened.", t->path);
        return info_label(a, msg);
    }
    if (is_reading(t->mode)) {                                                  /* reading mode: the note as formatted pages */
        size_t tl = 0;
        char *txt = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), &tl);
        ExWidget *rv = txt ? ex_widget_reading(a->ui, txt, tl, t->path) : NULL;
        if (rv) {
            ex_reading_set_link_handler(rv, on_reading_link, a);
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
        {   /* the vault manager has no vault, so it reads the setting kept with the program: both windows must agree */
            char *cfg = ex_platform_config_dir();
            ExSettings *global = ex_settings_open(NULL, cfg);
            if (global) { ex_settings_set(global, "appearance", "color_scheme", to_dark ? "dark" : "light"); ex_settings_close(global); }
            free(cfg);
        }
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
static void capture_tab_state(App *a);
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
    w = make_content(a->panes, t, a);
    if (w) {
        if (a->findbar) ex_findbar_target_closed(a->findbar, ex_panes_content(a->panes, tab));
        ex_panes_replace_content(a->panes, tab, w);
        a->focus_tab = tab;
    }
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
    ex_features_register(a->features, "slash_commands", "Slash commands", NULL, a);
    ex_features_register(a->features, "word_count", "Word count", on_wordcount_feature, a);
    a->commands = ex_commands_create(a->features);
    ex_commands_bind(a->commands, "app.toggle-left-sidebar", cmd_toggle_left, NULL, a);
    ex_commands_bind(a->commands, "app.toggle-right-sidebar", cmd_toggle_right, NULL, a);
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
    if (last[0] == 'F' && last[1] >= '1' && last[1] <= '9' && strlen(last) <= 3) {            /* F1 to F12 */
        int n = atoi(last + 1);
        if (n < 1 || n > 12) return 0;
        return (int)k->key == (int)(SDLK_F1 + (n - 1)) && ((m & SDL_KMOD_CTRL) != 0) == ctrl && ((m & SDL_KMOD_SHIFT) != 0) == shift && ((m & SDL_KMOD_ALT) != 0) == alt;
    }
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

static void set_title_text(App *a, const char *text) {
    size_t n = strlen(text), have = ex_buffer_size(ex_doc_buffer(a->title_doc));
    ExChange c;
    c.offset = 0; c.remove_len = have; c.text = text; c.text_len = n;
    ex_doc_apply(a->title_doc, &c, 1, 0, NULL, 0);
    ex_doc_set_caret(a->title_doc, n);
}

static void title_revert(App *a) {
    char name[300];
    if (!a->title_for[0]) return;
    note_name_of(a->title_for, name, sizeof name);
    set_title_text(a, name);
}

/* Enter in the title: the note is renamed (a note that is not on disk yet is started again under the new name). */
static void title_commit(App *a, int keep_focus) {
    size_t len = 0;
    char *typed = ex_buffer_text(ex_doc_buffer(a->title_doc), &len), path[512];
    ExId tab = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    if (!typed || !a->title_for[0] || !a->panels) { free(typed); return; }
    snprintf(path, sizeof path, "%s", a->title_for);
    if (a->note && !ex_note_exists_on_disk(a->note) && ex_buffer_size(ex_doc_buffer(ex_note_doc(a->note))) == 0) {
        char to[700], name[300];
        const char *slash = strrchr(path, '/');
        size_t n = strlen(typed);
        while (n > 0 && typed[n - 1] == ' ') typed[--n] = 0;
        snprintf(name, sizeof name, "%s", typed);
        if (!name[0] || !ex_name_valid(name)) { set_status(a, "That is not a valid note name (no  # ^ [ ] | : and not empty)."); title_revert(a); free(typed); return; }
        snprintf(to, sizeof to, "%.*s%s%s.md", slash ? (int)(slash - path) : 0, path, slash ? "/" : "", name);
        if (strcmp(to, path)) {
            ExPathInfo info;
            char abs[1400];
            snprintf(abs, sizeof abs, "%s/%s", ex_vault_path(a->vault), to);
            if (ex_platform_path_info(abs, &info) == 0) { set_status(a, "A note with that name already exists."); title_revert(a); free(typed); return; }
            if (tab) ex_ws_close_tab(a->ws, tab);
            host_open_note(a, to, (size_t)-1, 0);
        }
    } else if (ex_panels_rename_to(a->panels, path, typed) != 0) title_revert(a);
    free(typed);
    if (keep_focus && a->editor) ex_ui_set_focus(a->ui, a->editor);
}

#define NAME_FORBIDDEN "\\/:#^[]|"

/* The title is a file name: the characters a name cannot have are not typed (a bubble says which), the rest of a paste goes in. */
static int title_text(ExWidget *w, const char *utf8, size_t len, void *user) {
    App *a = user;
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
    a->title_warn_until = SDL_GetTicks() + 3000;
    return 1;
}

static int title_key(ExWidget *w, ExKey key, int mods, void *user) {
    App *a = user;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { title_commit(a, 1); return 1; }
    if (key == EX_KEY_ESCAPE) { title_revert(a); if (a->editor) ex_ui_set_focus(a->ui, a->editor); return 1; }
    if (key == EX_KEY_DOWN) { if (a->editor) ex_ui_set_focus(a->ui, a->editor); return 1; }
    return 0;
}

/* Shows the title of the active note (not while it is being typed in). Leaving the title with a changed name renames the note, as Enter does. */
static void sync_title(App *a) {
    int focused = a->title && ex_ui_focus(a->ui) == a->title;
    if (a->title_was_focused && !focused && a->title_for[0]) {
        size_t len = 0;
        char *typed = ex_buffer_text(ex_doc_buffer(a->title_doc), &len), name[300];
        note_name_of(a->title_for, name, sizeof name);
        if (typed && strcmp(typed, name)) title_commit(a, 0);          /* clicked elsewhere or switched away: the rename is made */
        free(typed);
    }
    a->title_was_focused = focused;
    ExId t = ex_ws_group_active(a->ws, ex_ws_active_group(a->ws));
    const ExTab *tab = t ? ex_ws_tab(a->ws, t) : NULL;
    int show = a->title && tab && a->note && tab->path[0] && ex_file_kind(tab->path) == EX_KIND_MARKDOWN && ex_settings_get_bool(a->settings, "editor", "inline_title");
    if (!a->title) return;
    a->title->hidden = !show;
    if (!show) { a->title_for[0] = 0; return; }
    {   /* the field is as tall as the rows of the title (one frame behind the width: it is measured at the width of the last layout) */
        float h = ex_editor_content_height(a->title->editor, a->r), scale = ex_renderer_scale(a->r), one = (float)ex_px(a->r, 56);
        if (h < one) h = one;
        a->title->height = ex_size_px(a->ui, (h + (float)ex_px(a->r, 8)) / (scale > 0 ? scale : 1.0f));
    }
    if (strcmp(a->title_for, tab->path) || a->title_for[0] == 0) {
        snprintf(a->title_for, sizeof a->title_for, "%s", tab->path);
        title_revert(a);
        if (!ex_note_exists_on_disk(a->note)) ex_ui_set_focus(a->ui, a->title);   /* a new note: the first thing typed is its name */
    }
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
    sync_title(a);
    if (a->rb_left) a->rb_left->selected = ex_ws_sidebar_visible(a->ws, EX_SIDE_LEFT);          /* pressed while the sidebar shows */
    if (a->rb_right) a->rb_right->selected = ex_ws_sidebar_visible(a->ws, EX_SIDE_RIGHT);
    if (a->rb_mode) { const ExTab *rt = t ? ex_ws_tab(a->ws, t) : NULL; a->rb_mode->selected = rt && rt->mode && !strcmp(rt->mode, "source"); }
    if (a->sel_path && a->note && a->editor && !strcmp(ex_note_path(a->note), a->sel_path)) {      /* a link or a search result opened this note at a place */
        ex_editor_select(a->editor->editor, a->r, a->sel_offset, a->sel_offset);
        ex_ui_set_focus(a->ui, a->editor);
        free(a->sel_path); a->sel_path = NULL;
    } else if (a->sel_path && a->note && !a->editor && !strcmp(ex_note_path(a->note), a->sel_path)) { free(a->sel_path); a->sel_path = NULL; }
    update_wordcount(a);
    if (a->findbar && ex_findbar_is_open(a->findbar)) { if (a->editor) ex_findbar_set_target(a->findbar, a->editor); ex_findbar_update(a->findbar); }
    if (a->note != a->banner_note && !a->diff_doc) banner_for_note(a, a->note);
    if (a->focus_tab && a->focus_tab == t && a->editor) { if (!(a->title && !a->title->hidden && a->note && !ex_note_exists_on_disk(a->note))) ex_ui_set_focus(a->ui, a->editor); a->focus_tab = 0; }   /* a new note keeps the focus on its title */
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
            if (a->vault && !ex_vault_find(a->vault, ev.rel)) {                      /* the first write of a new note: the explorer shows it now */
                ex_vault_rescan(a->vault);
                if (a->tree) ex_tree_select_path(a->tree, ev.rel);
            }
            if (a->panels) ex_panels_file_changed(a->panels, ev.rel);
            if (ev.note == a->note && a->banner_kind == BANNER_ERROR) show_banner(a, BANNER_NONE, "", NULL, NULL, NULL);
            break;
        case EX_NOTE_EVENT_SAVE_FAILED:
            if (ev.note == a->note) show_banner(a, BANNER_ERROR, ev.message, NULL, NULL, NULL); /* stays until a save works */
            break;
        case EX_NOTE_EVENT_RELOADED:
            if (a->panels) ex_panels_file_changed(a->panels, ev.rel);
            if (ev.note == a->note) set_status(a, "This note was changed outside Exhaust; the new text is shown (Undo goes back).");
            break;
        case EX_NOTE_EVENT_CONFLICT:
            if (ev.note == a->note) show_conflict_banner(a);
            break;
        case EX_NOTE_EVENT_TREE_CHANGED:
            ex_vault_rescan(a->vault);
            if (a->panels) ex_panels_tree_changed(a->panels);
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
    if (a->editor && a->editor->editor) { ex_editor_select(a->editor->editor, a->r, from, to); ex_ui_set_focus(a->ui, a->editor); }
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
static void host_flush(void *u) { App *a = u; if (a->notes) ex_notes_flush(a->notes); }

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
    if (ex_file_kind(path) != EX_KIND_MARKDOWN && ex_file_kind(path) != EX_KIND_BASE && ex_file_kind(path) != EX_KIND_CANVAS) { on_open_file(NULL, path, a); return; }
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

static ExWidget *ribbon_icon(App *a, int icon, const char *name, ExClickFn fn, int disabled) {
    ExWidget *b = ex_widget_button(a->ui, name, fn, a);
    if (!b) return NULL;
    ex_widget_set_icon(b, icon);
    b->bg_token = -1; b->border_token = -1;
    if (disabled) { b->disabled = 1; b->focusable = 0; }
    return b;
}

static void on_ribbon_switcher(ExWidget *w, void *user) { (void)w; ex_commands_run(((App *)user)->commands, "quick-switcher.open-quick-switcher"); }
static void on_ribbon_palette(ExWidget *w, void *user) { (void)w; ex_commands_run(((App *)user)->commands, "command-palette.open-command-palette"); }


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
    app.spell = ex_spell_create_en_us();                                    /* English (US), built in; the words you added come from the vault */
    load_custom_words(&app);
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
    ribbon->pad_x = 0.5f; ribbon->pad_y = 1.0f; ribbon->gap = 0.5f; ribbon->align_x = 1;     /* the icons sit in the middle of the bar */
    /* the ribbon: the actions at the top, the view switches at the bottom; drawn icons, each with its name for screen readers. The
     * graph and canvas stay disabled until those views exist; the settings button comes with the settings screens (sub-slice 7a). */
    app.rb_switcher = ribbon_icon(&app, EX_ICON_SEARCH, "Quick switcher", on_ribbon_switcher, 0);
    app.rb_graph = ribbon_icon(&app, EX_ICON_GRAPH, "Graph view (not available yet)", NULL, 1);
    app.rb_canvas = ribbon_icon(&app, EX_ICON_CANVAS, "Canvas (not available yet)", NULL, 1);
    app.rb_palette = ribbon_icon(&app, EX_ICON_PALETTE, "Command palette", on_ribbon_palette, 0);
    ex_widget_add(ribbon, app.rb_switcher);
    ex_widget_add(ribbon, app.rb_graph);
    ex_widget_add(ribbon, app.rb_canvas);
    ex_widget_add(ribbon, app.rb_palette);
    {   ExWidget *gap = ex_widget_box(app.ui, 1, "ribbon space");
        gap->width = ex_size_grow(); gap->height = ex_size_grow();
        ex_widget_add(ribbon, gap);
    }
    app.rb_mode = ribbon_icon(&app, EX_ICON_MODE, "Live preview or source mode of this note", on_toggle_mode_button, 0);
    app.rb_left = ribbon_icon(&app, EX_ICON_SIDEBAR_LEFT, "Show or hide the left sidebar", on_toggle_left, 0);
    app.rb_right = ribbon_icon(&app, EX_ICON_SIDEBAR_RIGHT, "Show or hide the right sidebar", on_toggle_right, 0);
    app.rb_theme = ribbon_icon(&app, EX_ICON_THEME, "Switch between light and dark", on_toggle_theme, 0);
    ex_widget_add(ribbon, app.rb_mode);
    ex_widget_add(ribbon, app.rb_left);
    ex_widget_add(ribbon, app.rb_right);
    ex_widget_add(ribbon, app.rb_theme);
    ex_widget_add(row, ribbon);
    {   /* a thin line between the ribbon and the file explorer */
        ExWidget *line = ex_widget_box(app.ui, 1, "Ribbon separator");
        line->width = ex_size_px(app.ui, 1); line->height = ex_size_grow(); line->bg_token = EX_COLOR_BORDER;
        ex_widget_add(row, line);
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
    if (app.vault) {
        ExPanelsInit pi;
        memset(&pi, 0, sizeof pi);
        pi.ui = app.ui; pi.vault_root = ex_vault_path(app.vault); pi.vault_id = ex_vault_id(app.vault);
        pi.settings = app.settings; pi.features = app.features; pi.commands = app.commands; pi.ws = app.ws; pi.files = left_content;
        pi.host.user = &app;
        pi.host.open_note = host_open_note; pi.host.active_path = host_active_path; pi.host.active_text = host_active_text; pi.host.read_text = host_read_text;
        pi.host.replace_text = host_replace_text; pi.host.select_range = host_select_range; pi.host.move_file = host_move_file; pi.host.file_op = host_file_op; pi.host.message = host_message;
        pi.host.flush_notes = host_flush; pi.host.query_results = host_query_results; pi.host.active_version = host_active_version; pi.host.caret_offset = host_caret_offset;
        pi.wake = wake_ui;
        app.panels = ex_panels_create(&pi);
        if (app.panels) {
            ex_panels_register(app.panels);
            left_content = ex_panels_left(app.panels);
            left_content->width = ex_size_grow(); left_content->height = ex_size_grow(); left_content->bg_token = EX_COLOR_BACKGROUND_ALT;
        }
    }
    app.left_sb = ex_sidebar_create(app.ui, app.ws, EX_SIDE_LEFT, left_content, "Left sidebar");
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
    app.title_doc = ex_doc_create("", 0);
    app.title = app.title_doc ? ex_widget_editor(app.ui, app.title_doc, "Note title") : NULL;
    if (app.title) {                                                           /* the inline title: large and bold, one line, no frame */
        ex_editor_set_mode(app.title->editor, EX_EDITOR_PLAIN);
        ex_editor_set_wrap(app.title->editor, 1);                              /* a long title continues on the next row and the field grows */
        app.title->text_scale = 2.0f; app.title->bold = 1;
        app.title->width = ex_size_grow(); app.title->height = ex_size_token(EX_SIZE_SPACING, 7.0f);
        app.title->on_key = title_key; app.title->on_key_user = &app;
        app.title->on_text = title_text; app.title->on_text_user = &app;
        app.title->hidden = 1;
        ex_panes_set_header(app.panes, app.title);                              /* between the tab strip and the text, as in Obsidian */
    }
    ex_widget_add(col, ex_panes_root(app.panes));
    if (app.panels) ex_widget_add(col, ex_panels_bottom(app.panels));
    ex_widget_add(row, col);

    {
        ExWidget *right_content = app.panels ? ex_panels_right(app.panels) : info_label(&app, "Open a vault to see backlinks, the outline and tags.");
        if (right_content) { right_content->bg_token = EX_COLOR_BACKGROUND_ALT; right_content->width = ex_size_grow(); right_content->height = ex_size_grow(); }
        app.right_sb = ex_sidebar_create(app.ui, app.ws, EX_SIDE_RIGHT, right_content, "Right sidebar");
        ex_widget_add(row, ex_sidebar_divider_create(app.right_sb));
        ex_widget_add(row, app.right_sb);
    }
    status = ex_widget_label(app.ui, app.vault ? "Typing, selecting, several cursors (Ctrl+Alt+Up or Down, Alt+click), undo and redo work. Ctrl+Plus and Ctrl+Minus zoom."
                                                : "No vault is open. Start with:  exhaust FOLDER");
    status->text_token = EX_COLOR_TEXT_MUTED;
    status->text_scale = 0.85f;
    status->width = ex_size_grow();
    status->pad_x = 1.0f; status->pad_y = 0.5f;
    status->bg_token = EX_COLOR_BACKGROUND_ALT;
    {
        ExWidget *srow = ex_widget_box(app.ui, 0, "Status");
        srow->width = ex_size_grow();
        srow->bg_token = EX_COLOR_BACKGROUND_ALT;
        status->bg_token = -1;
        app.wordcount = ex_widget_label(app.ui, "");
        app.wordcount->text_token = EX_COLOR_TEXT_MUTED;
        app.wordcount->text_scale = 0.85f;
        app.wordcount->pad_x = 1.0f; app.wordcount->pad_y = 0.5f;
        ex_widget_add(srow, status);
        ex_widget_add(srow, app.wordcount);
        ex_widget_add(app.root, row);
        ex_widget_add(app.root, srow);
        on_wordcount_feature("word_count", ex_features_enabled(app.features, "word_count"), &app);
    }
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
    script_start();
    if (getenv("EXHAUST_OPEN") && app.vault) open_path(&app, 0, getenv("EXHAUST_OPEN"));       /* a developer's aid: open this note at start */
    relayout_and_draw(&app);
    while (running) {
        ExTextEvent te;
        ExKey key;
        int mods;
        script_step(&app);
        int timed_out = !SDL_WaitEventTimeout(&e, 100); /* wakes ten times a second for autosave and the watcher */
        static int idle_ticks;
        if (g_signal) running = 0;
        int woke = timed_out, redraw = !timed_out;     /* an idle wake-up draws only when the caret blinks: nothing else changed */
        /* Every event that is waiting is handled before the next frame: a frame waits for the screen (vsync), so one frame
         * for each of the hundreds of mouse-motion and resize events a second made the interface fall seconds behind. */
        for (;;) {
        if (e.type == g_wake_event && !timed_out) woke = 1;
        if (timed_out) {
            if (++idle_ticks % 5 == 0) { app.blink_on = !app.blink_on; redraw = 1; }   /* the caret blinks about every half second */
        } else if (e.type == g_wake_event) {
            ExMessage m;
            while (ex_queue_pop(results, &m))
                if (m.type == MSG_FONTS_READY) ex_renderer_set_font_db(app.r, (ExFontDb *)m.payload);
        } else if (e.type == SDL_EVENT_KEY_DOWN && !(app.panels && ex_panels_modal(app.panels)) && run_shortcut(&app, &e.key)) {
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
                ExWidget *input_root = app.panels && ex_panels_modal(app.panels) ? ex_panels_overlay(app.panels) : app.root;
                if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.button == SDL_BUTTON_RIGHT) {   /* the context menu of a file or folder */
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && app.panels && input_root == app.root) { ex_panels_pointer(app.panels, app.root, -1, -1, 0); ex_panels_context_menu(app.panels, app.root, x, y); }
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
                } else if (app.panels) {
                    ex_panels_pointer(app.panels, app.root, x, y, (ex_mods_from_sdl() & EX_MOD_PRIMARY) != 0);
                }
                set_cursor_shape(ex_ui_cursor_at(app.ui, app.root, x, y));       /* the resize pointer over a sidebar's edge */
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                float density = SDL_GetWindowPixelDensity(ex_renderer_window(app.r));
                ExWidget *wheel_root = app.panels && ex_panels_modal(app.panels) ? ex_panels_overlay(app.panels) : app.root;
                ExWidget *under = ex_ui_widget_at(wheel_root, e.wheel.mouse_x * density, e.wheel.mouse_y * density);
                while (under && under->type != EX_WIDGET_EDITOR && under->type != EX_WIDGET_READING && under->type != EX_WIDGET_LIST) under = under->parent;   /* the editor, reading view or list under the pointer, not only the active one */
                if (!under && wheel_root == app.root) under = app.editor;
                if (under && under->type == EX_WIDGET_LIST) ex_list_scroll(under, -e.wheel.y * 3.0f * ex_list_row_height(under));
                else if (under && under->type == EX_WIDGET_READING) ex_reading_scroll(under, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
                else if (under && under->editor) ex_editor_scroll(under->editor, -e.wheel.y * 3.0f * (float)ex_px(app.r, 24));
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
        if (timed_out || !running || !SDL_PollEvent(&e)) break;
        }
        if (app.notes) { ex_notes_tick(app.notes); handle_note_events(&app); }
        if (woke) {
            capture_tab_state(&app);
            if (app.session) ex_session_tick(app.session, app.ws, (int64_t)SDL_GetTicks());
        }
        if (app.panels) ex_panels_update(app.panels);
        if (redraw || app.title_warn_until || (app.panels && ex_panels_overlay(app.panels)) || ex_ws_revision(app.ws) != g_drawn_revision) {
            g_drawn_revision = ex_ws_revision(app.ws);
            if (app.title_warn_until && SDL_GetTicks() >= app.title_warn_until) app.title_warn_until = 0;     /* drawn once more without the bubble */
            relayout_and_draw(&app);
        }
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
    ex_findbar_free(app.findbar);
    ex_slash_free(app.slash);
    ex_spell_free(app.spell);
    ex_panes_free(app.panes);                                          /* contents are detached and closed; their notes are closed too */
    if (app.title) { ex_ui_release(app.ui, app.title); ex_widget_free(app.title); app.title = NULL; }
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
    ex_doc_free(app.title_doc);
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
