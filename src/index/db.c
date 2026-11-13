/* Opening, creating and migrating the index database. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "index_internal.h"

/* Numbered migrations. Never edit one that has shipped: add the next. Each only adds tables, indexes and nullable or
 * defaulted columns, and says whether the notes have to be read again (reindex = 1 marks every file as not yet indexed). */
typedef struct Migration { const char *sql; int reindex; } Migration;

static const Migration MIGRATIONS[] = {
    /* 1 (sub-slice 4a-i): files, the trigram text index, lines, sections, blocks and tasks */
    {
        "CREATE TABLE files(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, kind INTEGER NOT NULL, size INTEGER NOT NULL,"
        " mtime INTEGER NOT NULL, hash BLOB, indexed INTEGER NOT NULL DEFAULT 0);"
        "CREATE VIRTUAL TABLE fts USING fts5(body, content='', contentless_delete=1, tokenize='trigram', detail=none);"
        "CREATE TABLE lines(file_id INTEGER PRIMARY KEY, count INTEGER NOT NULL, offsets BLOB NOT NULL);"
        "CREATE TABLE sections(file_id INTEGER NOT NULL, ord INTEGER NOT NULL, level INTEGER NOT NULL, title TEXT NOT NULL,"
        " start INTEGER NOT NULL, end_ INTEGER NOT NULL, section_end INTEGER NOT NULL, line INTEGER NOT NULL, PRIMARY KEY(file_id, ord)) WITHOUT ROWID;"
        "CREATE TABLE blocks(file_id INTEGER NOT NULL, block_id TEXT NOT NULL, start INTEGER NOT NULL, end_ INTEGER NOT NULL,"
        " line INTEGER NOT NULL, PRIMARY KEY(file_id, start)) WITHOUT ROWID;"
        "CREATE INDEX blocks_by_id ON blocks(file_id, block_id);"
        "CREATE TABLE tasks(file_id INTEGER NOT NULL, start INTEGER NOT NULL, end_ INTEGER NOT NULL, line INTEGER NOT NULL,"
        " state INTEGER NOT NULL, text TEXT NOT NULL, PRIMARY KEY(file_id, start)) WITHOUT ROWID;",
        0
    },
    /* 2 (sub-slice 4a-ii): the names a link can use, and the links of every note */
    {
        "CREATE TABLE names(file_id INTEGER NOT NULL, kind INTEGER NOT NULL, key TEXT NOT NULL, text TEXT NOT NULL, PRIMARY KEY(file_id, kind, key)) WITHOUT ROWID;"
        "CREATE INDEX names_by_key ON names(key);"
        "CREATE TABLE links(file_id INTEGER NOT NULL, ord INTEGER NOT NULL, kind INTEGER NOT NULL, target TEXT NOT NULL, target_key TEXT NOT NULL,"
        " name_key TEXT NOT NULL, heading TEXT, block TEXT, alias TEXT, start INTEGER NOT NULL, end_ INTEGER NOT NULL, line INTEGER NOT NULL,"
        " PRIMARY KEY(file_id, ord)) WITHOUT ROWID;"
        "CREATE INDEX links_by_name ON links(name_key);",
        1
    },
    /* 3 (sub-slice 4a-iii): tags and properties */
    {
        "CREATE TABLE tags(file_id INTEGER NOT NULL, ord INTEGER NOT NULL, start INTEGER NOT NULL, tag TEXT NOT NULL, tag_key TEXT NOT NULL, line INTEGER NOT NULL,"
        " in_props INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(file_id, ord)) WITHOUT ROWID;"
        "CREATE INDEX tags_by_key ON tags(tag_key);"
        "CREATE TABLE props(file_id INTEGER NOT NULL, ord INTEGER NOT NULL, name TEXT NOT NULL, name_key TEXT NOT NULL, type INTEGER NOT NULL,"
        " value TEXT NOT NULL, start INTEGER NOT NULL, end_ INTEGER NOT NULL, line INTEGER NOT NULL, PRIMARY KEY(file_id, ord)) WITHOUT ROWID;"
        "CREATE INDEX props_by_name ON props(name_key);",
        1
    },
};
#define N_MIGRATIONS ((int)(sizeof MIGRATIONS / sizeof MIGRATIONS[0]))

int ex_db_exec(ExIndex *ix, const char *sql) { return sqlite3_exec(ix->db, sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1; }

sqlite3_stmt *ex_db_prepare(ExIndex *ix, const char *sql) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v3(ix->db, sql, -1, SQLITE_PREPARE_PERSISTENT, &st, NULL) != SQLITE_OK) return NULL;
    return st;
}

int ex_db_begin(ExIndex *ix) { return ex_db_exec(ix, "BEGIN IMMEDIATE"); }
int ex_db_commit(ExIndex *ix) { return ex_db_exec(ix, "COMMIT"); }
void ex_db_rollback(ExIndex *ix) { ex_db_exec(ix, "ROLLBACK"); }

static int user_version(sqlite3 *db) {
    sqlite3_stmt *st;
    int v = -1;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, NULL) != SQLITE_OK) return -1;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

