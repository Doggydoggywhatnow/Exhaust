/* The search engine: a parser for the query language, the candidate sets from the index and the evaluation on the notes. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_find.h"
#include "exhaust_nfc.h"
#include "exhaust_search.h"
#include "store_internal.h"

typedef enum Kind { N_AND, N_OR, N_NOT, N_TERM, N_SCOPE, N_FILE, N_PATH, N_TAG, N_PROP } Kind;
typedef enum Scope { SC_LINE, SC_BLOCK, SC_SECTION, SC_TASK, SC_TASK_TODO, SC_TASK_DONE } Scope;
typedef enum PropOp { P_EXISTS, P_EQ, P_NULL, P_GT, P_GE, P_LT, P_LE } PropOp;

typedef struct Node Node;
typedef struct Match { size_t start, end; } Match;
struct Node {
    Kind kind;
    Node **kids; size_t nk, capk;
    /* terms */
    char *text; int regex, case_mode;            /* case_mode: 0 default, 1 match-case, 2 ignore-case */
    int any;                                      /* a plain top-level term: the name too */
    ExFind *find;
    Match *cache; size_t ncache; unsigned long cache_serial; int cache_over;
    Scope scope;
    int plain_ascii, match_case;                   /* a plain ASCII term is looked for by a scan of its own */
    /* properties */
    char *pname, *pkey, *pvalue, *pvkey; int pop;
};

struct ExSearch { Node *root; };

static Node *node_new(Kind k) { Node *n = calloc(1, sizeof *n); if (n) n->kind = k; return n; }

static void node_free(Node *n) {
    size_t i;
    if (!n) return;
    for (i = 0; i < n->nk; i++) node_free(n->kids[i]);
    free(n->kids); free(n->text); ex_find_free(n->find); free(n->cache); free(n->pname); free(n->pkey); free(n->pvalue); free(n->pvkey);
    free(n);
}

static int node_add(Node *p, Node *k) {
    if (!k) return -1;
    if (p->nk == p->capk) { size_t nc = p->capk ? p->capk * 2 : 4; Node **nv = realloc(p->kids, nc * sizeof *nv); if (!nv) return -1; p->kids = nv; p->capk = nc; }
    p->kids[p->nk++] = k;
    return 0;
}

void ex_search_free(ExSearch *q) { if (q) { node_free(q->root); free(q); } }

/* ---- the parser ---- */

typedef struct P { const char *s; size_t i, n; int depth; char *err; size_t errcap; int failed; } P;

