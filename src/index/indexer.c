/* Filling the index from the vault: the walk, the per-note storage, the queries of 4a-i and the indexer thread. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_extract.h"
#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_util.h"
#include "exhaust_vault.h"
#include "index_internal.h"
#include "store_internal.h"

static volatile long g_note_reads;
long ex_index_note_reads(void) { return g_note_reads; }

char *ex_index_read_note(const char *vault_root, const char *rel, size_t *len) {
    size_t n = strlen(vault_root) + strlen(rel) + 2;
    char *path = malloc(n), *bytes, *text = NULL;
    size_t blen = 0, tlen = 0;
    if (!path) return NULL;
    snprintf(path, n, "%s/%s", vault_root, rel);
    bytes = ex_platform_read_file(path, &blen);
    free(path);
    g_note_reads++;
    if (!bytes) return NULL;
    if (ex_note_decode((const uint8_t *)bytes, blen, &text, &tlen, NULL) != 0) { free(bytes); return NULL; }
    free(bytes);
    if (len) *len = tlen;
    return text;
}

/* ---- storing one file ---- */

typedef struct Stmts { sqlite3_stmt *fts_del, *fts_ins, *lines_ins, *sec_ins, *blk_ins, *task_ins, *del[8]; int ndel; } Stmts;

static void put_varint(unsigned char **p, uint32_t v) { while (v >= 0x80) { *(*p)++ = (unsigned char)(v | 0x80); v >>= 7; } *(*p)++ = (unsigned char)v; }

static int run1(sqlite3_stmt *st) { int rc = sqlite3_step(st); sqlite3_reset(st); sqlite3_clear_bindings(st); return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1; }

