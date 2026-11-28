/* hunt_unicode: UTF-8 decoding and encoding, grapheme clusters, NFC and the name key that links are matched by.
 *
 * Everything the editor, the index, search and link resolution do with text starts here, and all of it must hold for ANY byte string: a note
 * can contain a broken sequence, an overlong form or a surrogate. This test
 *   A. decodes EVERY 1-, 2- and 3-byte string and a dense sample of 4-byte strings and compares with a strict reference decoder (Unicode
 *      Table 3-7): valid input must decode exactly, invalid input must give U+FFFD and move by one byte or by the valid prefix, and the decoder
 *      must never look past `len` (continuation bytes are placed just beyond the end to catch a decoder that peeks);
 *   B. encodes every code point up to U+10FFFF and one past it, surrogates included, and round-trips;
 *   C. walks random text by grapheme clusters: next() always moves forward, covers the text exactly, prev() agrees with next() at every
 *      boundary and at every position inside a cluster;
 *   D. checks NFC and the name key on known pairs (canonical reordering, Hangul, singletons, full case folding: sharp s, final sigma, dotted I)
 *      and on random text: idempotence, ASCII identity, broken bytes passed through unchanged;
 *   E. times NFC on text made to be slow (thousands of combining marks that need reordering): a link target in a note has no length limit.
 * Link: exhaust_unicode. */
#include "hunt.h"

#include "exhaust_nfc.h"
#include "exhaust_text.h"

/* ---- reference UTF-8 ---- */

/* Returns the length of the well-formed sequence at s (and its code point), or 0 if there is none; *maxsub is the length of the longest prefix that is the start of a well-formed sequence (at least 1). */
static int ref_decode(const unsigned char *s, size_t len, uint32_t *cp, size_t *maxsub) {
    unsigned char b0 = s[0];
    *maxsub = 1;
    if (b0 < 0x80) { *cp = b0; return 1; }
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (len < 2) return 0;
        if ((s[1] & 0xC0) != 0x80) return 0;
        *maxsub = 2; *cp = ((uint32_t)(b0 & 0x1F) << 6) | (s[1] & 0x3F); return 2;
    }
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        unsigned char lo = b0 == 0xE0 ? 0xA0 : 0x80, hi = b0 == 0xED ? 0x9F : 0xBF;
        if (len < 2 || s[1] < lo || s[1] > hi) return 0;
        *maxsub = 2;
        if (len < 3 || (s[2] & 0xC0) != 0x80) return 0;
        *maxsub = 3; *cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3;
    }
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        unsigned char lo = b0 == 0xF0 ? 0x90 : 0x80, hi = b0 == 0xF4 ? 0x8F : 0xBF;
        if (len < 2 || s[1] < lo || s[1] > hi) return 0;
        *maxsub = 2;
        if (len < 3 || (s[2] & 0xC0) != 0x80) return 0;
        *maxsub = 3;
        if (len < 4 || (s[3] & 0xC0) != 0x80) return 0;
        *maxsub = 4; *cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4;
    }
    return 0;
}

static unsigned long g_bad_decodes;

static void check_decode(const unsigned char *s, size_t len) {
    unsigned char buf[16];
    size_t i = 0, maxsub;
    uint32_t cp = 0, got, refcp;
    int rl;
    /* the string, then bytes that would complete a sequence if the decoder looked past the end */
    memset(buf, 0xBF, sizeof buf);
    memcpy(buf, s, len);
    got = ex_utf8_decode((const char *)buf, len, &i);
    rl = ref_decode(buf, len, &refcp, &maxsub);
    (void)cp;
    if (rl) {
        if (got != refcp || i != (size_t)rl) { if (g_bad_decodes++ < 20) HUNT_CHECKF(0, "valid sequence of %d bytes starting %02x %02x: decoded U+%04X using %lu bytes, want U+%04X", rl, buf[0], len > 1 ? buf[1] : 0, got, (unsigned long)i, refcp); }
    } else {
        if (got != 0xFFFD || i < 1 || (i != 1 && i != maxsub)) { if (g_bad_decodes++ < 20) HUNT_CHECKF(0, "invalid input %02x %02x %02x %02x (len %lu): got U+%04X using %lu bytes (valid prefix %lu)", buf[0], len > 1 ? buf[1] : 0, len > 2 ? buf[2] : 0, len > 3 ? buf[3] : 0, (unsigned long)len, got, (unsigned long)i, (unsigned long)maxsub); }
    }
    if (i > len) { if (g_bad_decodes++ < 20) HUNT_CHECKF(0, "decode moved to %lu in a text of %lu bytes", (unsigned long)i, (unsigned long)len); }
}

