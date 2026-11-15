/* graph (sub-slice 5a): the graph's data from the index. A small vault written by the test has every case: a link to a note, the same link
 * twice (one edge), a link to the note itself (none), a link by alias, by path and in another case, an embed of an image (an attachment),
 * two notes that link to a name that does not exist (one shared unresolved node), a note with no links at all, and a link from a folder
 * that must resolve to the note in its own folder. Then the performance vault (5,000 notes, 20,000 links) is read: the node and edge counts
 * are right and the index is asked far fewer questions than there are links.
 * Usage: test_graph <gen_vault> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_graph.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char *root, const char *rel, const char *text) {
    char path[512], dir[512], *slash;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    CHECK(ex_platform_write_file_atomic(path, text, strlen(text)) == 0);
}

static long node_of(const ExGraph *g, const char *path, int kind) {
    size_t i;
    for (i = 0; i < g->n; i++) if (g->nodes[i].kind == kind && !strcmp(g->nodes[i].path, path)) return (long)i;
    return -1;
}

static int has_edge(const ExGraph *g, long a, long b) {
    size_t i;
    for (i = 0; i < g->ne; i++) if ((long)g->edges[i].from == a && (long)g->edges[i].to == b) return 1;
    return 0;
}

static void small(void) {
    ExIndex *w, *r;
    ExGraph g;
    char err[200], *root;
    long a, b, c, orphan, pic, missing, gdeep, gtop, h;
    size_t i;
    ex_platform_remove_tree("graph_small");
    put("graph_small", "A.md", "---\ntitle: x\n---\nSee [[B]] and again [[B]], and [[b|bee]]. Self: [[A]] [[#Heading]]. ![[pic.png]] [[Missing note]]\n\n# Heading\n");
    put("graph_small", "B.md", "---\naliases: [Bee]\n---\nBack to [[A]]. By alias [[Bee]] is itself. Path: [[Folder/Deep]]\n");
    put("graph_small", "C.md", "Links to [[Missing Note]] (another case) and [[a]].\n");
    put("graph_small", "Orphan.md", "No links here.\n");
    put("graph_small", "Folder/Deep.md", "In a folder: [[Gamma]] and [[A]].\n");
    put("graph_small", "Folder/Gamma.md", "folder gamma\n");
    put("graph_small", "Gamma.md", "top gamma\n");
    put("graph_small", "pic.png", "not really a png");
    root = ex_platform_realpath("graph_small");
    ex_platform_remove_tree("graph_small_db");
    w = ex_index_open("graph_small_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    r = ex_index_open("graph_small_db/index.db", 0, err, sizeof err);
    CHECK(r != NULL);
    if (!w || !r) return;
    CHECK(ex_graph_load(r, &g) == 0);

    a = node_of(&g, "A.md", EX_GRAPH_NOTE); b = node_of(&g, "B.md", EX_GRAPH_NOTE); c = node_of(&g, "C.md", EX_GRAPH_NOTE);
    orphan = node_of(&g, "Orphan.md", EX_GRAPH_NOTE); pic = node_of(&g, "pic.png", EX_GRAPH_ATTACHMENT);
    missing = node_of(&g, "Missing note", EX_GRAPH_UNRESOLVED);
    gdeep = node_of(&g, "Folder/Gamma.md", EX_GRAPH_NOTE); gtop = node_of(&g, "Gamma.md", EX_GRAPH_NOTE);
    h = node_of(&g, "Folder/Deep.md", EX_GRAPH_NOTE);
    CHECK(a >= 0 && b >= 0 && c >= 0 && orphan >= 0 && pic >= 0 && missing >= 0 && gdeep >= 0 && gtop >= 0 && h >= 0);
    CHECK(g.n == 9);                                         /* 7 notes, the image, one unresolved name */
    CHECK(node_of(&g, "Missing Note", EX_GRAPH_UNRESOLVED) < 0);   /* C's link is the same name in another case: no second node */
    if (a < 0 || b < 0 || c < 0 || orphan < 0 || pic < 0 || missing < 0 || gdeep < 0 || gtop < 0 || h < 0) { ex_graph_free(&g); return; }
    CHECK(g.nodes[a].ctime > 0 && g.nodes[orphan].ctime > 0 && g.nodes[pic].ctime == 0 && g.nodes[missing].ctime == 0);   /* a note has its file's creation time (5c), other nodes none */
    CHECK(has_edge(&g, a, b));                               /* [[B]] twice and [[b|bee]]: one edge */
    CHECK(has_edge(&g, b, a));                               /* direction is kept: B links back to A */
    CHECK(has_edge(&g, a, pic) && has_edge(&g, a, missing) && has_edge(&g, c, missing));
    CHECK(has_edge(&g, c, a));                               /* [[a]] in another case */
    CHECK(has_edge(&g, b, h));                               /* by path */
    CHECK(has_edge(&g, h, gdeep) && !has_edge(&g, h, gtop)); /* from a folder, the note in the same folder wins */
    CHECK(has_edge(&g, h, a));
    CHECK(!has_edge(&g, a, a) && !has_edge(&g, b, b));       /* no link to itself (also by alias) */
    CHECK(g.ne == 9);
    CHECK(g.nodes[orphan].degree == 0);                      /* kept, with no edge */
    CHECK(g.nodes[a].degree == 6 && g.nodes[missing].degree == 2 && g.nodes[b].degree == 3);   /* A: to B, pic and the missing name, from B, C and Deep */
    for (i = 1; i < g.ne; i++) CHECK(g.edges[i - 1].from < g.edges[i].from || (g.edges[i - 1].from == g.edges[i].from && g.edges[i - 1].to < g.edges[i].to));   /* sorted, no duplicates */
    {   size_t len = 0; const char *l = ex_graph_label(&g.nodes[h], &len); CHECK(len == 4 && !strncmp(l, "Deep", 4)); }
    ex_graph_free(&g);
    ex_index_close(r); ex_index_close(w);
    free(root);
}

