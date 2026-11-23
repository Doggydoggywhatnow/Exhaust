/* compose (6e-ii): unique notes (names follow the prefix format and never collide), the note composer (the section of a heading, extraction with a link, an
 * embed or nothing left behind and a template with {{content}}, merging one note into another with the properties merged and every link to the merged note
 * pointing at the note it went into). Works in ./compose_test_vault and ./compose_test_db, which it removes before and after. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_compose.h"
#include "exhaust_datefmt.h"
#include "exhaust_platform.h"
#include "exhaust_templates.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "compose_test_vault"
#define DB "compose_test_db"

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
static int file_is(const char *rel, const char *want) {
    char *got = read_file(rel);
    int ok = got && !strcmp(got, want);
    if (!ok) fprintf(stderr, "%s differs\n--- expected\n%s\n--- got\n%s\n", rel, want, got ? got : "(missing)");
    free(got);
    return ok;
}
static void fresh(void) { ex_platform_remove_tree(V); ex_platform_remove_tree(DB); ex_platform_make_dirs(V); }

static void test_unique(void) {
    ExUniqueConfig c;
    char name[100], rel[300];
    double t = ex_date_make(2026, 11, 23, 7, 15, 42, 0, 0);
    ExVault *v;
    memset(&c, 0, sizeof c);
    CHECK(ex_unique_name(&c, t, name, sizeof name) == 0 && !strcmp(name, "202611230715"));             /* the default */
    c.prefix_format = "YYYY-MM-DD HHmmss";
    CHECK(ex_unique_name(&c, t, name, sizeof name) == 0 && !strcmp(name, "2026-11-23 071542"));
    c.prefix_format = "[Z]YYMMDD-HHmm"; CHECK(ex_unique_name(&c, t, name, sizeof name) == 0 && !strcmp(name, "Z261123-0715"));
    c.prefix_format = "YYYY/MM"; CHECK(ex_unique_name(&c, t, name, sizeof name) != 0);                  /* a slash is a folder, not a name */
    c.prefix_format = "YYYY:MM"; CHECK(ex_unique_name(&c, t, name, sizeof name) != 0);
    c.prefix_format = "";  CHECK(ex_unique_name(&c, t, name, sizeof name) == 0 && !strcmp(name, "202611230715"));
    c.tz_min = 120; CHECK(ex_unique_name(&c, t, name, sizeof name) == 0 && !strcmp(name, "202611230915"));  /* in a time zone */
    c.tz_min = 0;
    fresh();
    ex_platform_make_dirs(V "/Templates");
    write_file("Templates/Z.md", "---\ncreated: {{date:YYYY-MM-DD}}\n---\n# {{title}}\n");
    v = ex_vault_open(V);
    CHECK(v != NULL);
    if (!v) return;
    c.folder = "Zettel/inbox/"; c.template_path = "Templates/Z";
    CHECK(ex_unique_create(v, &c, t, "YYYY-MM-DD", "HH:mm", rel, sizeof rel) == 1 && !strcmp(rel, "Zettel/inbox/202611230715.md"));
    CHECK(file_is("Zettel/inbox/202611230715.md", "---\ncreated: 2026-11-23\n---\n# 202611230715\n"));
    {   /* the same minute again: nothing is replaced, a number is added; the user's text in the first is not touched */
        FILE *f = fopen(V "/Zettel/inbox/202611230715.md", "ab");
        if (f) { fputs("mine\n", f); fclose(f); }
    }
    CHECK(ex_unique_create(v, &c, t, "YYYY-MM-DD", "HH:mm", rel, sizeof rel) == 1 && !strcmp(rel, "Zettel/inbox/202611230715-2.md"));
    CHECK(ex_unique_create(v, &c, t, "YYYY-MM-DD", "HH:mm", rel, sizeof rel) == 1 && !strcmp(rel, "Zettel/inbox/202611230715-3.md"));
    CHECK(file_is("Zettel/inbox/202611230715-3.md", "---\ncreated: 2026-11-23\n---\n# 202611230715-3\n"));
    {
        char *t1 = read_file("Zettel/inbox/202611230715.md");
        CHECK(t1 && strstr(t1, "mine\n"));
        free(t1);
    }
    c.template_path = ""; c.folder = "";
    CHECK(ex_unique_create(v, &c, ex_date_make(2026, 11, 23, 7, 16, 0, 0, 0), "YYYY-MM-DD", "HH:mm", rel, sizeof rel) == 1 && !strcmp(rel, "202611230716.md"));
    CHECK(file_is("202611230716.md", ""));
    c.prefix_format = "YYYY:MM"; CHECK(ex_unique_create(v, &c, t, "YYYY-MM-DD", "HH:mm", rel, sizeof rel) == -1);
    c.prefix_format = ""; c.folder = "../out"; CHECK(ex_unique_create(v, &c, t, "YYYY-MM-DD", "HH:mm", rel, sizeof rel) == -1);
    ex_vault_close(v);
}

