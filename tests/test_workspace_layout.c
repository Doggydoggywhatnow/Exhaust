/* workspace_layout: tabs, splits, pop-out windows, sidebars, pinning and links on the
 * workspace model, driven by scripted calls; every step checks the tree invariants
 * (no split with fewer than two children, shares add up to 1, parents match children,
 * every tab is in exactly one group). Also session_restore: serialize, rebuild, compare. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_workspace.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void check_node(const ExWorkspace *ws, ExId id, ExId parent, size_t *groups) {
    if (ex_ws_node_kind(ws, id) == EX_NODE_GROUP) {
        size_t i;
        (*groups)++;
        CHECK(ex_ws_node_parent(ws, id) == parent);
        for (i = 0; i < ex_ws_group_tab_count(ws, id); i++) {
            const ExTab *t = ex_ws_tab(ws, ex_ws_group_tab(ws, id, i));
            CHECK(t && t->group == id);
        }
        if (ex_ws_group_tab_count(ws, id)) {
            ExId a = ex_ws_group_active(ws, id);
            size_t k;
            int found = 0;
            for (k = 0; k < ex_ws_group_tab_count(ws, id); k++) if (ex_ws_group_tab(ws, id, k) == a) found = 1;
            CHECK(found);
        }
    } else if (ex_ws_node_kind(ws, id) == EX_NODE_SPLIT) {
        size_t i, n = ex_ws_split_count(ws, id);
        float sum = 0;
        CHECK(n >= 2);
        CHECK(ex_ws_node_parent(ws, id) == parent);
        for (i = 0; i < n; i++) { sum += ex_ws_split_ratio(ws, id, i); check_node(ws, ex_ws_split_child(ws, id, i), id, groups); }
        CHECK(fabsf(sum - 1.0f) < 0.001f);
    } else CHECK(0);
}

static void invariants(const ExWorkspace *ws) {
    size_t i, groups = 0, in_groups = 0;
    CHECK(ex_ws_main_window(ws) != 0);
    for (i = 0; i < ex_ws_window_count(ws); i++) check_node(ws, ex_ws_window_root(ws, ex_ws_window_at(ws, i)), 0, &groups);
    CHECK(groups == ex_ws_group_count(ws));
    for (i = 0; i < ex_ws_tab_count(ws); i++) {
        ExId t = ex_ws_tab_at(ws, i);
        ExId g = ex_ws_tab(ws, t)->group;
        size_t k;
        int found = 0;
        for (k = 0; k < ex_ws_group_tab_count(ws, g); k++) if (ex_ws_group_tab(ws, g, k) == t) found++;
        CHECK(found == 1);
    }
    for (i = 0; i < ex_ws_window_count(ws); i++) { (void)i; }
    (void)in_groups;
    CHECK(ex_ws_node_kind(ws, ex_ws_active_group(ws)) == EX_NODE_GROUP);
}

static void test_tabs_and_splits(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g0, a, b, c, g1, g2, g3;
    CHECK(ws);
    g0 = ex_ws_active_group(ws);
    CHECK(ex_ws_group_count(ws) == 1 && ex_ws_window_count(ws) == 1);
    a = ex_ws_new_tab(ws, 0, "a.md", "note");
    b = ex_ws_new_tab(ws, 0, "b.md", "note");
    c = ex_ws_new_tab(ws, 0, "c.md", "note");
    CHECK(a && b && c && ex_ws_group_active(ws, g0) == c);
    invariants(ws);
    CHECK(ex_ws_find_tab(ws, "b.md") == b);

    /* split right with tab c: two groups side by side */
    g1 = ex_ws_split(ws, g0, EX_SPLIT_HORIZONTAL, 1, c);
    CHECK(g1 && ex_ws_group_count(ws) == 2);
    CHECK(ex_ws_tab(ws, c)->group == g1);
    CHECK(ex_ws_node_kind(ws, ex_ws_window_root(ws, ex_ws_main_window(ws))) == EX_NODE_SPLIT);
    invariants(ws);
    /* same direction again adds a column to the same split */
    g2 = ex_ws_split(ws, g1, EX_SPLIT_HORIZONTAL, 1, 0);
    CHECK(g2 && ex_ws_split_count(ws, ex_ws_window_root(ws, ex_ws_main_window(ws))) == 3);
    invariants(ws);
    /* the other direction nests */
    g3 = ex_ws_split(ws, g0, EX_SPLIT_VERTICAL, 1, 0);
    CHECK(g3 && ex_ws_node_kind(ws, ex_ws_node_parent(ws, g0)) == EX_NODE_SPLIT && ex_ws_split_dir(ws, ex_ws_node_parent(ws, g0)) == EX_SPLIT_VERTICAL);
    invariants(ws);
    /* the only tab of a group cannot be split off */
    CHECK(ex_ws_split(ws, g1, EX_SPLIT_VERTICAL, 1, c) == 0);

    /* ratio dragging keeps the sum and the minimum share */
    {
        ExId root = ex_ws_window_root(ws, ex_ws_main_window(ws));
        CHECK(ex_ws_set_ratio(ws, root, 0, 0.6f) == 0);
        CHECK(fabsf(ex_ws_split_ratio(ws, root, 0) - 0.6f) < 0.001f);
        CHECK(ex_ws_set_ratio(ws, root, 0, 5.0f) == 0);
        CHECK(ex_ws_split_ratio(ws, root, 1) >= 0.049f);
        invariants(ws);
    }

    /* moving a tab between groups; an emptied group goes away and splits collapse */
    CHECK(ex_ws_move_tab(ws, c, g0, 0) == 0);
    CHECK(ex_ws_tab(ws, c)->group == g0);
    invariants(ws);
    CHECK(ex_ws_close_tab(ws, a) == 0 && ex_ws_close_tab(ws, b) == 0 && ex_ws_close_tab(ws, c) == 0);
    invariants(ws);
    CHECK(ex_ws_tab_count(ws) == 0);
    ex_ws_free(ws);
}