/* Replaces what is stored about a note with what its text says. */
int ex_store_note(ExIndex *ix, int64_t id, const char *text, size_t len) {
    ExExtract *x = ex_extract(text, len);
    sqlite3_stmt *st;
    size_t i;
    int rc = 0;
    unsigned char *blob, *p;
    uint32_t prev = 0;
    if (!x) return -1;
    if (ex_store_clear(ix, id) != 0) { ex_extract_free(x); return -1; }
    st = ex_db_prepare(ix, "INSERT INTO fts(rowid, body) VALUES(?1, ?2)");
    if (!st) { ex_extract_free(x); return -1; }
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_text(st, 2, text, (int)len, SQLITE_STATIC);
    rc |= run1(st);
    sqlite3_finalize(st);
    blob = malloc(x->nlines * 5 + 1);
    if (!blob) { ex_extract_free(x); return -1; }
    p = blob;
    for (i = 0; i < x->nlines; i++) { put_varint(&p, x->line_starts[i] - prev); prev = x->line_starts[i]; }
    st = ex_db_prepare(ix, "INSERT INTO lines(file_id, count, offsets) VALUES(?1, ?2, ?3)");
    if (st) { sqlite3_bind_int64(st, 1, id); sqlite3_bind_int64(st, 2, (sqlite3_int64)x->nlines); sqlite3_bind_blob(st, 3, blob, (int)(p - blob), SQLITE_STATIC); rc |= run1(st); sqlite3_finalize(st); } else rc = -1;
    free(blob);
    st = ex_db_prepare(ix, "INSERT INTO sections(file_id, ord, level, title, start, end_, section_end, line) VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
    if (st) {
        for (i = 0; i < x->nheadings; i++) {
            const ExXHeading *h = &x->headings[i];
            sqlite3_bind_int64(st, 1, id); sqlite3_bind_int64(st, 2, (sqlite3_int64)i); sqlite3_bind_int(st, 3, h->level);
            sqlite3_bind_text(st, 4, h->text, -1, SQLITE_STATIC); sqlite3_bind_int64(st, 5, (sqlite3_int64)h->start);
            sqlite3_bind_int64(st, 6, (sqlite3_int64)h->end); sqlite3_bind_int64(st, 7, (sqlite3_int64)h->section_end); sqlite3_bind_int(st, 8, (int)h->line);
            rc |= run1(st);
        }
        sqlite3_finalize(st);
    } else rc = -1;
    st = ex_db_prepare(ix, "INSERT OR REPLACE INTO blocks(file_id, block_id, start, end_, line) VALUES(?1,?2,?3,?4,?5)");
    if (st) {
        for (i = 0; i < x->nblocks; i++) {
            const ExXBlock *b = &x->blocks[i];
            sqlite3_bind_int64(st, 1, id); sqlite3_bind_text(st, 2, b->id, -1, SQLITE_STATIC); sqlite3_bind_int64(st, 3, (sqlite3_int64)b->start);
            sqlite3_bind_int64(st, 4, (sqlite3_int64)b->end); sqlite3_bind_int(st, 5, (int)b->line);
            rc |= run1(st);
        }
        sqlite3_finalize(st);
    } else rc = -1;
    st = ex_db_prepare(ix, "INSERT OR REPLACE INTO tasks(file_id, start, end_, line, state, text) VALUES(?1,?2,?3,?4,?5,?6)");
    if (st) {
        for (i = 0; i < x->ntasks; i++) {
            const ExXTask *k = &x->tasks[i];
            sqlite3_bind_int64(st, 1, id); sqlite3_bind_int64(st, 2, (sqlite3_int64)k->start); sqlite3_bind_int64(st, 3, (sqlite3_int64)k->end);
            sqlite3_bind_int(st, 4, (int)k->line); sqlite3_bind_int(st, 5, k->state); sqlite3_bind_text(st, 6, k->text, -1, SQLITE_STATIC);
            rc |= run1(st);
        }
        sqlite3_finalize(st);
    } else rc = -1;
    rc |= ex_store_extra(ix, id, x);
    ex_extract_free(x);
    return rc ? -1 : 0;
}

int ex_store_clear(ExIndex *ix, int64_t id) {
    static const char *const tables[] = {"lines", "sections", "blocks", "tasks", NULL};
    char sql[160];
    size_t i;
    sqlite3_stmt *st = ex_db_prepare(ix, "DELETE FROM fts WHERE rowid = ?1");
    int rc = 0;
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    rc |= run1(st);
    sqlite3_finalize(st);
    for (i = 0; tables[i]; i++) {
        snprintf(sql, sizeof sql, "DELETE FROM %s WHERE file_id = ?1", tables[i]);
        st = ex_db_prepare(ix, sql);
        if (!st) return -1;
        sqlite3_bind_int64(st, 1, id);
        rc |= run1(st);
        sqlite3_finalize(st);
    }
    rc |= ex_store_clear_extra(ix, id);
    return rc ? -1 : 0;
}

/* ---- files ---- */

typedef struct Entry { char *rel; int64_t mtime, ctime, size; } Entry;
typedef struct Walk { const char *root; size_t rootlen; const char *const *excl; size_t nexcl; Entry *v; size_t n, cap; int fail; } Walk;

static int hidden(const char *rel) {
    const char *p = rel;
    while (*p) {
        if (*p == '.') return 1;
        while (*p && *p != '/') p++;
        if (*p == '/') p++;
    }
    return 0;
}

static int walk_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    Walk *w = user;
    const char *rel = path + w->rootlen;
    ExPathInfo pi;
    (void)mtime;
    while (*rel == '/') rel++;
    if (!*rel) return 0;
    if (is_dir) return 0;
    if (hidden(rel) || ex_patterns_match(w->excl, w->nexcl, rel)) return 0;
    if (ex_platform_path_info(path, &pi) != 0 || !pi.is_file) return 0;
    if (w->n == w->cap) {
        size_t nc = w->cap ? w->cap * 2 : 1024;
        Entry *nv = realloc(w->v, nc * sizeof *nv);
        if (!nv) { w->fail = 1; return 1; }
        w->v = nv; w->cap = nc;
    }
    w->v[w->n].rel = ex_strdup(rel);
    w->v[w->n].mtime = pi.mtime;
    w->v[w->n].ctime = pi.ctime;
    w->v[w->n].size = pi.size;
    if (!w->v[w->n].rel) { w->fail = 1; return 1; }
    w->n++;
    return 0;
}

static int cmp_entry(const void *a, const void *b) { return strcmp(((const Entry *)a)->rel, ((const Entry *)b)->rel); }

static int is_note_path(const char *rel) { return ex_file_kind(rel) == EX_KIND_MARKDOWN; }

