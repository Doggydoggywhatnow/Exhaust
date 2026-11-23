/* Templates and daily notes (6e-i). The properties of a template are merged into a note by text surgery: the lines of the note's own properties are kept as
 * they are (never reformatted); a property the note lacks is appended as the template wrote it; a list the note already has gets the items it lacks. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_datefmt.h"
#include "exhaust_platform.h"
#include "exhaust_templates.h"

/* ---- a growing string ---- */
typedef struct Sb { char *p; size_t n, cap; int oom; } Sb;
static void sb_put(Sb *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 128;
        char *t;
        while (nc < b->n + n + 1) nc *= 2;
        t = realloc(b->p, nc);
        if (!t) { b->oom = 1; return; }
        b->p = t; b->cap = nc;
    }
    if (n) memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void sb_str(Sb *b, const char *s) { sb_put(b, s, strlen(s)); }

/* ---- variables ---- */
char *ex_tpl_expand(const char *tpl, size_t len, const ExTplContext *c, size_t *out_len) {
    Sb o = {0};
    size_t i = 0;
    const char *df = c && c->date_format && *c->date_format ? c->date_format : "YYYY-MM-DD", *tf = c && c->time_format && *c->time_format ? c->time_format : "HH:mm";
    if (!tpl) len = 0;
    sb_put(&o, "", 0);
    while (i < len && !o.oom) {
        if (tpl[i] == '{' && i + 1 < len && tpl[i + 1] == '{') {
            size_t j = i + 2;
            while (j + 1 < len && !(tpl[j] == '}' && tpl[j + 1] == '}') && tpl[j] != '\n') j++;
            if (j + 1 < len && tpl[j] == '}' && tpl[j + 1] == '}') {
                size_t a = i + 2, b = j;
                const char *name, *fmt = NULL;
                size_t nl, fl = 0;
                int done = 1;
                while (a < b && (tpl[a] == ' ' || tpl[a] == '\t')) a++;
                while (b > a && (tpl[b - 1] == ' ' || tpl[b - 1] == '\t')) b--;
                name = tpl + a; nl = b - a;
                {   /* name or name:format */
                    size_t k;
                    for (k = 0; k < nl; k++) if (name[k] == ':') { fmt = name + k + 1; fl = nl - k - 1; nl = k; while (nl > 0 && (name[nl - 1] == ' ' || name[nl - 1] == '\t')) nl--; break; }
                }
                if (nl == 5 && !strncmp(name, "title", 5) && !fmt) { if (c && c->title) sb_str(&o, c->title); }
                else if (nl == 7 && !strncmp(name, "content", 7) && !fmt) { if (c && c->content) sb_str(&o, c->content); }
                else if (nl == 9 && !strncmp(name, "fromTitle", 9) && !fmt) { if (c && c->from_title) sb_str(&o, c->from_title); }
                else if (nl == 8 && !strncmp(name, "newTitle", 8) && !fmt) { if (c && c->new_title) sb_str(&o, c->new_title); }
                else if ((nl == 4 && !strncmp(name, "date", 4)) || (nl == 4 && !strncmp(name, "time", 4))) {
                    char f[200], out[400];
                    int is_date = name[0] == 'd';
                    if (fmt) { fl = fl < sizeof f - 1 ? fl : sizeof f - 1; memcpy(f, fmt, fl); f[fl] = 0; { size_t s0 = 0; while (f[s0] == ' ') s0++; memmove(f, f + s0, strlen(f + s0) + 1); } }
                    else snprintf(f, sizeof f, "%s", is_date ? df : tf);
                    ex_date_format(out, sizeof out, c ? (!is_date && c->use_time_ms ? c->time_ms : c->now_ms) : 0.0, c ? c->tz_min : 0, f);
                    sb_str(&o, out);
                } else done = 0;
                if (done) { i = j + 2; continue; }
            }
        }
        sb_put(&o, tpl + i, 1);
        i++;
    }
    if (o.oom) { free(o.p); return NULL; }
    if (out_len) *out_len = o.n;
    return o.p;
}

