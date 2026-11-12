/* shaping: grapheme clusters (against Unicode's own GraphemeBreakTest.txt),
 * bidi order, font fallback and glyph runs for Latin, Arabic, Hebrew, CJK,
 * emoji and combining marks, and the font scan. Usage: test_shaping <unicode-dir> <test-fonts-dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_fontdb.h"
#include "exhaust_platform.h"
#include "exhaust_shape.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- grapheme clusters: every line of GraphemeBreakTest.txt ---- */

static int test_grapheme_file(const char *dir) {
    char path[1024];
    size_t len = 0;
    char *data, *line, *next;
    int cases = 0;
    snprintf(path, sizeof path, "%s/GraphemeBreakTest.txt", dir);
    data = ex_platform_read_file(path, &len);
    if (!data) { fprintf(stderr, "cannot read %s\n", path); failures++; return 0; }
    for (line = data; *line; line = next) {
        char text[512];
        size_t tl = 0, expect[64], ne = 0, got[64], ng = 0, pos;
        char *p;
        next = strchr(line, '\n');
        if (next) *next++ = 0; else next = line + strlen(line);
        if (*line == '#' || !*line) continue;
        if ((p = strchr(line, '#'))) *p = 0;
        for (p = line; *p;) {
            if ((unsigned char)p[0] == 0xC3 && (unsigned char)p[1] == 0xB7) { expect[ne++] = tl; p += 2; }       /* ÷ break */
            else if ((unsigned char)p[0] == 0xC3 && (unsigned char)p[1] == 0x97) p += 2;                          /* × no break */
            else if (*p == ' ') p++;
            else {
                char *end;
                unsigned long cp = strtoul(p, &end, 16);
                if (end == p) { p++; continue; }
                tl += (size_t)ex_utf8_encode((uint32_t)cp, text + tl);
                p = end;
            }
        }
        /* expect[0] is the start of text and the last entry is the end */
        for (pos = 0; pos < tl;) {
            pos = ex_grapheme_next(text, tl, pos);
            got[ng++] = pos;
        }
        {
            size_t k, ok = (ne >= 2 && ng == ne - 1);
            for (k = 1; ok && k < ne; k++) ok = got[k - 1] == expect[k];
            if (!ok) { fprintf(stderr, "grapheme mismatch: %.100s\n", line); failures++; }
        }
        cases++;
    }
    free(data);
    fprintf(stderr, "grapheme cases: %d\n", cases);
    CHECK(cases > 700);
    return cases;
}

/* ---- helpers ---- */

static ExShaped shape(ExFontLib *lib, ExFont *f, int px, const char *s) {
    ExShaped sh;
    CHECK(ex_shape(lib, f, px, s, strlen(s), &sh) == 0);
    return sh;
}

static int run_has_family(const ExShaped *sh, size_t i, const char *family) {
    return i < sh->nruns && strcmp(ex_font_family(sh->runs[i].font), family) == 0;
}