/* Inserts or updates the row of one file and, for a note, its contents. text may be NULL (read it from disk). */
static int index_one(ExIndex *ix, const char *root, const char *rel, int64_t mtime, int64_t ctime, int64_t size, int64_t *existing_id, const char *given_text, size_t given_len) {
    int kind = (int)ex_file_kind(rel);
    sqlite3_stmt *st;
    int64_t id = existing_id ? *existing_id : 0;
    char *text = NULL;
    size_t len = 0;
    uint8_t digest[32];
    int have_text = 0, rc = 0;
    if (kind == EX_KIND_MARKDOWN) {
        if (given_text) { text = (char *)given_text; len = given_len; have_text = 1; }
        else { text = ex_index_read_note(root, rel, &len); have_text = text != NULL; }
    }
    if (have_text) ex_sha256(text, len, digest);
    if (!id) {
        st = ex_db_prepare(ix, "INSERT INTO files(path, kind, size, mtime, hash, indexed, ctime) VALUES(?1,?2,?3,?4,?5,1,?6)");
        if (!st) { if (have_text && !given_text) free(text); return -1; }
        sqlite3_bind_text(st, 1, rel, -1, SQLITE_STATIC); sqlite3_bind_int(st, 2, kind); sqlite3_bind_int64(st, 3, size); sqlite3_bind_int64(st, 4, mtime);
        sqlite3_bind_int64(st, 6, ctime);
        if (have_text) sqlite3_bind_blob(st, 5, digest, 16, SQLITE_STATIC); else sqlite3_bind_null(st, 5);
        rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
        sqlite3_finalize(st);
        id = sqlite3_last_insert_rowid(ix->db);
        if (existing_id) *existing_id = id;
    } else {
        st = ex_db_prepare(ix, "UPDATE files SET kind=?2, size=?3, mtime=?4, hash=?5, indexed=1, ctime=CASE WHEN ?6 > 0 THEN ?6 ELSE ctime END WHERE id=?1");
        if (!st) { if (have_text && !given_text) free(text); return -1; }
        sqlite3_bind_int64(st, 1, id); sqlite3_bind_int(st, 2, kind); sqlite3_bind_int64(st, 3, size); sqlite3_bind_int64(st, 4, mtime);
        sqlite3_bind_int64(st, 6, ctime);
        if (have_text) sqlite3_bind_blob(st, 5, digest, 16, SQLITE_STATIC); else sqlite3_bind_null(st, 5);
        rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
        sqlite3_finalize(st);
    }
    if (rc == 0) {
        if (kind == EX_KIND_MARKDOWN && have_text) rc = ex_store_note(ix, id, text, len);
        else rc = ex_store_clear(ix, id);                 /* not a note, or not UTF-8: nothing to search */
        if (rc == 0) rc = ex_store_names(ix, id, rel, kind);
    }
    if (have_text && !given_text) free(text);
    return rc;
}

static int drop_id(ExIndex *ix, int64_t id) {
    sqlite3_stmt *st;
    int rc = ex_store_clear(ix, id);
    ex_store_clear_names(ix, id);
    st = ex_db_prepare(ix, "DELETE FROM files WHERE id = ?1");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    rc |= run1(st);
    sqlite3_finalize(st);
    return rc ? -1 : 0;
}

typedef struct Old { int64_t id, size, mtime; int indexed; char *path; } Old;

static int load_old(ExIndex *ix, Old **out, size_t *n) {
    sqlite3_stmt *st = ex_db_prepare(ix, "SELECT id, path, size, mtime, indexed FROM files ORDER BY path");
    Old *v = NULL;
    size_t cnt = 0, cap = 0;
    if (!st) return -1;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) {
            size_t nc = cap ? cap * 2 : 1024;
            Old *nv = realloc(v, nc * sizeof *nv);
            if (!nv) { sqlite3_finalize(st); free(v); return -1; }
            v = nv; cap = nc;
        }
        v[cnt].id = sqlite3_column_int64(st, 0);
        v[cnt].path = ex_strdup((const char *)sqlite3_column_text(st, 1));
        v[cnt].size = sqlite3_column_int64(st, 2);
        v[cnt].mtime = sqlite3_column_int64(st, 3);
        v[cnt].indexed = sqlite3_column_int(st, 4);
        cnt++;
    }
    sqlite3_finalize(st);
    *out = v; *n = cnt;
    return 0;
}

