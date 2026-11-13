#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_nav.h"
#include "exhaust_vault.h"
#include "sqlite3.h"
#include "../index/index_internal.h"

static unsigned long long next(unsigned long long *s) {
    unsigned long long z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

char *ex_random_note(ExIndex *r, const char *except, unsigned long long *state) {
    sqlite3_stmt *st;
    long count = ex_index_file_count(r, EX_KIND_MARKDOWN);
    int skip = 0;
    char *out = NULL;
    if (count <= 0) return NULL;
    if (except && count > 1 && ex_index_file_id(r, except)) skip = 1;
    st = ex_db_prepare(r, "SELECT path FROM files WHERE kind = 1 AND path != ?1 ORDER BY id LIMIT 1 OFFSET ?2");
    if (!st) return NULL;
    sqlite3_bind_text(st, 1, skip ? except : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)(next(state) % (unsigned long long)(count - skip)));
    if (sqlite3_step(st) == SQLITE_ROW) out = ex_strdup((const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return out;
}
