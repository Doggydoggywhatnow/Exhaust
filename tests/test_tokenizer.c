/* tokenizer: the sample note that contains every element is tokenized and the result is checked
 * against a golden dump and against explicit expectations; callout types and aliases; the HTML subset.
 * Usage: test_tokenizer <sample-note.md> <golden-dump.txt> [--write-golden] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_html.h"
#include "exhaust_markdown.h"
#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct Dump { const char *text; char *out; size_t n, cap; size_t counts[EX_MD_KIND_COUNT]; } Dump;

static void put(Dump *d, const char *s) {
    size_t l = strlen(s);
    if (d->n + l + 1 > d->cap) { d->cap = (d->n + l + 1) * 2; d->out = realloc(d->out, d->cap); }
    memcpy(d->out + d->n, s, l + 1);
    d->n += l;
}

static int visit_dump(const ExMdSpan *sp, void *user) {
    Dump *d = user;
    char line[512], snip[48];
    size_t len = sp->end - sp->start, k, o = 0;
    for (k = 0; k < len && k < 28 && o + 4 < sizeof snip; k++) {
        unsigned char ch = (unsigned char)d->text[sp->start + k];
        if (ch == '\n') { snip[o++] = '\\'; snip[o++] = 'n'; } else if (ch == '"') { snip[o++] = '\''; } else if (ch < 32) snip[o++] = '?'; else snip[o++] = (char)ch;
    }
    snip[o] = 0;
    snprintf(line, sizeof line, "%s %zu-%zu%s%s^%s \"%s%s\"\n", ex_md_kind_name(sp->kind), sp->start, sp->end,
             sp->level ? " L" : "", sp->level ? (char[12]){(char)('0' + sp->level % 10), 0} : "", sp->parent_kind ? ex_md_kind_name(sp->parent_kind) : "-", snip, len > 28 ? "..." : "");
    if (sp->level >= 10 || sp->flags) {                                                   /* the detail that does not fit above */
        char extra[64];
        snprintf(extra, sizeof extra, "    level=%u flags=%u\n", (unsigned)sp->level, (unsigned)sp->flags);
        put(d, line);
        put(d, extra);
    } else put(d, line);
    d->counts[sp->kind]++;
    return 0;
}

static void dump(const char *text, size_t len, Dump *d) {
    ExMd *m = ex_md_create();
    memset(d, 0, sizeof *d);
    d->text = text;
    ex_md_tokenize(m, text, len);
    ex_md_visit(m, 0, len, visit_dump, d);
    ex_md_free(m);
}

typedef struct Find { ExMdKind kind; const char *text, *snip; int found; } Find;

static int find_cb(const ExMdSpan *sp, void *u) {
    Find *fp = u;
    if (sp->kind == fp->kind && strlen(fp->snip) == sp->end - sp->start && memcmp(fp->text + sp->start, fp->snip, strlen(fp->snip)) == 0) fp->found = 1;
    return fp->found;
}

static int has_span(const char *text, size_t len, ExMdKind kind, const char *snippet) {
    ExMd *m = ex_md_create();
    Find f;
    int found;
    f.kind = kind; f.text = text; f.snip = snippet; f.found = 0;
    ex_md_tokenize(m, text, len);
    ex_md_visit(m, 0, len, find_cb, &f);
    found = f.found;
    ex_md_free(m);
    return found;
}

typedef struct CalloutInfo { int callouts, types[4], folds[4], quotes; } CalloutInfo;

static int callout_cb(const ExMdSpan *sp, void *u) {
    CalloutInfo *ci = u;
    if (sp->kind == EX_MD_CALLOUT && ci->callouts < 4) { ci->types[ci->callouts] = sp->level; ci->folds[ci->callouts] = sp->flags; ci->callouts++; }
    if (sp->kind == EX_MD_BLOCKQUOTE) ci->quotes++;
    return 0;
}