static void test_collapse(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g0 = ex_ws_active_group(ws), t0 = ex_ws_new_tab(ws, 0, "x.md", "note"), t1 = ex_ws_new_tab(ws, 0, "y.md", "note"), g1;
    g1 = ex_ws_split(ws, g0, EX_SPLIT_HORIZONTAL, 1, t1);
    CHECK(g1 && ex_ws_group_count(ws) == 2);
    ex_ws_close_tab(ws, t1);                              /* g1 is empty: it and the split are gone */
    CHECK(ex_ws_group_count(ws) == 1);
    CHECK(ex_ws_node_kind(ws, ex_ws_window_root(ws, ex_ws_main_window(ws))) == EX_NODE_GROUP);
    CHECK(ex_ws_tab(ws, t0) != NULL);
    invariants(ws);
    ex_ws_close_tab(ws, t0);                              /* the last group of the main window stays, empty */
    CHECK(ex_ws_group_count(ws) == 1);
    invariants(ws);
    ex_ws_free(ws);
}

static void test_pinning(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g = ex_ws_active_group(ws), a = ex_ws_new_tab(ws, 0, "a.md", "note"), b = ex_ws_new_tab(ws, 0, "b.md", "note"), c = ex_ws_new_tab(ws, 0, "c.md", "note");
    CHECK(ex_ws_set_pinned(ws, c, 1) == 0);
    CHECK(ex_ws_group_tab(ws, g, 0) == c);
    CHECK(ex_ws_set_pinned(ws, b, 1) == 0);
    CHECK(ex_ws_group_tab(ws, g, 0) == c && ex_ws_group_tab(ws, g, 1) == b && ex_ws_group_tab(ws, g, 2) == a);
    CHECK(ex_ws_move_tab(ws, a, g, 0) == 0);                  /* an unpinned tab cannot go in front of pinned ones */
    CHECK(ex_ws_group_tab(ws, g, 2) == a);
    CHECK(ex_ws_move_tab(ws, c, g, 5) == 0);                  /* a pinned tab cannot go behind the unpinned */
    CHECK(ex_ws_group_tab(ws, g, 1) == c);
    CHECK(ex_ws_set_pinned(ws, c, 0) == 0);
    CHECK(ex_ws_group_tab(ws, g, 0) == b && ex_ws_group_tab(ws, g, 1) == c);
    invariants(ws);
    ex_ws_free(ws);
}

