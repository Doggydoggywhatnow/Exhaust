#include "exhaust_import.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 1000
#define MAX_FIELD (4u * 1024u * 1024u)

typedef struct Row { char *buf; size_t used, cap; int oom; } Row;

static void put(Row *r, char c) {
    if (r->oom) return;
    if (r->used + 1 > r->cap) {
        size_t nc = r->cap ? r->cap * 2 : 4096;
        char *nb = realloc(r->buf, nc);
        if (!nb) { r->oom = 1; return; }
        r->buf = nb; r->cap = nc;
    }
    r->buf[r->used++] = c;
}

size_t ex_csv_parse(const char *text, size_t len, ExCsvRowFn fn, void *user) {
    size_t i = 0, rows = 0;
    char delim = ',';
    const char **fields;
    size_t *lens, *offs;
    Row r;
    memset(&r, 0, sizeof r);
    if (!text) return 0;
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) i = 3;
    {   /* the delimiter: the one the first line has most of, outside quotes */
        size_t k, c = 0, s = 0, t = 0;
        int q = 0;
        for (k = i; k < len && !(text[k] == '\n' && !q); k++) {
            if (text[k] == '"') q = !q;
            else if (!q) { if (text[k] == ',') c++; else if (text[k] == ';') s++; else if (text[k] == '\t') t++; }
        }
        if (s > c && s >= t) delim = ';'; else if (t > c && t > s) delim = '\t';
    }
    fields = malloc(MAX_FIELDS * sizeof *fields); lens = malloc(MAX_FIELDS * sizeof *lens); offs = malloc(MAX_FIELDS * sizeof *offs);
    if (!fields || !lens || !offs) { free(fields); free(lens); free(offs); return 0; }
    while (i < len) {
        size_t nf = 0, k;
        int more = 1, stop;
        if (text[i] == '\n') { i++; continue; }
        if (text[i] == '\r') { i++; continue; }
        r.used = 0;
        while (more) {
            size_t flen = 0, at = r.used;
            if (i < len && text[i] == '"') {
                i++;
                while (i < len) {
                    if (text[i] == '"') { if (i + 1 < len && text[i + 1] == '"') { if (flen < MAX_FIELD) { put(&r, '"'); flen++; } i += 2; continue; } i++; break; }
                    if (flen < MAX_FIELD) { put(&r, text[i]); flen++; }
                    i++;
                }
                while (i < len && text[i] != delim && text[i] != '\n' && text[i] != '\r') i++;     /* what follows the closing quote is dropped */
            } else {
                while (i < len && text[i] != delim && text[i] != '\n' && text[i] != '\r') { if (flen < MAX_FIELD) { put(&r, text[i]); flen++; } i++; }
            }
            put(&r, 0);
            if (nf < MAX_FIELDS) { offs[nf] = at; lens[nf] = flen; nf++; }
            if (i < len && text[i] == delim) {
                i++;
                if (i >= len || text[i] == '\n' || text[i] == '\r') { put(&r, 0); if (nf < MAX_FIELDS) { offs[nf] = r.used - 1; lens[nf] = 0; nf++; } more = 0; }   /* a trailing delimiter: one more empty field */
            } else more = 0;
        }
        if (i < len && text[i] == '\r') i++;
        if (i < len && text[i] == '\n') i++;
        if (r.oom) break;
        for (k = 0; k < nf; k++) fields[k] = r.buf + offs[k];
        stop = fn ? fn(fields, lens, nf, rows, user) : 0;
        rows++;
        if (stop) break;
    }
    free(r.buf); free(fields); free(lens); free(offs);
    return rows;
}
