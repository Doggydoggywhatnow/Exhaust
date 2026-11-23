/* Mermaid state diagrams (6d-iii): `stateDiagram` and `stateDiagram-v2` with states, descriptions, `[*]` start and end points, transitions with labels,
 * composite states (`state X { ... }`, laid out on their own and put in a box in the parent), `<<choice>>`, `<<fork>>` and `<<join>>`, notes and `direction`.
 * A transition between states of different composites is drawn at the level where their composites meet, to the outline of the composite. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

enum { K_STATE, K_START, K_END, K_CHOICE, K_FORK, K_NOTE };
typedef struct SState { char *id, *label; int kind, scope, child; char **desc; int nd, capd; float w, h; ExDiagram *sub; float subw, subh; } SState;
typedef struct STrans { int a, b, dashed, arrow; char *label; } STrans;
typedef struct SScope { int owner, parent; int start, end; } SScope;
typedef struct SC {
    ExDiagram *d;
    SState *s; int ns;
    STrans *t; int nt, capt;
    SScope *sc; int nsc;
    int cur, dir, in_note, note_idx;
    char *note_text; int note_target, note_side;
} SC;

static int is_id_char(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }

static int new_state(SC *c, const char *id, size_t n, int kind) {
    SState *t;
    if (c->ns >= 400) { dg_fail(c->d, "more than 400 states"); return -1; }
    t = realloc(c->s, (size_t)(c->ns + 1) * sizeof *t);
    if (!t) { dg_fail(c->d, "out of memory"); return -1; }
    c->s = t;
    memset(&c->s[c->ns], 0, sizeof c->s[0]);
    c->s[c->ns].id = dg_strndup(id, n);
    c->s[c->ns].kind = kind;
    c->s[c->ns].scope = c->cur;
    c->s[c->ns].child = -1;
    return c->ns++;
}

static int state_for(SC *c, const char *id, size_t n) {
    int i;
    for (i = 0; i < c->ns; i++) if (c->s[i].kind != K_START && c->s[i].kind != K_END && c->s[i].kind != K_NOTE && strlen(c->s[i].id) == n && !memcmp(c->s[i].id, id, n)) return i;
    return new_state(c, id, n, K_STATE);
}

static int point_for(SC *c, int end) {   /* the [*] of the current scope: one start and one end each */
    SScope *sc = &c->sc[c->cur];
    int *slot = end ? &sc->end : &sc->start;
    if (*slot < 0) *slot = new_state(c, end ? "[*]e" : "[*]s", 4, end ? K_END : K_START);
    return *slot;
}

static int new_scope(SC *c, int owner) {
    SScope *t = realloc(c->sc, (size_t)(c->nsc + 1) * sizeof *t);
    if (!t) { dg_fail(c->d, "out of memory"); return -1; }
    c->sc = t;
    c->sc[c->nsc].owner = owner; c->sc[c->nsc].parent = c->cur; c->sc[c->nsc].start = c->sc[c->nsc].end = -1;
    return c->nsc++;
}

static void add_trans(SC *c, int a, int b, const char *label, size_t ln, int dashed, int arrow) {
    STrans *t;
    if (c->nt == c->capt) {
        int nc = c->capt ? c->capt * 2 : 16;
        STrans *tt = realloc(c->t, (size_t)nc * sizeof *tt);
        if (!tt) { dg_fail(c->d, "out of memory"); return; }
        c->t = tt; c->capt = nc;
    }
    t = &c->t[c->nt++];
    t->a = a; t->b = b; t->dashed = dashed; t->arrow = arrow;
    t->label = label && ln ? dg_clean_label(label, ln) : NULL;
}

static void add_desc(SState *st, const char *s, size_t n) {
    char *m;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    if (!n) return;
    m = dg_clean_label(s, n);
    if (!m) return;
    if (st->nd == st->capd) { int nc = st->capd ? st->capd * 2 : 4; char **t = realloc(st->desc, (size_t)nc * sizeof *t); if (!t) { free(m); return; } st->desc = t; st->capd = nc; }
    st->desc[st->nd++] = m;
}

