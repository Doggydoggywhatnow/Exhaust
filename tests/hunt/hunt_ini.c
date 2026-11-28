/* hunt_ini: the INI reader, writer and the in-place patcher (src/util/ini.c).
 *
 * Settings, hotkeys, workspaces, the session, themes' sibling files, plugin approvals and the vault registry all go through this module, and the
 * rules in AGENTS.md are strict: "never reformat what the user did not edit", "keep keys Exhaust does not recognize and write them back
 * unchanged", "never write to a note except through a user action" (and plugins.ini decides which plugins may run). This test
 *   A. generates INI files of every shape (comments, blanks, CRLF or LF, BOM, no final newline, duplicate keys and sections, garbage lines, headers with
 *      trailing text, indentation) and checks ex_ini_patch against ex_ini_parse as an oracle: the patched file reads back with the new value, every other
 *      value is unchanged, no other key appears, all other lines are byte for byte as they were, line endings and the BOM survive, and patching twice
 *      changes nothing;
 *   B. parse -> write -> parse is a fixed point for every model the parser can produce;
 *   C. tries values and keys the writer cannot represent (line breaks, `=`, ` #`, `]`, very long names, leading and trailing blanks): it must refuse them
 *      or round-trip them, never write a file that reads back as something else (an injected line is a security problem for plugins.ini);
 *   D. checks the typed getters on awkward numbers and spellings.
 * Link: exhaust_ini. */
#include "hunt.h"

#include "exhaust_ini.h"

/* ---- canonical dump of a model ---- */
static char *dump(const ExIni *ini, size_t *len) {
    size_t cap = 256, n = 0, i, k;
    char *out = (char *)malloc(cap);
    for (i = 0; i < ex_ini_section_count(ini); i++) {
        const char *sn = ex_ini_section_name(ini, i);
        for (k = 0; k < ex_ini_key_count(ini, sn); k++) {
            const char *kn = ex_ini_key_name(ini, sn, k), *v = ex_ini_get(ini, sn, kn);
            size_t need = strlen(sn) + strlen(kn) + (v ? strlen(v) : 6) + 8;
            if (n + need > cap) { cap = (n + need) * 2; out = (char *)realloc(out, cap); }
            n += (size_t)sprintf(out + n, "[%s]\x1f%s\x1f%s\n", sn, kn, v ? v : "(null)");
        }
        if (ex_ini_key_count(ini, sn) == 0) { if (n + strlen(sn) + 16 > cap) { cap = (n + strlen(sn) + 16) * 2; out = (char *)realloc(out, cap); } n += (size_t)sprintf(out + n, "[%s]\x1f(empty)\n", sn); }
    }
    out[n] = 0;
    if (len) *len = n;
    return out;
}

/* ---- A. generated files ---- */

static const char *const KEYS[] = { "alpha", "beta", "gamma", "font_size", "path", "x", "Key", "my key", "dotted.key", "a-b" };
static const char *const SECS[] = { "general", "editor", "window.1", "plugin.demo", "Files", "a b", "" };
static const char *const VALS[] = { "x", "true", "42", "a b", "#ff00aa", "\xC3\xA9t\xC3\xA9", "", "a=b", "path/to/Note.md", "[x]", "1.5", "-3", "yes", "a;b", "C:\\dir\\f" };

static size_t gen_ini(char *out, size_t cap) {
    size_t n = 0;
    int lines = (int)hunt_rnd(14), i, crlf = hunt_chance(25), bom = hunt_chance(12);
    const char *eol = crlf ? "\r\n" : "\n";
    if (bom) { memcpy(out, "\xEF\xBB\xBF", 3); n = 3; }
    for (i = 0; i < lines && n + 200 < cap; i++) {
        int k = (int)hunt_rnd(100);
        const char *sec = SECS[hunt_rnd(sizeof SECS / sizeof *SECS)], *key = KEYS[hunt_rnd(sizeof KEYS / sizeof *KEYS)], *val = VALS[hunt_rnd(sizeof VALS / sizeof *VALS)];
        if (k < 14) n += (size_t)sprintf(out + n, "[%s]%s%s", sec, hunt_chance(15) ? " # trailing" : "", eol);
        else if (k < 60) n += (size_t)sprintf(out + n, "%s%s%s%s%s%s%s", hunt_chance(10) ? "  " : "", key, hunt_chance(70) ? " = " : "=", val, hunt_chance(15) ? " # comment" : "", hunt_chance(5) ? "  " : "", eol);
        else if (k < 70) n += (size_t)sprintf(out + n, "# a comment with = sign%s", eol);
        else if (k < 76) n += (size_t)sprintf(out + n, "; another comment%s", eol);
        else if (k < 84) n += (size_t)sprintf(out + n, "%s", eol);
        else if (k < 88) n += (size_t)sprintf(out + n, "garbage line without equals%s", eol);
        else if (k < 91) n += (size_t)sprintf(out + n, "[unclosed section%s", eol);
        else if (k < 94) n += (size_t)sprintf(out + n, "=novalue key%s", eol);
        else if (k < 97) n += (size_t)sprintf(out + n, "\t%s\t=\t%s%s", key, val, eol);
        else n += (size_t)sprintf(out + n, "%s = %s", key, val);              /* no line end here: only the last line of a file can be like this */
    }
    if (n > 0 && hunt_chance(70) && out[n - 1] != '\n') n += (size_t)sprintf(out + n, "%s", eol);
    out[n] = 0;
    return n;
}