static void test_headings(void) {
    static const char text[] =
        "---\ntags: [x]\n---\n# Title\nintro\n\n## First\na\n```md\n## not a heading\n```\nb\n\n### Deep\nc\n\n## Second\nd\n#hashtag\n####### seven\n";
    size_t s, e, at;
    char h[100];
    at = strstr(text, "intro") - text;
    CHECK(ex_compose_heading_span(text, strlen(text), at, &s, &e, h, sizeof h) == 0 && !strcmp(h, "Title") && s == (size_t)(strstr(text, "# Title") - text) && e == strlen(text));
    at = strstr(text, "\na\n") - text + 1;
    CHECK(ex_compose_heading_span(text, strlen(text), at, &s, &e, h, sizeof h) == 0 && !strcmp(h, "First") && s == (size_t)(strstr(text, "## First") - text) && e == (size_t)(strstr(text, "## Second") - text));
    at = strstr(text, "## not a heading") - text;                                    /* inside a fence: still First's section */
    CHECK(ex_compose_heading_span(text, strlen(text), at, &s, &e, h, sizeof h) == 0 && !strcmp(h, "First"));
    at = strstr(text, "\nc\n") - text + 1;
    CHECK(ex_compose_heading_span(text, strlen(text), at, &s, &e, h, sizeof h) == 0 && !strcmp(h, "Deep") && e == (size_t)(strstr(text, "## Second") - text));
    at = strstr(text, "d\n#hashtag") - text;
    CHECK(ex_compose_heading_span(text, strlen(text), at, &s, &e, h, sizeof h) == 0 && !strcmp(h, "Second") && e == strlen(text));     /* to the end of the text */
    CHECK(ex_compose_heading_span(text, strlen(text), 6, &s, &e, h, sizeof h) == -1);              /* inside the properties */
    CHECK(ex_compose_heading_span("no heading here\n", 15, 3, &s, &e, h, sizeof h) == -1);        /* none before the cursor */
    CHECK(ex_compose_heading_span("text\n# H\nbody\n", 14, 1, &s, &e, h, sizeof h) == -1);         /* before the first heading */
    CHECK(ex_compose_heading_span("# H #\nbody\n", 11, 7, &s, &e, h, sizeof h) == 0 && !strcmp(h, "H"));    /* closing hashes */
    CHECK(ex_compose_heading_span("#\nx\n", 4, 2, &s, &e, h, sizeof h) == 0 && !strcmp(h, ""));
    {
        char n[100];
        ex_compose_suggest_name("  # My *big*: idea / part|2  \nsecond line", 40, n, sizeof n);
        CHECK(!strcmp(n, "My big idea part 2"));
        ex_compose_suggest_name("", 0, n, sizeof n); CHECK(!strcmp(n, "Untitled"));
        ex_compose_suggest_name("***", 3, n, sizeof n); CHECK(!strcmp(n, "Untitled"));
        ex_compose_suggest_name("a very long first line that goes on and on and on and on and on and on and on", 75, n, sizeof n); CHECK(strlen(n) <= 60 && n[0] == 'a');
    }
}

