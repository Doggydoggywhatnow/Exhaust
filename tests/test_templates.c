/* dates_templates (6e-i): the documented date tokens, the template variables, the merge of a template's properties into a note (text surgery: nothing the
 * template does not add is touched), and daily notes (paths with folders and formats, the day of a path, a note made in the configured folder from the
 * template, one that exists never changed). Works in ./templates_test, which it removes before and after. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_datefmt.h"
#include "exhaust_platform.h"
#include "exhaust_templates.h"
#include "exhaust_vault.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void expect_fmt(double ms, int tz, const char *fmt, const char *want) {
    char out[300];
    ex_date_format(out, sizeof out, ms, tz, fmt);
    if (strcmp(out, want)) { fprintf(stderr, "format %s: got \"%s\", want \"%s\"\n", fmt, out, want); failures++; }
}

static void test_tokens(void) {
    double d = ex_date_make(2026, 11, 23, 14, 5, 9, 123, 0);    /* Monday */
    double leap = ex_date_make(2024, 2, 29, 0, 7, 0, 0, 0);
    expect_fmt(d, 0, "YYYY-MM-DD", "2026-11-23");
    expect_fmt(d, 0, "YYYY YY", "2026 26");
    expect_fmt(d, 0, "M MM MMM MMMM", "11 11 Nov November");
    expect_fmt(d, 0, "D DD Do", "23 23 23rd");
    expect_fmt(d, 0, "DDD DDDD", "327 327");
    expect_fmt(d, 0, "d dd ddd dddd", "1 Mo Mon Monday");
    expect_fmt(d, 0, "E e", "1 1");
    expect_fmt(d, 0, "w W", "48 48");
    expect_fmt(d, 0, "Q Qo", "4 4th");
    expect_fmt(d, 0, "H HH h hh k kk", "14 14 2 02 14 14");
    expect_fmt(d, 0, "m mm s ss", "5 05 9 09");
    expect_fmt(d, 0, "SSS", "123");
    expect_fmt(d, 0, "a A", "pm PM");
    expect_fmt(d, 0, "X", "1795442709");
    expect_fmt(d, 0, "Z ZZ", "+00:00 +0000");
    expect_fmt(d, 90, "HH:mm Z", "15:35 +01:30");                  /* a time zone east of UTC */
    expect_fmt(d, -300, "YYYY-MM-DD HH:mm", "2026-11-23 09:05");
    expect_fmt(d, 0, "[Today is] dddd", "Today is Monday");
    expect_fmt(d, 0, "\\Y YYYY", "Y 2026");
    expect_fmt(leap, 0, "YYYYMMDDHHmm", "202402290007");           /* the leap day */
    expect_fmt(leap, 0, "DDD W", "60 9");
    expect_fmt(ex_date_add_months(ex_date_make(2024, 1, 31, 0, 0, 0, 0, 0), 0, 1), 0, "YYYY-MM-DD", "2024-02-29");
    expect_fmt(ex_date_make(2026, 1, 1, 0, 0, 0, 0, 0), 0, "dddd Do [of] MMMM", "Thursday 1st of January");
    expect_fmt(d, 0, "L", "11/23/2026");
    expect_fmt(d, 0, "LL", "November 23, 2026");
}

static const char *expand(const char *tpl, const ExTplContext *c, char *out, size_t cap) {
    size_t n = 0;
    char *r = ex_tpl_expand(tpl, strlen(tpl), c, &n);
    snprintf(out, cap, "%s", r ? r : "(null)");
    free(r);
    return out;
}

