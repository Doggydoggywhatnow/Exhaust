#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_markdown.h"
#include "exhaust_nav.h"

typedef struct Ref { char *id; size_t at; } Ref;
typedef struct Def { char *id; char *text; size_t s, e; } Def;
typedef struct St { const char *t; size_t len; Ref *refs; size_t nrefs, cr; Def *defs; size_t ndefs, cd; Ref *inl; size_t ninl, ci; Def *inl_defs; } St;

static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }

static char *trim_dup(const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
    return dupn(s, n);
}

static int visit(const ExMdSpan *sp, void *user) {
    St *st = user;
    const char *t = st->t;
    if (sp->kind == EX_MD_FOOTNOTE_REF && sp->end - sp->start >= 4) {
        if (st->nrefs == st->cr) { size_t nc = st->cr ? st->cr * 2 : 8; Ref *nv = realloc(st->refs, nc * sizeof *nv); if (!nv) return 1; st->refs = nv; st->cr = nc; }
        st->refs[st->nrefs].id = dupn(t + sp->start + 2, sp->end - sp->start - 3);
        st->refs[st->nrefs].at = sp->start;
        st->nrefs++;
    } else if (sp->kind == EX_MD_FOOTNOTE_DEF) {
        size_t k = sp->start + 2, e = k;
        while (e < sp->end && t[e] != ']') e++;
        if (e + 1 < sp->end && t[e + 1] == ':') {
            if (st->ndefs == st->cd) { size_t nc = st->cd ? st->cd * 2 : 8; Def *nv = realloc(st->defs, nc * sizeof *nv); if (!nv) return 1; st->defs = nv; st->cd = nc; }
            st->defs[st->ndefs].id = dupn(t + k, e - k);
            st->defs[st->ndefs].text = trim_dup(t + e + 2, sp->end - e - 2);
            st->defs[st->ndefs].s = sp->start; st->defs[st->ndefs].e = sp->end;
            st->ndefs++;
        }
    } else if (sp->kind == EX_MD_INLINE_FOOTNOTE && sp->end - sp->start >= 3) {
        if (st->ninl == st->ci) { size_t nc = st->ci ? st->ci * 2 : 8; Ref *nv = realloc(st->inl, nc * sizeof *nv); Def *nd = realloc(st->inl_defs, nc * sizeof *nd); if (!nv || !nd) return 1; st->inl = nv; st->inl_defs = nd; st->ci = nc; }
        st->inl[st->ninl].id = NULL; st->inl[st->ninl].at = sp->start;
        st->inl_defs[st->ninl].id = NULL;
        st->inl_defs[st->ninl].text = trim_dup(t + sp->start + 2, sp->end - sp->start - 3);
        st->inl_defs[st->ninl].s = sp->start; st->inl_defs[st->ninl].e = sp->end;
        st->ninl++;
    }
    return 0;
}

void ex_footnotes_free(ExFootnote *f, size_t n) { size_t i; for (i = 0; i < n; i++) { free(f[i].id); free(f[i].text); } free(f); }

int ex_footnotes(const char *text, size_t len, ExFootnote **out, size_t *n) {
    St st;
    ExMd *md = ex_md_create();
    ExFootnote *v;
    size_t i, k, cnt = 0, number = 0;
    memset(&st, 0, sizeof st);
    st.t = text; st.len = len;
    *out = NULL; *n = 0;
    if (!md) return -1;
    ex_md_tokenize(md, text, len);
    ex_md_visit(md, 0, len, visit, &st);
    ex_md_free(md);
    v = calloc(st.nrefs + st.ninl + st.ndefs + 1, sizeof *v);
    if (!v) return -1;
    /* every reference and inline note in the order of the text, numbered by the first reference to a name */
    {
        size_t ri = 0, ii = 0;
        while (ri < st.nrefs || ii < st.ninl) {
            int take_ref = ii >= st.ninl || (ri < st.nrefs && st.refs[ri].at < st.inl[ii].at);
            if (take_ref) {
                size_t j;
                int found = -1;
                for (j = 0; j < cnt; j++) if (!v[j].inline_note && v[j].id && !strcmp(v[j].id, st.refs[ri].id)) { found = (int)j; break; }
                if (found >= 0) v[found].refs++;
                else {
                    v[cnt].id = dupn(st.refs[ri].id, strlen(st.refs[ri].id));
                    v[cnt].number = (int)++number;
                    v[cnt].first_ref = st.refs[ri].at; v[cnt].refs = 1;
                    for (k = 0; k < st.ndefs; k++) if (!strcmp(st.defs[k].id, st.refs[ri].id)) { v[cnt].text = dupn(st.defs[k].text, strlen(st.defs[k].text)); v[cnt].def_start = st.defs[k].s; v[cnt].def_end = st.defs[k].e; break; }
                    cnt++;
                }
                ri++;
            } else {
                char name[32];
                snprintf(name, sizeof name, "inline-%zu", ii + 1);
                v[cnt].id = dupn(name, strlen(name));
                v[cnt].text = dupn(st.inl_defs[ii].text, strlen(st.inl_defs[ii].text));
                v[cnt].number = (int)++number; v[cnt].inline_note = 1;
                v[cnt].def_start = st.inl_defs[ii].s; v[cnt].def_end = st.inl_defs[ii].e; v[cnt].first_ref = st.inl[ii].at; v[cnt].refs = 1;
                cnt++; ii++;
            }
        }
    }
    for (k = 0; k < st.ndefs; k++) {                                         /* definitions nothing refers to */
        size_t j;
        int used = 0;
        for (j = 0; j < cnt; j++) if (!v[j].inline_note && !strcmp(v[j].id, st.defs[k].id)) used = 1;
        if (used) continue;
        v[cnt].id = dupn(st.defs[k].id, strlen(st.defs[k].id));
        v[cnt].text = dupn(st.defs[k].text, strlen(st.defs[k].text));
        v[cnt].def_start = st.defs[k].s; v[cnt].def_end = st.defs[k].e;
        cnt++;
    }
    for (i = 0; i < st.nrefs; i++) free(st.refs[i].id);
    for (i = 0; i < st.ndefs; i++) { free(st.defs[i].id); free(st.defs[i].text); }
    for (i = 0; i < st.ninl; i++) free(st.inl_defs[i].text);
    free(st.refs); free(st.defs); free(st.inl); free(st.inl_defs);
    *out = v; *n = cnt;
    return 0;
}
