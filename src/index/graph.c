#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_graph.h"
#include "exhaust_links.h"
#include "exhaust_search.h"
#include "store_internal.h"

/* ---- two small hash maps: a string to a number, and a number to a number ---- */

typedef struct SMap { char **k; int64_t *v; size_t cap, n; } SMap;

static uint64_t fnv(const char *s) { uint64_t h = 1469598103934665603ull; for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; } return h; }

static int smap_grow(SMap *m) {
    size_t nc = m->cap ? m->cap * 2 : 1024, i;
    char **nk = calloc(nc, sizeof *nk);
    int64_t *nv = calloc(nc, sizeof *nv);
    if (!nk || !nv) { free(nk); free(nv); return -1; }
    for (i = 0; i < m->cap; i++) if (m->k[i]) { size_t j = (size_t)(fnv(m->k[i]) & (nc - 1)); while (nk[j]) j = (j + 1) & (nc - 1); nk[j] = m->k[i]; nv[j] = m->v[i]; }
    free(m->k); free(m->v);
    m->k = nk; m->v = nv; m->cap = nc;
    return 0;
}

/* The slot of key: its own, or the empty one it would take. */
static size_t smap_slot(const SMap *m, const char *key) { size_t j = (size_t)(fnv(key) & (m->cap - 1)); while (m->k[j] && strcmp(m->k[j], key)) j = (j + 1) & (m->cap - 1); return j; }

static int smap_get(SMap *m, const char *key, int64_t *val) {
    size_t j;
    if (!m->cap) return 0;
    j = smap_slot(m, key);
    if (!m->k[j]) return 0;
    *val = m->v[j];
    return 1;
}

static int smap_put(SMap *m, char *key_owned, int64_t val) {
    size_t j;
    if ((m->n + 1) * 2 > m->cap && smap_grow(m) != 0) return -1;
    j = smap_slot(m, key_owned);
    if (!m->k[j]) m->n++;
    m->k[j] = key_owned; m->v[j] = val;
    return 0;
}

static void smap_free(SMap *m) { size_t i; for (i = 0; i < m->cap; i++) free(m->k[i]); free(m->k); free(m->v); memset(m, 0, sizeof *m); }

typedef struct IMap { int64_t *k; uint32_t *v; unsigned char *used; size_t cap, n; } IMap;

static size_t imap_hash(int64_t k, size_t cap) { uint64_t x = (uint64_t)k * 0x9E3779B97F4A7C15ull; return (size_t)((x >> 20) & (cap - 1)); }

static int imap_grow(IMap *m) {
    size_t nc = m->cap ? m->cap * 2 : 1024, i;
    int64_t *nk = calloc(nc, sizeof *nk);
    uint32_t *nv = calloc(nc, sizeof *nv);
    unsigned char *nu = calloc(nc, 1);
    if (!nk || !nv || !nu) { free(nk); free(nv); free(nu); return -1; }
    for (i = 0; i < m->cap; i++) if (m->used[i]) { size_t j = imap_hash(m->k[i], nc); while (nu[j]) j = (j + 1) & (nc - 1); nk[j] = m->k[i]; nv[j] = m->v[i]; nu[j] = 1; }
    free(m->k); free(m->v); free(m->used);
    m->k = nk; m->v = nv; m->used = nu; m->cap = nc;
    return 0;
}

static int imap_get(const IMap *m, int64_t key, uint32_t *val) {
    size_t j;
    if (!m->cap) return 0;
    for (j = imap_hash(key, m->cap); m->used[j]; j = (j + 1) & (m->cap - 1)) if (m->k[j] == key) { *val = m->v[j]; return 1; }
    return 0;
}

static int imap_put(IMap *m, int64_t key, uint32_t val) {
    size_t j;
    if ((m->n + 1) * 2 > m->cap && imap_grow(m) != 0) return -1;
    for (j = imap_hash(key, m->cap); m->used[j]; j = (j + 1) & (m->cap - 1)) if (m->k[j] == key) { m->v[j] = val; return 0; }
    m->used[j] = 1; m->k[j] = key; m->v[j] = val; m->n++;
    return 0;
}

static void imap_free(IMap *m) { free(m->k); free(m->v); free(m->used); memset(m, 0, sizeof *m); }