/* ---- the properties block ---- */
static int is_line_front(const char *t, size_t n, size_t at, size_t *line_end) {   /* a line `---` (spaces allowed after) at `at`; line_end: after its break */
    size_t i = at;
    if (n - at < 3 || strncmp(t + at, "---", 3)) return 0;
    i += 3;
    while (i < n && (t[i] == ' ' || t[i] == '\t')) i++;
    if (i < n && t[i] == '\r') i++;
    if (i < n && t[i] != '\n') return 0;
    *line_end = i < n ? i + 1 : i;
    return 1;
}

int ex_tpl_front(const char *t, size_t n, size_t *inner0, size_t *inner1, size_t *after) {
    size_t i0, p;
    if (!t) return 0;
    if (n >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) { t += 3; n -= 3; }   /* a BOM is not part of the text */
    if (!is_line_front(t, n, 0, &i0)) return 0;
    p = i0;
    while (p < n) {
        size_t e = p, le;
        while (e < n && t[e] != '\n') e++;
        le = e < n ? e + 1 : e;
        if (is_line_front(t, n, p, &le) || (n - p >= 3 && !strncmp(t + p, "...", 3) && (p + 3 == n || t[p + 3] == '\n' || t[p + 3] == '\r' || t[p + 3] == ' '))) {
            if (n - p >= 3 && !strncmp(t + p, "...", 3)) { le = e < n ? e + 1 : e; }
            *inner0 = i0; *inner1 = p; *after = le;
            return 1;
        }
        p = le;
    }
    return 0;
}

/* ---- entries of a properties block ---- */
typedef struct Entry { size_t s, e; size_t ks, ke; int kind; /* 0 scalar or empty, 1 block list, 2 flow list */ size_t last_item_end; size_t first_item_indent; } Entry;
enum { K_SCALAR, K_BLOCK, K_FLOW };

static int is_key_line(const char *t, size_t s, size_t e, size_t *ks, size_t *ke) {
    size_t i = s;
    if (i >= e || t[i] == ' ' || t[i] == '\t' || t[i] == '#' || t[i] == '-' || t[i] == '\n' || t[i] == '\r') return 0;
    if (t[i] == '"' || t[i] == '\'') {
        char q = t[i];
        size_t k = i + 1;
        while (k < e && t[k] != q) k++;
        if (k >= e) return 0;
        *ks = i + 1; *ke = k;
        k++;
        while (k < e && t[k] == ' ') k++;
        return k < e && t[k] == ':';
    }
    while (i < e && t[i] != ':' && t[i] != '\n') i++;
    if (i >= e || t[i] != ':') return 0;
    if (i + 1 < e && t[i + 1] != ' ' && t[i + 1] != '\t' && t[i + 1] != '\n' && t[i + 1] != '\r') return 0;   /* a colon inside a word is not a key */
    *ks = s; *ke = i;
    while (*ke > *ks && (t[*ke - 1] == ' ' || t[*ke - 1] == '\t')) (*ke)--;
    return *ke > *ks;
}

