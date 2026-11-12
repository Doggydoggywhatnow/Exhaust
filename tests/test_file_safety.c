/* file_safety: an outside edit updates the open buffer, or raises the conflict banner
 * when there are unsaved edits; CRLF and a byte-order mark survive a save byte for byte;
 * invalid UTF-8 is refused; every forbidden note name is rejected; diffs; the
 * recovery journal is replayed after a "crash". Works in ./file_safety_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "file_safety_test/vault"
#define DATA "file_safety_test/data"

static uint64_t g_now;
static uint64_t fake_clock(void *u) { (void)u; return g_now; }

static void put_bytes(const char *rel, const void *data, size_t len) {
    char p[400];
    snprintf(p, sizeof p, ROOT "/%s", rel);
    CHECK(ex_platform_write_file_atomic(p, data, len) == 0);
}
static void put(const char *rel, const char *text) { put_bytes(rel, text, strlen(text)); }
static char *get(const char *rel, size_t *len) {
    char p[400];
    snprintf(p, sizeof p, ROOT "/%s", rel);
    return ex_platform_read_file(p, len);
}
#define STR_EQ(rel, s) bytes_eq(rel, s, strlen(s))
static int bytes_eq(const char *rel, const void *want, size_t n) {
    size_t len = 0;
    char *g = get(rel, &len);
    int ok = g && len == n && memcmp(g, want, n) == 0;
    free(g);
    return ok;
}

static ExNotes *make_notes(ExVault *v, int poll_ms) {
    ExNotesConfig c;
    memset(&c, 0, sizeof c);
    c.data_dir = DATA;
    c.clock = fake_clock;
    c.poll_ms = poll_ms;
    return ex_notes_create(v, &c);
}

static void advance(ExNotes *m, uint64_t ms) { g_now += ms; ex_notes_tick(m); }

/* Ticks (with a little real time for the worker threads) until fn is true or 5 s pass. */
static int wait_for(ExNotes *m, int (*fn)(void *), void *arg) {
    int i;
    for (i = 0; i < 1000; i++) {
        ex_notes_tick(m);
        if (fn(arg)) return 1;
        SDL_Delay(5);
    }
    return 0;
}
static int is_clean(void *p) { return !ex_note_dirty((ExNote *)p); }
static int has_conflict(void *p) { return ex_note_has_conflict((ExNote *)p); }
static int text_is(ExNote *n, const char *want) {
    char *t = ex_buffer_text(ex_doc_buffer(ex_note_doc(n)), NULL);
    int ok = t && strcmp(t, want) == 0;
    if (!ok) fprintf(stderr, "text is [%s], wanted [%s]\n", t ? t : "?", want);
    free(t);
    return ok;
}
static void type_at_end(ExNote *n, const char *s) {
    ExDoc *d = ex_note_doc(n);
    ex_doc_set_caret(d, ex_buffer_size(ex_doc_buffer(d)));
    ex_doc_replace_selections(d, s, strlen(s), 1);
}
static int reloaded_text(void *p) { (void)p; return 0; }

