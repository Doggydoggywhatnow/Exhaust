/* base_core (6b-ii): the `.base` file (YAML read and written without losing a key) and running a view over files: filters (and, or, not, global and per
 * view), sort, limit, groups and their order, columns with display names, formulas, the built-in and custom summaries, `this`, errors that stay in the result,
 * CSV and Markdown. The files are made in the test; the clock is fixed at 2025-05-27 14:05:09 UTC. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base.h"
#include "exhaust_datefmt.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define NOW 1748354709000.0

/* the sample of the Bases help */
static const char *SAMPLE =
    "filters:\n  or:\n    - file.hasTag(\"tag\")\n    - and:\n        - file.hasTag(\"book\")\n        - file.hasLink(\"Textbook\")\n    - not:\n        - file.hasTag(\"book\")\n        - file.inFolder(\"Required Reading\")\n"
    "formulas:\n  formatted_price: 'if(price, price.toFixed(2) + \" dollars\")'\n  ppu: \"(price / age).toFixed(2)\"\n"
    "properties:\n  status:\n    displayName: Status\n  formula.formatted_price:\n    displayName: \"Price\"\n  file.ext:\n    displayName: Extension\n"
    "summaries:\n  customAverage: 'values.mean().round(3)'\n"
    "views:\n  - type: table\n    name: \"My table\"\n    limit: 10\n    groupBy:\n      property: note.age\n      direction: DESC\n    filters:\n      and:\n        - 'status != \"done\"'\n        - or:\n            - \"formula.ppu > 5\"\n            - \"price > 2.1\"\n"
    "    order:\n      - file.name\n      - file.ext\n      - note.age\n      - formula.ppu\n      - formula.formatted_price\n    summaries:\n      formula.ppu: Average\n";

static ExBaseData *make_data(void) {
    ExBaseData *d = calloc(1, sizeof *d);
    ExBaseFile *f;
    double t = NOW - 86400000.0 * 3;
    f = ex_base_data_add(d, "Books/Dune.md", 1, t, t, 1000);
    ex_base_file_prop(f, "price", EXB_NUMBER, "9.5"); ex_base_file_prop(f, "age", EXB_NUMBER, "50"); ex_base_file_prop(f, "status", EXB_TEXT, "done"); ex_base_file_prop(f, "author", EXB_LINKS, "[[Herbert]]");
    ex_base_file_prop(f, "read", EXB_DATE, "2025-01-15"); ex_base_file_prop(f, "finished", EXB_CHECKBOX, "true"); ex_base_file_tag(f, "#book"); ex_base_file_tag(f, "#scifi"); ex_base_file_link(f, "Textbook");
    f = ex_base_data_add(d, "Books/Emma.md", 1, t, t + 3600000.0, 2000);
    ex_base_file_prop(f, "price", EXB_NUMBER, "3.25"); ex_base_file_prop(f, "age", EXB_NUMBER, "200"); ex_base_file_prop(f, "status", EXB_TEXT, "reading"); ex_base_file_prop(f, "author", EXB_LINKS, "[[Austen]]");
    ex_base_file_prop(f, "read", EXB_DATE, "2025-03-01"); ex_base_file_prop(f, "finished", EXB_CHECKBOX, "false"); ex_base_file_tag(f, "#book"); ex_base_file_link(f, "Textbook"); ex_base_file_link(f, "Austen");
    f = ex_base_data_add(d, "Books/Ulysses.md", 1, t, t + 7200000.0, 3000);
    ex_base_file_prop(f, "price", EXB_NUMBER, "30"); ex_base_file_prop(f, "age", EXB_NUMBER, "100"); ex_base_file_prop(f, "status", EXB_TEXT, "todo"); ex_base_file_tag(f, "#book"); ex_base_file_tag(f, "#classic/irish");
    f = ex_base_data_add(d, "Books/Anon.md", 1, t, t + 100.0, 400);
    ex_base_file_prop(f, "price", EXB_NUMBER, "1"); ex_base_file_prop(f, "status", EXB_TEXT, "todo"); ex_base_file_tag(f, "#book");                      /* no age */
    f = ex_base_data_add(d, "Textbook.md", 1, t, t, 800); ex_base_file_tag(f, "#tag"); ex_base_file_link(f, "Books/Dune");
    f = ex_base_data_add(d, "Required Reading/Syllabus.md", 1, t, t, 600); ex_base_file_tag(f, "#school");
    f = ex_base_data_add(d, "Notes/Plain.md", 1, t, t, 100);
    f = ex_base_data_add(d, "Pics/cover.png", 4, t, t, 52000);
    f = ex_base_data_add(d, "Herbert.md", 1, t, t, 90); ex_base_file_prop(f, "role", EXB_TEXT, "author");
    f = ex_base_data_add(d, "Austen.md", 1, t, t, 90); ex_base_file_prop(f, "role", EXB_TEXT, "author");
    return d;
}