static size_t parse_entries(const char *t, size_t s, size_t e, Entry **out) {
    Entry *en = NULL;
    size_t n = 0, cap = 0, p = s;
    while (p < e) {
        size_t le = p, ks, ke;
        while (le < e && t[le] != '\n') le++;
        if (le < e) le++;
        if (is_key_line(t, p, le, &ks, &ke)) {
            if (n == cap) { size_t nc = cap ? cap * 2 : 8; Entry *x = realloc(en, nc * sizeof *x); if (!x) { free(en); *out = NULL; return 0; } en = x; cap = nc; }
            memset(&en[n], 0, sizeof en[n]);
            en[n].s = p; en[n].e = le; en[n].ks = ks; en[n].ke = ke;
            n++;
        } else if (n > 0) en[n - 1].e = le;
        p = le;
    }
    for (p = 0; p < n; p++) {   /* the kind of each value */
        Entry *x = &en[p];
        size_t v = x->ke, line_end = x->s;
        while (line_end < x->e && t[line_end] != '\n') line_end++;
        while (v < line_end && (t[v] == ':' || t[v] == '"' || t[v] == '\'')) v++;
        while (v < line_end && (t[v] == ' ' || t[v] == '\t')) v++;
        if (v < line_end && t[v] == '[') x->kind = K_FLOW;
        else if (v >= line_end || t[v] == '\r' || t[v] == '#') {
            size_t q = line_end < x->e ? line_end + 1 : x->e;
            while (q < x->e) {
                size_t le = q, i = q;
                while (le < x->e && t[le] != '\n') le++;
                while (i < le && (t[i] == ' ' || t[i] == '\t')) i++;
                if (i < le && t[i] == '-' && (i + 1 == le || t[i + 1] == ' ' || t[i + 1] == '\t' || t[i + 1] == '\r')) {
                    if (x->kind != K_BLOCK) { x->kind = K_BLOCK; x->first_item_indent = i - q; }
                    x->last_item_end = le < x->e ? le + 1 : le;
                }
                q = le < x->e ? le + 1 : le;
            }
        }
    }
    *out = en;
    return n;
}

/* the items of a list entry (trimmed, quotes kept), as one malloc'ed string each */
static size_t list_items(const char *t, const Entry *x, char ***out) {
    char **it = NULL;
    size_t n = 0, cap = 0;
#define ADD(a, l) do { const char *aa = (a); size_t ll = (l); \
        while (ll && (*aa == ' ' || *aa == '\t')) { aa++; ll--; } \
        while (ll && (aa[ll - 1] == ' ' || aa[ll - 1] == '\t' || aa[ll - 1] == '\r')) ll--; \
        if (ll) { if (n == cap) { size_t nc = cap ? cap * 2 : 8; char **x2 = realloc(it, nc * sizeof *x2); if (!x2) goto fail; it = x2; cap = nc; } \
            it[n] = malloc(ll + 1); if (!it[n]) goto fail; memcpy(it[n], aa, ll); it[n][ll] = 0; n++; } } while (0)
    if (x->kind == K_FLOW) {
        size_t a = x->ke, line_end = x->s, st, i;
        int q = 0;
        char qc = 0;
        while (line_end < x->e && t[line_end] != '\n') line_end++;
        while (a < line_end && t[a] != '[') a++;
        a++;
        st = a;
        for (i = a; i < line_end; i++) {
            if (q) { if (t[i] == qc) q = 0; continue; }
            if (t[i] == '"' || t[i] == '\'') { q = 1; qc = t[i]; continue; }
            if (t[i] == ',' || t[i] == ']') { ADD(t + st, i - st); st = i + 1; if (t[i] == ']') break; }
        }
    } else if (x->kind == K_BLOCK) {
        size_t q = x->s;
        while (q < x->e && t[q] != '\n') q++;
        if (q < x->e) q++;
        while (q < x->e) {
            size_t le = q, i = q;
            while (le < x->e && t[le] != '\n') le++;
            while (i < le && (t[i] == ' ' || t[i] == '\t')) i++;
            if (i < le && t[i] == '-' && (i + 1 == le || t[i + 1] == ' ' || t[i + 1] == '\t' || t[i + 1] == '\r')) { ADD(t + i + 1, le - i - 1); }
            q = le < x->e ? le + 1 : le;
        }
    }
#undef ADD
    *out = it;
    return n;
fail:
    while (n) free(it[--n]);
    free(it);
    *out = NULL;
    return 0;
}

static void strip_quotes(const char *s, size_t *a, size_t *b) {
    *a = 0; *b = strlen(s);
    if (*b >= 2 && (s[0] == '"' || s[0] == '\'') && s[*b - 1] == s[0]) { *a = 1; *b -= 1; }
}
static int same_item(const char *x, const char *y) {
    size_t xa, xb, ya, yb;
    strip_quotes(x, &xa, &xb); strip_quotes(y, &ya, &yb);
    return xb - xa == yb - ya && !strncmp(x + xa, y + ya, xb - xa);
}

typedef struct Ins { size_t at; char *text; } Ins;

