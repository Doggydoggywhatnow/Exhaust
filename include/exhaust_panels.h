/* The panels and dialogs (sub-slice 4c): the file-independent half of the interface of 4a and 4b.
 *
 * Left sidebar: Files, Search (with saved searches) and Bookmarks. Right sidebar: Backlinks (with unlinked mentions), Outgoing
 * links, Tags, Properties (all of the vault), Outline and Footnotes. Dialogs: the quick switcher, the command palette, a prompt
 * (rename, save a search...), a choice (property type) and the confirmation before links are rewritten. A page preview shows
 * when the pointer rests on a link. The heavy work (search, links, tags, rewriting) runs on the panel service's thread. */
#ifndef EXHAUST_PANELS_H
#define EXHAUST_PANELS_H
#include <stddef.h>
#include "exhaust_commands.h"
#include "exhaust_graphopts.h"
#include "exhaust_ui.h"
#include "exhaust_workspace.h"

struct ExGraph;
struct ExBaseResult;
struct ExImage;
struct ExAudioDecoder;
struct ExMpv;
struct ExPdf;
struct ExPlayer;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExPanels ExPanels;
enum { EX_FILEOP_DELETE, EX_FILEOP_NEW_FOLDER, EX_FILEOP_COPY_REL, EX_FILEOP_COPY_ABS, EX_FILEOP_OPEN_DEFAULT, EX_FILEOP_SHOW, EX_FILEOP_COPY_FILE };

typedef struct ExPanelsHost {
    void *user;
    /* open a note (created, not yet on disk, if it does not exist), and put the caret at offset ((size_t)-1: leave it) */
    void (*open_note)(void *user, const char *path, size_t offset, int new_tab);
    const char *(*active_path)(void *user);                              /* the note of the active tab, or NULL */
    char *(*active_text)(void *user, size_t *len);                       /* a copy of its text (the buffer) */
    char *(*read_text)(void *user, const char *path, size_t *len);       /* a note's text: the buffer if it is open, else the file */
    int (*replace_text)(void *user, const char *path, const char *text, size_t len);   /* as one edit of the open buffer; 0 on success */
    void (*select_range)(void *user, size_t from, size_t to);            /* in the active editor */
    unsigned long (*active_version)(void *user);                         /* grows when the active note's text changes */
    size_t (*caret_offset)(void *user);                                  /* the caret in the active editor, or (size_t)-1 */
    int (*move_file)(void *user, const char *from, const char *to);      /* moves a note or folder (tabs, tree and index follow); 0 on success */
    void (*message)(void *user, const char *text);                       /* a line in the status bar */
    void (*query_results)(void *user, const char *path, const char *expanded);   /* reading mode: the note with its query blocks replaced by their results */
    void (*flush_notes)(void *user);                                     /* write every open note now */
    /* what the explorer menu does with a file or folder (EX_FILEOP_*): delete (closes its tabs, by files.trash_mode), new folder (arg is the
     * name, path the folder), copy the path, open with the default program, show in the file manager. 0 on success. */
    int (*file_op)(void *user, int op, const char *path, const char *arg);
    /* the graph's data, read on the service thread (sub-slice 5a): the receiver owns the graph and frees it (ex_graph_free, then free) */
    void (*graph_data)(void *user, struct ExGraph *graph);
    /* the local graph's data (5c): the open note and what is within the options' local_depth links of it; same ownership */
    void (*local_graph_data)(void *user, struct ExGraph *graph);
    /* the user picked an earlier version of a note in the history (7e): its text, and the unified diff of it against the note as it is now */
    void (*history_version)(void *user, const char *rel, const char *label, const char *old_text, size_t len, const char *diff);
    /* the start of a note a canvas card shows (6a), asked for with ex_panels_file_text: key is what was asked for, text NULL when it cannot be read */
    void (*file_text)(void *user, const char *key, const char *text);
    /* a base view run (6b-iii), asked for with ex_panels_run_base: the callback takes the result (NULL: error holds the reason) */
    void (*base_result)(void *user, unsigned long req, struct ExBaseResult *result, const char *error);
    /* a picture decoded on the service thread (6c-i), asked for with ex_panels_load_image: the callback takes the picture (NULL: error says why) */
    void (*image_result)(void *user, const char *path, int svg_width, struct ExImage *image, const char *error);
    /* an audio file opened on the service thread (6c-ii), asked for with ex_panels_load_audio: the callback takes the decoder (NULL: error says why) */
    void (*audio_result)(void *user, const char *path, struct ExAudioDecoder *decoder, const char *error);
    /* a media file opened by libmpv on the service thread (6c-iii), asked for with ex_panels_load_media: the callback takes the player (NULL: error says why) */
    void (*media_result)(void *user, const char *path, struct ExMpv *player, const char *error);
    /* a PDF opened on the service thread (6c-iv), asked for with ex_panels_load_pdf: the callback takes the document (NULL: error says why) */
    void (*pdf_result)(void *user, const char *path, struct ExPdf *doc, const char *error);
} ExPanelsHost;

