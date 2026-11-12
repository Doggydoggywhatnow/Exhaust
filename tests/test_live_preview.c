/* live_preview: for the sample note, live preview hides the syntax of the constructs the cursor is
 * not in and shows it under the cursor; source mode shows the text unchanged; switching modes never
 * changes the text; carets and clicks map correctly between the text and the screen; and the editor's
 * Markdown tokens stay equal to a fresh tokenization through typing, deleting, undo and redo.
 * Usage: test_live_preview <sample-note.md> [--dump | --ppm PREFIX [CARET]]
 * (--ppm writes PREFIX-live.ppm and PREFIX-source.ppm: the editor drawn in both modes, for looking at) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_markdown.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ExRenderer *g_r;

static size_t find_line(const char *text, const char *needle) {
    const char *p = strstr(text, needle);
    size_t line = 0;
    const char *q;
    if (!p) return (size_t)-1;
    for (q = text; q < p; q++) if (*q == '\n') line++;
    return line;
}

static size_t line_start(const char *text, size_t line) {
    size_t l = 0, i = 0;
    while (l < line && text[i]) { if (text[i] == '\n') l++; i++; }
    return i;
}

static char *display(ExEditor *e, size_t line) {
    static char buf[2048];
    ex_editor_line_display(e, g_r, line, buf, sizeof buf);
    return buf;
}

static int display_is(ExEditor *e, size_t line, const char *want) {
    char *got = display(e, line);
    if (strcmp(got, want) != 0) { fprintf(stderr, "line %zu shows [%s], wanted [%s]\n", line, got, want); return 0; }
    return 1;
}

/* Puts the caret at the first occurrence of `needle` plus `delta` bytes (so inside a construct) and returns its line. */
static size_t caret_at(ExEditor *e, ExDoc *d, const char *text, const char *needle, size_t delta) {
    const char *p = strstr(text, needle);
    if (!p) return (size_t)-1;
    ex_doc_set_caret(d, (size_t)(p - text) + delta);
    (void)e;
    return find_line(text, needle);
}

static int spans_equal(const ExMd *a, const ExMd *b, size_t len);

/* Draws the note in source mode and in live preview and writes both as PPM images. */
static int write_ppms(const char *sample, const char *prefix, size_t caret) {
    size_t len = 0;
    char *text = ex_platform_read_file(sample, &len);
    ExRenderer *r;
    ExTheme th;
    int m;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("ppm", 980, 1500);
    if (!r || !text) return 1;
    ex_theme_init_builtin(&th, EX_THEME_DARK);
    for (m = 1; m <= 2; m++) {
        ExDoc *d = ex_doc_create(text, len);
        ExEditor *e = ex_editor_create(d);
        int w, h;
        uint8_t *px;
        char path[512];
        FILE *f;
        ex_editor_set_font(e, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
        ex_editor_set_viewport(e, 0, 0, 980, 1500);
        ex_editor_set_mode(e, m == 1 ? EX_EDITOR_SOURCE : EX_EDITOR_LIVE);
        ex_doc_set_caret(d, caret > len ? len : caret);
        ex_renderer_clear(r, ex_theme_color(&th, EX_COLOR_BACKGROUND));
        ex_editor_draw(e, r, &th, 1, 1);
        px = ex_renderer_read_rgb(r, &w, &h);
        snprintf(path, sizeof path, "%s-%s.ppm", prefix, m == 1 ? "source" : "live");
        f = fopen(path, "wb");
        if (f && px) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(px, 1, (size_t)w * (size_t)h * 3, f); }
        if (f) fclose(f);
        free(px);
        ex_editor_free(e);
        ex_doc_free(d);
    }
    free(text);
    ex_renderer_destroy(r);
    return 0;
}

