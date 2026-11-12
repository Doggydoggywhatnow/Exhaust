/* settings: loading, layers (vault file over global file over the default), validation, write-through,
 * and above all that nothing the user wrote is lost: comments, order, unknown keys and sections,
 * line endings and the byte-order mark survive every change. Also checks the generated table
 * against the document it comes from. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "exhaust_vault.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define DIR "settings_test"

static char *patch(const char *text, const char *sec, const char *key, const char *val) {
    return ex_ini_patch(text, strlen(text), sec, key, val, NULL);
}
static void expect(const char *got, const char *want, int line) {
    if (!got || strcmp(got, want) != 0) { fprintf(stderr, "line %d:\n--- got ---\n%s\n--- want ---\n%s\n", line, got ? got : "(null)", want); failures++; }
}
#define EXPECT(got, want) do { char *g_ = (got); expect(g_, want, __LINE__); free(g_); } while (0)

static void test_patch(void) {
    /* an existing key keeps its line and its trailing comment; everything else is untouched */
    EXPECT(patch("# my settings\n[editor]\nvim_mode = false  # try it\nunknown = 1\n\n[files]\ntrash_mode = system\n", "editor", "vim_mode", "true"),
           "# my settings\n[editor]\nvim_mode = true  # try it\nunknown = 1\n\n[files]\ntrash_mode = system\n");
    /* a new key goes at the end of its section, before the blank line */
    EXPECT(patch("[editor]\nvim_mode = false\n\n[files]\na = b\n", "editor", "line_numbers", "true"),
           "[editor]\nvim_mode = false\nline_numbers = true\n\n[files]\na = b\n");
    /* a new section goes at the end */
    EXPECT(patch("[editor]\nvim_mode = false\n", "files", "trash_mode", "permanent"), "[editor]\nvim_mode = false\n\n[files]\ntrash_mode = permanent\n");
    /* an empty file */
    EXPECT(patch("", "general", "language", "de"), "[general]\nlanguage = de\n");
    /* no line ending at the end of the file */
    EXPECT(patch("[editor]\nvim_mode = false", "editor", "line_numbers", "true"), "[editor]\nvim_mode = false\nline_numbers = true\n");
    EXPECT(patch("[editor]\nvim_mode = false", "editor", "vim_mode", "true"), "[editor]\nvim_mode = true");
    /* CRLF files stay CRLF */
    EXPECT(patch("[editor]\r\nvim_mode = false\r\n", "editor", "line_numbers", "true"), "[editor]\r\nvim_mode = false\r\nline_numbers = true\r\n");
    EXPECT(patch("[editor]\r\nvim_mode = false\r\n", "editor", "vim_mode", "true"), "[editor]\r\nvim_mode = true\r\n");
    /* a byte-order mark stays */
    EXPECT(patch("\xEF\xBB\xBF[editor]\nvim_mode = false\n", "editor", "vim_mode", "true"), "\xEF\xBB\xBF[editor]\nvim_mode = true\n");
    /* the key name is matched exactly and only inside its section */
    EXPECT(patch("[a]\nvim_mode = 1\n[editor]\nvim_mode_x = 2\n", "editor", "vim_mode", "true"), "[a]\nvim_mode = 1\n[editor]\nvim_mode_x = 2\nvim_mode = true\n");
    /* a duplicated key: the last line is the one that counts when reading, so it is the one changed */
    EXPECT(patch("[editor]\nvim_mode = false\nvim_mode = false\n", "editor", "vim_mode", "true"), "[editor]\nvim_mode = false\nvim_mode = true\n");
    /* spaces around the key and no spaces around the equals sign */
    EXPECT(patch("[editor]\n  vim_mode=false\n", "editor", "vim_mode", "true"), "[editor]\n  vim_mode = true\n");
    /* commented-out key is not the key */
    EXPECT(patch("[editor]\n# vim_mode = false\n", "editor", "vim_mode", "true"), "[editor]\n# vim_mode = false\nvim_mode = true\n");
    /* a section that appears twice: the new key goes in the last block */
    EXPECT(patch("[files]\na = 1\n[editor]\nx = 1\n[files]\nb = 2\n", "files", "c", "3"), "[files]\na = 1\n[editor]\nx = 1\n[files]\nb = 2\nc = 3\n");
    /* the result reads back */
    {
        char *t = patch("[editor]\nvim_mode = false\n", "editor", "indent_width", "2");
        ExIni *ini = ex_ini_parse(t, strlen(t));
        CHECK(ex_ini_get_int(ini, "editor", "indent_width", 0) == 2 && !ex_ini_get_bool(ini, "editor", "vim_mode", 1));
        ex_ini_free(ini);
        free(t);
    }
}

static void write_text(const char *path, const char *text) { CHECK(ex_platform_write_file_atomic(path, text, strlen(text)) == 0); }
static int exists(const char *path) { ExPathInfo i; return ex_platform_path_info(path, &i) == 0; }

