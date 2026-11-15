#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_layout.h"
#include "exhaust_worker.h"

#define LINK_DISTANCE 30.0f
#define CHARGE (-30.0f)
#define THETA 0.9f
#define ALPHA_MIN 0.001f
#define ALPHA_DECAY 0.0228f          /* 1 - 0.001^(1/300): from 1 to the minimum in about 300 steps */
#define VELOCITY_DECAY 0.6f
#define MAX_DEPTH 40

/* ---- a quadtree over the points ---- */

typedef struct QNode {
    float x0, y0, size;              /* the square this cell covers */
    float mass, cx, cy;              /* the number of points in it and their centre of mass */
    int child[4];                    /* -1: none */
    int body;                        /* a leaf: its first point, others follow through next[] */
} QNode;

typedef struct QTree {
    QNode *nodes;
    int n, cap;
    int *next;                       /* points that share a leaf (the same place, or the depth limit): a chain */
    int nnext;
} QTree;

static int qt_new(QTree *t, float x0, float y0, float size) {
    QNode *q;
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 1024;
        QNode *nn = realloc(t->nodes, (size_t)nc * sizeof *nn);
        if (!nn) return -1;
        t->nodes = nn; t->cap = nc;
    }
    q = &t->nodes[t->n];
    q->x0 = x0; q->y0 = y0; q->size = size; q->mass = 0; q->cx = q->cy = 0;
    q->child[0] = q->child[1] = q->child[2] = q->child[3] = -1;
    q->body = -1;
    return t->n++;
}

static int qt_build(QTree *t, const float *xy, int n) {
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1], size;
    int i;
    t->n = 0;
    if (n == 0) return 0;
    for (i = 1; i < n; i++) {
        if (xy[2 * i] < minx) minx = xy[2 * i];
        if (xy[2 * i] > maxx) maxx = xy[2 * i];
        if (xy[2 * i + 1] < miny) miny = xy[2 * i + 1];
        if (xy[2 * i + 1] > maxy) maxy = xy[2 * i + 1];
    }
    size = fmaxf(maxx - minx, maxy - miny) + 1.0f;
    if (qt_new(t, minx, miny, size) < 0) return -1;
    for (i = 0; i < n; i++) {
        int q = 0, depth = 0;
        float px = xy[2 * i], py = xy[2 * i + 1];
        for (;;) {
            QNode *c = &t->nodes[q];
            c->cx += px; c->cy += py; c->mass += 1.0f;
            if (c->mass == 1.0f) { c->body = i; t->next[i] = -1; break; }        /* an empty cell takes it */
            if (depth >= MAX_DEPTH) { t->next[i] = c->body; c->body = i; break; }  /* too deep to split: a chain of points in one leaf */
            if (c->body >= 0 && c->child[0] < 0 && c->child[1] < 0 && c->child[2] < 0 && c->child[3] < 0 && c->mass == 2.0f) {
                /* a leaf holding one point gets a second: push the first down, then carry on with the new one */
                int old = c->body, qd, half_ok;
                float h = c->size / 2.0f, ox = xy[2 * old], oy = xy[2 * old + 1];
                qd = (ox >= c->x0 + h ? 1 : 0) + (oy >= c->y0 + h ? 2 : 0);
                half_ok = qt_new(t, c->x0 + (qd & 1 ? h : 0), c->y0 + (qd & 2 ? h : 0), h);
                if (half_ok < 0) return -1;
                c = &t->nodes[q];                                              /* the pool may have moved */
                c->child[qd] = half_ok; c->body = -1;
                t->nodes[half_ok].cx = ox; t->nodes[half_ok].cy = oy; t->nodes[half_ok].mass = 1.0f; t->nodes[half_ok].body = old; t->next[old] = -1;
            }
            {
                float h = c->size / 2.0f;
                int qd = (px >= c->x0 + h ? 1 : 0) + (py >= c->y0 + h ? 2 : 0);
                if (c->child[qd] < 0) {
                    int nn = qt_new(t, c->x0 + (qd & 1 ? h : 0), c->y0 + (qd & 2 ? h : 0), h);
                    if (nn < 0) return -1;
                    c = &t->nodes[q];
                    c->child[qd] = nn;
                }
                q = c->child[qd];
                depth++;
            }
        }
    }
    return 0;
}