static void fail(P *p, const char *msg) { if (!p->failed && p->err && p->errcap) snprintf(p->err, p->errcap, "%s", msg); p->failed = 1; }
static void skip_ws(P *p) { while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n')) p->i++; }
static char *dupn(const char *s, size_t n) { char *c = malloc(n + 1); if (c) { memcpy(c, s, n); c[n] = 0; } return c; }

static Node *parse_or(P *p);

static Node *term_node(char *text, int regex) {
    Node *n = node_new(N_TERM);
    if (!n) { free(text); return NULL; }
    n->text = text; n->regex = regex; n->any = 1;
    return n;
}


/* One argument: a "phrase", a /regex/, a (group) or a word. */
static Node *parse_arg(P *p) {
    if (p->i >= p->n) { fail(p, "an operator needs something to look for"); return NULL; }
    if (p->s[p->i] == '(') {
        Node *g;
        p->i++; p->depth++;
        g = parse_or(p);
        skip_ws(p);
        if (p->i >= p->n || p->s[p->i] != ')') { fail(p, "a parenthesis is not closed"); node_free(g); return NULL; }
        p->i++; p->depth--;
        return g;
    }
    if (p->s[p->i] == '"') {
        size_t a = ++p->i;
        while (p->i < p->n && p->s[p->i] != '"') p->i++;
        if (p->i >= p->n) { fail(p, "a quotation mark is not closed"); return NULL; }
        { Node *t = term_node(dupn(p->s + a, p->i - a), 0); p->i++; return t; }
    }
    if (p->s[p->i] == '/') {
        size_t a = p->i + 1, e = a;
        while (e < p->n && p->s[e] != '/') { if (p->s[e] == '\\' && e + 1 < p->n) e++; e++; }
        if (e < p->n && e > a) { p->i = e + 1; return term_node(dupn(p->s + a, e - a), 1); }
    }
    {
        size_t a = p->i;
        while (p->i < p->n && !(p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || (p->depth > 0 && p->s[p->i] == ')'))) p->i++;
        if (p->i == a) { fail(p, "something is missing after the operator"); return NULL; }
        return term_node(dupn(p->s + a, p->i - a), 0);
    }
}

static void map_terms(Node *n, void (*fn)(Node *, void *), void *arg) {
    size_t i;
    if (n->kind == N_TERM) fn(n, arg);
    for (i = 0; i < n->nk; i++) map_terms(n->kids[i], fn, arg);
}
static void set_case(Node *n, void *arg) { n->case_mode = *(int *)arg; }
static void clear_any(Node *n, void *arg) { (void)arg; n->any = 0; }
static void to_kind(Node *n, void *arg) {
    Kind k = *(Kind *)arg;
    n->kind = k;
    n->any = 0;
    if (k == N_TAG) { char *t = n->text; while (*t == '#') t++; if (t != n->text) memmove(n->text, t, strlen(t) + 1); }
}

static Node *parse_prop(P *p) {
    size_t a = ++p->i, e;
    int quote = 0;
    char *body, *colon;
    Node *res = NULL;
    for (e = a; e < p->n; e++) {
        if (p->s[e] == '"') quote = !quote;
        else if (p->s[e] == ']' && !quote) break;
    }
    if (e >= p->n) { fail(p, "a bracket is not closed"); return NULL; }
    body = dupn(p->s + a, e - a);
    p->i = e + 1;
    if (!body) return NULL;
    colon = strchr(body, ':');
    {
        char *name = colon ? dupn(body, (size_t)(colon - body)) : dupn(body, strlen(body));
        char *val = colon ? colon + 1 : NULL;
        size_t nl;
        if (!name) { free(body); return NULL; }
        nl = strlen(name);
        while (nl && name[nl - 1] == ' ') name[--nl] = 0;
        { char *t = name; while (*t == ' ') t++; if (t != name) memmove(name, t, strlen(t) + 1); }
        if (!name[0]) { fail(p, "a property filter needs a name"); free(name); free(body); return NULL; }
        if (!val) {
            res = node_new(N_PROP);
            if (res) { res->pname = name; res->pop = P_EXISTS; res->pkey = ex_name_key(name, strlen(name)); }
            else free(name);
        } else {
            /* alternatives: value OR value */
            Node *orn = node_new(N_OR);
            char *cur = val;
            while (orn) {
                char *sep = NULL, *q;
                int inq = 0;
                for (q = cur; *q; q++) { if (*q == '"') inq = !inq; else if (!inq && !strncmp(q, " OR ", 4)) { sep = q; break; } }
                {
                    size_t vl = sep ? (size_t)(sep - cur) : strlen(cur);
                    char *v = dupn(cur, vl), *t;
                    Node *pn = node_new(N_PROP);
                    if (!v || !pn) { free(v); node_free(pn); node_free(orn); orn = NULL; break; }
                    t = v; while (*t == ' ') t++;
                    vl = strlen(t); while (vl && t[vl - 1] == ' ') t[--vl] = 0;
                    pn->pname = dupn(name, strlen(name)); pn->pkey = ex_name_key(name, strlen(name));
                    pn->pop = P_EQ;
                    if (!strncmp(t, ">=", 2)) { pn->pop = P_GE; t += 2; } else if (!strncmp(t, "<=", 2)) { pn->pop = P_LE; t += 2; }
                    else if (*t == '>') { pn->pop = P_GT; t++; } else if (*t == '<') { pn->pop = P_LT; t++; }
                    while (*t == ' ') t++;
                    vl = strlen(t);
                    if (vl >= 2 && t[0] == '"' && t[vl - 1] == '"') { t[vl - 1] = 0; t++; }
                    else if (pn->pop == P_EQ && !strcmp(t, "null")) pn->pop = P_NULL;
                    pn->pvalue = dupn(t, strlen(t)); pn->pvkey = ex_name_key(t, strlen(t));
                    free(v);
                    node_add(orn, pn);
                }
                if (!sep) break;
                cur = sep + 4;
            }
            free(name);
            if (orn && orn->nk == 1) { res = orn->kids[0]; orn->nk = 0; node_free(orn); } else res = orn;
        }
    }
    free(body);
    return res;
}

static Node *parse_factor(P *p) {
    int neg = 0;
    Node *n = NULL;
    skip_ws(p);
    if (p->i >= p->n) return NULL;
    if (p->s[p->i] == '-' && p->i + 1 < p->n && p->s[p->i + 1] != ' ') { neg = 1; p->i++; }
    if (p->s[p->i] == '(' || p->s[p->i] == '"' || p->s[p->i] == '[' || p->s[p->i] == '/') {
        if (p->s[p->i] == '[') n = parse_prop(p);
        else {
            n = parse_arg(p);
            if (n && n->kind == N_TERM) n->any = 1;
        }
    } else {
        /* an operator: letters and dashes before a colon */
        size_t a = p->i, e = a;
        while (e < p->n && (isalpha((unsigned char)p->s[e]) || p->s[e] == '-')) e++;
        if (e < p->n && p->s[e] == ':' && e > a) {
            char op[16];
            size_t ol = e - a;
            memset(op, 0, sizeof op);
            if (ol < sizeof op) memcpy(op, p->s + a, ol);
            if (!strcmp(op, "file") || !strcmp(op, "path") || !strcmp(op, "content") || !strcmp(op, "match-case") || !strcmp(op, "ignore-case") || !strcmp(op, "tag") ||
                !strcmp(op, "line") || !strcmp(op, "block") || !strcmp(op, "section") || !strcmp(op, "task") || !strcmp(op, "task-todo") || !strcmp(op, "task-done")) {
                Node *arg;
                p->i = e + 1;
                arg = parse_arg(p);
                if (!arg) return NULL;
                if (!strcmp(op, "match-case") || !strcmp(op, "ignore-case")) { int m = op[0] == 'm' ? 1 : 2; map_terms(arg, set_case, &m); n = arg; }
                else if (!strcmp(op, "content")) { map_terms(arg, clear_any, NULL); n = arg; }
                else if (!strcmp(op, "file") || !strcmp(op, "path") || !strcmp(op, "tag")) {
                    Kind k = op[0] == 'f' ? N_FILE : op[0] == 'p' ? N_PATH : N_TAG;
                    map_terms(arg, to_kind, &k);
                    n = arg;
                } else {
                    Node *sc = node_new(N_SCOPE);
                    if (!sc) { node_free(arg); return NULL; }
                    map_terms(arg, clear_any, NULL);
                    sc->scope = !strcmp(op, "line") ? SC_LINE : !strcmp(op, "block") ? SC_BLOCK : !strcmp(op, "section") ? SC_SECTION : !strcmp(op, "task") ? SC_TASK : !strcmp(op, "task-todo") ? SC_TASK_TODO : SC_TASK_DONE;
                    node_add(sc, arg);
                    n = sc;
                }
            }
        }
        if (!n) {
            size_t a2 = p->i;
            while (p->i < p->n && !(p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || (p->depth > 0 && p->s[p->i] == ')'))) p->i++;
            if (p->i == a2) return NULL;
            n = term_node(dupn(p->s + a2, p->i - a2), 0);
        }
    }
    if (n && neg) {
        Node *nn = node_new(N_NOT);
        if (!nn) { node_free(n); return NULL; }
        node_add(nn, n);
        n = nn;
    }
    return n;
}

static int is_or_word(P *p) {
    size_t i = p->i;
    return i + 2 <= p->n && p->s[i] == 'O' && p->s[i + 1] == 'R' && (i + 2 == p->n || p->s[i + 2] == ' ' || p->s[i + 2] == '(' ) ;
}

static Node *parse_and(P *p) {
    Node *and = node_new(N_AND);
    if (!and) return NULL;
    for (;;) {
        Node *f;
        skip_ws(p);
        if (p->failed || p->i >= p->n || p->s[p->i] == ')' || is_or_word(p)) break;
        f = parse_factor(p);
        if (!f) { if (!p->failed && p->i < p->n && p->s[p->i] != ')') fail(p, "cannot read the search"); break; }
        node_add(and, f);
    }
    if (and->nk == 1) { Node *k = and->kids[0]; and->nk = 0; node_free(and); return k; }
    return and;
}

static Node *parse_or(P *p) {
    Node *first = parse_and(p), *orn = NULL;
    for (;;) {
        skip_ws(p);
        if (p->failed || !is_or_word(p)) break;
        p->i += 2;
        if (!orn) { orn = node_new(N_OR); if (!orn) break; node_add(orn, first); first = NULL; }
        node_add(orn, parse_and(p));
    }
    return orn ? orn : first;
}

ExSearch *ex_search_parse(const char *query, char *err, size_t errcap) {
    P p;
    Node *root;
    ExSearch *q;
    memset(&p, 0, sizeof p);
    p.s = query; p.n = strlen(query); p.err = err; p.errcap = errcap;
    if (err && errcap) err[0] = 0;
    root = parse_or(&p);
    skip_ws(&p);
    if (!p.failed && p.i < p.n) fail(&p, "a closing parenthesis has no opening one");
    if (p.failed || !root || (root->kind == N_AND && root->nk == 0)) {
        if (!p.failed && err && errcap) snprintf(err, errcap, "the search is empty");
        node_free(root);
        return NULL;
    }
    q = calloc(1, sizeof *q);
    if (!q) { node_free(root); return NULL; }
    q->root = root;
    return q;
}

/* ---- evaluation ---- */

typedef struct MSet { Match *v; size_t n, cap; } MSet;
static void ms_add(MSet *m, size_t s, size_t e) {
    if (m->n == m->cap) { size_t nc = m->cap ? m->cap * 2 : 32; Match *nv = realloc(m->v, nc * sizeof *nv); if (!nv) return; m->v = nv; m->cap = nc; }
    m->v[m->n].start = s; m->v[m->n].end = e; m->n++;
}

typedef struct Task { size_t s, e; int state; } Task;

typedef struct Ctx {
    ExIndex *r;
    sqlite3_stmt *tag_st, *prop_st, *sec_st, *task_st;
    int64_t id;
    const char *path, *name;
    const char *text; size_t len;
    unsigned long serial;
    int name_match;
    size_t *lines; size_t nlines;           /* line starts */
    size_t *secs; size_t nsecs; int have_secs;
    size_t *blocks; size_t nblocks;
    Task *tasks; size_t ntasks; int have_tasks;
} Ctx;

static void ctx_reset(Ctx *c) {
    free(c->lines); free(c->secs); free(c->blocks); free(c->tasks);
    c->lines = NULL; c->secs = NULL; c->blocks = NULL; c->tasks = NULL;
    c->nlines = c->nsecs = c->nblocks = c->ntasks = 0;
    c->have_secs = c->have_tasks = 0;
    c->name_match = 0;
}

static void ensure_lines(Ctx *c) {
    size_t i, cap = 0;
    if (c->lines) return;
    for (i = 0; i <= c->len; i++) {
        if (i == 0 || (i <= c->len && c->text[i - 1] == '\n')) {
            if (c->nlines == cap) { size_t nc = cap ? cap * 2 : 64; size_t *nv = realloc(c->lines, nc * sizeof *nv); if (!nv) return; c->lines = nv; cap = nc; }
            c->lines[c->nlines++] = i;
        }
    }
}

static uint32_t line_of(Ctx *c, size_t off) {
    size_t lo = 0, hi;
    ensure_lines(c);
    hi = c->nlines;
    while (lo + 1 < hi) { size_t mid = (lo + hi) / 2; if (c->lines[mid] <= off) lo = mid; else hi = mid; }
    return (uint32_t)lo + 1;
}

/* Blocks start at a blank-line-separated paragraph or at a list item. */
static void ensure_blocks(Ctx *c) {
    size_t i, cap = 0;
    int prev_blank = 1;
    if (c->blocks) return;
    ensure_lines(c);
    for (i = 0; i < c->nlines; i++) {
        size_t s = c->lines[i], e = i + 1 < c->nlines ? c->lines[i + 1] : c->len, k = s;
        int blank, item;
        while (k < e && (c->text[k] == ' ' || c->text[k] == '\t')) k++;
        blank = k >= e || c->text[k] == '\n' || c->text[k] == '\r';
        item = !blank && ((c->text[k] == '-' || c->text[k] == '*' || c->text[k] == '+') && k + 1 < e && c->text[k + 1] == ' ');
        if (!item && !blank) { size_t d = k; while (d < e && isdigit((unsigned char)c->text[d])) d++; if (d > k && d + 1 < e && (c->text[d] == '.' || c->text[d] == ')') && c->text[d + 1] == ' ') item = 1; }
        if (!blank && (prev_blank || item)) {
            if (c->nblocks == cap) { size_t nc = cap ? cap * 2 : 32; size_t *nv = realloc(c->blocks, nc * sizeof *nv); if (!nv) return; c->blocks = nv; cap = nc; }
            c->blocks[c->nblocks++] = s;
        }
        prev_blank = blank;
    }
}

static void ensure_secs(Ctx *c) {
    size_t cap = 0;
    if (c->have_secs) return;
    c->have_secs = 1;
    sqlite3_reset(c->sec_st);
    sqlite3_bind_int64(c->sec_st, 1, c->id);
    while (sqlite3_step(c->sec_st) == SQLITE_ROW) {
        if (c->nsecs == cap) { size_t nc = cap ? cap * 2 : 16; size_t *nv = realloc(c->secs, nc * sizeof *nv); if (!nv) break; c->secs = nv; cap = nc; }
        c->secs[c->nsecs++] = (size_t)sqlite3_column_int64(c->sec_st, 0);
    }
    sqlite3_reset(c->sec_st);
}

static void ensure_tasks(Ctx *c) {
    size_t cap = 0;
    if (c->have_tasks) return;
    c->have_tasks = 1;
    sqlite3_reset(c->task_st);
    sqlite3_bind_int64(c->task_st, 1, c->id);
    while (sqlite3_step(c->task_st) == SQLITE_ROW) {
        if (c->ntasks == cap) { size_t nc = cap ? cap * 2 : 16; Task *nv = realloc(c->tasks, nc * sizeof *nv); if (!nv) break; c->tasks = nv; cap = nc; }
        c->tasks[c->ntasks].s = (size_t)sqlite3_column_int64(c->task_st, 0);
        c->tasks[c->ntasks].e = (size_t)sqlite3_column_int64(c->task_st, 1);
        c->tasks[c->ntasks].state = sqlite3_column_int(c->task_st, 2);
        c->ntasks++;
    }
    sqlite3_reset(c->task_st);
}

static void leaf_matches(Node *n, Ctx *c) {
    size_t pos = 0, s, e;
    if (n->cache_serial == c->serial) return;
    n->cache_serial = c->serial;
    n->ncache = 0;
    n->cache_over = 0;
    free(n->cache); n->cache = NULL;
    if (n->plain_ascii) {                      /* a scan without the regular expression engine: ASCII letters fold, other bytes match exactly */
        size_t cap = 0, nl = strlen(n->text), i;
        unsigned char f0 = (unsigned char)n->text[0], lo0 = (unsigned char)((f0 >= 'A' && f0 <= 'Z') ? f0 + 32 : f0), up0 = (unsigned char)((lo0 >= 'a' && lo0 <= 'z') ? lo0 - 32 : lo0);
        for (i = 0; i + nl <= c->len; i++) {
            unsigned char b = (unsigned char)c->text[i];
            size_t k;
            if (n->match_case ? b != f0 : (b != lo0 && b != up0)) continue;
            for (k = 1; k < nl; k++) {
                unsigned char x = (unsigned char)c->text[i + k], y = (unsigned char)n->text[k];
                if (!n->match_case) { if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32); if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32); }
                if (x != y) break;
            }
            if (k < nl) continue;
            if (n->ncache >= 5000) { n->cache_over = 1; break; }
            if (n->ncache == cap) { size_t nc = cap ? cap * 2 : 8; Match *nv = realloc(n->cache, nc * sizeof *nv); if (!nv) break; n->cache = nv; cap = nc; }
            n->cache[n->ncache].start = i; n->cache[n->ncache].end = i + nl; n->ncache++;
            i += nl - 1;
        }
    } else {
        size_t cap = 0;
        while (n->find && ex_find_next(n->find, c->text, c->len, pos, &s, &e) == EX_FIND_MATCH) {
            if (n->ncache >= 5000) { n->cache_over = 1; break; }
            if (n->ncache == cap) { size_t nc = cap ? cap * 2 : 8; Match *nv = realloc(n->cache, nc * sizeof *nv); if (!nv) break; n->cache = nv; cap = nc; }
            n->cache[n->ncache].start = s; n->cache[n->ncache].end = e; n->ncache++;
            pos = e > s ? e : s + 1;
            if (pos > c->len) break;
        }
    }
}

