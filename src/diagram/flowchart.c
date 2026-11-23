/* Mermaid flowcharts (6d-ii): `flowchart TD`, `graph LR` and the rest of that family: node shapes, link styles with labels and heads, chains and `&`,
 * subgraphs. Class, style, click and link-style lines are read and ignored. The layout is the layered one of layered.c. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

typedef struct FNode { char *id, *label; int shape; int sub; float w, h; } FNode;
typedef struct FEdge { int a, b, style, hend, hstart; char *label; } FEdge;
typedef struct FSub { char *id, *title; int parent, depth; float x0, y0, x1, y1; int has; } FSub;
typedef struct FC {
    ExDiagram *d;
    FNode *n; int nn, capn;
    FEdge *e; int ne, cape;
    FSub *s; int ns, caps;
    int cur_sub, dir;
} FC;

#define SH_RECT EXD_RECT

static int is_id_char(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }

static int find_node(FC *c, const char *id, size_t n) {
    int i;
    for (i = 0; i < c->nn; i++) if (strlen(c->n[i].id) == n && !memcmp(c->n[i].id, id, n)) return i;
    return -1;
}

static int node_for(FC *c, const char *id, size_t n) {
    int i = find_node(c, id, n);
    if (i >= 0) return i;
    if (c->nn >= DG_MAX_NODES) { dg_fail(c->d, "more than %d nodes", DG_MAX_NODES); return -1; }
    if (c->nn == c->capn) {
        int nc = c->capn ? c->capn * 2 : 16;
        FNode *t = realloc(c->n, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return -1; }
        c->n = t; c->capn = nc;
    }
    memset(&c->n[c->nn], 0, sizeof c->n[0]);
    c->n[c->nn].id = dg_strndup(id, n);
    c->n[c->nn].shape = SH_RECT;
    c->n[c->nn].sub = -1;
    return c->nn++;
}

static void skip_ws(const char *s, size_t n, size_t *p) { while (*p < n && (s[*p] == ' ' || s[*p] == '\t')) (*p)++; }

static const struct { const char *open, *close, *close2; int shape; } shapes[] = {
    {"(((", ")))", NULL, EXD_DOUBLE_CIRCLE}, {"((", "))", NULL, EXD_ELLIPSE}, {"([", "])", NULL, EXD_STADIUM}, {"[[", "]]", NULL, EXD_SUBROUTINE},
    {"[(", ")]", NULL, EXD_CYLINDER}, {"{{", "}}", NULL, EXD_HEXAGON}, {"[/", "/]", "\\]", EXD_PARALLELOGRAM}, {"[\\", "\\]", "/]", EXD_PARALLELOGRAM_ALT},
    {"{", "}", NULL, EXD_DIAMOND}, {"(", ")", NULL, EXD_ROUND}, {"[", "]", NULL, EXD_RECT}, {">", "]", NULL, EXD_FLAG},
};

/* one node reference: id, an optional shape with its text, an optional :::class */
static int parse_node(FC *c, const char *s, size_t n, size_t *pos) {
    size_t p = *pos, st = p;
    int idx;
    size_t k;
    while (p < n && is_id_char((unsigned char)s[p])) p++;
    if (p == st) { dg_fail(c->d, "a node was expected: %.*s", (int)(n - st > 24 ? 24 : n - st), s + st); return -1; }
    idx = node_for(c, s + st, p - st);
    if (idx < 0) return -1;
    for (k = 0; k < sizeof shapes / sizeof shapes[0]; k++) {
        size_t ol = strlen(shapes[k].open);
        if (n - p >= ol && !strncmp(s + p, shapes[k].open, ol)) {
            size_t q = p + ol, text0 = q, text1 = 0;
            int in_quote = 0, found = 0, which = 1;
            while (q < n) {
                if (s[q] == '"') in_quote = !in_quote;
                if (!in_quote) {
                    size_t cl = strlen(shapes[k].close);
                    if (n - q >= cl && !strncmp(s + q, shapes[k].close, cl)) { text1 = q; q += cl; found = 1; which = 1; break; }
                    if (shapes[k].close2) { size_t c2 = strlen(shapes[k].close2); if (n - q >= c2 && !strncmp(s + q, shapes[k].close2, c2)) { text1 = q; q += c2; found = 1; which = 2; break; } }
                }
                q++;
            }
            if (!found) { dg_fail(c->d, "a shape is not closed: %.*s", (int)(n - st > 30 ? 30 : n - st), s + st); return -1; }
            free(c->n[idx].label);
            c->n[idx].label = dg_clean_label(s + text0, text1 - text0);
            c->n[idx].shape = shapes[k].shape;
            if (which == 2) c->n[idx].shape = shapes[k].shape == EXD_PARALLELOGRAM ? EXD_TRAPEZOID : EXD_TRAPEZOID_ALT;   /* [/ text \] and [\ text /] */
            p = q;
            break;
        }
    }
    if (n - p >= 3 && !strncmp(s + p, ":::", 3)) { p += 3; while (p < n && is_id_char((unsigned char)s[p])) p++; }
    if (c->cur_sub >= 0 && c->n[idx].sub < 0) c->n[idx].sub = c->cur_sub;
    *pos = p;
    return idx;
}

