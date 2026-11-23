/* The structure check of the PDF writer's own files (sub-slice 6e-iv): the cross-reference table, the objects it lists, the pages, the fonts (embedded,
 * subset, with a ToUnicode map), the text recovered through those maps and the colors of the shapes. It reads what ex_pdfw_finish writes, not any
 * PDF: it is the test's eyes, and the export's own check of its output. */
#include "exhaust_pdfwrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct Obj { const unsigned char *dict; size_t dict_len; const unsigned char *data; size_t data_len; int has_stream; int present; } Obj;

typedef struct Ctx {
    const unsigned char *pdf; size_t len;
    Obj *objs; size_t n;
} Ctx;

static void fail(ExPdfCheck *c, const char *m) { snprintf(c->err, sizeof c->err, "%s", m); c->ok = 0; }

static const unsigned char *find(const unsigned char *s, size_t n, const char *pat) {
    size_t pl = strlen(pat), i;
    if (pl == 0 || n < pl) return NULL;
    for (i = 0; i + pl <= n; i++) if (s[i] == (unsigned char)pat[0] && !memcmp(s + i, pat, pl)) return s + i;
    return NULL;
}
static const unsigned char *rfind(const unsigned char *s, size_t n, const char *pat) {
    size_t pl = strlen(pat), i;
    if (pl == 0 || n < pl) return NULL;
    for (i = n - pl + 1; i-- > 0;) if (s[i] == (unsigned char)pat[0] && !memcmp(s + i, pat, pl)) return s + i;
    return NULL;
}

/* "/Key 12 0 R" in a dictionary: the object number, or 0 */
static size_t ref_of(const unsigned char *d, size_t n, const char *key) {
    const unsigned char *p = find(d, n, key);
    size_t v = 0, k = 0;
    if (!p) return 0;
    p += strlen(key);
    while (p < d + n && *p == ' ') p++;
    while (p < d + n && *p >= '0' && *p <= '9') { v = v * 10 + (size_t)(*p - '0'); p++; k++; }
    return k ? v : 0;
}

static unsigned char *inflate_all(const unsigned char *src, size_t n, size_t *out_len) {
    z_stream zs;
    size_t cap = n * 4 + 1024, used = 0;
    unsigned char *out = malloc(cap);
    int rc;
    if (!out) return NULL;
    memset(&zs, 0, sizeof zs);
    if (inflateInit(&zs) != Z_OK) { free(out); return NULL; }
    zs.next_in = (Bytef *)src; zs.avail_in = (uInt)n;
    for (;;) {
        if (used == cap) { unsigned char *no = realloc(out, cap * 2); if (!no) { free(out); inflateEnd(&zs); return NULL; } out = no; cap *= 2; }
        zs.next_out = out + used; zs.avail_out = (uInt)(cap - used);
        rc = inflate(&zs, Z_NO_FLUSH);
        used = cap - zs.avail_out;
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK && rc != Z_BUF_ERROR) { free(out); inflateEnd(&zs); return NULL; }
        if (rc == Z_BUF_ERROR && zs.avail_in == 0) { free(out); inflateEnd(&zs); return NULL; }
    }
    inflateEnd(&zs);
    *out_len = used;
    return out;
}

static unsigned char *object_stream(const Ctx *x, size_t id, size_t *n) {
    const Obj *o;
    if (id == 0 || id >= x->n || !x->objs[id].present || !x->objs[id].has_stream) return NULL;
    o = &x->objs[id];
    if (find(o->dict, o->dict_len, "/FlateDecode")) return inflate_all(o->data, o->data_len, n);
    {   unsigned char *c = malloc(o->data_len + 1);
        if (c) { memcpy(c, o->data, o->data_len); *n = o->data_len; }
        return c; }
}

typedef struct CMap { uint32_t cp[65536]; } CMap;

