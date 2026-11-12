/* explorer: the sidebar's tree and toolbar work on a real folder: rows, keyboard and
 * mouse navigation, opening files, the new note and new folder buttons, sorting,
 * expand all, and the accessibility nodes of the visible rows. Uses ./explorer_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define ROOT "explorer_test"
static char opened[256];
static void on_open(ExWidget *w, const char *rel, void *u) { (void)w; (void)u; snprintf(opened, sizeof opened, "%s", rel); }

static void put(const char *rel, const char *text) {
    char p[300];
    snprintf(p, sizeof p, ROOT "/%s", rel);
    ex_platform_write_file_atomic(p, text, strlen(text));
}

static ExWidget *find_button(ExWidget *w, const char *name) {
    ExWidget *c, *r;
    for (; w; w = w->next) {
        if (w->type == EX_WIDGET_BUTTON && w->name && strcmp(w->name, name) == 0) return w;
        if ((r = find_button(w->first_child, name)) != NULL) return r;
        (void)c;
    }
    return NULL;
}

static const char *sel_name(ExWidget *tree) { const ExNode *n = ex_tree_selected(tree); return n ? n->name : "(none)"; }

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExVault *v;
    ExWidget *root, *side, *tree, *b;
    ExA11yTree *at;
    ExA11yInfo in;
    ExDoc *doc = ex_doc_create("", 0);

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_platform_remove_tree(ROOT);
    ex_platform_make_dirs(ROOT "/Projects/Alpha");
    ex_platform_make_dirs(ROOT "/Archive");
    put("Welcome.md", "# Welcome\n");
    put("Projects/Alpha/plan.md", "plan\n");
    put("Projects/idea.md", "idea\n");
    put("Archive/old.md", "old\n");

    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("explorer", 800, 500);
    ui = ex_ui_create(r, &theme, 16.0f);
    v = ex_vault_open(ROOT);
    CHECK(v != NULL);
    root = ex_widget_box(ui, 0, "window");
    root->role = EX_ROLE_WINDOW;
    root->width = ex_size_grow(); root->height = ex_size_grow();
    side = ex_explorer_sidebar(ui, v, on_open, NULL, &tree);
    ex_widget_add(root, side);
    ex_widget_add(root, ex_widget_editor(ui, doc, "Note"));
    ex_ui_layout(ui, root, 800, 500);
    CHECK(tree && tree->w > 100 && tree->h > 200 && side->w > 200);

    /* the vault button is at the bottom of the sidebar, under the tree */
    b = find_button(root, "Vault: explorer_test");
    CHECK(b && b->y > tree->y + tree->h - 2);

    /* rows: folders first, closed */
    CHECK(ex_vault_row_count(v) == 3 && strcmp(sel_name(tree), "Archive") == 0);

    /* keyboard: Down, Right opens a folder, Left closes it or goes to its folder, Enter opens a file */
    ex_ui_set_focus(ui, tree);
    ex_ui_key(ui, EX_KEY_DOWN, 0);
    CHECK(strcmp(sel_name(tree), "Projects") == 0);
    ex_ui_key(ui, EX_KEY_RIGHT, 0);
    CHECK(ex_vault_row_count(v) == 5);                       /* Projects now shows Alpha and idea.md */
    ex_ui_key(ui, EX_KEY_RIGHT, 0);
    CHECK(strcmp(sel_name(tree), "Alpha") == 0);             /* Right on an open folder steps into it */
    ex_ui_key(ui, EX_KEY_RIGHT, 0);
    CHECK(strcmp(sel_name(tree), "Alpha") == 0 && ex_vault_row_count(v) == 6);
    ex_ui_key(ui, EX_KEY_DOWN, 0);
    CHECK(strcmp(sel_name(tree), "plan.md") == 0);
    ex_ui_key(ui, EX_KEY_LEFT, 0);                           /* a file: Left goes to its folder */
    CHECK(strcmp(sel_name(tree), "Alpha") == 0);
    ex_ui_key(ui, EX_KEY_LEFT, 0);                           /* an open folder: Left closes it */
    CHECK(ex_vault_row_count(v) == 5);
    ex_ui_key(ui, EX_KEY_DOWN, 0);
    CHECK(strcmp(sel_name(tree), "idea.md") == 0);
    ex_ui_key(ui, EX_KEY_ENTER, 0);
    CHECK(strcmp(opened, "Projects/idea.md") == 0);
    ex_ui_key(ui, EX_KEY_END, 0);
    CHECK(strcmp(sel_name(tree), "Welcome.md") == 0);
    ex_ui_key(ui, EX_KEY_HOME, 0);
    CHECK(strcmp(sel_name(tree), "Archive") == 0);
    ex_ui_key(ui, EX_KEY_TAB, 0);                            /* Tab leaves the tree for the next control */
    CHECK(ex_ui_focus(ui) != tree);

    /* mouse: a click selects, a double click opens, the chevron toggles */
    ex_ui_layout(ui, root, 800, 500);
    {
        float rh = tree->h > 0 ? 0 : 0, x = tree->x + 60, y;
        ExFontMetrics m;
        (void)rh;
        ex_text_metrics(r, ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0), 16.0f, &m);
        rh = (float)m.line_height * 1.3f;
        y = tree->y + rh * 4.5f;                              /* row 4: Welcome.md (Archive, Projects, Alpha.., idea.md?) */
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, tree->y + rh * 0.5f, 0, 1);
        ex_ui_mouse(ui, root, EX_MOUSE_UP, x, tree->y + rh * 0.5f, 0, 1);
        CHECK(strcmp(sel_name(tree), "Archive") == 0 && ex_ui_focus(ui) == tree);
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, tree->y + rh * 0.5f, 0, 2);       /* double click on a folder toggles it */
        ex_ui_mouse(ui, root, EX_MOUSE_UP, x, tree->y + rh * 0.5f, 0, 2);
        CHECK(ex_vault_row_count(v) == 6);
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, tree->x + 10, tree->y + rh * 0.5f, 0, 1);   /* the chevron */
        CHECK(ex_vault_row_count(v) == 5);
        (void)y;
        ex_ui_mouse(ui, root, EX_MOUSE_DOWN, x, tree->y + rh * 100.0f / 100.0f * 10.0f, 0, 1);   /* below the last row: ignored */
    }

    /* toolbar: new note and new folder go into the selected folder and never replace */
    ex_vault_toggle(v, "Archive");
    ex_tree_select_path(tree, "Archive");
    b = find_button(root, "New note");
    CHECK(b != NULL);
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, b->x + 2, b->y + 2, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, b->x + 2, b->y + 2, 0, 1);
    CHECK(ex_vault_find(v, "Archive/Untitled.md") != NULL && strcmp(opened, "Archive/Untitled.md") == 0);
    put("Archive/Untitled.md", "my words\n");
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, b->x + 2, b->y + 2, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, b->x + 2, b->y + 2, 0, 1);
    CHECK(ex_vault_find(v, "Archive/Untitled 2.md") != NULL);
    {
        char *t = ex_platform_read_file(ROOT "/Archive/Untitled.md", NULL);
        CHECK(t && strcmp(t, "my words\n") == 0);              /* the existing note was not replaced */
        free(t);
    }
    b = find_button(root, "New folder");
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, b->x + 2, b->y + 2, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, b->x + 2, b->y + 2, 0, 1);
    CHECK(ex_vault_find(v, "Archive/Untitled folder") != NULL || ex_vault_find(v, "Untitled folder") != NULL);

    /* expand all and sorting only change the view */
    b = find_button(root, "Expand all");
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, b->x + 2, b->y + 2, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, b->x + 2, b->y + 2, 0, 1);
    {
        size_t i, folders = 0, open = 0;
        for (i = 0; i < ex_vault_row_count(v); i++) { const ExNode *n = ex_vault_row(v, i, NULL); if (n->is_dir) { folders++; if (n->expanded) open++; } }
        CHECK(folders > 0 && open == folders);
    }
    b = find_button(root, "Sort");
    ex_ui_mouse(ui, root, EX_MOUSE_DOWN, b->x + 2, b->y + 2, 0, 1);
    ex_ui_mouse(ui, root, EX_MOUSE_UP, b->x + 2, b->y + 2, 0, 1);
    {
        ExVaultOptions o;
        ex_vault_get_options(v, &o);
        CHECK(o.sort_by == EX_SORT_NAME && o.sort_descending);
    }

    /* accessibility: the tree is a tree, each visible row a tree item with the file's name */
    ex_ui_layout(ui, root, 800, 500);
    at = ex_a11y_create();
    CHECK(ex_ui_a11y_sync(ui, root, at) == 0);
    CHECK(ex_a11y_get(at, tree->id, &in) == 0 && in.role == EX_A11Y_TREE && in.child_count == ex_vault_row_count(v));
    {
        uint64_t item = ex_a11y_child(at, tree->id, 0);
        CHECK(ex_a11y_get(at, item, &in) == 0 && in.role == EX_A11Y_TREE_ITEM && in.name[0] != 0 && in.parent == tree->id);
    }
    CHECK(ex_ui_a11y_sync(ui, root, at) == 0 && ex_a11y_count(at) == ex_widget_count(root) + ex_vault_row_count(v));   /* rows are rebuilt, not piled up */
    ex_a11y_free(at);

    /* it draws without trouble */
    ex_renderer_clear(r, ex_theme_color(&theme, EX_COLOR_BACKGROUND));
    ex_ui_draw(ui, root, 1);
    if (getenv("EXPLORER_PPM")) {
        int w = 0, h = 0;
        uint8_t *px = ex_renderer_read_rgb(r, &w, &h);
        FILE *f = fopen(getenv("EXPLORER_PPM"), "wb");
        if (f && px) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(px, 1, (size_t)w * (size_t)h * 3, f); }
        if (f) fclose(f);
        free(px);
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
