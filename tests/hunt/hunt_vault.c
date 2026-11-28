/* hunt_vault: names, excluded-file patterns, the vault's file operations, the registry of vaults and the file recovery snapshots.
 *
 * This is the code that "creates, renames, moves or deletes the user's files" (AGENTS.md hard rule 1), keeps the list of vaults, and keeps the history of
 * every note. Its promises are: a bad name or path is refused and nothing changes; nothing is ever replaced; the vault folder itself is never removed;
 * a snapshot is never lost except by pruning or Clear history. This test
 *   A. checks ex_name_valid against every rule of its header and some Windows traps, file kinds by extension, and excluded patterns against a dynamic
 *      programming matcher (plus the exponential-time pattern `*a*a*a*a*b`);
 *   B. runs the vault actions with hostile names and paths (`..`, absolute paths, the empty path, a symbolic link out of the vault): nothing outside the
 *      vault may change, nothing may be replaced, and the vault root must survive a delete of "";
 *   C. round-trips the vault registry through its file with names that mean something to an INI file (` #`, `=`, line breaks, brackets);
 *   D. checks the snapshot store: file names that tell notes apart, same-second replacement, order, pruning by age (conflict copies stay), clearing,
 *      reading by names that try to leave the folder, and local time stamps across a daylight saving change (two hours with one stamp lose a snapshot).
 * Link: exhaust_vault exhaust_notes exhaust_platform. */
#include "hunt.h"
#include "hunt_fs.h"

#include <ctype.h>
#include <time.h>

#include "exhaust_history.h"
#include "exhaust_vault.h"

/* ---- A. names, kinds, patterns ---- */

static void test_names(void) {
    struct { const char *name; int ok; int suspect; } n[] = {
        { "Note", 1, 0 }, { "Note.md", 1, 0 }, { "a b c", 1, 0 }, { "caf\xC3\xA9", 1, 0 }, { "\xE4\xBD\xA0\xE5\xA5\xBD.md", 1, 0 }, { "a-b_c.d.e", 1, 0 }, { "a%b", 1, 0 }, { "a%", 1, 0 }, { "%a", 1, 0 }, { "a(b)", 1, 0 }, { "a{b}", 1, 0 }, { "a,b;c", 1, 0 },
        { "a&b", 1, 0 }, { "a'b", 1, 0 }, { "a@b", 1, 0 }, { "a!b", 1, 0 }, { "a$b", 1, 0 }, { "a+b", 1, 0 }, { "a=b", 1, 0 }, { "a~b", 1, 0 }, { "a`b", 1, 0 }, { ".hidden", 1, 0 }, { "a.b.", 0, 0 }, { "...", 0, 0 },
        { "", 0, 0 }, { ".", 0, 0 }, { "..", 0, 0 }, { "a/b", 0, 0 }, { "a\\b", 0, 0 }, { "a#b", 0, 0 }, { "a^b", 0, 0 }, { "a[b", 0, 0 }, { "a]b", 0, 0 }, { "a|b", 0, 0 }, { "a:b", 0, 0 }, { "a%%b", 0, 0 }, { "%%", 0, 0 }, { "a%%%b", 0, 0 },
        { "a\tb", 0, 0 }, { "a\nb", 0, 0 }, { "a\rb", 0, 0 }, { "a\x01" "b", 0, 0 }, { "a\x7f" "b", 0, 0 }, { "a ", 0, 0 }, { "a.md ", 0, 0 }, { "a. ", 0, 0 }, { " a", 0, 1 },
        { "CON", 0, 0 }, { "con", 0, 0 }, { "Con.md", 0, 0 }, { "PRN", 0, 0 }, { "AUX.txt", 0, 0 }, { "NUL", 0, 0 }, { "nul.md", 0, 0 }, { "COM1", 0, 0 }, { "com9.md", 0, 0 }, { "LPT1", 0, 0 }, { "lpt5.txt", 0, 0 }, { "COM0", 1, 1 }, { "COM10", 1, 0 },
        { "CONSOLE", 1, 0 }, { "CON1", 1, 0 }, { "xCON", 1, 0 }, { "COM", 1, 0 }, { "LPT", 1, 0 }, { "COM1x", 1, 0 }, { "CON.", 0, 0 }, { "NUL.tar.gz", 0, 0 }, { "a*b", 0, 1 }, { "a?b", 0, 1 }, { "a<b", 0, 1 }, { "a>b", 0, 1 }, { "a\"b", 0, 1 },
        { "\xFF\xFE", 0, 1 }, { "a\xC3", 0, 1 }, { "e\xCC\x81", 1, 0 }, { "\xE2\x80\xAE" "txt.exe", 0, 1 }, { "a\xE2\x80\x8B" "b", 1, 1 }, { "\xEF\xBB\xBF" "a", 1, 1 }, { "a\xC2\xA0", 1, 1 },
    };
    size_t i;
    char *big;
    hunt_step("names: the rules");
    for (i = 0; i < sizeof n / sizeof *n; i++) {
        int got = ex_name_valid(n[i].name) != 0;
        if (n[i].suspect) { HUNT_SUSPECT(got == n[i].ok, "a name the documentation does not cover: read by hand (Windows, macOS and the sync tools disagree about these)"); if (got != n[i].ok) hunt_note_("ex_name_valid(\"%s\") = %d", n[i].name, got); }
        else HUNT_SPECF(got == n[i].ok, "include/exhaust_vault.h, ex_name_valid", "ex_name_valid(\"%s\") = %d, want %d", n[i].name, got, n[i].ok);
    }
    hunt_step("names: length is in bytes");
    big = (char *)malloc(600);
    memset(big, 'a', 255); big[255] = 0;
    HUNT_CHECK(ex_name_valid(big));
    big[255] = 'a'; big[256] = 0;
    HUNT_CHECK(!ex_name_valid(big));
    { size_t k; for (k = 0; k < 127; k++) memcpy(big + k * 2, "\xC3\xA9", 2); big[254] = 0; HUNT_CHECK(ex_name_valid(big)); big[254] = (char)0xC3; big[255] = (char)0xA9; big[256] = 0; HUNT_SPEC(!ex_name_valid(big), "include/exhaust_vault.h: at most 255 bytes (128 two-byte characters are 256)"); }
    free(big);

    hunt_step("kinds: by extension, any case");
    {
        struct { const char *name; ExFileKind k; } kinds[] = { { "a.md", EX_KIND_MARKDOWN }, { "a.MD", EX_KIND_MARKDOWN }, { "a.Md", EX_KIND_MARKDOWN }, { "a.base", EX_KIND_BASE }, { "a.BASE", EX_KIND_BASE }, { "a.canvas", EX_KIND_CANVAS }, { "a.png", EX_KIND_IMAGE }, { "a.PNG", EX_KIND_IMAGE },
            { "a.jpg", EX_KIND_IMAGE }, { "a.jpeg", EX_KIND_IMAGE }, { "a.gif", EX_KIND_IMAGE }, { "a.webp", EX_KIND_IMAGE }, { "a.svg", EX_KIND_IMAGE }, { "a.bmp", EX_KIND_IMAGE }, { "a.avif", EX_KIND_IMAGE }, { "a.mp3", EX_KIND_AUDIO }, { "a.wav", EX_KIND_AUDIO }, { "a.flac", EX_KIND_AUDIO },
            { "a.ogg", EX_KIND_AUDIO }, { "a.mp4", EX_KIND_VIDEO }, { "a.webm", EX_KIND_VIDEO }, { "a.pdf", EX_KIND_PDF }, { "a.PDF", EX_KIND_PDF }, { "a.txt", EX_KIND_OTHER }, { "a", EX_KIND_OTHER }, { "md", EX_KIND_OTHER }, { "a.", EX_KIND_OTHER }, { ".md", EX_KIND_OTHER },
            { "a.md.txt", EX_KIND_OTHER }, { "a.md ", EX_KIND_OTHER }, { "a.mdx", EX_KIND_OTHER }, { "a.m", EX_KIND_OTHER }, { "a.png.bak", EX_KIND_OTHER }, { "", EX_KIND_OTHER }, { "dir.md/file", EX_KIND_OTHER }, { "a.base.md", EX_KIND_MARKDOWN } };
        for (i = 0; i < sizeof kinds / sizeof *kinds; i++) {
            ExFileKind got = ex_file_kind(kinds[i].name);
            if (!strcmp(kinds[i].name, ".md")) { HUNT_SUSPECT(got == kinds[i].k, "a file named just `.md` is a hidden file, not a note: the index skips names with a dot first, and the explorer should agree"); continue; }
            HUNT_CHECKF(got == kinds[i].k, "ex_file_kind(\"%s\") = %d, want %d", kinds[i].name, (int)got, (int)kinds[i].k);
        }
    }
}

