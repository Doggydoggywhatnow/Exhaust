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
#include "exhaust_ui.h"
#include "exhaust_workspace.h"

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
void ex_panels_search_for(ExPanels *p, const char *query);          /* opens Search with this text and runs it */
void ex_panels_rename_file(ExPanels *p, const char *path);
/* Renames a note to new_name (no folder, no extension) in its own folder, through the link-safe flow; the link question is asked as
 * for any rename. Returns 0 if it was started, -1 (with a message in the status line) if the name is not valid or is taken. */
int ex_panels_rename_to(ExPanels *p, const char *path, const char *new_name);
void ex_panels_move_file_prompt(ExPanels *p, const char *path);    /* asks for the folder, then for the links */
void ex_panels_file_menu(ExPanels *p, const char *path);           /* the menu of a file or folder ("" the vault) at a default place */
void ex_panels_file_menu_at(ExPanels *p, const char *path, float x, float y);   /* ... at a window position, next to the pointer */
int ex_panels_context_menu(ExPanels *p, ExWidget *root, float x, float y);   /* right click on a row of the file explorer */          /* asks for the new name, then for the links */
/* Reading mode: asks for the note's text with every `query` block replaced by its results; the answer comes through host.query_results. */
void ex_panels_expand_queries(ExPanels *p, const char *path, const char *text, size_t len);
void ex_panels_open_switcher(ExPanels *p);
void ex_panels_open_palette(ExPanels *p);
void ex_panels_close_dialog(ExPanels *p);
/* The suggestions for [[ typed in the editor: fills out, returns the count. */
struct ExLinkSuggestion;
size_t ex_panels_suggest_links(void *panels, const char *query, struct ExLinkSuggestion *out, size_t max);

#ifdef __cplusplus
}
#endif
#endif
