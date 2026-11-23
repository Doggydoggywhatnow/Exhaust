/* Bases: the `.base` file and running a view over files. See include/exhaust_base.h and docs/formats.md. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base.h"
#include "exhaust_datefmt.h"

static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static int ieq(const char *a, const char *b) { while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; a++; b++; } return !*a && !*b; }

/* ---- the file ---- */

struct ExBase { ExYNode *root; };

ExBase *ex_base_parse(const char *text, size_t len, char *err, size_t errcap) {
    ExBase *b;
    ExYNode *root = ex_y_parse(text, len, err, errcap);
    if (!root) return NULL;
    if (root->kind != EXY_MAP) { ex_y_free(root); if (err && errcap) snprintf(err, errcap, "a base is a YAML mapping (filters, formulas, views...), not a list or a single value"); return NULL; }
    b = calloc(1, sizeof *b);
    if (!b) { ex_y_free(root); if (err && errcap) snprintf(err, errcap, "out of memory"); return NULL; }
    b->root = root;
    return b;
}

ExBase *ex_base_new(void) {
    static const char text[] = "views:\n  - type: table\n    name: Table\n";
    return ex_base_parse(text, sizeof text - 1, NULL, 0);
}
void ex_base_free(ExBase *b) { if (!b) return; ex_y_free(b->root); free(b); }
char *ex_base_write(const ExBase *b, size_t *len) { return ex_y_write(b->root, len); }
ExYNode *ex_base_root(ExBase *b) { return b->root; }
const ExYNode *ex_base_root_const(const ExBase *b) { return b->root; }

size_t ex_base_view_count(const ExBase *b) { const ExYNode *v = ex_y_get(b->root, "views"); return v && v->kind == EXY_SEQ ? v->n : 0; }
const ExYNode *ex_base_view(const ExBase *b, size_t i) { const ExYNode *v = ex_y_get(b->root, "views"); const ExYNode *n = v ? ex_y_item(v, i) : NULL; return n && n->kind == EXY_MAP ? n : NULL; }
const char *ex_base_view_name(const ExBase *b, size_t i) { const ExYNode *v = ex_base_view(b, i); const char *s = v ? ex_y_str(ex_y_get(v, "name")) : NULL; return s ? s : ""; }
const char *ex_base_view_type(const ExBase *b, size_t i) { const ExYNode *v = ex_base_view(b, i); const char *s = v ? ex_y_str(ex_y_get(v, "type")) : NULL; return s && *s ? s : "table"; }
int ex_base_find_view(const ExBase *b, const char *name) { size_t i, n = ex_base_view_count(b); for (i = 0; i < n; i++) if (ieq(ex_base_view_name(b, i), name)) return (int)i; return -1; }

static const struct { const char *id, *name; } DEFAULT_NAMES[] = {
    { "file.name", "File name" }, { "file.basename", "Basename" }, { "file.path", "Path" }, { "file.folder", "Folder" }, { "file.ext", "Extension" }, { "file.size", "Size" },
    { "file.ctime", "Created time" }, { "file.mtime", "Modified time" }, { "file.tags", "Tags" }, { "file.links", "Links" }, { "file.embeds", "Embeds" }, { "file.backlinks", "Backlinks" },
    { "file.properties", "Properties" },
};

const char *ex_base_display_name(const ExBase *b, const char *id, char *buf, size_t cap) {
    const ExYNode *p = ex_y_get(ex_y_get(b->root, "properties"), id);
    const char *dn = p ? ex_y_str(ex_y_get(p, "displayName")) : NULL;
    size_t i;
    if (dn && *dn) return dn;
    for (i = 0; i < sizeof DEFAULT_NAMES / sizeof *DEFAULT_NAMES; i++) if (!strcmp(DEFAULT_NAMES[i].id, id)) return DEFAULT_NAMES[i].name;
    if (!strncmp(id, "note.", 5)) id += 5;
    else if (!strncmp(id, "formula.", 8)) id += 8;
    snprintf(buf, cap, "%s", id);
    return buf;
}

/* ---- the data ---- */

static void file_free(ExBaseFile *f) {
    size_t i;
    free(f->path);
    for (i = 0; i < f->nprops; i++) { free(f->props[i].name); free(f->props[i].value); }
    free(f->props);
    for (i = 0; i < f->ntags; i++) free(f->tags[i]);
    free(f->tags);
    for (i = 0; i < f->nlinks; i++) free(f->links[i]);
    free(f->links);
    for (i = 0; i < f->nembeds; i++) free(f->embeds[i]);
    free(f->embeds);
}
void ex_base_data_free(ExBaseData *d) { size_t i; if (!d) return; for (i = 0; i < d->n; i++) file_free(&d->files[i]); free(d->files); memset(d, 0, sizeof *d); }

ExBaseFile *ex_base_data_add(ExBaseData *d, const char *path, int kind, double ctime_ms, double mtime_ms, double size) {
    ExBaseFile *f;
    if (d->n == d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 64;
        ExBaseFile *nf = realloc(d->files, nc * sizeof *nf);
        if (!nf) return NULL;
        d->files = nf; d->cap = nc;
    }
    f = &d->files[d->n];
    memset(f, 0, sizeof *f);
    f->path = dups(path);
    if (!f->path) return NULL;
    f->kind = kind; f->ctime_ms = ctime_ms; f->mtime_ms = mtime_ms; f->size = size;
    d->n++;
    return f;
}