int ex_index_sync(ExIndex *w, const char *root, const char *const *excl, size_t nexcl, ExIndexStats *stats, ExIndexProgress progress, void *user) {
    Walk wk;
    ExIndexStats s;
    Old *old = NULL;
    size_t i = 0, j = 0, nold = 0, ops = 0, in_tx = 0;
    int rc = 0;
    memset(&wk, 0, sizeof wk);
    memset(&s, 0, sizeof s);
    wk.root = root; wk.rootlen = strlen(root); wk.excl = excl; wk.nexcl = nexcl;
    if (ex_platform_walk(root, walk_cb, &wk) != 0 || wk.fail) { rc = -1; goto out; }
    qsort(wk.v, wk.n, sizeof *wk.v, cmp_entry);
    s.scanned = wk.n;
    if (load_old(w, &old, &nold) != 0) { rc = -1; goto out; }
    if (ex_db_begin(w) != 0) { rc = -1; goto out; }
    while (i < wk.n || j < nold) {
        int c = j >= nold ? -1 : i >= wk.n ? 1 : strcmp(wk.v[i].rel, old[j].path);
        if (c < 0) {                                                    /* new */
            if (index_one(w, root, wk.v[i].rel, wk.v[i].mtime, wk.v[i].ctime, wk.v[i].size, NULL, NULL, 0) != 0) rc = -1; else s.added++;
            i++;
        } else if (c > 0) {                                             /* gone */
            if (drop_id(w, old[j].id) != 0) rc = -1; else s.removed++;
            j++;
        } else {
            if (old[j].size != wk.v[i].size || old[j].mtime != wk.v[i].mtime || old[j].indexed == 0) {
                if (index_one(w, root, wk.v[i].rel, wk.v[i].mtime, wk.v[i].ctime, wk.v[i].size, &old[j].id, NULL, 0) != 0) rc = -1; else s.updated++;
            } else s.unchanged++;
            i++; j++;
        }
        ops++;
        if (++in_tx >= 200) {                                           /* short transactions: readers and the UI are never held up */
            ex_db_commit(w);
            if (progress) progress(i, wk.n, user);
            if (ex_db_begin(w) != 0) { rc = -1; goto out; }
            in_tx = 0;
        }
    }
    ex_db_commit(w);
    if (progress) progress(wk.n, wk.n, user);
out:
    for (i = 0; i < wk.n; i++) free(wk.v[i].rel);
    free(wk.v);
    for (j = 0; j < nold; j++) free(old[j].path);
    free(old);
    (void)ops;
    if (stats) *stats = s;
    return rc;
}

int ex_index_update_file(ExIndex *w, const char *root, const char *rel) {
    char *abs;
    ExPathInfo pi;
    int64_t id = ex_index_file_id(w, rel);
    int rc;
    size_t n = strlen(root) + strlen(rel) + 2;
    if (hidden(rel)) return 0;
    abs = malloc(n);
    if (!abs) return -1;
    snprintf(abs, n, "%s/%s", root, rel);
    if (ex_platform_path_info(abs, &pi) != 0 || !pi.is_file) { free(abs); return ex_index_remove_file(w, rel); }
    free(abs);
    if (ex_db_begin(w) != 0) return -1;
    rc = index_one(w, root, rel, pi.mtime, pi.ctime, pi.size, id ? &id : NULL, NULL, 0);
    if (rc == 0) ex_db_commit(w); else ex_db_rollback(w);
    return rc;
}

int ex_index_update_text(ExIndex *w, const char *rel, const char *text, size_t len) {
    int64_t id = ex_index_file_id(w, rel);
    int rc;
    if (!is_note_path(rel) || hidden(rel)) return 0;
    if (ex_db_begin(w) != 0) return -1;
    rc = index_one(w, "", rel, 0, 0, (int64_t)len, id ? &id : NULL, text, len);
    if (rc == 0) {
        /* the stored modification time stays 0 so the next sync reads the file once it is saved */
        ex_db_commit(w);
    } else ex_db_rollback(w);
    return rc;
}

int ex_index_remove_file(ExIndex *w, const char *rel) {
    int64_t id = ex_index_file_id(w, rel);
    int rc;
    if (!id) return 0;
    if (ex_db_begin(w) != 0) return -1;
    rc = drop_id(w, id);
    if (rc == 0) ex_db_commit(w); else ex_db_rollback(w);
    return rc;
}

