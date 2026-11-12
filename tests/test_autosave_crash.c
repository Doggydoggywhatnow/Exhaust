/* autosave_crash: a harness starts a process that types into a note, kills it with
 * SIGKILL (TerminateProcess on Windows) about a second after typing began, relaunches
 * and finds all the text restored. Second case: killed after the autosave has run, the
 * text is already in the note file and nothing needs recovering.
 * Usage: test_autosave_crash <path-of-autosave_child> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define BASE "autosave_crash_test"

static char *expected_text(int count) {
    char *t = malloc((size_t)count + 2);
    int i;
    for (i = 0; i < count; i++) t[i] = (i % 40 == 39) ? '\n' : (char)('a' + (i * 7) % 26);
    t[count] = 0;
    return t;
}

/* Runs the child, waits for its marker, waits `extra_ms` more, kills it. Returns the typed count, or -1. */
static int run_and_kill(const char *child, const char *note, int chars, int extra_ms) {
    char chars_s[16];
    const char *args[8];
    SDL_Process *p;
    uint64_t start = SDL_GetTicks();
    char *marker;
    int typed = -1;
    snprintf(chars_s, sizeof chars_s, "%d", chars);
    args[0] = child; args[1] = BASE "/vault"; args[2] = BASE "/data"; args[3] = note; args[4] = chars_s; args[5] = BASE "/marker.txt"; args[6] = NULL;
    ex_platform_remove_tree(BASE "/marker.txt");
    p = SDL_CreateProcess(args, false);
    if (!p) { fprintf(stderr, "cannot start %s: %s\n", child, SDL_GetError()); return -1; }
    while (SDL_GetTicks() - start < 30000) {
        marker = ex_platform_read_file(BASE "/marker.txt", NULL);
        if (marker) { typed = atoi(marker); free(marker); break; }
        SDL_Delay(10);
    }
    SDL_Delay((Uint32)extra_ms);
    CHECK(SDL_KillProcess(p, true));                        /* kill -9: no chance to clean up, flush or save */
    SDL_WaitProcess(p, true, NULL);
    SDL_DestroyProcess(p);
    return typed;
}

static ExNotes *relaunch(ExVault **vout) {
    ExNotesConfig c;
    ExVault *v = ex_vault_open(BASE "/vault");
    memset(&c, 0, sizeof c);
    c.data_dir = BASE "/data";
    *vout = v;
    return v ? ex_notes_create(v, &c) : NULL;
}

int main(int argc, char **argv) {
    const char *child = argc > 1 ? argv[1] : "./autosave_child";
    int typed;
    ExVault *v;
    ExNotes *m;
    ExNote *n;
    ExOpenResult res;
    char *want, *got;

    ex_platform_remove_tree(BASE);
    ex_platform_make_dirs(BASE "/vault");
    ex_platform_write_file_atomic(BASE "/vault/crash.md", "", 0);

    /* case 1: killed about a second after typing began, before the autosave: the journal has it */
    typed = run_and_kill(child, "crash.md", 120, 500);
    CHECK(typed == 120);
    {
        char *disk = ex_platform_read_file(BASE "/vault/crash.md", NULL);
        CHECK(disk != NULL);
        fprintf(stderr, "note file after the kill holds %zu bytes (the journal holds the rest)\n", disk ? strlen(disk) : 0);
        free(disk);
    }
    m = relaunch(&v);
    CHECK(m != NULL);
    if (!m) return 1;
    CHECK(ex_notes_unsaved_count(m) == 1);
    n = ex_notes_open(m, "crash.md", &res);
    CHECK(n && (res == EX_OPEN_RECOVERED || res == EX_OPEN_OK));
    want = expected_text(typed > 0 ? typed : 0);
    got = n ? ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), NULL) : NULL;
    CHECK(got && strcmp(got, want) == 0);                    /* all the text is back */
    if (got && strcmp(got, want) != 0) fprintf(stderr, "restored %zu bytes, wanted %zu\n", strlen(got), strlen(want));
    CHECK(n && ex_note_recovered(n));
    /* and it reaches the note file again */
    if (n) CHECK(ex_note_save_now(n) == 0);
    {
        char *disk = ex_platform_read_file(BASE "/vault/crash.md", NULL);
        CHECK(disk && strcmp(disk, want) == 0);
        free(disk);
    }
    free(got); free(want);
    ex_notes_destroy(m);
    ex_vault_close(v);

    /* a launch after that has nothing left to recover */
    m = relaunch(&v);
    CHECK(m && ex_notes_unsaved_count(m) == 0);
    ex_notes_destroy(m);
    ex_vault_close(v);

    /* case 2: killed after the autosave ran (2.5 s after the last keystroke): already in the file */
    ex_platform_remove_tree(BASE);
    ex_platform_make_dirs(BASE "/vault");
    ex_platform_write_file_atomic(BASE "/vault/late.md", "", 0);
    typed = run_and_kill(child, "late.md", 60, 2500);
    CHECK(typed == 60);
    want = expected_text(typed > 0 ? typed : 0);
    {
        char *disk = ex_platform_read_file(BASE "/vault/late.md", NULL);
        CHECK(disk && strcmp(disk, want) == 0);              /* autosave wrote it before the kill */
        free(disk);
    }
    m = relaunch(&v);
    CHECK(m && ex_notes_unsaved_count(m) == 0);
    ex_notes_destroy(m);
    ex_vault_close(v);
    free(want);

    ex_platform_remove_tree(BASE);
    return failures ? 1 : 0;
}