typedef struct ExPanelsInit {
    ExUi *ui;
    const char *vault_root, *vault_id;
    ExSettings *settings;
    ExFeatures *features;
    ExCommands *commands;
    ExWorkspace *ws;
    ExWidget *files;                  /* the file explorer's sidebar content (not owned by the panels until added) */
    ExPanelsHost host;
    void (*wake)(void *);             /* wakes the interface loop when a result is ready */
    void *wake_user;
    const char *index_path;           /* NULL: the one in the OS cache folder */
} ExPanelsInit;

ExPanels *ex_panels_create(const ExPanelsInit *init);
void ex_panels_free(ExPanels *p);

ExWidget *ex_panels_left(ExPanels *p);       /* the content of the left sidebar */
ExWidget *ex_panels_right(ExPanels *p);      /* the content of the right sidebar */
ExWidget *ex_panels_bottom(ExPanels *p);     /* backlinks at the bottom of the note (setting backlinks.show_at_bottom); hidden otherwise */
/* The root of what is drawn over the window (a dialog, the preview) or NULL. A modal dialog takes the input: ex_panels_modal. */
ExWidget *ex_panels_overlay(ExPanels *p);
int ex_panels_modal(const ExPanels *p);
int ex_panels_index_ready(const ExPanels *p);       /* the first sync of the index has finished */

/* Once a frame, before layout: takes the service's results, follows the active note, runs the debounced search. */
void ex_panels_update(ExPanels *p);
/* The pointer moved (window pixels); ctrl: the Ctrl key is down. Starts or stops the page preview. */
void ex_panels_pointer(ExPanels *p, ExWidget *root, float x, float y, int ctrl);
/* What the application learns and the panels must know. */
void ex_panels_note_opened(ExPanels *p, const char *path);          /* recent notes, bookmarks of it */
void ex_panels_file_changed(ExPanels *p, const char *path);         /* saved or changed on disk: indexed again */
void ex_panels_file_removed(ExPanels *p, const char *path);
void ex_panels_tree_changed(ExPanels *p);                           /* something changed outside: sync the index */
void ex_panels_files_moved(ExPanels *p, const char *from, const char *to);   /* bookmarks follow */

/* The commands of this area and the feature switches (called once, after the registries exist). */
void ex_panels_register(ExPanels *p);

/* Actions the application calls itself. */
void ex_panels_show(ExPanels *p, const char *panel);                /* "files", "search", "bookmarks", "backlinks", ... */
/* The graph view is open (wanted = 1): the panels read the graph's data now and again whenever the index changes (at most once every 1.5
 * seconds), and hand it to host.graph_data. wanted = 0 stops that. */
/* A popup menu at a window position for the application (the pane menu of 5d): each item has a label, an icon (ExIcon), an id the callback
 * gets, and may be a red one or have a separator before it. arg must stay valid until the callback runs. */
typedef struct ExMenuItem { const char *label; int icon; int id; int danger, sep_before; } ExMenuItem;
void ex_panels_popup(ExPanels *p, float x, float y, const ExMenuItem *items, size_t n, void (*cb)(ExPanels *p, int id, void *arg), void *arg);
void ex_panels_bookmark_note(ExPanels *p, const char *path);          /* the Bookmark entry of the note menu */
/* The right sidebar's group of tabs (5e). available: the group has tabs, so the strip shows a button for them; showing: the tabs are what
 * the sidebar shows, so the panels' view (below the strip) is hidden. ex_panels_take_right_click tells the application, once, that the user
 * pressed a view button of the right strip (*to_tabs = 0: show the panels) or the tabs button (*to_tabs = 1); it returns 0 when neither. */
void ex_panels_set_side_tabs(ExPanels *p, int available, int showing);
int ex_panels_take_right_click(ExPanels *p, int *to_tabs);
void ex_panels_graph_wanted(ExPanels *p, int wanted);
/* The same for the local graph (5c): read while a local graph tab exists, around the note `center` (a path; NULL: no note is open), to the
 * depth of the options given to ex_panels_graph_options. A new centre reads it again at once. */