static void test_codec(void) {
    ExNoteFormat f;
    char *t;
    size_t tl, bl;
    uint8_t *b;
    /* BOM and CRLF are remembered and written back */
    const uint8_t crlf_bom[] = {0xEF, 0xBB, 0xBF, 'a', '\r', '\n', 'b', '\r', '\n'};
    CHECK(ex_note_decode(crlf_bom, sizeof crlf_bom, &t, &tl, &f) == 0 && f.bom && f.eol == EX_EOL_CRLF && strcmp(t, "a\nb\n") == 0);
    b = ex_note_encode(t, tl, &f, &bl);
    CHECK(b && bl == sizeof crlf_bom && memcmp(b, crlf_bom, bl) == 0);
    free(b); free(t);
    /* a typed LF in a CRLF note is written as CRLF; text that already holds CRLF is not doubled */
    CHECK(ex_note_decode((const uint8_t *)"x\r\n", 3, &t, &tl, &f) == 0);
    b = ex_note_encode("x\ny\r\nz\n", 7, &f, &bl);
    CHECK(b && bl == 9 && memcmp(b, "x\r\ny\r\nz\r\n", 9) == 0);
    free(b); free(t);
    /* mixed line endings and lone CRs stay as they are */
    CHECK(ex_note_decode((const uint8_t *)"a\r\nb\nc\r", 7, &t, &tl, &f) == 0 && f.eol == EX_EOL_MIXED && strcmp(t, "a\r\nb\nc\r") == 0);
    b = ex_note_encode(t, tl, &f, &bl);
    CHECK(b && bl == 7 && memcmp(b, "a\r\nb\nc\r", 7) == 0);
    free(b); free(t);
    CHECK(ex_note_decode((const uint8_t *)"plain\n", 6, &t, &tl, &f) == 0 && !f.bom && f.eol == EX_EOL_LF);
    free(t);
    CHECK(ex_note_decode((const uint8_t *)"", 0, &t, &tl, &f) == 0 && tl == 0);
    free(t);
    /* invalid UTF-8 is refused */
    CHECK(ex_note_decode((const uint8_t *)"ok \xFF\xFE", 5, &t, &tl, &f) != 0);
    CHECK(ex_note_decode((const uint8_t *)"\xC3", 1, &t, &tl, &f) != 0);                       /* truncated */
    CHECK(ex_note_decode((const uint8_t *)"\xC0\x80", 2, &t, &tl, &f) != 0);                   /* overlong */
    CHECK(ex_note_decode((const uint8_t *)"\xED\xA0\x80", 3, &t, &tl, &f) != 0);               /* surrogate */
    CHECK(ex_note_decode((const uint8_t *)"\xF4\x90\x80\x80", 4, &t, &tl, &f) != 0);           /* above U+10FFFF */
    CHECK(ex_note_decode((const uint8_t *)"\xFF", 1, &t, &tl, &f) != 0);
    CHECK(ex_note_decode((const uint8_t *)"caf\xC3\xA9 \xE4\xBD\xA0 \xF0\x9F\x98\x80", 14, &t, &tl, &f) == 0);
    free(t);
    CHECK(ex_utf8_valid("a\xEF\xBB\xBF" "b", 5));            /* a BOM in the middle is just a character */

    /* every forbidden note name is rejected */
    {
        static const char *bad[] = {"", ".", "..", "a/b", "a\\b", "a#b", "a^b", "a[b", "a]b", "a|b", "a:b", "a%%b", "CON", "con.md", "PRN", "AUX", "NUL",
                                    "COM1", "COM9.md", "LPT1", "lpt9.txt", "name.", "name ", " name", "tab\tname", "nl\nname", "q?", "s*", "q\"", "lt<", "gt>"};
        size_t i;
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) if (ex_name_valid(bad[i])) { fprintf(stderr, "accepted [%s]\n", bad[i]); failures++; }
        CHECK(ex_name_valid("Normal note.md") && ex_name_valid("Ünïcode ノート.md") && ex_name_valid("100% done"));
    }

    /* the diff */
    {
        char *d = ex_diff_unified("a\nb\nc\nd\n", "a\nb\nX\nd\n", "mine", "disk");
        CHECK(d && strstr(d, "--- mine\n+++ disk\n") == d && strstr(d, "-c\n+X\n") && strstr(d, " b\n") && strstr(d, " d\n") && strstr(d, "@@ -1,4 +1,4 @@"));
        free(d);
        d = ex_diff_unified("same\n", "same\n", "a", "b");
        CHECK(d && d[0] == 0);
        free(d);
        d = ex_diff_unified("", "new\n", "a", "b");
        CHECK(d && strstr(d, "+new\n"));
        free(d);
    }
}