static int add_str(char ***arr, size_t *n, const char *s) {
    char **na = realloc(*arr, (*n + 1) * sizeof **arr);
    char *c = dups(s);
    if (!na || !c) { free(c); if (na) *arr = na; return -1; }
    *arr = na;
    (*arr)[(*n)++] = c;
    return 0;
}
int ex_base_file_prop(ExBaseFile *f, const char *name, int type, const char *value) {
    ExBaseProp *np = realloc(f->props, (f->nprops + 1) * sizeof *np);
    if (!np) return -1;
    f->props = np;
    f->props[f->nprops].name = dups(name); f->props[f->nprops].value = dups(value ? value : ""); f->props[f->nprops].type = type;
    if (!f->props[f->nprops].name || !f->props[f->nprops].value) return -1;
    f->nprops++;
    return 0;
}
int ex_base_file_tag(ExBaseFile *f, const char *tag) { return add_str(&f->tags, &f->ntags, tag); }
int ex_base_file_link(ExBaseFile *f, const char *target) { return add_str(&f->links, &f->nlinks, target); }
int ex_base_file_embed(ExBaseFile *f, const char *target) { return add_str(&f->embeds, &f->nembeds, target); }

/* ---- the context of a run ---- */

typedef struct FExpr { const char *name; ExExpr *expr; } FExpr;
typedef struct EExpr { const char *text; ExExpr *expr; } EExpr;
typedef struct PathIdx { const char *key; size_t file; } PathIdx;

typedef struct Ctx {
    const ExBase *base;
    const ExBaseData *data;
    ExArena *arena;                         /* the result's arena: what the cells hold */
    ExArena *tmp;                           /* per row work, reset often */
    const ExBaseFile *row;
    const char *this_path;
    FExpr *formulas; size_t nformulas;
    EExpr *cache; size_t ncache, capcache;
    PathIdx *by_path; PathIdx *by_name;     /* sorted: the path, and the lowercase name without .md */
    char *name_keys;                        /* storage of by_name keys */
    char **errs; size_t nerrs;
    ExExprHost host;
    ExValue **backlinks;                    /* per file: lazily built list of the files that link to it */
    int backlinks_built;
    size_t *bl_count; size_t **bl_files;
} Ctx;

static int cmp_pathidx(const void *a, const void *b) { return strcmp(((const PathIdx *)a)->key, ((const PathIdx *)b)->key); }
static const PathIdx *find_key(const PathIdx *arr, size_t n, const char *key) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; int c = strcmp(arr[mid].key, key); if (!c) { while (mid > 0 && !strcmp(arr[mid - 1].key, key)) mid--; return &arr[mid]; } if (c < 0) lo = mid + 1; else hi = mid; }
    return NULL;
}

static void record_error(Ctx *c, const char *what, const char *msg) {
    char buf[400];
    size_t i;
    if (c->nerrs >= 16) return;
    snprintf(buf, sizeof buf, "%s: %s", what, msg);
    for (i = 0; i < c->nerrs; i++) if (!strcmp(c->errs[i], buf)) return;
    c->errs[c->nerrs++] = ex_arena_strdup(c->arena, buf);
}

static const ExBaseFile *file_by_path(Ctx *c, const char *path) {
    const PathIdx *p = find_key(c->by_path, c->data->n, path);
    return p ? &c->data->files[p->file] : NULL;
}

static const char *base_of(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

/* The file a link text names: a path (with or without .md), else a name (without the extension), any case. */
static int ctx_resolve(void *u, const char *from, const char *text, char *path, size_t cap) {
    Ctx *c = u;
    char key[700], *h;
    const PathIdx *p;
    size_t n;
    (void)from;
    snprintf(key, sizeof key, "%s", text);
    if ((h = strchr(key, '#'))) *h = 0;
    if ((h = strchr(key, '|'))) *h = 0;
    if ((p = find_key(c->by_path, c->data->n, key))) { snprintf(path, cap, "%s", c->data->files[p->file].path); return 1; }
    n = strlen(key);
    if (n < 600) { char withmd[720]; snprintf(withmd, sizeof withmd, "%s.md", key); if ((p = find_key(c->by_path, c->data->n, withmd))) { snprintf(path, cap, "%s", c->data->files[p->file].path); return 1; } }
    {
        size_t i;
        for (i = 0; key[i]; i++) key[i] = (char)tolower((unsigned char)key[i]);
        if (n > 3 && !strcmp(key + n - 3, ".md")) key[n - 3] = 0;
        if ((p = find_key(c->by_name, c->data->n, base_of(key)))) {
            const char *want = key;
            const PathIdx *q = p;
            size_t best = (size_t)-1;
            while (q < c->by_name + c->data->n && !strcmp(q->key, p->key)) {                 /* several files with that name: the one whose path ends like the text, else the shortest */
                const char *fp = c->data->files[q->file].path;
                size_t fl = strlen(fp), wl = strlen(want);
                if (strchr(want, '/') && fl >= wl && ieq(fp + fl - wl, want)) { best = q->file; break; }
                if (best == (size_t)-1 || fl < strlen(c->data->files[best].path)) best = q->file;
                q++;
            }
            snprintf(path, cap, "%s", c->data->files[best].path);
            return 1;
        }
    }
    return 0;
}

/* ---- the values of properties ---- */

static ExValue text_to_value(ExArena *a, const char *s) {
    size_t n = strlen(s);
    if (n >= 4 && s[0] == '[' && s[1] == '[' && s[n - 1] == ']' && s[n - 2] == ']') {
        char *w = ex_arena_strndup(a, s + 2, n - 4), *bar, *hash;
        const char *disp = NULL;
        if (!w) return ex_v_null();
        bar = strchr(w, '|');
        if (bar) { *bar = 0; disp = bar + 1; }
        hash = strchr(w, '#');
        if (hash) *hash++ = 0;
        return ex_v_link(a, w, hash, disp);
    }
    if (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8)) return ex_v_url(a, s, NULL);
    return ex_v_str(a, s);
}

