/* search (sub-slice 4b-i): one test for each operator of the README (and for OR, -, parentheses, quotes, regular expressions,
 * property filters, sorting and context), on a small vault; plus the performance test on the generated vault (search_perf).
 * Usage: test_search <name> [<gen_vault>] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_search.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char g_vault[96], g_db[96], g_index[128];
static ExIndex *g_ix;
static char *g_root;

static void write_file(const char *rel, const char *content) {
    char path[512], *dir, *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", g_vault, rel);
    dir = malloc(strlen(path) + 1); strcpy(dir, path);
    slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    free(dir);
    f = fopen(path, "wb");
    if (f) { fwrite(content, 1, strlen(content), f); fclose(f); }
}

static void setup(void) {
    char err[200];
    ex_platform_remove_tree(g_vault);
    ex_platform_remove_tree(g_db);
    write_file("Alpha.md",
        "---\nstatus: active\nrating: 4\ndue: 2024-06-01\ntags: [work]\naliases: [First]\n---\n# Alpha project\n\n"
        "The quick brown fox jumps. Meeting notes #meeting\n- [ ] write report\n- [x] send email\n\n## Details\nFox hunting is not allowed. The Quick fox.\n\n"
        "Second paragraph with Fox and a lazy dog on one line: fox dog\n");
    write_file("Beta.md", "---\nstatus: done\nrating: 2\ndue: 2025-03-10\n---\nLazy dog. The FOX is brown.\n#work/sub\n\n- [ ] call the dog\n- [-] cancelled fox task\n");
    write_file("notes/Fox diary.md", "---\nstatus: waiting\ntags:\n  - personal\n---\nNothing about animals here, only the weather.\n\n# Weather\nRain. A fox? No.\n");
    write_file("Gamma.md", "Plain note: brown dog, no fox at all.\nThe dog is brown\nand a fox\n\nSeparate block about a dog\n");
    g_root = ex_platform_realpath(g_vault);
    g_ix = ex_index_open(g_index, 1, err, sizeof err);
    CHECK(g_ix && ex_index_sync(g_ix, g_root, NULL, 0, NULL, NULL, NULL) == 0);
}

/* Runs a query and compares the sorted list of paths with the expected, comma separated. */
static ExSearchResult *g_res;
static size_t g_n;
static void run(const char *query, const char *expect) {
    char err[200], got[600] = "";
    ExSearch *q = ex_search_parse(query, err, sizeof err);
    size_t i;
    ExSearchOptions o;
    memset(&o, 0, sizeof o);
    ex_search_results_free(g_res, g_n); g_res = NULL; g_n = 0;
    if (!q) { fprintf(stderr, "query [%s]: %s\n", query, err); failures++; return; }
    if (ex_search_run(g_ix, g_root, q, &o, &g_res, &g_n, NULL) != 0) { fprintf(stderr, "query [%s] failed\n", query); failures++; ex_search_free(q); return; }
    {   /* the order of the results is tested apart: compare the paths sorted */
        char *paths[64];
        size_t a, b;
        for (i = 0; i < g_n && i < 64; i++) paths[i] = g_res[i].path;
        for (a = 0; a < i; a++) for (b = a + 1; b < i; b++) if (strcmp(paths[b], paths[a]) < 0) { char *t = paths[a]; paths[a] = paths[b]; paths[b] = t; }
        for (a = 0; a < i; a++) { if (a) strcat(got, ","); strcat(got, paths[a]); }
    }
    if (strcmp(got, expect) != 0) { fprintf(stderr, "query [%s]: expected [%s], got [%s]\n", query, expect, got); failures++; }
    ex_search_free(q);
}

