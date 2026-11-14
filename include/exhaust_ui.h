/* Exhaust's widget tree. Widgets are plain structs in a tree; each frame the tree
 * is turned into a Clay layout (the layout engine), and the resulting rectangles
 * are stored back in the widgets for drawing and hit testing. Every widget has a
 * role and a name for the accessibility tree (sub-slice 1b). Colors, sizes and
 * fonts come only from theme tokens. */
#ifndef EXHAUST_UI_H
#define EXHAUST_UI_H

#include "exhaust_a11y.h"
#include "exhaust_editor.h"
#include "exhaust_reading.h"
#include "exhaust_vault.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExWidgetType { EX_WIDGET_BOX, EX_WIDGET_LABEL, EX_WIDGET_BUTTON, EX_WIDGET_EDITOR, EX_WIDGET_TREE, EX_WIDGET_READING, EX_WIDGET_LIST, EX_WIDGET_TYPE_COUNT } ExWidgetType;

typedef enum ExRole { EX_ROLE_WINDOW, EX_ROLE_GROUP, EX_ROLE_LABEL, EX_ROLE_BUTTON, EX_ROLE_TEXT_INPUT, EX_ROLE_TREE, EX_ROLE_LIST } ExRole;

typedef enum ExSizing { EX_SIZING_FIT, EX_SIZING_GROW, EX_SIZING_FIXED, EX_SIZING_PERCENT } ExSizing;

/* One axis of a widget's size. FIXED is size_token (a theme size) times value; with
 * size_token < 0 it is value times the spacing token. PERCENT is value (0 to 1) of the parent. */
typedef struct ExAxis { ExSizing mode; int size_token; float value; } ExAxis;

typedef struct ExWidget ExWidget;
typedef void (*ExClickFn)(ExWidget *w, void *user);

struct ExWidget {
    ExWidgetType type;
    ExRole role;
    char *name;                   /* accessible name */
    uint32_t id;                  /* unique in the tree, stable */
    ExWidget *parent, *first_child, *last_child, *next;

    /* layout */
    int vertical;                 /* children stack top to bottom (else left to right) */
    ExAxis width, height;
    float pad_x, pad_y, gap;      /* in units of the theme's spacing token */
    int clip;                     /* clip children to this widget */
    int align_x, align_y;         /* where the children sit in the free room: 0 start, 1 center, 2 end */
    int hidden;                   /* not laid out, drawn, focused or exposed to screen readers */

    /* look: theme token ids, -1 for none */
    int bg_token;                 /* ExColorToken */
    int border_token;             /* ExColorToken */
    int radius_token;             /* ExSizeToken */

    /* label and button text */
    char *text;
    int text_token;               /* ExColorToken */
    int font_token;               /* ExFontToken */
    float text_scale;             /* times the base text size (1.0) */
    int bold;
    int icon;                     /* ExIcon: a button drawn as a picture (its name stays the accessible name) */

    /* behaviour */
    int focusable;
    int hover, pressed;           /* set by the UI from the mouse */
    ExClickFn on_click;
    void *on_click_user;
    ExEditor *editor;             /* EX_WIDGET_EDITOR */
    struct ExReadingView *reading;   /* EX_WIDGET_READING */
    struct ExListData *list;      /* EX_WIDGET_LIST (owned) */
    ExVault *vault;               /* EX_WIDGET_TREE: the vault whose rows it shows (not owned) */
    size_t tree_selected;         /* selected row */
    float tree_scroll;            /* pixels */
    void (*on_tree_open)(ExWidget *w, const char *rel, void *user); /* Enter or double click on a file */
    void *on_tree_open_user;
    /* Drag a row onto a folder row (or the empty area: the vault folder, to_folder ""): the application moves it. */
    void (*on_tree_drop)(ExWidget *w, const char *from_rel, const char *to_folder, void *user);
    void *on_tree_drop_user;
    long tree_drag_row, tree_drop_row;   /* the row being dragged (-1 none) and the folder row under it (-1 none) */
    int tree_dragging;
    float tree_drag_x, tree_drag_y;
    /* Mouse hook: called with press, drag and release for a press that started on this widget or a
     * descendant that has no hook of its own. `under` is the widget below the pointer. Returning
     * non-zero on the press takes it: the widget then gets the drag and the release too, and the
     * normal handling (focus, buttons, editors) is skipped. */
    int (*on_mouse)(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user);
    void *on_mouse_user;
    /* Drop hook for files and text dropped from the desktop onto this widget or a descendant. */
    int (*on_drop)(ExWidget *w, int is_file, const char *data, float x, float y, void *user);
    void *on_drop_user;
    /* Key hook: the focused widget sees every key first; returning non-zero takes it (a text field uses
     * it for Enter and Escape). */
    int (*on_key)(ExWidget *w, ExKey key, int mods, void *user);
    void *on_key_user;
    uint32_t tag;                 /* for the application: the pane or tab id a widget stands for */
    int selected;                 /* drawn as the current item (an active tab) */
    void *owned;                  /* anything the widget owns besides itself; freed with it */
    void (*free_owned)(void *);