static int add_edge(FC *c, int a, int b, int style, int hs, int he, const char *label) {
    FEdge *e;
    if (c->ne >= DG_MAX_NODES * 4) { dg_fail(c->d, "too many links"); return -1; }
    if (c->ne == c->cape) {
        int nc = c->cape ? c->cape * 2 : 16;
        FEdge *t = realloc(c->e, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return -1; }
        c->e = t; c->cape = nc;
    }
    e = &c->e[c->ne++];
    e->a = a; e->b = b; e->style = style; e->hstart = hs; e->hend = he;
    e->label = label && *label ? dg_clean_label(label, strlen(label)) : NULL;
    return 0;
}

static int head_char(const char *s, size_t n, size_t p, int *head) {   /* an arrow head after a link body */
    if (p >= n) return 0;
    if (s[p] == '>') { *head = EXD_H_ARROW; return 1; }
    if ((s[p] == 'o' || s[p] == 'x') && (p + 1 >= n || s[p + 1] == ' ' || s[p + 1] == '|')) { *head = s[p] == 'o' ? EXD_H_CIRCLE : EXD_H_CROSS; return 1; }
    return 0;
}

/* a link; returns 0 on success with its parts, -1 when no link starts here */
static int parse_link(const char *s, size_t n, size_t *pos, int *style, int *hs, int *he, char **label) {
    size_t p = *pos, tl0 = 0, tl1 = 0;
    int has_text = 0, h = 0;
    *style = 0; *hs = 0; *he = 0; *label = NULL;
    if (p >= n) return -1;
    if (s[p] == '<') { *hs = EXD_H_ARROW; p++; }
    else if ((s[p] == 'o' || s[p] == 'x') && p + 1 < n && (s[p + 1] == '-' || s[p + 1] == '=')) { *hs = s[p] == 'o' ? EXD_H_CIRCLE : EXD_H_CROSS; p++; }
    if (p + 2 < n + 0 && s[p] == '~' && s[p + 1] == '~' && s[p + 2] == '~') { *style = 3; while (p < n && s[p] == '~') p++; }
    else if (p + 1 < n && s[p] == '=' && s[p + 1] == '=') {
        *style = 2; p += 2;
        if (p < n && s[p] == ' ') {
            size_t q = p;
            while (q + 2 < n + 0 && !(s[q] == '=' && s[q + 1] == '=' && (s[q + 2] == '>' || s[q + 2] == '=' || s[q + 2] == 'o' || s[q + 2] == 'x'))) q++;
            if (q + 2 >= n + 0 + 0 && !(q + 2 < n)) return -1;
            tl0 = p; tl1 = q; has_text = 1; p = q + 2;
            while (p < n && s[p] == '=') p++;
        } else while (p < n && s[p] == '=') p++;
        if (head_char(s, n, p, &h)) { *he = h; p++; }
    } else if (p + 1 < n && s[p] == '-' && s[p + 1] == '.') {
        *style = 1; p += 2;
        if (p < n && s[p] == ' ') {
            size_t q = p;
            while (q + 1 < n && !(s[q] == '.' && s[q + 1] == '-')) q++;
            if (q + 1 >= n) return -1;
            tl0 = p; tl1 = q; has_text = 1; p = q + 2;
        } else {
            while (p < n && s[p] == '.') p++;
            if (p >= n || s[p] != '-') return -1;
            p++;
        }
        if (p < n && s[p] == '>') { *he = EXD_H_ARROW; p++; }
    } else if (p + 1 < n && s[p] == '-' && s[p + 1] == '-') {
        p += 2;
        if (p < n && s[p] == ' ') {
            size_t q = p;
            while (q + 2 < n + 0 && !(s[q] == '-' && s[q + 1] == '-' && (s[q + 2] == '>' || s[q + 2] == '-' || s[q + 2] == 'o' || s[q + 2] == 'x'))) q++;
            if (!(q + 2 < n)) return -1;
            tl0 = p; tl1 = q; has_text = 1; p = q + 2;
            while (p < n && s[p] == '-') p++;
        } else while (p < n && s[p] == '-') p++;
        if (head_char(s, n, p, &h)) { *he = h; p++; }
    } else return -1;
    if (has_text) *label = dg_strndup(s + tl0, tl1 - tl0);
    skip_ws(s, n, &p);
    if (p < n && s[p] == '|') {
        size_t q = p + 1;
        while (q < n && s[q] != '|') q++;
        if (q < n) { free(*label); *label = dg_strndup(s + p + 1, q - p - 1); p = q + 1; }
    }
    *pos = p;
    return 0;
}

