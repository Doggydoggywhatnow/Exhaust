#define _POSIX_C_SOURCE 200809L
/* File recovery snapshots: see include/exhaust_history.h and "File recovery snapshots" in docs/formats.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exhaust_history.h"
#include "exhaust_notes.h"
#include "exhaust_platform.h"

enum { STAMP_LEN = 15, NAME_MAX_ENC = 150 };

static int local_tm(int64_t t, struct tm *out) {
    time_t tt = (time_t)t;
#ifdef _WIN32
    return localtime_s(out, &tt) == 0 ? 0 : -1;
#else
    return localtime_r(&tt, out) ? 0 : -1;
#endif
}

static void stamp_of(int64_t t, char out[STAMP_LEN + 1]) {
    struct tm tmv;
    if (local_tm(t, &tmv) != 0) { snprintf(out, STAMP_LEN + 1, "00000000-000000"); return; }
    strftime(out, STAMP_LEN + 1, "%Y%m%d-%H%M%S", &tmv);
}

static int64_t time_of_stamp(const char *s) {
    struct tm tmv;
    int i;
    for (i = 0; i < STAMP_LEN; i++) if (i == 8 ? s[i] != '-' : (s[i] < '0' || s[i] > '9')) return -1;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0') - 1900;
    tmv.tm_mon = (s[4] - '0') * 10 + (s[5] - '0') - 1;
    tmv.tm_mday = (s[6] - '0') * 10 + (s[7] - '0');
    tmv.tm_hour = (s[9] - '0') * 10 + (s[10] - '0');
    tmv.tm_min = (s[11] - '0') * 10 + (s[12] - '0');
    tmv.tm_sec = (s[13] - '0') * 10 + (s[14] - '0');
    tmv.tm_isdst = -1;
    return (int64_t)mktime(&tmv);
}

char *ex_history_format_time(int64_t t, char *buf, size_t cap) {
    struct tm tmv;
    if (!buf || !cap) return buf;
    if (local_tm(t, &tmv) != 0 || strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tmv) == 0) snprintf(buf, cap, "unknown time");
    return buf;
}

size_t ex_history_encode(const char *rel, char *out, size_t cap) {
    size_t i, k = 0, n;
    if (!rel || !*rel || cap < 4) return 0;
    n = strlen(rel);
    for (i = 0; i < n; i++) {
        if (rel[i] == '/') { if (k + 2 >= cap) return 0; out[k++] = '_'; out[k++] = '_'; }
        else if (rel[i] == '_') { if (k + 2 >= cap) return 0; out[k++] = '_'; out[k++] = 'u'; }
        else { if (k + 1 >= cap) return 0; out[k++] = rel[i]; }
    }
    out[k] = 0;
    if (k > NAME_MAX_ENC) {      /* too long for a file name: the start of it and a hash of the whole path, still one name for one note */
        uint8_t h[16];
        static const char hex[] = "0123456789abcdef";
        size_t cut = NAME_MAX_ENC - 33;                                         /* room for the "~" and 32 hex digits: the name never passes NAME_MAX_ENC */
        int j;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;     /* not in the middle of a character */
        ex_hash16(rel, n, h);
        out[cut++] = '~';
        for (j = 0; j < 16; j++) { out[cut++] = hex[h[j] >> 4]; out[cut++] = hex[h[j] & 15]; }
        out[cut] = 0;
        k = cut;
    }
    return k;
}

static const char *kind_ext(int kind) { return kind == EX_SNAP_CONFLICT ? "bak" : "snap"; }

int ex_history_take(const char *dir, const char *rel, const void *bytes, size_t len, int64_t now, ExSnapKind kind) {
    char enc[700], stamp[STAMP_LEN + 1], *path;
    size_t k = ex_history_encode(rel, enc, sizeof enc);
    int rc;
    if (!k || !dir) return -1;
    stamp_of(now, stamp);
    path = malloc(strlen(dir) + k + 40);
    if (!path) return -1;
    sprintf(path, "%s/%s.%s.%s", dir, enc, stamp, kind_ext(kind));
    ex_platform_make_dirs(dir);
    rc = ex_platform_write_file_atomic(path, bytes ? bytes : "", len);
    free(path);
    return rc == 0 ? 0 : -1;
}

/* Parses "<anything>.<stamp>.<snap|bak>"; fills the stamp's time and the kind; *enc_len is the length of the part before the stamp's dot. */
static int parse_name(const char *name, int64_t *t, int *kind, size_t *enc_len) {
    size_t n = strlen(name), extlen;
    const char *ext;
    int64_t tt;
    if (n > 5 && !strcmp(name + n - 5, ".snap")) { *kind = EX_SNAP_AUTO; extlen = 5; ext = name + n - 5; }
    else if (n > 4 && !strcmp(name + n - 4, ".bak")) { *kind = EX_SNAP_CONFLICT; extlen = 4; ext = name + n - 4; }
    else return -1;
    (void)ext;
    if (n < extlen + STAMP_LEN + 2) return -1;
    if (name[n - extlen - STAMP_LEN - 1] != '.') return -1;
    tt = time_of_stamp(name + n - extlen - STAMP_LEN);
    if (tt < 0) return -1;
    *t = tt;
    *enc_len = n - extlen - STAMP_LEN - 1;
    return 0;
}

