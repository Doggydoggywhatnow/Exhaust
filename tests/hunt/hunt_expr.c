/* hunt_expr: the Bases expression engine (include/exhaust_base_expr.h, docs/formats.md "Bases expressions").
 *
 * tests/test_base_expr.c walks the documented examples. This test looks for what examples do not reach:
 *   A. a differential test of arithmetic: random expression trees printed with minimal parentheses against the same tree evaluated in C doubles
 *      (precedence, associativity, unary minus, `%`, division by zero, huge and tiny numbers);
 *   B. numbers as text: every double (random bit patterns, subnormals, powers of ten, integers around 2^53) must print the way JavaScript prints it
 *      (shortest digits that read back exactly, fixed notation from 1e-6 up to 1e21, else exponent notation), `toFixed` and `round` on the cases where
 *      naive code is wrong (1.005, 2.5, -2.5, 0.49999999999999994);
 *   C. a sweep: every one of the 72 functions called on every kind of receiver with every kind of argument, thousands of combinations each. Nothing may
 *      crash or run for long, results must be the same on a second run, and a text produced from valid UTF-8 must be valid UTF-8 (a `reverse` or
 *      `slice` by bytes cuts a character in half);
 *   D. the laws of values: ex_value_compare is a total order that agrees with ex_value_equal, repr and toString never fail;
 *   E. limits: expressions nested a million deep, chains of a million operators, repeat() asking for gigabytes, a regular expression that backtracks
 *      for ever, formulas that call each other in a circle, lists of lists of lists;
 *   F. names with a hyphen, the clock functions in a time zone, and date arithmetic;
 *   G. random token soup into the parser: no crash, and an error position inside the text.
 * Link: exhaust_base. */
#include "hunt.h"

#include <math.h>

#include "exhaust_base_expr.h"
#include "exhaust_datefmt.h"

#define T0 1748354709123.0

/* ---- a host that answers a few questions and remembers which names were asked ---- */
static char g_asked[16][64];
static int g_nasked;
static const ExExpr *g_formulas[8];
static const char *g_fnames[8];
static ExExprHost HOST;

static int note_prop(void *u, ExArena *a, const char *name, ExValue *out) {
    (void)u;
    if (g_nasked < 16) snprintf(g_asked[g_nasked++], sizeof g_asked[0], "%s", name);
    if (!strcmp(name, "price")) { *out = ex_v_num(12.5); return 1; }
    if (!strcmp(name, "title")) { *out = ex_v_str(a, "Some Title"); return 1; }
    if (!strcmp(name, "x")) { ExValue l[2]; l[0] = ex_v_num(1); l[1] = ex_v_str(a, "two"); *out = ex_v_list(a, l, 2); return 1; }
    return 0;
}
static int file_field(void *u, ExArena *a, const char *path, const char *field, ExValue *out) {
    (void)u; (void)path;
    if (!strcmp(field, "tags")) { ExValue t[2]; t[0] = ex_v_str(a, "#a"); t[1] = ex_v_str(a, "#b/c"); *out = ex_v_list(a, t, 2); return 1; }
    if (!strcmp(field, "name")) { *out = ex_v_str(a, "Name.md"); return 1; }
    if (!strcmp(field, "links")) { ExValue t[1]; t[0] = ex_v_link(a, "Note", NULL, NULL); *out = ex_v_list(a, t, 1); return 1; }
    if (!strcmp(field, "properties")) { const char *k[2] = { "role", "n" }; ExValue v[2]; v[0] = ex_v_str(a, "r"); v[1] = ex_v_num(3); *out = ex_v_object(a, k, v, 2); return 1; }
    return 0;
}
static int resolve(void *u, const char *from, const char *text, char *path, size_t cap) { (void)u; (void)from; if (!strcmp(text, "Note")) { snprintf(path, cap, "Note.md"); return 1; } return 0; }
static const ExExpr *formula(void *u, const char *name) { size_t i; (void)u; for (i = 0; i < 8; i++) if (g_fnames[i] && !strcmp(g_fnames[i], name)) return g_formulas[i]; return NULL; }
static double rnd01(void *u) { (void)u; return 0.25; }

static void host_init(void) {
    memset(&HOST, 0, sizeof HOST);
    HOST.note_prop = note_prop; HOST.file_field = file_field; HOST.resolve = resolve; HOST.formula = formula; HOST.random = rnd01;
    HOST.file_path = "Notes/Name.md"; HOST.this_path = "Note.md"; HOST.now_ms = T0; HOST.tz_min = 0;
}