static int starts_kw(const char *s, size_t n, const char *kw) {
    size_t l = strlen(kw);
    return n >= l && !strncmp(s, kw, l) && (n == l || s[l] == ' ' || s[l] == '\t');
}

static void do_subgraph(FC *c, const char *s, size_t n) {
    size_t p = 8;
    char *id, *title;
    FSub *sb;
    skip_ws(s, n, &p);
    if (c->ns == c->caps) {
        int nc = c->caps ? c->caps * 2 : 8;
        FSub *t = realloc(c->s, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return; }
        c->s = t; c->caps = nc;
    }
    {   /* `id [title]` or a title of its own */
        size_t q = p;
        while (q < n && is_id_char((unsigned char)s[q])) q++;
        if (q > p && q < n && (s[q] == '[' || (s[q] == ' ' && q + 1 < n && s[q + 1] == '['))) {
            size_t b = q;
            while (b < n && s[b] != '[') b++;
            {
                size_t e = n;
                while (e > b && s[e - 1] != ']') e--;
                id = dg_strndup(s + p, q - p);
                title = e > b + 1 ? dg_clean_label(s + b + 1, e - 1 - (b + 1)) : dg_strndup(id, strlen(id));
            }
        } else {
            title = dg_clean_label(s + p, n - p);
            id = dg_strndup(s + p, n - p);
        }
    }
    sb = &c->s[c->ns++];
    memset(sb, 0, sizeof *sb);
    sb->id = id; sb->title = title; sb->parent = c->cur_sub;
    sb->depth = c->cur_sub >= 0 ? c->s[c->cur_sub].depth + 1 : 0;
    c->cur_sub = c->ns - 1;
}

/* nodes joined by & */
static int parse_group(FC *c, const char *s, size_t n, size_t *p, int **grp, int *ng) {
    int cap = 0, idx;
    *grp = NULL; *ng = 0;
    for (;;) {
        skip_ws(s, n, p);
        idx = parse_node(c, s, n, p);
        if (idx < 0) { free(*grp); *grp = NULL; return -1; }
        if (*ng == cap) {
            int *t = realloc(*grp, (size_t)(cap ? cap * 2 : 4) * sizeof(int));
            if (!t) { free(*grp); *grp = NULL; dg_fail(c->d, "out of memory"); return -1; }
            *grp = t; cap = cap ? cap * 2 : 4;
        }
        (*grp)[(*ng)++] = idx;
        skip_ws(s, n, p);
        if (*p < n && s[*p] == '&') { (*p)++; continue; }
        return 0;
    }
}

