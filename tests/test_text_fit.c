/* ex_text_fit (GitHub issue 19 / ISSUES 19): a name longer than its room is cut with an ellipsis, and short or empty input is safe.
 * The cases of TESTS.md T-TEXT-BOX-01 for this function: very long, very short, empty, one wide character, combining marks, a buffer
 * that is too small, a width that is too small. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_render.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int valid_utf8(const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1, i;
        if (n < 0) return 0;
        for (i = 0; i < n; i++) if (((unsigned char)*++s & 0xC0) != 0x80) return 0;
    }
    return 1;
}

int main(void) {
    ExRenderer *r = ex_renderer_create("fit", 200, 100);
    ExFont *f;
    char out[64], big[2000], *longname;
    size_t n, i;
    float size = 16.0f, w;
    if (!r) { fprintf(stderr, "no renderer\n"); return 1; }
    f = ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR);

    /* it fits: unchanged */
    n = ex_text_fit(r, f, size, "Home", 4, 500.0f, out, sizeof out);
    CHECK(n == 4 && strcmp(out, "Home") == 0);
    /* empty, no room, no buffer */
    n = ex_text_fit(r, f, size, "", 0, 100.0f, out, sizeof out);
    CHECK(n == 0 && out[0] == 0);
    n = ex_text_fit(r, f, size, "Home", 4, 0.0f, out, sizeof out);
    CHECK(n == 0 && out[0] == 0);
    n = ex_text_fit(r, f, size, "Home", 4, 100.0f, out, 0);
    CHECK(n == 0);
    n = ex_text_fit(r, f, size, "A very long name indeed", 23, 100.0f, out, 3);      /* a buffer too small for even the ellipsis */
    CHECK(n == 0 && out[0] == 0);
    /* one character, in a room that is only just big enough for it, and one that is not */
    w = ex_text_measure(r, f, size, "W", 1);
    n = ex_text_fit(r, f, size, "W", 1, w, out, sizeof out);
    CHECK(n == 1 && strcmp(out, "W") == 0);
    n = ex_text_fit(r, f, size, "W", 1, 1.0f, out, sizeof out);
    CHECK(n == 0 || (valid_utf8(out) && ex_text_measure(r, f, size, out, n) <= 1.0f));
    /* very long names: always valid UTF-8, never wider than the room, ending in the ellipsis, for every room from tiny to wide */
    longname = malloc(1200);
    for (i = 0; i < 1199; i++) longname[i] = 'A' + (char)(i % 26);
    longname[1199] = 0;
    for (w = 4.0f; w < 400.0f; w += 7.0f) {
        n = ex_text_fit(r, f, size, longname, 1199, w, big, sizeof big);
        CHECK(valid_utf8(big) && strlen(big) == n);
        if (n) CHECK(ex_text_measure(r, f, size, big, n) <= w + 0.01f && n >= 3 && memcmp(big + n - 3, "\xE2\x80\xA6", 3) == 0);
    }
    /* a longer name gets a longer start in a wider room */
    {
        size_t a = ex_text_fit(r, f, size, longname, 1199, 120.0f, big, sizeof big), b;
        char big2[2000];
        b = ex_text_fit(r, f, size, longname, 1199, 240.0f, big2, sizeof big2);
        CHECK(b > a && a > 3);
    }
    /* wide and combining characters are not cut in the middle: a letter with its mark stays together, a UTF-8 sequence stays whole */
    {
        const char *s = "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81";          /* e + combining acute, eight times */
        const char *cjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
        n = ex_text_fit(r, f, size, s, strlen(s), 40.0f, out, sizeof out);
        CHECK(valid_utf8(out) && strlen(out) == n);
        if (n > 3) CHECK(((unsigned char)out[n - 4] & 0xC0) != 0x80 || memcmp(out + n - 5, "\xCC\x81", 2) == 0);
        n = ex_text_fit(r, f, size, cjk, strlen(cjk), 30.0f, out, sizeof out);
        CHECK(valid_utf8(out) && strlen(out) == n);
    }
    ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("text_fit: ok");
    return 0;
}
