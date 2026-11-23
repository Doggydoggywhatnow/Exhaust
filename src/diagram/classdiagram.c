/* Mermaid class diagrams (6d-iii): classes with annotations, attributes and methods (in a `{ }` body or as `Name : member` lines), the relations `<|--`
 * `*--` `o--` `-->` `..>` `..|>` `--` `..` and their mirror images with cardinalities and a label, `direction`. Laid out with the layered layout: the class
 * written on the left of a relation is above the one on the right, so a parent written first is on top. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

typedef struct CClass { char *id, *label, *annot; char **attrs, **meths; int na, nm, capa, capm; float w, h, nameh; } CClass;
typedef struct CRel { int a, b, dash, hs, he; char *label, *ca, *cb; } CRel;
typedef struct CC { ExDiagram *d; CClass *c; int nc; CRel *r; int nr, capr; int dir, in_body, ns_depth; } CC;

static int is_id_char(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80 || c == '~' || c == '.' ; }

/* `List~int~` is shown as `List<int>` */
static char *generic(const char *s, size_t n) {
    char *o = malloc(n * 2 + 1);
    size_t i, k = 0;
    int open = 0;
    if (!o) return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '~') { o[k++] = open ? '>' : '<'; open = !open; }
        else o[k++] = s[i];
    }
    o[k] = 0;
    return o;
}

static int class_for(CC *c, const char *id, size_t n) {
    int i;
    char *raw = dg_strndup(id, n), *base;
    char *tilde;
    if (!raw) return -1;
    tilde = strchr(raw, '~');
    base = tilde ? dg_strndup(raw, (size_t)(tilde - raw)) : dg_strndup(raw, n);   /* the id is the name without its generic part */
    for (i = 0; i < c->nc; i++) if (!strcmp(c->c[i].id, base)) {
        if (tilde) { free(c->c[i].label); c->c[i].label = generic(raw, n); }
        free(raw); free(base);
        return i;
    }
    if (c->nc >= 300) { dg_fail(c->d, "more than 300 classes"); free(raw); free(base); return -1; }
    {
        CClass *t = realloc(c->c, (size_t)(c->nc + 1) * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); free(raw); free(base); return -1; }
        c->c = t;
    }
    memset(&c->c[c->nc], 0, sizeof c->c[0]);
    c->c[c->nc].id = base;
    c->c[c->nc].label = generic(raw, n);
    free(raw);
    return c->nc++;
}

static void add_member(CClass *k, const char *s, size_t n) {
    char *m;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (!n) return;
    m = generic(s, n);
    if (!m) return;
    if (strchr(m, '(')) {
        if (k->nm == k->capm) { int nc = k->capm ? k->capm * 2 : 4; char **t = realloc(k->meths, (size_t)nc * sizeof *t); if (!t) { free(m); return; } k->meths = t; k->capm = nc; }
        k->meths[k->nm++] = m;
    } else {
        if (k->na == k->capa) { int nc = k->capa ? k->capa * 2 : 4; char **t = realloc(k->attrs, (size_t)nc * sizeof *t); if (!t) { free(m); return; } k->attrs = t; k->capa = nc; }
        k->attrs[k->na++] = m;
    }
}

static int kw(const char *s, size_t n, const char *w) { size_t l = strlen(w); return n >= l && !strncmp(s, w, l) && (n == l || s[l] == ' ' || s[l] == '\t' || s[l] == '{'); }

static const struct { const char *op; int hs, he, dash; } relops[] = {
    {"<|--", EXD_H_TRIANGLE, 0, 0}, {"--|>", 0, EXD_H_TRIANGLE, 0}, {"<|..", EXD_H_TRIANGLE, 0, 1}, {"..|>", 0, EXD_H_TRIANGLE, 1},
    {"*--", EXD_H_DIAMOND_FILLED, 0, 0}, {"--*", 0, EXD_H_DIAMOND_FILLED, 0}, {"o--", EXD_H_DIAMOND, 0, 0}, {"--o", 0, EXD_H_DIAMOND, 0},
    {"<--", EXD_H_OPEN, 0, 0}, {"-->", 0, EXD_H_OPEN, 0}, {"<..", EXD_H_OPEN, 0, 1}, {"..>", 0, EXD_H_OPEN, 1}, {"--", 0, 0, 0}, {"..", 0, 0, 1},
};