    /* set by layout: pixels in window coordinates */
    float x, y, w, h;
};

typedef struct ExUi ExUi;

/* The UI draws with the renderer's fonts and the theme (which must outlive it). */
ExUi *ex_ui_create(ExRenderer *r, const ExTheme *theme, float base_text_size);
void ex_ui_free(ExUi *ui);
void ex_ui_set_theme(ExUi *ui, const ExTheme *theme);
const ExTheme *ex_ui_theme(const ExUi *ui);
ExRenderer *ex_ui_renderer(ExUi *ui);
/* Font used for a font token: the built-in interface, text (also Atkinson) and mono fonts. */
ExFont *ex_ui_font(ExUi *ui, int font_token, int bold);

/* Widget constructors. All colors and sizes default to none or fit. */
ExWidget *ex_widget_box(ExUi *ui, int vertical, const char *name);
ExWidget *ex_widget_label(ExUi *ui, const char *text);
ExWidget *ex_widget_button(ExUi *ui, const char *text, ExClickFn fn, void *user);
/* Icons for buttons (sub-slice 4d): drawn in the button's text color; the button keeps its accessible name. */
typedef enum ExIcon {
    EX_ICON_NONE, EX_ICON_FILES, EX_ICON_SEARCH, EX_ICON_BOOKMARK, EX_ICON_BACKLINKS, EX_ICON_LINKS, EX_ICON_TAGS, EX_ICON_PROPERTIES,
    EX_ICON_FILE_PROPERTIES, EX_ICON_OUTLINE, EX_ICON_FOOTNOTES, EX_ICON_NEW_NOTE, EX_ICON_NEW_FOLDER, EX_ICON_SORT, EX_ICON_EXPAND, EX_ICON_MORE
} ExIcon;
void ex_widget_set_icon(ExWidget *button, int icon);                 /* a square button of the theme's spacing: 3 units */
void ex_icon_draw(ExRenderer *r, int icon, float x, float y, float w, float h, ExColor c);
ExWidget *ex_widget_editor(ExUi *ui, ExDoc *doc, const char *name);
/* Reading mode: the note's text drawn as formatted pages to the width of the widget (read only, scrolls, follows links
 * and folds callouts). The text is copied; use ex_reading_set_text to show another or a changed one. */
ExWidget *ex_widget_reading(ExUi *ui, const char *text, size_t len, const char *name);
void ex_reading_set_text(ExWidget *w, const char *text, size_t len);
void ex_reading_set_link_handler(ExWidget *w, void (*fn)(ExRSpanKind kind, const char *target, void *user), void *user);
int ex_reading_internal_link_at(ExWidget *w, float x, float y, char *target, size_t cap);
void ex_reading_scroll(ExWidget *w, float dy);
float ex_reading_scroll_y(const ExWidget *w);
void ex_reading_set_scroll(ExWidget *w, float y);
/* The file explorer's tree: shows ex_vault_row() rows with chevrons, keeps a selection,
 * and calls on_open for a file. Up and Down move the selection, Right and Left open
 * and close folders (Left on a closed folder or a file goes to its folder), Enter opens. */
ExWidget *ex_widget_tree(ExUi *ui, ExVault *vault, const char *name,
                         void (*on_open)(ExWidget *w, const char *rel, void *user), void *user);
