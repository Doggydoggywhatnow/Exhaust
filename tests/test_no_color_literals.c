/* no_color_literals: Exhaust's own sources must not contain color literals; every
 * color comes from a theme token. The token loader (src/ui/theme.c) is the one
 * place allowed to turn text into colors. Usage: test_no_color_literals <repo-root>
 *
 * A literal is: a "#rrggbb" or "#rrggbbaa" hex color, or an ExColor that is
 * initialized from braces ("ExColor x = {...}" or "(ExColor){...}"). The checker
 * is tested on samples first, so it cannot silently stop detecting. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int is_hex(int c) { return isxdigit((unsigned char)c) != 0; }

/* Returns 1 and the byte offset of the first color literal in text, or 0. */
static int find_color_literal(const char *s, size_t *where) {
    size_t i, n = strlen(s);
    for (i = 0; i < n; i++) {
        if (s[i] == '#') { /* #rrggbb or #rrggbbaa not followed by another hex digit or letter */
            size_t k = 0;
            while (is_hex(s[i + 1 + k])) k++;
            if ((k == 6 || k == 8) && !isalnum((unsigned char)s[i + 1 + k]) && s[i + 1 + k] != '_') { *where = i; return 1; }
        }
        if (strncmp(s + i, "ExColor", 7) == 0 && (i == 0 || !(isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_')) &&
            !isalnum((unsigned char)s[i + 7]) && s[i + 7] != '_') {
            size_t j = i + 7;
            /* (ExColor){ ... } */
            if (i > 0 && s[i - 1] == '(') {
                size_t q = j;
                if (s[q] == ')') { q++; while (s[q] == ' ' || s[q] == '\t') q++; if (s[q] == '{') { *where = i; return 1; } }
            }
            /* ExColor [const] name[...] = { */
            while (s[j] == ' ' || s[j] == '\t' || s[j] == '*') j++;
            if (strncmp(s + j, "const ", 6) == 0) j += 6;
            while (s[j] == ' ' || s[j] == '\t') j++;
            while (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '[' || s[j] == ']') j++;
            while (s[j] == ' ' || s[j] == '\t') j++;
            if (s[j] == '=') {
                j++;
                while (s[j] == ' ' || s[j] == '\t' || s[j] == '\n') j++;
                if (s[j] == '{') { *where = i; return 1; }
            }
        }
    }
    return 0;
}

static void test_detector(void) {
    size_t w;
    CHECK(find_color_literal("const char *c = \"#ff00aa\";", &w));
    CHECK(find_color_literal("x = \"#FF00AA80\";", &w));
    CHECK(find_color_literal("static const ExColor BG = {0x1e, 0x1f, 0x2d, 0xff};", &w));
    CHECK(find_color_literal("ExColor c = {1, 2, 3, 4};", &w));
    CHECK(find_color_literal("ExColor c =\n  {1, 2, 3, 4};", &w));
    CHECK(find_color_literal("f((ExColor){1, 2, 3, 4});", &w));
    CHECK(find_color_literal("ExColor palette[] = {{1,2,3,4}};", &w));
    CHECK(!find_color_literal("#include <stdio.h>\n#define X 1\n#ifdef Y\n#endif\n#if 0\n", &w));
    CHECK(!find_color_literal("ExColor c = ex_theme_color(t, EX_COLOR_TEXT);", &w));
    CHECK(!find_color_literal("ExColor *p; void f(ExColor c); typedef struct ExColor { int r; } ExColor;", &w));
    CHECK(!find_color_literal("/* a number #1234567 is not a color */", &w));
    CHECK(!find_color_literal("\"#abcdef0\"", &w)); /* seven digits */
    CHECK(!find_color_literal("MyExColor x = {1};", &w));
}

typedef struct Scan { const char *root; size_t root_len; int files, bad; } Scan;

static int has_ext(const char *p, const char *ext) {
    size_t n = strlen(p), e = strlen(ext);
    return n > e && strcmp(p + n - e, ext) == 0;
}

static int on_file(const char *path, int is_dir, int64_t mtime, void *user) {
    Scan *sc = user;
    size_t len = 0;
    char *text;
    size_t where;
    (void)mtime;
    if (is_dir || !(has_ext(path, ".c") || has_ext(path, ".h") || has_ext(path, ".cc"))) return 0;
    if (has_ext(path, "src/ui/theme.c")) return 0; /* the token loader */
    text = ex_platform_read_file(path, &len);
    if (!text) return 0;
    sc->files++;
    if (find_color_literal(text, &where)) {
        size_t line = 1, i;
        for (i = 0; i < where; i++) if (text[i] == '\n') line++;
        fprintf(stderr, "color literal in %s:%zu\n", path, line);
        sc->bad++;
    }
    free(text);
    return 0;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : ".";
    static const char *dirs[] = {"src", "include"};
    Scan sc = {0};
    size_t i;
    char path[1024];
    test_detector();
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", root, dirs[i]);
        CHECK(ex_platform_walk(path, on_file, &sc) == 0);
    }
    fprintf(stderr, "scanned %d files\n", sc.files);
    CHECK(sc.files > 20);
    CHECK(sc.bad == 0);
    return failures ? 1 : 0;
}
