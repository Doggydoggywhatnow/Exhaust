/* A layered graph layout (6d-ii) for the flowchart, class, state and entity-relationship diagrams: cycles are broken by reversing the edges that close them,
 * ranks follow the longest path (every edge is two ranks long, so the rank between holds its label), long edges get a dummy node on every rank, the order
 * inside a rank is improved by barycentre sweeps, and the position across is found by isotonic regression (nodes keep their order and a minimum gap).
 * Deterministic: the same graph always gives the same picture. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

#define MAX_TOTAL 6000

typedef struct G {
    int n, nreal;
    float *across, *along, *pos;   /* size across the rank, size along it, position across */
    int *rank, *dummy;             /* dummy: -1 for a real node, else the edge it belongs to */
    int **up, **down; int *nup, *ndown, *capup, *capdown;
} G;

static int add_adj(int ***arr, int **cnt, int **cap, int v, int w) {
    if ((*cnt)[v] == (*cap)[v]) {
        int nc = (*cap)[v] ? (*cap)[v] * 2 : 2;
        int *t = realloc((*arr)[v], (size_t)nc * sizeof(int));
        if (!t) return -1;
        (*arr)[v] = t; (*cap)[v] = nc;
    }
    (*arr)[v][(*cnt)[v]++] = w;
    return 0;
}

static void dfs(int v, int ne_ignored, const LgGraph *g, int *state, int **out_edges, int *nout, int *reversed) {
    int i;
    (void)ne_ignored;
    state[v] = 1;
    for (i = 0; i < nout[v]; i++) {
        int e = out_edges[v][i], w = g->edges[e].b;
        if (g->edges[e].a == g->edges[e].b) continue;
        if (state[w] == 1) reversed[e] = 1;
        else if (state[w] == 0) dfs(w, 0, g, state, out_edges, nout, reversed);
    }
    state[v] = 2;
}

void lg_free(LgGraph *g) {
    int i;
    if (!g || !g->edges) return;
    for (i = 0; i < g->ne; i++) { free(g->edges[i].pts); g->edges[i].pts = NULL; g->edges[i].npts = 0; }
}

static float gap_between(const G *s, int a, int b, float nodesep) {
    float sep = (s->dummy[a] >= 0 || s->dummy[b] >= 0) ? 0.5f : nodesep;
    return (s->across[a] + s->across[b]) / 2.0f + sep;
}

/* Puts the nodes of one layer (in order) as close to their wanted positions as their gaps allow: isotonic regression of the wanted positions minus the offsets. */
static void place_layer(G *s, const int *ord, int m, const float *want, float nodesep, float *tmp_off, float *tmp_val, float *tmp_w, int *tmp_cnt) {
    int i, blocks = 0;
    if (m == 0) return;
    tmp_off[0] = 0;
    for (i = 1; i < m; i++) tmp_off[i] = tmp_off[i - 1] + gap_between(s, ord[i - 1], ord[i], nodesep);
    for (i = 0; i < m; i++) {
        float v = want[i] - tmp_off[i];
        tmp_val[blocks] = v; tmp_w[blocks] = 1.0f; tmp_cnt[blocks] = 1; blocks++;
        while (blocks > 1 && tmp_val[blocks - 2] > tmp_val[blocks - 1]) {
            float w1 = tmp_w[blocks - 2], w2 = tmp_w[blocks - 1];
            tmp_val[blocks - 2] = (tmp_val[blocks - 2] * w1 + tmp_val[blocks - 1] * w2) / (w1 + w2);
            tmp_w[blocks - 2] = w1 + w2;
            tmp_cnt[blocks - 2] += tmp_cnt[blocks - 1];
            blocks--;
        }
    }
    {
        int k = 0, b;
        for (b = 0; b < blocks; b++) { int c; for (c = 0; c < tmp_cnt[b]; c++, k++) s->pos[ord[k]] = tmp_val[b] + tmp_off[k]; }
    }
}