void ex_tree_select_path(ExWidget *tree, const char *rel);          /* reveals and selects */
const ExNode *ex_tree_selected(ExWidget *tree);
/* The row under a window position (and the node of it) or NULL; selects it when select is set. For the context menu. */
const ExNode *ex_tree_node_at(ExUi *ui, ExWidget *tree, float y, int select);
void ex_tree_set_drop_handler(ExWidget *tree, void (*fn)(ExWidget *w, const char *from_rel, const char *to_folder, void *user), void *user);
/* The explorer sidebar of the screenshots: a toolbar (new note, new folder, expand or
 * collapse all, sort), the tree, and the vault button at the bottom. */
ExWidget *ex_explorer_sidebar(ExUi *ui, ExVault *vault, void (*on_open)(ExWidget *w, const char *rel, void *user), void *user,
                              ExWidget **tree_out);

/* ---- lists (panels, dialogs): rows of text, one selected, scrolling, keyboard and mouse ---- */
enum { EX_GLYPH_NONE, EX_GLYPH_CHEVRON_CLOSED, EX_GLYPH_CHEVRON_OPEN, EX_GLYPH_BULLET };
typedef struct ExListRow {
    const char *text;             /* the row's text; the strings of a row are copied */
    const char *detail;           /* muted text at the right (a count, a shortcut), or NULL */
    const char *action;           /* a small button at the right ("Link"), or NULL */
    const char *key;              /* a string for the application (a path), or NULL */
    int indent;                   /* levels of indent */
    int glyph;                    /* EX_GLYPH_* before the text */
    int muted;                    /* drawn in the muted color */
    int bold;
    int64_t id;                   /* a number for the application */
    size_t aux1, aux2;            /* two more numbers (offsets) */
} ExListRow;
typedef struct ExListHandlers {
    void (*activate)(ExWidget *w, size_t row, void *user);          /* Enter, Space, a double click (or one click when single_click is set) */
    void (*action)(ExWidget *w, size_t row, void *user);            /* the action button of a row was pressed */
    void (*toggle)(ExWidget *w, size_t row, void *user);            /* a chevron was clicked, or Left or Right was pressed on a row with one */
    void (*move)(ExWidget *w, size_t from, size_t to, void *user);  /* a row was dragged onto another (only if drag is set) */
    void (*hover)(ExWidget *w, long row, float x, float y, void *user); /* the pointer is over a row (-1: none) */
    void *user;
    int single_click;             /* activate on a single click */
    int drag;                     /* rows can be dragged to another place */
} ExListHandlers;
ExWidget *ex_widget_list(ExUi *ui, const char *name);
void ex_list_clear(ExWidget *w);
size_t ex_list_add(ExWidget *w, const ExListRow *row);          /* returns its index */
size_t ex_list_count(const ExWidget *w);
const ExListRow *ex_list_row(const ExWidget *w, size_t i);
size_t ex_list_selected(const ExWidget *w);
void ex_list_select(ExWidget *w, size_t i);
void ex_list_set_handlers(ExWidget *w, const ExListHandlers *h);
void ex_list_set_empty_text(ExWidget *w, const char *text);     /* shown when there are no rows */
void ex_list_scroll(ExWidget *w, float dy);
float ex_list_scroll_y(const ExWidget *w);
void ex_list_set_scroll_y(ExWidget *w, float y);
float ex_list_row_height(const ExWidget *w);
long ex_list_row_at(const ExWidget *w, float x, float y);       /* -1 if none */
float ex_ui_base_size(const ExUi *ui);
/* Used by ui.c. */
void ex_list_draw(ExUi *ui, ExWidget *w);
void ex_list_key(ExUi *ui, ExWidget *w, ExKey key);
void ex_list_mouse(ExUi *ui, ExWidget *w, ExMouse kind, float x, float y, int clicks);

void ex_widget_insert_before(ExWidget *parent, ExWidget *child, ExWidget *before);
void ex_widget_remove(ExWidget *parent, ExWidget *child);   /* unlinks it; the caller frees it or adds it elsewhere */
/* The New note button creates no file: it asks the application (which starts a note that is
 * only written once something is typed). Without a handler the button creates an empty file. */