static void test_popout(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g0 = ex_ws_active_group(ws), a = ex_ws_new_tab(ws, 0, "a.md", "note"), b = ex_ws_new_tab(ws, 0, "b.md", "note"), w, w2;
    w = ex_ws_popout(ws, b);
    CHECK(w && w != ex_ws_main_window(ws) && ex_ws_window_count(ws) == 2);
    CHECK(ex_ws_window_of_node(ws, ex_ws_tab(ws, b)->group) == w);
    CHECK(ex_ws_group_active(ws, g0) == a);
    invariants(ws);
    CHECK(ex_ws_popout(ws, b) == w);                            /* already alone in its window */
    w2 = ex_ws_popout(ws, a);
    CHECK(w2 && ex_ws_window_count(ws) == 3);
    invariants(ws);
    CHECK(ex_ws_close_window(ws, w) == 0);                     /* its tab returns to the main window */
    CHECK(ex_ws_window_count(ws) == 2);
    CHECK(ex_ws_window_of_node(ws, ex_ws_tab(ws, b)->group) == ex_ws_main_window(ws));
    invariants(ws);
    CHECK(ex_ws_close_tab(ws, a) == 0);                         /* the pop-out window with no tab left closes */
    CHECK(ex_ws_window_count(ws) == 1);
    CHECK(ex_ws_close_window(ws, ex_ws_main_window(ws)) != 0);
    invariants(ws);
    ex_ws_free(ws);
}

static void test_sidebars(void) {
    ExWorkspace *ws = ex_ws_create();
    CHECK(ex_ws_sidebar_visible(ws, EX_SIDE_LEFT) && ex_ws_sidebar_visible(ws, EX_SIDE_RIGHT));
    CHECK(ex_ws_sidebar_width(ws, EX_SIDE_LEFT) == 260 && ex_ws_sidebar_width(ws, EX_SIDE_RIGHT) == 300);
    ex_ws_set_sidebar(ws, EX_SIDE_LEFT, 0, 10);
    CHECK(!ex_ws_sidebar_visible(ws, EX_SIDE_LEFT) && ex_ws_sidebar_width(ws, EX_SIDE_LEFT) == EX_SIDEBAR_MIN_WIDTH);
    CHECK(ex_ws_open_panel(ws, EX_SIDE_RIGHT, "outline") == 0 && ex_ws_open_panel(ws, EX_SIDE_RIGHT, "outline") == 0);
    CHECK(ex_ws_panel_count(ws, EX_SIDE_RIGHT) == 1);
    CHECK(ex_ws_open_panel(ws, EX_SIDE_LEFT, "files") == 0 && ex_ws_close_panel(ws, EX_SIDE_RIGHT, "outline") == 0);
    CHECK(ex_ws_panel_count(ws, EX_SIDE_RIGHT) == 0 && ex_ws_close_panel(ws, EX_SIDE_RIGHT, "outline") != 0);
    ex_ws_free(ws);
}

static void test_links_and_revision(void) {
    ExWorkspace *ws = ex_ws_create();
    ExId g0 = ex_ws_active_group(ws), t = ex_ws_new_tab(ws, 0, "a.md", "note"), g1 = ex_ws_split(ws, g0, EX_SPLIT_HORIZONTAL, 1, 0);
    unsigned long r;
    CHECK(ex_ws_link_groups(ws, g0, g1) == 0 && ex_ws_linked_count(ws, g0) == 1 && ex_ws_linked_group(ws, g1, 0) == g0);
    CHECK(ex_ws_link_groups(ws, g0, g0) != 0);
    r = ex_ws_revision(ws);
    ex_ws_set_tab_state(ws, t, 3, 4, 10.0f);
    CHECK(ex_ws_revision(ws) > r);
    r = ex_ws_revision(ws);
    ex_ws_set_tab_state(ws, t, 3, 4, 10.0f);                    /* no change, no new revision */
    CHECK(ex_ws_revision(ws) == r);
    ex_ws_close_group(ws, g1);
    CHECK(ex_ws_linked_count(ws, g0) == 0 && ex_ws_group_count(ws) == 1);
    invariants(ws);
    ex_ws_free(ws);
}

/* ---- session_restore ---- */