/* A matcher that cannot go wrong: dynamic programming over pattern and text. `**` crosses folders, `*` does not, `?` is one character that is not a slash. ASCII case is ignored. */
static int dp_glob(const char *p, const char *t) {
    size_t pl = strlen(p), tl = strlen(t), i, j;
    unsigned char *m = (unsigned char *)calloc((pl + 1) * (tl + 1), 1);
    #define M(a, b) m[(a) * (tl + 1) + (b)]
    int res;
    M(0, 0) = 1;
    for (i = 0; i < pl; i++) {
        int dstar = p[i] == '*' && ((i + 1 < pl && p[i + 1] == '*') || (i > 0 && p[i - 1] == '*'));
        for (j = 0; j <= tl; j++) {
            if (!M(i, j)) continue;
            if (p[i] == '*') {
                size_t k;
                M(i + 1, j) = 1;
                if (dstar) { for (k = j; k < tl; k++) { M(i + 1, k + 1) = 1; } }
                else { for (k = j; k < tl && t[k] != '/'; k++) M(i + 1, k + 1) = 1; }
            } else if (j < tl) {
                if (p[i] == '?') { if (t[j] != '/') M(i + 1, j + 1) = 1; }
                else if (tolower((unsigned char)p[i]) == tolower((unsigned char)t[j])) M(i + 1, j + 1) = 1;
            }
        }
    }
    res = M(pl, tl);
    #undef M
    free(m);
    return res;
}