static ExValue prop_value(ExArena *a, const ExBaseProp *p) {
    double ms;
    int has;
    char *end;
    switch (p->type) {
    case EXB_NUMBER: { double d = strtod(p->value, &end); return end != p->value && !*end ? ex_v_num(d) : ex_v_str(a, p->value); }
    case EXB_CHECKBOX: return ex_v_bool(!strcmp(p->value, "true"));
    case EXB_DATE: case EXB_DATETIME: if (!ex_date_parse(p->value, strlen(p->value), 0, &ms, &has)) return ex_v_date(ms, p->type == EXB_DATETIME || has); return ex_v_str(a, p->value);
    case EXB_LIST: case EXB_LINKS: {
        ExValue *items;
        size_t n = 0, cap = 1, i;
        const char *s = p->value;
        if (!*s) return ex_v_list(a, NULL, 0);
        for (i = 0; s[i]; i++) if (s[i] == '\n') cap++;
        items = ex_arena_alloc(a, cap * sizeof *items);
        if (!items) return ex_v_null();
        while (1) {
            const char *e = strchr(s, '\n');
            char *item = ex_arena_strndup(a, s, e ? (size_t)(e - s) : strlen(s));
            items[n++] = text_to_value(a, item ? item : "");
            if (!e) break;
            s = e + 1;
        }
        return ex_v_list(a, items, n);
    }
    default: return text_to_value(a, p->value);
    }
}

static const ExBaseProp *find_prop(const ExBaseFile *f, const char *name) {
    size_t i;
    for (i = 0; i < f->nprops; i++) if (!strcmp(f->props[i].name, name)) return &f->props[i];
    for (i = 0; i < f->nprops; i++) if (ieq(f->props[i].name, name)) return &f->props[i];
    return NULL;
}

static int ctx_note_prop(void *u, ExArena *a, const char *name, ExValue *out) {
    Ctx *c = u;
    const ExBaseProp *p = c->row ? find_prop(c->row, name) : NULL;
    if (!p) return 0;
    *out = prop_value(a, p);
    return 1;
}

static void build_backlinks(Ctx *c) {
    size_t i, j;
    c->bl_count = calloc(c->data->n ? c->data->n : 1, sizeof *c->bl_count);
    c->bl_files = calloc(c->data->n ? c->data->n : 1, sizeof *c->bl_files);
    c->backlinks_built = 1;
    if (!c->bl_count || !c->bl_files) return;
    for (i = 0; i < c->data->n; i++) {
        const ExBaseFile *f = &c->data->files[i];
        for (j = 0; j < f->nlinks; j++) {
            char path[700];
            const PathIdx *t;
            size_t k, tgt;
            if (!ctx_resolve(c, f->path, f->links[j], path, sizeof path)) continue;
            t = find_key(c->by_path, c->data->n, path);
            if (!t) continue;
            tgt = t->file;
            for (k = 0; k < c->bl_count[tgt] && c->bl_files[tgt][k] != i; k++) {}
            if (k < c->bl_count[tgt]) continue;
            { size_t *nb = realloc(c->bl_files[tgt], (c->bl_count[tgt] + 1) * sizeof *nb); if (!nb) continue; c->bl_files[tgt] = nb; c->bl_files[tgt][c->bl_count[tgt]++] = i; }
        }
    }
}

static ExValue str_list(ExArena *a, char **v, size_t n, int as_links) {
    ExValue *items = n ? ex_arena_alloc(a, n * sizeof *items) : NULL;
    size_t i;
    if (n && !items) return ex_v_null();
    for (i = 0; i < n; i++) {
        if (as_links) { char *w = ex_arena_strdup(a, v[i]), *hash; hash = w ? strchr(w, '#') : NULL; if (hash) *hash++ = 0; items[i] = ex_v_link(a, w ? w : "", hash, NULL); }
        else items[i] = ex_v_str(a, v[i]);
    }
    return ex_v_list(a, items, n);
}

static int ctx_file_field(void *u, ExArena *a, const char *path, const char *f, ExValue *out) {
    Ctx *c = u;
    const ExBaseFile *file = file_by_path(c, path);
    const char *base, *dot;
    size_t i;
    if (!file) return 0;
    base = base_of(file->path);
    dot = strrchr(base, '.');
    if (!strcmp(f, "name")) *out = ex_v_str(a, base);
    else if (!strcmp(f, "basename")) *out = ex_v_strn(a, base, dot && dot != base ? (size_t)(dot - base) : strlen(base));
    else if (!strcmp(f, "path")) *out = ex_v_str(a, file->path);
    else if (!strcmp(f, "folder")) *out = ex_v_strn(a, file->path, base > file->path ? (size_t)(base - file->path - 1) : 0);
    else if (!strcmp(f, "ext")) *out = ex_v_str(a, dot && dot != base ? dot + 1 : "");
    else if (!strcmp(f, "size")) *out = ex_v_num(file->size);
    else if (!strcmp(f, "ctime")) *out = ex_v_date(file->ctime_ms, 1);
    else if (!strcmp(f, "mtime")) *out = ex_v_date(file->mtime_ms, 1);
    else if (!strcmp(f, "tags")) *out = str_list(a, file->tags, file->ntags, 0);
    else if (!strcmp(f, "links")) *out = str_list(a, file->links, file->nlinks, 1);
    else if (!strcmp(f, "embeds")) *out = str_list(a, file->embeds, file->nembeds, 1);
    else if (!strcmp(f, "backlinks")) {
        size_t idx = (size_t)(file - c->data->files), k;
        ExValue *items;
        if (!c->backlinks_built) build_backlinks(c);
        k = c->bl_count ? c->bl_count[idx] : 0;
        items = k ? ex_arena_alloc(a, k * sizeof *items) : NULL;
        for (i = 0; i < k; i++) items[i] = ex_v_file(a, c->data->files[c->bl_files[idx][i]].path);
        *out = ex_v_list(a, items, k);
    }
    else if (!strcmp(f, "properties")) {
        const char **keys = file->nprops ? ex_arena_alloc(a, file->nprops * sizeof *keys) : NULL;
        ExValue *vals = file->nprops ? ex_arena_alloc(a, file->nprops * sizeof *vals) : NULL;
        for (i = 0; i < file->nprops; i++) { keys[i] = file->props[i].name; vals[i] = prop_value(a, &file->props[i]); }
        *out = ex_v_object(a, keys, vals, file->nprops);
    }
    else return 0;
    return 1;
}