static int find_in(ExFind *f, const char *s, size_t len) { size_t a, b; return f && ex_find_next(f, s, len, 0, &a, &b) == EX_FIND_MATCH; }

/* Does the term occur in a short string (a name or a path)? */
static int term_in(const Node *n, const char *s, size_t len) {
    if (n->plain_ascii) {
        size_t nl = strlen(n->text), i, k;
        for (i = 0; i + nl <= len; i++) {
            for (k = 0; k < nl; k++) {
                unsigned char x = (unsigned char)s[i + k], y = (unsigned char)n->text[k];
                if (!n->match_case) { if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32); if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32); }
                if (x != y) break;
            }
            if (k == nl) return 1;
        }
        return 0;
    }
    return find_in(n->find, s, len);
}

/* Does one stored value of the property satisfy the filter? (The same test for the candidates and for the evaluation.) */
static int prop_match(const Node *n, const char *v) {
    if (n->pop == P_EXISTS) return 1;
    if (n->pop == P_NULL) return v[0] == 0;
    if (n->pop == P_EQ) {
        const char *s = v;
        for (;;) {
            const char *e = strchr(s, '\n');
            size_t l = e ? (size_t)(e - s) : strlen(s);
            char *k = ex_name_key(s, l);
            int eq = k && n->pvkey && !strcmp(k, n->pvkey);
            free(k);
            if (eq) return 1;
            if (!e) return 0;
            s = e + 1;
        }
    } else {
        char *e1, *e2;
        double a = strtod(v, &e1), b = strtod(n->pvalue, &e2);
        int cmp;
        if (v[0] && n->pvalue[0] && *e1 == 0 && *e2 == 0) cmp = a < b ? -1 : a > b;
        else cmp = strcmp(v, n->pvalue);
        return n->pop == P_GT ? cmp > 0 : n->pop == P_GE ? cmp >= 0 : n->pop == P_LT ? cmp < 0 : cmp <= 0;
    }
}