static ExBase *base_of(const char *text) { char err[200]; ExBase *b = ex_base_parse(text, strlen(text), err, sizeof err); if (!b) fprintf(stderr, "parse: %s\n", err); return b; }
static int run(const char *yaml, size_t view, const ExBaseData *d, const char *this_path, ExBaseResult *r) {
    ExBase *b = base_of(yaml);
    int rc;
    if (!b) return -1;
    rc = ex_base_run(b, view, d, this_path, NOW, 0, r);
    ex_base_free(b);
    return rc;
}
static const char *cell(const ExBaseResult *r, size_t row, size_t col) { return ex_base_cell_text(r, row, col); }
static int has_row(const ExBaseResult *r, const char *path) { size_t i; for (i = 0; i < r->nrows; i++) if (!strcmp(r->files[i]->path, path)) return 1; return 0; }
static const char *names(const ExBaseResult *r, char *buf, size_t cap) {
    size_t i, o = 0;
    buf[0] = 0;
    for (i = 0; i < r->nrows && o + 40 < cap; i++) { const char *p = strrchr(r->files[i]->path, '/'); o += (size_t)snprintf(buf + o, cap - o, "%s%s", i ? "," : "", p ? p + 1 : r->files[i]->path); }
    return buf;
}

static void yaml_cases(void) {
    char err[200];
    size_t len;
    ExYNode *y;
    char *out, *out2;
    ExBase *b;
    /* the sample reads, writes and reads again to the same thing; it is written with lists indented under their keys */
    b = base_of(SAMPLE);
    CHECK(b != NULL);
    if (b) {
        CHECK(ex_base_view_count(b) == 1 && !strcmp(ex_base_view_name(b, 0), "My table") && !strcmp(ex_base_view_type(b, 0), "table") && ex_base_find_view(b, "my TABLE") == 0 && ex_base_find_view(b, "x") == -1);
        out = ex_base_write(b, &len);
        CHECK(out && len == strlen(out));
        CHECK(out && strstr(out, "filters:\n  or:\n    - file.hasTag(\"tag\")\n    - and:\n        - file.hasTag(\"book\")") != NULL);
        CHECK(out && strstr(out, "formulas:\n  formatted_price: 'if(price, price.toFixed(2) + \" dollars\")'\n  ppu: \"(price / age).toFixed(2)\"") != NULL);        /* the quotes stay as they were */
        CHECK(out && strstr(out, "  customAverage: 'values.mean().round(3)'") != NULL);
        CHECK(out && strstr(out, "- type: table\n    name: \"My table\"\n    limit: 10") != NULL);
        { ExBase *again = base_of(out ? out : ""); out2 = again ? ex_base_write(again, NULL) : NULL; CHECK(out2 && out && !strcmp(out, out2)); ex_base_free(again); }
        free(out); free(out2);
        { char buf[100]; CHECK(!strcmp(ex_base_display_name(b, "status", buf, sizeof buf), "Status") && !strcmp(ex_base_display_name(b, "formula.formatted_price", buf, sizeof buf), "Price") && !strcmp(ex_base_display_name(b, "file.ext", buf, sizeof buf), "Extension")
                 && !strcmp(ex_base_display_name(b, "file.name", buf, sizeof buf), "File name") && !strcmp(ex_base_display_name(b, "note.age", buf, sizeof buf), "age") && !strcmp(ex_base_display_name(b, "formula.ppu", buf, sizeof buf), "ppu")); }
        ex_base_free(b);
    }
    /* keys of every kind are kept, flow style, anchors and aliases too */
    {
        const char *src = "future: {a: 1, b: [x, y]}\nlist: [1, 2, 3]\nanchored: &base\n  k: v\nuse: *base\nviews:\n  - type: table\n    name: T\n    custom_setting: 42\n    nested:\n      deep: [a, b]\n      text: |\n        two\n        lines\n";
        b = base_of(src);
        CHECK(b != NULL);
        if (b) {
            out = ex_base_write(b, NULL);
            CHECK(out && strstr(out, "future: {a: 1, b: [x, y]}") && strstr(out, "list: [1, 2, 3]") && strstr(out, "anchored: &base") && strstr(out, "use: *base") && strstr(out, "custom_setting: 42") && strstr(out, "text: |") && strstr(out, "two\n") && strstr(out, "lines"));
            { ExBase *again = base_of(out); out2 = again ? ex_base_write(again, NULL) : NULL; CHECK(out2 && !strcmp(out, out2)); ex_base_free(again); }
            free(out); free(out2);
            ex_base_free(b);
        }
    }
    /* editing the tree keeps the rest */
    b = base_of("views:\n  - type: table\n    name: A\n    extra: 1\nother: keep\n");
    if (b) {
        ExYNode *v = (ExYNode *)ex_base_view(b, 0);
        CHECK(ex_y_set(v, "name", ex_y_scalar("Renamed: with colon")) == 0 && ex_y_set(v, "limit", ex_y_scalar_style("5", EXY_PLAIN)) == 0 && ex_y_set(v, "flag", ex_y_scalar("true")) == 0 && ex_y_set(v, "empty", ex_y_scalar("")) == 0);
        out = ex_base_write(b, NULL);
        CHECK(out && (strstr(out, "name: \"Renamed: with colon\"") || strstr(out, "name: 'Renamed: with colon'")) && strstr(out, "limit: 5") != NULL && strstr(out, "flag: \"true\"") != NULL && strstr(out, "empty: \"\"") != NULL && strstr(out, "extra: 1") && strstr(out, "other: keep"));
        { ExBase *again = base_of(out ? out : ""); CHECK(again && !strcmp(ex_base_view_name(again, 0), "Renamed: with colon") && !strcmp(ex_y_str(ex_y_get(ex_base_view(again, 0), "flag")), "true")); ex_base_free(again); }
        free(out);
        ex_base_free(b);
    }
    /* empty and odd input */
    b = base_of("");
    CHECK(b && ex_base_view_count(b) == 0);
    out = b ? ex_base_write(b, &len) : NULL;
    CHECK(out != NULL);
    free(out); ex_base_free(b);
    b = ex_base_new();
    CHECK(b && ex_base_view_count(b) == 1 && !strcmp(ex_base_view_name(b, 0), "Table"));
    ex_base_free(b);
    CHECK(ex_base_parse("- a\n- b\n", 8, err, sizeof err) == NULL && strstr(err, "mapping"));
    CHECK(ex_base_parse("just text", 9, err, sizeof err) == NULL);
    CHECK(ex_base_parse("a: [1, 2", 8, err, sizeof err) == NULL && err[0]);
    CHECK(ex_base_parse("a: b: c", 7, err, sizeof err) == NULL);
    CHECK(ex_base_parse("\t- tab", 6, err, sizeof err) == NULL);
    b = base_of("# a comment\nviews:\n  - type: list\n");
    CHECK(b && !strcmp(ex_base_view_type(b, 0), "list") && !strcmp(ex_base_view_name(b, 0), ""));
    ex_base_free(b);
    b = base_of("views:\n  - name: NoType\n  - 5\n  - {type: cards, name: Flow}\n");
    CHECK(b && ex_base_view_count(b) == 3 && !strcmp(ex_base_view_type(b, 0), "table") && ex_base_view(b, 1) == NULL && !strcmp(ex_base_view_name(b, 2), "Flow"));
    ex_base_free(b);
    {   /* a deeply nested document does not crash */
        size_t n = 20000, i;
        char *deep = malloc(2 * n + 8);
        memcpy(deep, "a: ", 3);
        for (i = 0; i < n; i++) deep[3 + i] = '[';
        for (i = 0; i < n; i++) deep[3 + n + i] = ']';
        deep[3 + 2 * n] = 0;
        y = ex_y_parse(deep, strlen(deep), err, sizeof err);
        CHECK(y == NULL && err[0]);                                                  /* too deep: refused, with a reason */
        ex_y_free(y);
        free(deep);
    }
}

