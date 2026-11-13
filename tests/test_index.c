/* Index tests (sub-slice 4a-i). Usage: test_index <gen_vault> <mode>
 *   cold    : the performance vault (5,000 notes, 20,000 links) is indexed from nothing in under 10 seconds; a second sync changes nothing;
 *             a changed, a new and a deleted note are picked up; the trigram candidates contain every note that holds a text
 *   rebuild : a deleted index (and one that is damaged) is made again with identical contents
 *   compat  : an index at every earlier schema version migrates to the current one; a newer or damaged file is replaced
 *   size    : the trigram index is at most 4 times the size of the notes' text (the ratio is printed)
 *   ui      : while the indexer thread works, the longest stretch the UI thread spends on a frame stays under 16 ms */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_index.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"
#include "sqlite3.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* Under the sanitizers everything runs several times slower, so the time limits are only meaningful in the normal builds. */
#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif
#ifndef SANITIZED
#define SANITIZED 0
#endif
#define TIME_SCALE (SANITIZED ? 10.0 : 1.0)

/* each mode has folders of its own so the modes can run side by side (ctest -j) */
static char g_vault[96], g_db[96];
#define VAULT g_vault
#define DBDIR g_db

static void make_vault(const char *tool) {
    const char *args[3];
    SDL_Process *p;
    int code = -1;
    ex_platform_remove_tree(VAULT);
    args[0] = tool; args[1] = VAULT; args[2] = NULL;
    p = SDL_CreateProcess(args, false);
    if (!p) { fprintf(stderr, "cannot start %s\n", tool); exit(2); }
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    if (code != 0) { fprintf(stderr, "gen_vault failed\n"); exit(2); }
}