static void big(const char *tool) {
    const char *args[3];
    SDL_Process *p;
    int code = -1;
    ExIndex *w, *r;
    ExGraph g;
    char *root, err[200];
    size_t i, deg_sum = 0, unres = 0;
    Uint64 t0, t1;
    ex_platform_remove_tree("graph_perf_vault");
    ex_platform_remove_tree("graph_perf_db");
    args[0] = tool; args[1] = "graph_perf_vault"; args[2] = NULL;
    p = SDL_CreateProcess(args, false);
    CHECK(p != NULL);
    if (!p) return;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    CHECK(code == 0);
    root = ex_platform_realpath("graph_perf_vault");
    w = ex_index_open("graph_perf_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    r = ex_index_open("graph_perf_db/index.db", 0, err, sizeof err);
    if (!w || !r) return;
    t0 = SDL_GetTicksNS();
    CHECK(ex_graph_load(r, &g) == 0);
    t1 = SDL_GetTicksNS();
    for (i = 0; i < g.n; i++) { deg_sum += g.nodes[i].degree; if (g.nodes[i].kind == EX_GRAPH_UNRESOLVED) unres++; }
    printf("graph: %zu nodes, %zu edges, %zu links read, %zu index questions, %.0f ms\n", g.n, g.ne, g.links_read, g.resolves, (double)(t1 - t0) / 1e6);
    CHECK(g.n >= 5000);                                      /* every note is a node */
    CHECK(g.links_read >= 19000 && g.links_read <= 21000);   /* the vault has 20,000 links */
    CHECK(g.ne > 12000 && g.ne <= g.links_read);             /* repeats and self links make it fewer */
    CHECK(deg_sum == 2 * g.ne);                              /* every edge touches two nodes */
    CHECK(g.resolves > 0 && g.resolves <= g.links_read);     /* never more questions than links */
    (void)unres;
    {   /* 5b: the filters and groups on the big graph (run on the service thread in the app): the time is printed, the result is checked */
        ExGraphOptions o;
        unsigned bad = 0;
        size_t n0 = g.n, ne0 = g.ne;
        ex_graphopts_defaults(&o);
        snprintf(o.search, sizeof o.search, "Copper");
        snprintf(o.groups[0].query, sizeof o.groups[0].query, "path:Area");
        snprintf(o.groups[1].query, sizeof o.groups[1].query, "tag:#x");
        o.show_orphans = 0;
        t0 = SDL_GetTicksNS();
        CHECK(ex_graph_apply_options(r, root, &o, &g, &bad, NULL, 0) == 0 && bad == 0);
        t1 = SDL_GetTicksNS();
        printf("graph filter (a search, two groups, no orphans) on %zu nodes: %zu nodes and %zu edges left, %.0f ms\n", n0, g.n, g.ne, (double)(t1 - t0) / 1e6);
        CHECK(g.n > 0 && g.n < n0 && g.ne < ne0);
        for (i = 0, deg_sum = 0; i < g.n; i++) { deg_sum += g.nodes[i].degree; CHECK(g.nodes[i].degree > 0); }
        CHECK(deg_sum == 2 * g.ne);
    }
    ex_graph_free(&g);
    ex_index_close(r); ex_index_close(w);
    free(root);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: test_graph <gen_vault>\n"); return 2; }
    small();
    big(argv[1]);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