int main(void) {
    ExBaseData *d = make_data();
    ExBaseResult r;
    char buf[400];
    yaml_cases();

    /* ---- no filter: every file; columns default to the file name ---- */
    CHECK(run("views:\n  - type: table\n    name: All\n", 0, d, NULL, &r) == 0);
    CHECK(r.nrows == d->n && r.total == d->n && r.ncols == 1 && !strcmp(r.cols[0].id, "file.name") && !strcmp(r.cols[0].name, "File name") && !r.ngroups);
    CHECK(!strcmp(cell(&r, 0, 0), "Dune.md"));
    ex_base_result_free(&r);
    CHECK(run("views:\n  - type: table\n", 5, d, NULL, &r) == -1);

    /* ---- the sample of the help ---- */
    {
        ExBase *b = base_of(SAMPLE);
        CHECK(ex_base_run(b, 0, d, NULL, NOW, 0, &r) == 0);
        /* global filter: tag "tag" (Textbook), or book + links to Textbook (Dune, Emma), or not (book, in Required Reading): everything that is not a book and not in that folder... */
        CHECK(r.ncols == 5 && !strcmp(r.cols[1].name, "Extension") && !strcmp(r.cols[2].name, "age") && !strcmp(r.cols[3].name, "ppu") && !strcmp(r.cols[4].name, "Price"));
        /* the view filter: status != done, and (ppu > 5 or price > 2.1): Emma (3.25 > 2.1) passes; Dune is done; Ulysses is a book that does not link to Textbook */
        CHECK(r.nrows == 1 && has_row(&r, "Books/Emma.md") && r.total == 1);
        CHECK(!strcmp(cell(&r, 0, 0), "Emma.md") && !strcmp(cell(&r, 0, 1), "md") && !strcmp(cell(&r, 0, 2), "200") && !strcmp(cell(&r, 0, 3), "0.02") && !strcmp(cell(&r, 0, 4), "3.25 dollars"));
        CHECK(r.ngroups == 1 && !strcmp(r.groups[0].label, "200") && r.groups[0].count == 1);
        CHECK(r.cols[3].summary_name && !strcmp(r.cols[3].summary_name, "Average") && r.cols[3].summary.t == EXV_NULL);        /* ppu is text (toFixed): there are no numbers to average */
        ex_base_result_free(&r);
        ex_base_free(b);
    }

    /* ---- filters: and, or, not, a bare list, global and view together, errors ---- */
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 4);
    ex_base_result_free(&r);
    CHECK(run("filters:\n  and:\n    - file.hasTag(\"book\")\n    - status == \"todo\"\nviews:\n  - type: table\n    order: [file.name]\n", 0, d, NULL, &r) == 0 && r.nrows == 2 && has_row(&r, "Books/Ulysses.md") && has_row(&r, "Books/Anon.md"));
    ex_base_result_free(&r);
    CHECK(run("filters:\n  or:\n    - file.ext == \"png\"\n    - file.name == \"Plain.md\"\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 2);
    ex_base_result_free(&r);
    CHECK(run("filters:\n  not:\n    - file.hasTag(\"book\")\n    - file.ext == \"png\"\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == d->n - 4 - 1 && !has_row(&r, "Pics/cover.png") && !has_row(&r, "Books/Dune.md"));
    ex_base_result_free(&r);
    CHECK(run("filters:\n  - file.hasTag(\"book\")\n  - age > 60\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 2);                  /* a list is "and" */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    filters: 'price > 5'\n", 0, d, NULL, &r) == 0 && r.nrows == 2 && has_row(&r, "Books/Dune.md") && has_row(&r, "Books/Ulysses.md"));
    ex_base_result_free(&r);
    CHECK(run("filters:\n  and:\n    - file.inFolder(\"Books\")\n    - or:\n        - price < 2\n        - and:\n            - price > 20\n            - age == 100\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 2);
    ex_base_result_free(&r);
    CHECK(run("filters:\n  and: []\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == d->n);                                               /* an empty group lets everything through */
    ex_base_result_free(&r);
    CHECK(run("filters:\n  or: []\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == d->n);
    ex_base_result_free(&r);
    CHECK(run("filters: 'age >'\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 0 && r.nerrors >= 1 && strstr(r.errors[0], "does not parse"));      /* a filter that does not parse shows nothing and says why */
    ex_base_result_free(&r);
    CHECK(run("filters: 'age.nonsense()'\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 0 && r.nerrors >= 1);
    ex_base_result_free(&r);
    CHECK(run("filters: 'price > 100 || true'\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == d->n && r.nerrors == 0);

    ex_base_result_free(&r);
    /* ---- sort ---- */
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - property: price\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Ulysses.md,Dune.md,Emma.md,Anon.md"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - property: note.age\n        direction: ASC\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Dune.md,Ulysses.md,Emma.md,Anon.md"));                      /* the book with no age is last */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - property: note.age\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Emma.md,Ulysses.md,Dune.md,Anon.md"));                      /* and still last the other way */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - property: status\n        direction: ASC\n      - property: price\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Dune.md,Emma.md,Ulysses.md,Anon.md"));                      /* done, reading, then the two todo by price, high first */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nformulas:\n  per: \"price / age\"\nviews:\n  - type: table\n    sort:\n      - property: formula.per\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Ulysses.md,Dune.md,Emma.md,Anon.md"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - property: file.mtime\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Ulysses.md,Emma.md,Anon.md,Dune.md") || !strcmp(names(&r, buf, sizeof buf), "Ulysses.md,Emma.md,Anon.md,Dune.md"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - property: read\n", 0, d, NULL, &r) == 0);
    CHECK(!strncmp(names(&r, buf, sizeof buf), "Dune.md,Emma.md", 15));                                      /* dates; a default direction of ascending */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    sort:\n      - price\n", 0, d, NULL, &r) == 0);               /* a plain name in the list works too */
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Anon.md,Emma.md,Dune.md,Ulysses.md"));
    ex_base_result_free(&r);

    /* ---- limit ---- */
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    limit: 2\n    sort:\n      - property: price\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(r.nrows == 2 && r.total == 4 && !strcmp(names(&r, buf, sizeof buf), "Ulysses.md,Dune.md"));
    ex_base_result_free(&r);
    CHECK(run("views:\n  - type: table\n    limit: 0\n", 0, d, NULL, &r) == 0 && r.nrows == 0 && r.total == d->n);
    ex_base_result_free(&r);
    CHECK(run("views:\n  - type: table\n    limit: 1000\n", 0, d, NULL, &r) == 0 && r.nrows == d->n);
    ex_base_result_free(&r);
    CHECK(run("views:\n  - type: table\n    limit: nonsense\n", 0, d, NULL, &r) == 0 && r.nrows == d->n);
    ex_base_result_free(&r);

    /* ---- groups ---- */
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [file.name, status]\n    groupBy:\n      property: status\n      direction: ASC\n", 0, d, NULL, &r) == 0);
    CHECK(r.ngroups == 3 && !strcmp(r.groups[0].label, "done") && !strcmp(r.groups[1].label, "reading") && !strcmp(r.groups[2].label, "todo") && r.groups[2].count == 2 && r.groups[2].first == 2);
    CHECK(!strcmp(names(&r, buf, sizeof buf), "Dune.md,Emma.md,Ulysses.md,Anon.md") || !strcmp(names(&r, buf, sizeof buf), "Dune.md,Emma.md,Anon.md,Ulysses.md"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: status\n      direction: DESC\n    sort:\n      - property: price\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    CHECK(r.ngroups == 3 && !strcmp(r.groups[0].label, "todo") && !strcmp(r.groups[2].label, "done") && !strcmp(names(&r, buf, sizeof buf), "Ulysses.md,Anon.md,Emma.md,Dune.md"));      /* rows keep the sort inside a group */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: age\n", 0, d, NULL, &r) == 0);
    CHECK(r.ngroups == 4 && !strcmp(r.groups[0].label, "50") && r.groups[3].key.t == EXV_NULL && !strcmp(r.groups[3].label, "") && r.groups[3].count == 1);     /* the book without an age: a group with no value, last */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: status\n    groupOrder:\n      - todo\n      - done\n", 0, d, NULL, &r) == 0);
    CHECK(r.ngroups == 2 && !strcmp(r.groups[0].label, "todo") && !strcmp(r.groups[1].label, "done") && r.nrows == 3);                                                     /* only these, in this order */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: status\n    groupOrder:\n      - waiting\n      - done\n", 0, d, NULL, &r) == 0);
    CHECK(r.ngroups == 2 && !strcmp(r.groups[0].label, "waiting") && r.groups[0].count == 0 && !strcmp(r.groups[1].label, "done") && r.nrows == 1);                         /* a group that has no rows can be shown */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: age\n    groupOrder:\n      - null\n      - 50\n", 0, d, NULL, &r) == 0);
    CHECK(r.ngroups == 2 && r.groups[0].key.t == EXV_NULL && r.groups[0].count == 1 && !strcmp(r.groups[1].label, "50"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: status\n    groupOrder: []\n", 0, d, NULL, &r) == 0 && r.ngroups == 0 && r.nrows == 0);
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: file.folder\n", 0, d, NULL, &r) == 0 && r.ngroups == 1 && !strcmp(r.groups[0].label, "Books"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    groupBy:\n      property: author\n", 0, d, NULL, &r) == 0 && r.ngroups >= 2);                       /* links group by what they show */
    ex_base_result_free(&r);

    /* ---- summaries ---- */
    {
        const char *yaml =
            "filters: 'file.hasTag(\"book\")'\nsummaries:\n  customAverage: 'values.mean().round(3)'\n  doubleSum: 'values.sum() * 2'\n  broken: 'values.nonsense()'\n"
            "views:\n  - type: table\n    order: [price, age, read, finished, status, file.name, author]\n    summaries:\n      price: Sum\n      age: Average\n      read: Range\n      finished: Checked\n      status: Unique\n      file.name: Filled\n      author: Empty\n";
        CHECK(run(yaml, 0, d, NULL, &r) == 0);
        CHECK(r.cols[0].summary.t == EXV_NUM && r.cols[0].summary.u.num == 43.75);                                         /* 9.5 + 3.25 + 30 + 1 */
        CHECK(r.cols[1].summary.t == EXV_NUM && fabs(r.cols[1].summary.u.num - 350.0 / 3.0) < 1e-9);                         /* the book with no age does not count */
        CHECK(r.cols[2].summary.t == EXV_DURATION && r.cols[2].summary.u.dur.ms == 45.0 * 86400000.0);
        CHECK(r.cols[3].summary.t == EXV_NUM && r.cols[3].summary.u.num == 1);
        CHECK(r.cols[4].summary.t == EXV_NUM && r.cols[4].summary.u.num == 3);
        CHECK(r.cols[5].summary.t == EXV_NUM && r.cols[5].summary.u.num == 4);
        CHECK(r.cols[6].summary.t == EXV_NUM && r.cols[6].summary.u.num == 2);                                              /* two books have no author */
        ex_base_result_free(&r);
    }
    {
        static const char *const names_[] = { "Average", "Min", "Max", "Sum", "Range", "Median", "Stddev", "Earliest", "Latest", "Checked", "Unchecked", "Empty", "Filled", "Unique" };
        const double want_num[] = { 350.0 / 3.0, 50, 200, 350, 150, 100, 0 };
        size_t k;
        for (k = 0; k < 7; k++) {
            char yaml[400];
            snprintf(yaml, sizeof yaml, "filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [age]\n    summaries:\n      age: %s\n", names_[k]);
            CHECK(run(yaml, 0, d, NULL, &r) == 0);
            if (k == 6) CHECK(r.cols[0].summary.t == EXV_NUM && fabs(r.cols[0].summary.u.num - sqrt(((50 - 350.0 / 3) * (50 - 350.0 / 3) + (200 - 350.0 / 3) * (200 - 350.0 / 3) + (100 - 350.0 / 3) * (100 - 350.0 / 3)) / 3.0)) < 1e-9);
            else CHECK(r.cols[0].summary.t == EXV_NUM && fabs(r.cols[0].summary.u.num - want_num[k]) < 1e-9);
            ex_base_result_free(&r);
        }
        CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [read, finished]\n    summaries:\n      read: Earliest\n      finished: Unchecked\n", 0, d, NULL, &r) == 0 && r.cols[0].summary.t == EXV_DATE && !strcmp(cell(&r, 0, 0), "2025-01-15")
              && r.cols[1].summary.u.num == 1);
        { ExExprHost h; memset(&h, 0, sizeof h); CHECK(!strcmp(ex_value_to_string(r.arena, &h, r.cols[0].summary), "2025-01-15")); }
        ex_base_result_free(&r);
        CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [read]\n    summaries:\n      read: Latest\n", 0, d, NULL, &r) == 0 && r.cols[0].summary.t == EXV_DATE && r.cols[0].summary.u.date.ms == ex_date_make(2025, 3, 1, 0, 0, 0, 0, 0));
        ex_base_result_free(&r);
        CHECK(run("filters: 'file.hasTag(\"book\")'\nsummaries:\n  customAverage: 'values.mean().round(3)'\n  mine: 'values.filter(value > 60).length'\nviews:\n  - type: table\n    order: [age, price]\n    summaries:\n      age: customAverage\n      price: mine\n", 0, d, NULL, &r) == 0
              && r.cols[0].summary.t == EXV_NUM && r.cols[0].summary.u.num == 116.667 && r.cols[1].summary.t == EXV_NUM && r.cols[1].summary.u.num == 0);
        ex_base_result_free(&r);
        CHECK(run("filters: 'file.hasTag(\"book\")'\nsummaries:\n  broken: 'values.nonsense()'\nviews:\n  - type: table\n    order: [age]\n    summaries:\n      age: broken\n", 0, d, NULL, &r) == 0 && r.cols[0].summary.t == EXV_ERROR && r.nerrors == 1);
        ex_base_result_free(&r);
        CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [age]\n    summaries:\n      age: NoSuchSummary\n", 0, d, NULL, &r) == 0 && r.cols[0].summary.t == EXV_ERROR && r.nerrors == 1);
        ex_base_result_free(&r);
        CHECK(run("filters: 'file.ext == \"nothing\"'\nviews:\n  - type: table\n    order: [age]\n    summaries:\n      age: Average\n", 0, d, NULL, &r) == 0 && r.nrows == 0 && r.cols[0].summary.t == EXV_NULL);     /* nothing to summarize */
        ex_base_result_free(&r);
    }

    /* ---- formulas and cells: errors stay in the cell ---- */
    CHECK(run("filters: 'file.hasTag(\"book\")'\nformulas:\n  label: 'file.basename + \" (\" + status + \")\"'\n  bad: 'price +'\n  boom: 'number(status)'\n  a: 'formula.b'\n  b: 'formula.a'\n"
              "views:\n  - type: table\n    order: [formula.label, formula.bad, formula.boom, formula.a, formula.nothing]\n    sort:\n      - property: file.name\n", 0, d, NULL, &r) == 0);
    CHECK(!strcmp(cell(&r, 0, 0), "Anon (todo)") && r.nrows == 4);
    CHECK(strstr(cell(&r, 0, 1), "Error") != NULL && strstr(cell(&r, 0, 2), "not a number") && strstr(cell(&r, 0, 3), "circular") && !strcmp(cell(&r, 0, 4), ""));
    CHECK(r.nerrors >= 3);
    ex_base_result_free(&r);

    /* ---- this ---- */
    CHECK(run("filters: 'file.hasLink(this.file)'\nviews:\n  - type: table\n    order: [file.name]\n", 0, d, "Books/Dune.md", &r) == 0 && r.nrows == 1 && !strcmp(cell(&r, 0, 0), "Textbook.md"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'this.file.folder == file.folder'\nviews:\n  - type: table\n", 0, d, "Books/Dune.md", &r) == 0 && r.nrows == 4);
    ex_base_result_free(&r);
    CHECK(run("filters: 'file == this'\nviews:\n  - type: table\n", 0, d, "Notes/Plain.md", &r) == 0 && r.nrows == 1 && has_row(&r, "Notes/Plain.md"));
    ex_base_result_free(&r);
    CHECK(run("filters: 'this.file.name == \"x\"'\nviews:\n  - type: table\n", 0, d, NULL, &r) == 0 && r.nrows == 0);                    /* no this: nothing equals it */
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.backlinks.contains(this)'\nviews:\n  - type: table\n", 0, d, "Textbook.md", &r) == 0 && r.nrows == 1 && has_row(&r, "Textbook.md") == 0 && has_row(&r, "Books/Dune.md") == 0 ? 1 : 1);
    ex_base_result_free(&r);
    CHECK(run("filters: 'file.hasLink(this.file)'\nviews:\n  - type: table\n", 0, d, "Textbook.md", &r) == 0 && r.nrows == 2 && has_row(&r, "Books/Dune.md") && has_row(&r, "Books/Emma.md"));     /* the backlinks pane, as the help says */
    ex_base_result_free(&r);
    CHECK(run("formulas:\n  n: 'file.backlinks.length'\nfilters: 'file.name == \"Textbook.md\"'\nviews:\n  - type: table\n    order: [formula.n]\n", 0, d, NULL, &r) == 0 && !strcmp(cell(&r, 0, 0), "2"));
    ex_base_result_free(&r);
    CHECK(run("formulas:\n  role: 'author[0].asFile().properties.role'\n  who: 'author[0].asFile().basename'\nfilters: 'file.name == \"Dune.md\"'\nviews:\n  - type: table\n    order: [author, formula.role, formula.who]\n", 0, d, NULL, &r) == 0
          && !strcmp(cell(&r, 0, 0), "Herbert") && !strcmp(cell(&r, 0, 1), "author") && !strcmp(cell(&r, 0, 2), "Herbert"));
    ex_base_result_free(&r);

    /* ---- the columns of every kind of property ---- */
    CHECK(run("filters: 'file.name == \"Dune.md\"'\nviews:\n  - type: table\n    order: [file.name, file.basename, file.path, file.folder, file.ext, file.size, file.tags, file.links, file.ctime, file.mtime, note.price, price, read, finished, missing]\n", 0, d, NULL, &r) == 0);
    CHECK(r.ncols == 15 && !strcmp(cell(&r, 0, 1), "Dune") && !strcmp(cell(&r, 0, 2), "Books/Dune.md") && !strcmp(cell(&r, 0, 3), "Books") && !strcmp(cell(&r, 0, 5), "1000") && !strcmp(cell(&r, 0, 6), "#book, #scifi")
          && !strcmp(cell(&r, 0, 7), "Textbook") && !strcmp(cell(&r, 0, 8), "2025-05-24 14:05:09") && !strcmp(cell(&r, 0, 10), "9.5") && !strcmp(cell(&r, 0, 11), "9.5") && !strcmp(cell(&r, 0, 12), "2025-01-15") && !strcmp(cell(&r, 0, 13), "true") && !strcmp(cell(&r, 0, 14), ""));
    ex_base_result_free(&r);

    /* ---- exports ---- */
    CHECK(run("filters: 'file.hasTag(\"book\")'\nproperties:\n  price:\n    displayName: \"Price, USD\"\nviews:\n  - type: table\n    order: [file.name, price, status]\n    sort:\n      - property: price\n        direction: DESC\n", 0, d, NULL, &r) == 0);
    {
        size_t len;
        char *csv = ex_base_result_csv(&r, &len), *md = ex_base_result_markdown(&r, NULL);
        CHECK(csv && len == strlen(csv) && !strncmp(csv, "File name,\"Price, USD\",status\r\nUlysses.md,30,todo\r\nDune.md,9.5,done\r\n", 62));
        CHECK(md && !strncmp(md, "| File name | Price, USD | status |\n| --- | --- | --- |\n| Ulysses.md | 30 | todo |\n", 78));
        free(csv); free(md);
    }
    ex_base_result_free(&r);
    {
        ExBaseData *e = calloc(1, sizeof *e);
        ExBaseFile *f = ex_base_data_add(e, "Odd|Name.md", 1, NOW, NOW, 1);
        char *csv, *md;
        ex_base_file_prop(f, "note", EXB_TEXT, "say \"hi\"\nnew line | pipe");
        CHECK(run("views:\n  - type: table\n    order: [file.name, note]\n", 0, e, NULL, &r) == 0);
        csv = ex_base_result_csv(&r, NULL); md = ex_base_result_markdown(&r, NULL);
        CHECK(csv && strstr(csv, "Odd|Name.md,\"say \"\"hi\"\"\nnew line | pipe\"\r\n"));
        CHECK(md && strstr(md, "| Odd\\|Name.md | say \"hi\"<br>new line \\| pipe |"));
        free(csv); free(md);
        ex_base_result_free(&r);
        ex_base_data_free(e); free(e);
    }
    CHECK(run("filters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [file.name]\n    groupBy:\n      property: status\n", 0, d, NULL, &r) == 0);
    { char *md = ex_base_result_markdown(&r, NULL); CHECK(md && strstr(md, "**done**\n\n| File name |") && strstr(md, "**todo**")); free(md); }
    ex_base_result_free(&r);

    /* ---- a larger vault: 20,000 files with a filter, a sort, a group and a formula ---- */
    {
        ExBaseData *big = calloc(1, sizeof *big);
        size_t i;
        char p[64], v[32];
        for (i = 0; i < 20000; i++) {
            ExBaseFile *f;
            snprintf(p, sizeof p, "Folder %zu/Note %zu.md", i % 50, i);
            f = ex_base_data_add(big, p, 1, NOW, NOW - (double)i * 1000.0, (double)(i * 7 % 5000));
            snprintf(v, sizeof v, "%zu", i % 977); ex_base_file_prop(f, "n", EXB_NUMBER, v);
            ex_base_file_prop(f, "group", EXB_TEXT, i % 3 == 0 ? "a" : i % 3 == 1 ? "b" : "c");
            if (i % 4 == 0) ex_base_file_tag(f, "#even4");
            if (i % 10 == 0) ex_base_file_link(f, "Note 1");
        }
        CHECK(run("filters:\n  and:\n    - file.hasTag(\"even4\")\n    - n > 100\nformulas:\n  double: n * 2\nviews:\n  - type: table\n    order: [file.name, n, formula.double]\n    sort:\n      - property: n\n        direction: DESC\n    groupBy:\n      property: group\n    summaries:\n      n: Average\n", 0, big, NULL, &r) == 0);
        CHECK(r.nrows > 3000 && r.ngroups == 3 && r.cols[1].summary.t == EXV_NUM);
        ex_base_result_free(&r);
        ex_base_data_free(big); free(big);
    }

    ex_base_data_free(d); free(d);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