/* Splits into lines, each with its terminator. */
static size_t split_lines(const char *t, size_t len, size_t *starts, size_t max) {
    size_t n = 0, p = 0;
    while (p < len && n < max) { starts[n++] = p; while (p < len && t[p] != '\n') p++; if (p < len) p++; }
    starts[n] = len;
    return n;
}

static int has_pair(const ExIni *ini, const char *s, const char *k) { return ex_ini_get(ini, s, k) != NULL; }

static void check_patch(const char *text, size_t len, const char *sec, const char *key, const char *val) {
    size_t outlen = 0, olen = 0, i, k;
    char *patched = ex_ini_patch(text, len, sec, key, val, &outlen);
    ExIni *before, *after;
    char *again;
    size_t alen = 0;
    int weird_key = strchr(key, ' ') || strchr(key, '\t');
    int weird_val = !*val ? 0 : (val[0] == ' ' || strstr(val, " #") != NULL);
    (void)olen;
    HUNT_CHECK(patched != NULL);
    if (!patched) return;
    HUNT_CHECK(strlen(patched) == outlen || memchr(patched, 0, outlen) != NULL);
    before = ex_ini_parse(text, len);
    after = ex_ini_parse(patched, outlen);

    if (weird_key) {
        HUNT_SUSPECT(ex_ini_get(after, sec, key) != NULL && !strcmp(ex_ini_get(after, sec, key), val), "patching a key that has a space in it should still change that key");
    } else if (!weird_val) {
        const char *got = ex_ini_get(after, sec, key);
        HUNT_CHECKF(got && !strcmp(got, val), "after patching [%s] %s = %s, the file reads back %s", sec, key, val, got ? got : "(missing)");
    }
    /* every other value is as it was */
    for (i = 0; i < ex_ini_section_count(before); i++) {
        const char *sn = ex_ini_section_name(before, i);
        for (k = 0; k < ex_ini_key_count(before, sn); k++) {
            const char *kn = ex_ini_key_name(before, sn, k);
            if (!strcmp(sn, sec) && !strcmp(kn, key)) continue;
            HUNT_CHECKF(ex_ini_get(after, sn, kn) && !strcmp(ex_ini_get(after, sn, kn), ex_ini_get(before, sn, kn)), "patching [%s] %s changed [%s] %s from \"%s\" to \"%s\"", sec, key, sn, kn, ex_ini_get(before, sn, kn), ex_ini_get(after, sn, kn) ? ex_ini_get(after, sn, kn) : "(missing)");
        }
    }
    /* no other key appears */
    for (i = 0; i < ex_ini_section_count(after); i++) {
        const char *sn = ex_ini_section_name(after, i);
        for (k = 0; k < ex_ini_key_count(after, sn); k++) {
            const char *kn = ex_ini_key_name(after, sn, k);
            if (!strcmp(sn, sec) && !strcmp(kn, key)) continue;
            HUNT_CHECKF(has_pair(before, sn, kn), "patching [%s] %s made [%s] %s appear", sec, key, sn, kn);
        }
    }
    /* byte for byte: the same lines, except the one that changed or the lines added at the end of a section or the file */
    {
        /* the byte-order mark belongs to the first line; compare the lines after it, as the BOM stays at the top of the file whatever is put in front of the old first line */
        const char *btext = text, *bpatched = patched;
        size_t blen = len, boutlen = outlen;
        if (blen >= 3 && !memcmp(btext, "\xEF\xBB\xBF", 3)) { btext += 3; blen -= 3; if (boutlen >= 3 && !memcmp(bpatched, "\xEF\xBB\xBF", 3)) { bpatched += 3; boutlen -= 3; } }
        size_t ls[64], ps[64], nl = split_lines(btext, blen, ls, 62), np = split_lines(bpatched, boutlen, ps, 62), same_prefix = 0, same_suffix = 0;
        if (nl < 62 && np < 62) {
            while (same_prefix < nl && same_prefix < np && ls[same_prefix + 1] - ls[same_prefix] == ps[same_prefix + 1] - ps[same_prefix] &&
                   !memcmp(btext + ls[same_prefix], bpatched + ps[same_prefix], ls[same_prefix + 1] - ls[same_prefix])) same_prefix++;
            while (same_suffix < nl - same_prefix && same_suffix < np - same_prefix &&
                   ls[nl - same_suffix] - ls[nl - same_suffix - 1] == ps[np - same_suffix] - ps[np - same_suffix - 1] &&
                   !memcmp(btext + ls[nl - same_suffix - 1], bpatched + ps[np - same_suffix - 1], ls[nl - same_suffix] - ls[nl - same_suffix - 1])) same_suffix++;
            /* what is left in the middle may be one changed line, or one added line (or two when the last line had no line end) */
            /* a section that the file does not have is added at the end with its header and a blank line before it: up to three lines, and the last old line when it had no line ending and now has one */
            int has_sec = 0;
            for (i = 0; i < ex_ini_section_count(before); i++) if (!strcmp(ex_ini_section_name(before, i), sec)) has_sec = 1;
            HUNT_CHECKF(nl - same_prefix - same_suffix <= 1 && np - same_prefix - same_suffix <= (has_sec || !sec[0] ? 2u : 3u + (blen > 0 && btext[blen - 1] != '\n' ? 1u : 0u)), "patching [%s] %s touched %lu old lines and %lu new lines", sec, key, (unsigned long)(nl - same_prefix - same_suffix), (unsigned long)(np - same_prefix - same_suffix));
        }
    }
    /* the BOM and the line ending style */
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) HUNT_CHECK(outlen >= 3 && !memcmp(patched, "\xEF\xBB\xBF", 3));
    else HUNT_CHECK(outlen < 3 || memcmp(patched, "\xEF\xBB\xBF", 3) != 0);
    {
        const char *nl = (const char *)memchr(text, '\n', len);
        int was_crlf = nl && nl > text && nl[-1] == '\r', bare = 0;
        for (i = 0; i < outlen; i++) if (patched[i] == '\n' && (i == 0 || patched[i - 1] != '\r')) bare = 1;
        if (was_crlf) {
            int all_crlf = 1;
            for (i = 0; i < len; i++) if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) all_crlf = 0;
            if (all_crlf) HUNT_CHECKF(!bare, "patching a CRLF file wrote a bare LF");
        }
    }
    /* patching again changes nothing */
    again = ex_ini_patch(patched, outlen, sec, key, val, &alen);
    if (!weird_key && !weird_val) HUNT_CHECKF(again && alen == outlen && !memcmp(again, patched, outlen), "patching twice with the same value changed the file");
    free(again);
    ex_ini_free(before); ex_ini_free(after);
    free(patched);
}