static void test_outside_edits_and_save(void) {
    ExVault *v;
    ExNotes *m;
    ExNote *n;
    ExOpenResult res;
    ExNoteEvent ev;
    int got_reload = 0, got_conflict = 0;
    const uint8_t crlf_bom[] = {0xEF, 0xBB, 0xBF, 'h', 'i', '\r', '\n', 't', 'w', 'o', '\r', '\n'};

    put("plain.md", "one\ntwo\n");
    put_bytes("crlf.md", crlf_bom, sizeof crlf_bom);
    put_bytes("bad.md", "bad \xFF\xFE bytes", 11);
    v = ex_vault_open(ROOT);
    m = make_notes(v, 40);
    CHECK(v && m);
    if (!m) return;

    /* invalid UTF-8: refused, and never written to */
    CHECK(ex_notes_open(m, "bad.md", &res) == NULL && res == EX_OPEN_INVALID_UTF8);
    CHECK(bytes_eq("bad.md", "bad \xFF\xFE bytes", 11));
    CHECK(ex_notes_open(m, "missing.md", &res) == NULL && res == EX_OPEN_NOT_FOUND);

    /* CRLF and a BOM survive a save byte for byte, also after typing */
    n = ex_notes_open(m, "crlf.md", &res);
    CHECK(n && res == EX_OPEN_OK && ex_note_format(n)->bom && ex_note_format(n)->eol == EX_EOL_CRLF && text_is(n, "hi\ntwo\n"));
    type_at_end(n, "three\n");
    CHECK(ex_note_dirty(n));
    advance(m, 1500);                                  /* the idle time has passed: autosave */
    CHECK(wait_for(m, is_clean, n));
    {
        const uint8_t want[] = {0xEF, 0xBB, 0xBF, 'h', 'i', '\r', '\n', 't', 'w', 'o', '\r', '\n', 't', 'h', 'r', 'e', 'e', '\r', '\n'};
        CHECK(bytes_eq("crlf.md", want, sizeof want));
    }
    /* opening and saving without changes keeps every byte */
    CHECK(ex_notes_close(m, n) == 0);
    n = ex_notes_open(m, "crlf.md", NULL);
    ex_doc_set_caret(ex_note_doc(n), 0);
    ex_doc_replace_selections(ex_note_doc(n), "x", 1, 0);
    ex_doc_undo(ex_note_doc(n));
    advance(m, 1500);
    wait_for(m, is_clean, n);
    {
        const uint8_t want[] = {0xEF, 0xBB, 0xBF, 'h', 'i', '\r', '\n', 't', 'w', 'o', '\r', '\n', 't', 'h', 'r', 'e', 'e', '\r', '\n'};
        CHECK(bytes_eq("crlf.md", want, sizeof want));
    }
    ex_notes_close(m, n);

    /* an outside edit updates a buffer that has no unsaved edits */
    n = ex_notes_open(m, "plain.md", NULL);
    CHECK(n && text_is(n, "one\ntwo\n") && !ex_note_dirty(n));
    SDL_Delay(1100);                                   /* make the modified time differ even on coarse file systems */
    put("plain.md", "one\ntwo changed outside\nthree\n");
    {
        int i;
        for (i = 0; i < 400 && !got_reload; i++) {
            ex_notes_tick(m);
            while (ex_notes_poll_event(m, &ev)) if (ev.kind == EX_NOTE_EVENT_RELOADED && ev.note == n) got_reload = 1;
            SDL_Delay(5);
        }
    }
    CHECK(got_reload && text_is(n, "one\ntwo changed outside\nthree\n") && !ex_note_dirty(n) && !ex_note_has_conflict(n));
    ex_doc_undo(ex_note_doc(n));                       /* the reload is one undoable step */
    CHECK(text_is(n, "one\ntwo\n"));
    ex_doc_redo(ex_note_doc(n));

    /* with unsaved edits, the same outside change raises the conflict banner and keeps the buffer */
    type_at_end(n, "my own words\n");
    CHECK(ex_note_dirty(n));
    SDL_Delay(1100);
    put("plain.md", "one\nreplaced entirely outside\n");
    CHECK(wait_for(m, has_conflict, n));
    CHECK(text_is(n, "one\ntwo changed outside\nthree\nmy own words\n"));           /* the buffer is untouched */
    CHECK(ex_note_conflict_disk_text(n) && strcmp(ex_note_conflict_disk_text(n), "one\nreplaced entirely outside\n") == 0);
    {
        char *diff = ex_note_conflict_diff(n);
        CHECK(diff && strstr(diff, "+my own words") == NULL && strstr(diff, "-my own words") && strstr(diff, "+replaced entirely outside"));
        free(diff);
    }
    while (ex_notes_poll_event(m, &ev)) if (ev.kind == EX_NOTE_EVENT_CONFLICT) got_conflict = 1;
    CHECK(got_conflict);
    /* while the conflict is open nothing is written, even after a long idle time */
    advance(m, 20000);
    ex_notes_tick(m);
    CHECK(STR_EQ("plain.md", "one\nreplaced entirely outside\n") && ex_note_dirty(n));
    /* keep mine: the disk version is copied to the recovery folder first, then mine is saved */
    CHECK(ex_note_resolve_conflict(n, EX_KEEP_MINE) == 0 && !ex_note_has_conflict(n));
    advance(m, 6000);
    CHECK(wait_for(m, is_clean, n));
    CHECK(STR_EQ("plain.md", "one\ntwo changed outside\nthree\nmy own words\n"));
    {
        int found = 0;
        char p[400];
        ExPathInfo info;
        const char *names[] = {DATA "/recovery"};
        (void)names;
        snprintf(p, sizeof p, DATA "/recovery");
        found = ex_platform_path_info(p, &info) == 0 && info.is_dir;
        CHECK(found);
    }
    ex_notes_close(m, n);

    /* a conflict resolved the other way: use the disk version, mine goes to the recovery folder */
    n = ex_notes_open(m, "plain.md", NULL);
    type_at_end(n, "unsaved line\n");
    SDL_Delay(1100);
    put("plain.md", "disk wins\n");
    CHECK(wait_for(m, has_conflict, n));
    CHECK(ex_note_resolve_conflict(n, EX_USE_DISK) == 0 && text_is(n, "disk wins\n") && !ex_note_dirty(n));
    ex_doc_undo(ex_note_doc(n));
    CHECK(text_is(n, "one\ntwo changed outside\nthree\nmy own words\nunsaved line\n"));   /* undo brings mine back */
    ex_doc_redo(ex_note_doc(n));
    ex_notes_close(m, n);
    CHECK(STR_EQ("plain.md", "disk wins\n"));

    /* a new note that nothing was typed into is never created on disk */
    n = ex_notes_new(m, "", "Untitled");
    CHECK(n && !ex_note_exists_on_disk(n) && strcmp(ex_note_path(n), "Untitled.md") == 0);
    advance(m, 20000);
    ex_notes_tick(m);
    CHECK(get("Untitled.md", NULL) == NULL);
    type_at_end(n, "now it has text\n");
    advance(m, 1500);
    CHECK(wait_for(m, is_clean, n) && ex_note_exists_on_disk(n) && STR_EQ("Untitled.md", "now it has text\n"));
    ex_notes_close(m, n);
    ex_notes_destroy(m);
    ex_vault_close(v);
    (void)reloaded_text;
}

