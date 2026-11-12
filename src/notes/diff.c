#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_notes.h"

typedef struct Lines { const char **p; size_t *len; size_t n; } Lines;

static int split(const char *s, Lines *l) {
    size_t cap = 64, i = 0, start = 0, n = strlen(s);
    l->p = malloc(cap * sizeof *l->p);
    l->len = malloc(cap * sizeof *l->len);
    l->n = 0;
    if (!l->p || !l->len) return -1;
    while (start < n || (start == n && 0)) {
        const char *nl = memchr(s + start, '\n', n - start);
        size_t end = nl ? (size_t)(nl - s) : n;
        if (l->n == cap) {
            cap *= 2;
            l->p = realloc(l->p, cap * sizeof *l->p);
            l->len = realloc(l->len, cap * sizeof *l->len);
            if (!l->p || !l->len) return -1;
        }
        l->p[l->n] = s + start;
        l->len[l->n++] = end - start;
        start = nl ? end + 1 : n;
        (void)i;
    }
    return 0;
}

static int same(const Lines *a, size_t i, const Lines *b, size_t j) {
    return a->len[i] == b->len[j] && memcmp(a->p[i], b->p[j], a->len[i]) == 0;
}

typedef struct Out { char *p; size_t n, cap; } Out;
static void put(Out *o, const char *s, size_t n) {
    if (o->n + n + 1 > o->cap) { o->cap = (o->n + n + 1) * 2; o->p = realloc(o->p, o->cap); }
    if (o->p) { memcpy(o->p + o->n, s, n); o->n += n; o->p[o->n] = 0; }
}

/* op: ' ' same, '-' only in a, '+' only in b */
typedef struct Op { char c; size_t a, b; } Op;

char *ex_diff_unified(const char *sa, const char *sb, const char *label_a, const char *label_b) {
    Lines a = {0}, b = {0};
    size_t pre = 0, suf = 0, na, nb, i, j, nops = 0, cap;
    Op *ops = NULL;
    Out o = {0};
    unsigned *dp = NULL;
    char *result;
    if (strcmp(sa, sb) == 0) { result = malloc(1); if (result) result[0] = 0; return result; }
    if (split(sa, &a) || split(sb, &b)) { free(a.p); free(a.len); free(b.p); free(b.len); return NULL; }
    while (pre < a.n && pre < b.n && same(&a, pre, &b, pre)) pre++;
    while (suf < a.n - pre && suf < b.n - pre && same(&a, a.n - 1 - suf, &b, b.n - 1 - suf)) suf++;
    na = a.n - pre - suf;
    nb = b.n - pre - suf;
    cap = a.n + b.n + 8;
    ops = malloc(cap * sizeof *ops);
    if (!ops) goto done;
    for (i = 0; i < pre; i++) { ops[nops].c = ' '; ops[nops].a = i; ops[nops].b = i; nops++; }
    if ((double)na * (double)nb <= 4000000.0 && na > 0 && nb > 0) {
        dp = malloc((na + 1) * (nb + 1) * sizeof *dp);
        if (!dp) goto done;
        for (i = na + 1; i-- > 0;)
            for (j = nb + 1; j-- > 0;) {
                if (i == na || j == nb) dp[i * (nb + 1) + j] = 0;
                else if (same(&a, pre + i, &b, pre + j)) dp[i * (nb + 1) + j] = dp[(i + 1) * (nb + 1) + j + 1] + 1;
                else { unsigned x = dp[(i + 1) * (nb + 1) + j], y = dp[i * (nb + 1) + j + 1]; dp[i * (nb + 1) + j] = x > y ? x : y; }
            }
        i = j = 0;
        while (i < na || j < nb) {
            if (i < na && j < nb && same(&a, pre + i, &b, pre + j)) { ops[nops].c = ' '; ops[nops].a = pre + i; ops[nops].b = pre + j; nops++; i++; j++; }
            else if (j >= nb || (i < na && dp[(i + 1) * (nb + 1) + j] >= dp[i * (nb + 1) + j + 1])) { ops[nops].c = '-'; ops[nops].a = pre + i; ops[nops].b = pre + j; nops++; i++; }
            else { ops[nops].c = '+'; ops[nops].a = pre + i; ops[nops].b = pre + j; nops++; j++; }
        }
    } else { /* a large or one-sided change: show the middle as removed and added as a whole */
        for (i = 0; i < na; i++) { ops[nops].c = '-'; ops[nops].a = pre + i; ops[nops].b = pre; nops++; }
        for (j = 0; j < nb; j++) { ops[nops].c = '+'; ops[nops].a = pre + na; ops[nops].b = pre + j; nops++; }
    }
    for (i = 0; i < suf; i++) { ops[nops].c = ' '; ops[nops].a = a.n - suf + i; ops[nops].b = b.n - suf + i; nops++; }

    {   /* hunks with three lines of context */
        char head[512];
        size_t k = 0;
        snprintf(head, sizeof head, "--- %s\n+++ %s\n", label_a, label_b);
        put(&o, head, strlen(head));
        while (k < nops) {
            size_t s, e, last_change, la = 0, lb = 0, ca = 0, cb = 0, x;
            while (k < nops && ops[k].c == ' ') k++;
            if (k >= nops) break;
            s = k >= 3 ? k - 3 : 0;
            e = k;
            last_change = k;
            for (x = k; x < nops; x++) {
                if (ops[x].c != ' ') last_change = x;
                else if (x > last_change + 6) break;
                e = x + 1;
            }
            if (last_change + 4 < e) e = last_change + 4;
            if (e > nops) e = nops;
            for (x = s; x < e; x++) { if (ops[x].c != '+') ca++; if (ops[x].c != '-') cb++; }
            la = ops[s].a + 1; lb = ops[s].b + 1;
            snprintf(head, sizeof head, "@@ -%zu,%zu +%zu,%zu @@\n", la, ca, lb, cb);
            put(&o, head, strlen(head));
            for (x = s; x < e; x++) {
                char c = ops[x].c;
                const char *line = c == '+' ? b.p[ops[x].b] : a.p[ops[x].a];
                size_t len = c == '+' ? b.len[ops[x].b] : a.len[ops[x].a];
                put(&o, &c, 1);
                put(&o, line, len);
                put(&o, "\n", 1);
            }
            k = e;
        }
    }
done:
    free(dp); free(ops); free(a.p); free(a.len); free(b.p); free(b.len);
    if (!o.p) { o.p = malloc(1); if (o.p) o.p[0] = 0; }
    return o.p;
}
