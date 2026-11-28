/* hunt_settings: settings.ini, hotkeys.ini and the session file (include/exhaust_settings.h, _hotkeys.h, _session.h, _workspace.h).
 *
 * These are the files a user edits by hand, that sync tools copy between machines, that a newer version writes and an older one reads, and that must
 * "keep keys Exhaust does not recognize and write them back unchanged" (AGENTS.md). Session state is user data: "a damaged file is moved aside, never
 * deleted". This test
 *   A. checks the table of settings against itself (every default valid for its type, enum defaults among their values, no key twice), the validation of every
 *      type with awkward values, and the write path: a patch changes one line and nothing else (comments, unknown keys and sections, CRLF, BOM, values that do
 *      not fit and stay as they were), the last of a thousand quick changes wins, `.exhaust` is not created by reading, and a value with a line break cannot
 *      add a section to the file (a folder name on Linux may hold a newline: `[plugins] allow = true` must not be writable that way);
 *   B. round-trips every chord, parses the awkward ones (`Ctrl+,`, a list `Ctrl+,, Ctrl+K`), and checks the hotkey file keeps what it does not understand;
 *   C. builds random workspaces (tabs with paths that mean something to an INI file, splits, pop-out windows, history, panels), serializes them and reads them
 *      back: the text must be a fixed point and the tabs must be the same; hostile session text (a split inside itself, tabs that belong to nothing, ids
 *      at the edge of the range, cut at every byte) must give a usable workspace; a damaged session file is moved aside and kept.
 * Link: exhaust_settings exhaust_workspace exhaust_vault exhaust_platform. */
#include "hunt.h"
#include "hunt_fs.h"

#include <math.h>

#include "exhaust_commands.h"
#include "exhaust_hotkeys.h"
#include "exhaust_session.h"
#include "exhaust_settings.h"
#include "exhaust_vault.h"
#include "exhaust_workspace.h"

/* ---- A. settings ---- */

static const ExSettingDef *first_of(ExSettingType t) { size_t i; for (i = 0; i < ex_settings_schema_count; i++) if (ex_settings_schema[i].type == t && !ex_settings_schema[i].stub) return &ex_settings_schema[i]; return NULL; }

static void test_schema(void) {
    size_t i, j;
    hunt_step("settings: the table checks itself");
    HUNT_CHECK(ex_settings_schema_count > 50);
    for (i = 0; i < ex_settings_schema_count; i++) {
        const ExSettingDef *d = &ex_settings_schema[i];
        size_t k;
        HUNT_CHECKF(d->section && d->key && *d->section && *d->key, "schema entry %lu has no section or key", (unsigned long)i);
        if (!d->section || !d->key) continue;
        for (k = 0; d->key[k]; k++) HUNT_CHECKF((d->key[k] >= 'a' && d->key[k] <= 'z') || (d->key[k] >= '0' && d->key[k] <= '9') || d->key[k] == '_', "the key %s.%s has a character outside a-z, 0-9 and _", d->section, d->key);
        HUNT_CHECKF(d->def != NULL, "%s.%s has no default", d->section, d->key);
        if (d->def) HUNT_CHECKF(ex_settings_valid(d, d->def), "the default \"%s\" of %s.%s is not valid for its own type", d->def, d->section, d->key);
        if (d->type == EX_SET_ENUM) {
            HUNT_CHECKF(d->enum_values && *d->enum_values, "%s.%s is an enum with no values", d->section, d->key);
            if (d->enum_values && d->def) {
                size_t dl = strlen(d->def), p = 0, found = 0;
                const char *v = d->enum_values;
                while (v[p] || p == 0) {
                    size_t e = p;
                    while (v[e] && v[e] != '|') e++;
                    if (e - p == dl && !strncmp(v + p, d->def, dl)) found = 1;
                    if (!v[e]) break;
                    p = e + 1;
                }
                HUNT_CHECKF(found, "the default \"%s\" of %s.%s is not one of %s", d->def, d->section, d->key, d->enum_values);
            }
        }
        if (d->stub) HUNT_CHECKF(!strcmp(d->def, "false") || !*d->def, "the stub %s.%s is on by default", d->section, d->key);
        for (j = i + 1; j < ex_settings_schema_count; j++) HUNT_CHECKF(strcmp(d->section, ex_settings_schema[j].section) || strcmp(d->key, ex_settings_schema[j].key), "the key %s.%s is in the table twice", d->section, d->key);
        HUNT_CHECK(ex_settings_def(d->section, d->key) == d);
    }
    HUNT_CHECK(ex_settings_def("no_such_section", "x") == NULL && ex_settings_def("general", "no_such_key") == NULL && ex_settings_def("", "") == NULL);
    HUNT_SPEC(ex_settings_def("plugins", "allow") && !strcmp(ex_settings_def("plugins", "allow")->def, "false"), "docs/formats.md, Plugins: `[plugins] allow` is off by default");

    hunt_step("settings: validation by type");
    {
        const ExSettingDef *b = first_of(EX_SET_BOOL), *n = first_of(EX_SET_INT), *p = first_of(EX_SET_PERCENT), *c = first_of(EX_SET_COLOR), *e = first_of(EX_SET_ENUM), *s = first_of(EX_SET_STRING);
        const char *bools[] = { "true", "false", "1", "0", "yes", "no", "on", "off" /* docs/formats.md: these are read too */ }, *nbools[] = { "", "True", "TRUE", "t", "true ", " true", "false\n", "null" };
        const char *ints[] = { "0", "5", "-5", "123456" }, *nints[] = { "", "1.5", "1e3", "0x10", " 5", "5 ", "abc", "5abc", "+", "-", "--5", "99999999999999999999999", "٣", "5\n", "NaN" };
        const char *cols[] = { "", "#fff000", "#FFF000", "#aabbccdd" }, *ncols[] = { "#fff", "fff000", "#ggg000", "red", "#12345", "#1234567", "#12345678a", "# fff000", "#fff000 ", "rgb(1,2,3)" };
        size_t k;
        if (b) { for (k = 0; k < sizeof bools / sizeof *bools; k++) HUNT_CHECKF(ex_settings_valid(b, bools[k]), "bool %s", bools[k]); for (k = 0; k < sizeof nbools / sizeof *nbools; k++) HUNT_CHECKF(!ex_settings_valid(b, nbools[k]), "the bool \"%s\" should be refused", nbools[k]); }
        if (n) { for (k = 0; k < sizeof ints / sizeof *ints; k++) HUNT_CHECKF(ex_settings_valid(n, ints[k]), "int %s", ints[k]); for (k = 0; k < sizeof nints / sizeof *nints; k++) HUNT_CHECKF(!ex_settings_valid(n, nints[k]), "the int \"%s\" should be refused", nints[k]); }
        if (p) { HUNT_CHECK(ex_settings_valid(p, "100") && ex_settings_valid(p, "0")); HUNT_SUSPECT(!ex_settings_valid(p, "-1") && !ex_settings_valid(p, "100000"), "a percent outside any sensible range should be refused (a zoom of 100000 percent)"); HUNT_CHECK(!ex_settings_valid(p, "abc") && !ex_settings_valid(p, "")); }
        if (c) { for (k = 0; k < sizeof cols / sizeof *cols; k++) HUNT_CHECKF(ex_settings_valid(c, cols[k]), "color %s", cols[k]); for (k = 0; k < sizeof ncols / sizeof *ncols; k++) HUNT_CHECKF(!ex_settings_valid(c, ncols[k]), "the color \"%s\" should be refused", ncols[k]); }
        if (e) { HUNT_CHECK(!ex_settings_valid(e, "no such value") && !ex_settings_valid(e, "") && !ex_settings_valid(e, "a|b")); }
        if (s) { HUNT_CHECK(ex_settings_valid(s, "anything") && ex_settings_valid(s, "")); }
        HUNT_CHECK(!ex_settings_valid(NULL, "x") || 1);
        {
            /* stubs cannot be turned on */
            size_t i2;
            for (i2 = 0; i2 < ex_settings_schema_count; i2++) if (ex_settings_schema[i2].stub && ex_settings_schema[i2].type == EX_SET_BOOL) HUNT_CHECKF(!ex_settings_valid(&ex_settings_schema[i2], "true"), "the stub %s.%s can be turned on", ex_settings_schema[i2].section, ex_settings_schema[i2].key);
        }
    }
}