/* whether w occurs in the first n bytes (the line is not NUL-terminated) */
static int has_word(const char *s, size_t n, const char *w) {
    size_t l = strlen(w), i;
    for (i = 0; i + l <= n; i++) if (!memcmp(s + i, w, l)) return 1;
    return 0;
}

static int kw(const char *s, size_t n, const char *w) { size_t l = strlen(w); return n >= l && !strncmp(s, w, l) && (n == l || s[l] == ' ' || s[l] == '\t' || s[l] == '{'); }

static void note(SC *c, int target, int side, const char *text, size_t n) {
    int idx;
    char id[24];
    (void)side;
    snprintf(id, sizeof id, "note%d", c->ns);
    idx = new_state(c, id, strlen(id), K_NOTE);
    if (idx < 0) return;
    c->s[idx].label = dg_clean_label(text, n);
    c->s[idx].scope = c->s[target].scope;
    add_trans(c, target, idx, NULL, 0, 1, 0);
}

static void statement(SC *c, const char *s, size_t n) {
    size_t p = 0, st;
    int a, b;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (!n) return;
    if (c->in_note) {
        if (kw(s, n, "end") && n >= 8 && !strncmp(s, "end note", 8)) {
            if (c->note_text) { note(c, c->note_target, c->note_side, c->note_text, strlen(c->note_text)); free(c->note_text); c->note_text = NULL; }
            c->in_note = 0;
            return;
        }
        {   /* the lines of a note are joined with line breaks */
            size_t old = c->note_text ? strlen(c->note_text) : 0;
            char *t = realloc(c->note_text, old + n + 2);
            if (!t) { dg_fail(c->d, "out of memory"); return; }
            if (old) t[old++] = '\n';
            memcpy(t + old, s, n);
            t[old + n] = 0;
            c->note_text = t;
        }
        return;
    }
    if (kw(s, n, "stateDiagram") || kw(s, n, "stateDiagram-v2")) return;
    if (kw(s, n, "direction")) {
        size_t q = 9;
        while (q < n && s[q] == ' ') q++;
        if (n - q >= 2 && c->cur == 0) { if (!strncmp(s + q, "LR", 2)) c->dir = 2; else if (!strncmp(s + q, "RL", 2)) c->dir = 3; else if (!strncmp(s + q, "BT", 2)) c->dir = 1; else if (!strncmp(s + q, "TB", 2) || !strncmp(s + q, "TD", 2)) c->dir = 0; else dg_fail(c->d, "unknown direction"); }
        return;
    }
    if (kw(s, n, "classDef") || kw(s, n, "class") || kw(s, n, "style") || kw(s, n, "click") || kw(s, n, "accTitle") || kw(s, n, "accDescr") || kw(s, n, "hide") || kw(s, n, "scale") || (n == 2 && s[0] == '-' && s[1] == '-')) return;
    if (n == 1 && s[0] == '}') {
        if (c->cur == 0) { dg_fail(c->d, "} without a composite state"); return; }
        c->cur = c->sc[c->cur].parent;
        return;
    }
    if (kw(s, n, "note")) {
        size_t q = 4;
        int side = 0, target;
        while (q < n && s[q] == ' ') q++;
        if (n - q >= 5 && !strncmp(s + q, "right", 5)) { side = 1; q += 5; } else if (n - q >= 4 && !strncmp(s + q, "left", 4)) { side = 0; q += 4; } else { dg_fail(c->d, "a note is put right of or left of a state"); return; }
        while (q < n && s[q] == ' ') q++;
        if (n - q >= 2 && !strncmp(s + q, "of", 2)) q += 2;
        while (q < n && s[q] == ' ') q++;
        st = q;
        while (q < n && is_id_char((unsigned char)s[q])) q++;
        if (q == st) { dg_fail(c->d, "a note needs a state"); return; }
        target = state_for(c, s + st, q - st);
        if (target < 0) return;
        while (q < n && s[q] == ' ') q++;
        if (q < n && s[q] == ':') { note(c, target, side, s + q + 1, n - q - 1); return; }
        c->in_note = 1; c->note_target = target; c->note_side = side; c->note_text = NULL;
        return;
    }
    if (kw(s, n, "state")) {
        size_t q = 5;
        char *desc = NULL;
        int idx;
        while (q < n && s[q] == ' ') q++;
        if (q < n && s[q] == '"') {
            size_t e = q + 1;
            while (e < n && s[e] != '"') e++;
            if (e >= n) { dg_fail(c->d, "a state name is not closed"); return; }
            desc = dg_clean_label(s + q + 1, e - q - 1);
            q = e + 1;
            while (q < n && s[q] == ' ') q++;
            if (n - q >= 2 && !strncmp(s + q, "as", 2)) q += 2;
            while (q < n && s[q] == ' ') q++;
        }
        st = q;
        while (q < n && is_id_char((unsigned char)s[q])) q++;
        if (q == st) { dg_fail(c->d, "a state name was expected"); free(desc); return; }
        idx = state_for(c, s + st, q - st);
        if (idx < 0) { free(desc); return; }
        if (desc) { free(c->s[idx].label); c->s[idx].label = desc; }
        while (q < n && s[q] == ' ') q++;
        if (n - q >= 2 && s[q] == '<' && s[q + 1] == '<') {
            if (has_word(s + q, n - q, "choice")) c->s[idx].kind = K_CHOICE; else if (has_word(s + q, n - q, "fork") || has_word(s + q, n - q, "join")) c->s[idx].kind = K_FORK;
            else dg_fail(c->d, "unknown state type: %.*s", (int)(n - q > 20 ? 20 : n - q), s + q);
            return;
        }
        if (q < n && s[q] == '{') {
            int sc = new_scope(c, idx);
            if (sc < 0) return;
            c->s[idx].child = sc;
            c->cur = sc;
        }
        return;
    }
    /* transitions and descriptions */
    st = p;
    if (n >= 3 && !strncmp(s, "[*]", 3)) { p = 3; a = -2; }
    else {
        while (p < n && is_id_char((unsigned char)s[p])) p++;
        if (p == st) { dg_fail(c->d, "unknown line: %.*s", (int)(n > 30 ? 30 : n), s); return; }
        a = state_for(c, s + st, p - st);
        if (a < 0) return;
    }
    while (p < n && s[p] == ' ') p++;
    if (p < n && s[p] == ':') { if (a < 0) { dg_fail(c->d, "[*] has no description"); return; } add_desc(&c->s[a], s + p + 1, n - p - 1); return; }
    if (p + 3 <= n && !strncmp(s + p, "-->", 3)) {
        size_t q = p + 3;
        while (q < n && s[q] == ' ') q++;
        st = q;
        if (n - q >= 3 && !strncmp(s + q, "[*]", 3)) { q += 3; b = -2; }
        else {
            while (q < n && is_id_char((unsigned char)s[q])) q++;
            if (q == st) { dg_fail(c->d, "a state was expected after -->"); return; }
            b = state_for(c, s + st, q - st);
            if (b < 0) return;
        }
        if (a == -2) { if (b == -2) { dg_fail(c->d, "[*] --> [*] is not a transition"); return; } a = point_for(c, 0); }
        if (b == -2) b = point_for(c, 1);
        if (a < 0 || b < 0) return;
        while (q < n && s[q] == ' ') q++;
        if (q < n && s[q] == ':') add_trans(c, a, b, s + q + 1, n - q - 1, 0, 1);
        else if (q < n) { dg_fail(c->d, "unexpected text after the transition: %.*s", (int)(n - q > 20 ? 20 : n - q), s + q); return; }
        else add_trans(c, a, b, NULL, 0, 0, 1);
        return;
    }
    if (a >= 0 && p >= n) return;      /* a state named alone */
    dg_fail(c->d, "unknown line: %.*s", (int)(n > 30 ? 30 : n), s);
}