/* The More button of the toolbar calls this with the selected row's path ("" when nothing is selected). */
void ex_explorer_set_menu_handler(ExWidget *sidebar, void (*fn)(ExWidget *tree, const char *rel, void *user), void *user);
void ex_explorer_set_new_note_handler(ExWidget *sidebar, void (*fn)(ExWidget *w, const char *parent_rel, void *user), void *user);
/* The vault button at the bottom of the explorer calls this (the application opens the vault manager). */
void ex_explorer_set_vault_handler(ExWidget *sidebar, void (*fn)(ExWidget *w, void *user), void *user);
void ex_widget_add(ExWidget *parent, ExWidget *child);
void ex_widget_set_text(ExWidget *w, const char *text);
void ex_widget_free(ExWidget *root); /* frees the tree; editors are freed with it, documents are not */

/* Convenience for axis sizes. */
ExAxis ex_size_grow(void);
ExAxis ex_size_fit(void);
ExAxis ex_size_token(int size_token, float multiple);
ExAxis ex_size_percent(float fraction);
/* A size in pixels at 100% zoom, for data the user sets (a sidebar's width); everything else uses theme tokens. */
ExAxis ex_size_px(const ExUi *ui, float px);

/* Layout and drawing. width and height are the window's pixel size. */
void ex_ui_layout(ExUi *ui, ExWidget *root, float width, float height);
void ex_ui_draw(ExUi *ui, ExWidget *root, int blink_on);

/* Input, in window pixel coordinates. */
ExWidget *ex_ui_widget_at(ExWidget *root, float x, float y); /* the deepest widget under the point */
void ex_ui_mouse(ExUi *ui, ExWidget *root, ExMouse kind, float x, float y, int mods, int clicks);
/* Keyboard. Tab and Shift+Tab move the focus through every focusable widget in
 * tree order (the order of the accessibility tree), wrapping around; Enter and
 * Space press the focused button. In an editor Tab types a tab, so the editor is
 * left with Ctrl+Tab or Ctrl+Shift+Tab, or with Escape (once the selection is a
 * single caret) followed by Tab. The focused widget is always drawn with a visible
 * ring in the accent color. */
void ex_ui_key(ExUi *ui, ExKey key, int mods);
/* Moves the focus to the next (dir > 0) or previous (dir < 0) focusable widget of
 * the tree last laid out; returns the newly focused widget or NULL. */
ExWidget *ex_ui_focus_move(ExUi *ui, int dir);
void ex_ui_text_input(ExUi *ui, const char *utf8, size_t len);
/* Files (is_file, a path) or text dropped from the desktop at a window position: handed to the
 * deepest widget under the point that has an on_drop hook (a widget further up is asked when the
 * one below returns 0). Returns 1 when some hook took it. */
int ex_ui_drop(ExUi *ui, ExWidget *root, int is_file, const char *data, float x, float y);
/* Call before freeing or detaching a subtree: the UI forgets it as focus, pressed or captured widget. */
void ex_ui_release(ExUi *ui, ExWidget *subtree);
ExWidget *ex_ui_focus(const ExUi *ui);
void ex_ui_set_focus(ExUi *ui, ExWidget *w);

/* Mirrors the widget tree into an accessibility tree: one node for each widget (the
 * node id is the widget id), with its role, name, bounds, actions and, for editors,
 * the text. Nodes of widgets that no longer exist are removed and the focus follows
 * the UI's focus. Call it after layout whenever the UI changed. Returns 0. */
int ex_ui_a11y_sync(ExUi *ui, ExWidget *root, ExA11yTree *tree);

/* The sample window: ribbon with a theme switch button, the note editor on doc, and a
 * status line. The demo program and the accessibility and keyboard tests all use it,
 * so what the tests check is what the program shows. toggle_theme may be NULL. */
ExWidget *ex_sample_window(ExUi *ui, ExDoc *doc, ExClickFn toggle_theme, void *user, ExWidget **editor_out);

/* Number of widgets in a tree. */
size_t ex_widget_count(const ExWidget *root);

#ifdef __cplusplus
}
#endif
#endif
