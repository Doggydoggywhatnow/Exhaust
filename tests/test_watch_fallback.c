/* watch_fallback: with a fake watcher backend that refuses every watch (as inotify does
 * when the OS limit is reached), the polling fallback still sees an outside edit; the
 * watcher says it is polling; excluded files are never reported; and the native backend
 * (where there is one) sees changes too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "watch_test"

static void *refuse(const char *root) { (void)root; return NULL; }              /* "no more watches" */
static int never(void *c, char *rel, size_t cap) { (void)c; (void)rel; (void)cap; return 0; }
static void nothing(void *c) { (void)c; }
static const ExWatchBackend REFUSING = {"refusing", refuse, never, nothing};

static void put(const char *rel, const char *text) {
    char p[300];
    snprintf(p, sizeof p, ROOT "/%s", rel);
    ex_platform_write_file_atomic(p, text, strlen(text));
}

/* Waits up to ms for a report of `want`; returns 1 if seen. Other reports are collected in *other. */
static int wait_for_path(ExWatcher *w, const char *want, int ms, int *saw_other_than, const char *forbidden) {
    int waited, seen = 0;
    char rel[512];
    for (waited = 0; waited < ms && !seen; waited += 10) {
        while (ex_watcher_poll(w, rel, sizeof rel)) {
            if (strcmp(rel, want) == 0) seen = 1;
            if (forbidden && strstr(rel, forbidden)) (*saw_other_than)++;
        }
        SDL_Delay(10);
    }
    return seen;
}

int main(void) {
    ExWatcher *w;
    int forbidden_seen = 0;
    const char *excl[] = {"secret/", "*.tmp"};

    ex_platform_remove_tree(ROOT);
    ex_platform_make_dirs(ROOT "/sub");
    ex_platform_make_dirs(ROOT "/secret");
    ex_platform_make_dirs(ROOT "/.exhaust");
    put("a.md", "one\n");
    put("sub/b.md", "two\n");
    put("secret/c.md", "hidden\n");

    /* the refusing backend: the watcher falls back to polling and says so */
    w = ex_watcher_start(ROOT, &REFUSING, excl, 2, 50);
    CHECK(w != NULL && ex_watcher_is_polling(w));
    if (!w) return 1;
    SDL_Delay(200);                                              /* the first scan makes the baseline */
    SDL_Delay(1100);                                             /* modified times can be a whole second apart on some file systems */
    put("a.md", "one changed outside\n");
    CHECK(wait_for_path(w, "a.md", 3000, &forbidden_seen, NULL));            /* an edit */
    put("sub/new.md", "created\n");
    CHECK(wait_for_path(w, "sub/new.md", 3000, &forbidden_seen, NULL));      /* a new file in a subfolder */
    {
        char p[300];
        snprintf(p, sizeof p, ROOT "/sub/b.md");
        ex_platform_remove_tree(p);
    }
    CHECK(wait_for_path(w, "sub/b.md", 3000, &forbidden_seen, NULL));        /* a deleted file */
    /* excluded and hidden files are never reported */
    put("secret/c.md", "hidden but changed\n");
    put("scratch.tmp", "excluded by pattern\n");
    put(".exhaust/settings.ini", "[general]\n");
    put("visible.md", "this one is reported\n");
    CHECK(wait_for_path(w, "visible.md", 3000, &forbidden_seen, "secret"));
    CHECK(forbidden_seen == 0);
    {
        char rel[512];
        int stray = 0;
        SDL_Delay(300);
        while (ex_watcher_poll(w, rel, sizeof rel)) if (strstr(rel, "secret") || strstr(rel, ".tmp") || strstr(rel, ".exhaust")) stray++;
        CHECK(stray == 0);
    }
    ex_watcher_stop(w);

    /* the native backend, where there is one, reports changes without polling */
    w = ex_watcher_start(ROOT, NULL, excl, 2, 50);
    CHECK(w != NULL);
    if (w) {
#ifdef __linux__
        CHECK(!ex_watcher_is_polling(w));
        put("native.md", "native event\n");
        CHECK(wait_for_path(w, "native.md", 3000, &forbidden_seen, NULL));
        put("sub/deeper.md", "in a subfolder\n");
        CHECK(wait_for_path(w, "sub/deeper.md", 3000, &forbidden_seen, NULL));
        ex_platform_make_dirs(ROOT "/newfolder");
        SDL_Delay(100);
        put("newfolder/inside.md", "folders created later are watched too\n");
        CHECK(wait_for_path(w, "newfolder/inside.md", 3000, &forbidden_seen, NULL));
#else
        CHECK(ex_watcher_is_polling(w));                          /* no native backend yet: it polls and says so */
#endif
        ex_watcher_stop(w);
    }
    ex_platform_remove_tree(ROOT);
    return failures ? 1 : 0;
}