/* The merged properties text of a note and a template (both without their `---` lines); *changed is 0 when the template adds nothing. */
static char *merge_inner(const char *note, size_t nn, const char *tpl, size_t tn, int *changed, size_t *out_len) {
    Entry *ne = NULL, *te = NULL;
    size_t nne, nte, i, j, nins = 0;
    Ins *ins = NULL;
    Sb out = {0}, tail = {0};
    *changed = 0;
    nne = parse_entries(note, 0, nn, &ne);
    nte = parse_entries(tpl, 0, tn, &te);
    ins = calloc(nte * 4 + 4, sizeof *ins);
    if (!ins) goto fail;
    for (j = 0; j < nte; j++) {
        const Entry *t = &te[j];
        const Entry *n = NULL;
        for (i = 0; i < nne; i++) if (ne[i].ke - ne[i].ks == t->ke - t->ks && !strncmp(note + ne[i].ks, tpl + t->ks, t->ke - t->ks)) { n = &ne[i]; break; }
        if (!n) {   /* a property the note lacks: appended as the template wrote it */
            Sb e = {0};
            sb_put(&e, tpl + t->s, t->e - t->s);
            if (e.n && e.p[e.n - 1] != '\n') sb_put(&e, "\n", 1);
            if (e.oom) { free(e.p); goto fail; }
            sb_put(&tail, e.p, e.n);
            free(e.p);
            *changed = 1;
            continue;
        }
        if (t->kind != K_SCALAR && (n->kind != K_SCALAR || (n->kind == K_SCALAR && t->kind != K_SCALAR))) {
            char **ti = NULL, **ni = NULL;
            size_t nti = list_items(tpl, t, &ti), nni = list_items(note, n, &ni), k, m, added = 0;
            Sb add = {0};
            int note_scalar_filled = 0;
            if (n->kind == K_SCALAR) {   /* a scalar with a value is not a list: kept; an empty one takes the items */
                size_t v = n->ke, le = n->s;
                while (le < n->e && note[le] != '\n') le++;
                while (v < le && (note[v] == ':' || note[v] == '"' || note[v] == '\'')) v++;
                while (v < le && (note[v] == ' ' || note[v] == '\t' || note[v] == '\r')) v++;
                note_scalar_filled = v < le && note[v] != '#';
            }
            if (!note_scalar_filled) {
                for (k = 0; k < nti; k++) {
                    int have = 0;
                    for (m = 0; m < nni; m++) if (same_item(ti[k], ni[m])) { have = 1; break; }
                    if (have) continue;
                    if (n->kind == K_FLOW) { if (add.n || nni) sb_str(&add, ", "); sb_str(&add, ti[k]); }
                    else {
                        size_t ind = n->kind == K_BLOCK ? n->first_item_indent : 2, z;
                        for (z = 0; z < ind; z++) sb_put(&add, " ", 1);
                        sb_str(&add, "- "); sb_str(&add, ti[k]); sb_str(&add, "\n");
                    }
                    added++;
                }
            }
            if (added && !add.oom) {
                size_t at;
                if (n->kind == K_FLOW) {
                    size_t le = n->s, br;
                    while (le < n->e && note[le] != '\n') le++;
                    br = le;
                    while (br > n->ks && note[br - 1] != ']') br--;
                    at = br > 0 ? br - 1 : le;
                } else if (n->kind == K_BLOCK) at = n->last_item_end;
                else { at = n->s; while (at < n->e && note[at] != '\n') at++; at = at < n->e ? at + 1 : n->e; }
                if (n->kind != K_FLOW && at > 0 && note[at - 1] != '\n') { Sb fix = {0}; sb_str(&fix, "\n"); sb_put(&fix, add.p, add.n); free(add.p); add = fix; }
                ins[nins].at = at; ins[nins].text = add.p; nins++;
                *changed = 1;
            } else free(add.p);
            for (k = 0; k < nti; k++) free(ti[k]);
            for (k = 0; k < nni; k++) free(ni[k]);
            free(ti); free(ni);
        }
    }
    /* apply the insertions inside entries (from the back of the note), then the appended entries */
    {
        size_t cur = nn;
        Sb res = {0};
        size_t k;
        for (k = 0; k < nins; k++) for (i = k + 1; i < nins; i++) if (ins[i].at > ins[k].at) { Ins t = ins[k]; ins[k] = ins[i]; ins[i] = t; }
        sb_put(&res, note, nn);
        for (k = 0; k < nins; k++) {
            size_t at = ins[k].at, tl = strlen(ins[k].text);
            Sb tmp = {0};
            (void)cur;
            sb_put(&tmp, res.p, at);
            sb_put(&tmp, ins[k].text, tl);
            sb_put(&tmp, res.p + at, res.n - at);
            free(res.p);
            res = tmp;
            free(ins[k].text);
        }
        if (tail.n) {
            if (res.n && res.p[res.n - 1] != '\n') sb_str(&res, "\n");
            sb_put(&res, tail.p, tail.n);
        }
        free(tail.p);
        out = res;
    }
    free(ne); free(te); free(ins);
    if (out.oom) { free(out.p); return NULL; }
    if (!out.p) { sb_put(&out, "", 0); }
    *out_len = out.n;
    return out.p;
fail:
    free(ne); free(te);
    if (ins) { for (i = 0; i < nins; i++) free(ins[i].text); free(ins); }
    free(tail.p); free(out.p);
    return NULL;
}

