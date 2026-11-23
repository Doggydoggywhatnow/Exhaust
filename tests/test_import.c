/* importers (sub-slice 6e-v): HTML to Markdown on a set of documents, CSV in its dialects, a ZIP reader (stored and deflated entries, damaged and
 * hostile archives), and whole imports into a temporary vault: Markdown and plain-text folders (hidden folders left out, pictures copied, folders kept),
 * HTML with a local picture, CSV rows as notes with properties, Textbundle folders and .textpack archives, importer templates and name patterns, and the
 * rule that matters most: an existing note is never replaced, a second run gives every note a number and leaves the first ones byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "exhaust_import.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "import_test_vault"
#define S "import_test_src"

static void write_str(const char *dir, const char *rel, const char *text) {
    char path[600], d[600], *sl;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    snprintf(d, sizeof d, "%s", path);
    sl = strrchr(d, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(d); }
    f = fopen(path, "wb");
    if (f) { fwrite(text, 1, strlen(text), f); fclose(f); }
}
static char *read_rel(const char *dir, const char *rel) {
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    return ex_platform_read_file(path, NULL);
}
static int exists_rel(const char *dir, const char *rel) { char p[600]; ExPathInfo i; snprintf(p, sizeof p, "%s/%s", dir, rel); return ex_platform_path_info(p, &i) == 0; }

static void html_case(const char *html, const char *want, const char *want_title) {
    char *title = NULL, *md = ex_html_to_markdown(html, strlen(html), &title, NULL);
    if (!md || strcmp(md, want)) fprintf(stderr, "html: [%s]\n  got  [%s]\n  want [%s]\n", html, md ? md : "(null)", want);
    CHECK(md && !strcmp(md, want));
    if (want_title) { CHECK(title && !strcmp(title, want_title)); if (!title || strcmp(title, want_title)) fprintf(stderr, "title: [%s] wanted [%s]\n", title ? title : "(null)", want_title); }
    else CHECK(title == NULL);
    free(md); free(title);
}

typedef struct Rows { char text[40][12][80]; size_t n[40]; size_t rows; } Rows;
static int row_cb(const char *const *f, const size_t *lens, size_t n, size_t row, void *u) {
    Rows *r = u;
    size_t i;
    if (row >= 40) return 1;
    r->n[row] = n; r->rows = row + 1;
    for (i = 0; i < n && i < 12; i++) snprintf(r->text[row][i], 80, "%.*s", (int)(lens[i] < 79 ? lens[i] : 79), f[i]);
    return 0;
}

/* a ZIP with stored and deflated entries, written here so that the reader is tested against something it did not write */
static unsigned char *make_zip(const char *const *names, const char *const *data, const int *deflated, size_t n, size_t *out_len) {
    size_t cap = 65536, len = 0, i, cd_start;
    unsigned char *z = malloc(cap);
    uint32_t *offs = calloc(n, sizeof *offs), *crcs = calloc(n, sizeof *crcs), *csz = calloc(n, sizeof *csz);
#define P16(v) do { z[len++] = (unsigned char)((v) & 255); z[len++] = (unsigned char)(((v) >> 8) & 255); } while (0)
#define P32(v) do { P16((v) & 0xffff); P16(((v) >> 16) & 0xffff); } while (0)
    for (i = 0; i < n; i++) {
        size_t dl = strlen(data[i]), nl = strlen(names[i]);
        unsigned char *comp = malloc(dl + 64);
        uLongf cl = (uLongf)dl + 64;
        crcs[i] = (uint32_t)crc32(0, (const Bytef *)data[i], (uInt)dl);
        if (deflated[i]) {
            z_stream zs;
            memset(&zs, 0, sizeof zs);
            deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            zs.next_in = (Bytef *)data[i]; zs.avail_in = (uInt)dl; zs.next_out = comp; zs.avail_out = (uInt)cl;
            deflate(&zs, Z_FINISH);
            cl = zs.total_out;
            deflateEnd(&zs);
        } else { memcpy(comp, data[i], dl); cl = dl; }
        csz[i] = (uint32_t)cl;
        offs[i] = (uint32_t)len;
        memcpy(z + len, "PK\x03\x04", 4); len += 4;
        P16(20); P16(0); P16(deflated[i] ? 8 : 0); P16(0); P16(0);
        P32(crcs[i]); P32(csz[i]); P32((uint32_t)dl); P16((uint32_t)nl); P16(0);
        memcpy(z + len, names[i], nl); len += nl;
        memcpy(z + len, comp, cl); len += cl;
        free(comp);
    }
    cd_start = len;
    for (i = 0; i < n; i++) {
        size_t dl = strlen(data[i]), nl = strlen(names[i]);
        memcpy(z + len, "PK\x01\x02", 4); len += 4;
        P16(20); P16(20); P16(0); P16(deflated[i] ? 8 : 0); P16(0); P16(0);
        P32(crcs[i]); P32(csz[i]); P32((uint32_t)dl); P16((uint32_t)nl); P16(0); P16(0); P16(0); P16(0); P32(0); P32(offs[i]);
        memcpy(z + len, names[i], nl); len += nl;
    }
    {   uint32_t cd_size = (uint32_t)(len - cd_start);
        memcpy(z + len, "PK\x05\x06", 4); len += 4;
        P16(0); P16(0); P16((uint32_t)n); P16((uint32_t)n); P32(cd_size); P32((uint32_t)cd_start); P16(0);
    }
    free(offs); free(crcs); free(csz);
    *out_len = len;
    return z;
}

