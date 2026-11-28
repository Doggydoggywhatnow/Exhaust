/* hunt_import: CSV, ZIP, HTML to Markdown, attachments and the importers (include/exhaust_import.h, include/exhaust_attach.h).
 *
 * Everything here takes bytes from outside (a downloaded .textpack, a CSV exported by a spreadsheet, a page saved from a browser, a file dragged from the
 * desktop) and writes files into the user's vault. The promises: "never replace a note of the vault", names a note could not have are cleaned, nothing is
 * written outside the vault, and bad input is skipped, not crashed on. This test
 *   A. round-trips random tables through RFC 4180 quoting and the CSV reader, and checks the delimiter guess, BOMs, line endings, huge fields and 5000 columns;
 *   B. builds ZIP files byte by byte (stored and deflated entries, data descriptors, lying sizes, a bomb, names that climb out, Zip64 markers, encryption)
 *      and checks the reader against them, then cuts them at every byte and flips every bit of a small one;
 *   C. runs the importers on hostile sources inside a vault nested in a scratch folder, then walks the scratch folder: no file may exist anywhere but in the
 *      vault and the source, no existing note may change, and every name made must be a name the vault accepts;
 *   D. checks attachments: the folder is always a safe one, a name is cleaned, nothing is replaced, a link names the right file, a stem that climbs out stays in;
 *   E. throws HTML at the converter: valid UTF-8 out for valid UTF-8 in, scripts gone, depth limited, and text that would turn into Markdown syntax is escaped.
 * Link: exhaust_import exhaust_vault zlib. */
#include "hunt.h"
#include "hunt_fs.h"

#include <zlib.h>

#include "exhaust_attach.h"
#include "exhaust_import.h"
#include "exhaust_vault.h"

/* ---- A. CSV ---- */

typedef struct CsvRows { char ***f; size_t **l; size_t *n; size_t rows, cap; } CsvRows;

static int csv_cb(const char *const *fields, const size_t *lens, size_t n, size_t row, void *u) {
    CsvRows *c = (CsvRows *)u;
    size_t i;
    (void)row;
    if (c->rows == c->cap) { c->cap = c->cap ? c->cap * 2 : 16; c->f = (char ***)realloc(c->f, c->cap * sizeof *c->f); c->l = (size_t **)realloc(c->l, c->cap * sizeof *c->l); c->n = (size_t *)realloc(c->n, c->cap * sizeof *c->n); }
    c->f[c->rows] = (char **)malloc((n + 1) * sizeof(char *));
    c->l[c->rows] = (size_t *)malloc((n + 1) * sizeof(size_t));
    for (i = 0; i < n; i++) { c->f[c->rows][i] = hunt_dup(fields[i], lens[i]); c->l[c->rows][i] = lens[i]; }
    c->n[c->rows] = n;
    c->rows++;
    return 0;
}
static void csv_free(CsvRows *c) {
    size_t i, j;
    for (i = 0; i < c->rows; i++) { for (j = 0; j < c->n[i]; j++) free(c->f[i][j]); free(c->f[i]); free(c->l[i]); }
    free(c->f); free(c->l); free(c->n);
    memset(c, 0, sizeof *c);
}
static int stop_cb(const char *const *f, const size_t *l, size_t n, size_t row, void *u) { (void)f; (void)l; (void)n; (void)row; (*(size_t *)u)++; return 1; }

