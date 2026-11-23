/* What a base needs to know about every file, read from the index in a few passes (sub-slice 6b-ii). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base.h"
#include "exhaust_extract.h"
#include "exhaust_index.h"
#include "store_internal.h"

static size_t find_id(const int64_t *ids, size_t n, int64_t id) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (ids[mid] == id) return mid; if (ids[mid] < id) lo = mid + 1; else hi = mid; }
    return (size_t)-1;
}

static int has_tag(const ExBaseFile *f, const char *tag) {
    size_t i;
    for (i = 0; i < f->ntags; i++) {
        const char *a = f->tags[i], *b = tag;
        while (*a && *b && ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) == (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b))) { a++; b++; }
        if (!*a && !*b) return 1;
    }
    return 0;
}

int ex_base_data_load(ExIndex *r, ExBaseData *out) {
    sqlite3_stmt *st;
    int64_t *ids = NULL;
    size_t n = 0, cap = 0, k;
    memset(out, 0, sizeof *out);
    st = ex_db_prepare(r, "SELECT id, path, kind, size, mtime, ctime FROM files ORDER BY id");
    if (!st) return -1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        ExBaseFile *f;
        double mt = (double)sqlite3_column_int64(st, 4) / 1e6, ct = (double)sqlite3_column_int64(st, 5) / 1e6;
        if (n == cap) { size_t nc = cap ? cap * 2 : 256; int64_t *ni = realloc(ids, nc * sizeof *ni); if (!ni) { sqlite3_finalize(st); free(ids); ex_base_data_free(out); return -1; } ids = ni; cap = nc; }
        f = ex_base_data_add(out, (const char *)sqlite3_column_text(st, 1), sqlite3_column_int(st, 2), ct > 0 ? ct : mt, mt, (double)sqlite3_column_int64(st, 3));
        if (!f) { sqlite3_finalize(st); free(ids); ex_base_data_free(out); return -1; }
        ids[n++] = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    /* properties */
    st = ex_db_prepare(r, "SELECT file_id, name, type, value FROM props ORDER BY file_id, ord");
    if (st) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            k = find_id(ids, n, sqlite3_column_int64(st, 0));
            if (k != (size_t)-1) ex_base_file_prop(&out->files[k], (const char *)sqlite3_column_text(st, 1), sqlite3_column_int(st, 2), (const char *)sqlite3_column_text(st, 3));
        }
        sqlite3_finalize(st);
    }
    /* tags: with their # and once each */
    st = ex_db_prepare(r, "SELECT file_id, tag FROM tags ORDER BY file_id, ord");
    if (st) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            char buf[400];
            k = find_id(ids, n, sqlite3_column_int64(st, 0));
            if (k == (size_t)-1) continue;
            snprintf(buf, sizeof buf, "#%s", (const char *)sqlite3_column_text(st, 1));
            if (!has_tag(&out->files[k], buf)) ex_base_file_tag(&out->files[k], buf);
        }
        sqlite3_finalize(st);
    }
    /* links and embeds, as written (with the heading or block part) */
    st = ex_db_prepare(r, "SELECT file_id, kind, target, heading, block FROM links ORDER BY file_id, ord");
    if (st) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            char buf[1200];
            const char *t = (const char *)sqlite3_column_text(st, 2), *h = (const char *)sqlite3_column_text(st, 3), *b = (const char *)sqlite3_column_text(st, 4);
            int kind = sqlite3_column_int(st, 1);
            k = find_id(ids, n, sqlite3_column_int64(st, 0));
            if (k == (size_t)-1) continue;
            if (h && *h) snprintf(buf, sizeof buf, "%s#%s", t, h);
            else if (b && *b) snprintf(buf, sizeof buf, "%s#^%s", t, b);
            else snprintf(buf, sizeof buf, "%s", t);
            if (kind == EX_LINK_EMBED || kind == EX_LINK_MD_EMBED) ex_base_file_embed(&out->files[k], buf); else ex_base_file_link(&out->files[k], buf);
        }
        sqlite3_finalize(st);
    }
    free(ids);
    return 0;
}
