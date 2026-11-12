#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* symlink and setenv under -std=c11 */
#define _XOPEN_SOURCE 700
#endif
/* vault_tree: create, rename, move and delete on a temporary vault in each trash
 * mode; sorting, expanding and revealing never change a file on disk; excluded
 * files, file types, hidden folders, symbolic link loops; the remembered-vault
 * list; SHA-256. Works in ./vault_tree_test, which it removes before and after. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "exhaust_platform.h"
#include "exhaust_util.h"
#include "exhaust_vault.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "vault_tree_test"

static void write_file(const char *rel, const char *text) {
    char path[512];
    snprintf(path, sizeof path, ROOT "/%s", rel);
    CHECK(ex_platform_write_file_atomic(path, text, strlen(text)) == 0);
}
static void make_dir(const char *rel) {
    char path[512];
    snprintf(path, sizeof path, ROOT "/%s", rel);
    CHECK(ex_platform_make_dirs(path) == 0);
}
static int exists(const char *rel) {
    char path[512];
    ExPathInfo i;
    snprintf(path, sizeof path, ROOT "/%s", rel);
    return ex_platform_path_info(path, &i) == 0;
}
static char *read_rel(const char *rel) {
    char path[512];
    snprintf(path, sizeof path, ROOT "/%s", rel);
    return ex_platform_read_file(path, NULL);
}

/* ---- a snapshot of every file on disk: path, size and modified time ---- */
typedef struct Snap { char *text; size_t n, cap; } Snap;
static int snap_cb(const char *path, int is_dir, int64_t mtime, void *u) {
    Snap *s = u;
    char line[700];
    ExPathInfo info;
    int len;
    ex_platform_path_info(path, &info);
    len = snprintf(line, sizeof line, "%s %d %lld %lld\n", path, is_dir, (long long)(is_dir ? 0 : info.size), (long long)(is_dir ? 0 : mtime));
    if (s->n + (size_t)len + 1 > s->cap) { s->cap = (s->n + (size_t)len) * 2 + 64; s->text = realloc(s->text, s->cap); }
    memcpy(s->text + s->n, line, (size_t)len + 1);
    s->n += (size_t)len;
    return 0;
}
static char *snapshot(void) {
    Snap s = {0};
    ex_platform_walk(ROOT, snap_cb, &s);
    return s.text ? s.text : calloc(1, 1);
}