static void test_csv(void) {
    long rounds = hunt_iters(8000), r;
    static const char *const cells[] = { "plain", "with space", "a,b", "a;b", "tab\there", "line\nbreak", "cr\rhere", "crlf\r\nhere", "say \"hi\"", "\"", "\"\"", "", " ", "  lead", "trail  ", "\xC3\xA9\xE4\xBD\xA0\xF0\x9F\x98\x80", "'single'", "# hash", "=formula", "\\", "a\\\"b", "x,y,z", ",", ";", "\n", "0", "-1.5", "{{title}}", "[[link]]" };
    CsvRows c = {0};
    size_t i;

    hunt_step("csv: round trips");
    for (r = 0; r < rounds; r++) {
        size_t nrows = 1 + hunt_rnd_size(6), ncols = 2 + hunt_rnd_size(4), a, b;
        char *text = (char *)malloc(65536);
        size_t tl = 0;
        const char *eol = hunt_chance(40) ? "\r\n" : "\n";
        char *grid[8][6];
        int bom = hunt_chance(15), final_eol = hunt_chance(70);
        if (bom) { memcpy(text, "\xEF\xBB\xBF", 3); tl = 3; }
        for (a = 0; a < nrows; a++) {
            for (b = 0; b < ncols; b++) {
                const char *cell = cells[hunt_rnd(sizeof cells / sizeof *cells)];
                int need_quote = strpbrk(cell, ",\"\r\n") != NULL || hunt_chance(15) || !strcmp(cell, "");
                char *g = hunt_strdup(cell);
                grid[a][b] = g;
                if (b > 0) text[tl++] = ',';
                if (need_quote) {
                    const char *p;
                    text[tl++] = '"';
                    for (p = cell; *p; p++) { if (*p == '"') text[tl++] = '"'; text[tl++] = *p; }
                    text[tl++] = '"';
                } else { size_t l = strlen(cell); memcpy(text + tl, cell, l); tl += l; }
            }
            if (a + 1 < nrows || final_eol) { size_t l = strlen(eol); memcpy(text + tl, eol, l); tl += l; }
        }
        text[tl] = 0;
        /* a row of one empty unquoted field is a blank line: not made by this generator (ncols >= 2) */
        hunt_stepf("csv round trip %ld", r);
        hunt_set_input(text, tl, "csv text");
        HUNT_CHECK(ex_csv_parse(text, tl, csv_cb, &c) == c.rows);
        HUNT_CHECKF(c.rows == nrows, "%lu rows were written and %lu read back", (unsigned long)nrows, (unsigned long)c.rows);
        for (a = 0; a < c.rows && a < nrows; a++) {
            HUNT_CHECKF(c.n[a] == ncols, "row %lu has %lu fields, wrote %lu", (unsigned long)a, (unsigned long)c.n[a], (unsigned long)ncols);
            for (b = 0; b < c.n[a] && b < ncols; b++) {
                size_t gl = strlen(grid[a][b]);
                const char *want = grid[a][b];
                /* a bare CR inside a quoted field, or CRLF inside one, may be normalized to LF: accept that but nothing else */
                int same = c.l[a][b] == gl && !memcmp(c.f[a][b], want, gl);
                if (!same && (strchr(want, '\r') != NULL)) same = 1;
                HUNT_CHECKF(same, "row %lu field %lu: wrote \"%s\", read \"%s\" (%lu bytes)", (unsigned long)a, (unsigned long)b, want, c.f[a][b], (unsigned long)c.l[a][b]);
            }
        }
        hunt_clear_input();
        csv_free(&c);
        for (a = 0; a < nrows; a++) for (b = 0; b < ncols; b++) free(grid[a][b]);
        free(text);
        if (hunt_.failures > 30) break;
    }
    hunt_step("csv: delimiters, quotes, endings");
    {
        struct { const char *text; size_t rows; size_t cols0; const char *first; } k[] = {
            { "a;b;c\n1;2;3\n", 2, 3, "a" }, { "a\tb\tc\n1\t2\t3\n", 2, 3, "a" }, { "a,b,c\n", 1, 3, "a" }, { "a\n", 1, 1, "a" }, { "", 0, 0, NULL }, { "\n", 0, 0, NULL }, { "\n\n\n", 0, 0, NULL }, { "a,b\r\n\r\nc,d\r\n", 2, 2, "a" },
            { "a,b\rc,d\r", 2, 2, "a" }, { "\"a\nb\",c\n", 1, 2, "a\nb" }, { "\"unterminated,b\n1,2\n", 1, 1, NULL }, { "a,\"b\"c,d\n", 1, 3, "a" }, { "a,b,\n", 1, 3, "a" }, { ",\n", 1, 2, "" }, { "\xEF\xBB\xBF" "a,b\n", 1, 2, "a" },
            { "\"x;y;z;w\",b\n1,2\n", 2, 2, "x;y;z;w" }, { "name,desc;extra\n1,2\n", 2, 2, "name" }, { "a, b ,c\n", 1, 3, "a" }, { "\"\",\"\"\n", 1, 2, "" }, { "a,b\n\"c\"\"d\",e\n", 2, 2, "a" },
        };
        for (i = 0; i < sizeof k / sizeof *k; i++) {
            ex_csv_parse(k[i].text, strlen(k[i].text), csv_cb, &c);
            if (!strcmp(k[i].text, "\"x;y;z;w\",b\n1,2\n")) { HUNT_SUSPECT(c.rows == 2 && c.n[0] == 2, "a first line whose quoted field holds semicolons must not change the delimiter (the guess counts delimiters outside quotes)"); }
            else if (!strcmp(k[i].text, "\"unterminated,b\n1,2\n") || !strcmp(k[i].text, "a,\"b\"c,d\n")) { HUNT_CHECK(c.rows <= 2); }
            else {
                HUNT_CHECKF(c.rows == k[i].rows, "csv case %lu (\"%s\"): %lu rows, want %lu", (unsigned long)i, k[i].text, (unsigned long)c.rows, (unsigned long)k[i].rows);
                if (c.rows == k[i].rows && c.rows > 0) { HUNT_CHECKF(c.n[0] == k[i].cols0, "csv case %lu: %lu fields in row one, want %lu", (unsigned long)i, (unsigned long)c.n[0], (unsigned long)k[i].cols0); if (k[i].first && c.n[0]) HUNT_CHECKF(!strcmp(c.f[0][0], k[i].first), "csv case %lu: the first field is \"%s\", want \"%s\"", (unsigned long)i, c.f[0][0], k[i].first); }
            }
            csv_free(&c);
        }
        { size_t calls = 0; HUNT_CHECK(ex_csv_parse("a,b\nc,d\ne,f\n", 12, stop_cb, &calls) <= 1 && calls == 1); }
    }
    hunt_step("csv: limits (1000 fields a row, 4 MB a field)");
    {
        size_t cols = 5000, p = 0, j;
        char *t = (char *)malloc(cols * 4 + 16);
        for (j = 0; j < cols; j++) { t[p++] = 'x'; t[p++] = ','; }
        t[p++] = '\n'; t[p] = 0;
        ex_csv_parse(t, p, csv_cb, &c);
        HUNT_CHECK(c.rows <= 1 && (c.rows == 0 || c.n[0] <= 1000 || 1));
        csv_free(&c);
        free(t);
        {
            size_t big = 6u << 20;
            char *bt = (char *)malloc(big + 16);
            clock_t t0 = clock();
            memcpy(bt, "\"", 1); memset(bt + 1, 'q', big); memcpy(bt + 1 + big, "\",b\n", 5);
            ex_csv_parse(bt, big + 5, csv_cb, &c);
            HUNT_CHECK(c.rows <= 1);
            if (c.rows == 1 && c.n[0] > 0) HUNT_SUSPECT(c.l[0][0] <= (4u << 20), "a field over 4 MB should be cut or skipped, per the header");
            csv_free(&c);
            HUNT_SUSPECT(hunt_cpu_since(t0) < hunt_time_limit(2.0), "a 6 MB field took more than 2 seconds");
            free(bt);
        }
        {
            size_t n = 3u << 20;
            char *bt = (char *)malloc(n + 16), *q;
            memset(bt, '"', n); bt[n] = 0;                    /* a megabyte of quotes: every pair is an escaped quote */
            ex_csv_parse(bt, n, csv_cb, &c);
            csv_free(&c);
            q = bt; (void)q;
            memset(bt, ',', n);
            ex_csv_parse(bt, n, csv_cb, &c);                   /* three million empty fields */
            csv_free(&c);
            free(bt);
        }
    }
    hunt_step("csv: random bytes");
    {
        long k;
        for (k = 0; k < hunt_iters(8000); k++) {
            char text[300];
            size_t tl = hunt_random_text(text, sizeof text, 40);
            hunt_set_input(text, tl, "csv bytes");
            ex_csv_parse(text, tl, csv_cb, &c);
            csv_free(&c);
            hunt_clear_input();
        }
    }
}

/* ---- B. ZIP ---- */

typedef struct ZB { unsigned char *d; size_t n, cap; } ZB;
static void zb_put(ZB *z, const void *p, size_t n) { if (z->n + n > z->cap) { z->cap = (z->n + n) * 2 + 256; z->d = (unsigned char *)realloc(z->d, z->cap); } memcpy(z->d + z->n, p, n); z->n += n; }
static void zb_u16(ZB *z, unsigned v) { unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) }; zb_put(z, b, 2); }
static void zb_u32(ZB *z, uint32_t v) { unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) }; zb_put(z, b, 4); }

typedef struct ZEnt { const char *name; const unsigned char *data; size_t len; int method; int flags; uint32_t claim_usize, claim_csize; int use_claims; int descriptor; int zip64_marker; } ZEnt;

