#include "exhaust_editor.h"

static const char *const NAMES[EX_ACT_COUNT] = {
    "none", "copy", "cut", "paste", "paste-plain", "undo", "redo",
    "delete-back", "delete-forward", "delete-word-back", "delete-word-forward",
    "delete-to-line-start", "delete-to-line-end", "delete-line",
    "left", "right", "word-left", "word-right", "line-start", "line-end",
    "up", "down", "doc-start", "doc-end", "page-up", "page-down",
    "select-all", "simplify", "newline", "tab",
    "bold", "italic", "add-cursor-above", "add-cursor-below"
};

const char *ex_edit_action_name(ExEditAction a) { return a >= 0 && a < EX_ACT_COUNT ? NAMES[a] : "none"; }

int ex_edit_action_for(ExKey key, int mods, int mac, ExEditAction *action, int *extend) {
    int shift = (mods & EX_MOD_SHIFT) != 0, prim = (mods & EX_MOD_PRIMARY) != 0, alt = (mods & EX_MOD_ALT) != 0;
    int ctrl = (mods & EX_MOD_CTRL) != 0;                     /* macOS only */
    ExEditAction a = EX_ACT_NONE;
    int ext = 0;
    switch (key) {
    case EX_KEY_LEFT: case EX_KEY_RIGHT: {
        int right = key == EX_KEY_RIGHT;
        ext = shift;
        if (mac && prim) a = right ? EX_ACT_LINE_END : EX_ACT_LINE_START;               /* Cmd+Left, Cmd+Right */
        else if (prim || alt) a = right ? EX_ACT_WORD_RIGHT : EX_ACT_WORD_LEFT;         /* Ctrl or Option (Alt elsewhere too) */
        else a = right ? EX_ACT_RIGHT : EX_ACT_LEFT;
        break;
    }
    case EX_KEY_UP: case EX_KEY_DOWN: {
        int down = key == EX_KEY_DOWN;
        ext = shift;
        if (prim && alt && !mac) { a = down ? EX_ACT_ADD_CURSOR_BELOW : EX_ACT_ADD_CURSOR_ABOVE; ext = 0; }
        else if (mac && prim && alt) { a = down ? EX_ACT_ADD_CURSOR_BELOW : EX_ACT_ADD_CURSOR_ABOVE; ext = 0; }
        else if (mac && prim) a = down ? EX_ACT_DOC_END : EX_ACT_DOC_START;             /* Cmd+Up, Cmd+Down */
        else if (mac && ctrl && shift) a = down ? EX_ACT_PAGE_DOWN : EX_ACT_PAGE_UP;    /* Ctrl+Shift+Up, Ctrl+Shift+Down */
        else a = down ? EX_ACT_DOWN : EX_ACT_UP;
        break;
    }
    case EX_KEY_HOME: case EX_KEY_END: {
        int end = key == EX_KEY_END;
        ext = shift;
        a = prim ? (end ? EX_ACT_DOC_END : EX_ACT_DOC_START) : (end ? EX_ACT_LINE_END : EX_ACT_LINE_START);
        break;
    }
    case EX_KEY_PAGE_UP: a = EX_ACT_PAGE_UP; ext = shift; break;
    case EX_KEY_PAGE_DOWN: a = EX_ACT_PAGE_DOWN; ext = shift; break;
    case EX_KEY_BACKSPACE:
        if (mac) a = prim ? EX_ACT_DELETE_TO_LINE_START : alt ? EX_ACT_DELETE_WORD_BACK : EX_ACT_DELETE_BACK;
        else a = (prim || alt) ? EX_ACT_DELETE_WORD_BACK : EX_ACT_DELETE_BACK;
        break;
    case EX_KEY_DELETE:
        if (mac) a = prim ? EX_ACT_DELETE_TO_LINE_END : alt ? EX_ACT_DELETE_WORD_FORWARD : EX_ACT_DELETE_FORWARD;
        else a = (prim || alt) ? EX_ACT_DELETE_WORD_FORWARD : EX_ACT_DELETE_FORWARD;
        break;
    case EX_KEY_ENTER: a = EX_ACT_NEWLINE; break;
    case EX_KEY_TAB: a = EX_ACT_TAB; break;
    case EX_KEY_ESCAPE: a = EX_ACT_SIMPLIFY; break;
    case EX_KEY_SPACE: break;                                  /* typed through text input */
    case EX_KEY_A: if (prim) a = EX_ACT_SELECT_ALL; break;
    case EX_KEY_B: if (prim) a = EX_ACT_BOLD; break;
    case EX_KEY_I: if (prim) a = EX_ACT_ITALIC; break;
    case EX_KEY_C: if (prim) a = EX_ACT_COPY; break;
    case EX_KEY_X: if (prim) a = EX_ACT_CUT; break;
    case EX_KEY_V: if (prim) a = shift ? EX_ACT_PASTE_PLAIN : EX_ACT_PASTE; break;
    case EX_KEY_K: if (prim && shift) a = EX_ACT_DELETE_LINE; break;
    case EX_KEY_Z: if (prim) a = shift ? EX_ACT_REDO : EX_ACT_UNDO; break;
    case EX_KEY_Y: if (prim) a = EX_ACT_REDO; break;
    default: break;
    }
    if (a == EX_ACT_NONE) return 0;
    *action = a;
    *extend = ext;
    return 1;
}
