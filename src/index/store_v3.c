/* The part of storing a note that belongs to schema version 3 (sub-slice 4a-iii): tags and properties. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_nfc.h"
#include "store_internal.h"

static int run(sqlite3_stmt *st) { int rc = sqlite3_step(st); sqlite3_reset(st); sqlite3_clear_bindings(st); return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1; }

int ex_store_extra_v3(ExIndex *ix, int64_t id, const ExExtract *x) {
    sqlite3_stmt *st;
    size_t i;
    int rc = 0;
    if (ix->version < 3) return 0;
    st = ex_db_prepare(ix, "INSERT INTO tags(file_id, ord, start, tag, tag_key, line, in_props) VALUES(?1,?2,?3,?4,?5,?6,?7)");
    if (!st) return -1;
    for (i = 0; i < x->ntags; i++) {
        const ExXTag *t = &x->tags[i];
        char *key = ex_name_key(t->tag, strlen(t->tag));
        if (!key) { rc = -1; break; }
        sqlite3_bind_int64(st, 1, id); sqlite3_bind_int64(st, 2, (sqlite3_int64)i); sqlite3_bind_int64(st, 3, (sqlite3_int64)t->start); sqlite3_bind_text(st, 4, t->tag, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 5, key, -1, SQLITE_STATIC); sqlite3_bind_int(st, 6, (int)t->line); sqlite3_bind_int(st, 7, t->in_props);
        rc |= run(st);
        free(key);
    }
    sqlite3_finalize(st);
    st = ex_db_prepare(ix, "INSERT INTO props(file_id, ord, name, name_key, type, value, start, end_, line) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)");
    if (!st) return -1;
    for (i = 0; i < x->nprops; i++) {
        const ExXProp *p = &x->props[i];
        char *key = ex_name_key(p->name, strlen(p->name));
        if (!key) { rc = -1; break; }
        sqlite3_bind_int64(st, 1, id); sqlite3_bind_int64(st, 2, (sqlite3_int64)i); sqlite3_bind_text(st, 3, p->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 4, key, -1, SQLITE_STATIC); sqlite3_bind_int(st, 5, p->type); sqlite3_bind_text(st, 6, p->value, -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 7, (sqlite3_int64)p->start); sqlite3_bind_int64(st, 8, (sqlite3_int64)p->end); sqlite3_bind_int(st, 9, (int)p->line);
        rc |= run(st);
        free(key);
    }
    sqlite3_finalize(st);
    return rc ? -1 : 0;
}

int ex_store_clear_extra_v3(ExIndex *ix, int64_t id) {
    static const char *const sql[] = {"DELETE FROM tags WHERE file_id = ?1", "DELETE FROM props WHERE file_id = ?1"};
    int rc = 0;
    size_t i;
    if (ix->version < 3) return 0;
    for (i = 0; i < 2; i++) {
        sqlite3_stmt *st = ex_db_prepare(ix, sql[i]);
        if (!st) return -1;
        sqlite3_bind_int64(st, 1, id);
        rc |= run(st);
        sqlite3_finalize(st);
    }
    return rc ? -1 : 0;
}

int ex_clear_extra_tables_v3(ExIndex *ix) { return ix->version < 3 ? 0 : ex_db_exec(ix, "DELETE FROM tags; DELETE FROM props"); }
