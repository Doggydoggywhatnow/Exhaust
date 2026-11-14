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
    EX_KEY_A, EX_KEY_B, EX_KEY_C, EX_KEY_I, EX_KEY_K, EX_KEY_V, EX_KEY_X, EX_KEY_Y, EX_KEY_Z
} ExKey;

/* Modifiers. PRIMARY is Ctrl on Windows and Linux and Cmd on macOS. */
enum { EX_MOD_SHIFT = 1, EX_MOD_PRIMARY = 2, EX_MOD_ALT = 4,
       EX_MOD_CTRL = 8 };   /* the Control key on macOS, where PRIMARY is Cmd and ALT is Option; never set elsewhere */

/* What a key press does in the text (the default editing shortcuts in docs/formats.md). Moves and selection
 * changes carry `extend`: the Shift key keeps the anchor. Copy and cut with nothing selected act on the
 * current line (a paragraph is one line of the note). */
typedef enum ExEditAction {
    EX_ACT_NONE = 0,
    EX_ACT_COPY, EX_ACT_CUT, EX_ACT_PASTE, EX_ACT_PASTE_PLAIN, EX_ACT_UNDO, EX_ACT_REDO,
    EX_ACT_DELETE_BACK, EX_ACT_DELETE_FORWARD, EX_ACT_DELETE_WORD_BACK, EX_ACT_DELETE_WORD_FORWARD,
    EX_ACT_DELETE_TO_LINE_START, EX_ACT_DELETE_TO_LINE_END, EX_ACT_DELETE_LINE,
    EX_ACT_LEFT, EX_ACT_RIGHT, EX_ACT_WORD_LEFT, EX_ACT_WORD_RIGHT, EX_ACT_LINE_START, EX_ACT_LINE_END,
    EX_ACT_UP, EX_ACT_DOWN, EX_ACT_DOC_START, EX_ACT_DOC_END, EX_ACT_PAGE_UP, EX_ACT_PAGE_DOWN,
    EX_ACT_SELECT_ALL, EX_ACT_SIMPLIFY, EX_ACT_NEWLINE, EX_ACT_TAB,
    EX_ACT_BOLD, EX_ACT_ITALIC, EX_ACT_ADD_CURSOR_ABOVE, EX_ACT_ADD_CURSOR_BELOW,
    EX_ACT_COUNT
} ExEditAction;
const char *ex_edit_action_name(ExEditAction a);
/* The action of a key press on Windows and Linux (mac == 0) or on macOS (mac != 0); returns 0 if the key is not
 * an editing key there. *extend is set for moves that extend the selection. */
int ex_edit_action_for(ExKey key, int mods, int mac, ExEditAction *action, int *extend);

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

/* Rectangular selection: one selection on every line from the line at (ax, ay) to the line at (x, y) (window pixels),
 * each covering the same horizontal span (shorter lines get a caret at their end). Mouse: drag with Alt+Shift held. */
void ex_editor_rect_select(ExEditor *e, ExRenderer *r, float ax, float ay, float x, float y);

/* The slash menu (sub-slice 3d): when a menu is set, typing "/" at the start of a line or after a space opens a list of
 * editor commands under the caret; what is typed after the slash narrows it (fuzzy), Up and Down move, Enter or Tab or a
 * click removes the "/..." and runs the command, Escape or a space closes it. NULL turns it off (not owned). */
struct ExSlashMenu;
void ex_editor_set_slash(ExEditor *e, struct ExSlashMenu *menu);
int ex_editor_slash_open(const ExEditor *e);

/* Link suggestions (sub-slice 4c): typing "[[" opens a list of notes under the caret; what is typed after it narrows the list
 * (the application's function decides how: names, aliases, "Note#" for headings, "Note#^" for blocks). Up and Down move, Enter
 * or Tab or a click inserts the item and closes the brackets, Escape, "]" or "|" or a line break closes the list. NULL: off.
 * Typing "](" (after a Markdown link's text) opens the same list for the address: the function is called with the query prefixed by the
 * character '\x02' and answers with paths from the vault; accepting inserts the path and ")". */
typedef struct ExLinkSuggestion { char label[160]; char insert[200]; char detail[64]; } ExLinkSuggestion;
typedef size_t (*ExLinkSuggestFn)(void *user, const char *query, ExLinkSuggestion *out, size_t max);
void ex_editor_set_link_suggest(ExEditor *e, ExLinkSuggestFn fn, void *user);
int ex_editor_link_suggest_open(const ExEditor *e);
/* The target of the wikilink or Markdown link at a window position, for the page preview: path part, heading and block (each may
 * be empty). Returns 1 if there is a link there. */
/* Ctrl (Command on a Mac) and click on a link in the text: the application follows it, or creates the note it names. A plain
 * click only places the caret. target, heading and block are as in ex_editor_link_at. */
typedef void (*ExEditorLinkClickFn)(void *user, const char *target, const char *heading, const char *block);
void ex_editor_set_link_click(ExEditor *e, ExEditorLinkClickFn fn, void *user);
int ex_editor_link_at(ExEditor *e, ExRenderer *r, float x, float y, char *target, size_t tcap, char *heading, size_t hcap, char *block, size_t bcap);