void ex_index_delete_files(const char *path) {
    char buf[1100];
    remove(path);
    snprintf(buf, sizeof buf, "%s-wal", path); remove(buf);
    snprintf(buf, sizeof buf, "%s-shm", path); remove(buf);
}

int ex_index_apply_migrations(ExIndex *ix, int up_to) {
    int v = user_version(ix->db);
    if (v < 0) return -1;
    while (v < up_to && v < N_MIGRATIONS) {
        char pragma[40];
        if (ex_db_begin(ix) != 0) return -1;
        if (ex_db_exec(ix, MIGRATIONS[v].sql) != 0) { ex_db_rollback(ix); return -1; }
        if (MIGRATIONS[v].reindex && ex_db_exec(ix, "UPDATE files SET indexed = 0") != 0) { ex_db_rollback(ix); return -1; }
        snprintf(pragma, sizeof pragma, "PRAGMA user_version = %d", v + 1);
        if (ex_db_exec(ix, pragma) != 0) { ex_db_rollback(ix); return -1; }
        if (ex_db_commit(ix) != 0) { ex_db_rollback(ix); return -1; }
        v++;
    }
    ix->version = v;
    return 0;
}

static void set_err(char *err, size_t cap, const char *what, sqlite3 *db) {
    if (err && cap) snprintf(err, cap, "%s%s%s", what, db ? ": " : "", db ? sqlite3_errmsg(db) : "");
}

static ExIndex *open_one(const char *path, int writer, int stop_at, int *was_rebuilt, char *err, size_t errcap) {
    ExIndex *ix = calloc(1, sizeof *ix);
    int flags = writer ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY, v;
    if (!ix) return NULL;
    ix->path = ex_strdup(path);
    ix->writer = writer;
    if (!ix->path || sqlite3_open_v2(path, &ix->db, flags | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK) {
        set_err(err, errcap, "cannot open the index", ix->db);
        goto fail;
    }
    sqlite3_busy_timeout(ix->db, 10000);
    if (writer) {
        sqlite3_stmt *st = NULL;
        int ok = 0;
        if (sqlite3_prepare_v2(ix->db, "PRAGMA journal_mode = WAL", -1, &st, NULL) == SQLITE_OK) { ok = sqlite3_step(st) == SQLITE_ROW; sqlite3_finalize(st); }
        if (!ok) { ix->version = -1; goto damaged; }
        ex_db_exec(ix, "PRAGMA synchronous = NORMAL");
    }
    v = user_version(ix->db);
    if (v < 0) goto damaged;
    if (writer) {
        if (v > EX_INDEX_SCHEMA_VERSION) goto damaged;       /* written by a newer Exhaust: it is only a cache */
        if (ex_index_apply_migrations(ix, stop_at) != 0) goto damaged;
        if (sqlite3_exec(ix->db, "SELECT count(*) FROM files", NULL, NULL, NULL) != SQLITE_OK && ix->version > 0) goto damaged;
    } else {
        if (v != EX_INDEX_SCHEMA_VERSION) { set_err(err, errcap, "the index is not ready", NULL); goto fail; }
        ix->version = v;
    }
    return ix;
damaged:
    if (writer && !*was_rebuilt) {
        sqlite3_close(ix->db);
        ex_index_delete_files(path);
        free(ix->path);
        free(ix);
        *was_rebuilt = 1;
        ix = open_one(path, writer, stop_at, was_rebuilt, err, errcap);
        return ix;
    }
    set_err(err, errcap, "the index is damaged", NULL);
fail:
    if (ix->db) sqlite3_close(ix->db);
    free(ix->path);
    free(ix);
    return NULL;
}

static ExIndex *open_path(const char *path, int writer, int stop_at, char *err, size_t errcap) {
    int rebuilt = 0;
    ExIndex *ix;
    if (writer) {
        char *dir = ex_strdup(path), *slash;
        if (dir && ((slash = strrchr(dir, '/')) || (slash = strrchr(dir, '\\')))) { *slash = 0; ex_platform_make_dirs(dir); }
        free(dir);
    }
    ix = open_one(path, writer, stop_at, &rebuilt, err, errcap);
    if (ix) ix->rebuilt = rebuilt;
    return ix;
}

ExIndex *ex_index_open(const char *path, int writer, char *err, size_t errcap) { return open_path(path, writer, EX_INDEX_SCHEMA_VERSION, err, errcap); }
ExIndex *ex_index_open_at_version(const char *path, int version, char *err, size_t errcap) { return open_path(path, 1, version, err, errcap); }

void ex_index_close(ExIndex *ix) {
    if (!ix) return;
    if (ix->db) sqlite3_close(ix->db);
    free(ix->path);
    free(ix);
}

int ex_index_schema_version(const ExIndex *ix) { return ix->version; }
int ex_index_was_rebuilt(const ExIndex *ix) { return ix->rebuilt; }

char *ex_index_path_for_vault(const char *vault_id) {
    char *cache = ex_platform_cache_dir(), *out;
    size_t n;
    if (!cache) return NULL;
    n = strlen(cache) + strlen(vault_id) + 32;
    out = malloc(n);
    if (out) snprintf(out, n, "%s/vaults/%s/index.db", cache, vault_id);
    free(cache);
    return out;
}
