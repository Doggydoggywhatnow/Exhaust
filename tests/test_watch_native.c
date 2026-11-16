/* watch_native (sub-slice 7i): the operating system's own file watcher (inotify on Linux, ReadDirectoryChangesW on Windows, FSEvents on macOS) and
 * the queue it reports through. The queue: a path waits only once, in the order it came, and lost events are reported once as the empty path. The
 * watcher, with no polling: an outside create, edit, rename and delete of a note and of a folder reach it within a second; a burst of 1,000 file writes
 * arrives as 1,000 reports at most and none is lost; names that start with a dot and excluded files are never reported; and with the native watcher
 * refusing, the polling fallback still reports an edit. Works in ./watch_native_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_watch_native.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "watch_native_test"

static void test_queue(void) {
    ExPathQueue *q = ex_pathq_create();
    char rel[64];
    size_t i;
    CHECK(q && ex_pathq_count(q) == 0 && !ex_pathq_pop(q, rel, sizeof rel));
    ex_pathq_push(q, "a.md"); ex_pathq_push(q, "b.md"); ex_pathq_push(q, "a.md"); ex_pathq_push(q, "dir/c.md"); ex_pathq_push(q, "b.md");
    CHECK(ex_pathq_count(q) == 3);                                                         /* a path waits once */
    CHECK(ex_pathq_pop(q, rel, sizeof rel) && !strcmp(rel, "a.md") && ex_pathq_pop(q, rel, sizeof rel) && !strcmp(rel, "b.md") && ex_pathq_pop(q, rel, sizeof rel) && !strcmp(rel, "dir/c.md"));
    CHECK(!ex_pathq_pop(q, rel, sizeof rel));
    ex_pathq_push(q, "a.md");                                                              /* once read, it can be reported again */
    CHECK(ex_pathq_pop(q, rel, sizeof rel) && !strcmp(rel, "a.md"));
    ex_pathq_push(q, "x"); ex_pathq_overflow(q); ex_pathq_overflow(q);
    CHECK(ex_pathq_pop(q, rel, sizeof rel) && rel[0] == 0);                                /* lost events: the empty path, once, before the rest */
    CHECK(ex_pathq_pop(q, rel, sizeof rel) && !strcmp(rel, "x") && !ex_pathq_pop(q, rel, sizeof rel));
    /* many paths: the order is kept while the ring grows and wraps */
    for (i = 0; i < 5000; i++) { char p[32]; snprintf(p, sizeof p, "n/%zu.md", i); ex_pathq_push(q, p); }
    for (i = 0; i < 2500; i++) { char p[32]; CHECK(ex_pathq_pop(q, rel, sizeof rel)); snprintf(p, sizeof p, "n/%zu.md", i); if (strcmp(rel, p)) { CHECK(0 && "order"); break; } }
    for (i = 5000; i < 7000; i++) { char p[32]; snprintf(p, sizeof p, "n/%zu.md", i); ex_pathq_push(q, p); }
    for (i = 2500; i < 7000; i++) { char p[32]; if (!ex_pathq_pop(q, rel, sizeof rel)) { CHECK(0 && "short"); break; } snprintf(p, sizeof p, "n/%zu.md", i); if (strcmp(rel, p)) { CHECK(0 && "order 2"); break; } }
    CHECK(ex_pathq_count(q) == 0);
    ex_pathq_free(q);
}

static void put(const char *rel, const char *text) {
    char p[400];
    snprintf(p, sizeof p, ROOT "/%s", rel);
    CHECK(ex_platform_write_file_atomic(p, text, strlen(text)) == 0);
}

/* Reads reports until `want` comes (true) or ms pass; collects every report in seen (one string, paths separated by newlines). */
static int wait_for(ExWatcher *w, const char *want, int ms, char *seen, size_t cap) {
    int waited;
    char rel[512];
    for (waited = 0; waited <= ms; waited += 5) {
        while (ex_watcher_poll(w, rel, sizeof rel)) {
            if (seen && strlen(seen) + strlen(rel) + 2 < cap) { strcat(seen, rel); strcat(seen, "\n"); }
            if (!strcmp(rel, want)) return 1;
        }
        SDL_Delay(5);
    }
    return 0;
}

static int has(const char *seen, const char *path) {
    const char *p = seen;
    size_t n = strlen(path);
    while ((p = strstr(p, path)) != NULL) {
        if ((p == seen || p[-1] == '\n') && p[n] == '\n') return 1;
        p += n;
    }
    return 0;
}

static void drain(ExWatcher *w) { char rel[512]; SDL_Delay(300); while (ex_watcher_poll(w, rel, sizeof rel)) {} SDL_Delay(100); while (ex_watcher_poll(w, rel, sizeof rel)) {} }

static void *refuse(const char *root) { (void)root; return NULL; }
static int never(void *c, char *rel, size_t cap) { (void)c; (void)rel; (void)cap; return 0; }
static void nothing(void *c) { (void)c; }
static const ExWatchBackend REFUSING = {"refusing", refuse, never, nothing};

