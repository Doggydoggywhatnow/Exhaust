#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_markdown.h"
#include "exhaust_nfc.h"
#include "exhaust_platform.h"
#include "exhaust_query.h"
#include "store_internal.h"

/* ---- query blocks ---- */

typedef struct Cur { size_t start, end, body_s, body_e; int is_query, have; } Cur;
typedef struct Blocks { ExQueryBlock *v; size_t n, cap; const char *t; Cur cur; } Blocks;

static int flush_block(Blocks *b) {
    if (b->cur.have && b->cur.is_query) {
        size_t i, o = 0, s = b->cur.body_s, e = b->cur.body_e;
        char *q;
        ExQueryBlock *blk;
        if (b->n == b->cap) { size_t nc = b->cap ? b->cap * 2 : 4; ExQueryBlock *nv = realloc(b->v, nc * sizeof *nv); if (!nv) return -1; b->v = nv; b->cap = nc; }
        q = malloc(e - s + 1);
        if (!q) return -1;
        for (i = s; i < e; i++) { char c = b->t[i]; q[o++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c; }
        q[o] = 0;
        { char *a = q; size_t l; while (*a == ' ') a++; l = strlen(a); while (l && a[l - 1] == ' ') a[--l] = 0; if (a != q) memmove(q, a, strlen(a) + 1); }
        blk = &b->v[b->n++];
        blk->start = b->cur.start; blk->end = b->cur.end; blk->query = q;
    }
    memset(&b->cur, 0, sizeof b->cur);
    return 0;
}

static int block_visit(const ExMdSpan *sp, void *user) {
    Blocks *b = user;
    if (sp->kind == EX_MD_CODE_BLOCK && sp->flags == 0) {
        if (flush_block(b) != 0) return 1;
        b->cur.have = 1; b->cur.start = sp->start; b->cur.end = sp->end;
    } else if (sp->kind == EX_MD_CODE_INFO && b->cur.have && sp->parent_start == b->cur.start) {
        size_t s = sp->start, e = sp->end;
        while (s < e && (b->t[s] == ' ' || b->t[s] == '\t')) s++;
        while (e > s && (b->t[e - 1] == ' ' || b->t[e - 1] == '\t' || b->t[e - 1] == '\r')) e--;
        b->cur.is_query = e - s == 5 && !strncmp(b->t + s, "query", 5);
    } else if (sp->kind == EX_MD_CODE_BODY && b->cur.have && sp->parent_start == b->cur.start) {
        b->cur.body_s = sp->start; b->cur.body_e = sp->end;
    }
    return 0;
}

int ex_query_blocks(const char *text, size_t len, ExQueryBlock **out, size_t *n) {
    Blocks b;
    ExMd *md = ex_md_create();
    memset(&b, 0, sizeof b);
    b.t = text;
    *out = NULL; *n = 0;
    if (!md) return -1;
    ex_md_tokenize(md, text, len);
    ex_md_visit(md, 0, len, block_visit, &b);
    ex_md_free(md);
    if (flush_block(&b) != 0) { ex_query_blocks_free(b.v, b.n); return -1; }
    *out = b.v; *n = b.n;
    return 0;
}

void ex_query_blocks_free(ExQueryBlock *b, size_t n) { size_t i; for (i = 0; i < n; i++) free(b[i].query); free(b); }

/* ---- results ---- */

typedef struct Out { char *v; size_t n, cap; } Out;
static void put(Out *o, const char *s, size_t n) {
    if (o->n + n + 1 > o->cap) { size_t nc = (o->n + n + 1) * 2; char *nv = realloc(o->v, nc); if (!nv) return; o->v = nv; o->cap = nc; }
    memcpy(o->v + o->n, s, n);
    o->n += n;
    o->v[o->n] = 0;
}
static void puts_(Out *o, const char *s) { put(o, s, strlen(s)); }

static void link_text(Out *o, const char *path) {
    size_t n = strlen(path);
    const char *slash = strrchr(path, '/'), *base = slash ? slash + 1 : path;
    size_t bn = strlen(base);
    int md = n > 3 && !strcmp(path + n - 3, ".md");
    if (md) { n -= 3; bn -= 3; }
    puts_(o, "[[");
    put(o, path, n);
    if (slash) { puts_(o, "|"); put(o, base, bn); }
    puts_(o, "]]");
}

static char *render(const ExSearchResult *r, size_t n, const char *root, const ExResultView *view, int markdown) {
    Out o = {0};
    size_t i, k, max = view && view->max_matches ? view->max_matches : 5;
    int collapse = view && view->collapse, more = view && view->more_context;
    puts_(&o, "");
    for (i = 0; i < n; i++) {
        char *text = NULL;
        size_t len = 0;
        char count[48];
        if (markdown) puts_(&o, "- "); 
        link_text(&o, r[i].path);
        if (r[i].total_matches) { snprintf(count, sizeof count, " (%zu)", r[i].total_matches); puts_(&o, count); }
        puts_(&o, "\n");
        if (collapse || !r[i].nmatches) continue;
        text = ex_index_read_note(root, r[i].path, &len);
        if (!text) continue;
        for (k = 0; k < r[i].nmatches && k < max; k++) {
            size_t off = 0, ml = r[i].matches[k].end - r[i].matches[k].start;
            char *ctx = ex_search_context(text, len, &r[i].matches[k], more ? 1 : 0, &off), *line;
            if (!ctx) continue;
            for (line = ctx; line; ) {
                char *nl = strchr(line, '\n');
                size_t ll = nl ? (size_t)(nl - line) : strlen(line), ls = (size_t)(line - ctx);
                puts_(&o, markdown ? "  > " : "    ");
                if (off >= ls && off <= ls + ll && markdown) {          /* the line that holds the match: mark it */
                    size_t in = off - ls, mlen = ml < ll - in ? ml : ll - in;
                    put(&o, line, in);
                    if (mlen) { puts_(&o, "=="); put(&o, line + in, mlen); puts_(&o, "=="); }
                    put(&o, line + in + mlen, ll - in - mlen);
                } else put(&o, line, ll);
                puts_(&o, "\n");
                line = nl ? nl + 1 : NULL;
            }
            free(ctx);
        }
        if (r[i].nmatches > max || r[i].total_matches > r[i].nmatches) {
            char more_s[64];
            size_t rest = r[i].total_matches - (r[i].nmatches < max ? r[i].nmatches : max);
            snprintf(more_s, sizeof more_s, "%s and %zu more\n", markdown ? "  >" : "   ", rest);
            puts_(&o, more_s);
        }
        free(text);
    }
    if (!o.v) o.v = ex_strdup("");
    return o.v;
}

char *ex_results_markdown(const ExSearchResult *r, size_t n, const char *root, const ExResultView *view) { return render(r, n, root, view, 1); }
char *ex_results_copy_text(const ExSearchResult *r, size_t n, const char *root, const ExResultView *view) { return render(r, n, root, view, 0); }

char *ex_query_expand(ExIndex *ix, const char *root, const char *text, size_t len, const ExSearchOptions *opt, const ExResultView *view) {
    ExQueryBlock *blocks;
    size_t nb, i, at = 0;
    Out o = {0};
    if (ex_query_blocks(text, len, &blocks, &nb) != 0) return NULL;
    for (i = 0; i < nb; i++) {
        char err[220] = "";
        ExSearch *q;
        ExSearchOptions so;
        ExSearchResult *res = NULL;
        size_t n = 0;
        char *md = NULL;
        put(&o, text + at, blocks[i].start - at);
        memset(&so, 0, sizeof so);
        if (opt) so = *opt;
        q = ex_search_parse(blocks[i].query, err, sizeof err);
        if (q && ex_search_run(ix, root, q, &so, &res, &n, NULL) == 0) {
            md = ex_results_markdown(res, n, root, view);
            if (n == 0) { free(md); md = ex_strdup("*No results.*\n"); }
        } else {
            char msg[300];
            snprintf(msg, sizeof msg, "*Query error: %s*\n", err[0] ? err : "the search failed");
            md = ex_strdup(msg);
        }
        if (md) put(&o, md, strlen(md));
        free(md);
        ex_search_results_free(res, n);
        ex_search_free(q);
        at = blocks[i].end;
        if (at < len && text[at] == '\n') at++;
    }
    put(&o, text + at, len - at);
    ex_query_blocks_free(blocks, nb);
    return o.v ? o.v : ex_strdup("");
}

/* ---- saved queries ---- */

struct ExSavedQueries { char *path; ExSavedQuery *v; size_t n; ExIni *ini; };

/* INI values lose " #" (a comment) and line breaks, so those characters (and "%") are written as %23, %0A and %25. */
static char *enc(const char *s) {
    size_t n = strlen(s), i, o = 0;
    char *out = malloc(n * 3 + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '%' || s[i] == '#' || s[i] == '\n' || s[i] == '\r') o += (size_t)sprintf(out + o, "%%%02X", (unsigned char)s[i]);
        else out[o++] = s[i];
    }
    out[o] = 0;
    return out;
}

