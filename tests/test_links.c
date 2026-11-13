/* links (sub-slice 4a-ii): on a small sample vault, link resolution (case-insensitive, NFC, paths, aliases, duplicates),
 * outgoing links, backlinks and unlinked mentions match a golden table, including unresolved links; the page preview
 * returns a section and a block; and, on the performance vault, finding the unlinked mentions of a note reads no note
 * files except the matches the index returns (unlinked_lookup).
 * Usage: test_links <sample-vault> <golden-file> | test_links --lookup <gen_vault> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_links.h"
#include "exhaust_platform.h"
#include "exhaust_vault.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct Out { char *v; size_t n, cap; } Out;
static void out_add(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void out_add(Out *o, const char *fmt, ...) {
    va_list ap;
    char buf[1024];
    size_t n;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    n = strlen(buf);
    if (o->n + n + 1 > o->cap) { o->cap = (o->n + n + 1) * 2; o->v = realloc(o->v, o->cap); }
    memcpy(o->v + o->n, buf, n + 1);
    o->n += n;
}

typedef struct Walk { char **paths; size_t n; const char *root; } Walk;
static int collect_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    Walk *w = user;
    const char *rel = path + strlen(w->root);
    (void)mtime;
    while (*rel == '/') rel++;
    if (is_dir || !*rel) return 0;
    w->paths = realloc(w->paths, (w->n + 1) * sizeof *w->paths);
    { size_t rl = strlen(rel) + 1; char *c = malloc(rl); memcpy(c, rel, rl); w->paths[w->n++] = c; }
    return 0;
}
static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void golden(const char *vault, const char *golden_path) {
    ExIndex *w;
    char err[200], *root = ex_platform_realpath(vault), *want;
    Walk wk = {NULL, 0, root};
    Out o = {0};
    size_t i, k;
    ex_platform_remove_tree("links_test_db");
    w = ex_index_open("links_test_db/index.db", 1, err, sizeof err);
    CHECK(w != NULL);
    if (!w) return;
    CHECK(ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    ex_platform_walk(root, collect_cb, &wk);
    qsort(wk.paths, wk.n, sizeof *wk.paths, cmp);
    for (i = 0; i < wk.n; i++) {
        ExLinkItem *items;
        size_t n;
        ExMention *men;
        if (ex_file_kind(wk.paths[i]) != EX_KIND_MARKDOWN) continue;
        out_add(&o, "== %s\n", wk.paths[i]);
        CHECK(ex_links_outgoing(w, wk.paths[i], &items, &n) == 0);
        for (k = 0; k < n; k++)
            out_add(&o, "out %u kind=%d [%s]%s%s%s%s%s%s -> %s\n", items[k].line, items[k].kind, items[k].target, items[k].heading ? " #" : "", items[k].heading ? items[k].heading : "",
                    items[k].block ? " ^" : "", items[k].block ? items[k].block : "", items[k].alias ? " |" : "", items[k].alias ? items[k].alias : "", items[k].dest ? items[k].dest : "UNRESOLVED");
        ex_link_items_free(items, n);
        CHECK(ex_links_backlinks(w, wk.paths[i], &items, &n) == 0);
        for (k = 0; k < n; k++) out_add(&o, "back %s:%u [%s]\n", items[k].source, items[k].line, items[k].target);
        ex_link_items_free(items, n);
        CHECK(ex_links_unlinked(w, root, wk.paths[i], &men, &n) == 0);
        for (k = 0; k < n; k++) out_add(&o, "mention %s:%u\n", men[k].source, men[k].line);
        ex_mentions_free(men, n);
    }
    want = ex_platform_read_file(golden_path, NULL);
    if (!want || !o.v || strcmp(want, o.v) != 0) {
        FILE *f = fopen("links_actual.txt", "wb");
        if (f) { fputs(o.v ? o.v : "", f); fclose(f); }
        fprintf(stderr, "the table differs from %s (actual output written to links_actual.txt)\n", golden_path);
        failures++;
    }
    free(want);
    /* page previews */
    {
        char *p = ex_link_preview(w, root, "Beta.md", "Section", NULL, 1000);
        CHECK(p && strstr(p, "A paragraph about the second letter.") && strstr(p, "## Section"));
        free(p);
        p = ex_link_preview(w, root, "Beta.md", NULL, "para", 1000);
        CHECK(p && strstr(p, "A paragraph about the second letter. ^para") && !strstr(p, "Home page mention"));
        free(p);
        p = ex_link_preview(w, root, "Beta.md", "No such heading", NULL, 1000);
        CHECK(p == NULL);
        p = ex_link_preview(w, root, "Alpha.md", NULL, NULL, 12);
        CHECK(p && strlen(p) <= 12);
        free(p);
        p = ex_links_line_text(root, "Alpha.md", 5);
        CHECK(p && !strncmp(p, "Alpha links to", 14));
        free(p);
    }
    for (i = 0; i < wk.n; i++) free(wk.paths[i]);
    free(wk.paths); free(o.v); free(root);
    ex_index_close(w);
}