int main(int argc, char **argv) {
    size_t len = 0, glen = 0;
    char *text, *golden;
    Dump d;
    int write_golden = argc > 3 && strcmp(argv[3], "--write-golden") == 0;
    int known;
    if (argc < 3) { fprintf(stderr, "usage: test_tokenizer SAMPLE GOLDEN\n"); return 2; }
    text = ex_platform_read_file(argv[1], &len);
    CHECK(text != NULL);
    if (!text) return 1;
    dump(text, len, &d);

    /* every kind of the documented list appears in the sample */
    {
        int k;
        for (k = EX_MD_FRONTMATTER; k < EX_MD_KIND_COUNT; k++)
            if (d.counts[k] == 0) { fprintf(stderr, "kind never produced by the sample: %s\n", ex_md_kind_name((ExMdKind)k)); failures++; }
    }
    /* some exact spans */
    CHECK(has_span(text, len, EX_MD_STRONG, "**strong**") && has_span(text, len, EX_MD_EMPHASIS, "*emphasis*") && has_span(text, len, EX_MD_EMPHASIS, "_underscores_"));
    CHECK(has_span(text, len, EX_MD_STRIKE, "~~strike~~") && has_span(text, len, EX_MD_HIGHLIGHT, "==highlight=="));
    CHECK(has_span(text, len, EX_MD_CODE_SPAN, "`code span`") && has_span(text, len, EX_MD_ESCAPE, "\\*"));
    CHECK(has_span(text, len, EX_MD_MATH_INLINE, "$a^2 + b^2 = c^2$") && has_span(text, len, EX_MD_MATH_INLINE, "$$x = y$$"));
    CHECK(has_span(text, len, EX_MD_WIKILINK, "[[Wikilink]]") && has_span(text, len, EX_MD_WIKILINK, "[[Page|alias]]") && has_span(text, len, EX_MD_WIKILINK, "[[Page#^block]]"));
    CHECK(has_span(text, len, EX_MD_LINK_TARGET, "Page") && has_span(text, len, EX_MD_LINK_HEADING, "Heading") && has_span(text, len, EX_MD_LINK_BLOCK, "block") && has_span(text, len, EX_MD_LINK_ALIAS, "alias"));
    CHECK(has_span(text, len, EX_MD_EMBED, "![[embed.png|100]]") && has_span(text, len, EX_MD_LINK, "[text](https://example.com \"Title\")") && has_span(text, len, EX_MD_IMAGE, "![alt](image.png)"));
    CHECK(has_span(text, len, EX_MD_AUTOLINK, "<https://auto.link>") && has_span(text, len, EX_MD_AUTOLINK, "https://bare.example/path?x=1#frag") && has_span(text, len, EX_MD_AUTOLINK, "https://example.org/end"));
    CHECK(has_span(text, len, EX_MD_FOOTNOTE_REF, "[^note]") && has_span(text, len, EX_MD_INLINE_FOOTNOTE, "^[footnote text]"));
    CHECK(has_span(text, len, EX_MD_TAG, "#tag") && has_span(text, len, EX_MD_TAG, "#nested/tag-two") && !has_span(text, len, EX_MD_TAG, "#123") && !has_span(text, len, EX_MD_TAG, "#b"));
    CHECK(has_span(text, len, EX_MD_BLOCK_ID, "^block-id") && has_span(text, len, EX_MD_COMMENT_INLINE, "%%hidden *text*%%"));
    CHECK(has_span(text, len, EX_MD_HTML_INLINE, "<b>") && has_span(text, len, EX_MD_HTML_INLINE, "</b>") && has_span(text, len, EX_MD_HTML_INLINE, "<br>") && has_span(text, len, EX_MD_HTML_INLINE, "<!-- note -->"));
    CHECK(!has_span(text, len, EX_MD_EMPHASIS, "*text*"));                                    /* inside a comment: not parsed */
    CHECK(has_span(text, len, EX_MD_CALLOUT_TYPE, "note") && has_span(text, len, EX_MD_CALLOUT_TYPE, "WARNING") && has_span(text, len, EX_MD_CALLOUT_TYPE, "tldr") && has_span(text, len, EX_MD_CALLOUT_TYPE, "custom-type"));
    CHECK(has_span(text, len, EX_MD_CALLOUT_TITLE, "A callout title") && has_span(text, len, EX_MD_CALLOUT_TITLE, "Closed and foldable"));
    CHECK(has_span(text, len, EX_MD_CODE_INFO, "python title=\"x\"") && has_span(text, len, EX_MD_FRONTMATTER, "---\ntitle: Every element\ntags: [sample, markdown]\naliases: [Sample note]\n---"));
    CHECK(has_span(text, len, EX_MD_FOOTNOTE_DEF, "[^note]: The footnote text,\n    continued here.") && has_span(text, len, EX_MD_COMMENT_BLOCK, "%%\na multi-line\ncomment block\n%%"));
    CHECK(has_span(text, len, EX_MD_MATH_BLOCK, "$$\n\\int_0^1 x\\,dx\n$$") && has_span(text, len, EX_MD_HTML_BLOCK, "<div class=\"box\">\n  <p>An HTML block</p>\n</div>"));
    CHECK(d.counts[EX_MD_HEADING] == 4 && d.counts[EX_MD_TASK] == 3 && d.counts[EX_MD_CALLOUT] == 4 && d.counts[EX_MD_TABLE] == 1 && d.counts[EX_MD_TABLE_ROW] == 4);
    CHECK(d.counts[EX_MD_CODE_BLOCK] == 3 && d.counts[EX_MD_HR] == 1 && d.counts[EX_MD_BLOCKQUOTE] == 2);       /* fenced twice and indented once; the quote and the nested quote */
    CHECK(d.counts[EX_MD_TABLE_CELL] == 11);

    /* callout types and aliases */
    CHECK(ex_callout_type("NOTE", 4, &known) == EX_CALLOUT_NOTE && known);
    CHECK(ex_callout_type("tldr", 4, &known) == EX_CALLOUT_ABSTRACT && known && ex_callout_type("Summary", 7, &known) == EX_CALLOUT_ABSTRACT && known);
    CHECK(ex_callout_type("hint", 4, &known) == EX_CALLOUT_TIP && ex_callout_type("important", 9, &known) == EX_CALLOUT_TIP);
    CHECK(ex_callout_type("check", 5, &known) == EX_CALLOUT_SUCCESS && ex_callout_type("DONE", 4, &known) == EX_CALLOUT_SUCCESS);
    CHECK(ex_callout_type("help", 4, &known) == EX_CALLOUT_QUESTION && ex_callout_type("faq", 3, &known) == EX_CALLOUT_QUESTION);
    CHECK(ex_callout_type("caution", 7, &known) == EX_CALLOUT_WARNING && ex_callout_type("attention", 9, &known) == EX_CALLOUT_WARNING);
    CHECK(ex_callout_type("fail", 4, &known) == EX_CALLOUT_FAILURE && ex_callout_type("missing", 7, &known) == EX_CALLOUT_FAILURE);
    CHECK(ex_callout_type("error", 5, &known) == EX_CALLOUT_DANGER && ex_callout_type("cite", 4, &known) == EX_CALLOUT_QUOTE);
    CHECK(ex_callout_type("bug", 3, &known) == EX_CALLOUT_BUG && ex_callout_type("example", 7, &known) == EX_CALLOUT_EXAMPLE && ex_callout_type("todo", 4, &known) == EX_CALLOUT_TODO && ex_callout_type("info", 4, &known) == EX_CALLOUT_INFO);
    CHECK(ex_callout_type("custom-type", 11, &known) == EX_CALLOUT_NOTE && !known);                 /* unknown types show as note */
    CHECK(strcmp(ex_callout_name(EX_CALLOUT_ABSTRACT), "abstract") == 0);
    {   /* the callout spans carry the canonical type and the fold state */
        ExMd *m = ex_md_create();
        const char *src = "> [!Tip]+ Open\n> body\n\n> [!faq]- Closed\n\n> [!nope]\n> x\n\n> [!note\n> not a callout\n";
        CalloutInfo ci;
        memset(&ci, 0, sizeof ci);
        ex_md_tokenize(m, src, strlen(src));
        ex_md_visit(m, 0, strlen(src), callout_cb, &ci);
        CHECK(ci.callouts == 3 && ci.quotes == 1);
        CHECK(ci.types[0] == EX_CALLOUT_TIP && ci.folds[0] == EX_CALLOUT_FOLD_OPEN);
        CHECK(ci.types[1] == EX_CALLOUT_QUESTION && ci.folds[1] == EX_CALLOUT_FOLD_CLOSED);
        CHECK(ci.types[2] == EX_CALLOUT_NOTE && ci.folds[2] == EX_CALLOUT_NOT_FOLDABLE);
        ex_md_free(m);
    }

    /* the golden dump */
    if (write_golden) {
        FILE *f = fopen(argv[2], "wb");
        if (f) { fwrite(d.out, 1, d.n, f); fclose(f); }
        fprintf(stderr, "golden written\n");
    } else {
        golden = ex_platform_read_file(argv[2], &glen);
        CHECK(golden != NULL);
        if (golden) {
            if (glen != d.n || memcmp(golden, d.out, d.n) != 0) {
                size_t i = 0;
                while (i < glen && i < d.n && golden[i] == d.out[i]) i++;
                fprintf(stderr, "dump differs from the golden file at byte %zu:\n--- got ---\n%.200s\n--- golden ---\n%.200s\n", i, d.out + (i > 60 ? i - 60 : 0), golden + (i > 60 ? i - 60 : 0));
                failures++;
            }
            free(golden);
        }
    }
    free(d.out);
    free(text);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("tokenizer: ok");
    return 0;
}