static void test_values(void) {
    ExSettings *s;
    ex_platform_remove_tree(DIR);
    ex_platform_make_dirs(DIR "/global");
    ex_platform_make_dirs(DIR "/vault");                 /* the vault folder exists, its .exhaust folder does not */

    /* defaults, and reading never writes or creates anything */
    s = ex_settings_open(DIR "/vault/.exhaust", DIR "/global");
    CHECK(s != NULL);
    CHECK(strcmp(ex_settings_get(s, "files", "trash_mode"), "system") == 0);
    CHECK(ex_settings_get_bool(s, "editor", "spellcheck") == 1 && ex_settings_get_bool(s, "editor", "vim_mode") == 0);
    CHECK(ex_settings_get_int(s, "appearance", "font_size") == 16 && ex_settings_get_int(s, "files", "autosave_idle_ms") == 1000);
    CHECK(strcmp(ex_settings_get(s, "appearance", "font_mono"), "Atkinson Hyperlegible Mono") == 0);
    CHECK(ex_settings_get(s, "nothing", "here") == NULL);
    CHECK(!exists(DIR "/vault/.exhaust"));
    ex_settings_close(s);
    CHECK(!exists(DIR "/vault/.exhaust"));

    /* layers: vault over global over default; a bad value is ignored and stays in the file */
    write_text(DIR "/global/settings.ini", "[appearance]\nfont_size = 18\ncolor_scheme = dark\n[editor]\nindent_width = 2\n");
    ex_platform_make_dirs(DIR "/vault/.exhaust");
    write_text(DIR "/vault/.exhaust/settings.ini", "# mine\n[appearance]\nfont_size = 20\ncolor_scheme = sepia\n[future]\nthing = 1\n");
    s = ex_settings_open(DIR "/vault/.exhaust", DIR "/global");
    CHECK(ex_settings_get_int(s, "appearance", "font_size") == 20);                      /* vault wins */
    CHECK(strcmp(ex_settings_get(s, "appearance", "color_scheme"), "dark") == 0);         /* "sepia" is not a value: the global file's applies */
    CHECK(ex_settings_get_int(s, "editor", "indent_width") == 2);                         /* only in the global file */
    CHECK(ex_settings_get_int(s, "editor", "indent_width") == 2 && !ex_settings_is_set(s, "editor", "indent_width"));
    CHECK(ex_settings_is_set(s, "appearance", "font_size"));
    CHECK(strcmp(ex_settings_get(s, "future", "thing"), "1") == 0);                      /* unknown keys can be read */

    /* validation */
    CHECK(ex_settings_set(s, "editor", "vim_mode", "maybe") == -1);
    CHECK(ex_settings_set(s, "editor", "indent_width", "four") == -1);
    CHECK(ex_settings_set(s, "appearance", "color_scheme", "sepia") == -1);
    CHECK(ex_settings_set(s, "appearance", "accent_color", "red") == -1 && ex_settings_set(s, "appearance", "accent_color", "#aabbcc") == 0);
    CHECK(ex_settings_set(s, "appearance", "accent_color", "#aabbccdd") == 0 && ex_settings_set(s, "appearance", "accent_color", "") == 0);
    CHECK(ex_settings_set(s, "nothing", "here", "1") == -1);                              /* only known keys are created */
    CHECK(ex_settings_set(s, "features", "sync", "true") == -1);                          /* a stub cannot be turned on */
    CHECK(ex_settings_set(s, "features", "sync", "false") == 0);
    CHECK(ex_settings_set(s, "editor", "vim_mode", "yes") == 0 && strcmp(ex_settings_get(s, "editor", "vim_mode"), "true") == 0);   /* canonical spelling */
    CHECK(ex_settings_set_int(s, "appearance", "font_size", 22) == 0 && ex_settings_get_int(s, "appearance", "font_size") == 22);
    ex_settings_flush(s);
    {
        char *t = ex_platform_read_file(DIR "/vault/.exhaust/settings.ini", NULL);
        CHECK(t && strncmp(t, "# mine\n[appearance]\nfont_size = 22\ncolor_scheme = sepia\n", 52) == 0);   /* the bad value was not touched */
        CHECK(t && strstr(t, "[future]\nthing = 1\n") != NULL);
        CHECK(t && strstr(t, "vim_mode = true") != NULL);
        free(t);
    }
    ex_settings_close(s);

    /* no vault: changes go to the global file */
    s = ex_settings_open(NULL, DIR "/global");
    CHECK(ex_settings_set_bool(s, "editor", "line_numbers", 1) == 0 && ex_settings_get_bool(s, "editor", "line_numbers") == 1);
    ex_settings_close(s);
    {
        char *t = ex_platform_read_file(DIR "/global/settings.ini", NULL);
        CHECK(t && strstr(t, "line_numbers = true") && strstr(t, "font_size = 18"));
        free(t);
    }

    /* the first change creates .exhaust */
    ex_platform_remove_tree(DIR "/vault2");
    ex_platform_make_dirs(DIR "/vault2");
    s = ex_settings_open(DIR "/vault2/.exhaust", DIR "/global");
    CHECK(!exists(DIR "/vault2/.exhaust"));
    CHECK(ex_settings_set(s, "files", "trash_mode", "permanent") == 0);
    ex_settings_close(s);                                    /* waits for the write */
    CHECK(exists(DIR "/vault2/.exhaust/settings.ini"));
    s = ex_settings_open(DIR "/vault2/.exhaust", DIR "/global");
    CHECK(strcmp(ex_settings_get(s, "files", "trash_mode"), "permanent") == 0);
    ex_settings_close(s);
}