static void lookup(const char *tool) {
    const char *args[3];
    SDL_Process *p;
    int code = -1, i;
    ExIndex *w;
    char *root, err[200];
    ex_platform_remove_tree("links_perf_vault");
    ex_platform_remove_tree("links_perf_db");
    args[0] = tool; args[1] = "links_perf_vault"; args[2] = NULL;
    p = SDL_CreateProcess(args, false);
    CHECK(p != NULL);
    if (!p) return;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    CHECK(code == 0);
    root = ex_platform_realpath("links_perf_vault");
    w = ex_index_open("links_perf_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) return;
    for (i = 0; i < 5; i++) {
        char rel[128];
        ExMention *men;
        size_t n, cands = 0;
        int64_t *ids = NULL;
        long before;
        char *title = NULL, *path;
        snprintf(rel, sizeof rel, "Area %02d/", i);
        path = NULL;
        /* pick the first note of the folder: its path is "Area NN/<name>.md" */
        {
            int64_t id;
            for (id = 1; id < 6000 && !path; id++) { char *pp = ex_index_file_path(w, id); if (pp && !strncmp(pp, rel, strlen(rel)) && strstr(pp, ".md")) path = pp; else free(pp); }
        }
        CHECK(path != NULL);
        if (!path) continue;
        {
            const char *slash = strrchr(path, '/'), *b = slash + 1;
            size_t bl = strlen(b) - 3;
            title = malloc(bl + 1); memcpy(title, b, bl); title[bl] = 0;
        }
        CHECK(ex_index_candidates(w, title, strlen(title), &ids, &cands, NULL) == 0);
        {   /* the aliases are looked up too: "aliases: [word N]" */
            char full[600], *text, *a;
            snprintf(full, sizeof full, "links_perf_vault/%s", path);
            text = ex_platform_read_file(full, NULL);
            a = text ? strstr(text, "aliases: [") : NULL;
            if (a) {
                char *e = strchr(a, ']'), *alias;
                int64_t *ids2 = NULL;
                size_t c2 = 0;
                if (e) {
                    *e = 0; alias = a + 10;
                    if (ex_index_candidates(w, alias, strlen(alias), &ids2, &c2, NULL) == 0) cands += c2;
                    free(ids2);
                }
            }
            free(text);
        }
        before = ex_index_note_reads();
        CHECK(ex_links_unlinked(w, root, path, &men, &n) == 0);
        printf("%s: %zu mentions; %zu candidates, %ld note files read of %ld notes\n", path, n, cands, ex_index_note_reads() - before, ex_index_file_count(w, EX_KIND_MARKDOWN));
        CHECK(ex_index_note_reads() - before <= (long)cands);
        CHECK(ex_index_note_reads() - before < 100);
        ex_mentions_free(men, n);
        free(ids); free(title); free(path);
    }
    free(root);
    ex_index_close(w);
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--lookup")) lookup(argv[2]);
    else if (argc >= 3) golden(argv[1], argv[2]);
    else { fprintf(stderr, "usage: test_links <vault> <golden> | --lookup <gen_vault>\n"); return 2; }
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