int main(int argc, char **argv) {
    size_t len = 0, nlines = 0, i;
    char *text, *after;
    ExDoc *doc;
    ExEditor *ed;
    ExFont *font;
    int dump = argc > 2 && strcmp(argv[2], "--dump") == 0;
    size_t hl, st;
    if (argc < 2) { fprintf(stderr, "usage: test_live_preview SAMPLE\n"); return 2; }
    if (argc > 3 && strcmp(argv[2], "--ppm") == 0) return write_ppms(argv[1], argv[3], argc > 4 ? (size_t)atol(argv[4]) : (size_t)-1);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    g_r = ex_renderer_create("live preview", 900, 600);
    if (!g_r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    text = ex_platform_read_file(argv[1], &len);
    CHECK(text != NULL);
    if (!text) return 1;
    for (i = 0; i < len; i++) if (text[i] == '\n') nlines++;
    doc = ex_doc_create(text, len);
    ed = ex_editor_create(doc);
    font = ex_font_builtin(ex_renderer_fonts(g_r), EX_FONT_UI_REGULAR);
    ex_editor_set_font(ed, font, 16.0f, 150);
    ex_editor_set_viewport(ed, 0, 0, 800, 500);

    /* the plain mode and the source mode show the text unchanged, line by line */
    CHECK(ex_editor_mode(ed) == EX_EDITOR_PLAIN && ex_editor_tokens(ed) == NULL);
    ex_editor_set_mode(ed, EX_EDITOR_SOURCE);
    CHECK(ex_editor_tokens(ed) != NULL);
    for (i = 0; i < nlines; i++) {
        size_t s = line_start(text, i), e = s;
        char want[2048];
        while (text[e] && text[e] != '\n') e++;
        snprintf(want, sizeof want, "%.*s", (int)(e - s), text + s);
        ex_doc_set_caret(doc, s);
        if (!display_is(ed, i, want)) { failures++; break; }
    }
    ex_doc_set_caret(doc, len);

    /* live preview: the cursor is at the very end of the note, away from everything */
    ex_editor_set_mode(ed, EX_EDITOR_LIVE);
    ex_doc_set_caret(doc, len);
    if (dump) { for (i = 0; i < nlines; i++) printf("%3zu | %s\n", i, display(ed, i)); }
    hl = find_line(text, "# Heading one #");
    CHECK(display_is(ed, hl, "Heading one "));                                   /* the # characters are gone (the space before the closing # stays) */
    CHECK(display_is(ed, find_line(text, "## Heading two"), "Heading two"));
    CHECK(display_is(ed, find_line(text, "###### Heading six"), "Heading six"));
    CHECK(display_is(ed, find_line(text, "Setext heading"), "Setext heading"));
    CHECK(display_is(ed, find_line(text, "=============="), ""));
    CHECK(display_is(ed, find_line(text, "A paragraph with"), "A paragraph with emphasis, underscores, strong, both, strike, highlight, code span, and an escaped * star."));
    CHECK(display_is(ed, find_line(text, "A second line"), "A second line with a hard break at the end"));
    CHECK(display_is(ed, find_line(text, "Links:"), "Links: Wikilink, alias, Page > Heading, Page > block, ![[embed.png|100]], text, ![alt](image.png), https://auto.link, https://bare.example/path?x=1#frag, and a footnotenote or inlinefootnote text."));
    CHECK(display_is(ed, find_line(text, "- bullet one"), "\xE2\x80\xA2 bullet one"));
    CHECK(display_is(ed, find_line(text, "- [ ] open task"), "\xE2\x98\x90 open task"));
    CHECK(display_is(ed, find_line(text, "- [x] done task"), "\xE2\x98\x91 done task"));
    CHECK(display_is(ed, find_line(text, "1. ordered one"), "1. ordered one"));    /* an ordered marker stays */
    CHECK(display_is(ed, find_line(text, "> A quote"), "\xE2\x94\x82 A quote"));
    CHECK(display_is(ed, find_line(text, "> [!note] A callout title"), "\xE2\x94\x82 A callout title"));
    CHECK(display_is(ed, find_line(text, "> [!WARNING]- Closed"), "\xE2\x94\x82 Closed and foldable"));
    CHECK(display_is(ed, find_line(text, "```python"), "python title=\"x\""));      /* the fence goes, the language stays as a label */
    CHECK(display_is(ed, find_line(text, "def f():"), "def f():"));
    CHECK(display_is(ed, find_line(text, "---\n\n[^note]"), ""));                    /* a rule is drawn instead of the dashes */
    CHECK(display_is(ed, find_line(text, "| Left | Center | Right |"), "| Left | Center | Right |"));   /* tables stay as source until they have a renderer */
    CHECK(display_is(ed, find_line(text, "Tags #tag"), "Tags #tag and #nested/tag-two but not #123 or a#b. A block id follows. ^block-id"));
    CHECK(display_is(ed, find_line(text, "Comment %%hidden"), "Comment %%hidden *text*%% inline and <b>html</b> <br> inline <!-- note -->."));   /* comments, math, embeds and html keep their syntax */
    CHECK(display_is(ed, find_line(text, "$$\n\\int"), "$$"));                       /* math blocks are source for now */
    CHECK(display_is(ed, find_line(text, "title: Every element"), "title: Every element"));   /* front matter is source until the properties editor */

    /* the cursor reveals exactly the construct it is in */
    caret_at(ed, doc, text, "*emphasis*", 3);
    CHECK(display_is(ed, find_line(text, "A paragraph with"), "A paragraph with *emphasis*, underscores, strong, both, strike, highlight, code span, and an escaped * star."));
    caret_at(ed, doc, text, "**strong**", 4);
    CHECK(display_is(ed, find_line(text, "A paragraph with"), "A paragraph with emphasis, underscores, **strong**, both, strike, highlight, code span, and an escaped * star."));
    caret_at(ed, doc, text, "~~strike~~", 0);                                         /* at the edge of a construct counts as in it */
    CHECK(strstr(display(ed, find_line(text, "A paragraph with")), "~~strike~~") != NULL);
    caret_at(ed, doc, text, "[[Page|alias]]", 3);
    CHECK(strstr(display(ed, find_line(text, "Links:")), "[[Page|alias]]") != NULL && strstr(display(ed, find_line(text, "Links:")), "[[Wikilink]]") == NULL);
    caret_at(ed, doc, text, "[text](https", 2);
    CHECK(strstr(display(ed, find_line(text, "Links:")), "[text](https://example.com \"Title\")") != NULL);
    hl = caret_at(ed, doc, text, "# Heading one #", 4);
    CHECK(display_is(ed, hl, "# Heading one #"));
    hl = caret_at(ed, doc, text, "- bullet one", 5);
    CHECK(display_is(ed, hl, "- bullet one") && display_is(ed, hl + 1, "\xE2\x80\xA2 bullet two"));
    hl = caret_at(ed, doc, text, "> A quote", 4);
    CHECK(display_is(ed, hl, "> A quote") && display_is(ed, hl + 1, "\xE2\x94\x82 continues here"));   /* only the line the cursor is on shows its > */
    hl = caret_at(ed, doc, text, "> [!note] A callout title", 12);
    CHECK(display_is(ed, hl, "> [!note] A callout title"));
    hl = caret_at(ed, doc, text, "def f():", 2);
    CHECK(display_is(ed, hl - 1, "```python title=\"x\"") && display_is(ed, hl + 2, "```"));   /* inside a code block both fences show */
    /* a selection reveals every construct it touches */
    {
        const char *a = strstr(text, "*emphasis*"), *b = strstr(text, "`code span`");
        ExSel sel;
        sel.anchor = (size_t)(a - text) + 2; sel.head = (size_t)(b - text) + 3;
        ex_doc_set_selections(doc, &sel, 1);
        CHECK(display_is(ed, find_line(text, "A paragraph with"), "A paragraph with *emphasis*, _underscores_, **strong**, ***both***, ~~strike~~, ==highlight==, `code span`, and an escaped * star."));
    }
    /* several cursors: each reveals its own construct */
    {
        const char *a = strstr(text, "*emphasis*"), *b = strstr(text, "==highlight==");
        ExSel sel[2];
        sel[0].anchor = sel[0].head = (size_t)(a - text) + 2;
        sel[1].anchor = sel[1].head = (size_t)(b - text) + 3;
        ex_doc_set_selections(doc, sel, 2);
        CHECK(display_is(ed, find_line(text, "A paragraph with"), "A paragraph with *emphasis*, underscores, strong, both, strike, ==highlight==, code span, and an escaped * star."));
    }
    ex_doc_set_caret(doc, len);

    /* switching modes never changes the text, and back to source it is all there again */
    after = ex_buffer_text(ex_doc_buffer(doc), NULL);
    CHECK(strcmp(after, text) == 0);
    free(after);
    ex_editor_set_mode(ed, EX_EDITOR_SOURCE);
    CHECK(display_is(ed, find_line(text, "A paragraph with"), "A paragraph with *emphasis*, _underscores_, **strong**, ***both***, ~~strike~~, ==highlight==, `code span`, and an escaped \\* star."));

    /* clicks: with the cursor away, clicking along a line gives offsets that never go backwards, and the click on the
     * first shown letter of "emphasis" (its asterisk is hidden) lands next to that word */
    ex_editor_set_mode(ed, EX_EDITOR_LIVE);
    {
        size_t line = find_line(text, "A paragraph with"), s = line_start(text, line);
        float rx, ry, rw, rh, xx;
        size_t prev = 0, back = 0;
        ex_doc_set_caret(doc, s);
        ex_editor_caret_rect(ed, g_r, &rx, &ry, &rw, &rh);
        for (xx = 0; xx < 900; xx += 3) {
            size_t head;
            ex_doc_set_caret(doc, len);
            ex_editor_mouse(ed, g_r, EX_MOUSE_DOWN, xx, ry + rh / 2.0f, 0, 1);
            ex_editor_mouse(ed, g_r, EX_MOUSE_UP, xx, ry + rh / 2.0f, 0, 1);
            head = ex_doc_sel(doc, 0).head;
            if (head < prev) back++;
            prev = head;
            CHECK(head >= s);
        }
        CHECK(back == 0);
        {
            /* the x of "emphasis" in the shown text: measure it with the caret inside the next construct, where the line looks the same up to there */
            const char *a = strstr(text, "*emphasis*");
            size_t off = (size_t)(a - text) + 1;
            float x, y, w, h;
            ex_doc_set_caret(doc, (size_t)(strstr(text, "_underscores_") - text) + 2);   /* reveals _underscores_ only: everything before it is laid out as before */
            ex_editor_caret_rect(ed, g_r, &x, &y, &w, &h);
            (void)x;
            ex_doc_set_caret(doc, len);
            {
                float lx;
                size_t found = 0;
                for (lx = 0; lx < 900 && !found; lx += 1) {
                    size_t head;
                    ex_doc_set_caret(doc, len);
                    ex_editor_mouse(ed, g_r, EX_MOUSE_DOWN, lx, ry + rh / 2.0f, 0, 1);
                    ex_editor_mouse(ed, g_r, EX_MOUSE_UP, lx, ry + rh / 2.0f, 0, 1);
                    head = ex_doc_sel(doc, 0).head;
                    if (head >= off) { found = head; st = head; }
                }
                CHECK(found && st <= off + 1);                                  /* the first click that is past the asterisk lands on the first letter */
            }
        }
    }

    /* the tokens the editor keeps equal a fresh tokenization after typing, deleting, undo and redo, also with several cursors */
    {
        ExMd *fresh;
        char *now;
        size_t nlen = 0;
        ex_doc_set_caret(doc, line_start(text, find_line(text, "A paragraph with")));
        ex_editor_text_input(ed, "**", 2);
        (void)ex_editor_tokens(ed);
        ex_editor_key(ed, g_r, EX_KEY_END, 0);
        ex_editor_text_input(ed, "**\n# new heading\n", 17);
        for (i = 0; i < 5; i++) ex_editor_key(ed, g_r, EX_KEY_BACKSPACE, 0);
        now = ex_buffer_text(ex_doc_buffer(doc), &nlen);
        fresh = ex_md_create();
        ex_md_tokenize(fresh, now, nlen);
        CHECK(spans_equal(ex_editor_tokens(ed), fresh, nlen));
        ex_editor_key(ed, g_r, EX_KEY_Z, EX_MOD_PRIMARY);
        ex_editor_key(ed, g_r, EX_KEY_Z, EX_MOD_PRIMARY);
        free(now);
        now = ex_buffer_text(ex_doc_buffer(doc), &nlen);
        ex_md_tokenize(fresh, now, nlen);
        CHECK(spans_equal(ex_editor_tokens(ed), fresh, nlen));
        ex_editor_key(ed, g_r, EX_KEY_Z, EX_MOD_PRIMARY | EX_MOD_SHIFT);
        free(now);
        now = ex_buffer_text(ex_doc_buffer(doc), &nlen);
        ex_md_tokenize(fresh, now, nlen);
        CHECK(spans_equal(ex_editor_tokens(ed), fresh, nlen));
        {   /* several cursors typing at once: several edits between two looks at the tokens */
            ExSel sel[3];
            char *t2 = ex_buffer_text(ex_doc_buffer(doc), &nlen);
            sel[0].anchor = sel[0].head = 10; sel[1].anchor = sel[1].head = 200; sel[2].anchor = sel[2].head = 500;
            ex_doc_set_selections(doc, sel, 3);
            ex_editor_text_input(ed, "x_", 2);
            free(t2);
            t2 = ex_buffer_text(ex_doc_buffer(doc), &nlen);
            ex_md_tokenize(fresh, t2, nlen);
            CHECK(spans_equal(ex_editor_tokens(ed), fresh, nlen));
            free(t2);
        }
        ex_md_free(fresh);
        free(now);
    }
    ex_editor_free(ed);
    ex_doc_free(doc);
    free(text);
    ex_renderer_destroy(g_r);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("live_preview: ok");
    return 0;
}

typedef struct Flat { ExMdSpan *v; size_t n, cap; } Flat;
static int collect(const ExMdSpan *s, void *u) {
    Flat *f = u;
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 512; f->v = realloc(f->v, f->cap * sizeof *f->v); }
    f->v[f->n++] = *s;
    return 0;
}
static int spans_equal(const ExMd *a, const ExMd *b, size_t len) {
    Flat fa = {0}, fb = {0};
    size_t i;
    int ok = 1;
    ex_md_visit(a, 0, len + 1, collect, &fa);
    ex_md_visit(b, 0, len + 1, collect, &fb);
    if (fa.n != fb.n) ok = 0;
    for (i = 0; ok && i < fa.n; i++)
        if (fa.v[i].kind != fb.v[i].kind || fa.v[i].start != fb.v[i].start || fa.v[i].end != fb.v[i].end || fa.v[i].level != fb.v[i].level || fa.v[i].flags != fb.v[i].flags) ok = 0;
    free(fa.v); free(fb.v);
    return ok;
}