static int valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) p++;
        else if (*p >= 0xC2 && *p <= 0xDF && (p[1] & 0xC0) == 0x80) p += 2;
        else if (*p == 0xE0 && p[1] >= 0xA0 && p[1] <= 0xBF && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p >= 0xE1 && *p <= 0xEC && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p == 0xED && p[1] >= 0x80 && p[1] <= 0x9F && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p >= 0xEE && *p <= 0xEF && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p == 0xF0 && p[1] >= 0x90 && p[1] <= 0xBF && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else if (*p >= 0xF1 && *p <= 0xF3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else if (*p == 0xF4 && p[1] >= 0x80 && p[1] <= 0x8F && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else return 0;
    }
    return 1;
}

/* ---- A. arithmetic against C doubles ---- */

typedef struct Node { int op; double num; struct Node *l, *r; } Node;      /* op: 0 number, '-' unary is 'n', else + - * / % */

static Node *gen(int depth) {
    Node *n = (Node *)calloc(1, sizeof *n);
    int k = depth <= 0 ? 0 : (int)hunt_rnd(10);
    if (k < 3) {
        static const double nums[] = { 0, 1, 2, 3, 5, 7, 10, 0.5, 0.1, 0.2, 2.5, 100, 1e3, 1e21, 1e-7, 123456789, 3.14159, 9007199254740993.0, 1e308, 4.9e-324 };
        n->op = 0; n->num = nums[hunt_rnd(sizeof nums / sizeof *nums)];
    } else if (k == 3) { n->op = 'n'; n->l = gen(depth - 1); }
    else { static const char ops[] = "+-*/%+-*"; n->op = ops[hunt_rnd(8)]; n->l = gen(depth - 1); n->r = gen(depth - 1); }
    return n;
}
static void node_free(Node *n) { if (!n) return; node_free(n->l); node_free(n->r); free(n); }

static double node_eval(const Node *n) {
    double a, b;
    switch (n->op) {
    case 0: return n->num;
    case 'n': return -node_eval(n->l);
    default: break;
    }
    a = node_eval(n->l); b = node_eval(n->r);
    switch (n->op) { case '+': return a + b; case '-': return a - b; case '*': return a * b; case '/': return a / b; default: return fmod(a, b); }
}
static int prec(const Node *n) { return n->op == 0 ? 9 : n->op == 'n' ? 5 : (n->op == '*' || n->op == '/' || n->op == '%') ? 4 : 3; }

/* A number as JavaScript source: the shortest text that reads back to the same double. */
static void num_src(double d, char *out) {
    int p;
    for (p = 1; p <= 17; p++) { snprintf(out, 40, "%.*g", p, d); if (strtod(out, NULL) == d) return; }
}

static size_t node_print(const Node *n, char *o, size_t cap) {
    size_t k = 0;
    if (n->op == 0) { char b[48]; num_src(n->num, b); return (size_t)snprintf(o, cap, "%s", b); }
    if (n->op == 'n') {
        int paren = prec(n->l) < 5 || n->l->op == 'n' || (n->l->op == 0 && 0);
        k += (size_t)snprintf(o + k, cap - k, "-%s", paren ? "(" : "");
        k += node_print(n->l, o + k, cap - k);
        if (paren) k += (size_t)snprintf(o + k, cap - k, ")");
        return k;
    }
    {
        int p = prec(n), pl = prec(n->l) < p, pr = prec(n->r) <= p;      /* left associative: an equal precedence on the right needs parentheses */
        if (n->l->op == 'n' && p == 4) pl = 0;
        if (pl) k += (size_t)snprintf(o + k, cap - k, "(");
        k += node_print(n->l, o + k, cap - k);
        if (pl) k += (size_t)snprintf(o + k, cap - k, ")");
        k += (size_t)snprintf(o + k, cap - k, " %c ", n->op);
        if (pr) k += (size_t)snprintf(o + k, cap - k, "(");
        k += node_print(n->r, o + k, cap - k);
        if (pr) k += (size_t)snprintf(o + k, cap - k, ")");
    }
    return k;
}

static void test_arithmetic(void) {
    long rounds = hunt_iters(30000), r;
    ExArena *a = ex_arena_new();
    for (r = 0; r < rounds; r++) {
        Node *n = gen(1 + (int)hunt_rnd(5));
        char src[4096];
        double want, got;
        ExValue v;
        node_print(n, src, sizeof src);
        want = node_eval(n);
        hunt_stepf("arithmetic: %s", src);
        v = ex_expr_run(src, a, &HOST, NULL);
        if (v.t != EXV_NUM) { HUNT_CHECKF(0, "%s gave %s, want the number %.17g", src, ex_value_repr(a, v), want); }
        else {
            got = v.u.num;
            if (!((isnan(got) && isnan(want)) || got == want)) HUNT_CHECKF(0, "%s = %.17g, C says %.17g", src, got, want);
        }
        node_free(n);
        if ((r & 255) == 0) ex_arena_reset(a);
        if (hunt_.failures > 30) break;
    }
    ex_arena_free(a);
}

/* ---- B. numbers as text ---- */

/* The number of significant digits in a JavaScript-style number text. */
static int sig_digits(const char *s) {
    int n = 0, seen_nonzero = 0;
    const char *p = s;
    if (*p == '-') p++;
    for (; *p && *p != 'e'; p++) {
        if (*p == '.') continue;
        if (*p >= '0' && *p <= '9') { if (*p != '0') seen_nonzero = 1; if (seen_nonzero) n++; }
    }
    /* trailing zeros of an integer are not significant: 1200 has two */
    {
        const char *e = strchr(s, 'e');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (!strchr(s, '.') || (e && 0)) { size_t k = len; while (k > 0 && s[k - 1] == '0') { n--; k--; } }
    }
    return n < 1 ? 1 : n;
}
static int shortest_digits(double d) {
    int p;
    char b[48];
    for (p = 1; p <= 17; p++) { snprintf(b, sizeof b, "%.*e", p - 1, d); if (strtod(b, NULL) == d) return p; }
    return 17;
}

static void check_number_text(double d) {
    char out[64];
    size_t n = ex_number_to_string(d, out, sizeof out);
    double back;
    if (isnan(d)) { HUNT_CHECKF(!strcmp(out, "NaN"), "NaN printed as %s", out); return; }
    if (isinf(d)) { HUNT_CHECKF(!strcmp(out, d > 0 ? "Infinity" : "-Infinity"), "infinity printed as %s", out); return; }
    HUNT_CHECKF(n == strlen(out), "ex_number_to_string returned %lu for \"%s\"", (unsigned long)n, out);
    back = strtod(out, NULL);
    if (d == 0) { HUNT_CHECKF(!strcmp(out, "0"), "zero (%g) printed as \"%s\"", d, out); return; }
    HUNT_CHECKF(back == d, "%.17g printed as \"%s\", which reads back as %.17g", d, out, back);
    {
        double ad = fabs(d);
        int has_exp = strchr(out, 'e') != NULL;
        HUNT_CHECKF(has_exp == (ad >= 1e21 || ad < 1e-6), "%.17g printed as \"%s\": JavaScript uses an exponent only below 1e-6 and from 1e21", d, out);
        if (has_exp) {
            const char *e = strchr(out, 'e');
            HUNT_CHECKF(e[1] == '+' || e[1] == '-', "\"%s\": the exponent needs its sign", out);
            HUNT_CHECKF(!(e[2] == '0'), "\"%s\": no leading zero in the exponent", out);
        }
        HUNT_CHECKF(!(out[0] == '0' && out[1] != '.' && out[1] != 0) && !(out[0] == '-' && out[1] == '0' && out[2] != '.'), "\"%s\" has a leading zero", out);
        HUNT_CHECKF(sig_digits(out) == shortest_digits(d), "%.17g printed as \"%s\" with %d digits, the shortest that reads back has %d", d, out, sig_digits(out), shortest_digits(d));
    }
}

static void test_number_text(void) {
    long rounds = hunt_iters(120000), r;
    size_t i;
    double fixed[] = { 0.1, 0.2, 0.1 + 0.2, 1.0 / 3, 2.0 / 3, 100, 1e20, 1e21, 1e22, 123456789012345680000.0, 1e-6, 1e-7, 0.000001234, 5e-324, 1.7976931348623157e308, 9007199254740991.0, 9007199254740992.0, 9007199254740993.0,
        4503599627370497.5, 0.5, 1.5, 2.5e-5, 123.456, -123.456, -0.0, 1e300, 1e23, 8.41e21, 5e-7, 4.35, 0.3, 255.0 / 256, 1.0000000000000002, 0.9999999999999999, 999999999999999900000.0 };
    char out[64];
    hunt_step("numbers as text: known values");
    for (i = 0; i < sizeof fixed / sizeof *fixed; i++) { hunt_stepf("number text %.17g", fixed[i]); check_number_text(fixed[i]); }
    check_number_text(NAN); check_number_text(INFINITY); check_number_text(-INFINITY);
    ex_number_to_string(0.1 + 0.2, out, sizeof out); HUNT_EQ_STR(out, "0.30000000000000004");
    ex_number_to_string(1e21, out, sizeof out); HUNT_EQ_STR(out, "1e+21");
    ex_number_to_string(1e-7, out, sizeof out); HUNT_EQ_STR(out, "1e-7");
    ex_number_to_string(-0.0, out, sizeof out); HUNT_EQ_STR(out, "0");
    ex_number_to_string(123456789012345680000.0, out, sizeof out); HUNT_EQ_STR(out, "123456789012345680000");
    ex_number_to_string(5e-324, out, sizeof out); HUNT_EQ_STR(out, "5e-324");
    ex_number_to_string(1.7976931348623157e308, out, sizeof out); HUNT_EQ_STR(out, "1.7976931348623157e+308");
    ex_number_to_string(9007199254740993.0, out, sizeof out); HUNT_EQ_STR(out, "9007199254740992");
    ex_number_to_string(0.000001, out, sizeof out); HUNT_EQ_STR(out, "0.000001");
    ex_number_to_string(-1.5, out, sizeof out); HUNT_EQ_STR(out, "-1.5");
    hunt_step("numbers as text: a small buffer");
    {
        size_t cap;
        for (cap = 0; cap < 30; cap++) {
            char *b = (char *)malloc(cap ? cap : 1);
            size_t n = ex_number_to_string(0.30000000000000004, cap ? b : NULL, cap);
            HUNT_CHECK(n == 19 || n <= cap);
            if (cap) HUNT_CHECK(memchr(b, 0, cap) != NULL);
            free(b);
        }
    }
    hunt_step("numbers as text: random bit patterns, integers, powers of ten");
    for (r = 0; r < rounds; r++) {
        double d;
        int k = (int)hunt_rnd(5);
        if (k == 0) { uint64_t bits = hunt_rand64(); memcpy(&d, &bits, 8); }
        else if (k == 1) d = (double)((int64_t)(hunt_rand64() >> (hunt_rnd(60))) - (int64_t)((uint64_t)hunt_rnd(2) << 40));
        else if (k == 2) d = (double)(int64_t)hunt_rnd(2000000) / 1000.0;
        else if (k == 3) d = pow(10.0, (double)((int)hunt_rnd(80) - 40)) * (double)(1 + hunt_rnd(9));
        else d = ldexp((double)(hunt_rand64() >> 11), (int)hunt_rnd(200) - 150);
        if (isnan(d) || isinf(d)) continue;
        check_number_text(d);
        if (hunt_.failures > 30) break;
    }

    hunt_step("toFixed and round");
    {
        ExArena *a = ex_arena_new();
        struct { const char *expr, *want; } fx[] = {
            { "(1.005).toFixed(2)", "1.00" }, { "(2.5).toFixed(0)", "3" }, { "(0.5).toFixed(0)", "1" }, { "(1.5).toFixed(0)", "2" }, { "(-2.5).toFixed(0)", "-3" }, { "(1.45).toFixed(1)", "1.4" }, { "(8.345).toFixed(2)", "8.35" },
            { "(10.235).toFixed(2)", "10.23" }, { "(0).toFixed(2)", "0.00" }, { "(1234.5678).toFixed(2)", "1234.57" }, { "(1e21).toFixed(2)", "1e+21" }, { "(0.1).toFixed(20)", "0.10000000000000000555" },
            { "(123.456).toFixed(0)", "123" }, { "(0.000001).toFixed(7)", "0.0000010" }, { "(-0.0001).toFixed(2)", "-0.00" }, { "(5).toFixed(3)", "5.000" }, { "(999.9999).toFixed(2)", "1000.00" }, { "(0.5).toFixed(1)", "0.5" },
            { "(1/0).toFixed(2)", "Infinity" }, { "(0/0).toFixed(2)", "NaN" }, { "(1e-10).toFixed(3)", "0.000" },
        };
        struct { const char *expr, *want; } rd[] = {
            { "(2.5).round()", "3" }, { "(-2.5).round()", "-2" }, { "(0.5).round()", "1" }, { "(-0.5).round()", "0" }, { "(1.4999999999999999).round()", "2" }, { "(0.49999999999999994).round()", "0" },
            { "(2.4999999999999996).round()", "2" }, { "(4503599627370497).round()", "4503599627370497" }, { "(-1e300).round()", "-1e+300" }, { "(1e300).round()", "1e+300" }, { "(3).round()", "3" }, { "(-3.5).round()", "-3" },
            { "(1.5).round()", "2" }, { "(-1.5).round()", "-1" }, { "(0.1).round()", "0" }, { "(0.9).round()", "1" }, { "(-0.4).round()", "0" }, { "(9007199254740991).round()", "9007199254740991" }, { "(0/0).round()", "NaN" },
        };
        for (i = 0; i < sizeof fx / sizeof *fx; i++) {
            ExValue v = ex_expr_run(fx[i].expr, a, &HOST, NULL);
            char *s = ex_value_to_string(a, &HOST, v);
            HUNT_SPECF(!strcmp(s, fx[i].want), "docs/formats.md, Bases expressions: toFixed writes the exact decimal value of the number", "%s = \"%s\", want \"%s\"", fx[i].expr, s, fx[i].want);
        }
        for (i = 0; i < sizeof rd / sizeof *rd; i++) {
            ExValue v = ex_expr_run(rd[i].expr, a, &HOST, NULL);
            char *s = ex_value_to_string(a, &HOST, v);
            HUNT_SPECF(!strcmp(s, rd[i].want), "docs/formats.md, Bases expressions: round rounds halves up (as Math.round)", "%s = \"%s\", want \"%s\"", rd[i].expr, s, rd[i].want);
        }
        {
            ExValue v = ex_expr_run("(1).toFixed(101)", a, &HOST, NULL);
            HUNT_SUSPECT(v.t == EXV_ERROR, "toFixed of more than 100 digits is a RangeError in JavaScript; here it should be an error value");
            v = ex_expr_run("(1).toFixed(-1)", a, &HOST, NULL);
            HUNT_SUSPECT(v.t == EXV_ERROR, "toFixed of a negative number of digits is a RangeError in JavaScript");
            v = ex_expr_run("(1).toFixed(1e300)", a, &HOST, NULL);
            (void)v;
        }
        ex_arena_free(a);
    }
}

/* ---- C. the function sweep ---- */

static const char *const POOL[] = {
    "null", "true", "false", "0", "-1", "2.5", "1e308", "(0/0)", "(1/0)", "\"\"", "\"a\"", "\"Hello World\"", "\"\xC3\xA9\xF0\x9F\x98\x80x\"", "\"  pad \"", "\"a,b,,c\"", "\"https://x.y/z?q=1\"",
    "[]", "[1,2,3]", "[[1],[2,[3]]]", "[\"b\",\"a\",null,2,true]", "[1,1,2,\"1\"]", "date(\"2025-01-01\")", "date(\"2025-05-27 14:05:09\")", "duration(\"1d 2h\")", "duration(\"-3M\")",
    "link(\"Note#H|Shown\")", "link(\"missing\")", "file", "this", "/a+b/i", "now()", "today()", "file.properties", "file.tags", "file.links", "html(\"<b>x</b>\")", "image(\"a.png\")", "icon(\"home\")", "list(1)", "list([1,2])",
    "formula.nothing", "price", "title", "x",
};
#define NPOOL (sizeof POOL / sizeof POOL[0])

static int deterministic(const char *name) { return !strstr(name, "now") && !strstr(name, "today") && !strstr(name, "random") && !strstr(name, "relative"); }

static void test_sweep(void) {
    size_t nf = ex_expr_function_count(), f;
    long per = hunt_iters(1500), k;
    ExArena *a = ex_arena_new();
    HUNT_EQ_INT(nf, 72);
    for (f = 0; f < nf; f++) {
        const char *full = ex_expr_function_name(f), *colon = strchr(full, ':');
        char kind[16], name[32];
        snprintf(kind, sizeof kind, "%.*s", (int)(colon - full), full);
        snprintf(name, sizeof name, "%s", colon + 1);
        for (k = 0; k < per; k++) {
            char expr[800];
            int nargs = (int)hunt_rnd(4);
            const char *recv = POOL[hunt_rnd(NPOOL)];
            char args[400] = "";
            int j;
            ExValue v1, v2;
            char *r1, *r2;
            clock_t t0;
            double secs;
            for (j = 0; j < nargs; j++) { strcat(args, j ? "," : ""); strcat(args, POOL[hunt_rnd(NPOOL)]); }
            if (!strcmp(kind, "global")) snprintf(expr, sizeof expr, "%s(%s)", name, args);
            else snprintf(expr, sizeof expr, "(%s).%s(%s)", recv, name, args);
            hunt_stepf("sweep: %s", expr);
            hunt_set_input(expr, strlen(expr), "expression");
            t0 = clock();
            v1 = ex_expr_run(expr, a, &HOST, NULL);
            r1 = ex_value_repr(a, v1);
            secs = hunt_cpu_since(t0);
            if (secs > hunt_time_limit(2.0)) { HUNT_SUSPECT(0, "an expression took more than two seconds"); hunt_note_("%s took %.2f s", expr, secs); }
            (void)ex_value_to_string(a, &HOST, v1);
            (void)ex_value_truthy(v1);
            (void)ex_value_is_empty(v1);
            (void)ex_value_type_name(v1);
            if (deterministic(name) && !strstr(expr, "now()") && !strstr(expr, "today()")) {
                v2 = ex_expr_run(expr, a, &HOST, NULL);
                r2 = ex_value_repr(a, v2);
                HUNT_CHECKF(!strcmp(r1, r2), "%s gave a different value the second time: %s then %s", expr, r1, r2);
            }
            /* a text made of valid UTF-8 stays valid UTF-8 */
            if (v1.t == EXV_STR && !strstr(expr, "\\") && !strstr(expr, "html(") && !strstr(expr, "image(") && !strstr(expr, "icon(")) {
                HUNT_CHECKF(valid_utf8(v1.u.str), "%s produced text that is not valid UTF-8: %s", expr, r1);
            }
            if (v1.t == EXV_LIST) {
                size_t i;
                for (i = 0; i < v1.u.list.n; i++) if (v1.u.list.v[i].t == EXV_STR) HUNT_CHECKF(valid_utf8(v1.u.list.v[i].u.str), "%s produced a list with text that is not valid UTF-8: %s", expr, r1);
            }
            hunt_clear_input();
            if ((k & 63) == 0) ex_arena_reset(a);
            if (hunt_.failures > 40) break;
        }
        ex_arena_reset(a);
    }
    ex_arena_free(a);
}

/* ---- D. laws of values ---- */

static void test_value_laws(void) {
    ExArena *a = ex_arena_new();
    ExValue vs[NPOOL + 6];
    size_t n = 0, i, j, k;
    static const char *const more[] = { "\"b\"", "\"A\"", "-0", "date(\"2024-12-31 23:59:59\")", "duration(\"1h\")", "duration(\"60m\")" };
    for (i = 0; i < NPOOL; i++) { if (!strstr(POOL[i], "now") && !strstr(POOL[i], "today")) vs[n++] = ex_expr_run(POOL[i], a, &HOST, NULL); }
    for (i = 0; i < sizeof more / sizeof *more; i++) vs[n++] = ex_expr_run(more[i], a, &HOST, NULL);
    hunt_step("value laws: order and equality");
    for (i = 0; i < n; i++) {
        char *r = ex_value_repr(a, vs[i]);
        HUNT_CHECKF(ex_value_compare(vs[i], vs[i]) == 0 || (vs[i].t == EXV_NUM && isnan(vs[i].u.num)), "compare(x, x) != 0 for %s", r);
        for (j = 0; j < n; j++) {
            int ab = ex_value_compare(vs[i], vs[j]), ba = ex_value_compare(vs[j], vs[i]);
            int nan_involved = (vs[i].t == EXV_NUM && isnan(vs[i].u.num)) || (vs[j].t == EXV_NUM && isnan(vs[j].u.num));
            HUNT_CHECKF(ab >= -1 && ab <= 1, "compare returned %d", ab);
            if (!nan_involved) HUNT_CHECKF(ab == -ba, "compare is not antisymmetric for %s and %s (%d, %d)", r, ex_value_repr(a, vs[j]), ab, ba);
            if (!nan_involved && ex_value_equal(&HOST, vs[i], vs[j])) HUNT_CHECKF(ab == 0 || vs[i].t != vs[j].t, "equal values %s and %s compare as %d", r, ex_value_repr(a, vs[j]), ab);
            if (!nan_involved && ex_value_equal(&HOST, vs[i], vs[j]) != ex_value_equal(&HOST, vs[j], vs[i])) HUNT_CHECKF(0, "equality is not symmetric for %s and %s", r, ex_value_repr(a, vs[j]));
            if (nan_involved) continue;
            for (k = 0; k < n; k += 3) {
                int bc, ac;
                if (vs[k].t == EXV_NUM && isnan(vs[k].u.num)) continue;
                if (ab <= 0 && (bc = ex_value_compare(vs[j], vs[k])) <= 0) {
                    ac = ex_value_compare(vs[i], vs[k]);
                    HUNT_CHECKF(ac <= 0, "compare is not transitive: %s <= %s <= %s but the first is greater than the last", r, ex_value_repr(a, vs[j]), ex_value_repr(a, vs[k]));
                }
            }
        }
        (void)ex_value_to_string(a, &HOST, vs[i]);
        HUNT_CHECK(ex_value_type_name(vs[i]) != NULL);
    }
    hunt_step("value laws: sort() puts null, booleans, numbers, strings, dates, durations in that order");
    {
        ExValue v = ex_expr_run("[\"b\", 2, null, true, date(\"2025-01-01\"), 1, \"a\", false, duration(\"1d\")].sort()", a, &HOST, NULL);
        char *r = ex_value_repr(a, v);
        HUNT_SPECF(v.t == EXV_LIST && !strcmp(r, "[null,false,true,1,2,\"a\",\"b\",date(2025-01-01),duration(1d)]"), "docs/formats.md, Bases expressions: sort() puts null, then booleans, numbers, texts, dates and durations", "got %s", r);
        v = ex_expr_run("[3,1,2,10,-5,2.5].sort()", a, &HOST, NULL);
        r = ex_value_repr(a, v);
        HUNT_CHECKF(!strcmp(r, "[-5,1,2,2.5,3,10]"), "numbers sorted as %s (as text would put 10 first)", r);
        v = ex_expr_run("[\"b\",\"B\",\"a\",\"A\",\"\xC3\xA9\",\"e\",\"z\"].sort()", a, &HOST, NULL);
        r = ex_value_repr(a, v);
        HUNT_SUSPECT(v.t == EXV_LIST, "sorting text must at least produce a list");
        (void)r;
    }
    ex_arena_free(a);
}

/* ---- E. limits ---- */

static char *repeat_str(const char *unit, size_t times, const char *prefix, const char *suffix) {
    size_t u = strlen(unit), p = strlen(prefix), s = strlen(suffix), i;
    char *o = (char *)malloc(p + u * times + s + 1);
    memcpy(o, prefix, p);
    for (i = 0; i < times; i++) memcpy(o + p + i * u, unit, u);
    memcpy(o + p + u * times, suffix, s + 1);
    return o;
}

static void run_limit(const char *name, const char *expr, double limit_s, int expect_error) {
    ExArena *a = ex_arena_new();
    clock_t t0 = clock();
    ExValue v;
    ExExpr *e;
    char err[200];
    int pos = -1;
    double secs;
    hunt_stepf("limit: %s", name);
    e = ex_expr_parse(expr, err, sizeof err, &pos);
    if (e) { v = ex_expr_eval(e, a, &HOST, NULL); ex_expr_free(e); }
    else { v = ex_v_error(a, "%s", err); HUNT_CHECKF(pos >= 0 && (size_t)pos <= strlen(expr), "%s: error position %d outside the %lu byte text", name, pos, (unsigned long)strlen(expr)); }
    secs = hunt_cpu_since(t0);
    if (secs > hunt_time_limit(limit_s)) { HUNT_SUSPECT(0, "took longer than the limit"); hunt_note_("%s: %.2f s (limit %.1f s)", name, secs, hunt_time_limit(limit_s)); }
    if (expect_error) HUNT_CHECKF(v.t == EXV_ERROR, "%s should be an error value, got %s", name, ex_value_type_name(v));
    (void)ex_value_repr(a, v);
    ex_arena_free(a);
}

static void test_limits(void) {
    char *s;
    size_t n;
    hunt_step("limits: nesting and long chains (a stack overflow here is a crash of the whole program)");
    for (n = 1000; n <= 1000000; n *= 10) {
        char name[80];
        snprintf(name, sizeof name, "%lu open parentheses", (unsigned long)n);
        s = repeat_str("(", n, "", "1"); run_limit(name, s, 3.0, 1); free(s);
        snprintf(name, sizeof name, "%lu balanced parentheses", (unsigned long)n);
        { char *o = repeat_str("(", n, "", "1"); char *c = repeat_str(")", n, o, ""); run_limit(name, c, 3.0, 1); free(o); free(c); }
        snprintf(name, sizeof name, "%lu open brackets", (unsigned long)n);
        s = repeat_str("[", n, "", "1"); run_limit(name, s, 3.0, 1); free(s);
        snprintf(name, sizeof name, "%lu unary minus", (unsigned long)n);
        s = repeat_str("-", n, "", "1"); run_limit(name, s, 3.0, 0); free(s);
        snprintf(name, sizeof name, "%lu logical not", (unsigned long)n);
        s = repeat_str("!", n, "", "1"); run_limit(name, s, 3.0, 0); free(s);
        snprintf(name, sizeof name, "%lu additions", (unsigned long)n);
        s = repeat_str("1+", n, "", "1"); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu string concatenations", (unsigned long)n);
        s = repeat_str("\"a\"+", n, "", "\"a\""); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu member accesses", (unsigned long)n);
        s = repeat_str(".a", n, "file", ""); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu index accesses", (unsigned long)n);
        s = repeat_str("[0]", n, "[1]", ""); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu method calls", (unsigned long)n);
        s = repeat_str(".trim()", n, "\"a\"", ""); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu nested calls", (unsigned long)n);
        { char *o = repeat_str("if(true,", n, "", "1"); char *c = repeat_str(",2)", n, o, ""); run_limit(name, c, 5.0, 1); free(o); free(c); }
        snprintf(name, sizeof name, "%lu comparisons", (unsigned long)n);
        s = repeat_str("1<", n, "", "1"); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu logical ands", (unsigned long)n);
        s = repeat_str("true&&", n, "", "true"); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu list items", (unsigned long)n);
        s = repeat_str("1,", n, "[", "1]"); run_limit(name, s, 5.0, 0); free(s);
        snprintf(name, sizeof name, "%lu call arguments", (unsigned long)n);
        s = repeat_str("1,", n, "max(", "1)"); run_limit(name, s, 5.0, 0); free(s);
    }
    hunt_step("limits: memory and steps");
    run_limit("repeat of a billion", "\"abc\".repeat(1000000000)", 2.0, 1);
    run_limit("repeat of 1e15", "\"abc\".repeat(1e15)", 2.0, 1);
    run_limit("repeat negative", "\"abc\".repeat(-5)", 2.0, 0);
    run_limit("repeat NaN", "\"abc\".repeat(0/0)", 2.0, 0);
    run_limit("repeat then concatenate past a megabyte", "\"abc\".repeat(300000) + \"abc\".repeat(300000)", 2.0, 1);
    run_limit("doubling a string 40 times", "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40].reduce(acc + acc, \"abcdefghij\")", 3.0, 1);
    run_limit("a map inside a map inside a map", "list(1,2,3,4,5,6,7,8,9,10).map(list(1,2,3,4,5,6,7,8,9,10).map(list(1,2,3,4,5,6,7,8,9,10).map(list(1,2,3,4,5,6,7,8,9,10).map(list(1,2,3,4,5,6,7,8,9,10).map(list(1,2,3,4,5,6,7,8,9,10).map(value))))))", 5.0, 1);
    run_limit("reduce that grows a list", "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30].reduce([acc, acc], [1])", 3.0, 0);
    run_limit("flat of nested lists", "[[[[[[[[[[1]]]]]]]]]].flat().flat().flat()", 1.0, 0);
    run_limit("list(...) of a long range via map of a repeat", "\"a\".repeat(900000).split(\"\").map(value + value)", 5.0, 0);
    run_limit("split of a megabyte on empty", "(\"abcdefghij\".repeat(100000)).split(\"\")", 5.0, 0);
    run_limit("unique of a big list", "(\"abcdefghij\".repeat(50000)).split(\"\").unique()", 5.0, 0);
    run_limit("sort of a big list", "(\"abcdefghij\".repeat(100000)).split(\"\").sort()", 5.0, 0);
    run_limit("contains in a big list", "(\"abcdefghij\".repeat(100000)).split(\"\").containsAll(\"a\",\"b\",\"c\")", 5.0, 0);
    run_limit("replace everywhere in a megabyte", "(\"a\".repeat(900000)).replace(\"a\", \"bbbbbbbbbbbb\")", 3.0, 0);
    run_limit("replace of the empty string", "\"abc\".replace(\"\", \"x\")", 1.0, 0);
    run_limit("a regular expression that backtracks for ever", "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!\".matches(/(a+)+$/)", 3.0, 0);
    run_limit("another one", "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\".matches(/(a|aa)+$/)", 3.0, 0);
    run_limit("an invalid regular expression", "\"a\".matches(/(/)", 1.0, 0);
    run_limit("a regular expression with a huge repeat", "\"a\".matches(/a{1000000}/)", 2.0, 0);

    hunt_step("limits: formulas that call each other");
    {
        const ExExpr *saved[8];
        const char *savedn[8];
        size_t i;
        ExArena *a = ex_arena_new();
        ExValue v;
        char *r;
        memcpy(saved, g_formulas, sizeof saved); memcpy(savedn, g_fnames, sizeof savedn);
        g_fnames[0] = "a"; g_formulas[0] = ex_expr_parse("formula.b + 1", NULL, 0, NULL);
        g_fnames[1] = "b"; g_formulas[1] = ex_expr_parse("formula.a + 1", NULL, 0, NULL);
        g_fnames[2] = "self"; g_formulas[2] = ex_expr_parse("formula.self", NULL, 0, NULL);
        g_fnames[3] = "ok"; g_formulas[3] = ex_expr_parse("1 + 1", NULL, 0, NULL);
        g_fnames[4] = "viaif"; g_formulas[4] = ex_expr_parse("if(true, 5, formula.viaif)", NULL, 0, NULL);
        g_fnames[5] = "bad"; g_formulas[5] = ex_expr_failed("this formula does not parse");
        v = ex_expr_run("formula.a", a, &HOST, NULL); r = ex_value_repr(a, v);
        HUNT_SPECF(v.t == EXV_ERROR && strstr(r, "circular"), "docs/formats.md, Bases expressions: a circle is an error naming it", "formula.a gave %s", r);
        v = ex_expr_run("formula.self", a, &HOST, NULL); r = ex_value_repr(a, v);
        HUNT_CHECKF(v.t == EXV_ERROR && strstr(r, "circular"), "formula.self gave %s", r);
        v = ex_expr_run("formula.ok", a, &HOST, NULL);
        HUNT_CHECK(v.t == EXV_NUM && v.u.num == 2);
        v = ex_expr_run("formula.ok + formula.ok", a, &HOST, NULL);
        HUNT_CHECK(v.t == EXV_NUM && v.u.num == 4);            /* the same formula twice is not a circle */
        v = ex_expr_run("formula.viaif", a, &HOST, NULL);
        HUNT_SUSPECT((v.t == EXV_NUM && v.u.num == 5) || v.t == EXV_ERROR, "if() does not look at the branch it does not take, but a circle is a circle either way");
        v = ex_expr_run("formula.bad", a, &HOST, NULL);
        HUNT_CHECK(v.t == EXV_ERROR);
        v = ex_expr_run("formula.missing", a, &HOST, NULL);
        (void)v;
        for (i = 0; i < 8; i++) ex_expr_free((ExExpr *)g_formulas[i]);
        memcpy(g_formulas, saved, sizeof saved); memcpy(g_fnames, savedn, sizeof savedn);
        ex_arena_free(a);
    }
}

/* ---- F. names, the clock, dates ---- */

static void names_asked(const char *expr, char *out, size_t cap) {
    ExArena *a = ex_arena_new();
    size_t i, k = 0;
    g_nasked = 0;
    (void)ex_expr_run(expr, a, &HOST, NULL);
    out[0] = 0;
    for (i = 0; i < (size_t)g_nasked; i++) k += (size_t)snprintf(out + k, cap - k, "%s%s", i ? "," : "", g_asked[i]);
    ex_arena_free(a);
}

static void test_names_and_dates(void) {
    char got[200];
    ExArena *a = ex_arena_new();
    struct { const char *expr, *want; } nm[] = { { "price-5", "price" }, { "image-property", "image-property" }, { "price - age", "price,age" }, { "price-age", "price-age" }, { "price -age", "price" },
        { "a-b-c", "a-b-c" }, { "a - b-c", "a,b-c" }, { "price-(5)", "price" }, { "x-1", "x" }, { "x[0]", "x" }, { "note.price", "price" }, { "title.title", "title" } };
    size_t i;
    hunt_step("names with a hyphen");
    for (i = 0; i < sizeof nm / sizeof *nm; i++) {
        names_asked(nm[i].expr, got, sizeof got);
        HUNT_SPECF(!strcmp(got, nm[i].want), "docs/formats.md, Bases expressions: Syntax (a name may contain a - followed by a letter and with no space around it)", "%s asked for [%s], want [%s]", nm[i].expr, got, nm[i].want);
    }

    hunt_step("the clock: now() and today() in several zones");
    {
        int tzs[] = { 0, 330, -480, 840, -720, -30, 60 };
        for (i = 0; i < sizeof tzs / sizeof *tzs; i++) {
            ExValue v;
            ExDateParts p;
            double midnight;
            HOST.tz_min = tzs[i];
            v = ex_expr_run("now()", a, &HOST, NULL);
            HUNT_CHECKF(v.t == EXV_DATE && v.u.date.ms == T0 && v.u.date.has_time, "now() in zone %d is not the host's clock", tzs[i]);
            v = ex_expr_run("today()", a, &HOST, NULL);
            ex_date_parts(T0, tzs[i], &p);
            midnight = ex_date_make(p.year, p.month, p.day, 0, 0, 0, 0, tzs[i]);
            HUNT_CHECKF(v.t == EXV_DATE && v.u.date.ms == midnight && !v.u.date.has_time, "today() in zone %d is %.0f (has_time %d), want midnight of the local day %.0f", tzs[i], v.t == EXV_DATE ? v.u.date.ms : 0.0, v.t == EXV_DATE ? v.u.date.has_time : -1, midnight);
        }
        HOST.tz_min = 0;
    }

    hunt_step("date arithmetic");
    {
        struct { const char *expr, *want; } da[] = {
            { "date(\"2024-01-31\") + \"1M\"", "date(2024-02-29)" }, { "date(\"2023-01-31\") + \"1M\"", "date(2023-02-28)" }, { "date(\"2024-03-31\") - \"1M\"", "date(2024-02-29)" }, { "date(\"2024-02-29\") + \"1y\"", "date(2025-02-28)" },
            { "date(\"2024-01-01\") + \"1d\"", "date(2024-01-02)" }, { "date(\"2024-01-01\") + \"36h\"", "date(2024-01-02 12:00:00)" }, { "date(\"2024-12-31\") + \"1d\"", "date(2025-01-01)" }, { "date(\"2024-01-01 10:00:00\") + \"1M\"", "date(2024-02-01 10:00:00)" },
            { "date(\"2024-01-01 10:00:00\") - \"10h\"", "date(2024-01-01)" }, { "date(\"2024-03-01\") - \"1d\"", "date(2024-02-29)" }, { "date(\"2025-01-01\") - date(\"2024-01-01\")", "duration(366d)" }, { "(date(\"2025-01-01\") - date(\"2024-01-01\")).days", "366" },
            { "date(\"2024-02-29\").year", "2024" }, { "date(\"2024-02-29\").month", "2" }, { "date(\"2024-02-29\").day", "29" }, { "date(\"2024-02-29 13:14:15.016\").hour", "13" }, { "date(\"2024-02-29 13:14:15.016\").millisecond", "16" },
            { "date(\"2024-02-29\").format(\"YYYY/MM/DD dddd\")", "\"2024/02/29 Thursday\"" }, { "date(\"2024-02-29\") == date(\"2024-02-29\")", "true" }, { "date(\"2024-02-29\") < date(\"2024-03-01\")", "true" },
            { "duration(\"1d\") * 3", "duration(3d)" }, { "duration(\"1d\") / 2", "duration(12h)" }, { "duration(\"1d\") + duration(\"1h\")", "duration(1d 1h)" }, { "duration(\"2h\") - duration(\"30m\")", "duration(1h 30m)" },
            { "date(\"2024-01-31\") + \"1M\" + \"1M\"", "date(2024-03-29)" },
        };
        for (i = 0; i < sizeof da / sizeof *da; i++) {
            ExValue v = ex_expr_run(da[i].expr, a, &HOST, NULL);
            char *r = ex_value_repr(a, v);
            HUNT_SPECF(!strcmp(r, da[i].want), "docs/formats.md, Bases expressions: Dates", "%s = %s, want %s", da[i].expr, r, da[i].want);
        }
    }
    ex_arena_free(a);
}

/* ---- G. token soup ---- */

static void test_soup(void) {
    static const char *const toks[] = { "1", "2.5", "1e3", ".5", "\"a\"", "'b'", "\"\\u00e9\"", "\"\\", "\"", "'", "true", "false", "null", "price", "title", "file", "this", "note", "formula", "value", "index", "acc", "values", "x", "image-property",
        "+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "===", "!==", "&&", "||", "!", "(", ")", "[", "]", ",", ".", "/a+/", "/a/gi", "/", "//", "/*", "$", "_", "\xC3\xA9", "\xF0\x9F\x98\x80", " ", "  ", "\n", "\t",
        "if(", "date(", "list(", "link(", "map(", "filter(", "reduce(", ".map(", ".filter(", ".sort()", ".format(", ".toFixed(", ".replace(", ".slice(", "\\", "\x01", "?", ":", "=", "&", "|", "{", "}", "#", "@", "~", "`", "0x1F", "1.2.3", "..", "1e", "e5" };
    long rounds = hunt_iters(60000), r;
    ExArena *a = ex_arena_new();
    for (r = 0; r < rounds; r++) {
        char expr[300];
        size_t n = 0, parts = 1 + hunt_rnd_size(14);
        char err[80];
        int pos = -2;
        ExExpr *e;
        expr[0] = 0;
        while (parts--) { const char *t = toks[hunt_rnd(sizeof toks / sizeof *toks)]; size_t l = strlen(t); if (n + l + 1 > sizeof expr) break; memcpy(expr + n, t, l); n += l; }
        expr[n] = 0;
        hunt_stepf("token soup %ld", r);
        hunt_set_input(expr, n, "expression");
        e = ex_expr_parse(expr, err, sizeof err, &pos);
        if (!e) {
            HUNT_CHECKF(err[0] != 0, "a parse error with no message");
            HUNT_CHECKF(pos >= 0 && (size_t)pos <= n, "error position %d for a text of %lu bytes", pos, (unsigned long)n);
        } else {
            ExValue v1 = ex_expr_eval(e, a, &HOST, NULL), v2;
            char *r1 = ex_value_repr(a, v1), *r2;
            (void)ex_value_to_string(a, &HOST, v1);
            v2 = ex_expr_eval(e, a, &HOST, NULL);
            r2 = ex_value_repr(a, v2);
            if (!strstr(expr, "random") && !strstr(expr, "now") && !strstr(expr, "today")) HUNT_CHECKF(!strcmp(r1, r2), "%s evaluates differently twice: %s then %s", expr, r1, r2);
            ex_expr_free(e);
        }
        /* no error buffer at all, and one byte */
        { ExExpr *e2 = ex_expr_parse(expr, NULL, 0, NULL); if (e2) ex_expr_free(e2); }
        { char one[1]; ExExpr *e3 = ex_expr_parse(expr, one, 1, &pos); if (e3) ex_expr_free(e3); }
        hunt_clear_input();
        if ((r & 127) == 0) ex_arena_reset(a);
    }
    ex_arena_free(a);
}

int main(int argc, char **argv) {
    hunt_begin("hunt_expr", argc, argv);
    host_init();
    test_arithmetic();
    test_number_text();
    test_sweep();
    test_value_laws();
    test_limits();
    test_names_and_dates();
    test_soup();
    return hunt_end();
}