int main(int argc, char **argv) {
    const char *unicode_dir = argc > 1 ? argv[1] : "third_party/unicode";
    const char *fonts_dir = argc > 2 ? argv[2] : "tests/fonts";
    ExFontLib *lib = ex_fontlib_create();
    ExFont *ui;
    ExFontDb *db;
    ExFontScanStats st;
    const char *dirs[1];
    ExShaped sh;
    size_t i;

    test_grapheme_file(unicode_dir);

    /* hand-written boundary checks */
    CHECK(ex_grapheme_next("e\xCC\x81x", 4, 0) == 3);                         /* e + combining acute */
    CHECK(ex_grapheme_next("\r\n", 2, 0) == 2);                               /* CR LF */
    CHECK(ex_grapheme_next("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8x", 9, 0) == 8);  /* flag: two regional indicators */
    CHECK(ex_grapheme_next("\xF0\x9F\x91\x8D\xE2\x80\x8D\xF0\x9F\x98\x80", 11, 0) == 11); /* emoji ZWJ sequence */
    CHECK(ex_grapheme_next("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", 9, 0) == 9); /* Devanagari conjunct (GB9c) */
    CHECK(ex_grapheme_next("ab", 2, 0) == 1 && ex_grapheme_next("ab", 2, 1) == 2 && ex_grapheme_next("ab", 2, 2) == 2);

    CHECK(lib != NULL);
    ui = ex_font_builtin(lib, EX_FONT_UI_REGULAR);
    CHECK(ui != NULL);

    /* the font scan finds fonts, and fallback uses them */
    dirs[0] = fonts_dir;
    db = ex_fontdb_scan(dirs, 1, "", &st);
    CHECK(db != NULL && st.faces >= 4 && st.files_opened >= 4);
    {
        ExFontDb *system = ex_fontdb_scan(NULL, 0, "", &st);
        fprintf(stderr, "system fonts found: %d faces in %d folders\n", st.faces, st.folders_visited);
        ex_fontdb_free(system);
    }
    ex_fontlib_set_db(lib, db, 1);

    /* Latin: one run, one glyph per letter, clusters are byte offsets */
    sh = shape(lib, ui, 20, "Hello");
    CHECK(sh.nglyphs == 5 && sh.nruns == 1 && sh.runs[0].level == 0 && sh.runs[0].script == EX_TAG('L', 'a', 't', 'n'));
    for (i = 0; i < sh.nglyphs; i++) CHECK(sh.glyphs[i].cluster == i && sh.glyphs[i].glyph != 0 && sh.glyphs[i].advance > 0);
    CHECK(sh.width > 40 && sh.width < 100 && !sh.base_rtl);
    ex_shaped_free(&sh);

    /* kerning from the font's GPOS table: AV is tighter than A + V */
    {
        ExShaped av = shape(lib, ui, 100, "AV"), a = shape(lib, ui, 100, "A"), v = shape(lib, ui, 100, "V");
        fprintf(stderr, "AV %.2f, A %.2f + V %.2f\n", av.width, a.width, v.width);
        CHECK(av.width < a.width + v.width);
        ex_shaped_free(&av); ex_shaped_free(&a); ex_shaped_free(&v);
    }

    /* combining mark: one cluster, both code points map to cluster 0 */
    sh = shape(lib, ui, 20, "e\xCC\x81z");
    CHECK(sh.nglyphs >= 2);
    CHECK(sh.glyphs[0].cluster == 0);
    CHECK(sh.glyphs[sh.nglyphs - 1].cluster == 3);
    for (i = 0; i + 1 < sh.nglyphs; i++) CHECK(sh.glyphs[i].cluster == 0);
    ex_shaped_free(&sh);

    /* Arabic: right to left run in a fallback font, joined forms */
    {
        const char *ar = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"; /* مرحبا */
        sh = shape(lib, ui, 24, ar);
        CHECK(sh.nruns == 1 && sh.runs[0].level == 1 && sh.base_rtl);
        CHECK(run_has_family(&sh, 0, "Noto Sans Arabic"));
        CHECK(sh.runs[0].script == EX_TAG('A', 'r', 'a', 'b'));
        /* the font splits some letters into base and dot (ccmp), so there are at least five glyphs */
        CHECK(sh.nglyphs >= 5 && sh.nglyphs <= 7);
        /* visual order is right to left: the first glyph on screen belongs to the last letter */
        CHECK(sh.glyphs[0].cluster == 8 && sh.glyphs[sh.nglyphs - 1].cluster == 0);
        for (i = 0; i + 1 < sh.nglyphs; i++) CHECK(sh.glyphs[i].cluster >= sh.glyphs[i + 1].cluster);
        /* shaping chose joining forms: the first letter (initial form) is not its nominal glyph */
        {
            ExFont *af = sh.runs[0].font;
            CHECK(sh.glyphs[sh.nglyphs - 1].glyph != ex_font_glyph_index(af, 0x0645));
        }
        ex_shaped_free(&sh);
    }

    /* Hebrew: right to left, no joining, glyph per letter */
    sh = shape(lib, ui, 24, "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"); /* שלום */
    CHECK(sh.nruns == 1 && sh.runs[0].level == 1 && sh.nglyphs == 4 && run_has_family(&sh, 0, "Noto Sans Hebrew"));
    CHECK(sh.glyphs[0].cluster == 6 && sh.glyphs[3].cluster == 0);
    ex_shaped_free(&sh);

    /* mixed bidi, left to right paragraph: abc | Hebrew | def */
    {
        const char *t = "abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D def";
        sh = shape(lib, ui, 20, t);
        CHECK(!sh.base_rtl && sh.nruns >= 3);
        CHECK(sh.runs[0].text_start == 0 && sh.runs[0].level == 0);
        CHECK(sh.runs[sh.nruns - 1].text_end == strlen(t) && sh.runs[sh.nruns - 1].level == 0);
        {
            int saw_rtl = 0;
            for (i = 0; i < sh.nruns; i++) if (sh.runs[i].level == 1) { saw_rtl = 1; CHECK(run_has_family(&sh, i, "Noto Sans Hebrew")); }
            CHECK(saw_rtl);
        }
        ex_shaped_free(&sh);
    }

    /* right to left paragraph (first strong character is Hebrew): the Latin word
     * is on the left on screen but later in the text */
    {
        const char *t = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D abc";
        sh = shape(lib, ui, 20, t);
        CHECK(sh.base_rtl && sh.nruns >= 2);
        CHECK(sh.runs[0].text_end == strlen(t));                 /* first on screen: the end of the text */
        CHECK(sh.runs[0].level == 2);                            /* Latin inside RTL: level 2 */
        CHECK(sh.runs[sh.nruns - 1].text_start == 0);            /* last on screen: the Hebrew at the start */
        ex_shaped_free(&sh);
    }

    /* CJK: fallback font, one glyph per ideograph, Han script */
    sh = shape(lib, ui, 24, "\xE4\xBD\xA0\xE5\xA5\xBD"); /* 你好 */
    CHECK(sh.nruns == 1 && sh.nglyphs == 2 && run_has_family(&sh, 0, "Noto Sans SC"));
    CHECK(sh.runs[0].script == EX_TAG('H', 'a', 'n', 'i') && sh.glyphs[0].glyph != 0 && sh.glyphs[1].cluster == 3);
    ex_shaped_free(&sh);

    /* emoji: color font, own run, between two Latin runs */
    sh = shape(lib, ui, 24, "a\xF0\x9F\x98\x80" "b");
    CHECK(sh.nruns == 3 && sh.nglyphs == 3);
    CHECK(!sh.runs[0].color && sh.runs[1].color && !sh.runs[2].color);
    CHECK(run_has_family(&sh, 1, "Noto Color Emoji") && sh.glyphs[1].cluster == 1 && sh.glyphs[2].cluster == 5);
    CHECK(sh.glyphs[1].glyph != 0);
    {
        ExGlyphBitmap bm;
        CHECK(ex_font_rasterize(sh.runs[1].font, sh.glyphs[1].glyph, 24, &bm) == 0);
        CHECK(bm.color && bm.rgba && bm.width > 8 && bm.height > 8 && bm.width <= 40);
        {
            long opaque = 0;
            int k;
            for (k = 0; k < bm.width * bm.height; k++) if (bm.rgba[k * 4 + 3] > 200) opaque++;
            CHECK(opaque > 50);
        }
    }
    ex_shaped_free(&sh);

    /* emoji ZWJ sequence stays one cluster in one run */
    sh = shape(lib, ui, 24, "\xF0\x9F\x91\x8D\xE2\x80\x8D\xF0\x9F\x98\x80");
    CHECK(sh.nruns == 1 && sh.runs[0].color && sh.runs[0].text_end == 11);
    ex_shaped_free(&sh);

    /* a code point no font has: the primary font's missing-glyph box, no crash */
    sh = shape(lib, ui, 20, "\xE0\xB8\x81"); /* Thai KO KAI */
    CHECK(sh.nruns == 1 && sh.nglyphs >= 1 && sh.glyphs[0].glyph == 0 && sh.glyphs[0].font == ui);
    ex_shaped_free(&sh);

    /* control characters and empty input */
    sh = shape(lib, ui, 20, "a\tb\n");
    CHECK(sh.nglyphs >= 2);
    ex_shaped_free(&sh);
    CHECK(ex_shape(lib, ui, 20, "", 0, &sh) == 0 && sh.nglyphs == 0 && sh.nruns == 0);
    {   /* malformed UTF-8 */
        const char bad[] = "a\xFF\xC3z";
        CHECK(ex_shape(lib, ui, 20, bad, sizeof bad - 1, &sh) == 0 && sh.nglyphs >= 3);
        ex_shaped_free(&sh);
    }

    ex_fontlib_destroy(lib);
    return failures ? 1 : 0;
}