static int add_rel(CC *c, int a, int b, int hs, int he, int dash, char *ca, char *cb, char *label) {
    CRel *r;
    if (c->nr == c->capr) {
        int nc = c->capr ? c->capr * 2 : 8;
        CRel *t = realloc(c->r, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return -1; }
        c->r = t; c->capr = nc;
    }
    r = &c->r[c->nr++];
    r->a = a; r->b = b; r->hs = hs; r->he = he; r->dash = dash; r->ca = ca; r->cb = cb; r->label = label;
    return 0;
}

static void statement(CC *c, const char *s, size_t n) {
    size_t p = 0, st;
    int a, b, k;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (!n) return;
    if (c->in_body >= 0) {   /* inside class X { ... } */
        CClass *cl = &c->c[c->in_body];
        if (n == 1 && s[0] == '}') { c->in_body = -1; return; }
        if (n > 4 && s[0] == '<' && s[1] == '<') { const char *e = s + 2; while (e < s + n && !(e[0] == '>' && e[1] == '>')) e++; free(cl->annot); cl->annot = dg_strndup(s + 2, (size_t)(e - s - 2)); return; }
        add_member(cl, s, n);
        return;
    }
    if (kw(s, n, "classDiagram") || kw(s, n, "classDiagram-v2")) return;
    if (kw(s, n, "direction")) {
        size_t q = 9;
        while (q < n && s[q] == ' ') q++;
        if (n - q >= 2) { if (!strncmp(s + q, "LR", 2)) c->dir = 2; else if (!strncmp(s + q, "RL", 2)) c->dir = 3; else if (!strncmp(s + q, "BT", 2)) c->dir = 1; else if (!strncmp(s + q, "TB", 2) || !strncmp(s + q, "TD", 2)) c->dir = 0; else dg_fail(c->d, "unknown direction"); }
        return;
    }
    if (kw(s, n, "namespace")) { c->ns_depth++; return; }
    if (n == 1 && s[0] == '}') { if (c->ns_depth > 0) c->ns_depth--; return; }
    if (kw(s, n, "note") || kw(s, n, "style") || kw(s, n, "classDef") || kw(s, n, "cssClass") || kw(s, n, "click") || kw(s, n, "callback") || kw(s, n, "link") || kw(s, n, "accTitle") || kw(s, n, "accDescr") || kw(s, n, "title")) return;
    if (kw(s, n, "class")) {
        size_t q = 5, e;
        int idx;
        while (q < n && s[q] == ' ') q++;
        e = q;
        while (e < n && is_id_char((unsigned char)s[e])) e++;
        if (e == q) { dg_fail(c->d, "a class name was expected"); return; }
        idx = class_for(c, s + q, e - q);
        if (idx < 0) return;
        while (e < n && s[e] == ' ') e++;
        if (e < n && s[e] == '[') {   /* class A["label"] */
            size_t close = e;
            while (close < n && s[close] != ']') close++;
            if (close < n) { free(c->c[idx].label); c->c[idx].label = dg_clean_label(s + e + 1, close - e - 1); e = close + 1; while (e < n && s[e] == ' ') e++; }
        }
        if (n - e >= 3 && !strncmp(s + e, ":::", 3)) { e += 3; while (e < n && is_id_char((unsigned char)s[e])) e++; while (e < n && s[e] == ' ') e++; }
        if (e < n && s[e] == '{') { c->in_body = idx; if (n > e + 1) { const char *rest = s + e + 1; size_t rn = n - e - 1; while (rn && rest[rn - 1] == ' ') rn--; if (rn && rest[rn - 1] == '}') { c->in_body = -1; rn--; } if (rn) add_member(&c->c[idx], rest, rn); } }
        return;
    }
    if (n > 4 && s[0] == '<' && s[1] == '<') {   /* <<interface>> Name */
        const char *e = s + 2;
        int idx;
        size_t q;
        while (e < s + n && !(e[0] == '>' && e[1] == '>')) e++;
        if (e >= s + n) { dg_fail(c->d, "an annotation is not closed"); return; }
        q = (size_t)(e - s) + 2;
        while (q < n && s[q] == ' ') q++;
        st = q;
        while (q < n && is_id_char((unsigned char)s[q])) q++;
        idx = class_for(c, s + st, q - st);
        if (idx >= 0) { free(c->c[idx].annot); c->c[idx].annot = dg_strndup(s + 2, (size_t)(e - s - 2)); }
        return;
    }
    /* Name : member    or    A rel B */
    st = p;
    while (p < n && is_id_char((unsigned char)s[p])) p++;
    if (p == st) { dg_fail(c->d, "unknown line: %.*s", (int)(n > 30 ? 30 : n), s); return; }
    a = class_for(c, s + st, p - st);
    if (a < 0) return;
    while (p < n && s[p] == ' ') p++;
    if (p < n && s[p] == ':') { add_member(&c->c[a], s + p + 1, n - p - 1); return; }
    {   /* relation */
        char *ca = NULL, *cb = NULL, *label = NULL;
        size_t os, oe;
        int hs = 0, he = 0, dash = 0, found = 0;
        if (p < n && s[p] == '"') { size_t q = p + 1; while (q < n && s[q] != '"') q++; if (q >= n) { dg_fail(c->d, "a cardinality is not closed"); return; } ca = dg_strndup(s + p + 1, q - p - 1); p = q + 1; while (p < n && s[p] == ' ') p++; }
        os = p;
        while (p < n && (s[p] == '<' || s[p] == '>' || s[p] == '|' || s[p] == '*' || s[p] == 'o' || s[p] == '-' || s[p] == '.')) p++;
        oe = p;
        for (k = 0; k < (int)(sizeof relops / sizeof relops[0]); k++)
            if (strlen(relops[k].op) == oe - os && !strncmp(s + os, relops[k].op, oe - os)) { hs = relops[k].hs; he = relops[k].he; dash = relops[k].dash; found = 1; break; }
        if (!found) { dg_fail(c->d, "a relation was expected: %.*s", (int)(n > 30 ? 30 : n), s); free(ca); return; }
        while (p < n && s[p] == ' ') p++;
        if (p < n && s[p] == '"') { size_t q = p + 1; while (q < n && s[q] != '"') q++; if (q >= n) { dg_fail(c->d, "a cardinality is not closed"); free(ca); return; } cb = dg_strndup(s + p + 1, q - p - 1); p = q + 1; while (p < n && s[p] == ' ') p++; }
        st = p;
        while (p < n && is_id_char((unsigned char)s[p])) p++;
        if (p == st) { dg_fail(c->d, "a class name was expected after the relation"); free(ca); free(cb); return; }
        b = class_for(c, s + st, p - st);
        if (b < 0) { free(ca); free(cb); return; }
        while (p < n && s[p] == ' ') p++;
        if (p < n && s[p] == ':') label = dg_clean_label(s + p + 1, n - p - 1);
        else if (p < n) { dg_fail(c->d, "unexpected text after the relation: %.*s", (int)(n - p > 20 ? 20 : n - p), s + p); free(ca); free(cb); return; }
        add_rel(c, a, b, hs, he, dash, ca, cb, label);
    }
}

