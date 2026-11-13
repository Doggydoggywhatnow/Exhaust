#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_links.h"
#include "exhaust_nfc.h"
#include "exhaust_rename.h"
#include "exhaust_vault.h"
#include "store_internal.h"

typedef struct Edit { size_t from, to; char *text; } Edit;
typedef struct Edits { Edit *v; size_t n, cap; } Edits;

static int edit_add(Edits *e, size_t from, size_t to, char *text) {
    if (e->n == e->cap) { size_t nc = e->cap ? e->cap * 2 : 8; Edit *nv = realloc(e->v, nc * sizeof *nv); if (!nv) { free(text); return -1; } e->v = nv; e->cap = nc; }
    e->v[e->n].from = from; e->v[e->n].to = to; e->v[e->n].text = text; e->n++;
    return 0;
}
static void edits_free(Edits *e) { size_t i; for (i = 0; i < e->n; i++) free(e->v[i].text); free(e->v); }

typedef struct Moved { int64_t *ids; char **from, **to; size_t n; } Moved;

static int moved_index(const Moved *m, int64_t id) { size_t i; for (i = 0; i < m->n; i++) if (m->ids[i] == id) return (int)i; return -1; }

static const char *base_of(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static int ends_md(const char *s) { size_t n = strlen(s); return n > 3 && !strcmp(s + n - 3, ".md"); }
static size_t dir_len(const char *p) { const char *s = strrchr(p, '/'); return s ? (size_t)(s - p) + 1 : 0; }

/* Is another file (not one that moves) already known by this name key? Then a bare name is not enough. */
static int name_taken(ExIndex *r, const Moved *m, const char *name_key) {
    char sql[1500], ids[1200] = "0";
    size_t i, o = 1;
    sqlite3_stmt *st;
    int taken = 0;
    for (i = 0; i < m->n && o < sizeof ids - 24; i++) o += (size_t)snprintf(ids + o, sizeof ids - o, ",%lld", (long long)m->ids[i]);
    snprintf(sql, sizeof sql, "SELECT count(*) FROM names WHERE kind IN (0, 2) AND key = ?1 AND file_id NOT IN (%s)", ids);
    st = ex_db_prepare(r, sql);
    if (!st) return 1;
    sqlite3_bind_text(st, 1, name_key, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) taken = sqlite3_column_int(st, 0) > 0;
    sqlite3_finalize(st);
    return taken;
}

static int path_is(const char *p, const char *target_path) {
    return p && (!strcmp(p, target_path) || (ends_md(target_path) && strlen(p) + 3 == strlen(target_path) && !strncmp(p, target_path, strlen(p))));
}

static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }

/* "a/b/c.md" seen from the folder "a/x/" is "../b/c.md". */
static char *relative_path(const char *from_dir, size_t dlen, const char *to) {
    size_t common = 0, i, ups = 0;
    char *out;
    for (i = 0; i < dlen; i++) if (from_dir[i] == '/') {
        size_t seg_end = i + 1;
        if (seg_end <= strlen(to) && !strncmp(from_dir, to, seg_end)) common = seg_end; else break;
    }
    for (i = common; i < dlen; i++) if (from_dir[i] == '/') ups++;
    out = malloc(ups * 3 + strlen(to) - common + 1);
    if (!out) return NULL;
    out[0] = 0;
    for (i = 0; i < ups; i++) strcat(out, "../");
    strcat(out, to + common);
    return out;
}

/* Percent-encoding for a Markdown link destination: what would end the destination, and "%". Non-ASCII bytes are encoded
 * only when the old destination was (they are written the way the note already writes them). */
