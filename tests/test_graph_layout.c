/* graph_perf (sub-slice 5a): the layout of the performance vault's size (5,000 nodes, 20,000 edges). It measures WORK, not frames per
 * second, so a slow runner cannot fail it (AGENTS.md): the number of repulsion terms of a step against all pairs, the number of steps to
 * settle, and the accuracy of the approximation against the exact forces. It also checks that the UI side of the triple buffer never sees
 * a torn copy while the worker runs, that a changed graph starts where the old one was, that a layout is reproducible, and that stopping
 * a running layout works.
 * Usage: test_graph_layout */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_layout.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char *dup_s(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }

static uint32_t rs = 12345u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

/* A random graph with some hubs (the endpoint is the square of a uniform number, so low indexes are busy), as links are. */
static void make_graph(ExGraph *g, int n, int ne) {
    int i, k;
    unsigned char *seen = NULL;
    memset(g, 0, sizeof *g);
    g->nodes = calloc((size_t)n, sizeof *g->nodes);
    g->edges = calloc((size_t)ne, sizeof *g->edges);
    for (i = 0; i < n; i++) { char p[32]; snprintf(p, sizeof p, "n%05d.md", i); g->nodes[i].path = dup_s(p); g->nodes[i].kind = EX_GRAPH_NOTE; }
    (void)seen;
    for (k = 0; k < ne;) {
        double u = (double)(rnd() & 0xffffff) / 16777216.0;
        uint32_t a = (uint32_t)((double)n * u * u), b = rnd() % (uint32_t)n;
        if (a == b || a >= (uint32_t)n) continue;
        g->edges[k].from = a; g->edges[k].to = b; k++;
    }
    g->n = (size_t)n; g->ne = (size_t)ne;
}

static void free_graph(ExGraph *g) { size_t i; for (i = 0; i < g->n; i++) free(g->nodes[i].path); free(g->nodes); free(g->edges); }

static int wait_settled(ExLayout *l, int seconds) {
    ExLayoutStats st;
    Uint64 t0 = SDL_GetTicks();
    for (;;) {
        ex_layout_stats(l, &st);
        if (st.settled) return 1;
        if (SDL_GetTicks() - t0 > (Uint64)seconds * 1000) return 0;
        SDL_Delay(5);
    }
}

static void accuracy(void) {
    enum { N = 400 };
    float xy[2 * N], fx[N], fy[N], ex[N], ey[N];
    double err9 = 0, err3 = 0, mag = 0;
    unsigned long w9, w3;
    int i;
    for (i = 0; i < 2 * N; i++) xy[i] = (float)(rnd() % 6000) / 10.0f;
    ex_layout_repulsion_exact(xy, N, -30.0f, ex, ey);
    w9 = ex_layout_repulsion_bh(xy, N, -30.0f, 0.9f, fx, fy);
    for (i = 0; i < N; i++) { err9 += hypot(fx[i] - ex[i], fy[i] - ey[i]); mag += hypot(ex[i], ey[i]); }
    w3 = ex_layout_repulsion_bh(xy, N, -30.0f, 0.3f, fx, fy);
    for (i = 0; i < N; i++) err3 += hypot(fx[i] - ex[i], fy[i] - ey[i]);
    printf("accuracy: error at theta 0.9 = %.2f%%, at 0.3 = %.2f%%; terms %lu and %lu of %d\n", 100 * err9 / mag, 100 * err3 / mag, w9, w3, N * (N - 1));
    CHECK(err9 / mag < 0.05);                                 /* the approximation is close to the exact forces */
    CHECK(err3 < err9);                                       /* and a smaller angle is closer */
    CHECK(w9 < w3 && w3 < (unsigned long)N * (N - 1));       /* and does less work than all pairs, the more the wider the angle */
}