static void test_patch(void) {
    long rounds = hunt_iters(6000), r;
    for (r = 0; r < rounds; r++) {
        char text[2400];
        size_t len = gen_ini(text, sizeof text);
        const char *sec = SECS[hunt_rnd(sizeof SECS / sizeof *SECS)], *key = KEYS[hunt_rnd(sizeof KEYS / sizeof *KEYS)], *val = VALS[hunt_rnd(sizeof VALS / sizeof *VALS)];
        char *exact = hunt_dup(text, len);
        hunt_stepf("ini patch %ld [%s] %s", r, sec, key);
        hunt_set_input(exact, len, "ini text");
        check_patch(exact, len, sec, key, val);
        hunt_clear_input();
        free(exact);
    }

    hunt_step("patch: empty text, only a BOM, only blank lines, a file without a section");
    check_patch("", 0, "general", "a", "1");
    check_patch("", 0, "", "a", "1");
    check_patch("\xEF\xBB\xBF", 3, "general", "a", "1");
    check_patch("\n\n\n", 3, "general", "a", "1");
    check_patch("a = 1", 5, "", "a", "2");
    check_patch("a = 1", 5, "s", "a", "2");
    check_patch("[s]\na = 1", 9, "s", "b", "2");
    check_patch("[s]\r\na = 1", 10, "s", "b", "2");
    check_patch("[s]\na = 1 # note\n", 17, "s", "a", "2");
    check_patch("[s]\na = #fff\n", 13, "s", "a", "#000");
    check_patch("[s]\n[t]\n[s]\na = 1\n", 18, "s", "b", "2");
}

