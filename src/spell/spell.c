#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_spell.h"

#define MAX_WORD 128

/* ---- data ---- */

typedef struct Cond { int n; unsigned char set[24][32]; } Cond;     /* a condition: up to 24 positions, each a set of bytes */

typedef struct Rule {
    int prefix, cross;
    char flag;
    char strip[32], append[32];
    size_t slen, alen;
    Cond cond;
} Rule;

typedef struct Entry { char *word; char *flags; } Entry;

typedef struct Pair { char *from, *to; int begin, end; } Pair;

struct ExSpell {
    Entry *tab;                 /* open addressing, a power of two long */
    size_t cap, count;
    Rule *rules;
    size_t nrules, caprules;
    Pair *rep;  size_t nrep, caprep;
    Pair *iconv; size_t niconv, capiconv;
    char **keys; size_t nkeys;  /* rows of the keyboard */
    uint32_t *try_cp; size_t ntry;
    char nosuggest, onlyincompound;
};

/* ---- small helpers ---- */

static size_t utf8_len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

static size_t decode(const char *s, size_t n, uint32_t *cp) {
    unsigned char c = (unsigned char)s[0];
    size_t l = utf8_len(c), i;
    uint32_t v;
    if (l > n || c >= 0xF8 || (c >= 0x80 && c < 0xC0)) { *cp = c; return 1; }
    v = l == 1 ? c : l == 2 ? (c & 0x1F) : l == 3 ? (c & 0x0F) : (c & 0x07);
    for (i = 1; i < l; i++) { if (((unsigned char)s[i] & 0xC0) != 0x80) { *cp = c; return 1; } v = (v << 6) | ((unsigned char)s[i] & 0x3F); }
    *cp = v;
    return l;
}