/* the state of scope `sc` that holds state `s` (or is it), or -1 */
static int lift(const SC *c, int s, int sc) {
    while (s >= 0) {
        if (c->s[s].scope == sc) return s;
        s = c->sc[c->s[s].scope].owner;
    }
    return -1;
}

static int layout_scope(SC *c, int scope, ExDiagram *out, int dir);

static void size_state(SC *c, SState *st, int dir) {
    const char *name = st->label ? st->label : st->id;
    float tw, th, w, h;
    switch (st->kind) {
    case K_START: st->w = st->h = 1.1f; return;
    case K_END: st->w = st->h = 1.5f; return;
    case K_CHOICE: st->w = st->h = 1.9f; return;
    case K_FORK: if (dir == 2 || dir == 3) { st->w = 0.4f; st->h = 5.0f; } else { st->w = 5.0f; st->h = 0.4f; } return;
    case K_NOTE: dg_text_size(c->d, name, strlen(name), 0.95f, &tw, &th); st->w = tw + 1.4f; st->h = th + 0.8f; return;
    default: break;
    }
    dg_text_size(c->d, name, strlen(name), 1.0f, &tw, &th);
    w = tw + 1.8f; h = th + 1.0f;
    if (st->nd) {
        int i;
        for (i = 0; i < st->nd; i++) { float dw; dg_text_size(c->d, st->desc[i], strlen(st->desc[i]), 0.95f, &dw, NULL); if (dw + 1.8f > w) w = dw + 1.8f; }
        h = th + 0.9f + (float)st->nd * 1.25f * 0.95f + 0.7f;
    }
    if (w < 3.0f) w = 3.0f;
    if (h < 2.0f) h = 2.0f;
    st->w = w; st->h = h;
}