/* A tiny repeatable jitter for two points in the same place. */
static float jitter(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return ((float)(*s >> 8) / 16777216.0f - 0.5f) * 1e-3f; }

/* The repulsion on point i from the tree: the push is alpha-less here (the layout multiplies by alpha). Returns the interactions made. */
static unsigned long qt_force(const QTree *t, const float *xy, int i, float charge, float theta2, uint32_t *rng, float *fx, float *fy) {
    int stack[4 * (MAX_DEPTH + 2)], sp = 0;
    unsigned long count = 0;
    float px = xy[2 * i], py = xy[2 * i + 1], ax = 0, ay = 0;
    if (t->n == 0) { *fx = *fy = 0; return 0; }
    stack[sp++] = 0;
    while (sp > 0) {
        const QNode *q = &t->nodes[stack[--sp]];
        float dx, dy, l;
        if (q->mass == 0) continue;
        dx = q->cx / q->mass - px; dy = q->cy / q->mass - py;
        l = dx * dx + dy * dy;
        if (q->size * q->size / theta2 < l && !(q->body >= 0 && q->child[0] < 0 && q->child[1] < 0 && q->child[2] < 0 && q->child[3] < 0)) {
            if (l < 1.0f) l = sqrtf(l);                                           /* d3's distance floor: close points do not fly apart */
            { float w = charge * q->mass / l; ax += dx * w; ay += dy * w; }
            count++;
            continue;
        }
        if (q->body >= 0) {                                                       /* a leaf: its points one by one */
            int b;
            for (b = q->body; b >= 0; b = t->next[b]) {
                float bx, by, bl, w;
                if (b == i) continue;
                bx = xy[2 * b] - px; by = xy[2 * b + 1] - py;
                if (bx == 0.0f) bx = jitter(rng);
                if (by == 0.0f) by = jitter(rng);
                bl = bx * bx + by * by;
                if (bl < 1.0f) bl = sqrtf(bl);
                w = charge / bl; ax += bx * w; ay += by * w;
                count++;
            }
            continue;
        }
        {
            int c;
            for (c = 0; c < 4; c++) if (q->child[c] >= 0 && sp < (int)(sizeof stack / sizeof *stack)) stack[sp++] = q->child[c];
        }
    }
    *fx = ax; *fy = ay;
    return count;
}

void ex_layout_repulsion_exact(const float *xy, int n, float charge, float *fx, float *fy) {
    uint32_t rng = 12345u;
    int i, j;
    for (i = 0; i < n; i++) {
        float ax = 0, ay = 0;
        for (j = 0; j < n; j++) {
            float dx, dy, l;
            if (j == i) continue;
            dx = xy[2 * j] - xy[2 * i]; dy = xy[2 * j + 1] - xy[2 * i + 1];
            if (dx == 0.0f) dx = jitter(&rng);
            if (dy == 0.0f) dy = jitter(&rng);
            l = dx * dx + dy * dy;
            if (l < 1.0f) l = sqrtf(l);
            ax += dx * charge / l; ay += dy * charge / l;
        }
        fx[i] = ax; fy[i] = ay;
    }
}

unsigned long ex_layout_repulsion_bh(const float *xy, int n, float charge, float theta, float *fx, float *fy) {
    QTree t;
    uint32_t rng = 12345u;
    unsigned long total = 0;
    int i;
    memset(&t, 0, sizeof t);
    t.next = malloc((size_t)(n ? n : 1) * sizeof *t.next);
    if (!t.next || qt_build(&t, xy, n) != 0) { free(t.next); free(t.nodes); return 0; }
    for (i = 0; i < n; i++) total += qt_force(&t, xy, i, charge, theta * theta, &rng, &fx[i], &fy[i]);
    free(t.next); free(t.nodes);
    return total;
}

/* ---- the layout ---- */

typedef struct Pub { uint64_t gen_a, gen_b; float *xy; } Pub;   /* a published copy; gen_a and gen_b differ only if it was torn */