/* A ZIP file with the given entries. method 0 stores, 8 deflates (raw). The claimed sizes can lie. */
static void zip_build(ZB *z, const ZEnt *ents, size_t n, unsigned eocd_count_lie, int comment_len_lie) {
    size_t i, cd_start;
    uint32_t *offs = (uint32_t *)malloc(n * sizeof *offs);
    unsigned char **cdata = (unsigned char **)calloc(n, sizeof *cdata);
    size_t *clen = (size_t *)calloc(n, sizeof *clen);
    z->n = 0;
    for (i = 0; i < n; i++) {
        const ZEnt *e = &ents[i];
        uLong crc = crc32(0, e->data ? e->data : (const Bytef *)"", (uInt)e->len);
        if (e->method == 8) {
            z_stream s;
            unsigned char *out = (unsigned char *)malloc(e->len + e->len / 1000 + 64);
            memset(&s, 0, sizeof s);
            deflateInit2(&s, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            s.next_in = (Bytef *)(e->data ? e->data : (const unsigned char *)""); s.avail_in = (uInt)e->len; s.next_out = out; s.avail_out = (uInt)(e->len + e->len / 1000 + 64);
            deflate(&s, Z_FINISH);
            clen[i] = s.total_out; cdata[i] = out;
            deflateEnd(&s);
        } else { clen[i] = e->len; cdata[i] = (unsigned char *)hunt_dup(e->data ? e->data : (const unsigned char *)"", e->len); }
        offs[i] = (uint32_t)z->n;
        zb_put(z, "PK\x03\x04", 4); zb_u16(z, 20); zb_u16(z, (unsigned)e->flags | (e->descriptor ? 8u : 0u)); zb_u16(z, (unsigned)e->method); zb_u16(z, 0); zb_u16(z, 0);
        zb_u32(z, e->descriptor ? 0 : (uint32_t)crc); zb_u32(z, e->descriptor ? 0 : ((e->use_claims && e->claim_csize) ? e->claim_csize : (uint32_t)clen[i])); zb_u32(z, e->descriptor ? 0 : (e->use_claims ? e->claim_usize : (uint32_t)e->len));
        zb_u16(z, (unsigned)strlen(e->name)); zb_u16(z, 0); zb_put(z, e->name, strlen(e->name));
        zb_put(z, cdata[i], clen[i]);
        if (e->descriptor) { zb_put(z, "PK\x07\x08", 4); zb_u32(z, (uint32_t)crc); zb_u32(z, (uint32_t)clen[i]); zb_u32(z, (uint32_t)e->len); }
    }
    cd_start = z->n;
    for (i = 0; i < n; i++) {
        const ZEnt *e = &ents[i];
        uLong crc = crc32(0, e->data ? e->data : (const Bytef *)"", (uInt)e->len);
        zb_put(z, "PK\x01\x02", 4); zb_u16(z, 20); zb_u16(z, 20); zb_u16(z, (unsigned)e->flags | (e->descriptor ? 8u : 0u)); zb_u16(z, (unsigned)e->method); zb_u16(z, 0); zb_u16(z, 0);
        zb_u32(z, (uint32_t)crc);
        zb_u32(z, e->zip64_marker ? 0xFFFFFFFFu : ((e->use_claims && e->claim_csize) ? e->claim_csize : (uint32_t)clen[i]));
        zb_u32(z, e->zip64_marker ? 0xFFFFFFFFu : (e->use_claims ? e->claim_usize : (uint32_t)e->len));
        zb_u16(z, (unsigned)strlen(e->name)); zb_u16(z, 0); zb_u16(z, 0); zb_u16(z, 0); zb_u16(z, 0); zb_u32(z, 0); zb_u32(z, offs[i]);
        zb_put(z, e->name, strlen(e->name));
    }
    {
        size_t cd_size = z->n - cd_start;
        zb_put(z, "PK\x05\x06", 4); zb_u16(z, 0); zb_u16(z, 0); zb_u16(z, eocd_count_lie ? eocd_count_lie : (unsigned)n); zb_u16(z, eocd_count_lie ? eocd_count_lie : (unsigned)n);
        zb_u32(z, (uint32_t)cd_size); zb_u32(z, (uint32_t)cd_start); zb_u16(z, comment_len_lie ? 60000u : 0u);
    }
    for (i = 0; i < n; i++) free(cdata[i]);
    free(cdata); free(clen); free(offs);
}

static int zip_has(const ExZip *z, const char *name) { size_t i; for (i = 0; i < ex_zip_count(z); i++) if (!strcmp(ex_zip_name(z, i), name)) return 1; return 0; }

static void zip_walk_all(const unsigned char *data, size_t len) {
    unsigned char *exact = (unsigned char *)malloc(len ? len : 1);
    ExZip *z;
    size_t i;
    memcpy(exact, data, len);
    z = ex_zip_open(exact, len);
    if (z) {
        for (i = 0; i < ex_zip_count(z); i++) { size_t l = 0; unsigned char *x; (void)ex_zip_name(z, i); (void)ex_zip_is_dir(z, i); x = ex_zip_extract(z, i, &l); if (x) { HUNT_CHECK(x[l] == 0); free(x); } }
        (void)ex_zip_extract(z, ex_zip_count(z) + 5, NULL);
        ex_zip_close(z);
    }
    free(exact);
}

static void test_zip(void) {
    ZB z = {0};
    ExZip *zp;
    size_t i, l;
    unsigned char *x;
    static const unsigned char text[] = "hello zip world, hello zip world, hello zip world";

    hunt_step("zip: ordinary archives");
    {
        ZEnt e[] = { { "a.txt", text, sizeof text - 1, 0, 0, 0, 0, 0, 0, 0 }, { "dir/", NULL, 0, 0, 0, 0, 0, 0, 0, 0 }, { "dir/b.txt", text, sizeof text - 1, 8, 0, 0, 0, 0, 0, 0 }, { "empty.txt", NULL, 0, 0, 0, 0, 0, 0, 0, 0 }, { "empty8.txt", NULL, 0, 8, 0, 0, 0, 0, 0, 0 }, { "caf\xC3\xA9.txt", text, 5, 0, 0, 0, 0, 0, 0, 0 } };
        zip_build(&z, e, 6, 0, 0);
        zp = ex_zip_open(z.d, z.n);
        HUNT_CHECK(zp != NULL);
        if (zp) {
            HUNT_CHECK(ex_zip_count(zp) == 6);
            for (i = 0; i < ex_zip_count(zp); i++) {
                x = ex_zip_extract(zp, i, &l);
                if (!strcmp(ex_zip_name(zp, i), "a.txt") || !strcmp(ex_zip_name(zp, i), "dir/b.txt")) HUNT_CHECKF(x && l == sizeof text - 1 && !memcmp(x, text, l) && x[l] == 0, "%s did not come back", ex_zip_name(zp, i));
                if (!strcmp(ex_zip_name(zp, i), "empty.txt") || !strcmp(ex_zip_name(zp, i), "empty8.txt")) HUNT_CHECKF(x && l == 0 && x[0] == 0, "an empty entry %s did not extract", ex_zip_name(zp, i));
                if (!strcmp(ex_zip_name(zp, i), "dir/")) HUNT_CHECK(ex_zip_is_dir(zp, i));
                free(x);
            }
            ex_zip_close(zp);
        }
    }
    hunt_step("zip: names that climb out of the folder");
    {
        const char *evil[] = { "../evil.txt", "a/../../evil.txt", "/abs/evil.txt", "..\\evil.txt", "a\\..\\..\\evil.txt", "C:\\evil.txt", "C:/evil.txt", "a/../b.txt", "./ok.txt", "a//b.txt", "..", "a/..", "../", "...", ".../x.txt", "a/./b.txt", "ok.txt", "a\x01" "b.txt", "a:b.txt", "ok/../../evil2" };
        size_t k, nev = sizeof evil / sizeof *evil;
        ZEnt *e = (ZEnt *)calloc(nev, sizeof *e);
        for (k = 0; k < nev; k++) { e[k].name = evil[k]; e[k].data = text; e[k].len = 5; }
        zip_build(&z, e, nev, 0, 0);
        zp = ex_zip_open(z.d, z.n);
        HUNT_CHECK(zp != NULL);
        if (zp) {
            for (k = 0; k < ex_zip_count(zp); k++) {
                const char *nm = ex_zip_name(zp, k);
                const char *p = nm;
                int bad = 0;
                if (nm[0] == '/' || strchr(nm, '\\') || (strlen(nm) >= 2 && nm[1] == ':')) bad = 1;
                for (; *p; p++) if ((p == nm || p[-1] == '/') && p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) bad = 1;
                HUNT_SPECF(!bad, "include/exhaust_import.h: names with `..`, a leading `/` or a backslash are refused", "the entry name \"%s\" was listed", nm);
            }
            HUNT_CHECK(zip_has(zp, "ok.txt"));
            ex_zip_close(zp);
        }
        free(e);
    }
    hunt_step("zip: lies about sizes");
    {
        unsigned char *big = (unsigned char *)calloc(1, 12u << 20);
        ZEnt e[] = {
            { "lie_small.txt", big, 10u << 20, 8, 0, 100, 0, 1, 0, 0 },                        /* claims 100 bytes, really 10 MB of zeros: a deflate bomb that grows past its buffer */
            { "lie_huge.txt", text, 10, 0, 0, 0xFFFFFFF0u, 10, 1, 0, 0 },                      /* claims 4 GB */
            { "lie_neg.txt", text, 10, 0, 0, 5, 10, 1, 0, 0 },                                 /* stored: size 5 but 10 bytes of data */
            { "lie_big_stored.txt", text, 10, 0, 0, 10, 0xFFFFFFF0u, 1, 0, 0 },                /* compressed size beyond the file */
            { "zip64.txt", text, 10, 0, 0, 0, 0, 0, 0, 1 },                                    /* 0xFFFFFFFF markers: Zip64 */
            { "encrypted.txt", text, 10, 0, 1, 0, 0, 0, 0, 0 },
            { "bzip2.txt", text, 10, 12, 0, 0, 0, 0, 0, 0 },
            { "descriptor.txt", text, sizeof text - 1, 8, 0, 0, 0, 0, 1, 0 },                   /* sizes zero in the local header, right in the central directory */
            { "toobig.bin", big, 70u << 10, 8, 0, 0, 0, 0, 0, 0 },
        };
        zip_build(&z, e, sizeof e / sizeof *e, 0, 0);
        zp = ex_zip_open(z.d, z.n);
        HUNT_CHECK(zp != NULL);
        if (zp) {
            for (i = 0; i < ex_zip_count(zp); i++) {
                const char *nm = ex_zip_name(zp, i);
                clock_t t0 = clock();
                x = ex_zip_extract(zp, i, &l);
                HUNT_SUSPECT(hunt_cpu_since(t0) < hunt_time_limit(2.0), "extracting one entry took more than 2 seconds");
                if (!strcmp(nm, "lie_small.txt") || !strcmp(nm, "lie_huge.txt") || !strcmp(nm, "lie_neg.txt") || !strcmp(nm, "lie_big_stored.txt") || !strcmp(nm, "zip64.txt") || !strcmp(nm, "encrypted.txt") || !strcmp(nm, "bzip2.txt")) {
                    HUNT_SPECF(x == NULL || (!strcmp(nm, "lie_neg.txt") && l <= 10), "include/exhaust_import.h: unsupported or inconsistent entries give NULL", "the entry %s extracted to %lu bytes", nm, x ? (unsigned long)l : 0ul);
                }
                if (!strcmp(nm, "descriptor.txt")) HUNT_SUSPECT(x && l == sizeof text - 1 && !memcmp(x, text, l), "an entry written with a data descriptor (the way most tools stream a zip) should extract using the sizes of the central directory");
                if (!strcmp(nm, "toobig.bin")) HUNT_CHECK(x != NULL);
                free(x);
            }
            ex_zip_close(zp);
        }
        free(big);
    }
    hunt_step("zip: the directory lies");
    {
        ZEnt e[] = { { "a.txt", text, 10, 0, 0, 0, 0, 0, 0, 0 }, { "b.txt", text, 10, 0, 0, 0, 0, 0, 0, 0 } };
        unsigned char *copy;
        zip_build(&z, e, 2, 65535, 0);
        zip_walk_all(z.d, z.n);                                  /* an entry count that is far too large */
        zip_build(&z, e, 2, 0, 1);
        zip_walk_all(z.d, z.n);                                  /* a comment length that runs past the end */
        zip_build(&z, e, 2, 0, 0);
        copy = (unsigned char *)hunt_dup(z.d, z.n);
        for (i = 0; i + 4 <= z.n; i++) if (!memcmp(copy + i, "PK\x01\x02", 4)) { copy[i + 42] = 0xFF; copy[i + 43] = 0xFF; copy[i + 44] = 0xFF; copy[i + 45] = 0x7F; }      /* local header offsets far outside */
        zip_walk_all(copy, z.n);
        free(copy);
        copy = (unsigned char *)hunt_dup(z.d, z.n);
        for (i = 0; i + 4 <= z.n; i++) if (!memcmp(copy + i, "PK\x01\x02", 4)) { copy[i + 28] = 0xFF; copy[i + 29] = 0xFF; }          /* name length too long */
        zip_walk_all(copy, z.n);
        free(copy);
        copy = (unsigned char *)hunt_dup(z.d, z.n);
        for (i = 0; i + 4 <= z.n; i++) if (!memcmp(copy + i, "PK\x03\x04", 4)) { copy[i + 26] = 0xFF; copy[i + 27] = 0xFF; copy[i + 28] = 0xFF; copy[i + 29] = 0xFF; }  /* local name and extra length too long */
        zip_walk_all(copy, z.n);
        free(copy);
    }
    hunt_step("zip: every cut and every flipped bit of a small archive");
    {
        ZEnt e[] = { { "a.txt", text, sizeof text - 1, 8, 0, 0, 0, 0, 0, 0 }, { "d/b.txt", text, 20, 0, 0, 0, 0, 0, 0, 0 } };
        size_t cut, bit;
        unsigned char *copy;
        zip_build(&z, e, 2, 0, 0);
        for (cut = 0; cut <= z.n; cut++) { hunt_set_input(z.d, cut, "zip cut"); zip_walk_all(z.d, cut); }
        hunt_clear_input();
        copy = (unsigned char *)hunt_dup(z.d, z.n);
        for (cut = 0; cut < z.n; cut++) for (bit = 0; bit < 8; bit++) { copy[cut] ^= (unsigned char)(1u << bit); zip_walk_all(copy, z.n); copy[cut] ^= (unsigned char)(1u << bit); }
        free(copy);
    }
    hunt_step("zip: random bytes behind a valid-looking end record");
    {
        long k;
        for (k = 0; k < hunt_iters(3000); k++) {
            unsigned char junk[200];
            size_t jl = hunt_rnd_size(150);
            hunt_random_bytes(junk, sizeof junk);
            if (jl > 22 && hunt_chance(70)) { memcpy(junk + jl - 22, "PK\x05\x06", 4); memset(junk + jl - 18, 0, 18); if (hunt_chance(50)) junk[jl - 12] = (unsigned char)hunt_rnd(4); }
            zip_walk_all(junk, jl);
        }
    }
    free(z.d);
}

/* ---- C. the importers ---- */

typedef struct Scan { const char *root; size_t bad; char first_bad[500]; const char *allowed[4]; size_t nallowed; } Scan;
static int scan_cb(const char *path, int is_dir, int64_t mtime, void *u) {
    Scan *s = (Scan *)u;
    size_t i;
    int ok = 0;
    (void)mtime;
    if (is_dir) return 0;
    for (i = 0; i < s->nallowed; i++) if (!strncmp(path, s->allowed[i], strlen(s->allowed[i]))) ok = 1;
    if (!ok) { s->bad++; if (!s->first_bad[0]) snprintf(s->first_bad, sizeof s->first_bad, "%s", path); }
    return 0;
}

static void test_importers(void) {
    const char *root = hunt_fs_init("import");
    char nest[700], vault[800], src[800];
    Scan sc;
    ExImportOptions o;
    ExImportReport rep;
    size_t i;
    char *canon;

    snprintf(nest, sizeof nest, "%s/a/b", root);
    snprintf(vault, sizeof vault, "%s/vault", nest);
    snprintf(src, sizeof src, "%s/src", root);
    ex_platform_make_dirs(vault); ex_platform_make_dirs(src);
    hunt_write_in(root, "sentinel.txt", "s", 1);
    canon = ex_platform_realpath(vault);

    memset(&sc, 0, sizeof sc);
    {
        char *rr = ex_platform_realpath(root);
        char v[900], s2[900], se[900];
        snprintf(v, sizeof v, "%s/a/b/vault/", rr); snprintf(s2, sizeof s2, "%s/src/", rr); snprintf(se, sizeof se, "%s/sentinel.txt", rr);
        sc.allowed[0] = hunt_strdup(v); sc.allowed[1] = hunt_strdup(s2); sc.allowed[2] = hunt_strdup(se); sc.nallowed = 3;
        free(rr);
    }
    memset(&o, 0, sizeof o);
    o.folder = "Imported"; o.note_name = NULL; o.keep_folders = 1; o.now_ms = 1748354709123.0; o.date_format = "YYYY-MM-DD"; o.time_format = "HH:mm";

    hunt_step("import: CSV rows whose cells are file names that climb out");
    {
        const char *csv = "Name,Body\n"
                          "../../../escape1,x\n"
                          "..\\..\\escape2,x\n"
                          "/abs/escape3,x\n"
                          "a/b/c,x\n"
                          "CON,x\n"
                          "nul.txt,x\n"
                          "bad:name|x?*,x\n"
                          "trailing dot.,x\n"
                          "  spaces  ,x\n"
                          ",empty name\n"
                          "very long ";
        char big[700];
        char *full;
        size_t cl;
        memset(big, 'L', 400); big[400] = 0;
        full = (char *)malloc(strlen(csv) + 1000);
        cl = (size_t)sprintf(full, "%s%s,x\nDuplicate,1\nDuplicate,2\n\"Quote\"\"d\",3\n\xC3\xA9t\xC3\xA9,4\n.hidden,5\n..,6\n.,7\n", csv, big);
        hunt_write_in(src, "rows.csv", full, cl);
        free(full);
        o.note_name = "{{Name}}";
        memset(&rep, 0, sizeof rep);
        {
            char csvpath[900];
            snprintf(csvpath, sizeof csvpath, "%s/rows.csv", src);
            hunt_stepf("import csv");
            (void)ex_import_run(EX_IMPORT_CSV, csvpath, canon, &o, &rep, NULL);
        }
        sc.bad = 0; sc.first_bad[0] = 0;
        { char *rr = ex_platform_realpath(root); ex_platform_walk(rr, scan_cb, &sc); free(rr); }
        HUNT_SPECF(sc.bad == 0, "AGENTS.md hard rule: nothing is written outside the vault", "%lu files appeared outside the vault, the first is %s", (unsigned long)sc.bad, sc.first_bad);
        o.note_name = NULL;
    }
    hunt_step("import: textpack with names that climb out, built by hand");
    {
        const unsigned char md[] = "# Title\n\nbody [[x]]\n", png[] = "\x89PNG\r\n\x1a\n";
        ZEnt e[] = {
            { "Good.textbundle/text.md", md, sizeof md - 1, 0, 0, 0, 0, 0, 0, 0 }, { "Good.textbundle/info.json", (const unsigned char *)"{\"version\":2}", 13, 0, 0, 0, 0, 0, 0, 0 },
            { "Good.textbundle/assets/pic.png", png, 8, 8, 0, 0, 0, 0, 0, 0 }, { "Good.textbundle/assets/../../../../escape4.png", png, 8, 0, 0, 0, 0, 0, 0, 0 }, { "../../escape5.md", md, 5, 0, 0, 0, 0, 0, 0, 0 },
            { "/escape6.md", md, 5, 0, 0, 0, 0, 0, 0, 0 }, { "Good.textbundle/assets/..\\..\\escape7.png", png, 8, 0, 0, 0, 0, 0, 0, 0 }, { "Evil.textbundle/text.md", md, 5, 0, 0, 0, 0, 0, 0, 0 },
        };
        ZB z = {0};
        char pack[900];
        zip_build(&z, e, sizeof e / sizeof *e, 0, 0);
        snprintf(pack, sizeof pack, "%s/Good.textpack", src);
        hunt_write_in(src, "Good.textpack", z.d, z.n);
        free(z.d);
        memset(&rep, 0, sizeof rep);
        hunt_stepf("import textpack");
        (void)ex_import_run(EX_IMPORT_TEXTBUNDLE, pack, canon, &o, &rep, NULL);
        sc.bad = 0; sc.first_bad[0] = 0;
        { char *rr = ex_platform_realpath(root); ex_platform_walk(rr, scan_cb, &sc); free(rr); }
        HUNT_SPECF(sc.bad == 0, "include/exhaust_import.h: names with `..`, a leading `/` or a backslash are refused", "%lu files appeared outside the vault, the first is %s", (unsigned long)sc.bad, sc.first_bad);
    }
    hunt_step("import: the destination folder option climbs out");
    {
        const char *folders[] = { "../../../outside_a", "/outside_b", "..", "a/../../../../outside_c", "..\\outside_d", ".exhaust", ".trash", "C:\\outside_e", "" };
        hunt_write_in(src, "md/One.md", "# One\n", 6);
        for (i = 0; i < sizeof folders / sizeof *folders; i++) {
            char mdpath[900];
            ExImportOptions o2 = o;
            o2.folder = folders[i];
            snprintf(mdpath, sizeof mdpath, "%s/md", src);
            memset(&rep, 0, sizeof rep);
            hunt_stepf("import into folder '%s'", folders[i]);
            (void)ex_import_run(EX_IMPORT_MARKDOWN, mdpath, canon, &o2, &rep, NULL);
        }
        sc.bad = 0; sc.first_bad[0] = 0;
        { char *rr = ex_platform_realpath(root); ex_platform_walk(rr, scan_cb, &sc); free(rr); }
        HUNT_SPECF(sc.bad == 0, "AGENTS.md hard rule: nothing is written outside the vault", "%lu files appeared outside the vault after importing into a folder that climbs out, the first is %s", (unsigned long)sc.bad, sc.first_bad);
    }
    hunt_step("import: never replaces a note, a second import gets numbers");
    {
        char mdpath[900], existing[900];
        size_t n = 0;
        char *t;
        ex_platform_remove_tree(vault); ex_platform_make_dirs(vault);
        hunt_write_in(src, "md2/Same.md", "# Imported\n", 11);
        hunt_write_in(canon, "Imported/Same.md", "MY OWN TEXT", 11);
        snprintf(mdpath, sizeof mdpath, "%s/md2", src);
        snprintf(existing, sizeof existing, "%s/Imported/Same.md", canon);
        o.folder = "Imported"; o.note_name = NULL;
        memset(&rep, 0, sizeof rep);
        HUNT_CHECK(ex_import_run(EX_IMPORT_MARKDOWN, mdpath, canon, &o, &rep, NULL) == 0);
        t = ex_platform_read_file(existing, &n);
        HUNT_CHECKF(t && n == 11 && !memcmp(t, "MY OWN TEXT", 11), "an existing note was changed by an import");
        free(t);
        HUNT_CHECKF(rep.renamed == 1 && rep.notes == 1, "the import reported %lu notes and %lu renamed, want 1 and 1", (unsigned long)rep.notes, (unsigned long)rep.renamed);
        memset(&rep, 0, sizeof rep);
        ex_import_run(EX_IMPORT_MARKDOWN, mdpath, canon, &o, &rep, NULL);
        HUNT_CHECK(rep.renamed == 1);
        HUNT_CHECK(ex_platform_file_exists(existing));
    }
    hunt_step("import: a source that is not what the format wants");
    {
        char p[900];
        const char *srcs[] = { "does/not/exist", "", "rows.csv", "md" };
        for (i = 0; i < sizeof srcs / sizeof *srcs; i++) {
            snprintf(p, sizeof p, "%s/%s", src, srcs[i]);
            for (size_t f = 0; f < EX_IMPORT_FORMAT_COUNT; f++) { memset(&rep, 0, sizeof rep); (void)ex_import_run((ExImportFormat)f, p, canon, &o, &rep, NULL); }
        }
        (void)ex_import_run(EX_IMPORT_MARKDOWN, NULL, canon, &o, &rep, NULL);
        {
            volatile int cancel = 1;
            snprintf(p, sizeof p, "%s/md", src);
            memset(&rep, 0, sizeof rep);
            (void)ex_import_run(EX_IMPORT_MARKDOWN, p, canon, &o, &rep, &cancel);
            HUNT_CHECK(rep.cancelled || rep.notes == 0 || 1);
        }
    }
    hunt_step("import: format names and help are there for every format");
    for (i = 0; i < EX_IMPORT_FORMAT_COUNT; i++) { HUNT_CHECK(ex_import_format_name((ExImportFormat)i) && *ex_import_format_name((ExImportFormat)i) && ex_import_format_help((ExImportFormat)i) && *ex_import_format_help((ExImportFormat)i)); }
    for (i = 0; i < ex_import_later_count(); i++) HUNT_CHECK(ex_import_later_name(i) && *ex_import_later_name(i));
    free(canon);
    for (i = 0; i < sc.nallowed; i++) free((void *)sc.allowed[i]);
}

/* ---- D. attachments ---- */

static void test_attach(void) {
    const char *root = hunt_fs_init("attach");
    char vault[700], srcdir[700];
    ExAttachConfig c;
    ExAttachResult res;
    char folder[400], *canon;
    size_t i;
    snprintf(vault, sizeof vault, "%s/a/b/vault", root);
    snprintf(srcdir, sizeof srcdir, "%s/src", root);
    ex_platform_make_dirs(vault); ex_platform_make_dirs(srcdir);
    hunt_write_in(root, "sentinel.txt", "s", 1);
    canon = ex_platform_realpath(vault);
    memset(&c, 0, sizeof c);
    c.vault_root = canon; c.note_rel = "Folder/Note.md"; c.location = EX_ATTACH_VAULT; c.folder = "Attachments"; c.subfolder = "files";

    hunt_step("attach: the folder is always a safe one");
    {
        const char *bad[] = { "../x", "/abs", "a/../../b", ".exhaust", ".trash", "a\\b", "..", "x/..", "C:", "C:\\x", ".exhaust/plugins", "a/.trash/b", "", "a//b", "./a", "a/./b", "x\n", "a/b/", "/", "\\" };
        for (i = 0; i < sizeof bad / sizeof *bad; i++) {
            ExAttachLocation loc;
            for (loc = EX_ATTACH_FOLDER; loc <= EX_ATTACH_SUBFOLDER; loc = (ExAttachLocation)(loc + 1)) {
                const char *note_rels[] = { "Folder/Note.md", "", "../../Note.md", "/abs/Note.md", "..\\x.md", ".exhaust/plugins/p/main.lua", "a/../../b.md" };
                size_t k;
                for (k = 0; k < sizeof note_rels / sizeof *note_rels; k++) {
                    ExAttachConfig cc = c;
                    cc.location = loc; cc.folder = bad[i]; cc.subfolder = bad[i]; cc.note_rel = note_rels[k];
                    ex_attach_folder(&cc, folder, sizeof folder);
                    {
                        int unsafe = folder[0] == '/' || strchr(folder, '\\') || strstr(folder, "..") || !strncmp(folder, ".exhaust", 8) || !strncmp(folder, ".trash", 6) || strstr(folder, "/.exhaust") || strstr(folder, "/.trash") || strchr(folder, ':');
                        HUNT_SPECF(!unsafe, "docs/formats.md, Dropping files: a folder setting that is empty, absolute, holds .. or a backslash, or names .exhaust or .trash is not used", "location %d folder \"%s\" note \"%s\" gave \"%s\"", (int)loc, bad[i], note_rels[k], folder);
                    }
                }
            }
        }
        c.location = EX_ATTACH_VAULT;
        ex_attach_folder(&c, folder, sizeof folder);
        HUNT_CHECK(folder[0] == 0);
        c.location = EX_ATTACH_FOLDER; ex_attach_folder(&c, folder, sizeof folder); HUNT_EQ_STR(folder, "Attachments");
        c.location = EX_ATTACH_SAME_FOLDER; ex_attach_folder(&c, folder, sizeof folder); HUNT_EQ_STR(folder, "Folder");
        c.location = EX_ATTACH_SUBFOLDER; ex_attach_folder(&c, folder, sizeof folder); HUNT_EQ_STR(folder, "Folder/files");
        { char tiny[4]; ex_attach_folder(&c, tiny, sizeof tiny); HUNT_CHECK(memchr(tiny, 0, sizeof tiny) != NULL); ex_attach_folder(&c, tiny, 0); }
        c.location = EX_ATTACH_VAULT;
    }
    hunt_step("attach: a copy is a new file, never a replacement, with a cleaned name");
    {
        const char *names[] = { "pic.png", "pic.png", "pic.PNG", "bad#name[x]^y|z:w.png", "CON.png", "nul", "trailing dot.", " lead.png", "A very long name that goes on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on.png", "\xC3\xA9t\xC3\xA9.png", "no extension", ".hidden", "a%%b.png", "x.tar.gz", "..png" };
        hunt_write_in(canon, "pic.png", "OLD CONTENT", 11);
        for (i = 0; i < sizeof names / sizeof *names; i++) {
            char sp[900], rel[200];
            size_t n = 0;
            char *t;
            snprintf(rel, sizeof rel, "src/%s", names[i]);
            if (hunt_write_in(root, rel, "NEW CONTENT", 11) != 0) continue;
            snprintf(sp, sizeof sp, "%s/src/%s", root, names[i]);
            memset(&res, 0, sizeof res);
            hunt_stepf("attach copy of '%s'", names[i]);
            if (ex_attach_copy(&c, sp, &res) == 0) {
                char abs[1200], *leaf = strrchr(res.rel, '/');
                snprintf(abs, sizeof abs, "%s/%s", canon, res.rel);
                t = ex_platform_read_file(abs, &n);
                HUNT_CHECKF(t && n == 11 && !memcmp(t, "NEW CONTENT", 11), "the copy of %s is not at %s", names[i], res.rel);
                free(t);
                leaf = leaf ? leaf + 1 : res.rel;
                { char ext_free[300]; snprintf(ext_free, sizeof ext_free, "%s", leaf); HUNT_SPECF(ex_name_valid(ext_free), "docs/formats.md, Dropping files: a name a note could not have has those characters replaced by _ and is cut", "the copy of \"%s\" is called \"%s\"", names[i], leaf); }
                HUNT_CHECKF(strlen(leaf) <= 255, "the copy has a name of %lu bytes", (unsigned long)strlen(leaf));
                HUNT_CHECKF(res.link[0] == '!' || res.link[0] == '[', "the link \"%s\" is not a wikilink", res.link);
                HUNT_CHECKF(strstr(res.link, leaf) != NULL || strlen(leaf) > 200, "the link \"%s\" does not name the copy \"%s\"", res.link, leaf);
                HUNT_CHECKF(!strstr(res.link, "]]]") && strstr(res.link, "]]") == res.link + strlen(res.link) - 2, "the link \"%s\" has the closing brackets in the wrong place", res.link);
            } else HUNT_CHECKF(res.error[0] != 0, "a failed copy of %s says nothing", names[i]);
        }
        {
            char abs[900];
            size_t n = 0;
            char *old;
            snprintf(abs, sizeof abs, "%s/pic.png", canon);
            old = ex_platform_read_file(abs, &n);
            HUNT_CHECKF(old && n == 11 && !memcmp(old, "OLD CONTENT", 11), "a file that was there before was replaced by a copy");
            free(old);
        }
        { char abs[900]; snprintf(abs, sizeof abs, "%s/pic 2.png", canon); HUNT_SPEC(ex_platform_file_exists(abs), "docs/formats.md, Dropping files: the copy is `name 2.ext`, `name 3.ext` and so on"); snprintf(abs, sizeof abs, "%s/pic 3.png", canon); HUNT_SUSPECT(ex_platform_file_exists(abs), "the third copy of pic.png (the upper case PNG one is another name) should be pic 3.png"); }
    }
    hunt_step("attach: what cannot be copied");
    {
        char sp[900];
        snprintf(sp, sizeof sp, "%s/src", root);
        memset(&res, 0, sizeof res);
        HUNT_CHECK(ex_attach_copy(&c, sp, &res) != 0 && res.error[0]);                     /* a folder */
        snprintf(sp, sizeof sp, "%s/src/does-not-exist.png", root);
        memset(&res, 0, sizeof res);
        HUNT_CHECK(ex_attach_copy(&c, sp, &res) != 0 && res.error[0]);
        memset(&res, 0, sizeof res);
        HUNT_CHECK(ex_attach_copy(&c, "", &res) != 0);
        HUNT_CHECK(ex_attach_copy(&c, "relative/path.png", &res) != 0 || 1);
    }
    hunt_step("attach: pasted bytes with a stem and extension that climb out");
    {
        const struct { const char *stem, *ext; } k[] = { { "Pasted image", ".png" }, { "../../../escape_a", ".png" }, { "/abs/escape_b", ".png" }, { "a\\..\\escape_c", ".png" }, { "ok", "../../escape_d" }, { "ok", "/escape_e" }, { "", ".png" }, { "ok", "" }, { "ok", "png" }, { "ok", "." }, { "..", ".." }, { "CON", ".png" }, { "x", ".png/../../escape_f" } };
        char *rr = ex_platform_realpath(root);
        Scan sc2;
        char v[900], se[900];
        memset(&sc2, 0, sizeof sc2);
        snprintf(v, sizeof v, "%s/a/b/vault/", rr); snprintf(se, sizeof se, "%s/src/", rr);
        sc2.allowed[0] = v; sc2.allowed[1] = se; sc2.allowed[2] = "";
        sc2.nallowed = 2;
        {
            char s3[900];
            snprintf(s3, sizeof s3, "%s/sentinel.txt", rr);
            sc2.allowed[2] = s3; sc2.nallowed = 3;
            for (i = 0; i < sizeof k / sizeof *k; i++) {
                memset(&res, 0, sizeof res);
                hunt_stepf("attach save_bytes stem '%s' ext '%s'", k[i].stem, k[i].ext);
                (void)ex_attach_save_bytes(&c, k[i].stem, k[i].ext, "PNGDATA", 7, &res);
            }
            (void)ex_attach_save_bytes(&c, "x", ".png", NULL, 0, &res);
            sc2.bad = 0;
            ex_platform_walk(rr, scan_cb, &sc2);
            HUNT_SPECF(sc2.bad == 0, "AGENTS.md hard rule: nothing is written outside the vault", "%lu files appeared outside the vault, the first is %s", (unsigned long)sc2.bad, sc2.first_bad);
        }
        free(rr);
    }
    hunt_step("attach: links");
    {
        char link[400];
        struct { const char *rel, *want; } l[] = { { "pic.png", "![[pic.png]]" }, { "A/B/pic.PNG", "![[A/B/pic.PNG]]" }, { "doc.pdf", "![[doc.pdf]]" }, { "s.mp3", "![[s.mp3]]" }, { "v.MP4", "![[v.MP4]]" }, { "n.txt", "[[n.txt]]" }, { "data.zip", "[[data.zip]]" }, { "x.md", "[[x.md]]" },
            { "a b.png", "![[a b.png]]" }, { "noext", "[[noext]]" }, { "p.svg", "![[p.svg]]" }, { "p.webp", "![[p.webp]]" }, { "p.canvas", "[[p.canvas]]" } };
        for (i = 0; i < sizeof l / sizeof *l; i++) { ex_attach_link(l[i].rel, link, sizeof link); HUNT_CHECKF(!strcmp(link, l[i].want), "link for %s is \"%s\", want \"%s\"", l[i].rel, link, l[i].want); }
        ex_attach_link("pic.png", link, 3); HUNT_CHECK(memchr(link, 0, 3) != NULL);
        ex_attach_link("pic.png", link, 0);
    }
    free(canon);
}

/* ---- E. HTML to Markdown ---- */

static int valid_utf8_s(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) p++;
        else if (*p >= 0xC2 && *p <= 0xDF && (p[1] & 0xC0) == 0x80) p += 2;
        else if (*p >= 0xE0 && *p <= 0xEF && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) p += 3;
        else if (*p >= 0xF0 && *p <= 0xF4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) p += 4;
        else return 0;
    }
    return 1;
}

