#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"
#include "exhaust_editor.h"
#include "exhaust_graphpanel.h"

#define NSEC 4
#define NSLIDER 7

enum { SEC_FILTERS, SEC_GROUPS, SEC_DISPLAY, SEC_FORCES };

typedef struct SliderDef { const char *name; float min, max, step; } SliderDef;
static const SliderDef sliders[NSLIDER] = {
    {"Text fade threshold", -3.0f, 3.0f, 0.1f}, {"Node size", 0.3f, 3.0f, 0.1f}, {"Link thickness", 0.3f, 4.0f, 0.1f},
    {"Center force", 0.0f, 1.0f, 0.05f}, {"Repel force", 0.0f, 20.0f, 0.5f}, {"Link force", 0.0f, 1.0f, 0.05f}, {"Link distance", 10.0f, 200.0f, 5.0f}
};
enum { S_FADE, S_NODE, S_LINK, S_CENTER, S_REPEL, S_LINKF, S_DIST };

static const int slot_token[6] = {EX_COLOR_ACCENT, EX_COLOR_SUCCESS, EX_COLOR_WARNING, EX_COLOR_ERROR, EX_COLOR_TAG, EX_COLOR_LINK};
static const char *slot_name[6] = {"accent", "green", "yellow", "red", "tag", "link"};

struct ExGraphPanel {
    ExUi *ui;
    ExGraphModel *m;
    ExGraphPanelChanged changed;
    void *user;
    ExGraphOptions o;                    /* what the controls show */
    ExWidget *graph, *gear, *panel, *col;
    ExWidget *head[NSEC], *body[NSEC];
    ExWidget *b_orphans, *b_unres, *b_attach, *b_arrows, *b_new_group;
    ExWidget *search_f, *group_row[EX_GRAPH_MAX_GROUPS], *group_f[EX_GRAPH_MAX_GROUPS], *group_c[EX_GRAPH_MAX_GROUPS];
    ExWidget *slider[NSLIDER], *value[NSLIDER];
    ExDoc *search_doc, *group_doc[EX_GRAPH_MAX_GROUPS];
    unsigned long search_ver, group_ver[EX_GRAPH_MAX_GROUPS];
    int shown_groups;
    int open_sec[NSEC];
    int open;
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void set_name(ExWidget *w, const char *name) { char *n = dup_str(name); if (n) { free(w->name); w->name = n; } }

static char *field_text(ExDoc *d) {
    char *t = ex_buffer_text(ex_doc_buffer(d), NULL), *nl;
    if (!t) return NULL;
    nl = strpbrk(t, "\r\n");
    if (nl) *nl = 0;
    return t;
}

static void set_field(ExDoc *d, const char *text) {
    const ExBuffer *b = ex_doc_buffer(d);
    ExChange c;
    ExSel all;
    c.offset = 0; c.remove_len = ex_buffer_size(b); c.text = text; c.text_len = strlen(text);
    ex_doc_apply(d, &c, 1, 0, NULL, 0);
    ex_doc_seal(d);
    all.anchor = 0; all.head = strlen(text);
    ex_doc_set_selections(d, &all, 1);
}

static void commit(ExGraphPanel *p) {
    int filters;
    ex_graphopts_clamp(&p->o);
    filters = ex_graphmodel_set_options(p->m, &p->o);
    if (p->changed) p->changed(p->user, &p->o, filters);
}

/* ---- showing the state ---- */

static void show_toggle(ExWidget *b, int on, const char *what) {
    char name[100];
    ex_widget_set_text(b, on ? "On" : "Off");
    b->bg_token = on ? EX_COLOR_ACCENT : EX_COLOR_BACKGROUND;
    snprintf(name, sizeof name, "%s: %s", what, on ? "on" : "off");
    set_name(b, name);
}

static void show_value(ExGraphPanel *p, int i, float v) {
    char b[24];
    snprintf(b, sizeof b, "%.3g", (double)v);
    ex_widget_set_text(p->value[i], b);
}

static void show_group_color(ExGraphPanel *p, int i) {
    char name[100];
    p->group_c[i]->bg_token = slot_token[p->o.groups[i].color % 6];
    snprintf(name, sizeof name, "Group %d colour: %s", i + 1, slot_name[p->o.groups[i].color % 6]);
    set_name(p->group_c[i], name);
}

static void show_sections(ExGraphPanel *p) {
    static const char *titles[NSEC] = {"Filters", "Groups", "Display", "Forces"};
    int i;
    char name[60];
    for (i = 0; i < NSEC; i++) {
        p->body[i]->hidden = !p->open_sec[i];
        p->head[i]->selected = p->open_sec[i];
        p->head[i]->icon = p->open_sec[i] ? EX_ICON_CHEVRON_DOWN : EX_ICON_CHEVRON_RIGHT;          /* the chevron shows whether it is open */
        snprintf(name, sizeof name, "%s, %s", titles[i], p->open_sec[i] ? "expanded" : "collapsed");
        set_name(p->head[i], name);
    }
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) p->group_row[i]->hidden = i >= p->shown_groups;
    p->b_new_group->hidden = p->shown_groups >= EX_GRAPH_MAX_GROUPS;
}