static const ExExpr *ctx_formula(void *u, const char *name) {
    Ctx *c = u;
    size_t i;
    for (i = 0; i < c->nformulas; i++) if (!strcmp(c->formulas[i].name, name)) return c->formulas[i].expr;
    return NULL;
}

/* The parsed expression of a text of the base (cached for the run: the same text is evaluated once for every row). */
static const ExExpr *expr_of(Ctx *c, const char *text, int is_summary_or_filter) {
    size_t i;
    char err[200];
    int pos = 0;
    ExExpr *e;
    (void)is_summary_or_filter;
    for (i = 0; i < c->ncache; i++) if (c->cache[i].text == text) return c->cache[i].expr;
    e = ex_expr_parse(text, err, sizeof err, &pos);
    if (!e) {
        char m[300];
        snprintf(m, sizeof m, "%s (at character %d of %s)", err, pos + 1, text);
        record_error(c, "an expression does not parse", m);
        e = ex_expr_failed(err);
    }
    if (c->ncache == c->capcache) { size_t nc = c->capcache ? c->capcache * 2 : 16; EExpr *nx = realloc(c->cache, nc * sizeof *nx); if (!nx) { ex_expr_free(e); return NULL; } c->cache = nx; c->capcache = nc; }
    c->cache[c->ncache].text = text; c->cache[c->ncache].expr = e; c->ncache++;
    return e;
}

static void set_row(Ctx *c, const ExBaseFile *row) {
    c->row = row;
    c->host.file_path = row ? row->path : NULL;
}

/* A property id evaluated for the current row. */
static ExValue eval_id(Ctx *c, ExArena *a, const char *id) {
    ExValue v = ex_v_null();
    const ExBaseFile *row = c->row;
    if (!row) return v;
    if (!strncmp(id, "file.", 5)) { if (ctx_file_field(c, a, row->path, id + 5, &v)) return v; return ex_v_null(); }
    if (!strncmp(id, "formula.", 8)) {
        const ExExpr *e = ctx_formula(c, id + 8);
        if (!e) return ex_v_null();
        v = ex_expr_eval(e, a, &c->host, NULL);
        if (v.t == EXV_ERROR) record_error(c, id, v.u.str);
        return v;
    }
    if (!strncmp(id, "note.", 5)) id += 5;
    if (ctx_note_prop(c, a, id, &v)) return v;
    return ex_v_null();
}

static int is_true_expr(Ctx *c, const char *text) {
    const ExExpr *e = expr_of(c, text, 1);
    ExValue v;
    if (!e) return 0;
    ex_arena_reset(c->tmp);
    v = ex_expr_eval(e, c->tmp, &c->host, NULL);
    if (v.t == EXV_ERROR) { record_error(c, text, v.u.str); return 0; }
    return ex_value_truthy(v);
}

/* A filter: a statement, or and / or / not of filters. */
static int filter_passes(Ctx *c, const ExYNode *f, int depth) {
    size_t i, n;
    if (!f || depth > 50) return 1;
    if (f->kind == EXY_SCALAR) { const char *t = ex_y_str(f); return !t || !*t ? 1 : is_true_expr(c, t); }
    if (f->kind == EXY_SEQ) { for (i = 0; i < f->n; i++) if (!filter_passes(c, f->kids[i], depth + 1)) return 0; return 1; }
    if (f->kind != EXY_MAP) return 1;
    n = ex_y_count(f);
    for (i = 0; i < n; i++) {
        const char *k = ex_y_key(f, i);
        const ExYNode *list = ex_y_val(f, i);
        size_t j, m;
        if (!strcmp(k, "and")) { m = ex_y_count(list); for (j = 0; j < m; j++) if (!filter_passes(c, ex_y_item(list, j), depth + 1)) return 0; }
        else if (!strcmp(k, "or")) {
            int any = 0;
            m = ex_y_count(list);
            if (!m) continue;
            for (j = 0; j < m && !any; j++) any = filter_passes(c, ex_y_item(list, j), depth + 1);
            if (!any) return 0;
        }
        else if (!strcmp(k, "not")) { m = ex_y_count(list); for (j = 0; j < m; j++) if (filter_passes(c, ex_y_item(list, j), depth + 1)) return 0; }
    }
    return 1;
}

/* ---- running a view ---- */

typedef struct Key { size_t row; ExValue *v; } Key;
typedef struct SortSpec { const char *id; int desc; } SortSpec;
static SortSpec *g_sort; static size_t g_nsort;
static int sort_cmp(const void *x, const void *y) {
    const Key *a = x, *b = y;
    size_t i;
    for (i = 0; i < g_nsort; i++) {
        ExValue va = a->v[i], vb = b->v[i];
        int an = va.t == EXV_NULL || va.t == EXV_ERROR, bn = vb.t == EXV_NULL || vb.t == EXV_ERROR, c;
        if (an || bn) { if (an && bn) continue; return an ? 1 : -1; }                                  /* a missing value is last, whichever way it sorts */
        c = ex_value_compare(va, vb);
        if (c) return g_sort[i].desc ? -c : c;
    }
    return a->row < b->row ? -1 : a->row > b->row;
}