void ex_panels_local_graph(ExPanels *p, int wanted, const char *center);
/* The filters and groups the next graph read applies on the service thread (sub-slice 5b); the graph is read again at once. */
void ex_panels_graph_options(ExPanels *p, const ExGraphOptions *o);
void ex_panels_search_for(ExPanels *p, const char *query);          /* opens Search with this text and runs it */
void ex_panels_rename_file(ExPanels *p, const char *path);
/* Renames a note to new_name (no folder, no extension) in its own folder, through the link-safe flow; the link question is asked as
 * for any rename. Returns 0 if it was started, -1 (with a message in the status line) if the name is not valid or is taken. */
int ex_panels_rename_to(ExPanels *p, const char *path, const char *new_name);
void ex_panels_move_file_prompt(ExPanels *p, const char *path);    /* asks for the folder, then for the links */
void ex_panels_file_menu(ExPanels *p, const char *path);           /* the menu of a file or folder ("" the vault) at a default place */
void ex_panels_file_menu_at(ExPanels *p, const char *path, float x, float y);
void ex_panels_unresolved_menu_at(ExPanels *p, const char *name, float x, float y);   /* a graph node nothing answers to yet: Create note */   /* ... at a window position, next to the pointer */
int ex_panels_context_menu(ExPanels *p, ExWidget *root, float x, float y);   /* right click on a row of the file explorer */          /* asks for the new name, then for the links */
/* Reading mode: asks for the note's text with every `query` block replaced by its results; the answer comes through host.query_results. */
void ex_panels_expand_queries(ExPanels *p, const char *path, const char *text, size_t len);
/* Asks the service thread for the start of a note (6a): `path`, `path#Heading` or `path#^block`; the answer goes to host.file_text. */
void ex_panels_file_text(ExPanels *p, const char *key);
/* Runs view `view` of the base file `text` over the vault on the service thread (6b-iii); this_path is what `this` stands for. Returns the request number the
 * answer (host.base_result) carries, or 0 when the index is not ready. */
/* Grows whenever the index changed (a note was saved and read again, the first index finished): views of the vault's data run again. */
unsigned long ex_panels_index_generation(const ExPanels *p);
/* Asks the service thread to read and decode a picture of the vault (6c-i); the answer goes to host.image_result. */
void ex_panels_load_image(ExPanels *p, const char *path, int svg_width);
/* Asks the service thread to read an audio file, open its decoder and open the player's output device (6c-ii); the answer goes to host.audio_result. */
/* Asks the service thread to open a video (video 1) or an audio file (0) with libmpv (6c-iii); the answer goes to host.media_result. */
/* Asks the service thread to read a PDF of the vault and open it with PDFium (6c-iv); the answer goes to host.pdf_result. */
void ex_panels_load_pdf(ExPanels *p, const char *path);
void ex_panels_load_media(ExPanels *p, const char *path, int video);
void ex_panels_load_audio(ExPanels *p, const char *path, struct ExPlayer *player);
unsigned long ex_panels_run_base(ExPanels *p, const char *text, size_t len, size_t view, const char *this_path);
void ex_panels_open_switcher(ExPanels *p);
void ex_panels_open_palette(ExPanels *p);
void ex_panels_close_dialog(ExPanels *p);
/* Questions the application asks (7e). The answer comes in the callback, after the dialog has closed; Cancel and Escape call nothing. */
void ex_panels_ask_text(ExPanels *p, const char *title, const char *label, const char *initial, void (*cb)(const char *text, void *arg), void *arg);
void ex_panels_ask_choice(ExPanels *p, const char *title, const char *const *labels, size_t n, void (*cb)(size_t index, void *arg), void *arg);
/* the same with row `selected` chosen when it opens */
void ex_panels_ask_choice_at(ExPanels *p, const char *title, const char *const *labels, size_t n, size_t selected, void (*cb)(size_t index, void *arg), void *arg);
/* up to three buttons; the callback gets 1, 2 or 3 */
void ex_panels_ask_confirm(ExPanels *p, const char *title, const char *message, const char *b1, const char *b2, const char *b3, void (*cb)(int button, void *arg), void *arg);
/* File recovery (7e): lists the earlier versions of a note (read from `dir` on the service thread), asks which one, and hands it to the host's
 * history_version; and clears the automatic snapshots of every note after a confirmation. */
void ex_panels_history_open(ExPanels *p, const char *dir, const char *rel);
void ex_panels_history_clear(ExPanels *p, const char *dir);
/* The suggestions for [[ typed in the editor: fills out, returns the count. */
struct ExLinkSuggestion;
size_t ex_panels_suggest_links(void *panels, const char *query, struct ExLinkSuggestion *out, size_t max);

#ifdef __cplusplus
}
#endif
#endif