static void put8(char **buf, size_t *n, size_t *cap, uint32_t cp) {
    char t[4];
    int k = 0;
    if (cp < 0x80) t[k++] = (char)cp;
    else if (cp < 0x800) { t[k++] = (char)(0xC0 | (cp >> 6)); t[k++] = (char)(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { t[k++] = (char)(0xE0 | (cp >> 12)); t[k++] = (char)(0x80 | ((cp >> 6) & 0x3f)); t[k++] = (char)(0x80 | (cp & 0x3f)); }
    else { t[k++] = (char)(0xF0 | (cp >> 18)); t[k++] = (char)(0x80 | ((cp >> 12) & 0x3f)); t[k++] = (char)(0x80 | ((cp >> 6) & 0x3f)); t[k++] = (char)(0x80 | (cp & 0x3f)); }
    if (*n + 5 > *cap) { size_t nc = *cap ? *cap * 2 : 1024; char *nb = realloc(*buf, nc); if (!nb) return; *buf = nb; *cap = nc; }
    memcpy(*buf + *n, t, (size_t)k); *n += (size_t)k; (*buf)[*n] = 0;
}

static unsigned hexval(unsigned char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16; }

/* parses "<gid> <cp>" pairs of the bfchar sections */
static void parse_cmap(const unsigned char *s, size_t n, CMap *m) {
    size_t i = 0;
    int in = 0;
    while (i < n) {
        if (n - i >= 11 && !memcmp(s + i, "beginbfchar", 11)) { in = 1; i += 11; continue; }
        if (n - i >= 9 && !memcmp(s + i, "endbfchar", 9)) { in = 0; i += 9; continue; }
        if (in && s[i] == '<') {
            uint32_t a = 0, v1 = 0, v2 = 0, b;
            size_t k = i + 1;
            int digits = 0;
            while (k < n && hexval(s[k]) < 16) { a = a * 16 + hexval(s[k]); k++; }
            while (k < n && s[k] != '<') k++;
            k++;
            while (k < n && hexval(s[k]) < 16) {
                if (digits < 4) v1 = v1 * 16 + hexval(s[k]); else v2 = v2 * 16 + hexval(s[k]);
                k++; digits++;
            }
            b = digits == 8 ? 0x10000 + ((v1 - 0xD800) << 10) + (v2 - 0xDC00) : v1;
            if (a < 65536) m->cp[a] = b;
            i = k;
            continue;
        }
        i++;
    }
}

static int parse_num(const unsigned char **p, const unsigned char *end, double *v) {
    char t[40];
    size_t k = 0;
    while (*p < end && (**p == ' ' || **p == '\n')) (*p)++;
    while (*p < end && k < sizeof t - 1 && ((**p >= '0' && **p <= '9') || **p == '.' || **p == '-')) t[k++] = (char)*(*p)++;
    if (k == 0) return 0;
    t[k] = 0;
    *v = atof(t);
    return 1;
}

static void recover_text(const Ctx *x, size_t page_id, ExPdfCheck *c, char **buf, size_t *bn, size_t *bcap) {
    const Obj *pg = &x->objs[page_id];
    size_t content = ref_of(pg->dict, pg->dict_len, "/Contents"), cn = 0, i;
    unsigned char *cs = object_stream(x, content, &cn);
    CMap *maps[64];
    size_t map_font[64], nmaps = 0;
    CMap *curmap = NULL;
    double last_y = 1e9, tx, ty;
    int in_text = 0;
    memset(maps, 0, sizeof maps);
    if (!cs) { fail(c, "a page has no readable content"); return; }
    for (i = 0; i < cn; i++) {
        if (cs[i] == '/' && cs[i + 1] == 'F' && i + 2 < cn) {          /* /F<k> size Tf */
            size_t k = 0, j = i + 2, fid;
            const unsigned char *fp;
            while (j < cn && cs[j] >= '0' && cs[j] <= '9') { k = k * 10 + (size_t)(cs[j] - '0'); j++; }
            fp = find(pg->dict, pg->dict_len, "/Font <<");
            if (fp) {
                char key[24];
                const unsigned char *e = find(fp, pg->dict_len - (size_t)(fp - pg->dict), ">>");
                snprintf(key, sizeof key, "/F%zu ", k);
                fid = e ? ref_of(fp, (size_t)(e - fp), key) : 0;
                if (fid) {
                    size_t m;
                    for (m = 0; m < nmaps; m++) if (map_font[m] == fid) break;
                    if (m == nmaps && nmaps < 64) {
                        const Obj *t0 = &x->objs[fid];
                        size_t tu = ref_of(t0->dict, t0->dict_len, "/ToUnicode"), tn = 0;
                        unsigned char *ts = object_stream(x, tu, &tn);
                        maps[m] = calloc(1, sizeof(CMap));
                        map_font[m] = fid;
                        if (maps[m] && ts) parse_cmap(ts, tn, maps[m]);
                        free(ts);
                        nmaps++;
                    }
                    if (m < nmaps) curmap = maps[m];
                }
            }
            i = j;
        } else if (cs[i] == 'B' && cs[i + 1] == 'T') { in_text = 1; i++; }
        else if (cs[i] == 'E' && cs[i + 1] == 'T') { in_text = 0; i++; }
        else if (in_text && cs[i] == 'T' && cs[i + 1] == 'd') {        /* x y Td: look back for the two numbers */
            size_t j = i;
            const unsigned char *p;
            while (j > 0 && cs[j - 1] != 'f') j--;                   /* after "Tf " */
            p = cs + j;
            if (parse_num(&p, cs + i, &tx) && parse_num(&p, cs + i, &ty)) {
                if (ty < last_y - 0.1 || ty > last_y + 0.1) { if (*bn && (*buf)[*bn - 1] != '\n') put8(buf, bn, bcap, '\n'); last_y = ty; }
            }
            i++;
        } else if (in_text && cs[i] == '<' && curmap) {
            size_t j = i + 1;
            while (j < cn && cs[j] != '>') {
                uint32_t g = 0;
                int d;
                for (d = 0; d < 4 && j < cn && hexval(cs[j]) < 16; d++, j++) g = g * 16 + hexval(cs[j]);
                if (d < 4) break;
                if (curmap->cp[g]) put8(buf, bn, bcap, curmap->cp[g]);
            }
            i = j;
        }
    }
    if (*bn && (*buf)[*bn - 1] != '\n') put8(buf, bn, bcap, '\n');
    { size_t m; for (m = 0; m < nmaps; m++) free(maps[m]); }
    free(cs);
}

static double lum(double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }

static void scan_fills(const unsigned char *cs, size_t n, ExPdfCheck *c) {
    size_t i;
    for (i = 0; i + 2 < n; i++) {
        if (cs[i] == 'r' && cs[i + 1] == 'g' && cs[i + 2] == '\n' && i >= 6) {      /* "r g b rg\n": walk back over three numbers */
            double v[3];
            size_t j = i, k;
            const unsigned char *p;
            const unsigned char *end;
            int ok = 1;
            for (k = 0; k < 3 && ok; k++) {
                size_t e = j;
                while (j > 0 && cs[j - 1] == ' ') j--;
                e = j;
                while (j > 0 && ((cs[j - 1] >= '0' && cs[j - 1] <= '9') || cs[j - 1] == '.' || cs[j - 1] == '-')) j--;
                if (e == j) ok = 0; else { p = cs + j; end = cs + e; if (!parse_num(&p, end, &v[2 - k])) ok = 0; }
            }
            if (ok) {
                const unsigned char *q = cs + i, *stop = cs + n;
                const unsigned char *close = find(q, (size_t)(stop - q), "\nQ\n");
                size_t span = close ? (size_t)(close - q) : (size_t)(stop - q);
                int text = find(q, span, "BT ") != NULL;
                int big = 0;
                {   /* a rectangle of at least 200 by 200 points: a background, not a rule or a box */
                    const unsigned char *re = find(q, span, "re\n");
                    if (re) {
                        double a, b, w, h;
                        const unsigned char *pp = q + 3;                        /* after "rg\n" */
                        if (parse_num(&pp, re, &a) && parse_num(&pp, re, &b) && parse_num(&pp, re, &w) && parse_num(&pp, re, &h)) big = w >= 200 && h >= 200;
                    }
                }
                if (!text && big && lum(v[0], v[1], v[2]) < 0.5) c->dark_fills++;
            }
        }
    }
}

int ex_pdfw_check(const unsigned char *pdf, size_t len, ExPdfCheck *c) {
    Ctx x;
    const unsigned char *sx, *xr, *p;
    size_t xref_at = 0, size = 0, i;
    char *text = NULL;
    size_t tn = 0, tcap = 0;
    memset(c, 0, sizeof *c);
    memset(&x, 0, sizeof x);
    c->ok = 1;
    if (!pdf || len < 40 || memcmp(pdf, "%PDF-1.", 7)) { fail(c, "not a PDF"); return -1; }
    if (len < 6 || memcmp(pdf + len - 6, "%%EOF\n", 6)) { fail(c, "no end-of-file marker"); return -1; }
    sx = rfind(pdf, len, "startxref\n");
    if (!sx) { fail(c, "no startxref"); return -1; }
    xref_at = (size_t)atoll((const char *)sx + 10);
    if (xref_at >= len || memcmp(pdf + xref_at, "xref\n0 ", 7)) { fail(c, "startxref does not point to the cross-reference table"); return -1; }
    xr = pdf + xref_at;
    size = (size_t)atoll((const char *)xr + 7);
    if (size < 4 || size > 10000000) { fail(c, "bad cross-reference size"); return -1; }
    p = find(xr, len - xref_at, "\n") + 1;           /* after "xref" */
    p = find(p, len - (size_t)(p - pdf), "\n") + 1;   /* after "0 N" */
    x.pdf = pdf; x.len = len; x.n = size;
    x.objs = calloc(size, sizeof *x.objs);
    if (!x.objs) { fail(c, "out of memory"); return -1; }
    if (memcmp(p, "0000000000 65535 f \n", 20)) { fail(c, "the free entry is wrong"); goto bad; }
    for (i = 1; i < size; i++) {
        const unsigned char *e = p + 20 * i;
        size_t off = (size_t)atoll((const char *)e), hl;
        char head[40];
        Obj *o = &x.objs[i];
        const unsigned char *body, *st;
        if ((size_t)(e - pdf) + 20 > len || e[10] != ' ' || e[16] != ' ' || e[17] != 'n' || e[19] != '\n') { fail(c, "a cross-reference entry is malformed"); goto bad; }
        if (off == 0 || off >= xref_at) { fail(c, "an object offset is outside the file"); goto bad; }
        hl = (size_t)snprintf(head, sizeof head, "%zu 0 obj\n", i);
        if (memcmp(pdf + off, head, hl)) { fail(c, "an offset does not point to its object"); goto bad; }
        body = pdf + off + hl;
        st = find(body, xref_at - off - hl, "\nstream\n");
        {
            const unsigned char *eo = find(body, xref_at - off - hl, "endobj\n");
            const unsigned char *dict_end = body;
            if (!eo) { fail(c, "an object has no endobj"); goto bad; }
            if (st && st < eo) {
                size_t slen = ref_of(body, (size_t)(st - body), "/Length");
                (void)slen;
                {   /* /Length is a number, not a reference */
                    const unsigned char *lp = find(body, (size_t)(st - body), "/Length ");
                    slen = lp ? (size_t)atoll((const char *)lp + 8) : 0;
                }
                o->has_stream = 1; o->dict = body; o->dict_len = (size_t)(st - body);
                o->data = st + 8; o->data_len = slen;
                if (o->data + slen + 11 > pdf + xref_at || memcmp(o->data + slen, "\nendstream\n", 11)) { fail(c, "a stream's length is wrong"); goto bad; }
            } else { dict_end = eo; o->dict = body; o->dict_len = (size_t)(dict_end - body); }
            o->present = 1;
        }
    }
    {
        const unsigned char *tr = find(xr, len - xref_at, "trailer\n");
        if (!tr || !find(tr, len - (size_t)(tr - pdf), "/Root 1 0 R") || (size_t)atoll((const char *)find(tr, len - (size_t)(tr - pdf), "/Size ") + 6) != size) { fail(c, "the trailer is wrong"); goto bad; }
    }
    /* the catalog, the page tree */
    if (!x.objs[1].present || !find(x.objs[1].dict, x.objs[1].dict_len, "/Type /Catalog") || !x.objs[2].present || !find(x.objs[2].dict, x.objs[2].dict_len, "/Type /Pages")) { fail(c, "no catalog or page tree"); goto bad; }
    for (i = 4; i < size; i++) {
        const Obj *o = &x.objs[i];
        if (!o->present) continue;
        if (find(o->dict, o->dict_len, "/Type /Page /")) c->pages++;
        if (find(o->dict, o->dict_len, "/Subtype /Type0")) {
            size_t tu = ref_of(o->dict, o->dict_len, "/ToUnicode"), tn2 = 0;
            unsigned char *ts;
            c->fonts++;
            ts = object_stream(&x, tu, &tn2);
            if (ts && find(ts, tn2, "begincmap")) c->fonts_tounicode++;
            free(ts);
        }
        if (find(o->dict, o->dict_len, "/Type /FontDescriptor")) {
            const unsigned char *fn = find(o->dict, o->dict_len, "/FontName /");
            size_t ff = ref_of(o->dict, o->dict_len, "/FontFile2 ");
            if (!ff) ff = ref_of(o->dict, o->dict_len, "/FontFile3 ");
            if (ff && ff < size && x.objs[ff].present && x.objs[ff].has_stream && x.objs[ff].data_len > 100) c->fonts_embedded++;
            if (fn) {
                int k, up = 1;
                for (k = 0; k < 6; k++) if (fn[11 + k] < 'A' || fn[11 + k] > 'Z') up = 0;
                if (up && fn[17] == '+') c->fonts_subset++;
            }
        }
        if (find(o->dict, o->dict_len, "/Subtype /Image") && find(o->dict, o->dict_len, "/DeviceRGB")) c->images++;
    }
    {   /* the page tree's count and kids agree with the pages found */
        const unsigned char *cnt = find(x.objs[2].dict, x.objs[2].dict_len, "/Count ");
        if (!cnt || (int)atoi((const char *)cnt + 7) != c->pages) { fail(c, "the page count does not match the pages"); goto bad; }
    }
    {   /* every page in the order of the kids: its text, and the colors of its shapes */
        const unsigned char *kids = find(x.objs[2].dict, x.objs[2].dict_len, "/Kids [");
        const unsigned char *q;
        if (!kids) { fail(c, "no kids"); goto bad; }
        q = kids + 7;
        while (q < x.objs[2].dict + x.objs[2].dict_len && *q != ']') {
            size_t id = (size_t)atoll((const char *)q);
            if (id == 0 || id >= size || !x.objs[id].present) { fail(c, "a kid is not an object"); goto bad; }
            recover_text(&x, id, c, &text, &tn, &tcap);
            {   size_t cid = ref_of(x.objs[id].dict, x.objs[id].dict_len, "/Contents"), cn = 0;
                unsigned char *cs = object_stream(&x, cid, &cn);
                if (cs) { scan_fills(cs, cn, c); free(cs); }
            }
            while (*q && *q != ' ') q++;
            q += 5;                                              /* " 0 R " */
            while (*q == ' ') q++;
            if (*(q - 1) != ' ' && *q != ']') break;
        }
    }
    free(x.objs);
    c->text = text ? text : calloc(1, 1);
    (void)i;
    return c->ok ? 0 : -1;
bad:
    free(x.objs);
    free(text);
    return -1;
}

void ex_pdfw_check_free(ExPdfCheck *c) { if (c) { free(c->text); c->text = NULL; } }
