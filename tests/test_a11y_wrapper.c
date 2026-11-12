/* a11y_wrapper: a tree built through the wrapper reads back with the same roles,
 * names and parents; the widget tree maps to exactly one node per widget; the
 * snapshot and (when AccessKit is built in) the AccessKit update carry every node. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_a11y.h"
#include "exhaust_ui.h"

#ifdef EX_HAVE_A11Y
#include "accesskit.h"
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void test_tree(void) {
    ExA11yTree *t = ex_a11y_create();
    ExA11yInfo in;
    uint64_t order[8];
    char *dump;
    CHECK(ex_a11y_set_node(t, 1, 0, EX_A11Y_WINDOW, "Exhaust") == 0 && ex_a11y_root(t) == 1);
    CHECK(ex_a11y_set_node(t, 2, 1, EX_A11Y_GROUP, "Sidebar") == 0);
    CHECK(ex_a11y_set_node(t, 3, 2, EX_A11Y_BUTTON, "New note") == 0);
    CHECK(ex_a11y_set_node(t, 4, 1, EX_A11Y_TEXT_INPUT, "Note text") == 0);
    CHECK(ex_a11y_set_node(t, 5, 2, EX_A11Y_LABEL, "Files") == 0);
    CHECK(ex_a11y_count(t) == 5);

    CHECK(ex_a11y_get(t, 3, &in) == 0 && in.parent == 2 && in.role == EX_A11Y_BUTTON && strcmp(in.name, "New note") == 0 && in.child_count == 0);
    CHECK(ex_a11y_get(t, 1, &in) == 0 && in.parent == 0 && in.child_count == 2 && ex_a11y_child(t, 1, 0) == 2 && ex_a11y_child(t, 1, 1) == 4);
    CHECK(ex_a11y_child(t, 2, 0) == 3 && ex_a11y_child(t, 2, 1) == 5 && ex_a11y_child(t, 2, 2) == 0);
    CHECK(ex_a11y_get(t, 99, &in) == -1);

    /* bad requests change nothing */
    CHECK(ex_a11y_set_node(t, 0, 1, EX_A11Y_GROUP, "zero id") == -1);
    CHECK(ex_a11y_set_node(t, 6, 77, EX_A11Y_GROUP, "no such parent") == -1);
    CHECK(ex_a11y_set_node(t, 7, 0, EX_A11Y_GROUP, "second root") == -1);
    CHECK(ex_a11y_set_node(t, 2, 3, EX_A11Y_GROUP, "cycle") == -1);   /* 3 is below 2 */
    CHECK(ex_a11y_set_node(t, 2, 2, EX_A11Y_GROUP, "self") == -1);
    CHECK(ex_a11y_set_node(t, 6, 1, (ExA11yRole)99, "bad role") == -1);
    CHECK(ex_a11y_count(t) == 5);

    /* actions, value, bounds, focus */
    CHECK(ex_a11y_set_focus(t, 3) == -1);                       /* cannot take focus yet */
    CHECK(ex_a11y_set_actions(t, 3, EX_A11Y_ACTION_CLICK | EX_A11Y_ACTION_FOCUS) == 0);
    CHECK(ex_a11y_set_actions(t, 4, EX_A11Y_ACTION_FOCUS) == 0);
    CHECK(ex_a11y_set_bounds(t, 3, 10, 20, 100, 30) == 0 && ex_a11y_set_value(t, 4, "hello") == 0);
    CHECK(ex_a11y_get(t, 3, &in) == 0 && in.x == 10 && in.y == 20 && in.w == 100 && in.h == 30 && in.actions == 3);
    CHECK(ex_a11y_get(t, 4, &in) == 0 && strcmp(in.value, "hello") == 0);
    CHECK(ex_a11y_set_focus(t, 4) == 0 && ex_a11y_focus(t) == 4);
    CHECK(ex_a11y_focus_order(t, order, 8) == 2 && order[0] == 3 && order[1] == 4);

    /* renaming and moving */
    CHECK(ex_a11y_set_node(t, 3, 1, EX_A11Y_BUTTON, "Create note") == 0);
    CHECK(ex_a11y_get(t, 3, &in) == 0 && in.parent == 1 && strcmp(in.name, "Create note") == 0);
    CHECK(ex_a11y_child(t, 2, 0) == 5 && ex_a11y_child(t, 1, 2) == 3);
    CHECK(ex_a11y_focus_order(t, order, 8) == 2 && order[0] == 4 && order[1] == 3); /* tree order changed */

    dump = ex_a11y_dump(t);
    CHECK(dump && strstr(dump, "window \"Exhaust\"\n  group \"Sidebar\"\n    label \"Files\"\n  text-input \"Note text\" [focusable] [focused] value=\"hello\"\n  button \"Create note\" [click] [focusable]\n") == dump);
    free(dump);

    /* removing a node removes its subtree and the focus if it was inside */
    CHECK(ex_a11y_remove(t, 4) == 0 && ex_a11y_focus(t) == 0 && ex_a11y_count(t) == 4);
    CHECK(ex_a11y_remove(t, 2) == 0 && ex_a11y_count(t) == 2 && ex_a11y_get(t, 5, &in) == -1);
    CHECK(ex_a11y_remove(t, 99) == -1);
    CHECK(ex_a11y_remove(t, 1) == 0 && ex_a11y_count(t) == 0 && ex_a11y_root(t) == 0);
    CHECK(ex_a11y_set_node(t, 1, 0, EX_A11Y_WINDOW, "again") == 0);   /* a new root is allowed after that */
    ex_a11y_free(t);
}