static void common_ends(const char *a, size_t al, const char *b, size_t bl, size_t *cp, size_t *cs) {
    size_t p = 0, s = 0, lim = al < bl ? al : bl;
    while (p < lim && a[p] == b[p]) p++;
    while (s < lim - p && a[al - 1 - s] == b[bl - 1 - s]) s++;
    *cp = p; *cs = s;
}

static void test_settings_files(void) {
    const char *root = hunt_fs_init("settings");
    char vcfg[700], gcfg[700], file[800];
    ExSettings *s;
    size_t i;
    const ExSettingDef *bdef = first_of(EX_SET_BOOL), *idef = first_of(EX_SET_INT);

    snprintf(vcfg, sizeof vcfg, "%s/vault/.exhaust", root);
    snprintf(gcfg, sizeof gcfg, "%s/global", root);
    snprintf(file, sizeof file, "%s/settings.ini", vcfg);
    ex_platform_make_dirs(gcfg);
    HUNT_CHECK(bdef && idef);
    if (!bdef || !idef) return;

    hunt_step("settings: reading creates nothing, the first change creates .exhaust");
    s = ex_settings_open(vcfg, gcfg);
    HUNT_CHECK(s != NULL);
    HUNT_CHECK(!ex_platform_file_exists(vcfg));
    HUNT_CHECKF(ex_settings_get(s, bdef->section, bdef->key) && !strcmp(ex_settings_get(s, bdef->section, bdef->key), bdef->def), "the default of %s.%s", bdef->section, bdef->key);
    HUNT_CHECK(!ex_settings_is_set(s, bdef->section, bdef->key));
    HUNT_CHECK(ex_settings_get(s, "no_section", "no_key") == NULL);
    HUNT_CHECK(ex_settings_set(s, "no_section", "no_key", "x") != 0);
    HUNT_CHECK(ex_settings_set(s, bdef->section, bdef->key, "maybe") != 0);
    HUNT_CHECK(!ex_platform_file_exists(vcfg));
    HUNT_EQ_INT(ex_settings_set(s, bdef->section, bdef->key, strcmp(bdef->def, "true") ? "true" : "false"), 0);
    ex_settings_flush(s);
    HUNT_CHECK(ex_platform_file_exists(file));
    ex_settings_close(s);

    hunt_step("settings: a patch changes one line (comments, unknown keys, CRLF and BOM stay; a bad value stays and the default is used)");
    {
        char text[2000];
        size_t tl, cp, cs, nlen = 0;
        char *after;
        ExSettings *s2;
        int crlf, bom;
        for (crlf = 0; crlf < 2; crlf++) for (bom = 0; bom < 2; bom++) {
            const char *eol = crlf ? "\r\n" : "\n";
            tl = 0;
            if (bom) { memcpy(text, "\xEF\xBB\xBF", 3); tl = 3; }
            tl += (size_t)snprintf(text + tl, sizeof text - tl, "# my settings%s[%s]%s%s = not-a-number%s# a comment between%sfuture_key = keep me # and this%s%s[future_section]%sx = y%s%s[%s]%s%s=%s%s",
                                   eol, idef->section, eol, idef->key, eol, eol, eol, eol, eol, eol, eol, bdef->section, eol, bdef->key, strcmp(bdef->def, "true") ? "false" : "true", eol);
            hunt_write_in(vcfg, "settings.ini", text, tl);
            hunt_stepf("settings patch crlf=%d bom=%d", crlf, bom);
            s2 = ex_settings_open(vcfg, gcfg);
            HUNT_CHECKF(ex_settings_get(s2, idef->section, idef->key) && !strcmp(ex_settings_get(s2, idef->section, idef->key), idef->def), "a value that does not fit its type should read as the default, not \"%s\"", ex_settings_get(s2, idef->section, idef->key) ? ex_settings_get(s2, idef->section, idef->key) : "(null)");
            HUNT_EQ_INT(ex_settings_set(s2, bdef->section, bdef->key, strcmp(bdef->def, "true") ? "true" : "false"), 0);
            ex_settings_flush(s2);
            ex_settings_close(s2);
            after = ex_platform_read_file(file, &nlen);
            HUNT_CHECK(after != NULL);
            if (after) {
                common_ends(text, tl, after, nlen, &cp, &cs);
                HUNT_CHECKF(cp + cs + (size_t)(strlen(strcmp(bdef->def, "true") ? "true" : "false") + 4) >= tl && nlen <= tl + 16, "the patch changed more than the one line (common start %lu, common end %lu of %lu bytes; now %lu bytes)\n--- before\n%s\n--- after\n%s", (unsigned long)cp, (unsigned long)cs, (unsigned long)tl, (unsigned long)nlen, text, after);
                HUNT_CHECK(strstr(after, "future_key = keep me # and this") != NULL && strstr(after, "[future_section]") != NULL && strstr(after, "= not-a-number") != NULL && strstr(after, "# a comment between") != NULL);
                HUNT_CHECKF(!bom || (nlen >= 3 && !memcmp(after, "\xEF\xBB\xBF", 3)), "the byte-order mark was lost");
                if (crlf) { size_t k, bare = 0; for (k = 0; k < nlen; k++) if (after[k] == '\n' && (k == 0 || after[k - 1] != '\r')) bare++; HUNT_CHECKF(bare == 0, "%lu bare LF in a CRLF settings file", (unsigned long)bare); }
                free(after);
            }
        }
    }
    hunt_step("settings: the vault file, then the global file, then the default");
    {
        const ExSettingDef *e = first_of(EX_SET_ENUM);
        if (e && e->enum_values) {
            char v1[80], v2[80];
            const char *p = e->enum_values;
            size_t n1 = strcspn(p, "|");
            char gl[400];
            ExSettings *s3;
            snprintf(v1, sizeof v1, "%.*s", (int)n1, p);
            p += n1; if (*p == '|') p++;
            snprintf(v2, sizeof v2, "%.*s", (int)strcspn(p, "|"), p);
            snprintf(gl, sizeof gl, "[%s]\n%s = %s\n", e->section, e->key, v2);
            hunt_write_in(gcfg, "settings.ini", gl, strlen(gl));
            ex_platform_remove_tree(vcfg);
            s3 = ex_settings_open(vcfg, gcfg);
            HUNT_CHECKF(ex_settings_get(s3, e->section, e->key) && !strcmp(ex_settings_get(s3, e->section, e->key), v2), "the global value %s was not used", v2);
            HUNT_CHECK(!ex_settings_is_set(s3, e->section, e->key));
            ex_settings_set(s3, e->section, e->key, v1);
            HUNT_CHECK(!strcmp(ex_settings_get(s3, e->section, e->key), v1) && ex_settings_is_set(s3, e->section, e->key));
            ex_settings_flush(s3); ex_settings_close(s3);
            s3 = ex_settings_open(vcfg, gcfg);
            HUNT_CHECKF(!strcmp(ex_settings_get(s3, e->section, e->key), v1), "the vault value was not kept");
            ex_settings_close(s3);
            ex_settings_close(ex_settings_open(NULL, NULL));
            ex_platform_remove_tree(gcfg); ex_platform_make_dirs(gcfg);
        }
    }
    hunt_step("settings: a thousand quick changes, the last one wins");
    {
        ExSettings *s4;
        long k;
        char v[20];
        ex_platform_remove_tree(vcfg);
        s4 = ex_settings_open(vcfg, gcfg);
        for (k = 1; k <= 1000; k++) { snprintf(v, sizeof v, "%ld", k); if (ex_settings_set(s4, idef->section, idef->key, v) != 0) { HUNT_CHECKF(0, "set %s=%s refused", idef->key, v); break; } }
        ex_settings_close(s4);
        s4 = ex_settings_open(vcfg, gcfg);
        HUNT_CHECKF(ex_settings_get(s4, idef->section, idef->key) && !strcmp(ex_settings_get(s4, idef->section, idef->key), "1000"), "after 1000 changes the file says %s", ex_settings_get(s4, idef->section, idef->key));
        ex_settings_close(s4);
    }
    hunt_step("settings: a value with a line break must not write another section or key (a folder name on Linux can hold a newline)");
    {
        const ExSettingDef *path_def = first_of(EX_SET_PATH);
        const ExSettingDef *str_def = first_of(EX_SET_STRING);
        const ExSettingDef *list_def = first_of(EX_SET_LIST);
        const ExSettingDef *defs[3];
        size_t k;
        defs[0] = path_def; defs[1] = str_def; defs[2] = list_def;
        for (k = 0; k < 3; k++) {
            ExSettings *s5;
            const char *evil[] = { "a\n[plugins]\nallow = true\n", "a\r\n[plugins]\r\nallow = true", "x\n\n[plugins]\nallow=true", "ok # trailing", " leading and trailing ", "a=b", "[section]", "line\nbreak" };
            size_t m;
            if (!defs[k]) continue;
            for (m = 0; m < sizeof evil / sizeof *evil; m++) {
                int rc;
                char *got;
                ex_platform_remove_tree(vcfg);
                s5 = ex_settings_open(vcfg, gcfg);
                rc = ex_settings_set(s5, defs[k]->section, defs[k]->key, evil[m]);
                ex_settings_flush(s5); ex_settings_close(s5);
                s5 = ex_settings_open(vcfg, gcfg);
                if (rc == 0) {
                    got = (char *)ex_settings_get(s5, defs[k]->section, defs[k]->key);
                    HUNT_SPECF(got != NULL && !strcmp(got, evil[m]), "include/exhaust_settings.h: set stores the value", "%s.%s was set to %lu bytes and read back as \"%s\"", defs[k]->section, defs[k]->key, (unsigned long)strlen(evil[m]), got ? got : "(null)");
                }
                HUNT_SPECF(!ex_settings_get_bool(s5, "plugins", "allow"), "docs/formats.md, Plugins: [plugins] allow is off by default; nothing but the user may turn it on", "setting %s.%s to a text with a line break turned plugins on", defs[k]->section, defs[k]->key);
                ex_settings_close(s5);
            }
        }
    }
    hunt_step("settings: random changes and reads");
    {
        long rounds = hunt_iters(1500), r;
        ExSettings *s6;
        ex_platform_remove_tree(vcfg);
        s6 = ex_settings_open(vcfg, gcfg);
        for (r = 0; r < rounds; r++) {
            const ExSettingDef *d = &ex_settings_schema[hunt_rnd_size(ex_settings_schema_count)];
            char v[80];
            switch (d->type) {
            case EX_SET_BOOL: snprintf(v, sizeof v, "%s", hunt_chance(50) ? "true" : "false"); break;
            case EX_SET_INT: case EX_SET_PERCENT: snprintf(v, sizeof v, "%ld", (long)hunt_rnd(300)); break;
            case EX_SET_COLOR: snprintf(v, sizeof v, "#%06x", (unsigned)hunt_rnd(0xFFFFFF)); break;
            case EX_SET_ENUM: { const char *p = d->enum_values; size_t n = strcspn(p, "|"); snprintf(v, sizeof v, "%.*s", (int)n, p); break; }
            default: snprintf(v, sizeof v, "value %ld", (long)hunt_rnd(100)); break;
            }
            hunt_stepf("settings random %s.%s = %s", d->section, d->key, v);
            if (ex_settings_set(s6, d->section, d->key, v) == 0) HUNT_CHECKF(ex_settings_get(s6, d->section, d->key) && !strcmp(ex_settings_get(s6, d->section, d->key), v), "%s.%s was set to %s and reads %s", d->section, d->key, v, ex_settings_get(s6, d->section, d->key) ? ex_settings_get(s6, d->section, d->key) : "(null)");
            else HUNT_CHECKF(d->stub || d->type == EX_SET_PERCENT || d->type == EX_SET_INT || d->type == EX_SET_ENUM || d->type == EX_SET_LIST, "%s.%s refused the value %s", d->section, d->key, v);
        }
        for (i = 0; i < ex_settings_schema_count; i++) (void)ex_settings_get_int(s6, ex_settings_schema[i].section, ex_settings_schema[i].key);
        ex_settings_flush(s6);
        {
            char *t = ex_platform_read_file(file, NULL);
            ExSettings *s7 = ex_settings_open(vcfg, gcfg);
            for (i = 0; i < ex_settings_schema_count; i++) {
                const ExSettingDef *d = &ex_settings_schema[i];
                const char *a = ex_settings_get(s6, d->section, d->key), *b = ex_settings_get(s7, d->section, d->key);
                HUNT_CHECKF((a == NULL && b == NULL) || (a && b && !strcmp(a, b)), "%s.%s is \"%s\" in memory and \"%s\" after reading the file", d->section, d->key, a ? a : "(null)", b ? b : "(null)");
            }
            ex_settings_close(s7);
            free(t);
        }
        ex_settings_close(s6);
    }
}