static int prop_eval(Node *n, Ctx *c) {
    int ok = 0;
    sqlite3_reset(c->prop_st);
    sqlite3_bind_int64(c->prop_st, 1, c->id);
    sqlite3_bind_text(c->prop_st, 2, n->pkey, -1, SQLITE_STATIC);
    while (!ok && sqlite3_step(c->prop_st) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(c->prop_st, 0);
        ok = prop_match(n, v ? v : "");
    }
    sqlite3_reset(c->prop_st);
    return ok;
}

static int tag_eval(Node *n, Ctx *c) {
    int ok;
    sqlite3_reset(c->tag_st);
    sqlite3_bind_int64(c->tag_st, 1, c->id);
    sqlite3_bind_text(c->tag_st, 2, n->pkey, -1, SQLITE_STATIC);
    ok = sqlite3_step(c->tag_st) == SQLITE_ROW;
    sqlite3_reset(c->tag_st);
    return ok;
}

static int eval(Node *n, Ctx *c, size_t rs, size_t re, int whole, MSet *out);

static int eval_scope(Node *n, Ctx *c, MSet *out) {
    size_t i, saved = out ? out->n : 0;
    int ok = 0;
    Node *kid = n->kids[0];
    switch (n->scope) {
    case SC_LINE:
        ensure_lines(c);
        for (i = 0; i < c->nlines; i++) {
            size_t s = c->lines[i], e = i + 1 < c->nlines ? c->lines[i + 1] : c->len, before = out ? out->n : 0;
            if (eval(kid, c, s, e, 0, out)) ok = 1; else if (out) out->n = before;
        }
        break;
    case SC_BLOCK:
        ensure_blocks(c);
        for (i = 0; i < c->nblocks; i++) {
            size_t s = c->blocks[i], e = i + 1 < c->nblocks ? c->blocks[i + 1] : c->len, before = out ? out->n : 0;
            if (eval(kid, c, s, e, 0, out)) ok = 1; else if (out) out->n = before;
        }
        break;
    case SC_SECTION: {
        size_t prev = 0;
        ensure_secs(c);
        for (i = 0; i <= c->nsecs; i++) {
            size_t e = i < c->nsecs ? c->secs[i] : c->len, before = out ? out->n : 0;
            if (e > prev && eval(kid, c, prev, e, 0, out)) ok = 1; else if (out) out->n = before;
            if (i < c->nsecs) prev = e;
        }
        break;
    }
    default:
        ensure_tasks(c);
        for (i = 0; i < c->ntasks; i++) {
            size_t before = out ? out->n : 0;
            if (n->scope == SC_TASK_TODO && c->tasks[i].state != 0) continue;
            if (n->scope == SC_TASK_DONE && c->tasks[i].state != 1) continue;
            if (eval(kid, c, c->tasks[i].s, c->tasks[i].e, 0, out)) ok = 1; else if (out) out->n = before;
        }
        break;
    }
    if (!ok && out) out->n = saved;
    return ok;
}

