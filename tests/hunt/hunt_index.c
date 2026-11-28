/* hunt_index: the vault index, search and link-safe rename, against models that are slower and simpler (include/exhaust_index.h, _search.h, _links.h, _rename.h).
 *
 * The index is a cache that "must always be rebuildable from the notes", search must find what is there, and rename must keep every link pointing at the
 * same file. A cache that drifts, a search that misses a note or a rename that retargets a link are the bugs that cost a user their trust in the graph.
 *   A. Drift: a random sequence of file actions (new notes, edits, deletes, moves, hidden files, other files, Unicode and case variants of names) is applied to
 *      a vault and told to one index through update_file, remove_file, rename_file and sync. A second index is built from nothing. Their digests must be equal
 *      after every few actions (the digest is over everything stored, by path). `clear` followed by `sync` must give the same digest again.
 *   B. Search: random queries (terms, phrases, OR, minus, groups, file:, path:, content:, tag:, line:, match-case:) are run on a random vault and compared with
 *      an evaluator written directly from the documentation; offsets of the matches must point at the matched text in the note as the editor holds it (no BOM, LF
 *      for a CRLF file); a query that does not parse must say why.
 *   C. Rename: a vault of notes that link to each other in every way is indexed, one note is moved or renamed, links are rewritten, the index is read
 *      again, and every link must lead to the same file as before (the same file under its new path), the links must be the same links (kind, heading,
 *      block, alias), and nothing outside the links of a note may change.
 * Link: exhaust_index exhaust_platform. */
#include "hunt.h"
#include "hunt_fs.h"

#include <ctype.h>

#include "exhaust_index.h"
#include "exhaust_links.h"
#include "exhaust_rename.h"
#include "exhaust_search.h"

static ExIndex *open_index(const char *dir, const char *name) {
    char path[800], err[200];
    ExIndex *w;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    ex_index_delete_files(path);
    w = ex_index_open(path, 1, err, sizeof err);
    if (!w) { fprintf(stderr, "HUNT cannot open the index %s: %s\n", path, err); exit(2); }
    return w;
}

/* ---- A. drift ---- */

static const char *const NAMES[] = { "Alpha", "Beta", "Gamma", "Delta", "alpha", "ALPHA", "\xC3\x89psilon", "E\xCC\x81psilon", "Zeta Note", "Beta 2", "caf\xC3\xA9", "cafe\xCC\x81", "Note #1x", "a b c" };
static const char *const DIRS[] = { "", "", "", "Folder/", "Folder/Sub/", "Other/", ".hidden/", ".exhaust/", ".trash/", ".git/", "Folder/.cache/" };

static size_t make_note(char *out, size_t cap) {
    static const char *const bits[] = { "# Heading\n", "## Second ##\n", "text [[Alpha]] and [[Beta|b]]\n", "![[Gamma]] and ![[img.png]]\n", "[m](Folder/Alpha.md) [n](../Beta.md)\n", "- [ ] task one\n", "- [x] done\n", "#tag1 #tag2/sub\n", "block para ^blk1\n",
        "---\ntitle: T\nrelated: \"[[Delta]]\"\ntags: [pt]\n---\n", "```\n[[Code]] #codetag\n```\n", "plain words here\n", "\n", "[[Zeta Note#Heading]] [[Nope]]\n", "\xC3\xA9\xE4\xBD\xA0\xF0\x9F\x98\x80\n", "$x^2$ and %%hidden [[Hid]]%%\n", "|a|b|\n|-|-|\n|1|2|\n" };
    size_t n = 0;
    int parts = 1 + (int)hunt_rnd(7), crlf = hunt_chance(15), bom = hunt_chance(8);
    if (bom) { memcpy(out, "\xEF\xBB\xBF", 3); n = 3; }
    while (parts--) {
        const char *b = bits[hunt_rnd(sizeof bits / sizeof *bits)];
        size_t l = strlen(b), k;
        if (n + l * 2 + 8 > cap) break;
        for (k = 0; k < l; k++) { if (crlf && b[k] == '\n') out[n++] = '\r'; out[n++] = b[k]; }
    }
    out[n] = 0;
    return n;
}

static void random_note_path(char *out, size_t cap) {
    snprintf(out, cap, "%s%s%s", DIRS[hunt_rnd(sizeof DIRS / sizeof *DIRS)], NAMES[hunt_rnd(sizeof NAMES / sizeof *NAMES)], hunt_chance(88) ? ".md" : hunt_chance(50) ? ".png" : ".txt");
}

/* The notes that exist now, by walking the folder. */
typedef struct Listing { char **paths; size_t n, cap; } Listing;
static int list_cb(const char *path, int is_dir, int64_t mtime, void *u) {
    Listing *l = (Listing *)u;
    (void)mtime;
    if (is_dir) return 0;
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 32; l->paths = (char **)realloc(l->paths, l->cap * sizeof *l->paths); }
    l->paths[l->n++] = hunt_strdup(path);
    return 0;
}
static void listing_free(Listing *l) { size_t i; for (i = 0; i < l->n; i++) free(l->paths[i]); free(l->paths); l->paths = NULL; l->n = l->cap = 0; }