static void test_patterns(void) {
    long rounds = hunt_iters(40000), r;
    static const char *const pieces_p[] = { "a", "b", "A", "/", "*", "**", "?", ".", "md", "x", "_" };
    static const char *const pieces_t[] = { "a", "b", "A", "/", ".", "md", "x", "_", "ab" };
    hunt_step("patterns: ex_glob_match against a dynamic programming matcher");
    for (r = 0; r < rounds; r++) {
        char p[40] = "", t[40] = "";
        size_t i, np = hunt_rnd_size(7), nt = hunt_rnd_size(9);
        int a, b;
        for (i = 0; i < np; i++) strcat(p, pieces_p[hunt_rnd(sizeof pieces_p / sizeof *pieces_p)]);
        for (i = 0; i < nt; i++) strcat(t, pieces_t[hunt_rnd(sizeof pieces_t / sizeof *pieces_t)]);
        a = ex_glob_match(p, t) != 0;
        b = dp_glob(p, t);
        if (a != b) { HUNT_CHECKF(0, "glob \"%s\" against \"%s\": ex_glob_match says %d, the reference says %d", p, t, a, b); if (hunt_.failures > 30) break; }
    }
    hunt_step("patterns: known cases");
    {
        struct { const char *p, *t; int ok; } g[] = { { "*.md", "a.md", 1 }, { "*.md", "dir/a.md", 0 }, { "**/*.md", "dir/a.md", 1 }, { "**.md", "dir/sub/a.md", 1 }, { "a?c", "abc", 1 }, { "a?c", "a/c", 0 }, { "A*", "abc", 1 }, { "*", "", 1 }, { "*", "a/b", 0 }, { "**", "a/b", 1 }, { "", "", 1 }, { "", "a", 0 },
            { "a**b", "a/x/b", 1 }, { "a*b", "a/x/b", 0 }, { "a/**/b", "a/x/y/b", 1 }, { "?", "\xC3\xA9", 1 } };
        size_t k;
        for (k = 0; k < sizeof g / sizeof *g; k++) {
            int got = ex_glob_match(g[k].p, g[k].t) != 0;
            if (!strcmp(g[k].p, "?")) HUNT_SUSPECT(got == g[k].ok, "? should match one character, and \xC3\xA9 is one character (two bytes)");
            else HUNT_CHECKF(got == g[k].ok, "glob \"%s\" against \"%s\": %d, want %d", g[k].p, g[k].t, got, g[k].ok);
        }
        HUNT_SUSPECT(ex_glob_match("a/**/b", "a/b") != 0, "a/**/b should match a/b (a double star matches zero folders, as in .gitignore)");
    }
    hunt_step("patterns: ex_patterns_match (a pattern without a slash matches any part of the path; a trailing slash means a folder)");
    {
        const char *p1[] = { "*.tmp" }, *p2[] = { "Private/" }, *p3[] = { "Archive/**" }, *p4[] = { "drafts/*.md" }, *p5[] = { "/top.md" }, *p6[] = { "node_modules" }, *p7[] = { "" }, *p8[] = { "**" }, *p9[] = { "a/b" };
        struct { const char *const *pats; const char *rel; int ok; } m[] = {
            { p1, "x.tmp", 1 }, { p1, "dir/x.tmp", 1 }, { p1, "dir.tmp/x.md", 1 }, { p1, "x.md", 0 }, { p2, "Private/a.md", 1 }, { p2, "Private", 1 }, { p2, "x/Private/a.md", 1 }, { p2, "Private.md", 0 }, { p2, "PRIVATE/a.md", 1 },
            { p3, "Archive/a/b.md", 1 }, { p3, "Archive", 0 }, { p4, "drafts/x.md", 1 }, { p4, "drafts/sub/x.md", 0 }, { p4, "a/drafts/x.md", 0 }, { p5, "top.md", 1 }, { p5, "sub/top.md", 0 }, { p6, "node_modules/a/b.js", 1 }, { p6, "x/node_modules", 1 },
            { p7, "a.md", 0 }, { p8, "anything/at/all", 1 }, { p9, "a/b/c.md", 1 }, { p9, "x/a/b", 0 },
        };
        size_t k;
        for (k = 0; k < sizeof m / sizeof *m; k++) {
            int got = ex_patterns_match(m[k].pats, 1, m[k].rel) != 0;
            HUNT_CHECKF(got == m[k].ok, "pattern \"%s\" against \"%s\": %d, want %d", m[k].pats[0], m[k].rel, got, m[k].ok);
        }
        HUNT_CHECK(ex_patterns_match(NULL, 0, "a.md") == 0);
        HUNT_CHECK(ex_patterns_match(p1, 0, "x.tmp") == 0);
    }
    hunt_step("patterns: a pattern that takes exponential time");
    {
        const char *pats[] = { "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b", "**a**a**a**a**a**a**a**a**a**a**a**a**a**a**a**a**b", "*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?*?x", "a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b" };
        size_t k;
        char text[80];
        memset(text, 'a', 60); text[60] = 0;
        for (k = 0; k < sizeof pats / sizeof *pats; k++) {
            clock_t t0 = clock();
            int got = ex_glob_match(pats[k], text);
            double s = hunt_cpu_since(t0);
            HUNT_CHECK(got == 0);
            if (s > hunt_time_limit(1.0)) { HUNT_SUSPECT(0, "matching an excluded-files pattern took more than a second: a pattern typed in Settings can freeze the file watcher and every folder scan"); hunt_note_("%s took %.2f s", pats[k], s); }
        }
    }
}

/* ---- B. the vault's actions ---- */

static int file_here(const char *rel) { return hunt_exists(rel); }