/* Spell check: words the checker does not know get a wavy line under them in source mode and live preview (not in code,
 * math, links' addresses, tags, front matter, HTML or the word the caret is in). NULL turns it off; the checker is not owned.
 * Call ex_editor_spell_changed after words were added to it. */
struct ExSpell;
void ex_editor_set_spell(ExEditor *e, struct ExSpell *spell);
void ex_editor_spell_changed(ExEditor *e);
/* The word around the primary caret (letters and inner apostrophes and hyphens), as a byte range; 0 if the caret is not in one. */
int ex_editor_word_at_caret(ExEditor *e, size_t *start, size_t *end);

/* Find matches: the editor draws every match of `find` (not owned; NULL clears) in the highlight color. */
struct ExFind;
void ex_editor_set_find(ExEditor *e, struct ExFind *find);
/* Selects [a, b) (a is the anchor), scrolls to show it and opens folds that hide it. */
void ex_editor_select(ExEditor *e, ExRenderer *r, size_t a, size_t b);

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
/* Does an action as if its key had been pressed (the commands use it for adding cursors). */
void ex_editor_perform(ExEditor *e, ExRenderer *r, ExEditAction act, int extend);
/* The text a line shows in the current mode (hidden syntax left out, stand-ins written as the characters they
 * look like: a bullet, a box), for tests and for screen readers. Returns the length. */
size_t ex_editor_line_display(ExEditor *e, ExRenderer *r, size_t line, char *out, size_t cap);
/* The Markdown tokens of the document as the editor last saw them (NULL in plain mode). */
const struct ExMd *ex_editor_tokens(ExEditor *e);
/* Text formatting commands (the editor commands of the command registry, sub-slice 3d). Each one acts on every
 * selection, is one undo step and returns through ex_doc_format 1 if the text changed. Inline toggles wrap the selection
 * (or put the markers around the caret) or take the markers off when they are already there; line commands act on every
 * line the selections touch. */
typedef enum ExFormat {
    EX_FMT_BOLD, EX_FMT_ITALIC, EX_FMT_STRIKE, EX_FMT_HIGHLIGHT, EX_FMT_CODE, EX_FMT_INLINE_MATH, EX_FMT_COMMENT,
    EX_FMT_HEADING_CYCLE,                      /* none, 1, 2 ... 6, none */
    EX_FMT_HEADING_1, EX_FMT_HEADING_2, EX_FMT_HEADING_3, EX_FMT_HEADING_4, EX_FMT_HEADING_5, EX_FMT_HEADING_6,   /* again on the same level: off */
    EX_FMT_REMOVE_HEADING,
    EX_FMT_BULLET, EX_FMT_NUMBERED, EX_FMT_QUOTE, EX_FMT_CHECKBOX_STATUS, EX_FMT_CYCLE_BULLET_CHECKBOX,
    EX_FMT_INDENT, EX_FMT_UNINDENT, EX_FMT_MOVE_LINE_UP, EX_FMT_MOVE_LINE_DOWN, EX_FMT_DELETE_PARAGRAPH,
    EX_FMT_HR, EX_FMT_CODE_BLOCK, EX_FMT_CALLOUT, EX_FMT_MATH_BLOCK, EX_FMT_TABLE, EX_FMT_FOOTNOTE,
    EX_FMT_MD_LINK, EX_FMT_INTERNAL_LINK, EX_FMT_EMBED, EX_FMT_TAG, EX_FMT_CLEAR,
    EX_FMT_COUNT
} ExFormat;
typedef struct ExFormatOptions { int use_tabs; int indent_width; } ExFormatOptions;   /* the settings indent_with_tabs and indent_width */
int ex_doc_format(ExDoc *d, ExFormat f, const ExFormatOptions *opt);
void ex_editor_format(ExEditor *e, ExFormat f);
void ex_editor_set_indent(ExEditor *e, int use_tabs, int indent_width);

/* Folding of headings and list items (sub-slice 3c), in source mode and live preview. A heading folds the lines up to
 * the next heading of the same or a higher level, a list item its continuation lines and nested items. Folds are a
 * view of the text: the text is never changed, a caret that lands in hidden text opens the fold, and the folded
 * regions follow the edits (a region that no longer exists is shown again). They are not saved in the session. */
typedef enum ExFoldCmd { EX_FOLD_ALL, EX_FOLD_UNFOLD_ALL, EX_FOLD_MORE, EX_FOLD_LESS, EX_FOLD_TOGGLE_LINE } ExFoldCmd;
/* Which kinds of regions can be folded (the settings editor.fold_heading and editor.fold_indent). */
void ex_editor_set_folding(ExEditor *e, int headings, int lists);
/* MORE folds the regions of the deepest level that are shown and open; LESS opens the shallowest folded level. */
void ex_editor_fold(ExEditor *e, ExFoldCmd cmd);
size_t ex_editor_fold_count(ExEditor *e);      /* folded regions that hide something now */
size_t ex_editor_region_count(ExEditor *e);    /* regions that could be folded */
int ex_editor_line_hidden(ExEditor *e, size_t line);
size_t ex_editor_row_count(ExEditor *e);       /* lines shown: all lines minus the hidden ones */
size_t ex_editor_row_of_line(ExEditor *e, size_t line);
/* Folds or opens the region whose header is this line; 0 if the line is not a header. */
int ex_editor_toggle_fold_at_line(ExEditor *e, size_t line);

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