static int layout_scope(SC *c, int scope, ExDiagram *out, int dir) {
    int *idx = NULL, nn = 0, i, e, ne = 0;
    int *map = NULL;
    LgGraph g;
    STrans *tr = NULL;
    int *ta = NULL, *tb = NULL;
    int rc = -1;
    memset(&g, 0, sizeof g);
    idx = malloc((size_t)(c->ns ? c->ns : 1) * sizeof(int));
    map = malloc((size_t)(c->ns ? c->ns : 1) * sizeof(int));
    if (!idx || !map) { dg_fail(c->d, "out of memory"); goto done; }
    for (i = 0; i < c->ns; i++) { map[i] = -1; if (c->s[i].scope == scope) { map[i] = nn; idx[nn++] = i; } }
    for (i = 0; i < nn; i++) {
        SState *st = &c->s[idx[i]];
        if (st->child >= 0) {   /* a composite: its inside first */
            ExDiagram *sub = calloc(1, sizeof *sub);
            if (!sub) { dg_fail(c->d, "out of memory"); goto done; }
            sub->font = c->d->font;
            if (layout_scope(c, st->child, sub, dir)) { ex_diagram_free(sub); goto done; }
            st->sub = sub;
            {
                float tw, th;
                const char *name = st->label ? st->label : st->id;
                dg_text_size(c->d, name, strlen(name), 1.0f, &tw, &th);
                st->subw = sub->w; st->subh = sub->h;
                st->w = (sub->w > tw + 2.0f ? sub->w : tw + 2.0f) + 0.4f; st->h = sub->h + 2.2f;
            }
        } else size_state(c, st, dir);
    }
    /* the transitions drawn at this level */
    ta = malloc((size_t)(c->nt ? c->nt : 1) * sizeof(int)); tb = malloc((size_t)(c->nt ? c->nt : 1) * sizeof(int)); tr = malloc((size_t)(c->nt ? c->nt : 1) * sizeof(STrans));
    if (!ta || !tb || !tr) { dg_fail(c->d, "out of memory"); goto done; }
    for (i = 0; i < c->nt; i++) {
        int la = lift(c, c->t[i].a, scope), lb = lift(c, c->t[i].b, scope);
        if (la < 0 || lb < 0) continue;
        if (la == lb && c->t[i].a != c->t[i].b) continue;
        ta[ne] = map[la]; tb[ne] = map[lb]; tr[ne] = c->t[i]; ne++;
    }
    g.nn = nn; g.ne = ne; g.dir = dir; g.nodesep = 1.8f; g.ranksep = 2.0f;
    g.nodes = calloc((size_t)(nn ? nn : 1), sizeof *g.nodes);
    g.edges = calloc((size_t)(ne ? ne : 1), sizeof *g.edges);
    if (!g.nodes || !g.edges) { dg_fail(c->d, "out of memory"); goto done; }
    for (i = 0; i < nn; i++) { g.nodes[i].w = c->s[idx[i]].w; g.nodes[i].h = c->s[idx[i]].h; }
    for (e = 0; e < ne; e++) {
        g.edges[e].a = ta[e]; g.edges[e].b = tb[e]; g.edges[e].minlen = 1;
        if (tr[e].label) { float tw, th; dg_text_size(c->d, tr[e].label, strlen(tr[e].label), 0.9f, &tw, &th); g.edges[e].lw = tw + 0.6f; g.edges[e].lh = th + 0.3f; }
    }
    if (nn && lg_layout(&g)) { dg_fail(c->d, "the diagram is too large to lay out"); goto done; }
    /* composite boxes, then transitions, then states */
    for (i = 0; i < nn; i++) {
        SState *st = &c->s[idx[i]];
        const char *name = st->label ? st->label : st->id;
        float x = g.nodes[i].x - st->w / 2.0f, y = g.nodes[i].y - st->h / 2.0f;
        if (st->child < 0) continue;
        dg_box(out, EXD_ROUND, x, y, st->w, st->h, EXD_C_PANEL, EXD_C_NODE_STROKE);
        dg_label(out, x + 0.8f, y + 1.0f, name, strlen(name), 1, 1.0f, EXD_C_TEXT, 1);
        if (st->sub) dg_append(out, st->sub, x + (st->w - st->subw) / 2.0f, y + 2.0f);
    }
    for (e = 0; e < ne; e++) {
        const SState *sa = &c->s[idx[ta[e]]], *sb = &c->s[idx[tb[e]]];
        int shape_a = sa->kind == K_START || sa->kind == K_END ? EXD_ELLIPSE : (sa->kind == K_CHOICE ? EXD_DIAMOND : EXD_RECT);
        int shape_b = sb->kind == K_START || sb->kind == K_END ? EXD_ELLIPSE : (sb->kind == K_CHOICE ? EXD_DIAMOND : EXD_RECT);
        dg_edge_line(out, &g, e, shape_a, shape_b, EXD_C_LINE, tr[e].dashed ? 1 : 0, 0, tr[e].arrow ? EXD_H_ARROW : 0);
        if (tr[e].label) dg_edge_label(out, g.edges[e].lx, g.edges[e].ly, tr[e].label, 0.9f);
    }
    for (i = 0; i < nn; i++) {
        SState *st = &c->s[idx[i]];
        const char *name = st->label ? st->label : st->id;
        float x = g.nodes[i].x - st->w / 2.0f, y = g.nodes[i].y - st->h / 2.0f, tw, th;
        switch (st->kind) {
        case K_START: dg_box(out, EXD_ELLIPSE, x, y, st->w, st->h, EXD_C_LINE, EXD_C_LINE); break;
        case K_END: dg_box(out, EXD_ELLIPSE, x, y, st->w, st->h, EXD_C_NODE_FILL, EXD_C_LINE); dg_box(out, EXD_ELLIPSE, x + 0.3f, y + 0.3f, st->w - 0.6f, st->h - 0.6f, EXD_C_LINE, EXD_C_LINE); break;
        case K_CHOICE: dg_box(out, EXD_DIAMOND, x, y, st->w, st->h, EXD_C_NODE_FILL, EXD_C_NODE_STROKE); break;
        case K_FORK: dg_box(out, EXD_RECT, x, y, st->w, st->h, EXD_C_LINE, EXD_C_LINE); break;
        case K_NOTE: {
            int lines = 1;
            const char *q;
            for (q = name; *q; q++) if (*q == '\n') lines++;
            dg_text_size(c->d, name, strlen(name), 0.95f, &tw, &th);
            dg_box(out, EXD_RECT, x, y, st->w, st->h, EXD_C_NOTE_FILL, EXD_C_NODE_STROKE);
            dg_label(out, x + st->w / 2.0f, y + st->h / 2.0f - (float)(lines - 1) * 1.25f * 0.95f / 2.0f, name, strlen(name), 0, 0.95f, EXD_C_TEXT, 0);
            break;
        }
        default:
            if (st->child >= 0) break;
            dg_box(out, EXD_ROUND, x, y, st->w, st->h, EXD_C_NODE_FILL, EXD_C_NODE_STROKE);
            if (st->nd) {
                float ln2[4], top = y + 0.9f;
                int k;
                dg_text_size(c->d, name, strlen(name), 1.0f, &tw, &th);
                dg_label(out, x + st->w / 2.0f, y + 0.5f + th / 2.0f - (th - 1.25f) / 2.0f, name, strlen(name), 0, 1.0f, EXD_C_TEXT, 1);
                ln2[0] = x; ln2[1] = y + th + 0.9f; ln2[2] = x + st->w; ln2[3] = y + th + 0.9f;
                dg_line(out, ln2, 2, EXD_C_NODE_STROKE, 0, 0, 0);
                top = y + th + 0.9f;
                for (k = 0; k < st->nd; k++) dg_label(out, x + 0.9f, top + 0.35f + (float)k * 1.25f * 0.95f + 0.6f, st->desc[k], strlen(st->desc[k]), 1, 0.95f, EXD_C_TEXT, 0);
            } else {
                int lines = 1;
                const char *q;
                for (q = name; *q; q++) if (*q == '\n') lines++;
                dg_label(out, x + st->w / 2.0f, y + st->h / 2.0f - (float)(lines - 1) * 1.25f / 2.0f, name, strlen(name), 0, 1.0f, EXD_C_TEXT, 0);
            }
            break;
        }
    }
    lg_free(&g);
    dg_finalize(out, 0.6f);
    rc = 0;
done:
    free(g.nodes); free(g.edges); free(idx); free(map); free(ta); free(tb); free(tr);
    return rc;
}