static int clicks;
static void on_click(ExWidget *w, void *u) { (void)w; (void)u; clicks++; }

static void test_widgets(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExDoc *doc = ex_doc_create("text", 4);
    ExWidget *root, *box, *label, *button, *editor;
    ExA11yTree *t = ex_a11y_create();
    ExA11yInfo in;
    size_t before;

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    ex_theme_init_builtin(&theme, EX_THEME_DARK);
    r = ex_renderer_create("a11y", 400, 300);
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "Exhaust");
    root->role = EX_ROLE_WINDOW;
    root->width = ex_size_grow(); root->height = ex_size_grow();
    box = ex_widget_box(ui, 0, "Toolbar");
    label = ex_widget_label(ui, "Status line");
    button = ex_widget_button(ui, "Save", on_click, NULL);
    editor = ex_widget_editor(ui, doc, "Note text");
    ex_widget_add(box, label);
    ex_widget_add(box, button);
    ex_widget_add(root, box);
    ex_widget_add(root, editor);
    ex_ui_layout(ui, root, 400, 300);
    ex_ui_set_focus(ui, editor);

    /* exactly one node for each widget, with the widget's id */
    CHECK(ex_ui_a11y_sync(ui, root, t) == 0);
    CHECK(ex_a11y_count(t) == ex_widget_count(root) && ex_a11y_count(t) == 5);
    CHECK(ex_a11y_root(t) == root->id);
    CHECK(ex_a11y_get(t, root->id, &in) == 0 && in.role == EX_A11Y_WINDOW && strcmp(in.name, "Exhaust") == 0 && in.parent == 0 && in.child_count == 2);
    CHECK(ex_a11y_get(t, box->id, &in) == 0 && in.role == EX_A11Y_GROUP && in.parent == root->id && in.child_count == 2);
    CHECK(ex_a11y_get(t, label->id, &in) == 0 && in.role == EX_A11Y_LABEL && strcmp(in.name, "Status line") == 0 && in.parent == box->id && in.actions == 0);
    CHECK(ex_a11y_get(t, button->id, &in) == 0 && in.role == EX_A11Y_BUTTON && strcmp(in.name, "Save") == 0 &&
          in.actions == (EX_A11Y_ACTION_CLICK | EX_A11Y_ACTION_FOCUS) && in.w > 0 && in.h > 0);
    CHECK(ex_a11y_get(t, editor->id, &in) == 0 && in.role == EX_A11Y_TEXT_INPUT && strcmp(in.name, "Note text") == 0 &&
          strcmp(in.value, "text") == 0 && in.parent == root->id && (in.actions & EX_A11Y_ACTION_FOCUS) && !(in.actions & EX_A11Y_ACTION_CLICK));
    CHECK(ex_a11y_focus(t) == editor->id);
    {
        uint64_t order[8];
        CHECK(ex_a11y_focus_order(t, order, 8) == 2 && order[0] == button->id && order[1] == editor->id);
    }

    /* changes show up on the next sync: text, focus, new widget, removed widget */
    ex_ui_set_focus(ui, button);
    ex_doc_set_caret(doc, 4);
    ex_ui_text_input(ui, "x", 1);          /* focus is on the button, so the text does not change */
    ex_ui_set_focus(ui, editor);
    ex_ui_text_input(ui, "!", 1);
    ex_widget_set_text(label, "Saved");
    CHECK(ex_ui_a11y_sync(ui, root, t) == 0);
    CHECK(ex_a11y_get(t, editor->id, &in) == 0 && strcmp(in.value, "text!") == 0);
    CHECK(ex_a11y_get(t, label->id, &in) == 0 && in.role == EX_A11Y_LABEL);
    {
        ExWidget *extra = ex_widget_button(ui, "Undo", on_click, NULL);
        ex_widget_add(box, extra);
        ex_ui_layout(ui, root, 400, 300);
        before = ex_a11y_count(t);
        CHECK(ex_ui_a11y_sync(ui, root, t) == 0 && ex_a11y_count(t) == before + 1 && ex_a11y_get(t, extra->id, &in) == 0 && in.parent == box->id);
    }
    /* a node whose widget is gone is removed by the next sync */
    {
        ExWidget *w = box->first_child, *prev = NULL;
        uint64_t gone = button->id;
        while (w && w != button) { prev = w; w = w->next; }
        if (prev) prev->next = button->next; else box->first_child = button->next;
        if (box->last_child == button) box->last_child = prev;
        button->next = NULL; button->parent = NULL;
        CHECK(ex_ui_a11y_sync(ui, root, t) == 0 && ex_a11y_get(t, gone, &in) == -1 && ex_a11y_count(t) == ex_widget_count(root));
        ex_widget_free(button);
    }

    /* a hidden widget has no node, is skipped by Tab, and comes back when shown */
    {
        ExWidget *tgt = box->first_child; /* "Status line" label has no focus; use the editor for the focus order */
        size_t cnt = ex_a11y_count(t);
        (void)tgt;
        editor->hidden = 1;
        CHECK(ex_ui_a11y_sync(ui, root, t) == 0 && ex_a11y_get(t, editor->id, &in) == -1 && ex_a11y_count(t) == cnt - 1);
        editor->hidden = 0;
        ex_ui_layout(ui, root, 400, 300);
        CHECK(ex_ui_a11y_sync(ui, root, t) == 0 && ex_a11y_get(t, editor->id, &in) == 0 && ex_a11y_count(t) == cnt);
    }

    /* the snapshot for the operating system's thread is a faithful copy */
    {
        ExA11ySnapshot *s = ex_a11y_snapshot(t), *s2;
        CHECK(s && ex_a11y_snapshot_count(s) == ex_a11y_count(t));
        s2 = ex_a11y_snapshot_ref(s);
        ex_a11y_snapshot_unref(s);
        ex_a11y_set_node(t, 9999, root->id, EX_A11Y_GROUP, "later change");      /* does not affect the copy */
        CHECK(ex_a11y_snapshot_count(s2) == ex_a11y_count(t) - 1);
#ifdef EX_HAVE_A11Y
        {
            struct accesskit_tree_update *u = ex_a11y_build_update(s2);
            char *text;
            CHECK(u != NULL);
            if (u) {
                text = accesskit_tree_update_debug(u);
                CHECK(text && strstr(text, "Note text") != NULL && strstr(text, "Toolbar") != NULL && strstr(text, "Undo") != NULL);
                CHECK(text && strstr(text, "text!") != NULL);
                if (text) accesskit_string_free(text);
                accesskit_tree_update_free(u);
            }
        }
#endif
        ex_a11y_snapshot_unref(s2);
    }

    ex_widget_free(root);
    ex_a11y_free(t);
    ex_doc_free(doc);
    ex_ui_free(ui);
    ex_theme_free(&theme);
    ex_renderer_destroy(r);
}

int main(void) {
    test_tree();
    test_widgets();
    SDL_Quit();
    return failures ? 1 : 0;
}