static int eval(Node *n, Ctx *c, size_t rs, size_t re, int whole, MSet *out) {
    size_t i, saved = out ? out->n : 0;
    int ok;
    switch (n->kind) {
    case N_TERM: {
        int found = 0;
        leaf_matches(n, c);
        {   /* the matches are in order of position: start at the first one that can be inside the region */
            size_t lo = 0, hi = n->ncache;
            while (lo < hi) { size_t mid = (lo + hi) / 2; if (n->cache[mid].start < rs) lo = mid + 1; else hi = mid; }
            for (i = lo; i < n->ncache && n->cache[i].start < re; i++) {
                if (n->cache[i].end <= re) { found = 1; if (out) ms_add(out, n->cache[i].start, n->cache[i].end); }
            }
        }
        if (n->any && whole && term_in(n, c->name, strlen(c->name))) { c->name_match = 1; found = 1; }
        return found;
    }
    case N_FILE: return term_in(n, c->name, strlen(c->name));
    case N_PATH: return term_in(n, c->path, strlen(c->path));
    case N_TAG: return tag_eval(n, c);
    case N_PROP: return prop_eval(n, c);
    case N_NOT: return !eval(n->kids[0], c, rs, re, whole, NULL);
    case N_AND:
        for (i = 0; i < n->nk; i++) if (!eval(n->kids[i], c, rs, re, whole, out)) { if (out) out->n = saved; return 0; }
        return 1;
    case N_OR:
        ok = 0;
        for (i = 0; i < n->nk; i++) { size_t b = out ? out->n : 0; if (eval(n->kids[i], c, rs, re, whole, out)) ok = 1; else if (out) out->n = b; }
        return ok;
    case N_SCOPE: return eval_scope(n, c, out);
    }
    return 0;
}

/* ---- candidates ---- */

typedef struct Ids { int64_t *v; size_t n; int all; } Ids;

typedef struct Notes { int64_t *id; char **path; int64_t *mtime, *ctime; size_t n; } Notes;

static void ids_free(Ids *s) { free(s->v); s->v = NULL; s->n = 0; }

static Ids ids_intersect(Ids a, Ids b) {
    Ids r = {0};
    size_t i = 0, j = 0;
    if (a.all) { return b; }
    if (b.all) { return a; }
    r.v = malloc((a.n < b.n ? a.n : b.n) * sizeof *r.v + 1);
    while (r.v && i < a.n && j < b.n) {
        if (a.v[i] < b.v[j]) i++; else if (a.v[i] > b.v[j]) j++; else r.v[r.n++] = a.v[i], i++, j++;
    }
    free(a.v); free(b.v);
    return r;
}

static Ids ids_union(Ids a, Ids b) {
    Ids r = {0};
    size_t i = 0, j = 0;
    if (a.all || b.all) { ids_free(&a); ids_free(&b); r.all = 1; return r; }
    r.v = malloc((a.n + b.n) * sizeof *r.v + 1);
    while (r.v && (i < a.n || j < b.n)) {
        if (j >= b.n || (i < a.n && a.v[i] < b.v[j])) r.v[r.n++] = a.v[i++];
        else if (i >= a.n || b.v[j] < a.v[i]) r.v[r.n++] = b.v[j++];
        else { r.v[r.n++] = a.v[i]; i++; j++; }
    }
    free(a.v); free(b.v);
    return r;
}

static int utf8_len(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }

static Ids sql_ids(ExIndex *r, const char *sql, const char *arg) {
    Ids s = {0};
    sqlite3_stmt *st = ex_db_prepare(r, sql);
    size_t cap = 0;
    if (!st) { s.all = 1; return s; }
    if (arg) sqlite3_bind_text(st, 1, arg, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (s.n == cap) { size_t nc = cap ? cap * 2 : 64; int64_t *nv = realloc(s.v, nc * sizeof *nv); if (!nv) break; s.v = nv; cap = nc; }
        s.v[s.n++] = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    return s;
}

static Ids candidates(Node *n, ExIndex *r, const Notes *notes) {
    Ids s = {0};
    size_t i;
    switch (n->kind) {
    case N_TERM:
        if (!n->regex && utf8_len(n->text) >= 3) {
            int64_t *ids = NULL;
            size_t cnt = 0;
            int scanned = 0;
            if (ex_index_candidates(r, n->text, strlen(n->text), &ids, &cnt, &scanned) == 0 && !scanned) {
                s.v = ids; s.n = cnt;
                if (n->any) {            /* the name may match instead */
                    Ids names = {0};
                    names.v = malloc(notes->n * sizeof *names.v + 1);
                    for (i = 0; names.v && i < notes->n; i++) {
                        const char *b = strrchr(notes->path[i], '/');
                        b = b ? b + 1 : notes->path[i];
                        if (term_in(n, b, strlen(b))) names.v[names.n++] = notes->id[i];
                    }
                    return ids_union(s, names);
                }
                return s;
            }
            free(ids);
        }
        s.all = 1; return s;
    case N_AND: {
        Ids acc = {0};
        acc.all = 1;
        for (i = 0; i < n->nk; i++) acc = ids_intersect(acc, candidates(n->kids[i], r, notes));
        return acc;
    }
    case N_OR: {
        Ids acc = {0};
        for (i = 0; i < n->nk; i++) { Ids k = candidates(n->kids[i], r, notes); acc = i ? ids_union(acc, k) : k; }
        return acc;
    }
    case N_SCOPE: {
        Ids k = candidates(n->kids[0], r, notes);
        if (n->scope >= SC_TASK) {
            Ids t = sql_ids(r, n->scope == SC_TASK ? "SELECT DISTINCT file_id FROM tasks ORDER BY file_id" : n->scope == SC_TASK_TODO ? "SELECT DISTINCT file_id FROM tasks WHERE state = 0 ORDER BY file_id" : "SELECT DISTINCT file_id FROM tasks WHERE state = 1 ORDER BY file_id", NULL);
            k = ids_intersect(k, t);
        }
        return k;
    }
    case N_FILE: case N_PATH: {
        Ids names = {0};
        names.v = malloc(notes->n * sizeof *names.v + 1);
        for (i = 0; names.v && i < notes->n; i++) {
            const char *b = strrchr(notes->path[i], '/'), *subject = n->kind == N_PATH ? notes->path[i] : (b ? b + 1 : notes->path[i]);
            if (term_in(n, subject, strlen(subject))) names.v[names.n++] = notes->id[i];
        }
        return names;
    }
    case N_TAG:
        return sql_ids(r, "SELECT DISTINCT file_id FROM tags WHERE tag_key = ?1 OR substr(tag_key, 1, length(?1) + 1) = ?1 || '/' ORDER BY file_id", n->pkey);
    case N_PROP:
        if (n->pop != P_EXISTS) {              /* exactly the notes whose value passes: the others are never read */
            sqlite3_stmt *st = ex_db_prepare(r, "SELECT file_id, value FROM props WHERE name_key = ?1 ORDER BY file_id");
            size_t cap = 0;
            if (!st) { s.all = 1; return s; }
            sqlite3_bind_text(st, 1, n->pkey, -1, SQLITE_STATIC);
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *v = (const char *)sqlite3_column_text(st, 1);
                int64_t id = sqlite3_column_int64(st, 0);
                if (!prop_match(n, v ? v : "") || (s.n && s.v[s.n - 1] == id)) continue;
                if (s.n == cap) { size_t nc = cap ? cap * 2 : 64; int64_t *nv = realloc(s.v, nc * sizeof *nv); if (!nv) break; s.v = nv; cap = nc; }
                s.v[s.n++] = id;
            }
            sqlite3_finalize(st);
            return s;
        }
        return sql_ids(r, "SELECT DISTINCT file_id FROM props WHERE name_key = ?1 ORDER BY file_id", n->pkey);
    default: s.all = 1; return s;
    }
}

static int needs_text(const Node *n) {
    size_t i;
    if (n->kind == N_TERM || n->kind == N_SCOPE) return 1;
    for (i = 0; i < n->nk; i++) if (needs_text(n->kids[i])) return 1;
    return 0;
}

static int prepare(Node *n, const ExSearchOptions *o, char *err) {
    size_t i;
    if (n->kind == N_TERM || n->kind == N_FILE || n->kind == N_PATH) {
        ExFindOptions fo;
        char msg[160];
        memset(&fo, 0, sizeof fo);
        fo.regex = n->regex;
        fo.match_case = n->case_mode == 1 || (n->case_mode == 0 && o->match_case);
        ex_find_free(n->find);
        n->find = ex_find_compile(n->text, strlen(n->text), &fo, msg, sizeof msg);
        n->match_case = fo.match_case;
        n->plain_ascii = !n->regex && n->text[0] != 0;
        { const char *q; for (q = n->text; *q; q++) if ((unsigned char)*q >= 0x80) n->plain_ascii = 0; }
        if (!n->find) { snprintf(err, 200, "%s", msg[0] ? msg : "the term is empty"); return -1; }
    } else if (n->kind == N_TAG) {
        free(n->pkey);
        n->pkey = ex_name_key(n->text, strlen(n->text));
    }
    for (i = 0; i < n->nk; i++) if (prepare(n->kids[i], o, err) != 0) return -1;
    return 0;
}

static int load_notes(ExIndex *r, Notes *notes) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT id, path, mtime, ctime FROM files WHERE kind = 1 AND hash IS NOT NULL ORDER BY id");
    size_t cap = 0;
    memset(notes, 0, sizeof *notes);
    if (!st) return -1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (notes->n == cap) {
            size_t nc = cap ? cap * 2 : 1024;
            int64_t *a = realloc(notes->id, nc * sizeof *a), *b;
            char **p;
            if (!a) break;
            notes->id = a;
            p = realloc(notes->path, nc * sizeof *p); if (!p) break; notes->path = p;
            b = realloc(notes->mtime, nc * sizeof *b); if (!b) break; notes->mtime = b;
            b = realloc(notes->ctime, nc * sizeof *b); if (!b) break; notes->ctime = b;
            cap = nc;
        }
        notes->id[notes->n] = sqlite3_column_int64(st, 0);
        notes->path[notes->n] = ex_strdup((const char *)sqlite3_column_text(st, 1));
        notes->mtime[notes->n] = sqlite3_column_int64(st, 2);
        notes->ctime[notes->n] = sqlite3_column_int64(st, 3);
        notes->n++;
    }
    sqlite3_finalize(st);
    return 0;
}

static void notes_free(Notes *n) { size_t i; for (i = 0; i < n->n; i++) free(n->path[i]); free(n->id); free(n->path); free(n->mtime); free(n->ctime); }

void ex_search_results_free(ExSearchResult *r, size_t n) { size_t i; for (i = 0; i < n; i++) { free(r[i].path); free(r[i].matches); } free(r); }

static int cmp_match(const void *a, const void *b) {
    const Match *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start ? 1 : x->end < y->end ? -1 : x->end > y->end;
}

