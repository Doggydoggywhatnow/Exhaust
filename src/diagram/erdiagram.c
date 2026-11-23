/* Mermaid entity-relationship diagrams (6d-iii): entities with attributes (type, name, keys PK FK UK, comment), relationships `A ||--o{ B : label` with the
 * four cardinalities on each side (drawn as crow's-foot marks) and a solid (identifying) or dashed (non-identifying) line, `direction`. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

typedef struct EAttr { char *type, *name, *keys, *comment; } EAttr;
typedef struct EEnt { char *id, *label; EAttr *a; int na, capa; float w, h, cw[4]; } EEnt;
typedef struct ERel { int a, b, ha, hb, dash; char *label; } ERel;
typedef struct EC { ExDiagram *d; EEnt *e; int ne; ERel *r; int nr, capr; int dir, in_body; } EC;

static int is_id_char(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c >= 0x80; }

static int ent_for(EC *c, const char *id, size_t n) {
    int i;
    EEnt *t;
    for (i = 0; i < c->ne; i++) if (strlen(c->e[i].id) == n && !memcmp(c->e[i].id, id, n)) return i;
    if (c->ne >= 300) { dg_fail(c->d, "more than 300 entities"); return -1; }
    t = realloc(c->e, (size_t)(c->ne + 1) * sizeof *t);
    if (!t) { dg_fail(c->d, "out of memory"); return -1; }
    c->e = t;
    memset(&c->e[c->ne], 0, sizeof c->e[0]);
    c->e[c->ne].id = dg_strndup(id, n);
    return c->ne++;
}

static char *word(const char **s, const char *end) {   /* the next blank-separated word, or a quoted text */
    const char *p = *s, *st;
    char *w;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end) { *s = p; return NULL; }
    if (*p == '"') {
        st = ++p;
        while (p < end && *p != '"') p++;
        w = dg_strndup(st, (size_t)(p - st));
        if (p < end) p++;
    } else {
        st = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != ',') p++;
        w = dg_strndup(st, (size_t)(p - st));
    }
    *s = p;
    return w;
}

static void add_attr(EC *c, EEnt *en, const char *s, size_t n) {
    const char *end = s + n;
    EAttr at;
    char *k;
    memset(&at, 0, sizeof at);
    at.type = word(&s, end);
    at.name = word(&s, end);
    if (!at.type || !at.name) { dg_fail(c->d, "an attribute needs a type and a name"); free(at.type); free(at.name); return; }
    for (;;) {
        while (s < end && (*s == ' ' || *s == '\t' || *s == ',')) s++;
        if (s >= end) break;
        if (*s == '"') { at.comment = word(&s, end); break; }
        k = word(&s, end);
        if (!k) break;
        if (strcmp(k, "PK") && strcmp(k, "FK") && strcmp(k, "UK")) { dg_fail(c->d, "unknown key: %s", k); free(k); free(at.type); free(at.name); free(at.keys); return; }
        if (at.keys) { char *t = malloc(strlen(at.keys) + strlen(k) + 3); if (t) { sprintf(t, "%s, %s", at.keys, k); free(at.keys); at.keys = t; } free(k); }
        else at.keys = k;
    }
    {
        EAttr *t;
        if (en->na == en->capa) { int nc = en->capa ? en->capa * 2 : 4; t = realloc(en->a, (size_t)nc * sizeof *t); if (!t) { dg_fail(c->d, "out of memory"); return; } en->a = t; en->capa = nc; }
        en->a[en->na++] = at;
    }
}

static int card(char x, char y, int left) {   /* the two characters of a cardinality; left: the form written before the line */
    if (left) {
        if (x == '|' && y == '|') return EXD_H_ONLY_ONE;
        if (x == '|' && y == 'o') return EXD_H_ZERO_ONE;
        if (x == '}' && y == '|') return EXD_H_ONE_MANY;
        if (x == '}' && y == 'o') return EXD_H_ZERO_MANY;
    } else {
        if (x == '|' && y == '|') return EXD_H_ONLY_ONE;
        if (x == 'o' && y == '|') return EXD_H_ZERO_ONE;
        if (x == '|' && y == '{') return EXD_H_ONE_MANY;
        if (x == 'o' && y == '{') return EXD_H_ZERO_MANY;
    }
    return 0;
}