typedef struct GKey { size_t row; ExValue key; } GKey;
static int gk_desc;
static int group_cmp(const void *x, const void *y) {
    const GKey *a = x, *b = y;
    int an = a->key.t == EXV_NULL, bn = b->key.t == EXV_NULL, c;
    if (an || bn) { if (an && bn) return a->row < b->row ? -1 : a->row > b->row; return an ? 1 : -1; }
    c = ex_value_compare(a->key, b->key);
    if (c) return gk_desc ? -c : c;
    return a->row < b->row ? -1 : a->row > b->row;
}

static const ExYNode *dir_node(const ExYNode *m) { return m ? ex_y_get(m, "direction") : NULL; }
static int is_desc(const ExYNode *m) { const char *d = ex_y_str(dir_node(m)); return d && ieq(d, "DESC"); }

static ExValue summarize(Ctx *c, const char *name, const ExValue *vals, size_t n, ExArena *a) {
    size_t i;
    double lo = 0, hi = 0, sum = 0, *nums = NULL;
    size_t cnt = 0;
    ExValue earliest = ex_v_null(), latest = ex_v_null();
    int dates = 0, nonempty;
    if (!strcmp(name, "Empty") || !strcmp(name, "Filled")) {
        size_t empty = 0;
        for (i = 0; i < n; i++) empty += ex_value_is_empty(vals[i]);
        return ex_v_num((double)(name[0] == 'E' ? empty : n - empty));
    }
    if (!strcmp(name, "Unique")) {
        size_t u = 0, j;
        for (i = 0; i < n; i++) { int dup = 0; for (j = 0; j < i && !dup; j++) dup = ex_value_equal(&c->host, vals[i], vals[j]); if (!dup) u++; }
        return ex_v_num((double)u);
    }
    if (!strcmp(name, "Checked") || !strcmp(name, "Unchecked")) {
        size_t k = 0;
        for (i = 0; i < n; i++) if (vals[i].t == EXV_BOOL && (vals[i].u.num != 0) == (name[0] == 'C')) k++;
        return ex_v_num((double)k);
    }
    nums = n ? ex_arena_alloc(a, n * sizeof *nums) : NULL;
    for (i = 0; i < n; i++) {
        if (vals[i].t == EXV_NUM && !isnan(vals[i].u.num)) { if (!cnt || vals[i].u.num < lo) lo = vals[i].u.num; if (!cnt || vals[i].u.num > hi) hi = vals[i].u.num; sum += vals[i].u.num; nums[cnt++] = vals[i].u.num; }
        else if (vals[i].t == EXV_DATE) {
            dates++;
            if (earliest.t == EXV_NULL || vals[i].u.date.ms < earliest.u.date.ms) earliest = vals[i];
            if (latest.t == EXV_NULL || vals[i].u.date.ms > latest.u.date.ms) latest = vals[i];
        }
    }
    nonempty = cnt > 0;
    if (!strcmp(name, "Earliest")) return earliest;
    if (!strcmp(name, "Latest")) return latest;
    if (!strcmp(name, "Range")) {
        if (dates && !cnt) return ex_v_duration(latest.u.date.ms - earliest.u.date.ms, 0);
        return nonempty ? ex_v_num(hi - lo) : ex_v_null();
    }
    if (!nonempty) return ex_v_null();
    if (!strcmp(name, "Sum")) return ex_v_num(sum);
    if (!strcmp(name, "Average")) return ex_v_num(sum / (double)cnt);
    if (!strcmp(name, "Min")) return ex_v_num(lo);
    if (!strcmp(name, "Max")) return ex_v_num(hi);
    if (!strcmp(name, "Median")) {
        size_t j;
        for (i = 1; i < cnt; i++) { double t = nums[i]; for (j = i; j > 0 && nums[j - 1] > t; j--) nums[j] = nums[j - 1]; nums[j] = t; }
        return ex_v_num(cnt % 2 ? nums[cnt / 2] : (nums[cnt / 2 - 1] + nums[cnt / 2]) / 2.0);
    }
    if (!strcmp(name, "Stddev")) { double mean = sum / (double)cnt, ss = 0; for (i = 0; i < cnt; i++) ss += (nums[i] - mean) * (nums[i] - mean); return ex_v_num(sqrt(ss / (double)cnt)); }
    return ex_v_error(a, "there is no summary called %s", name);
}

