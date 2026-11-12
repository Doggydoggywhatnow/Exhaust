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
    EX_KEY_BACKSPACE, EX_KEY_DELETE, EX_KEY_ENTER, EX_KEY_SPACE, EX_KEY_TAB, EX_KEY_ESCAPE,
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

/* How the text is shown. PLAIN is one font and one color (what fields and diffs use). SOURCE shows the
 * Markdown unchanged, with its syntax in quieter colors and headings, code and links styled. LIVE
 * (live preview) also hides the syntax of every construct the cursor is not in and shows bullets,
 * checkboxes and quote bars in its place; math, embeds, images and tables stay as source until they
 * have renderers. The text itself is never changed by the mode. */
typedef enum ExEditorMode { EX_EDITOR_PLAIN, EX_EDITOR_SOURCE, EX_EDITOR_LIVE } ExEditorMode;

/* The editor edits doc but does not own it. */
ExEditor *ex_editor_create(ExDoc *doc);
void ex_editor_free(ExEditor *e);
ExDoc *ex_editor_doc(ExEditor *e);
/* Shows another document (not owned). Selections of the old one are forgotten. */
void ex_editor_set_doc(ExEditor *e, ExDoc *doc);
/* A read-only editor moves, selects and copies but ignores every change. Until notes
 * are saved (sub-slice 2b) the demo window opens real files this way, so typed text
 * can never be lost. */
void ex_editor_set_readonly(ExEditor *e, int readonly);
int ex_editor_readonly(const ExEditor *e);

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
void ex_editor_set_mode(ExEditor *e, ExEditorMode mode);
ExEditorMode ex_editor_mode(const ExEditor *e);
/* The text a line shows in the current mode (hidden syntax left out, stand-ins written as the characters they
 * look like: a bullet, a box), for tests and for screen readers. Returns the length. */
size_t ex_editor_line_display(ExEditor *e, ExRenderer *r, size_t line, char *out, size_t cap);
/* The Markdown tokens of the document as the editor last saw them (NULL in plain mode). */
const struct ExMd *ex_editor_tokens(ExEditor *e);
/* The primary caret as a line and the byte offset in that line (both 0-based), and the scroll
 * position, for the session file. Setting clamps to the text and never lands inside a character:
 * a note that changed since the session was saved still opens near the same place. */
void ex_editor_get_state(const ExEditor *e, int *line, int *column, float *scroll_y);
void ex_editor_set_state(ExEditor *e, int line, int column, float scroll_y);
/* Text dropped at a window position: the caret goes there, then the text is typed (one undo step).
 * A read-only editor ignores it. */
void ex_editor_drop_text(ExEditor *e, ExRenderer *r, float x, float y, const char *utf8, size_t len);

#ifdef __cplusplus
}
#endif
#endif
