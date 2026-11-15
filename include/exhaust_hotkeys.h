/* Hotkeys (sub-slice 7c): the shortcuts of the commands, `hotkeys.ini` and the matching of a key press.
 *
 * A shortcut is text (`Ctrl+Shift+T`, `Mod+E`, `Alt+Enter`, `F5`, `Ctrl+,`); see "hotkeys.ini" in docs/formats.md. It is parsed into a
 * chord (modifier bits and a key code) and compared as a chord, so `Shift+Ctrl+t` and `Ctrl+Shift+T` are the same shortcut and a duplicate
 * is found. A command's shortcuts are its defaults from the command table, unless hotkeys.ini has a line for it: then only that line's
 * shortcuts count (an empty value is "no shortcut"). Only changes from the defaults are kept in the file: a command whose list goes back to
 * its defaults loses its line. Everything else in the file (comments, other sections, lines for ids this version does not know) is kept
 * byte for byte, and a file saved by a newer version is never discarded. Writes happen on a worker thread, atomically. */
#ifndef EXHAUST_HOTKEYS_H
#define EXHAUST_HOTKEYS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { EX_HK_CTRL = 1, EX_HK_SHIFT = 2, EX_HK_ALT = 4, EX_HK_META = 8 };

/* Key codes: a printable key is its character in lower case ('a', '5', ','); the named keys are above 256. */
enum {
    EX_HK_ENTER = 0x101, EX_HK_TAB, EX_HK_ESCAPE, EX_HK_SPACE, EX_HK_BACKSPACE, EX_HK_DELETE, EX_HK_INSERT, EX_HK_HOME, EX_HK_END,
    EX_HK_PAGEUP, EX_HK_PAGEDOWN, EX_HK_LEFT, EX_HK_RIGHT, EX_HK_UP, EX_HK_DOWN,
    EX_HK_F1 = 0x120                                            /* F1 to F12 are EX_HK_F1 + 0 to 11 */
};

typedef struct ExChord { unsigned mods; int key; } ExChord;

/* Parses a shortcut. `Mod` is Ctrl, or Meta (Cmd) when mac is set. Modifiers may come in any order, in any case; a key is a letter, a digit,
 * F1 to F12, one of Enter Tab Escape Space Backspace Delete Insert Home End PageUp PageDown Left Right Up Down, or one of , . / ; ' [ ] \ - = `
 * A shortcut with no key, or a modifier twice, or an unknown word is refused. Returns 0, or -1. */
int ex_chord_parse(const char *text, int mac, ExChord *out);
/* The canonical text of a chord: `Ctrl+Alt+Shift+Meta+` then the key (a letter in capitals, a name as above). Returns the length, 0 on a bad chord. */
size_t ex_chord_format(const ExChord *c, char *out, size_t cap);

typedef struct ExHotkeys ExHotkeys;

/* config_dir: the folder hotkeys.ini is in (`<vault>/.exhaust`, or the OS config folder when there is no vault); NULL keeps everything in memory
 * and writes nothing. mac: `Mod` means Cmd, and the defaults are the macOS ones (see formats.md). Never fails on a bad file: lines it cannot
 * read are kept and ignored. */
ExHotkeys *ex_hotkeys_open(const char *config_dir, int mac);
void ex_hotkeys_close(ExHotkeys *h);                              /* waits for the writes that are queued */
void ex_hotkeys_flush(ExHotkeys *h);

/* The shortcuts of a command now, canonical text, at most max; returns how many (0 for none or an unknown id). out entries are 48 bytes. */
size_t ex_hotkeys_get(const ExHotkeys *h, const char *id, char (*out)[48], size_t max);
int ex_hotkeys_is_custom(const ExHotkeys *h, const char *id);     /* the file has a line for it */
/* Adds a shortcut (0), -1 for an unknown id or a bad shortcut, 1 when the command has it already. A duplicate of another command's shortcut is
 * allowed: ex_hotkeys_conflicts shows it. */
int ex_hotkeys_add(ExHotkeys *h, const char *id, const char *shortcut);
int ex_hotkeys_remove(ExHotkeys *h, const char *id, const char *shortcut);      /* 0, -1 unknown, 1 it was not there */
int ex_hotkeys_reset(ExHotkeys *h, const char *id);                             /* back to the defaults: the line goes */
/* The other commands that have this shortcut: their ids, at most max; returns how many. */
size_t ex_hotkeys_conflicts(const ExHotkeys *h, const char *id, const char *shortcut, const char **ids, size_t max);
/* Every command that has this chord now (ids, at most max): what a key press may run. */
size_t ex_hotkeys_find(const ExHotkeys *h, const ExChord *c, const char **ids, size_t max);
/* A number that grows with every change, so a screen knows when to draw again. */
unsigned long ex_hotkeys_version(const ExHotkeys *h);

#ifdef __cplusplus
}
#endif
#endif
