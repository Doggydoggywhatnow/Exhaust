/* Exhaust's widget tree. Widgets are plain structs in a tree; each frame the tree
 * is turned into a Clay layout (the layout engine), and the resulting rectangles
 * are stored back in the widgets for drawing and hit testing. Every widget has a
 * role and a name for the accessibility tree (sub-slice 1b). Colors, sizes and
 * fonts come only from theme tokens. */
#ifndef EXHAUST_UI_H
#define EXHAUST_UI_H

#include "exhaust_a11y.h"
#include "exhaust_editor.h"
#include "exhaust_vault.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExWidgetType { EX_WIDGET_BOX, EX_WIDGET_LABEL, EX_WIDGET_BUTTON, EX_WIDGET_EDITOR, EX_WIDGET_TREE, EX_WIDGET_TYPE_COUNT } ExWidgetType;

typedef enum ExRole { EX_ROLE_WINDOW, EX_ROLE_GROUP, EX_ROLE_LABEL, EX_ROLE_BUTTON, EX_ROLE_TEXT_INPUT, EX_ROLE_TREE } ExRole;

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

    /* behaviour */
    int focusable;
    int hover, pressed;           /* set by the UI from the mouse */
    ExClickFn on_click;
    void *on_click_user;
    ExEditor *editor;             /* EX_WIDGET_EDITOR */
    ExVault *vault;               /* EX_WIDGET_TREE: the vault whose rows it shows (not owned) */
    size_t tree_selected;         /* selected row */
    float tree_scroll;            /* pixels */
    void (*on_tree_open)(ExWidget *w, const char *rel, void *user); /* Enter or double click on a file */
    void *on_tree_open_user;
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
ExWidget *ex_widget_editor(ExUi *ui, ExDoc *doc, const char *name);
/* The file explorer's tree: shows ex_vault_row() rows with chevrons, keeps a selection,
 * and calls on_open for a file. Up and Down move the selection, Right and Left open
 * and close folders (Left on a closed folder or a file goes to its folder), Enter opens. */
ExWidget *ex_widget_tree(ExUi *ui, ExVault *vault, const char *name,
                         void (*on_open)(ExWidget *w, const char *rel, void *user), void *user);
void ex_tree_select_path(ExWidget *tree, const char *rel);          /* reveals and selects */
const ExNode *ex_tree_selected(ExWidget *tree);
/* The explorer sidebar of the screenshots: a toolbar (new note, new folder, expand or
 * collapse all, sort), the tree, and the vault button at the bottom. */
ExWidget *ex_explorer_sidebar(ExUi *ui, ExVault *vault, void (*on_open)(ExWidget *w, const char *rel, void *user), void *user,
                              ExWidget **tree_out);
void ex_widget_insert_before(ExWidget *parent, ExWidget *child, ExWidget *before);
void ex_widget_add(ExWidget *parent, ExWidget *child);
void ex_widget_set_text(ExWidget *w, const char *text);
void ex_widget_free(ExWidget *root); /* frees the tree; editors are freed with it, documents are not */

/* Convenience for axis sizes. */
ExAxis ex_size_grow(void);
ExAxis ex_size_fit(void);
ExAxis ex_size_token(int size_token, float multiple);
ExAxis ex_size_percent(float fraction);

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