static void do_statement(FC *c, const char *s, size_t n) {
    size_t p = 0;
    int *prev = NULL, nprev = 0;
    skip_ws(s, n, &p);
    if (p >= n) return;
    s += p; n -= p; p = 0;
    if (starts_kw(s, n, "subgraph")) { do_subgraph(c, s, n); return; }
    if (n == 3 && !strncmp(s, "end", 3)) { if (c->cur_sub >= 0) c->cur_sub = c->s[c->cur_sub].parent; return; }
    if (starts_kw(s, n, "direction") || starts_kw(s, n, "classDef") || starts_kw(s, n, "class") || starts_kw(s, n, "style") || starts_kw(s, n, "linkStyle") ||
        starts_kw(s, n, "click") || starts_kw(s, n, "accTitle") || starts_kw(s, n, "accDescr") || starts_kw(s, n, "title")) return;
    if (parse_group(c, s, n, &p, &prev, &nprev)) return;
    for (;;) {
        int style, hs, he, *grp, ng, i, j;
        char *label;
        skip_ws(s, n, &p);
        if (p >= n) break;
        if (parse_link(s, n, &p, &style, &hs, &he, &label)) { dg_fail(c->d, "a link was expected: %.*s", (int)(n - p > 24 ? 24 : n - p), s + p); break; }
        skip_ws(s, n, &p);
        if (parse_group(c, s, n, &p, &grp, &ng)) { free(label); break; }
        for (i = 0; i < nprev && !c->d->has_err; i++) for (j = 0; j < ng && !c->d->has_err; j++) add_edge(c, prev[i], grp[j], style, hs, he, label);
        free(label);
        free(prev);
        prev = grp; nprev = ng;
    }
    free(prev);
}

/* where a ray from the centre of a node towards (tx, ty) leaves its outline */
static void clip(const FNode *nd, float cx, float cy, float tx, float ty, float *ox, float *oy) {
    float dx = tx - cx, dy = ty - cy, hw = nd->w / 2.0f, hh = nd->h / 2.0f, t;
    if (dx == 0 && dy == 0) { *ox = cx; *oy = cy; return; }
    switch (nd->shape) {
    case EXD_ELLIPSE: case EXD_DOUBLE_CIRCLE: t = 1.0f / sqrtf(dx * dx / (hw * hw) + dy * dy / (hh * hh)); break;
    case EXD_DIAMOND: t = 1.0f / (fabsf(dx) / hw + fabsf(dy) / hh); break;
    default: { float a = fabsf(dx) / hw, b = fabsf(dy) / hh; t = 1.0f / (a > b ? a : b); break; }
    }
    *ox = cx + dx * t; *oy = cy + dy * t;
}

/* the y of the middle of the first line of a text centred on cy */
static float first_line_y(float cy, const char *s, float size) {
    int lines = 1;
    for (; *s; s++) if (*s == '\n') lines++;
    return cy - (float)(lines - 1) * dg_line_height() * size / 2.0f;
}

