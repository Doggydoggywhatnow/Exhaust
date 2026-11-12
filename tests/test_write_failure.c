/* write_failure: a failing write keeps the text in the journal, a persistent error names
 * the file, saving is retried, and when the failure goes away the text is written.
 * (The failure is injected with ex_platform_set_save_fault so it works on every OS and
 * as the superuser.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "write_failure_test/vault"
#define DATA "write_failure_test/data"

static uint64_t g_now;
static uint64_t fake_clock(void *u) { (void)u; return g_now; }

static ExNotes *make(ExVault *v) {
    ExNotesConfig c;
    memset(&c, 0, sizeof c);
    c.data_dir = DATA; c.clock = fake_clock; c.retry_ms = 2000; c.poll_ms = 50;
    return ex_notes_create(v, &c);
}
static void pump(ExNotes *m) { int i; for (i = 0; i < 60; i++) { ex_notes_tick(m); SDL_Delay(5); } }

int main(void) {
    ExVault *v;
    ExNotes *m;
    ExNote *n;
    ExNoteEvent ev;
    int failed_events = 0, saved_events = 0, i;
    char *t;
    size_t len;

    ex_platform_remove_tree("write_failure_test");
    ex_platform_make_dirs(ROOT);
    ex_platform_write_file_atomic(ROOT "/n.md", "start\n", 6);
    v = ex_vault_open(ROOT);
    m = make(v);
    n = ex_notes_open(m, "n.md", NULL);
    CHECK(v && m && n);

    /* the disk "is full" */
    ex_platform_set_save_fault("No space left on device", 28);
    ex_doc_set_caret(ex_note_doc(n), 6);
    ex_doc_replace_selections(ex_note_doc(n), "text that must not be lost\n", 27, 1);
    g_now += 1500;
    pump(m);
    CHECK(ex_note_dirty(n));
    CHECK(ex_note_error(n) && strstr(ex_note_error(n), "n.md") && strstr(ex_note_error(n), "No space left on device"));   /* names the file and says why */
    while (ex_notes_poll_event(m, &ev)) if (ev.kind == EX_NOTE_EVENT_SAVE_FAILED && strstr(ev.message, "n.md")) failed_events++;
    CHECK(failed_events >= 1);
    t = ex_platform_read_file(ROOT "/n.md", &len);
    CHECK(t && strcmp(t, "start\n") == 0);                       /* the old file is untouched: no partial write */
    free(t);

    /* the text is in the journal: flush it and read it back as a recovery */
    ex_notes_destroy_without_saving(m);                          /* a crash while the disk is still failing */
    m = make(v);
    CHECK(ex_notes_unsaved_count(m) == 1);                       /* the journal kept the text */
    {
        ExOpenResult res;
        n = ex_notes_open(m, "n.md", &res);
        CHECK(n && res == EX_OPEN_RECOVERED);
        t = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), NULL);
        CHECK(t && strcmp(t, "start\ntext that must not be lost\n") == 0);
        free(t);
    }

    /* retries: still failing, still dirty, error persists; nothing is discarded */
    for (i = 0; i < 3; i++) { g_now += 2500; pump(m); }
    CHECK(ex_note_dirty(n) && ex_note_error(n) != NULL);
    CHECK(ex_notes_flush(m) == 1);                               /* a clean close reports the note it could not save */

    /* the problem goes away: the next retry writes everything and the error clears */
    ex_platform_set_save_fault(NULL, 0);
    g_now += 2500;
    pump(m);
    CHECK(!ex_note_dirty(n) && ex_note_error(n) == NULL);
    while (ex_notes_poll_event(m, &ev)) if (ev.kind == EX_NOTE_EVENT_SAVED) saved_events++;
    CHECK(saved_events >= 1);
    t = ex_platform_read_file(ROOT "/n.md", &len);
    CHECK(t && strcmp(t, "start\ntext that must not be lost\n") == 0);
    free(t);

    /* a locked file looks the same to the program (the message differs) */
    ex_platform_set_save_fault("The process cannot access the file because it is being used by another process", 32);
    ex_doc_replace_selections(ex_note_doc(n), "more\n", 5, 0);
    g_now += 1500;
    pump(m);
    CHECK(ex_note_dirty(n) && ex_note_error(n) && strstr(ex_note_error(n), "being used by another process"));
    ex_platform_set_save_fault(NULL, 0);
    CHECK(ex_notes_flush(m) == 0 && !ex_note_dirty(n));
    ex_notes_destroy(m);
    ex_vault_close(v);
    ex_platform_remove_tree("write_failure_test");
    return failures ? 1 : 0;
}