static int same_node(const ExWorkspace *a, ExId x, const ExWorkspace *b, ExId y) {
    if (ex_ws_node_kind(a, x) != ex_ws_node_kind(b, y)) return 0;
    if (ex_ws_node_kind(a, x) == EX_NODE_SPLIT) {
        size_t i;
        if (ex_ws_split_dir(a, x) != ex_ws_split_dir(b, y) || ex_ws_split_count(a, x) != ex_ws_split_count(b, y)) return 0;
        for (i = 0; i < ex_ws_split_count(a, x); i++) {
            if (fabsf(ex_ws_split_ratio(a, x, i) - ex_ws_split_ratio(b, y, i)) > 0.001f) return 0;
            if (!same_node(a, ex_ws_split_child(a, x, i), b, ex_ws_split_child(b, y, i))) return 0;
        }
        return 1;
    } else {
        size_t i;
        if (ex_ws_group_tab_count(a, x) != ex_ws_group_tab_count(b, y)) return 0;
        for (i = 0; i < ex_ws_group_tab_count(a, x); i++) {
            const ExTab *p = ex_ws_tab(a, ex_ws_group_tab(a, x, i)), *q = ex_ws_tab(b, ex_ws_group_tab(b, y, i));
            if (!p || !q || strcmp(p->path, q->path) || strcmp(p->view, q->view) || strcmp(p->mode, q->mode) || p->pinned != q->pinned ||
                p->stacked != q->stacked || p->cursor_line != q->cursor_line || p->cursor_column != q->cursor_column || fabsf(p->scroll_y - q->scroll_y) > 0.1f) return 0;
        }
        return ex_ws_group_active(a, x) == ex_ws_group_active(b, y);
    }
}

static void test_session_restore(void) {
    ExWorkspace *ws = ex_ws_create(), *re = ex_ws_create();
    ExId g0 = ex_ws_active_group(ws), a, b, c, g1, g2, w;
    char *text, *text2;
    size_t len, len2;
    a = ex_ws_new_tab(ws, 0, "notes/a.md", "note");
    b = ex_ws_new_tab(ws, 0, "b.md", "note");
    c = ex_ws_new_tab(ws, 0, "Graph", "graph");
    ex_ws_set_pinned(ws, a, 1);
    ex_ws_set_stacked(ws, b, 1);
    ex_ws_set_tab_state(ws, a, 12, 3, 240.5f);
    ex_ws_set_tab_mode(ws, b, "source");
    g1 = ex_ws_split(ws, g0, EX_SPLIT_HORIZONTAL, 1, b);
    g2 = ex_ws_split(ws, g1, EX_SPLIT_VERTICAL, 1, c);
    CHECK(g1 && g2);
    ex_ws_link_groups(ws, g0, g1);
    ex_ws_set_ratio(ws, ex_ws_window_root(ws, ex_ws_main_window(ws)), 0, 0.3f);
    ex_ws_set_sidebar(ws, EX_SIDE_LEFT, 1, 333);
    ex_ws_set_sidebar(ws, EX_SIDE_RIGHT, 0, 280);
    ex_ws_open_panel(ws, EX_SIDE_LEFT, "files");
    ex_ws_open_panel(ws, EX_SIDE_RIGHT, "backlinks");
    ex_ws_window_set_rect(ws, ex_ws_main_window(ws), 10, 20, 1000, 700, 0);
    ex_ws_set_active_group(ws, g1);
    w = ex_ws_popout(ws, c);
    CHECK(w);
    ex_ws_window_set_rect(ws, w, 100, 120, 640, 480, 1);
    invariants(ws);

    text = ex_ws_serialize(ws, &len);
    CHECK(text && len == strlen(text));
    CHECK(ex_ws_deserialize(re, text, len) == 0);
    invariants(re);
    CHECK(ex_ws_window_count(re) == ex_ws_window_count(ws) && ex_ws_tab_count(re) == ex_ws_tab_count(ws) && ex_ws_group_count(re) == ex_ws_group_count(ws));
    {
        size_t i;
        for (i = 0; i < ex_ws_window_count(ws); i++) {
            int x, y, wd, h, m, x2, y2, wd2, h2, m2;
            CHECK(ex_ws_window_at(ws, i) == ex_ws_window_at(re, i));
            CHECK(same_node(ws, ex_ws_window_root(ws, ex_ws_window_at(ws, i)), re, ex_ws_window_root(re, ex_ws_window_at(re, i))));
            ex_ws_window_get_rect(ws, ex_ws_window_at(ws, i), &x, &y, &wd, &h, &m);
            ex_ws_window_get_rect(re, ex_ws_window_at(re, i), &x2, &y2, &wd2, &h2, &m2);
            CHECK(x == x2 && y == y2 && wd == wd2 && h == h2 && m == m2);
        }
    }
    CHECK(ex_ws_main_window(re) == ex_ws_main_window(ws));
    CHECK(ex_ws_sidebar_visible(re, EX_SIDE_RIGHT) == 0 && ex_ws_sidebar_width(re, EX_SIDE_LEFT) == 333 && ex_ws_sidebar_width(re, EX_SIDE_RIGHT) == 280);
    CHECK(ex_ws_panel_count(re, EX_SIDE_LEFT) == 1 && strcmp(ex_ws_panel(re, EX_SIDE_LEFT, 0), "files") == 0);
    CHECK(ex_ws_panel_count(re, EX_SIDE_RIGHT) == 1 && strcmp(ex_ws_panel(re, EX_SIDE_RIGHT, 0), "backlinks") == 0);
    CHECK(ex_ws_linked_count(re, g0) == 1 && ex_ws_linked_group(re, g0, 0) == g1);
    CHECK(ex_ws_active_group(re) == ex_ws_active_group(ws));
    /* the same state writes the same text again */
    text2 = ex_ws_serialize(re, &len2);
    CHECK(text2 && len2 == len && memcmp(text, text2, len) == 0);
    /* new ids do not collide with the restored ones */
    {
        ExId n = ex_ws_new_tab(re, 0, "new.md", "note");
        CHECK(n && n > c);
        invariants(re);
    }
    free(text); free(text2);
    ex_ws_free(ws); ex_ws_free(re);
}