static void statement(EC *c, const char *s, size_t n) {
    size_t p = 0, st;
    int a, b;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (!n) return;
    if (c->in_body >= 0) {
        if (n == 1 && s[0] == '}') { c->in_body = -1; return; }
        add_attr(c, &c->e[c->in_body], s, n);
        return;
    }
    if (n >= 9 && !strncmp(s, "erDiagram", 9)) return;
    if (n >= 9 && !strncmp(s, "direction", 9)) {
        size_t q = 9;
        while (q < n && s[q] == ' ') q++;
        if (n - q >= 2) { if (!strncmp(s + q, "LR", 2)) c->dir = 2; else if (!strncmp(s + q, "RL", 2)) c->dir = 3; else if (!strncmp(s + q, "BT", 2)) c->dir = 1; else if (!strncmp(s + q, "TB", 2) || !strncmp(s + q, "TD", 2)) c->dir = 0; else dg_fail(c->d, "unknown direction"); }
        return;
    }
    if ((n >= 5 && !strncmp(s, "style", 5)) || (n >= 5 && !strncmp(s, "class", 5)) || (n >= 8 && !strncmp(s, "classDef", 8)) || (n >= 8 && !strncmp(s, "accTitle", 8)) || (n >= 8 && !strncmp(s, "accDescr", 8)) || (n >= 5 && !strncmp(s, "title", 5))) return;
    st = p;
    while (p < n && is_id_char((unsigned char)s[p])) p++;
    if (p == st) { dg_fail(c->d, "unknown line: %.*s", (int)(n > 30 ? 30 : n), s); return; }
    a = ent_for(c, s + st, p - st);
    if (a < 0) return;
    if (p < n && s[p] == '[') {   /* an alias: p[Person] */
        size_t e = p;
        while (e < n && s[e] != ']') e++;
        if (e >= n) { dg_fail(c->d, "an alias is not closed"); return; }
        free(c->e[a].label);
        c->e[a].label = dg_clean_label(s + p + 1, e - p - 1);
        p = e + 1;
    }
    while (p < n && s[p] == ' ') p++;
    if (p >= n) return;
    if (s[p] == '{') { c->in_body = a; if (n > p + 1) { size_t rn = n - p - 1; const char *rest = s + p + 1; while (rn && rest[rn - 1] == ' ') rn--; if (rn && rest[rn - 1] == '}') { c->in_body = -1; rn--; } if (rn) add_attr(c, &c->e[a], rest, rn); } return; }
    {   /* a relationship */
        const char *op = s + p;
        size_t ol = 0;
        int ha, hb, dash;
        while (p + ol < n && s[p + ol] != ' ') ol++;
        if (ol != 6 || !((op[2] == '-' && op[3] == '-') || (op[2] == '.' && op[3] == '.'))) { dg_fail(c->d, "a relationship such as ||--o{ was expected: %.*s", (int)(n - p > 12 ? 12 : n - p), s + p); return; }
        ha = card(op[0], op[1], 1); hb = card(op[4], op[5], 0); dash = op[2] == '.';
        if (!ha || !hb) { dg_fail(c->d, "unknown cardinality in %.6s", op); return; }
        p += 6;
        while (p < n && s[p] == ' ') p++;
        st = p;
        while (p < n && is_id_char((unsigned char)s[p])) p++;
        if (p == st) { dg_fail(c->d, "an entity was expected after the relationship"); return; }
        b = ent_for(c, s + st, p - st);
        if (b < 0) return;
        while (p < n && s[p] == ' ') p++;
        {
            ERel *t;
            char *label = NULL;
            if (p < n && s[p] == ':') label = dg_clean_label(s + p + 1, n - p - 1);
            else if (p < n) { dg_fail(c->d, "unexpected text after the relationship: %.*s", (int)(n - p > 20 ? 20 : n - p), s + p); return; }
            if (c->nr == c->capr) { int nc = c->capr ? c->capr * 2 : 8; t = realloc(c->r, (size_t)nc * sizeof *t); if (!t) { dg_fail(c->d, "out of memory"); free(label); return; } c->r = t; c->capr = nc; }
            c->r[c->nr].a = a; c->r[c->nr].b = b; c->r[c->nr].ha = ha; c->r[c->nr].hb = hb; c->r[c->nr].dash = dash; c->r[c->nr].label = label;
            c->nr++;
        }
    }
}

