#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_find.h"
#include "exhaust_links.h"
#include "exhaust_markdown.h"
#include "exhaust_nfc.h"
#include "store_internal.h"

typedef struct Cand { int64_t id; char *path; } Cand;

static int depth_of(const char *p) { int d = 0; for (; *p; p++) if (*p == '/') d++; return d; }

static size_t dir_len(const char *p) { const char *s = strrchr(p, '/'); return s ? (size_t)(s - p) + 1 : 0; }

static int better(const char *from, const Cand *a, const Cand *b) {
    size_t fd = from ? dir_len(from) : 0;
    int ain = from && dir_len(a->path) == fd && !strncmp(a->path, from, fd), bin = from && dir_len(b->path) == fd && !strncmp(b->path, from, fd);
    if (ain != bin) return ain;
    if (depth_of(a->path) != depth_of(b->path)) return depth_of(a->path) < depth_of(b->path);
    return strcmp(a->path, b->path) < 0;
}

static int64_t best_of(sqlite3_stmt *st, const char *from) {
    Cand best = {0, NULL};
    while (sqlite3_step(st) == SQLITE_ROW) {
        Cand c;
        c.id = sqlite3_column_int64(st, 0);
        c.path = ex_strdup((const char *)sqlite3_column_text(st, 1));
        if (!c.path) continue;
        if (!best.path || better(from, &c, &best)) { free(best.path); best = c; } else free(c.path);
    }
    sqlite3_finalize(st);
    free(best.path);
    return best.id;
}

/* "a/b/../c" -> "a/c"; NULL if it goes above the vault. */
char *ex_path_normalize(const char *dir, size_t dlen, const char *rel) {
    size_t n = dlen + strlen(rel) + 2, o = 0;
    char *tmp = malloc(n), *out = malloc(n);
    char *seg, *save;
    int ok = 1;
    if (!tmp || !out) { free(tmp); free(out); return NULL; }
    memcpy(tmp, dir, dlen);
    strcpy(tmp + dlen, rel);
    out[0] = 0;
    for (seg = tmp; seg && *seg;) {
        save = strchr(seg, '/');
        if (save) *save++ = 0;
        if (!strcmp(seg, "..")) { if (o == 0) { ok = 0; break; } o--; while (o > 0 && out[o - 1] != '/') o--; out[o] = 0; }
        else if (strcmp(seg, ".") && *seg) { if (o) out[o++] = '/'; strcpy(out + o, seg); o += strlen(seg); }
        seg = save;
    }
    free(tmp);
    if (!ok) { free(out); return NULL; }
    return out;
}

static int64_t by_path(ExIndex *r, const char *path) {
    int64_t id = ex_index_file_id(r, path);
    if (!id) {
        size_t n = strlen(path) + 4;
        char *withmd = malloc(n);
        if (!withmd) return 0;
        snprintf(withmd, n, "%s.md", path);
        id = ex_index_file_id(r, withmd);
        free(withmd);
    }
    return id;
}

int64_t ex_link_resolve(ExIndex *r, const char *from, const char *target, int link_kind) {
    char *key;
    sqlite3_stmt *st;
    int64_t id = 0;
    if (!target[0]) return from ? ex_index_file_id(r, from) : 0;
    if ((link_kind == EX_LINK_MD || link_kind == EX_LINK_MD_EMBED) && from) {
        char *p = ex_path_normalize(from, dir_len(from), target);
        if (p) { id = by_path(r, p); free(p); }
        if (id) return id;
        p = ex_path_normalize("", 0, target);
        if (p) { id = by_path(r, p); free(p); }
        if (id) return id;
    }
    key = ex_link_target_key(target);
    if (!key) return 0;
    if (strchr(target, '/')) {
        st = ex_db_prepare(r, "SELECT f.id, f.path FROM names n JOIN files f ON f.id = n.file_id WHERE n.kind = 1 AND (n.key = ?1 OR substr(n.key, -(length(?1) + 1)) = '/' || ?1)");
        if (st) { sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC); id = best_of(st, from); }
    } else {
        st = ex_db_prepare(r, "SELECT f.id, f.path FROM names n JOIN files f ON f.id = n.file_id WHERE n.kind = 0 AND n.key = ?1");
        if (st) { sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC); id = best_of(st, from); }
        if (!id) {
            st = ex_db_prepare(r, "SELECT f.id, f.path FROM names n JOIN files f ON f.id = n.file_id WHERE n.kind = 2 AND n.key = ?1");
            if (st) { sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC); id = best_of(st, from); }
        }
    }
    free(key);
    return id;
}