static void size_node(FC *c, FNode *nd) {
    float tw, th, w, h;
    const char *lab = nd->label ? nd->label : nd->id;
    dg_text_size(c->d, lab, strlen(lab), 1.0f, &tw, &th);
    w = tw + 1.6f; h = th + 1.0f;
    switch (nd->shape) {
    case EXD_DIAMOND: w = 1.6f * tw + 1.8f; h = 1.6f * th + 1.2f; break;
    case EXD_ELLIPSE: case EXD_DOUBLE_CIRCLE: { float r = (tw + 1.2f > th + 1.4f ? tw + 1.2f : th + 1.4f); w = r; h = r; if (nd->shape == EXD_DOUBLE_CIRCLE) { w += 0.6f; h += 0.6f; } break; }
    case EXD_STADIUM: w += th * 0.7f; break;
    case EXD_HEXAGON: case EXD_PARALLELOGRAM: case EXD_PARALLELOGRAM_ALT: case EXD_TRAPEZOID: case EXD_TRAPEZOID_ALT: w += 1.4f; break;
    case EXD_CYLINDER: h += 1.0f; break;
    case EXD_SUBROUTINE: w += 0.8f; break;
    case EXD_FLAG: w += 1.0f; break;
    default: break;
    }
    if (w < 2.6f) w = 2.6f;
    if (h < 1.9f) h = 1.9f;
    nd->w = w; nd->h = h;
}

static void free_all(FC *c) {
    int i;
    for (i = 0; i < c->nn; i++) { free(c->n[i].id); free(c->n[i].label); }
    for (i = 0; i < c->ne; i++) free(c->e[i].label);
    for (i = 0; i < c->ns; i++) { free(c->s[i].id); free(c->s[i].title); }
    free(c->n); free(c->e); free(c->s);
}