/* ---- B. parse, write, parse ---- */

static void test_roundtrip(void) {
    long rounds = hunt_iters(4000), r;
    for (r = 0; r < rounds; r++) {
        char text[2400];
        size_t len = gen_ini(text, sizeof text), wl = 0, d1l, d2l;
        ExIni *a, *b;
        char *written, *d1, *d2;
        hunt_stepf("ini round trip %ld", r);
        a = ex_ini_parse(text, len);
        HUNT_CHECK(a != NULL);
        if (!a) continue;
        written = ex_ini_write(a, &wl);
        HUNT_CHECK(written != NULL && strlen(written) == wl);
        b = ex_ini_parse(written, wl);
        d1 = dump(a, &d1l); d2 = dump(b, &d2l);
        HUNT_CHECKF(d1l == d2l && !memcmp(d1, d2, d1l), "parse, write, parse changed the model\n--- file\n%s\n--- first\n%s--- written\n%s--- second\n%s", text, d1, written, d2);
        /* order of sections and keys is the order of the file */
        {
            char *w2 = ex_ini_write(b, NULL);
            HUNT_CHECK(w2 && !strcmp(w2, written));
            free(w2);
        }
        free(d1); free(d2); free(written);
        ex_ini_free(a); ex_ini_free(b);
    }
    hunt_step("parse: no input at all, NULs, huge lines");
    {
        ExIni *i = ex_ini_parse("", 0);
        HUNT_CHECK(i && ex_ini_section_count(i) == 0);
        ex_ini_free(i);
        i = ex_ini_parse("a = b\0c = d\n", 12);               /* a NUL in the middle: the reader works with C strings; it must not run past the end */
        HUNT_CHECK(i != NULL);
        ex_ini_free(i);
        {
            size_t n = 1 << 20;
            char *big = (char *)malloc(n + 16);
            memset(big, 'k', n);
            memcpy(big + n, " = v\n", 6);
            i = ex_ini_parse(big, n + 5);
            HUNT_CHECK(i != NULL);
            ex_ini_free(i);
            memset(big, ' ', n);
            i = ex_ini_parse(big, n);
            HUNT_CHECK(i != NULL);
            ex_ini_free(i);
            free(big);
        }
        {
            char longsec[600];
            ExIni *j;
            memset(longsec, 's', 599); longsec[599] = 0;
            { char *t = (char *)malloc(1000); int l = sprintf(t, "[%s]\nk = 1\n", longsec); j = ex_ini_parse(t, (size_t)l); free(t); }
            HUNT_CHECKF(ex_ini_section_count(j) == 1 && ex_ini_get(j, longsec, "k") != NULL, "a section name of 599 bytes is cut to 255 when read, so [%s...] cannot be found again", "sss");
            ex_ini_free(j);
        }
    }
}

/* ---- C. things the writer cannot represent ---- */