/* ---- the graph ---- */

void ex_graph_free(ExGraph *g) {
    size_t i;
    if (!g) return;
    for (i = 0; i < g->n; i++) free(g->nodes[i].path);
    free(g->nodes);
    free(g->edges);
    memset(g, 0, sizeof *g);
}

const char *ex_graph_label(const ExGraphNode *n, size_t *len) {
    const char *s = strrchr(n->path, '/'), *b = s ? s + 1 : n->path;
    size_t l = strlen(b);
    if (n->kind == EX_GRAPH_NOTE && l > 3 && !strcmp(b + l - 3, ".md")) l -= 3;
    if (len) *len = l;
    return b;
}

typedef struct Build {
    ExGraph *g;
    size_t cap;
    uint64_t *pairs;
    size_t np, pcap;
} Build;

static int add_node(Build *b, char *path_owned, int kind) {
    ExGraph *g = b->g;
    if (g->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 1024;
        ExGraphNode *nv = realloc(g->nodes, nc * sizeof *nv);
        if (!nv) { free(path_owned); return -1; }
        g->nodes = nv; b->cap = nc;
    }
    g->nodes[g->n].path = path_owned;
    g->nodes[g->n].kind = kind;
    g->nodes[g->n].degree = 0;
    g->nodes[g->n].group = 0;
    return (int)g->n++;
}

static int add_pair(Build *b, uint32_t from, uint32_t to) {
    if (b->np == b->pcap) {
        size_t nc = b->pcap ? b->pcap * 2 : 4096;
        uint64_t *np = realloc(b->pairs, nc * sizeof *np);
        if (!np) return -1;
        b->pairs = np; b->pcap = nc;
    }
    b->pairs[b->np++] = ((uint64_t)from << 32) | to;
    return 0;
}

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static char *dir_key(const char *path, int kind, const char *target) {
    const char *slash = strrchr(path, '/');
    size_t dl = slash ? (size_t)(slash - path) : 0, tl = strlen(target);
    char *k = malloc(dl + tl + 6);
    if (!k) return NULL;
    memcpy(k, path, dl);
    k[dl] = 1; k[dl + 1] = (char)('0' + kind); k[dl + 2] = 1;
    memcpy(k + dl + 3, target, tl + 1);
    return k;
}