static void t_text(void) {
    run("fox", "Alpha.md,Beta.md,Gamma.md,notes/Fox diary.md");        /* in the text, and the name of "Fox diary" */
    run("QUICK brown", "Alpha.md");                                      /* case is ignored, both words */
    run("\"brown fox\"", "Alpha.md");                                    /* a phrase */
    run("ox jum", "Alpha.md");                                           /* parts of words */
    run("zzzz", "");
    run("do", "Alpha.md,Beta.md,Gamma.md");                              /* under three characters: scanned */
}
static void t_or_not_group(void) {
    run("hunting OR weather", "Alpha.md,notes/Fox diary.md");
    run("fox -lazy", "Gamma.md,notes/Fox diary.md");
    run("-fox", "");
    run("dog -(lazy OR plain)", "");
    run("(hunting OR weather) fox", "Alpha.md,notes/Fox diary.md");
    run("fox (lazy OR weather) -hunting", "Beta.md,notes/Fox diary.md");
}
static void t_regex(void) {
    run("/qu[a-z]+k/", "Alpha.md");
    run("/^Lazy dog\\./", "Beta.md");
    run("/F[Oo]x/ -brown", "notes/Fox diary.md");
    { char err[100]; ExSearch *p = ex_search_parse("/[unclosed/", err, sizeof err); CHECK(p != NULL); ex_search_free(p); }   /* a pattern that does not compile fails when it runs */
    {
        ExSearch *q = ex_search_parse("/[unclosed/", NULL, 0);
        ExSearchOptions o; ExSearchResult *r = NULL; size_t n = 0;
        memset(&o, 0, sizeof o);
        CHECK(q && ex_search_run(g_ix, g_root, q, &o, &r, &n, NULL) != 0);
        ex_search_free(q);
    }
}
static void t_file(void) { run("file:alpha", "Alpha.md"); run("file:\"fox diary\"", "notes/Fox diary.md"); run("file:.md -file:a", ""); run("file:(alpha OR beta)", "Alpha.md,Beta.md"); }
static void t_path(void) { run("path:notes", "notes/Fox diary.md"); run("path:notes/fox", "notes/Fox diary.md"); run("path:/ ", "notes/Fox diary.md"); }
static void t_content(void) { run("content:diary", ""); run("diary", "notes/Fox diary.md"); run("content:weather", "notes/Fox diary.md"); }
static void t_case(void) { run("match-case:FOX", "Beta.md"); run("match-case:fox", "Alpha.md,Beta.md,Gamma.md,notes/Fox diary.md"); run("ignore-case:FOX content:brown", "Alpha.md,Beta.md,Gamma.md"); }
static void t_tag(void) {
    run("tag:#work", "Alpha.md,Beta.md");           /* the property and the nested tag work/sub */
    run("tag:work/sub", "Beta.md");
    run("tag:#meeting", "Alpha.md");
    run("tag:personal", "notes/Fox diary.md");
    run("tag:work -tag:work/sub", "Alpha.md");
}
static void t_line(void) { run("line:(fox dog)", "Alpha.md,Beta.md,Gamma.md"); run("line:(brown dog)", "Beta.md,Gamma.md"); run("line:fox", "Alpha.md,Beta.md,Gamma.md,notes/Fox diary.md"); }
static void t_block(void) { run("block:(lazy dog)", "Alpha.md,Beta.md"); run("block:(fox dog)", "Alpha.md,Beta.md,Gamma.md"); run("block:(fox separate)", ""); }
static void t_section(void) { run("section:(hunting quick)", "Alpha.md"); run("section:(weather rain)", "notes/Fox diary.md"); run("section:(rain animals)", ""); }
static void t_task(void) { run("task:report", "Alpha.md"); run("task:dog", "Beta.md"); run("task:fox", "Beta.md"); }
static void t_task_todo(void) { run("task-todo:report", "Alpha.md"); run("task-todo:email", ""); run("task-todo:(call dog)", "Beta.md"); }
static void t_task_done(void) { run("task-done:email", "Alpha.md"); run("task-done:report", ""); run("task-done:fox", ""); }
static void t_property(void) {
    run("[status:active]", "Alpha.md");
    run("[status]", "Alpha.md,Beta.md,notes/Fox diary.md");
    run("[status:done OR waiting]", "Beta.md,notes/Fox diary.md");
    run("[rating:>3]", "Alpha.md");
    run("[rating:<=2]", "Beta.md");
    run("[due:<2025-01-01]", "Alpha.md");
    run("[status:ACTIVE] fox", "Alpha.md");
    run("-[status] fox", "Gamma.md");
    run("[tags:work]", "Alpha.md");
    run("[aliases:first]", "Alpha.md");
    run("[nothing]", "");
}
static void t_results(void) {
    ExSearch *q = ex_search_parse("fox", NULL, 0);
    ExSearchOptions o;
    size_t i;
    memset(&o, 0, sizeof o);
    /* matches: positions and lines, in order */
    CHECK(q && ex_search_run(g_ix, g_root, q, &o, &g_res, &g_n, NULL) == 0);
    for (i = 0; i < g_n; i++) {
        if (!strcmp(g_res[i].path, "Alpha.md")) {
            char *text = ex_platform_read_file("search_test_vault_results/Alpha.md", NULL);
            size_t off = 0, k;
            char *ctx;
            CHECK(g_res[i].total_matches == 5 && g_res[i].nmatches == 5);     /* fox, Fox, fox, Fox, fox, fox: the title says none */
            for (k = 1; k < g_res[i].nmatches; k++) CHECK(g_res[i].matches[k].start > g_res[i].matches[k - 1].start);
            CHECK(g_res[i].matches[0].line == 10);
            ctx = text ? ex_search_context(text, strlen(text), &g_res[i].matches[0], 1, &off) : NULL;
            CHECK(ctx && strstr(ctx, "The quick brown fox jumps.") && !strncmp(ctx + off, "fox", 3));
            free(ctx); free(text);
        }
        if (!strcmp(g_res[i].path, "notes/Fox diary.md")) CHECK(g_res[i].name_match && g_res[i].total_matches == 1);
    }
    ex_search_free(q);
    /* sorting */
    { ExSearch *s = ex_search_parse("fox", NULL, 0);
      ex_search_results_free(g_res, g_n); g_res = NULL;
      o.sort = EX_SEARCH_PATH_ZA; CHECK(ex_search_run(g_ix, g_root, s, &o, &g_res, &g_n, NULL) == 0 && g_n == 4 && !strcmp(g_res[0].path, "notes/Fox diary.md"));
      ex_search_results_free(g_res, g_n); g_res = NULL;
      o.sort = EX_SEARCH_PATH_AZ; CHECK(ex_search_run(g_ix, g_root, s, &o, &g_res, &g_n, NULL) == 0 && !strcmp(g_res[0].path, "Alpha.md"));
      ex_search_results_free(g_res, g_n); g_res = NULL;
      o.sort = EX_SEARCH_MATCHES_MOST; CHECK(ex_search_run(g_ix, g_root, s, &o, &g_res, &g_n, NULL) == 0 && !strcmp(g_res[0].path, "Alpha.md"));
      ex_search_results_free(g_res, g_n); g_res = NULL;
      o.sort = EX_SEARCH_MATCHES_LEAST; CHECK(ex_search_run(g_ix, g_root, s, &o, &g_res, &g_n, NULL) == 0 && g_res[0].total_matches <= g_res[g_n - 1].total_matches);
      ex_search_results_free(g_res, g_n); g_res = NULL;
      o.sort = 0; o.max_results = 2; CHECK(ex_search_run(g_ix, g_root, s, &o, &g_res, &g_n, NULL) == 0 && g_n == 2);
      ex_search_free(s); }
    /* syntax errors */
    { char err[200];
      CHECK(ex_search_parse("(fox", err, sizeof err) == NULL && err[0]);
      CHECK(ex_search_parse("fox )", err, sizeof err) == NULL && err[0]);
      CHECK(ex_search_parse("\"fox", err, sizeof err) == NULL && err[0]);
      CHECK(ex_search_parse("[status", err, sizeof err) == NULL && err[0]);
      CHECK(ex_search_parse("   ", err, sizeof err) == NULL && err[0]);
      CHECK(ex_search_parse("tag:", err, sizeof err) == NULL); }
}

