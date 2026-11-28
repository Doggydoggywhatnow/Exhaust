/* base_expr (6b-i): the Bases expression engine against the documented examples (every one of the 72 functions), the operators and date arithmetic,
 * the properties of the note and the file, formulas and their cycles, null handling, errors that stay values, and the limits that keep a bad expression
 * from running away. The clock is fixed: 2025-05-27 14:05:09.123 UTC. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base_expr.h"
#include "exhaust_datefmt.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExArena *A;
static char *g_all;                        /* every expression run, to see that each function was exercised */
static size_t g_all_len, g_all_cap;

/* ---- a small vault for the host ---- */

typedef struct MockFile { const char *path; const char *tags[4]; const char *links[4]; } MockFile;
static const MockFile FILES[] = {
    { "Notes/Book.md", { "#book", "#x/y", NULL }, { "Textbook", "Alice", NULL } },
    { "Textbook.md", { "#school", NULL }, { NULL } },
    { "Alice.md", { NULL }, { "Bob", NULL } },
    { "Bob.md", { NULL }, { NULL } },
    { "Pics/photo.png", { NULL }, { NULL } },
};
#define NFILES (sizeof FILES / sizeof *FILES)
#define T0 1748354709123.0

static const MockFile *find_file(const char *path) { size_t i; for (i = 0; i < NFILES; i++) if (!strcmp(FILES[i].path, path)) return &FILES[i]; return NULL; }

static const char *base_of(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static int ieq(const char *a, const char *b) { while (*a && *b) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; a++; b++; } return !*a && !*b; }

static int mock_resolve(void *u, const char *from, const char *text, char *path, size_t cap) {
    size_t i;
    char t[300];
    size_t tl = strlen(text);
    (void)u; (void)from;
    snprintf(t, sizeof t, "%s", text);
    if (tl > 3 && !strcmp(t + tl - 3, ".md")) t[tl - 3] = 0;
    for (i = 0; i < NFILES; i++) {
        char b[300];
        size_t bl;
        snprintf(b, sizeof b, "%s", base_of(FILES[i].path));
        bl = strlen(b);
        if (bl > 3 && !strcmp(b + bl - 3, ".md")) b[bl - 3] = 0;
        if (ieq(b, t) || ieq(FILES[i].path, text)) { snprintf(path, cap, "%s", FILES[i].path); return 1; }
    }
    return 0;
}

static ExValue book_props(ExArena *a) {
    const char *k[] = { "price", "age", "status", "title", "authors", "image-property", "release_date", "empty", "nested" };
    ExValue authors[2], nested_v[2], v[9];
    const char *nk[] = { "a", "b" };
    authors[0] = ex_v_link(a, "Alice", NULL, NULL); authors[1] = ex_v_link(a, "Bob", NULL, NULL);
    nested_v[0] = ex_v_num(1); nested_v[1] = ex_v_str(a, "two");
    v[0] = ex_v_num(12.5); v[1] = ex_v_num(5); v[2] = ex_v_str(a, "done"); v[3] = ex_v_str(a, "The Book");
    v[4] = ex_v_list(a, authors, 2); v[5] = ex_v_str(a, "img.png"); v[6] = ex_v_date(ex_date_make(2025, 6, 1, 0, 0, 0, 0, 0), 0); v[7] = ex_v_str(a, "");
    v[8] = ex_v_object(a, nk, nested_v, 2);
    return ex_v_object(a, k, v, 9);
}

static int mock_note_prop(void *u, ExArena *a, const char *name, ExValue *out) {
    ExValue p = book_props(a);
    size_t i;
    (void)u;
    for (i = 0; i < p.u.obj.n; i++) if (!strcmp(p.u.obj.keys[i], name)) { *out = p.u.obj.v[i]; return 1; }
    return 0;
}

static int mock_file_field(void *u, ExArena *a, const char *path, const char *f, ExValue *out) {
    const MockFile *mf = find_file(path);
    const char *base = base_of(path), *dot = strrchr(base, '.');
    size_t i;
    (void)u;
    if (!mf) return 0;
    if (!strcmp(f, "name")) *out = ex_v_str(a, base);
    else if (!strcmp(f, "basename")) *out = ex_v_strn(a, base, dot ? (size_t)(dot - base) : strlen(base));
    else if (!strcmp(f, "path")) *out = ex_v_str(a, path);
    else if (!strcmp(f, "folder")) *out = ex_v_strn(a, path, base > path ? (size_t)(base - path - 1) : 0);
    else if (!strcmp(f, "ext")) *out = ex_v_str(a, dot ? dot + 1 : "");
    else if (!strcmp(f, "size")) *out = ex_v_num(1234);
    else if (!strcmp(f, "ctime")) *out = ex_v_date(ex_date_make(2025, 1, 2, 3, 4, 5, 0, 0), 1);
    else if (!strcmp(f, "mtime")) *out = ex_v_date(ex_date_make(2025, 5, 26, 10, 0, 0, 0, 0), 1);
    else if (!strcmp(f, "tags")) { ExValue t[4]; for (i = 0; mf->tags[i]; i++) t[i] = ex_v_str(a, mf->tags[i]); *out = ex_v_list(a, t, i); }
    else if (!strcmp(f, "links")) { ExValue t[4]; for (i = 0; mf->links[i]; i++) t[i] = ex_v_link(a, mf->links[i], NULL, NULL); *out = ex_v_list(a, t, i); }
    else if (!strcmp(f, "embeds") || !strcmp(f, "backlinks")) *out = ex_v_list(a, NULL, 0);
    else if (!strcmp(f, "properties")) { if (!strcmp(path, "Notes/Book.md")) *out = book_props(a); else if (!strcmp(path, "Alice.md")) { const char *k[] = { "role" }; ExValue v = ex_v_str(a, "author"); *out = ex_v_object(a, k, &v, 1); } else *out = ex_v_object(a, NULL, NULL, 0); }
    else return 0;
    return 1;
}

