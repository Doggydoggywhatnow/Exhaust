/* format converter (sub-slice 6e-iv): alias, tag and cssclass become aliases, tags and cssclasses in every note that has them. Only the characters of the key
 * change: every other byte stays (also with CRLF and a byte-order mark), a note that already has the new key is left alone, notes without the old keys are
 * not rewritten at all (their modification time is kept), and values that look like keys inside the body or a value are not touched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_props.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define V "fmtconv_test_vault"

static void write_str(const char *rel, const char *s) {
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    f = fopen(path, "wb");
    if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
}
static char *read_all(const char *rel) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", V, rel);
    return ex_platform_read_file(path, NULL);
}
static void expect(const char *rel, const char *want) {
    char *t = read_all(rel);
    if (!t || strcmp(t, want)) fprintf(stderr, "%s differs:\n--- got\n%s\n--- want\n%s\n", rel, t ? t : "(null)", want);
    CHECK(t && !strcmp(t, want));
    free(t);
}

int main(void) {
    ExIndex *w;
    char err[200], *root;
    ExNoteIO io;
    ExMigrateReport rep;
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("fmtconv_test_db");
    ex_platform_make_dirs(V);
    ex_platform_make_dirs(V "/.exhaust");
    write_str("One.md", "---\ntitle: One\nalias: Uno\ntag: x\ncssclass: wide\nstatus: tag\n---\nBody with alias: and tag: text.\n");
    write_str("Two.md", "---\nalias:\n  - Dos\n  - Deux\ntags:\n  - a\n---\n# Two\n");
    write_str("Both.md", "---\ntag: old\ntags: new\n---\nboth\n");
    write_str("Crlf.md", "\xEF\xBB\xBF---\r\ntag: win\r\ntitle: Crlf\r\n---\r\nbody\r\n");
    write_str("Quoted.md", "---\n\"alias\": Q\n---\nq\n");
    write_str("Clean.md", "---\ntitle: Clean\naliases: [C]\ntags: [c]\n---\nclean\n");
    write_str("Plain.md", "No properties, alias: nothing.\n");
    write_str("Similar.md", "---\ntagline: keep\naliasing: keep\ncssclasses: [k]\n---\nsimilar\n");
    root = ex_platform_realpath(V);
    w = ex_index_open("fmtconv_test_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) return 1;
    io = ex_note_io_files(root);
    CHECK(ex_props_migrate_frontmatter(w, &io, root, &rep) == 0);
    CHECK(rep.renamed[0] == 3 && rep.renamed[1] == 2 && rep.renamed[2] == 1);          /* alias: One, Two, Quoted; tag: One, Crlf; cssclass: One */
    CHECK(rep.skipped[0] == 0 && rep.skipped[1] == 1 && rep.skipped[2] == 0);           /* Both.md has tag and tags */
    expect("One.md", "---\ntitle: One\naliases: Uno\ntags: x\ncssclasses: wide\nstatus: tag\n---\nBody with alias: and tag: text.\n");
    expect("Two.md", "---\naliases:\n  - Dos\n  - Deux\ntags:\n  - a\n---\n# Two\n");
    expect("Both.md", "---\ntag: old\ntags: new\n---\nboth\n");
    expect("Crlf.md", "\xEF\xBB\xBF---\r\ntags: win\r\ntitle: Crlf\r\n---\r\nbody\r\n");
    expect("Quoted.md", "---\n\"aliases\": Q\n---\nq\n");
    expect("Clean.md", "---\ntitle: Clean\naliases: [C]\ntags: [c]\n---\nclean\n");
    expect("Plain.md", "No properties, alias: nothing.\n");
    expect("Similar.md", "---\ntagline: keep\naliasing: keep\ncssclasses: [k]\n---\nsimilar\n");
    /* running it again changes nothing (the index is read again first, as the app does after the watcher) */
    ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL);
    CHECK(ex_props_migrate_frontmatter(w, &io, root, &rep) == 0);
    CHECK(rep.renamed[0] == 0 && rep.renamed[1] == 0 && rep.renamed[2] == 0 && rep.skipped[1] == 1);
    ex_note_io_free(&io);
    ex_index_close(w);
    free(root);
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("fmtconv_test_db");
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("format_converter ok\n");
    return 0;
}