static void test_actions(void) {
    const char *root = hunt_fs_init("vault_actions");
    char vroot[600], outside[600];
    ExVault *v;
    char *out = NULL;
    ExVaultResult rc;
    char *canon;

    snprintf(vroot, sizeof vroot, "%s/v", root);
    ex_platform_make_dirs(vroot);
    snprintf(outside, sizeof outside, "%s/outside", root);
    ex_platform_make_dirs(outside);
    hunt_write_in(outside, "precious.md", "do not touch", 12);
    hunt_write_in(vroot, "Existing.md", "keep me", 7);
    hunt_write_in(vroot, "Folder/inner.md", "inner", 5);
    hunt_write_in(vroot, "Folder/Sub/deep.md", "deep", 4);
    hunt_write_in(vroot, "Other.md", "other", 5);

    v = ex_vault_open(vroot);
    HUNT_CHECK(v != NULL);
    if (!v) return;
    canon = ex_platform_realpath(vroot);
    HUNT_CHECK(canon && !strcmp(ex_vault_path(v), canon));
    HUNT_CHECK(strlen(ex_vault_id(v)) == 32);
    {
        const char *id = ex_vault_id(v);
        size_t k;
        for (k = 0; id[k]; k++) HUNT_CHECKF((id[k] >= '0' && id[k] <= '9') || (id[k] >= 'a' && id[k] <= 'f'), "the vault id %s is not 32 lowercase hexadecimal digits", id);
    }

    hunt_step("actions: creating notes");
    rc = ex_vault_create_note(v, "", "New", &out);
    HUNT_CHECK(rc == EX_VAULT_OK && out && !strcmp(out, "New.md") && file_here("v/New.md"));
    free(out); out = NULL;
    rc = ex_vault_create_note(v, "", "New", &out);
    HUNT_CHECKF(rc == EX_VAULT_EXISTS, "creating a note that exists gave %d", (int)rc);
    HUNT_CHECK(out == NULL);
    { size_t n = 0; char *t = hunt_read("v/New.md", &n); HUNT_CHECK(t && n == 0); free(t); }
    rc = ex_vault_create_note(v, "", "Existing", &out);
    HUNT_CHECK(rc == EX_VAULT_EXISTS);
    { size_t n = 0; char *t = hunt_read("v/Existing.md", &n); HUNT_CHECKF(t && n == 7 && !memcmp(t, "keep me", 7), "an existing note was changed by a failed create"); free(t); }
    free(out); out = NULL;
    rc = ex_vault_create_note(v, "", "existing", &out);
    HUNT_SUSPECT(rc == EX_VAULT_EXISTS, "a note whose name differs only by case from another is a second note on Linux and the same file on macOS and Windows: sync between them loses one; creating it should be refused");
    free(out); out = NULL;
    {
        const char *bad[] = { "", "a/b", "..", ".", "a:b", "CON", "a#b", "a|b", "a%%b", "x\n", " ", "a.", "con.md" };
        size_t k;
        for (k = 0; k < sizeof bad / sizeof *bad; k++) { rc = ex_vault_create_note(v, "", bad[k], &out); HUNT_CHECKF(rc == EX_VAULT_INVALID, "create_note(\"%s\") gave %d", bad[k], (int)rc); free(out); out = NULL; }
    }
    {
        const char *parents[] = { "..", "../outside", "Folder/../..", "/", "/tmp", "\\", "Folder\\Sub", "C:", "Folder//Sub", "./Folder", "Folder/.", "Folder/..", "Missing", "Existing.md" };
        size_t k;
        for (k = 0; k < sizeof parents / sizeof *parents; k++) {
            rc = ex_vault_create_note(v, parents[k], "Escape", &out);
            HUNT_CHECKF(rc != EX_VAULT_OK, "create_note in the folder \"%s\" succeeded (%s)", parents[k], out ? out : "");
            free(out); out = NULL;
        }
        HUNT_CHECK(!hunt_exists("outside/Escape.md") && !hunt_exists("Escape.md") && !hunt_exists("v/Escape.md"));
        HUNT_CHECK(!ex_platform_file_exists("/tmp/Escape.md"));
        rc = ex_vault_create_note(v, "Folder/Sub", "Deep2", &out);
        HUNT_CHECK(rc == EX_VAULT_OK && out && !strcmp(out, "Folder/Sub/Deep2.md"));
        free(out); out = NULL;
    }
    hunt_step("actions: creating folders");
    rc = ex_vault_create_folder(v, "", "NewFolder", &out);
    HUNT_CHECK(rc == EX_VAULT_OK && out && !strcmp(out, "NewFolder"));
    free(out); out = NULL;
    HUNT_CHECK(ex_vault_create_folder(v, "", "NewFolder", &out) == EX_VAULT_EXISTS);
    free(out); out = NULL;
    HUNT_CHECK(ex_vault_create_folder(v, "", "Existing.md", &out) == EX_VAULT_EXISTS);
    free(out); out = NULL;
    HUNT_CHECK(ex_vault_create_folder(v, "..", "x", &out) != EX_VAULT_OK);
    free(out); out = NULL;

    hunt_step("actions: renaming");
    {
        struct { const char *rel, *name; } bad[] = { { "Other.md", "Existing.md" }, { "Other.md", "a/b.md" }, { "Other.md", ".." }, { "Other.md", "" }, { "Other.md", "../x.md" }, { "Other.md", "Other.md" }, { "Missing.md", "x.md" }, { "", "x" }, { "../outside/precious.md", "x.md" }, { "/etc/passwd", "x" }, { "Folder/inner.md", "Other.md" } };
        size_t k;
        for (k = 0; k < sizeof bad / sizeof *bad; k++) {
            rc = ex_vault_rename(v, bad[k].rel, bad[k].name, &out);
            HUNT_CHECKF(rc != EX_VAULT_OK || (!strcmp(bad[k].rel, "Other.md") && !strcmp(bad[k].name, "Other.md")), "rename(\"%s\", \"%s\") gave %d", bad[k].rel, bad[k].name, (int)rc);
            free(out); out = NULL;
        }
        { size_t n = 0; char *t = hunt_read("v/Existing.md", &n); HUNT_CHECK(t && n == 7 && !memcmp(t, "keep me", 7)); free(t); t = hunt_read("v/Other.md", &n); HUNT_CHECK(t && n == 5); free(t); }
        HUNT_CHECK(hunt_exists("outside/precious.md"));
        rc = ex_vault_rename(v, "Other.md", "Renamed.md", &out);
        HUNT_CHECK(rc == EX_VAULT_OK && out && !strcmp(out, "Renamed.md") && file_here("v/Renamed.md") && !file_here("v/Other.md"));
        free(out); out = NULL;
        rc = ex_vault_rename(v, "Folder", "Folder2", &out);
        HUNT_CHECK(rc == EX_VAULT_OK && file_here("v/Folder2/inner.md") && file_here("v/Folder2/Sub/deep.md") && !file_here("v/Folder"));
        free(out); out = NULL;
        rc = ex_vault_rename(v, "Folder2", "Folder", &out);
        HUNT_CHECK(rc == EX_VAULT_OK);
        free(out); out = NULL;
    }
    hunt_step("actions: moving");
    {
        struct { const char *rel, *to; } bad[] = { { "Existing.md", "Missing" }, { "Existing.md", "../outside" }, { "Existing.md", "/tmp" }, { "Folder", "Folder" }, { "Folder", "Folder/Sub" }, { "Folder/Sub", "Folder/Sub" }, { "Folder", "Folder/Sub/Nope" }, { "Existing.md", "Existing.md" },
            { "../outside/precious.md", "" }, { "", "Folder" }, { "Existing.md", "Folder/inner.md" } };
        size_t k;
        hunt_write_in(vroot, "Folder/Existing.md", "other existing", 14);
        for (k = 0; k < sizeof bad / sizeof *bad; k++) {
            rc = ex_vault_move(v, bad[k].rel, bad[k].to, &out);
            HUNT_CHECKF(rc != EX_VAULT_OK, "move(\"%s\" into \"%s\") gave %d (%s)", bad[k].rel, bad[k].to, (int)rc, out ? out : "");
            free(out); out = NULL;
        }
        { size_t n = 0; char *t = hunt_read("v/Existing.md", &n); HUNT_CHECK(t && n == 7); free(t); t = hunt_read("v/Folder/Existing.md", &n); HUNT_CHECK(t && n == 14 && !memcmp(t, "other existing", 14)); free(t); }
        HUNT_CHECK(file_here("v/Folder/Sub/deep.md") && hunt_exists("outside/precious.md"));
        rc = ex_vault_move(v, "Renamed.md", "Folder/Sub", &out);
        HUNT_CHECK(rc == EX_VAULT_OK && out && !strcmp(out, "Folder/Sub/Renamed.md") && file_here("v/Folder/Sub/Renamed.md"));
        free(out); out = NULL;
        rc = ex_vault_move(v, "Folder/Sub/Renamed.md", "", &out);
        HUNT_CHECK(rc == EX_VAULT_OK && out && !strcmp(out, "Renamed.md"));
        free(out); out = NULL;
    }
    hunt_step("actions: deleting (never the vault, never outside it)");
    {
        ExVaultOptions o;
        const char *bad[] = { "", "..", "../outside", "../outside/precious.md", "/", "/tmp", "Folder/..", "Folder/../..", "./", ".", "Folder//..", "C:", "\\", "Missing.md" };
        size_t k;
        ex_vault_get_options(v, &o);
        o.trash_mode = EX_TRASH_PERMANENT;
        ex_vault_set_options(v, &o);
        for (k = 0; k < sizeof bad / sizeof *bad; k++) {
            rc = ex_vault_delete(v, bad[k]);
            HUNT_CHECKF(rc != EX_VAULT_OK, "delete(\"%s\") returned OK", bad[k]);
            HUNT_CHECKF(ex_platform_file_exists(vroot) && hunt_exists("outside/precious.md") && file_here("v/Existing.md") && file_here("v/Folder/Sub/deep.md"), "after delete(\"%s\") something that must not be deleted is gone", bad[k]);
        }
        rc = ex_vault_delete(v, "Folder/Sub/Deep2.md");
        HUNT_CHECK(rc == EX_VAULT_OK && !file_here("v/Folder/Sub/Deep2.md"));
        rc = ex_vault_delete(v, "Folder");
        HUNT_CHECK(rc == EX_VAULT_OK && !file_here("v/Folder"));
        HUNT_CHECK(file_here("v/Existing.md"));
        o.trash_mode = EX_TRASH_VAULT;
        ex_vault_set_options(v, &o);
        rc = ex_vault_delete(v, "Existing.md");
        HUNT_SPEC(rc == EX_VAULT_OK && !file_here("v/Existing.md") && file_here("v/.trash/Existing.md"), "docs/formats.md, Vault folders that Exhaust owns: `.trash` holds notes deleted with trash_mode = vault_trash (the deleted file keeps its path)");
        {
            size_t n = 0;
            char *t = hunt_read("v/.trash/Existing.md", &n);
            HUNT_CHECKF(t && n == 7 && !memcmp(t, "keep me", 7), "the note in .trash is not the note that was deleted");
            free(t);
        }
        hunt_write_in(vroot, "Existing.md", "second version", 14);
        rc = ex_vault_delete(v, "Existing.md");
        HUNT_CHECKF(rc == EX_VAULT_OK, "deleting a second note with the same name into .trash gave %d", (int)rc);
        {
            size_t n = 0;
            char *t = hunt_read("v/.trash/Existing.md", &n);
            HUNT_CHECKF(t && n == 7 && !memcmp(t, "keep me", 7), "the second delete replaced the first note in .trash: a deleted note was lost for good");
            free(t);
        }
    }
    hunt_step("actions: a symbolic link that leaves the vault");
    {
#if !defined(_WIN32)
        char link[700], target[700];
        snprintf(link, sizeof link, "%s/v/link_out", root);
        snprintf(target, sizeof target, "%s/outside", root);
        if (symlink(target, link) == 0) {
            ExVaultOptions o;
            ex_vault_rescan(v);
            ex_vault_get_options(v, &o); o.trash_mode = EX_TRASH_PERMANENT; ex_vault_set_options(v, &o);
            rc = ex_vault_delete(v, "link_out");
            HUNT_CHECKF(hunt_exists("outside/precious.md"), "deleting a symbolic link deleted what it points to (delete(link_out) gave %d)", (int)rc);
            if (!ex_platform_file_exists(link)) { if (symlink(target, link) != 0) {} }
            rc = ex_vault_rename(v, "link_out", "link_renamed", &out);
            free(out); out = NULL;
            HUNT_CHECK(hunt_exists("outside/precious.md"));
            unlink(link);
        }
#endif
    }
    free(canon);
    ex_vault_close(v);
}