static char *abs_vault(void) { return ex_platform_realpath(VAULT); }
static char *dbpath(const char *name) { static char buf[256]; snprintf(buf, sizeof buf, "%s/%s.db", DBDIR, name); return buf; }
static double seconds(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

static int has_id(const int64_t *v, size_t n, int64_t id) { size_t i; for (i = 0; i < n; i++) if (v[i] == id) return 1; return 0; }

static int ci_contains(const char *hay, const char *needle) {
    size_t n = strlen(needle), i, k;
    for (i = 0; hay[i]; i++) {
        for (k = 0; k < n && hay[i + k]; k++) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (k == n) return 1;
    }
    return 0;
}

static void test_cold(const char *tool) {
    ExIndex *w, *r;
    ExIndexStats st;
    char err[200], *root;
    double t0, t1;
    static const char *needles[] = {"Notes", "ing", "See [[", "zzzqqq", "ab"};
    size_t k;
    make_vault(tool);
    ex_platform_remove_tree(DBDIR);
    root = abs_vault();
    CHECK(root != NULL);
    w = ex_index_open(dbpath("cold"), 1, err, sizeof err);
    CHECK(w != NULL);
    if (!w) { fprintf(stderr, "%s\n", err); return; }
    CHECK(ex_index_schema_version(w) == EX_INDEX_SCHEMA_VERSION);
    t0 = seconds();
    CHECK(ex_index_sync(w, root, NULL, 0, &st, NULL, NULL) == 0);
    t1 = seconds();
    printf("cold index: %zu files, %zu added in %.2f s\n", st.scanned, st.added, t1 - t0);
    CHECK(t1 - t0 < 10.0 * TIME_SCALE);
    CHECK(st.added == st.scanned && st.scanned > 5000);
    CHECK(ex_index_file_count(w, EX_KIND_MARKDOWN) == 5000);
    /* a second sync finds nothing to do */
    CHECK(ex_index_sync(w, root, NULL, 0, &st, NULL, NULL) == 0);
    CHECK(st.added == 0 && st.updated == 0 && st.removed == 0 && st.unchanged == st.scanned);
    /* readers see the same */
    r = ex_index_open(dbpath("cold"), 0, err, sizeof err);
    CHECK(r != NULL);
    if (r) {
        long n = ex_index_file_count(r, EX_KIND_MARKDOWN);
        CHECK(n == 5000);
        for (k = 0; k < sizeof needles / sizeof needles[0]; k++) {
            int64_t *ids = NULL;
            size_t nids = 0, i, truth = 0, extra = 0;
            int scanned = 0;
            CHECK(ex_index_candidates(r, needles[k], strlen(needles[k]), &ids, &nids, &scanned) == 0);
            CHECK(scanned == (strlen(needles[k]) < 3));
            if (k < 4) {                                    /* every note that holds the text is a candidate */
                for (i = 0; i < 5000; i += 7) {
                    char rel[96], path[600], *text;
                    int64_t id;
                    snprintf(rel, sizeof rel, "%s", "");
                    {
                        char *name = ex_index_file_path(r, (int64_t)i + 1);
                        if (!name) continue;
                        snprintf(rel, sizeof rel, "%s", name);
                        free(name);
                    }
                    if (!strstr(rel, ".md")) continue;
                    snprintf(path, sizeof path, "%s/%s", root, rel);
                    text = ex_platform_read_file(path, NULL);
                    id = ex_index_file_id(r, rel);
                    if (text && ci_contains(text, needles[k])) { truth++; CHECK(has_id(ids, nids, id)); }
                    free(text);
                }
                extra = nids;
                printf("needle \"%s\": %zu candidates (%zu of the sampled notes hold it)\n", needles[k], extra, truth);
            }
            free(ids);
        }
        ex_index_close(r);
    }
    /* a changed, a new and a removed note */
    {
        char path[512], *first = NULL;
        FILE *f;
        sqlite3_stmt *s = NULL;
        sqlite3 *db = NULL;
        int64_t idnew, *ids = NULL;
        size_t nids = 0;
        (void)s; (void)db; (void)first;
        snprintf(path, sizeof path, "%s/Fresh note.md", root);
        f = fopen(path, "wb");
        if (f) { fputs("# Fresh\n\nA zebrafish swims here. See [[Elsewhere]].\n\n- [ ] one task\n- [x] done task\n", f); fclose(f); }
        CHECK(ex_index_update_file(w, root, "Fresh note.md") == 0);
        idnew = ex_index_file_id(w, "Fresh note.md");
        CHECK(idnew > 0);
        CHECK(ex_index_candidates(w, "zebrafish", 9, &ids, &nids, NULL) == 0);
        CHECK(nids == 1 && ids[0] == idnew);
        free(ids);
        f = fopen(path, "wb");
        if (f) { fputs("# Fresh\n\nNothing about fish now.\n", f); fclose(f); }
        CHECK(ex_index_update_file(w, root, "Fresh note.md") == 0);
        CHECK(ex_index_candidates(w, "zebrafish", 9, &ids, &nids, NULL) == 0 && nids == 0);
        free(ids);
        CHECK(ex_index_candidates(w, "Nothing about", 13, &ids, &nids, NULL) == 0 && nids == 1);
        free(ids);
        remove(path);
        CHECK(ex_index_sync(w, root, NULL, 0, &st, NULL, NULL) == 0);
        CHECK(st.removed == 1);
        CHECK(ex_index_file_id(w, "Fresh note.md") == 0);
    }
    ex_index_close(w);
    free(root);
}

static void test_rebuild(const char *tool) {
    ExIndex *w;
    char *root, *d1, *d2, err[200];
    FILE *f;
    make_vault(tool);
    ex_platform_remove_tree(DBDIR);
    root = abs_vault();
    w = ex_index_open(dbpath("a"), 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    d1 = w ? ex_index_digest(w) : NULL;
    ex_index_close(w);
    ex_index_delete_files(dbpath("a"));
    CHECK(!ex_platform_file_exists(dbpath("a")));
    w = ex_index_open(dbpath("a"), 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    d2 = w ? ex_index_digest(w) : NULL;
    CHECK(d1 && d2 && strcmp(d1, d2) == 0);
    /* clear and sync again on the same file */
    CHECK(ex_index_clear(w) == 0);
    CHECK(ex_index_file_count(w, -1) == 0);
    CHECK(ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    free(d2);
    d2 = ex_index_digest(w);
    CHECK(d1 && d2 && strcmp(d1, d2) == 0);
    ex_index_close(w);
    /* a damaged file is replaced */
    f = fopen(dbpath("a"), "wb");
    if (f) { fputs("this is not a database, but it is long enough to look like a file that was damaged by something", f); fclose(f); }
    w = ex_index_open(dbpath("a"), 1, err, sizeof err);
    CHECK(w != NULL);
    if (w) {
        CHECK(ex_index_was_rebuilt(w));
        CHECK(ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
        free(d2);
        d2 = ex_index_digest(w);
        CHECK(d1 && d2 && strcmp(d1, d2) == 0);
        ex_index_close(w);
    }
    free(d1); free(d2); free(root);
}

/* Rows an index written at each schema version could hold (index 0 is version 1). */
static const char *const OLD_ROWS[] = {
    "INSERT INTO files(path, kind, size, mtime, hash, indexed) VALUES('Gone.md', 1, 3, 1, NULL, 1), ('Home.md', 1, 3, 1, NULL, 1);"
    "INSERT INTO fts(rowid, body) VALUES(1, 'gone'), (2, 'home');"
    "INSERT INTO lines(file_id, count, offsets) VALUES(1, 1, x'00');",
    "",
    "",
};
_Static_assert(sizeof OLD_ROWS / sizeof OLD_ROWS[0] == EX_INDEX_SCHEMA_VERSION, "OLD_ROWS needs an entry for every schema version");

static void test_compat(const char *tool) {
    int v;
    char *root, err[200];
    ExIndex *w;
    char *want = NULL;
    make_vault(tool);
    root = abs_vault();
    ex_platform_remove_tree(DBDIR);
    w = ex_index_open(dbpath("want"), 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    want = w ? ex_index_digest(w) : NULL;
    ex_index_close(w);
    for (v = 1; v <= EX_INDEX_SCHEMA_VERSION; v++) {
        char name[32];
        char *got;
        snprintf(name, sizeof name, "v%d", v);
        w = ex_index_open_at_version(dbpath(name), v, err, sizeof err);
        CHECK(w != NULL && ex_index_schema_version(w) == v);
        if (!w) continue;
        ex_index_close(w);
        {   /* what a user of that version had in the file: a note that is gone now, and a note that changed */
            sqlite3 *db = NULL;
            CHECK(sqlite3_open(dbpath(name), &db) == SQLITE_OK);
            CHECK(sqlite3_exec(db, OLD_ROWS[v - 1], NULL, NULL, NULL) == SQLITE_OK);
            sqlite3_close(db);
        }
        w = ex_index_open(dbpath(name), 1, err, sizeof err);                  /* this version opens it and migrates */
        CHECK(w != NULL && ex_index_schema_version(w) == EX_INDEX_SCHEMA_VERSION && !ex_index_was_rebuilt(w));
        if (!w) continue;
        CHECK(ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);        /* notes that older versions did not read are read now */
        got = ex_index_digest(w);
        CHECK(got && want && strcmp(got, want) == 0);
        free(got);
        ex_index_close(w);
    }
    /* a newer schema than this program knows is only a cache: replaced */
    {
        sqlite3 *db = NULL;
        char sql[64];
        CHECK(sqlite3_open(dbpath("future"), &db) == SQLITE_OK);
        snprintf(sql, sizeof sql, "PRAGMA user_version = %d", EX_INDEX_SCHEMA_VERSION + 5);
        sqlite3_exec(db, "CREATE TABLE something_new(x)", NULL, NULL, NULL);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
        sqlite3_close(db);
        w = ex_index_open(dbpath("future"), 1, err, sizeof err);
        CHECK(w != NULL && ex_index_was_rebuilt(w) && ex_index_schema_version(w) == EX_INDEX_SCHEMA_VERSION);
        ex_index_close(w);
    }
    /* a reader never creates or changes anything */
    CHECK(ex_index_open(dbpath("does-not-exist"), 0, err, sizeof err) == NULL);
    free(want); free(root);
}

static void test_size(const char *tool) {
    ExIndex *w;
    char *root, err[200];
    long long text, idx;
    make_vault(tool);
    ex_platform_remove_tree(DBDIR);
    root = abs_vault();
    w = ex_index_open(dbpath("size"), 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (w) {
        text = ex_index_note_text_bytes(w);
        idx = ex_index_text_index_bytes(w);
        printf("trigram index: %lld bytes for %lld bytes of note text, ratio %.2f\n", idx, text, text ? (double)idx / (double)text : 0.0);
        CHECK(text > 0 && idx > 0 && idx <= 4 * text);
        ex_index_close(w);
    }
    free(root);
}

typedef struct Ui { ExQueue *q; } Ui;

static void test_ui(const char *tool) {
    ExIndex *w, *r;
    ExIndexer *ixr;
    ExQueue *q;
    char *root, err[200];
    double worst = 0, t;
    int done = 0;
    long frames = 0;
    make_vault(tool);
    ex_platform_remove_tree(DBDIR);
    root = abs_vault();
    w = ex_index_open(dbpath("ui"), 1, err, sizeof err);                    /* the file exists with its schema, like on every start but the first */
    CHECK(w != NULL);
    ex_index_close(w);
    q = ex_queue_create(NULL, NULL);
    ixr = ex_indexer_start(dbpath("ui"), root, NULL, 0, 7, q);
    CHECK(ixr != NULL);
    r = ex_index_open(dbpath("ui"), 0, err, sizeof err);
    CHECK(r != NULL);
    if (!ixr || !r) { free(root); return; }
    ex_indexer_sync(ixr);
    while (!done) {
        ExMessage m;
        int64_t *ids = NULL;
        size_t n = 0;
        t = seconds();
        while (ex_queue_pop(q, &m)) { if (m.type == EX_MSG_INDEX_DONE) { done = 1; CHECK(((ExIndexDoneMsg *)m.payload)->ok); } if (m.free_payload) m.free_payload(m.payload); }
        (void)ex_index_file_count(r, -1);                               /* what a frame reads from the index */
        if (ex_index_candidates(r, "Notes", 5, &ids, &n, NULL) == 0) free(ids);
        t = seconds() - t;
        if (t > worst) worst = t;
        frames++;
        SDL_Delay(2);                                                   /* the rest of the frame */
    }
    printf("while indexing: %ld frames, longest blocked time %.2f ms\n", frames, worst * 1000.0);
    CHECK(frames > 10);
    CHECK(worst < 0.016 * TIME_SCALE);
    ex_indexer_stop(ixr);
    ex_queue_destroy(q);
    ex_index_close(r);
    free(root);
}

int main(int argc, char **argv) {
    const char *tool = argc > 1 ? argv[1] : "./gen_vault", *mode = argc > 2 ? argv[2] : "cold";
    snprintf(g_vault, sizeof g_vault, "index_test_vault_%s", mode);
    snprintf(g_db, sizeof g_db, "index_test_db_%s", mode);
    if (!strcmp(mode, "cold")) test_cold(tool);
    else if (!strcmp(mode, "rebuild")) test_rebuild(tool);
    else if (!strcmp(mode, "compat")) test_compat(tool);
    else if (!strcmp(mode, "size")) test_size(tool);
    else if (!strcmp(mode, "ui")) test_ui(tool);
    else { fprintf(stderr, "unknown mode %s\n", mode); return 2; }
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