static void show_all(ExGraphPanel *p) {
    int i;
    show_toggle(p->b_orphans, p->o.show_orphans, "Orphans"); show_toggle(p->b_unres, p->o.show_unresolved, "Unresolved links");
    show_toggle(p->b_attach, p->o.show_attachments, "Attachments"); show_toggle(p->b_arrows, p->o.arrows, "Arrows");
    ex_slider_set_value(p->slider[S_FADE], p->o.text_fade); ex_slider_set_value(p->slider[S_NODE], p->o.node_size);
    ex_slider_set_value(p->slider[S_LINK], p->o.link_thickness); ex_slider_set_value(p->slider[S_CENTER], p->o.center);
    ex_slider_set_value(p->slider[S_REPEL], p->o.repel); ex_slider_set_value(p->slider[S_LINKF], p->o.link_strength);
    ex_slider_set_value(p->slider[S_DIST], p->o.link_distance);
    for (i = 0; i < NSLIDER; i++) show_value(p, i, ex_slider_value(p->slider[i]));
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) show_group_color(p, i);
    if (ex_ui_focus(p->ui) != p->search_f) { set_field(p->search_doc, p->o.search); p->search_ver = ex_doc_version(p->search_doc); }
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (ex_ui_focus(p->ui) != p->group_f[i]) { set_field(p->group_doc[i], p->o.groups[i].query); p->group_ver[i] = ex_doc_version(p->group_doc[i]); }
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (p->o.groups[i].query[0] && i + 1 > p->shown_groups) p->shown_groups = i + 1;
    show_sections(p);
}

/* ---- callbacks ---- */

static void on_gear(ExWidget *w, void *u) { ExGraphPanel *p = u; (void)w; ex_graphpanel_open(p, !p->open); }
/* One section is open at a time, so the panel is never taller than its longest section (a small pane still shows it whole). */
static void on_head(ExWidget *w, void *u) {
    ExGraphPanel *p = u;
    int i, was;
    for (i = 0; i < NSEC; i++) if (p->head[i] == w) { was = p->open_sec[i]; memset(p->open_sec, 0, sizeof p->open_sec); p->open_sec[i] = !was; }
    show_sections(p);
}
static void on_orphans(ExWidget *w, void *u) { ExGraphPanel *p = u; (void)w; p->o.show_orphans = !p->o.show_orphans; show_all(p); commit(p); }
static void on_unres(ExWidget *w, void *u) { ExGraphPanel *p = u; (void)w; p->o.show_unresolved = !p->o.show_unresolved; show_all(p); commit(p); }
static void on_attach(ExWidget *w, void *u) { ExGraphPanel *p = u; (void)w; p->o.show_attachments = !p->o.show_attachments; show_all(p); commit(p); }
static void on_arrows(ExWidget *w, void *u) { ExGraphPanel *p = u; (void)w; p->o.arrows = !p->o.arrows; show_all(p); commit(p); }
static void on_new_group(ExWidget *w, void *u) { ExGraphPanel *p = u; (void)w; if (p->shown_groups < EX_GRAPH_MAX_GROUPS) p->shown_groups++; show_sections(p); ex_ui_set_focus(p->ui, p->group_f[p->shown_groups - 1]); }
static void on_group_color(ExWidget *w, void *u) {
    ExGraphPanel *p = u;
    int i;
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (p->group_c[i] == w) { p->o.groups[i].color = (p->o.groups[i].color + 1) % 6; show_group_color(p, i); commit(p); }
}