/* ---- C. the registry ---- */

static void test_registry(void) {
    const char *root = hunt_fs_init("vault_registry");
    char ini[600], v1[600], v2[600], v3[600];
    ExRegistry *r;
    const char *id1, *id2, *id3;
    ExRegistryEntry e;
    size_t i;
    snprintf(ini, sizeof ini, "%s/vaults.ini", root);
    snprintf(v1, sizeof v1, "%s/plain", root);
    snprintf(v2, sizeof v2, "%s/Notes #1 (old)", root);
    snprintf(v3, sizeof v3, "%s/a = b; [c] \xC3\xA9", root);
    ex_platform_make_dirs(v1); ex_platform_make_dirs(v2); ex_platform_make_dirs(v3);
    hunt_step("registry: add, save, load, with folder names that mean something to an INI file");
    r = ex_registry_load(ini);
    HUNT_CHECK(r != NULL && ex_registry_count(r) == 0);
    if (!r) return;
    id1 = ex_registry_add(r, v1);
    HUNT_CHECK(id1 != NULL);
    {
        char *copy = id1 ? hunt_strdup(id1) : NULL;
        const char *again = ex_registry_add(r, v1);
        HUNT_CHECK(again && copy && !strcmp(again, copy) && ex_registry_count(r) == 1);
        free(copy);
    }
    id2 = ex_registry_add(r, v2);
    id3 = ex_registry_add(r, v3);
    HUNT_CHECK(id2 && id3 && ex_registry_count(r) == 3);
    HUNT_CHECK(ex_registry_add(r, "/no/such/folder/at/all") == NULL);
    HUNT_CHECK(ex_registry_add(r, ini) == NULL);                 /* a file is not a vault */
    if (id1 && id2 && id3) {
        char *i1 = hunt_strdup(id1), *i2 = hunt_strdup(id2), *i3 = hunt_strdup(id3);
        char *c1 = ex_platform_realpath(v1), *c2 = ex_platform_realpath(v2), *c3 = ex_platform_realpath(v3);
        HUNT_EQ_INT(ex_registry_rename(r, i1, "Plain name"), 0);
        HUNT_EQ_INT(ex_registry_rename(r, i2, "Name with # hash and = equals"), 0);
        HUNT_EQ_INT(ex_registry_rename(r, i3, "Line one\nline two = injected\n[vaults]\nlast = nonsense"), 0);
        HUNT_CHECK(ex_registry_rename(r, "nonexistent id", "x") != 0);
        HUNT_CHECK(ex_registry_rename(r, i1, "") != 0);
        HUNT_EQ_INT(ex_registry_touch(r, i2, 1700000000), 0);
        HUNT_EQ_INT(ex_registry_save(r), 0);
        ex_registry_free(r);
        r = ex_registry_load(ini);
        HUNT_CHECK(r != NULL);
        if (r) {
            int found1 = 0, found2 = 0, found3 = 0;
            HUNT_CHECKF(ex_registry_count(r) == 3, "three vaults were saved and %lu were read back", (unsigned long)ex_registry_count(r));
            for (i = 0; i < ex_registry_count(r); i++) {
                ex_registry_get(r, i, &e);
                if (!strcmp(e.id, i1)) { found1 = 1; HUNT_CHECK(e.path && c1 && !strcmp(e.path, c1)); HUNT_CHECK(e.name && !strcmp(e.name, "Plain name")); }
                if (!strcmp(e.id, i2)) { found2 = 1; HUNT_CHECKF(e.path && c2 && !strcmp(e.path, c2), "the path of a vault whose folder name has ' #' was saved as \"%s\" and read back as \"%s\"", c2 ? c2 : "", e.path ? e.path : "(null)");
                    HUNT_CHECKF(e.name && !strcmp(e.name, "Name with # hash and = equals"), "a name with ' # ' came back as \"%s\"", e.name ? e.name : "(null)"); HUNT_CHECK(e.opened == 1700000000); }
                if (!strcmp(e.id, i3)) { found3 = 1; HUNT_CHECKF(e.path && c3 && !strcmp(e.path, c3), "the path of a vault whose folder name has '=', ';' and brackets came back as \"%s\"", e.path ? e.path : "(null)");
                    HUNT_CHECKF(e.name && !strcmp(e.name, "Line one\nline two = injected\n[vaults]\nlast = nonsense"), "a name with line breaks came back as \"%s\"", e.name ? e.name : "(null)"); }
            }
            HUNT_CHECK(found1 && found2 && found3);
            HUNT_CHECKF(ex_registry_last(r) == NULL || !strcmp(ex_registry_last(r), i2), "the last vault is \"%s\" after the injected `last = nonsense`", ex_registry_last(r) ? ex_registry_last(r) : "(null)");
            HUNT_EQ_INT(ex_registry_remove(r, i1), 0);
            HUNT_CHECK(ex_registry_remove(r, i1) != 0);
            HUNT_CHECK(ex_registry_count(r) == 2);
            HUNT_CHECK(ex_platform_file_exists(v1));            /* removing from the list touches no file */
            ex_registry_free(r);
        }
        free(i1); free(i2); free(i3); free(c1); free(c2); free(c3);
    } else ex_registry_free(r);
    hunt_step("registry: a damaged file");
    {
        const char *junk[] = { "", "[", "[vault.", "garbage\n", "[vault.zzz]\npath = /nonexistent\n", "\xFF\xFE\x00\x01", "[vaults]\nlast = x\n[vault.1]\n[vault.1]\n", "[vault.a]\npath =\nname =\nopened = abc\n" };
        for (i = 0; i < sizeof junk / sizeof *junk; i++) {
            ExRegistry *g;
            hunt_write_in(root, "bad.ini", junk[i], i == 5 ? 4 : strlen(junk[i]));
            snprintf(ini, sizeof ini, "%s/bad.ini", root);
            g = ex_registry_load(ini);
            HUNT_CHECKF(g != NULL, "a damaged vaults.ini (case %lu) gave no registry", (unsigned long)i);
            if (g) { size_t k; for (k = 0; k < ex_registry_count(g); k++) ex_registry_get(g, k, &e); (void)ex_registry_last(g); ex_registry_save(g); ex_registry_free(g); }
        }
    }
}