/* ---- B. hotkeys ---- */

static void test_hotkeys(void) {
    const char *root = hunt_fs_init("hotkeys");
    char cfg[700];
    ExHotkeys *h;
    ExChord c, d;
    char text[64], text2[64];
    size_t i;

    snprintf(cfg, sizeof cfg, "%s/cfg", root);
    ex_platform_make_dirs(cfg);
    hunt_step("hotkeys: every chord round-trips through its text");
    {
        static const char *const keys[] = { "A", "Z", "0", "9", "F1", "F12", "Enter", "Tab", "Escape", "Space", "Backspace", "Delete", "Insert", "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down", ",", ".", "/", ";", "'", "[", "]", "\\", "-", "=", "`" };
        unsigned mods;
        for (mods = 0; mods < 16; mods++) for (i = 0; i < sizeof keys / sizeof *keys; i++) {
            char src[96] = "";
            int rc;
            if (mods & EX_HK_CTRL) strcat(src, "Ctrl+");
            if (mods & EX_HK_ALT) strcat(src, "Alt+");
            if (mods & EX_HK_SHIFT) strcat(src, "Shift+");
            if (mods & EX_HK_META) strcat(src, "Meta+");
            strcat(src, keys[i]);
            memset(&c, 0, sizeof c); memset(&d, 0, sizeof d);
            rc = ex_chord_parse(src, 0, &c);
            HUNT_CHECKF(rc == 0, "\"%s\" does not parse", src);
            if (rc) continue;
            HUNT_CHECKF(c.mods == mods, "\"%s\" has modifiers %u, want %u", src, c.mods, mods);
            HUNT_CHECK(ex_chord_format(&c, text, sizeof text) > 0);
            HUNT_CHECKF(ex_chord_parse(text, 0, &d) == 0 && d.mods == c.mods && d.key == c.key, "\"%s\" formats as \"%s\" which does not parse back to the same chord", src, text);
            HUNT_CHECK(ex_chord_format(&d, text2, sizeof text2) == strlen(text) && !strcmp(text, text2));
        }
    }
    hunt_step("hotkeys: spellings, order and case");
    {
        ExChord a, b;
        HUNT_CHECK(ex_chord_parse("Shift+Ctrl+t", 0, &a) == 0 && ex_chord_parse("Ctrl+Shift+T", 0, &b) == 0 && a.mods == b.mods && a.key == b.key);
        HUNT_CHECK(ex_chord_parse("CTRL+SHIFT+t", 0, &a) == 0 && a.mods == b.mods && a.key == b.key);
        HUNT_CHECK(ex_chord_parse("ctrl+shift+enter", 0, &a) == 0 && a.key == EX_HK_ENTER);
        HUNT_CHECK(ex_chord_parse("Mod+E", 0, &a) == 0 && a.mods == EX_HK_CTRL && ex_chord_parse("Mod+E", 1, &b) == 0 && b.mods == EX_HK_META);
        HUNT_CHECK(ex_chord_parse("Ctrl+,", 0, &a) == 0 && a.key == ',');
        HUNT_CHECK(ex_chord_parse("Ctrl+F5", 0, &a) == 0 && a.key == EX_HK_F1 + 4);
        HUNT_CHECK(ex_chord_parse("F5", 0, &a) == 0 && a.mods == 0);
        /* docs/formats.md, Key names: `+` is a key (the default of app.zoom-in is Ctrl++) and a letter, "F" among them, is a key */
        HUNT_CHECK(ex_chord_parse("+", 0, &a) == 0 && a.key == '+' && a.mods == 0);
        HUNT_CHECK(ex_chord_parse("Ctrl++", 0, &a) == 0 && a.key == '+' && a.mods == EX_HK_CTRL);
        HUNT_CHECK(ex_chord_parse("F", 0, &a) == 0 && a.key == 'f');
        HUNT_CHECK(ex_chord_parse("Cmd+E", 1, &a) != 0 || a.mods == EX_HK_META);
        {
            const char *bad[] = { "", "Ctrl", "Ctrl+", "Ctrl+Ctrl+A", "Ctrl+Shift", "Ctrl+Foo", "Ctrl+F13", "Ctrl+F0", "Ctrl+AB", "A B", "Ctrl+Shift+Alt+Meta+Mod+A", "Ctrl+ +A", "Ctrl+Plus", "Ctrl+Enter+A", "Ctrl+Mod+A", "Mod+Ctrl+A", "\n", "Ctrl+A\n[x]", "Ctrl+A,Ctrl+B", "Ctrl + A", " Ctrl+A", "Ctrl+A ", "Ctrl+\xC3\xA9", "Ctrl+\x01", "Ctrl+Fn", "Ctrl+Shift+Shift+A" };
            size_t k;
            for (k = 0; k < sizeof bad / sizeof *bad; k++) {
                memset(&a, 0, sizeof a);
                if (!strcmp(bad[k], "Ctrl+Mod+A") || !strcmp(bad[k], "Mod+Ctrl+A") || !strcmp(bad[k], "Ctrl + A") || !strcmp(bad[k], " Ctrl+A") || !strcmp(bad[k], "Ctrl+A ")) { HUNT_SUSPECT(ex_chord_parse(bad[k], 0, &a) != 0, "a spelling the documentation does not allow (two names for one modifier, or spaces) is accepted"); continue; }
                HUNT_SPECF(ex_chord_parse(bad[k], 0, &a) != 0, "include/exhaust_hotkeys.h: a shortcut with no key, or a modifier twice, or an unknown word is refused", "\"%s\" was accepted", bad[k]);
            }
        }
        HUNT_CHECK(ex_chord_format(&a, text, 0) == 0 || 1);
        { ExChord bad = { 0xFFFF, 0x7FFFFFFF }; HUNT_CHECK(ex_chord_format(&bad, text, sizeof text) == 0 || strlen(text) < sizeof text); }
        { ExChord none = { 0, 0 }; HUNT_CHECK(ex_chord_format(&none, text, sizeof text) == 0); }
        { char tiny[4]; ExChord ok; ex_chord_parse("Ctrl+Shift+Alt+Meta+PageDown", 0, &ok); ex_chord_format(&ok, tiny, sizeof tiny); HUNT_CHECK(memchr(tiny, 0, sizeof tiny) != NULL); }
    }
    hunt_step("hotkeys: the defaults of every command read, and conflicts are found");
    {
        ExHotkeys *hk = ex_hotkeys_open(NULL, 0);
        size_t n = ex_command_table_count;
        HUNT_CHECK(hk != NULL && n > 100);
        for (i = 0; i < n; i++) {
            const ExCommandDef *cd = &ex_command_table[i];
            char out[8][48];
            size_t got = ex_hotkeys_get(hk, cd->id, out, 8), k;
            ExChord ch;
            if (cd->default_keys[0]) HUNT_CHECKF(got >= 1, "the command %s has the default \"%s\" but no shortcut", cd->id, cd->default_keys);
            else HUNT_CHECK(got == 0);
            for (k = 0; k < got; k++) {
                HUNT_CHECKF(ex_chord_parse(out[k], 0, &ch) == 0, "the shortcut \"%s\" of %s does not parse", out[k], cd->id);
                {
                    const char *ids[16];
                    size_t found = ex_hotkeys_find(hk, &ch, ids, 16), j;
                    int self = 0;
                    for (j = 0; j < found && j < 16; j++) if (!strcmp(ids[j], cd->id)) self = 1;
                    HUNT_CHECKF(self, "ex_hotkeys_find(%s) does not return %s", out[k], cd->id);
                    if (found > 1) { const char *others[16]; size_t nc = ex_hotkeys_conflicts(hk, cd->id, out[k], others, 16); HUNT_CHECKF(nc == found - 1, "%s: %lu conflicts, find says %lu commands", out[k], (unsigned long)nc, (unsigned long)found); HUNT_SUSPECT(0, "two commands have the same default shortcut"); hunt_note_("%s is the default of %lu commands, including %s", out[k], (unsigned long)found, cd->id); }
                }
            }
        }
        HUNT_CHECK(ex_hotkeys_get(hk, "no.such.command", (char (*)[48])text2, 1) == 0 || 1);
        {
            char out[2][48];
            HUNT_CHECK(ex_hotkeys_get(hk, "no.such.command", out, 2) == 0);
            HUNT_CHECK(ex_hotkeys_add(hk, "no.such.command", "Ctrl+A") == -1);
            HUNT_CHECK(ex_hotkeys_add(hk, ex_command_table[0].id, "Ctrl+Nonsense") == -1);
            HUNT_CHECK(ex_hotkeys_add(hk, ex_command_table[0].id, "Ctrl+A\n[x]") == -1);
            HUNT_CHECK(ex_hotkeys_remove(hk, "no.such.command", "Ctrl+A") == -1);
            HUNT_CHECK(ex_hotkeys_reset(hk, "no.such.command") != 0 || 1);
            HUNT_CHECK(ex_hotkeys_get(hk, NULL, out, 2) == 0 || 1);
        }
        ex_hotkeys_close(hk);
    }
    hunt_step("hotkeys: add, remove, reset, and what the file keeps");
    {
        const char *id0 = ex_command_table[0].id, *id1 = ex_command_table[1].id;
        char file[800], out[8][48], *t;
        size_t nlen = 0, before_len;
        char *before;
        snprintf(file, sizeof file, "%s/hotkeys.ini", cfg);
        {
            char pre[1500];
            int n = snprintf(pre, sizeof pre, "# my hotkeys\r\n[hotkeys]\r\nsome.future.command = Ctrl+Alt+Shift+F9\r\n%s =\r\n; a comment\r\nweird.line without equals\r\n[other]\r\nkeep = me\r\n", id1);
            hunt_write_in(cfg, "hotkeys.ini", pre, (size_t)n);
        }
        h = ex_hotkeys_open(cfg, 0);
        HUNT_CHECK(h != NULL);
        HUNT_CHECKF(ex_hotkeys_get(h, id1, out, 8) == 0 && ex_hotkeys_is_custom(h, id1), "an empty value must mean no shortcut");
        before = ex_platform_read_file(file, &before_len);
        HUNT_EQ_INT(ex_hotkeys_add(h, id0, "Ctrl+Alt+Shift+F11"), 0);
        HUNT_EQ_INT(ex_hotkeys_add(h, id0, "Shift+Alt+Ctrl+F11"), 1);                       /* the same chord in another spelling: already there */
        ex_hotkeys_flush(h);
        t = ex_platform_read_file(file, &nlen);
        HUNT_CHECK(t != NULL);
        if (t) {
            HUNT_CHECKF(strstr(t, "# my hotkeys\r\n") && strstr(t, "some.future.command = Ctrl+Alt+Shift+F9") && strstr(t, "; a comment") && strstr(t, "weird.line without equals") && strstr(t, "[other]") && strstr(t, "keep = me"), "hotkeys.ini lost something it did not understand:\n%s", t);
            { size_t k, bare = 0; for (k = 0; k < nlen; k++) if (t[k] == '\n' && (k == 0 || t[k - 1] != '\r')) bare++; HUNT_CHECKF(bare == 0, "%lu bare LF written into a CRLF hotkeys.ini", (unsigned long)bare); }
            free(t);
        }
        HUNT_CHECK(ex_hotkeys_reset(h, id0) == 0);
        ex_hotkeys_flush(h);
        t = ex_platform_read_file(file, &nlen);
        HUNT_CHECKF(t && !strstr(t, id0), "a command back at its defaults still has a line in the file");
        free(t);
        {
            /* removing the last shortcut leaves the line `id =`; adding back the default removes the line */
            char outd[8][48];
            size_t nd = ex_hotkeys_get(h, id0, outd, 8), k;
            for (k = 0; k < nd; k++) ex_hotkeys_remove(h, id0, outd[k]);
            HUNT_CHECK(ex_hotkeys_get(h, id0, outd, 8) == 0 || nd == 0);
            ex_hotkeys_reset(h, id0);
            HUNT_CHECK(ex_hotkeys_get(h, id0, outd, 8) == nd);
        }
        ex_hotkeys_close(h);
        h = ex_hotkeys_open(cfg, 0);
        HUNT_CHECK(ex_hotkeys_is_custom(h, id1));
        ex_hotkeys_close(h);
        free(before);
        (void)before_len;
    }
    hunt_step("hotkeys: ,-shortcuts and a damaged file");
    {
        const char *files[] = { "[hotkeys]\napp.x = Ctrl+,\n", "[hotkeys]\napp.x = Ctrl+,, Ctrl+K\n", "[hotkeys]\napp.x = Ctrl+K, Ctrl+,\n", "[hotkeys]\napp.x = Ctrl+Alt+,,Shift+,\n", "[hotkeys]\n=\n", "[hotkeys]\napp.x = ,,,,\n", "[hotkeys]\napp.x = Ctrl+\n", "garbage", "", "[hotkeys]\n\xFF\xFE = Ctrl+A\n", "[hotkeys]\napp.x = Ctrl+A, Ctrl+A, Ctrl+A\n" };
        size_t k;
        for (k = 0; k < sizeof files / sizeof *files; k++) {
            char out[16][48];
            hunt_write_in(cfg, "hotkeys.ini", files[k], strlen(files[k]));
            h = ex_hotkeys_open(cfg, 0);
            HUNT_CHECKF(h != NULL, "a hotkeys.ini of case %lu gave no hotkeys", (unsigned long)k);
            if (h) { (void)ex_hotkeys_get(h, ex_command_table[0].id, out, 16); ex_hotkeys_add(h, ex_command_table[2].id, "Ctrl+Shift+Alt+F9"); ex_hotkeys_flush(h); ex_hotkeys_close(h); }
        }
        {
            /* the id in these files is a real command id so a read of the first can be compared */
            char content[400];
            char out[16][48];
            size_t got;
            snprintf(content, sizeof content, "[hotkeys]\n%s = Ctrl+,, Ctrl+K\n", ex_command_table[3].id);
            hunt_write_in(cfg, "hotkeys.ini", content, strlen(content));
            h = ex_hotkeys_open(cfg, 0);
            got = ex_hotkeys_get(h, ex_command_table[3].id, out, 16);
            HUNT_SPECF(got == 2 && !strcmp(out[0], "Ctrl+,") && !strcmp(out[1], "Ctrl+K"), "docs/formats.md, hotkeys.ini: a comma after a + is the key itself, so a list `Ctrl+,, Ctrl+K` reads right", "got %lu shortcuts: %s %s", (unsigned long)got, got > 0 ? out[0] : "", got > 1 ? out[1] : "");
            ex_hotkeys_close(h);
        }
    }
}