int ex_base_run(const ExBase *b, size_t view, const ExBaseData *data, const char *this_path, double now_ms, int tz_min, ExBaseResult *out) {
    Ctx c;
    const ExYNode *v = ex_base_view(b, view), *gfilters, *vfilters, *order, *sort, *group, *forder, *summ, *fm, *csum;
    size_t i, j, k, nrows = 0, *rows = NULL;
    long limit = -1;
    memset(out, 0, sizeof *out);
    if (!v) return -1;
    memset(&c, 0, sizeof c);
    c.base = b; c.data = data; c.this_path = this_path;
    c.arena = out->arena = ex_arena_new();
    c.tmp = ex_arena_new();
    if (!c.arena || !c.tmp) { ex_arena_free(c.tmp); ex_base_result_free(out); return -1; }
    c.errs = ex_arena_alloc(c.arena, 16 * sizeof *c.errs);
    c.host.user = &c; c.host.note_prop = ctx_note_prop; c.host.file_field = ctx_file_field; c.host.resolve = ctx_resolve; c.host.formula = ctx_formula;
    c.host.this_path = this_path; c.host.now_ms = now_ms; c.host.tz_min = tz_min;
    /* indexes by path and by name */
    c.by_path = malloc((data->n ? data->n : 1) * sizeof *c.by_path);
    c.by_name = malloc((data->n ? data->n : 1) * sizeof *c.by_name);
    if (!c.by_path || !c.by_name) { free(c.by_path); free(c.by_name); ex_arena_free(c.tmp); ex_base_result_free(out); return -1; }
    for (i = 0; i < data->n; i++) {
        const char *base = base_of(data->files[i].path);
        size_t bl = strlen(base);
        char *key = ex_arena_alloc(c.arena, bl + 1);
        const char *dot = strrchr(base, '.');
        size_t kl = dot && dot != base && !strcmp(dot, ".md") ? (size_t)(dot - base) : bl;
        size_t q;
        for (q = 0; q < kl; q++) key[q] = (char)tolower((unsigned char)base[q]);
        key[kl] = 0;
        c.by_path[i].key = data->files[i].path; c.by_path[i].file = i;
        c.by_name[i].key = key; c.by_name[i].file = i;
    }
    qsort(c.by_path, data->n, sizeof *c.by_path, cmp_pathidx);
    qsort(c.by_name, data->n, sizeof *c.by_name, cmp_pathidx);
    /* formulas */
    fm = ex_y_get(b->root, "formulas");
    c.nformulas = ex_y_count(fm);
    c.formulas = c.nformulas ? calloc(c.nformulas, sizeof *c.formulas) : NULL;
    for (i = 0; i < c.nformulas; i++) {
        const char *text = ex_y_str(ex_y_val(fm, i));
        char err[200];
        int pos = 0;
        c.formulas[i].name = ex_y_key(fm, i);
        c.formulas[i].expr = ex_expr_parse(text ? text : "", err, sizeof err, &pos);
        if (!c.formulas[i].expr) {
            char m[300];
            snprintf(m, sizeof m, "%s (at character %d)", err, pos + 1);
            record_error(&c, c.formulas[i].name, m);
            c.formulas[i].expr = ex_expr_failed(err);
        }
    }
    /* filters: the base's and the view's */
    gfilters = ex_y_get(b->root, "filters"); vfilters = ex_y_get(v, "filters");
    rows = malloc((data->n ? data->n : 1) * sizeof *rows);
    if (!rows) goto fail;
    for (i = 0; i < data->n; i++) {
        set_row(&c, &data->files[i]);
        if (filter_passes(&c, gfilters, 0) && filter_passes(&c, vfilters, 0)) rows[nrows++] = i;
    }
    out->total = nrows;
    out->tz_min = tz_min;
    /* sort */
    sort = ex_y_get(v, "sort");
    if (sort && sort->kind == EXY_SEQ && sort->n && nrows > 1) {
        size_t ns = 0;
        SortSpec *specs = calloc(sort->n, sizeof *specs);
        Key *keys = malloc(nrows * sizeof *keys);
        if (!specs || !keys) { free(specs); free(keys); goto fail; }
        for (j = 0; j < sort->n; j++) {
            const ExYNode *sp = sort->kids[j];
            const char *id = sp && sp->kind == EXY_MAP ? ex_y_str(ex_y_get(sp, "property")) : ex_y_str(sp);
            if (!id || !*id) continue;
            specs[ns].id = id; specs[ns].desc = sp->kind == EXY_MAP && is_desc(sp);
            ns++;
        }
        for (i = 0; i < nrows; i++) {
            keys[i].row = rows[i];
            keys[i].v = ex_arena_alloc(c.tmp, (ns ? ns : 1) * sizeof(ExValue));
            set_row(&c, &data->files[rows[i]]);
            for (j = 0; j < ns; j++) keys[i].v[j] = eval_id(&c, c.tmp, specs[j].id);
        }
        g_sort = specs; g_nsort = ns;
        qsort(keys, nrows, sizeof *keys, sort_cmp);
        for (i = 0; i < nrows; i++) rows[i] = keys[i].row;
        g_sort = NULL; g_nsort = 0;
        free(specs); free(keys);
        ex_arena_reset(c.tmp);
    }
    /* limit */
    { const char *ls = ex_y_str(ex_y_get(v, "limit")); if (ls && *ls) { char *end; long l = strtol(ls, &end, 10); if (!*end && l >= 0) limit = l; } }
    if (limit >= 0 && (size_t)limit < nrows) nrows = (size_t)limit;
    /* columns */
    order = ex_y_get(v, "order");
    {
        size_t nc = order && order->kind == EXY_SEQ ? order->n : 0, used = 0;
        out->cols = ex_arena_alloc(c.arena, (nc ? nc : 1) * sizeof *out->cols);
        if (!out->cols) goto fail;
        for (i = 0; i < nc; i++) {
            const char *id = ex_y_str(order->kids[i]);
            char buf[300];
            if (!id || !*id) continue;
            out->cols[used].id = ex_arena_strdup(c.arena, id);
            out->cols[used].name = ex_arena_strdup(c.arena, ex_base_display_name(b, id, buf, sizeof buf));
            used++;
        }
        if (!used) {                                                                                    /* no columns asked for: the name of the file */
            out->cols[0].id = "file.name"; out->cols[0].name = "File name";
            used = 1;
        }
        out->ncols = used;
    }
    /* groups */
    group = ex_y_get(v, "groupBy");
    forder = ex_y_get(v, "groupOrder");
    out->files = ex_arena_alloc(c.arena, (nrows ? nrows : 1) * sizeof *out->files);
    if (!out->files) goto fail;
    if (group && group->kind == EXY_MAP && ex_y_str(ex_y_get(group, "property")) && nrows) {
        const char *gid = ex_y_str(ex_y_get(group, "property"));
        GKey *gk = malloc(nrows * sizeof *gk);
        size_t ng = 0, *order_rows;
        ExBaseGroup *groups;
        if (!gk) goto fail;
        for (i = 0; i < nrows; i++) { set_row(&c, &data->files[rows[i]]); gk[i].row = i; gk[i].key = eval_id(&c, c.arena, gid); if (gk[i].key.t == EXV_ERROR) gk[i].key = ex_v_null(); }
        gk_desc = is_desc(group);
        qsort(gk, nrows, sizeof *gk, group_cmp);
        groups = ex_arena_alloc(c.arena, (nrows + (forder ? ex_y_count(forder) : 0) + 1) * sizeof *groups);
        order_rows = malloc(nrows * sizeof *order_rows);
        if (!groups || !order_rows) { free(gk); free(order_rows); goto fail; }
        for (i = 0; i < nrows; i++) {
            ExValue key = gk[i].key;
            if (!ng || !ex_value_equal(&c.host, groups[ng - 1].key, key)) {
                groups[ng].key = key;
                groups[ng].label = key.t == EXV_NULL ? "" : ex_value_to_string(c.arena, &c.host, key);
                groups[ng].first = i; groups[ng].count = 0;
                ng++;
            }
            groups[ng - 1].count++;
            order_rows[i] = gk[i].row;
        }
        if (forder && forder->kind == EXY_SEQ) {                                                         /* groupOrder: only these groups, in this order (an empty one still shown) */
            ExBaseGroup *shown = ex_arena_alloc(c.arena, (forder->n + 1) * sizeof *shown);
            size_t ns = 0, *ordered = malloc((nrows ? nrows : 1) * sizeof *ordered), at = 0;
            if (!shown || !ordered) { free(gk); free(order_rows); free(ordered); goto fail; }
            for (j = 0; j < forder->n; j++) {
                const ExYNode *want = forder->kids[j];
                int is_null = ex_y_is_null(want);
                const char *wt = ex_y_str(want);
                int found = 0;
                for (k = 0; k < ng && !found; k++) {
                    int match = is_null ? groups[k].key.t == EXV_NULL : (groups[k].key.t != EXV_NULL && wt && !strcmp(groups[k].label, wt));
                    if (match) {
                        size_t q;
                        shown[ns] = groups[k]; shown[ns].first = at;
                        for (q = 0; q < groups[k].count; q++) ordered[at++] = order_rows[groups[k].first + q];
                        ns++; found = 1;
                    }
                }
                if (!found) { shown[ns].key = is_null ? ex_v_null() : ex_v_str(c.arena, wt ? wt : ""); shown[ns].label = is_null ? "" : ex_arena_strdup(c.arena, wt ? wt : ""); shown[ns].first = at; shown[ns].count = 0; ns++; }
            }
            memcpy(order_rows, ordered, at * sizeof *ordered);
            free(ordered);
            groups = shown; ng = ns; nrows = at;
        }
        { size_t *nr = ex_arena_alloc(c.arena, (nrows ? nrows : 1) * sizeof *nr); if (nr) { for (i = 0; i < nrows; i++) nr[i] = rows[order_rows[i]]; memcpy(rows, nr, nrows * sizeof *rows); } }
        out->groups = groups; out->ngroups = ng;
        free(gk); free(order_rows);
    }
    out->nrows = nrows;
    for (i = 0; i < nrows; i++) out->files[i] = &data->files[rows[i]];
    out->paths = ex_arena_alloc(c.arena, (nrows ? nrows : 1) * sizeof *out->paths);
    if (!out->paths) goto fail;
    for (i = 0; i < nrows; i++) { out->paths[i] = ex_arena_strdup(c.arena, out->files[i]->path); if (!out->paths[i]) goto fail; }
    /* cells */
    out->cells = ex_arena_alloc(c.arena, (nrows * out->ncols > 0 ? nrows * out->ncols : 1) * sizeof *out->cells);
    if (!out->cells) goto fail;
    for (i = 0; i < nrows; i++) {
        set_row(&c, out->files[i]);
        for (j = 0; j < out->ncols; j++) out->cells[i * out->ncols + j] = eval_id(&c, c.arena, out->cols[j].id);
    }
    /* summaries */
    summ = ex_y_get(v, "summaries");
    csum = ex_y_get(b->root, "summaries");
    for (j = 0; j < out->ncols; j++) {
        const char *sn = summ && summ->kind == EXY_MAP ? ex_y_str(ex_y_get(summ, out->cols[j].id)) : NULL;
        ExValue *vals;
        const ExYNode *custom;
        if (!sn || !*sn) { out->cols[j].summary = ex_v_null(); continue; }
        out->cols[j].summary_name = ex_arena_strdup(c.arena, sn);
        vals = ex_arena_alloc(c.arena, (nrows ? nrows : 1) * sizeof *vals);
        if (!vals) goto fail;
        for (i = 0; i < nrows; i++) vals[i] = out->cells[i * out->ncols + j];
        custom = csum ? ex_y_get(csum, sn) : NULL;
        if (custom && ex_y_str(custom)) {
            ExValue list = ex_v_list(c.arena, vals, nrows), r;
            const ExExpr *e = expr_of(&c, ex_y_str(custom), 1);
            set_row(&c, NULL);
            r = e ? ex_expr_eval(e, c.arena, &c.host, &list) : ex_v_null();
            if (r.t == EXV_ERROR) record_error(&c, sn, r.u.str);
            out->cols[j].summary = r;
        } else {
            out->cols[j].summary = summarize(&c, sn, vals, nrows, c.arena);
            if (out->cols[j].summary.t == EXV_ERROR) record_error(&c, sn, out->cols[j].summary.u.str);
        }
    }
    out->errors = (const char **)c.errs; out->nerrors = c.nerrs;
    /* tidy */
    for (i = 0; i < c.nformulas; i++) ex_expr_free(c.formulas[i].expr);
    for (i = 0; i < c.ncache; i++) ex_expr_free(c.cache[i].expr);
    free(c.formulas); free(c.cache); free(c.by_path); free(c.by_name); free(rows);
    if (c.bl_files) { for (i = 0; i < data->n; i++) free(c.bl_files[i]); }
    free(c.bl_files); free(c.bl_count);
    ex_arena_free(c.tmp);
    return 0;
fail:
    for (i = 0; i < c.nformulas; i++) ex_expr_free(c.formulas[i].expr);
    for (i = 0; i < c.ncache; i++) ex_expr_free(c.cache[i].expr);
    free(c.formulas); free(c.cache); free(c.by_path); free(c.by_name); free(rows);
    if (c.bl_files) { for (i = 0; i < data->n; i++) free(c.bl_files[i]); }
    free(c.bl_files); free(c.bl_count);
    ex_arena_free(c.tmp);
    ex_base_result_free(out);
    return -1;
}