static void test_variables(void) {
    ExTplContext c;
    char out[500];
    memset(&c, 0, sizeof c);
    c.title = "My note"; c.now_ms = ex_date_make(2026, 11, 23, 14, 5, 9, 0, 0);
    CHECK(!strcmp(expand("# {{title}}", &c, out, sizeof out), "# My note"));
    CHECK(!strcmp(expand("{{date}} {{time}}", &c, out, sizeof out), "2026-11-23 14:05"));        /* the defaults */
    CHECK(!strcmp(expand("{{date:dddd, MMMM Do}} at {{time:h:mm a}}", &c, out, sizeof out), "Monday, November 23rd at 2:05 pm"));
    CHECK(!strcmp(expand("{{ title }} {{ date : YYYY }}", &c, out, sizeof out), "My note 2026"));  /* spaces */
    c.date_format = "DD.MM.YYYY"; c.time_format = "HH:mm:ss";
    CHECK(!strcmp(expand("{{date}} {{time}}", &c, out, sizeof out), "23.11.2026 14:05:09"));
    CHECK(!strcmp(expand("{{unknown}} {{date:}} {{title", &c, out, sizeof out), "{{unknown}}  {{title"));    /* what is not a variable stays */
    c.content = "BODY"; c.from_title = "From"; c.new_title = "New";
    CHECK(!strcmp(expand("{{fromTitle}} -> {{newTitle}}: {{content}}", &c, out, sizeof out), "From -> New: BODY"));
    CHECK(!strcmp(expand("{{title}}{{title}}", NULL, out, sizeof out), ""));                     /* no context: empty, no crash */
    CHECK(!strcmp(expand("no braces { } {x}", &c, out, sizeof out), "no braces { } {x}"));
    c.time_ms = ex_date_make(2026, 11, 23, 9, 30, 0, 0, 0); c.use_time_ms = 1;
    CHECK(!strcmp(expand("{{date:HH}} {{time:HH}}", &c, out, sizeof out), "14 09"));              /* a daily note: the day and the time it was made */
}

typedef struct MergeCase { const char *note, *tpl, *want; size_t cursor; } MergeCase;
static const char *apply(const char *note, const char *tpl, size_t cursor, char *out, size_t cap) {
    ExTplContext c;
    size_t n, after;
    char *r;
    memset(&c, 0, sizeof c);
    c.title = "T"; c.now_ms = ex_date_make(2026, 1, 2, 3, 4, 5, 0, 0);
    r = ex_tpl_apply(note, strlen(note), tpl, strlen(tpl), &c, cursor, &n, &after);
    snprintf(out, cap, "%s", r ? r : "(null)");
    free(r);
    return out;
}

