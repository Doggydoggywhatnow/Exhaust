#include <stdio.h>
#include <string.h>
#include "exhaust_text.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint32_t dec1(const char *s, size_t len, size_t *used) {
    size_t i = 0;
    uint32_t cp = ex_utf8_decode(s, len, &i);
    *used = i;
    return cp;
}

int main(void) {
    size_t used;
    char buf[4];
    uint32_t cp;

    CHECK(dec1("A", 1, &used) == 'A' && used == 1);
    CHECK(dec1("\xC3\xA9", 2, &used) == 0xE9 && used == 2);          /* e acute */
    CHECK(dec1("\xE2\x82\xAC", 3, &used) == 0x20AC && used == 3);    /* euro */
    CHECK(dec1("\xF0\x9F\x98\x80", 4, &used) == 0x1F600 && used == 4);
    CHECK(dec1("\xE0\xA4\xB9", 3, &used) == 0x939 && used == 3);     /* Devanagari ha */
    CHECK(dec1("\xD7\xA9", 2, &used) == 0x5E9 && used == 2);         /* Hebrew shin */

    /* Invalid input gives U+FFFD and always makes progress. */
    CHECK(dec1("\x80", 1, &used) == 0xFFFD && used == 1);            /* stray continuation */
    CHECK(dec1("\xC0\x80", 2, &used) == 0xFFFD && used == 1);        /* overlong, never valid lead */
    CHECK(dec1("\xE0\x80\x80", 3, &used) == 0xFFFD);                 /* overlong 3-byte */
    CHECK(dec1("\xED\xA0\x80", 3, &used) == 0xFFFD);                 /* surrogate */
    CHECK(dec1("\xF4\x90\x80\x80", 4, &used) == 0xFFFD);             /* above U+10FFFF */
    CHECK(dec1("\xF5\x80\x80\x80", 4, &used) == 0xFFFD && used == 1);
    CHECK(dec1("\xE2\x82", 2, &used) == 0xFFFD && used == 2);        /* truncated at end */
    CHECK(dec1("\xE2\x82" "A", 3, &used) == 0xFFFD && used == 2);    /* broken by ASCII, A kept */
    CHECK(dec1("\xF0\x9F", 2, &used) == 0xFFFD && used == 2);

    /* Round trip of every scalar value class boundary. */
    {
        static const uint32_t pts[] = {0, 0x7F, 0x80, 0x7FF, 0x800, 0xFFFF, 0x10000, 0x10FFFF};
        size_t k;
        for (k = 0; k < sizeof pts / sizeof pts[0]; k++) {
            size_t i = 0;
            int n = ex_utf8_encode(pts[k], buf);
            cp = ex_utf8_decode(buf, (size_t)n, &i);
            CHECK(cp == pts[k] && i == (size_t)n);
        }
    }
    CHECK(ex_utf8_encode(0xD800, buf) == 3 && memcmp(buf, "\xEF\xBF\xBD", 3) == 0);
    CHECK(ex_utf8_encode(0x110000, buf) == 3 && memcmp(buf, "\xEF\xBF\xBD", 3) == 0);

    /* Every possible two-byte input terminates and consumes at least one byte. */
    {
        unsigned a, b;
        for (a = 0; a < 256; a++)
            for (b = 0; b < 256; b++) {
                char s[2];
                size_t i = 0;
                s[0] = (char)a; s[1] = (char)b;
                while (i < 2) {
                    size_t before = i;
                    (void)ex_utf8_decode(s, 2, &i);
                    CHECK(i > before && i <= 2);
                }
            }
    }
    return failures ? 1 : 0;
}