static void test_schema(void) {
    size_t i, features = 0;
    char *doc = ex_platform_read_file(DOC_PATH, NULL);
    CHECK(doc != NULL);
    CHECK(ex_settings_schema_count > 100);
    for (i = 0; i < ex_settings_schema_count; i++) {
        const ExSettingDef *d = &ex_settings_schema[i];
        char needle[200];
        /* every key is in the document, and its default is a valid value for its type */
        snprintf(needle, sizeof needle, "| `%s` |", d->key);
        CHECK(doc && strstr(doc, needle) != NULL);
        CHECK(ex_settings_valid(d, d->def) || ((d->type == EX_SET_INT || d->type == EX_SET_PERCENT) && !d->def[0]));
        CHECK(ex_settings_def(d->section, d->key) == d);
        if (strcmp(d->section, "features") == 0) features++;
        if (d->type == EX_SET_ENUM) { char e[256]; snprintf(e, sizeof e, "|%s|", d->enum_values); { char dv[128]; snprintf(dv, sizeof dv, "|%s|", d->def); CHECK(strstr(e, dv) != NULL); } }
    }
    CHECK(features == 28);                       /* the core plugin switches of the document */
    CHECK(ex_settings_def("features", "sync") && ex_settings_def("features", "sync")->stub);
    CHECK(ex_settings_def("features", "publish") && ex_settings_def("features", "publish")->stub);
    CHECK(ex_settings_def("features", "graph") && !ex_settings_def("features", "graph")->stub);
    free(doc);
}

static void test_apply(void) {
    ExSettings *s;
    ExVault *v;
    ExVaultOptions o;
    ex_platform_remove_tree(DIR "/applyvault");
    ex_platform_make_dirs(DIR "/applyvault/sub");
    write_text(DIR "/applyvault/a.md", "a");
    write_text(DIR "/applyvault/sub/b.md", "b");
    write_text(DIR "/applyvault/c.png", "x");
    write_text(DIR "/applyvault/weird.xyz", "x");
    s = ex_settings_open(DIR "/applyvault/.exhaust", NULL);
    v = ex_vault_open(DIR "/applyvault");
    ex_settings_apply_vault(s, v);                              /* defaults */
    ex_vault_get_options(v, &o);
    CHECK(!o.show_all_files && o.trash_mode == EX_TRASH_SYSTEM && o.sort_by == EX_SORT_NAME && !o.sort_descending && o.nexcluded == 0);
    CHECK(ex_vault_find(v, "weird.xyz") == NULL && ex_vault_find(v, "a.md") != NULL);
    ex_settings_set(s, "files", "show_all_file_types", "true");
    ex_settings_set(s, "files", "trash_mode", "vault_trash");
    ex_settings_set(s, "files", "sort_by", "modified");
    ex_settings_set(s, "files", "sort_descending", "true");
    ex_settings_set(s, "files", "excluded", "sub/, *.png ,, drafts");
    ex_settings_apply_vault(s, v);
    ex_vault_get_options(v, &o);
    CHECK(o.show_all_files && o.trash_mode == EX_TRASH_VAULT && o.sort_by == EX_SORT_MODIFIED && o.sort_descending && o.nexcluded == 3);
    CHECK(ex_vault_find(v, "weird.xyz") != NULL);
    CHECK(strcmp(o.excluded[0], "sub/") == 0 && strcmp(o.excluded[1], "*.png") == 0 && strcmp(o.excluded[2], "drafts") == 0);
    ex_vault_close(v);
    ex_settings_close(s);
    {   /* opening the vault and applying settings wrote nothing into it except settings.ini */
        char *a = ex_platform_read_file(DIR "/applyvault/a.md", NULL);
        CHECK(a && strcmp(a, "a") == 0);
        free(a);
    }
}

int main(void) {
    test_patch();
    test_apply();
    test_values();
    test_schema();
    ex_platform_remove_tree(DIR);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("settings: ok");
    return 0;
}
