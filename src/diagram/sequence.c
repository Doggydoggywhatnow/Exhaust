/* Mermaid sequence diagrams (6d-ii): participants and actors, messages with the eight arrow forms, activations (`activate`, `+` and `-`), notes, the blocks
 * loop, alt, opt, par, critical, break and rect with their else/and/option parts, autonumber. Participant boxes are drawn at the top and the bottom with
 * dashed lifelines between; the columns are spread so every message text and note fits. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagram_internal.h"

typedef struct SPart { char *id, *label; int actor; float w, h, x; } SPart;
enum { EV_MSG, EV_NOTE, EV_ACT, EV_DEACT, EV_BLOCK, EV_MID, EV_END };
enum { NOTE_OVER, NOTE_LEFT, NOTE_RIGHT };
typedef struct SEv {
    int type, a, b, dash, head, side, number;
    char *text, *kw;
    float y, h;       /* the middle of the line (message) or the top (note, block), and the height it takes */
    int block;        /* EV_BLOCK/MID/END: the index of the block */
    int act_on_target, act_off_sender;
} SEv;
typedef struct SBlock { int start_ev, end_ev, depth; float y0, y1; } SBlock;

typedef struct SC {
    ExDiagram *d;
    SPart *p; int np, capp;
    SEv *e; int ne, cape;
    SBlock *b; int nb, capb;
    int open[32], nopen;
    int autonumber, boxes;
} SC;