int ex_tpl_plan(const char *note, size_t nlen, const char *tpl, size_t tlen, const ExTplContext *c, size_t cursor, ExTplPlan *p) {
    char *t = ex_tpl_expand(tpl, tlen, c, &tlen);
    size_t ti0 = 0, ti1 = 0, tafter = 0, ni0 = 0, ni1 = 0, nafter = 0, bom = 0;
    int tf, nf;
    memset(p, 0, sizeof *p);
    if (!t) return -1;
    if (nlen >= 3 && (unsigned char)note[0] == 0xEF && (unsigned char)note[1] == 0xBB && (unsigned char)note[2] == 0xBF) bom = 3;
    tf = ex_tpl_front(t, tlen, &ti0, &ti1, &tafter);
    nf = ex_tpl_front(note + bom, nlen - bom, &ni0, &ni1, &nafter);
    if (nf) { ni0 += bom; ni1 += bom; nafter += bom; }
    {   /* the body: the template without its properties */
        const char *b = tf ? t + tafter : t;
        size_t bl = tf ? tlen - tafter : tlen;
        size_t at = cursor > nlen ? nlen : cursor;
        if (nf && at < nafter) at = nafter;      /* never into the properties */
        if (bom && at < bom) at = bom;
        p->body_text = malloc(bl + 1);
        if (!p->body_text) { free(t); return -1; }
        memcpy(p->body_text, b, bl); p->body_text[bl] = 0;
        p->body_len = bl;
        p->body_off = at;
    }
    if (tf && ti1 > ti0) {
        if (nf) {
            int changed = 0;
            size_t ol = 0;
            char *m = merge_inner(note + ni0, ni1 - ni0, t + ti0, ti1 - ti0, &changed, &ol);
            if (!m) { free(t); ex_tpl_plan_free(p); return -1; }
            if (changed) { p->has_front = 1; p->front_off = ni0; p->front_del = ni1 - ni0; p->front_text = m; p->front_len = ol; }
            else free(m);
        } else {
            Sb f = {0};
            sb_str(&f, "---\n");
            sb_put(&f, t + ti0, ti1 - ti0);
            if (f.n && f.p[f.n - 1] != '\n') sb_str(&f, "\n");
            sb_str(&f, "---\n");
            if (f.oom) { free(f.p); free(t); ex_tpl_plan_free(p); return -1; }
            p->has_front = 1; p->front_off = bom; p->front_del = 0; p->front_text = f.p; p->front_len = f.n;
            if (p->body_off < p->front_off) p->body_off = p->front_off;
        }
    }
    free(t);
    return 0;
}