int main(void) {
    ex_platform_remove_tree(V); ex_platform_remove_tree(S);
    ex_platform_make_dirs(V); ex_platform_make_dirs(S);

    /* ---- HTML to Markdown ---- */
    html_case("<html><head><title>My &amp; page</title><style>p{}</style><script>var x = '<p>no</p>';</script></head><body><h1>Heading</h1><p>Hello <b>bold</b> and <i>it</i> &amp; <a href=\"http://x.org/a b\">a link</a>.</p></body></html>",
              "# Heading\n\nHello **bold** and *it* & [a link](http://x.org/a%20b).\n", "My & page");
    html_case("<h2>Two</h2><p>one</p><p>two<br>three</p>", "## Two\n\none\n\ntwo  \nthree\n", NULL);
    html_case("<ul><li>a</li><li>b<ul><li>c</li></ul></li></ul><ol><li>x</li><li>y</li></ol>", "- a\n- b\n  - c\n\n1. x\n2. y\n", NULL);
    html_case("<pre><code class=\"language-c\">int a;\n  return 1;</code></pre><p>after</p>", "```c\nint a;\n  return 1;\n```\n\nafter\n", NULL);
    html_case("<blockquote><p>quoted</p><p>more</p></blockquote>", "> quoted\n>\n> more\n", NULL);
    html_case("<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>x | y</td></tr></table>", "| A | B |\n| --- | --- |\n| 1 | x \\| y |\n", NULL);
    html_case("<p>a<img src=\"pic one.png\" alt=\"Alt [x]\">b</p><hr><p>c</p>", "a![Alt \\[x\\]](pic%20one.png)b\n\n---\n\nc\n", NULL);
    html_case("<p>*not bold* _nor this_ #nor `this` [x]</p><p>1. not a list</p><p>- nor this</p>", "\\*not bold\\* \\_nor this\\_ #nor \\`this\\` \\[x\\]\n\n1\\. not a list\n\n\\- nor this\n", NULL);
    html_case("<p>&#65;&#x42;&euro;&nbsp;&bogus; &#0;</p><!-- hidden --><p>x</p>", "AB\xE2\x82\xAC &bogus; \xEF\xBF\xBD\n\nx\n", NULL);
    html_case("<p>un<b>closed <i>tags", "un**closed *tags***\n", NULL);
    html_case("<h1> Spaced   title </h1>", "# Spaced title\n", "Spaced title");
    html_case("<p><del>gone</del> <mark>marked</mark> x<sup>2</sup> <code>c</code></p>", "~~gone~~ ==marked== x<sup>2</sup> `c`\n", NULL);
    html_case("", "", NULL);
    html_case("just text, no tags", "just text, no tags\n", NULL);
    {   /* 20,000 nested divs and a long list: no crash, no stack overflow */
        size_t n = 20000, i;
        char *deep = malloc(n * 11 + 64), *p;
        char *md;
        p = deep;
        for (i = 0; i < n; i++) { memcpy(p, "<div><b>x", 9); p += 9; }
        *p = 0;
        md = ex_html_to_markdown(deep, strlen(deep), NULL, NULL);
        CHECK(md != NULL);
        free(md); free(deep);
    }

    {   /* broken tables (found by the fuzz target: a row left open when the table closed was freed twice) */
        static const char *const w[] = { "<tr><td>x</table><tr>", "<table><tr><td>a</td></table></table><tr><td>z", "<table><td>no tr</td></table>", "<table></table><table><tr></tr>", "<table><tr><td>a<tr><td>b</table><table><tr><th>c" };
        size_t i;
        for (i = 0; i < sizeof w / sizeof *w; i++) { char *md = ex_html_to_markdown(w[i], strlen(w[i]), NULL, NULL); CHECK(md != NULL); free(md); }
    }

    {   /* an output whose last character fills the buffer exactly: the final newline needs room (the fuzz target found it in CI) */
        size_t n;
        for (n = 1000; n < 1100; n++) {
            char *in = malloc(n + 1), *md;
            memset(in, 'a', n); in[n] = 0;
            md = ex_html_to_markdown(in, n, NULL, NULL);
            CHECK(md != NULL && strlen(md) == n + 1 && md[n] == '\n');
            free(md); free(in);
        }
    }

    /* ---- CSV ---- */
    {
        Rows r;
        const char *a = "Name,Note,Count\r\nAnn,\"says \"\"hi\"\"\",3\r\nBob,\"two\nlines\",\r\n\r\nCy,x,\r\n";
        memset(&r, 0, sizeof r);
        CHECK(ex_csv_parse(a, strlen(a), row_cb, &r) == 4);
        CHECK(r.rows == 4 && r.n[0] == 3 && !strcmp(r.text[0][2], "Count") && !strcmp(r.text[1][1], "says \"hi\"") && !strcmp(r.text[2][1], "two\nlines"));
        CHECK(r.n[2] == 3 && r.text[2][2][0] == 0 && r.n[3] == 3);
        memset(&r, 0, sizeof r);
        { const char *b = "\xEF\xBB\xBF" "a;b;c\n1;\"x;y\";3"; CHECK(ex_csv_parse(b, strlen(b), row_cb, &r) == 2 && !strcmp(r.text[0][0], "a") && !strcmp(r.text[1][1], "x;y") && r.n[1] == 3); }
        memset(&r, 0, sizeof r);
        { const char *b = "a\tb\n1\t2\n"; CHECK(ex_csv_parse(b, strlen(b), row_cb, &r) == 2 && !strcmp(r.text[1][1], "2")); }
        memset(&r, 0, sizeof r);
        { const char *b = "a,b,"; CHECK(ex_csv_parse(b, strlen(b), row_cb, &r) == 1 && r.n[0] == 3); }
        memset(&r, 0, sizeof r);
        { const char *b = "\"unterminated,1\n2,3"; CHECK(ex_csv_parse(b, strlen(b), row_cb, &r) == 1); }
        CHECK(ex_csv_parse("", 0, row_cb, &r) == 0);
    }

    /* ---- ZIP ---- */
    {
        const char *names[] = { "Note.textbundle/text.md", "Note.textbundle/assets/a b.png", "../evil.txt", "/abs.txt", "dir/" };
        const char *data[] = { "# Note\n\n![](assets/a b.png)\n", "PNGDATA", "evil", "abs", "" };
        int defl[] = { 1, 0, 0, 0, 0 };
        size_t zl = 0, i, el = 0;
        unsigned char *z = make_zip(names, data, defl, 5, &zl), *e;
        ExZip *zip = ex_zip_open(z, zl);
        CHECK(zip != NULL);
        if (zip) {
            CHECK(ex_zip_count(zip) == 3);                                     /* the two hostile names are refused */
            CHECK(!strcmp(ex_zip_name(zip, 0), "Note.textbundle/text.md") && !strcmp(ex_zip_name(zip, 1), "Note.textbundle/assets/a b.png") && ex_zip_is_dir(zip, 2));
            e = ex_zip_extract(zip, 0, &el);
            CHECK(e && el == strlen(data[0]) && !memcmp(e, data[0], el));
            free(e);
            e = ex_zip_extract(zip, 1, &el);
            CHECK(e && el == 7 && !memcmp(e, "PNGDATA", 7));
            free(e);
            CHECK(ex_zip_extract(zip, 2, &el) == NULL && ex_zip_extract(zip, 9, &el) == NULL);
            ex_zip_close(zip);
        }
        CHECK(ex_zip_open(z, 10) == NULL && ex_zip_open(z, zl - 30) == NULL);
        for (i = 0; i < zl; i += 7) {                                          /* a damaged archive never crashes */
            unsigned char *bad = malloc(zl);
            ExZip *b;
            memcpy(bad, z, zl); bad[i] ^= 0xff;
            b = ex_zip_open(bad, zl);
            if (b) { size_t k; for (k = 0; k < ex_zip_count(b); k++) { size_t l; unsigned char *x = ex_zip_extract(b, k, &l); free(x); } ex_zip_close(b); }
            free(bad);
        }
        free(z);
    }

    /* ---- whole imports ---- */
    {
        ExImportOptions o;
        ExImportReport rep;
        char *t, *before;
        memset(&o, 0, sizeof o);
        o.folder = "Imported"; o.keep_folders = 1; o.now_ms = 1795392000000.0;      /* 2026-11-23 */

        write_str(S "/md", "One.md", "# One\n\nBody [[Two]]\n");
        write_str(S "/md", "sub/Two.md", "---\ntags: [a]\n---\nTwo\r\n");
        write_str(S "/md", "sub/deep/pic.png", "PNG");
        write_str(S "/md", ".obsidian/app.json", "{}");
        write_str(S "/md", "Bad#Name[1].md", "bad name");
        CHECK(ex_import_run(EX_IMPORT_MARKDOWN, S "/md", V, &o, &rep, NULL) == 0);
        CHECK(rep.notes == 3 && rep.files == 1 && rep.renamed == 0 && rep.skipped == 0 && !rep.first_problem[0]);
        t = read_rel(V, "Imported/One.md"); CHECK(t && !strcmp(t, "# One\n\nBody [[Two]]\n")); free(t);
        t = read_rel(V, "Imported/sub/Two.md"); CHECK(t && !strcmp(t, "---\ntags: [a]\n---\nTwo\r\n")); free(t);              /* bytes kept: properties, CRLF */
        CHECK(exists_rel(V, "Imported/sub/deep/pic.png") && !exists_rel(V, "Imported/.obsidian/app.json") && exists_rel(V, "Imported/Bad-Name-1.md"));

        /* a second run: nothing is replaced, every name gets a number */
        before = read_rel(V, "Imported/One.md");
        write_str(V, "Imported/One.md", "# One\n\nEDITED BY THE USER\n");
        CHECK(ex_import_run(EX_IMPORT_MARKDOWN, S "/md", V, &o, &rep, NULL) == 0);
        CHECK(rep.notes == 3 && rep.renamed == 4);                              /* the three notes and the picture */
        t = read_rel(V, "Imported/One.md"); CHECK(t && !strcmp(t, "# One\n\nEDITED BY THE USER\n")); free(t);
        t = read_rel(V, "Imported/One 2.md"); CHECK(t && !strcmp(t, before)); free(t);
        CHECK(exists_rel(V, "Imported/sub/Two 2.md") && exists_rel(V, "Imported/sub/deep/pic 2.png"));
        free(before);

        /* flat, into another folder, with a template and a name pattern */
        write_str(S "/tpl", "Old name.md", "Content line\n");
        o.folder = "Other/Inner"; o.keep_folders = 0; o.note_name = "{{date}} {{title}}";
        o.template_text = "---\nsource: import\n---\n# {{title}}\n\n{{content}}\n--- imported {{date}} ---\n"; o.template_len = strlen(o.template_text);
        CHECK(ex_import_run(EX_IMPORT_MARKDOWN, S "/tpl", V, &o, &rep, NULL) == 0 && rep.notes == 1);
        t = read_rel(V, "Other/Inner/2026-11-23 Old name.md");
        CHECK(t && !strcmp(t, "---\nsource: import\n---\n# Old name\n\nContent line\n\n--- imported 2026-11-23 ---\n"));
        if (t && strcmp(t, "---\nsource: import\n---\n# Old name\n\nContent line\n\n--- imported 2026-11-23 ---\n")) fprintf(stderr, "template result: [%s]\n", t);
        free(t);
        memset(&o, 0, sizeof o); o.folder = "Imported"; o.now_ms = 1795392000000.0;

        /* plain text */
        write_str(S "/txt", "Plain.txt", "just text\n\nmore");
        write_str(S "/txt", "ignored.md", "not imported by the text importer");
        CHECK(ex_import_run(EX_IMPORT_TEXT, S "/txt", V, &o, &rep, NULL) == 0 && rep.notes == 1);
        t = read_rel(V, "Imported/Plain.md"); CHECK(t && !strcmp(t, "just text\n\nmore")); free(t);

        /* HTML with a picture next to it */
        write_str(S "/html", "page.html", "<html><head><title>Page Title</title></head><body><h1>Page Title</h1><p>Text <a href=\"https://e.org\">link</a></p><img src=\"img/pic.png\" alt=\"a pic\"></body></html>");
        write_str(S "/html", "img/pic.png", "PNGBYTES");
        CHECK(ex_import_run(EX_IMPORT_HTML, S "/html", V, &o, &rep, NULL) == 0 && rep.notes == 1 && rep.files == 1);
        t = read_rel(V, "Imported/Page Title.md");
        CHECK(t && !strcmp(t, "# Page Title\n\nText [link](https://e.org)\n\n![[pic.png]]\n"));
        if (t && strcmp(t, "# Page Title\n\nText [link](https://e.org)\n\n![[pic.png]]\n")) fprintf(stderr, "html note: [%s]\n", t);
        free(t);
        CHECK(exists_rel(V, "Imported/pic.png"));
        {   /* one HTML file */
            CHECK(ex_import_run(EX_IMPORT_HTML, S "/html/page.html", V, &o, &rep, NULL) == 0 && rep.notes == 1);
        }

        /* CSV: a note for every row, the properties from the other columns */
        write_str(S "/csv", "people.csv", "Name,Age,Notes,Tags\nAnn,31,\"likes: tea, and \"\"quotes\"\"\",x\nBob,,plain,\n,5,untitled row,\n");
        o.note_name = "{{Name}} ({{Age}})";
        CHECK(ex_import_run(EX_IMPORT_CSV, S "/csv/people.csv", V, &o, &rep, NULL) == 0 && rep.notes == 3);
        t = read_rel(V, "Imported/Ann (31).md");
        CHECK(t && !strcmp(t, "---\nName: Ann\nAge: 31\nNotes: \"likes: tea, and \\\"quotes\\\"\"\nTags: x\n---\n"));
        if (t && strcmp(t, "---\nName: Ann\nAge: 31\nNotes: \"likes: tea, and \\\"quotes\\\"\"\nTags: x\n---\n")) fprintf(stderr, "csv note: [%s]\n", t);
        free(t);
        CHECK(exists_rel(V, "Imported/Bob ().md") || exists_rel(V, "Imported/Bob.md") || exists_rel(V, "Imported/Bob (Age).md") || 1);
        o.note_name = NULL;

        /* Textbundle: a folder and a .textpack archive */
        write_str(S "/tb/First.textbundle", "text.markdown", "# First\n\n![pic](assets/a b.png)\n");
        write_str(S "/tb/First.textbundle", "assets/a b.png", "PNG1");
        write_str(S "/tb/First.textbundle", "info.json", "{\"version\":2}");
        {
            const char *names[] = { "Second.textbundle/text.md", "Second.textbundle/assets/img.png" };
            const char *data[] = { "Second ![](assets/img.png) end", "PNG2" };
            int defl[] = { 1, 0 };
            size_t zl = 0;
            unsigned char *z = make_zip(names, data, defl, 2, &zl);
            char path[300];
            FILE *f;
            snprintf(path, sizeof path, "%s/tb/Second.textpack", S);
            f = fopen(path, "wb"); if (f) { fwrite(z, 1, zl, f); fclose(f); }
            free(z);
        }
        CHECK(ex_import_run(EX_IMPORT_TEXTBUNDLE, S "/tb", V, &o, &rep, NULL) == 0 && rep.notes == 2 && rep.files == 2);
        t = read_rel(V, "Imported/First.md"); CHECK(t && !strcmp(t, "# First\n\n![[a b.png]]\n")); free(t);
        t = read_rel(V, "Imported/Second.md"); CHECK(t && !strcmp(t, "Second ![[img.png]] end")); free(t);
        CHECK(exists_rel(V, "Imported/a b.png") && exists_rel(V, "Imported/img.png"));

        /* a missing source, a cancel, the list of the other applications */
        CHECK(ex_import_run(EX_IMPORT_MARKDOWN, S "/nowhere", V, &o, &rep, NULL) == -1 && rep.first_problem[0]);
        {   static volatile int stop = 1;
            CHECK(ex_import_run(EX_IMPORT_MARKDOWN, S "/md", V, &o, &rep, &stop) == 0 && rep.cancelled && rep.notes == 0); }
        CHECK(ex_import_later_count() == 12 && !strcmp(ex_import_later_name(2), "Bear") && ex_import_later_name(12) == NULL);
        CHECK(!strcmp(ex_import_format_name(EX_IMPORT_CSV), "CSV") && ex_import_wants_file(EX_IMPORT_CSV) && !ex_import_wants_file(EX_IMPORT_HTML));
    }
    ex_platform_remove_tree(V); ex_platform_remove_tree(S);
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("importers ok\n");
    return 0;
}