static void test_drift(void) {
    const char *root = hunt_fs_init("index_drift");
    char vault[700], dbdir[700];
    long rounds = hunt_iters(40), r;
    snprintf(vault, sizeof vault, "%s/v", root);
    snprintf(dbdir, sizeof dbdir, "%s/db", root);
    ex_platform_make_dirs(vault); ex_platform_make_dirs(dbdir);
    for (r = 0; r < rounds; r++) {
        ExIndex *inc, *fresh;
        char *canon;
        int steps = 30 + (int)hunt_rnd(40), s;
        ex_platform_remove_tree(vault); ex_platform_make_dirs(vault);
        canon = ex_platform_realpath(vault);
        inc = open_index(dbdir, "inc.db");
        HUNT_CHECK(ex_index_sync(inc, canon, NULL, 0, NULL, NULL, NULL) == 0);
        hunt_stepf("index drift round %ld", r);
        for (s = 0; s < steps; s++) {
            char rel[300], text[2400];
            size_t tl;
            int op = (int)hunt_rnd(100);
            hunt_stepf("index drift round %ld step %d op %d", r, s, op);
            if (op < 40) {
                random_note_path(rel, sizeof rel);
                tl = make_note(text, sizeof text);
                hunt_write_in(canon, rel, text, tl);
                HUNT_CHECK(ex_index_update_file(inc, canon, rel) == 0);
            } else if (op < 55) {
                Listing l = {0};
                ex_platform_walk(canon, list_cb, &l);
                if (l.n) {
                    const char *abs = l.paths[hunt_rnd_size(l.n)], *relp = abs + strlen(canon) + 1;
                    snprintf(rel, sizeof rel, "%s", relp);
                    ex_platform_remove_tree(abs);
                    HUNT_CHECK(ex_index_remove_file(inc, rel) == 0);
                }
                listing_free(&l);
            } else if (op < 75) {
                Listing l = {0};
                ex_platform_walk(canon, list_cb, &l);
                if (l.n) {
                    const char *abs = l.paths[hunt_rnd_size(l.n)], *relp = abs + strlen(canon) + 1;
                    char to[300], toabs[900], *slash, dir[900];
                    snprintf(rel, sizeof rel, "%s", relp);
                    random_note_path(to, sizeof to);
                    snprintf(toabs, sizeof toabs, "%s/%s", canon, to);
                    snprintf(dir, sizeof dir, "%s", toabs);
                    slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
                    if (!ex_platform_file_exists(toabs) && ex_platform_rename_noreplace(abs, toabs) == 0) HUNT_CHECK(ex_index_rename_file(inc, rel, to) == 0);
                }
                listing_free(&l);
            } else if (op < 85) {
                /* changed outside Exhaust: found by the next sync (the mtime or the size changes) */
                Listing l = {0};
                ex_platform_walk(canon, list_cb, &l);
                if (l.n) {
                    const char *abs = l.paths[hunt_rnd_size(l.n)];
                    tl = make_note(text, sizeof text);
                    { size_t k; for (k = 0; k < 3; k++) text[tl++] = 'x'; text[tl] = 0; }
                    hunt_write_in(canon, abs + strlen(canon) + 1, text, tl);
                }
                listing_free(&l);
                HUNT_CHECK(ex_index_sync(inc, canon, NULL, 0, NULL, NULL, NULL) == 0);
            } else if (op < 90) {
                HUNT_CHECK(ex_index_sync(inc, canon, NULL, 0, NULL, NULL, NULL) == 0);
            } else if (op < 93) {
                HUNT_CHECK(ex_index_clear(inc) == 0);
                HUNT_CHECK(ex_index_sync(inc, canon, NULL, 0, NULL, NULL, NULL) == 0);
            } else {
                /* a file that is not text */
                random_note_path(rel, sizeof rel);
                { unsigned char junk[200]; hunt_random_bytes(junk, sizeof junk); hunt_write_in(canon, rel, junk, hunt_rnd_size(sizeof junk)); }
                HUNT_CHECK(ex_index_update_file(inc, canon, rel) == 0);
            }
            if (s % 10 == 9 || s == steps - 1) {
                char *d1, *d2;
                fresh = open_index(dbdir, "fresh.db");
                HUNT_CHECK(ex_index_sync(fresh, canon, NULL, 0, NULL, NULL, NULL) == 0);
                HUNT_CHECK(ex_index_sync(inc, canon, NULL, 0, NULL, NULL, NULL) == 0 || 1);
                d1 = ex_index_digest(inc); d2 = ex_index_digest(fresh);
                if (!d1 || !d2 || strcmp(d1, d2)) {
                    /* the incremental index before and after a sync tells which action left it wrong */
                    HUNT_CHECKF(0, "round %ld step %d: the index kept up with the actions one at a time does not equal an index built from nothing (%s and %s)", r, s, d1 ? d1 : "(null)", d2 ? d2 : "(null)");
                    hunt_note_("files in the incremental index: %ld, in a fresh one: %ld", ex_index_file_count(inc, -1), ex_index_file_count(fresh, -1));
                }
                free(d1); free(d2);
                ex_index_close(fresh);
                if (hunt_.failures > 10) break;
            }
        }
        ex_index_close(inc);
        free(canon);
        if (hunt_.failures > 10) break;
    }

    hunt_step("index: what is never indexed");
    {
        char *canon;
        ExIndex *w;
        ex_platform_remove_tree(vault); ex_platform_make_dirs(vault);
        canon = ex_platform_realpath(vault);
        hunt_write_in(canon, "Visible.md", "x", 1);
        hunt_write_in(canon, ".hidden.md", "x", 1);
        hunt_write_in(canon, ".exhaust/settings.ini", "x", 1);
        hunt_write_in(canon, ".trash/Deleted.md", "x", 1);
        hunt_write_in(canon, "Folder/.secret/Note.md", "x", 1);
        hunt_write_in(canon, ".git/HEAD", "x", 1);
        hunt_write_in(canon, "Folder/Ok.md", "x", 1);
        w = open_index(dbdir, "never.db");
        HUNT_CHECK(ex_index_sync(w, canon, NULL, 0, NULL, NULL, NULL) == 0);
        HUNT_SPECF(ex_index_file_count(w, -1) == 2, "docs/formats.md, Index: every file of the vault that is not hidden (no name starting with a dot) and not excluded", "%ld files indexed, want 2", ex_index_file_count(w, -1));
        HUNT_CHECK(ex_index_file_id(w, ".hidden.md") == 0 && ex_index_file_id(w, ".trash/Deleted.md") == 0 && ex_index_file_id(w, "Folder/.secret/Note.md") == 0);
        HUNT_CHECK(ex_index_update_file(w, canon, ".hidden.md") == 0 || 1);
        HUNT_SUSPECT(ex_index_file_id(w, ".hidden.md") == 0, "update_file on a hidden file must not put it in the index (the watcher reports every file it sees)");
        {
            const char *ex[] = { "Folder/" };
            ExIndexStats st;
            HUNT_CHECK(ex_index_sync(w, canon, ex, 1, &st, NULL, NULL) == 0);
            HUNT_CHECKF(ex_index_file_count(w, -1) == 1, "after excluding Folder/ there are %ld files, want 1", ex_index_file_count(w, -1));
            HUNT_CHECKF(st.removed == 1, "excluding a folder removed %lu files from the index", (unsigned long)st.removed);
        }
        ex_index_close(w);
        free(canon);
    }
    hunt_step("index: an edit that keeps the size, in the same instant");
    {
        char *canon;
        ExIndex *w;
        ExSearch *q;
        ExSearchResult *res = NULL;
        size_t n = 0;
        ex_platform_remove_tree(vault); ex_platform_make_dirs(vault);
        canon = ex_platform_realpath(vault);
        hunt_write_in(canon, "Same.md", "aaaa bbbb cccc", 14);
        w = open_index(dbdir, "same.db");
        HUNT_CHECK(ex_index_sync(w, canon, NULL, 0, NULL, NULL, NULL) == 0);
        hunt_write_in(canon, "Same.md", "zzzz yyyy xxxx", 14);
        HUNT_CHECK(ex_index_sync(w, canon, NULL, 0, NULL, NULL, NULL) == 0);
        q = ex_search_parse("zzzz", NULL, 0);
        if (q) { HUNT_CHECK(ex_search_run(w, canon, q, &(ExSearchOptions){0, 0, 0, 0}, &res, &n, NULL) == 0); HUNT_SUSPECT(n == 1, "a note rewritten with the same size within the file system's time step is not found by the next sync (size and mtime are all it compares): a sync after a crash or a sync tool's write can leave a stale index until Rebuild"); ex_search_results_free(res, n); ex_search_free(q); }
        ex_index_close(w);
        free(canon);
    }
}