int lg_layout(LgGraph *g) {
    int nn = g->nn, ne = g->ne, i, e, total = 0, R = 0, l, it, rc = -1;
    int *state = NULL, *reversed = NULL, **out_edges = NULL, *nout = NULL, *rank = NULL;
    G s;
    int *chain_first = NULL, *chain_len = NULL, *label_dummy = NULL;
    int **layers = NULL, *nlayer = NULL;
    float *rpos = NULL, *thick = NULL, *want = NULL, *t1 = NULL, *t2 = NULL, *t3 = NULL, *bary = NULL;
    int *t4 = NULL, *idx = NULL;
    int vertical = g->dir == 0 || g->dir == 1;
    memset(&s, 0, sizeof s);
    if (nn == 0) return 0;
    state = calloc((size_t)nn, sizeof *state);
    reversed = calloc((size_t)(ne ? ne : 1), sizeof *reversed);
    out_edges = calloc((size_t)nn, sizeof *out_edges);
    nout = calloc((size_t)nn, sizeof *nout);
    rank = calloc((size_t)nn, sizeof *rank);
    if (!state || !reversed || !out_edges || !nout || !rank) goto done;
    for (e = 0; e < ne; e++) if (g->edges[e].a != g->edges[e].b) nout[g->edges[e].a]++;
    for (i = 0; i < nn; i++) { out_edges[i] = malloc((size_t)(nout[i] ? nout[i] : 1) * sizeof(int)); if (!out_edges[i]) goto done; nout[i] = 0; }
    for (e = 0; e < ne; e++) if (g->edges[e].a != g->edges[e].b) out_edges[g->edges[e].a][nout[g->edges[e].a]++] = e;
    for (i = 0; i < nn; i++) if (state[i] == 0) dfs(i, 0, g, state, out_edges, nout, reversed);

    /* ranks: longest path over the acyclic edges, every edge two ranks long per unit of minlen */
    {
        int changed = 1, pass = 0;
        while (changed && pass++ < nn + 2) {
            changed = 0;
            for (e = 0; e < ne; e++) {
                int a = g->edges[e].a, b = g->edges[e].b, ml = g->edges[e].minlen > 0 ? g->edges[e].minlen : 1;
                if (a == b) continue;
                if (reversed[e]) { int t = a; a = b; b = t; }
                if (rank[b] < rank[a] + 2 * ml) { rank[b] = rank[a] + 2 * ml; changed = 1; }
            }
        }
    }
    /* dummies */
    total = nn;
    chain_first = calloc((size_t)(ne ? ne : 1), sizeof *chain_first);
    chain_len = calloc((size_t)(ne ? ne : 1), sizeof *chain_len);
    label_dummy = calloc((size_t)(ne ? ne : 1), sizeof *label_dummy);
    if (!chain_first || !chain_len || !label_dummy) goto done;
    for (e = 0; e < ne; e++) {
        int a = g->edges[e].a, b = g->edges[e].b;
        chain_first[e] = -1; label_dummy[e] = -1;
        if (a == b) continue;
        if (reversed[e]) { int t = a; a = b; b = t; }
        chain_len[e] = rank[b] - rank[a] - 1;
        chain_first[e] = total;
        total += chain_len[e];
        if (total > MAX_TOTAL) goto done;
    }
    s.n = total; s.nreal = nn;
    s.across = calloc((size_t)total, sizeof(float)); s.along = calloc((size_t)total, sizeof(float)); s.pos = calloc((size_t)total, sizeof(float));
    s.rank = calloc((size_t)total, sizeof(int)); s.dummy = malloc((size_t)total * sizeof(int));
    s.up = calloc((size_t)total, sizeof(int *)); s.down = calloc((size_t)total, sizeof(int *));
    s.nup = calloc((size_t)total, sizeof(int)); s.ndown = calloc((size_t)total, sizeof(int)); s.capup = calloc((size_t)total, sizeof(int)); s.capdown = calloc((size_t)total, sizeof(int));
    if (!s.across || !s.along || !s.pos || !s.rank || !s.dummy || !s.up || !s.down || !s.nup || !s.ndown || !s.capup || !s.capdown) goto done;
    for (i = 0; i < total; i++) s.dummy[i] = -1;
    for (i = 0; i < nn; i++) {
        s.across[i] = vertical ? g->nodes[i].w : g->nodes[i].h;
        s.along[i] = vertical ? g->nodes[i].h : g->nodes[i].w;
        s.rank[i] = rank[i];
    }
    for (e = 0; e < ne; e++) {
        int a = g->edges[e].a, b = g->edges[e].b, k, prev;
        if (a == b) continue;
        if (reversed[e]) { int t = a; a = b; b = t; }
        prev = a;
        label_dummy[e] = chain_first[e] + (chain_len[e] - 1) / 2;
        for (k = 0; k < chain_len[e]; k++) {
            int v = chain_first[e] + k;
            s.dummy[v] = e;
            s.rank[v] = rank[a] + 1 + k;
            if (v == label_dummy[e] && (g->edges[e].lw > 0 || g->edges[e].lh > 0)) {
                s.across[v] = (vertical ? g->edges[e].lw : g->edges[e].lh) + 0.4f;
                s.along[v] = vertical ? g->edges[e].lh : g->edges[e].lw;
            }
            if (add_adj(&s.down, &s.ndown, &s.capdown, prev, v) || add_adj(&s.up, &s.nup, &s.capup, v, prev)) goto done;
            prev = v;
        }
        if (add_adj(&s.down, &s.ndown, &s.capdown, prev, b) || add_adj(&s.up, &s.nup, &s.capup, b, prev)) goto done;
    }
    for (i = 0; i < total; i++) if (s.rank[i] > R) R = s.rank[i];
    layers = calloc((size_t)R + 1, sizeof *layers);
    nlayer = calloc((size_t)R + 1, sizeof *nlayer);
    if (!layers || !nlayer) goto done;
    for (i = 0; i < total; i++) nlayer[s.rank[i]]++;
    for (l = 0; l <= R; l++) { layers[l] = malloc((size_t)(nlayer[l] ? nlayer[l] : 1) * sizeof(int)); if (!layers[l]) goto done; nlayer[l] = 0; }
    for (i = 0; i < total; i++) layers[s.rank[i]][nlayer[s.rank[i]]++] = i;
    bary = malloc((size_t)total * sizeof(float));
    idx = malloc((size_t)total * sizeof(int));
    want = malloc((size_t)total * sizeof(float));
    t1 = malloc((size_t)total * sizeof(float)); t2 = malloc((size_t)total * sizeof(float)); t3 = malloc((size_t)total * sizeof(float)); t4 = malloc((size_t)total * sizeof(int));
    if (!bary || !idx || !want || !t1 || !t2 || !t3 || !t4) goto done;

    /* order: barycentre sweeps with a stable insertion sort */
    for (it = 0; it < 8; it++) {
        int down = (it % 2) == 0;
        for (l = down ? 1 : R - 1; down ? l <= R : l >= 0; l += down ? 1 : -1) {
            int m = nlayer[l], k, j;
            for (k = 0; k < m; k++) idx[layers[l][k]] = k;
            for (k = 0; k < m; k++) {
                int v = layers[l][k], c = down ? s.nup[v] : s.ndown[v];
                int *nb = down ? s.up[v] : s.down[v];
                float sum = 0;
                if (c == 0) { bary[v] = (float)k; continue; }
                for (j = 0; j < c; j++) {
                    int w = nb[j], pos = 0, q;
                    for (q = 0; q < nlayer[s.rank[w]]; q++) if (layers[s.rank[w]][q] == w) { pos = q; break; }
                    sum += (float)pos;
                }
                bary[v] = sum / (float)c;
            }
            for (k = 1; k < m; k++) {   /* insertion sort, stable */
                int v = layers[l][k];
                j = k - 1;
                while (j >= 0 && bary[layers[l][j]] > bary[v]) { layers[l][j + 1] = layers[l][j]; j--; }
                layers[l][j + 1] = v;
            }
        }
    }
    /* position across: first packed and centred, then pulled towards the neighbours */
    {
        float nodesep = g->nodesep > 0 ? g->nodesep : 1.5f;
        for (l = 0; l <= R; l++) {
            int m = nlayer[l], k;
            float x = 0, total_w = 0;
            for (k = 0; k < m; k++) total_w += s.across[layers[l][k]] + (k ? gap_between(&s, layers[l][k - 1], layers[l][k], nodesep) - (s.across[layers[l][k - 1]] + s.across[layers[l][k]]) / 2.0f : 0);
            (void)total_w;
            for (k = 0; k < m; k++) {
                int v = layers[l][k];
                if (k) x += gap_between(&s, layers[l][k - 1], v, nodesep);
                s.pos[v] = x;
            }
            if (m) { float mid = (s.pos[layers[l][0]] + s.pos[layers[l][m - 1]]) / 2.0f; for (k = 0; k < m; k++) s.pos[layers[l][k]] -= mid; }
        }
        for (it = 0; it < 12; it++) {
            int mode = it % 3;   /* 0: look up, 1: look down, 2: both */
            for (l = mode == 1 ? R : 0; mode == 1 ? l >= 0 : l <= R; l += mode == 1 ? -1 : 1) {
                int m = nlayer[l], k, j;
                for (k = 0; k < m; k++) {
                    int v = layers[l][k], c = 0;
                    float sum = 0;
                    if (mode != 1) for (j = 0; j < s.nup[v]; j++) { sum += s.pos[s.up[v][j]]; c++; }
                    if (mode != 0) for (j = 0; j < s.ndown[v]; j++) { sum += s.pos[s.down[v][j]]; c++; }
                    want[k] = c ? sum / (float)c : s.pos[v];
                }
                place_layer(&s, layers[l], m, want, nodesep, t1, t2, t3, t4);
            }
        }
    }
    /* position along the ranks */
    thick = calloc((size_t)R + 1, sizeof(float));
    rpos = calloc((size_t)R + 1, sizeof(float));
    if (!thick || !rpos) goto done;
    for (i = 0; i < total; i++) if (s.along[i] > thick[s.rank[i]]) thick[s.rank[i]] = s.along[i];
    {
        float ranksep = g->ranksep > 0 ? g->ranksep : 1.8f, half = ranksep / 2.0f;
        for (l = 0; l <= R; l++) {
            float gap = half;
            if (l == 0) rpos[0] = thick[0] / 2.0f;
            else {
                /* an odd layer with nothing in it needs no gap of its own on either side beyond the half step */
                rpos[l] = rpos[l - 1] + thick[l - 1] / 2.0f + gap + thick[l] / 2.0f;
            }
        }
    }
    /* back to x and y */
    for (i = 0; i < nn; i++) {
        float a = s.pos[i], r = rpos[s.rank[i]];
        switch (g->dir) {
        case 0: g->nodes[i].x = a; g->nodes[i].y = r; break;
        case 1: g->nodes[i].x = a; g->nodes[i].y = -r; break;
        case 2: g->nodes[i].x = r; g->nodes[i].y = a; break;
        default: g->nodes[i].x = -r; g->nodes[i].y = a; break;
        }
    }
    for (e = 0; e < ne; e++) {
        LgEdge *ed = &g->edges[e];
        int k, cnt;
        float *p;
        ed->pts = NULL; ed->npts = 0; ed->lx = ed->ly = 0;
        if (ed->a == ed->b) { ed->lx = g->nodes[ed->a].x; ed->ly = g->nodes[ed->a].y; continue; }
        cnt = chain_len[e] + 2;
        p = malloc((size_t)cnt * 2 * sizeof(float));
        if (!p) goto done;
        for (k = 0; k < cnt; k++) {
            int v = k == 0 ? (reversed[e] ? ed->b : ed->a) : (k == cnt - 1 ? (reversed[e] ? ed->a : ed->b) : chain_first[e] + k - 1);
            float a = v < nn ? s.pos[v] : s.pos[v], r = v < nn ? rpos[s.rank[v]] : rpos[s.rank[v]];
            float x, y;
            switch (g->dir) {
            case 0: x = a; y = r; break;
            case 1: x = a; y = -r; break;
            case 2: x = r; y = a; break;
            default: x = -r; y = a; break;
            }
            p[2 * k] = x; p[2 * k + 1] = y;
        }
        if (reversed[e]) { /* the points run from the lower rank: turn them so they start at the edge's own start */
            for (k = 0; k < cnt / 2; k++) { float tx = p[2 * k], ty = p[2 * k + 1]; p[2 * k] = p[2 * (cnt - 1 - k)]; p[2 * k + 1] = p[2 * (cnt - 1 - k) + 1]; p[2 * (cnt - 1 - k)] = tx; p[2 * (cnt - 1 - k) + 1] = ty; }
        }
        ed->pts = p; ed->npts = cnt;
        {
            int ld = label_dummy[e];
            float a = s.pos[ld], r = rpos[s.rank[ld]];
            switch (g->dir) {
            case 0: ed->lx = a; ed->ly = r; break;
            case 1: ed->lx = a; ed->ly = -r; break;
            case 2: ed->lx = r; ed->ly = a; break;
            default: ed->lx = -r; ed->ly = a; break;
            }
        }
    }
    rc = 0;
done:
    free(state); free(reversed); free(nout); free(rank); free(chain_first); free(chain_len); free(label_dummy);
    if (out_edges) { for (i = 0; i < nn; i++) free(out_edges[i]); free(out_edges); }
    if (layers) { for (l = 0; l <= R; l++) free(layers[l]); free(layers); }
    free(nlayer); free(rpos); free(thick); free(want); free(t1); free(t2); free(t3); free(t4); free(bary); free(idx);
    free(s.across); free(s.along); free(s.pos); free(s.rank); free(s.dummy);
    if (s.up) { for (i = 0; i < total; i++) free(s.up[i]); free(s.up); }
    if (s.down) { for (i = 0; i < total; i++) free(s.down[i]); free(s.down); }
    free(s.nup); free(s.ndown); free(s.capup); free(s.capdown);
    if (rc) lg_free(g);
    return rc;
}