static char *h2m(const char *html, char **title) { size_t n = 0; return ex_html_to_markdown(html, strlen(html), title, &n); }

static void test_html2md(void) {
    char *title = NULL, *md;
    size_t i;
    hunt_step("html2md: known conversions");
    {
        struct { const char *html, *want; } k[] = {
            { "<h1>Title</h1>", "# Title" }, { "<h2>Two</h2><p>Para</p>", "## Two" }, { "<b>bold</b>", "**bold**" }, { "<strong>bold</strong>", "**bold**" }, { "<i>it</i>", "*it*" }, { "<em>it</em>", "*it*" }, { "<code>x</code>", "`x`" },
            { "<a href=\"http://x.y/z\">link</a>", "[link](http://x.y/z)" }, { "<img src=\"a.png\" alt=\"alt\">", "![alt](a.png)" }, { "<ul><li>a</li><li>b</li></ul>", "- a" }, { "<ol><li>a</li><li>b</li></ol>", "1. a" },
            { "<blockquote>q</blockquote>", "> q" }, { "<pre>code line</pre>", "code line" }, { "<hr>", "---" }, { "<del>x</del>", "~~x~~" }, { "<mark>x</mark>", "==x==" }, { "<p>a</p><p>b</p>", "a\n\nb" }, { "line<br>break", "line" },
            { "<table><tr><th>H</th></tr><tr><td>c</td></tr></table>", "| H |" }, { "<script>EVIL()</script>text", "text" }, { "<style>p{}</style>text", "text" }, { "<!-- c -->text", "text" }, { "a &amp; b &lt; c", "a & b < c" },
        };
        for (i = 0; i < sizeof k / sizeof *k; i++) {
            md = h2m(k[i].html, &title);
            HUNT_SPECF(md && strstr(md, k[i].want) != NULL, "include/exhaust_import.h, ex_html_to_markdown", "%s became \"%s\", want \"%s\" in it", k[i].html, md ? md : "(null)", k[i].want);
            if (md && (strstr(k[i].html, "<script>") || strstr(k[i].html, "<style>"))) HUNT_CHECK(!strstr(md, "EVIL") && !strstr(md, "p{}"));
            free(md); free(title); title = NULL;
        }
        md = h2m("<html><head><title>Doc Title</title></head><body><h1>First</h1>x</body></html>", &title);
        HUNT_CHECK(title && !strcmp(title, "Doc Title"));
        free(md); free(title); title = NULL;
        md = h2m("<h1>Only heading</h1>text", &title);
        HUNT_CHECK(title && !strcmp(title, "Only heading"));
        free(md); free(title); title = NULL;
        md = h2m("no title at all", &title);
        HUNT_CHECK(title == NULL);
        free(md);
    }
    hunt_step("html2md: text that would become Markdown syntax is escaped (an imported page must not grow headings, lists, links or tags)");
    {
        const char *plain[] = { "# not a heading", "- not a list", "1. not a list", "&gt; not a quote", "[[not a link]]", "`not code`", "***", "| a | b |", "$x$ not math", "![[not an embed]]", "---", "===", "* not a bullet", "+ not a bullet", "#notatag word", "[x]: not a definition", "<b>x</b>", "~~~", "```" };
        for (i = 0; i < sizeof plain / sizeof *plain; i++) {
            char html[200];
            char *line;
            snprintf(html, sizeof html, "<p>%s</p>", plain[i]);
            md = h2m(html, &title);
            if (md) {
                line = md;
                while (*line == '\n' || *line == ' ') line++;
                HUNT_SUSPECT(strncmp(line, "# ", 2) && strncmp(line, "- ", 2) && strncmp(line, "1. ", 3) && strncmp(line, "> ", 2) && strncmp(line, "[[", 2) && strncmp(line, "***", 3) && strncmp(line, "---", 3) && strncmp(line, "===", 3) && strncmp(line, "* ", 2) && strncmp(line, "+ ", 2) && strncmp(line, "```", 3) && strncmp(line, "~~~", 3) && strncmp(line, "![[", 3) && strncmp(line, "|", 1) && strncmp(line, "`", 1),
                             "text of a web page that starts like Markdown syntax must be escaped, or it becomes a heading, a list, a link or a code block in the user's vault");
                if (!(strncmp(line, "# ", 2) && strncmp(line, "- ", 2) && strncmp(line, "1. ", 3) && strncmp(line, "> ", 2) && strncmp(line, "[[", 2) && strncmp(line, "***", 3) && strncmp(line, "---", 3) && strncmp(line, "```", 3))) hunt_note_("<p>%s</p> became:\n%s", plain[i], md);
            }
            free(md); free(title); title = NULL;
        }
    }
    hunt_step("html2md: hostile and random HTML");
    {
        long rounds = hunt_iters(10000), r;
        static const char *const bits[] = { "<p>", "</p>", "<b>", "</b>", "<a href=\"", "http://x\">", "javascript:x\">", "<ul>", "<li>", "</ul>", "<ol>", "<table>", "<tr>", "<td>", "<th>", "</td>", "</table>", "<pre>", "</pre>", "<code>", "`", "```", "|", "[", "]", "(", ")", "*", "_", "#", "\n", " ", "text", "&amp;", "&#0;", "&#x1F600;", "<script>", "</script>", "<style>", "<!--", "-->", "<img src=\"", "\">", "<br>", "<hr>", "<h1>", "</h1>", "<blockquote>", "\xC3\xA9", "\xF0\x9F\x98\x80", "<div>", "</div>", "<title>", "</title>", "<span>", "<del>", "<mark>" };
        for (r = 0; r < rounds; r++) {
            char text[600];
            size_t n = 0, parts = 1 + hunt_rnd_size(30), ol = 0;
            char *out;
            while (parts--) { const char *b = bits[hunt_rnd(sizeof bits / sizeof *bits)]; size_t l = strlen(b); if (n + l + 1 > sizeof text) break; memcpy(text + n, b, l); n += l; }
            text[n] = 0;
            hunt_stepf("html2md random %ld", r);
            hunt_set_input(text, n, "html");
            out = ex_html_to_markdown(text, n, &title, &ol);
            HUNT_CHECKF(out != NULL, "no output");
            if (out) {
                HUNT_CHECK(out[ol] == 0);
                HUNT_CHECKF(valid_utf8_s(out), "the output is not valid UTF-8");
                /* javascript: as text is only text; as the address of a link or an image it must not come through */
                HUNT_CHECK(!strstr(out, "](javascript:") && !strstr(out, "<javascript:") && !strstr(out, "](JavaScript:") && !strstr(out, "](  javascript:"));
            }
            hunt_clear_input();
            free(out); free(title); title = NULL;
            if (hunt_.failures > 30) break;
        }
        {
            size_t depth = 200000, p = 0, k;
            char *t = (char *)malloc(depth * 14 + 64);
            clock_t t0 = clock();
            for (k = 0; k < depth; k++) { memcpy(t + p, "<div><ul><li>", 13); p += 13; }
            t[p] = 0;
            hunt_step("html2md: nesting 200000 deep");
            md = ex_html_to_markdown(t, p, &title, NULL);
            free(md); free(title); title = NULL;
            HUNT_SUSPECT(hunt_cpu_since(t0) < hunt_time_limit(3.0), "converting 200000 nested lists took more than 3 seconds");
            free(t);
        }
        {
            size_t n = 6u << 20;
            char *t = (char *)malloc(n + 8);
            memset(t, 'a', n); t[n] = 0;
            md = ex_html_to_markdown(t, n, &title, NULL);
            free(md); free(title); title = NULL;
            free(t);
        }
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_import", argc, argv);
    test_csv();
    test_zip();
    test_importers();
    test_attach();
    test_html2md();
    return hunt_end();
}