void ex_base_result_free(ExBaseResult *r) { if (!r) return; ex_arena_free(r->arena); memset(r, 0, sizeof *r); }

const char *ex_base_cell_text(const ExBaseResult *r, size_t row, size_t col) {
    ExBaseResult *m = (ExBaseResult *)(void *)(size_t)(const void *)r;       /* the texts are a cache */
    ExExprHost h;
    if (row >= r->nrows || col >= r->ncols) return "";
    if (!m->texts) m->texts = ex_arena_alloc(m->arena, (r->nrows * r->ncols) * sizeof *m->texts);
    if (!m->texts) return "";
    if (!m->texts[row * r->ncols + col]) {
        memset(&h, 0, sizeof h);
        h.tz_min = r->tz_min;
        m->texts[row * r->ncols + col] = ex_value_to_string(r->arena, &h, r->cells[row * r->ncols + col]);
    }
    return m->texts[row * r->ncols + col];
}

/* ---- export ---- */

typedef struct Sb { char *p; size_t n, cap; } Sb;
static void sb_put(Sb *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) { size_t nc = s->cap ? s->cap * 2 : 256; char *np; while (nc < s->n + n + 1) nc *= 2; np = realloc(s->p, nc); if (!np) return; s->p = np; s->cap = nc; }
    memcpy(s->p + s->n, t, n); s->n += n; s->p[s->n] = 0;
}
static void sb_str(Sb *s, const char *t) { sb_put(s, t, strlen(t)); }