static void item_fields_free(ExLinkItem *it) { free(it->source); free(it->target); free(it->heading); free(it->block); free(it->alias); free(it->dest); }

void ex_link_items_free(ExLinkItem *items, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) item_fields_free(&items[i]);
    free(items);
}

static char *dupcol(sqlite3_stmt *st, int c) { const char *t = (const char *)sqlite3_column_text(st, c); return t ? ex_strdup(t) : NULL; }

/* The columns of a links row as an item; select them in this order: source path, kind, target, heading, block, alias, start, end_, line. */
static const char LINK_COLS[] = "f.path, l.kind, l.target, l.heading, l.block, l.alias, l.start, l.end_, l.line";

static int fill_item(ExIndex *r, sqlite3_stmt *st, ExLinkItem *it) {
    int64_t id;
    char *dest;
    memset(it, 0, sizeof *it);
    it->source = dupcol(st, 0);
    it->kind = sqlite3_column_int(st, 1);
    it->target = dupcol(st, 2);
    it->heading = dupcol(st, 3);
    it->block = dupcol(st, 4);
    it->alias = dupcol(st, 5);
    it->start = (size_t)sqlite3_column_int64(st, 6);
    it->end = (size_t)sqlite3_column_int64(st, 7);
    it->line = (uint32_t)sqlite3_column_int(st, 8);
    if (!it->source || !it->target) return -1;
    id = ex_link_resolve(r, it->source, it->target, it->kind);
    dest = id ? ex_index_file_path(r, id) : NULL;
    it->dest = dest;
    return 0;
}

static int collect(ExIndex *r, sqlite3_stmt *st, ExLinkItem **out, size_t *n, int64_t only_dest) {
    ExLinkItem *v = NULL;
    size_t cnt = 0, cap = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        ExLinkItem it;
        if (fill_item(r, st, &it) != 0) { item_fields_free(&it); continue; }
        if (only_dest) {
            int64_t did = it.dest ? ex_index_file_id(r, it.dest) : 0;
            if (did != only_dest) { item_fields_free(&it); continue; }
        }
        if (cnt == cap) { size_t nc = cap ? cap * 2 : 16; ExLinkItem *nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
        v[cnt++] = it;
    }
    sqlite3_finalize(st);
    *out = v; *n = cnt;
    return 0;
}

int ex_links_outgoing(ExIndex *r, const char *path, ExLinkItem **out, size_t *n) {
    char sql[300];
    sqlite3_stmt *st;
    *out = NULL; *n = 0;
    snprintf(sql, sizeof sql, "SELECT %s FROM links l JOIN files f ON f.id = l.file_id WHERE f.path = ?1 ORDER BY l.ord", LINK_COLS);
    st = ex_db_prepare(r, sql);
    if (!st) return -1;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    return collect(r, st, out, n, 0);
}

int ex_links_backlinks(ExIndex *r, const char *path, ExLinkItem **out, size_t *n) {
    char sql[500];
    sqlite3_stmt *st;
    int64_t id = ex_index_file_id(r, path);
    *out = NULL; *n = 0;
    if (!id) return 0;
    snprintf(sql, sizeof sql, "SELECT %s FROM links l JOIN files f ON f.id = l.file_id WHERE l.file_id != ?1 AND l.name_key IN (SELECT key FROM names WHERE file_id = ?1 AND kind IN (0, 2)) ORDER BY f.path, l.ord", LINK_COLS);
    st = ex_db_prepare(r, sql);
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    return collect(r, st, out, n, id);
}

