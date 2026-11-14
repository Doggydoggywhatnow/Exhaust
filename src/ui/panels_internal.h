#ifndef EXHAUST_PANELS_INTERNAL_H
#define EXHAUST_PANELS_INTERNAL_H
#include "exhaust_panels.h"
#include "exhaust_nav.h"
#include "exhaust_query.h"
#include "service.h"

enum { V_FILES, V_SEARCH, V_BOOKMARKS, V_BACKLINKS, V_OUTGOING, V_TAGS, V_PROPS, V_FILEPROPS, V_OUTLINE, V_FOOTNOTES, V_COUNT };

typedef struct View {
    const char *id, *title, *label, *feature;
    int side;                       /* 0 left, 1 right */
    int shown;                      /* its feature is on */
    int auto_chevron;
    ExWidget *root, *button, *summary, *list, *list2, *extra;
    QRows rows, rows2;
    char **collapsed; size_t ncollapsed;
    int collapse_all;
    int stale;
    unsigned long req, req2;
    char summary_text[240];
} View;

typedef enum { D_NONE, D_SWITCHER, D_PALETTE, D_PROMPT, D_CHOICE, D_CONFIRM, D_POPUP, D_INLINE } DialogKind;

struct ExPanels {
    ExPanelsInit in;
    ExUi *ui;
    ExQueue *results;
    QService *svc;
    ExIndexer *indexer;
    char *index_path, *root;
    ExIndex *reader;                 /* for the small lookups of the interface thread (link suggestions) */
    View views[V_COUNT];
    ExWidget *left, *right, *left_strip, *right_strip, *left_stack, *right_stack;
    int active[2];
    ExBookmarks *bookmarks;
    ExSavedQueries *saved;
    ExSwitcher *snapshot;
    int snapshot_stale;
    unsigned long next_req;
    char *cur_path;                  /* the note of the active tab as last seen */
    unsigned long text_version; unsigned long text_seen_at; int text_dirty;
    unsigned long index_dirty_at;
    int index_ready;
    char *recent[16]; size_t nrecent;
    /* search */
    ExDoc *search_doc; ExWidget *search_field, *b_case, *b_collapse, *b_context, *b_sort;
    unsigned long search_seen; unsigned long search_changed_at; int search_pending;
    int sort, match_case, more_context;
    /* the backlinks view, and the same list at the bottom of the note */
    int show_unlinked, bottom_on;
    ExWidget *bottom, *bottom_list;
    /* the properties view */
    ExWidget *prop_rename, *prop_type;
    /* the properties of the open note */
    ExWidget *fp_add, *fp_edit, *fp_remove, *fp_type;
    /* the bookmarks view */
    ExWidget *bm_remove, *bm_rename;
    /* dialogs */
    DialogKind dkind;
    ExWidget *overlay, *dialog_field, *dialog_list, *dialog_msg;
    ExDoc *dialog_doc;
    char dialog_title[160];
    void (*on_prompt)(ExPanels *p, const char *text, void *arg);
    void (*on_choice)(ExPanels *p, size_t index, const char *key, void *arg);
    void (*on_confirm)(ExPanels *p, int button, void *arg);
    void (*on_popup)(ExPanels *p, int id, void *arg);
    void *dialog_arg;
    char *dialog_arg_text;
    ExPaletteItem *palette; size_t npalette;
    ExSwitchItem sw_items[24]; size_t nsw; char sw_create[260]; int sw_has_create;
    /* the preview */
    ExWidget *preview;
    char prev_target[300], prev_heading[200], prev_block[100], prev_key[700];
    unsigned long prev_since; float prev_x, prev_y; int prev_pending, prev_shown; unsigned long prev_req;
    /* a rename in progress */
    char rename_from[512], rename_to[512];
    int rename_is_dir;
    unsigned long req_plan, req_rewrite, req_propop;
    ExMove *pending_moves; size_t npending;
    int link_mode;                   /* 0 ask, 1 automatic, 2 off, as read when the rename started */
};

/* views.c */
void views_build(ExPanels *p);
void views_free(ExPanels *p);
void views_refresh(ExPanels *p);               /* posts the requests for the views that are stale and visible */
void views_result(ExPanels *p, QRes *r);
void views_show(ExPanels *p, int view);
int view_visible(const ExPanels *p, int view);
void views_note_changed(ExPanels *p);
void views_search_tick(ExPanels *p);
void views_set_search_text(ExPanels *p, const char *text);
void views_bookmarks_changed(ExPanels *p);
void views_refresh_saved(ExPanels *p);
void views_features(ExPanels *p);
void fill_list(View *v, ExWidget *list, QRows *rows);
View *view_by_id(ExPanels *p, const char *id);
/* dialogs.c */
void dialogs_free(ExPanels *p);
void dialog_close(ExPanels *p);
void dialog_prompt(ExPanels *p, const char *title, const char *label, const char *initial, void (*cb)(ExPanels *, const char *, void *), void *arg);
void dialog_choice(ExPanels *p, const char *title, const char **labels, size_t n, size_t selected, void (*cb)(ExPanels *, size_t, const char *, void *), void *arg);
void dialog_confirm(ExPanels *p, const char *title, const char *message, const char *b1, const char *b2, const char *b3, void (*cb)(ExPanels *, int, void *), void *arg);
/* A popup menu at a window position (pixels): entries with an icon, separators between groups, a chevron for a submenu, a red
 * entry for something that deletes. Opens next to the pointer, kept inside the window; Escape or a click outside closes it. */
typedef struct PopupItem { const char *label; int icon; int id; int danger, sep_before, submenu; } PopupItem;
void dialog_popup(ExPanels *p, float x, float y, const PopupItem *items, size_t n, void (*cb)(ExPanels *, int, void *), void *arg);
/* A text field in place over a row of a list (window pixels): Enter or a click elsewhere accepts, Escape cancels. */
void dialog_inline(ExPanels *p, float x, float y, float w, float h, const char *initial, void (*cb)(ExPanels *, const char *, void *), void *arg);
void dialog_commit(ExPanels *p);                       /* an inline field in the way: its text is accepted (a click outside, a new dialog) */
void dialog_switcher(ExPanels *p);
void dialog_palette(ExPanels *p);
void dialog_refresh(ExPanels *p);
void preview_hide(ExPanels *p);
void preview_result(ExPanels *p, QRes *r);
/* panels.c */
unsigned long now_ms(void);
char *dup_s(const char *s);
ExWidget *make_field(ExPanels *p, ExDoc *doc, const char *name, int (*key)(ExWidget *, ExKey, int, void *), void *user);
ExWidget *make_button(ExPanels *p, const char *text, const char *name, ExClickFn fn);
char *field_text_of(ExDoc *d);
void set_doc_text(ExDoc *d, const char *text);
void send_job(ExPanels *p, QJob *j);
void link_update_mode(ExPanels *p);
void do_rename_flow(ExPanels *p, const char *from, const char *to);
void panels_open_note(ExPanels *p, const char *path, size_t offset, int new_tab);
/* fileprops.c */
void fileprops_on_add(ExWidget *w, void *u);
void fileprops_on_edit(ExWidget *w, void *u);
void fileprops_on_remove(ExWidget *w, void *u);
void fileprops_on_type(ExWidget *w, void *u);
void panels_move_done(ExPanels *p, const char *folder, void *arg);
void fileactions_init(ExPanels *p);
void note_title(const char *path, char *out, size_t cap);
#endif
