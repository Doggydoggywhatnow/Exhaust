/* live_blocks: the rendered blocks of live preview (docs/formats.md, "Rendered blocks in live preview"): a table, a `$$` equation, a Mermaid diagram, a
 * picture on a line of its own and the properties are drawn as pictures while no caret is in them, take the rows of their height, and show their source
 * (and only theirs) while a caret is. Also: the setting off, source mode, properties hidden or as source, a click, a selection over a block, undo, and
 * that the text is never changed by drawing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_editor.h"
#include "exhaust_render.h"
#include "exhaust_text.h"
#include "exhaust_theme.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static const char *NOTE =
    "---\ntags: [a, b]\naliases:\n  - x\n---\n"            /* lines 0-4: properties */
    "# Title\n"                                              /* 5 */
    "\n"                                                     /* 6 */
    "| a | b |\n|---|---|\n| 1 | 2 |\n"                    /* 7-9: table */
    "\n"                                                     /* 10 */
    "$$\n\\frac{a}{b}\n$$\n"                                 /* 11-13: equation */
    "\n"                                                     /* 14 */
    "```mermaid\nflowchart LR\n  A --> B\n```\n"             /* 15-18: diagram */
    "\n"                                                     /* 19 */
    "![[pic.png]]\n"                                         /* 20: picture */
    "\n"                                                     /* 21 */
    "```js\nlet x;\n```\n"                                   /* 22-24: ordinary code: not a block */
    "\n"                                                     /* 25 */
    "end of the note";                                       /* 26 */

static int g_link_calls;
static char g_link_target[200];
static void link_seen(int kind, const char *target, void *user) { (void)kind; (void)user; g_link_calls++; snprintf(g_link_target, sizeof g_link_target, "%s", target); }

static void frame(ExEditor *e, ExRenderer *r, const ExTheme *th) {
    ex_renderer_clear(r, ex_theme_color(th, EX_COLOR_BACKGROUND));
    ex_editor_draw(e, r, th, 1, 1);
}
static void frames(ExEditor *e, ExRenderer *r, const ExTheme *th, int n) { while (n-- > 0) frame(e, r, th); }

static ExEditor *new_editor(ExRenderer *r, const char *text, int blocks, int props) {
    ExDoc *d = ex_doc_create(text, strlen(text));
    ExEditor *e = ex_editor_create(d);
    ex_editor_set_font(e, ex_font_builtin(ex_renderer_fonts(r), EX_FONT_UI_REGULAR), 16.0f, 150);
    ex_editor_set_viewport(e, 0, 0, 700, 4000);
    ex_editor_set_mode(e, EX_EDITOR_LIVE);
    ex_editor_set_wrap(e, 1);
    ex_editor_set_blocks(e, blocks, props);
    return e;
}
static void free_editor(ExEditor *e) { ExDoc *d = ex_editor_doc(e); ex_editor_free(e); ex_doc_free(d); }
static size_t offset_of_line(ExEditor *e, size_t line) { return ex_buffer_line_start(ex_doc_buffer(ex_editor_doc(e)), line); }
static void caret_line(ExEditor *e, size_t line) { ex_doc_set_caret(ex_editor_doc(e), offset_of_line(e, line)); }
static char *text_of(ExEditor *e) { return ex_buffer_text(ex_doc_buffer(ex_editor_doc(e)), NULL); }