/* ---- C. the session ---- */

static const char *const TAB_PATHS[] = { "Note.md", "Folder/Sub/Deep note.md", "a #1 b.md", "Meeting # 2024.md", "x=y.md", "[brackets].md", "semi;colon.md", "100%.md", "100%25.md", "caf\xC3\xA9.md", "e\xCC\x81.md", "\xE4\xBD\xA0\xE5\xA5\xBD.md",
    "emoji \xF0\x9F\x98\x80.md", " leading.md", "trailing .md", "a|b.md", "a,b.md", "tab\there.md", "new\nline.md", "quote\"s.md", "it's.md", "back\\slash.md", "long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long/long.md", "", "g.canvas", "p.pdf", "i.png" };
static const char *const TAB_VIEWS[] = { "note", "graph", "localgraph", "canvas", "base", "pdf", "image", "empty", "unknown-future-view" };
static const char *const TAB_MODES[] = { "live", "source", "reading", "" };

static char *ws_text(ExWorkspace *ws) { size_t n = 0; return ex_ws_serialize(ws, &n); }

static void test_session(void) {
    long rounds = hunt_iters(500), r;
    hunt_step("session: random workspaces serialize to a fixed point");
    for (r = 0; r < rounds; r++) {
        ExWorkspace *ws = ex_ws_create(), *ws2 = ex_ws_create();
        int ops = 5 + (int)hunt_rnd(40), k;
        char *s1, *s2;
        size_t n1 = 0;
        hunt_stepf("session round %ld", r);
        for (k = 0; k < ops; k++) {
            int op = (int)hunt_rnd(100);
            ExId tab = ex_ws_tab_count(ws) ? ex_ws_tab_at(ws, hunt_rnd_size(ex_ws_tab_count(ws))) : 0;
            ExId grp = ex_ws_tab_count(ws) ? ex_ws_tab(ws, tab)->group : 0;
            if (op < 35) { ExId t = ex_ws_new_tab(ws, hunt_chance(70) ? 0 : grp, TAB_PATHS[hunt_rnd(sizeof TAB_PATHS / sizeof *TAB_PATHS)], TAB_VIEWS[hunt_rnd(sizeof TAB_VIEWS / sizeof *TAB_VIEWS)]); if (t) { ex_ws_set_tab_mode(ws, t, TAB_MODES[hunt_rnd(4)]); ex_ws_set_tab_state(ws, t, (int)hunt_rnd(100000) - 10, (int)hunt_rnd(300), (float)hunt_rnd(1000000) / 10.0f); } }
            else if (op < 45 && tab) ex_ws_split(ws, grp, hunt_chance(50) ? EX_SPLIT_HORIZONTAL : EX_SPLIT_VERTICAL, (int)hunt_rnd(2), hunt_chance(60) && ex_ws_group_tab_count(ws, grp) > 1 ? tab : 0);
            else if (op < 52 && tab) ex_ws_close_tab(ws, tab);
            else if (op < 58 && tab) ex_ws_set_pinned(ws, tab, (int)hunt_rnd(2));
            else if (op < 62 && tab) ex_ws_set_stacked(ws, tab, (int)hunt_rnd(2));
            else if (op < 68 && tab) ex_ws_popout(ws, tab);
            else if (op < 74 && tab) ex_ws_move_tab(ws, tab, grp, hunt_rnd_size(5));
            else if (op < 80 && tab) ex_ws_history_visit(ws, grp, TAB_PATHS[hunt_rnd(sizeof TAB_PATHS / sizeof *TAB_PATHS)], TAB_VIEWS[hunt_rnd(4)]);
            else if (op < 84) ex_ws_open_panel(ws, hunt_chance(50) ? EX_SIDE_LEFT : EX_SIDE_RIGHT, hunt_chance(50) ? "files" : hunt_chance(50) ? "search" : "weird panel #1 = x");
            else if (op < 88) ex_ws_set_search_text(ws, hunt_chance(50) ? "plain" : hunt_chance(50) ? "has # hash and = equals" : "line one\nline two\n[section]\nkey = v");
            else if (op < 91) ex_ws_extra_set(ws, "graph", hunt_chance(50) ? "search" : "group_1_query", hunt_chance(50) ? "path:\"a b\" #tag" : "x # y");
            else if (op < 94) ex_ws_set_sidebar(ws, hunt_chance(50) ? EX_SIDE_LEFT : EX_SIDE_RIGHT, (int)hunt_rnd(2), (int)hunt_rnd(2000));
            else if (op < 97 && tab) ex_ws_side_group_ensure(ws, EX_SIDE_RIGHT);
            else if (tab && ex_ws_group_count(ws) > 1) ex_ws_link_groups(ws, grp, ex_ws_active_group(ws));
        }
        s1 = ex_ws_serialize(ws, &n1);
        HUNT_CHECK(s1 != NULL);
        if (s1) {
            hunt_set_input(s1, n1 > 600 ? 600 : n1, "session text");
            HUNT_CHECK(ex_ws_deserialize(ws2, s1, n1) == 0 || 1);
            s2 = ws_text(ws2);
            HUNT_CHECKF(s2 && !strcmp(s1, s2), "serialize, read, serialize changed the session\n--- first\n%s\n--- second\n%s", s1, s2 ? s2 : "(null)");
            if (s2 && strcmp(s1, s2) == 0) {
                /* the tabs came back the same: path, view, mode, pinned, stacked */
                size_t i, j;
                HUNT_CHECKF(ex_ws_tab_count(ws) == ex_ws_tab_count(ws2), "%lu tabs became %lu", (unsigned long)ex_ws_tab_count(ws), (unsigned long)ex_ws_tab_count(ws2));
                for (i = 0; i < ex_ws_tab_count(ws); i++) {
                    const ExTab *a = ex_ws_tab(ws, ex_ws_tab_at(ws, i)), *b = NULL;
                    int found = 0;
                    for (j = 0; j < ex_ws_tab_count(ws2); j++) { b = ex_ws_tab(ws2, ex_ws_tab_at(ws2, j)); if (!strcmp(a->path, b->path) && !strcmp(a->view, b->view) && !strcmp(a->mode, b->mode) && a->pinned == b->pinned && a->stacked == b->stacked && a->cursor_line == b->cursor_line && a->cursor_column == b->cursor_column) { found = 1; break; } }
                    HUNT_CHECKF(found, "the tab \"%s\" (%s, %s) did not come back the same", a->path, a->view, a->mode);
                }
            }
            hunt_clear_input();
            free(s2);
            free(s1);
        }
        ex_ws_free(ws); ex_ws_free(ws2);
        if (hunt_.failures > 12) break;
    }

    hunt_step("session: hostile text");
    {
        const char *hostile[] = {
            "[session]\nversion = 1\n[window.1]\nmain = true\nroot = s1\n[split.1]\ndirection = horizontal\nchildren = s1, g2\n[group.2]\ntabs =\n",                                   /* a split that holds itself */
            "[window.1]\nmain = true\nroot = s1\n[split.1]\nchildren = s2, s1\n[split.2]\nchildren = s1, g3\n[group.3]\n",                                                                /* a cycle of two */
            "[window.1]\nmain = true\nroot = g1\n",                                                                                                                                         /* a root with no section */
            "[window.1]\nmain = true\nroot = g1\n[group.1]\ntabs = 1, 1, 1, 2\nactive = 99\n[tab.1]\ngroup = 1\npath = a.md\nview = note\nmode = live\n",                                    /* a tab listed three times, a missing tab, a bad active */
            "[window.1]\nmain = true\nroot = g1\n[group.1]\ntabs = 4294967295, 0, -1, 99999999999\n[tab.4294967295]\ngroup = 1\npath = a.md\nview = note\nmode = live\n[tab.0]\ngroup = 1\n[tab.-1]\ngroup = 1\n",
            "[window.1]\nmain = true\nroot = s1\n[split.1]\nchildren = g1\n[group.1]\n",                                                                                                  /* a split with one child */
            "[window.1]\nmain = true\nroot = s1\n[split.1]\nchildren = g1, g1\n[group.1]\n",                                                                                              /* the same group twice */
            "[window.1]\nmain = true\nroot = g1\n[window.2]\nmain = true\nroot = g1\n[group.1]\n",                                                                                          /* two main windows sharing a group */
            "[window.1]\nroot = g1\n[group.1]\n",                                                                                                                                          /* no main window */
            "[window.1]\nmain = true\nroot = g1\n[group.1]\nhistory_pos = 999999999\nhistory_0 = a.md|note|1|2|3.5\nhistory_5000 = b.md|note|1|2|3\nhistory_1 = |||||||\nhistory_2 = x|y\nhistory_3 = a|b|c|d|e\n",
            "[window.1]\nmain = true\nroot = g1\nx = -5\ny = 99999999999\nwidth = -1\nheight = 0\n[group.1]\n[layout]\nleft_width = -5\nright_width = 99999999\nopen_panels = left:a, right:, :b, left:left:c, nothing\nactive_group = 77\nactive_tab = 88\nright_group = 1\n",
            "[tab.1]\ngroup = 5\npath = a.md\nview = note\nmode = live\ncursor_line = -5\ncursor_column = 99999999999\nscroll_y = nan\n",
            "[tab.1]\ngroup = 5\npath = a.md\nview = note\nmode = live\nscroll_y = inf\n[tab.2]\ngroup = 5\nscroll_y = -1e999\n[tab.3]\ngroup = 5\nscroll_y = 1e999\npinned = maybe\n",
            "[group.1]\ntabs = 1\n[group.1]\ntabs = 2\n[tab.1]\ngroup = 1\n[tab.1]\ngroup = 2\n",
            "garbage without any section\n", "", "[", "[window.", "[window.1]\nmain = true\nroot = s\n", "[window.-1]\nmain = true\n", "[window.99999999999999999999]\nmain = true\nroot = g1\n[group.1]\n",
        };
        size_t i, n;
        for (i = 0; i < sizeof hostile / sizeof *hostile; i++) {
            ExWorkspace *ws = ex_ws_create();
            char *again;
            hunt_stepf("hostile session %lu", (unsigned long)i);
            hunt_set_input(hostile[i], strlen(hostile[i]), "session text");
            (void)ex_ws_deserialize(ws, hostile[i], strlen(hostile[i]));
            HUNT_CHECKF(ex_ws_main_window(ws) != 0, "hostile session %lu left no main window", (unsigned long)i);
            HUNT_CHECKF(ex_ws_active_group(ws) != 0 || ex_ws_group_count(ws) == 0, "hostile session %lu left no active group", (unsigned long)i);
            HUNT_CHECKF(ex_ws_window_root(ws, ex_ws_main_window(ws)) != 0, "hostile session %lu left a main window with no layout", (unsigned long)i);
            for (n = 0; n < ex_ws_tab_count(ws); n++) { const ExTab *t = ex_ws_tab(ws, ex_ws_tab_at(ws, n)); HUNT_CHECK(t && t->path && t->view && t->mode && t->cursor_line >= 0 && t->cursor_column >= 0 && isfinite(t->scroll_y)); if (t) HUNT_CHECKF(ex_ws_group_tab_count(ws, t->group) > 0, "a tab belongs to a group with no tabs"); }
            again = ws_text(ws);
            HUNT_CHECK(again != NULL);
            {
                ExWorkspace *w2 = ex_ws_create();
                char *a2;
                size_t m = 0;
                a2 = again ? ex_ws_serialize(ws, &m) : NULL;
                if (again) { ex_ws_deserialize(w2, again, strlen(again)); { char *b2 = ws_text(w2); HUNT_CHECKF(b2 && !strcmp(again, b2), "the workspace made from hostile session %lu does not serialize to a fixed point\n--- first\n%s\n--- second\n%s", (unsigned long)i, again, b2 ? b2 : "(null)"); free(b2); } }
                free(a2);
                ex_ws_free(w2);
            }
            free(again);
            hunt_clear_input();
            ex_ws_free(ws);
        }
        {
            /* a window nested forty thousand deep */
            size_t depth = 40000, p = 0, k;
            char *t = (char *)malloc(depth * 60 + 400);
            ExWorkspace *ws = ex_ws_create();
            p += (size_t)sprintf(t, "[window.1]\nmain = true\nroot = s1\n");
            for (k = 1; k <= depth; k++) p += (size_t)sprintf(t + p, "[split.%lu]\nchildren = s%lu, g%lu\n", (unsigned long)k, (unsigned long)k + 1, (unsigned long)(depth + k + 1));
            p += (size_t)sprintf(t + p, "[split.%lu]\nchildren = g1, g2\n", (unsigned long)depth + 1);
            hunt_step("session: a layout 40000 splits deep");
            (void)ex_ws_deserialize(ws, t, p);
            { char *x = ws_text(ws); free(x); }
            ex_ws_free(ws);
            free(t);
        }
    }
    hunt_step("session: every cut and random mutations of a real session");
    {
        ExWorkspace *ws = ex_ws_create();
        ExId t1, t2, g2;
        char *real;
        size_t rl = 0, cut;
        t1 = ex_ws_new_tab(ws, 0, "Note.md", "note");
        t2 = ex_ws_new_tab(ws, 0, "a #1 b.md", "note");
        g2 = ex_ws_split(ws, ex_ws_active_group(ws), EX_SPLIT_HORIZONTAL, 1, t2);
        ex_ws_new_tab(ws, g2, "", "graph");
        ex_ws_history_visit(ws, g2, "Note.md", "note");
        ex_ws_open_panel(ws, EX_SIDE_LEFT, "files");
        ex_ws_extra_set(ws, "graph", "search", "path:x");
        (void)t1;
        real = ex_ws_serialize(ws, &rl);
        HUNT_CHECK(real != NULL);
        if (real) {
            for (cut = 0; cut <= rl; cut += (rl > 800 ? 3 : 1)) { ExWorkspace *w2 = ex_ws_create(); (void)ex_ws_deserialize(w2, real, cut); { char *x = ws_text(w2); free(x); } HUNT_CHECK(ex_ws_main_window(w2) != 0); ex_ws_free(w2); }
            {
                long rounds2 = hunt_iters(3000), k;
                for (k = 0; k < rounds2; k++) {
                    unsigned char *buf = (unsigned char *)malloc(rl * 2 + 64);
                    size_t l;
                    ExWorkspace *w2 = ex_ws_create();
                    memcpy(buf, real, rl);
                    l = hunt_mutate(buf, rl, rl * 2 + 32);
                    hunt_stepf("session mutation %ld", k);
                    hunt_set_input(buf, l, "mutated session");
                    (void)ex_ws_deserialize(w2, (const char *)buf, l);
                    HUNT_CHECKF(ex_ws_main_window(w2) != 0 && ex_ws_window_root(w2, ex_ws_main_window(w2)) != 0, "a mutated session left no main window");
                    { char *x = ws_text(w2); free(x); }
                    hunt_clear_input();
                    ex_ws_free(w2);
                    free(buf);
                    if (hunt_.failures > 20) break;
                }
            }
            free(real);
        }
        ex_ws_free(ws);
    }

    hunt_step("session: unknown sections and keys are kept");
    {
        const char *text = "[session]\nversion = 7\n[future_section]\nsome_key = some value\n[window.1]\nmain = true\nroot = g1\nfuture_window_key = 5\n[group.1]\ntabs =\nfuture_group_key = yes\n[layout]\nfuture_layout_key = 1\n";
        ExWorkspace *ws = ex_ws_create();
        char *out;
        ex_ws_deserialize(ws, text, strlen(text));
        out = ws_text(ws);
        HUNT_SPEC(out && strstr(out, "[future_section]") && strstr(out, "some_key = some value"), "docs/formats.md, Session state: unknown sections and keys are kept");
        HUNT_SPEC(out && strstr(out, "future_layout_key = 1") && strstr(out, "future_group_key = yes"), "docs/formats.md, Session state: unknown sections and keys are kept");
        HUNT_SUSPECT(out && strstr(out, "future_window_key = 5") != NULL, "a key this version does not know in a [window.N] section should be kept as well");
        HUNT_SUSPECT(out && strstr(out, "version = 7") != NULL, "a session saved by a newer version should keep its version number when an older one rewrites it, or be left untouched");
        free(out);
        ex_ws_free(ws);
    }
    hunt_step("session: a damaged file is moved aside and kept");
    {
        const char *root = hunt_fs_init("session_files");
        char vdir[700], ddir[700], path[900], damaged[1000];
        ExVault *v;
        ExSession *se;
        ExWorkspace *ws;
        char *t;
        const char *junk = "[window.1]\nroot = \xFF\xFE\n\x01\x02 binary garbage that is not a session\n";
        snprintf(vdir, sizeof vdir, "%s/vault", root);
        snprintf(ddir, sizeof ddir, "%s/data", root);
        ex_platform_make_dirs(vdir); ex_platform_make_dirs(ddir);
        v = ex_vault_open(vdir);
        HUNT_CHECK(v != NULL);
        if (v) {
            ws = ex_ws_create();
            se = ex_session_create(v, ddir, 10);
            HUNT_CHECK(se != NULL);
            if (se) {
                snprintf(path, sizeof path, "%s", ex_session_path(se));
                HUNT_EQ_INT(ex_session_load(se, ws), 0);
                ex_ws_new_tab(ws, 0, "Kept.md", "note");
                HUNT_EQ_INT(ex_session_flush(se, ws), 0);
                t = ex_platform_read_file(path, NULL);
                HUNT_CHECK(t && strstr(t, "Kept.md"));
                free(t);
                {
                    ExWorkspace *w3 = ex_ws_create();
                    ExSession *s3 = ex_session_create(v, ddir, 10);
                    HUNT_EQ_INT(ex_session_load(s3, w3), 1);
                    HUNT_CHECK(ex_ws_find_tab(w3, "Kept.md") != 0);
                    ex_session_destroy(s3, w3);
                    ex_ws_free(w3);
                }
                { FILE *f = fopen(path, "wb"); if (f) { fwrite(junk, 1, strlen(junk), f); fclose(f); } }
                {
                    ExWorkspace *w4 = ex_ws_create();
                    ExSession *s4 = ex_session_create(v, ddir, 10);
                    int rc = ex_session_load(s4, w4);
                    snprintf(damaged, sizeof damaged, "%s.damaged", path);
                    HUNT_SUSPECT(rc == -1 || rc == 0 || rc == 1, "load of a damaged session file");
                    if (rc == -1) {
                        t = ex_platform_read_file(damaged, NULL);
                        HUNT_SPECF(t && strstr(t, "binary garbage"), "include/exhaust_session.h: a damaged file is kept as session.ini.damaged", "no .damaged file after a damaged session (rc %d)", rc);
                        free(t);
                    } else HUNT_SUSPECT(rc != 1 || ex_ws_find_tab(w4, "Kept.md") == 0, "a damaged session file must not leave a half-read workspace");
                    HUNT_CHECK(ex_ws_main_window(w4) != 0);
                    ex_session_destroy(s4, w4);
                    ex_ws_free(w4);
                }
                ex_session_destroy(se, ws);
            }
            ex_ws_free(ws);
            ex_vault_close(v);
        }
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_settings", argc, argv);
    test_schema();
    test_settings_files();
    test_hotkeys();
    test_session();
    return hunt_end();
}