static char *dec(const char *s) {
    size_t n = strlen(s), i, o = 0;
    char *out = malloc(n + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (s[i] == '%' && i + 2 < n + 1 && sscanf(s + i + 1, "%2x", &v) == 1) { out[o++] = (char)v; i += 2; }
        else out[o++] = s[i];
    }
    out[o] = 0;
    return out;
}

static const char *const SORTS[] = {"path_az", "path_za", "modified_new", "modified_old", "matches_most", "matches_least"};

static void query_free(ExSavedQuery *q) { free(q->name); free(q->text); }

ExSavedQueries *ex_saved_queries_load(const char *root) {
    ExSavedQueries *q = calloc(1, sizeof *q);
    char *text;
    size_t len = 0, i, k;
    if (!q) return NULL;
    q->path = malloc(strlen(root) + 32);
    if (!q->path) { free(q); return NULL; }
    sprintf(q->path, "%s/.exhaust/queries.ini", root);
    text = ex_platform_read_file(q->path, &len);
    q->ini = text ? ex_ini_parse(text, len) : ex_ini_new();
    free(text);
    if (!q->ini) { free(q->path); free(q); return NULL; }
    for (i = 0; i < ex_ini_section_count(q->ini); i++) {
        const char *sec = ex_ini_section_name(q->ini, i), *name = NULL, *qt = NULL, *sort;
        ExSavedQuery *e;
        ExSavedQuery *nv;
        if (strncmp(sec, "query.", 6)) continue;
        name = ex_ini_get(q->ini, sec, "name"); qt = ex_ini_get(q->ini, sec, "text");
        if (!name || !qt) continue;
        nv = realloc(q->v, (q->n + 1) * sizeof *nv);
        if (!nv) break;
        q->v = nv;
        e = &q->v[q->n++];
        memset(e, 0, sizeof *e);
        e->name = dec(name); e->text = dec(qt);
        sort = ex_ini_get(q->ini, sec, "sort");
        for (k = 0; sort && k < sizeof SORTS / sizeof SORTS[0]; k++) if (!strcmp(sort, SORTS[k])) e->sort = (int)k;
        e->collapse = ex_ini_get_bool(q->ini, sec, "collapse", 0);
        e->more_context = ex_ini_get_bool(q->ini, sec, "more_context", 0);
        e->match_case = ex_ini_get_bool(q->ini, sec, "match_case", 0);
    }
    return q;
}