struct ExLayout {
    ExWorker *worker;
    ExQueue *results;
    int n;
    size_t ne;
    uint32_t *es, *et;               /* spring ends, each pair once */
    float *strength, *bias;
    float *x, *y, *vx, *vy;          /* the worker's own */
    float *xy;                       /* scratch for the quadtree: x and y interleaved */
    QTree tree;
    float alpha;
    uint32_t rng;
    Pub pub[3];
    int back, front;                 /* back: the worker writes it; front: the UI reads it */
    uint64_t gen;
    SDL_AtomicInt latest;            /* the buffer last published (low two bits) and, in bit 2, that the UI has not taken it yet */
    SDL_AtomicInt stop, reheat, settled, iterations, interactions, alpha_milli;
};

static void publish(ExLayout *l) {
    Pub *b = &l->pub[l->back];
    int i, old;
    b->gen_a = ++l->gen;
    for (i = 0; i < l->n; i++) { b->xy[2 * i] = l->x[i]; b->xy[2 * i + 1] = l->y[i]; }
    b->gen_b = l->gen;
    old = SDL_SetAtomicInt(&l->latest, l->back | 4);                       /* a full memory barrier: the copy is done before the swap shows */
    l->back = old & 3;
}

static void step(ExLayout *l) {
    int n = l->n, i;
    size_t k;
    float cx = 0, cy = 0;
    unsigned long inter = 0;
    l->alpha += (0.0f - l->alpha) * ALPHA_DECAY;
    for (i = 0; i < n; i++) { l->xy[2 * i] = l->x[i]; l->xy[2 * i + 1] = l->y[i]; }
    if (qt_build(&l->tree, l->xy, n) == 0) {
        for (i = 0; i < n; i++) {
            float fx, fy;
            inter += qt_force(&l->tree, l->xy, i, CHARGE, THETA * THETA, &l->rng, &fx, &fy);
            l->vx[i] += fx * l->alpha; l->vy[i] += fy * l->alpha;
        }
    }
    for (k = 0; k < l->ne; k++) {                                          /* a spring along every edge */
        uint32_t s = l->es[k], t = l->et[k];
        float dx = l->x[t] + l->vx[t] - l->x[s] - l->vx[s], dy = l->y[t] + l->vy[t] - l->y[s] - l->vy[s], len, b;
        if (dx == 0.0f) dx = jitter(&l->rng);
        if (dy == 0.0f) dy = jitter(&l->rng);
        len = sqrtf(dx * dx + dy * dy);
        len = (len - LINK_DISTANCE) / len * l->alpha * l->strength[k];
        dx *= len; dy *= len;
        b = l->bias[k];
        l->vx[t] -= dx * b; l->vy[t] -= dy * b;
        l->vx[s] += dx * (1.0f - b); l->vy[s] += dy * (1.0f - b);
    }
    for (i = 0; i < n; i++) { l->x[i] += l->vx[i] *= VELOCITY_DECAY; l->y[i] += l->vy[i] *= VELOCITY_DECAY; cx += l->x[i]; cy += l->y[i]; }
    if (n > 0) { cx /= (float)n; cy /= (float)n; for (i = 0; i < n; i++) { l->x[i] -= cx; l->y[i] -= cy; } }   /* the centre of mass stays at the origin */
    SDL_SetAtomicInt(&l->interactions, (int)inter);
    SDL_AddAtomicInt(&l->iterations, 1);
    SDL_SetAtomicInt(&l->alpha_milli, (int)(l->alpha * 1000000.0f));
}

/* The job: step until stopped. When cold it waits (checking for a stop or a reheat) instead of burning a processor. */
static void run(ExWorker *w, const ExMessage *m, void *user) {
    ExLayout *l = user;
    (void)w; (void)m;
    while (!SDL_GetAtomicInt(&l->stop)) {
        if (SDL_SetAtomicInt(&l->reheat, 0)) { if (l->alpha < 0.5f) l->alpha = 0.5f; SDL_SetAtomicInt(&l->settled, 0); }
        if (l->alpha < ALPHA_MIN) {
            if (!SDL_GetAtomicInt(&l->settled)) { publish(l); SDL_SetAtomicInt(&l->settled, 1); }
            SDL_Delay(20);
            continue;
        }
        step(l);
        publish(l);
        SDL_Delay(1);                                                      /* leave the processor to the UI thread between steps */
    }
}

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