static int g_sort;
static const Notes *g_notes;
static char **g_keys;
static int cmp_order(const void *a, const void *b) {
    size_t x = *(const size_t *)a, y = *(const size_t *)b;
    int c;
    switch (g_sort) {
    case EX_SEARCH_MODIFIED_NEW: c = g_notes->mtime[x] < g_notes->mtime[y] ? 1 : g_notes->mtime[x] > g_notes->mtime[y] ? -1 : 0; break;
    case EX_SEARCH_MODIFIED_OLD: c = g_notes->mtime[x] < g_notes->mtime[y] ? -1 : g_notes->mtime[x] > g_notes->mtime[y]; break;
    case EX_SEARCH_CREATED_NEW: c = g_notes->ctime[x] < g_notes->ctime[y] ? 1 : g_notes->ctime[x] > g_notes->ctime[y] ? -1 : 0; break;
    case EX_SEARCH_CREATED_OLD: c = g_notes->ctime[x] < g_notes->ctime[y] ? -1 : g_notes->ctime[x] > g_notes->ctime[y]; break;
    case EX_SEARCH_PATH_ZA: c = strcmp(g_keys[y], g_keys[x]); break;
    default: c = strcmp(g_keys[x], g_keys[y]); break;
    }
    return c ? c : strcmp(g_notes->path[x], g_notes->path[y]);
}

static int cmp_result(const void *a, const void *b) {
    const ExSearchResult *x = a, *y = b;
    const char *bx = strrchr(x->path, '/'), *by = strrchr(y->path, '/');
    int c;
    switch (g_sort) {
    case EX_SEARCH_PATH_ZA: c = strcmp(y->path, x->path); break;
    case EX_SEARCH_MODIFIED_NEW: c = x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0; break;
    case EX_SEARCH_MODIFIED_OLD: c = x->mtime < y->mtime ? -1 : x->mtime > y->mtime; break;
    case EX_SEARCH_CREATED_NEW: c = x->ctime < y->ctime ? 1 : x->ctime > y->ctime ? -1 : 0; break;
    case EX_SEARCH_CREATED_OLD: c = x->ctime < y->ctime ? -1 : x->ctime > y->ctime; break;
    case EX_SEARCH_MATCHES_MOST: c = x->total_matches < y->total_matches ? 1 : x->total_matches > y->total_matches ? -1 : 0; break;
    case EX_SEARCH_MATCHES_LEAST: c = x->total_matches < y->total_matches ? -1 : x->total_matches > y->total_matches; break;
    default: {
        char *kx = ex_name_key(bx ? bx + 1 : x->path, strlen(bx ? bx + 1 : x->path)), *ky = ex_name_key(by ? by + 1 : y->path, strlen(by ? by + 1 : y->path));
        c = kx && ky ? strcmp(kx, ky) : 0;
        free(kx); free(ky);
    }
    }
    return c ? c : strcmp(x->path, y->path);
}

/* Reading the notes is most of the time of a search, so a second thread reads the next notes while this one looks at the
 * current one (at most PREFETCH ahead). The notes are handed over in the order they will be looked at. */
#define PREFETCH 48
typedef struct Prefetch {
    const char *root;
    char **paths; size_t n;
    char **text; size_t *len;
    SDL_Semaphore *items, *slots;
    SDL_AtomicInt stop;
    SDL_Thread *th;
} Prefetch;

static int prefetch_main(void *arg) {
    Prefetch *p = arg;
    size_t i;
    for (i = 0; i < p->n; i++) {
        SDL_WaitSemaphore(p->slots);
        if (SDL_GetAtomicInt(&p->stop)) break;
        p->text[i] = ex_index_read_note(p->root, p->paths[i], &p->len[i]);
        SDL_SignalSemaphore(p->items);
    }
    return 0;
}

static Prefetch *prefetch_start(const char *root, char **paths, size_t n) {
    Prefetch *p = calloc(1, sizeof *p);
    size_t i;
    if (!p) return NULL;
    p->root = root; p->paths = paths; p->n = n;
    p->text = calloc(n + 1, sizeof *p->text); p->len = calloc(n + 1, sizeof *p->len);
    p->items = SDL_CreateSemaphore(0); p->slots = SDL_CreateSemaphore(PREFETCH);
    if (!p->text || !p->len || !p->items || !p->slots) goto fail;
    p->th = SDL_CreateThread(prefetch_main, "search-read", p);
    if (!p->th) goto fail;
    return p;
fail:
    for (i = 0; i < n; i++) free(p->text ? p->text[i] : NULL);
    free(p->text); free(p->len);
    if (p->items) SDL_DestroySemaphore(p->items);
    if (p->slots) SDL_DestroySemaphore(p->slots);
    free(p);
    return NULL;
}

/* The text of the i-th note, in order; the caller frees it. */
static char *prefetch_take(Prefetch *p, size_t i, size_t *len) {
    char *t;
    SDL_WaitSemaphore(p->items);
    t = p->text[i]; p->text[i] = NULL;
    *len = p->len[i];
    SDL_SignalSemaphore(p->slots);
    return t;
}

static void prefetch_stop(Prefetch *p, size_t taken) {
    size_t i;
    if (!p) return;
    SDL_SetAtomicInt(&p->stop, 1);
    SDL_SignalSemaphore(p->slots);
    SDL_WaitThread(p->th, NULL);
    for (i = taken; i < p->n; i++) free(p->text[i]);
    free(p->text); free(p->len);
    SDL_DestroySemaphore(p->items); SDL_DestroySemaphore(p->slots);
    free(p);
}