static void perf(const char *tool) {
    const char *args[3];
    SDL_Process *p;
    int code = -1, i;
    char err[200];
    static const char *const queries[] = {"Notes", "\"quick brown\" -zzz", "tag:project OR file:copper", "line:(see note)", "[status:active] cobalt", "section:(see canyon)", "/Cop+er [a-z]+/"};
    ex_platform_remove_tree(g_vault); ex_platform_remove_tree(g_db);
    args[0] = tool; args[1] = g_vault; args[2] = NULL;
    p = SDL_CreateProcess(args, false);
    CHECK(p != NULL);
    if (!p) return;
    SDL_WaitProcess(p, true, &code); SDL_DestroyProcess(p);
    CHECK(code == 0);
    g_root = ex_platform_realpath(g_vault);
    g_ix = ex_index_open(g_index, 1, err, sizeof err);
    CHECK(g_ix && ex_index_sync(g_ix, g_root, NULL, 0, NULL, NULL, NULL) == 0);
    for (i = 0; g_ix && i < (int)(sizeof queries / sizeof queries[0]); i++) {
        ExSearch *q = ex_search_parse(queries[i], err, sizeof err);
        ExSearchOptions o;
        ExSearchResult *res = NULL;
        size_t n = 0, reads = 0;
        double t0, t1;
        memset(&o, 0, sizeof o);
        CHECK(q != NULL);
        if (!q) continue;
        {   /* the best of three runs: a single run is at the mercy of whatever else the machine is doing */
            int rep;
            double best = 1e9;
            for (rep = 0; rep < 3; rep++) {
                ex_search_results_free(res, n); res = NULL; n = 0;
                t0 = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
                CHECK(ex_search_run(g_ix, g_root, q, &o, &res, &n, &reads) == 0);
                t1 = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
                if (t1 - t0 < best) best = t1 - t0;
            }
            printf("[%s]: %zu results, %zu files read, %.1f ms (best of 3)\n", queries[i], n, reads, best * 1000.0);
#if defined(__SANITIZE_ADDRESS__)
            CHECK(best < 3.0);                                       /* the sanitizers run several times slower */
#else
            CHECK(best * 1000.0 < 100.0);
#endif
        }
        ex_search_results_free(res, n);
        ex_search_free(q);
    }
}

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "text";
    snprintf(g_vault, sizeof g_vault, "search_test_vault_%s", name);
    snprintf(g_db, sizeof g_db, "search_test_db_%s", name);
    snprintf(g_index, sizeof g_index, "%s/index.db", g_db);
    if (!strcmp(name, "perf")) { perf(argc > 2 ? argv[2] : "./gen_vault"); goto done; }
    setup();
    if (!g_ix) return 1;
    if (!strcmp(name, "text")) t_text();
    else if (!strcmp(name, "or_not_group")) t_or_not_group();
    else if (!strcmp(name, "regex")) t_regex();
    else if (!strcmp(name, "file")) t_file();
    else if (!strcmp(name, "path")) t_path();
    else if (!strcmp(name, "content")) t_content();
    else if (!strcmp(name, "case")) t_case();
    else if (!strcmp(name, "tag")) t_tag();
    else if (!strcmp(name, "line")) t_line();
    else if (!strcmp(name, "block")) t_block();
    else if (!strcmp(name, "section")) t_section();
    else if (!strcmp(name, "task")) t_task();
    else if (!strcmp(name, "task_todo")) t_task_todo();
    else if (!strcmp(name, "task_done")) t_task_done();
    else if (!strcmp(name, "property")) t_property();
    else if (!strcmp(name, "results")) t_results();
    else { fprintf(stderr, "unknown test %s\n", name); return 2; }
done:
    ex_search_results_free(g_res, g_n);
    ex_index_close(g_ix);
    free(g_root);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
