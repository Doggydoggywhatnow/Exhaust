/* gen_vault: the generator run twice with the same seed writes identical files (same paths, same
 * bytes), with 5,000 notes and 20,000 wikilinks, canvases, attachments, tags, properties and aliases;
 * another seed gives different files. Usage: test_gen_vault <path-of-gen_vault> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define A "gen_vault_test/a"
#define B "gen_vault_test/b"
#define C "gen_vault_test/c"

typedef struct Hash { uint64_t a, b; } Hash;   /* two independent 64-bit running hashes of everything fed to them */
static void feed(Hash *h, const void *data, size_t len) {
    const unsigned char *p = data;
    size_t i;
    for (i = 0; i < len; i++) {
        h->a = (h->a ^ p[i]) * 0x100000001B3ull;                       /* FNV-1a */
        h->b = ((h->b << 7) | (h->b >> 57)) ^ (p[i] * 0x9E3779B97F4A7C15ull + i);
    }
}

typedef struct Stats { size_t notes, links, canvases, pngs, with_tags, with_aliases, headings; Hash digest; size_t files; } Stats;

static const char *ext_of(const char *p) { const char *d = strrchr(p, '.'); return d ? d : ""; }

/* A running hash over every path (relative) and its content, in walk order. */
typedef struct Walk { const char *root; Stats *st; } Walk;

static int visit(const char *path, int is_dir, int64_t mtime, void *user) {
    Walk *w = user;
    const char *rel = path + strlen(w->root);
    size_t len = 0, i;
    char *data;
    (void)mtime;
    if (is_dir) return 0;
    while (*rel == '/') rel++;
    data = ex_platform_read_file(path, &len);
    CHECK(data != NULL);
    if (!data) return 0;
    feed(&w->st->digest, rel, strlen(rel) + 1);
    feed(&w->st->digest, data, len);
    w->st->files++;
    if (!strcmp(ext_of(path), ".md")) {
        w->st->notes++;
        for (i = 0; i + 1 < len; i++) if (data[i] == '[' && data[i + 1] == '[') w->st->links++;
        if (strstr(data, "\ntags: [")) w->st->with_tags++;
        if (strstr(data, "\naliases: [")) w->st->with_aliases++;
        if (strstr(data, "\n## ")) w->st->headings++;
    } else if (!strcmp(ext_of(path), ".canvas")) {
        w->st->canvases++;
        CHECK(strstr(data, "\"nodes\"") && strstr(data, "\"edges\""));
    } else if (!strcmp(ext_of(path), ".png")) {
        w->st->pngs++;
        CHECK(len > 8 && memcmp(data, "\x89PNG", 4) == 0);
    }
    free(data);
    return 0;
}

static int run(const char *tool, const char *out, const char *seed) {
    const char *args[6];
    SDL_Process *p;
    int code = -1;
    args[0] = tool; args[1] = out; args[2] = "--seed"; args[3] = seed; args[4] = NULL;
    if (!seed) args[2] = NULL;
    p = SDL_CreateProcess(args, false);
    if (!p) { fprintf(stderr, "cannot start %s: %s\n", tool, SDL_GetError()); return -1; }
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code;
}

static void scan(const char *root, Stats *st) {
    Walk w;
    memset(st, 0, sizeof *st);
    st->digest.a = 0xCBF29CE484222325ull;
    w.root = root; w.st = st;
    CHECK(ex_platform_walk(root, visit, &w) == 0);
}

int main(int argc, char **argv) {
    const char *tool = argc > 1 ? argv[1] : "./gen_vault";
    Stats a, b, c;
    ex_platform_remove_tree("gen_vault_test");
    ex_platform_make_dirs("gen_vault_test");
    CHECK(run(tool, A, NULL) == 0);
    CHECK(run(tool, B, NULL) == 0);
    CHECK(run(tool, C, "2") == 0);
    scan(A, &a); scan(B, &b); scan(C, &c);
    CHECK(a.notes == 5000);
    CHECK(a.links == 20000);
    CHECK(a.canvases >= 3 && a.pngs >= 10);
    CHECK(a.with_tags == 5000 && a.with_aliases == 5000 && a.headings > 4000);
    CHECK(a.files == b.files && a.digest.a == b.digest.a && a.digest.b == b.digest.b);          /* same seed: identical paths and bytes */
    CHECK((a.digest.a != c.digest.a || a.digest.b != c.digest.b) && c.notes == 5000 && c.links == 20000);   /* another seed: other text, same shape */
    ex_platform_remove_tree("gen_vault_test");
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("gen_vault: ok");
    return 0;
}