static size_t encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Simple case mapping: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic cover what the shipped dictionaries need. */
static uint32_t lower_cp(uint32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
static uint32_t upper_cp(uint32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
        return (c & 1) ? c - 1 : c;
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
static int is_upper_cp(uint32_t c) { return lower_cp(c) != c; }
static int is_lower_cp(uint32_t c) { return upper_cp(c) != c; }

/* Converts a UTF-8 string with a cp mapping; returns the length written. */
static size_t map_case(const char *s, size_t n, char *out, size_t cap, int mode /* 0 lower, 1 upper first only, 2 all upper */) {
    size_t i = 0, w = 0;
    int first = 1;
    while (i < n) {
        uint32_t cp;
        size_t l = decode(s + i, n - i, &cp);
        char tmp[4];
        size_t el;
        if (mode == 0) cp = lower_cp(cp);
        else if (mode == 2 || first) cp = upper_cp(cp);
        else cp = lower_cp(cp);
        el = encode(cp, tmp);
        if (w + el + 1 > cap) return 0;
        memcpy(out + w, tmp, el);
        w += el;
        i += l;
        first = 0;
    }
    out[w] = 0;
    return w;
}

/* ---- the dictionary table ---- */

static unsigned long hash_str(const char *s) { unsigned long h = 1469598103934665603ul; for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ul; } return h; }

static int tab_grow(ExSpell *s) {
    size_t ncap = s->cap ? s->cap * 2 : 1024, i;
    Entry *nt = calloc(ncap, sizeof *nt);
    if (!nt) return -1;
    for (i = 0; i < s->cap; i++)
        if (s->tab[i].word) {
            size_t k = hash_str(s->tab[i].word) & (ncap - 1);
            while (nt[k].word) k = (k + 1) & (ncap - 1);
            nt[k] = s->tab[i];
        }
    free(s->tab);
    s->tab = nt;
    s->cap = ncap;
    return 0;
}

static char *dup_n(const char *p, size_t n) { char *r = malloc(n + 1); if (r) { memcpy(r, p, n); r[n] = 0; } return r; }

/* Adds a stem; a second entry for the same word merges its flags. */
static int tab_add(ExSpell *s, const char *word, size_t wl, const char *flags, size_t fl) {
    size_t k;
    char *w;
    if (s->count * 2 >= s->cap && tab_grow(s)) return -1;
    {
        char tmp[MAX_WORD + 1];
        if (wl > MAX_WORD) return 0;
        memcpy(tmp, word, wl); tmp[wl] = 0;
        k = hash_str(tmp) & (s->cap - 1);
        while (s->tab[k].word) {
            if (strcmp(s->tab[k].word, tmp) == 0) {
                size_t ol = strlen(s->tab[k].flags);
                char *nf = realloc(s->tab[k].flags, ol + fl + 1);
                if (!nf) return -1;
                memcpy(nf + ol, flags, fl); nf[ol + fl] = 0;
                s->tab[k].flags = nf;
                return 0;
            }
            k = (k + 1) & (s->cap - 1);
        }
        w = dup_n(tmp, wl);
    }
    s->tab[k].word = w;
    s->tab[k].flags = dup_n(flags, fl);
    if (!s->tab[k].word || !s->tab[k].flags) return -1;
    s->count++;
    return 0;
}

static const Entry *tab_find(const ExSpell *s, const char *w) {
    size_t k;
    if (!s->cap) return NULL;
    k = hash_str(w) & (s->cap - 1);
    while (s->tab[k].word) {
        if (strcmp(s->tab[k].word, w) == 0) return &s->tab[k];
        k = (k + 1) & (s->cap - 1);
    }
    return NULL;
}

/* ---- parsing the affix file ---- */

static int parse_cond(const char *c, Cond *out) {
    size_t i = 0;
    out->n = 0;
    if (strcmp(c, ".") == 0) return 0;
    while (c[i]) {
        unsigned char *set;
        int k;
        if (out->n >= 24) return -1;
        set = out->set[out->n];
        memset(set, 0, 32);
        if (c[i] == '[') {
            int neg = 0;
            i++;
            if (c[i] == '^') { neg = 1; i++; }
            while (c[i] && c[i] != ']') { set[(unsigned char)c[i] >> 3] |= (unsigned char)(1u << ((unsigned char)c[i] & 7)); i++; }
            if (c[i] != ']') return -1;
            i++;
            if (neg) for (k = 0; k < 32; k++) set[k] = (unsigned char)~set[k];
        } else if (c[i] == '.') {
            memset(set, 0xFF, 32);
            i++;
        } else {
            set[(unsigned char)c[i] >> 3] |= (unsigned char)(1u << ((unsigned char)c[i] & 7));
            i++;
        }
        out->n++;
    }
    return 0;
}

static int cond_has(const Cond *c, int pos, unsigned char b) { return (c->set[pos][b >> 3] >> (b & 7)) & 1; }

static int add_rule(ExSpell *s, const Rule *r) {
    if (s->nrules == s->caprules) {
        size_t nc = s->caprules ? s->caprules * 2 : 128;
        Rule *p = realloc(s->rules, nc * sizeof *p);
        if (!p) return -1;
        s->rules = p;
        s->caprules = nc;
    }
    s->rules[s->nrules++] = *r;
    return 0;
}

static int add_pair(Pair **arr, size_t *n, size_t *cap, const char *from, const char *to) {
    Pair p;
    size_t fl = strlen(from);
    p.begin = from[0] == '^';
    p.end = fl > 0 && from[fl - 1] == '$';
    p.from = dup_n(from + p.begin, fl - (size_t)p.begin - (size_t)p.end);
    p.to = dup_n(to, strlen(to));
    if (!p.from || !p.to) return -1;
    {
        char *u;
        for (u = p.from; *u; u++) if (*u == '_') *u = ' ';
        for (u = p.to; *u; u++) if (*u == '_') *u = ' ';
    }
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        Pair *q = realloc(*arr, nc * sizeof *q);
        if (!q) return -1;
        *arr = q;
        *cap = nc;
    }
    (*arr)[(*n)++] = p;
    return 0;
}