int ex_search_run(ExIndex *r, const char *root, ExSearch *q, const ExSearchOptions *opt, ExSearchResult **out, size_t *nout, size_t *files_read) {
    ExSearchOptions o;
    Notes notes;
    Ids cand;
    Ctx c;
    ExSearchResult *res = NULL;
    size_t i, nres = 0, cap = 0, k, max_results, max_matches, *order = NULL, oi, norder = 0;
    int by_matches, early;
    Prefetch *pf = NULL;
    long reads0 = ex_index_note_reads();
    int text_needed, rc = 0;
    char err[220];
    memset(&o, 0, sizeof o);
    if (opt) o = *opt;
    max_results = o.max_results ? o.max_results : 1000;
    max_matches = o.max_matches ? o.max_matches : 200;
    *out = NULL; *nout = 0;
    if (prepare(q->root, &o, err) != 0) return -1;
    if (load_notes(r, &notes) != 0) return -1;
    cand = candidates(q->root, r, &notes);
    text_needed = needs_text(q->root);
    memset(&c, 0, sizeof c);
    c.r = r;
    c.tag_st = ex_db_prepare(r, "SELECT 1 FROM tags WHERE file_id = ?1 AND (tag_key = ?2 OR substr(tag_key, 1, length(?2) + 1) = ?2 || '/') LIMIT 1");
    c.prop_st = ex_db_prepare(r, "SELECT value FROM props WHERE file_id = ?1 AND name_key = ?2");
    c.sec_st = ex_db_prepare(r, "SELECT start FROM sections WHERE file_id = ?1 ORDER BY ord");
    c.task_st = ex_db_prepare(r, "SELECT start, end_, state FROM tasks WHERE file_id = ?1 ORDER BY start");
    if (!c.tag_st || !c.prop_st || !c.sec_st || !c.task_st) rc = -1;
    /* the notes in the order of the results: when that does not depend on the matches the search stops at the first max_results hits */
    by_matches = o.sort == EX_SEARCH_MATCHES_MOST || o.sort == EX_SEARCH_MATCHES_LEAST;
    early = !by_matches;
    order = malloc((notes.n + 1) * sizeof *order);
    if (!order) rc = -1;
    norder = 0;
    for (i = 0; order && i < notes.n; i++) {              /* only the notes the index names (ids are sorted: binary search) */
        if (!cand.all) {
            size_t lo = 0, hi = cand.n;
            while (lo < hi) { size_t mid = (lo + hi) / 2; if (cand.v[mid] < notes.id[i]) lo = mid + 1; else hi = mid; }
            if (lo >= cand.n || cand.v[lo] != notes.id[i]) continue;
        }
        order[norder++] = i;
    }
    if (order && early && norder > 1) {
        char **keys = calloc(notes.n, sizeof *keys);
        if (keys) {
            const char *b;
            for (oi = 0; oi < norder; oi++) { i = order[oi]; b = strrchr(notes.path[i], '/'); b = b ? b + 1 : notes.path[i]; keys[i] = ex_name_key(b, strlen(b)); if (!keys[i]) keys[i] = ex_strdup(""); }
            g_sort = o.sort; g_notes = &notes; g_keys = keys;
            qsort(order, norder, sizeof *order, cmp_order);
            for (oi = 0; oi < norder; oi++) free(keys[order[oi]]);
            free(keys);
        }
    }
    if (text_needed && norder > 2 * PREFETCH && rc == 0) {
        char **pp = malloc(norder * sizeof *pp);
        if (pp) { for (oi = 0; oi < norder; oi++) pp[oi] = notes.path[order[oi]]; pf = prefetch_start(root, pp, norder); if (!pf) free(pp); }
    }
    for (oi = 0; rc == 0 && oi < norder; oi++) {
        MSet ms = {0};
        i = order[oi];
        if (early && nres >= max_results) break;
        char *text = NULL;
        size_t len = 0;
        int hit;
        c.id = notes.id[i]; c.path = notes.path[i];
        c.name = strrchr(c.path, '/') ? strrchr(c.path, '/') + 1 : c.path;
        c.serial++;
        ctx_reset(&c);
        if (text_needed) {
            text = pf ? prefetch_take(pf, oi, &len) : ex_index_read_note(root, c.path, &len);
            if (!text) continue;
        }
        c.text = text ? text : ""; c.len = len;
        hit = eval(q->root, &c, 0, len, 1, &ms);
        if (hit) {
            ExSearchResult *rr;
            size_t total;
            if (nres == cap) { size_t nc = cap ? cap * 2 : 64; ExSearchResult *nv = realloc(res, nc * sizeof *nv); if (!nv) { free(ms.v); free(text); rc = -1; break; } res = nv; cap = nc; }
            rr = &res[nres++];
            memset(rr, 0, sizeof *rr);
            rr->path = ex_strdup(c.path);
            rr->mtime = notes.mtime[i];
            rr->ctime = notes.ctime[i];
            rr->name_match = c.name_match;
            if (ms.n > 1) qsort(ms.v, ms.n, sizeof *ms.v, cmp_match);
            /* drop duplicates (two terms found the same place) */
            for (k = 0, total = 0; k < ms.n; k++) if (!total || ms.v[k].start != ms.v[total - 1].start || ms.v[k].end != ms.v[total - 1].end) ms.v[total++] = ms.v[k];
            rr->total_matches = total;
            rr->nmatches = total < max_matches ? total : max_matches;
            if (rr->nmatches) {
                rr->matches = malloc(rr->nmatches * sizeof *rr->matches);
                for (k = 0; rr->matches && k < rr->nmatches; k++) { rr->matches[k].start = ms.v[k].start; rr->matches[k].end = ms.v[k].end; rr->matches[k].line = line_of(&c, ms.v[k].start); }
            }
        }
        free(ms.v);
        free(text);
    }
    if (pf) { char **pp = pf->paths; prefetch_stop(pf, oi < norder ? oi : norder); free(pp); }
    ctx_reset(&c);
    sqlite3_finalize(c.tag_st); sqlite3_finalize(c.prop_st); sqlite3_finalize(c.sec_st); sqlite3_finalize(c.task_st);
    ids_free(&cand);
    notes_free(&notes);
    free(order);
    g_sort = o.sort;
    if (nres > 1) qsort(res, nres, sizeof *res, cmp_result);
    if (nres > max_results) { for (i = max_results; i < nres; i++) { free(res[i].path); free(res[i].matches); } nres = max_results; }
    if (files_read) *files_read = (size_t)(ex_index_note_reads() - reads0);
    if (rc != 0) { ex_search_results_free(res, nres); return -1; }
    *out = res; *nout = nres;
    return 0;
}

char *ex_search_context(const char *text, size_t len, const ExSearchMatch *m, int extra, size_t *offset) {
    size_t s = m->start > len ? len : m->start, a = s, b = s;
    int k;
    char *out;
    for (k = 0; k <= extra; k++) { while (a > 0 && text[a - 1] != '\n') a--; if (k < extra && a > 0) a--; }
    for (k = 0; k <= extra; k++) { while (b < len && text[b] != '\n') b++; if (k < extra && b < len) b++; }
    out = malloc(b - a + 1);
    if (!out) return NULL;
    memcpy(out, text + a, b - a);
    out[b - a] = 0;
    if (offset) *offset = s - a;
    return out;
}