int ex_index_rename_file(ExIndex *w, const char *from, const char *to) {
    sqlite3_stmt *st;
    int64_t id = ex_index_file_id(w, from);
    int rc;
    if (!id) return 0;
    if (ex_db_begin(w) != 0) return -1;
    st = ex_db_prepare(w, "UPDATE files SET path = ?2, kind = ?3 WHERE id = ?1");
    if (!st) { ex_db_rollback(w); return -1; }
    sqlite3_bind_int64(st, 1, id); sqlite3_bind_text(st, 2, to, -1, SQLITE_STATIC); sqlite3_bind_int(st, 3, (int)ex_file_kind(to));
    rc = run1(st);
    sqlite3_finalize(st);
    if (rc == 0) { ex_store_clear_names(w, id); rc = ex_store_names(w, id, to, (int)ex_file_kind(to)); }
    if (rc == 0) ex_db_commit(w); else ex_db_rollback(w);
    return rc;
}

int ex_index_clear(ExIndex *w) {
    int rc;
    if (ex_db_begin(w) != 0) return -1;
    rc = ex_db_exec(w, "DELETE FROM files; DELETE FROM fts; DELETE FROM lines; DELETE FROM sections; DELETE FROM blocks; DELETE FROM tasks");
    if (rc == 0) rc = ex_clear_extra_tables(w);
    if (rc == 0) ex_db_commit(w); else ex_db_rollback(w);
    return rc;
}

/* ---- reading ---- */

static long long scalar_i64(ExIndex *r, const char *sql) {
    sqlite3_stmt *st = ex_db_prepare(r, sql);
    long long v = -1;
    if (!st) return -1;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

long ex_index_file_count(ExIndex *r, int kind) {
    char sql[96];
    if (kind < 0) return (long)scalar_i64(r, "SELECT count(*) FROM files");
    snprintf(sql, sizeof sql, "SELECT count(*) FROM files WHERE kind = %d", kind);
    return (long)scalar_i64(r, sql);
}

int64_t ex_index_file_id(ExIndex *r, const char *rel) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT id FROM files WHERE path = ?1");
    int64_t id = 0;
    if (!st) return 0;
    sqlite3_bind_text(st, 1, rel, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return id;
}

char *ex_index_file_path(ExIndex *r, int64_t id) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT path FROM files WHERE id = ?1");
    char *p = NULL;
    if (!st) return NULL;
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) p = ex_strdup((const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return p;
}

int ex_index_files_under(ExIndex *r, const char *folder, char ***paths, size_t *n) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT path FROM files WHERE substr(path, 1, length(?1) + 1) = ?1 || '/' ORDER BY path");
    char **v = NULL;
    size_t cnt = 0, cap = 0;
    *paths = NULL; *n = 0;
    if (!st) return -1;
    sqlite3_bind_text(st, 1, folder, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 32; char **nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt++] = ex_strdup((const char *)sqlite3_column_text(st, 0));
    }
    sqlite3_finalize(st);
    *paths = v; *n = cnt;
    return 0;
}

long long ex_index_text_index_bytes(ExIndex *r) {
    return scalar_i64(r, "SELECT coalesce(sum(pgsize), 0) FROM dbstat WHERE name LIKE 'fts\\_%' ESCAPE '\\'");
}

long long ex_index_note_text_bytes(ExIndex *r) { return scalar_i64(r, "SELECT coalesce(sum(size), 0) FROM files WHERE kind = 1 AND hash IS NOT NULL"); }

