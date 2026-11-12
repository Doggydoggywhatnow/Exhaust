/* editing_shortcuts: every default editing shortcut in docs/formats.md (Windows and Linux, and macOS) maps to the action
 * its row describes. The tables of the document are read, so a row added there without an expectation here fails.
 * Also: keys on other keyboard layouts (a letter typed with a non-Latin layout is found by its position). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_editor.h"
#include "exhaust_input.h"
#include "exhaust_platform.h"

static int failures, checked;

static int has(const char *s, const char *needle) { return strstr(s, needle) != NULL; }

/* The action a row of the document describes. *ext is set for rows that extend the selection; *either is set for "one character" (left or right). */
static ExEditAction expected(const char *label, int *ext, int *either) {
    *ext = strncmp(label, "Extend selection", 16) == 0;
    *either = 0;
    if (!strcmp(label, "Copy") || !strcmp(label, "Copy paragraph")) return EX_ACT_COPY;
    if (!strcmp(label, "Cut") || !strcmp(label, "Cut paragraph")) return EX_ACT_CUT;
    if (!strcmp(label, "Paste")) return EX_ACT_PASTE;
    if (!strcmp(label, "Paste without formatting")) return EX_ACT_PASTE_PLAIN;
    if (!strcmp(label, "Undo")) return EX_ACT_UNDO;
    if (!strcmp(label, "Redo")) return EX_ACT_REDO;
    if (!strcmp(label, "Bold text")) return EX_ACT_BOLD;
    if (!strcmp(label, "Italic text")) return EX_ACT_ITALIC;
    if (!strncmp(label, "Insert new line", 15)) return EX_ACT_NEWLINE;
    if (!strcmp(label, "Delete the previous character")) return EX_ACT_DELETE_BACK;
    if (!strcmp(label, "Delete the next character")) return EX_ACT_DELETE_FORWARD;
    if (!strcmp(label, "Delete the previous word")) return EX_ACT_DELETE_WORD_BACK;
    if (!strcmp(label, "Delete the next word")) return EX_ACT_DELETE_WORD_FORWARD;
    if (!strcmp(label, "Delete to the beginning of the current line")) return EX_ACT_DELETE_TO_LINE_START;
    if (!strcmp(label, "Delete to the end of the current line")) return EX_ACT_DELETE_TO_LINE_END;
    if (!strcmp(label, "Delete the current line")) return EX_ACT_DELETE_LINE;
    if (!strcmp(label, "Simplify selection")) return EX_ACT_SIMPLIFY;
    if (!strcmp(label, "Select all")) return EX_ACT_SELECT_ALL;
    if (has(label, "one character")) { *either = 1; return EX_ACT_LEFT; }
    if (has(label, "previous word")) return EX_ACT_WORD_LEFT;
    if (has(label, "next word")) return EX_ACT_WORD_RIGHT;
    if (has(label, "beginning of the current line")) return EX_ACT_LINE_START;
    if (has(label, "end of the current line")) return EX_ACT_LINE_END;
    if (has(label, "previous line")) return EX_ACT_UP;
    if (has(label, "next line")) return EX_ACT_DOWN;
    if (has(label, "beginning of the note")) return EX_ACT_DOC_START;
    if (has(label, "end of the note")) return EX_ACT_DOC_END;
    if (has(label, "one page up") || has(label, "up one page")) return EX_ACT_PAGE_UP;
    if (has(label, "one page down") || has(label, "down one page")) return EX_ACT_PAGE_DOWN;
    return EX_ACT_NONE;
}