int main(void) {
    ExWatcher *w;
    static char seen[200000];
    const char *excl[] = {"*.tmp"};
    int i, burst_reports = 0, burst_files = 0;
    char rel[512], path[400];

    test_queue();

    ex_platform_remove_tree(ROOT);
    ex_platform_make_dirs(ROOT "/sub");
    ex_platform_make_dirs(ROOT "/.exhaust");
    put("existing.md", "one\n");
    w = ex_watcher_start(ROOT, NULL, excl, 1, 2000);
    CHECK(w != NULL);
    if (!w) return 1;
    CHECK(!ex_watcher_is_polling(w));                                           /* the system's own watcher took the folder */
    drain(w);

    /* a note: created, edited, renamed, deleted */
    seen[0] = 0; put("note.md", "hello\n");
    CHECK(wait_for(w, "note.md", 1000, seen, sizeof seen));
    drain(w);
    seen[0] = 0; put("note.md", "hello again, longer\n");
    CHECK(wait_for(w, "note.md", 1000, seen, sizeof seen));
    drain(w);
    seen[0] = 0;
    CHECK(rename(ROOT "/note.md", ROOT "/moved.md") == 0);
    CHECK(wait_for(w, "moved.md", 1000, seen, sizeof seen));
    if (!has(seen, "note.md")) wait_for(w, "note.md", 1000, seen, sizeof seen);
    CHECK(has(seen, "note.md") && has(seen, "moved.md"));                       /* the old name and the new one */
    drain(w);
    seen[0] = 0;
    CHECK(remove(ROOT "/moved.md") == 0);
    CHECK(wait_for(w, "moved.md", 1000, seen, sizeof seen));
    drain(w);

    /* a folder: created, with a note in it, renamed, deleted */
    seen[0] = 0;
    ex_platform_make_dirs(ROOT "/newdir");
    CHECK(wait_for(w, "newdir", 1000, seen, sizeof seen));
    drain(w);
    seen[0] = 0; put("newdir/inside.md", "x\n");
    CHECK(wait_for(w, "newdir/inside.md", 1000, seen, sizeof seen));            /* a note in a folder made after the watch began */
    drain(w);
    seen[0] = 0;
    CHECK(rename(ROOT "/newdir", ROOT "/renamed") == 0);
    CHECK(wait_for(w, "renamed", 1000, seen, sizeof seen));
    if (!has(seen, "newdir")) wait_for(w, "newdir", 1000, seen, sizeof seen);
    CHECK(has(seen, "newdir") && has(seen, "renamed"));
    drain(w);
    seen[0] = 0; put("renamed/again.md", "y\n");
    CHECK(wait_for(w, "renamed/again.md", 1000, seen, sizeof seen));            /* the renamed folder is watched under its new name */
    drain(w);
    seen[0] = 0;
    CHECK(ex_platform_remove_tree(ROOT "/renamed") == 0);
    CHECK(wait_for(w, "renamed", 1000, seen, sizeof seen));
    drain(w);

    /* hidden names and excluded files are never reported */
    seen[0] = 0;
    put(".exhaust/state.ini", "k=v\n"); put(".hidden.md", "h\n"); put("skip.tmp", "t\n"); put("visible.md", "v\n");
    CHECK(wait_for(w, "visible.md", 1000, seen, sizeof seen));
    SDL_Delay(300);
    while (ex_watcher_poll(w, rel, sizeof rel)) { if (strlen(seen) + strlen(rel) + 2 < sizeof seen) { strcat(seen, rel); strcat(seen, "\n"); } }
    CHECK(!strstr(seen, ".exhaust") && !strstr(seen, ".hidden") && !strstr(seen, "skip.tmp"));
    drain(w);

    /* a burst of 1,000 writes: every file is reported, and no more than 1,000 times */
    ex_platform_make_dirs(ROOT "/burst");
    drain(w);
    for (i = 0; i < 1000; i++) { snprintf(path, sizeof path, "burst/f%04d.md", i); put(path, "content\n"); }
    {   static char got[1000];
        int waited;
        memset(got, 0, sizeof got);
        for (waited = 0; waited < 8000 && burst_files < 1000; waited += 10) {
            while (ex_watcher_poll(w, rel, sizeof rel)) {
                int k;
                if (!rel[0]) { burst_files = 1000; burst_reports = -1; break; }                             /* lost events were reported: nothing was lost silently */
                if (sscanf(rel, "burst/f%d.md", &k) == 1 && k >= 0 && k < 1000) { burst_reports++; if (!got[k]) { got[k] = 1; burst_files++; } }
            }
            SDL_Delay(10);
        }
        CHECK(burst_files == 1000);                                                                         /* none lost (or an honest "everything changed") */
        CHECK(burst_reports <= 1000);                                                                       /* at most one report for each file */
    }
    ex_watcher_stop(w);

    /* with the native watcher forced to fail, the polling fallback still reports an outside edit */
    w = ex_watcher_start(ROOT, &REFUSING, NULL, 0, 50);
    CHECK(w != NULL && ex_watcher_is_polling(w));
    if (w) {
        SDL_Delay(200);
        seen[0] = 0; put("existing.md", "changed outside, and longer\n");
        CHECK(wait_for(w, "existing.md", 3000, seen, sizeof seen));
        ex_watcher_stop(w);
    }
    ex_platform_remove_tree(ROOT);
    if (failures) { fprintf(stderr, "watch_native: %d failed\n", failures); return 1; }
    printf("watch_native: ok\n");
    return 0;
}