void ex_mentions_free(ExMention *m, size_t n) { size_t i; for (i = 0; i < n; i++) free(m[i].source); free(m); }

/* The titles to look for: the name of the note and its aliases. */
static int titles_of(ExIndex *r, int64_t id, char ***out, size_t *n) {
    sqlite3_stmt *st = ex_db_prepare(r, "SELECT text FROM names WHERE file_id = ?1 AND kind IN (0, 2) ORDER BY kind, key");
    char **v = NULL;
    size_t cnt = 0;
    *out = NULL; *n = 0;
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        char **nv = realloc(v, (cnt + 1) * sizeof *v);
        if (!nv) break;
        v = nv;
        v[cnt++] = dupcol(st, 0);
    }
    sqlite3_finalize(st);
    *out = v; *n = cnt;
    return 0;
}

/* Where a name is not a mention: inside links (also web links), code, math, HTML, comments and the frontmatter. */
typedef struct Ranges { size_t *v, n, cap; } Ranges;

static int range_visit(const ExMdSpan *sp, void *user) {
    Ranges *r = user;
    switch (sp->kind) {
    case EX_MD_LINK: case EX_MD_IMAGE: case EX_MD_WIKILINK: case EX_MD_EMBED: case EX_MD_AUTOLINK: case EX_MD_CODE_SPAN: case EX_MD_CODE_BLOCK:
    case EX_MD_MATH_BLOCK: case EX_MD_MATH_INLINE: case EX_MD_HTML_BLOCK: case EX_MD_HTML_INLINE: case EX_MD_COMMENT_BLOCK: case EX_MD_COMMENT_INLINE: case EX_MD_FRONTMATTER:
        if (r->n == r->cap) { size_t nc = r->cap ? r->cap * 2 : 32; size_t *nv = realloc(r->v, nc * 2 * sizeof *nv); if (!nv) return 1; r->v = nv; r->cap = nc; }
        r->v[r->n * 2] = sp->start; r->v[r->n * 2 + 1] = sp->end; r->n++;
        break;
    default: break;
    }
    return 0;
}

static void ranges_of(const char *text, size_t len, Ranges *r) {
    ExMd *md = ex_md_create();
    if (!md) return;
    ex_md_tokenize(md, text, len);
    ex_md_visit(md, 0, len, range_visit, r);
    ex_md_free(md);
}

static int utf8_chars(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }

int ex_links_unlinked(ExIndex *r, const char *root, const char *path, ExMention **out, size_t *n) {
    int64_t id = ex_index_file_id(r, path);
    char **titles = NULL;
    size_t nt = 0, ti, cnt = 0, cap = 0;
    ExMention *v = NULL;
    *out = NULL; *n = 0;
    if (!id || titles_of(r, id, &titles, &nt) != 0) return id ? -1 : 0;
    for (ti = 0; ti < nt; ti++) {
        int64_t *ids = NULL;
        size_t nids = 0, ci;
        ExFindOptions opt;
        ExFind *f;
        if (!titles[ti] || utf8_chars(titles[ti]) < 3) continue;
        memset(&opt, 0, sizeof opt);
        opt.whole_word = 1;
        f = ex_find_compile(titles[ti], strlen(titles[ti]), &opt, NULL, 0);
        if (!f) continue;
        if (ex_index_candidates(r, titles[ti], strlen(titles[ti]), &ids, &nids, NULL) != 0) { ex_find_free(f); continue; }
        for (ci = 0; ci < nids; ci++) {
            char *rel, *text;
            size_t len = 0, pos = 0, ms, me;
            Ranges rg = {0};
            size_t k;
            if (ids[ci] == id) continue;
            rel = ex_index_file_path(r, ids[ci]);
            if (!rel) continue;
            text = ex_index_read_note(root, rel, &len);
            if (text) ranges_of(text, len, &rg);
            while (text && ex_find_next(f, text, len, pos, &ms, &me) == EX_FIND_MATCH) {
                int inside = 0;
                pos = me > ms ? me : ms + 1;
                for (k = 0; k < rg.n; k++) if (ms >= rg.v[k * 2] && ms < rg.v[k * 2 + 1]) { inside = 1; break; }
                if (inside) continue;
                if (cnt == cap) { size_t nc = cap ? cap * 2 : 16; ExMention *nv = realloc(v, nc * sizeof *nv); if (!nv) break; v = nv; cap = nc; }
                v[cnt].source = ex_strdup(rel);
                v[cnt].start = ms; v[cnt].end = me;
                { uint32_t line = 1; size_t q; for (q = 0; q < ms; q++) if (text[q] == '\n') line++; v[cnt].line = line; }
                cnt++;
            }
            free(text); free(rg.v); free(rel);
        }
        free(ids);
        ex_find_free(f);
    }
    for (ti = 0; ti < nt; ti++) free(titles[ti]);
    free(titles);
    *out = v; *n = cnt;
    return 0;
}

