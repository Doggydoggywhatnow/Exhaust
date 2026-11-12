#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_find.h"
#include "pcre2.h"

struct ExFind {
    pcre2_code *code;
    pcre2_match_context *mctx;
    pcre2_match_data *md;
    int regex;
};

#define MATCH_LIMIT 2000000u

/* A plain text pattern as a regular expression: every character that could mean something is escaped. */
static char *escape_plain(const char *p, size_t len, size_t *out) {
    char *s = malloc(len * 2 + 1);
    size_t i, n = 0;
    if (!s) return NULL;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x80 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ' ')) {
            if (c == '\n') { s[n++] = '\\'; s[n++] = 'n'; continue; }
            if (c == '\r') { s[n++] = '\\'; s[n++] = 'r'; continue; }
            if (c == '\t') { s[n++] = '\\'; s[n++] = 't'; continue; }
            if (c < 0x20 || c == 0x7f) { s[n++] = (char)c; continue; }      /* other control characters stand for themselves */
            s[n++] = '\\';
        }
        s[n++] = (char)c;
    }
    s[n] = 0;
    *out = n;
    return s;
}

ExFind *ex_find_compile(const char *pattern, size_t len, const ExFindOptions *opt, char *err, size_t errcap) {
    ExFind *f;
    pcre2_compile_context *cc;
    char *pat, *full;
    size_t plen, flen;
    int rc;
    PCRE2_SIZE eoff;
    uint32_t flags = PCRE2_UTF | PCRE2_UCP | PCRE2_MULTILINE;
    static const char *WORD_L = "(?<![\\p{L}\\p{N}_])(?:", *WORD_R = ")(?![\\p{L}\\p{N}_])";
    if (err && errcap) err[0] = 0;
    if (!pattern || len == 0) { if (err && errcap) snprintf(err, errcap, "Nothing to search for"); return NULL; }
    if (!opt->match_case) flags |= PCRE2_CASELESS;
    if (opt->regex) { pat = malloc(len + 1); if (pat) { memcpy(pat, pattern, len); pat[len] = 0; plen = len; } }
    else pat = escape_plain(pattern, len, &plen);
    if (!pat) return NULL;
    if (opt->whole_word) {
        flen = plen + strlen(WORD_L) + strlen(WORD_R);
        full = malloc(flen + 1);
        if (!full) { free(pat); return NULL; }
        memcpy(full, WORD_L, strlen(WORD_L));
        memcpy(full + strlen(WORD_L), pat, plen);
        memcpy(full + strlen(WORD_L) + plen, WORD_R, strlen(WORD_R) + 1);
        free(pat);
        pat = full;
        plen = flen;
    }
    f = calloc(1, sizeof *f);
    cc = pcre2_compile_context_create(NULL);
    if (!f || !cc) { free(f); pcre2_compile_context_free(cc); free(pat); return NULL; }
    pcre2_set_newline(cc, PCRE2_NEWLINE_ANYCRLF);
    f->code = pcre2_compile((PCRE2_SPTR)pat, plen, flags, &rc, &eoff, cc);
    pcre2_compile_context_free(cc);
    free(pat);
    if (!f->code) {
        if (err && errcap) {
            PCRE2_UCHAR buf[256];
            pcre2_get_error_message(rc, buf, sizeof buf);
            snprintf(err, errcap, "%s", (const char *)buf);
        }
        free(f);
        return NULL;
    }
    f->regex = opt->regex != 0;
    f->mctx = pcre2_match_context_create(NULL);
    f->md = pcre2_match_data_create_from_pattern(f->code, NULL);
    if (!f->mctx || !f->md) { ex_find_free(f); return NULL; }
    pcre2_set_match_limit(f->mctx, MATCH_LIMIT);
    pcre2_set_depth_limit(f->mctx, MATCH_LIMIT);
    pcre2_set_heap_limit(f->mctx, 20000);          /* kilobytes */
    return f;
}