int dg_state(ExDiagram *d, DgLines *L, const char *first, size_t flen) {
    SC c;
    const char *line;
    size_t ln;
    int i, j, rc;
    (void)first; (void)flen;
    memset(&c, 0, sizeof c);
    c.d = d;
    c.sc = calloc(1, sizeof *c.sc);
    if (!c.sc) { dg_fail(d, "out of memory"); return -1; }
    c.nsc = 1; c.sc[0].owner = -1; c.sc[0].parent = -1; c.sc[0].start = c.sc[0].end = -1;
    while (!d->has_err && dg_next_line(L, &line, &ln)) statement(&c, line, ln);
    if (!d->has_err && c.in_note) dg_fail(d, "a note is not closed with end note");
    if (!d->has_err && c.cur != 0) dg_fail(d, "a composite state is not closed with }");
    rc = d->has_err ? -1 : 0;
    if (!rc) {
        if (c.ns == 0) { d->w = d->h = 2.0f; }
        else {
            ExDiagram tmp;
            memset(&tmp, 0, sizeof tmp);
            tmp.font = d->font;
            rc = layout_scope(&c, 0, &tmp, c.dir);
            if (!rc) {
                dg_append(d, &tmp, 0, 0);
                d->w = tmp.w; d->h = tmp.h;
            }
            free(tmp.items); free(tmp.pts); free(tmp.text);
            if (tmp.has_err) dg_fail(d, "%s", tmp.err);
        }
    }
    for (i = 0; i < c.ns; i++) {
        free(c.s[i].id); free(c.s[i].label);
        for (j = 0; j < c.s[i].nd; j++) free(c.s[i].desc[j]);
        free(c.s[i].desc);
        ex_diagram_free(c.s[i].sub);
    }
    for (i = 0; i < c.nt; i++) free(c.t[i].label);
    free(c.s); free(c.t); free(c.sc); free(c.note_text);
    return d->has_err ? -1 : 0;
}