static void test_unrepresentable(void) {
    struct { const char *sec, *key, *val; const char *what; } c[] = {
        { "s", "k", "line one\nline two", "a value with a line break" },
        { "s", "k", "v\r\nother = injected", "a value with CRLF and a second key" },
        { "s", "k", "v\n[plugins]\nallow = true", "a value that injects a section" },
        { "s", "k", " leading", "a value with a leading space" },
        { "s", "k", "trailing ", "a value with a trailing space" },
        { "s", "k", "has # hash", "a value with a space and a hash (read as a comment)" },
        { "s", "k", "a#b", "a value with a hash and no space" },
        { "s", "k", "\t", "a value of a tab" },
        { "s", "k=x", "v", "a key with an equals sign" },
        { "s", "k\nm", "v", "a key with a line break" },
        { "s", " k", "v", "a key with a leading space" },
        { "s", "[k]", "v", "a key that looks like a header" },
        { "s", "#k", "v", "a key that looks like a comment" },
        { "s]x", "k", "v", "a section name with a bracket" },
        { "s\nx", "k", "v", "a section name with a line break" },
        { "", "k", "v", "the unnamed section" },
    };
    size_t i;
    for (i = 0; i < sizeof c / sizeof *c; i++) {
        ExIni *ini = ex_ini_new(), *back;
        char *w, *dumpa, *dumpb;
        int rc;
        size_t wl = 0;
        hunt_stepf("unrepresentable: %s", c[i].what);
        rc = ex_ini_set(ini, c[i].sec, c[i].key, c[i].val);
        w = ex_ini_write(ini, &wl);
        back = ex_ini_parse(w, wl);
        dumpa = dump(ini, NULL); dumpb = dump(back, NULL);
        if (rc == 0) {
            /* accepted: it must read back the same, or at least not be a different set of keys */
            size_t ka = 0, kb = 0, k;
            for (k = 0; k < ex_ini_section_count(ini); k++) ka += ex_ini_key_count(ini, ex_ini_section_name(ini, k));
            for (k = 0; k < ex_ini_section_count(back); k++) kb += ex_ini_key_count(back, ex_ini_section_name(back, k));
            HUNT_SUSPECT(ka == kb && ex_ini_section_count(ini) == ex_ini_section_count(back), "the writer accepted this and the file reads back with other keys or sections: a line injected into a file the user or a plugin controls");
            if (!(ka == kb && ex_ini_section_count(ini) == ex_ini_section_count(back))) hunt_note_("%s: set returned 0, wrote:\n%s", c[i].what, w);
            HUNT_SUSPECT(!strcmp(dumpa, dumpb), "the writer accepted this and the file reads back with a different value");
            if (strcmp(dumpa, dumpb)) hunt_note_("%s: wrote then read a different model", c[i].what);
        }
        free(dumpa); free(dumpb); free(w);
        ex_ini_free(ini); ex_ini_free(back);
    }

    hunt_step("patch with values the file cannot hold");
    {
        const char *vals[] = { "a\nb = injected", "a\r\n[x]", "v # c", " v", "v " };
        size_t i2;
        for (i2 = 0; i2 < sizeof vals / sizeof *vals; i2++) {
            const char *text = "[s]\nk = 1\nm = 2\n";
            char *p = ex_ini_patch(text, strlen(text), "s", "k", vals[i2], NULL);
            ExIni *back = ex_ini_parse(p, strlen(p));
            size_t keys = ex_ini_key_count(back, "s");
            HUNT_SUSPECT(keys == 2 && ex_ini_section_count(back) == 1 && ex_ini_get(back, "s", "m") && !strcmp(ex_ini_get(back, "s", "m"), "2"), "patching one key with this value must not add keys or sections, or change another key");
            if (!(keys == 2 && ex_ini_section_count(back) == 1)) hunt_note_("value %lu wrote:\n%s", (unsigned long)i2, p);
            ex_ini_free(back); free(p);
        }
    }

    hunt_step("set, get, remove with the same names in several sections");
    {
        ExIni *ini = ex_ini_new();
        ex_ini_set(ini, "a", "k", "1"); ex_ini_set(ini, "b", "k", "2"); ex_ini_set(ini, "a", "j", "3");
        HUNT_EQ_STR(ex_ini_get(ini, "a", "k"), "1");
        HUNT_EQ_STR(ex_ini_get(ini, "b", "k"), "2");
        ex_ini_set(ini, "a", "k", "9");
        HUNT_EQ_INT(ex_ini_key_count(ini, "a"), 2);
        ex_ini_remove_key(ini, "a", "k");
        HUNT_CHECK(ex_ini_get(ini, "a", "k") == NULL && ex_ini_get(ini, "b", "k") != NULL);
        ex_ini_remove_key(ini, "a", "nothing");
        ex_ini_remove_key(ini, "nothing", "k");
        ex_ini_remove_section(ini, "nothing");
        ex_ini_remove_section(ini, "a");
        HUNT_CHECK(!ex_ini_has_section(ini, "a") && ex_ini_has_section(ini, "b"));
        HUNT_CHECK(ex_ini_section_name(ini, 99) == NULL && ex_ini_key_name(ini, "b", 99) == NULL && ex_ini_key_name(ini, "zz", 0) == NULL);
        HUNT_EQ_INT(ex_ini_key_count(ini, "zz"), 0);
        ex_ini_free(ini);
        ex_ini_free(NULL);
    }
}

/* ---- D. typed getters ---- */

