/* tree_drag: a folder deep in a long tree can be dragged to the vault folder (ISSUES 16). The tree scrolls with the mouse wheel and by
 * itself while a row is dragged near its top or bottom edge, and there is always blank space under the last row to drop on. Uses ./tree_drag_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "tree_drag_test"
static char dropped_from[300], dropped_to[300] = "(none)";
static int drops;
static void on_drop(ExWidget *w, const char *from, const char *to, void *u) { (void)w; (void)u; snprintf(dropped_from, sizeof dropped_from, "%s", from); snprintf(dropped_to, sizeof dropped_to, "%s", to); drops++; }

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExVault *v;
    ExWidget *root, *side, *tree;
    ExDoc *doc = ex_doc_create("", 0);
    char p[300];
    int i;
    float x, y, rh;
    size_t row;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_platform_remove_tree(ROOT);
    ex_platform_make_dirs(ROOT "/0Deep/Inner");
    for (i = 0; i < 40; i++) { snprintf(p, sizeof p, ROOT "/Folder%02d", i); ex_platform_make_dirs(p); }   /* more folders than fit: the tree is longer than its view */
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("tree drag", 800, 500);
    ui = ex_ui_create(r, &theme, 16.0f);
    v = ex_vault_open(ROOT);
    CHECK(v != NULL);
    root = ex_widget_box(ui, 0, "window");
    root->role = EX_ROLE_WINDOW;
    root->width = ex_size_grow(); root->height = ex_size_grow();
    side = ex_explorer_sidebar(ui, v, NULL, NULL, &tree);
    ex_widget_add(root, side);
    ex_widget_add(root, ex_widget_editor(ui, doc, "Note"));
    ex_tree_set_drop_handler(tree, on_drop, NULL);
    ex_vault_toggle(v, "0Deep");                                  /* rows: 0Deep, Inner, Folder00 ... Folder39 */
    ex_ui_layout(ui, root, 800, 500);
    CHECK(ex_vault_row_count(v) == 42);
    {
        ExFontMetrics m;
        ex_text_metrics(r, ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0), 16.0f, &m);
        rh = (float)m.line_height * 1.45f;
    }
    CHECK((float)ex_vault_row_count(v) * rh > tree->h);          /* the tree really is longer than the view */

    /* the wheel scrolls the tree, and it stops with one blank row under the last */
    ex_tree_scroll(ui, tree, 100.0f);
    CHECK(tree->tree_scroll > 99.0f && tree->tree_scroll < 101.0f);
    ex_tree_scroll(ui, tree, 100000.0f);
    CHECK(tree->tree_scroll > 0 && tree->tree_scroll < (float)ex_vault_row_count(v) * rh + rh);
    CHECK((float)ex_vault_row_count(v) * rh - tree->tree_scroll < tree->h);   /* the last row is in view... */
    CHECK((float)ex_vault_row_count(v) * rh - tree->tree_scroll <= tree->h - rh + 1.0f);   /* ...with blank space under it */
    ex_tree_scroll(ui, tree, -100000.0f);
    CHECK(tree->tree_scroll == 0.0f);

    /* drag Inner (row 1) to the bottom edge: the tree scrolls by itself; release on the blank space: the vault folder */
    x = tree->x + 60.0f;
    y = tree->y + rh * 1.5f;
    row = 1;
    (void)row;
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, y, 0, 1);
    for (i = 0; i < 80; i++) ex_ui_mouse(ui, root, EX_MOUSE_DRAG, x, tree->y + tree->h - 2.0f, 0, 1);
    CHECK(tree->tree_scroll > 0.0f);                              /* it scrolled while the pointer waited at the bottom edge */
    ex_ui_mouse(ui, root, EX_MOUSE_UP, x, tree->y + tree->h - rh * 0.25f, 0, 1);   /* the blank row under the last */
    CHECK(drops == 1 && strcmp(dropped_from, "0Deep/Inner") == 0 && dropped_to[0] == 0);

    /* and the other way: scrolled to the end, dragging at the top edge scrolls back up */
    ex_tree_scroll(ui, tree, 100000.0f);
    {
        float before = tree->tree_scroll;
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, tree->y + rh * 2.0f, 0, 1);       /* a folder row in view */
        for (i = 0; i < 10; i++) ex_ui_mouse(ui, root, EX_MOUSE_DRAG, x, tree->y + 2.0f, 0, 1);
        CHECK(tree->tree_scroll < before);
        ex_ui_mouse(ui, root, EX_MOUSE_UP, x, tree->y + tree->h * 0.5f, 0, 1);
    }

    ex_widget_free(root);
    ex_vault_close(v);
    ex_doc_free(doc);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
    SDL_Quit();
    ex_platform_remove_tree(ROOT);
    return failures ? 1 : 0;
}