void ex_tpl_plan_free(ExTplPlan *p) { if (p) { free(p->front_text); free(p->body_text); memset(p, 0, sizeof *p); } }

char *ex_tpl_apply(const char *note, size_t nlen, const char *tpl, size_t tlen, const ExTplContext *c, size_t cursor, size_t *out_len, size_t *cursor_after) {
    ExTplPlan p;
    Sb o = {0};
    size_t pos = 0;
    if (ex_tpl_plan(note, nlen, tpl, tlen, c, cursor, &p)) return NULL;
    sb_put(&o, "", 0);
    if (p.has_front) {
        sb_put(&o, note, p.front_off);
        sb_put(&o, p.front_text, p.front_len);
        pos = p.front_off + p.front_del;
    }
    sb_put(&o, note + pos, p.body_off - pos);
    sb_put(&o, p.body_text, p.body_len);
    if (cursor_after) *cursor_after = o.n;
    sb_put(&o, note + p.body_off, nlen - p.body_off);
    ex_tpl_plan_free(&p);
    if (o.oom) { free(o.p); return NULL; }
    if (out_len) *out_len = o.n;
    return o.p;
}

/* ---- daily notes ---- */
static const char *df_of(const ExDailyConfig *c) { return c->date_format && *c->date_format ? c->date_format : "YYYY-MM-DD"; }

int ex_daily_path(const ExDailyConfig *c, double ms, char *out, size_t cap) {
    char name[400], folder[400];
    size_t fl;
    if (!c || !out || cap < 8) return -1;
    if (ex_date_format(name, sizeof name, ms, c->tz_min, df_of(c)) >= sizeof name || !name[0]) return -1;
    {   /* a name must not start or end with a slash, hold two in a row, or be a path that leaves the folder */
        size_t i, n = strlen(name);
        if (name[0] == '/' || name[n - 1] == '/') return -1;
        for (i = 0; i + 1 < n; i++) if (name[i] == '/' && name[i + 1] == '/') return -1;
        for (i = 0; i < n; i++) if (name[i] == '\\' || name[i] == ':' || name[i] == '#' || name[i] == '^' || name[i] == '[' || name[i] == ']' || name[i] == '|') return -1;
        if (!strcmp(name, "..") || !strncmp(name, "../", 3) || strstr(name, "/../") || (n >= 3 && !strcmp(name + n - 3, "/.."))) return -1;
    }
    snprintf(folder, sizeof folder, "%s", c->folder ? c->folder : "");
    fl = strlen(folder);
    while (fl && folder[fl - 1] == '/') folder[--fl] = 0;
    if (strstr(folder, "..")) return -1;
    if ((fl ? fl + 1 : 0) + strlen(name) + 3 + 1 > cap) return -1;
    snprintf(out, cap, "%s%s%s.md", folder, fl ? "/" : "", name);
    return 0;
}

int ex_daily_date_of(const ExDailyConfig *c, const char *rel, double *ms) {
    char folder[400];
    size_t fl, n;
    const char *p = rel;
    char name[400];
    if (!c || !rel || !ms) return -1;
    snprintf(folder, sizeof folder, "%s", c->folder ? c->folder : "");
    fl = strlen(folder);
    while (fl && folder[fl - 1] == '/') folder[--fl] = 0;
    if (fl) {
        if (strncmp(rel, folder, fl) || rel[fl] != '/') return -1;
        p = rel + fl + 1;
    }
    n = strlen(p);
    if (n < 4 || strcmp(p + n - 3, ".md")) return -1;
    if (n - 3 >= sizeof name) return -1;
    memcpy(name, p, n - 3); name[n - 3] = 0;
    {   /* the text must parse under the format, and format back to the same text (a loose parse must not claim another note) */
        double v;
        char back[400];
        if (ex_date_parse_format(name, strlen(name), df_of(c), c->tz_min, &v)) return -1;
        ex_date_format(back, sizeof back, v, c->tz_min, df_of(c));
        if (strcmp(back, name)) return -1;
        *ms = v;
        return 0;
    }
}

