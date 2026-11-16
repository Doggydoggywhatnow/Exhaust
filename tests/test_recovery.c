/* recovery: file recovery snapshots (sub-slice 7e). The names (one note never takes another's), taking, listing newest first, reading,
 * pruning by age (the conflict copies stay), clearing; and through the note manager: the first save keeps the version that was on disk,
 * later saves wait for the interval, an unchanged text takes none, a switched-off history takes none, a restored text is the exact earlier
 * text, and the other side of a conflict is kept with its own kind. Works in ./recovery_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_history.h"
#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "recovery_test/vault"
#define DATA "recovery_test/data"
#define HDIR "recovery_test/hist"

static uint64_t g_ms = 1000;
static int64_t g_unix = 1700000000;
static uint64_t fake_ms(void *u) { (void)u; return g_ms; }
static int64_t fake_unix(void *u) { (void)u; return g_unix; }

static void put(const char *rel, const char *text) {
    char p[400];
    snprintf(p, sizeof p, ROOT "/%s", rel);
    CHECK(ex_platform_write_file_atomic(p, text, strlen(text)) == 0);
}

static int snap_is(const char *dir, const ExSnapshot *s, const char *text) {
    size_t len = 0;
    char *d = ex_history_read(dir, s->file, &len);
    int ok = d && len == strlen(text) && memcmp(d, text, len) == 0;
    free(d);
    return ok;
}

static size_t count(const char *dir, const char *rel) {
    ExSnapshot *l;
    size_t n = ex_history_list(dir, rel, &l);
    ex_history_free(l, n);
    return n;
}

static void test_files(void) {
    ExSnapshot *l;
    size_t n, i;
    char enc1[700], enc2[700], enc3[700], longname[600];
    int64_t t0 = 1700000000;
    ex_platform_remove_tree(HDIR);

    /* one name for one note */
    CHECK(ex_history_encode("a/b.md", enc1, sizeof enc1) > 0 && !strcmp(enc1, "a__b.md"));
    CHECK(ex_history_encode("a__b.md", enc2, sizeof enc2) > 0 && strcmp(enc1, enc2) != 0);
    CHECK(ex_history_encode("a_b.md", enc3, sizeof enc3) > 0 && strcmp(enc3, enc2) != 0 && strcmp(enc3, enc1) != 0);
    CHECK(ex_history_encode("", enc1, sizeof enc1) == 0);
    memset(longname, 'x', sizeof longname - 1); longname[sizeof longname - 1] = 0;
    memcpy(longname + 590, ".md", 4);
    CHECK(ex_history_encode(longname, enc1, sizeof enc1) > 0 && strlen(enc1) < 190);
    longname[300] = 'y';
    CHECK(ex_history_encode(longname, enc2, sizeof enc2) > 0 && strcmp(enc1, enc2) != 0);       /* two long names that differ late stay two */

    /* taking and listing */
    CHECK(ex_history_take(HDIR, "a/b.md", "v1", 2, t0, EX_SNAP_AUTO) == 0);
    CHECK(ex_history_take(HDIR, "a/b.md", "v2 longer", 9, t0 + 600, EX_SNAP_AUTO) == 0);
    CHECK(ex_history_take(HDIR, "a/b.md", "mine", 4, t0 + 1200, EX_SNAP_CONFLICT) == 0);
    CHECK(ex_history_take(HDIR, "a__b.md", "other note", 10, t0 + 5, EX_SNAP_AUTO) == 0);
    CHECK(ex_history_take(HDIR, "a/b.md", "v1 again", 8, t0, EX_SNAP_AUTO) == 0);                 /* the same second replaces */
    n = ex_history_list(HDIR, "a/b.md", &l);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(l[0].time == t0 + 1200 && l[0].kind == EX_SNAP_CONFLICT && l[1].time == t0 + 600 && l[2].time == t0);
        CHECK(snap_is(HDIR, &l[0], "mine") && snap_is(HDIR, &l[1], "v2 longer") && snap_is(HDIR, &l[2], "v1 again"));
        CHECK(l[1].size == 9);
    }
    ex_history_free(l, n);
    CHECK(count(HDIR, "a__b.md") == 1 && count(HDIR, "a_b.md") == 0 && count(HDIR, "nothing.md") == 0);
    CHECK(ex_history_last_auto(HDIR, "a/b.md") == t0 + 600);                                      /* the conflict copy does not count */
    CHECK(ex_history_last_auto(HDIR, "nothing.md") == 0);

    /* only snapshot file names are read */
    n = ex_history_list(HDIR, "a/b.md", &l);
    CHECK(n == 3 && ex_history_read(HDIR, "../x.snap", &i) == NULL && ex_history_read(HDIR, "a__b.md", &i) == NULL && ex_history_read(HDIR, "nope.20240101-000000.snap", &i) == NULL);
    ex_history_free(l, n);
    {   /* a stray file is ignored */
        ExSnapshot *l2;
        CHECK(ex_platform_write_file_atomic(HDIR "/readme.txt", "x", 1) == 0);
        CHECK(ex_platform_write_file_atomic(HDIR "/a__b.md.2024.snap", "x", 1) == 0);
        n = ex_history_list(HDIR, "a/b.md", &l2);
        CHECK(n == 3);
        ex_history_free(l2, n);
    }

    /* pruning by age: automatic ones only */
    CHECK(ex_history_prune(HDIR, t0 + 7 * 86400 + 3, 7) == 1);          /* only the one at t0 is older than 7 days */
    n = ex_history_list(HDIR, "a/b.md", &l);
    CHECK(n == 2 && l[0].kind == EX_SNAP_CONFLICT && l[1].time == t0 + 600);
    ex_history_free(l, n);
    CHECK(ex_history_prune(HDIR, t0 + 400 * 86400, 7) == 2);            /* the rest, a/b at t0+600 and a__b at t0+5 */
    n = ex_history_list(HDIR, "a/b.md", &l);
    CHECK(n == 1 && l[0].kind == EX_SNAP_CONFLICT && snap_is(HDIR, &l[0], "mine"));           /* the conflict copy is never pruned */
    ex_history_free(l, n);
    CHECK(ex_history_prune(HDIR, t0 + 400 * 86400, 0) == 0);            /* days below 1 count as 1; nothing automatic is left */

    /* clearing: one note, then all; conflict copies stay */
    CHECK(ex_history_take(HDIR, "c.md", "c", 1, t0, EX_SNAP_AUTO) == 0 && ex_history_take(HDIR, "d.md", "d", 1, t0, EX_SNAP_AUTO) == 0);
    CHECK(ex_history_clear(HDIR, "c.md") == 1 && count(HDIR, "c.md") == 0 && count(HDIR, "d.md") == 1);
    CHECK(ex_history_clear(HDIR, NULL) >= 1 && count(HDIR, "d.md") == 0 && count(HDIR, "a/b.md") == 1);
}