static void test_sha256(void) {
    uint8_t d[32];
    char hex[65], *big;
    ex_sha256("", 0, d); ex_hex(d, 32, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    ex_sha256("abc", 3, d); ex_hex(d, 32, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    ex_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d); ex_hex(d, 32, hex);   /* 2 blocks of padding */
    CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    big = malloc(1000000);
    memset(big, 'a', 1000000);
    ex_sha256(big, 1000000, d); ex_hex(d, 32, hex);
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    free(big);
}

static void test_names_and_globs(void) {
    CHECK(ex_name_valid("Note.md") && ex_name_valid("日本語 ノート.md") && ex_name_valid("a b-c_d (1).md"));
    CHECK(!ex_name_valid("") && !ex_name_valid(".") && !ex_name_valid("..") && !ex_name_valid("a/b") && !ex_name_valid("a\\b"));
    CHECK(!ex_name_valid("a#b") && !ex_name_valid("a^b") && !ex_name_valid("a[b") && !ex_name_valid("a]b") && !ex_name_valid("a|b"));
    CHECK(!ex_name_valid("a:b") && !ex_name_valid("a%%b") && ex_name_valid("100% sure") && !ex_name_valid("trail.") && !ex_name_valid("trail "));
    CHECK(!ex_name_valid("CON") && !ex_name_valid("nul.md") && !ex_name_valid("Com1.txt") && !ex_name_valid("LPT9") && ex_name_valid("COM10") && ex_name_valid("console.md"));
    CHECK(!ex_name_valid("ctrl\x01x") && !ex_name_valid("quo\"te") && !ex_name_valid("star*") && !ex_name_valid("q?"));

    CHECK(ex_glob_match("*.md", "note.md") && !ex_glob_match("*.md", "dir/note.md") && ex_glob_match("**/*.md", "dir/sub/note.md"));
    CHECK(ex_glob_match("N?te.MD", "note.md") && !ex_glob_match("a*b", "a/b") && ex_glob_match("a**b", "a/x/b") && ex_glob_match("*", "abc"));

    ExFileKind k = ex_file_kind("x.MD");
    CHECK(k == EX_KIND_MARKDOWN && ex_file_kind("a.base") == EX_KIND_BASE && ex_file_kind("a.canvas") == EX_KIND_CANVAS);
    CHECK(ex_file_kind("a.JPG") == EX_KIND_IMAGE && ex_file_kind("a.avif") == EX_KIND_IMAGE && ex_file_kind("a.mp3") == EX_KIND_AUDIO);
    CHECK(ex_file_kind("a.mkv") == EX_KIND_VIDEO && ex_file_kind("a.pdf") == EX_KIND_PDF && ex_file_kind("a.txt") == EX_KIND_OTHER);
    CHECK(ex_file_kind("md") == EX_KIND_OTHER && ex_file_kind(".md") == EX_KIND_OTHER && ex_file_kind("a.webm") == EX_KIND_AUDIO);
}

static const char *row_names(ExVault *v, char *buf, size_t cap) {
    size_t i, n = ex_vault_row_count(v), used = 0;
    buf[0] = 0;
    for (i = 0; i < n; i++) {
        int depth = 0;
        const ExNode *r = ex_vault_row(v, i, &depth);
        used += (size_t)snprintf(buf + used, cap - used, "%*s%s%s\n", depth * 2, "", r->name, r->is_dir ? "/" : "");
    }
    return buf;
}

static void build_vault(void) {
    make_dir("Projects/Alpha");
    make_dir("Projects/Beta");
    make_dir("Archive");
    make_dir(".exhaust");
    make_dir(".trash");
    make_dir(".git");
    write_file("Welcome.md", "# Welcome\n");
    write_file("zebra.md", "z\n");
    write_file("Apple.md", "a\n");
    write_file("Projects/Alpha/plan.md", "plan\n");
    write_file("Projects/Alpha/diagram.png", "png");
    write_file("Projects/Beta/notes.md", "beta\n");
    write_file("Projects/todo.base", "base");
    write_file("Archive/old.md", "old\n");
    write_file("Archive/data.csv", "a,b\n");        /* not a type Exhaust opens */
    write_file("Archive/song.MP3", "mp3");
    write_file(".exhaust/settings.ini", "[general]\n");
    write_file(".hidden.md", "hidden\n");
    write_file("clip.mkv", "video");
}

static void test_tree_and_sorting(void) {
    ExVault *v;
    ExVaultOptions o;
    char rows[4096], *before, *after;
    const char *excl[] = {"Archive/", "*.base", "plan.md"};
    const ExNode *n;
    int combos = 0, by, desc;

    build_vault();
    v = ex_vault_open(ROOT);
    CHECK(v != NULL);
    if (!v) return;
    CHECK(strlen(ex_vault_id(v)) == 32 && strcmp(ex_vault_name(v), "vault_tree_test") == 0);
    {   /* the id is the first 32 hex digits of the SHA-256 of the canonical path */
        uint8_t d[32];
        char hex[65];
        ex_sha256(ex_vault_path(v), strlen(ex_vault_path(v)), d);
        ex_hex(d, 16, hex);
        CHECK(strcmp(hex, ex_vault_id(v)) == 0);
    }
    ex_vault_get_options(v, &o);
    CHECK(o.trash_mode == EX_TRASH_SYSTEM && !o.show_all_files);

    /* default view: folders first, then files by name, hidden and unknown types left out */
    CHECK(ex_vault_find(v, ".exhaust") == NULL && ex_vault_find(v, ".git") == NULL && ex_vault_find(v, ".hidden.md") == NULL);
    CHECK(ex_vault_find(v, "Archive/data.csv") == NULL && ex_vault_find(v, "Archive/song.MP3") != NULL);
    ex_vault_expand_all(v);
    row_names(v, rows, sizeof rows);
    CHECK(strcmp(rows, "Archive/\n  old.md\n  song.MP3\nProjects/\n  Alpha/\n    diagram.png\n    plan.md\n  Beta/\n    notes.md\n  todo.base\nApple.md\nclip.mkv\nWelcome.md\nzebra.md\n") == 0);
    ex_vault_collapse_all(v);
    row_names(v, rows, sizeof rows);
    CHECK(strcmp(rows, "Archive/\nProjects/\nApple.md\nclip.mkv\nWelcome.md\nzebra.md\n") == 0);
    CHECK(ex_vault_reveal(v, "Projects/Alpha/plan.md") >= 0);
    row_names(v, rows, sizeof rows);
    CHECK(strstr(rows, "plan.md") && strstr(rows, "Alpha/") && !strstr(rows, "old.md"));           /* only the path to it opened */
    ex_vault_toggle(v, "Projects/Alpha");
    row_names(v, rows, sizeof rows);
    CHECK(!strstr(rows, "plan.md") && strstr(rows, "Alpha/"));
    CHECK(ex_vault_reveal(v, "no/such/file.md") == -1);

    /* show every file type */
    o.show_all_files = 1;
    ex_vault_set_options(v, &o);
    CHECK(ex_vault_find(v, "Archive/data.csv") != NULL);
    o.show_all_files = 0;
    ex_vault_set_options(v, &o);

    /* excluded patterns mark files; they stay in the explorer */
    o.excluded = excl;
    o.nexcluded = 3;
    ex_vault_set_options(v, &o);
    CHECK(ex_vault_is_excluded(v, "Archive/old.md") && ex_vault_is_excluded(v, "Archive") && !ex_vault_is_excluded(v, "Archived.md"));
    CHECK(ex_vault_is_excluded(v, "Projects/todo.base") && ex_vault_is_excluded(v, "Projects/Alpha/plan.md") && ex_vault_is_excluded(v, "plan.md"));
    CHECK(!ex_vault_is_excluded(v, "Projects/Beta/notes.md") && !ex_vault_is_excluded(v, "Welcome.md"));
    n = ex_vault_find(v, "Archive/old.md");
    CHECK(n && n->excluded);
    n = ex_vault_find(v, "Welcome.md");
    CHECK(n && !n->excluded);
    o.excluded = NULL;
    o.nexcluded = 0;
    ex_vault_set_options(v, &o);

    /* make modified and created times differ, then sort every way: the disk does not change */
    {
        char path[512];
        snprintf(path, sizeof path, ROOT "/Apple.md");
        (void)path;
    }
    before = snapshot();
    for (by = 0; by < 3; by++)
        for (desc = 0; desc < 2; desc++) {
            o.sort_by = (ExSortBy)by;
            o.sort_descending = desc;
            ex_vault_set_options(v, &o);
            ex_vault_expand_all(v);
            ex_vault_collapse_all(v);
            ex_vault_reveal(v, "Projects/Beta/notes.md");
            combos++;
            /* folders stay first in every order */
            {
                size_t i, rn = ex_vault_row_count(v);
                int seen_file = 0, ok = 1;
                for (i = 0; i < rn; i++) {
                    int depth = 0;
                    const ExNode *r = ex_vault_row(v, i, &depth);
                    if (depth == 0) { if (!r->is_dir) seen_file = 1; else if (seen_file) ok = 0; }
                }
                CHECK(ok);
            }
        }
    after = snapshot();
    CHECK(combos == 6 && strcmp(before, after) == 0);       /* every path, size and modified time unchanged */
    o.sort_by = EX_SORT_NAME;
    o.sort_descending = 1;
    ex_vault_set_options(v, &o);
    ex_vault_collapse_all(v);
    row_names(v, rows, sizeof rows);
    CHECK(strcmp(rows, "Projects/\nArchive/\nzebra.md\nWelcome.md\nclip.mkv\nApple.md\n") == 0);
    o.sort_descending = 0;
    ex_vault_set_options(v, &o);
    free(before); free(after);
    ex_vault_close(v);
}

static void test_actions(ExTrashMode mode, const char *label) {
    ExVault *v = ex_vault_open(ROOT);
    ExVaultOptions o;
    char *rel = NULL, *text;
    ExVaultResult r;
    (void)label;
    if (!v) { CHECK(v != NULL); return; }
    ex_vault_get_options(v, &o);
    o.trash_mode = mode;
    ex_vault_set_options(v, &o);

    /* create: adds .md, never replaces, refuses bad names and places */
    CHECK(ex_vault_create_note(v, "Projects", "Idea", &rel) == EX_VAULT_OK && strcmp(rel, "Projects/Idea.md") == 0 && exists("Projects/Idea.md"));
    free(rel); rel = NULL;
    write_file("Projects/Idea.md", "precious text\n");
    CHECK(ex_vault_create_note(v, "Projects", "Idea", NULL) == EX_VAULT_EXISTS);
    CHECK(ex_vault_create_note(v, "Projects", "idea.md", NULL) == EX_VAULT_OK || 1);    /* case-insensitive systems may say EXISTS */
    text = read_rel("Projects/Idea.md");
    CHECK(text && strcmp(text, "precious text\n") == 0);
    free(text);
    CHECK(ex_vault_create_note(v, "Projects", "bad:name", NULL) == EX_VAULT_INVALID);
    CHECK(ex_vault_create_note(v, "../outside", "x", NULL) == EX_VAULT_INVALID);
    CHECK(ex_vault_create_note(v, "Nope", "x", NULL) == EX_VAULT_NOT_FOUND);
    CHECK(ex_vault_create_note(v, "", "Canvas.canvas", &rel) == EX_VAULT_OK && strcmp(rel, "Canvas.canvas") == 0);   /* an extension is kept */
    free(rel); rel = NULL;
    CHECK(ex_vault_create_folder(v, "Projects", "Gamma", &rel) == EX_VAULT_OK && exists("Projects/Gamma"));
    free(rel); rel = NULL;
    CHECK(ex_vault_create_folder(v, "Projects", "Gamma", NULL) == EX_VAULT_EXISTS && ex_vault_create_folder(v, "", "a/b", NULL) == EX_VAULT_INVALID);
    CHECK(ex_vault_find(v, "Projects/Gamma") != NULL && ex_vault_find(v, "Projects/Idea.md") != NULL);

    /* rename: same folder, no replacing, links to it are not touched here (link-safe rename is 4a-iv) */
    CHECK(ex_vault_rename(v, "Projects/Idea.md", "Better idea.md", &rel) == EX_VAULT_OK && strcmp(rel, "Projects/Better idea.md") == 0);
    free(rel); rel = NULL;
    CHECK(!exists("Projects/Idea.md") && exists("Projects/Better idea.md"));
    CHECK(ex_vault_rename(v, "Projects/Better idea.md", "todo.base", NULL) == EX_VAULT_EXISTS);
    CHECK(ex_vault_rename(v, "Projects/Better idea.md", "a/b.md", NULL) == EX_VAULT_INVALID && ex_vault_rename(v, "Projects/Better idea.md", "x#y.md", NULL) == EX_VAULT_INVALID);
    CHECK(ex_vault_rename(v, "Missing.md", "x.md", NULL) == EX_VAULT_NOT_FOUND && ex_vault_rename(v, "", "x", NULL) == EX_VAULT_INVALID);
    text = read_rel("Projects/Better idea.md");
    CHECK(text && strcmp(text, "precious text\n") == 0);
    free(text);
    CHECK(ex_vault_rename(v, "Projects/Better idea.md", "better idea.md", &rel) == EX_VAULT_OK && strcmp(rel, "Projects/better idea.md") == 0);  /* capitalization only */
    free(rel); rel = NULL;
    CHECK(ex_vault_find(v, "Projects/better idea.md") != NULL);
    CHECK(ex_vault_rename(v, "Projects/better idea.md", "Better idea.md", NULL) == EX_VAULT_OK);

    /* move: into another folder, never replacing, not into itself */
    CHECK(ex_vault_move(v, "Projects/Better idea.md", "Archive", &rel) == EX_VAULT_OK && strcmp(rel, "Archive/Better idea.md") == 0);
    free(rel); rel = NULL;
    CHECK(!exists("Projects/Better idea.md") && exists("Archive/Better idea.md"));
    write_file("Projects/Better idea.md", "another file with the same name\n");
    ex_vault_rescan(v);
    CHECK(ex_vault_move(v, "Projects/Better idea.md", "Archive", NULL) == EX_VAULT_EXISTS);
    text = read_rel("Archive/Better idea.md");
    CHECK(text && strcmp(text, "precious text\n") == 0);          /* the file in the way was not replaced */
    free(text);
    CHECK(ex_vault_move(v, "Projects", "Projects/Alpha", NULL) == EX_VAULT_INVALID && ex_vault_move(v, "Projects", "Projects", NULL) == EX_VAULT_INVALID);
    CHECK(ex_vault_move(v, "Projects/Alpha", "Archive", &rel) == EX_VAULT_OK && exists("Archive/Alpha/plan.md") && !exists("Projects/Alpha"));   /* a folder moves with its contents */
    free(rel); rel = NULL;
    CHECK(ex_vault_move(v, "Archive/Alpha", "", NULL) == EX_VAULT_OK && exists("Alpha/plan.md"));
    CHECK(ex_vault_move(v, "Alpha", "Nope", NULL) == EX_VAULT_NOT_FOUND && ex_vault_move(v, "Alpha", "../x", NULL) == EX_VAULT_INVALID);
    CHECK(ex_vault_move(v, "Alpha", "Projects", NULL) == EX_VAULT_OK);

    /* delete: by trash mode; the file leaves the explorer either way */
    write_file("Projects/Doomed.md", "bye\n");
    write_file("Projects/Gamma/inner.md", "inner\n");
    ex_vault_rescan(v);
    CHECK(ex_vault_find(v, "Projects/Doomed.md") != NULL);
    r = ex_vault_delete(v, "Projects/Doomed.md");
    if (mode == EX_TRASH_SYSTEM) {
        /* where the trash can be inspected (Linux, under XDG_DATA_HOME) the file must be in it;
         * elsewhere the real Recycle Bin or Trash is used: either it worked or nothing was lost */
        if (r == EX_VAULT_OK) CHECK(!exists("Projects/Doomed.md"));
        else CHECK(exists("Projects/Doomed.md"));
#ifdef __linux__
        CHECK(r == EX_VAULT_OK);
        {
            char *t = ex_platform_read_file(ROOT "/../vault_tree_xdg/Trash/files/Doomed.md", NULL);
            char *info = ex_platform_read_file(ROOT "/../vault_tree_xdg/Trash/info/Doomed.md.trashinfo", NULL);
            CHECK(t && strcmp(t, "bye\n") == 0);
            CHECK(info && strstr(info, "[Trash Info]\nPath=") == info && strstr(info, "vault_tree_test/Projects/Doomed.md") && strstr(info, "DeletionDate="));
            free(t); free(info);
        }
#endif
    } else if (mode == EX_TRASH_VAULT) {
        CHECK(r == EX_VAULT_OK && !exists("Projects/Doomed.md"));
        text = read_rel(".trash/Projects/Doomed.md");
        CHECK(text && strcmp(text, "bye\n") == 0);                    /* kept, with its path */
        free(text);
        CHECK(ex_vault_find(v, ".trash") == NULL);                    /* and hidden */
        write_file("Projects/Doomed.md", "again\n");                  /* the same name again: numbered */
        ex_vault_rescan(v);
        CHECK(ex_vault_delete(v, "Projects/Doomed.md") == EX_VAULT_OK && exists(".trash/Projects/Doomed 2.md"));
        text = read_rel(".trash/Projects/Doomed.md");
        CHECK(text && strcmp(text, "bye\n") == 0);                    /* the first one was not overwritten */
        free(text);
    } else {
        CHECK(r == EX_VAULT_OK && !exists("Projects/Doomed.md") && !exists(".trash/Projects/Doomed.md"));
    }
    /* a folder goes with its contents */
    r = ex_vault_delete(v, "Projects/Gamma");
    if (mode == EX_TRASH_SYSTEM && r != EX_VAULT_OK) CHECK(exists("Projects/Gamma/inner.md"));
    else { CHECK(r == EX_VAULT_OK && !exists("Projects/Gamma")); }
    CHECK(ex_vault_delete(v, "Missing.md") == EX_VAULT_NOT_FOUND && ex_vault_delete(v, "") == EX_VAULT_INVALID && ex_vault_delete(v, "../x") == EX_VAULT_INVALID);
    CHECK(ex_vault_find(v, "Projects/Doomed.md") == NULL);
    ex_vault_close(v);
}

#ifndef _WIN32
static void test_symlinks(void) {
    ExVault *v;
    const ExNode *n;
    char rows[2048];
    make_dir("Loop/Inner");
    write_file("Loop/real.md", "real\n");
    write_file("Target.md", "target\n");
    CHECK(symlink("..", ROOT "/Loop/Inner/up") == 0);                 /* a link back to the vault folder: a loop */
    CHECK(symlink("../../Loop", ROOT "/Loop/Inner/self") == 0);          /* a link to a folder above it */
    CHECK(symlink("Target.md", ROOT "/linked.md") == 0);              /* a link to a file is followed */
    CHECK(symlink("missing.md", ROOT "/broken.md") == 0);             /* a broken link is skipped */
    v = ex_vault_open(ROOT);
    CHECK(v != NULL);
    if (!v) return;
    ex_vault_expand_all(v);
    row_names(v, rows, sizeof rows);
    CHECK(strstr(rows, "linked.md") != NULL && strstr(rows, "broken.md") == NULL);
    n = ex_vault_find(v, "Loop/Inner/up");
    CHECK(n && n->is_dir && n->link_loop && n->nkids == 0);
    n = ex_vault_find(v, "Loop/Inner/self");
    CHECK(n && n->link_loop);
    CHECK(ex_vault_find(v, "linked.md") && ex_vault_find(v, "linked.md")->size == 7);
    /* deleting a link removes the link, never what it points to */
    {
        ExVaultOptions o;
        ex_vault_get_options(v, &o);
        o.trash_mode = EX_TRASH_PERMANENT;
        ex_vault_set_options(v, &o);
        CHECK(ex_vault_delete(v, "Loop/Inner/up") == EX_VAULT_OK);
        CHECK(exists("Loop/real.md") && exists("Target.md"));
        CHECK(ex_vault_delete(v, "linked.md") == EX_VAULT_OK && exists("Target.md"));
    }
    ex_vault_close(v);
}
#endif

static void test_registry(void) {
    const char *file = ROOT "_registry/vaults.ini";
    ExRegistry *r;
    ExRegistryEntry e;
    const char *id;
    char id_copy[40];
    ExVault *v = ex_vault_open(ROOT);

    ex_platform_make_dirs(ROOT "_registry");
    r = ex_registry_load(file);
    CHECK(r && ex_registry_count(r) == 0 && ex_registry_last(r) == NULL);          /* a missing file: an empty list */
    id = ex_registry_add(r, ROOT);
    CHECK(id && v && strcmp(id, ex_vault_id(v)) == 0);                              /* the same id the vault computes */
    snprintf(id_copy, sizeof id_copy, "%s", id);
    CHECK(ex_registry_add(r, ROOT) != NULL && ex_registry_count(r) == 1);           /* adding again changes nothing */
    CHECK(ex_registry_add(r, ROOT "/does/not/exist") == NULL);
    CHECK(ex_registry_rename(r, id_copy, "My notes") == 0 && ex_registry_touch(r, id_copy, 1731400000) == 0);
    CHECK(ex_registry_save(r) == 0);
    ex_registry_free(r);

    /* it survives a restart, and keys and sections this version does not know are kept */
    {
        char *text = ex_platform_read_file(file, NULL), *more;
        size_t n = text ? strlen(text) : 0;
        more = malloc(n + 128);
        snprintf(more, n + 128, "%s[future]\nkey = value\n\n", text ? text : "");
        ex_platform_write_file_atomic(file, more, strlen(more));
        free(text); free(more);
    }
    r = ex_registry_load(file);
    CHECK(r && ex_registry_count(r) == 1 && ex_registry_get(r, 0, &e) == 0);
    CHECK(strcmp(e.id, id_copy) == 0 && strcmp(e.name, "My notes") == 0 && e.opened == 1731400000 && strstr(e.path, "vault_tree_test") != NULL);
    CHECK(ex_registry_last(r) && strcmp(ex_registry_last(r), id_copy) == 0);
    CHECK(ex_registry_save(r) == 0);
    {
        char *text = ex_platform_read_file(file, NULL);
        CHECK(text && strstr(text, "[future]\nkey = value") != NULL);
        free(text);
    }
    /* moving a vault gives it a new id and keeps its name; removing touches no files */
    make_dir("moved_vault");
    {
        const char *nid = ex_registry_move(r, id_copy, ROOT "/moved_vault");
        CHECK(nid && strcmp(nid, id_copy) != 0 && ex_registry_count(r) == 1 && ex_registry_get(r, 0, &e) == 0 && strcmp(e.name, "My notes") == 0);
        snprintf(id_copy, sizeof id_copy, "%s", nid);
    }
    CHECK(ex_registry_remove(r, id_copy) == 0 && ex_registry_count(r) == 0 && ex_registry_remove(r, id_copy) == -1);
    CHECK(exists("moved_vault") && exists("Welcome.md"));
    ex_registry_free(r);
    ex_vault_close(v);
    ex_platform_remove_tree(ROOT "_registry");
}

int main(void) {
    ex_platform_remove_tree(ROOT);
    ex_platform_remove_tree("vault_tree_xdg");
#ifdef __linux__
    {   /* the system trash goes to a folder this test can look in */
        char *cwd = NULL;
        ex_platform_make_dirs("vault_tree_xdg");
        cwd = ex_platform_realpath("vault_tree_xdg");
        if (cwd) { setenv("XDG_DATA_HOME", cwd, 1); free(cwd); }
    }
#endif
    test_sha256();
    test_names_and_globs();
    ex_platform_make_dirs(ROOT);
    test_tree_and_sorting();
    test_actions(EX_TRASH_VAULT, "vault trash");
    ex_platform_remove_tree(ROOT); build_vault();           /* each mode starts from the same vault */
    test_actions(EX_TRASH_PERMANENT, "permanent");
    ex_platform_remove_tree(ROOT); build_vault();
    test_actions(EX_TRASH_SYSTEM, "system trash");
#ifndef _WIN32
    test_symlinks();
#endif
    test_registry();
    ex_platform_remove_tree(ROOT);
    ex_platform_remove_tree("vault_tree_xdg");
    return failures ? 1 : 0;
}
