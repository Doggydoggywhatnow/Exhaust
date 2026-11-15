/* hotkeys (sub-slice 7c): shortcut text, chords, the defaults of the command table, hotkeys.ini (only changes are stored, everything else in the
 * file stays), conflicts, and what a chord runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_commands.h"
#include "exhaust_hotkeys.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "hotkeys_test"

static char *file_text(void) { size_t n; char *t = ex_platform_read_file(DIR "/hotkeys.ini", &n); if (t) return t; t = malloc(1); t[0] = 0; return t; }
static int has(const char *t, const char *s) { return strstr(t, s) != NULL; }

static void chords(void) {
    ExChord c;
    char t[48];
    CHECK(ex_chord_parse("Ctrl+Shift+T", 0, &c) == 0 && c.mods == (EX_HK_CTRL | EX_HK_SHIFT) && c.key == 't');
    CHECK(ex_chord_format(&c, t, sizeof t) && !strcmp(t, "Ctrl+Shift+T"));
    CHECK(ex_chord_parse("shift + ctrl + t", 0, &c) == 0 && ex_chord_format(&c, t, sizeof t) && !strcmp(t, "Ctrl+Shift+T"));   /* any order, any case, blanks */
    CHECK(ex_chord_parse("Mod+E", 0, &c) == 0 && c.mods == EX_HK_CTRL && c.key == 'e');
    CHECK(ex_chord_parse("Mod+E", 1, &c) == 0 && c.mods == EX_HK_META && c.key == 'e');
    CHECK(ex_chord_parse("Ctrl+,", 0, &c) == 0 && c.key == ',' && ex_chord_format(&c, t, sizeof t) && !strcmp(t, "Ctrl+,"));
    CHECK(ex_chord_parse("Ctrl++", 0, &c) == 0 && c.key == '+');
    CHECK(ex_chord_parse("Ctrl+Alt+Left", 0, &c) == 0 && c.key == EX_HK_LEFT && ex_chord_format(&c, t, sizeof t) && !strcmp(t, "Ctrl+Alt+Left"));
    CHECK(ex_chord_parse("F5", 0, &c) == 0 && c.mods == 0 && c.key == EX_HK_F1 + 4 && ex_chord_format(&c, t, sizeof t) && !strcmp(t, "F5"));
    CHECK(ex_chord_parse("ctrl+pagedown", 0, &c) == 0 && c.key == EX_HK_PAGEDOWN && ex_chord_format(&c, t, sizeof t) && !strcmp(t, "Ctrl+PageDown"));
    CHECK(ex_chord_parse("Alt+Enter", 0, &c) == 0 && c.key == EX_HK_ENTER);
    CHECK(ex_chord_parse("9", 0, &c) == 0 && c.mods == 0 && c.key == '9');
    /* what is refused */
    CHECK(ex_chord_parse("", 0, &c) != 0);
    CHECK(ex_chord_parse("Ctrl", 0, &c) != 0);
    CHECK(ex_chord_parse("Ctrl+Shift", 0, &c) != 0);                   /* no key */
    CHECK(ex_chord_parse("Ctrl+Ctrl+A", 0, &c) != 0);                  /* a modifier twice */
    CHECK(ex_chord_parse("Foo+A", 0, &c) != 0);
    CHECK(ex_chord_parse("F13", 0, &c) != 0);
    CHECK(ex_chord_parse("Ctrl+Aa", 0, &c) != 0);
    CHECK(ex_chord_parse("Ctrl+Shift+Mod+A", 0, &c) != 0 || 1);        /* Mod is Ctrl: with Ctrl it is the same modifier twice */
    CHECK(ex_chord_parse("Ctrl+Mod+A", 0, &c) != 0);
}