int dg_er(ExDiagram *d, DgLines *L) {
    EC c;
    LgGraph g;
    const char *line;
    size_t ln;
    int i, j, k;
    memset(&c, 0, sizeof c);
    memset(&g, 0, sizeof g);
    c.d = d; c.in_body = -1;
    while (!d->has_err && dg_next_line(L, &line, &ln)) statement(&c, line, ln);
    if (!d->has_err && c.in_body >= 0) dg_fail(d, "an entity body is not closed with }");
    if (!d->has_err && c.ne == 0) d->w = d->h = 2.0f;
    if (d->has_err || c.ne == 0) goto done;
    for (i = 0; i < c.ne; i++) {
        EEnt *en = &c.e[i];
        const char *name = en->label ? en->label : en->id;
        float tw, th, cols = 0;
        dg_text_size(d, name, strlen(name), 1.0f, &tw, &th);
        for (k = 0; k < 4; k++) en->cw[k] = 0;
        for (j = 0; j < en->na; j++) {
            float w;
            const char *f[4];
            f[0] = en->a[j].type; f[1] = en->a[j].name; f[2] = en->a[j].keys; f[3] = en->a[j].comment;
            for (k = 0; k < 4; k++) if (f[k]) { dg_text_size(d, f[k], strlen(f[k]), 0.95f, &w, NULL); if (w > en->cw[k]) en->cw[k] = w; }
        }
        for (k = 0; k < 4; k++) if (en->cw[k] > 0) cols += en->cw[k] + (cols > 0 ? 1.0f : 0.0f);
        en->w = (tw + 2.0f > cols + 1.6f ? tw + 2.0f : cols + 1.6f);
        if (en->w < 5.0f) en->w = 5.0f;
        en->h = th + 1.0f + (en->na ? (float)en->na * 1.35f + 0.3f : 0.0f);
        if (en->h < 2.2f) en->h = 2.2f;
    }
    g.nn = c.ne; g.ne = c.nr; g.dir = c.dir; g.nodesep = 3.0f; g.ranksep = 3.6f;
    g.nodes = calloc((size_t)c.ne, sizeof *g.nodes);
    g.edges = calloc((size_t)(c.nr ? c.nr : 1), sizeof *g.edges);
    if (!g.nodes || !g.edges) { dg_fail(d, "out of memory"); goto done; }
    for (i = 0; i < c.ne; i++) { g.nodes[i].w = c.e[i].w; g.nodes[i].h = c.e[i].h; }
    for (i = 0; i < c.nr; i++) {
        g.edges[i].a = c.r[i].a; g.edges[i].b = c.r[i].b; g.edges[i].minlen = 1;
        if (c.r[i].label) { float tw, th; dg_text_size(d, c.r[i].label, strlen(c.r[i].label), 0.9f, &tw, &th); g.edges[i].lw = tw + 0.6f; g.edges[i].lh = th + 0.3f; }
    }
    if (lg_layout(&g)) { dg_fail(d, "the diagram is too large to lay out"); goto done; }
    for (i = 0; i < c.nr; i++) {
        dg_edge_line(d, &g, i, EXD_RECT, EXD_RECT, EXD_C_LINE, c.r[i].dash, c.r[i].ha, c.r[i].hb);
        if (c.r[i].label) dg_edge_label(d, g.edges[i].lx, g.edges[i].ly, c.r[i].label, 0.9f);
    }
    for (i = 0; i < c.ne; i++) {
        const EEnt *en = &c.e[i];
        const char *name = en->label ? en->label : en->id;
        float x = g.nodes[i].x - en->w / 2.0f, y = g.nodes[i].y - en->h / 2.0f, tw, th, yy;
        dg_text_size(d, name, strlen(name), 1.0f, &tw, &th);
        dg_box(d, EXD_RECT, x, y, en->w, en->h, EXD_C_NODE_FILL, EXD_C_NODE_STROKE);
        dg_label(d, x + en->w / 2.0f, y + 0.5f + dg_line_height() / 2.0f, name, strlen(name), 0, 1.0f, EXD_C_TEXT, 1);
        yy = y + th + 1.0f;
        if (en->na) {
            float ln2[4];
            ln2[0] = x; ln2[1] = yy; ln2[2] = x + en->w; ln2[3] = yy;
            dg_line(d, ln2, 2, EXD_C_NODE_STROKE, 0, 0, 0);
        }
        for (j = 0; j < en->na; j++) {
            float cx0 = x + 0.8f, ry = yy + 0.15f + (float)j * 1.35f + 0.675f;
            const char *f[4];
            f[0] = en->a[j].type; f[1] = en->a[j].name; f[2] = en->a[j].keys; f[3] = en->a[j].comment;
            for (k = 0; k < 4; k++) if (en->cw[k] > 0) {
                if (f[k]) dg_label(d, cx0, ry, f[k], strlen(f[k]), 1, 0.95f, k == 1 ? EXD_C_TEXT : EXD_C_MUTED, 0);
                cx0 += en->cw[k] + 1.0f;
            }
        }
    }
    lg_free(&g);
    dg_finalize(d, 0.8f);
done:
    free(g.nodes); free(g.edges);
    for (i = 0; i < c.ne; i++) {
        for (j = 0; j < c.e[i].na; j++) { free(c.e[i].a[j].type); free(c.e[i].a[j].name); free(c.e[i].a[j].keys); free(c.e[i].a[j].comment); }
        free(c.e[i].a); free(c.e[i].id); free(c.e[i].label);
    }
    for (i = 0; i < c.nr; i++) free(c.r[i].label);
    free(c.e); free(c.r);
    return d->has_err ? -1 : 0;
}