static ExNotes *make_notes(ExVault *v) {
    ExNotesConfig c;
    memset(&c, 0, sizeof c);
    c.data_dir = DATA;
    c.clock = fake_ms;
    c.unix_time = fake_unix;
    c.poll_ms = 100000;
    return ex_notes_create(v, &c);
}

static void type_at_end(ExNote *n, const char *s) {
    ExDoc *d = ex_note_doc(n);
    ex_doc_set_caret(d, ex_buffer_size(ex_doc_buffer(d)));
    ex_doc_replace_selections(d, s, strlen(s), 1);
}

/* Lets autosave run and waits until the note is on disk (and so its snapshot, if one was due, is too). */
static void save(ExNotes *m, ExNote *n) {
    int i;
    g_ms += 1500;
    for (i = 0; i < 1000; i++) {
        ex_notes_tick(m);
        if (!ex_note_dirty(n)) return;
        SDL_Delay(5);
    }
    CHECK(0 && "the note was not saved");
}

static void test_notes(void) {
    ExVault *v;
    ExNotes *m;
    ExNote *n;
    ExSnapshot *l;
    size_t k;
    const char *hd;
    ex_platform_remove_tree("recovery_test");
    ex_platform_make_dirs(ROOT);
    put("n.md", "original\n");
    put("off.md", "off original\n");
    v = ex_vault_open(ROOT);
    m = make_notes(v);
    CHECK(v && m);
    if (!m) return;
    hd = ex_notes_history_dir(m);
    CHECK(hd != NULL);

    /* history off (the default): no snapshot */
    n = ex_notes_open(m, "off.md", NULL);
    type_at_end(n, "edit\n");
    save(m, n);
    CHECK(count(hd, "off.md") == 0);

    ex_notes_set_history(m, 1, 5, 7);
    /* the first save of a note keeps the version that was on disk */
    n = ex_notes_open(m, "n.md", NULL);
    type_at_end(n, "one\n");
    save(m, n);
    k = ex_history_list(hd, "n.md", &l);
    CHECK(k == 1 && snap_is(hd, &l[0], "original\n") && l[0].time == g_unix);
    ex_history_free(l, k);

    /* inside the interval: nothing new */
    g_unix += 120;
    type_at_end(n, "two\n");
    save(m, n);
    CHECK(count(hd, "n.md") == 1);

    /* after the interval: the text just written */
    g_unix += 400;
    type_at_end(n, "three\n");
    save(m, n);
    k = ex_history_list(hd, "n.md", &l);
    CHECK(k == 2 && snap_is(hd, &l[0], "original\none\ntwo\nthree\n") && snap_is(hd, &l[1], "original\n"));
    ex_history_free(l, k);

    /* the same text again: none */
    g_unix += 700;
    type_at_end(n, "x\n");
    ex_doc_undo(ex_note_doc(n));
    save(m, n);
    CHECK(count(hd, "n.md") == 2);

    /* a restore is the exact earlier text: read it back byte for byte */
    k = ex_history_list(hd, "n.md", &l);
    if (k == 2) {
        size_t len = 0;
        char *old = ex_history_read(hd, l[1].file, &len);
        CHECK(old && len == 9 && !memcmp(old, "original\n", 9));
        free(old);
    }
    ex_history_free(l, k);

    /* switched off again: none, however long it has been */
    ex_notes_set_history(m, 0, 5, 7);
    g_unix += 5000;
    type_at_end(n, "four\n");
    save(m, n);
    CHECK(count(hd, "n.md") == 2);
    ex_notes_set_history(m, 1, 5, 7);

    /* the other side of a conflict is kept, with its own kind, and the interval's pruning leaves it */
    type_at_end(n, "mine\n");
    SDL_Delay(1100);
    put("n.md", "someone else\n");
    {
        int i;
        for (i = 0; i < 1000 && !ex_note_has_conflict(n); i++) { g_ms += 100; ex_notes_tick(m); SDL_Delay(5); }
        if (!ex_note_has_conflict(n)) { g_ms += 5000; for (i = 0; i < 1000 && !ex_note_has_conflict(n); i++) { ex_notes_tick(m); SDL_Delay(5); } }
    }
    CHECK(ex_note_has_conflict(n));
    ex_note_resolve_conflict(n, EX_KEEP_MINE);
    k = ex_history_list(hd, "n.md", &l);
    {
        size_t i; int found = 0;
        for (i = 0; i < k; i++) if (l[i].kind == EX_SNAP_CONFLICT && snap_is(hd, &l[i], "someone else\n")) found = 1;
        CHECK(found);
    }
    ex_history_free(l, k);

    ex_notes_destroy(m);
    ex_vault_close(v);
}

int main(void) {
    test_files();
    test_notes();
    if (failures) { fprintf(stderr, "recovery: %d failed\n", failures); return 1; }
    printf("recovery: ok\n");
    return 0;
}