static void test_merge(void) {
    static const MergeCase cases[] = {
        /* no properties in the note: the template's become the note's */
        { "Text\n", "---\ntags: [a]\nstatus: draft\n---\nBody\n", "---\ntags: [a]\nstatus: draft\n---\nBody\nText\n", 0 },
        /* the note's own value of a scalar wins; a new key is appended as written; the note's lines stay byte for byte */
        { "---\nstatus:   done\nauthor: \"Me\"\n---\nText\n", "---\nstatus: draft\ncreated: {{date}}\n---\n", "---\nstatus:   done\nauthor: \"Me\"\ncreated: 2026-01-02\n---\nText\n", 0 },
        /* lists: the items the note lacks are added (block lists, the note's indent kept) */
        { "---\ntags:\n  - a\n  - b\n---\nX\n", "---\ntags:\n  - b\n  - c\n---\n", "---\ntags:\n  - a\n  - b\n  - c\n---\nX\n", 0 },
        { "---\ntags:\n- a\n---\n", "---\ntags: [a, \"b c\"]\n---\n", "---\ntags:\n- a\n- \"b c\"\n---\n", 0 },
        /* flow lists */
        { "---\ntags: [a, b]\n---\n", "---\ntags: [b, c]\n---\n", "---\ntags: [a, b, c]\n---\n", 0 },
        { "---\ntags: []\n---\n", "---\ntags: [x, y]\n---\n", "---\ntags: [x, y]\n---\n", 0 },
        /* an empty value takes the items; a scalar with a value is not touched */
        { "---\ntags:\n---\n", "---\ntags:\n  - one\n---\n", "---\ntags:\n  - one\n---\n", 0 },
        { "---\ntags: single\n---\n", "---\ntags: [other]\n---\n", "---\ntags: single\n---\n", 0 },
        /* nothing to add: the note is unchanged apart from the body */
        { "---\ntags: [a]\n---\nHi\n", "---\ntags: [a]\n---\n", "---\ntags: [a]\n---\nHi\n", 0 },
        /* a template without properties is only a body, put at the cursor */
        { "Hello world\n", "inserted {{title}}\n", "Hello inserted T\nworld\n", 6 },
        { "---\na: 1\n---\nHello\n", "plain\n", "---\na: 1\n---\nplain\nHello\n", 0 },       /* a cursor in the properties goes after them */
        /* comments and other lines of the note's properties are kept */
        { "---\n# keep me\naliases: [x]   # why\nnested:\n  k: v\n---\n", "---\nnested: other\nnew: 1\n---\n", "---\n# keep me\naliases: [x]   # why\nnested:\n  k: v\nnew: 1\n---\n", 0 },
        /* a note with Windows line breaks: the key is found, the lines are kept */
        { "---\r\ntags: [a]\r\n---\r\n", "---\nnew: 1\n---\n", "---\r\ntags: [a]\r\nnew: 1\n---\r\n", 0 },
        /* the closing line may be ... and the opening a --- with spaces */
        { "---  \nx: 1\n...\nB\n", "---\ny: 2\n---\n", "---  \nx: 1\ny: 2\n...\nB\n", 0 },
        /* a note that only looks like it has properties (no closing line) gets a block of its own */
        { "---\nnot closed\n", "---\nk: v\n---\n", "---\nk: v\n---\n---\nnot closed\n", 0 },
        /* an empty template changes nothing */
        { "text\n", "", "text\n", 2 },
    };
    size_t i;
    char out[800];
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        apply(cases[i].note, cases[i].tpl, cases[i].cursor, out, sizeof out);
        if (strcmp(out, cases[i].want)) { fprintf(stderr, "merge case %zu:\n got  [%s]\n want [%s]\n", i, out, cases[i].want); failures++; }
    }
    {   /* the plan: two edits in the coordinates of the note, the front one first */
        ExTplContext c;
        ExTplPlan p;
        const char *note = "---\na: 1\n---\nHi\n", *tpl = "---\nb: 2\n---\nbody";
        memset(&c, 0, sizeof c);
        CHECK(ex_tpl_plan(note, strlen(note), tpl, strlen(tpl), &c, 100, &p) == 0);      /* a cursor past the end is the end */
        CHECK(p.has_front && p.front_off == 4 && p.front_del == 5 && p.front_len == 10 && !memcmp(p.front_text, "a: 1\nb: 2\n", 10));
        CHECK(p.body_off == strlen(note) && p.body_len == 4 && p.front_off + p.front_del <= p.body_off);
        ex_tpl_plan_free(&p);
    }
    {   /* a byte order mark stays first */
        const char *note = "\xEF\xBB\xBFText", *tpl = "---\nk: v\n---\n";
        ExTplContext c;
        size_t n, after;
        char *r;
        memset(&c, 0, sizeof c);
        r = ex_tpl_apply(note, strlen(note), tpl, strlen(tpl), &c, 0, &n, &after);
        CHECK(r && !strncmp(r, "\xEF\xBB\xBF---\nk: v\n---\nText", n));
        free(r);
    }
}