char *ex_links_line_text(const char *root, const char *path, uint32_t line) {
    size_t len = 0, i = 0, start;
    uint32_t cur = 1;
    char *text = ex_index_read_note(root, path, &len), *out;
    if (!text) return NULL;
    while (i < len && cur < line) { if (text[i] == '\n') cur++; i++; }
    start = i;
    while (i < len && text[i] != '\n') i++;
    out = malloc(i - start + 1);
    if (out) { memcpy(out, text + start, i - start); out[i - start] = 0; }
    free(text);
    return out;
}

char *ex_link_preview(ExIndex *r, const char *root, const char *path, const char *heading, const char *block, size_t max) {
    int64_t id = ex_index_file_id(r, path);
    size_t from = 0, to = (size_t)-1, len = 0;
    char *text, *out;
    if (!id) return NULL;
    if (heading && heading[0]) {
        sqlite3_stmt *st = ex_db_prepare(r, "SELECT title, start, section_end FROM sections WHERE file_id = ?1 ORDER BY ord");
        char *want = ex_name_key(heading, strlen(heading));
        int found = 0;
        if (!st || !want) { sqlite3_finalize(st); free(want); return NULL; }
        sqlite3_bind_int64(st, 1, id);
        while (!found && sqlite3_step(st) == SQLITE_ROW) {
            const char *t = (const char *)sqlite3_column_text(st, 0);
            char *k = ex_name_key(t, strlen(t));
            if (k && !strcmp(k, want)) { found = 1; from = (size_t)sqlite3_column_int64(st, 1); to = (size_t)sqlite3_column_int64(st, 2); }
            free(k);
        }
        sqlite3_finalize(st);
        free(want);
        if (!found) return NULL;
    } else if (block && block[0]) {
        sqlite3_stmt *st = ex_db_prepare(r, "SELECT start, end_ FROM blocks WHERE file_id = ?1 AND block_id = ?2");
        int found = 0;
        if (!st) return NULL;
        sqlite3_bind_int64(st, 1, id); sqlite3_bind_text(st, 2, block, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) { found = 1; from = (size_t)sqlite3_column_int64(st, 0); to = (size_t)sqlite3_column_int64(st, 1); }
        sqlite3_finalize(st);
        if (!found) return NULL;
    }
    text = ex_index_read_note(root, path, &len);
    if (!text) return NULL;
    if (to > len) to = len;
    if (from > to) from = to;
    if (to - from > max) {                                   /* cut at a line break when there is one */
        size_t cut = from + max;
        while (cut > from + max / 2 && text[cut] != '\n') cut--;
        to = text[cut] == '\n' ? cut : from + max;
    }
    out = malloc(to - from + 1);
    if (out) { memcpy(out, text + from, to - from); out[to - from] = 0; }
    free(text);
    return out;
}