/* "Ctrl+Shift+Z" to a key and modifiers; returns 0 if the text is not understood. */
static int parse_shortcut(const char *s, int mac, ExKey *key, int *mods) {
    char buf[64], *p, *part;
    size_t n = strlen(s);
    int fn = 0;
    *mods = 0;
    *key = EX_KEY_NONE;
    if (n >= sizeof buf) return 0;
    memcpy(buf, s, n + 1);
    for (p = buf; ; ) {
        char *plus = strchr(p, '+');
        part = p;
        if (!plus || !plus[1]) break;                      /* the last part is the key ("+" itself is not used) */
        *plus = 0;
        if (!strcmp(part, "Shift")) *mods |= EX_MOD_SHIFT;
        else if (!strcmp(part, "Ctrl")) *mods |= mac ? EX_MOD_CTRL : EX_MOD_PRIMARY;
        else if (!strcmp(part, "Cmd") && mac) *mods |= EX_MOD_PRIMARY;
        else if ((!strcmp(part, "Alt") && !mac) || (!strcmp(part, "Option") && mac)) *mods |= EX_MOD_ALT;
        else if (!strcmp(part, "Fn") && mac) fn = 1;
        else return 0;
        p = plus + 1;
    }
    part = p;
    if (!strcmp(part, "Left") || !strcmp(part, "\xE2\x86\x90")) *key = EX_KEY_LEFT;
    else if (!strcmp(part, "\xE2\x86\x92")) *key = EX_KEY_RIGHT;
    else if (!strcmp(part, "\xE2\x86\x91")) *key = EX_KEY_UP;
    else if (!strcmp(part, "\xE2\x86\x93")) *key = EX_KEY_DOWN;
    else if (!strcmp(part, "Home")) *key = EX_KEY_HOME;
    else if (!strcmp(part, "End")) *key = EX_KEY_END;
    else if (!strcmp(part, "Page up")) *key = EX_KEY_PAGE_UP;
    else if (!strcmp(part, "Page down")) *key = EX_KEY_PAGE_DOWN;
    else if (!strcmp(part, "Backspace")) *key = EX_KEY_BACKSPACE;
    else if (!strcmp(part, "Delete")) *key = EX_KEY_DELETE;
    else if (!strcmp(part, "Escape")) *key = EX_KEY_ESCAPE;
    else if (!strcmp(part, "Enter")) *key = EX_KEY_ENTER;
    else if (strlen(part) == 1) {
        switch (part[0]) {
        case 'A': *key = EX_KEY_A; break; case 'B': *key = EX_KEY_B; break; case 'C': *key = EX_KEY_C; break;
        case 'I': *key = EX_KEY_I; break; case 'K': *key = EX_KEY_K; break; case 'V': *key = EX_KEY_V; break;
        case 'X': *key = EX_KEY_X; break; case 'Y': *key = EX_KEY_Y; break; case 'Z': *key = EX_KEY_Z; break;
        default: return 0;
        }
    } else return 0;
    if (fn) {                                              /* Fn+Up and Fn+Down are the Page Up and Page Down keys of a Mac laptop */
        if (*key == EX_KEY_UP) *key = EX_KEY_PAGE_UP;
        else if (*key == EX_KEY_DOWN) *key = EX_KEY_PAGE_DOWN;
        else return 0;
    }
    return *key != EX_KEY_NONE;
}

static void check_row(const char *label, const char *cell, int mac) {
    int ext, either, k;
    ExEditAction want = expected(label, &ext, &either);
    char tmp[256], *tok;
    size_t n = strlen(cell);
    if (n >= sizeof tmp) return;
    memcpy(tmp, cell, n + 1);
    if (want == EX_ACT_NONE) { failures++; fprintf(stderr, "no expectation for the row \"%s\"\n", label); return; }
    /* every backquoted token of the cell is one way to do it */
    for (tok = strchr(tmp, '`'); tok; ) {
        char *end = strchr(tok + 1, '`'), *slash;
        char alt[2][64];
        int nalt = 1;
        if (!end) break;
        *end = 0;
        snprintf(alt[0], sizeof alt[0], "%s", tok + 1);
        slash = strchr(alt[0], '/');
        if (slash) {                                       /* "Shift+Left/\xE2\x86\x92" is two shortcuts */
            char prefix[64];
            size_t pl = 0, i;
            *slash = 0;
            for (i = 0; alt[0][i]; i++) if (alt[0][i] == '+') pl = i + 1;
            memcpy(prefix, alt[0], pl); prefix[pl] = 0;
            snprintf(alt[1], sizeof alt[1], "%s%s", prefix, slash + 1);
            nalt = 2;
        }
        for (k = 0; k < nalt; k++) {
            ExKey key;
            int mods, got_ext = 0;
            ExEditAction got = EX_ACT_NONE;
            checked++;
            if (!parse_shortcut(alt[k], mac, &key, &mods)) { failures++; fprintf(stderr, "cannot read the shortcut \"%s\" (row \"%s\")\n", alt[k], label); continue; }
            if (!ex_edit_action_for(key, mods, mac, &got, &got_ext)) { failures++; fprintf(stderr, "%s \"%s\": %s is not an editing key\n", mac ? "macOS" : "Windows/Linux", label, alt[k]); continue; }
            if (either ? (got != EX_ACT_LEFT && got != EX_ACT_RIGHT) : got != want) {
                failures++;
                fprintf(stderr, "%s \"%s\" (%s): got %s, wanted %s\n", mac ? "macOS" : "Windows/Linux", label, alt[k], ex_edit_action_name(got), ex_edit_action_name(want));
            }
            if (got_ext != ext) { failures++; fprintf(stderr, "%s \"%s\" (%s): extend is %d, wanted %d\n", mac ? "macOS" : "Windows/Linux", label, alt[k], got_ext, ext); }
        }
        tok = strchr(end + 1, '`');
    }
}