static void test_daily(void) {
    ExDailyConfig c;
    char path[300], root[300] = "templates_test";
    double d = ex_date_make(2026, 11, 23, 14, 5, 9, 0, 0), back;
    ExVault *v;
    char rel[300];
    char *abs;
    size_t len;
    char *text;
    memset(&c, 0, sizeof c);
    CHECK(ex_daily_path(&c, d, path, sizeof path) == 0 && !strcmp(path, "2026-11-23.md"));       /* the defaults */
    c.folder = "Journal/"; c.date_format = "YYYY/MM/YYYY-MM-DD";
    CHECK(ex_daily_path(&c, d, path, sizeof path) == 0 && !strcmp(path, "Journal/2026/11/2026-11-23.md"));
    CHECK(ex_daily_date_of(&c, path, &back) == 0 && back == ex_date_make(2026, 11, 23, 0, 0, 0, 0, 0));
    CHECK(ex_daily_date_of(&c, "Other/2026/11/2026-11-23.md", &back) != 0 && ex_daily_date_of(&c, "Journal/2026/11/x.md", &back) != 0 && ex_daily_date_of(&c, "Journal/2026/11/2026-11-23.txt", &back) != 0);
    CHECK(ex_daily_date_of(&c, "Journal/2026/11/2026-02-30.md", &back) != 0);                      /* a date that does not exist */
    c.date_format = "dddd, MMMM Do YYYY"; c.folder = "";
    CHECK(ex_daily_path(&c, d, path, sizeof path) == 0 && !strcmp(path, "Monday, November 23rd 2026.md"));
    CHECK(ex_daily_date_of(&c, path, &back) == 0 && back == ex_date_make(2026, 11, 23, 0, 0, 0, 0, 0));
    c.date_format = "YYYY:MM:DD";                                                              /* a colon is not allowed in a note name */
    CHECK(ex_daily_path(&c, d, path, sizeof path) != 0);
    c.date_format = "../YYYY"; CHECK(ex_daily_path(&c, d, path, sizeof path) != 0);              /* nor a way out of the folder */
    c.date_format = "YYYY-MM-DD"; c.folder = "../x"; CHECK(ex_daily_path(&c, d, path, sizeof path) != 0);
    c.folder = "";
    {   /* the day before and after, across a month and a year end and a leap day */
        double e;
        c.tz_min = 0;
        e = ex_daily_shift(&c, ex_date_make(2026, 12, 31, 23, 59, 0, 0, 0), 1);
        ex_daily_path(&c, e, path, sizeof path); CHECK(!strcmp(path, "2027-01-01.md"));
        e = ex_daily_shift(&c, ex_date_make(2024, 3, 1, 5, 0, 0, 0, 0), -1);
        ex_daily_path(&c, e, path, sizeof path); CHECK(!strcmp(path, "2024-02-29.md"));
        e = ex_daily_shift(&c, ex_date_make(2024, 3, 1, 5, 0, 0, 0, 120), -1);                   /* in a time zone */
        c.tz_min = 120; ex_daily_path(&c, e, path, sizeof path); CHECK(!strcmp(path, "2024-02-29.md"));
        c.tz_min = 0;
    }

    /* a note made in a vault */
    ex_platform_remove_tree(root);
    ex_platform_make_dirs(root);
    ex_platform_make_dirs("templates_test/Templates");
    {
        FILE *f = fopen("templates_test/Templates/Daily.md", "wb");
        CHECK(f != NULL);
        if (f) { fputs("---\ntags: [daily]\n---\n# {{title}}\n\nCreated {{time:HH:mm}} on {{date:dddd}}.\n", f); fclose(f); }
    }
    v = ex_vault_open(root);
    CHECK(v != NULL);
    if (!v) return;
    c.folder = "Journal"; c.date_format = "YYYY/YYYY-MM-DD"; c.template_path = "Templates/Daily";
    CHECK(ex_daily_ensure(v, &c, d, ex_date_make(2026, 11, 23, 9, 41, 0, 0, 0), "HH:mm", rel, sizeof rel) == 1);
    CHECK(!strcmp(rel, "Journal/2026/2026-11-23.md"));
    abs = ex_vault_abs_path(v, rel);
    text = abs ? ex_platform_read_file(abs, &len) : NULL;
    CHECK(text && !strcmp(text, "---\ntags: [daily]\n---\n# 2026-11-23\n\nCreated 09:41 on Monday.\n"));
    free(text);
    {   /* the user wrote in it: asking again never changes it */
        FILE *f = fopen(abs, "ab");
        if (f) { fputs("my own line\n", f); fclose(f); }
        CHECK(ex_daily_ensure(v, &c, d, 0, "HH:mm", rel, sizeof rel) == 0);
        text = ex_platform_read_file(abs, &len);
        CHECK(text && strstr(text, "my own line") && strstr(text, "Created 09:41"));
        free(text);
    }
    free(abs);
    {   /* the next day, in the same folder, without a template */
        double next = ex_daily_shift(&c, d, 1);
        c.template_path = "";
        CHECK(ex_daily_ensure(v, &c, next, 0, "HH:mm", rel, sizeof rel) == 1 && !strcmp(rel, "Journal/2026/2026-11-24.md"));
        abs = ex_vault_abs_path(v, rel);
        text = ex_platform_read_file(abs, &len);
        CHECK(text != NULL && len == 0);                                                     /* an empty note */
        free(text); free(abs);
        c.template_path = "Templates/Missing";                                               /* a template that is not there: still a note */
        next = ex_daily_shift(&c, d, 2);
        CHECK(ex_daily_ensure(v, &c, next, 0, "HH:mm", rel, sizeof rel) == 1);
    }
    c.date_format = "YYYY:MM"; CHECK(ex_daily_ensure(v, &c, d, 0, "HH:mm", rel, sizeof rel) == -1);      /* an invalid name makes nothing */
    ex_vault_close(v);
    ex_platform_remove_tree(root);
}

int main(void) {
    test_tokens();
    test_variables();
    test_merge();
    test_daily();
    if (failures) fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