static void on_slider(ExWidget *w, float v, void *u) {
    ExGraphPanel *p = u;
    int i;
    for (i = 0; i < NSLIDER; i++) if (p->slider[i] == w) break;
    if (i == NSLIDER) return;
    switch (i) {
    case S_FADE: p->o.text_fade = v; break;
    case S_NODE: p->o.node_size = v; break;
    case S_LINK: p->o.link_thickness = v; break;
    case S_CENTER: p->o.center = v; break;
    case S_REPEL: p->o.repel = v; break;
    case S_LINKF: p->o.link_strength = v; break;
    default: p->o.link_distance = v; break;
    }
    show_value(p, i, v);
    commit(p);
}

/* Reads the text fields into the options; 1 if any changed. */
static int read_fields(ExGraphPanel *p) {
    int i, ch = 0;
    char *t = field_text(p->search_doc);
    if (t) { if (strncmp(t, p->o.search, EX_GRAPH_QUERY_MAX - 1)) { strncpy(p->o.search, t, EX_GRAPH_QUERY_MAX - 1); p->o.search[EX_GRAPH_QUERY_MAX - 1] = 0; ch = 1; } free(t); }
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) {
        t = field_text(p->group_doc[i]);
        if (t) { if (strncmp(t, p->o.groups[i].query, EX_GRAPH_QUERY_MAX - 1)) { strncpy(p->o.groups[i].query, t, EX_GRAPH_QUERY_MAX - 1); p->o.groups[i].query[EX_GRAPH_QUERY_MAX - 1] = 0; ch = 1; } free(t); }
    }
    return ch;
}

static int field_key(ExWidget *w, ExKey key, int mods, void *u) {
    ExGraphPanel *p = u;
    (void)w; (void)mods;
    if (key == EX_KEY_ENTER) { if (read_fields(p)) commit(p); return 1; }       /* commit at once */
    if (key == EX_KEY_ESCAPE) { ex_graphpanel_open(p, 0); return 1; }
    return 0;
}

/* ---- building ---- */

static ExWidget *toggle(ExGraphPanel *p, ExClickFn fn) {
    ExWidget *b = ex_widget_button(p->ui, "Off", fn, p);
    if (b) { b->pad_x = 1.0f; b->pad_y = 0.4f; b->width = ex_size_token(EX_SIZE_SPACING, 5.0f); }
    return b;
}

static ExWidget *field(ExGraphPanel *p, ExDoc *d, const char *name) {
    ExWidget *f = ex_widget_editor(p->ui, d, name);
    if (!f) return NULL;
    f->bg_token = EX_COLOR_BACKGROUND; f->border_token = EX_COLOR_BORDER; f->radius_token = EX_SIZE_RADIUS;
    f->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f);
    f->width = ex_size_grow();
    f->on_key = field_key; f->on_key_user = p;
    return f;
}

/* A row: a label that takes the room, then the control. */
static ExWidget *row(ExGraphPanel *p, const char *label, ExWidget *control) {
    ExWidget *r = ex_widget_box(p->ui, 0, label), *l = ex_widget_label(p->ui, label);
    if (!r || !l || !control) return r;
    r->width = ex_size_grow(); r->gap = 0.75f; r->align_y = 1;
    l->width = ex_size_grow();
    ex_widget_add(r, l);
    ex_widget_add(r, control);
    return r;
}

static ExWidget *slider_row(ExGraphPanel *p, int i) {
    ExWidget *r = ex_widget_box(p->ui, 1, sliders[i].name), *top = ex_widget_box(p->ui, 0, ""), *l = ex_widget_label(p->ui, sliders[i].name);
    p->slider[i] = ex_widget_slider(p->ui, sliders[i].name, sliders[i].min, sliders[i].max, sliders[i].step, 0.0f, on_slider, p);
    p->value[i] = ex_widget_label(p->ui, "0");
    if (!r || !top || !l || !p->slider[i] || !p->value[i]) return r;
    r->width = ex_size_grow(); r->gap = 0.25f;
    top->width = ex_size_grow();
    l->width = ex_size_grow();
    p->value[i]->text_token = EX_COLOR_TEXT_MUTED;
    ex_widget_add(top, l); ex_widget_add(top, p->value[i]);
    ex_widget_add(r, top); ex_widget_add(r, p->slider[i]);
    return r;
}