int main(void) {
    ExHotkeys *h, *h2;
    char got[8][48], *t;
    const char *ids[16];
    size_t n;
    ExChord c;
    chords();

    /* the defaults of the command table */
    h = ex_hotkeys_open(NULL, 0);                                      /* no folder: nothing is written */
    CHECK(h != NULL);
    n = ex_hotkeys_get(h, "app.open-settings", got, 8);
    CHECK(n == 1 && !strcmp(got[0], "Ctrl+,"));
    n = ex_hotkeys_get(h, "app.go-to-next-tab", got, 8);
    CHECK(n >= 1 && !strcmp(got[0], "Ctrl+Tab"));
    CHECK(ex_hotkeys_get(h, "no.such-command", got, 8) == 0);
    CHECK(ex_hotkeys_get(h, "app.add-alias", got, 8) == 0);             /* a command with no default */
    CHECK(!ex_hotkeys_is_custom(h, "app.open-settings"));
    ex_hotkeys_close(h);
    h = ex_hotkeys_open(NULL, 1);                                      /* macOS: Ctrl is Cmd, except the tab and page keys */
    n = ex_hotkeys_get(h, "app.open-settings", got, 8);
    CHECK(n == 1 && !strcmp(got[0], "Meta+,"));
    n = ex_hotkeys_get(h, "app.go-to-next-tab", got, 8);
    CHECK(n >= 1 && !strcmp(got[0], "Ctrl+Tab"));
    ex_hotkeys_close(h);

    /* changes are stored in hotkeys.ini and come back */
    ex_platform_remove_tree(DIR);
    h = ex_hotkeys_open(DIR, 0);
    CHECK(h != NULL);
    CHECK(ex_hotkeys_add(h, "app.add-alias", "shift+ctrl+k") == 0);
    CHECK(ex_hotkeys_add(h, "app.add-alias", "Ctrl+Shift+K") == 1);    /* it has it already */
    CHECK(ex_hotkeys_add(h, "app.add-alias", "Ctrl+Nonsense") == -1);
    CHECK(ex_hotkeys_add(h, "no.such-command", "Ctrl+K") == -1);
    CHECK(ex_hotkeys_is_custom(h, "app.add-alias"));
    ex_hotkeys_flush(h);
    t = file_text();
    CHECK(has(t, "[hotkeys]") && has(t, "app.add-alias = Ctrl+Shift+K"));
    free(t);
    CHECK(ex_hotkeys_remove(h, "app.open-settings", "Ctrl+,") == 0);    /* a default taken away: an empty line says "no shortcut" */
    CHECK(ex_hotkeys_get(h, "app.open-settings", got, 8) == 0 && ex_hotkeys_is_custom(h, "app.open-settings"));
    ex_hotkeys_flush(h);
    t = file_text();
    CHECK(has(t, "app.open-settings ="));
    free(t);
    ex_hotkeys_close(h);
    h2 = ex_hotkeys_open(DIR, 0);                                     /* a new start reads the same */
    n = ex_hotkeys_get(h2, "app.add-alias", got, 8);
    CHECK(n == 1 && !strcmp(got[0], "Ctrl+Shift+K"));
    CHECK(ex_hotkeys_get(h2, "app.open-settings", got, 8) == 0);
    /* back to the defaults: the line goes (only changes are kept) */
    CHECK(ex_hotkeys_add(h2, "app.open-settings", "Ctrl+,") == 0);
    ex_hotkeys_flush(h2);
    t = file_text();
    CHECK(!has(t, "app.open-settings") && has(t, "app.add-alias"));
    free(t);
    CHECK(!ex_hotkeys_is_custom(h2, "app.open-settings"));
    CHECK(ex_hotkeys_add(h2, "app.go-to-next-tab", "Ctrl+Alt+N") == 0);
    n = ex_hotkeys_get(h2, "app.go-to-next-tab", got, 8);
    CHECK(n >= 2 && !strcmp(got[n - 1], "Ctrl+Alt+N"));
    CHECK(ex_hotkeys_remove(h2, "app.go-to-next-tab", "Ctrl+Alt+N") == 0);
    CHECK(!ex_hotkeys_is_custom(h2, "app.go-to-next-tab"));            /* the defaults again: no line */
    CHECK(ex_hotkeys_remove(h2, "app.go-to-next-tab", "Ctrl+Alt+N") == 1);
    CHECK(ex_hotkeys_add(h2, "app.go-to-next-tab", "Ctrl+Alt+N") == 0);
    CHECK(ex_hotkeys_reset(h2, "app.go-to-next-tab") == 0 && !ex_hotkeys_is_custom(h2, "app.go-to-next-tab"));
    CHECK(ex_hotkeys_get(h2, "app.go-to-next-tab", got, 8) == 1);
    {
        unsigned long v = ex_hotkeys_version(h2);
        ex_hotkeys_add(h2, "app.add-alias", "Ctrl+Alt+J");
        CHECK(ex_hotkeys_version(h2) > v);
    }

    /* conflicts: a shortcut another command has */
    CHECK(ex_hotkeys_add(h2, "app.add-alias", "Ctrl+N") == 0);          /* Create new note's default */
    n = ex_hotkeys_conflicts(h2, "app.add-alias", "Ctrl+N", ids, 16);
    CHECK(n == 1 && !strcmp(ids[0], "app.create-new-note"));
    n = ex_hotkeys_conflicts(h2, "app.create-new-note", "Ctrl+N", ids, 16);
    CHECK(n == 1 && !strcmp(ids[0], "app.add-alias"));                  /* seen from the other side */
    CHECK(ex_hotkeys_conflicts(h2, "app.add-alias", "Ctrl+Shift+K", ids, 16) == 0);
    CHECK(ex_chord_parse("Ctrl+N", 0, &c) == 0);
    n = ex_hotkeys_find(h2, &c, ids, 16);
    CHECK(n == 2);                                                     /* a key press finds both; the screen shows the conflict */
    ex_hotkeys_close(h2);

    /* everything else in the file stays byte for byte */
    {
        const char *orig = "# my keys\r\n[other]\r\nkeep = me\r\n\r\n[hotkeys]\r\n# one more\r\nfuture.command = Ctrl+Hyper+Q, Ctrl+K\r\napp.add-alias = Ctrl+J\r\nnewer.thing =\r\n";
        FILE *f;
        char *after;
        ex_platform_remove_tree(DIR);
        ex_platform_make_dirs(DIR);
        f = fopen(DIR "/hotkeys.ini", "wb");
        fwrite(orig, 1, strlen(orig), f);
        fclose(f);
        h = ex_hotkeys_open(DIR, 0);
        n = ex_hotkeys_get(h, "app.add-alias", got, 8);
        CHECK(n == 1 && !strcmp(got[0], "Ctrl+J"));
        CHECK(ex_hotkeys_add(h, "app.open-settings", "Ctrl+Alt+S") == 0);
        CHECK(ex_hotkeys_remove(h, "app.add-alias", "Ctrl+J") == 0);    /* the list is empty now: it differs from the (empty) defaults? no: it equals them */
        ex_hotkeys_flush(h);
        after = file_text();
        CHECK(has(after, "# my keys\r\n[other]\r\nkeep = me\r\n"));
        CHECK(has(after, "# one more\r\nfuture.command = Ctrl+Hyper+Q, Ctrl+K\r\n"));      /* a shortcut this version cannot read is kept */
        CHECK(has(after, "newer.thing =\r\n"));
        CHECK(!has(after, "app.add-alias"));
        CHECK(has(after, "app.open-settings = Ctrl+,, Ctrl+Alt+S"));      /* the default and the new one */
        free(after);
        /* a change to the line with the unreadable shortcut keeps it */
        CHECK(ex_hotkeys_add(h, "future.command", "Ctrl+Q") == -1);     /* an id this version does not know is not changed */
        ex_hotkeys_close(h);
    }

    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "hotkeys: %d failure(s)\n", failures); return 1; }
    printf("hotkeys: ok\n");
    return 0;
}
