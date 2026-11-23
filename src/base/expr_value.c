/* Values of the Bases expression language: the arena, constructors, text, equality and order. See include/exhaust_base_expr.h. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base_expr.h"
#include "exhaust_datefmt.h"

/* ---- the arena ---- */

typedef struct Block { struct Block *next; size_t used, cap; } Block;
struct ExArena { Block *head; };

ExArena *ex_arena_new(void) { return calloc(1, sizeof(ExArena)); }
static void arena_release(ExArena *a) { Block *b = a->head; while (b) { Block *n = b->next; free(b); b = n; } a->head = NULL; }
void ex_arena_free(ExArena *a) { if (!a) return; arena_release(a); free(a); }
void ex_arena_reset(ExArena *a) { if (a) arena_release(a); }

void *ex_arena_alloc(ExArena *a, size_t n) {
    Block *b;
    void *p;
    n = (n + 15u) & ~(size_t)15u;
    if (!n) n = 16;
    b = a->head;
    if (!b || b->cap - b->used < n) {
        size_t cap = n > 16384 ? n : 16384;
        Block *nb = malloc(sizeof *nb + cap);
        if (!nb) return NULL;
        nb->next = a->head; nb->used = 0; nb->cap = cap;
        a->head = b = nb;
    }
    p = (char *)(b + 1) + b->used;
    b->used += n;
    memset(p, 0, n);
    return p;
}
char *ex_arena_strndup(ExArena *a, const char *s, size_t n) { char *p = ex_arena_alloc(a, n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }
char *ex_arena_strdup(ExArena *a, const char *s) { return ex_arena_strndup(a, s, strlen(s)); }

/* ---- constructors ---- */

ExValue ex_v_null(void) { ExValue v; memset(&v, 0, sizeof v); v.t = EXV_NULL; return v; }
ExValue ex_v_bool(int b) { ExValue v = ex_v_null(); v.t = EXV_BOOL; v.u.num = b ? 1 : 0; return v; }
ExValue ex_v_num(double d) { ExValue v = ex_v_null(); v.t = EXV_NUM; v.u.num = d; return v; }
ExValue ex_v_strn(ExArena *a, const char *s, size_t n) {
    ExValue v = ex_v_null();
    v.t = EXV_STR; v.u.str = ex_arena_strndup(a, s ? s : "", s ? n : 0);
    if (!v.u.str) v.u.str = "";
    return v;
}
ExValue ex_v_str(ExArena *a, const char *s) { return ex_v_strn(a, s, s ? strlen(s) : 0); }
ExValue ex_v_date(double ms, int has_time) { ExValue v = ex_v_null(); v.t = EXV_DATE; v.u.date.ms = ms; v.u.date.has_time = has_time; return v; }
ExValue ex_v_duration(double ms, int64_t months) { ExValue v = ex_v_null(); v.t = EXV_DURATION; v.u.dur.ms = ms; v.u.dur.months = months; return v; }
ExValue ex_v_list(ExArena *a, const ExValue *items, size_t n) {
    ExValue v = ex_v_null();
    ExValue *copy = n ? ex_arena_alloc(a, n * sizeof *copy) : NULL;
    v.t = EXV_LIST;
    if (n && !copy) return ex_v_error(a, "out of memory");
    if (n) memcpy(copy, items, n * sizeof *copy);
    v.u.list.v = copy; v.u.list.n = n;
    return v;
}
ExValue ex_v_object(ExArena *a, const char *const *keys, const ExValue *vals, size_t n) {
    ExValue v = ex_v_null();
    const char **k = n ? ex_arena_alloc(a, n * sizeof *k) : NULL;
    ExValue *c = n ? ex_arena_alloc(a, n * sizeof *c) : NULL;
    size_t i;
    v.t = EXV_OBJECT;
    if (n && (!k || !c)) return ex_v_error(a, "out of memory");
    for (i = 0; i < n; i++) { k[i] = ex_arena_strdup(a, keys[i]); c[i] = vals[i]; }
    v.u.obj.keys = (const char *const *)k; v.u.obj.v = c; v.u.obj.n = n;
    return v;
}
ExValue ex_v_link(ExArena *a, const char *path, const char *sub, const char *display) {
    ExValue v = ex_v_null();
    v.t = EXV_LINK;
    v.u.link.path = ex_arena_strdup(a, path ? path : "");
    v.u.link.sub = sub && *sub ? ex_arena_strdup(a, sub) : NULL;
    v.u.link.display = display ? ex_arena_strdup(a, display) : NULL;
    return v;
}
ExValue ex_v_url(ExArena *a, const char *url, const char *display) {
    ExValue v = ex_v_null();
    v.t = EXV_URL; v.u.url.url = ex_arena_strdup(a, url ? url : ""); v.u.url.display = display ? ex_arena_strdup(a, display) : NULL;
    return v;
}
ExValue ex_v_file(ExArena *a, const char *path) { ExValue v = ex_v_null(); v.t = EXV_FILE; v.u.file = ex_arena_strdup(a, path ? path : ""); return v; }
ExValue ex_v_error(ExArena *a, const char *fmt, ...) {
    char buf[300];
    va_list ap;
    ExValue v = ex_v_null();
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    v.t = EXV_ERROR;
    v.u.str = a ? ex_arena_strdup(a, buf) : "error";
    if (!v.u.str) v.u.str = "error";
    return v;
}

const char *ex_value_type_name(ExValue v) {
    switch (v.t) {
    case EXV_NULL: return "null"; case EXV_BOOL: return "boolean"; case EXV_NUM: return "number"; case EXV_STR: return "string"; case EXV_DATE: return "date";
    case EXV_DURATION: return "duration"; case EXV_LIST: return "list"; case EXV_OBJECT: return "object"; case EXV_LINK: return "link"; case EXV_URL: return "url";
    case EXV_FILE: return "file"; case EXV_REGEXP: return "regexp"; case EXV_HTML: return "html"; case EXV_IMAGE: return "image"; case EXV_ICON: return "icon";
    case EXV_ERROR: return "error"; default: return "namespace";
    }
}

/* ---- numbers as JavaScript writes them ---- */

size_t ex_number_to_string(double d, char *out, size_t cap) {
    char sci[40], digits[40], tmp[400];
    int p, exp10, neg = 0, n;
    size_t len = 0;
    const char *e;
    if (isnan(d)) { snprintf(out, cap, "NaN"); return 3; }
    if (isinf(d)) { snprintf(out, cap, d < 0 ? "-Infinity" : "Infinity"); return d < 0 ? 9 : 8; }
    if (d == 0) { snprintf(out, cap, "0"); return 1; }
    if (d < 0) { neg = 1; d = -d; }
    for (p = 0; p < 17; p++) { snprintf(sci, sizeof sci, "%.*e", p, d); if (strtod(sci, NULL) == d) break; }       /* the fewest digits that read back as the same number */
    e = strchr(sci, 'e');
    exp10 = atoi(e + 1);
    n = 0;
    { const char *c; for (c = sci; c < e; c++) if (*c >= '0' && *c <= '9') digits[n++] = *c; }
    while (n > 1 && digits[n - 1] == '0') n--;
    digits[n] = 0;
    if (exp10 >= 0 && exp10 < 21) {
        if (n <= exp10 + 1) { memcpy(tmp, digits, (size_t)n); memset(tmp + n, '0', (size_t)(exp10 + 1 - n)); len = (size_t)(exp10 + 1); }
        else { memcpy(tmp, digits, (size_t)exp10 + 1); tmp[exp10 + 1] = '.'; memcpy(tmp + exp10 + 2, digits + exp10 + 1, (size_t)(n - exp10 - 1)); len = (size_t)n + 1; }
    } else if (exp10 < 0 && exp10 >= -6) {
        tmp[0] = '0'; tmp[1] = '.';
        memset(tmp + 2, '0', (size_t)(-exp10 - 1));
        memcpy(tmp + 2 + (-exp10 - 1), digits, (size_t)n);
        len = (size_t)(2 + (-exp10 - 1) + n);
    } else {
        len = 0;
        tmp[len++] = digits[0];
        if (n > 1) { tmp[len++] = '.'; memcpy(tmp + len, digits + 1, (size_t)(n - 1)); len += (size_t)(n - 1); }
        len += (size_t)snprintf(tmp + len, sizeof tmp - len, "e%c%d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
    }
    tmp[len] = 0;
    if (cap) snprintf(out, cap, "%s%s", neg ? "-" : "", tmp);
    return len + (size_t)neg;
}

/* ---- text ---- */

typedef struct Sb { char *p; size_t n, cap; ExArena *a; } Sb;
static void sb_put(Sb *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 128;
        char *np;
        while (nc < s->n + n + 1) nc *= 2;
        np = ex_arena_alloc(s->a, nc);
        if (!np) return;
        if (s->n) memcpy(np, s->p, s->n);
        s->p = np; s->cap = nc;
    }
    memcpy(s->p + s->n, t, n);
    s->n += n;
    s->p[s->n] = 0;
}
static void sb_str(Sb *s, const char *t) { sb_put(s, t, strlen(t)); }

static void date_text(Sb *s, ExValue v, int tz_min) {
    char b[48];
    ex_date_format(b, sizeof b, v.u.date.ms, tz_min, v.u.date.has_time ? "YYYY-MM-DD HH:mm:ss" : "YYYY-MM-DD");
    sb_str(s, b);
}

static void duration_text(Sb *s, ExValue v) {
    double ms = v.u.dur.ms;
    int64_t months = v.u.dur.months;
    int any = 0;
    char b[64];
    int64_t y = months / 12, mo = months % 12;
    long long d, h, mi, sec, msec;
    if (ms < 0 && months >= 0) { sb_str(s, "-"); ms = -ms; }
    d = (long long)(ms / 86400000.0); ms -= (double)d * 86400000.0;
    h = (long long)(ms / 3600000.0); ms -= (double)h * 3600000.0;
    mi = (long long)(ms / 60000.0); ms -= (double)mi * 60000.0;
    sec = (long long)(ms / 1000.0); ms -= (double)sec * 1000.0;
    msec = (long long)ms;
    if (y) { snprintf(b, sizeof b, "%lldy", (long long)y); sb_str(s, b); any = 1; }
    if (mo) { snprintf(b, sizeof b, "%s%lldM", any ? " " : "", (long long)mo); sb_str(s, b); any = 1; }
    if (d) { snprintf(b, sizeof b, "%s%lldd", any ? " " : "", d); sb_str(s, b); any = 1; }
    if (h) { snprintf(b, sizeof b, "%s%lldh", any ? " " : "", h); sb_str(s, b); any = 1; }
    if (mi) { snprintf(b, sizeof b, "%s%lldm", any ? " " : "", mi); sb_str(s, b); any = 1; }
    if (sec) { snprintf(b, sizeof b, "%s%llds", any ? " " : "", sec); sb_str(s, b); any = 1; }
    if (msec) { snprintf(b, sizeof b, "%s%lldms", any ? " " : "", msec); sb_str(s, b); any = 1; }
    if (!any) sb_str(s, "0s");
}

static void text_of(Sb *s, const ExExprHost *host, ExValue v, int depth);

static void json_string(Sb *s, const char *t) {
    const unsigned char *u = (const unsigned char *)t;
    sb_put(s, "\"", 1);
    for (; *u; u++) {
        char b[8];
        switch (*u) {
        case '"': sb_str(s, "\\\""); break;
        case '\\': sb_str(s, "\\\\"); break;
        case '\n': sb_str(s, "\\n"); break;
        case '\r': sb_str(s, "\\r"); break;
        case '\t': sb_str(s, "\\t"); break;
        default: if (*u < 0x20) { snprintf(b, sizeof b, "\\u%04x", *u); sb_str(s, b); } else sb_put(s, (const char *)u, 1);
        }
    }
    sb_put(s, "\"", 1);
}

static void text_of(Sb *s, const ExExprHost *host, ExValue v, int depth) {
    char b[400];
    size_t i;
    int tz = host ? host->tz_min : 0;
    if (depth > 20) { sb_str(s, "..."); return; }
    switch (v.t) {
    case EXV_NULL: break;
    case EXV_BOOL: sb_str(s, v.u.num ? "true" : "false"); break;
    case EXV_NUM: ex_number_to_string(v.u.num, b, sizeof b); sb_str(s, b); break;
    case EXV_STR: case EXV_HTML: sb_str(s, v.u.str); break;
    case EXV_ICON: sb_str(s, v.u.str); break;
    case EXV_DATE: date_text(s, v, tz); break;
    case EXV_DURATION: duration_text(s, v); break;
    case EXV_LIST: for (i = 0; i < v.u.list.n; i++) { if (i) sb_str(s, ", "); text_of(s, host, v.u.list.v[i], depth + 1); } break;
    case EXV_OBJECT:
        sb_str(s, "{");
        for (i = 0; i < v.u.obj.n; i++) { if (i) sb_str(s, ", "); sb_str(s, v.u.obj.keys[i]); sb_str(s, ": "); text_of(s, host, v.u.obj.v[i], depth + 1); }
        sb_str(s, "}");
        break;
    case EXV_LINK: sb_str(s, v.u.link.display ? v.u.link.display : v.u.link.path); break;
    case EXV_URL: sb_str(s, v.u.url.display ? v.u.url.display : v.u.url.url); break;
    case EXV_FILE: sb_str(s, v.u.file); break;
    case EXV_REGEXP: sb_str(s, "/"); sb_str(s, v.u.re.pattern); sb_str(s, "/"); sb_str(s, v.u.re.flags ? v.u.re.flags : ""); break;
    case EXV_IMAGE: sb_str(s, v.u.image); break;
    case EXV_ERROR: sb_str(s, "Error: "); sb_str(s, v.u.str); break;
    default: break;
    }
}

char *ex_value_to_string(ExArena *a, const ExExprHost *host, ExValue v) {
    Sb s;
    memset(&s, 0, sizeof s);
    s.a = a;
    text_of(&s, host, v, 0);
    if (!s.p) return ex_arena_strdup(a, "");
    return s.p;
}

static void repr_of(Sb *s, ExValue v, int depth) {
    char b[400];
    size_t i;
    if (depth > 20) { sb_str(s, "..."); return; }
    switch (v.t) {
    case EXV_NULL: sb_str(s, "null"); break;
    case EXV_BOOL: sb_str(s, v.u.num ? "true" : "false"); break;
    case EXV_NUM: ex_number_to_string(v.u.num, b, sizeof b); sb_str(s, b); break;
    case EXV_STR: json_string(s, v.u.str); break;
    case EXV_DATE: sb_str(s, "date("); date_text(s, v, 0); sb_str(s, ")"); break;
    case EXV_DURATION: sb_str(s, "duration("); duration_text(s, v); sb_str(s, ")"); break;
    case EXV_LIST: sb_str(s, "["); for (i = 0; i < v.u.list.n; i++) { if (i) sb_str(s, ","); repr_of(s, v.u.list.v[i], depth + 1); } sb_str(s, "]"); break;
    case EXV_OBJECT:
        sb_str(s, "{");
        for (i = 0; i < v.u.obj.n; i++) { if (i) sb_str(s, ","); json_string(s, v.u.obj.keys[i]); sb_str(s, ":"); repr_of(s, v.u.obj.v[i], depth + 1); }
        sb_str(s, "}");
        break;
    case EXV_LINK: sb_str(s, "link("); json_string(s, v.u.link.path); if (v.u.link.sub) { sb_str(s, "#"); sb_str(s, v.u.link.sub); } if (v.u.link.display) { sb_str(s, ","); json_string(s, v.u.link.display); } sb_str(s, ")"); break;
    case EXV_URL: sb_str(s, "url("); json_string(s, v.u.url.url); if (v.u.url.display) { sb_str(s, ","); json_string(s, v.u.url.display); } sb_str(s, ")"); break;
    case EXV_FILE: sb_str(s, "file("); json_string(s, v.u.file); sb_str(s, ")"); break;
    case EXV_REGEXP: sb_str(s, "/"); sb_str(s, v.u.re.pattern); sb_str(s, "/"); sb_str(s, v.u.re.flags ? v.u.re.flags : ""); break;
    case EXV_HTML: sb_str(s, "html("); json_string(s, v.u.str); sb_str(s, ")"); break;
    case EXV_IMAGE: sb_str(s, "image("); json_string(s, v.u.image); sb_str(s, ")"); break;
    case EXV_ICON: sb_str(s, "icon("); json_string(s, v.u.str); sb_str(s, ")"); break;
    case EXV_ERROR: sb_str(s, "#ERR("); sb_str(s, v.u.str); sb_str(s, ")"); break;
    default: sb_str(s, "namespace"); break;
    }
}

char *ex_value_repr(ExArena *a, ExValue v) {
    Sb s;
    memset(&s, 0, sizeof s);
    s.a = a;
    repr_of(&s, v, 0);
    return s.p ? s.p : ex_arena_strdup(a, "");
}

/* ---- truth, emptiness, equality, order ---- */

int ex_value_truthy(ExValue v) {
    switch (v.t) {
    case EXV_NULL: case EXV_ERROR: return 0;
    case EXV_BOOL: return v.u.num != 0;
    case EXV_NUM: return v.u.num != 0 && !isnan(v.u.num);
    case EXV_STR: case EXV_HTML: return v.u.str[0] != 0;
    case EXV_LIST: return v.u.list.n > 0;
    case EXV_OBJECT: return v.u.obj.n > 0;
    default: return 1;
    }
}

int ex_value_is_empty(ExValue v) {
    switch (v.t) {
    case EXV_NULL: return 1;
    case EXV_NUM: return isnan(v.u.num);
    case EXV_STR: case EXV_HTML: return v.u.str[0] == 0;
    case EXV_LIST: return v.u.list.n == 0;
    case EXV_OBJECT: return v.u.obj.n == 0;
    default: return 0;
    }
}

static const char *link_target(const ExExprHost *host, ExValue v, char *buf, size_t cap) {
    if (v.t == EXV_FILE) return v.u.file;
    if (v.t == EXV_LINK) {
        if (host && host->resolve && host->resolve(host->user, host->file_path ? host->file_path : "", v.u.link.path, buf, cap)) return buf;
        return v.u.link.path;
    }
    return NULL;
}

int ex_value_equal(const ExExprHost *host, ExValue a, ExValue b) {
    size_t i;
    if ((a.t == EXV_LINK || a.t == EXV_FILE) && (b.t == EXV_LINK || b.t == EXV_FILE)) {
        char x[700], y[700];
        const char *p = link_target(host, a, x, sizeof x), *q = link_target(host, b, y, sizeof y);
        return p && q && !strcmp(p, q);
    }
    if (a.t != b.t) return 0;
    switch (a.t) {
    case EXV_NULL: return 1;
    case EXV_BOOL: case EXV_NUM: return a.u.num == b.u.num;
    case EXV_STR: case EXV_HTML: case EXV_ICON: return !strcmp(a.u.str, b.u.str);
    case EXV_DATE: return a.u.date.ms == b.u.date.ms;
    case EXV_DURATION: return a.u.dur.ms == b.u.dur.ms && a.u.dur.months == b.u.dur.months;
    case EXV_LIST:
        if (a.u.list.n != b.u.list.n) return 0;
        for (i = 0; i < a.u.list.n; i++) if (!ex_value_equal(host, a.u.list.v[i], b.u.list.v[i])) return 0;
        return 1;
    case EXV_OBJECT:
        if (a.u.obj.n != b.u.obj.n) return 0;
        for (i = 0; i < a.u.obj.n; i++) {
            size_t k;
            int found = 0;
            for (k = 0; k < b.u.obj.n; k++) if (!strcmp(a.u.obj.keys[i], b.u.obj.keys[k])) { found = ex_value_equal(host, a.u.obj.v[i], b.u.obj.v[k]); break; }
            if (!found) return 0;
        }
        return 1;
    case EXV_URL: return !strcmp(a.u.url.url, b.u.url.url);
    case EXV_IMAGE: return !strcmp(a.u.image, b.u.image);
    case EXV_REGEXP: return !strcmp(a.u.re.pattern, b.u.re.pattern) && !strcmp(a.u.re.flags ? a.u.re.flags : "", b.u.re.flags ? b.u.re.flags : "");
    default: return 0;
    }
}

static int rank_of(ExValue v) {
    switch (v.t) { case EXV_NULL: return 0; case EXV_BOOL: return 1; case EXV_NUM: return 2; case EXV_STR: return 3; case EXV_DATE: return 4; case EXV_DURATION: return 5; default: return 6 + (int)v.t; }
}
static int cmp_d(double a, double b) { return a < b ? -1 : a > b ? 1 : 0; }

int ex_value_compare(ExValue a, ExValue b) {
    int ra = rank_of(a), rb = rank_of(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (a.t) {
    case EXV_BOOL: case EXV_NUM:
        if (isnan(a.u.num) || isnan(b.u.num)) return isnan(a.u.num) ? (isnan(b.u.num) ? 0 : 1) : -1;      /* NaN last */
        return cmp_d(a.u.num, b.u.num);
    case EXV_STR: case EXV_HTML: case EXV_ICON: { int c = strcmp(a.u.str, b.u.str); return c < 0 ? -1 : c > 0 ? 1 : 0; }
    case EXV_DATE: return cmp_d(a.u.date.ms, b.u.date.ms);
    case EXV_DURATION: { int c = cmp_d((double)a.u.dur.months, (double)b.u.dur.months); return c ? c : cmp_d(a.u.dur.ms, b.u.dur.ms); }
    case EXV_LIST: {
        size_t i;
        for (i = 0; i < a.u.list.n && i < b.u.list.n; i++) { int c = ex_value_compare(a.u.list.v[i], b.u.list.v[i]); if (c) return c; }
        return a.u.list.n < b.u.list.n ? -1 : a.u.list.n > b.u.list.n ? 1 : 0;
    }
    case EXV_LINK: { int c = strcmp(a.u.link.path, b.u.link.path); return c < 0 ? -1 : c > 0 ? 1 : 0; }
    case EXV_FILE: { int c = strcmp(a.u.file, b.u.file); return c < 0 ? -1 : c > 0 ? 1 : 0; }
    case EXV_URL: { int c = strcmp(a.u.url.url, b.u.url.url); return c < 0 ? -1 : c > 0 ? 1 : 0; }
    default: return 0;
    }
}
