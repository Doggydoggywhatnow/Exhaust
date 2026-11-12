/* Words in a text, and the word count. */
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_atomic.h>

#include "exhaust_markdown.h"
#include "exhaust_spell.h"
#include "pcre2.h"

static pcre2_code *g_words;       /* compiled once, shared by every thread (matching with a compiled pattern is thread-safe) */

static pcre2_code *words_code(void) {
    pcre2_code *c = (pcre2_code *)SDL_GetAtomicPointer((void **)&g_words), *n;
    int rc;
    PCRE2_SIZE eo;
    static const char *PAT =
        "[\\p{Han}\\p{Hiragana}\\p{Katakana}]"
        "|(?:(?![\\p{Han}\\p{Hiragana}\\p{Katakana}])[\\p{L}\\p{N}\\p{M}])+"
        "(?:['\xE2\x80\x99\\-](?:(?![\\p{Han}\\p{Hiragana}\\p{Katakana}])[\\p{L}\\p{N}\\p{M}])+)*";
    if (c) return c;
    n = pcre2_compile((PCRE2_SPTR)PAT, PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_UCP, &rc, &eo, NULL);
    if (!n) return NULL;
    if (!SDL_CompareAndSwapAtomicPointer((void **)&g_words, NULL, n)) { pcre2_code_free(n); return (pcre2_code *)SDL_GetAtomicPointer((void **)&g_words); }
    return n;
}

int ex_words_next(const char *text, size_t len, size_t *pos, size_t *start, size_t *end) {
    pcre2_code *c = words_code();
    pcre2_match_data *md;
    PCRE2_SIZE *ov;
    int rc;
    if (!c || *pos >= len) return 0;
    md = pcre2_match_data_create_from_pattern(c, NULL);
    if (!md) return 0;
    rc = pcre2_match(c, (PCRE2_SPTR)text, len, *pos, 0, md, NULL);       /* invalid UTF-8 ends the search: nothing is a word then */
    if (rc < 0) { pcre2_match_data_free(md); *pos = len; return 0; }
    ov = pcre2_get_ovector_pointer(md);
    *start = ov[0];
    *end = ov[1];
    *pos = ov[1] > ov[0] ? ov[1] : ov[0] + 1;
    pcre2_match_data_free(md);
    return 1;
}

typedef struct Skip { size_t s, e; } Skip;
typedef struct SkipList { Skip *v; size_t n, cap; int oom; } SkipList;

static int collect(const ExMdSpan *sp, void *user) {
    SkipList *l = user;
    switch (sp->kind) {
    case EX_MD_FRONTMATTER: case EX_MD_COMMENT_BLOCK: case EX_MD_COMMENT_INLINE: case EX_MD_MARKER: case EX_MD_LINK_DEST:
    case EX_MD_CODE_INFO: case EX_MD_EMBED: case EX_MD_IMAGE: case EX_MD_HTML_INLINE: case EX_MD_HTML_BLOCK: case EX_MD_BLOCK_ID:
        break;
    default:
        return 0;
    }
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 256;
        Skip *p = realloc(l->v, nc * sizeof *p);
        if (!p) { l->oom = 1; return 1; }
        l->v = p;
        l->cap = nc;
    }
    l->v[l->n].s = sp->start;
    l->v[l->n].e = sp->end;
    l->n++;
    return 0;
}

static int cmp_skip(const void *a, const void *b) {
    const Skip *x = a, *y = b;
    return x->s < y->s ? -1 : x->s > y->s ? 1 : (x->e < y->e ? 1 : x->e > y->e ? -1 : 0);
}

ExWordCount ex_wordcount(const char *text, size_t len, const struct ExMd *md) {
    ExWordCount wc = { 0, 0 };
    SkipList sk = { NULL, 0, 0, 0 };
    size_t pos = 0, s, e, i, k = 0, reach = 0;
    if (md) {
        ex_md_visit(md, 0, len, collect, &sk);
        if (sk.oom) { free(sk.v); sk.v = NULL; sk.n = 0; }
        else qsort(sk.v, sk.n, sizeof *sk.v, cmp_skip);
    }
    /* merge: `reach` is how far the skipped ranges so far extend */
    while (ex_words_next(text, len, &pos, &s, &e)) {
        while (k < sk.n && sk.v[k].s <= s) { if (sk.v[k].e > reach) reach = sk.v[k].e; k++; }
        if (s < reach) continue;                                        /* the word lies in a skipped range */
        wc.words++;
    }
    k = 0; reach = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        while (k < sk.n && sk.v[k].s <= i) { if (sk.v[k].e > reach) reach = sk.v[k].e; k++; }
        if (i < reach) continue;
        if (c == '\n' || c == '\r') continue;
        if ((c & 0xC0) == 0x80) continue;                               /* a continuation byte: part of a character already counted */
        wc.characters++;
    }
    free(sk.v);
    return wc;
}