static ExExpr *g_formulas[40];
static char g_chain_names[16][8], g_chain_texts[16][32];
static const char *g_fnames[40] = { "ppu", "a", "b", "double", "label", "self", NULL };
static const char *g_ftexts[40] = { "(price / age).toFixed(2)", "formula.b + 1", "formula.a + 1", "formula.ppu + \"x\"", "if(price, \"$\" + price.toFixed(2), \"\")", "formula.self", NULL };
static const ExExpr *mock_formula(void *u, const char *name) { size_t i; (void)u; for (i = 0; g_fnames[i]; i++) if (!strcmp(g_fnames[i], name)) return g_formulas[i]; return NULL; }
static double fixed_random(void *u) { (void)u; return 0.25; }

static ExExprHost HOST;

static void run(const char *expr, const char *want, int line) {
    ExValue v = ex_expr_run(expr, A, &HOST, NULL);
    char *got = ex_value_repr(A, v);
    size_t n = strlen(expr);
    if (g_all_len + n + 2 > g_all_cap) { g_all_cap = (g_all_len + n + 2) * 2; g_all = realloc(g_all, g_all_cap); }
    memcpy(g_all + g_all_len, expr, n); g_all_len += n; g_all[g_all_len++] = '\n'; g_all[g_all_len] = 0;
    if (strcmp(got, want)) { fprintf(stderr, "line %d: %s\n   got: %s\n  want: %s\n", line, expr, got, want); failures++; }
}
#define T(expr, want) run(expr, want, __LINE__)
/* an error whose message holds a word */
static void run_err(const char *expr, const char *word, int line) {
    ExValue v = ex_expr_run(expr, A, &HOST, NULL);
    char *got = ex_value_repr(A, v);
    if (v.t != EXV_ERROR || !strstr(got, word)) { fprintf(stderr, "line %d: %s\n   got: %s\n  wanted an error with \"%s\"\n", line, expr, got, word); failures++; }
}
#define E(expr, word) run_err(expr, word, __LINE__)
static void text(const char *expr, const char *want, int line) {
    ExValue v = ex_expr_run(expr, A, &HOST, NULL);
    char *got = ex_value_to_string(A, &HOST, v);
    if (strcmp(got, want)) { fprintf(stderr, "line %d: toString of %s\n   got: %s\n  want: %s\n", line, expr, got, want); failures++; }
}
#define S(expr, want) text(expr, want, __LINE__)