static void test_extract(void) {
    ExVault *v;
    ExComposeConfig c;
    char *rel = NULL, *rep = NULL;
    memset(&c, 0, sizeof c);
    fresh();
    write_file("Notes/Source.md", "Intro\nSelected text\nline 2\nOutro\n");
    v = ex_vault_open(V);
    CHECK(v != NULL);
    if (!v) return;
    CHECK(ex_compose_extract(v, "Notes/Source.md", "Selected text\nline 2", 20, "Extracted", &c, &rel, &rep) == 0);
    CHECK(rel && !strcmp(rel, "Notes/Extracted.md") && rep && !strcmp(rep, "[[Extracted]]"));
    CHECK(file_is("Notes/Extracted.md", "Selected text\nline 2"));
    free(rel); free(rep);
    c.after_extract = EX_AFTER_EMBED;
    CHECK(ex_compose_extract(v, "Notes/Source.md", "More", 4, "Embedded", &c, &rel, &rep) == 0 && !strcmp(rep, "![[Embedded]]"));
    free(rel); free(rep);
    c.after_extract = EX_AFTER_NONE;
    CHECK(ex_compose_extract(v, "Notes/Source.md", "Gone", 4, "Silent", &c, &rel, &rep) == 0 && !strcmp(rep, "") && file_is("Notes/Silent.md", "Gone"));
    free(rel); free(rep);
    c.after_extract = EX_AFTER_LINK;
    /* a name that is taken: nothing is made, nothing is changed */
    {
        FILE *f = fopen(V "/Notes/Taken.md", "wb");
        if (f) { fputs("mine", f); fclose(f); }
    }
    ex_vault_rescan(v);
    CHECK(ex_compose_extract(v, "Notes/Source.md", "X", 1, "Taken", &c, &rel, &rep) == EX_VAULT_EXISTS && !rel && !rep && file_is("Notes/Taken.md", "mine"));
    CHECK(ex_compose_extract(v, "Notes/Source.md", "X", 1, "a/b", &c, &rel, &rep) == EX_VAULT_INVALID && ex_compose_extract(v, "Notes/Source.md", "X", 1, "x:y", &c, &rel, &rep) == EX_VAULT_INVALID
          && ex_compose_extract(v, "Notes/Source.md", "X", 1, "", &c, &rel, &rep) == EX_VAULT_INVALID && ex_compose_extract(v, "Notes/Source.md", "X", 1, "a|b", &c, &rel, &rep) == EX_VAULT_INVALID
          && ex_compose_extract(v, "Notes/Source.md", "X", 1, "CON", &c, &rel, &rep) == EX_VAULT_INVALID);
    /* templates: variables, the content where {{content}} is, after the template when there is no {{content}} */
    {
        static const char t1[] = "---\nfrom: \"{{fromTitle}}\"\n---\n# {{newTitle}}\n\n{{content}}\n\nMade {{date:YYYY-MM-DD}}.";
        static const char t2[] = "Header\n";
        c.template_text = t1; c.template_len = sizeof t1 - 1; c.now_ms = ex_date_make(2026, 11, 23, 0, 0, 0, 0, 0);
        CHECK(ex_compose_extract(v, "Notes/Source.md", "the part", 8, "Templated", &c, &rel, &rep) == 0);
        CHECK(file_is("Notes/Templated.md", "---\nfrom: \"Source\"\n---\n# Templated\n\nthe part\n\nMade 2026-11-23."));
        free(rel); free(rep);
        c.template_text = t2; c.template_len = sizeof t2 - 1;
        CHECK(ex_compose_extract(v, "Notes/Source.md", "tail", 4, "Appended", &c, &rel, &rep) == 0 && file_is("Notes/Appended.md", "Header\ntail"));
        free(rel); free(rep);
        c.template_text = NULL; c.template_len = 0;
    }
    CHECK(ex_compose_extract(v, "Top.md", "t", 1, "AtTop", &c, &rel, &rep) == 0 && !strcmp(rel, "AtTop.md"));      /* a source in the top folder */
    free(rel); free(rep);
    ex_vault_close(v);
}

typedef struct Io { char root[128]; } Io;

