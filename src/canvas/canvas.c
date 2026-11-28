/* JSON Canvas: the model, reading and writing (include/exhaust_canvas.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exhaust_canvas.h"
#include "yyjson.h"

/* ---- small helpers ---- */

static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }
static char *dups(const char *s) { return s ? dupn(s, strlen(s)) : NULL; }

static int str_in(const char *const *names, size_t n, const char *s) { size_t i; for (i = 0; i < n; i++) if (!strcmp(names[i], s)) return (int)i; return -1; }

/* ---- the keys this version knows ---- */

enum { NK_ID, NK_TYPE, NK_X, NK_Y, NK_W, NK_H, NK_COLOR, NK_TEXT, NK_FILE, NK_SUBPATH, NK_URL, NK_LABEL, NK_BG, NK_BGSTYLE, NK_COUNT };
static const char *const NODE_KEYS[NK_COUNT] = { "id", "type", "x", "y", "width", "height", "color", "text", "file", "subpath", "url", "label", "background", "backgroundStyle" };
enum { EK_ID, EK_FROM, EK_FSIDE, EK_FEND, EK_TO, EK_TSIDE, EK_TEND, EK_COLOR, EK_LABEL, EK_COUNT };
static const char *const EDGE_KEYS[EK_COUNT] = { "id", "fromNode", "fromSide", "fromEnd", "toNode", "toSide", "toEnd", "color", "label" };
static const char *const SIDES[4] = { "top", "right", "bottom", "left" };
static const char *const ENDS[2] = { "none", "arrow" };
static const char *const KINDS[4] = { "text", "file", "link", "group" };

/* ---- the generator of ids ---- */

static uint64_t g_seed = 0;
static uint64_t next_rand(void) {
    uint64_t x;
    if (!g_seed) { g_seed = 0x9E3779B97F4A7C15ull ^ (uint64_t)(size_t)&g_seed ^ ((uint64_t)time(NULL) << 20) ^ (uint64_t)clock(); if (!g_seed) g_seed = 1; }
    x = g_seed; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; g_seed = x;
    return x * 0x2545F4914F6CDD1Dull;
}
void ex_canvas_set_seed(uint64_t seed) { g_seed = seed ? seed : 1; }

