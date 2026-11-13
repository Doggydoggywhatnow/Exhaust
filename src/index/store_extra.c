/* What the index stores about a note besides its text index, lines, sections, blocks and tasks: names, aliases and links
 * (4a-ii); tags and properties follow in 4a-iii. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_nfc.h"
#include "exhaust_vault.h"
#include "store_internal.h"

/* The key of the last part of a link target, without ".md". */
char *ex_link_name_key(const char *target) {
    const char *slash = strrchr(target, '/'), *base = slash ? slash + 1 : target;
    size_t n = strlen(base);
    if (n > 3 && !strcmp(base + n - 3, ".md")) n -= 3;
    return ex_name_key(base, n);
}

char *ex_link_target_key(const char *target) {
    size_t n = strlen(target);
    if (n > 3 && !strcmp(target + n - 3, ".md")) n -= 3;
    return ex_name_key(target, n);
}

static int run(sqlite3_stmt *st) { int rc = sqlite3_step(st); sqlite3_reset(st); sqlite3_clear_bindings(st); return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1; }

static int insert_name(ExIndex *ix, int64_t id, int kind, const char *text, size_t n) {
    sqlite3_stmt *st = ex_db_prepare(ix, "INSERT OR IGNORE INTO names(file_id, kind, key, text) VALUES(?1,?2,?3,?4)");
    char *key = ex_name_key(text, n);
    int rc;
    if (!st || !key) { sqlite3_finalize(st); free(key); return -1; }
    sqlite3_bind_int64(st, 1, id); sqlite3_bind_int(st, 2, kind); sqlite3_bind_text(st, 3, key, -1, SQLITE_STATIC); sqlite3_bind_text(st, 4, text, (int)n, SQLITE_STATIC);
    rc = run(st);
    sqlite3_finalize(st);
    free(key);
    return rc;
}

/* The file name, and the path, as link keys. A note is known without ".md", every other file with its extension. */
int ex_store_names(ExIndex *ix, int64_t id, const char *rel, int kind) {
    const char *slash = strrchr(rel, '/'), *base = slash ? slash + 1 : rel;
    size_t bn = strlen(base), pn = strlen(rel);
    int rc = 0;
    if (kind == EX_KIND_MARKDOWN && bn > 3 && !strcmp(base + bn - 3, ".md")) { bn -= 3; pn -= 3; }
    rc |= insert_name(ix, id, 0, base, bn);
    rc |= insert_name(ix, id, 1, rel, pn);
    return rc ? -1 : 0;
}

static int delete_names(ExIndex *ix, int64_t id, const char *where) {
    char sql[120];
    sqlite3_stmt *st;
    int rc;
    snprintf(sql, sizeof sql, "DELETE FROM names WHERE file_id = ?1 AND %s", where);
    st = ex_db_prepare(ix, sql);
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    rc = run(st);
    sqlite3_finalize(st);
    return rc;
}

int ex_store_clear_names(ExIndex *ix, int64_t id) { return delete_names(ix, id, "kind IN (0, 1)"); }

int ex_store_clear_extra(ExIndex *ix, int64_t id) {
    sqlite3_stmt *st = ex_db_prepare(ix, "DELETE FROM links WHERE file_id = ?1");
    int rc;
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    rc = run(st);
    sqlite3_finalize(st);
    rc |= delete_names(ix, id, "kind = 2");
    rc |= ex_store_clear_extra_v3(ix, id);
    return rc ? -1 : 0;
}

int ex_store_extra(ExIndex *ix, int64_t id, const ExExtract *x) {
    sqlite3_stmt *st = ex_db_prepare(ix, "INSERT INTO links(file_id, ord, kind, target, target_key, name_key, heading, block, alias, start, end_, line) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
    size_t i;
    int rc = 0;
    if (!st) return -1;
    for (i = 0; i < x->nlinks; i++) {
        const ExXLink *l = &x->links[i];
        char *tk = ex_link_target_key(l->target), *nk = ex_link_name_key(l->target);
        if (!tk || !nk) { free(tk); free(nk); rc = -1; break; }
        sqlite3_bind_int64(st, 1, id); sqlite3_bind_int64(st, 2, (sqlite3_int64)i); sqlite3_bind_int(st, 3, l->kind);
        sqlite3_bind_text(st, 4, l->target, -1, SQLITE_STATIC); sqlite3_bind_text(st, 5, tk, -1, SQLITE_STATIC); sqlite3_bind_text(st, 6, nk, -1, SQLITE_STATIC);
        if (l->heading) sqlite3_bind_text(st, 7, l->heading, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 7);
        if (l->block) sqlite3_bind_text(st, 8, l->block, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 8);
        if (l->alias) sqlite3_bind_text(st, 9, l->alias, -1, SQLITE_STATIC); else sqlite3_bind_null(st, 9);
        sqlite3_bind_int64(st, 10, (sqlite3_int64)l->start); sqlite3_bind_int64(st, 11, (sqlite3_int64)l->end); sqlite3_bind_int(st, 12, (int)l->line);
        rc |= run(st);
        free(tk); free(nk);
    }
    sqlite3_finalize(st);
    /* aliases: the list of the property "aliases" (or "alias") */
    for (i = 0; i < x->nprops; i++) {
        const ExXProp *p = &x->props[i];
        if (strcmp(p->name, "aliases") && strcmp(p->name, "alias")) continue;
        {
            const char *v = p->value;
            size_t a = 0, n = strlen(v);
            int comma_text = p->type != EX_PROP_LIST;       /* a text is a list separated by commas */
            while (a <= n) {
                size_t b = a, s, e;
                while (b < n && v[b] != '\n' && !(comma_text && v[b] == ',')) b++;
                s = a; e = b;
                while (s < e && v[s] == ' ') s++;
                while (e > s && v[e - 1] == ' ') e--;
                if (e > s) rc |= insert_name(ix, id, 2, v + s, e - s);
                a = b + 1;
            }
        }
    }
    rc |= ex_store_extra_v3(ix, id, x);
    return rc ? -1 : 0;
}

int ex_clear_extra_tables(ExIndex *ix) {
    return ex_db_exec(ix, "DELETE FROM names; DELETE FROM links") == 0 && ex_clear_extra_tables_v3(ix) == 0 ? 0 : -1;
}

const char *const ex_digest_queries[] = {
    "SELECT f.path, t.count, hex(t.offsets) FROM lines t JOIN files f ON f.id = t.file_id ORDER BY f.path",
    "SELECT f.path, t.ord, t.level, t.title, t.start, t.end_, t.section_end, t.line FROM sections t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.ord",
    "SELECT f.path, t.block_id, t.start, t.end_, t.line FROM blocks t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.start",
    "SELECT f.path, t.start, t.end_, t.line, t.state, t.text FROM tasks t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.start",
    "SELECT f.path, t.kind, t.key, t.text FROM names t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.kind, t.key",
    "SELECT f.path, t.ord, t.start, t.tag, t.tag_key, t.line, t.in_props FROM tags t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.ord",
    "SELECT f.path, t.ord, t.name, t.name_key, t.type, t.value, t.start, t.end_, t.line FROM props t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.ord",
    "SELECT f.path, t.ord, t.kind, t.target, t.target_key, t.name_key, t.heading, t.block, t.alias, t.start, t.end_, t.line FROM links t JOIN files f ON f.id = t.file_id ORDER BY f.path, t.ord",
    NULL
};