static void test_merge(void) {
    ExIndex *w;
    char err[200], *root;
    ExNoteIO io;
    ExRenameReport rep;
    ExComposeConfig c;
    char *a, *b, *merged;
    size_t al = 0, bl = 0, ml = 0;
    ExVault *v;
    memset(&c, 0, sizeof c);
    fresh();
    write_file("A.md", "---\ntags: [a]\nstatus: draft\n---\n# A\nbody of A with {{title}}\nsee [[B]] and [[A]]\n");
    write_file("B.md", "---\ntags: [b]\n---\nB text\n");
    write_file("C.md", "Links: [[A]] and [[A|alias]] and [[A#Sec]] and ![[A]] and [A](A.md) and [[B]].\n");
    write_file("D/E.md", "[x](../A.md) [[A]]\n");
    write_file("F.md", "no links at all\n");
    root = ex_platform_realpath(V);
    w = ex_index_open(DB "/index.db", 1, err, sizeof err);
    if (!w) { ex_platform_make_dirs(DB); w = ex_index_open(DB "/index.db", 1, err, sizeof err); }
    CHECK(w != NULL && ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) == 0);
    if (!w) { free(root); return; }
    io = ex_note_io_files(root);
    a = io.read(io.user, "A.md", &al);
    b = io.read(io.user, "B.md", &bl);
    CHECK(a && b);
    /* the text: properties merged (the note's own first, the new items after), the text added after a blank line, {{title}} of the merged text left as it is */
    merged = ex_compose_merge_text(a, al, "A", b, bl, "B", &c, &ml);
    CHECK(merged && !strcmp(merged, "---\ntags: [b, a]\nstatus: draft\n---\nB text\n\n# A\nbody of A with {{title}}\nsee [[B]] and [[A]]\n"));
    /* the links, planned first (nothing written), then written */
    CHECK(ex_retarget_plan(w, &io, "A.md", "B.md", &rep) == 0);
    CHECK(rep.total_links == 7 && rep.n == 2);            /* C has five links to A, D/E two */
    ex_rename_report_free(&rep);
    CHECK(file_is("C.md", "Links: [[A]] and [[A|alias]] and [[A#Sec]] and ![[A]] and [A](A.md) and [[B]].\n"));    /* the plan changed nothing */
    CHECK(io.write(io.user, "B.md", merged, ml) == 0);
    CHECK(ex_index_update_text(w, "B.md", merged, ml) == 0);                // the index learns the text of B (the service job does this before it rewrites the links)
    CHECK(ex_retarget_links(w, &io, "A.md", "B.md", &rep) == 0);
    ex_rename_report_free(&rep);
    CHECK(file_is("C.md", "Links: [[B]] and [[B|alias]] and [[B#Sec]] and ![[B]] and [A](B.md) and [[B]].\n"));    /* only the target of a Markdown link changes, not its text */
    CHECK(file_is("D/E.md", "[x](../B.md) [[B]]\n"));
    CHECK(file_is("F.md", "no links at all\n"));
    {   /* A is not touched by the retargeting (the caller deletes it), and B holds the merged text, its link to A now to itself */
        char *t = read_file("B.md");
        CHECK(t && strstr(t, "see [[B]] and [[B]]") && strstr(t, "tags: [b, a]"));
        free(t);
        CHECK(file_is("A.md", "---\ntags: [a]\nstatus: draft\n---\n# A\nbody of A with {{title}}\nsee [[B]] and [[A]]\n"));
    }
    free(merged); free(a); free(b);
    ex_note_io_free(&io);
    ex_index_close(w);
    free(root);

    /* the merge text in other situations */
    {
        char *r;
        size_t n;
        r = ex_compose_merge_text("Just text", 9, "From", "", 0, "Into", &c, &n);
        CHECK(r && !strcmp(r, "Just text\n"));                                                  /* into an empty note */
        free(r);
        r = ex_compose_merge_text("From body\n", 10, "From", "Into without end", 16, "Into", &c, &n);
        CHECK(r && !strcmp(r, "Into without end\n\nFrom body\n"));
        free(r);
        r = ex_compose_merge_text("---\nk: \"{{x}}\"\n---\nbody", 23, "From", "---\nj: 1\n---\nInto\n", 17, "Into", &c, &n);
        CHECK(r && !strcmp(r, "---\nj: 1\nk: \"{{x}}\"\n---\nInto\n\nbody\n"));                  /* the user's text is never filled in */
        free(r);
        {
            static const char tpl[] = "## From {{fromTitle}} into {{newTitle}}\n\n{{content}}";
            c.template_text = tpl; c.template_len = sizeof tpl - 1;
            r = ex_compose_merge_text("---\nt: 1\n---\nWords\n", 19, "Src", "Dst\n", 4, "Dest", &c, &n);
            CHECK(r && !strcmp(r, "---\nt: 1\n---\nDst\n\n## From Src into Dest\n\nWords\n"));
            free(r);
            c.template_text = NULL; c.template_len = 0;
        }
        r = ex_compose_merge_text("\xEF\xBB\xBF---\ntags: [z]\n---\nbody\n", 28, "F", "Into\n", 5, "I", &c, &n);
        CHECK(r && strstr(r, "tags: [z]") && strstr(r, "body"));                              /* a byte order mark in the merged note */
        free(r);
    }
    {   /* a merge removes the merged note by the user's setting: here it is moved to the vault's trash */
        v = ex_vault_open(V);
        CHECK(v != NULL);
        if (v) { ExVaultOptions o; ex_vault_get_options(v, &o); o.trash_mode = EX_TRASH_PERMANENT; ex_vault_set_options(v, &o); CHECK(ex_vault_delete(v, "A.md") == EX_VAULT_OK); ex_vault_close(v); }
    }
}

int main(void) {
    test_unique();
    test_headings();
    test_extract();
    test_merge();
    ex_platform_remove_tree(V);
    ex_platform_remove_tree(DB);
    if (failures) fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