int main(int argc, char **argv) {
    size_t len = 0, rows = 0;
    char *doc, *p, *line;
    int mac = -1;
    if (argc < 2) { fprintf(stderr, "usage: test_shortcuts formats.md\n"); return 2; }
    doc = ex_platform_read_file(argv[1], &len);
    if (!doc) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    p = strstr(doc, "## Default editing shortcuts");
    if (!p) { fprintf(stderr, "no section\n"); return 1; }
    for (line = strtok(p, "\n"); line; line = strtok(NULL, "\n")) {
        char label[160], *c1, *c2, *end;
        if (!strncmp(line, "### Windows and Linux shortcuts", 31)) { mac = 0; continue; }
        if (!strncmp(line, "### macOS shortcuts", 19)) { mac = 1; continue; }
        if (!strncmp(line, "## ", 3) && !strstr(line, "Default editing shortcuts")) break;
        if (mac < 0 || line[0] != '|') continue;
        c1 = line + 1;
        c2 = strchr(c1, '|');
        if (!c2) continue;
        *c2++ = 0;
        end = strchr(c2, '|');
        if (end) *end = 0;
        while (*c1 == ' ') c1++;
        { size_t n = strlen(c1); while (n && c1[n - 1] == ' ') n--; if (n >= sizeof label) n = sizeof label - 1; memcpy(label, c1, n); label[n] = 0; }
        if (!strcmp(label, "Action") || label[0] == '-' || !strchr(c2, '`')) continue;
        rows++;
        check_row(label, c2, mac);
    }
    free(doc);
    if (rows < 60) { failures++; fprintf(stderr, "only %zu rows read\n", rows); }
    /* other keyboard layouts: letters found by position, text typed with the layout's own characters */
    {
        ExKey key;
        if (ex_key_from_keycode(0x61 /* a */, 4 /* scancode A */, &key) != 1 || key != EX_KEY_A) { failures++; fputs("a\n", stderr); }
        if (ex_key_from_keycode(0x43F /* Cyrillic pe */, 19 /* the P position */, &key) != 0) { failures++; fputs("P is not a shortcut key\n", stderr); }
        if (ex_key_from_keycode(0x441 /* Cyrillic es */, 6 /* the C position */, &key) != 1 || key != EX_KEY_C) { failures++; fputs("Cyrillic C position\n", stderr); }
        if (ex_key_from_keycode(0x79 /* y */, 28 /* the Y position on QWERTY */, &key) != 1 || key != EX_KEY_Y) { failures++; fputs("y\n", stderr); }
        if (ex_key_from_keycode(0x7a /* z typed on QWERTZ where the Z position holds y */, 28, &key) != 1 || key != EX_KEY_Z) { failures++; fputs("QWERTZ z\n", stderr); }
        if (ex_key_from_keycode(0x61 /* AZERTY a at the Q position */, 20, &key) != 1 || key != EX_KEY_A) { failures++; fputs("AZERTY a\n", stderr); }
    }
    printf("%s (%zu rows, %d shortcuts)\n", failures ? "FAIL" : "ok", rows, checked);
    return failures ? 1 : 0;
}
