/* a11y_adapters (sub-slice 7h): the path between the operating system's screen reader and the widgets, everything of it that does not need a
 * screen reader. An adapter with no platform part stands in for the operating system: an action it is handed (as AccessKit's callback on the
 * operating system's thread would hand it) reaches the right widget on the interface thread (a press runs the button's handler, a focus request
 * moves the focus), actions for widgets that are gone, hidden or disabled do nothing, the queue is bounded, the tree is sent only when a
 * screen reader is attached, and the adapter of a real window under the dummy video driver is inert. The part with a real client of the
 * platform's accessibility interface (AT-SPI, UI Automation, NSAccessibility) is not here: see docs/PROGRESS.md and docs/ISSUES.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_a11y_adapter.h"
#include "exhaust_ui.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int g_presses; static ExWidget *g_pressed;
static void on_press(ExWidget *w, void *u) { (void)u; g_presses++; g_pressed = w; }

int main(void) {
    ExTheme theme;
    ExRenderer *r;
    ExUi *ui;
    ExWidget *root, *ok, *cancel, *hidden, *off, *ed, *label;
    ExA11yAdapter *ad, *real;
    ExA11yActionReq req;
    size_t i;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(ex_theme_init_builtin(&theme, EX_THEME_DARK) == 0);
    r = ex_renderer_create_hidden("a11y adapters", 400, 300);
    if (!r) { fprintf(stderr, "renderer: %s\n", ex_render_error()); return 1; }
    ui = ex_ui_create(r, &theme, 16.0f);
    root = ex_widget_box(ui, 1, "Exhaust"); root->role = EX_ROLE_WINDOW;
    ok = ex_widget_button(ui, "OK", on_press, NULL);
    cancel = ex_widget_button(ui, "Cancel", on_press, NULL);
    hidden = ex_widget_button(ui, "Hidden", on_press, NULL); hidden->hidden = 1;
    off = ex_widget_button(ui, "Off", on_press, NULL); off->disabled = 1;
    label = ex_widget_label(ui, "Just text");
    { ExDoc *d = ex_doc_create("note text", 9); ed = ex_widget_editor(ui, d, "Note text"); }
    ex_widget_add(root, ok); ex_widget_add(root, cancel); ex_widget_add(root, hidden); ex_widget_add(root, off); ex_widget_add(root, label); ex_widget_add(root, ed);
    ex_ui_layout(ui, root, 400, 300);

    ad = ex_a11y_adapter_create_inert();
    CHECK(ad != NULL && ex_a11y_adapter_tree(ad) != NULL && !ex_a11y_adapter_active(ad));
    CHECK(!ex_a11y_adapter_wants_update(ad, 1000) && !ex_a11y_adapter_wants_update(ad, 5000));          /* no screen reader attached: the tree is not built */

    /* the tree is made from the widgets and committed (the snapshot is what an activation would be given) */
    CHECK(ex_ui_a11y_sync(ui, root, ex_a11y_adapter_tree(ad)) == 0);
    {   ExA11yInfo in;
        CHECK(ex_a11y_get(ex_a11y_adapter_tree(ad), ok->id, &in) == 0 && in.role == EX_A11Y_BUTTON && !strcmp(in.name, "OK") && (in.actions & EX_A11Y_ACTION_CLICK));
        CHECK(ex_a11y_get(ex_a11y_adapter_tree(ad), hidden->id, &in) == -1);                           /* a hidden widget is not in the tree */ }
    CHECK(ex_a11y_adapter_commits(ad) == 0);
    ex_a11y_adapter_commit(ad);
    CHECK(ex_a11y_adapter_commits(ad) == 1);

    /* a press and a focus request, as the operating system's thread makes them */
    CHECK(!ex_a11y_adapter_poll(ad, &req));
    ex_a11y_adapter_inject(ad, ok->id, EX_A11Y_ACTION_CLICK);
    ex_a11y_adapter_inject(ad, cancel->id, EX_A11Y_ACTION_CLICK);
    ex_a11y_adapter_inject(ad, ed->id, EX_A11Y_ACTION_FOCUS);
    g_presses = 0;
    CHECK(ex_a11y_adapter_poll(ad, &req) && req.id == ok->id && req.action == EX_A11Y_ACTION_CLICK);
    CHECK(ex_ui_a11y_action(ui, root, req.id, req.action) == 0 && g_presses == 1 && g_pressed == ok);
    CHECK(ex_a11y_adapter_poll(ad, &req) && ex_ui_a11y_action(ui, root, req.id, req.action) == 0 && g_presses == 2 && g_pressed == cancel);   /* in the order they came */
    CHECK(ex_a11y_adapter_poll(ad, &req) && req.action == EX_A11Y_ACTION_FOCUS && ex_ui_a11y_action(ui, root, req.id, req.action) == 0 && ex_ui_focus(ui) == ed);
    CHECK(!ex_a11y_adapter_poll(ad, &req));

    /* what cannot be done is refused and changes nothing */
    g_presses = 0;
    CHECK(ex_ui_a11y_action(ui, root, hidden->id, EX_A11Y_ACTION_CLICK) == -1);                       /* hidden */
    CHECK(ex_ui_a11y_action(ui, root, off->id, EX_A11Y_ACTION_CLICK) == -1);                          /* disabled */
    CHECK(ex_ui_a11y_action(ui, root, 987654321u, EX_A11Y_ACTION_CLICK) == -1);                       /* no such widget */
    CHECK(ex_ui_a11y_action(ui, root, label->id, EX_A11Y_ACTION_CLICK) == -1);                        /* not a button */
    CHECK(ex_ui_a11y_action(ui, root, label->id, EX_A11Y_ACTION_FOCUS) == -1);                        /* a label cannot take the focus */
    CHECK(ex_ui_a11y_action(ui, root, 0x100000005ull, EX_A11Y_ACTION_CLICK) == -1);                   /* rows of a list or tree are not pressed from here yet */
    CHECK(g_presses == 0);
    CHECK(ex_ui_focus(ui) == ed);

    /* the queue holds a bounded number of requests: a screen reader that floods it cannot grow memory */
    for (i = 0; i < 1000; i++) ex_a11y_adapter_inject(ad, ok->id, EX_A11Y_ACTION_CLICK);
    { size_t n = 0; while (ex_a11y_adapter_poll(ad, &req)) n++; CHECK(n > 0 && n <= 64); }

    /* the adapter of a real window under the dummy video driver has no platform part: it never becomes active and costs nothing */
    real = ex_a11y_adapter_create(ex_renderer_window(r));
    CHECK(real != NULL && !ex_a11y_adapter_active(real) && !ex_a11y_adapter_wants_update(real, 1000));
    ex_a11y_adapter_set_window_bounds(real, 10, 20, 400, 300);                                        /* harmless */
    ex_a11y_adapter_inject(real, ok->id, EX_A11Y_ACTION_CLICK);
    CHECK(ex_a11y_adapter_poll(real, &req) && req.id == ok->id);

    ex_a11y_adapter_free(real); ex_a11y_adapter_free(ad);
    ex_a11y_adapter_free(NULL);
    ex_widget_free(root);
    ex_ui_free(ui); ex_theme_free(&theme); ex_renderer_destroy(r);
    if (failures) { fprintf(stderr, "a11y_adapters: %d failed\n", failures); return 1; }
    printf("a11y_adapters: ok\n");
    return 0;
}