static void test_replay_after_crash(void) {
    ExVault *v;
    ExNotes *m;
    ExNote *n;
    ExOpenResult res;
    ExNoteEvent ev;
    int saw_saved = 0;

    ex_platform_remove_tree("file_safety_test");
    ex_platform_make_dirs(ROOT);
    put("crash.md", "line one\n");
    v = ex_vault_open(ROOT);
    m = make_notes(v, 40);
    n = ex_notes_open(m, "crash.md", NULL);
    type_at_end(n, "typed before the crash\n");
    ex_doc_set_caret(ex_note_doc(n), 0);
    ex_doc_replace_selections(ex_note_doc(n), ">> ", 3, 0);
    /* "crash": the journal is flushed but the note is never saved and the manager is not closed properly */
    ex_notes_tick(m);
    SDL_Delay(300);
    CHECK(STR_EQ("crash.md", "line one\n"));       /* nothing was saved yet */
    /* the process "dies": the manager goes away without saving the note */
    ex_notes_destroy_without_saving(m);
    ex_vault_close(v);

    /* relaunch */
    v = ex_vault_open(ROOT);
    m = make_notes(v, 40);
    CHECK(ex_notes_unsaved_count(m) == 1);
    n = ex_notes_open(m, "crash.md", &res);
    CHECK(n && res == EX_OPEN_RECOVERED && ex_note_recovered(n) && text_is(n, ">> line one\ntyped before the crash\n"));
    CHECK(ex_note_dirty(n) && ex_notes_unsaved_count(m) == 0);
    advance(m, 1500);
    CHECK(wait_for(m, is_clean, n) && STR_EQ("crash.md", ">> line one\ntyped before the crash\n"));
    while (ex_notes_poll_event(m, &ev)) if (ev.kind == EX_NOTE_EVENT_SAVED) saw_saved = 1;
    CHECK(saw_saved);
    ex_notes_close(m, n);
    ex_notes_destroy(m);
    ex_vault_close(v);

    /* a third launch finds nothing to recover: the saved marker cleared it */
    v = ex_vault_open(ROOT);
    m = make_notes(v, 40);
    CHECK(ex_notes_unsaved_count(m) == 0);
    ex_notes_destroy(m);
    ex_vault_close(v);

    /* the file changed on disk between the crash and the next launch: both versions are kept */
    ex_platform_remove_tree("file_safety_test");
    ex_platform_make_dirs(ROOT);
    put("both.md", "original\n");
    v = ex_vault_open(ROOT);
    m = make_notes(v, 40);
    n = ex_notes_open(m, "both.md", NULL);
    type_at_end(n, "my unsaved text\n");
    ex_notes_tick(m);
    SDL_Delay(300);
    ex_notes_destroy_without_saving(m);
    ex_vault_close(v);
    put("both.md", "changed by another program\n");
    v = ex_vault_open(ROOT);
    {
        ExNotes *m2 = make_notes(v, 40);
        ExNote *n2 = ex_notes_open(m2, "both.md", &res);
        CHECK(n2 && res == EX_OPEN_CONFLICT && ex_note_has_conflict(n2));
        CHECK(ex_note_conflict_disk_text(n2) && strcmp(ex_note_conflict_disk_text(n2), "changed by another program\n") == 0);
        CHECK(STR_EQ("both.md", "changed by another program\n"));        /* the disk version is untouched */
        ex_notes_destroy(m2);
    }
    ex_vault_close(v);
}

int main(void) {
    ex_platform_remove_tree("file_safety_test");
    ex_platform_make_dirs(ROOT);
    test_codec();
    test_outside_edits_and_save();
    test_replay_after_crash();
    ex_platform_remove_tree("file_safety_test");
    return failures ? 1 : 0;
}
