/* base_index (6b-ii): the files a base runs over are read from the real index (properties with their types, tags, links, embeds, times), a base file runs
 * over them, and a note that holds a ```base block or embeds a .base file shows the Markdown of the view. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_base_index.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "base_index_vault"
#define DB "base_index_db"

static void write_file(const char *rel, const char *content) {
    char path[512], *slash;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", V, rel);
    slash = strrchr(path, '/');
    if (slash) { *slash = 0; ex_platform_make_dirs(path); *slash = '/'; }
    f = fopen(path, "wb");
    if (f) { fwrite(content, 1, strlen(content), f); fclose(f); }
}

static const ExBaseFile *find(const ExBaseData *d, const char *path) { size_t i; for (i = 0; i < d->n; i++) if (!strcmp(d->files[i].path, path)) return &d->files[i]; return NULL; }
static const ExBaseProp *prop(const ExBaseFile *f, const char *name) { size_t i; for (i = 0; f && i < f->nprops; i++) if (!strcmp(f->props[i].name, name)) return &f->props[i]; return NULL; }

int main(void) {
    ExIndex *w;
    char err[200], *root;
    ExBaseData d;
    ExBaseResult r;
    ExBase *b;
    const char *base_text = "filters: 'file.hasTag(\"book\")'\nformulas:\n  pages2: 'pages * 2'\nviews:\n  - type: table\n    name: Books\n    order: [file.name, status, pages, formula.pages2, author]\n    sort:\n      - property: pages\n        direction: DESC\n    summaries:\n      pages: Sum\n  - type: list\n    name: Titles\n    order: [file.basename]\n";
    char *out, *text;
    size_t olen = 0;
    ex_platform_remove_tree(V);
    ex_platform_remove_tree(DB);
    write_file("Books/Dune.md", "---\npages: 412\nstatus: done\nfinished: true\nread: 2025-01-15\nauthor: \"[[Herbert]]\"\ntags: [scifi, classic]\naliases: [Arrakis]\n---\n# Dune\nA #book about sand. See [[Textbook#Intro]] and ![[cover.png]].\n");
    write_file("Books/Emma.md", "---\npages: 474\nstatus: reading\nauthor: \"[[Austen]]\"\n---\nA #book. Links to [[Dune]].\n");
    write_file("Books/Plain.md", "No properties here, no book tag.\n");
    write_file("Textbook.md", "# Intro\nText.\n");
    write_file("Herbert.md", "---\nrole: author\n---\n");
    write_file("Austen.md", "---\nrole: author\n---\n");
    write_file("cover.png", "PNG");
    write_file("Board.base", base_text);
    ex_platform_make_dirs(DB);
    root = ex_platform_realpath(V);
    w = ex_index_open(DB "/index.db", 1, err, sizeof err);
    CHECK(w != NULL);
    if (!w || ex_index_sync(w, root, NULL, 0, NULL, NULL, NULL) != 0) { fprintf(stderr, "cannot index\n"); return 1; }
    CHECK(ex_base_data_load(w, &d) == 0 && d.n == 8);
    {
        const ExBaseFile *dune = find(&d, "Books/Dune.md"), *png = find(&d, "cover.png");
        CHECK(dune && prop(dune, "pages") && prop(dune, "pages")->type == EXB_NUMBER && !strcmp(prop(dune, "pages")->value, "412"));
        CHECK(prop(dune, "finished") && prop(dune, "finished")->type == EXB_CHECKBOX && prop(dune, "read") && prop(dune, "read")->type == EXB_DATE && !strcmp(prop(dune, "read")->value, "2025-01-15"));
        CHECK(prop(dune, "tags") && prop(dune, "tags")->type == EXB_LIST && prop(dune, "aliases") && prop(dune, "author") && !strcmp(prop(dune, "author")->value, "[[Herbert]]"));
        CHECK(dune && dune->ntags >= 3);                                                             /* #book from the text, scifi and classic from the properties */
        {
            size_t i;
            int book = 0, scifi = 0;
            for (i = 0; dune && i < dune->ntags; i++) { if (!strcmp(dune->tags[i], "#book")) book = 1; if (!strcmp(dune->tags[i], "#scifi")) scifi = 1; }
            CHECK(book && scifi);
        }
        CHECK(dune && dune->nlinks >= 2 && dune->nembeds == 1 && !strcmp(dune->embeds[0], "cover.png"));
        {
            size_t i;
            int intro = 0;
            for (i = 0; dune && i < dune->nlinks; i++) if (!strcmp(dune->links[i], "Textbook#Intro")) intro = 1;
            CHECK(intro);                                                                            /* the heading part is kept */
        }
        CHECK(dune && dune->mtime_ms > 1.6e12 && dune->ctime_ms > 1.6e12 && dune->size > 100);
        CHECK(png && png->kind == 4 && png->nprops == 0 && png->size == 3);
    }
    /* run the .base over the real files */
    b = ex_base_parse(base_text, strlen(base_text), err, sizeof err);
    CHECK(b && ex_base_run(b, 0, &d, "Board.base", 1.75e12, 0, &r) == 0);
    CHECK(r.nrows == 2 && !strcmp(ex_base_cell_text(&r, 0, 0), "Emma.md") && !strcmp(ex_base_cell_text(&r, 0, 2), "474") && !strcmp(ex_base_cell_text(&r, 0, 3), "948") && !strcmp(ex_base_cell_text(&r, 1, 0), "Dune.md")
          && !strcmp(ex_base_cell_text(&r, 1, 4), "Herbert") && r.cols[2].summary.t == EXV_NUM && r.cols[2].summary.u.num == 886);
    ex_base_result_free(&r);
    ex_base_free(b);
    ex_base_data_free(&d);
    /* a note that holds a base, and a note that embeds one */
    text = "# Reading\n\nBefore.\n\n```base\nfilters: 'file.hasTag(\"book\")'\nviews:\n  - type: table\n    order: [file.name, status]\n    sort:\n      - property: file.name\n```\n\nAfter.\n\n![[Board.base#Titles]]\n\n![[Board.base]]\n\n![[Nothing.base]]\n\n```base\nviews: [\n```\n\nEnd.\n";
    out = ex_base_expand(w, root, "Reading.md", text, strlen(text), 0, 1.75e12, &olen);
    CHECK(out && olen == strlen(out));
    CHECK(out && !strncmp(out, "# Reading\n\nBefore.\n\n| File name | status |\n| --- | --- |\n| Dune.md | done |\n| Emma.md | reading |\n\nAfter.\n\n", 94));
    CHECK(out && strstr(out, "- Dune\n") != NULL && strstr(out, "- Emma\n") != NULL && strstr(out, "| Dune |") == NULL);                                           /* the embed of the list view: the second view by its name */
    CHECK(out && strstr(out, "| File name | status | pages | pages2 | author |") != NULL && strstr(out, "| Emma.md | reading | 474 | 948 | Austen |") != NULL);   /* ![[Board.base]]: the first view */
    CHECK(out && strstr(out, "*The base Nothing.base was not found.*") != NULL);
    CHECK(out && strstr(out, "> [!failure] This base could not be read") != NULL && strstr(out, "End.\n") != NULL);
    free(out);
    CHECK(ex_base_expand(w, root, "x.md", "Nothing here.\n```js\nlet a;\n```\n![[image.png]]\n", 43, 0, 1.75e12, NULL) == NULL);        /* no base: the text is not touched */
    out = ex_base_expand(w, root, "x.md", "![[Board.base#Missing view]]\n", 29, 0, 1.75e12, NULL);
    CHECK(out && strstr(out, "no view called Missing view"));
    free(out);
    ex_index_close(w);
    free(root);
    ex_platform_remove_tree(V);
    ex_platform_remove_tree(DB);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