typedef struct ListCtx {
    const char *enc; size_t enc_len;     /* the note, or NULL for every note */
    ExSnapshot *items; size_t n, cap;
    const char *root;
    int only_auto;
    size_t removed;
    int64_t cutoff;                      /* prune: delete the automatic ones older than this */
    int mode;                            /* 0 list, 1 delete (clear), 2 delete (prune) */
} ListCtx;

static const char *base_of(const char *path) {
    const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : path;
}

static int walk_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    ListCtx *c = user;
    const char *name;
    int64_t t;
    int kind;
    size_t el;
    (void)mtime;
    if (is_dir) return 0;
    name = base_of(path);
    if (parse_name(name, &t, &kind, &el) != 0) return 0;
    if (c->enc && (el != c->enc_len || memcmp(name, c->enc, el) != 0)) return 0;
    if (c->mode == 0) {
        ExPathInfo info;
        if (c->n == c->cap) {
            size_t nc = c->cap ? c->cap * 2 : 16;
            ExSnapshot *ni = realloc(c->items, nc * sizeof *ni);
            if (!ni) return 1;
            c->items = ni; c->cap = nc;
        }
        c->items[c->n].file = malloc(strlen(name) + 1);
        if (!c->items[c->n].file) return 1;
        strcpy(c->items[c->n].file, name);
        c->items[c->n].time = t;
        c->items[c->n].kind = kind;
        c->items[c->n].size = ex_platform_path_info(path, &info) == 0 ? info.size : 0;
        c->n++;
    } else if (kind == EX_SNAP_AUTO && (c->mode == 1 || t < c->cutoff)) {
        if (ex_platform_remove_tree(path) == 0) c->removed++;
    }
    return 0;
}

static int by_newest(const void *a, const void *b) {
    const ExSnapshot *x = a, *y = b;
    if (x->time != y->time) return x->time < y->time ? 1 : -1;
    if (x->kind != y->kind) return x->kind < y->kind ? 1 : -1;
    return strcmp(y->file, x->file);
}

size_t ex_history_list(const char *dir, const char *rel, ExSnapshot **out) {
    ListCtx c;
    char enc[700];
    size_t k;
    *out = NULL;
    if (!dir) return 0;
    k = ex_history_encode(rel, enc, sizeof enc);
    if (!k) return 0;
    memset(&c, 0, sizeof c);
    c.enc = enc; c.enc_len = k; c.root = dir;
    ex_platform_walk(dir, walk_cb, &c);
    if (c.n > 1) qsort(c.items, c.n, sizeof *c.items, by_newest);
    *out = c.items;
    return c.n;
}

void ex_history_free(ExSnapshot *list, size_t n) {
    size_t i;
    if (!list) return;
    for (i = 0; i < n; i++) free(list[i].file);
    free(list);
}

int64_t ex_history_last_auto(const char *dir, const char *rel) {
    ExSnapshot *l;
    size_t n = ex_history_list(dir, rel, &l), i;
    int64_t best = 0;
    for (i = 0; i < n; i++) if (l[i].kind == EX_SNAP_AUTO && l[i].time > best) best = l[i].time;
    ex_history_free(l, n);
    return best;
}

char *ex_history_read(const char *dir, const char *file, size_t *len) {
    int64_t t;
    int kind;
    size_t el, n;
    char *path, *data;
    if (!dir || !file || strchr(file, '/') || strchr(file, '\\') || strstr(file, "..") || parse_name(file, &t, &kind, &el) != 0) return NULL;
    n = strlen(dir) + strlen(file) + 2;
    path = malloc(n);
    if (!path) return NULL;
    snprintf(path, n, "%s/%s", dir, file);
    data = ex_platform_read_file(path, len);
    free(path);
    return data;
}

size_t ex_history_prune(const char *dir, int64_t now, int days) {
    ListCtx c;
    if (!dir) return 0;
    if (days < 1) days = 1;
    memset(&c, 0, sizeof c);
    c.mode = 2;
    c.cutoff = now - (int64_t)days * 86400;
    ex_platform_walk(dir, walk_cb, &c);
    return c.removed;
}

size_t ex_history_clear(const char *dir, const char *rel) {
    ListCtx c;
    char enc[700];
    if (!dir) return 0;
    memset(&c, 0, sizeof c);
    c.mode = 1;
    if (rel) {
        c.enc_len = ex_history_encode(rel, enc, sizeof enc);
        if (!c.enc_len) return 0;
        c.enc = enc;
    }
    ex_platform_walk(dir, walk_cb, &c);
    return c.removed;
}
