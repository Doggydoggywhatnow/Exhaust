/* properties (sub-slice 4a-iii): properties and tags read from notes (types, lists, links, nested tags, cssclasses, aliases);
 * edits change only the edited lines and leave every other byte as it was (also in a file with CRLF and a byte-order mark);
 * rename and change type work across a sample vault; the vault's types are kept in .exhaust/types.ini. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_notes.h"
#include "exhaust_platform.h"
#include "exhaust_props.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "props_test_vault"

static void write_file(const char *rel, const char *bytes, size_t n) {
    char path[512], *dir, *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    dir = malloc(strlen(path) + 1); strcpy(dir, path);
    slash = strrchr(dir, '/'); if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    free(dir);
    f = fopen(path, "wb");
    if (f) { fwrite(bytes, 1, n, f); fclose(f); }
}
static void write_str(const char *rel, const char *s) { write_file(rel, s, strlen(s)); }
static char *read_all(const char *rel) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", V, rel);
    return ex_platform_read_file(path, NULL);
}

static const char *NOTE_A =
    "---\ntitle: Alpha\nstatus: active\nrating: 4\ndone: true\ncreated: 2024-05-17\nwhen: 2024-05-17T09:30\nlink: \"[[Beta]]\"\n"
    "tags:\n  - project\n  - area/work\naliases: [First, \"Letter A\"]\ncssclasses: [wide, cards]\nrelated:\n  - \"[[Beta]]\"\n  - \"[[Gamma|g]]\"\n---\n"
    "# Alpha\n\nBody with #inline and #area/work/sub tags, and not #123.\n";
static const char *NOTE_B = "---\nstatus: done\nrating: high\ntags: project, Area/Work\n---\nBeta text #Project\n";
static const char *NOTE_C = "No frontmatter, just text with #lonely tag.\n";
static const char *NOTE_D = "---\nstatus: [unclosed\n---\ntext\n";
static const char *NOTE_E = "---\nstatus: waiting\nother: 1\n---\n";

static int strcasecmp_like(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b; if (x != y) return 1; }
    return *a != *b;
}

static const ExNoteProp *prop_named(const ExNoteProp *p, size_t n, const char *name) {
    size_t i;
    for (i = 0; i < n; i++) if (!strcmp(p[i].name, name)) return &p[i];
    return NULL;
}

static void test_read(ExIndex *w) {
    ExNoteProp *p;
    size_t n, i;
    ExTagInfo *tags;
    char **paths, **cls;
    CHECK(ex_note_props(w, "Alpha.md", &p, &n) == 0);
    CHECK(n == 11);
    CHECK(prop_named(p, n, "status") && prop_named(p, n, "status")->type == EX_PROP_TEXT && !strcmp(prop_named(p, n, "status")->value, "active"));
    CHECK(prop_named(p, n, "rating") && prop_named(p, n, "rating")->type == EX_PROP_NUMBER);
    CHECK(prop_named(p, n, "done") && prop_named(p, n, "done")->type == EX_PROP_CHECKBOX);
    CHECK(prop_named(p, n, "created") && prop_named(p, n, "created")->type == EX_PROP_DATE);
    CHECK(prop_named(p, n, "when") && prop_named(p, n, "when")->type == EX_PROP_DATETIME);
    CHECK(prop_named(p, n, "link") && prop_named(p, n, "link")->type == EX_PROP_LINKS);
    CHECK(prop_named(p, n, "tags") && prop_named(p, n, "tags")->type == EX_PROP_LIST && !strcmp(prop_named(p, n, "tags")->value, "project\narea/work"));
    CHECK(prop_named(p, n, "aliases") && prop_named(p, n, "aliases")->type == EX_PROP_LIST && !strcmp(prop_named(p, n, "aliases")->value, "First\nLetter A"));
    CHECK(prop_named(p, n, "related") && prop_named(p, n, "related")->type == EX_PROP_LIST);
    for (i = 1; i < n; i++) CHECK(p[i].start >= p[i - 1].end);
    ex_note_props_free(p, n);
    CHECK(ex_note_props(w, "Gone.md", &p, &n) == 0 && n == 0);
    CHECK(ex_note_props(w, "Bad.md", &p, &n) == 0 && n == 0);          /* not valid YAML: no properties */
    CHECK(ex_note_cssclasses(w, "Alpha.md", &cls, &n) == 0 && n == 2 && !strcmp(cls[0], "wide") && !strcmp(cls[1], "cards"));
    for (i = 0; i < n; i++) free(cls[i]);
    free(cls);
    /* tags: the property and the text; nested; without regard to case */
    CHECK(ex_tags_all(w, &tags, &n) == 0);
    {
        int saw_project = 0, saw_sub = 0, saw_123 = 0;
        for (i = 0; i < n; i++) {
            if (!strcasecmp_like(tags[i].tag, "project")) { saw_project = 1; CHECK(tags[i].notes == 2); }       /* Alpha (property), Beta (property and text) */
            if (!strcmp(tags[i].tag, "area/work/sub")) saw_sub = 1;
            if (!strcmp(tags[i].tag, "123")) saw_123 = 1;
        }
        CHECK(saw_project && saw_sub && !saw_123);
    }
    ex_tags_free(tags, n);
    CHECK(ex_tag_notes(w, "project", 0, &paths, &n) == 0 && n == 2 && !strcmp(paths[0], "Alpha.md") && !strcmp(paths[1], "Beta.md"));
    ex_paths_free(paths, n);
    CHECK(ex_tag_notes(w, "#area/work", 0, &paths, &n) == 0 && n == 2);       /* Alpha, Beta (Area/Work: case is ignored) */
    ex_paths_free(paths, n);
    CHECK(ex_tag_notes(w, "area", 1, &paths, &n) == 0 && n == 2);              /* nested: also area/work/sub */
    ex_paths_free(paths, n);
    CHECK(ex_tag_notes(w, "area", 0, &paths, &n) == 0 && n == 0);
    ex_paths_free(paths, n);
    CHECK(ex_tag_notes(w, "lonely", 0, &paths, &n) == 0 && n == 1 && !strcmp(paths[0], "C.md"));
    ex_paths_free(paths, n);
    {
        ExPropInfo *info;
        CHECK(ex_props_all(w, V, &info, &n) == 0);
        for (i = 0; i < n; i++) {
            if (!strcmp(info[i].name, "status")) CHECK(info[i].notes == 2 && info[i].type == EX_PROP_TEXT);
            if (!strcmp(info[i].name, "rating")) CHECK(info[i].notes == 2);
        }
        ex_props_all_free(info, n);
    }
}