static int is_id_char(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
static int ci_starts(const char *s, size_t n, const char *kw) {
    size_t l = strlen(kw), i;
    if (n < l) return 0;
    for (i = 0; i < l; i++) if (tolower((unsigned char)s[i]) != kw[i]) return 0;
    return n == l || s[l] == ' ' || s[l] == '\t' || s[l] == ':';
}

static int part_for(SC *c, const char *id, size_t n) {
    int i;
    for (i = 0; i < c->np; i++) if (strlen(c->p[i].id) == n && !memcmp(c->p[i].id, id, n)) return i;
    if (c->np >= 40) { dg_fail(c->d, "more than 40 participants"); return -1; }
    if (c->np == c->capp) {
        int nc = c->capp ? c->capp * 2 : 8;
        SPart *t = realloc(c->p, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return -1; }
        c->p = t; c->capp = nc;
    }
    memset(&c->p[c->np], 0, sizeof c->p[0]);
    c->p[c->np].id = dg_strndup(id, n);
    return c->np++;
}

static SEv *add_ev(SC *c, int type) {
    SEv *e;
    if (c->ne >= 1500) { dg_fail(c->d, "more than 1500 events"); return NULL; }
    if (c->ne == c->cape) {
        int nc = c->cape ? c->cape * 2 : 32;
        SEv *t = realloc(c->e, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return NULL; }
        c->e = t; c->cape = nc;
    }
    e = &c->e[c->ne++];
    memset(e, 0, sizeof *e);
    e->type = type; e->block = -1;
    return e;
}

static void trim(const char **s, size_t *n) {
    while (*n && (**s == ' ' || **s == '\t')) { (*s)++; (*n)--; }
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t')) (*n)--;
}

static int read_id(SC *c, const char *s, size_t n, size_t *p) {
    size_t st = *p;
    while (*p < n && is_id_char((unsigned char)s[*p])) (*p)++;
    if (*p == st) return -1;
    return part_for(c, s + st, *p - st);
}

static void do_participant(SC *c, const char *s, size_t n, int actor) {
    size_t p = 0;
    int idx;
    const char *as;
    skip_kw: ;
    trim(&s, &n);
    if (n > 7 && !strncmp(s, "create ", 7)) { s += 7; n -= 7; goto skip_kw; }
    p = 0;
    idx = read_id(c, s, n, &p);
    if (idx < 0) { dg_fail(c->d, "a participant name was expected"); return; }
    c->p[idx].actor = actor;
    as = NULL;
    {
        size_t q = p;
        while (q < n && s[q] == ' ') q++;
        if (q + 2 < n && !strncmp(s + q, "as ", 3)) { as = s + q + 3; free(c->p[idx].label); c->p[idx].label = dg_clean_label(as, n - (size_t)(as - s)); }
    }
}

static int arrow_at(const char *s, size_t n, size_t p, size_t *len, int *dash, int *head) {
    static const struct { const char *t; int dash, head; } forms[] = {
        {"-->>", 1, EXD_H_ARROW}, {"->>", 0, EXD_H_ARROW}, {"--x", 1, EXD_H_CROSS}, {"-x", 0, EXD_H_CROSS}, {"--)", 1, EXD_H_OPEN}, {"-)", 0, EXD_H_OPEN},
        {"-->", 1, EXD_H_NONE}, {"->", 0, EXD_H_NONE},
    };
    size_t i;
    for (i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        size_t l = strlen(forms[i].t);
        if (n - p >= l && !strncmp(s + p, forms[i].t, l)) { *len = l; *dash = forms[i].dash; *head = forms[i].head; return 1; }
    }
    return 0;
}

static void do_message(SC *c, const char *s, size_t n) {
    size_t p = 0, al;
    int a, b, dash, head, on = 0, off = 0;
    SEv *e;
    a = read_id(c, s, n, &p);
    if (a < 0) { dg_fail(c->d, "unknown line: %.*s", (int)(n > 30 ? 30 : n), s); return; }
    while (p < n && s[p] == ' ') p++;
    if (!arrow_at(s, n, p, &al, &dash, &head)) { dg_fail(c->d, "an arrow was expected: %.*s", (int)(n > 30 ? 30 : n), s); return; }
    p += al;
    if (p < n && s[p] == '+') { on = 1; p++; } else if (p < n && s[p] == '-') { off = 1; p++; }
    while (p < n && s[p] == ' ') p++;
    b = read_id(c, s, n, &p);
    if (b < 0) { dg_fail(c->d, "a participant was expected after the arrow"); return; }
    while (p < n && s[p] == ' ') p++;
    e = add_ev(c, EV_MSG);
    if (!e) return;
    e->a = a; e->b = b; e->dash = dash; e->head = head; e->act_on_target = on; e->act_off_sender = off;
    if (p < n && s[p] == ':') { p++; e->text = dg_clean_label(s + p, n - p); }
    else e->text = dg_strndup("", 0);
    if (c->autonumber) {
        char buf[16];
        int num = 0, i;
        for (i = 0; i < c->ne; i++) if (c->e[i].type == EV_MSG) num++;
        snprintf(buf, sizeof buf, "%d. ", num);
        {
            size_t l1 = strlen(buf), l2 = strlen(e->text);
            char *t = malloc(l1 + l2 + 1);
            if (t) { memcpy(t, buf, l1); memcpy(t + l1, e->text, l2 + 1); free(e->text); e->text = t; }
        }
    }
}

static void do_note(SC *c, const char *s, size_t n) {
    size_t p = 4;
    int a, b, side = NOTE_OVER;
    SEv *e;
    while (p < n && s[p] == ' ') p++;
    if (n - p >= 4 && ci_starts(s + p, n - p, "over")) { p += 4; }
    else if (n - p >= 7 && ci_starts(s + p, n - p, "left")) { side = NOTE_LEFT; p += 4; while (p < n && s[p] == ' ') p++; if (n - p >= 2 && ci_starts(s + p, n - p, "of")) p += 2; }
    else if (n - p >= 8 && ci_starts(s + p, n - p, "right")) { side = NOTE_RIGHT; p += 5; while (p < n && s[p] == ' ') p++; if (n - p >= 2 && ci_starts(s + p, n - p, "of")) p += 2; }
    else { dg_fail(c->d, "a note is placed over, left of or right of a participant"); return; }
    while (p < n && s[p] == ' ') p++;
    a = read_id(c, s, n, &p);
    if (a < 0) { dg_fail(c->d, "a note needs a participant"); return; }
    b = a;
    while (p < n && s[p] == ' ') p++;
    if (side == NOTE_OVER && p < n && s[p] == ',') { p++; while (p < n && s[p] == ' ') p++; b = read_id(c, s, n, &p); if (b < 0) { dg_fail(c->d, "a note needs a second participant"); return; } while (p < n && s[p] == ' ') p++; }
    if (p >= n || s[p] != ':') { dg_fail(c->d, "a note needs its text after a colon"); return; }
    p++;
    e = add_ev(c, EV_NOTE);
    if (!e) return;
    e->a = a < b ? a : b; e->b = a < b ? b : a; e->side = side;
    e->text = dg_clean_label(s + p, n - p);
}

static void do_block(SC *c, const char *kw, const char *s, size_t n) {
    SEv *e;
    SBlock *b;
    if (c->nopen >= 30) { dg_fail(c->d, "blocks are nested too deep"); return; }
    if (c->nb == c->capb) {
        int nc = c->capb ? c->capb * 2 : 8;
        SBlock *t = realloc(c->b, (size_t)nc * sizeof *t);
        if (!t) { dg_fail(c->d, "out of memory"); return; }
        c->b = t; c->capb = nc;
    }
    e = add_ev(c, EV_BLOCK);
    if (!e) return;
    b = &c->b[c->nb];
    memset(b, 0, sizeof *b);
    b->start_ev = c->ne - 1;
    b->depth = c->nopen;
    e->block = c->nb;
    e->kw = dg_strndup(kw, strlen(kw));
    e->text = dg_clean_label(s, n);
    c->open[c->nopen++] = c->nb++;
}

static void do_statement(SC *c, const char *s, size_t n) {
    trim(&s, &n);
    if (!n) return;
    if (ci_starts(s, n, "participant")) { do_participant(c, s + 11, n - 11, 0); return; }
    if (ci_starts(s, n, "actor")) { do_participant(c, s + 5, n - 5, 1); return; }
    if (ci_starts(s, n, "create")) { size_t k = 6; while (k < n && s[k] == ' ') k++; if (ci_starts(s + k, n - k, "participant")) do_participant(c, s + k + 11, n - k - 11, 0); else if (ci_starts(s + k, n - k, "actor")) do_participant(c, s + k + 5, n - k - 5, 1); return; }
    if (ci_starts(s, n, "destroy") || ci_starts(s, n, "title") || ci_starts(s, n, "link") || ci_starts(s, n, "links") || ci_starts(s, n, "accTitle") || ci_starts(s, n, "accDescr")) return;
    if (ci_starts(s, n, "autonumber")) { c->autonumber = 1; return; }
    if (ci_starts(s, n, "note")) { do_note(c, s, n); return; }
    if (ci_starts(s, n, "activate") || ci_starts(s, n, "deactivate")) {
        int on = !ci_starts(s, n, "deactivate");
        size_t p = on ? 8 : 10;
        int a;
        SEv *e;
        while (p < n && s[p] == ' ') p++;
        a = read_id(c, s, n, &p);
        if (a < 0) { dg_fail(c->d, "activate needs a participant"); return; }
        e = add_ev(c, on ? EV_ACT : EV_DEACT);
        if (e) e->a = a;
        return;
    }
    if (ci_starts(s, n, "box")) { if (c->nopen < 30) c->open[c->nopen++] = -1; return; }
    if (ci_starts(s, n, "end")) {
        SEv *e;
        if (c->nopen == 0) { dg_fail(c->d, "end without a block"); return; }
        c->nopen--;
        if (c->open[c->nopen] < 0) return;
        e = add_ev(c, EV_END);
        if (e) { e->block = c->open[c->nopen]; c->b[e->block].end_ev = c->ne - 1; }
        return;
    }
    {
        static const char *const mids[] = { "else", "and", "option" };
        size_t k;
        for (k = 0; k < 3; k++)
            if (ci_starts(s, n, mids[k])) {
                SEv *e;
                size_t l = strlen(mids[k]);
                if (c->nopen == 0 || c->open[c->nopen - 1] < 0) { dg_fail(c->d, "%s outside a block", mids[k]); return; }
                e = add_ev(c, EV_MID);
                if (e) { e->block = c->open[c->nopen - 1]; e->kw = dg_strndup(mids[k], l); e->text = dg_clean_label(s + l, n - l); }
                return;
            }
    }
    {
        static const char *const blocks[] = { "loop", "alt", "opt", "par", "critical", "break", "rect" };
        size_t k;
        for (k = 0; k < 7; k++)
            if (ci_starts(s, n, blocks[k])) {
                size_t l = strlen(blocks[k]);
                do_block(c, blocks[k], s + l, n - l);
                return;
            }
    }
    do_message(c, s, n);
}

static void free_all(SC *c) {
    int i;
    for (i = 0; i < c->np; i++) { free(c->p[i].id); free(c->p[i].label); }
    for (i = 0; i < c->ne; i++) { free(c->e[i].text); free(c->e[i].kw); }
    free(c->p); free(c->e); free(c->b);
}

typedef struct Con { int lo, hi; float need; } Con;

int dg_sequence(ExDiagram *d, DgLines *L) {
    SC c;
    const char *line;
    size_t ln;
    int i, k, np;
    float *gap = NULL, *cx = NULL, H = 0, y;
    Con *cons = NULL;
    int ncons = 0;
    memset(&c, 0, sizeof c);
    c.d = d;
    while (!d->has_err && dg_next_line(L, &line, &ln)) do_statement(&c, line, ln);
    if (!d->has_err && c.nopen > 0) dg_fail(d, "a block is not closed with end");
    if (d->has_err) { free_all(&c); return -1; }
    np = c.np;
    if (np == 0) { d->w = d->h = 2.0f; free_all(&c); return 0; }
    for (i = 0; i < np; i++) {
        const char *lab = c.p[i].label ? c.p[i].label : c.p[i].id;
        float tw, th;
        dg_text_size(d, lab, strlen(lab), 1.0f, &tw, &th);
        c.p[i].w = tw + 1.6f; if (c.p[i].w < 3.0f) c.p[i].w = 3.0f;
        c.p[i].h = th + 1.0f; if (c.p[i].h < 2.0f) c.p[i].h = 2.0f;
        if (c.p[i].h > H) H = c.p[i].h;
    }
    gap = calloc((size_t)np, sizeof *gap);
    cx = calloc((size_t)np, sizeof *cx);
    cons = calloc((size_t)(c.ne + np + 1), sizeof *cons);
    if (!gap || !cx || !cons) { dg_fail(d, "out of memory"); free(gap); free(cx); free(cons); free_all(&c); return -1; }
    for (k = 0; k + 1 < np; k++) gap[k] = (c.p[k].w + c.p[k + 1].w) / 2.0f + 1.2f;
    for (i = 0; i < c.ne; i++) {
        SEv *e = &c.e[i];
        float tw = 0, th = 0;
        if (e->text) dg_text_size(d, e->text, strlen(e->text), 0.95f, &tw, &th);
        if (e->type == EV_MSG) {
            if (e->a == e->b) { if (e->a + 1 < np) { cons[ncons].lo = e->a; cons[ncons].hi = e->a + 1; cons[ncons].need = tw + 2.6f; ncons++; } }
            else { cons[ncons].lo = e->a < e->b ? e->a : e->b; cons[ncons].hi = e->a < e->b ? e->b : e->a; cons[ncons].need = tw + 1.8f; ncons++; }
        } else if (e->type == EV_NOTE) {
            float nw = tw + 1.4f;
            if (e->side == NOTE_OVER && e->a == e->b) {
                if (e->a > 0) { cons[ncons].lo = e->a - 1; cons[ncons].hi = e->a; cons[ncons].need = nw / 2.0f + c.p[e->a - 1].w / 2.0f + 0.3f; ncons++; }
                if (e->a + 1 < np) { cons[ncons].lo = e->a; cons[ncons].hi = e->a + 1; cons[ncons].need = nw / 2.0f + c.p[e->a + 1].w / 2.0f + 0.3f; ncons++; }
            } else if (e->side == NOTE_OVER) { cons[ncons].lo = e->a; cons[ncons].hi = e->b; cons[ncons].need = nw - 0.4f; ncons++; }
            else if (e->side == NOTE_LEFT) { if (e->a > 0) { cons[ncons].lo = e->a - 1; cons[ncons].hi = e->a; cons[ncons].need = nw + 1.0f; ncons++; } }
            else { if (e->a + 1 < np) { cons[ncons].lo = e->a; cons[ncons].hi = e->a + 1; cons[ncons].need = nw + 1.0f; ncons++; } }
        }
    }
    {   /* short spans first, so a long message only widens what the short ones left too narrow */
        int span;
        for (span = 1; span < np; span++)
            for (i = 0; i < ncons; i++) {
                float sum = 0;
                if (cons[i].hi - cons[i].lo != span) continue;
                for (k = cons[i].lo; k < cons[i].hi; k++) sum += gap[k];
                if (sum < cons[i].need) { float add = (cons[i].need - sum) / (float)span; for (k = cons[i].lo; k < cons[i].hi; k++) gap[k] += add; }
            }
    }
    cx[0] = c.p[0].w / 2.0f;
    for (k = 0; k + 1 < np; k++) cx[k + 1] = cx[k] + gap[k];
    for (i = 0; i < np; i++) c.p[i].x = cx[i];

    /* vertical positions */
    y = H + 1.0f;
    for (i = 0; i < c.ne; i++) {
        SEv *e = &c.e[i];
        float tw = 0, th = 0;
        if (e->text) dg_text_size(d, e->text, strlen(e->text), 0.95f, &tw, &th);
        switch (e->type) {
        case EV_MSG:
            if (e->a == e->b) { e->y = y + 0.4f; e->h = 2.2f > th + 1.0f ? 2.2f : th + 1.0f; }
            else { e->y = y + (e->text && *e->text ? th + 0.35f : 0.5f); e->h = (e->text && *e->text ? th : 0) + 1.2f; }
            y += e->h;
            break;
        case EV_NOTE: e->y = y; e->h = th + 0.8f; y += e->h + 0.7f; break;
        case EV_BLOCK: c.b[e->block].y0 = y; e->y = y; y += 1.6f; break;
        case EV_MID: e->y = y + 0.2f; y += 1.5f; break;
        case EV_END: c.b[e->block].y1 = y; e->y = y; y += 0.9f; break;
        default: e->y = y; break;
        }
    }
    y += 0.4f;

    /* items: frames, lifelines, activation bars, messages and notes, participant boxes */
    {
        float left = cx[0] - c.p[0].w / 2.0f - 0.4f, right = cx[np - 1] + c.p[np - 1].w / 2.0f + 0.4f;
        int *depth = calloc((size_t)np, sizeof(int)), *act_n = calloc((size_t)np, sizeof(int));
        float *act_y = calloc((size_t)np * 16, sizeof(float));
        if (!depth || !act_n || !act_y) { dg_fail(d, "out of memory"); free(depth); free(act_n); free(act_y); free(gap); free(cx); free(cons); free_all(&c); return -1; }
        for (i = 0; i < c.nb; i++) {
            SBlock *b = &c.b[i];
            float ind = (float)b->depth * 0.4f, tw, th;
            const SEv *e = &c.e[b->start_ev];
            if (b->y1 <= b->y0) b->y1 = y - 0.2f;
            dg_box(d, EXD_RECT, left + ind, b->y0, right - left - 2.0f * ind, b->y1 - b->y0, EXD_C_NONE, EXD_C_NODE_STROKE);
            if (strcmp(e->kw, "rect") != 0) {
                dg_text_size(d, e->kw, strlen(e->kw), 0.95f, &tw, &th);
                dg_box(d, EXD_RECT, left + ind, b->y0, tw + 1.2f, 1.3f, EXD_C_PANEL, EXD_C_NODE_STROKE);
                dg_label(d, left + ind + 0.6f, b->y0 + 0.65f, e->kw, strlen(e->kw), 1, 0.95f, EXD_C_TEXT, 1);
                if (e->text && *e->text) {
                    char *cond = malloc(strlen(e->text) + 3);
                    if (cond) { sprintf(cond, "[%s]", e->text); dg_label(d, left + ind + tw + 1.8f, b->y0 + 0.65f, cond, strlen(cond), 1, 0.95f, EXD_C_MUTED, 0); free(cond); }
                }
            }
        }
        for (i = 0; i < c.ne; i++) {
            const SEv *e = &c.e[i];
            if (e->type == EV_MID) {
                float ind = (float)c.b[e->block].depth * 0.4f, ln2[4];
                ln2[0] = left + ind; ln2[1] = e->y; ln2[2] = right - ind; ln2[3] = e->y;
                dg_line(d, ln2, 2, EXD_C_NODE_STROKE, 1, 0, 0);
                if (e->text && *e->text) {
                    char *cond = malloc(strlen(e->text) + 3);
                    if (cond) { sprintf(cond, "[%s]", e->text); dg_label(d, left + ind + 0.6f, e->y + 0.7f, cond, strlen(cond), 1, 0.95f, EXD_C_MUTED, 0); free(cond); }
                }
            }
        }
        for (k = 0; k < np; k++) {
            float ln2[4];
            ln2[0] = cx[k]; ln2[1] = H; ln2[2] = cx[k]; ln2[3] = y;
            dg_line(d, ln2, 2, EXD_C_NODE_STROKE, 1, 0, 0);
        }
        /* activation bars, from the events in order */
        for (i = 0; i < c.ne; i++) {
            const SEv *e = &c.e[i];
            int pa = -1, on = 0;
            if (e->type == EV_ACT) { pa = e->a; on = 1; }
            else if (e->type == EV_DEACT) { pa = e->a; on = 0; }
            else if (e->type == EV_MSG && e->act_on_target) { pa = e->b; on = 1; }
            else if (e->type == EV_MSG && e->act_off_sender) { pa = e->a; on = 0; }
            if (pa < 0) continue;
            if (on) { if (act_n[pa] < 16) act_y[pa * 16 + act_n[pa]] = e->type == EV_MSG ? e->y : e->y; act_n[pa]++; }
            else if (act_n[pa] > 0) {
                int lvl = --act_n[pa];
                float y0 = lvl < 16 ? act_y[pa * 16 + lvl] : 0;
                dg_box(d, EXD_RECT, cx[pa] - 0.25f + 0.25f * (float)lvl, y0, 0.5f, e->y - y0, EXD_C_ACCENT_FILL, EXD_C_NODE_STROKE);
            }
        }
        for (k = 0; k < np; k++) while (act_n[k] > 0) {
            int lvl = --act_n[k];
            float y0 = lvl < 16 ? act_y[k * 16 + lvl] : 0;
            dg_box(d, EXD_RECT, cx[k] - 0.25f + 0.25f * (float)lvl, y0, 0.5f, y - 0.2f - y0, EXD_C_ACCENT_FILL, EXD_C_NODE_STROKE);
        }
        memset(act_n, 0, (size_t)np * sizeof(int));
        for (i = 0; i < c.ne; i++) {   /* messages and notes (activation depth is tracked again for the line ends) */
            const SEv *e = &c.e[i];
            if (e->type == EV_ACT) act_n[e->a]++;
            else if (e->type == EV_DEACT) { if (act_n[e->a] > 0) act_n[e->a]--; }
            else if (e->type == EV_MSG) {
                float tw = 0, th = 0, xa = cx[e->a], xb = cx[e->b];
                ExDItem *it;
                if (e->act_on_target) act_n[e->b]++;
                if (e->text && *e->text) dg_text_size(d, e->text, strlen(e->text), 0.95f, &tw, &th);
                if (e->a == e->b) {
                    float q[8], bar = act_n[e->a] > 0 ? 0.25f : 0.0f;
                    q[0] = xa + bar; q[1] = e->y; q[2] = xa + 1.4f; q[3] = e->y; q[4] = xa + 1.4f; q[5] = e->y + 1.0f; q[6] = xa + bar; q[7] = e->y + 1.0f;
                    it = dg_line(d, q, 4, EXD_C_LINE, e->dash, 0, e->head);
                    (void)it;
                    if (e->text && *e->text) dg_label(d, xa + 1.7f, e->y + 0.5f - (th / 0.95f > 1.25f ? (th - 1.2f) / 2.0f : 0.0f) + (th > 1.3f ? 0.0f : 0.0f), e->text, strlen(e->text), 1, 0.95f, EXD_C_TEXT, 0);
                } else {
                    float q[4], dir = xb > xa ? 1.0f : -1.0f, from_bar = act_n[e->a] > 0 ? 0.25f : 0.0f, to_bar = act_n[e->b] > 0 ? 0.25f : 0.0f;
                    q[0] = xa + dir * from_bar; q[1] = e->y; q[2] = xb - dir * to_bar; q[3] = e->y;
                    dg_line(d, q, 2, EXD_C_LINE, e->dash, 0, e->head);
                    if (e->text && *e->text) dg_label(d, (xa + xb) / 2.0f, e->y - 0.3f - th + 0.5f * dg_line_height() * 0.95f, e->text, strlen(e->text), 0, 0.95f, EXD_C_TEXT, 0);
                }
                if (e->act_off_sender && act_n[e->a] > 0) act_n[e->a]--;
            } else if (e->type == EV_NOTE) {
                float tw, th, nw, x0, x1;
                dg_text_size(d, e->text, strlen(e->text), 0.95f, &tw, &th);
                nw = tw + 1.4f;
                if (e->side == NOTE_OVER) { x0 = (cx[e->a] + cx[e->b]) / 2.0f - nw / 2.0f; x1 = x0 + nw; if (e->a != e->b) { float sp0 = cx[e->a] - 0.6f, sp1 = cx[e->b] + 0.6f; if (sp1 - sp0 > nw) { x0 = sp0; x1 = sp1; } } }
                else if (e->side == NOTE_LEFT) { x1 = cx[e->a] - 0.5f; x0 = x1 - nw; }
                else { x0 = cx[e->a] + 0.5f; x1 = x0 + nw; }
                dg_box(d, EXD_RECT, x0, e->y, x1 - x0, e->h, EXD_C_NOTE_FILL, EXD_C_NODE_STROKE);
                dg_label(d, (x0 + x1) / 2.0f, e->y + 0.4f + 0.5f * dg_line_height() * 0.95f, e->text, strlen(e->text), 0, 0.95f, EXD_C_TEXT, 0);
            }
        }
        for (k = 0; k < np; k++) {
            const char *lab = c.p[k].label ? c.p[k].label : c.p[k].id;
            int shape = c.p[k].actor ? EXD_STADIUM : EXD_RECT, row;
            for (row = 0; row < 2; row++) {
                float top = row == 0 ? 0.0f : y;
                float tw, th;
                dg_text_size(d, lab, strlen(lab), 1.0f, &tw, &th);
                dg_box(d, shape, cx[k] - c.p[k].w / 2.0f, top, c.p[k].w, H, EXD_C_NODE_FILL, EXD_C_NODE_STROKE);
                dg_label(d, cx[k], top + H / 2.0f - (th - dg_line_height()) / 2.0f, lab, strlen(lab), 0, 1.0f, EXD_C_TEXT, 0);
            }
        }
        y += H;
        free(depth); free(act_n); free(act_y);
    }
    free(gap); free(cx); free(cons);
    free_all(&c);
    dg_finalize(d, 0.8f);
    return 0;
}