void ex_find_free(ExFind *f) {
    if (!f) return;
    pcre2_match_data_free(f->md);
    pcre2_match_context_free(f->mctx);
    pcre2_code_free(f->code);
    free(f);
}

static ExFindResult run(ExFind *f, const char *text, size_t len, size_t from, uint32_t flags, size_t *s, size_t *e) {
    int rc = pcre2_match(f->code, (PCRE2_SPTR)text, len, from, flags, f->md, f->mctx);
    PCRE2_SIZE *ov;
    if (rc == PCRE2_ERROR_NOMATCH) return EX_FIND_NONE;
    if (rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT || rc == PCRE2_ERROR_HEAPLIMIT) return EX_FIND_LIMIT;
    if (rc < 0) return rc <= PCRE2_ERROR_UTF8_ERR1 && rc >= PCRE2_ERROR_UTF8_ERR21 ? EX_FIND_BAD_TEXT : EX_FIND_LIMIT;
    ov = pcre2_get_ovector_pointer(f->md);
    if (ov[0] > ov[1]) return EX_FIND_NONE;       /* \K used in a lookbehind: not a usable match */
    *s = ov[0];
    *e = ov[1];
    return EX_FIND_MATCH;
}

ExFindResult ex_find_next(ExFind *f, const char *text, size_t len, size_t from, size_t *start, size_t *end) {
    if (from > len) return EX_FIND_NONE;
    return run(f, text, len, from, 0, start, end);
}

/* The next character boundary after off (UTF-8: skips continuation bytes). */
static size_t advance(const char *text, size_t len, size_t off) {
    off++;
    while (off < len && ((unsigned char)text[off] & 0xC0) == 0x80) off++;
    return off;
}

ExFindResult ex_find_prev(ExFind *f, const char *text, size_t len, size_t before, size_t *start, size_t *end) {
    size_t pos = 0, s, e, bs = 0, be = 0;
    int have = 0;
    ExFindResult r = run(f, text, len, 0, 0, &s, &e);                 /* validates the text once */
    if (r < 0) return r;
    while (r == EX_FIND_MATCH && s < before) {
        have = 1; bs = s; be = e;
        pos = e > s ? e : advance(text, len, s);
        if (pos > len) break;
        r = run(f, text, len, pos, PCRE2_NO_UTF_CHECK, &s, &e);
        if (r < 0) return r;
    }
    if (!have) return EX_FIND_NONE;
    *start = bs; *end = be;
    return EX_FIND_MATCH;
}

size_t ex_find_count(ExFind *f, const char *text, size_t len, size_t limit) {
    size_t n = 0, pos = 0, s, e;
    uint32_t flags = 0;
    for (;;) {
        ExFindResult r = run(f, text, len, pos, flags, &s, &e);
        if (r != EX_FIND_MATCH) break;
        flags = PCRE2_NO_UTF_CHECK;
        if (++n >= limit) break;
        pos = e > s ? e : advance(text, len, s);
        if (pos > len) break;
    }
    return n;
}

typedef struct Out { char *p; size_t n, cap; int oom; } Out;
static void out_add(Out *o, const char *s, size_t n) {
    if (o->oom) return;
    if (o->n + n + 1 > o->cap) {
        size_t nc = (o->cap ? o->cap * 2 : 64) + n;
        char *q = realloc(o->p, nc);
        if (!q) { o->oom = 1; return; }
        o->p = q;
        o->cap = nc;
    }
    memcpy(o->p + o->n, s, n);
    o->n += n;
    o->p[o->n] = 0;
}