/* Splits a line into whitespace separated fields (in place); returns the count. */
static size_t fields(char *line, char **f, size_t max) {
    size_t n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') break;
        f[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

static int is_number(const char *s) { if (!*s) return 0; for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0; return 1; }

static int parse_aff(ExSpell *s, const char *aff, size_t len, char *err, size_t errcap) {
    size_t pos = 0;
    int have_keys = 0, curcross = 1;
    char curflag = 0, curtype = 0;
    while (pos < len) {
        size_t e = pos;
        char line[1024], *f[8];
        size_t n, ll;
        while (e < len && aff[e] != '\n') e++;
        ll = e - pos;
        if (ll >= sizeof line) ll = sizeof line - 1;
        memcpy(line, aff + pos, ll);
        line[ll] = 0;
        if (ll && line[ll - 1] == '\r') line[ll - 1] = 0;
        pos = e + 1;
        if (pos == 1 && (unsigned char)line[0] == 0xEF) memmove(line, line + 3, strlen(line) - 2);     /* byte-order mark */
        n = fields(line, f, 8);
        if (n == 0) continue;
        if (!strcmp(f[0], "SET")) {
            if (n < 2 || (strcmp(f[1], "UTF-8") && strcmp(f[1], "utf-8") && strcmp(f[1], "UTF8"))) { snprintf(err, errcap, "only SET UTF-8 is supported"); return -1; }
        } else if (!strcmp(f[0], "FLAG")) {
            snprintf(err, errcap, "FLAG %s is not supported: only one-character flags", n > 1 ? f[1] : "");
            return -1;
        } else if (!strcmp(f[0], "PFX") || !strcmp(f[0], "SFX")) {
            Rule r;
            if (n == 4 && (!strcmp(f[2], "Y") || !strcmp(f[2], "N")) && is_number(f[3])) {              /* the header of a group of rules */
                curflag = f[1][0]; curtype = f[0][0]; curcross = f[2][0] == 'Y';
                continue;
            }
            if (n < 5) continue;
            memset(&r, 0, sizeof r);
            r.prefix = f[0][0] == 'P';
            r.flag = f[1][0];
            r.cross = f[1][0] == curflag && f[0][0] == curtype ? curcross : 1;
            {
                char *slash = strchr(f[3], '/');
                if (slash) *slash = 0;                              /* continuation classes are not supported: dropped */
            }
            if (strlen(f[2]) > 30 || strlen(f[3]) > 30) continue;
            snprintf(r.strip, sizeof r.strip, "%s", strcmp(f[2], "0") ? f[2] : "");
            snprintf(r.append, sizeof r.append, "%s", strcmp(f[3], "0") ? f[3] : "");
            r.slen = strlen(r.strip); r.alen = strlen(r.append);
            if (parse_cond(f[4], &r.cond)) continue;
            if (add_rule(s, &r)) return -1;
        } else if (!strcmp(f[0], "REP") && n >= 3) {
            if (add_pair(&s->rep, &s->nrep, &s->caprep, f[1], f[2])) return -1;
        } else if (!strcmp(f[0], "ICONV") && n >= 3) {
            if (add_pair(&s->iconv, &s->niconv, &s->capiconv, f[1], f[2])) return -1;
        } else if (!strcmp(f[0], "KEY") && n >= 2) {
            char *row = f[1], *bar;
            have_keys = 1;
            s->nkeys = 0;
            for (;;) {
                bar = strchr(row, '|');
                if (bar) *bar = 0;
                {
                    char **nk = realloc(s->keys, (s->nkeys + 1) * sizeof *nk);
                    if (!nk) return -1;
                    s->keys = nk;
                    s->keys[s->nkeys++] = dup_n(row, strlen(row));
                }
                if (!bar) break;
                row = bar + 1;
            }
        } else if (!strcmp(f[0], "TRY") && n >= 2) {
            size_t i = 0, tl = strlen(f[1]);
            free(s->try_cp);
            s->try_cp = malloc((tl + 1) * sizeof *s->try_cp);
            s->ntry = 0;
            if (!s->try_cp) return -1;
            while (i < tl) { uint32_t cp; i += decode(f[1] + i, tl - i, &cp); s->try_cp[s->ntry++] = cp; }
        } else if (!strcmp(f[0], "NOSUGGEST") && n >= 2) s->nosuggest = f[1][0];
        else if (!strcmp(f[0], "ONLYINCOMPOUND") && n >= 2) s->onlyincompound = f[1][0];
    }
    if (!have_keys) {
        static const char *const def[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
        size_t i;
        s->keys = malloc(3 * sizeof *s->keys);
        if (!s->keys) return -1;
        for (i = 0; i < 3; i++) s->keys[i] = dup_n(def[i], strlen(def[i]));
        s->nkeys = 3;
    }
    if (!s->try_cp) {                                           /* without TRY: the letters of the alphabet */
        size_t i;
        s->try_cp = malloc(27 * sizeof *s->try_cp);
        if (!s->try_cp) return -1;
        for (i = 0; i < 26; i++) s->try_cp[i] = (uint32_t)('a' + i);
        s->try_cp[26] = '\'';
        s->ntry = 27;
    }
    return 0;
}

static int parse_dic(ExSpell *s, const char *dic, size_t len) {
    size_t pos = 0, lineno = 0;
    while (pos < len) {
        size_t e = pos, wl, fl = 0;
        const char *line = dic + pos, *slash = NULL, *p;
        while (e < len && dic[e] != '\n') e++;
        wl = e - pos;
        pos = e + 1;
        if (wl && line[wl - 1] == '\r') wl--;
        lineno++;
        if (lineno == 1) {
            size_t i;
            for (i = 0; i < wl && isdigit((unsigned char)line[i]); i++) {}
            if (i == wl) continue;                           /* the number of entries */
            if (wl >= 3 && (unsigned char)line[0] == 0xEF) { line += 3; wl -= 3; }
        }
        if (wl == 0 || line[0] == '#' || line[0] == '\t') continue;
        for (p = line; p < line + wl; p++) {
            if (*p == '\\' && p + 1 < line + wl) { p++; continue; }
            if (*p == '/' && !slash) slash = p;
            if (*p == ' ' || *p == '\t') { wl = (size_t)(p - line); break; }                 /* morphological fields follow */
        }
        if (slash && slash < line + wl) { fl = (size_t)(line + wl - slash - 1); wl = (size_t)(slash - line); slash++; }
        if (wl == 0) continue;
        if (tab_add(s, line, wl, slash ? slash : "", slash ? fl : 0)) return -1;
    }
    return 0;
}

ExSpell *ex_spell_create(const char *aff, size_t aff_len, const char *dic, size_t dic_len, char *err, size_t errcap) {
    ExSpell *s = calloc(1, sizeof *s);
    char local[128];
    if (!err) { err = local; errcap = sizeof local; }
    err[0] = 0;
    if (!s) return NULL;
    if (parse_aff(s, aff, aff_len, err, errcap) || parse_dic(s, dic, dic_len)) {
        if (!err[0]) snprintf(err, errcap, "out of memory");
        ex_spell_free(s);
        return NULL;
    }
    return s;
}

static void free_pairs(Pair *p, size_t n) { size_t i; for (i = 0; i < n; i++) { free(p[i].from); free(p[i].to); } free(p); }

void ex_spell_free(ExSpell *s) {
    size_t i;
    if (!s) return;
    for (i = 0; i < s->cap; i++) { free(s->tab[i].word); free(s->tab[i].flags); }
    free(s->tab);
    free(s->rules);
    free_pairs(s->rep, s->nrep);
    free_pairs(s->iconv, s->niconv);
    for (i = 0; i < s->nkeys; i++) free(s->keys[i]);
    free(s->keys);
    free(s->try_cp);
    free(s);
}

size_t ex_spell_stem_count(const ExSpell *s) { return s->count; }

/* ---- checking ---- */

static int stem_ok(const ExSpell *s, const Entry *e, char flag, char flag2) {
    if (!e) return 0;
    if (s->onlyincompound && strchr(e->flags, s->onlyincompound)) return 0;
    if (flag && !strchr(e->flags, flag)) return 0;
    if (flag2 && !strchr(e->flags, flag2)) return 0;
    return 1;
}

static int cond_end(const Cond *c, const char *stem, size_t sl) {
    int i;
    if (c->n == 0) return 1;
    if ((size_t)c->n > sl) return 0;
    for (i = 0; i < c->n; i++) if (!cond_has(c, i, (unsigned char)stem[sl - (size_t)c->n + (size_t)i])) return 0;
    return 1;
}

static int cond_start(const Cond *c, const char *stem, size_t sl) {
    int i;
    if (c->n == 0) return 1;
    if ((size_t)c->n > sl) return 0;
    for (i = 0; i < c->n; i++) if (!cond_has(c, i, (unsigned char)stem[i])) return 0;
    return 1;
}

/* Is w a stem, or a stem with affixes? w is exactly as written (case already decided by the caller). */
static int check_exact(const ExSpell *s, const char *w, size_t wl) {
    size_t i, j;
    char stem[MAX_WORD * 2 + 2], stem2[MAX_WORD * 2 + 2];
    if (wl > MAX_WORD) return 0;
    if (stem_ok(s, tab_find(s, w), 0, 0)) return 1;
    for (i = 0; i < s->nrules; i++) {
        const Rule *r = &s->rules[i];
        size_t sl;
        if (r->prefix) {
            if (r->alen > wl || memcmp(w, r->append, r->alen) != 0) continue;
            memcpy(stem, r->strip, r->slen);
            memcpy(stem + r->slen, w + r->alen, wl - r->alen);
            sl = r->slen + wl - r->alen;
            stem[sl] = 0;
            if (sl == 0 || !cond_start(&r->cond, stem, sl)) continue;
            if (stem_ok(s, tab_find(s, stem), r->flag, 0)) return 1;
        } else {
            if (r->alen > wl || memcmp(w + wl - r->alen, r->append, r->alen) != 0) continue;
            memcpy(stem, w, wl - r->alen);
            memcpy(stem + wl - r->alen, r->strip, r->slen);
            sl = wl - r->alen + r->slen;
            stem[sl] = 0;
            if (sl == 0 || !cond_end(&r->cond, stem, sl)) continue;
            if (stem_ok(s, tab_find(s, stem), r->flag, 0)) return 1;
            if (!r->cross) continue;
            for (j = 0; j < s->nrules; j++) {                       /* a prefix as well */
                const Rule *p = &s->rules[j];
                size_t sl2;
                if (!p->prefix || !p->cross || p->alen > sl || memcmp(stem, p->append, p->alen) != 0) continue;
                memcpy(stem2, p->strip, p->slen);
                memcpy(stem2 + p->slen, stem + p->alen, sl - p->alen);
                sl2 = p->slen + sl - p->alen;
                stem2[sl2] = 0;
                if (sl2 == 0 || !cond_start(&p->cond, stem2, sl2)) continue;
                if (stem_ok(s, tab_find(s, stem2), r->flag, p->flag)) return 1;
            }
        }
    }
    return 0;
}

static void apply_iconv(const ExSpell *s, const char *w, size_t wl, char *out, size_t cap) {
    size_t i;
    snprintf(out, cap, "%.*s", (int)wl, w);
    for (i = 0; i < s->niconv; i++) {
        const Pair *p = &s->iconv[i];
        size_t fl = strlen(p->from), tl = strlen(p->to);
        char *at, tmp[MAX_WORD * 3];
        while (fl && (at = strstr(out, p->from)) != NULL) {
            size_t ol = strlen(out);
            if (ol - fl + tl + 1 > cap || ol - fl + tl + 1 > sizeof tmp) break;
            memcpy(tmp, out, (size_t)(at - out));
            memcpy(tmp + (at - out), p->to, tl);
            strcpy(tmp + (at - out) + tl, at + fl);
            strcpy(out, tmp);
        }
    }
}

int ex_spell_check(const ExSpell *s, const char *word, size_t len) {
    char w[MAX_WORD * 3], low[MAX_WORD * 3], cap[MAX_WORD * 3];
    size_t i, wl, nchars = 0, nupper = 0, nlower = 0;
    int first_upper = 0, has_other = 0;
    if (len == 0) return 1;
    if (len > 60) return 1;                                           /* not a word: a long run of letters is left alone */
    for (i = 0; i < len; i++) if (isdigit((unsigned char)word[i])) return 1;      /* words with digits are not checked */
    apply_iconv(s, word, len, w, sizeof w);
    wl = strlen(w);
    for (i = 0; i < wl; ) {
        uint32_t cp;
        i += decode(w + i, wl - i, &cp);
        if (nchars == 0) first_upper = is_upper_cp(cp);
        if (is_upper_cp(cp)) nupper++; else if (is_lower_cp(cp)) nlower++; else has_other++;
        nchars++;
    }
    if (nchars <= 1) return 1;                                        /* one letter: always fine */
    if (check_exact(s, w, wl)) return 1;
    if (nupper == 0) return 0;                                        /* lower case: only exactly */
    if (first_upper && nupper == 1) {                                 /* Capitalized: also the lower case stem */
        map_case(w, wl, low, sizeof low, 0);
        return check_exact(s, low, strlen(low));
    }
    if (nlower == 0) {                                                /* ALL CAPS: lower case and Capitalized stems */
        map_case(w, wl, low, sizeof low, 0);
        if (check_exact(s, low, strlen(low))) return 1;
        map_case(low, strlen(low), cap, sizeof cap, 1);
        return check_exact(s, cap, strlen(cap));
    }
    (void)has_other;
    return 0;
}

int ex_spell_add_word(ExSpell *s, const char *word, size_t len) {
    char low[MAX_WORD * 3];
    if (len == 0 || len > 60) return -1;
    if (tab_add(s, word, len, "", 0)) return -1;
    map_case(word, len, low, sizeof low, 0);
    if (strcmp(low, word) != 0 && tab_add(s, low, strlen(low), "", 0)) return -1;
    return 0;
}

/* ---- suggestions ---- */

typedef struct Sug { char w[EX_SPELL_WORD_MAX]; int pri; } Sug;
typedef struct SugList { Sug v[32]; size_t n; } SugList;

static int suggestible(const ExSpell *s, const char *w, size_t wl) {
    const Entry *e = tab_find(s, w);
    if (s->nosuggest && e && strchr(e->flags, s->nosuggest)) return 0;
    return ex_spell_check(s, w, wl);
}

static void sug_add_checked(const ExSpell *s, SugList *l, const char *w, size_t wl, int pri, int check) {
    size_t i;
    if (wl == 0 || wl >= EX_SPELL_WORD_MAX || l->n >= 32) return;
    for (i = 0; i < l->n; i++) if (!strcmp(l->v[i].w, w)) { if (pri < l->v[i].pri) l->v[i].pri = pri; return; }
    if (check && !suggestible(s, w, wl)) return;
    memcpy(l->v[l->n].w, w, wl + 1);
    l->v[l->n].pri = pri;
    l->n++;
}

static void sug_add(const ExSpell *s, SugList *l, const char *w, size_t wl, int pri) { sug_add_checked(s, l, w, wl, pri, 1); }

static void sug_cps(const ExSpell *s, SugList *l, const uint32_t *cp, size_t n, int pri) {
    char buf[EX_SPELL_WORD_MAX + 8];
    size_t i, w = 0;
    for (i = 0; i < n; i++) { if (w + 4 >= EX_SPELL_WORD_MAX) return; w += encode(cp[i], buf + w); }
    buf[w] = 0;
    sug_add(s, l, buf, w, pri);
}

size_t ex_spell_suggest(const ExSpell *s, const char *word, size_t len, char out[][EX_SPELL_WORD_MAX], size_t max) {
    SugList l;
    char w[MAX_WORD * 3], low[MAX_WORD * 3];
    uint32_t cp[EX_SPELL_WORD_MAX], tmp[EX_SPELL_WORD_MAX + 2];
    size_t n = 0, i, k, t, wl, r;
    int cap_first = 0, all_caps = 0;
    l.n = 0;
    if (len == 0 || len > 40) return 0;
    apply_iconv(s, word, len, w, sizeof w);
    wl = strlen(w);
    for (i = 0; i < wl && n < 40; ) { i += decode(w + i, wl - i, &cp[n]); n++; }
    if (n == 0) return 0;
    cap_first = is_upper_cp(cp[0]);
    {
        size_t up = 0;
        for (i = 0; i < n; i++) if (is_upper_cp(cp[i])) up++;
        all_caps = n > 1 && up == n;
    }
    /* work on the lower case form, and give the capitals back to the suggestions at the end */
    map_case(w, wl, low, sizeof low, 0);
    n = 0;
    for (i = 0, wl = strlen(low); i < wl && n < 40; ) { i += decode(low + i, wl - i, &cp[n]); n++; }
    /* REP: every replacement at every place it fits */
    for (r = 0; r < s->nrep; r++) {
        const Pair *p = &s->rep[r];
        size_t fl = strlen(p->from), tl = strlen(p->to), at;
        for (at = 0; at + fl <= wl; at++) {
            char buf[MAX_WORD * 3];
            if (memcmp(low + at, p->from, fl) != 0) continue;
            if (p->begin && at != 0) continue;
            if (p->end && at + fl != wl) continue;
            if (wl - fl + tl >= sizeof buf) continue;
            memcpy(buf, low, at); memcpy(buf + at, p->to, tl); memcpy(buf + at + tl, low + at + fl, wl - at - fl + 1);
            sug_add(s, &l, buf, strlen(buf), 0);
            {   /* a replacement can make two words */
                char *sp = strchr(buf, ' ');
                if (sp) { *sp = 0; if (ex_spell_check(s, buf, strlen(buf)) && ex_spell_check(s, sp + 1, strlen(sp + 1))) { *sp = ' '; sug_add_checked(s, &l, buf, strlen(buf), 7, 0); } }
            }
        }
    }
    /* a swap of two neighbouring letters */
    for (i = 0; i + 1 < n; i++) { memcpy(tmp, cp, n * sizeof *cp); tmp[i] = cp[i + 1]; tmp[i + 1] = cp[i]; sug_cps(s, &l, tmp, n, 1); }
    /* a key next to the one typed */
    for (i = 0; i < n; i++)
        for (k = 0; k < s->nkeys; k++) {
            const char *row = s->keys[k];
            size_t rl = strlen(row), pos;
            for (pos = 0; pos < rl; pos++) {
                if ((uint32_t)(unsigned char)row[pos] != cp[i]) continue;
                if (pos > 0) { memcpy(tmp, cp, n * sizeof *cp); tmp[i] = (uint32_t)(unsigned char)row[pos - 1]; sug_cps(s, &l, tmp, n, 2); }
                if (pos + 1 < rl) { memcpy(tmp, cp, n * sizeof *cp); tmp[i] = (uint32_t)(unsigned char)row[pos + 1]; sug_cps(s, &l, tmp, n, 2); }
            }
        }
    /* a letter too many */
    for (i = 0; i < n && n > 1; i++) { memcpy(tmp, cp, i * sizeof *cp); memcpy(tmp + i, cp + i + 1, (n - i - 1) * sizeof *cp); sug_cps(s, &l, tmp, n - 1, 3); }
    /* a wrong letter */
    for (i = 0; i < n; i++) for (t = 0; t < s->ntry; t++) {
        if (s->try_cp[t] == cp[i]) continue;
        memcpy(tmp, cp, n * sizeof *cp); tmp[i] = s->try_cp[t]; sug_cps(s, &l, tmp, n, 4);
    }
    /* a letter missing */
    if (n + 1 < EX_SPELL_WORD_MAX / 4)
        for (i = 0; i <= n; i++) for (t = 0; t < s->ntry; t++) {
            memcpy(tmp, cp, i * sizeof *cp); tmp[i] = s->try_cp[t]; memcpy(tmp + i + 1, cp + i, (n - i) * sizeof *cp);
            sug_cps(s, &l, tmp, n + 1, 5);
        }
    /* two words */
    for (i = 1; i < n; i++) {
        char a[MAX_WORD], b[MAX_WORD];
        size_t al = 0, bl = 0, q;
        for (q = 0; q < i; q++) al += encode(cp[q], a + al);
        for (q = i; q < n; q++) bl += encode(cp[q], b + bl);
        a[al] = 0; b[bl] = 0;
        if (al > 1 && bl > 1 && ex_spell_check(s, a, al) && ex_spell_check(s, b, bl) && l.n < 32 && al + bl + 2 < EX_SPELL_WORD_MAX) {
            char both[EX_SPELL_WORD_MAX];
            snprintf(both, sizeof both, "%.*s %.*s", 30, a, 30, b);
            sug_add_checked(s, &l, both, strlen(both), 6, 0);
        }
    }
    /* best first, then in the order they were found */
    for (i = 1; i < l.n; i++) { Sug it = l.v[i]; for (k = i; k > 0 && l.v[k - 1].pri > it.pri; k--) l.v[k] = l.v[k - 1]; l.v[k] = it; }
    for (i = 0; i < l.n && i < max; i++) {
        if (all_caps) map_case(l.v[i].w, strlen(l.v[i].w), out[i], EX_SPELL_WORD_MAX, 2);
        else if (cap_first) map_case(l.v[i].w, strlen(l.v[i].w), out[i], EX_SPELL_WORD_MAX, 1);
        else snprintf(out[i], EX_SPELL_WORD_MAX, "%.*s", EX_SPELL_WORD_MAX - 1, l.v[i].w);
    }
    return i;
}