/* ---- B. search ---- */

typedef struct QNode { int op; char word[24], word2[24]; struct QNode *a, *b; } QNode;
enum { Q_TERM, Q_CONTENT, Q_FILE, Q_PATH, Q_TAG, Q_PHRASE, Q_LINE, Q_CASE, Q_AND, Q_OR, Q_NOT };

typedef struct Note { char path[200], name[120], *text; size_t len; char tags[8][24]; int ntags; } Note;

static const char *const VOCAB[] = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "theta", "iota", "Alpha", "BETA", "ab", "abc", "x" };
static const char *const TAGS[] = { "t1", "t1/sub", "t2", "Tag3" };

static char *lowerdup(const char *s, size_t n) { char *o = hunt_dup(s, n); size_t i; for (i = 0; i < n; i++) o[i] = (char)tolower((unsigned char)o[i]); return o; }
static int contains_ci(const char *hay, size_t hl, const char *needle) { char *h = lowerdup(hay, hl), *n = lowerdup(needle, strlen(needle)); int r = strstr(h, n) != NULL; free(h); free(n); return r; }

static QNode *qgen(int depth) {
    QNode *q = (QNode *)calloc(1, sizeof *q);
    int k = depth <= 0 ? (int)hunt_rnd(8) : (int)hunt_rnd(14);
    snprintf(q->word, sizeof q->word, "%s", VOCAB[hunt_rnd(sizeof VOCAB / sizeof *VOCAB)]);
    snprintf(q->word2, sizeof q->word2, "%s", VOCAB[hunt_rnd(sizeof VOCAB / sizeof *VOCAB)]);
    if (k < 3) q->op = Q_TERM;
    else if (k == 3) q->op = Q_CONTENT;
    else if (k == 4) { q->op = Q_FILE; snprintf(q->word, sizeof q->word, "%s", VOCAB[hunt_rnd(8)]); }
    else if (k == 5) { q->op = Q_PATH; snprintf(q->word, sizeof q->word, "%s", hunt_chance(50) ? "dirx" : "diry"); }
    else if (k == 6) { q->op = Q_TAG; snprintf(q->word, sizeof q->word, "%s", TAGS[hunt_rnd(sizeof TAGS / sizeof *TAGS)]); }
    else if (k == 7) { q->op = Q_PHRASE; }
    else if (k == 8) { q->op = Q_LINE; }
    else if (k == 9) { q->op = Q_CASE; }
    else if (k < 12) { q->op = Q_AND; q->a = qgen(depth - 1); q->b = qgen(depth - 1); }
    else if (k == 12) { q->op = Q_OR; q->a = qgen(depth - 1); q->b = qgen(depth - 1); }
    else { q->op = Q_NOT; q->a = qgen(depth - 1); }
    return q;
}
static void qfree(QNode *q) { if (!q) return; qfree(q->a); qfree(q->b); free(q); }

static int is_composite(const QNode *q) { return q->op == Q_AND || q->op == Q_OR; }
static size_t qprint(const QNode *q, char *o, size_t cap, int wrap) {
    size_t n = 0;
    switch (q->op) {
    case Q_TERM: return (size_t)snprintf(o, cap, "%s", q->word);
    case Q_CONTENT: return (size_t)snprintf(o, cap, "content:%s", q->word);
    case Q_FILE: return (size_t)snprintf(o, cap, "file:%s", q->word);
    case Q_PATH: return (size_t)snprintf(o, cap, "path:%s", q->word);
    case Q_TAG: return (size_t)snprintf(o, cap, "tag:#%s", q->word);
    case Q_PHRASE: return (size_t)snprintf(o, cap, "\"%s %s\"", q->word, q->word2);
    case Q_LINE: return (size_t)snprintf(o, cap, "line:(%s %s)", q->word, q->word2);
    case Q_CASE: return (size_t)snprintf(o, cap, "match-case:%s", q->word);
    case Q_NOT:
        n += (size_t)snprintf(o + n, cap - n, "-");
        if (is_composite(q->a) || q->a->op == Q_NOT) { n += (size_t)snprintf(o + n, cap - n, "("); n += qprint(q->a, o + n, cap - n, 0); n += (size_t)snprintf(o + n, cap - n, ")"); }
        else n += qprint(q->a, o + n, cap - n, 0);
        return n;
    default: break;
    }
    if (wrap) n += (size_t)snprintf(o + n, cap - n, "(");
    {
        int l = is_composite(q->a) || q->a->op == Q_NOT, r = is_composite(q->b) || q->b->op == Q_NOT;
        if (l) n += (size_t)snprintf(o + n, cap - n, "(");
        n += qprint(q->a, o + n, cap - n, 0);
        if (l) n += (size_t)snprintf(o + n, cap - n, ")");
        n += (size_t)snprintf(o + n, cap - n, q->op == Q_OR ? " OR " : " ");
        if (r) n += (size_t)snprintf(o + n, cap - n, "(");
        n += qprint(q->b, o + n, cap - n, 0);
        if (r) n += (size_t)snprintf(o + n, cap - n, ")");
    }
    if (wrap) n += (size_t)snprintf(o + n, cap - n, ")");
    return n;
}