void ex_saved_queries_free(ExSavedQueries *q) {
    size_t i;
    if (!q) return;
    for (i = 0; i < q->n; i++) query_free(&q->v[i]);
    free(q->v); ex_ini_free(q->ini); free(q->path); free(q);
}

size_t ex_saved_queries_count(const ExSavedQueries *q) { return q->n; }
const ExSavedQuery *ex_saved_queries_get(const ExSavedQueries *q, size_t i) { return i < q->n ? &q->v[i] : NULL; }

static int same_name(const char *a, const char *b) {
    char *x = ex_name_key(a, strlen(a)), *y = ex_name_key(b, strlen(b));
    int eq = x && y && !strcmp(x, y);
    free(x); free(y);
    return eq;
}

const ExSavedQuery *ex_saved_queries_find(const ExSavedQueries *q, const char *name) {
    size_t i;
    for (i = 0; i < q->n; i++) if (same_name(q->v[i].name, name)) return &q->v[i];
    return NULL;
}

int ex_saved_queries_put(ExSavedQueries *q, const ExSavedQuery *in) {
    size_t i;
    ExSavedQuery *e = NULL;
    if (!in->name || !in->name[0] || !in->text) return -1;
    for (i = 0; i < q->n; i++) if (same_name(q->v[i].name, in->name)) { e = &q->v[i]; query_free(e); break; }
    if (!e) {
        ExSavedQuery *nv = realloc(q->v, (q->n + 1) * sizeof *nv);
        if (!nv) return -1;
        q->v = nv;
        e = &q->v[q->n++];
    }
    *e = *in;
    e->name = ex_strdup(in->name); e->text = ex_strdup(in->text);
    return e->name && e->text ? 0 : -1;
}

int ex_saved_queries_remove(ExSavedQueries *q, const char *name) {
    size_t i;
    for (i = 0; i < q->n; i++) if (same_name(q->v[i].name, name)) {
        query_free(&q->v[i]);
        memmove(&q->v[i], &q->v[i + 1], (q->n - i - 1) * sizeof *q->v);
        q->n--;
        return 0;
    }
    return -1;
}

int ex_saved_queries_save(const ExSavedQueries *q) {
    ExIni *out = ex_ini_new();
    size_t i, k, len = 0;
    char sec[32], *text, *dir, *slash;
    int rc;
    if (!out) return -1;
    for (i = 0; i < ex_ini_section_count(q->ini); i++) {          /* what this version does not know */
        const char *name = ex_ini_section_name(q->ini, i);
        if (!strncmp(name, "query.", 6)) continue;
        for (k = 0; k < ex_ini_key_count(q->ini, name); k++) ex_ini_set(out, name, ex_ini_key_name(q->ini, name, k), ex_ini_get(q->ini, name, ex_ini_key_name(q->ini, name, k)));
    }
    for (i = 0; i < q->n; i++) {
        const ExSavedQuery *e = &q->v[i];
        snprintf(sec, sizeof sec, "query.%zu", i + 1);
        { char *en = enc(e->name), *et = enc(e->text); if (en) ex_ini_set(out, sec, "name", en); if (et) ex_ini_set(out, sec, "text", et); free(en); free(et); }
        ex_ini_set(out, sec, "sort", SORTS[e->sort >= 0 && e->sort < 6 ? e->sort : 0]);
        if (e->collapse) ex_ini_set(out, sec, "collapse", "true");
        if (e->more_context) ex_ini_set(out, sec, "more_context", "true");
        if (e->match_case) ex_ini_set(out, sec, "match_case", "true");
    }
    text = ex_ini_write(out, &len);
    ex_ini_free(out);
    if (!text) return -1;
    dir = ex_strdup(q->path);
    slash = dir ? strrchr(dir, '/') : NULL;
    if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    free(dir);
    rc = ex_platform_save_file(q->path, text, len, NULL);
    free(text);
    return rc;
}