static ExWidget *section(ExGraphPanel *p, int i, const char *title) {
    ExWidget *b = ex_widget_button(p->ui, title, on_head, p), *body = ex_widget_box(p->ui, 1, title);
    if (!b || !body) return NULL;
    b->width = ex_size_grow(); b->bold = 1; b->menu_item = 1; b->submenu = 0; b->bg_token = EX_COLOR_SURFACE; b->border_token = -1;
    body->width = ex_size_grow(); body->gap = 0.75f; body->pad_x = 0.5f;
    p->head[i] = b; p->body[i] = body;
    ex_widget_add(p->panel, b);
    ex_widget_add(p->panel, body);
    return body;
}

static void free_panel(void *vp) {
    ExGraphPanel *p = vp;
    int i;
    if (!p) return;
    ex_doc_free(p->search_doc);
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) ex_doc_free(p->group_doc[i]);
    free(p);
}

static void tick_fn(void *p, int64_t now) { ex_graphpanel_tick(p, now); }

ExGraphPanel *ex_graphpanel_create(ExUi *ui, ExWidget *graph, ExGraphModel *m, ExGraphPanelChanged changed, void *user) {
    ExGraphPanel *p = calloc(1, sizeof *p);
    ExWidget *body;
    int i;
    char name[60];
    if (!p) return NULL;
    p->ui = ui; p->graph = graph; p->m = m; p->changed = changed; p->user = user;
    p->o = *ex_graphmodel_options(m);
    p->search_doc = ex_doc_create("", 0);
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) p->group_doc[i] = ex_doc_create("", 0);
    p->col = ex_widget_box(ui, 1, "Graph settings");
    p->gear = ex_widget_button(ui, "Graph settings", on_gear, p);
    p->panel = ex_widget_box(ui, 1, "Graph settings panel");
    if (!p->col || !p->gear || !p->panel || !p->search_doc) { free_panel(p); return NULL; }
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (!p->group_doc[i]) { free_panel(p); return NULL; }
    ex_widget_set_icon(p->gear, EX_ICON_GEAR);
    p->gear->bg_token = EX_COLOR_SURFACE;
    p->col->gap = 0.5f; p->col->align_x = 2;
    p->panel->width = ex_size_token(EX_SIZE_SPACING, 36.0f);
    p->panel->bg_token = EX_COLOR_SURFACE; p->panel->border_token = EX_COLOR_BORDER; p->panel->radius_token = EX_SIZE_RADIUS;
    p->panel->pad_x = 1.0f; p->panel->pad_y = 0.75f; p->panel->gap = 0.5f;
    p->panel->hidden = 1;

    body = section(p, SEC_FILTERS, "Filters");
    p->search_f = field(p, p->search_doc, "Search files in the graph");
    p->b_orphans = toggle(p, on_orphans); p->b_unres = toggle(p, on_unres); p->b_attach = toggle(p, on_attach);
    { ExWidget *sl = ex_widget_label(ui, "Search files"); if (sl) { sl->text_token = EX_COLOR_TEXT_MUTED; ex_widget_add(body, sl); } }
    ex_widget_add(body, p->search_f);
    ex_widget_add(body, row(p, "Orphans", p->b_orphans));
    ex_widget_add(body, row(p, "Unresolved links", p->b_unres));
    ex_widget_add(body, row(p, "Attachments", p->b_attach));

    body = section(p, SEC_GROUPS, "Groups");
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) {
        snprintf(name, sizeof name, "Group %d query", i + 1);
        p->group_f[i] = field(p, p->group_doc[i], name);
        p->group_c[i] = ex_widget_button(ui, "", on_group_color, p);
        if (p->group_c[i]) { p->group_c[i]->width = ex_size_token(EX_SIZE_SPACING, 2.5f); p->group_c[i]->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f); p->group_c[i]->pad_x = p->group_c[i]->pad_y = 0; }
        p->group_row[i] = ex_widget_box(ui, 0, name);
        if (p->group_row[i] && p->group_f[i] && p->group_c[i]) { p->group_row[i]->width = ex_size_grow(); p->group_row[i]->gap = 0.5f; ex_widget_add(p->group_row[i], p->group_f[i]); ex_widget_add(p->group_row[i], p->group_c[i]); ex_widget_add(body, p->group_row[i]); }
    }
    p->b_new_group = ex_widget_button(ui, "New group", on_new_group, p);
    if (p->b_new_group) ex_widget_add(body, p->b_new_group);

    body = section(p, SEC_DISPLAY, "Display");
    p->b_arrows = toggle(p, on_arrows);
    ex_widget_add(body, row(p, "Arrows", p->b_arrows));
    ex_widget_add(body, slider_row(p, S_FADE)); ex_widget_add(body, slider_row(p, S_NODE)); ex_widget_add(body, slider_row(p, S_LINK));

    body = section(p, SEC_FORCES, "Forces");
    ex_widget_add(body, slider_row(p, S_CENTER)); ex_widget_add(body, slider_row(p, S_REPEL)); ex_widget_add(body, slider_row(p, S_LINKF)); ex_widget_add(body, slider_row(p, S_DIST));

    for (i = 0; i < NSLIDER; i++) if (!p->slider[i]) { free_panel(p); return NULL; }
    ex_widget_add(p->col, p->gear);
    ex_widget_add(p->col, p->panel);
    ex_widget_add(graph, p->col);
    show_all(p);
    ex_graphview_set_extra(graph, p, free_panel, tick_fn);
    return p;
}