typedef struct Key { const ExGraphNode *n; int idx; } Key;
static int cmp_key(const void *a, const void *b) {
    const Key *x = a, *y = b;
    if (x->n->kind != y->n->kind) return x->n->kind < y->n->kind ? -1 : 1;
    return strcmp(x->n->path, y->n->path);
}

ExLayout *ex_layout_create(const ExGraph *g, const ExGraph *prev_g, const float *prev_xy, uint32_t seed) {
    ExLayout *l = calloc(1, sizeof *l);
    int n = (int)g->n, i, kept = 0;
    size_t k, ne = 0;
    uint64_t *pairs;
    unsigned char *placed;
    uint32_t *deg;
    if (!l) return NULL;
    l->n = n; l->rng = seed ? seed : 1u;
    l->x = calloc((size_t)n + 1, sizeof *l->x); l->y = calloc((size_t)n + 1, sizeof *l->y);
    l->vx = calloc((size_t)n + 1, sizeof *l->vx); l->vy = calloc((size_t)n + 1, sizeof *l->vy);
    l->xy = calloc(2 * (size_t)n + 2, sizeof *l->xy);
    l->tree.next = malloc(((size_t)n + 1) * sizeof *l->tree.next);
    pairs = malloc((g->ne + 1) * sizeof *pairs);
    placed = calloc((size_t)n + 1, 1);
    deg = calloc((size_t)n + 1, sizeof *deg);
    for (i = 0; i < 3; i++) l->pub[i].xy = calloc(2 * (size_t)n + 2, sizeof(float));
    l->results = ex_queue_create(NULL, NULL);
    if (!l->x || !l->y || !l->vx || !l->vy || !l->xy || !l->tree.next || !pairs || !placed || !deg || !l->pub[0].xy || !l->pub[1].xy || !l->pub[2].xy || !l->results) goto fail;

    /* each edge once as a pair, whichever way the link goes */
    for (k = 0; k < g->ne; k++) {
        uint32_t a = g->edges[k].from, b = g->edges[k].to;
        if (a == b || a >= (uint32_t)n || b >= (uint32_t)n) continue;
        pairs[ne++] = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    }
    qsort(pairs, ne, sizeof *pairs, cmp_u64);
    {
        size_t w = 0;
        for (k = 0; k < ne; k++) if (k == 0 || pairs[k] != pairs[k - 1]) pairs[w++] = pairs[k];
        ne = w;
    }
    l->ne = ne;
    l->es = malloc((ne + 1) * sizeof *l->es); l->et = malloc((ne + 1) * sizeof *l->et);
    l->strength = malloc((ne + 1) * sizeof *l->strength); l->bias = malloc((ne + 1) * sizeof *l->bias);
    if (!l->es || !l->et || !l->strength || !l->bias) goto fail;
    for (k = 0; k < ne; k++) { l->es[k] = (uint32_t)(pairs[k] >> 32); l->et[k] = (uint32_t)(pairs[k] & 0xffffffffu); deg[l->es[k]]++; deg[l->et[k]]++; }
    for (k = 0; k < ne; k++) {
        float ds = (float)deg[l->es[k]], dt = (float)deg[l->et[k]];
        l->strength[k] = 1.0f / (ds < dt ? ds : dt);                       /* a hub is not torn apart by its many springs */
        l->bias[k] = ds / (ds + dt);
    }

    /* starting positions: where the node was, else near a neighbour that was, else on a spiral */
    if (prev_g && prev_xy && prev_g->n > 0) {
        Key *pk = malloc(prev_g->n * sizeof *pk);
        if (pk) {
            size_t m;
            for (m = 0; m < prev_g->n; m++) { pk[m].n = &prev_g->nodes[m]; pk[m].idx = (int)m; }
            qsort(pk, prev_g->n, sizeof *pk, cmp_key);
            for (i = 0; i < n; i++) {
                Key want, *hit;
                want.n = &g->nodes[i]; want.idx = 0;
                hit = bsearch(&want, pk, prev_g->n, sizeof *pk, cmp_key);
                if (hit) { l->x[i] = prev_xy[2 * hit->idx]; l->y[i] = prev_xy[2 * hit->idx + 1]; placed[i] = 1; kept++; }
            }
            free(pk);
        }
    }
    for (i = 0; i < n; i++) {
        if (placed[i]) continue;
        {
            float r = 10.0f * sqrtf(0.5f + (float)i), a = (float)i * 2.39996323f, sx = 0, sy = 0;
            int cnt = 0;
            for (k = 0; k < ne && kept > 0; k++) {                         /* near a neighbour that kept its place */
                int o = -1;
                if ((int)l->es[k] == i) o = (int)l->et[k]; else if ((int)l->et[k] == i) o = (int)l->es[k];
                if (o >= 0 && placed[o]) { sx += l->x[o]; sy += l->y[o]; cnt++; }
            }
            if (cnt) { l->x[i] = sx / (float)cnt + jitter(&l->rng) * 4000.0f; l->y[i] = sy / (float)cnt + jitter(&l->rng) * 4000.0f; }
            else { l->x[i] = r * cosf(a); l->y[i] = r * sinf(a); }
        }
    }
    l->alpha = kept > 0 ? 0.3f : 1.0f;
    SDL_SetAtomicInt(&l->alpha_milli, (int)(l->alpha * 1000000.0f));

    /* all three buffers hold the start, so the UI has something valid to read before the first step */
    for (i = 0; i < 3; i++) { int j; for (j = 0; j < n; j++) { l->pub[i].xy[2 * j] = l->x[j]; l->pub[i].xy[2 * j + 1] = l->y[j]; } }
    l->back = 0; l->front = 2;
    SDL_SetAtomicInt(&l->latest, 1);
    free(pairs); free(placed); free(deg);
    l->worker = ex_worker_start("layout", 7, run, l, l->results);
    if (!l->worker) { pairs = NULL; placed = NULL; deg = NULL; goto fail; }
    {
        ExMessage m;
        memset(&m, 0, sizeof m);
        m.type = 1;
        ex_worker_post(l->worker, &m);
    }
    return l;
fail:
    free(pairs); free(placed); free(deg);
    ex_layout_free(l);
    return NULL;
}

