#ifndef EXHAUST_INDEX_INTERNAL_H
#define EXHAUST_INDEX_INTERNAL_H
#include "exhaust_index.h"
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"

static inline char *ex_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

struct ExIndex {
    sqlite3 *db;
    char *path;
    int writer;
    int rebuilt;
    int version;
};

/* db.c */
int ex_db_exec(ExIndex *ix, const char *sql);
sqlite3_stmt *ex_db_prepare(ExIndex *ix, const char *sql);
int ex_db_begin(ExIndex *ix);
int ex_db_commit(ExIndex *ix);
void ex_db_rollback(ExIndex *ix);
int ex_index_apply_migrations(ExIndex *ix, int up_to);

/* The note files read by the index code (a counter the tests look at). */
char *ex_index_read_note(const char *vault_root, const char *rel, size_t *len);
#endif