void ex_graphpanel_open(ExGraphPanel *p, int open) {
    if (!p) return;
    p->open = open != 0;
    p->panel->hidden = !p->open;
    p->gear->selected = p->open;
    if (!p->open && ex_ui_focus(p->ui) && ex_ui_focus(p->ui) != p->gear) {                 /* a control that is hidden cannot keep the focus */
        ExWidget *f = ex_ui_focus(p->ui);
        while (f && f != p->panel) f = f->parent;
        if (f) ex_ui_set_focus(p->ui, p->gear);
    }
}

int ex_graphpanel_is_open(const ExGraphPanel *p) { return p && p->open; }
ExWidget *ex_graphpanel_box(const ExGraphPanel *p) { return p ? p->panel : NULL; }
ExWidget *ex_graphpanel_gear(const ExGraphPanel *p) { return p ? p->gear : NULL; }

static ExWidget *find_in(ExWidget *w, const char *name) {
    for (; w; w = w->next) {
        ExWidget *f;
        if (w->focusable && w->name && !strncmp(w->name, name, strlen(name))) return w;
        f = find_in(w->first_child, name);
        if (f) return f;
    }
    return NULL;
}
ExWidget *ex_graphpanel_find(const ExGraphPanel *p, const char *name) { return p ? find_in(p->panel->first_child, name) : NULL; }

void ex_graphpanel_tick(ExGraphPanel *p, int64_t now_ms) {
    int i, typed = 0;
    (void)now_ms;
    if (!p) return;
    if (p->graph->w > 0.0f) {                                         /* the panel never wider than the pane */
        float sp = (float)ex_theme_size(ex_ui_theme(p->ui), EX_SIZE_SPACING) * ex_renderer_scale(ex_ui_renderer(p->ui));
        float want = 36.0f * sp, room = p->graph->w - 2.0f * sp;
        p->panel->width = ex_size_px(p->ui, room < want ? (room > sp * 8.0f ? room : sp * 8.0f) : want);
    }
    if (ex_doc_version(p->search_doc) != p->search_ver) { p->search_ver = ex_doc_version(p->search_doc); typed = 1; }
    for (i = 0; i < EX_GRAPH_MAX_GROUPS; i++) if (ex_doc_version(p->group_doc[i]) != p->group_ver[i]) { p->group_ver[i] = ex_doc_version(p->group_doc[i]); typed = 1; }
    if (typed && read_fields(p)) commit(p);                       /* the service coalesces: a query typed letter by letter is read once */
    if (!typed && !ex_graphopts_equal(&p->o, ex_graphmodel_options(p->m))) { p->o = *ex_graphmodel_options(p->m); show_all(p); }   /* changed from elsewhere */
}
