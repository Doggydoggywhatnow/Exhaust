/* Finding your way (sub-slices 4b-iii and 4b-iv): the quick switcher, the command palette, the outline, bookmarks, the
 * random note and the footnotes pane. Everything here is a function of the index, the command registry or a note's text;
 * the panels that show it are in the interface. */
#ifndef EXHAUST_NAV_H
#define EXHAUST_NAV_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_commands.h"
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the quick switcher (Ctrl+O) ---- */
typedef struct ExSwitcher ExSwitcher;
/* A snapshot of the names in the vault: file names (notes without .md), paths and aliases. include_attachments is the
 * setting quick_switcher.show_attachments: other files than notes are listed too. */
ExSwitcher *ex_switcher_load(ExIndex *r, int include_attachments);
void ex_switcher_free(ExSwitcher *s);
size_t ex_switcher_size(const ExSwitcher *s);

typedef struct ExSwitchItem {
    const char *path;       /* the file, from the vault (owned by the switcher) */
    const char *label;      /* what matched: the name, an alias or the path */
    int via_alias;          /* label is an alias of the note: shown as "alias -> Note" */
    int is_note;
    int score;
} ExSwitchItem;
/* The best matches, best first (at most max, written to out). An empty query lists the recent notes (paths, newest
 * first) that still exist, then nothing else. Fuzzy: the letters of the query in order, word starts count more. */
size_t ex_switcher_query(const ExSwitcher *s, const char *query, const char *const *recent, size_t nrecent, size_t max, ExSwitchItem *out);

typedef struct ExSwitchAction {
    int create;             /* 1: make a new note at path (nothing exists there yet); 0: open path */
    char path[512];
} ExSwitchAction;
/* What Enter does: open the selected item; with no item (nothing matched, or show_existing_only is off and the query is a
 * new name) create the note the query names ("Folder/Name"; ".md" is added). -1 if the query is not a valid note name. */
int ex_switcher_enter(const ExSwitcher *s, const char *query, const ExSwitchItem *selected, int show_existing_only, ExSwitchAction *out);

/* ---- the command palette (Ctrl+P) ---- */
typedef struct ExPaletteItem {
    const char *id, *name, *keys;     /* from the command table */
    int available;                    /* can run now (bound, feature on, check passes) */
    int pinned;
    int score;
} ExPaletteItem;
/* Every command of the registry that matches, best first; pinned commands (ids, from command_palette.pinned) come first
 * when the query is empty and rank above the others when it is not. *out is malloc'ed. */
size_t ex_palette_query(const ExCommands *c, const char *query, const char *const *pinned, size_t npinned, ExPaletteItem **out);


/* ---- the outline (sub-slice 4b-iv) ---- */
typedef struct ExOutlineItem {
    int level;                      /* 1 to 6 */
    char *text;
    size_t start, end;              /* the heading */
    size_t section_end;             /* where its section ends: the next heading of the same or a higher level, or the end */
    uint32_t line;                  /* 1-based */
} ExOutlineItem;
int ex_outline(const char *text, size_t len, ExOutlineItem **out, size_t *n);
void ex_outline_free(ExOutlineItem *items, size_t n);
/* Drag and drop in the outline: the section of heading `from` (with the sections below it) moves to just before heading `to`
 * (to == the number of headings: to the end of the note). Levels are not changed; nothing else in the note moves. Returns the
 * new text (malloc'ed), or NULL if the move makes no sense (into itself, or nowhere). */
char *ex_outline_move(const char *text, size_t len, size_t from, size_t to, size_t *out_len);

/* ---- bookmarks (.exhaust/bookmarks.ini, docs/formats.md "Bookmarks") ---- */
typedef enum ExBookmarkType { EX_BM_NOTE, EX_BM_HEADING, EX_BM_BLOCK, EX_BM_SEARCH, EX_BM_GRAPH, EX_BM_GROUP } ExBookmarkType;
typedef struct ExBookmark {
    int id;                         /* stable, never reused within a file */
    int type;                       /* ExBookmarkType */
    char *title;
    char *path;                     /* note, heading and block bookmarks: the note */
    char *heading, *block;          /* heading and block bookmarks */
    char *query;                    /* search bookmarks: the search text; graph bookmarks: the graph settings as text */
    int parent;                     /* the group it is in (a bookmark of type GROUP), 0 for the top */
} ExBookmark;
typedef struct ExBookmarks ExBookmarks;
/* Loads the file (missing: empty). With async set, every change is written by a worker thread at once (the interface never
 * waits for the disk); without it the file is written before the call returns. */
ExBookmarks *ex_bookmarks_load(const char *vault_root, int async);
/* Writes what is pending, stops the worker and frees. */
void ex_bookmarks_free(ExBookmarks *b);
size_t ex_bookmarks_count(const ExBookmarks *b);
const ExBookmark *ex_bookmarks_at(const ExBookmarks *b, size_t i);       /* in stored order: the bookmarks of a group are the entries whose parent is its id, in this order */
const ExBookmark *ex_bookmarks_find(const ExBookmarks *b, int id);
/* Returns the new id, or 0. title may be NULL (a default is made). */
int ex_bookmarks_add(ExBookmarks *b, const ExBookmark *what, int parent);
int ex_bookmarks_remove(ExBookmarks *b, int id);                         /* a group's children move up one level */
int ex_bookmarks_rename(ExBookmarks *b, int id, const char *title);
/* Moves a bookmark into a group (0: the top) and before another bookmark of that group (0: to the end). */
int ex_bookmarks_move(ExBookmarks *b, int id, int parent, int before);
/* A note or folder was renamed or moved: bookmarks of it follow (a folder: everything under it). Returns how many changed. */
int ex_bookmarks_follow_rename(ExBookmarks *b, const char *from, const char *to);
/* Waits until the writes are done (for tests and for leaving). */
void ex_bookmarks_flush(ExBookmarks *b);

/* ---- the random note ---- */
/* A note of the vault chosen at random (not `except`, the open note, if there is another one); *state is the generator's state
 * (any value; it changes). NULL if the vault has no note. malloc'ed vault path. */
char *ex_random_note(ExIndex *r, const char *except, unsigned long long *state);

/* ---- footnotes ---- */
typedef struct ExFootnote {
    char *id;                       /* the name in [^name]; inline footnotes get "inline-N" */
    char *text;                     /* the text of the definition or inline note; NULL if the reference has no definition */
    int number;                     /* 1, 2, ... by the first reference; 0 for a definition nothing refers to */
    int inline_note;
    size_t def_start, def_end;      /* the definition or inline note in the text (0, 0 if missing) */
    size_t first_ref;               /* where the first reference is (the inline note itself for an inline one) */
    size_t refs;
} ExFootnote;
/* The footnotes of a note, numbered references first, then definitions that nothing refers to. */
int ex_footnotes(const char *text, size_t len, ExFootnote **out, size_t *n);
void ex_footnotes_free(ExFootnote *f, size_t n);

#ifdef __cplusplus
}
#endif
#endif