int ex_index_candidates(ExIndex *r, const char *needle, size_t nlen, int64_t **ids, size_t *n, int *scanned) {
    size_t i = 0, chars = 0, cap = 0, count = 0;
    size_t starts[512];
    char *q = NULL, *p;
    sqlite3_stmt *st;
    int64_t *v = NULL;
    *ids = NULL; *n = 0;
    if (scanned) *scanned = 0;
    /* the character boundaries of the needle */
    while (i < nlen && chars < 511) { starts[chars++] = i; i++; while (i < nlen && ((unsigned char)needle[i] & 0xC0) == 0x80) i++; }
    starts[chars] = i;
    if (i < nlen) { /* a very long needle: the first 511 characters are enough to narrow it down */ }
    if (chars < 3) {
        st = ex_db_prepare(r, "SELECT id FROM files WHERE kind = 1 AND hash IS NOT NULL ORDER BY id");
        if (scanned) *scanned = 1;
    } else {
        size_t t, ntri = chars - 2 > 64 ? 64 : chars - 2;
        char *buf = malloc(ntri * (3 * 8 + 12) + 8);
        char sql[96 + 1];
        (void)sql;
        if (!buf) return -1;
        p = buf; *p = 0;
        for (t = 0; t < ntri; t++) {
            size_t a = starts[t], b = starts[t + 3], k;
            if (t) { memcpy(p, " AND ", 5); p += 5; }
            *p++ = '"';
            for (k = a; k < b; k++) { if (needle[k] == '"') *p++ = '"'; *p++ = needle[k]; }
            *p++ = '"';
        }
        *p = 0;
        q = buf;
        st = ex_db_prepare(r, "SELECT rowid FROM fts WHERE fts MATCH ?1 ORDER BY rowid");
        if (st) sqlite3_bind_text(st, 1, q, -1, SQLITE_STATIC);
    }
    if (!st) { free(q); return -1; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (count == cap) { size_t nc = cap ? cap * 2 : 64; int64_t *nv = realloc(v, nc * sizeof *nv); if (!nv) { free(v); sqlite3_finalize(st); free(q); return -1; } v = nv; cap = nc; }
        v[count++] = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    free(q);
    *ids = v; *n = count;
    return 0;
}


/* ---- digest and lines ---- */

typedef struct Acc { char *v; size_t n, cap; } Acc;
static void acc_add(Acc *a, const void *p, size_t n) {
    if (!n) return;
    if (a->n + n + 1 > a->cap) { size_t nc = (a->n + n + 1) * 2; char *nv = realloc(a->v, nc); if (!nv) return; a->v = nv; a->cap = nc; }
    memcpy(a->v + a->n, p, n);
    a->n += n;
}

static void digest_query(ExIndex *r, Acc *a, const char *sql, int skip_col) {
    sqlite3_stmt *st = ex_db_prepare(r, sql);
    if (!st) { acc_add(a, "?", 1); return; }
    acc_add(a, sql, strlen(sql));
    while (sqlite3_step(st) == SQLITE_ROW) {
        int c, nc = sqlite3_column_count(st);
        for (c = 0; c < nc; c++) {
            const void *p;
            int n;
            if (c == skip_col) continue;
            p = sqlite3_column_blob(st, c);
            n = sqlite3_column_bytes(st, c);
            acc_add(a, "|", 1);
            if (sqlite3_column_type(st, c) == SQLITE_NULL) acc_add(a, "\0", 1); else acc_add(a, p, (size_t)n);
        }
        acc_add(a, "\n", 1);
    }
    sqlite3_finalize(st);
}

char *ex_index_digest(ExIndex *r) {
    Acc a = {0};
    uint8_t h[32];
    char *hex = malloc(65);
    size_t i;
    if (!hex) return NULL;
    digest_query(r, &a, "SELECT path, kind, size, hex(hash), indexed FROM files ORDER BY path", -1);
    for (i = 0; ex_digest_queries[i]; i++) digest_query(r, &a, ex_digest_queries[i], -1);
    ex_sha256(a.v ? a.v : "", a.n, h);
    ex_hex(h, 32, hex);
    free(a.v);
    return hex;
}

int ex_index_line_count(ExIndex *r, int64_t id, uint32_t *count) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT count FROM lines WHERE file_id = ?1");
    int rc = -1;
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) { *count = (uint32_t)sqlite3_column_int64(st, 0); rc = 0; }
    sqlite3_finalize(st);
    return rc;
}

int ex_index_line_start(ExIndex *r, int64_t id, uint32_t line, size_t *start) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT count, offsets FROM lines WHERE file_id = ?1");
    int rc = -1;
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        uint32_t count = (uint32_t)sqlite3_column_int64(st, 0), cur = 0, k;
        const unsigned char *p = sqlite3_column_blob(st, 1), *end = p + sqlite3_column_bytes(st, 1);
        if (line >= 1 && line <= count) {
            for (k = 0; k < line && p < end; k++) {
                uint32_t v = 0; int sh = 0;
                while (p < end) { unsigned char b = *p++; v |= (uint32_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
                cur += v;
            }
            *start = cur; rc = 0;
        }
    }
    sqlite3_finalize(st);
    return rc;
}

