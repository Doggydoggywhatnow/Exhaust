/* The process that autosave_crash kills. Opens a vault and a note with the real clock and
 * the default autosave settings, types a deterministic text one character at a time, and
 * then keeps running until it is killed.
 * Usage: autosave_child <vault> <data-dir> <note> <chars> <marker-file> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"

int main(int argc, char **argv) {
    ExVault *v;
    ExNotes *m;
    ExNote *n;
    ExNotesConfig c;
    int i, count;
    if (argc < 6) return 2;
    count = atoi(argv[4]);
    v = ex_vault_open(argv[1]);
    memset(&c, 0, sizeof c);
    c.data_dir = argv[2];
    m = v ? ex_notes_create(v, &c) : NULL;
    n = m ? ex_notes_open(m, argv[3], NULL) : NULL;
    if (!n) return 3;
    for (i = 0; i < count; i++) {
        char ch = (i % 40 == 39) ? '\n' : (char)('a' + (i * 7) % 26);
        ex_doc_set_caret(ex_note_doc(n), ex_buffer_size(ex_doc_buffer(ex_note_doc(n))));
        ex_doc_replace_selections(ex_note_doc(n), &ch, 1, 1);
        if (i % 3 == 0) ex_notes_tick(m);
        SDL_Delay(4);
    }
    {   /* tell the harness that every character has been typed */
        char text[32];
        snprintf(text, sizeof text, "%d", count);
        ex_platform_write_file_atomic(argv[5], text, strlen(text));
    }
    for (;;) { ex_notes_tick(m); SDL_Delay(10); } /* until killed */
}