static void test_damaged_and_unknown(void) {
    ExWorkspace *ws = ex_ws_create();
    const char *old = "[session]\nversion = 1\n[tab.1]\npath = a.md\nview = note\nmode = live\n[tab.2]\npath = b.md\nview = note\nmode = live\n[layout]\nleft_width = 99999\n";
    const char *loop = "[window.1]\nmain = true\nroot = s1\n[split.1]\nchildren = s1, g2\n[group.2]\ntabs = 5\n";
    const char *future = "[session]\nversion = 7\n[window.1]\nmain = true\nroot = g1\n[group.1]\ntabs = 1\nfuture_key = hello\n[tab.1]\npath = a.md\nview = note\nmode = live\ngroup = 1\n[plugins]\nsomething = 42\n";
    char *out;
    size_t len;
    /* an old file with no layout tree: one group with all the tabs */
    CHECK(ex_ws_deserialize(ws, old, strlen(old)) == 0);
    invariants(ws);
    CHECK(ex_ws_group_count(ws) == 1 && ex_ws_tab_count(ws) == 2 && ex_ws_group_tab_count(ws, ex_ws_active_group(ws)) == 2);
    CHECK(ex_ws_sidebar_width(ws, EX_SIDE_LEFT) == 1200);
    /* a loop in the tree is cut and does not hang */
    CHECK(ex_ws_deserialize(ws, loop, strlen(loop)) == 0);
    invariants(ws);
    /* garbage gives the default workspace */
    CHECK(ex_ws_deserialize(ws, "\x01\x02 not = [ini\n[[[\n", 17) == 0);
    invariants(ws);
    CHECK(ex_ws_group_count(ws) >= 1);
    /* unknown keys and sections survive a read and a write */
    CHECK(ex_ws_deserialize(ws, future, strlen(future)) == 0);
    out = ex_ws_serialize(ws, &len);
    CHECK(out && strstr(out, "[plugins]") && strstr(out, "something = 42"));
    free(out);
    ex_ws_free(ws);
}

int main(int argc, char **argv) {
    int layout = argc < 2 || strcmp(argv[1], "session") != 0, session = argc < 2 || strcmp(argv[1], "layout") != 0;
    if (layout) {
        test_tabs_and_splits();
        test_collapse();
        test_pinning();
        test_popout();
        test_sidebars();
        test_links_and_revision();
    }
    if (session) {
        test_session_restore();
        test_damaged_and_unknown();
    }
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("workspace tests: ok");
    return 0;
}
