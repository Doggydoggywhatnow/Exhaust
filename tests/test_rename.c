/* rename (sub-slice 4a-iv): link-safe rename and move, one case for each kind of link the README lists, plus moving notes and
 * folders, duplicates, aliases, files with CRLF and a byte-order mark, and the dry run. Each case builds a small vault, indexes
 * it, moves the files, rewrites the links with ex_rename_links and compares the notes byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "exhaust_rename.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* each case has folders of its own so the cases can run side by side (ctest -j) */
static char g_v[96], g_db[96], g_db_index[128];
#define V g_v

typedef struct File { const char *path, *content; } File;
typedef struct Move { const char *from, *to; } Move;

static void write_file(const char *rel, const char *content) {
    char path[512], *dir, *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    dir = malloc(strlen(path) + 1); strcpy(dir, path);
    slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    free(dir);
    f = fopen(path, "wb");
    if (f) { fwrite(content, 1, strlen(content), f); fclose(f); }
}

static char *read_file(const char *rel) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", V, rel);
    return ex_platform_read_file(path, NULL);
}

/* Runs one case. expect lists the notes (by their path after the move) and their exact text afterwards. */
static const char *g_filter;

static size_t run_case(const char *name, const File *files, size_t nfiles, const Move *moves, size_t nmoves, const File *expect, size_t nexpect, int dry) {
    ExIndex *w;
    char err[200], *root;
    ExNoteIO io;
    ExMove mv[8];
    ExRenameReport rep;
    size_t i, before_failures = (size_t)failures, links = 0;
    if (g_filter && strcmp(g_filter, name)) return 3;       /* not the case asked for (the dry run case only checks this value when it runs) */
    ex_platform_remove_tree(V);
    ex_platform_remove_tree(g_db);
    for (i = 0; i < nfiles; i++) write_file(files[i].path, files[i].content);
    root = ex_platform_realpath(V);
    w = ex_index_open(g_db_index, 1, err, sizeof err);
    if (!w || ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) != 0) { fprintf(stderr, "%s: cannot index\n", name); failures++; free(root); return 0; }
    io = ex_note_io_files(root);
    for (i = 0; i < nmoves; i++) {
        char from[512], to[512], *dir, *slash;
        snprintf(from, sizeof from, "%s/%s", root, moves[i].from);
        snprintf(to, sizeof to, "%s/%s", root, moves[i].to);
        dir = malloc(strlen(to) + 1); strcpy(dir, to);
        slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
        free(dir);
        CHECK(ex_platform_rename_noreplace(from, to) == 0);
        mv[i].from = moves[i].from; mv[i].to = moves[i].to;
    }
    if (dry) {
        /* the dry run happens after the move too (the index still has the old state); it must not change a byte */
        CHECK(ex_rename_plan(w, &io, mv, nmoves, &rep) == 0);
    } else CHECK(ex_rename_links(w, &io, mv, nmoves, &rep) == 0);
    links = rep.total_links;
    ex_rename_report_free(&rep);
    for (i = 0; i < nexpect; i++) {
        char *got = read_file(expect[i].path);
        if (!got || strcmp(got, expect[i].content) != 0) {
            fprintf(stderr, "case %s: %s differs\n--- expected\n%s\n--- got\n%s\n", name, expect[i].path, expect[i].content, got ? got : "(missing)");
            failures++;
        }
        free(got);
    }
    ex_note_io_free(&io);
    ex_index_close(w);
    free(root);
    (void)before_failures;
    return links;
}