/* ---- the indexer thread ---- */

struct ExIndexer {
    ExWorker *w;
    char *index_path, *root;
    char **excl;
    size_t nexcl;
    ExIndex *ix;                       /* the writer: only the worker thread touches it */
    uint32_t id;
};

enum { JOB_SYNC = 1, JOB_FILE, JOB_REMOVE, JOB_REBUILD };

static void send_progress(size_t done, size_t total, void *user) {
    ExWorker *self = user;
    ExIndexProgressMsg *m = malloc(sizeof *m);
    if (!m) return;
    m->done = done; m->total = total;
    ex_worker_send(self, EX_MSG_INDEX_PROGRESS, m, sizeof *m, free);
}

static int ensure_open(ExIndexer *x) {
    if (x->ix) return 0;
    x->ix = ex_index_open(x->index_path, 1, NULL, 0);
    return x->ix ? 0 : -1;
}

static void do_sync(ExIndexer *x, ExWorker *self) {
    ExIndexDoneMsg *d = calloc(1, sizeof *d);
    if (!d) return;
    d->ok = ensure_open(x) == 0 && ex_index_sync(x->ix, x->root, (const char *const *)x->excl, x->nexcl, &d->stats, send_progress, self) == 0;
    ex_worker_send(self, EX_MSG_INDEX_DONE, d, sizeof *d, free);
}

static void indexer_job(ExWorker *self, const ExMessage *job, void *user) {
    ExIndexer *x = user;
    switch (job->type) {
    case JOB_SYNC: do_sync(x, self); break;
    case JOB_REBUILD: if (ensure_open(x) == 0) ex_index_clear(x->ix); do_sync(x, self); break;
    case JOB_FILE: if (ensure_open(x) == 0 && job->payload) ex_index_update_file(x->ix, x->root, job->payload); break;
    case JOB_REMOVE: if (ensure_open(x) == 0 && job->payload) ex_index_remove_file(x->ix, job->payload); break;
    default: break;
    }
}

ExIndexer *ex_indexer_start(const char *index_path, const char *root, const char *const *excl, size_t nexcl, uint32_t worker_id, ExQueue *results) {
    ExIndexer *x = calloc(1, sizeof *x);
    size_t i;
    if (!x) return NULL;
    x->index_path = ex_strdup(index_path);
    x->root = ex_strdup(root);
    x->id = worker_id;
    if (nexcl) x->excl = calloc(nexcl, sizeof *x->excl);
    if (!x->index_path || !x->root || (nexcl && !x->excl)) goto fail;
    for (i = 0; i < nexcl; i++) { x->excl[i] = ex_strdup(excl[i]); if (!x->excl[i]) goto fail; x->nexcl++; }
    x->w = ex_worker_start("indexer", worker_id, indexer_job, x, results);
    if (!x->w) goto fail;
    return x;
fail:
    for (i = 0; i < x->nexcl; i++) free(x->excl[i]);
    free(x->excl); free(x->index_path); free(x->root); free(x);
    return NULL;
}

static void post(ExIndexer *x, uint32_t type, const char *rel) {
    ExMessage m;
    memset(&m, 0, sizeof m);
    m.type = type;
    if (rel) { m.payload = ex_strdup(rel); m.size = strlen(rel) + 1; m.free_payload = free; if (!m.payload) return; }
    ex_worker_post(x->w, &m);
}

void ex_indexer_sync(ExIndexer *x) { post(x, JOB_SYNC, NULL); }
void ex_indexer_rebuild(ExIndexer *x) { post(x, JOB_REBUILD, NULL); }
void ex_indexer_file_changed(ExIndexer *x, const char *rel) { post(x, JOB_FILE, rel); }
void ex_indexer_file_removed(ExIndexer *x, const char *rel) { post(x, JOB_REMOVE, rel); }
const char *ex_indexer_path(const ExIndexer *x) { return x->index_path; }

void ex_indexer_stop(ExIndexer *x) {
    size_t i;
    if (!x) return;
    ex_worker_stop(x->w);
    ex_index_close(x->ix);
    for (i = 0; i < x->nexcl; i++) free(x->excl[i]);
    free(x->excl); free(x->index_path); free(x->root); free(x);
}