void ex_layout_free(ExLayout *l) {
    int i;
    if (!l) return;
    if (l->worker) { SDL_SetAtomicInt(&l->stop, 1); ex_worker_stop(l->worker); }
    ex_queue_destroy(l->results);
    for (i = 0; i < 3; i++) free(l->pub[i].xy);
    free(l->tree.nodes); free(l->tree.next);
    free(l->es); free(l->et); free(l->strength); free(l->bias);
    free(l->x); free(l->y); free(l->vx); free(l->vy); free(l->xy);
    free(l);
}

const float *ex_layout_positions(ExLayout *l, uint64_t *generation) {
    if (SDL_GetAtomicInt(&l->latest) & 4) {                                 /* something new: take it, give the worker our old buffer back */
        int old = SDL_SetAtomicInt(&l->latest, l->front);
        l->front = old & 3;
    }
    if (generation) *generation = l->pub[l->front].gen_a;
    return l->pub[l->front].xy;
}

int ex_layout_front_consistent(const ExLayout *l) { return l->pub[l->front].gen_a == l->pub[l->front].gen_b; }

void ex_layout_reheat(ExLayout *l) { SDL_SetAtomicInt(&l->reheat, 1); }

void ex_layout_stats(const ExLayout *l, ExLayoutStats *out) {
    SDL_AtomicInt *it = (SDL_AtomicInt *)&l->iterations;
    memset(out, 0, sizeof *out);
    out->nodes = l->n;
    out->iterations = (unsigned long)SDL_GetAtomicInt(it);
    out->interactions = (unsigned long)SDL_GetAtomicInt((SDL_AtomicInt *)&l->interactions);
    out->alpha = (float)SDL_GetAtomicInt((SDL_AtomicInt *)&l->alpha_milli) / 1000000.0f;
    out->settled = SDL_GetAtomicInt((SDL_AtomicInt *)&l->settled);
}