double ex_daily_shift(const ExDailyConfig *c, double ms, int days) {
    ExDateParts p;
    ex_date_parts(ms, c ? c->tz_min : 0, &p);
    return ex_date_make(p.year, p.month, p.day + days, 0, 0, 0, 0, c ? c->tz_min : 0);
}

static int ensure_folders(ExVault *v, const char *rel_dir) {
    char part[400], parent[400] = "";
    const char *p = rel_dir;
    while (*p) {
        const char *e = strchr(p, '/');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        char *made = NULL;
        ExVaultResult r;
        if (l == 0 || l >= sizeof part) return -1;
        memcpy(part, p, l); part[l] = 0;
        r = ex_vault_create_folder(v, parent, part, &made);
        free(made);
        if (r != EX_VAULT_OK && r != EX_VAULT_EXISTS) return -1;
        if (parent[0]) strncat(parent, "/", sizeof parent - strlen(parent) - 1);
        strncat(parent, part, sizeof parent - strlen(parent) - 1);
        p = e ? e + 1 : p + l;
    }
    return 0;
}

int ex_daily_ensure(ExVault *v, const ExDailyConfig *c, double ms, double now_ms, const char *time_format, char *rel_out, size_t cap) {
    char rel[700], dir[700], file[700], *abs, *tpl = NULL;
    ExPathInfo info;
    const char *slash;
    size_t tl = 0;
    ExTplContext ctx;
    char *text, *made = NULL;
    size_t xl = 0;
    ExVaultResult r;
    if (!v || !c) return -1;
    if (ex_daily_path(c, ms, rel, sizeof rel)) return -1;
    if (rel_out && cap) snprintf(rel_out, cap, "%s", rel);
    abs = ex_vault_abs_path(v, rel);
    if (!abs) return -1;
    if (ex_platform_path_info(abs, &info) == 0) { free(abs); return 0; }     /* already there: never touched */
    free(abs);
    slash = strrchr(rel, '/');
    if (slash) { snprintf(dir, sizeof dir, "%.*s", (int)(slash - rel), rel); snprintf(file, sizeof file, "%s", slash + 1); }
    else { dir[0] = 0; snprintf(file, sizeof file, "%s", rel); }
    if (dir[0] && ensure_folders(v, dir)) return -1;
    if (c->template_path && *c->template_path) {   /* the template's text, with its variables */
        char trel[700];
        char *tabs;
        size_t n = strlen(c->template_path);
        snprintf(trel, sizeof trel, "%.600s%s", c->template_path, (n >= 3 && !strcmp(c->template_path + n - 3, ".md")) ? "" : ".md");
        tabs = ex_vault_abs_path(v, trel);
        if (tabs) { tpl = ex_platform_read_file(tabs, &tl); free(tabs); }
    }
    memset(&ctx, 0, sizeof ctx);
    {
        char title[300];
        size_t tl2 = strlen(file);
        snprintf(title, sizeof title, "%.*s", (int)(tl2 >= 3 ? tl2 - 3 : tl2), file);
        ctx.title = title;
        ctx.now_ms = ms; ctx.tz_min = c->tz_min; ctx.date_format = df_of(c); ctx.time_format = time_format;
        ctx.time_ms = now_ms; ctx.use_time_ms = now_ms > 0;      /* {{date}} is the day of the note, {{time}} the time it was made */
        text = tpl ? ex_tpl_expand(tpl, tl, &ctx, &xl) : NULL;
    }
    free(tpl);
    r = ex_vault_create_note(v, dir, file, &made);     /* exclusive: never replaces a file that appeared meanwhile */
    if (r == EX_VAULT_EXISTS) { free(text); free(made); return 0; }
    if (r != EX_VAULT_OK) { free(text); free(made); return -1; }
    if (text && xl) {
        char *a2 = ex_vault_abs_path(v, made);
        int rc = a2 ? ex_platform_write_file_atomic(a2, text, xl) : -1;
        free(a2);
        if (rc) { free(text); free(made); return -1; }
    }
    free(text); free(made);
    return 1;
}