static void test_decode_exhaustive(void) {
    unsigned a, b, c;
    unsigned char s[4];
    static const unsigned char mids[] = { 0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC2, 0xE0, 0xF4, 0xFF };

    hunt_step("decode: every 1-byte string");
    for (a = 0; a < 256; a++) { s[0] = (unsigned char)a; check_decode(s, 1); }
    hunt_step("decode: every 2-byte string");
    for (a = 0; a < 256; a++) for (b = 0; b < 256; b++) { s[0] = (unsigned char)a; s[1] = (unsigned char)b; check_decode(s, 2); }
    hunt_step("decode: every 3-byte string");
    for (a = 0xC0; a < 256; a++) for (b = 0; b < 256; b++) for (c = 0; c < 256; c++) { s[0] = (unsigned char)a; s[1] = (unsigned char)b; s[2] = (unsigned char)c; check_decode(s, 3); }
    hunt_step("decode: a long string cut short (the first byte promises more than is there)");
    for (a = 0xC0; a < 256; a++) for (b = 0; b < 256; b += 1) { s[0] = (unsigned char)a; s[1] = (unsigned char)b; check_decode(s, 1); check_decode(s, 2); }
    hunt_step("decode: 4-byte strings, first bytes F0 to FF, the other three from a dense set");
    for (a = 0xF0; a < 256; a++) {
        size_t x, y, z;
        for (x = 0; x < sizeof mids; x++) for (y = 0; y < sizeof mids; y++) for (z = 0; z < sizeof mids; z++) {
            s[0] = (unsigned char)a; s[1] = mids[x]; s[2] = mids[y]; s[3] = mids[z];
            check_decode(s, 4); check_decode(s, 3); check_decode(s, 2);
        }
        for (b = 0x80; b < 0xC0; b++) for (c = 0x80; c < 0xC0; c += 5) { s[0] = (unsigned char)a; s[1] = (unsigned char)b; s[2] = (unsigned char)c; s[3] = 0x80; check_decode(s, 4); s[3] = 0xBF; check_decode(s, 4); }
    }
    HUNT_CHECK(g_bad_decodes == 0);
}

static void test_encode(void) {
    uint32_t cp;
    char out[8];
    hunt_step("encode every code point");
    for (cp = 0; cp <= 0x110000; cp++) {
        int n;
        size_t i = 0;
        uint32_t back;
        int surrogate = cp >= 0xD800 && cp <= 0xDFFF, beyond = cp > 0x10FFFF;
        memset(out, 0x55, sizeof out);
        n = ex_utf8_encode(cp, out);
        if (surrogate || beyond) {
            HUNT_CHECKF(n == 3 && !memcmp(out, "\xEF\xBF\xBD", 3), "U+%04X should encode as U+FFFD, got %d bytes", cp, n);
            continue;
        }
        {
            int want = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
            HUNT_CHECKF(n == want, "U+%04X encoded in %d bytes, want %d", cp, n, want);
            if (n != want) continue;
            HUNT_CHECKF((unsigned char)out[n] == 0x55, "U+%04X: wrote past its %d bytes", cp, n);
            back = ex_utf8_decode(out, (size_t)n, &i);
            HUNT_CHECKF(back == cp && i == (size_t)n, "U+%04X came back as U+%04X", cp, back);
        }
    }
}

/* ---- C. grapheme clusters ---- */

