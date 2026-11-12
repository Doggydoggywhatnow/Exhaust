/* font_cache: a second scan reads the cache file, returns the same font list and
 * does not scan the folders; a changed folder list or a damaged cache is rescanned.
 * Usage: test_font_cache <fonts-dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_fontdb.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int same_list(const ExFontDb *a, const ExFontDb *b) {
    int i, n = ex_fontdb_count(a);
    if (n != ex_fontdb_count(b)) return 0;
    for (i = 0; i < n; i++) {
        const ExFontEntry *x = ex_fontdb_entry(a, i), *y = ex_fontdb_entry(b, i);
        if (strcmp(x->path, y->path) || x->index != y->index || strcmp(x->family, y->family) ||
            strcmp(x->style, y->style) || x->flags != y->flags || memcmp(x->coverage, y->coverage, sizeof x->coverage))
            return 0;
    }
    return 1;
}

/* Removes everything the test creates (so it can run repeatedly). */
static void cleanup(void) {
    static const char *paths[] = {
        "font_cache_test/cache/fonts.cache", "font_cache_test/cache/fonts.cache.tmp",
        "font_cache_test/fonts/more/AtkinsonHyperlegible-Italic.ttf", "font_cache_test/fonts/more",
        "font_cache_test/fonts/AtkinsonHyperlegible-Regular.ttf", "font_cache_test/fonts/AtkinsonHyperlegible-Bold.ttf",
        "font_cache_test/fonts/AtkinsonHyperlegibleMono-Regular.ttf", "font_cache_test/fonts",
        "font_cache_test/cache", "font_cache_test"};
    size_t i;
    for (i = 0; i < sizeof paths / sizeof paths[0]; i++) SDL_RemovePath(paths[i]);
}

int main(int argc, char **argv) {
    const char *fonts = argc > 1 ? argv[1] : "third_party/fonts";
    char cache[256] = "font_cache_test/cache", dir[256] = "font_cache_test/fonts", more[256] = "font_cache_test/fonts/more";
    char file[512];
    const char *dirs[1];
    ExFontScanStats s1, s2, s3;
    ExFontDb *a, *b, *c;
    cleanup();

    CHECK(ex_platform_make_dirs(cache) == 0 && ex_platform_make_dirs(dir) == 0);
    {   /* a private copy of one family so the test can change the folder */
        const char *names[] = {"atkinson-hyperlegible/AtkinsonHyperlegible-Regular.ttf",
                               "atkinson-hyperlegible/AtkinsonHyperlegible-Bold.ttf",
                               "atkinson-hyperlegible-mono/AtkinsonHyperlegibleMono-Regular.ttf"};
        int k;
        for (k = 0; k < 3; k++) {
            size_t n = 0;
            char src[512], dst[512];
            char *data;
            snprintf(src, sizeof src, "%s/%s", fonts, names[k]);
            snprintf(dst, sizeof dst, "%s/%s", dir, strrchr(names[k], '/') + 1);
            data = ex_platform_read_file(src, &n);
            CHECK(data != NULL);
            if (data) { CHECK(ex_platform_write_file_atomic(dst, data, n) == 0); free(data); }
        }
    }
    dirs[0] = dir;

    a = ex_fontdb_scan(dirs, 1, cache, &s1);
    CHECK(a && !s1.cache_hit && s1.files_opened == 3 && s1.faces == 3);
    b = ex_fontdb_scan(dirs, 1, cache, &s2);
    CHECK(b && s2.cache_hit && s2.files_opened == 0 && s2.faces == 3);
    CHECK(s2.folders_visited == s1.folders_visited);
    CHECK(same_list(a, b));

    /* lookups */
    CHECK(ex_fontdb_find_family(a, "atkinson hyperlegible", EX_FONT_FLAG_BOLD) >= 0);
    CHECK(ex_fontdb_entry(a, ex_fontdb_find_family(a, "Atkinson Hyperlegible", EX_FONT_FLAG_BOLD))->flags & EX_FONT_FLAG_BOLD);
    CHECK(!(ex_fontdb_entry(a, ex_fontdb_find_family(a, "Atkinson Hyperlegible", 0))->flags & EX_FONT_FLAG_BOLD));
    CHECK(ex_fontdb_entry(a, ex_fontdb_find_family(a, "Atkinson Hyperlegible Mono", 0))->flags & EX_FONT_FLAG_MONO);
    CHECK(ex_fontdb_find_family(a, "No Such Family", 0) == -1);
    CHECK(ex_fontdb_find_codepoint(a, 'A', 0, 0, 0) >= 0);
    CHECK(ex_fontdb_find_codepoint(a, 0x4E2D, 0, 0, 0) == -1);   /* no CJK here */
    CHECK(ex_fontdb_find_codepoint(a, 'A', 0, 1, 0) == -1);      /* no color font here */
    ex_fontdb_free(b);

    /* a new subfolder changes the signature: the folders are scanned again */
    CHECK(ex_platform_make_dirs(more) == 0);
    {
        size_t n = 0;
        char src[512];
        char *data;
        snprintf(src, sizeof src, "%s/atkinson-hyperlegible/AtkinsonHyperlegible-Italic.ttf", fonts);
        snprintf(file, sizeof file, "%s/AtkinsonHyperlegible-Italic.ttf", more);
        data = ex_platform_read_file(src, &n);
        CHECK(data != NULL);
        if (data) { CHECK(ex_platform_write_file_atomic(file, data, n) == 0); free(data); }
    }
    c = ex_fontdb_scan(dirs, 1, cache, &s3);
    CHECK(c && !s3.cache_hit && s3.files_opened == 4 && s3.faces == 4 && s3.folders_visited == s1.folders_visited + 1);
    ex_fontdb_free(c);

    /* a damaged cache is ignored and rewritten */
    {
        char path[300];
        snprintf(path, sizeof path, "%s/fonts.cache", cache);
        CHECK(ex_platform_write_file_atomic(path, "garbage\n", 8) == 0);
        c = ex_fontdb_scan(dirs, 1, cache, &s3);
        CHECK(c && !s3.cache_hit && s3.faces == 4);
        ex_fontdb_free(c);
        c = ex_fontdb_scan(dirs, 1, cache, &s3);
        CHECK(c && s3.cache_hit && s3.faces == 4 && s3.files_opened == 0);
        ex_fontdb_free(c);
    }

    /* an empty cache folder name switches caching off */
    c = ex_fontdb_scan(dirs, 1, "", &s3);
    CHECK(c && !s3.cache_hit && s3.files_opened == 4);
    ex_fontdb_free(c);
    c = ex_fontdb_scan(dirs, 1, "", &s3);
    CHECK(c && !s3.cache_hit && s3.files_opened == 4);
    ex_fontdb_free(c);

    /* a folder that does not exist gives an empty list, not an error */
    {
        const char *none[1] = {"font_cache_test/no_such_folder"};
        c = ex_fontdb_scan(none, 1, "", &s3);
        CHECK(c && ex_fontdb_count(c) == 0);
        ex_fontdb_free(c);
    }
    ex_fontdb_free(a);
    cleanup();
    return failures ? 1 : 0;
}