static int q_eval(const QNode *q, const Note *nt) {
    switch (q->op) {
    case Q_TERM: return contains_ci(nt->text, nt->len, q->word) || contains_ci(nt->name, strlen(nt->name), q->word);
    case Q_CONTENT: return contains_ci(nt->text, nt->len, q->word);
    case Q_FILE: return contains_ci(nt->name, strlen(nt->name), q->word);
    case Q_PATH: return contains_ci(nt->path, strlen(nt->path), q->word);
    case Q_TAG: { int i; size_t l = strlen(q->word); for (i = 0; i < nt->ntags; i++) { if (!hunt_strcasecmp(nt->tags[i], q->word) || (!hunt_strncasecmp(nt->tags[i], q->word, l) && nt->tags[i][l] == '/')) return 1; } return 0; }
    case Q_PHRASE: { char ph[64]; snprintf(ph, sizeof ph, "%s %s", q->word, q->word2); return contains_ci(nt->text, nt->len, ph) || contains_ci(nt->name, strlen(nt->name), ph); }
    case Q_LINE: {
        size_t i = 0;
        while (i <= nt->len) {
            size_t e = i;
            while (e < nt->len && nt->text[e] != '\n') e++;
            if (contains_ci(nt->text + i, e - i, q->word) && contains_ci(nt->text + i, e - i, q->word2)) return 1;
            i = e + 1;
        }
        return 0;
    }
    case Q_CASE: return hunt_memmem(nt->text, nt->len, q->word, strlen(q->word)) != NULL || strstr(nt->name, q->word) != NULL;
    case Q_AND: return q_eval(q->a, nt) && q_eval(q->b, nt);
    case Q_OR: return q_eval(q->a, nt) || q_eval(q->b, nt);
    default: return !q_eval(q->a, nt);
    }
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void test_search(void) {
    const char *root = hunt_fs_init("index_search");
    char vault[700], dbdir[700];
    Note notes[40];
    size_t nn = 0, i;
    ExIndex *w;
    char *canon;
    long rounds = hunt_iters(1500), r;

    snprintf(vault, sizeof vault, "%s/v", root);
    snprintf(dbdir, sizeof dbdir, "%s/db", root);
    ex_platform_make_dirs(vault); ex_platform_make_dirs(dbdir);
    canon = ex_platform_realpath(vault);
    hunt_stepf("search: building the vault");
    for (i = 0; i < 36; i++) {
        Note *n = &notes[nn];
        char buf[3000];
        size_t bl = 0, lines = 1 + hunt_rnd_size(6), k;
        int crlf = hunt_chance(15);
        const char *dir = hunt_chance(40) ? "dirx/" : hunt_chance(50) ? "diry/" : "";
        memset(n, 0, sizeof *n);
        snprintf(n->name, sizeof n->name, "Note %s %02lu", VOCAB[hunt_rnd(8)], (unsigned long)i);
        snprintf(n->path, sizeof n->path, "%s%s.md", dir, n->name);
        for (k = 0; k < lines; k++) {
            size_t words = hunt_rnd_size(8), j;
            for (j = 0; j < words; j++) bl += (size_t)sprintf(buf + bl, "%s%s", j ? " " : "", VOCAB[hunt_rnd(sizeof VOCAB / sizeof *VOCAB)]);
            if (hunt_chance(20) && n->ntags < 6) { const char *t = TAGS[hunt_rnd(sizeof TAGS / sizeof *TAGS)]; bl += (size_t)sprintf(buf + bl, " #%s", t); snprintf(n->tags[n->ntags++], sizeof n->tags[0], "%s", t); }
            buf[bl++] = '\n';
        }
        n->text = hunt_dup(buf, bl);
        n->len = bl;
        {
            char *disk = (char *)malloc(bl * 2 + 8);
            size_t dl = 0;
            if (hunt_chance(10)) { memcpy(disk, "\xEF\xBB\xBF", 3); dl = 3; }
            for (k = 0; k < bl; k++) { if (crlf && buf[k] == '\n') disk[dl++] = '\r'; disk[dl++] = buf[k]; }
            hunt_write_in(canon, n->path, disk, dl);
            free(disk);
        }
        nn++;
    }
    w = open_index(dbdir, "search.db");
    HUNT_CHECK(ex_index_sync(w, canon, NULL, 0, NULL, NULL, NULL) == 0);

    for (r = 0; r < rounds; r++) {
        QNode *q = qgen(1 + (int)hunt_rnd(3));
        char text[600];
        ExSearch *s;
        ExSearchResult *res = NULL;
        size_t nres = 0, files_read = 0, k, expect = 0;
        char err[200] = "";
        qprint(q, text, sizeof text, 0);
        hunt_stepf("search %ld: %s", r, text);
        s = ex_search_parse(text, err, sizeof err);
        if (!s) { HUNT_CHECKF(0, "the query \"%s\" does not parse: %s", text, err); qfree(q); continue; }
        if (ex_search_run(w, canon, s, &(ExSearchOptions){EX_SEARCH_PATH_AZ, 1000, 200, 0}, &res, &nres, &files_read) != 0) { HUNT_CHECKF(0, "the query \"%s\" failed to run", text); ex_search_free(s); qfree(q); continue; }
        {
            char **got = (char **)calloc(nres + 1, sizeof *got), **want = (char **)calloc(nn + 1, sizeof *want);
            size_t ng = nres, nw = 0, a, mism = 0;
            for (k = 0; k < nres; k++) got[k] = res[k].path;
            for (k = 0; k < nn; k++) if (q_eval(q, &notes[k])) want[nw++] = notes[k].path;
            expect = nw;
            qsort(got, ng, sizeof *got, cmp_str); qsort(want, nw, sizeof *want, cmp_str);
            if (ng != nw) mism = 1;
            else for (a = 0; a < ng; a++) if (strcmp(got[a], want[a])) mism = 1;
            if (mism) {
                HUNT_CHECKF(0, "query \"%s\" found %lu notes, the documentation's rules give %lu", text, (unsigned long)ng, (unsigned long)expect);
                for (a = 0; a < ng; a++) { size_t b; int in_want = 0; for (b = 0; b < nw; b++) if (!strcmp(got[a], want[b])) in_want = 1; if (!in_want) hunt_note_("found but should not be: %s", got[a]); }
                for (a = 0; a < nw; a++) { size_t b; int in_got = 0; for (b = 0; b < ng; b++) if (!strcmp(got[b], want[a])) in_got = 1; if (!in_got) hunt_note_("not found but should be: %s", want[a]); }
            }
            free(got); free(want);
        }
        /* a plain term's matches point at that text in the note as the editor holds it */
        if (q->op == Q_TERM || q->op == Q_CONTENT) {
            for (k = 0; k < nres; k++) {
                const Note *nt = NULL;
                size_t j, m;
                for (j = 0; j < nn; j++) if (!strcmp(notes[j].path, res[k].path)) nt = &notes[j];
                if (!nt) continue;
                for (m = 0; m < res[k].nmatches; m++) {
                    const ExSearchMatch *mt = &res[k].matches[m];
                    HUNT_CHECKF(mt->end <= nt->len && mt->start < mt->end, "query \"%s\": a match %lu-%lu is outside %s (%lu bytes)", text, (unsigned long)mt->start, (unsigned long)mt->end, nt->path, (unsigned long)nt->len);
                    if (mt->end <= nt->len && mt->start < mt->end) HUNT_CHECKF(mt->end - mt->start == strlen(q->word) && contains_ci(nt->text + mt->start, mt->end - mt->start, q->word), "query \"%s\": the match %lu-%lu of %s is \"%.*s\"", text, (unsigned long)mt->start, (unsigned long)mt->end, nt->path, (int)(mt->end - mt->start), nt->text + mt->start);
                    HUNT_CHECK(m == 0 || mt->start >= res[k].matches[m - 1].start);
                }
                HUNT_CHECK(res[k].total_matches >= res[k].nmatches);
            }
        }
        HUNT_CHECKF(files_read <= nn, "query \"%s\" read %lu files in a vault of %lu notes", text, (unsigned long)files_read, (unsigned long)nn);
        ex_search_results_free(res, nres);
        /* the sort orders are each other's reverse */
        if (r % 7 == 0) {
            ExSearchResult *ra = NULL, *rb = NULL;
            size_t na = 0, nb = 0;
            if (ex_search_run(w, canon, s, &(ExSearchOptions){EX_SEARCH_PATH_AZ, 1000, 200, 0}, &ra, &na, NULL) == 0 && ex_search_run(w, canon, s, &(ExSearchOptions){EX_SEARCH_PATH_ZA, 1000, 200, 0}, &rb, &nb, NULL) == 0) {
                HUNT_CHECK(na == nb);
                for (k = 0; k < na && k < nb; k++) HUNT_CHECKF(!strcmp(ra[k].path, rb[nb - 1 - k].path), "A to Z and Z to A are not each other's reverse for \"%s\"", text);
                for (k = 1; k < na; k++) HUNT_SUSPECT(strcmp(ra[k - 1].path, ra[k].path) <= 0 || hunt_strcasecmp(ra[k - 1].path, ra[k].path) <= 0, "results sorted by path should be in order of the path");
            }
            ex_search_results_free(ra, na); ex_search_results_free(rb, nb);
            /* the result cap */
            {
                ExSearchResult *rc = NULL;
                size_t nc = 0;
                if (ex_search_run(w, canon, s, &(ExSearchOptions){EX_SEARCH_PATH_AZ, 3, 2, 0}, &rc, &nc, NULL) == 0) {
                    size_t j;
                    HUNT_CHECKF(nc <= 3, "max_results 3 gave %lu results", (unsigned long)nc);
                    for (j = 0; j < nc; j++) HUNT_CHECKF(rc[j].nmatches <= 2, "max_matches 2 kept %lu matches", (unsigned long)rc[j].nmatches);
                }
                ex_search_results_free(rc, nc);
            }
        }
        ex_search_free(s);
        qfree(q);
        if (hunt_.failures > 25) break;
    }

    hunt_step("search: queries that do not parse, and odd ones that must not crash");
    {
        const char *bad[] = { "(", ")", "(a", "a)", "\"unclosed", "/unclosed", "/(/", "line:(", "line:", "file:", "tag:", "-", "- -", "OR", "a OR", "OR a", "(a OR)", "[", "[a", "[a:", "[:b]", "match-case:", "block:(a", "section:", "task:(", "path:\"", "a b OR OR c", "((((((((((a))))))))))", "-(-(-(a)))" };
        size_t k;
        for (k = 0; k < sizeof bad / sizeof *bad; k++) {
            char err[200] = "";
            ExSearch *s = ex_search_parse(bad[k], err, sizeof err);
            if (s) { ExSearchResult *res = NULL; size_t n = 0; (void)ex_search_run(w, canon, s, &(ExSearchOptions){0, 0, 0, 0}, &res, &n, NULL); ex_search_results_free(res, n); ex_search_free(s); }
            else HUNT_CHECKF(err[0] != 0, "the query \"%s\" was refused with no reason", bad[k]);
        }
        HUNT_CHECK(ex_search_parse("", NULL, 0) == NULL);
        HUNT_CHECK(ex_search_parse("   ", NULL, 0) == NULL);
        {
            size_t depth = 20000, p = 0;
            char *deep = (char *)malloc(depth * 2 + 16);
            ExSearch *s;
            for (k = 0; k < depth; k++) deep[p++] = '(';
            deep[p++] = 'a';
            for (k = 0; k < depth; k++) deep[p++] = ')';
            deep[p] = 0;
            s = ex_search_parse(deep, NULL, 0);                 /* a stack overflow here is a crash from the search box */
            ex_search_free(s);
            for (p = 0, k = 0; k < depth; k++) { deep[p++] = '-'; }
            deep[p++] = 'a'; deep[p] = 0;
            s = ex_search_parse(deep, NULL, 0); ex_search_free(s);
            free(deep);
        }
    }
    hunt_step("search: CRLF and BOM notes give offsets in the text the editor holds");
    {
        char *canon2;
        ExIndex *w2;
        ExSearch *s;
        ExSearchResult *res = NULL;
        size_t n = 0;
        ex_platform_remove_tree(vault); ex_platform_make_dirs(vault);
        canon2 = ex_platform_realpath(vault);
        hunt_write_in(canon2, "Crlf.md", "\xEF\xBB\xBFline one\r\nneedle here\r\nline three\r\n", 3 + 10 + 13 + 12);
        w2 = open_index(dbdir, "crlf.db");
        HUNT_CHECK(ex_index_sync(w2, canon2, NULL, 0, NULL, NULL, NULL) == 0);
        s = ex_search_parse("needle", NULL, 0);
        if (s && ex_search_run(w2, canon2, s, &(ExSearchOptions){0, 0, 0, 0}, &res, &n, NULL) == 0) {
            HUNT_CHECK(n == 1 && res[0].nmatches == 1);
            if (n == 1 && res[0].nmatches == 1) {
                size_t len = 0;
                char *held = ex_index_read_note(canon2, "Crlf.md", &len);
                HUNT_CHECKF(held && res[0].matches[0].end <= len && !memcmp(held + res[0].matches[0].start, "needle", 6), "the match offsets do not point at \"needle\" in the text the editor holds");
                HUNT_CHECK(res[0].matches[0].line == 2);
                free(held);
            }
        }
        ex_search_results_free(res, n); ex_search_free(s);
        ex_index_close(w2);
        free(canon2);
    }
    for (i = 0; i < nn; i++) free(notes[i].text);
    ex_index_close(w);
    free(canon);
}

/* ---- C. rename ---- */

typedef struct Snap { char path[300]; char *text; size_t len; ExLinkItem *links; size_t nlinks; } Snap;
typedef struct MovePair { char from[300], to[300]; } MovePair;

static int is_note_rel(const char *rel) { size_t l = strlen(rel); return l > 3 && !strcmp(rel + l - 3, ".md"); }

static size_t snap_take(ExIndex *r, const char *canon, Snap *out, size_t max) {
    Listing l = {0};
    size_t i, n = 0;
    ex_platform_walk(canon, list_cb, &l);
    for (i = 0; i < l.n && n < max; i++) {
        const char *rel = l.paths[i] + strlen(canon) + 1;
        size_t len = 0;
        char *t;
        if (!is_note_rel(rel)) continue;
        t = ex_index_read_note(canon, rel, &len);
        if (!t) continue;
        snprintf(out[n].path, sizeof out[n].path, "%s", rel);
        out[n].text = t; out[n].len = len; out[n].links = NULL; out[n].nlinks = 0;
        ex_links_outgoing(r, rel, &out[n].links, &out[n].nlinks);
        n++;
    }
    listing_free(&l);
    return n;
}
static void snap_free(Snap *s, size_t n) { size_t i; for (i = 0; i < n; i++) { free(s[i].text); ex_link_items_free(s[i].links, s[i].nlinks); } }
static const Snap *snap_find(const Snap *s, size_t n, const char *path) { size_t i; for (i = 0; i < n; i++) if (!strcmp(s[i].path, path)) return &s[i]; return NULL; }

static const char *map_path(const MovePair *mv, size_t nm, const char *p, char *buf, size_t cap) {
    size_t i;
    for (i = 0; i < nm; i++) if (!strcmp(mv[i].from, p)) { snprintf(buf, cap, "%s", mv[i].to); return buf; }
    snprintf(buf, cap, "%s", p);                         /* not moved: callers read buf too */
    return buf;
}

/* The note's text with every link removed, to compare the text that is not a link. */
static char *without_links(const Snap *s) {
    char *out = (char *)malloc(s->len + 1);
    size_t i, k = 0, j;
    for (i = 0; i < s->len; i++) {
        int in = 0;
        for (j = 0; j < s->nlinks; j++) if (i >= s->links[j].start && i < s->links[j].end) { in = 1; break; }
        if (!in) out[k++] = s->text[i];
    }
    out[k] = 0;
    return out;
}

static void rel_from(const char *srcdir, const char *target, char *out, size_t cap) {
    /* a path from the folder `srcdir` ("" or "A/B/") to `target` */
    size_t common = 0, i = 0;
    char up[200] = "";
    while (srcdir[i] && target[i] && srcdir[i] == target[i]) { if (srcdir[i] == '/') common = i + 1; i++; }
    {
        const char *p = srcdir + common;
        while (*p) { if (*p == '/') strcat(up, "../"); p++; }
    }
    snprintf(out, cap, "%s%s", up, target + common);
}

static size_t gen_link_note(char *out, size_t cap, const char *self, char names[][300], size_t nnames, int crlf) {
    size_t n = 0, parts = 2 + hunt_rnd_size(8), p;
    char dir[300], *slash;
    snprintf(dir, sizeof dir, "%s", self);
    slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    if (hunt_chance(25)) { n += (size_t)snprintf(out + n, cap - n, "---\ntitle: T\nrelated: \"[[%.*s]]\"\n---\n", (int)(strlen(names[hunt_rnd_size(nnames)]) - 3), names[hunt_rnd_size(nnames)]); }
    n += (size_t)snprintf(out + n, cap - n, "# Head\n\nIntro paragraph ^blk\n\n");
    for (p = 0; p < parts && n + 400 < cap; p++) {
        const char *target = names[hunt_rnd_size(nnames)];
        char base[300], stem[300], rel[400];
        const char *sl2;
        size_t sl;
        int form = (int)hunt_rnd(16);
        snprintf(stem, sizeof stem, "%s", target);
        stem[strlen(stem) - 3] = 0;
        sl2 = strrchr(stem, '/');
        snprintf(base, sizeof base, "%s", sl2 ? sl2 + 1 : stem);
        sl = strlen(base);
        (void)sl;
        rel_from(dir, target, rel, sizeof rel);
        switch (form) {
        case 0: n += (size_t)snprintf(out + n, cap - n, "see [[%s]] here\n", base); break;
        case 1: n += (size_t)snprintf(out + n, cap - n, "see [[%s|shown text]]\n", base); break;
        case 2: n += (size_t)snprintf(out + n, cap - n, "see [[%s#Head]]\n", base); break;
        case 3: n += (size_t)snprintf(out + n, cap - n, "see [[%s#^blk]]\n", base); break;
        case 4: n += (size_t)snprintf(out + n, cap - n, "see [[%s#Head|alias]]\n", base); break;
        case 5: n += (size_t)snprintf(out + n, cap - n, "see [[%s]]\n", stem); break;
        case 6: n += (size_t)snprintf(out + n, cap - n, "![[%s]]\n", base); break;
        case 7: n += (size_t)snprintf(out + n, cap - n, "[text](%s)\n", rel); break;
        case 8: n += (size_t)snprintf(out + n, cap - n, "[text](%s)\n", target); break;
        case 9: n += (size_t)snprintf(out + n, cap - n, "[text](%s#Head)\n", rel); break;
        case 10: n += (size_t)snprintf(out + n, cap - n, "`[[%s]]` in code and [[%s]] outside\n", base, base); break;
        case 11: n += (size_t)snprintf(out + n, cap - n, "```\n[[%s]]\n[t](%s)\n```\n", base, rel); break;
        case 12: n += (size_t)snprintf(out + n, cap - n, "[[Nope]] and [t](Nope.md) and [[%s]]\n", base); break;
        case 13: n += (size_t)snprintf(out + n, cap - n, "[[%s]] [[%s]] [[%s]]\n", base, base, stem); break;
        case 14: n += (size_t)snprintf(out + n, cap - n, "[[%s]]", base); break;                     /* no line end */
        default: n += (size_t)snprintf(out + n, cap - n, "%s [[%s]] and #tag\n", crlf ? "crlf" : "lf", base); break;
        }
    }
    out[n] = 0;
    if (crlf) {
        char *tmp = (char *)malloc(n * 2 + 8);
        size_t i, j = 0;
        for (i = 0; i < n; i++) { if (out[i] == '\n') tmp[j++] = '\r'; tmp[j++] = out[i]; }
        memcpy(out, tmp, j); out[j] = 0; n = j;
        free(tmp);
    }
    return n;
}

static void test_rename(void) {
    const char *root = hunt_fs_init("index_rename");
    char vault[700], dbdir[700];
    long rounds = hunt_iters(250), r;
    static const char *const dirs[] = { "", "", "Folder/", "Folder/Sub/", "Other/" };
    static const char *const stems[] = { "Alpha", "Beta", "Gamma", "Delta", "Eps", "Zeta" };
    snprintf(vault, sizeof vault, "%s/v", root);
    snprintf(dbdir, sizeof dbdir, "%s/db", root);
    ex_platform_make_dirs(dbdir);
    for (r = 0; r < rounds; r++) {
        char names[16][300];
        size_t nnames = 0, i, nmoves = 0, nsnap = 0, npost = 0;
        char *canon;
        ExIndex *w;
        ExNoteIO io;
        ExRenameReport rep;
        MovePair mv[16];
        ExMove em[16];
        Snap before[20], after[20];
        int mode = (int)hunt_rnd(5);
        char what[300];

        ex_platform_remove_tree(vault); ex_platform_make_dirs(vault);
        canon = ex_platform_realpath(vault);
        while (nnames < 6 + hunt_rnd_size(6)) {
            char p[300];
            int dup = 0;
            snprintf(p, sizeof p, "%s%s.md", dirs[hunt_rnd(sizeof dirs / sizeof *dirs)], stems[hunt_rnd(sizeof stems / sizeof *stems)]);
            for (i = 0; i < nnames; i++) if (!hunt_strcasecmp(names[i], p)) dup = 1;
            if (!dup) snprintf(names[nnames++], sizeof names[0], "%s", p);
        }
        for (i = 0; i < nnames; i++) { char text[3000]; size_t tl = gen_link_note(text, sizeof text, names[i], names, nnames, hunt_chance(15)); if (hunt_chance(8)) { memmove(text + 3, text, tl + 1); memcpy(text, "\xEF\xBB\xBF", 3); tl += 3; } hunt_write_in(canon, names[i], text, tl); }
        w = open_index(dbdir, "rename.db");
        HUNT_CHECK(ex_index_sync(w, canon, NULL, 0, NULL, NULL, NULL) == 0);
        nsnap = snap_take(w, canon, before, 20);

        /* the move */
        if (mode == 4) {
            /* a whole folder */
            const char *src = "Folder", *dst = hunt_chance(50) ? "Moved" : "Other/Folder2";
            char from[900], to[900], dir2[900], *sl;
            for (i = 0; i < nnames; i++) if (!strncmp(names[i], "Folder/", 7)) {
                snprintf(mv[nmoves].from, sizeof mv[0].from, "%s", names[i]);
                snprintf(mv[nmoves].to, sizeof mv[0].to, "%s%s", dst, names[i] + 6);
                nmoves++;
            }
            snprintf(from, sizeof from, "%s/%s", canon, src);
            snprintf(to, sizeof to, "%s/%s", canon, dst);
            snprintf(dir2, sizeof dir2, "%s", to);
            sl = strrchr(dir2, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(dir2); }
            if (nmoves == 0 || ex_platform_file_exists(to) || ex_platform_rename_noreplace(from, to) != 0) nmoves = 0;
            snprintf(what, sizeof what, "round %ld folder Folder -> %s", r, dst);
        } else {
            size_t pick = hunt_rnd_size(nnames);
            char to[300], toabs[900], fromabs[900], dir2[900], *sl;
            const char *newdir = dirs[hunt_rnd(sizeof dirs / sizeof *dirs)], *stem = mode == 0 ? stems[hunt_rnd(sizeof stems / sizeof *stems)] : mode == 1 ? "Renamed" : mode == 2 ? "Renamed Two" : stems[hunt_rnd(sizeof stems / sizeof *stems)];
            const char *ownstem = strrchr(names[pick], '/') ? strrchr(names[pick], '/') + 1 : names[pick];
            char ownbuf[100];
            snprintf(ownbuf, sizeof ownbuf, "%s", ownstem);
            ownbuf[strlen(ownbuf) - 3] = 0;
            if (mode == 0 || mode == 3) { stem = mode == 0 ? ownbuf : stem; }
            if (mode == 1) newdir = names[pick];                 /* placeholder: same folder */
            snprintf(to, sizeof to, "%s%s.md", mode == 1 ? "" : newdir, stem);
            if (mode == 1 || mode == 2) {
                const char *sl2 = strrchr(names[pick], '/');
                if (sl2) snprintf(to, sizeof to, "%.*s%s.md", (int)(sl2 - names[pick] + 1), names[pick], stem);
            }
            snprintf(fromabs, sizeof fromabs, "%s/%s", canon, names[pick]);
            snprintf(toabs, sizeof toabs, "%s/%s", canon, to);
            snprintf(dir2, sizeof dir2, "%s", toabs);
            sl = strrchr(dir2, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(dir2); }
            if (ex_platform_file_exists(toabs) || !strcmp(to, names[pick]) || ex_platform_rename_noreplace(fromabs, toabs) != 0) nmoves = 0;
            else { snprintf(mv[0].from, sizeof mv[0].from, "%s", names[pick]); snprintf(mv[0].to, sizeof mv[0].to, "%s", to); nmoves = 1; }
            snprintf(what, sizeof what, "round %ld %s -> %s", r, names[pick], to);
        }
        hunt_stepf("%s", what);
        if (nmoves == 0) { snap_free(before, nsnap); ex_index_close(w); free(canon); continue; }
        for (i = 0; i < nmoves; i++) { em[i].from = mv[i].from; em[i].to = mv[i].to; }
        io = ex_note_io_files(canon);
        memset(&rep, 0, sizeof rep);
        /* the plan writes nothing */
        HUNT_CHECK(ex_rename_plan(w, &io, em, nmoves, &rep) == 0);
        ex_rename_report_free(&rep);
        memset(&rep, 0, sizeof rep);
        {
            /* what is on disk after the plan: the same as before the plan for every note */
            size_t k;
            for (k = 0; k < nsnap; k++) {
                char mapped[300], *cur;
                size_t cl = 0;
                map_path(mv, nmoves, before[k].path, mapped, sizeof mapped);
                cur = ex_index_read_note(canon, mapped, &cl);
                HUNT_CHECKF(cur && cl == before[k].len && !memcmp(cur, before[k].text, cl), "%s: the dry run changed %s", what, mapped);
                free(cur);
            }
        }
        HUNT_CHECK(ex_rename_links(w, &io, em, nmoves, &rep) == 0);
        HUNT_CHECKF(rep.failed == 0, "%s: %d notes could not be handled", what, rep.failed);
        HUNT_CHECK(ex_index_sync(w, canon, NULL, 0, NULL, NULL, NULL) == 0);
        npost = snap_take(w, canon, after, 20);
        HUNT_CHECKF(npost == nsnap, "%s: %lu notes before and %lu after", what, (unsigned long)nsnap, (unsigned long)npost);
        for (i = 0; i < nsnap && npost == nsnap; i++) {
            char mapped[300];
            const Snap *a, *b = &before[i];
            size_t k;
            char *rb, *ra;
            int changed;
            map_path(mv, nmoves, b->path, mapped, sizeof mapped);
            a = snap_find(after, npost, mapped);
            HUNT_CHECKF(a != NULL, "%s: the note %s is not at %s", what, b->path, mapped);
            if (!a) continue;
            HUNT_CHECKF(a->nlinks == b->nlinks, "%s: %s had %lu links and %s has %lu", what, b->path, (unsigned long)b->nlinks, mapped, (unsigned long)a->nlinks);
            for (k = 0; k < b->nlinks && k < a->nlinks; k++) {
                const ExLinkItem *lb = &b->links[k], *la = &a->links[k];
                char want[300] = "";
                const char *wd = lb->dest ? map_path(mv, nmoves, lb->dest, want, sizeof want) : NULL;
                int same_dest = (wd == NULL && la->dest == NULL) || (wd && la->dest && !strcmp(wd, la->dest));
                if (!same_dest) {
                    HUNT_CHECKF(0, "%s: in %s the link \"%.*s\" led to %s and now \"%.*s\" leads to %s (it should lead to %s)", what, mapped, (int)(lb->end - lb->start), b->text + lb->start, lb->dest ? lb->dest : "nothing", (int)(la->end - la->start), a->text + la->start, la->dest ? la->dest : "nothing", wd ? wd : "nothing");
                    hunt_note_("note before:\n%s\nnote after:\n%s", b->text, a->text);
                }
                HUNT_CHECKF(lb->kind == la->kind, "%s: a link changed its kind (%d to %d)", what, lb->kind, la->kind);
                HUNT_CHECKF((lb->heading == NULL) == (la->heading == NULL) && (!lb->heading || !strcmp(lb->heading, la->heading)), "%s: a link changed its heading (%s to %s)", what, lb->heading ? lb->heading : "none", la->heading ? la->heading : "none");
                HUNT_CHECKF((lb->block == NULL) == (la->block == NULL) && (!lb->block || !strcmp(lb->block, la->block)), "%s: a link changed its block", what);
                HUNT_CHECKF((lb->alias == NULL) == (la->alias == NULL) && (!lb->alias || !strcmp(lb->alias, la->alias)), "%s: a link changed its alias (%s to %s)", what, lb->alias ? lb->alias : "none", la->alias ? la->alias : "none");
            }
            rb = without_links(b); ra = without_links(a);
            HUNT_CHECKF(!strcmp(rb, ra), "%s: the text of %s outside its links changed", what, mapped);
            free(rb); free(ra);
            changed = !(a->len == b->len && !memcmp(a->text, b->text, a->len));
            {
                size_t j;
                int listed = 0;
                for (j = 0; j < rep.n; j++) if (!strcmp(rep.paths[j], mapped)) listed = 1;
                HUNT_CHECKF(!changed || listed, "%s: %s changed but the report does not list it", what, mapped);
                HUNT_SUSPECT(!listed || changed, "a note listed in the report as changed has the same text as before");
            }
        }
        ex_rename_report_free(&rep);
        ex_note_io_free(&io);
        snap_free(before, nsnap); snap_free(after, npost);
        ex_index_close(w);
        free(canon);
        if (hunt_.failures > 12) break;
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_index", argc, argv);
    test_drift();
    test_search();
    test_rename();
    return hunt_end();
}