static void big(void) {
    enum { N = 5000, E = 20000 };
    ExGraph g;
    ExLayout *l, *l2;
    ExLayoutStats st;
    const float *p, *p2;
    uint64_t gen = 0, last = 0;
    int i, torn = 0, nonmono = 0;
    long reads = 0;
    double edge_len = 0, pair_len = 0;
    float *saved;

    make_graph(&g, N, E);
    l = ex_layout_create(&g, NULL, NULL, 7);
    CHECK(l != NULL);
    if (!l) { free_graph(&g); return; }

    /* the UI side reads as fast as it can while the worker writes: every copy whole, generations never go back */
    for (;;) {
        p = ex_layout_positions(l, &gen);
        reads++;
        if (!ex_layout_front_consistent(l)) torn++;
        if (gen < last) nonmono++;
        last = gen;
        ex_layout_stats(l, &st);
        if (st.settled) break;
        if (reads > 400000000L) break;
    }
    CHECK(reads > 1000);
    CHECK(torn == 0 && nonmono == 0);
    CHECK(wait_settled(l, 120));
    ex_layout_stats(l, &st);
    p = ex_layout_positions(l, &gen);
    printf("layout: %lu steps to settle, %lu repulsion terms in the last step (all pairs: %d), %ld reads by the UI side, generation %llu\n", st.iterations, st.interactions, N * (N - 1), reads, (unsigned long long)gen);
    CHECK(st.iterations >= 250 && st.iterations <= 400);     /* cooling from 1 to 0.001 takes about 300 steps */
    CHECK(st.interactions * 5 <= (unsigned long)N * (N - 1)); /* at least five times less work than all pairs, at 5,000 nodes */
    CHECK(st.alpha < 0.0011f);

    for (i = 0; i < 2 * N; i++) if (!isfinite(p[i])) { CHECK(0); break; }
    for (i = 0; i < E; i++) { int a = (int)g.edges[i].from, b = (int)g.edges[i].to; edge_len += hypot(p[2 * a] - p[2 * b], p[2 * a + 1] - p[2 * b + 1]); }
    for (i = 0; i < E; i++) { int a = (int)(rnd() % N), b = (int)(rnd() % N); pair_len += hypot(p[2 * a] - p[2 * b], p[2 * a + 1] - p[2 * b + 1]); }
    printf("layout: mean edge %.1f, mean random pair %.1f\n", edge_len / E, pair_len / E);
    CHECK(edge_len < pair_len * 0.75);                        /* linked notes ended up close: the springs work */

    /* the same seed gives the same picture */
    saved = malloc(2 * N * sizeof *saved);
    memcpy(saved, p, 2 * N * sizeof *saved);
    l2 = ex_layout_create(&g, NULL, NULL, 7);
    CHECK(l2 && wait_settled(l2, 120));
    p2 = l2 ? ex_layout_positions(l2, NULL) : NULL;
    CHECK(p2 && memcmp(saved, p2, 2 * N * sizeof *saved) == 0);
    ex_layout_free(l2);

    /* a changed graph starts where the old one was: the first 90% of the nodes keep their places, the layout starts warm */
    {
        ExGraph g2;
        ExLayoutStats s2;
        const float *q;
        int kept_same = 0, kept = 0;
        make_graph(&g2, N + 200, E);
        for (i = 0; i < 200; i++) { char pth[32]; snprintf(pth, sizeof pth, "new%03d.md", i); free(g2.nodes[N + i].path); g2.nodes[N + i].path = dup_s(pth); }
        l2 = ex_layout_create(&g2, &g, saved, 9);
        CHECK(l2 != NULL);
        if (l2) {
            /* read before the worker has moved much: the first step has already run at most a few times, so compare loosely */
            q = ex_layout_positions(l2, NULL);
            for (i = 0; i < N; i++) { kept++; if (fabsf(q[2 * i] - saved[2 * i]) < 40.0f && fabsf(q[2 * i + 1] - saved[2 * i + 1]) < 40.0f) kept_same++; }
            CHECK(kept_same > kept * 9 / 10);
            CHECK(wait_settled(l2, 120));
            ex_layout_stats(l2, &s2);
            CHECK(s2.iterations < st.iterations);             /* warm: it cools from 0.3, not from 1 */
            ex_layout_free(l2);
        }
        free_graph(&g2);
    }
    free(saved);

    /* reheating a settled layout makes it step again, and it settles again */
    ex_layout_reheat(l);
    {
        unsigned long before = st.iterations;
        Uint64 t0 = SDL_GetTicks();
        do { SDL_Delay(5); ex_layout_stats(l, &st); } while (st.iterations == before && SDL_GetTicks() - t0 < 10000);
        CHECK(st.iterations > before);
        CHECK(wait_settled(l, 120));
    }
    ex_layout_free(l);
    free_graph(&g);
}

static void stop_while_running(void) {
    ExGraph g;
    int i;
    make_graph(&g, 3000, 9000);
    for (i = 0; i < 12; i++) {
        ExLayout *l = ex_layout_create(&g, NULL, NULL, (uint32_t)i + 1);
        CHECK(l != NULL);
        SDL_Delay((Uint32)(i % 4) * 3);                       /* stopped at different moments: before the first step, in the middle */
        ex_layout_free(l);                                    /* must return, not hang */
    }
    free_graph(&g);
}

static void empty_and_tiny(void) {
    ExGraph g;
    ExLayout *l;
    const float *p;
    make_graph(&g, 1, 0);
    l = ex_layout_create(&g, NULL, NULL, 1);
    CHECK(l && wait_settled(l, 20));
    p = l ? ex_layout_positions(l, NULL) : NULL;
    CHECK(p && isfinite(p[0]) && isfinite(p[1]));
    ex_layout_free(l);
    free_graph(&g);
    memset(&g, 0, sizeof g);                                  /* no nodes at all */
    l = ex_layout_create(&g, NULL, NULL, 1);
    CHECK(l && wait_settled(l, 20));
    ex_layout_free(l);
}

int main(void) {
    accuracy();
    big();
    stop_while_running();
    empty_and_tiny();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