int dg_class(ExDiagram *d, DgLines *L) {
    CC c;
    LgGraph g;
    const char *line;
    size_t ln;
    int i, j;
    memset(&c, 0, sizeof c);
    memset(&g, 0, sizeof g);
    c.d = d; c.in_body = -1;
    while (!d->has_err && dg_next_line(L, &line, &ln)) statement(&c, line, ln);
    if (!d->has_err && c.in_body >= 0) dg_fail(d, "a class body is not closed with }");
    if (!d->has_err && c.nc == 0) { d->w = d->h = 2.0f; }
    if (d->has_err || c.nc == 0) goto done;
    for (i = 0; i < c.nc; i++) {
        CClass *k = &c.c[i];
        float tw, th, w;
        const char *name = k->label ? k->label : k->id;
        dg_text_size(d, name, strlen(name), 1.0f, &tw, &th);
        w = tw + 2.0f;
        k->nameh = th + 0.9f + (k->annot ? 1.1f : 0.0f);
        if (k->annot) { float aw; char *t = malloc(strlen(k->annot) + 5); if (t) { sprintf(t, "\xC2\xAB%s\xC2\xBB", k->annot); dg_text_size(d, t, strlen(t), 0.85f, &aw, NULL); if (aw + 2.0f > w) w = aw + 2.0f; free(t); } }
        k->h = k->nameh;
        for (j = 0; j < k->na; j++) { float mw; dg_text_size(d, k->attrs[j], strlen(k->attrs[j]), 1.0f, &mw, NULL); if (mw + 1.6f > w) w = mw + 1.6f; }
        for (j = 0; j < k->nm; j++) { float mw; dg_text_size(d, k->meths[j], strlen(k->meths[j]), 1.0f, &mw, NULL); if (mw + 1.6f > w) w = mw + 1.6f; }
        if (k->na) k->h += (float)k->na * 1.3f + 0.7f;
        if (k->nm) k->h += (float)k->nm * 1.3f + 0.7f;
        if (!k->na && !k->nm) k->h += 0.5f;
        k->w = w < 5.0f ? 5.0f : w;
    }
    g.nn = c.nc; g.ne = c.nr; g.dir = c.dir; g.nodesep = 2.2f; g.ranksep = 3.0f;
    g.nodes = calloc((size_t)c.nc, sizeof *g.nodes);
    g.edges = calloc((size_t)(c.nr ? c.nr : 1), sizeof *g.edges);
    if (!g.nodes || !g.edges) { dg_fail(d, "out of memory"); goto done; }
    for (i = 0; i < c.nc; i++) { g.nodes[i].w = c.c[i].w; g.nodes[i].h = c.c[i].h; }
    for (i = 0; i < c.nr; i++) {
        g.edges[i].a = c.r[i].a; g.edges[i].b = c.r[i].b; g.edges[i].minlen = 1;
        if (c.r[i].label) { float tw, th; dg_text_size(d, c.r[i].label, strlen(c.r[i].label), 0.9f, &tw, &th); g.edges[i].lw = tw + 0.6f; g.edges[i].lh = th + 0.3f; }
    }
    if (lg_layout(&g)) { dg_fail(d, "the diagram is too large to lay out"); goto done; }
    for (i = 0; i < c.nr; i++) {
        const CRel *r = &c.r[i];
        const LgEdge *le = &g.edges[i];
        dg_edge_line(d, &g, i, EXD_RECT, EXD_RECT, EXD_C_LINE, r->dash, r->hs, r->he);
        if (r->label) dg_edge_label(d, le->lx, le->ly, r->label, 0.9f);
        if (le->npts >= 2 && le->pts && r->a != r->b) {   /* cardinalities, beside the ends */
            const float *p = le->pts;
            int np = le->npts;
            if (r->ca) {
                float x0, y0, dx = p[2] - p[0], dy = p[3] - p[1], len;
                dg_clip_box(g.nodes[r->a].x, g.nodes[r->a].y, g.nodes[r->a].w, g.nodes[r->a].h, EXD_RECT, p[2], p[3], &x0, &y0);
                len = sqrtf(dx * dx + dy * dy); if (len < 1e-3f) len = 1;
                dg_label(d, x0 + dx / len * 0.8f - dy / len * 0.7f, y0 + dy / len * 0.8f + dx / len * 0.7f, r->ca, strlen(r->ca), 0, 0.85f, EXD_C_MUTED, 0);
            }
            if (r->cb) {
                float x0, y0, dx = p[2 * np - 4] - p[2 * np - 2], dy = p[2 * np - 3] - p[2 * np - 1], len;
                dg_clip_box(g.nodes[r->b].x, g.nodes[r->b].y, g.nodes[r->b].w, g.nodes[r->b].h, EXD_RECT, p[2 * np - 4], p[2 * np - 3], &x0, &y0);
                len = sqrtf(dx * dx + dy * dy); if (len < 1e-3f) len = 1;
                dg_label(d, x0 + dx / len * 0.8f + dy / len * 0.7f, y0 + dy / len * 0.8f - dx / len * 0.7f, r->cb, strlen(r->cb), 0, 0.85f, EXD_C_MUTED, 0);
            }
        }
    }
    for (i = 0; i < c.nc; i++) {
        const CClass *k = &c.c[i];
        float x = g.nodes[i].x - k->w / 2.0f, y = g.nodes[i].y - k->h / 2.0f, yy = y;
        const char *name = k->label ? k->label : k->id;
        dg_box(d, EXD_RECT, x, y, k->w, k->h, EXD_C_NODE_FILL, EXD_C_NODE_STROKE);
        if (k->annot) {
            char *t = malloc(strlen(k->annot) + 5);
            if (t) { sprintf(t, "\xC2\xAB%s\xC2\xBB", k->annot); dg_label(d, x + k->w / 2.0f, y + 0.9f, t, strlen(t), 0, 0.85f, EXD_C_MUTED, 0); free(t); }
            yy += 1.1f;
        }
        dg_label(d, x + k->w / 2.0f, yy + (k->nameh - (k->annot ? 1.1f : 0.0f)) / 2.0f, name, strlen(name), 0, 1.0f, EXD_C_TEXT, 1);
        yy = y + k->nameh;
        if (k->na || k->nm || 1) {
            float ln2[4];
            if (k->na || k->nm) { ln2[0] = x; ln2[1] = yy; ln2[2] = x + k->w; ln2[3] = yy; dg_line(d, ln2, 2, EXD_C_NODE_STROKE, 0, 0, 0); }
            if (k->na) {
                for (j = 0; j < k->na; j++) dg_label(d, x + 0.8f, yy + 0.35f + (float)j * 1.3f + 0.65f, k->attrs[j], strlen(k->attrs[j]), 1, 1.0f, EXD_C_TEXT, 0);
                yy += (float)k->na * 1.3f + 0.7f;
                if (k->nm) { ln2[0] = x; ln2[1] = yy; ln2[2] = x + k->w; ln2[3] = yy; dg_line(d, ln2, 2, EXD_C_NODE_STROKE, 0, 0, 0); }
            }
            for (j = 0; j < k->nm; j++) dg_label(d, x + 0.8f, yy + 0.35f + (float)j * 1.3f + 0.65f, k->meths[j], strlen(k->meths[j]), 1, 1.0f, EXD_C_TEXT, 0);
        }
    }
    lg_free(&g);
    dg_finalize(d, 0.8f);
done:
    free(g.nodes); free(g.edges);
    for (i = 0; i < c.nc; i++) {
        free(c.c[i].id); free(c.c[i].label); free(c.c[i].annot);
        for (j = 0; j < c.c[i].na; j++) free(c.c[i].attrs[j]);
        for (j = 0; j < c.c[i].nm; j++) free(c.c[i].meths[j]);
        free(c.c[i].attrs); free(c.c[i].meths);
    }
    for (i = 0; i < c.nr; i++) { free(c.r[i].label); free(c.r[i].ca); free(c.r[i].cb); }
    free(c.c); free(c.r);
    return d->has_err ? -1 : 0;
}