static int id_taken(const ExCanvas *c, const char *id) {
    size_t i;
    for (i = 0; i < c->n; i++) if (c->nodes[i].id && !strcmp(c->nodes[i].id, id)) return 1;
    for (i = 0; i < c->ne; i++) if (c->edges[i].id && !strcmp(c->edges[i].id, id)) return 1;
    return 0;
}
void ex_canvas_new_id(const ExCanvas *c, char out[17]) {
    do {
        uint64_t v = next_rand();
        int i;
        for (i = 0; i < 16; i++) out[i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
        out[16] = 0;
    } while (c && id_taken(c, out));
}

/* ---- freeing ---- */

static void kv_free(ExCanvasKV *kv, size_t n) { size_t i; for (i = 0; i < n; i++) { free(kv[i].key); free(kv[i].json); } free(kv); }
static void order_free(char **o, size_t n) { size_t i; for (i = 0; i < n; i++) free(o[i]); free(o); }
static void node_free(ExCanvasNode *n) {
    { int k; for (k = 0; k < 4; k++) free(n->numtext[k]); }
    free(n->id); free(n->type); free(n->color); free(n->text); free(n->file); free(n->subpath); free(n->url); free(n->label); free(n->background); free(n->background_style);
    kv_free(n->extra, n->nextra); order_free(n->order, n->norder); free(n->raw);
}
static void edge_free(ExCanvasEdge *e) {
    free(e->id); free(e->from); free(e->to); free(e->color); free(e->label);
    kv_free(e->extra, e->nextra); order_free(e->order, e->norder); free(e->raw);
}
void ex_canvas_free(ExCanvas *c) {
    size_t i;
    if (!c) return;
    for (i = 0; i < c->n; i++) node_free(&c->nodes[i]);
    for (i = 0; i < c->ne; i++) edge_free(&c->edges[i]);
    free(c->nodes); free(c->edges); free(c->hash);
    kv_free(c->extra, c->nextra); order_free(c->order, c->norder);
    free(c);
}

ExCanvas *ex_canvas_new(void) {
    ExCanvas *c = calloc(1, sizeof *c);
    if (c) c->has_nodes = c->has_edges = 1;
    return c;
}

/* ---- growing arrays ---- */

static int kv_add(ExCanvasKV **kv, size_t *n, const char *key, size_t keylen, char *json) {
    ExCanvasKV *nv = realloc(*kv, (*n + 1) * sizeof **kv);
    char *k;
    if (!nv) { free(json); return -1; }
    *kv = nv;
    k = dupn(key, keylen);
    if (!k) { free(json); return -1; }
    (*kv)[*n].key = k; (*kv)[*n].json = json; (*n)++;
    return 0;
}
static int order_add(char ***o, size_t *n, const char *key, size_t keylen) {
    char **no;
    size_t i;
    char *k;
    for (i = 0; i < *n; i++) if (strlen((*o)[i]) == keylen && !memcmp((*o)[i], key, keylen)) return 0;    /* a repeated key is written once */
    no = realloc(*o, (*n + 1) * sizeof **o);
    if (!no) return -1;
    *o = no;
    k = dupn(key, keylen);
    if (!k) return -1;
    (*o)[(*n)++] = k;
    return 0;
}
static const ExCanvasKV *kv_find(const ExCanvasKV *kv, size_t n, const char *key) { size_t i; for (i = 0; i < n; i++) if (!strcmp(kv[i].key, key)) return &kv[i]; return NULL; }

/* ---- reading ---- */

static char *val_json(yyjson_val *v) { size_t n = 0; char *s = yyjson_val_write(v, YYJSON_WRITE_NOFLAG, &n); return s; }
static char *val_string(yyjson_val *v) { return yyjson_is_str(v) ? dupn(yyjson_get_str(v), yyjson_get_len(v)) : NULL; }
/* A number (read as raw text); returns 1 and sets *out, or 0. */
static int val_number(yyjson_val *v, double *out) {
    const char *raw;
    char *end;
    if (!v || !yyjson_is_raw(v)) return 0;
    raw = yyjson_get_raw(v);
    if (!raw || !*raw || (*raw != '-' && (*raw < '0' || *raw > '9'))) return 0;
    *out = strtod(raw, &end);
    if (*end || !isfinite(*out)) return 0;
    return 1;
}
static void set_str(char **field, char *s) { free(*field); *field = s; }

static int parse_node(ExCanvasNode *n, yyjson_val *o) {
    size_t idx, max;
    yyjson_val *k, *v;
    n->w = 250; n->h = 60; n->kind = EX_CN_OTHER;
    yyjson_obj_foreach(o, idx, max, k, v) {
        const char *key = yyjson_get_str(k);
        size_t kl = yyjson_get_len(k);
        char *key0 = dupn(key, kl);
        int id, handled = 0;
        double d;
        if (!key0) return -1;
        if (order_add(&n->order, &n->norder, key, kl) != 0) { free(key0); return -1; }
        id = str_in(NODE_KEYS, NK_COUNT, key0);
        switch (id) {
        case NK_ID: if (yyjson_is_str(v)) { set_str(&n->id, val_string(v)); handled = 1; } break;
        case NK_TYPE: if (yyjson_is_str(v)) { set_str(&n->type, val_string(v)); handled = 1; } break;
        case NK_X: if (val_number(v, &d)) { n->x = d; set_str(&n->numtext[0], dups(yyjson_get_raw(v))); handled = 1; } break;
        case NK_Y: if (val_number(v, &d)) { n->y = d; set_str(&n->numtext[1], dups(yyjson_get_raw(v))); handled = 1; } break;
        case NK_W: if (val_number(v, &d)) { n->w = d; set_str(&n->numtext[2], dups(yyjson_get_raw(v))); handled = 1; } break;
        case NK_H: if (val_number(v, &d)) { n->h = d; set_str(&n->numtext[3], dups(yyjson_get_raw(v))); handled = 1; } break;
        case NK_COLOR: if (yyjson_is_str(v)) { set_str(&n->color, val_string(v)); handled = 1; } break;
        case NK_TEXT: if (yyjson_is_str(v)) { set_str(&n->text, val_string(v)); handled = 1; } break;
        case NK_FILE: if (yyjson_is_str(v)) { set_str(&n->file, val_string(v)); handled = 1; } break;
        case NK_SUBPATH: if (yyjson_is_str(v)) { set_str(&n->subpath, val_string(v)); handled = 1; } break;
        case NK_URL: if (yyjson_is_str(v)) { set_str(&n->url, val_string(v)); handled = 1; } break;
        case NK_LABEL: if (yyjson_is_str(v)) { set_str(&n->label, val_string(v)); handled = 1; } break;
        case NK_BG: if (yyjson_is_str(v)) { set_str(&n->background, val_string(v)); handled = 1; } break;
        case NK_BGSTYLE: if (yyjson_is_str(v)) { set_str(&n->background_style, val_string(v)); handled = 1; } break;
        default: break;
        }
        if (!handled && !kv_find(n->extra, n->nextra, key0)) {                 /* an unknown key, or a known one with a value of the wrong kind: kept as it is */
            char *j = val_json(v);
            if (!j || kv_add(&n->extra, &n->nextra, key, kl, j) != 0) { free(key0); return -1; }
        }
        free(key0);
    }
    if (n->type) { int k2 = str_in(KINDS, 4, n->type); if (k2 >= 0) n->kind = (ExCanvasKind)k2; }
    if (n->kind != EX_CN_OTHER) { free(n->type); n->type = NULL; }     /* the kind says it */
    if (n->kind == EX_CN_TEXT && !n->text) n->text = dups("");
    if (n->kind == EX_CN_FILE && !n->file) n->file = dups("");
    if (n->kind == EX_CN_LINK && !n->url) n->url = dups("");
    return 0;
}

static int side_of(yyjson_val *v) {
    int s;
    char *t;
    if (!yyjson_is_str(v)) return -2;
    t = val_string(v);
    if (!t) return -2;
    s = str_in(SIDES, 4, t);
    free(t);
    return s < 0 ? -2 : s;
}
static int end_of(yyjson_val *v) {
    int s;
    char *t;
    if (!yyjson_is_str(v)) return -2;
    t = val_string(v);
    if (!t) return -2;
    s = str_in(ENDS, 2, t);
    free(t);
    return s < 0 ? -2 : s;
}

static int parse_edge(ExCanvasEdge *e, yyjson_val *o) {
    size_t idx, max;
    yyjson_val *k, *v;
    e->from_side = e->to_side = EX_CS_NONE; e->from_end = e->to_end = EX_CE_DEFAULT;
    yyjson_obj_foreach(o, idx, max, k, v) {
        const char *key = yyjson_get_str(k);
        size_t kl = yyjson_get_len(k);
        char *key0 = dupn(key, kl);
        int id, handled = 0, t;
        if (!key0) return -1;
        if (order_add(&e->order, &e->norder, key, kl) != 0) { free(key0); return -1; }
        id = str_in(EDGE_KEYS, EK_COUNT, key0);
        switch (id) {
        case EK_ID: if (yyjson_is_str(v)) { set_str(&e->id, val_string(v)); handled = 1; } break;
        case EK_FROM: if (yyjson_is_str(v)) { set_str(&e->from, val_string(v)); handled = 1; } break;
        case EK_TO: if (yyjson_is_str(v)) { set_str(&e->to, val_string(v)); handled = 1; } break;
        case EK_FSIDE: t = side_of(v); if (t != -2) { e->from_side = t; handled = 1; } break;
        case EK_TSIDE: t = side_of(v); if (t != -2) { e->to_side = t; handled = 1; } break;
        case EK_FEND: t = end_of(v); if (t != -2) { e->from_end = t; handled = 1; } break;
        case EK_TEND: t = end_of(v); if (t != -2) { e->to_end = t; handled = 1; } break;
        case EK_COLOR: if (yyjson_is_str(v)) { set_str(&e->color, val_string(v)); handled = 1; } break;
        case EK_LABEL: if (yyjson_is_str(v)) { set_str(&e->label, val_string(v)); handled = 1; } break;
        default: break;
        }
        if (!handled && !kv_find(e->extra, e->nextra, key0)) {
            char *j = val_json(v);
            if (!j || kv_add(&e->extra, &e->nextra, key, kl, j) != 0) { free(key0); return -1; }
        }
        free(key0);
    }
    if (!e->from) e->from = dups("");
    if (!e->to) e->to = dups("");
    return 0;
}

static int push_node(ExCanvas *c, ExCanvasNode **out) {
    if (c->n == c->ncap) {
        size_t nc = c->ncap ? c->ncap * 2 : 16;
        ExCanvasNode *nn = realloc(c->nodes, nc * sizeof *nn);
        if (!nn) return -1;
        c->nodes = nn; c->ncap = nc;
    }
    *out = &c->nodes[c->n];
    memset(*out, 0, sizeof **out);
    c->n++;
    return 0;
}
static int push_edge(ExCanvas *c, ExCanvasEdge **out) {
    if (c->ne == c->necap) {
        size_t nc = c->necap ? c->necap * 2 : 16;
        ExCanvasEdge *nn = realloc(c->edges, nc * sizeof *nn);
        if (!nn) return -1;
        c->edges = nn; c->necap = nc;
    }
    *out = &c->edges[c->ne];
    memset(*out, 0, sizeof **out);
    c->ne++;
    return 0;
}

static void fail(char *err, size_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }

typedef struct EdgeKey { const char *id; size_t at; } EdgeKey;
static int cmp_edge_id(const void *a, const void *b) {
    const EdgeKey *x = a, *y = b;
    int c = strcmp(x->id, y->id);
    return c ? c : (x->at < y->at ? -1 : x->at > y->at);
}

ExCanvas *ex_canvas_parse(const char *text, size_t len, char *err, size_t errcap) {
    yyjson_read_err re;
    yyjson_doc *doc;
    yyjson_val *root, *k, *v, *el;
    size_t idx, max, i;
    ExCanvas *c;
    int bad = 0;
    if (err && errcap) err[0] = 0;
    if (!text) { fail(err, errcap, "no text"); return NULL; }
    doc = yyjson_read_opts((char *)(void *)(size_t)(const void *)text, len, YYJSON_READ_NUMBER_AS_RAW, NULL, &re);
    if (!doc) {
        if (err && errcap) snprintf(err, errcap, "not valid JSON: %s (at byte %zu)", re.msg ? re.msg : "error", (size_t)re.pos);
        return NULL;
    }
    root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); fail(err, errcap, "not a JSON Canvas: the file is not a JSON object"); return NULL; }
    c = calloc(1, sizeof *c);
    if (!c) { yyjson_doc_free(doc); fail(err, errcap, "out of memory"); return NULL; }
    yyjson_obj_foreach(root, idx, max, k, v) {
        const char *key = yyjson_get_str(k);
        size_t kl = yyjson_get_len(k);
        int is_nodes = kl == 5 && !memcmp(key, "nodes", 5), is_edges = kl == 5 && !memcmp(key, "edges", 5);
        if (order_add(&c->order, &c->norder, key, kl) != 0) { bad = 1; break; }
        if (is_nodes && yyjson_is_arr(v) && !c->has_nodes) {
            size_t ai, am;
            c->has_nodes = 1;
            yyjson_arr_foreach(v, ai, am, el) {
                ExCanvasNode *n;
                if (push_node(c, &n) != 0) { bad = 1; break; }
                if (yyjson_is_obj(el)) { if (parse_node(n, el) != 0) { bad = 1; break; } }
                else { n->raw = val_json(el); n->kind = EX_CN_OTHER; if (!n->raw) { bad = 1; break; } }
            }
            if (bad) break;
        } else if (is_edges && yyjson_is_arr(v) && !c->has_edges) {
            size_t ai, am;
            c->has_edges = 1;
            yyjson_arr_foreach(v, ai, am, el) {
                ExCanvasEdge *e;
                if (push_edge(c, &e) != 0) { bad = 1; break; }
                if (yyjson_is_obj(el)) { if (parse_edge(e, el) != 0) { bad = 1; break; } }
                else { e->raw = val_json(el); e->from_side = e->to_side = EX_CS_NONE; e->from_end = e->to_end = EX_CE_DEFAULT; if (!e->raw) { bad = 1; break; } }
            }
            if (bad) break;
        } else {
            char *kz = dupn(key, kl), *j;
            int seen = kz ? kv_find(c->extra, c->nextra, kz) != NULL : 1;
            free(kz);
            if (seen) continue;                                                              /* a repeated key: the first stays */
            j = val_json(v);
            if (!j || kv_add(&c->extra, &c->nextra, key, kl, j) != 0) { bad = 1; break; }
        }
    }
    yyjson_doc_free(doc);
    if (bad) { ex_canvas_free(c); fail(err, errcap, "out of memory"); return NULL; }
    /* every card has an id that no other card has */
    for (i = 0; i < c->n; i++) {
        ExCanvasNode *n = &c->nodes[i];
        int dup = 0;
        if (n->raw) continue;
        if (!n->id || !*n->id) dup = 1;
        else dup = ex_canvas_find_node(c, n->id) != (int)i;                              /* the index finds the first card of an id */
        if (dup) { char nid[17]; ex_canvas_new_id(c, nid); free(n->id); n->id = dups(nid); c->repaired = 1; if (!n->id) { ex_canvas_free(c); fail(err, errcap, "out of memory"); return NULL; } }
    }
    {   /* every edge has an id that no other edge has: the first edge of an id keeps it */
        EdgeKey *ord = c->ne ? malloc(c->ne * sizeof *ord) : NULL;
        size_t no = 0;
        unsigned char *again = c->ne ? calloc(c->ne, 1) : NULL;
        if (c->ne && (!ord || !again)) { free(ord); free(again); ex_canvas_free(c); fail(err, errcap, "out of memory"); return NULL; }
        for (i = 0; i < c->ne; i++) if (!c->edges[i].raw && c->edges[i].id && *c->edges[i].id) { ord[no].id = c->edges[i].id; ord[no].at = i; no++; }
        qsort(ord, no, sizeof *ord, cmp_edge_id);
        for (i = 1; i < no; i++) if (!strcmp(ord[i - 1].id, ord[i].id)) again[ord[i].at] = 1;       /* sorted by id, then by place: all but the first of an id */
        for (i = 0; i < c->ne; i++) {
            ExCanvasEdge *e = &c->edges[i];
            if (e->raw) continue;
            if (!e->id || !*e->id || again[i]) { char nid[17]; ex_canvas_new_id(c, nid); free(e->id); e->id = dups(nid); c->repaired = 1; if (!e->id) { free(ord); free(again); ex_canvas_free(c); fail(err, errcap, "out of memory"); return NULL; } }
        }
        free(ord); free(again);
    }
    c->hvalid = 0;
    c->final_newline = len > 0 && text[len - 1] == '\n';
    return c;
}