static void csv_field(Sb *s, const char *t) {
    int quote = 0;
    const char *p;
    for (p = t; *p; p++) if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { quote = 1; break; }
    if (!quote) { sb_str(s, t); return; }
    sb_str(s, "\"");
    for (p = t; *p; p++) { if (*p == '"') sb_str(s, "\"\""); else sb_put(s, p, 1); }
    sb_str(s, "\"");
}

char *ex_base_result_csv(const ExBaseResult *r, size_t *len) {
    Sb s = {0};
    size_t i, j;
    for (j = 0; j < r->ncols; j++) { if (j) sb_str(&s, ","); csv_field(&s, r->cols[j].name); }
    sb_str(&s, "\r\n");
    for (i = 0; i < r->nrows; i++) {
        for (j = 0; j < r->ncols; j++) { if (j) sb_str(&s, ","); csv_field(&s, ex_base_cell_text(r, i, j)); }
        sb_str(&s, "\r\n");
    }
    if (!s.p) s.p = dups("");
    if (len) *len = s.n;
    return s.p;
}

static void md_cell(Sb *s, const char *t) {
    const char *p;
    for (p = t; *p; p++) {
        if (*p == '|') sb_str(s, "\\|");
        else if (*p == '\n') sb_str(s, "<br>");
        else if (*p == '\r') {}
        else sb_put(s, p, 1);
    }
}

static void md_table(Sb *s, const ExBaseResult *r, size_t from, size_t count) {
    size_t i, j;
    sb_str(s, "|");
    for (j = 0; j < r->ncols; j++) { sb_str(s, " "); md_cell(s, r->cols[j].name); sb_str(s, " |"); }
    sb_str(s, "\n|");
    for (j = 0; j < r->ncols; j++) sb_str(s, " --- |");
    sb_str(s, "\n");
    for (i = from; i < from + count; i++) {
        sb_str(s, "|");
        for (j = 0; j < r->ncols; j++) { sb_str(s, " "); md_cell(s, ex_base_cell_text(r, i, j)); sb_str(s, " |"); }
        sb_str(s, "\n");
    }
}

char *ex_base_result_markdown(const ExBaseResult *r, size_t *len) {
    Sb s = {0};
    size_t g;
    if (r->ngroups) {
        for (g = 0; g < r->ngroups; g++) {
            if (g) sb_str(&s, "\n");
            sb_str(&s, "**"); md_cell(&s, r->groups[g].label[0] ? r->groups[g].label : "(none)"); sb_str(&s, "**\n\n");
            md_table(&s, r, r->groups[g].first, r->groups[g].count);
        }
    } else md_table(&s, r, 0, r->nrows);
    if (!s.p) s.p = dups("");
    if (len) *len = s.n;
    return s.p;
}