char *ex_find_replacement(ExFind *f, const char *text, size_t len, size_t start, size_t end, const char *repl, size_t rlen, size_t *outlen) {
    Out o = { NULL, 0, 0, 0 };
    size_t i;
    PCRE2_SIZE *ov = NULL;
    uint32_t ngroups = 0;
    int have_groups = 0;
    (void)len;
    if (f->regex) {
        size_t s2, e2;
        /* the groups of this match: match again exactly at start (the match data of the last search may be another) */
        if (run(f, text, len, start, PCRE2_NO_UTF_CHECK | PCRE2_ANCHORED, &s2, &e2) == EX_FIND_MATCH && s2 == start && e2 == end) {
            ov = pcre2_get_ovector_pointer(f->md);
            pcre2_pattern_info(f->code, PCRE2_INFO_CAPTURECOUNT, &ngroups);
            have_groups = 1;
        }
    }
    if (!f->regex) {
        out_add(&o, repl, rlen);
    } else {
        for (i = 0; i < rlen; i++) {
            char c = repl[i];
            if (c == '\\' && i + 1 < rlen) {
                char d = repl[++i];
                if (d == 'n') out_add(&o, "\n", 1);
                else if (d == 't') out_add(&o, "\t", 1);
                else if (d == 'r') out_add(&o, "\r", 1);
                else out_add(&o, &d, 1);                               /* \\ and \$ and any other escaped character */
            } else if (c == '$' && i + 1 < rlen) {
                char d = repl[i + 1];
                size_t g = (size_t)-1, adv = 0;
                if (d == '$') { out_add(&o, "$", 1); i++; continue; }
                if (d >= '0' && d <= '9') { g = (size_t)(d - '0'); adv = 1; }
                else if (d == '{') {
                    size_t j = i + 2, v = 0;
                    while (j < rlen && repl[j] >= '0' && repl[j] <= '9' && v < 1000) { v = v * 10 + (size_t)(repl[j] - '0'); j++; }
                    if (j < rlen && repl[j] == '}' && j > i + 2) { g = v; adv = j - i; }
                }
                if (g == (size_t)-1) { out_add(&o, &c, 1); continue; }
                i += adv;
                if (g == 0) out_add(&o, text + start, end - start);
                else if (have_groups && g <= ngroups && ov[2 * g] != PCRE2_UNSET) out_add(&o, text + ov[2 * g], ov[2 * g + 1] - ov[2 * g]);
                /* a group that did not take part, or does not exist, gives nothing */
            } else {
                out_add(&o, &c, 1);
            }
        }
    }
    if (o.oom) { free(o.p); return NULL; }
    if (!o.p) { o.p = malloc(1); if (o.p) o.p[0] = 0; }
    if (outlen) *outlen = o.n;
    return o.p;
}

void ex_find_edits_free(ExFindEdit *edits, size_t n) {
    size_t i;
    if (!edits) return;
    for (i = 0; i < n; i++) free(edits[i].text);
    free(edits);
}

size_t ex_find_replace_all(ExFind *f, const char *text, size_t len, const char *repl, size_t rlen, ExFindEdit **edits) {
    size_t n = 0, cap = 0, pos = 0, s, e;
    uint32_t flags = 0;
    ExFindEdit *list = NULL;
    *edits = NULL;
    for (;;) {
        ExFindResult r = run(f, text, len, pos, flags, &s, &e);
        ExFindEdit *ne;
        if (r != EX_FIND_MATCH) break;
        flags = PCRE2_NO_UTF_CHECK;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            ne = realloc(list, cap * sizeof *list);
            if (!ne) { ex_find_edits_free(list, n); return 0; }
            list = ne;
        }
        list[n].offset = s;
        list[n].remove_len = e - s;
        list[n].text = ex_find_replacement(f, text, len, s, e, repl, rlen, &list[n].text_len);
        if (!list[n].text) { ex_find_edits_free(list, n); return 0; }
        n++;
        pos = e > s ? e : advance(text, len, s);
        if (pos > len) break;
    }
    /* an edit that changes nothing is dropped, so a replace that does not change the text leaves the note as it was */
    {
        size_t i, k = 0;
        for (i = 0; i < n; i++) {
            if (list[i].text_len == list[i].remove_len && memcmp(list[i].text, text + list[i].offset, list[i].remove_len) == 0) { free(list[i].text); continue; }
            list[k++] = list[i];
        }
        n = k;
    }
    if (n == 0) { free(list); return 0; }
    *edits = list;
    return n;
}