/* ---- writing ---- */

typedef struct Buf { char *p; size_t n, cap; int oom; } Buf;
static void bput(Buf *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        char *np;
        while (nc < b->n + n + 1) nc *= 2;
        np = realloc(b->p, nc);
        if (!np) { b->oom = 1; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void bstr(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void bch(Buf *b, char ch) { bput(b, &ch, 1); }
static void bjson_string(Buf *b, const char *s) {
    const unsigned char *u = (const unsigned char *)s;
    bch(b, '"');
    for (; *u; u++) {
        switch (*u) {
        case '"': bstr(b, "\\\""); break;
        case '\\': bstr(b, "\\\\"); break;
        case '\n': bstr(b, "\\n"); break;
        case '\r': bstr(b, "\\r"); break;
        case '\t': bstr(b, "\\t"); break;
        case '\b': bstr(b, "\\b"); break;
        case '\f': bstr(b, "\\f"); break;
        default:
            if (*u < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", *u); bstr(b, t); }
            else bch(b, (char)*u);
        }
    }
    bch(b, '"');
}
static void bnumber_as(Buf *b, double d, const char *spelled) {
    if (spelled && strtod(spelled, NULL) == d) { bstr(b, spelled); return; }       /* the value is the file's: so is the spelling */
    {
    char t[40];
    if (!isfinite(d)) d = 0;
    if (d == floor(d) && fabs(d) < 1e15) snprintf(t, sizeof t, "%lld", (long long)d);
    else {
        snprintf(t, sizeof t, "%.15g", d);
        if (strtod(t, NULL) != d) snprintf(t, sizeof t, "%.17g", d);
    }
    bstr(b, t);
    }
}
static void bkey(Buf *b, const char *k, int *first) { if (!*first) bch(b, ','); *first = 0; bjson_string(b, k); bch(b, ':'); }

/* One member of a node by its key: returns 1 if the node has it (and it was written). */
static int emit_node_key(Buf *b, const ExCanvasNode *n, int id, int *first) {
    const char *s = NULL;
    switch (id) {
    case NK_ID: s = n->id; break;
    case NK_TYPE: s = n->kind == EX_CN_OTHER ? n->type : KINDS[n->kind]; break;
    case NK_X: bkey(b, "x", first); bnumber_as(b, n->x, n->numtext[0]); return 1;
    case NK_Y: bkey(b, "y", first); bnumber_as(b, n->y, n->numtext[1]); return 1;
    case NK_W: bkey(b, "width", first); bnumber_as(b, n->w, n->numtext[2]); return 1;
    case NK_H: bkey(b, "height", first); bnumber_as(b, n->h, n->numtext[3]); return 1;
    case NK_COLOR: s = n->color; break;
    case NK_TEXT: s = n->text; break;
    case NK_FILE: s = n->file; break;
    case NK_SUBPATH: s = n->subpath; break;
    case NK_URL: s = n->url; break;
    case NK_LABEL: s = n->label; break;
    case NK_BG: s = n->background; break;
    case NK_BGSTYLE: s = n->background_style; break;
    default: return 0;
    }
    if (!s) return 0;
    bkey(b, NODE_KEYS[id], first);
    bjson_string(b, s);
    return 1;
}
static const char *side_name(int s) { return s >= 0 && s < 4 ? SIDES[s] : NULL; }
static const char *end_name(int e) { return e >= 0 && e < 2 ? ENDS[e] : NULL; }
static int emit_edge_key(Buf *b, const ExCanvasEdge *e, int id, int *first) {
    const char *s = NULL;
    switch (id) {
    case EK_ID: s = e->id; break;
    case EK_FROM: s = e->from; break;
    case EK_TO: s = e->to; break;
    case EK_FSIDE: s = side_name(e->from_side); break;
    case EK_TSIDE: s = side_name(e->to_side); break;
    case EK_FEND: s = end_name(e->from_end); break;
    case EK_TEND: s = end_name(e->to_end); break;
    case EK_COLOR: s = e->color; break;
    case EK_LABEL: s = e->label; break;
    default: return 0;
    }
    if (!s) return 0;
    bkey(b, EDGE_KEYS[id], first);
    bjson_string(b, s);
    return 1;
}

static void emit_extra(Buf *b, const ExCanvasKV *kv, const char *key, int *first) { bkey(b, key, first); bstr(b, kv->json); }

static void write_node(Buf *b, const ExCanvasNode *n) {
    int first = 1, done[NK_COUNT] = {0}, i;
    size_t k;
    if (n->raw) { bstr(b, n->raw); return; }
    bch(b, '{');
    for (k = 0; k < n->norder; k++) {
        int id = str_in(NODE_KEYS, NK_COUNT, n->order[k]);
        if (id >= 0 && !done[id] && emit_node_key(b, n, id, &first)) { done[id] = 1; continue; }
        {
            const ExCanvasKV *kv = kv_find(n->extra, n->nextra, n->order[k]);
            if (kv) emit_extra(b, kv, n->order[k], &first);
        }
    }
    for (i = 0; i < NK_COUNT; i++) if (!done[i] && emit_node_key(b, n, i, &first)) done[i] = 1;        /* what the file did not have yet */
    for (k = 0; k < n->nextra; k++) {                                                                  /* an unknown key whose place is not recorded */
        size_t o;
        int listed = 0;
        for (o = 0; o < n->norder; o++) if (!strcmp(n->order[o], n->extra[k].key)) { listed = 1; break; }
        if (!listed) emit_extra(b, &n->extra[k], n->extra[k].key, &first);
    }
    bch(b, '}');
}
static void write_edge(Buf *b, const ExCanvasEdge *e) {
    int first = 1, done[EK_COUNT] = {0}, i;
    size_t k;
    if (e->raw) { bstr(b, e->raw); return; }
    bch(b, '{');
    for (k = 0; k < e->norder; k++) {
        int id = str_in(EDGE_KEYS, EK_COUNT, e->order[k]);
        if (id >= 0 && !done[id] && emit_edge_key(b, e, id, &first)) { done[id] = 1; continue; }
        {
            const ExCanvasKV *kv = kv_find(e->extra, e->nextra, e->order[k]);
            if (kv) emit_extra(b, kv, e->order[k], &first);
        }
    }
    for (i = 0; i < EK_COUNT; i++) if (!done[i] && emit_edge_key(b, e, i, &first)) done[i] = 1;
    for (k = 0; k < e->nextra; k++) {
        size_t o;
        int listed = 0;
        for (o = 0; o < e->norder; o++) if (!strcmp(e->order[o], e->extra[k].key)) { listed = 1; break; }
        if (!listed) emit_extra(b, &e->extra[k], e->extra[k].key, &first);
    }
    bch(b, '}');
}

static void write_nodes(Buf *b, const ExCanvas *c) {
    size_t i;
    if (!c->n) { bstr(b, "[]"); return; }
    bstr(b, "[\n");
    for (i = 0; i < c->n; i++) { bstr(b, "\t\t"); write_node(b, &c->nodes[i]); bstr(b, i + 1 < c->n ? ",\n" : "\n"); }
    bstr(b, "\t]");
}
static void write_edges(Buf *b, const ExCanvas *c) {
    size_t i;
    if (!c->ne) { bstr(b, "[]"); return; }
    bstr(b, "[\n");
    for (i = 0; i < c->ne; i++) { bstr(b, "\t\t"); write_edge(b, &c->edges[i]); bstr(b, i + 1 < c->ne ? ",\n" : "\n"); }
    bstr(b, "\t]");
}

char *ex_canvas_write(const ExCanvas *c, size_t *len) {
    Buf b = {0};
    size_t k;
    int first = 1, did_nodes = 0, did_edges = 0;
    bstr(&b, "{");
    for (k = 0; k < c->norder; k++) {
        const char *key = c->order[k];
        if (!strcmp(key, "nodes") && c->has_nodes && !did_nodes) { bstr(&b, first ? "\n\t" : ",\n\t"); first = 0; bstr(&b, "\"nodes\":"); write_nodes(&b, c); did_nodes = 1; }
        else if (!strcmp(key, "edges") && c->has_edges && !did_edges) { bstr(&b, first ? "\n\t" : ",\n\t"); first = 0; bstr(&b, "\"edges\":"); write_edges(&b, c); did_edges = 1; }
        else {
            const ExCanvasKV *kv = kv_find(c->extra, c->nextra, key);
            if (kv) { bstr(&b, first ? "\n\t" : ",\n\t"); first = 0; bjson_string(&b, key); bch(&b, ':'); bstr(&b, kv->json); }
        }
    }
    if (c->has_nodes && !did_nodes) { bstr(&b, first ? "\n\t" : ",\n\t"); first = 0; bstr(&b, "\"nodes\":"); write_nodes(&b, c); }
    if (c->has_edges && !did_edges) { bstr(&b, first ? "\n\t" : ",\n\t"); first = 0; bstr(&b, "\"edges\":"); write_edges(&b, c); }
    for (k = 0; k < c->nextra; k++) {
        size_t o;
        int listed = 0;
        for (o = 0; o < c->norder; o++) if (!strcmp(c->order[o], c->extra[k].key)) { listed = 1; break; }
        if (!listed) { bstr(&b, first ? "\n\t" : ",\n\t"); first = 0; bjson_string(&b, c->extra[k].key); bch(&b, ':'); bstr(&b, c->extra[k].json); }
    }
    bstr(&b, first ? "}" : "\n}");
    if (c->final_newline) bch(&b, '\n');
    if (b.oom) { free(b.p); return NULL; }
    if (len) *len = b.n;
    return b.p;
}

/* ---- editing ---- */

static size_t id_hash(const char *id) { size_t h = 1469598103934665603ull, i; for (i = 0; id[i]; i++) { h ^= (unsigned char)id[i]; h *= 1099511628211ull; } return h; }

/* The index: open addressing over the positions of the cards, kept at most half full. A repeated id (the file had one that was not repaired yet) finds the first. */
static void build_index(ExCanvas *c) {
    size_t cap = 16, i;
    free(c->hash);
    c->hash = NULL; c->hcap = 0; c->hvalid = 0;
    while (cap < c->n * 2 + 2) cap *= 2;
    c->hash = calloc(cap, sizeof *c->hash);
    if (!c->hash) return;
    c->hcap = cap;
    for (i = 0; i < c->n; i++) {
        size_t h;
        if (c->nodes[i].raw || !c->nodes[i].id) continue;
        h = id_hash(c->nodes[i].id) & (cap - 1);
        while (c->hash[h]) {
            if (!strcmp(c->nodes[c->hash[h] - 1].id, c->nodes[i].id)) break;                  /* the first card of that id stays */
            h = (h + 1) & (cap - 1);
        }
        if (!c->hash[h]) c->hash[h] = i + 1;
    }
    c->hvalid = 1;
}

int ex_canvas_find_node(const ExCanvas *c, const char *id) {
    ExCanvas *m = (ExCanvas *)(void *)(size_t)(const void *)c;           /* the index is a cache: it is built on first use */
    size_t h;
    if (!id) return -1;
    if (!m->hvalid) build_index(m);
    if (!m->hash) { size_t i; for (i = 0; i < c->n; i++) if (c->nodes[i].id && !strcmp(c->nodes[i].id, id)) return (int)i; return -1; }
    h = id_hash(id) & (m->hcap - 1);
    while (m->hash[h]) {
        const ExCanvasNode *n = &c->nodes[m->hash[h] - 1];
        if (n->id && !strcmp(n->id, id)) return (int)(m->hash[h] - 1);
        h = (h + 1) & (m->hcap - 1);
    }
    return -1;
}

void ex_canvas_touch(ExCanvas *c) { if (c) c->rev++; }

int ex_canvas_add_node(ExCanvas *c, ExCanvasKind kind, double x, double y, double w, double h) {
    ExCanvasNode *n;
    char id[17];
    if (kind == EX_CN_OTHER || push_node(c, &n) != 0) return -1;
    c->hvalid = 0;
    ex_canvas_new_id(c, id);
    n->id = dups(id);
    n->kind = kind; n->x = x; n->y = y; n->w = w; n->h = h;
    if (kind == EX_CN_TEXT) n->text = dups("");
    else if (kind == EX_CN_FILE) n->file = dups("");
    else if (kind == EX_CN_LINK) n->url = dups("");
    if (!n->id || (kind == EX_CN_TEXT && !n->text) || (kind == EX_CN_FILE && !n->file) || (kind == EX_CN_LINK && !n->url)) { node_free(n); c->n--; return -1; }
    c->has_nodes = 1;
    c->rev++;
    return (int)(c->n - 1);
}

int ex_canvas_remove_node(ExCanvas *c, size_t i) {
    size_t e;
    char *id;
    if (i >= c->n) return -1;
    c->hvalid = 0;
    id = dups(c->nodes[i].id);
    node_free(&c->nodes[i]);
    memmove(&c->nodes[i], &c->nodes[i + 1], (c->n - i - 1) * sizeof *c->nodes);
    c->n--;
    for (e = c->ne; e > 0; e--) {
        const ExCanvasEdge *ed = &c->edges[e - 1];
        if (id && !ed->raw && ((ed->from && !strcmp(ed->from, id)) || (ed->to && !strcmp(ed->to, id)))) ex_canvas_remove_edge(c, e - 1);
    }
    free(id);
    c->rev++;
    return 0;
}

int ex_canvas_raise_node(ExCanvas *c, size_t i) {
    ExCanvasNode tmp;
    if (i >= c->n) return -1;
    if (i + 1 == c->n) return (int)i;
    c->hvalid = 0;
    tmp = c->nodes[i];
    memmove(&c->nodes[i], &c->nodes[i + 1], (c->n - i - 1) * sizeof *c->nodes);
    c->nodes[c->n - 1] = tmp;
    c->rev++;
    return (int)(c->n - 1);
}

int ex_canvas_set_string(ExCanvas *c, char **field, const char *s) {
    char *d = NULL;
    if (s && !(d = dups(s))) return -1;
    free(*field);
    *field = d;
    c->rev++;
    return 0;
}

int ex_canvas_add_edge(ExCanvas *c, size_t from, int from_side, size_t to, int to_side) {
    ExCanvasEdge *e;
    char id[17];
    if (from >= c->n || to >= c->n || c->nodes[from].raw || c->nodes[to].raw) return -1;
    if (push_edge(c, &e) != 0) return -1;
    ex_canvas_new_id(c, id);
    e->id = dups(id); e->from = dups(c->nodes[from].id); e->to = dups(c->nodes[to].id);
    e->from_side = from_side; e->to_side = to_side; e->from_end = e->to_end = EX_CE_DEFAULT;
    if (!e->id || !e->from || !e->to) { edge_free(e); c->ne--; return -1; }
    c->has_edges = 1;
    c->rev++;
    return (int)(c->ne - 1);
}

int ex_canvas_remove_edge(ExCanvas *c, size_t i) {
    if (i >= c->ne) return -1;
    edge_free(&c->edges[i]);
    memmove(&c->edges[i], &c->edges[i + 1], (c->ne - i - 1) * sizeof *c->edges);
    c->ne--;
    c->rev++;
    return 0;
}

/* ---- geometry ---- */

static int drawn(const ExCanvasNode *n) { return !n->raw && n->kind != EX_CN_OTHER; }

int ex_canvas_bounds(const ExCanvas *c, double *x0, double *y0, double *x1, double *y1) {
    size_t i;
    int any = 0;
    double ax = 0, ay = 0, bx = 0, by = 0;
    for (i = 0; i < c->n; i++) {
        const ExCanvasNode *n = &c->nodes[i];
        double l, t, r, b;
        if (!drawn(n)) continue;
        l = n->x < n->x + n->w ? n->x : n->x + n->w; r = n->x < n->x + n->w ? n->x + n->w : n->x;
        t = n->y < n->y + n->h ? n->y : n->y + n->h; b = n->y < n->y + n->h ? n->y + n->h : n->y;
        if (!any) { ax = l; ay = t; bx = r; by = b; any = 1; }
        else { if (l < ax) ax = l; if (t < ay) ay = t; if (r > bx) bx = r; if (b > by) by = b; }
    }
    if (!any) return 0;
    if (x0) *x0 = ax;
    if (y0) *y0 = ay;
    if (x1) *x1 = bx;
    if (y1) *y1 = by;
    return 1;
}

static int inside(const ExCanvasNode *n, double x, double y) { return x >= n->x && x <= n->x + n->w && y >= n->y && y <= n->y + n->h; }

int ex_canvas_node_at(const ExCanvas *c, double x, double y) {
    size_t i;
    for (i = c->n; i > 0; i--) { const ExCanvasNode *n = &c->nodes[i - 1]; if (drawn(n) && n->kind != EX_CN_GROUP && inside(n, x, y)) return (int)(i - 1); }
    for (i = c->n; i > 0; i--) { const ExCanvasNode *n = &c->nodes[i - 1]; if (drawn(n) && n->kind == EX_CN_GROUP && inside(n, x, y)) return (int)(i - 1); }
    return -1;
}

void ex_canvas_side_point(const ExCanvasNode *n, int side, double *x, double *y) {
    double px = n->x + n->w / 2, py = n->y + n->h / 2;
    switch (side) {
    case EX_CS_TOP: py = n->y; break;
    case EX_CS_RIGHT: px = n->x + n->w; break;
    case EX_CS_BOTTOM: py = n->y + n->h; break;
    case EX_CS_LEFT: px = n->x; break;
    default: break;
    }
    if (x) *x = px;
    if (y) *y = py;
}

int ex_canvas_nearest_side(const ExCanvasNode *n, double x, double y) {
    double w = n->w > 1 ? n->w : 1, h = n->h > 1 ? n->h : 1;
    double dx = (x - (n->x + n->w / 2)) / w, dy = (y - (n->y + n->h / 2)) / h;
    if (fabs(dx) >= fabs(dy)) return dx >= 0 ? EX_CS_RIGHT : EX_CS_LEFT;
    return dy >= 0 ? EX_CS_BOTTOM : EX_CS_TOP;
}

int ex_canvas_edge_ends(const ExCanvas *c, const ExCanvasEdge *e, double *x1, double *y1, int *side1, double *x2, double *y2, int *side2) {
    int a, b, s1, s2;
    const ExCanvasNode *na, *nb;
    double ax, ay, bx, by;
    if (e->raw) return 0;
    a = ex_canvas_find_node(c, e->from); b = ex_canvas_find_node(c, e->to);
    if (a < 0 || b < 0) return 0;
    na = &c->nodes[a]; nb = &c->nodes[b];
    if (!drawn(na) || !drawn(nb)) return 0;
    s1 = e->from_side; s2 = e->to_side;
    ax = na->x + na->w / 2; ay = na->y + na->h / 2; bx = nb->x + nb->w / 2; by = nb->y + nb->h / 2;
    if (s1 < 0) {
        double dx = bx - ax, dy = by - ay;
        s1 = fabs(dx) >= fabs(dy) ? (dx >= 0 ? EX_CS_RIGHT : EX_CS_LEFT) : (dy >= 0 ? EX_CS_BOTTOM : EX_CS_TOP);
    }
    if (s2 < 0) {
        double dx = ax - bx, dy = ay - by;
        s2 = fabs(dx) >= fabs(dy) ? (dx >= 0 ? EX_CS_RIGHT : EX_CS_LEFT) : (dy >= 0 ? EX_CS_BOTTOM : EX_CS_TOP);
    }
    ex_canvas_side_point(na, s1, x1, y1);
    ex_canvas_side_point(nb, s2, x2, y2);
    if (side1) *side1 = s1;
    if (side2) *side2 = s2;
    return 1;
}

size_t ex_canvas_group_members(const ExCanvas *c, size_t group, size_t *out, size_t max) {
    size_t i, k = 0;
    const ExCanvasNode *g;
    if (group >= c->n || c->nodes[group].kind != EX_CN_GROUP || c->nodes[group].raw) return 0;
    g = &c->nodes[group];
    for (i = 0; i < c->n; i++) {
        const ExCanvasNode *n = &c->nodes[i];
        if (i == group || !drawn(n)) continue;
        if (n->x >= g->x && n->y >= g->y && n->x + n->w <= g->x + g->w && n->y + n->h <= g->y + g->h && !(n->w == g->w && n->h == g->h && n->x == g->x && n->y == g->y)) {
            if (k < max) out[k] = i;
            k++;
        }
    }
    return k < max ? k : max;
}

int ex_canvas_color(const char *color, uint8_t rgb[3]) {
    unsigned v;
    size_t i;
    if (!color || !*color) return -1;
    if (color[1] == 0 && color[0] >= '1' && color[0] <= '6') return color[0] - '0';
    if (color[0] != '#' || (strlen(color) != 7 && strlen(color) != 4)) return -1;
    for (i = 1; color[i]; i++) if (!((color[i] >= '0' && color[i] <= '9') || (color[i] >= 'a' && color[i] <= 'f') || (color[i] >= 'A' && color[i] <= 'F'))) return -1;
    if (strlen(color) == 7) { if (sscanf(color + 1, "%x", &v) != 1) return -1; }
    else { unsigned r, g, b; if (sscanf(color + 1, "%1x%1x%1x", &r, &g, &b) != 3) return -1; v = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17); }
    rgb[0] = (uint8_t)(v >> 16); rgb[1] = (uint8_t)(v >> 8); rgb[2] = (uint8_t)v;
    return 0;
}
