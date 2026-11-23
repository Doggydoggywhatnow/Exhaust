/* Slides (6e-iii): see include/exhaust_slides.h. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_slides.h"

/* whether the n bytes at s hold the text w */
static int strstr_n(const char *s, size_t n, const char *w) {
    size_t l = strlen(w), i;
    for (i = 0; i + l <= n; i++) if (!memcmp(s + i, w, l)) return 1;
    return 0;
}

static int is_blank(const char *t, size_t a, size_t b) {
    for (; a < b; a++) if (t[a] != ' ' && t[a] != '\t' && t[a] != '\r' && t[a] != '\n') return 0;
    return 1;
}

static int is_separator(const char *t, size_t a, size_t b) {   /* three or more hyphens and nothing else but spaces */
    size_t i = a, h = 0;
    while (i < b && t[i] == ' ' && i - a < 3) i++;                  /* up to three spaces of indent, as Markdown allows */
    while (i < b && t[i] == '-') { i++; h++; }
    if (h < 3) return 0;
    while (i < b && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r')) i++;
    return i == b;
}

int ex_slides_split(const char *t, size_t n, ExSlide **out, size_t *count) {
    ExSlide *sl = NULL;
    size_t ns = 0, cap = 0, p = 0, start;
    int fence = 0, math = 0, comment = 0;
    char fch = 0;
    size_t flen = 0;
    int prev_blank = 1;     /* the line before the current one is blank (or there is none) */
    *out = NULL; *count = 0;
    if (!t) n = 0;
    /* the properties at the start are not a slide */
    if (n >= 4 && !strncmp(t, "---", 3)) {
        size_t q = 0, le;
        while (q < n && t[q] != '\n') q++;
        if (q < n && is_separator(t, 0, q) && q <= 5) {
            size_t r = q + 1;
            while (r < n) {
                le = r;
                while (le < n && t[le] != '\n') le++;
                if (is_separator(t, r, le) || (le - r >= 3 && !strncmp(t + r, "...", 3) && is_blank(t, r + 3, le))) { p = le < n ? le + 1 : le; break; }
                r = le < n ? le + 1 : le;
            }
        }
    }
    start = p;
#define CLOSE(from, to) do { size_t s0 = (from), e0 = (to); \
        while (s0 < e0) { size_t le2 = s0; while (le2 < e0 && t[le2] != '\n') le2++; if (is_blank(t, s0, le2)) s0 = le2 < e0 ? le2 + 1 : le2; else break; } \
        while (e0 > s0) { size_t ls = e0; if (t[ls - 1] == '\n') ls--; while (ls > s0 && t[ls - 1] != '\n') ls--; if (is_blank(t, ls, e0)) e0 = ls; else break; } \
        if (e0 > s0 && !is_blank(t, s0, e0)) { \
            if (ns == cap) { size_t nc = cap ? cap * 2 : 8; ExSlide *x = realloc(sl, nc * sizeof *x); if (!x) { free(sl); return -1; } sl = x; cap = nc; } \
            while (e0 > s0 && (t[e0 - 1] == '\n' || t[e0 - 1] == '\r')) e0--; \
            sl[ns].start = s0; sl[ns].end = e0; ns++; } } while (0)
    while (p < n) {
        size_t le = p, i;
        int blank;
        while (le < n && t[le] != '\n') le++;
        i = p;
        while (i < le && i - p < 4 && t[i] == ' ') i++;
        blank = is_blank(t, p, le);
        if (comment) { if (le - p >= 3 && memchr(t + p, '>', le - p) && strstr_n(t + p, le - p, "-->")) comment = 0; }
        else if (fence) {
            if (i < le && t[i] == fch) { size_t k = i; while (k < le && t[k] == fch) k++; if (k - i >= flen && is_blank(t, k, le)) fence = 0; }
        } else if (math) {
            if (le - p >= 2 && strstr_n(t + p, le - p, "$$")) math = 0;
        } else {
            if (i + 2 < le && (t[i] == '`' || t[i] == '~') && t[i + 1] == t[i] && t[i + 2] == t[i]) { size_t k = i; while (k < le && t[k] == t[i]) k++; fence = 1; fch = t[i]; flen = k - i; }
            else if (i + 1 < le && t[i] == '$' && t[i + 1] == '$') { size_t k = i + 2; if (!strstr_n(t + k, le - k, "$$")) math = 1; }
            else if (i + 3 < le && !strncmp(t + i, "<!--", 4)) { if (!strstr_n(t + i + 4, le - i - 4, "-->")) comment = 1; }
            else if (prev_blank && is_separator(t, p, le)) {
                CLOSE(start, p);
                start = le < n ? le + 1 : le;
            }
        }
        prev_blank = blank;
        p = le < n ? le + 1 : le;
    }
    CLOSE(start, n);
#undef CLOSE
    *out = sl; *count = ns;
    return 0;
}

void ex_slides_free(ExSlide *s) { free(s); }