static void test_typed(void) {
    ExIni *ini = ex_ini_new();
    struct { const char *v; long long want; int exact; } ints[] = {
        { "42", 42, 1 }, { "-7", -7, 1 }, { "+5", 5, 1 }, { "0", 0, 1 }, { "-0", 0, 1 }, { "007", 7, 1 }, { "12abc", -999, 1 }, { "abc", -999, 1 }, { "", -999, 1 }, { " 12", 12, 1 },
        { "1.5", -999, 1 }, { "0x10", -999, 1 }, { "9223372036854775807", 9223372036854775807ll, 1 }, { "9223372036854775808", -999, 0 }, { "99999999999999999999999", -999, 0 },
        { "-9223372036854775808", (long long)(-9223372036854775807ll - 1), 1 }, { "-99999999999999999999999", -999, 0 }, { "1 2", -999, 1 },
    };
    const char *trues[] = { "true", "yes", "on", "1" }, *falses[] = { "false", "no", "off", "0" };
    const char *cased[] = { "True", "TRUE", "Yes", "YES", "On", "ON", "False", "FALSE", "No", "Off" };
    struct { const char *v; double want; int bad; } fl[] = {
        { "1.5", 1.5, 0 }, { "-2", -2, 0 }, { "1e3", 1000, 0 }, { ".5", 0.5, 0 }, { "5.", 5, 0 }, { "abc", -1, 1 }, { "1.5x", -1, 1 }, { "", -1, 1 },
        { "nan", -1, 2 }, { "inf", -1, 2 }, { "-infinity", -1, 2 }, { "1e999", -1, 2 }, { "0x1p3", -1, 2 }, { "1,5", -1, 1 },
    };
    size_t i;
    hunt_step("typed getters: integers");
    for (i = 0; i < sizeof ints / sizeof *ints; i++) {
        long long got;
        ex_ini_set(ini, "t", "v", ints[i].v);
        got = ex_ini_get_int(ini, "t", "v", -999);
        if (ints[i].exact) HUNT_CHECKF(got == ints[i].want, "get_int(\"%s\") = %lld, want %lld", ints[i].v, got, ints[i].want);
        else HUNT_SUSPECT(got == ints[i].want, "an integer that does not fit should give the default, not the largest number (a window of 9223372036854775807 pixels)");
    }
    HUNT_EQ_INT(ex_ini_get_int(ini, "t", "missing", 5), 5);
    HUNT_EQ_INT(ex_ini_get_int(ini, "nosection", "v", 6), 6);
    hunt_step("typed getters: booleans");
    for (i = 0; i < 4; i++) {
        ex_ini_set(ini, "t", "b", trues[i]);  HUNT_CHECKF(ex_ini_get_bool(ini, "t", "b", 0) == 1, "get_bool(%s)", trues[i]);
        ex_ini_set(ini, "t", "b", falses[i]); HUNT_CHECKF(ex_ini_get_bool(ini, "t", "b", 1) == 0, "get_bool(%s)", falses[i]);
    }
    ex_ini_set(ini, "t", "b", "maybe");
    HUNT_EQ_INT(ex_ini_get_bool(ini, "t", "b", 1), 1);
    HUNT_EQ_INT(ex_ini_get_bool(ini, "t", "b", 0), 0);
    for (i = 0; i < sizeof cased / sizeof *cased; i++) {
        int want = (cased[i][0] == 'T' || cased[i][0] == 'Y' || (cased[i][0] == 'O' && cased[i][1] == 'N')) ? 1 : 0, got;
        ex_ini_set(ini, "t", "b", cased[i]);
        got = ex_ini_get_bool(ini, "t", "b", !want);
        HUNT_SUSPECT(got == want, "a hand-edited `True` or `YES` should be read like `true` and `yes` (the documented spellings are lower case; the user types what Python and Windows taught them)");
    }
    hunt_step("typed getters: floats");
    for (i = 0; i < sizeof fl / sizeof *fl; i++) {
        double got;
        ex_ini_set(ini, "t", "f", fl[i].v);
        got = ex_ini_get_float(ini, "t", "f", -1);
        if (fl[i].bad == 0) HUNT_CHECKF(got == fl[i].want, "get_float(\"%s\") = %g, want %g", fl[i].v, got, fl[i].want);
        else if (fl[i].bad == 1) HUNT_CHECKF(got == -1, "get_float(\"%s\") should give the default", fl[i].v);
        else HUNT_SUSPECT(got == -1, "nan, inf, an overflowing number and hex floats should give the default: a font size of nan or inf reaches layout code as it is");
    }
    ex_ini_free(ini);
}

int main(int argc, char **argv) {
    hunt_begin("hunt_ini", argc, argv);
    test_patch();
    test_roundtrip();
    test_unrepresentable();
    test_typed();
    return hunt_end();
}
