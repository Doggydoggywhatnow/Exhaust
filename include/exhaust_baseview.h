/* The Bases view (sub-slice 6b-iii): the widget that shows a `.base` file (or the base of an embed): a toolbar and one of five layouts (table, list, cards,
 * kanban, map) over an ExBaseResult.
 *
 * The widget never runs a view itself: it asks (handlers.run) and the application runs it on the service thread and hands the finished result back with
 * ex_baseview_set_result. It draws only the rows, cards and points that are in view. What the user changes in the toolbar (the view, the limit, the settings of
 * a layout) is written into the document of the `.base` file as one edit, so it goes through the journal and the autosave like typing; "Edit source" shows the
 * YAML itself in an editor on the same document, for everything the toolbar does not (filters, sorts, formulas, summaries).
 *
 * Layout settings (the keys of a view in the file; docs/formats.md, *Bases views*): table `rowHeight`; list `markers`, `indentProperties`, `separator`; cards
 * `cardSize`, `image`, `imageFit`, `imageAspectRatio`; kanban `columnWidth` and the same image keys; map `coordinates`. */
#ifndef EXHAUST_BASEVIEW_H
#define EXHAUST_BASEVIEW_H
#include "exhaust_base.h"
#include "exhaust_panels.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExBaseViewHandlers {
    void *user;
    ExPanels *panels;                                   /* for the menus and questions (may be NULL: there are none) */
    /* ask the application to run view `view` of the base with this text (a copy is yours to keep); returns the number the answer will carry */
    unsigned long (*run)(void *user, const char *text, size_t len, size_t view, const char *this_path);
    void (*open_file)(void *user, const char *path, int new_tab);
    void (*open_url)(void *user, const char *url);
    void (*message)(void *user, const char *text);
    /* Export CSV: the application writes the file and says where (the message); 0 on success */
    int (*export_file)(void *user, const char *suggested_name, const char *data, size_t len);
    /* the clipboard (copy to clipboard); NULL: the platform's */
    void (*set_clipboard)(void *user, const char *text);
} ExBaseViewHandlers;

/* doc is the document of the `.base` file (the caller owns it and it outlives the widget); this_path is what `this` is. */
ExWidget *ex_widget_base(ExUi *ui, ExDoc *doc, const char *path, const ExBaseViewHandlers *h);
/* The answer to a run: takes the result (malloc'ed ExBaseResult, freed with ex_baseview_result_free by whoever does not take it). error: NULL, or why it failed. */
void ex_baseview_set_result(ExWidget *w, unsigned long req, ExBaseResult *result, const char *error);
void ex_baseview_invalidate(ExWidget *w);               /* the vault changed: run again soon */
void ex_baseview_draw(ExUi *ui, ExWidget *w);
void ex_baseview_wheel(ExWidget *w, float dx, float dy, float x, float y);
void ex_baseview_context(ExWidget *w, float x, float y);
void ex_baseview_set_view(ExWidget *w, size_t view);
size_t ex_baseview_current_view(const ExWidget *w);
/* The current view as text for the clipboard and as CSV (malloc'ed; NULL when there is no result yet). */
char *ex_baseview_copy_text(const ExWidget *w);
char *ex_baseview_csv(const ExWidget *w, size_t *len);
int ex_baseview_editing_source(const ExWidget *w);
void ex_baseview_toggle_source(ExWidget *w);
void ex_baseview_commit(ExWidget *w);

typedef struct ExBaseViewStats {
    unsigned long runs, results;                        /* runs asked for and results taken */
    size_t rows, cols, groups;                          /* of the result on show */
    size_t items_drawn;                                 /* rows, cards or points drawn in the last frame */
    int layout;                                         /* 0 table, 1 list, 2 cards, 3 kanban, 4 map, -1 none */
    int loading, failed, source;
    float scroll_y, scroll_x;
    double own_ms;
} ExBaseViewStats;
void ex_baseview_stats(const ExWidget *w, ExBaseViewStats *out);
const ExBase *ex_baseview_base(const ExWidget *w);
const ExBaseResult *ex_baseview_result(const ExWidget *w);
/* Commands: copy the view as a Markdown table, add a view of a type (table, list, cards, kanban, map), open the menu of the views. */
void ex_baseview_copy(ExWidget *w);
void ex_baseview_add_view(ExWidget *w, const char *type);
void ex_baseview_view_menu(ExWidget *w);
int ex_baseview_owns_request(const ExWidget *w, unsigned long req);   /* is this the request the view last made? (the application finds the view an answer is for) */
void ex_baseresult_free_heap(ExBaseResult *r);          /* frees a result made by the service (the arena, then the struct) */

#ifdef __cplusplus
}
#endif
#endif