/* Everything outside [from, from + removed) of the old text is the same in the new one, in the same order. */
static int only_changed(const char *a, size_t al, const char *b, size_t bl, size_t at, size_t removed, size_t inserted) {
    return bl == al - removed + inserted && !memcmp(a, b, at) && !memcmp(a + at + removed, b + at + inserted, al - at - removed);
}

static void test_edits(void) {
    size_t len = strlen(NOTE_A), n = 0, i;
    char *out;
    const char *s = NOTE_A;
    size_t status_at = (size_t)(strstr(s, "status: active\n") - s);
    size_t rating_at = (size_t)(strstr(s, "rating: 4\n") - s);
    /* set an existing property: only its line changes */
    out = ex_prop_text_set(s, len, "status", EX_PROP_TEXT, "waiting for review", &n);
    CHECK(out && only_changed(s, len, out, n, status_at, strlen("status: active\n"), strlen("status: waiting for review\n")) && strstr(out, "status: waiting for review\n"));
    free(out);
    out = ex_prop_text_set(s, len, "status", EX_PROP_TEXT, "done: yes", &n);          /* a value that needs quotes */
    CHECK(out && strstr(out, "status: \"done: yes\"\n"));
    free(out);
    out = ex_prop_text_set(s, len, "rating", EX_PROP_NUMBER, "5", &n);
    CHECK(out && only_changed(s, len, out, n, rating_at, strlen("rating: 4\n"), strlen("rating: 5\n")));
    free(out);
    /* a list is written as a block list and replaces all lines of the old one */
    out = ex_prop_text_set(s, len, "tags", EX_PROP_LIST, "one\ntwo words\nthree", &n);
    CHECK(out && strstr(out, "tags:\n  - one\n  - two words\n  - three\naliases:") && strstr(out, "# Alpha\n\nBody with"));
    free(out);
    /* a new property goes before the closing dashes; a note without frontmatter gets one */
    out = ex_prop_text_set(s, len, "author", EX_PROP_TEXT, "Ann", &n);
    {
        const char *close = strstr(s, "---\n# Alpha");
        CHECK(out && only_changed(s, len, out, n, (size_t)(close - s), 0, strlen("author: Ann\n")));
    }
    free(out);
    out = ex_prop_text_set(NOTE_C, strlen(NOTE_C), "author", EX_PROP_TEXT, "Ann", &n);
    CHECK(out && !strncmp(out, "---\nauthor: Ann\n---\nNo frontmatter", 34) && !strcmp(out + strlen("---\nauthor: Ann\n---\n"), NOTE_C));
    free(out);
    CHECK(ex_prop_text_set(NOTE_D, strlen(NOTE_D), "x", EX_PROP_TEXT, "y", &n) == NULL);   /* not valid YAML: never touched */
    CHECK(ex_prop_text_set(s, len, "bad:key", EX_PROP_TEXT, "y", &n) == NULL);
    /* remove */
    out = ex_prop_text_remove(s, len, "done", &n);
    CHECK(out && only_changed(s, len, out, n, (size_t)(strstr(s, "done: true\n") - s), strlen("done: true\n"), 0));
    free(out);
    CHECK(ex_prop_text_remove(s, len, "nothing", &n) == NULL);
    /* rename: only the key's characters */
    out = ex_prop_text_rename(s, len, "status", "state", &n);
    CHECK(out && only_changed(s, len, out, n, status_at, strlen("status"), strlen("state")) && strstr(out, "state: active"));
    free(out);
    CHECK(ex_prop_text_rename(s, len, "status", "rating", &n) == NULL);                      /* the name is taken */
    CHECK(ex_prop_text_rename(s, len, "nothing", "x", &n) == NULL);
    /* change type */
    out = ex_prop_text_retype(s, len, "rating", EX_PROP_TEXT, &n);
    CHECK(out && strstr(out, "rating: \"4\"\n"));                                          /* a text that looks like a number is quoted */
    free(out);
    out = ex_prop_text_retype(s, len, "status", EX_PROP_LIST, &n);
    CHECK(out && strstr(out, "status:\n  - active\n"));
    free(out);
    out = ex_prop_text_retype(s, len, "status", EX_PROP_NUMBER, &n);
    CHECK(out == NULL);                                                                     /* "active" is not a number */
    out = ex_prop_text_retype(s, len, "tags", EX_PROP_TEXT, &n);
    CHECK(out && strstr(out, "tags: \"project, area/work\"\n") == NULL && strstr(out, "tags: project, area/work\n"));
    free(out);
    /* every other byte: edit and edit back gives the original */
    out = ex_prop_text_rename(s, len, "status", "state", &n);
    if (out) { size_t n2; char *back = ex_prop_text_rename(out, n, "state", "status", &n2); CHECK(back && n2 == len && !memcmp(back, s, len)); free(back); free(out); }
    (void)i;
}

