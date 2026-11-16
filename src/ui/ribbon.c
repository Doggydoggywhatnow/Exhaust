/* The ribbon: see include/exhaust_ribbon.h. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_ribbon.h"
#include "exhaust_shell.h"

typedef struct Btn { ExRibbon *r; size_t item; ExWidget *w; } Btn;

struct ExRibbon {
    ExUi *ui;
    ExRibbonHooks hooks;
    ExWidget *bar;
    Btn btn[16];
    size_t n;
    int shown;
};

static const int k_icons[] = {
    EX_ICON_SEARCH, EX_ICON_GRAPH, EX_ICON_CANVAS, EX_ICON_PALETTE, EX_ICON_MODE, EX_ICON_SIDEBAR_LEFT, EX_ICON_SIDEBAR_RIGHT, EX_ICON_THEME, EX_ICON_GEAR
};

static void on_press(ExWidget *w, void *u) {
    Btn *b = u;
    (void)w;
    if (b->r->hooks.run) b->r->hooks.run(b->r->hooks.user, ex_ribbon_item(b->item)->id);
}

ExRibbon *ex_ribbon_create(ExUi *ui, const ExRibbonHooks *hooks) {
    ExRibbon *r = calloc(1, sizeof *r);
    size_t i, nitems = ex_ribbon_item_count();
    int gap_done = 0;
    if (!r) return NULL;
    r->ui = ui;
    if (hooks) r->hooks = *hooks;
    r->bar = ex_widget_box(ui, 1, "ribbon");
    if (!r->bar) { free(r); return NULL; }
    r->bar->width = ex_size_token(EX_SIZE_RIBBON_WIDTH, 1.0f);
    r->bar->height = ex_size_grow();
    r->bar->bg_token = EX_COLOR_BACKGROUND_ALT;
    r->bar->pad_x = 0.5f; r->bar->pad_y = 1.0f; r->bar->gap = 0.5f; r->bar->align_x = 1;     /* the icons sit in the middle of the bar */
    for (i = 0; i < nitems && i < 16; i++) {
        const ExRibbonItem *it = ex_ribbon_item(i);
        Btn *b = &r->btn[r->n];
        int stub = !strncmp(it->id, "canvas.", 7);
        if (!it->top && !gap_done) {          /* the actions at the top, the view switches at the bottom */
            ExWidget *gap = ex_widget_box(ui, 1, "ribbon space");
            if (gap) { gap->width = ex_size_grow(); gap->height = ex_size_grow(); ex_widget_add(r->bar, gap); }
            gap_done = 1;
        }
        b->r = r; b->item = i;
        b->w = ex_widget_button(ui, it->name, stub ? NULL : on_press, b);
        if (!b->w) continue;
        ex_widget_set_icon(b->w, k_icons[i]);
        b->w->bg_token = -1; b->w->border_token = -1;
        if (stub) { b->w->disabled = 1; b->w->focusable = 0; }
        ex_widget_add(r->bar, b->w);
        r->n++;
    }
    r->shown = 1;
    return r;
}

void ex_ribbon_free(ExRibbon *r) { free(r); }
ExWidget *ex_ribbon_widget(ExRibbon *r) { return r ? r->bar : NULL; }

ExWidget *ex_ribbon_button(ExRibbon *r, const char *command_id) {
    size_t i;
    if (!r || !command_id) return NULL;
    for (i = 0; i < r->n; i++) if (!strcmp(ex_ribbon_item(r->btn[i].item)->id, command_id)) return r->btn[i].w;
    return NULL;
}

void ex_ribbon_apply(ExRibbon *r, int show, const char *items, int graph_feature) {
    size_t i;
    if (!r) return;
    r->shown = show != 0;
    r->bar->hidden = !r->shown;
    for (i = 0; i < r->n; i++) {
        const char *id = ex_ribbon_item(r->btn[i].item)->id;
        r->btn[i].w->hidden = !ex_ribbon_shown(items, id) || (!strcmp(id, "graph-view.open-graph-view") && !graph_feature);
    }
}

int ex_ribbon_visible(const ExRibbon *r) { return r && r->shown; }
int ex_ribbon_button_visible(const ExRibbon *r, const char *command_id) {
    ExWidget *w = ex_ribbon_button((ExRibbon *)r, command_id);
    return r && r->shown && w && !w->hidden;
}