int main(void) {
    size_t i;
    A = ex_arena_new();
    {   /* a chain of formulas c0 = c1 + 1, c1 = c2 + 1 ... c14 = 1: deeper than the limit from c0, fine from c10 */
        size_t k, base = 6;
        for (k = 0; k < 15; k++) {
            snprintf(g_chain_names[k], sizeof g_chain_names[k], "c%zu", k);
            if (k < 14) snprintf(g_chain_texts[k], sizeof g_chain_texts[k], "formula.c%zu + 1", k + 1); else snprintf(g_chain_texts[k], sizeof g_chain_texts[k], "1");
            g_fnames[base + k] = g_chain_names[k]; g_ftexts[base + k] = g_chain_texts[k];
        }
    }
    for (i = 0; g_fnames[i]; i++) { g_formulas[i] = ex_expr_parse(g_ftexts[i], NULL, 0, NULL); CHECK(g_formulas[i] != NULL); }
    HOST.note_prop = mock_note_prop; HOST.file_field = mock_file_field; HOST.resolve = mock_resolve; HOST.formula = mock_formula; HOST.random = fixed_random;
    HOST.file_path = "Notes/Book.md"; HOST.this_path = "Alice.md"; HOST.now_ms = T0; HOST.tz_min = 0;

    /* ---- the count ---- */
    CHECK(ex_expr_function_count() == 72);

    /* ---- literals, operators, precedence ---- */
    T("1 + 2 * 3", "7");
    T("(1 + 2) * 3", "9");
    T("10 / 4", "2.5");
    T("10 % 4", "2");
    T("-3 + 5", "2");
    T("2 * -3", "-6");
    T("1 - 2 - 3", "-4");
    T("8 / 2 / 2", "2");
    T("\"a\" + \"b\"", "\"ab\"");
    T("\"n=\" + 5", "\"n=5\"");
    T("5 + \"x\"", "\"5x\"");
    T("\"a\" + null + \"b\"", "\"ab\"");                    /* a missing value is nothing in a text */
    T("1 + null", "null");                                 /* and nothing in arithmetic: the result is missing */
    T("null * 5", "null");
    T("true", "true"); T("false", "false"); T("null", "null");
    T("'single' + \"double\"", "\"singledouble\"");
    T("\"tab\\there\\n\\\"q\\\" \\u00e9\"", "\"tab\\there\\n\\\"q\\\" \xc3\xa9\"");
    T("1e3", "1000"); T("2.5e-1", "0.25"); T(".5", "0.5"); T("0.1 + 0.2", "0.30000000000000004");
    T("[1, \"a\", [2], null]", "[1,\"a\",[2],null]");
    T("[]", "[]");
    T("1 == 1", "true"); T("1 != 1", "false"); T("1 == \"1\"", "false"); T("\"a\" == \"a\"", "true"); T("null == null", "true"); T("null == 0", "false");
    T("[1,2] == [1,2]", "true"); T("[1,2] == [2,1]", "false");
    T("1 < 2", "true"); T("2 <= 2", "true"); T("3 > 4", "false"); T("\"a\" < \"b\"", "true"); T("\"b\" >= \"a\"", "true");
    T("date(\"2025-01-01\") < date(\"2025-01-02\")", "true");
    T("null < 1", "false"); T("1 > null", "false");
    T("!true", "false"); T("!0", "true"); T("!\"\"", "true"); T("![]", "true"); T("![1]", "false");
    T("true && false", "false"); T("true || false", "true");
    T("0 || \"default\"", "\"default\""); T("5 && 7", "7"); T("0 && 7", "0"); T("null || null", "null");
    T("1 + 2 == 3 && 2 < 3", "true");
    T("true || number(\"nope\")", "true");                 /* the second operand is not looked at */
    T("false && number(\"nope\")", "false");
    E("1 < \"a\"", "compare");
    E("\"a\" - 1", "arithmetic");
    E("[1] + 1", "arithmetic");
    /* ---- numbers as text ---- */
    S("1", "1"); S("100", "100"); S("-0", "0"); S("0.5", "0.5"); S("123456789012", "123456789012"); S("1e21", "1e+21"); S("1e-7", "1e-7"); S("0.000001", "0.000001");
    S("1.5e300", "1.5e+300"); S("1/3", "0.3333333333333333"); S("2/3", "0.6666666666666666"); S("1e20", "100000000000000000000"); S("123456789.123456789", "123456789.12345679");
    S("0/0", "NaN"); S("1/0", "Infinity"); S("-1/0", "-Infinity"); S("true", "true"); S("null", ""); S("[1,2,3]", "1, 2, 3"); S("[]", "");
    char big[64]; (void)big;

    /* ---- global functions ---- */
    T("escapeHTML(\"<a href=\\\"x\\\">&'</a>\")", "\"&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;\"");
    T("escapeHTML(null)", "null");
    T("date(\"2025-05-27\")", "date(2025-05-27)");
    T("date(\"2025-01-01 12:00:00\")", "date(2025-01-01 12:00:00)");
    T("date(\"2025-05-27\") == date(\"2025-05-27 00:00:00\")", "true");
    E("date(\"not a date\")", "not a date");
    E("date(\"2025-02-30\")", "not a date");
    T("duration(\"1d\")", "duration(1d)");
    T("duration(\"2 hours\")", "duration(2h)");
    T("duration(\"1y 2M\")", "duration(1y 2M)");
    T("duration(\"90 minutes\")", "duration(1h 30m)");
    T("duration(\"1 week\")", "duration(7d)");
    T("duration(\"1.5h\")", "duration(1h 30m)");
    E("duration(\"soon\")", "not a duration");
    T("now() + (duration('1d') * 2)", "date(2025-05-29 14:05:09)");
    T("duration('5h') * 2", "duration(10h)");
    T("duration('5h') / 2", "duration(2h 30m)");
    T("duration('1d') + duration('2h')", "duration(1d 2h)");
    T("duration('1d') - duration('2h')", "duration(22h)");
    T("-duration('1h')", "duration(-1h)");
    E("2 * duration('5h')", "arithmetic");                  /* the duration must be on the left */
    T("file(\"Textbook\")", "file(\"Textbook.md\")");
    T("file(\"Notes/Book.md\")", "file(\"Notes/Book.md\")");
    T("file(link(\"[[Textbook]]\"))", "file(\"Textbook.md\")");
    T("file(\"No such file\")", "null");
    T("file(file(\"Alice\"))", "file(\"Alice.md\")");
    T("html(\"<b>x</b>\")", "html(\"<b>x</b>\")");
    T("if(true, \"a\", \"b\")", "\"a\""); T("if(false, \"a\", \"b\")", "\"b\""); T("if(false, \"a\")", "null"); T("if(0, 1, 2)", "2"); T("if(\"x\", 1, 2)", "1"); T("if([], 1, 2)", "2");
    T("if(isModified, \"Modified\", \"Unmodified\")", "\"Unmodified\"");
    T("if(true, 1, number(\"never\"))", "1");               /* only the branch taken is evaluated */
    T("if(price, \"$\" + price.toFixed(2), \"\")", "\"$12.50\"");
    T("image(\"a.png\")", "image(\"a.png\")");
    T("image(image-property)", "image(\"img.png\")");
    T("image(\"https://obsidian.md/images/obsidian-logo-gradient.svg\")", "image(\"https://obsidian.md/images/obsidian-logo-gradient.svg\")");
    T("image(file(\"Pics/photo.png\"))", "image(\"Pics/photo.png\")");
    T("icon(\"arrow-right\")", "icon(\"arrow-right\")");
    T("link(\"Textbook\")", "link(\"Textbook\")");
    T("link(\"[[Note#Head|Shown]]\")", "link(\"Note\"#Head,\"Shown\")");
    T("link(\"Note\", \"display\")", "link(\"Note\",\"display\")");
    T("link(\"https://obsidian.md\")", "url(\"https://obsidian.md\")");
    T("link(\"https://obsidian.md\", \"Obsidian\")", "url(\"https://obsidian.md\",\"Obsidian\")");
    T("link(file(\"Alice\"))", "link(\"Alice.md\")");
    S("link(\"Note\", \"display\")", "display"); S("link(\"Note\")", "Note");
    T("list(\"value\")", "[\"value\"]"); T("list([1,2])", "[1,2]"); T("list(null)", "[]"); T("list(5)[0]", "5");
    T("max(1, 5, 3)", "5"); T("min(4, 2, 8)", "2"); T("max([3, 9, 2])", "9"); T("min(price, age)", "5"); T("max(1)", "1"); T("max(1, null, 3)", "3");
    E("max()", "at least one"); E("max(1, \"a\")", "numbers");
    T("now()", "date(2025-05-27 14:05:09)");
    T("now().format(\"YYYY-MM-DD HH:mm:ss.SSS\")", "\"2025-05-27 14:05:09.123\"");
    T("today()", "date(2025-05-27)");
    T("today().format(\"HH:mm\")", "\"00:00\"");
    T("number(\"3.4\")", "3.4"); T("number(\" 7 \")", "7"); T("number(true)", "1"); T("number(false)", "0"); T("number(date(\"1970-01-02\"))", "86400000"); T("number(5)", "5"); T("number(null)", "null");
    E("number(\"abc\")", "not a number"); E("number(\"\")", "not a number"); E("number(\"1 2\")", "not a number"); E("number(\"0x10\")", "not a number"); E("number([1])", "number");
    T("random()", "0.25");

    /* ---- any ---- */
    T("(1).isTruthy()", "true"); T("(0).isTruthy()", "false"); T("\"\".isTruthy()", "false"); T("[0].isTruthy()", "true"); T("null.isTruthy()", "false"); T("missing.isTruthy()", "false");
    T("\"example\".isType(\"string\")", "true"); T("true.isType(\"boolean\")", "true"); T("(1).isType(\"number\")", "true"); T("[1].isType(\"list\")", "true");
    T("now().isType(\"date\")", "true"); T("\"x\".isType(\"number\")", "false"); T("null.isType(\"null\")", "true"); T("file.isType(\"file\")", "true");
    T("link(\"a\").isType(\"link\")", "true"); T("link(\"https://a.b\").isType(\"url\")", "true"); T("/a/.isType(\"regexp\")", "true"); T("file.properties.isType(\"object\")", "true");
    T("(123).toString()", "\"123\""); T("true.toString()", "\"true\""); T("[1,2].toString()", "\"1, 2\""); T("null.toString()", "\"\""); T("date(\"2025-05-27\").toString()", "\"2025-05-27\"");
    T("duration('1d 2h').toString()", "\"1d 2h\""); T("(1.50).toString()", "\"1.5\"");

    /* ---- dates ---- */
    T("date(\"2025-05-27 14:05:09.123\").year", "2025"); T("date(\"2025-05-27 14:05:09.123\").month", "5"); T("date(\"2025-05-27 14:05:09.123\").day", "27");
    T("date(\"2025-05-27 14:05:09.123\").hour", "14"); T("date(\"2025-05-27 14:05:09.123\").minute", "5"); T("date(\"2025-05-27 14:05:09.123\").second", "9"); T("date(\"2025-05-27 14:05:09.123\").millisecond", "123");
    T("now().date().format(\"YYYY-MM-DD HH:mm:ss\")", "\"2025-05-27 00:00:00\"");
    T("date(\"2025-05-27\").format(\"YYYY-MM-DD\")", "\"2025-05-27\"");
    T("date(\"2025-05-27 14:05:09\").format(\"dddd, MMMM Do YYYY\")", "\"Tuesday, May 27th 2025\"");
    T("now().time()", "\"14:05:09\"");
    T("(now() - \"3 days\").relative()", "\"3 days ago\"");
    T("(now() + \"3 days\").relative()", "\"in 3 days\"");
    T("(now() - \"5 minutes\").relative()", "\"5 minutes ago\"");
    T("(now() - \"10 seconds\").relative()", "\"a few seconds ago\"");
    T("(now() - \"2 hours\").relative()", "\"2 hours ago\"");
    T("(now() - \"1 year\").relative()", "\"a year ago\"");
    T("(now() - \"3 years\").relative()", "\"3 years ago\"");
    T("(now() + \"60 minutes\").relative()", "\"in an hour\""); T("(now() + \"90 minutes\").relative()", "\"in 2 hours\"");
    T("(now() - \"4 months\").relative()", "\"4 months ago\"");
    T("now().isEmpty()", "false");
    T("file.mtime.relative()", "\"a day ago\"");
    T("date(\"2024-12-01\") + \"1M\" + \"4h\" + \"3m\"", "date(2025-01-01 04:03:00)");
    T("now() + \"1 day\"", "date(2025-05-28 14:05:09)");
    T("today() + \"7d\"", "date(2025-06-03)"); T("today() + \"36h\"", "date(2025-05-28 12:00:00)");
    T("date(\"2025-01-31\") + \"1M\"", "date(2025-02-28)");
    T("date(\"2024-01-31\") + \"1M\"", "date(2024-02-29)");
    T("date(\"2025-03-31\") - \"1M\"", "date(2025-02-28)");
    T("date(\"2025-05-27\") + \"1y\"", "date(2026-05-27)");
    T("date(\"2025-05-27\") + \"2 weeks\"", "date(2025-06-10)");
    T("date(\"2025-05-27 10:00:00\") - \"2h\"", "date(2025-05-27 08:00:00)");
    T("date(\"2025-06-01\") - date(\"2025-05-27\")", "duration(5d)");
    T("note.release_date - today()", "duration(5d)");
    T("(date(\"2025-06-01\") - date(\"2025-05-27\")).days", "5");
    T("(date(\"2025-05-27 12:00\") - date(\"2025-05-27 09:30\")).hours", "2");
    T("(date(\"2025-05-27 12:00\") - date(\"2025-05-27 09:30\")).minutes", "150");
    T("file.mtime > now() - \"1 week\"", "true");
    T("file.mtime > now() - \"1 hour\"", "false");
    T("file.ctime < file.mtime", "true");
    T("date(\"2025-05-27\") + duration(\"1d\")", "date(2025-05-28)");
    T("duration(\"1d\") + date(\"2025-05-27\")", "date(2025-05-28)");
    E("date(\"2025-05-27\") + \"soon\"", "not a duration");
    E("date(\"2025-05-27\") + 5", "duration");
    E("date(\"2025-05-27\") * 2", "arithmetic");

    /* ---- strings ---- */
    T("\"hello\".length", "5"); T("\"h\xc3\xa9llo\".length", "5");
    T("\"hello\".contains(\"ell\")", "true"); T("\"hello\".contains(\"xyz\")", "false"); T("\"hello\".contains(\"\")", "true");
    T("\"hello\".containsAll(\"h\", \"e\")", "true"); T("\"hello\".containsAll(\"h\", \"z\")", "false");
    T("\"hello\".containsAny(\"x\", \"y\", \"e\")", "true"); T("\"hello\".containsAny(\"x\", \"y\")", "false");
    T("\"hello\".endsWith(\"lo\")", "true"); T("\"hello\".endsWith(\"he\")", "false"); T("\"hello\".startsWith(\"he\")", "true"); T("\"hello\".startsWith(\"lo\")", "false");
    T("\"Hello world\".isEmpty()", "false"); T("\"\".isEmpty()", "true"); T("missing.isEmpty()", "true");
    T("\"HeLLo\".lower()", "\"hello\"");
    T("\"a:b:c:d\".replace(/:/, \"-\")", "\"a-b:c:d\""); T("\"a:b:c:d\".replace(/:/g, \"-\")", "\"a-b-c-d\"");
    T("\"John Smith\".replace(/(\\w+) (\\w+)/, \"$2, $1\")", "\"Smith, John\"");
    T("\"a.b.c\".replace(\".\", \"-\")", "\"a-b-c\"");                          /* a text pattern is not a regular expression and every occurrence goes */
    T("\"aaa\".replace(\"a\", \"bb\")", "\"bbbbbb\"");
    T("\"Hello\".replace(/l/gi, \"L\")", "\"HeLLo\"");
    T("\"HELLO\".replace(/l/i, \"_\")", "\"HE_LO\"");
    T("\"abc\".replace(/b/, \"$&$&\")", "\"abbc\""); T("\"abc\".replace(/b/, \"$$\")", "\"a$c\""); T("\"abc\".replace(/(b)/, \"[$1][$2]\")", "\"a[b][$2]c\"");
    T("\"abc\".replace(/x*/g, \"-\")", "\"-a-b-c-\"");
    T("\"123\".repeat(2)", "\"123123\""); T("\"ab\".repeat(0)", "\"\"");
    T("\"hello\".reverse()", "\"olleh\""); T("\"h\xc3\xa9\".reverse()", "\"\xc3\xa9h\"");
    T("\"hello\".slice(1, 4)", "\"ell\""); T("\"hello\".slice(2)", "\"llo\""); T("\"hello\".slice(-3)", "\"llo\""); T("\"hello\".slice(1, -1)", "\"ell\""); T("\"hello\".slice(9)", "\"\""); T("\"hello\".slice(3, 1)", "\"\"");
    T("\"a,b,c,d\".split(\",\", 3)", "[\"a\",\"b\",\"c\"]"); T("\"a,b,c,d\".split(/,/, 3)", "[\"a\",\"b\",\"c\"]"); T("\"a,b,c,d\".split(\",\")", "[\"a\",\"b\",\"c\",\"d\"]");
    T("\"abc\".split(\"\")", "[\"a\",\"b\",\"c\"]"); T("\"a1b22c\".split(/[0-9]+/)", "[\"a\",\"b\",\"c\"]"); T("\"\".split(\",\")", "[\"\"]"); T("\",a,\".split(\",\")", "[\"\",\"a\",\"\"]");
    T("\"hello world\".title()", "\"Hello World\""); T("\"HELLO wORLD\".title()", "\"Hello World\"");
    T("\"  hi  \".trim()", "\"hi\""); T("\"\\t\\nx\\n\".trim()", "\"x\"");
    T("\"hello\"[1]", "\"e\""); T("\"hello\"[9]", "null");
    T("link(\"https://obsidian.md\").length", "19"); T("link(\"https://obsidian.md\").contains(\"obsidian\")", "true"); T("link(\"https://obsidian.md\").startsWith(\"https\")", "true");
    E("\"x\".nonsense()", "no function"); E("\"x\".repeat(-1)", "count"); E("\"x\".replace(1, 2)", "replace"); E("\"x\".contains()", "argument");

    /* ---- numbers ---- */
    T("(-5).abs()", "5"); T("(5).abs()", "5"); T("(2.1).ceil()", "3"); T("(-2.1).ceil()", "-2"); T("(2.9).floor()", "2"); T("(-2.1).floor()", "-3");
    T("(5).isEmpty()", "false"); T("(0/0).isEmpty()", "true");
    T("(2.5).round()", "3"); T("(2.3333).round(2)", "2.33"); T("(2.4).round()", "2"); T("(-2.5).round()", "-2"); T("(1234.5678).round(1)", "1234.6");
    T("(3.14159).toFixed(2)", "\"3.14\""); T("(5).toFixed(2)", "\"5.00\""); T("(2.5).toFixed(0)", "\"3\""); T("(-2.5).toFixed(0)", "\"-3\""); T("(0.5).toFixed(0)", "\"1\"");   /* a tie goes away from zero, as in JavaScript (printf gives "2", "-2" and "0") */ T("(1.005).toFixed(2)", "\"1.00\""); T("(0).toFixed(1)", "\"0.0\"");
    E("(1).toFixed(-1)", "digits"); E("(1).nonsense()", "no function");
    T("price.toFixed(2)", "\"12.50\"");

    /* ---- lists ---- */
    T("[1,2,3].length", "3");
    T("[1,2,3].contains(2)", "true"); T("[1,2,3].contains(5)", "false"); T("[\"a\"].contains(\"a\")", "true"); T("[[1]].contains([1])", "true");
    T("[1,2,3].containsAll(2,3)", "true"); T("[1,2,3].containsAll(2,9)", "false"); T("[1,2,3].containsAny(3,4)", "true"); T("[1,2,3].containsAny(8,9)", "false");
    T("[1,2,3,4].filter(value > 2)", "[3,4]"); T("[1,2,3,4].filter(index > 0 && value < 4)", "[2,3]"); T("[\"a\",\"\",\"b\"].filter(value)", "[\"a\",\"b\"]");
    T("[1,[2,3]].flat()", "[1,2,3]"); T("[[1],[2,[3]]].flat()", "[1,2,[3]]"); T("[].flat()", "[]");
    T("[1,2,3].isEmpty()", "false"); T("[].isEmpty()", "true");
    T("[1,2,3].join(\",\")", "\"1,2,3\""); T("[1,2,3].join(\" - \")", "\"1 - 2 - 3\""); T("[\"a\",\"b\"].join()", "\"a,b\""); T("[].join(\",\")", "\"\"");
    T("[1,2,3,4].map(value + 1)", "[2,3,4,5]"); T("[1,2,3].map(value * index)", "[0,2,6]"); T("[\"a\",\"b\"].map(value + \"!\")", "[\"a!\",\"b!\"]");
    T("[1,2,3].reduce(acc + value, 0)", "6"); T("[].reduce(acc + value, 7)", "7"); T("[1,2,3].reduce(acc + \",\" + value, \"\")", "\",1,2,3\"");
    T("[1, \"x\", 3, 2].filter(value.isType(\"number\")).reduce(if(acc == null || value > acc, value, acc), null)", "3");
    T("[\"x\"].filter(value.isType(\"number\")).reduce(if(acc == null || value > acc, value, acc), null)", "null");
    T("[1,2,3].reverse()", "[3,2,1]"); T("[].reverse()", "[]");
    T("[1,2,3,4].slice(1,3)", "[2,3]"); T("[1,2,3,4].slice(2)", "[3,4]"); T("[1,2,3,4].slice(-2)", "[3,4]"); T("[1,2,3,4].slice(0,-1)", "[1,2,3]");
    T("[3, 1, 2].sort()", "[1,2,3]"); T("[\"c\", \"a\", \"b\"].sort()", "[\"a\",\"b\",\"c\"]"); T("[3, \"a\", 1, null, true].sort()", "[null,true,1,3,\"a\"]");
    T("[date(\"2025-02-01\"), date(\"2025-01-01\")].sort()", "[date(2025-01-01),date(2025-02-01)]");
    T("[1,2,2,3].unique()", "[1,2,3]"); T("[\"a\",\"b\",\"a\"].unique()", "[\"a\",\"b\"]"); T("[[1],[1],[2]].unique()", "[[1],[2]]");
    T("[1,2,3,4].sum()", "10"); T("[1,2,3,4].mean()", "2.5"); T("[3,1,2].median()", "2"); T("[1,2,3,4].median()", "2.5"); T("[2,4,4,4,5,5,7,9].stddev()", "2");
    T("[1,\"x\",null,3].sum()", "4"); T("[].mean()", "null"); T("[].sum()", "0"); T("[1,2,3].mean().round(3)", "2"); T("[1,2,4].mean().round(3)", "2.333");
    T("[10,20,30][1]", "20"); T("[10,20,30][5]", "null"); T("[10,20,30][-1]", "null");
    T("authors.map(value.asFile().properties.role)", "[\"author\",null]");
    T("authors.map(value.asFile().name)", "[\"Alice.md\",\"Bob.md\"]");
    T("authors[0].asFile().properties.role", "\"author\"");
    T("list(authors)[0].asFile().basename", "\"Alice\"");
    T("authors.filter(value.asFile().properties.role != \"x\").length", "2");
    E("[1].nonsense()", "no function"); E("[1,2].filter()", "filter"); E("[1,2].reduce(acc)", "reduce");

    /* ---- links, files ---- */
    T("link(\"Textbook\").asFile()", "file(\"Textbook.md\")"); T("link(\"Nothing\").asFile()", "null"); T("link(\"[[Alice]]\").asFile().name", "\"Alice.md\"");
    T("link(\"Book\").linksTo(file(\"Textbook.md\"))", "true"); T("link(\"Book\").linksTo(file(\"Bob.md\"))", "false"); T("link(\"Alice\").linksTo(file(\"Bob\"))", "true");
    T("file.name", "\"Book.md\""); T("file.basename", "\"Book\""); T("file.path", "\"Notes/Book.md\""); T("file.folder", "\"Notes\""); T("file.ext", "\"md\""); T("file.size", "1234");
    T("file.tags", "[\"#book\",\"#x/y\"]"); T("file.links.length", "2"); T("file.ctime.year", "2025"); T("file.file.name", "\"Book.md\"");
    T("file.properties.price", "12.5"); T("file.properties[\"age\"]", "5"); T("file.properties.isEmpty()", "false");
    T("file.properties.keys().length", "9"); T("file.properties.keys()[0]", "\"price\""); T("file.properties.values()[0]", "12.5");
    T("file.asLink()", "link(\"Notes/Book.md\")"); T("file.asLink(\"This\")", "link(\"Notes/Book.md\",\"This\")"); S("file.asLink(\"This\")", "This");
    T("file.hasLink(\"Textbook\")", "true"); T("file.hasLink(file(\"Alice\"))", "true"); T("file.hasLink(link(\"Alice\"))", "true"); T("file.hasLink(\"Bob\")", "false"); T("file.hasLink(\"Nothing\")", "false");
    T("file(\"Alice\").hasLink(\"Bob\")", "true");
    T("file.hasProperty(\"price\")", "true"); T("file.hasProperty(\"nothing\")", "false");
    T("file.hasTag(\"book\")", "true"); T("file.hasTag(\"#book\")", "true"); T("file.hasTag(\"x\")", "true"); T("file.hasTag(\"x/y\")", "true"); T("file.hasTag(\"x/z\")", "false"); T("file.hasTag(\"school\", \"book\")", "true");
    T("file.hasTag(\"bo\")", "false"); T("file.hasTag(\"BOOK\")", "true"); T("file.hasTag()", "false"); T("file(\"Textbook\").hasTag(\"school\")", "true");
    T("file.inFolder(\"Notes\")", "true"); T("file.inFolder(\"Notes/\")", "true"); T("file.inFolder(\"Other\")", "false"); T("file.inFolder(\"Not\")", "false"); T("file(\"Textbook\").inFolder(\"Notes\")", "false"); T("file.inFolder(\"\")", "true");
    T("this.file.name", "\"Alice.md\""); T("this.file.folder", "\"\""); T("this.name", "\"Alice.md\""); T("this.role", "\"author\""); T("this.file.properties.role", "\"author\"");
    T("file.hasLink(this.file)", "true"); T("authors.contains(this)", "true"); T("authors[1] == this", "false"); T("authors[0] == this", "true"); T("authors[0] == this.file", "true"); T("link(\"Alice\") == link(\"alice\")", "true");
    T("link(\"Alice\") != link(\"Bob\")", "true"); T("link(\"Nothing\") == link(\"Nothing\")", "true"); T("link(\"Nothing\") == link(\"Other\")", "false");
    E("file.nonsense()", "no function"); E("link(\"a\").nonsense()", "no function");

    /* ---- objects, regular expressions ---- */
    T("note.nested.a", "1"); T("nested.b", "\"two\""); T("nested.missing", "null"); T("nested.keys()", "[\"a\",\"b\"]"); T("nested.values()", "[1,\"two\"]"); T("nested.isEmpty()", "false");
    T("note[\"nested\"][\"b\"]", "\"two\""); T("nested == nested", "true");
    T("/abc/.matches(\"abcde\")", "true"); T("/^abc$/.matches(\"abcde\")", "false"); T("/B/i.matches(\"abc\")", "true"); T("/b/.matches(\"abc\")", "true"); T("/a.c/s.matches(\"a\\nc\")", "true");
    T("/\\d+/.matches(\"x42\")", "true"); E("/(/.matches(\"x\")", "not valid");
    T("6 / 2 / /x/.matches(\"x\")", "3");                    /* a slash after an operand is a division */

    /* ---- properties, namespaces, formulas ---- */
    T("price", "12.5"); T("note.price", "12.5"); T("note[\"price\"]", "12.5"); T("missing", "null"); T("note.missing", "null"); T("title", "\"The Book\"");
    T("price * age", "62.5"); T("price / age", "2.5"); T("file.name + \" - \" + title", "\"Book.md - The Book\""); T("status == \"done\"", "true"); T("status != \"done\"", "false");
    T("empty.isEmpty()", "true"); T("title.isEmpty()", "false");
    T("image-property", "\"img.png\"");                      /* a hyphen with no spaces is part of a name */
    T("price-age", "null");                                   /* ... so this is a property called price-age */
    T("price - age", "7.5"); T("price -age", "7.5"); T("price-5", "7.5");
    T("formula.ppu", "\"2.50\""); T("formula.double", "\"2.50x\""); T("formula.label", "\"$12.50\""); T("formula.nothing", "null"); T("formula[\"ppu\"]", "\"2.50\"");
    E("formula.a", "circular reference: a -> b -> a"); E("formula.self", "circular reference: self -> self");
    E("formula", "needs a property name"); E("note", "needs a property name");
    T("formula.ppu.length", "4");
    T("if(formula.ppu, formula.ppu + \"!\", \"none\")", "\"2.50!\"");
    {   /* custom summaries see `values` */
        ExValue vals[4], list;
        ExValue r;
        vals[0] = ex_v_num(1); vals[1] = ex_v_num(2); vals[2] = ex_v_num(4); vals[3] = ex_v_null();
        list = ex_v_list(A, vals, 4);
        r = ex_expr_run("values.mean().round(3)", A, &HOST, &list); CHECK(r.t == EXV_NUM && r.u.num == 2.333);
        r = ex_expr_run("values.filter(value.isType(\"number\")).length", A, &HOST, &list); CHECK(r.t == EXV_NUM && r.u.num == 3);
        r = ex_expr_run("values", A, &HOST, NULL); CHECK(r.t == EXV_NULL);                           /* without a list it is a property named values */
    }

    /* ---- values on their own ---- */
    {
        ExValue a = ex_v_num(1), b = ex_v_str(A, "x");
        CHECK(ex_value_compare(ex_v_null(), a) < 0 && ex_value_compare(a, b) < 0 && ex_value_compare(b, ex_v_date(0, 0)) < 0 && ex_value_compare(a, a) == 0);
        CHECK(ex_value_truthy(a) && !ex_value_truthy(ex_v_num(0)) && !ex_value_truthy(ex_v_num(NAN)) && !ex_value_truthy(ex_v_null()) && ex_value_truthy(ex_v_date(0, 0)));
        CHECK(!ex_value_equal(&HOST, ex_v_num(NAN), ex_v_num(NAN)));
        CHECK(strcmp(ex_value_type_name(b), "string") == 0 && strcmp(ex_value_type_name(ex_v_error(A, "x")), "error") == 0);
        CHECK(!strcmp(ex_expr_function_name(0), "global:escapeHTML") && !strcmp(ex_expr_function_name(71), "regexp:matches") && ex_expr_function_name(72) == NULL);
    }

    /* ---- errors are values; nothing runs away ---- */
    E("1 +", "ends too soon"); E("(1", "missing"); E("1 2", "belong"); E("[1", "missing"); E("foo(", "ends"); E("\"open", "not closed"); E("/open", "not closed"); E("1 @ 2", "unexpected character");
    E("", "ends too soon"); E("   ", "ends too soon"); E("1 +* 2", "belong"); E("a.", "name is missing"); E("a[", "ends"); E(")", "belong"); E("5(3)", "called"); E("nothing()", "no function");
    T("\"x\".y.z()", "null"); T("1e999999", "Infinity");
    E("number(\"x\") + 1", "not a number"); E("[number(\"x\")]", "not a number"); E("[1,2].map(number(\"x\"))", "not a number");
    {
        char *deep = malloc(20000);
        size_t k;
        ExValue v;
        for (k = 0; k < 5000; k++) deep[k] = '(';
        deep[5000] = '1';
        for (k = 0; k < 5000; k++) deep[5001 + k] = ')';
        deep[10001] = 0;
        v = ex_expr_run(deep, A, &HOST, NULL);
        CHECK(v.t == EXV_ERROR && strstr(v.u.str, "nested"));
        for (k = 0; k < 5000; k++) deep[k] = '[';
        for (k = 0; k < 5000; k++) deep[5001 + k] = ']';
        v = ex_expr_run(deep, A, &HOST, NULL);
        CHECK(v.t == EXV_ERROR);
        for (k = 0; k < 4000; k++) memcpy(deep + k * 3, "-!-", 3);
        deep[12000] = '1'; deep[12001] = 0;
        v = ex_expr_run(deep, A, &HOST, NULL);
        CHECK(v.t == EXV_ERROR || v.t == EXV_NUM || v.t == EXV_BOOL);                                  /* no crash, whatever it says */
        free(deep);
    }
    E("\"ab\".repeat(1000000000)", "too long");
    E("\"x\".repeat(500000).replace(/x/g, \"yyyyyyyyyyyyyyyyyy\")", "too long");                     /* the regular expression's objects are freed on this way out too (the fuzz job found a leak here) */
    E("[1,2,3,4,5,6,7,8,9,10].map([1,2,3,4,5,6,7,8,9,10].map([1,2,3,4,5,6,7,8,9,10].map([1,2,3,4,5,6,7,8,9,10].map([1,2,3,4,5,6,7,8,9,10].map([1,2,3,4,5,6,7,8,9,10].map([1,2,3,4,5,6,7,8,9,10].map(value)))))))", "takes too long");
    E("formula.c0", "too deeply"); T("formula.c10", "5");                                                   /* the formulas that call each other are limited in depth */
    S("[1,2,3].map(value * 2).join(\"+\")", "2+4+6");

    /* ---- every function was run ---- */
    for (i = 0; i < ex_expr_function_count(); i++) {
        const char *full = ex_expr_function_name(i), *colon = strchr(full, ':'), *name = colon + 1;
        char probe[80];
        snprintf(probe, sizeof probe, "%s(", name);
        if (!strstr(g_all, probe)) { fprintf(stderr, "function %s was never run\n", full); failures++; }
    }
    free(g_all);
    for (i = 0; g_fnames[i]; i++) ex_expr_free(g_formulas[i]);
    ex_arena_free(A);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
