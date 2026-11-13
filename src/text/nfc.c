/* NFC normalization (canonical decomposition, reordering, composition; Hangul done by arithmetic) and case folding. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_nfc.h"
#include "exhaust_text.h"
#include "nfc_data.h"

enum { SBASE = 0xAC00, LBASE = 0x1100, VBASE = 0x1161, TBASE = 0x11A7, LCOUNT = 19, VCOUNT = 21, TCOUNT = 28, NCOUNT = VCOUNT * TCOUNT, SCOUNT = LCOUNT * NCOUNT };

typedef struct Buf { uint32_t *v; size_t n, cap; int oom; } Buf;

static void put(Buf *b, uint32_t c) {
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        uint32_t *nv = realloc(b->v, nc * sizeof *nv);
        if (!nv) { b->oom = 1; return; }
        b->v = nv; b->cap = nc;
    }
    b->v[b->n++] = c;
}

static const ExNfdEntry *find_entry(const ExNfdEntry *t, unsigned n, uint32_t cp) {
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (t[mid].cp < cp) lo = mid + 1; else hi = mid;
    }
    return lo < n && t[lo].cp == cp ? &t[lo] : NULL;
}

static int ccc_of(uint32_t cp) {
    unsigned lo = 0, hi = ex_ccc_count;
    if (cp < 0x300) return 0;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (ex_ccc_table[mid].cp < cp) lo = mid + 1; else hi = mid;
    }
    return lo < ex_ccc_count && ex_ccc_table[lo].cp == cp ? ex_ccc_table[lo].ccc : 0;
}

static void decompose(Buf *b, uint32_t cp) {
    if (cp >= SBASE && cp < SBASE + SCOUNT) {
        uint32_t s = cp - SBASE;
        put(b, LBASE + s / NCOUNT);
        put(b, VBASE + (s % NCOUNT) / TCOUNT);
        if (s % TCOUNT) put(b, TBASE + s % TCOUNT);
        return;
    }
    if (cp >= 0xC0) {
        const ExNfdEntry *e = find_entry(ex_nfd_table, ex_nfd_count, cp);
        if (e) { unsigned i; for (i = 0; i < e->len; i++) decompose(b, ex_nfd_pool[e->off + i]); return; }
    }
    put(b, cp);
}

static uint32_t compose_pair(uint32_t a, uint32_t c) {
    unsigned lo = 0, hi = ex_comp_count;
    if (a >= LBASE && a < LBASE + LCOUNT && c >= VBASE && c < VBASE + VCOUNT) return SBASE + ((a - LBASE) * VCOUNT + (c - VBASE)) * TCOUNT;
    if (a >= SBASE && a < SBASE + SCOUNT && (a - SBASE) % TCOUNT == 0 && c > TBASE && c < TBASE + TCOUNT) return a + (c - TBASE);
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        const ExCompEntry *e = &ex_comp_table[mid];
        if (e->a < a || (e->a == a && e->b < c)) lo = mid + 1; else hi = mid;
    }
    return lo < ex_comp_count && ex_comp_table[lo].a == a && ex_comp_table[lo].b == c ? ex_comp_table[lo].cp : 0;
}

static void reorder(uint32_t *v, size_t n) {
    size_t i = 0;
    while (i < n) {
        size_t j;
        if (ccc_of(v[i]) == 0) { i++; continue; }
        j = i;
        while (j < n && ccc_of(v[j]) != 0) j++;
        for (size_t a = i + 1; a < j; a++) {               /* stable insertion sort by combining class */
            uint32_t x = v[a];
            int cx = ccc_of(x);
            size_t k = a;
            while (k > i && ccc_of(v[k - 1]) > cx) { v[k] = v[k - 1]; k--; }
            v[k] = x;
        }
        i = j;
    }
}

static size_t compose(uint32_t *v, size_t n) {
    size_t out = 0, starter = (size_t)-1, i;
    int last_cc = 0;
    for (i = 0; i < n; i++) {
        uint32_t c = v[i];
        int cc = ccc_of(c);
        if (starter != (size_t)-1) {
            int blocked = (out - 1 != starter) && last_cc >= cc;   /* a character between them with a class at least as high */
            uint32_t p = blocked ? 0 : compose_pair(v[starter], c);
            if (p) { v[starter] = p; continue; }
        }
        if (cc == 0) starter = out;
        last_cc = cc;
        v[out++] = c;
    }
    return out;
}

/* Decodes into code points. Bytes that are not valid UTF-8 become U+DC00 + byte (lone surrogates, which never occur in
 * valid text) and are written back as the original byte. */
static void decode_all(Buf *in, const char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t at = i;
        uint32_t cp = ex_utf8_decode(s, len, &i);
        if (cp == 0xFFFD && !(len - at >= 3 && (unsigned char)s[at] == 0xEF && (unsigned char)s[at + 1] == 0xBF && (unsigned char)s[at + 2] == 0xBD)) {
            put(in, 0xDC00 + (unsigned char)s[at]);
            i = at + 1;
        } else put(in, cp);
    }
}

static char *encode_all(const uint32_t *v, size_t n) {
    char *out = malloc(n * 4 + 1), *p = out;
    size_t i;
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        if (v[i] >= 0xDC00 && v[i] <= 0xDCFF) *p++ = (char)(v[i] - 0xDC00);
        else p += ex_utf8_encode(v[i], p);
    }
    *p = 0;
    return out;
}

static char *nfc_of_cps(Buf *in) {
    Buf d = {0};
    size_t i, n;
    char *out;
    for (i = 0; i < in->n; i++) {
        if (in->v[i] >= 0xDC00 && in->v[i] <= 0xDCFF) put(&d, in->v[i]); else decompose(&d, in->v[i]);
    }
    if (d.oom) { free(d.v); return NULL; }
    reorder(d.v, d.n);
    n = compose(d.v, d.n);
    out = encode_all(d.v, n);
    free(d.v);
    return out;
}

char *ex_nfc(const char *s, size_t len) {
    Buf in = {0};
    char *out;
    decode_all(&in, s, len);
    if (in.oom) { free(in.v); return NULL; }
    out = nfc_of_cps(&in);
    free(in.v);
    return out;
}

char *ex_name_key(const char *s, size_t len) {
    char *a = ex_nfc(s, len), *out;
    Buf in = {0}, f = {0};
    size_t i;
    if (!a) return NULL;
    decode_all(&in, a, strlen(a));
    free(a);
    for (i = 0; i < in.n; i++) {
        const ExNfdEntry *e = in.v[i] >= 'A' ? find_entry(ex_fold_table, ex_fold_count, in.v[i]) : NULL;
        if (e) { unsigned k; for (k = 0; k < e->len; k++) put(&f, ex_fold_pool[e->off + k]); }
        else put(&f, in.v[i]);
    }
    free(in.v);
    if (f.oom) { free(f.v); return NULL; }
    out = nfc_of_cps(&f);
    free(f.v);
    return out;
}