static char *url_encode(const char *s, int encode_non_ascii) {
    size_t n = strlen(s), i, o = 0;
    char *out = malloc(n * 3 + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '%' || c == '(' || c == ')' || c == '<' || c == '>' || c == '#' || c == '?' || c < 0x20 || (c >= 0x80 && encode_non_ascii)) o += (size_t)sprintf(out + o, "%%%02X", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
    return out;
}

/* The bytes of the part of a link that names the target: [*ts, *te) in text. */
static int target_range(const char *text, const ExXLink *l, size_t *ts, size_t *te) {
    size_t i;
    if (l->kind == EX_LINK_WIKI || l->kind == EX_LINK_EMBED || l->kind == EX_LINK_PROP) {
        size_t s = l->start + (text[l->start] == '!' ? 3 : 2), e = s;
        if (l->end < s + 2) return -1;
        while (e < l->end - 2 && text[e] != '#' && text[e] != '|') e++;
        if (e > s && text[e - 1] == '\\' && e < l->end - 2 && text[e] == '|') e--;            /* [[a\|b]] in a table */
        *ts = s; *te = e;
        return 0;
    } else {
        int depth = 0;
        size_t ds, de, par = 0;
        for (i = l->start + (text[l->start] == '!' ? 1 : 0); i + 1 < l->end; i++) {
            if (text[i] == '\\') { i++; continue; }
            if (text[i] == '[') depth++;
            else if (text[i] == ']') { depth--; if (depth == 0 && text[i + 1] == '(') break; }
        }
        if (i + 1 >= l->end) return -1;
        ds = i + 2;
        while (ds < l->end && (text[ds] == ' ' || text[ds] == '\t')) ds++;
        if (ds < l->end && text[ds] == '<') { ds++; de = ds; while (de < l->end && text[de] != '>') de++; }
        else { de = ds; while (de < l->end) { if (text[de] == '(') par++; else if (text[de] == ')') { if (par == 0) break; par--; } else if (text[de] == ' ' || text[de] == '\t' || text[de] == '\n') break; de++; } }
        { size_t h = ds; while (h < de && text[h] != '#') h++; de = h; }
        *ts = ds; *te = de;
        return 0;
    }
}

static int collect_edits(ExIndex *r, const Moved *m, const char *s_old, const char *s_new, const char *text, size_t len, Edits *ed, size_t *nlinks) {
    ExExtract *x = ex_extract(text, len);
    size_t i;
    int rc = 0;
    if (!x) return -1;
    for (i = 0; i < x->nlinks && rc == 0; i++) {
        const ExXLink *l = &x->links[i];
        int64_t tid;
        int mi;
        const char *t_old, *t_new;
        char *new_target = NULL;
        size_t ts, te;
        int is_md = l->kind == EX_LINK_MD || l->kind == EX_LINK_MD_EMBED;
        int s_moved = strcmp(s_old, s_new) != 0;
        if (!l->target[0]) continue;
        tid = ex_link_resolve(r, s_old, l->target, l->kind);
        if (!tid) continue;
        mi = moved_index(m, tid);
        if (mi < 0 && !(is_md && s_moved)) continue;                 /* neither end moves, or a wikilink (named, not placed) in a note that moves */
        {
            char *tp = mi >= 0 ? NULL : ex_index_file_path(r, tid);
            t_old = mi >= 0 ? m->from[mi] : tp;
            t_new = mi >= 0 ? m->to[mi] : tp;
            if (!t_old) continue;
            if (is_md) {
                /* which way was it written: from its own folder, from the vault, or by a bare name? */
                char *np = ex_path_normalize(s_old, dir_len(s_old), l->target);
                int relative = path_is(np, t_old), from_root = 0;
                free(np);
                if (!relative) { np = ex_path_normalize("", 0, l->target); from_root = path_is(np, t_old); free(np); }
                {
                    int had_md = ends_md(l->target);
                    char *path_new, *enc;
                    int enc_non_ascii = 0;
                    size_t k;
                    if (relative) path_new = relative_path(s_new, dir_len(s_new), t_new);
                    else if (from_root) path_new = dupn(t_new, strlen(t_new));
                    else { const char *b = base_of(t_new); path_new = dupn(b, strlen(b)); }   /* a bare name */
                    if (!path_new) { rc = -1; free(tp); break; }
                    if (!had_md && ends_md(path_new)) path_new[strlen(path_new) - 3] = 0;
                    if (target_range(text, l, &ts, &te) != 0) { free(path_new); free(tp); continue; }
                    for (k = ts; k + 2 < te; k++) if (text[k] == '%' && (text[k + 1] >= '8' && text[k + 1] <= 'F')) enc_non_ascii = 1;
                    if (ts > 0 && text[ts - 1] == '<') enc = dupn(path_new, strlen(path_new));        /* <a b.md>: spaces need no encoding */
                    else enc = url_encode(path_new, enc_non_ascii);
                    free(path_new);
                    new_target = enc;
                    if (new_target && te - ts == strlen(new_target) && !memcmp(text + ts, new_target, te - ts)) { free(new_target); new_target = NULL; }
                }
            } else {
                /* a wikilink, an embed or a link in a property: written by name or by path */
                char *old_key = ex_link_name_key(l->target), *file_key = ex_link_name_key(t_old);
                int via_alias = old_key && file_key && strcmp(old_key, file_key) != 0;
                free(old_key); free(file_key);
                if (via_alias) { free(tp); continue; }               /* a note known by an alias keeps being found by it */
                {
                    int had_md = ends_md(l->target), note = ends_md(t_new);
                    char *shown = dupn(t_new, strlen(t_new)), *res = NULL;
                    if (!shown) { rc = -1; free(tp); break; }
                    if (note && !had_md) shown[strlen(shown) - 3] = 0;
                    if (!strchr(l->target, '/')) {
                        const char *b = base_of(shown);
                        char *k = ex_name_key(b, strlen(b));
                        int clash = k ? name_taken(r, m, k) : 1;
                        free(k);
                        res = clash ? dupn(shown, strlen(shown)) : dupn(b, strlen(b));
                    } else {
                        size_t segs = 1, i2;
                        const char *p = shown + strlen(shown), *start = shown;
                        for (i2 = 0; l->target[i2]; i2++) if (l->target[i2] == '/') segs++;
                        { size_t have = 1; for (i2 = 0; shown[i2]; i2++) if (shown[i2] == '/') have++;
                          if (segs < have && !path_is(l->target, t_old)) {              /* a path from the vault stays one; a shorter tail stays as short */ size_t skip = have - segs; for (start = shown; skip; start++) if (*start == '/') skip--; } }
                        (void)p;
                        res = dupn(start, strlen(start));
                    }
                    free(shown);
                    if (!res) { rc = -1; free(tp); break; }
                    if (target_range(text, l, &ts, &te) != 0) { free(res); free(tp); continue; }
                    if (te - ts == strlen(res) && !memcmp(text + ts, res, te - ts)) { free(res); res = NULL; }
                    new_target = res;
                }
            }
            free(tp);
        }
        if (new_target) {
            if (edit_add(ed, ts, te, new_target) != 0) rc = -1; else (*nlinks)++;
        }
    }
    ex_extract_free(x);
    return rc;
}

static int cmp_edit(const void *a, const void *b) { size_t x = ((const Edit *)a)->from, y = ((const Edit *)b)->from; return x < y ? -1 : x > y; }

static char *apply_edits(const char *text, size_t len, Edits *ed, size_t *out_len) {
    size_t i, o = 0, at = 0;
    char *out;
    qsort(ed->v, ed->n, sizeof *ed->v, cmp_edit);
    out = malloc(len + 1);
    for (i = 0; i < ed->n && out; i++) {
        size_t add = strlen(ed->v[i].text), keep = ed->v[i].from - at;
        char *nv = realloc(out, o + keep + add + (len - ed->v[i].to) + 1);
        if (!nv) { free(out); return NULL; }
        out = nv;
        memcpy(out + o, text + at, keep); o += keep;
        memcpy(out + o, ed->v[i].text, add); o += add;
        at = ed->v[i].to;
    }
    if (!out) return NULL;
    { char *nv = realloc(out, o + (len - at) + 1); if (!nv) { free(out); return NULL; } out = nv; }
    memcpy(out + o, text + at, len - at); o += len - at;
    out[o] = 0;
    *out_len = o;
    return out;
}

void ex_rename_report_free(ExRenameReport *r) {
    size_t i;
    for (i = 0; i < r->n; i++) free(r->paths[i]);
    free(r->paths); free(r->links);
    memset(r, 0, sizeof *r);
}

static int rename_impl(ExIndex *r, const ExNoteIO *io, const ExMove *moves, size_t n, ExRenameReport *report, int write) {
    Moved m;
    sqlite3_stmt *st;
    char **sources = NULL, **source_old = NULL;
    size_t nsrc = 0, cap = 0, i;
    int rc = 0;
    char sql[4096], ids[1200] = "0";
    size_t o = 1;
    memset(report, 0, sizeof *report);
    memset(&m, 0, sizeof m);
    m.ids = calloc(n ? n : 1, sizeof *m.ids); m.from = calloc(n ? n : 1, sizeof *m.from); m.to = calloc(n ? n : 1, sizeof *m.to);
    if (!m.ids || !m.from || !m.to) { free(m.ids); free(m.from); free(m.to); return -1; }
    for (i = 0; i < n; i++) {
        int64_t id = ex_index_file_id(r, moves[i].from);
        if (!id) continue;
        m.ids[m.n] = id; m.from[m.n] = (char *)moves[i].from; m.to[m.n] = (char *)moves[i].to; m.n++;
        if (o < sizeof ids - 24) o += (size_t)snprintf(ids + o, sizeof ids - o, ",%lld", (long long)id);
    }
    /* sources: the moved notes, and every note with a link whose last part is one of the moved files' names */
    snprintf(sql, sizeof sql,
        "SELECT path FROM files WHERE id IN (%s) AND kind = 1 UNION "
        "SELECT f.path FROM files f WHERE f.kind = 1 AND f.id IN (SELECT l.file_id FROM links l WHERE l.name_key IN (SELECT key FROM names WHERE file_id IN (%s) AND kind IN (0, 2))) ORDER BY 1", ids, ids);
    st = ex_db_prepare(r, sql);
    if (!st) { free(m.ids); free(m.from); free(m.to); return -1; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (nsrc == cap) { size_t nc = cap ? cap * 2 : 16; char **a = realloc(sources, nc * sizeof *a); if (!a) break; sources = a; cap = nc; }
        sources[nsrc++] = ex_strdup((const char *)sqlite3_column_text(st, 0));
    }
    sqlite3_finalize(st);
    source_old = sources;
    for (i = 0; i < nsrc; i++) {
        const char *s_old = source_old[i], *s_new = s_old;
        char *text;
        size_t len = 0, k, nl = 0, olen = 0;
        Edits ed = {0};
        int mi = -1;
        for (k = 0; k < m.n; k++) if (!strcmp(m.from[k], s_old)) { mi = (int)k; break; }
        if (mi >= 0) s_new = m.to[mi];
        text = io->read(io->user, write ? s_new : s_old, &len);
        if (!text && !write) text = io->read(io->user, s_new, &len);
        if (!text) { report->failed++; continue; }
        if (collect_edits(r, &m, s_old, s_new, text, len, &ed, &nl) != 0) { rc = -1; free(text); edits_free(&ed); continue; }
        if (ed.n) {
            if (write) {
                char *out = apply_edits(text, len, &ed, &olen);
                if (!out || io->write(io->user, s_new, out, olen) != 0) report->failed++;
                free(out);
            }
            {
                char **np = realloc(report->paths, (report->n + 1) * sizeof *np);
                size_t *nlk = realloc(report->links, (report->n + 1) * sizeof *nlk);
                if (np) report->paths = np;
                if (nlk) report->links = nlk;
                if (np && nlk) { report->paths[report->n] = ex_strdup(s_new); report->links[report->n] = nl; report->n++; report->total_links += nl; }
            }
        }
        free(text);
        edits_free(&ed);
    }
    for (i = 0; i < nsrc; i++) free(source_old[i]);
    free(sources);
    free(m.ids); free(m.from); free(m.to);
    return rc == 0 && report->failed == 0 ? 0 : -1;
}

int ex_rename_plan(ExIndex *r, const ExNoteIO *io, const ExMove *moves, size_t n, ExRenameReport *report) { return rename_impl(r, io, moves, n, report, 0); }
int ex_rename_links(ExIndex *r, const ExNoteIO *io, const ExMove *moves, size_t n, ExRenameReport *report) { return rename_impl(r, io, moves, n, report, 1); }