int main(int argc, char **argv) {
    if (argc > 1) g_filter = argv[1];
    snprintf(g_v, sizeof g_v, "rename_test_vault_%s", g_filter ? g_filter : "all");
    snprintf(g_db, sizeof g_db, "rename_test_db_%s", g_filter ? g_filter : "all");
    snprintf(g_db_index, sizeof g_db_index, "%s/index.db", g_db);
    /* wikilinks: plain, with an alias, any case; other links stay */
    {
        File f[] = {{"Note.md", "# Note\n"}, {"A.md", "See [[Note]], [[Note|shown]], [[note]] and [[Other]].\n"}, {"Other.md", "x\n"}};
        Move m[] = {{"Note.md", "Renamed.md"}};
        File e[] = {{"A.md", "See [[Renamed]], [[Renamed|shown]], [[Renamed]] and [[Other]].\n"}, {"Other.md", "x\n"}};
        run_case("wikilink", f, 3, m, 1, e, 2, 0);
    }
    /* embeds: a note and a file with its extension */
    {
        File f[] = {{"Pic.png", "PNG"}, {"Note.md", "N\n"}, {"A.md", "![[Pic.png]] ![[Pic.png|100]] ![[Note]] ![[Note#Head]]\n"}};
        Move m[] = {{"Pic.png", "Photo.png"}, {"Note.md", "Page.md"}};
        File e[] = {{"A.md", "![[Photo.png]] ![[Photo.png|100]] ![[Page]] ![[Page#Head]]\n"}};
        run_case("embed", f, 3, m, 2, e, 1, 0);
    }
    /* Markdown links, relative to their note and from the vault; the moved note into a deeper folder */
    {
        File f[] = {{"Note.md", "N\n"}, {"sub/A.md", "[up](../Note.md) and [root](Note.md) and [web](https://example.com/Note.md)\n"}, {"B.md", "[here](Note.md) [wiki]([[x]]) and [[Note]]\n"}};
        Move m[] = {{"Note.md", "other/Deep/Note.md"}};
        File e[] = {{"sub/A.md", "[up](../other/Deep/Note.md) and [root](other/Deep/Note.md) and [web](https://example.com/Note.md)\n"},
                    {"B.md", "[here](other/Deep/Note.md) [wiki]([[x]]) and [[Note]]\n"}};
        run_case("markdown_relative", f, 3, m, 1, e, 2, 0);
    }
    /* URL-encoded links */
    {
        File f[] = {{"My Note.md", "N\n"}, {"A.md", "[x](My%20Note.md) [y](<My Note.md>) [z](My%20Note.md#Part \"Title\")\n"}};
        Move m[] = {{"My Note.md", "Your Note (2).md"}};
        File e[] = {{"A.md", "[x](Your%20Note%20%282%29.md) [y](<Your Note (2).md>) [z](Your%20Note%20%282%29.md#Part \"Title\")\n"}};
        run_case("markdown_url_encoded", f, 2, m, 1, e, 1, 0);
    }
    /* links inside properties */
    {
        File f[] = {{"Note.md", "N\n"}, {"A.md", "---\nrelated: \"[[Note]]\"\nitems:\n  - \"[[Note#Head|h]]\"\n  - \"[[Other]]\"\ntitle: Note\n---\nBody [[Note]]\n"}, {"Other.md", "o\n"}};
        Move m[] = {{"Note.md", "Renamed.md"}};
        File e[] = {{"A.md", "---\nrelated: \"[[Renamed]]\"\nitems:\n  - \"[[Renamed#Head|h]]\"\n  - \"[[Other]]\"\ntitle: Note\n---\nBody [[Renamed]]\n"}};
        run_case("property_links", f, 3, m, 1, e, 1, 0);
    }
    /* #heading and #^block references keep their part; a link to a heading of the same note stays */
    {
        File f[] = {{"Note.md", "# Part\ntext ^blk1\nSee [[#Part]] and [[Note#Part]].\n"}, {"A.md", "[[Note#Part]] [[Note#^blk1]] [[Note#Part|shown]] [md](Note.md#Part) [blk](Note.md#^blk1)\n"}};
        Move m[] = {{"Note.md", "Renamed.md"}};
        File e[] = {{"A.md", "[[Renamed#Part]] [[Renamed#^blk1]] [[Renamed#Part|shown]] [md](Renamed.md#Part) [blk](Renamed.md#^blk1)\n"},
                    {"Renamed.md", "# Part\ntext ^blk1\nSee [[#Part]] and [[Renamed#Part]].\n"}};
        run_case("heading_and_block", f, 2, m, 1, e, 2, 0);
    }
    /* a moved note: its own relative links are rewritten for the new folder; names stay */
    {
        File f[] = {{"a/Note.md", "[r](../Other.md) [s](sibling.md) [[Other]] [[sibling]] [web](http://x.org)\n"}, {"Other.md", "o\n"}, {"a/sibling.md", "s\n"}, {"B.md", "[[Note]]\n"}};
        Move m[] = {{"a/Note.md", "b/c/Note.md"}};
        File e[] = {{"b/c/Note.md", "[r](../../Other.md) [s](../../a/sibling.md) [[Other]] [[sibling]] [web](http://x.org)\n"}, {"B.md", "[[Note]]\n"}};
        run_case("moved_note", f, 4, m, 1, e, 2, 0);
    }
    /* a name that another file already has: the link needs the path; links through aliases and paths */
    {
        File f[] = {{"x/Dup.md", "d\n"}, {"y/Same.md", "---\naliases: [Bee]\n---\ns\n"}, {"A.md", "[[Same]] [[y/Same]] [[Bee]] [[x/Dup]] [[Dup]]\n"}};
        Move m[] = {{"y/Same.md", "y/Dup.md"}};
        File e[] = {{"A.md", "[[y/Dup]] [[y/Dup]] [[Bee]] [[x/Dup]] [[Dup]]\n"}};
        run_case("duplicates", f, 3, m, 1, e, 1, 0);
    }
    /* a folder moves: path links follow, name links stay, and links between the moved notes are rewritten once */
    {
        File f[] = {{"docs/Target.md", "[[Other]] [t](Two.md)\n"}, {"docs/Two.md", "[[Target]] [t](Target.md)\n"}, {"Other.md", "o\n"}, {"A.md", "[[docs/Target]] [[Target]] [m](docs/Two.md)\n"}};
        Move m[] = {{"docs/Target.md", "archive/docs/Target.md"}, {"docs/Two.md", "archive/docs/Two.md"}};
        File e[] = {{"A.md", "[[archive/docs/Target]] [[Target]] [m](archive/docs/Two.md)\n"}, {"archive/docs/Target.md", "[[Other]] [t](Two.md)\n"}, {"archive/docs/Two.md", "[[Target]] [t](Target.md)\n"}};
        run_case("folder_move", f, 4, m, 2, e, 3, 0);
    }
    /* a file with a byte-order mark and CRLF: both stay, and nothing else changes */
    {
        File f[] = {{"Note.md", "N\n"}, {"A.md", "\xEF\xBB\xBF# A\r\nSee [[Note]].\r\nMore [[Note|n]].\r\n"}};
        Move m[] = {{"Note.md", "New.md"}};
        File e[] = {{"A.md", "\xEF\xBB\xBF# A\r\nSee [[New]].\r\nMore [[New|n]].\r\n"}};
        run_case("crlf_and_bom", f, 2, m, 1, e, 1, 0);
    }
    /* the dry run counts and writes nothing */
    {
        File f[] = {{"Note.md", "N\n"}, {"A.md", "[[Note]] [[Note]]\n"}, {"B.md", "[[Note]]\n"}, {"C.md", "[[Other]]\n"}};
        Move m[] = {{"Note.md", "Renamed.md"}};
        File e[] = {{"A.md", "[[Note]] [[Note]]\n"}, {"B.md", "[[Note]]\n"}, {"C.md", "[[Other]]\n"}};
        size_t links = run_case("dry_run", f, 4, m, 1, e, 3, 1);
        CHECK(links == 3);
    }
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