/* ---- D. file recovery snapshots ---- */

static int set_zone(const char *tz) {
#if !defined(_WIN32)
    setenv("TZ", tz, 1);
    tzset();
    return 1;
#else
    (void)tz; return 0;
#endif
}

static void test_history(void) {
    const char *root = hunt_fs_init("history");
    char dir[600], enc1[800], enc2[800], file[900];
    ExSnapshot *list = NULL;
    size_t n, i;
    int64_t now = 1700000000;

    snprintf(dir, sizeof dir, "%s/rec", root);
    ex_platform_make_dirs(dir);
    set_zone("UTC");

    hunt_step("history: names that tell notes apart");
    {
        const char *notes[] = { "a.md", "a/b.md", "a__b.md", "a_b.md", "a_ub.md", "a_u_ub.md", "A.md", "a b.md", "a.md.md", "dir/a_b.md", "dir_ua/b.md", "\xC3\xA9.md", "e\xCC\x81.md", "x/y/z.md", "x__y/z.md", "_", "__", "/", "_/", "/_", "u", "_u" };
        size_t a, b;
        char encs[sizeof notes / sizeof *notes][800];
        for (a = 0; a < sizeof notes / sizeof *notes; a++) {
            size_t l = ex_history_encode(notes[a], encs[a], sizeof encs[a]);
            HUNT_CHECKF(l > 0 && l == strlen(encs[a]), "encode(\"%s\") returned %lu", notes[a], (unsigned long)l);
            HUNT_CHECKF(!strchr(encs[a], '/'), "encode(\"%s\") = \"%s\" still has a slash", notes[a], encs[a]);
        }
        for (a = 0; a < sizeof notes / sizeof *notes; a++) for (b = a + 1; b < sizeof notes / sizeof *notes; b++)
            if (!strcmp(notes[a], "\xC3\xA9.md") || !strcmp(notes[b], "\xC3\xA9.md")) continue; else
            HUNT_CHECKF(strcmp(encs[a], encs[b]) != 0, "the notes \"%s\" and \"%s\" have the same snapshot name \"%s\"", notes[a], notes[b], encs[a]);
        HUNT_CHECK(ex_history_encode("", enc1, sizeof enc1) == 0);
        HUNT_CHECK(ex_history_encode(NULL, enc1, sizeof enc1) == 0);
        HUNT_CHECK(ex_history_encode("abc", enc1, 3) == 0);
        HUNT_CHECK(ex_history_encode("abc", enc1, 4) == 3 || ex_history_encode("abc", enc1, 4) == 0);
        { char tiny[5]; size_t l = ex_history_encode("a_b", tiny, sizeof tiny); HUNT_CHECK(l == 0 || strlen(tiny) < sizeof tiny); }
        HUNT_EQ_STR((ex_history_encode("a/b_c", enc1, sizeof enc1), enc1), "a__b_uc");
    }
    hunt_step("history: very long names stay one name for one note and never cut a character");
    {
        char longa[400], longb[400];
        memset(longa, 'x', 300); longa[300] = 0;
        memset(longb, 'x', 300); longb[299] = 'y'; longb[300] = 0;
        HUNT_CHECK(ex_history_encode(longa, enc1, sizeof enc1) > 0 && ex_history_encode(longb, enc2, sizeof enc2) > 0);
        HUNT_CHECKF(strcmp(enc1, enc2) != 0, "two long names that differ in the last byte have the same snapshot name");
        HUNT_CHECK(strlen(enc1) <= 150 && strlen(enc2) <= 150);
        {
            char uni[400];
            size_t k;
            for (k = 0; k < 140; k++) memcpy(uni + k * 2, "\xC3\xA9", 2);
            uni[280] = 0;
            ex_history_encode(uni, enc1, sizeof enc1);
            { const unsigned char *p = (const unsigned char *)enc1; int bad = 0; while (*p) { if (*p >= 0xC0 && (p[1] & 0xC0) != 0x80) bad = 1; p++; } HUNT_CHECKF(!bad, "a long name of accents was cut in the middle of a character"); }
        }
    }

    hunt_step("history: take, list (newest first), read");
    HUNT_EQ_INT(ex_history_take(dir, "a.md", "version one", 11, now, EX_SNAP_AUTO), 0);
    HUNT_EQ_INT(ex_history_take(dir, "a.md", "version two", 11, now + 60, EX_SNAP_AUTO), 0);
    HUNT_EQ_INT(ex_history_take(dir, "a.md", "conflict", 8, now + 120, EX_SNAP_CONFLICT), 0);
    HUNT_EQ_INT(ex_history_take(dir, "a_b.md", "other note", 10, now + 30, EX_SNAP_AUTO), 0);
    HUNT_EQ_INT(ex_history_take(dir, "a/b.md", "folder note", 11, now + 40, EX_SNAP_AUTO), 0);
    n = ex_history_list(dir, "a.md", &list);
    HUNT_CHECKF(n == 3, "a.md has %lu snapshots, want 3", (unsigned long)n);
    if (n == 3) {
        HUNT_CHECK(list[0].time == now + 120 && list[0].kind == EX_SNAP_CONFLICT && list[1].time == now + 60 && list[2].time == now);
        HUNT_CHECK(list[0].size == 8 && list[1].size == 11);
        { size_t l = 0; char *t = ex_history_read(dir, list[2].file, &l); HUNT_CHECK(t && l == 11 && !memcmp(t, "version one", 11) && t[l] == 0); free(t); }
    }
    ex_history_free(list, n);
    n = ex_history_list(dir, "a_b.md", &list); HUNT_CHECK(n == 1); ex_history_free(list, n);
    n = ex_history_list(dir, "a/b.md", &list); HUNT_CHECK(n == 1); ex_history_free(list, n);
    n = ex_history_list(dir, "nothing.md", &list); HUNT_CHECK(n == 0); ex_history_free(list, n);
    HUNT_CHECK(ex_history_last_auto(dir, "a.md") == now + 60);
    HUNT_CHECK(ex_history_last_auto(dir, "nothing.md") == 0);

    hunt_step("history: a second snapshot in the same second replaces the first (same kind only)");
    HUNT_EQ_INT(ex_history_take(dir, "a.md", "replaced", 8, now + 60, EX_SNAP_AUTO), 0);
    n = ex_history_list(dir, "a.md", &list);
    HUNT_CHECKF(n == 3, "a.md has %lu snapshots after a replacement, want 3", (unsigned long)n);
    ex_history_free(list, n);
    HUNT_EQ_INT(ex_history_take(dir, "a.md", "same second conflict", 20, now + 60, EX_SNAP_CONFLICT), 0);
    n = ex_history_list(dir, "a.md", &list);
    HUNT_CHECKF(n == 4, "a conflict copy in the same second as an automatic snapshot replaced it (%lu snapshots)", (unsigned long)n);
    ex_history_free(list, n);

    hunt_step("history: reading by a name that tries to leave the folder");
    {
        const char *evil[] = { "../precious.md", "../../etc/passwd", "/etc/passwd", "a.md.20231114-221320.snap/../../x", "..\\x", "a.md", "a.md.snap", "a.md.20231114-221320", "a.md.20231114-221320.txt", "", ".", "..", "rec", "a.md.20231114-221320.snap\x01",
            "a.md.20231114-221320.snap/", "./a.md.20231114-221320.snap", "a/../a.md.20231114-221320.snap" };
        size_t k;
        hunt_write_in(root, "precious.md", "secret", 6);
        hunt_write_in(dir, "a.md.20231114-221320.snap", "snap", 4);
        for (k = 0; k < sizeof evil / sizeof *evil; k++) {
            size_t l = 0;
            char *t = ex_history_read(dir, evil[k], &l);
            if (k >= 14) { HUNT_SUSPECT(t == NULL, "a name with a folder part is not a snapshot file name"); }
            else HUNT_CHECKF(t == NULL, "ex_history_read(\"%s\") returned the contents of a file", evil[k]);
            free(t);
        }
    }

    hunt_step("history: foreign files are ignored, and survive pruning and clearing");
    {
        hunt_write_in(dir, "notes.txt", "mine", 4);
        hunt_write_in(dir, "a.md.garbage.snap", "x", 1);
        hunt_write_in(dir, "a.md.20231114-221320.snap.tmp", "x", 1);
        hunt_write_in(dir, "a_b.md.99999999-999999.snap", "x", 1);
        hunt_write_in(dir, "a.md.2023-11-14.snap", "x", 1);
        n = ex_history_list(dir, "a.md", &list);
        for (i = 0; i < n; i++) HUNT_CHECKF(strncmp(list[i].file, "a.md.garbage", 12) != 0 && !strstr(list[i].file, ".tmp") && !strstr(list[i].file, "2023-11-14"), "the file %s was listed as a snapshot", list[i].file);
        ex_history_free(list, n);
        ex_history_prune(dir, now + 100 * 86400, 1);
        ex_history_clear(dir, NULL);
        HUNT_CHECK(hunt_exists("rec/notes.txt") && hunt_exists("rec/a.md.garbage.snap"));
    }
    hunt_step("history: pruning by age keeps conflict copies, clearing keeps them too");
    {
        ex_platform_remove_tree(dir); ex_platform_make_dirs(dir);
        ex_history_take(dir, "p.md", "old", 3, now, EX_SNAP_AUTO);
        ex_history_take(dir, "p.md", "older conflict", 14, now - 5 * 86400, EX_SNAP_CONFLICT);
        ex_history_take(dir, "p.md", "recent", 6, now + 10 * 86400, EX_SNAP_AUTO);
        ex_history_take(dir, "q.md", "q old", 5, now, EX_SNAP_AUTO);
        HUNT_CHECK(ex_history_prune(dir, now + 10 * 86400 + 5, 5) == 2);                   /* p old and q old are 10 days old; recent is 5 seconds old */
        n = ex_history_list(dir, "p.md", &list);
        HUNT_CHECKF(n == 2, "after pruning p.md has %lu snapshots, want the recent one and the conflict copy", (unsigned long)n);
        ex_history_free(list, n);
        HUNT_CHECK(ex_history_prune(dir, now + 10 * 86400 + 5, 0) == 0);                    /* days below 1 count as 1 */
        HUNT_CHECK(ex_history_prune(dir, now + 10 * 86400 + 86400 + 5, 0) == 1);
        HUNT_CHECK(ex_history_prune(dir, now + 400 * 86400, -3) == 0);
        ex_history_take(dir, "p.md", "x", 1, now + 1, EX_SNAP_AUTO);
        ex_history_take(dir, "q.md", "y", 1, now + 2, EX_SNAP_AUTO);
        HUNT_EQ_INT(ex_history_clear(dir, "p.md"), 1);
        n = ex_history_list(dir, "q.md", &list); HUNT_CHECK(n == 1); ex_history_free(list, n);
        n = ex_history_list(dir, "p.md", &list); HUNT_CHECKF(n == 1 && list[0].kind == EX_SNAP_CONFLICT, "Clear history on p.md must leave the conflict copy (%lu left)", (unsigned long)n); ex_history_free(list, n);
        HUNT_CHECK(ex_history_clear(dir, NULL) == 1);
        n = ex_history_list(dir, "p.md", &list); HUNT_CHECK(n == 1); ex_history_free(list, n);
    }
    hunt_step("history: the folder does not exist, and absurd times");
    {
        char nodir[700];
        snprintf(nodir, sizeof nodir, "%s/does/not/exist", root);
        (void)ex_history_take(nodir, "a.md", "x", 1, now, EX_SNAP_AUTO);
        n = ex_history_list(nodir, "a.md", &list); HUNT_CHECK(n == 0); ex_history_free(list, n);
        (void)ex_history_take(dir, "t.md", "x", 1, 0, EX_SNAP_AUTO);
        (void)ex_history_take(dir, "t.md", "x", 1, -1, EX_SNAP_AUTO);
        (void)ex_history_take(dir, "t.md", "x", 1, 253402300799LL, EX_SNAP_AUTO);
        (void)ex_history_take(dir, "t.md", "x", 1, (int64_t)1 << 60, EX_SNAP_AUTO);
        (void)ex_history_take(dir, "t.md", "x", 1, -((int64_t)1 << 60), EX_SNAP_AUTO);
        n = ex_history_list(dir, "t.md", &list); ex_history_free(list, n);
        { char b[40]; ex_history_format_time(0, b, sizeof b); ex_history_format_time((int64_t)1 << 60, b, sizeof b); ex_history_format_time(-1, b, 1); ex_history_format_time(5, NULL, 0); ex_history_format_time(1700000000, b, 4); }
        (void)file;
    }

    hunt_step("history: local time stamps across a daylight saving change");
    {
        /* 2024-11-03 in New York: 01:30 happens twice, once at 05:30 UTC (EDT) and once at 06:30 UTC (EST) */
        int64_t first = 1730611800, second = first + 3600;       /* 05:30:00Z and 06:30:00Z */
        set_zone("America/New_York");
        {
            struct tm a, b;
            time_t t1 = (time_t)first, t2 = (time_t)second;
            localtime_r(&t1, &a); localtime_r(&t2, &b);
            if (a.tm_hour == 1 && a.tm_min == 30 && b.tm_hour == 1 && b.tm_min == 30) {
                ex_platform_remove_tree(dir); ex_platform_make_dirs(dir);
                ex_history_take(dir, "dst.md", "first 01:30", 11, first, EX_SNAP_AUTO);
                ex_history_take(dir, "dst.md", "second 01:30", 12, second, EX_SNAP_AUTO);
                n = ex_history_list(dir, "dst.md", &list);
                HUNT_SUSPECT(n == 2, "two snapshots an hour apart, both at 01:30 local time on the night the clocks go back, have the same file name: the second silently replaced the first");
                if (n == 2) HUNT_CHECK(list[0].time != list[1].time);
                if (n >= 1) { size_t l = 0; char *t = ex_history_read(dir, list[n - 1].file, &l); HUNT_CHECK(t != NULL); free(t); }
                ex_history_free(list, n);
                /* and the time that comes back is the time that went in, in every hour of that night */
                {
                    int64_t t;
                    for (t = first - 7200; t <= first + 7200; t += 600) {
                        char nm[40];
                        snprintf(nm, sizeof nm, "night%lld.md", (long long)t);
                        ex_history_take(dir, nm, "x", 1, t, EX_SNAP_AUTO);
                        n = ex_history_list(dir, nm, &list);
                        if (n == 1 && list[0].time != t) HUNT_SUSPECT(0, "the time listed for a snapshot is not the time it was taken (local time stamps are ambiguous for an hour a year)");
                        if (n == 1 && list[0].time != t) { hunt_note_("taken at %lld, listed at %lld", (long long)t, (long long)list[0].time); ex_history_free(list, n); break; }
                        ex_history_free(list, n);
                    }
                }
            } else { HUNT_SUSPECT(1, "this machine has no time zone database: the daylight saving checks were skipped"); }
        }
        set_zone("UTC");
        /* a spring-forward gap: 02:30 does not exist in New York on 2024-03-10 */
        set_zone("America/New_York");
        ex_platform_remove_tree(dir); ex_platform_make_dirs(dir);
        { int64_t t0 = 1710054000; int64_t k; for (k = 0; k < 4; k++) { char nm[40]; snprintf(nm, sizeof nm, "gap%lld.md", (long long)k); ex_history_take(dir, nm, "x", 1, t0 + k * 1800, EX_SNAP_AUTO); n = ex_history_list(dir, nm, &list); if (n == 1 && list[0].time != t0 + k * 1800) { HUNT_SUSPECT(0, "the time listed for a snapshot is not the time it was taken, around the spring daylight saving change"); hunt_note_("taken at %lld, listed at %lld", (long long)(t0 + k * 1800), (long long)list[0].time); } ex_history_free(list, n); } }
        set_zone("UTC");
    }
}

int main(int argc, char **argv) {
    hunt_begin("hunt_vault", argc, argv);
    test_names();
    test_patterns();
    test_actions();
    test_registry();
    test_history();
    return hunt_end();
}