int main(void) {
    ExRenderer *r;
    ExTheme th;
    ExEditor *e;
    size_t total_lines, rows_off, rows_on;
    char *before, *after;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    r = ex_renderer_create("blocks", 800, 4000);
    if (!r) return 1;
    ex_theme_init_builtin(&th, EX_THEME_DARK);

    /* off: no blocks, every line a row */
    e = new_editor(r, NOTE, 0, 0);
    ex_doc_set_caret(ex_editor_doc(e), strlen(NOTE));
    frames(e, r, &th, 2);
    CHECK(ex_editor_block_count(e) == 0);
    total_lines = ex_buffer_line_count(ex_doc_buffer(ex_editor_doc(e)));
    rows_off = ex_editor_row_count(e);
    CHECK(rows_off == total_lines);
    free_editor(e);

    /* on, the caret in the last paragraph: the properties, table, equation, diagram and picture are blocks, the js code is not */
    e = new_editor(r, NOTE, 1, 0);
    ex_doc_set_caret(ex_editor_doc(e), strlen(NOTE));
    frames(e, r, &th, 6);                                                   /* a few frames: the equation and the diagram are made a pass at a time */
    CHECK(ex_editor_block_count(e) == 5);
    CHECK(ex_editor_blocks_drawn(e) == 5);
    rows_on = ex_editor_row_count(e);
    CHECK(rows_on < rows_off);                                              /* the lines after the first of each block are hidden */
    CHECK(ex_editor_line_hidden(e, 1) && ex_editor_line_hidden(e, 8) && ex_editor_line_hidden(e, 12) && ex_editor_line_hidden(e, 16));
    CHECK(!ex_editor_line_hidden(e, 0) && !ex_editor_line_hidden(e, 7) && !ex_editor_line_hidden(e, 11) && !ex_editor_line_hidden(e, 15) && !ex_editor_line_hidden(e, 20));
    CHECK(!ex_editor_line_hidden(e, 23));                                   /* ordinary code stays text */
    before = text_of(e);

    /* a block that is drawn takes more than one row (its height in rows, not one row for each of its lines) */
    {
        float h = ex_editor_content_height(e, r);
        CHECK(h > 0.0f);
    }

    /* the caret in the table: only the table shows its source */
    caret_line(e, 8);
    frames(e, r, &th, 2);
    CHECK(ex_editor_blocks_drawn(e) == 4);
    CHECK(!ex_editor_line_hidden(e, 8) && !ex_editor_line_hidden(e, 9));
    CHECK(ex_editor_line_hidden(e, 12) && ex_editor_line_hidden(e, 16));
    /* back out: drawn again */
    caret_line(e, 26);
    frames(e, r, &th, 2);
    CHECK(ex_editor_blocks_drawn(e) == 5);
    CHECK(ex_editor_line_hidden(e, 8));

    /* the caret on the first line of the note (inside the properties' first line) does not show their source */
    caret_line(e, 0);
    frames(e, r, &th, 2);
    CHECK(ex_editor_blocks_drawn(e) == 5);
    caret_line(e, 2);                                                       /* but a property line does */
    frames(e, r, &th, 2);
    CHECK(ex_editor_blocks_drawn(e) == 4);
    CHECK(!ex_editor_line_hidden(e, 1) && !ex_editor_line_hidden(e, 3));

    /* a selection from before a block to after it keeps it drawn; a selection that ends in it shows it */
    {
        ExSel s;
        s.anchor = offset_of_line(e, 6); s.head = offset_of_line(e, 14);
        ex_doc_set_selections(ex_editor_doc(e), &s, 1);
        frames(e, r, &th, 2);
        CHECK(ex_editor_blocks_drawn(e) == 5);                              /* the table and the equation are spanned, no end is in a block */
        CHECK(ex_editor_line_hidden(e, 8) && ex_editor_line_hidden(e, 12));
        s.anchor = offset_of_line(e, 6); s.head = offset_of_line(e, 12);     /* ends in the equation: its source shows */
        ex_doc_set_selections(ex_editor_doc(e), &s, 1);
        frames(e, r, &th, 2);
        CHECK(ex_editor_blocks_drawn(e) == 4);
        CHECK(!ex_editor_line_hidden(e, 12));
    }

    /* a click on a drawn block puts the caret in its source: the first line of a table */
    caret_line(e, 26);
    frames(e, r, &th, 3);
    {
        float x, y, w, h;
        ex_editor_caret_rect(e, r, &x, &y, &w, &h);
        (void)x; (void)w; (void)h;
        ex_editor_mouse(e, r, EX_MOUSE_DOWN, 120.0f, 0.0f, 0, 1);              /* the very first row: the properties */
        ex_editor_mouse(e, r, EX_MOUSE_UP, 120.0f, 0.0f, 0, 1);
        frames(e, r, &th, 2);
        {
            ExSel s = ex_doc_sel(ex_editor_doc(e), 0);
            size_t line = ex_buffer_line_of_offset(ex_doc_buffer(ex_editor_doc(e)), s.head);
            CHECK(line == 1);                                                /* the first property line */
        }
        CHECK(ex_editor_blocks_drawn(e) == 4);
    }

    /* drawing and moving the caret never changed the text */
    after = text_of(e);
    CHECK(strcmp(before, after) == 0);
    free(before); free(after);

    /* typing inside a revealed block works as ever, and the block is a block again when the caret leaves */
    caret_line(e, 9);
    frames(e, r, &th, 2);
    ex_doc_set_caret(ex_editor_doc(e), offset_of_line(e, 9) + 2);
    ex_editor_text_input(e, "Z", 1);
    after = text_of(e);
    CHECK(strstr(after, "| Z1 | 2 |") != NULL);
    free(after);
    caret_line(e, 26);
    frames(e, r, &th, 4);
    CHECK(ex_editor_blocks_drawn(e) == 5);
    CHECK(ex_doc_undo(ex_editor_doc(e)));
    free_editor(e);

    /* properties hidden: their lines take no row; the caret on them shows the text */
    e = new_editor(r, NOTE, 1, 1);
    ex_doc_set_caret(ex_editor_doc(e), strlen(NOTE));
    frames(e, r, &th, 6);
    CHECK(ex_editor_line_hidden(e, 0) && ex_editor_line_hidden(e, 4));
    CHECK(ex_editor_blocks_drawn(e) == 4);
    caret_line(e, 0);
    frames(e, r, &th, 2);
    CHECK(!ex_editor_line_hidden(e, 0));
    free_editor(e);

    /* properties as source: not a block at all */
    e = new_editor(r, NOTE, 1, 2);
    ex_doc_set_caret(ex_editor_doc(e), strlen(NOTE));
    frames(e, r, &th, 6);
    CHECK(ex_editor_block_count(e) == 4);
    CHECK(!ex_editor_line_hidden(e, 1));
    free_editor(e);

    /* source mode and a field that does not wrap: no blocks */
    e = new_editor(r, NOTE, 1, 0);
    ex_editor_set_mode(e, EX_EDITOR_SOURCE);
    frames(e, r, &th, 2);
    CHECK(ex_editor_block_count(e) == 0);
    ex_editor_set_mode(e, EX_EDITOR_LIVE);
    ex_editor_set_wrap(e, 0);
    frames(e, r, &th, 2);
    CHECK(ex_editor_block_count(e) == 0);
    free_editor(e);

    /* an image that is not alone on its line, and a one line equation */
    e = new_editor(r, "text ![[a.png]] more\n\n$$x^2$$\n\nend", 1, 0);
    ex_doc_set_caret(ex_editor_doc(e), strlen("text ![[a.png]] more\n\n$$x^2$$\n\nend"));
    frames(e, r, &th, 4);
    CHECK(ex_editor_block_count(e) == 1);                                    /* only the equation */
    free_editor(e);

    /* a link inside a drawn table takes the click (as in reading mode) and the block stays drawn; a click on plain text opens the source */
    {
        static const char *T = "| a | b |\n|---|---|\n| [[Target]] | plain |\n\nend";
        float x, y;
        int found = 0;
        e = new_editor(r, T, 1, 0);
        ex_editor_set_block_link(e, link_seen, NULL);
        ex_doc_set_caret(ex_editor_doc(e), strlen(T));
        frames(e, r, &th, 4);
        CHECK(ex_editor_blocks_drawn(e) == 1);
        for (y = 4.0f; y < 140.0f && !found; y += 6.0f)
            for (x = 20.0f; x < 400.0f && !found; x += 6.0f) {
                g_link_calls = 0;
                ex_editor_mouse(e, r, EX_MOUSE_DOWN, x, y, 0, 1);
                ex_editor_mouse(e, r, EX_MOUSE_UP, x, y, 0, 1);
                if (g_link_calls) found = 1;
                else { caret_line(e, 4); frames(e, r, &th, 2); }                 /* a miss opened the source: back out */
            }
        CHECK(found);
        CHECK(strcmp(g_link_target, "Target") == 0);
        frames(e, r, &th, 2);
        CHECK(ex_editor_blocks_drawn(e) == 1);                                 /* the click went to the link, the caret did not move into the table */
        {
            ExSel sel = ex_doc_sel(ex_editor_doc(e), 0);
            CHECK(ex_buffer_line_of_offset(ex_doc_buffer(ex_editor_doc(e)), sel.head) == 4);
        }
        free_editor(e);
    }

    ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("live_blocks: ok\n");
    return 0;
}
