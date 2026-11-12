/* The editor widget: draws and edits an ExDoc. It handles text input, keys and
 * the mouse as plain data (no SDL types), so tests drive it with scripted input.
 * Colors come from a theme; it holds no color of its own.
 *
 * Lines are not wrapped yet (soft wrap arrives with the Markdown editor in 3b).
 * Selections inside right-to-left text are drawn as one block from the start to
 * the end position. */
#ifndef EXHAUST_EDITOR_H
#define EXHAUST_EDITOR_H

#include "exhaust_buffer.h"
#include "exhaust_render.h"
#include "exhaust_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExKey {
    EX_KEY_NONE = 0,
    EX_KEY_LEFT, EX_KEY_RIGHT, EX_KEY_UP, EX_KEY_DOWN,
    EX_KEY_HOME, EX_KEY_END, EX_KEY_PAGE_UP, EX_KEY_PAGE_DOWN,
    EX_KEY_BACKSPACE, EX_KEY_DELETE, EX_KEY_ENTER, EX_KEY_TAB, EX_KEY_ESCAPE,
    EX_KEY_A, EX_KEY_C, EX_KEY_V, EX_KEY_X, EX_KEY_Y, EX_KEY_Z
} ExKey;

/* Modifiers. PRIMARY is Ctrl on Windows and Linux and Cmd on macOS. */
enum { EX_MOD_SHIFT = 1, EX_MOD_PRIMARY = 2, EX_MOD_ALT = 4 };

typedef enum ExMouse { EX_MOUSE_DOWN, EX_MOUSE_DRAG, EX_MOUSE_UP } ExMouse;

typedef struct ExClipboard {
    char *(*get)(void *user);              /* malloc'ed text or NULL */
    void (*set)(void *user, const char *); /* copies the text */
    void *user;
} ExClipboard;

typedef struct ExEditor ExEditor;

/* The editor edits doc but does not own it. */
ExEditor *ex_editor_create(ExDoc *doc);
void ex_editor_free(ExEditor *e);
ExDoc *ex_editor_doc(ExEditor *e);
/* Defaults to the operating system clipboard. */
void ex_editor_set_clipboard(ExEditor *e, const ExClipboard *cb);

/* Font and metrics. size is at 100% zoom, line_height_pct is the theme's
 * line_height token. The renderer supplies the scale. */
void ex_editor_set_font(ExEditor *e, ExFont *font, float size, int line_height_pct);
/* Placement of the text area in pixels. The editor scrolls inside it. */
void ex_editor_set_viewport(ExEditor *e, float x, float y, float w, float h);

/* Input. */
void ex_editor_text_input(ExEditor *e, const char *utf8, size_t len);
void ex_editor_set_composition(ExEditor *e, const char *utf8); /* IME text not yet committed; "" clears */
void ex_editor_key(ExEditor *e, ExRenderer *r, ExKey key, int mods);
/* clicks counts rapid presses at the same place: 1, 2 (word), 3 (line). */
void ex_editor_mouse(ExEditor *e, ExRenderer *r, ExMouse kind, float x, float y, int mods, int clicks);
void ex_editor_scroll(ExEditor *e, float dy_pixels);

/* Text of the selections, joined with line breaks; malloc'ed. */
char *ex_editor_selected_text(ExEditor *e, size_t *len);

/* Drawing. focused shows the carets; blink_on lets the caller blink them. */
void ex_editor_draw(ExEditor *e, ExRenderer *r, const ExTheme *t, int focused, int blink_on);

/* Geometry for tests and for placing the IME candidate window: the caret of the
 * primary selection in pixels (window coordinates), after the last draw or layout. */
void ex_editor_caret_rect(ExEditor *e, ExRenderer *r, float *x, float *y, float *w, float *h);
float ex_editor_scroll_y(const ExEditor *e);

#ifdef __cplusplus
}
#endif
#endif