static void test_grapheme(void) {
    long rounds = hunt_iters(3000), r;
    char text[400];

    hunt_step("grapheme: known clusters");
    {
        struct { const char *s; int clusters; } known[] = {
            { "e\xCC\x81", 1 }, { "\r\n", 1 }, { "\n\r", 2 }, { "\r\r", 2 }, { "a\r\nb", 3 },
            { "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", 1 },          /* family: man ZWJ woman ZWJ girl */
            { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA", 1 },                                                    /* DE */
            { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA\xF0\x9F\x87\xAB", 2 },                                    /* DE + a lone F */
            { "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7", 2 },                    /* DE FR */
            { "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 1 },                                                    /* thumbs up + skin tone */
            { "1\xEF\xB8\x8F\xE2\x83\xA3", 1 },                                                           /* keycap 1 */
            { "\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF", 1 },   /* England flag (tag sequence) */
            { "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB", 1 },                                                /* Hangul L V T */
            { "\xED\x95\x9C\xED\x95\x9C", 2 },
            { "a\xE2\x80\x8D" "b", 2 },                                                                   /* ZWJ after a letter joins to the letter, not to the next one */
            { "\xCC\x81", 1 }, { "\xCC\x81\xCC\x81", 1 },
            { "\xE0\xA4\xA8\xE0\xA4\xBF", 1 },                                                            /* Devanagari na + vowel sign i (extends) */
            { "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", 1 },                                                /* Devanagari ka virama ssa (InCB conjunct, Unicode 15.1) */
            { "", 0 },
        };
        size_t i;
        for (i = 0; i < sizeof known / sizeof *known; i++) {
            size_t len = strlen(known[i].s), pos = 0;
            int n = 0;
            while (pos < len) { size_t nx = ex_grapheme_next(known[i].s, len, pos); HUNT_CHECK(nx > pos && nx <= len); if (nx <= pos) break; pos = nx; n++; }
            HUNT_CHECKF(n == known[i].clusters, "known cluster case %lu: %d clusters, want %d", (unsigned long)i, n, known[i].clusters);
        }
        HUNT_EQ_INT(ex_grapheme_prev("abc", 3, 0), 0);
        HUNT_EQ_INT(ex_grapheme_next("abc", 3, 3), 3);
    }

    hunt_step("grapheme: properties of the emoji tables");
    HUNT_CHECK(ex_is_extended_pictographic(0x1F600) && ex_is_extended_pictographic(0x00A9) && ex_is_extended_pictographic(0x2640));
    HUNT_CHECK(!ex_is_extended_pictographic('a') && !ex_is_extended_pictographic(0x20) && !ex_is_extended_pictographic(0x110000) && !ex_is_extended_pictographic(0xFFFFFFFFu));
    HUNT_CHECK(ex_is_emoji_presentation(0x1F600) && !ex_is_emoji_presentation(0x00A9) && !ex_is_emoji_presentation('1'));
    HUNT_CHECK(!ex_is_emoji_presentation(0x110000) && !ex_is_emoji_presentation(0xFFFFFFFFu));

    for (r = 0; r < rounds; r++) {
        size_t len = hunt_chance(70) ? hunt_random_valid_text(text, sizeof text, 24) : hunt_random_text(text, sizeof text, 24), pos = 0, nb = 0, i;
        size_t bounds[420];
        char *exact = hunt_dup(text, len);            /* an exact block so a read past the end is a sanitizer error */
        hunt_stepf("grapheme random text %ld", r);
        hunt_set_input(exact, len, "grapheme text");
        bounds[nb++] = 0;
        while (pos < len && nb < 419) {
            size_t nx = ex_grapheme_next(exact, len, pos);
            if (!(nx > pos && nx <= len)) { HUNT_CHECKF(0, "next(%lu) = %lu in a text of %lu", (unsigned long)pos, (unsigned long)nx, (unsigned long)len); break; }
            pos = nx;
            bounds[nb++] = pos;
        }
        HUNT_CHECK(pos == len || nb >= 419);
        for (i = 1; i < nb; i++) HUNT_CHECKF(ex_grapheme_prev(exact, len, bounds[i]) == bounds[i - 1], "prev(%lu) != %lu", (unsigned long)bounds[i], (unsigned long)bounds[i - 1]);
        /* prev() from a position inside a cluster: the start of that cluster */
        for (pos = 1; pos <= len && nb < 419; pos++) {
            size_t want = 0, p;
            if (pos < len && ((unsigned char)exact[pos] & 0xC0) == 0x80) continue;          /* not the start of a character */
            for (i = 0; i < nb; i++) if (bounds[i] < pos) want = bounds[i];
            p = ex_grapheme_prev(exact, len, pos);
            HUNT_CHECKF(p == want, "prev(%lu) = %lu, the boundary before it is %lu", (unsigned long)pos, (unsigned long)p, (unsigned long)want);
        }
        hunt_clear_input();
        free(exact);
    }
}

/* ---- D. NFC and the name key ---- */

static void check_nfc(const char *in, const char *want, const char *what) {
    char *got = ex_nfc(in, strlen(in));
    HUNT_CHECKF(got && !strcmp(got, want), "nfc of %s", what);
    free(got);
}

static void test_nfc(void) {
    long rounds = hunt_iters(4000), r;

    hunt_step("nfc: known cases");
    check_nfc("e\xCC\x81", "\xC3\xA9", "e + acute");
    check_nfc("\xC3\xA9", "\xC3\xA9", "precomposed e acute");
    check_nfc("A\xCC\x8A", "\xC3\x85", "A + ring");
    check_nfc("\xE2\x84\xAB", "\xC3\x85", "Angstrom sign (a singleton: it must become A with ring)");
    check_nfc("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB", "\xED\x95\x9C", "Hangul jamo L V T");
    check_nfc("\xED\x95\x9C", "\xED\x95\x9C", "a Hangul syllable");
    check_nfc("a\xCC\x82\xCC\xA3", "\xE1\xBA\xAD", "a + circumflex + dot below (marks out of canonical order)");
    check_nfc("a\xCC\xA3\xCC\x82", "\xE1\xBA\xAD", "a + dot below + circumflex");
    check_nfc("\xEF\xAC\x81", "\xEF\xAC\x81", "the fi ligature stays (it is a compatibility form)");
    check_nfc("\xCC\x81", "\xCC\x81", "a lone combining mark");
    check_nfc("", "", "the empty string");
    check_nfc("plain ASCII 123", "plain ASCII 123", "ASCII");
    check_nfc("\xE0\xA4\xA8\xE0\xA4\xBC", "\xE0\xA4\xA9", "Devanagari na + nukta composes to nnna (U+0929)");
    check_nfc("\xE0\xA5\x98", "\xE0\xA4\x95\xE0\xA4\xBC", "U+0958 is a composition exclusion: it must decompose and stay decomposed");
    check_nfc("\xF0\x9D\x85\x97\xF0\x9D\x85\xA5", "\xF0\x9D\x85\x97\xF0\x9D\x85\xA5", "musical symbol (an exclusion, outside the BMP)");
    check_nfc("a\xFF" "b", "a\xFF" "b", "a stray byte is passed through");
    check_nfc("\xC3", "\xC3", "a truncated sequence is passed through");
    check_nfc("e\xCC\x81\xC3", "\xC3\xA9\xC3", "composing, then a truncated sequence");

    hunt_step("name key: known cases");
    {
        struct { const char *a, *b; int same; } pairs[] = {
            { "Note", "note", 1 }, { "NOTE", "nOtE", 1 }, { "caf\xC3\xA9", "cafe\xCC\x81", 1 }, { "CAF\xC3\x89", "cafe\xCC\x81", 1 },
            { "Stra\xC3\x9F" "e", "STRASSE", 1 }, { "Stra\xC3\x9F" "e", "strasse", 1 }, { "\xE1\xBA\x9E", "ss", 1 },              /* sharp s and capital sharp s: full case folding */
            { "\xCE\x9F\xCE\x94\xCE\xA5\xCE\xA3\xCE\xA3\xCE\x95\xCE\xA5\xCE\xA3", "\xCE\xBF\xCE\xB4\xCF\x85\xCF\x83\xCF\x83\xCE\xB5\xCF\x85\xCF\x82", 1 },   /* final sigma folds with sigma */
            { "\xEF\xAC\x81", "FI", 1 },                                                      /* fi ligature folds to fi */
            { "\xC4\xB0", "i\xCC\x87", 1 },                                                   /* dotted capital I */
            { "\xC4\xB1", "i", 0 },                                                           /* dotless i is not i (no Turkic folding) */
            { "\xE2\x84\xAB", "\xC3\xA5", 1 },                                                /* Angstrom sign folds like a-ring */
            { "a", "b", 0 }, { "a", "a ", 0 }, { "", "", 1 }, { "a/b", "a\\b", 0 },
            { "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB", "\xED\x95\x9C", 1 },
            { "\xCF\x82", "\xCF\x83", 1 },
        };
        size_t i;
        for (i = 0; i < sizeof pairs / sizeof *pairs; i++) {
            char *ka = ex_name_key(pairs[i].a, strlen(pairs[i].a)), *kb = ex_name_key(pairs[i].b, strlen(pairs[i].b));
            int same = ka && kb && !strcmp(ka, kb);
            HUNT_CHECKF(same == pairs[i].same, "name key pair %lu (%s vs %s): same=%d, want %d", (unsigned long)i, pairs[i].a, pairs[i].b, same, pairs[i].same);
            free(ka); free(kb);
        }
    }

    hunt_step("nfc and name key: random text");
    for (r = 0; r < rounds; r++) {
        char text[300];
        size_t len = hunt_chance(60) ? hunt_random_valid_text(text, sizeof text, 14) : hunt_random_text(text, sizeof text, 14);
        char *exact = hunt_dup(text, len), *n1, *n2, *k1, *k2;
        size_t i;
        int ascii = 1;
        hunt_stepf("nfc random %ld", r);
        hunt_set_input(exact, len, "nfc text");
        n1 = ex_nfc(exact, len);
        HUNT_CHECK(n1 != NULL);
        if (n1) {
            n2 = ex_nfc(n1, strlen(n1));
            HUNT_CHECKF(n2 && !strcmp(n1, n2), "nfc is not idempotent");
            free(n2);
        }
        k1 = ex_name_key(exact, len);
        HUNT_CHECK(k1 != NULL);
        if (k1) {
            k2 = ex_name_key(k1, strlen(k1));
            HUNT_CHECKF(k2 && !strcmp(k1, k2), "name key is not idempotent");
            free(k2);
        }
        for (i = 0; i < len; i++) if ((unsigned char)exact[i] >= 0x80 || exact[i] == 0) ascii = 0;
        if (ascii && n1) HUNT_CHECK(!strcmp(n1, exact));                       /* ASCII is its own NFC */
        if (ascii && k1) {
            char *low = hunt_strdup(exact);
            for (i = 0; low[i]; i++) if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] + 32);
            HUNT_CHECKF(!strcmp(k1, low), "name key of ASCII is its lower case");
            free(low);
        }
        free(n1); free(k1);
        hunt_clear_input();
        free(exact);
    }
}

/* ---- E. text that is slow to normalize ---- */

static double secs(clock_t a) { return (double)(clock() - a) / CLOCKS_PER_SEC; }

static void test_slow_text(void) {
    size_t marks = 60000, i, len = 1 + marks * 2;
    char *t = (char *)malloc(len + 1), *k;
    clock_t t0;
    double s;

    hunt_step("nfc timing: 60000 combining marks of two different classes in the worst order");
    t[0] = 'a';
    for (i = 0; i < marks; i++) {
        /* U+0301 (class 230) then U+0323 (class 220): every pair is out of order and reordering moves a mark past all the earlier ones */
        if (i % 2 == 0) { t[1 + i * 2] = (char)0xCC; t[2 + i * 2] = (char)0x81; } else { t[1 + i * 2] = (char)0xCC; t[2 + i * 2] = (char)0xA3; }
    }
    t[len] = 0;
    t0 = clock();
    k = ex_nfc(t, len);
    s = secs(t0);
    HUNT_SUSPECT(s < 2.0, "NFC of 60000 combining marks took more than 2 seconds of CPU: reordering is quadratic, and a link target in a note has no length limit");
    if (s >= 2.0) hunt_note_("took %.2f s", s);
    free(k);

    hunt_step("name key timing: 300000 bytes of one repeated cased letter");
    {
        size_t n = 300000;
        char *u = (char *)malloc(n + 1);
        memset(u, 'Q', n); u[n] = 0;
        t0 = clock();
        k = ex_name_key(u, n);
        s = secs(t0);
        HUNT_SUSPECT(s < 1.0, "name key of 300 KB of ASCII took more than a second");
        free(k); free(u);
    }
    free(t);
}

int main(int argc, char **argv) {
    hunt_begin("hunt_unicode", argc, argv);
    test_decode_exhaustive();
    test_encode();
    test_grapheme();
    test_nfc();
    test_slow_text();
    return hunt_end();
}
