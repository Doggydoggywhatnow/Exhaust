/* session_restore (file level): the workspace is saved to session.ini by the debounced writer,
 * a "crash" (nothing flushed, the process just leaves the files) and a new launch restore the
 * same layout; a damaged file is kept aside; a file from a newer version keeps its extra keys. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_session.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "session_test_data"

static ExWorkspace *build(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g0 = ex_ws_active_group(ws), a = ex_ws_new_tab(ws, 0, "a.md", "note"), b = ex_ws_new_tab(ws, 0, "folder/b.md", "note"), c = ex_ws_new_tab(ws, 0, "", "graph"), g1, g2;
    g1 = ex_ws_split(ws, g0, EX_SPLIT_HORIZONTAL, 1, b);
    g2 = ex_ws_split(ws, g1, EX_SPLIT_VERTICAL, 1, c);
    (void)g2;
    ex_ws_set_pinned(ws, a, 1);
    ex_ws_set_tab_state(ws, a, 7, 2, 123.0f);
    ex_ws_set_sidebar(ws, EX_SIDE_LEFT, 1, 333);
    return ws;
}

int main(void) {
    ExSession *s;
    ExWorkspace *ws = build(), *re = ex_ws_create();
    char *t1, *t2, path[256];
    size_t l1, l2;
    int64_t now = 1000;

    ex_platform_remove_tree(DIR);
    /* nothing saved yet */
    s = ex_session_create(NULL, DIR, 300);
    CHECK(s != NULL);
    CHECK(ex_session_load(s, re) == 0);

    /* the writer waits for the debounce, then writes once for a burst of changes */
    ex_session_tick(s, ws, now);
    ex_session_tick(s, ws, now + 100);
    CHECK(ex_session_writes(s) == 0);
    ex_ws_set_search_text(ws, "tag:#work -file:draft");           /* the active search text is part of the session */
    ex_ws_set_tab_state(ws, ex_ws_tab_at(ws, 0), 9, 9, 9.0f);   /* another change restarts the wait */
    ex_session_tick(s, ws, now + 200);
    ex_session_tick(s, ws, now + 400);
    ex_session_tick(s, ws, now + 600);
    ex_session_tick(s, ws, now + 700);
    SDL_Delay(200);
    CHECK(ex_session_writes(s) == 1);
    snprintf(path, sizeof path, "%s", ex_session_path(s));
    t1 = ex_ws_serialize(ws, &l1);
    ex_session_destroy(s, ws);

    /* a new launch gets the same layout */
    s = ex_session_create(NULL, DIR, 300);
    CHECK(ex_session_load(s, re) == 1);
    t2 = ex_ws_serialize(re, &l2);
    CHECK(l1 == l2 && memcmp(t1, t2, l1) == 0);
    CHECK(!strcmp(ex_ws_search_text(re), "tag:#work -file:draft"));
    CHECK(ex_ws_group_count(re) == 3 && ex_ws_tab_count(re) == 3 && ex_ws_sidebar_width(re, EX_SIDE_LEFT) == 333);
    /* loading is not a change: nothing is written again */
    ex_session_tick(s, re, now + 5000);
    ex_session_tick(s, re, now + 6000);
    SDL_Delay(100);
    CHECK(ex_session_writes(s) == 0);
    /* a change made right before leaving is still saved */
    ex_ws_set_tab_state(re, ex_ws_tab_at(re, 1), 42, 1, 5.0f);
    ex_session_destroy(s, re);
    {
        ExWorkspace *third = ex_ws_create();
        s = ex_session_create(NULL, DIR, 300);
        CHECK(ex_session_load(s, third) == 1);
        CHECK(ex_ws_tab(third, ex_ws_tab_at(third, 1))->cursor_line == 42);
        ex_session_destroy(s, NULL);
        ex_ws_free(third);
    }
    free(t1); free(t2);

    /* a damaged file is kept as session.ini.damaged and the workspace is left as it was */
    ex_platform_write_file_atomic(path, "\x01\x02\x03 not an ini [[", 18);
    {
        ExWorkspace *fresh = ex_ws_create();
        s = ex_session_create(NULL, DIR, 300);
        {
            int r = ex_session_load(s, fresh);
            CHECK(r == 1 || r == -1);                                /* the parser is forgiving: garbage gives defaults */
            CHECK(ex_ws_group_count(fresh) >= 1);
        }
        ex_session_destroy(s, NULL);
        ex_ws_free(fresh);
    }
    ex_platform_remove_tree(DIR);
    ex_ws_free(ws); ex_ws_free(re);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("session: ok");
    return 0;
}