int ex_graph_load(ExIndex *r, ExGraph *out) {
    Build b;
    sqlite3_stmt *st;
    IMap by_id = {0};                 /* file id -> node */
    SMap resolved = {0}, unresolved = {0};
    int rc = -1;
    size_t i, w;
    memset(out, 0, sizeof *out);
    memset(&b, 0, sizeof b);
    b.g = out;

    /* every Markdown note is a node, in path order */
    st = ex_db_prepare(r, "SELECT id, path FROM files WHERE kind = 1 ORDER BY path");
    if (!st) goto done;
    while (sqlite3_step(st) == SQLITE_ROW) {
        char *p = ex_strdup((const char *)sqlite3_column_text(st, 1));
        int idx = p ? add_node(&b, p, EX_GRAPH_NOTE) : -1;
        if (idx < 0 || imap_put(&by_id, sqlite3_column_int64(st, 0), (uint32_t)idx) != 0) { sqlite3_finalize(st); goto done; }
    }
    sqlite3_finalize(st);

    /* the links, note by note */
    st = ex_db_prepare(r, "SELECT l.file_id, l.kind, l.target FROM links l ORDER BY l.file_id, l.ord");
    if (!st) goto done;
    {
        int64_t cur_file = -1;
        uint32_t cur_node = 0;
        int cur_ok = 0;
        const char *cur_path = NULL;
        while (sqlite3_step(st) == SQLITE_ROW) {
            int64_t fid = sqlite3_column_int64(st, 0), dest;
            int kind = sqlite3_column_int(st, 1);
            const char *target = (const char *)sqlite3_column_text(st, 2);
            char *key, *tkey;
            uint32_t to;
            int have;
            out->links_read++;
            if (fid != cur_file) {
                cur_file = fid;
                cur_ok = imap_get(&by_id, fid, &cur_node);
                cur_path = cur_ok ? out->nodes[cur_node].path : NULL;
            }
            if (!cur_ok || !target) continue;
            if (!target[0]) continue;                                   /* [[#Heading]]: a link to the note itself */
            key = dir_key(cur_path, kind, target);
            if (!key) goto fail_stmt;
            have = smap_get(&resolved, key, &dest);
            if (have) free(key);
            else {
                dest = ex_link_resolve(r, cur_path, target, kind);
                out->resolves++;
                if (smap_put(&resolved, key, dest) != 0) { free(key); goto fail_stmt; }
            }
            if (dest) {
                if (!imap_get(&by_id, dest, &to)) {                      /* a file that is not a note: an attachment */
                    char *p = ex_index_file_path(r, dest);
                    int idx = p ? add_node(&b, p, EX_GRAPH_ATTACHMENT) : -1;
                    if (idx < 0 || imap_put(&by_id, dest, (uint32_t)idx) != 0) goto fail_stmt;
                    to = (uint32_t)idx;
                }
            } else {                                                    /* a name nothing answers to yet: one node for every link to it */
                int64_t idx;
                tkey = ex_link_target_key(target);
                if (!tkey) goto fail_stmt;
                if (smap_get(&unresolved, tkey, &idx)) free(tkey);
                else {
                    char *p = ex_strdup(target);
                    int ni = p ? add_node(&b, p, EX_GRAPH_UNRESOLVED) : -1;
                    if (ni < 0 || smap_put(&unresolved, tkey, ni) != 0) { free(tkey); goto fail_stmt; }
                    idx = ni;
                }
                to = (uint32_t)idx;
            }
            if (to == cur_node) continue;                                /* a link to itself */
            if (add_pair(&b, cur_node, to) != 0) goto fail_stmt;
        }
        sqlite3_finalize(st);
        goto edges;
    fail_stmt:
        sqlite3_finalize(st);
        goto done;
    }
edges:
    qsort(b.pairs, b.np, sizeof *b.pairs, cmp_u64);
    for (i = 0, w = 0; i < b.np; i++) if (i == 0 || b.pairs[i] != b.pairs[i - 1]) b.pairs[w++] = b.pairs[i];
    out->edges = malloc((w ? w : 1) * sizeof *out->edges);
    if (!out->edges) goto done;
    for (i = 0; i < w; i++) {
        out->edges[i].from = (uint32_t)(b.pairs[i] >> 32);
        out->edges[i].to = (uint32_t)(b.pairs[i] & 0xffffffffu);
        out->nodes[out->edges[i].from].degree++;
        out->nodes[out->edges[i].to].degree++;
    }
    out->ne = w;
    rc = 0;
done:
    free(b.pairs);
    imap_free(&by_id);
    smap_free(&resolved);
    smap_free(&unresolved);
    if (rc != 0) ex_graph_free(out);
    return rc;
}

/* ---- filters and groups (5b) ---- */

typedef struct PathSet { char **v; size_t n; } PathSet;
static int cmp_cstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* The paths a query finds, sorted; 0 on success, -1 if it does not parse, -2 if the search failed. */
static int found_paths(ExIndex *r, const char *root, const char *query, PathSet *out) {
    char err[200];
    ExSearch *q = ex_search_parse(query, err, sizeof err);
    ExSearchOptions so;
    ExSearchResult *res = NULL;
    size_t n = 0, i;
    out->v = NULL; out->n = 0;
    if (!q) return -1;
    memset(&so, 0, sizeof so);
    so.sort = EX_SEARCH_PATH_AZ; so.max_results = 1000000; so.max_matches = 1;
    if (ex_search_run(r, root, q, &so, &res, &n, NULL) != 0) { ex_search_free(q); return -2; }
    ex_search_free(q);
    out->v = malloc((n ? n : 1) * sizeof *out->v);
    if (!out->v) { ex_search_results_free(res, n); return -2; }
    for (i = 0; i < n; i++) { size_t l = strlen(res[i].path) + 1; out->v[i] = malloc(l); if (!out->v[i]) { out->n = i; break; } memcpy(out->v[i], res[i].path, l); out->n = i + 1; }
    ex_search_results_free(res, n);
    qsort(out->v, out->n, sizeof *out->v, cmp_cstr);
    return 0;
}
static void pathset_free(PathSet *s) { size_t i; for (i = 0; i < s->n; i++) free(s->v[i]); free(s->v); s->v = NULL; s->n = 0; }
static int pathset_has(const PathSet *s, const char *p) { return s->n && bsearch(&p, s->v, s->n, sizeof *s->v, cmp_cstr) != NULL; }