int dg_flowchart(ExDiagram *d, DgLines *L, const char *first, size_t flen) {
    FC c;
    LgGraph g;
    const char *line;
    size_t ln, p = 0;
    int i;
    memset(&c, 0, sizeof c);
    memset(&g, 0, sizeof g);
    c.d = d; c.cur_sub = -1;
    while (p < flen && first[p] != ' ' && first[p] != '\t' && first[p] != ';') p++;     /* flowchart | graph */
    skip_ws(first, flen, &p);
    if (p < flen) {
        size_t q = p;
        while (q < flen && first[q] != ' ' && first[q] != ';') q++;
        if (q - p == 2 && (!strncmp(first + p, "TD", 2) || !strncmp(first + p, "TB", 2))) c.dir = 0;
        else if (q - p == 2 && !strncmp(first + p, "BT", 2)) c.dir = 1;
        else if (q - p == 2 && !strncmp(first + p, "LR", 2)) c.dir = 2;
        else if (q - p == 2 && !strncmp(first + p, "RL", 2)) c.dir = 3;
        else if (q - p == 2 && !strncmp(first + p, "V ", 2)) c.dir = 0;
        else if (q > p) { dg_fail(d, "unknown direction: %.*s", (int)(q - p > 12 ? 12 : q - p), first + p); free_all(&c); return -1; }
    }
    {   /* statements after a ; on the type line */
        size_t semi = 0;
        while (semi < flen && first[semi] != ';') semi++;
        if (semi < flen) {
            size_t a2 = semi + 1;
            line = first; ln = flen;
            while (a2 <= ln && !d->has_err) {
                size_t b2 = a2;
                int depth = 0, quote = 0;
                while (b2 < ln) {
                    if (line[b2] == '"') quote = !quote;
                    else if (!quote) { if (line[b2] == '[' || line[b2] == '(' || line[b2] == '{') depth++; else if (line[b2] == ']' || line[b2] == ')' || line[b2] == '}') depth--; else if (line[b2] == ';' && depth <= 0) break; }
                    b2++;
                }
                do_statement(&c, line + a2, b2 - a2);
                a2 = b2 + 1;
            }
        }
    }
    while (!d->has_err && dg_next_line(L, &line, &ln)) {
        size_t a = 0;
        for (a = 0; a <= ln && !d->has_err; ) {   /* statements are separated by ; outside quotes and brackets */
            size_t b = a;
            int depth = 0, quote = 0;
            while (b < ln) {
                if (line[b] == '"') quote = !quote;
                else if (!quote) { if (line[b] == '[' || line[b] == '(' || line[b] == '{') depth++; else if (line[b] == ']' || line[b] == ')' || line[b] == '}') depth--; else if (line[b] == ';' && depth <= 0) break; }
                b++;
            }
            do_statement(&c, line + a, b - a);
            a = b + 1;
        }
    }
    if (d->has_err) { free_all(&c); return -1; }
    if (c.nn == 0) { d->w = d->h = 2.0f; free_all(&c); return 0; }
    /* sizes and the layout */
    for (i = 0; i < c.nn; i++) size_node(&c, &c.n[i]);
    g.nn = c.nn; g.ne = c.ne; g.dir = c.dir; g.nodesep = 1.6f; g.ranksep = 1.9f;
    g.nodes = calloc((size_t)c.nn, sizeof *g.nodes);
    g.edges = calloc((size_t)(c.ne ? c.ne : 1), sizeof *g.edges);
    if (!g.nodes || !g.edges) { dg_fail(d, "out of memory"); free(g.nodes); free(g.edges); free_all(&c); return -1; }
    for (i = 0; i < c.nn; i++) { g.nodes[i].w = c.n[i].w; g.nodes[i].h = c.n[i].h; }
    for (i = 0; i < c.ne; i++) {
        g.edges[i].a = c.e[i].a; g.edges[i].b = c.e[i].b; g.edges[i].minlen = 1;
        if (c.e[i].label) { float tw, th; dg_text_size(d, c.e[i].label, strlen(c.e[i].label), 0.9f, &tw, &th); g.edges[i].lw = tw + 0.6f; g.edges[i].lh = th + 0.3f; }
    }
    if (lg_layout(&g)) { dg_fail(d, "the diagram is too large to lay out"); lg_free(&g); free(g.nodes); free(g.edges); free_all(&c); return -1; }
    /* subgraph boxes: nodes first, then every box grows to hold the boxes inside it */
    for (i = 0; i < c.ns; i++) c.s[i].has = 0;
    for (i = 0; i < c.nn; i++) {
        int s = c.n[i].sub;
        while (s >= 0) {
            FSub *sb = &c.s[s];
            float x0 = g.nodes[i].x - c.n[i].w / 2.0f, x1 = g.nodes[i].x + c.n[i].w / 2.0f, y0 = g.nodes[i].y - c.n[i].h / 2.0f, y1 = g.nodes[i].y + c.n[i].h / 2.0f;
            if (!sb->has) { sb->x0 = x0; sb->x1 = x1; sb->y0 = y0; sb->y1 = y1; sb->has = 1; }
            else { if (x0 < sb->x0) sb->x0 = x0; if (x1 > sb->x1) sb->x1 = x1; if (y0 < sb->y0) sb->y0 = y0; if (y1 > sb->y1) sb->y1 = y1; }
            s = sb->parent;
        }
    }
    {
        int depth, k;
        for (depth = 8; depth >= 0; depth--)
            for (k = 0; k < c.ns; k++) {
                FSub *sb = &c.s[k];
                float tw, th;
                if (sb->depth != depth || !sb->has) continue;
                dg_text_size(d, sb->title, strlen(sb->title), 1.0f, &tw, &th);
                sb->x0 -= 1.0f; sb->x1 += 1.0f; sb->y0 -= 2.0f; sb->y1 += 1.0f;
                if (sb->x1 - sb->x0 < tw + 2.0f) { float grow = tw + 2.0f - (sb->x1 - sb->x0); sb->x0 -= grow / 2.0f; sb->x1 += grow / 2.0f; }
                if (sb->parent >= 0) {
                    FSub *pr = &c.s[sb->parent];
                    if (!pr->has) { pr->x0 = sb->x0; pr->x1 = sb->x1; pr->y0 = sb->y0; pr->y1 = sb->y1; pr->has = 1; }
                    else { if (sb->x0 < pr->x0) pr->x0 = sb->x0; if (sb->x1 > pr->x1) pr->x1 = sb->x1; if (sb->y0 < pr->y0) pr->y0 = sb->y0; if (sb->y1 > pr->y1) pr->y1 = sb->y1; }
                }
            }
    }
    /* items, in drawing order: subgraph boxes (outer first), links, nodes, link labels */
    {
        int depth, k;
        for (depth = 0; depth <= 8; depth++)
            for (k = 0; k < c.ns; k++) {
                FSub *sb = &c.s[k];
                if (sb->depth != depth || !sb->has) continue;
                dg_box(d, EXD_ROUND, sb->x0, sb->y0, sb->x1 - sb->x0, sb->y1 - sb->y0, EXD_C_PANEL, EXD_C_NODE_STROKE);
                dg_label(d, sb->x0 + 0.8f, sb->y0 + 1.0f, sb->title, strlen(sb->title), 1, 1.0f, EXD_C_TEXT, 1);
            }
    }
    for (i = 0; i < c.ne; i++) {
        const LgEdge *le = &g.edges[i];
        const FEdge *fe = &c.e[i];
        float *pts;
        int np, k;
        ExDItem *it;
        if (fe->style == 3) continue;
        if (fe->a == fe->b) {   /* a link from a node to itself: a loop on its right */
            const FNode *nd = &c.n[fe->a];
            float cx = g.nodes[fe->a].x, cy = g.nodes[fe->a].y, hw = nd->w / 2.0f, q[8];
            q[0] = cx + hw; q[1] = cy - nd->h * 0.25f; q[2] = cx + hw + 1.0f; q[3] = q[1]; q[4] = q[2]; q[5] = cy + nd->h * 0.25f; q[6] = cx + hw; q[7] = q[5];
            it = dg_line(d, q, 4, EXD_C_LINE, fe->style == 1, fe->hstart, fe->hend ? fe->hend : EXD_H_NONE);
            if (it && fe->style == 2) it->width = 0.2f;
            continue;
        }
        np = le->npts;
        if (np < 2) continue;
        pts = malloc((size_t)np * 2 * sizeof(float));
        if (!pts) continue;
        memcpy(pts, le->pts, (size_t)np * 2 * sizeof(float));
        clip(&c.n[fe->a], pts[0], pts[1], pts[2], pts[3], &pts[0], &pts[1]);
        clip(&c.n[fe->b], pts[2 * (np - 1)], pts[2 * (np - 1) + 1], pts[2 * (np - 2)], pts[2 * (np - 2) + 1], &pts[2 * (np - 1)], &pts[2 * (np - 1) + 1]);
        (void)k;
        it = dg_line(d, pts, (size_t)np, EXD_C_LINE, fe->style == 1 ? 1 : 0, fe->hstart, fe->hend);
        if (it && fe->style == 2) it->width = 0.2f;
        free(pts);
    }
    for (i = 0; i < c.nn; i++) {
        const FNode *nd = &c.n[i];
        const char *lab = nd->label ? nd->label : nd->id;
        dg_box(d, nd->shape, g.nodes[i].x - nd->w / 2.0f, g.nodes[i].y - nd->h / 2.0f, nd->w, nd->h, EXD_C_NODE_FILL, EXD_C_NODE_STROKE);
        dg_label(d, g.nodes[i].x, first_line_y(g.nodes[i].y + (nd->shape == EXD_CYLINDER ? 0.3f : 0.0f), lab, 1.0f), lab, strlen(lab), 0, 1.0f, EXD_C_TEXT, 0);
    }
    for (i = 0; i < c.ne; i++) {
        const FEdge *fe = &c.e[i];
        const LgEdge *le = &g.edges[i];
        float tw, th;
        if (!fe->label || fe->style == 3 || fe->a == fe->b) continue;
        dg_text_size(d, fe->label, strlen(fe->label), 0.9f, &tw, &th);
        dg_box(d, EXD_RECT, le->lx - tw / 2.0f - 0.2f, le->ly - th / 2.0f - 0.1f, tw + 0.4f, th + 0.2f, EXD_C_LABEL_BG, 0);
        dg_label(d, le->lx, first_line_y(le->ly, fe->label, 0.9f), fe->label, strlen(fe->label), 0, 0.9f, EXD_C_MUTED, 0);
    }
    lg_free(&g);
    free(g.nodes); free(g.edges);
    free_all(&c);
    dg_finalize(d, 0.8f);
    return 0;
}