static void test_vault(void) {
    ExIndex *w;
    char err[200], *text;
    ExNoteIO io;
    size_t changed = 0;
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("props_test_db");
    write_str("Alpha.md", NOTE_A);
    write_str("Beta.md", NOTE_B);
    write_str("C.md", NOTE_C);
    write_str("Bad.md", NOTE_D);
    write_str("E.md", NOTE_E);
    /* a note with a byte-order mark and CRLF line endings: both come back; and a note that mixes line endings */
    write_str("Crlf.md", "\xEF\xBB\xBF---\r\nstatus: crlf\r\nrating: 3\r\n---\r\nBody\r\nmore\r\n");
    write_str("Mixed.md", "---\nstatus: mixed\r\nx: 1\n---\nbody\r\n");
    ex_platform_make_dirs(V "/.exhaust");
    w = ex_index_open("props_test_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, V, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) return;
    {   /* the sample is read from the real path */
        char *root = ex_platform_realpath(V);
        ex_index_close(w);
        w = ex_index_open("props_test_db/index.db", 1, err, sizeof err);
        ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL);
        free(root);
    }
    CHECK(ex_prop_type_get(V, "rating") == -1);
    io = ex_note_io_files(V);
    CHECK(ex_props_rename_everywhere(w, &io, V, "status", "state", &changed) == 0);
    CHECK(changed == 5);                                                                   /* Alpha, Beta, E, Crlf, Mixed; Bad.md has no readable properties */
    text = read_all("Alpha.md");
    CHECK(text && strstr(text, "state: active\n") && !strstr(text, "status:") && strstr(text, "Body with #inline"));
    free(text);
    text = read_all("Crlf.md");
    CHECK(text && !strcmp(text, "\xEF\xBB\xBF---\r\nstate: crlf\r\nrating: 3\r\n---\r\nBody\r\nmore\r\n"));       /* BOM and CRLF kept, nothing else touched */
    free(text);
    text = read_all("Mixed.md");
    CHECK(text && !strcmp(text, "---\nstate: mixed\r\nx: 1\n---\nbody\r\n"));
    free(text);
    text = read_all("Bad.md");
    CHECK(text && !strcmp(text, NOTE_D));
    free(text);
    /* change a type across the vault: notes whose value does not fit are left as they are; the vault remembers the type */
    {
        char *root = ex_platform_realpath(V);
        ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL);                                 /* the changed files are read again */
        CHECK(ex_props_retype_everywhere(w, &io, root, "rating", EX_PROP_TEXT, &changed) == 0);
        CHECK(changed == 2);                                                               /* "4" and "3" become quoted text; "high" is already text */
        text = read_all("Alpha.md");
        CHECK(text && strstr(text, "rating: \"4\"\n"));
        free(text);
        text = read_all("Beta.md");
        CHECK(text && strstr(text, "rating: high\n"));
        free(text);
        CHECK(ex_prop_type_get(root, "rating") == EX_PROP_TEXT);
        {
            char *types = read_all(".exhaust/types.ini");
            CHECK(types && strstr(types, "rating = text"));
            free(types);
        }
        free(root);
    }
    ex_note_io_free(&io);
    ex_index_close(w);
}

int main(void) {
    ExIndex *w;
    char err[200], *root;
    ex_platform_remove_tree(V);
    ex_platform_remove_tree("props_test_db");
    write_str("Alpha.md", NOTE_A);
    write_str("Beta.md", NOTE_B);
    write_str("C.md", NOTE_C);
    write_str("Bad.md", NOTE_D);
    root = ex_platform_realpath(V);
    w = ex_index_open("props_test_db/index.db", 1, err, sizeof err);
    CHECK(w && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (w) { test_read(w); ex_index_close(w); }
    free(root);
    test_edits();
    test_vault();
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