int ex_graph_apply_options(ExIndex *r, const char *root, const ExGraphOptions *o, ExGraph *g, unsigned *bad) {
    size_t i, w, n = g->n;
    unsigned char *keep;
    uint32_t *remap;
    int gi, rc = -1;
    if (bad) *bad = 0;
    if (!g->nodes || n == 0) return 0;
    keep = malloc(n);
    remap = malloc(n * sizeof *remap);
    if (!keep || !remap) goto done;
    memset(keep, 1, n);
    for (i = 0; i < n; i++) g->nodes[i].group = 0;
    if (o->search[0]) {
        PathSet ps;
        int e = found_paths(r, root, o->search, &ps);
        if (e == -2) goto done;
        if (e == -1) { if (bad) *bad |= 1u; }
        else {
            unsigned char *linked = calloc(n, 1);
            if (!linked) { pathset_free(&ps); goto done; }
            for (i = 0; i < n; i++) if (g->nodes[i].kind == EX_GRAPH_NOTE) keep[i] = pathset_has(&ps, g->nodes[i].path) ? 1 : 0;
            for (i = 0; i < g->ne; i++) if (keep[g->edges[i].from] && g->nodes[g->edges[i].from].kind == EX_GRAPH_NOTE) linked[g->edges[i].to] = 1;
            for (i = 0; i < n; i++) if (g->nodes[i].kind != EX_GRAPH_NOTE) keep[i] = linked[i];   /* what the found notes link to */
            free(linked);
            pathset_free(&ps);
        }
    }
    for (gi = 0; gi < EX_GRAPH_MAX_GROUPS; gi++) {
        PathSet ps;
        int e;
        if (!o->groups[gi].query[0]) continue;
        e = found_paths(r, root, o->groups[gi].query, &ps);
        if (e == -2) goto done;
        if (e == -1) { if (bad) *bad |= 2u << gi; continue; }
        for (i = 0; i < n; i++) if (g->nodes[i].kind == EX_GRAPH_NOTE && !g->nodes[i].group && pathset_has(&ps, g->nodes[i].path)) g->nodes[i].group = (uint8_t)(gi + 1);
        pathset_free(&ps);
    }
    for (i = 0; i < n; i++) {
        if (g->nodes[i].kind == EX_GRAPH_UNRESOLVED && !o->show_unresolved) keep[i] = 0;
        if (g->nodes[i].kind == EX_GRAPH_ATTACHMENT && !o->show_attachments) keep[i] = 0;
    }
    if (!o->show_orphans) {                                     /* an orphan has no edge among the nodes that are left */
        uint32_t *deg = calloc(n, sizeof *deg);
        if (!deg) goto done;
        for (i = 0; i < g->ne; i++) if (keep[g->edges[i].from] && keep[g->edges[i].to]) { deg[g->edges[i].from]++; deg[g->edges[i].to]++; }
        for (i = 0; i < n; i++) if (!deg[i]) keep[i] = 0;
        free(deg);
    }
    for (i = 0, w = 0; i < n; i++) {                             /* compact: the order stays, so the edges stay sorted */
        if (keep[i]) { remap[i] = (uint32_t)w; g->nodes[w] = g->nodes[i]; w++; }
        else { remap[i] = 0xffffffffu; free(g->nodes[i].path); }
    }
    {
        size_t k = 0;
        for (i = 0; i < g->ne; i++) {
            uint32_t a = remap[g->edges[i].from], b = remap[g->edges[i].to];
            if (a == 0xffffffffu || b == 0xffffffffu) continue;
            g->edges[k].from = a; g->edges[k].to = b; k++;
        }
        g->ne = k;
    }
    g->n = w;
    for (i = 0; i < w; i++) g->nodes[i].degree = 0;
    for (i = 0; i < g->ne; i++) { g->nodes[g->edges[i].from].degree++; g->nodes[g->edges[i].to].degree++; }
    rc = 0;
done:
    free(keep); free(remap);
    return rc;
}
