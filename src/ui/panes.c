#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_panes.h"

typedef struct GroupWidgets { ExId group; ExWidget *box, *strip, *content; } GroupWidgets;
typedef struct Cached { ExId tab; ExWidget *w; } Cached;

typedef struct Drag {
    int active;                  /* a tab is held down */
    int moving;                  /* moved far enough to be a drag */
    ExId tab;
    float x0, y0;
    ExId over;                   /* the pane highlighted as the drop target */
} Drag;

typedef struct Divider { int active; ExId split; size_t index; } Divider;

struct ExPanes {
    ExUi *ui;
    ExWorkspace *ws;
    ExPanesHooks hooks;
    void *user;
    ExWidget *root;
    ExWidget *header;            /* shown between the tab strip and the content of the active group (not owned) */
    GroupWidgets *groups; size_t ngroups, capgroups;
    Cached *cache; size_t ncache, capcache;
    unsigned long built_layout;
    ExId shown_active;           /* the active group the highlighting was last set for */
    Drag drag;
    Divider div;
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

void ex_panes_tab_title(const ExTab *tab, char *out, size_t cap) {
    const char *base = tab->path, *slash, *dot;
    size_t n;
    if (!base || !base[0]) {
        snprintf(out, cap, "%s", strcmp(tab->view, "graph") == 0 ? "Graph" : strcmp(tab->view, "canvas") == 0 ? "Canvas" : "New tab");
        return;
    }
    slash = strrchr(base, '/');
    if (slash) base = slash + 1;
    n = strlen(base);
    dot = strrchr(base, '.');
    if (dot && dot != base && (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0)) n = (size_t)(dot - base);
    snprintf(out, cap, "%.*s", (int)(n < cap ? n : cap - 1), base);
}

static GroupWidgets *group_widgets(ExPanes *p, ExId g) {
    size_t i;
    for (i = 0; i < p->ngroups; i++) if (p->groups[i].group == g) return &p->groups[i];
    return NULL;
}

static Cached *cached(ExPanes *p, ExId tab) {
    size_t i;
    for (i = 0; i < p->ncache; i++) if (p->cache[i].tab == tab) return &p->cache[i];
    return NULL;
}

void ex_panes_replace_content(ExPanes *p, ExId tab, ExWidget *content) {
    Cached *c = cached(p, tab);
    if (!c || !content) { ex_widget_free(content); return; }
    ex_ui_release(p->ui, c->w);
    if (c->w->parent) ex_widget_remove(c->w->parent, c->w);
    ex_widget_free(c->w);
    c->w = content;
    p->built_layout = (unsigned long)-1;                       /* the next update puts the new widget in place */
}

ExWidget *ex_panes_content(ExPanes *p, ExId tab) { Cached *c = cached(p, tab); return c ? c->w : NULL; }

static int within(const ExWidget *w, float x, float y) { return w && x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h; }

ExId ex_panes_group_at(ExPanes *p, float x, float y) {
    size_t i;
    for (i = 0; i < p->ngroups; i++) if (within(p->groups[i].box, x, y)) return p->groups[i].group;
    return 0;
}

/* ---- input ---- */

static int in_chain(const ExWidget *root, const ExWidget *w) { for (; w; w = w->parent) if (w == root) return 1; return 0; }

static GroupWidgets *group_of_widget(ExPanes *p, const ExWidget *w) {
    size_t i;
    for (i = 0; i < p->ngroups; i++) if (in_chain(p->groups[i].box, w)) return &p->groups[i];
    return NULL;
}

static void set_drop_highlight(ExPanes *p, ExId group) {
    size_t i;
    for (i = 0; i < p->ngroups; i++) p->groups[i].box->border_token = p->groups[i].group == group ? EX_COLOR_ACCENT : EX_COLOR_BORDER;
    p->drag.over = group;
}

/* 0 middle, 1 left, 2 right, 3 above, 4 below: which part of a pane's content a point is in. */
static int zone_of(const ExWidget *c, float x, float y) {
    float fx = (x - c->x) / (c->w > 0 ? c->w : 1), fy = (y - c->y) / (c->h > 0 ? c->h : 1);
    if (fx < 0.25f && fx <= fy && fx <= 1 - fy) return 1;
    if (fx > 0.75f && 1 - fx <= fy && 1 - fx <= 1 - fy) return 2;
    if (fy < 0.25f) return 3;
    if (fy > 0.75f) return 4;
    return 0;
}

static int tab_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    ExPanes *p = user;
    ExRenderer *r = ex_ui_renderer(p->ui);
    if (kind == EX_MOUSE_DOWN) {
        p->drag.active = 1; p->drag.moving = 0; p->drag.tab = w->tag; p->drag.x0 = x; p->drag.y0 = y; p->drag.over = 0;
        return 1;
    }
    if (!p->drag.active) return 1;
    if (kind == EX_MOUSE_DRAG) {
        if (!p->drag.moving && (fabsf(x - p->drag.x0) > (float)ex_px(r, 6) || fabsf(y - p->drag.y0) > (float)ex_px(r, 6))) p->drag.moving = 1;
        if (p->drag.moving) {
            GroupWidgets *gw = group_of_widget(p, under);
            set_drop_highlight(p, gw ? gw->group : 0);
        }
        return 1;
    }
    /* released */
    p->drag.active = 0;
    set_drop_highlight(p, 0);
    if (!p->drag.moving) {                                       /* a click: show the tab */
        ex_ws_activate(p->ws, w->tag);
        if (p->hooks.tab_shown) p->hooks.tab_shown(p, w->tag, p->user);
        return 1;
    }
    p->drag.moving = 0;
    {
        GroupWidgets *gw = group_of_widget(p, under);
        ExId tab = p->drag.tab;
        if (!gw) {                                                /* outside every pane */
            if (p->hooks.popout && (x < p->root->x || y < p->root->y || x >= p->root->x + p->root->w || y >= p->root->y + p->root->h)) p->hooks.popout(p, tab, p->user);
            return 1;
        }
        if (in_chain(gw->strip, under)) {                         /* on a tab strip: before the first tab whose middle is to the right */
            size_t idx = 0;
            ExWidget *c;
            for (c = gw->strip->first_child; c; c = c->next) {
                if (c->tag == 0 || c->type != EX_WIDGET_BUTTON || c->on_mouse != tab_mouse) continue;
                if (c->x + c->w / 2 < x) idx++;
            }
            if (ex_ws_tab(p->ws, tab)->group == gw->group) {      /* moving inside a group: the tab itself is not counted */
                size_t k, before = 0;
                for (k = 0; k < idx && k < ex_ws_group_tab_count(p->ws, gw->group); k++) if (ex_ws_group_tab(p->ws, gw->group, k) == tab) before = 1;
                idx -= before;
            }
            ex_ws_move_tab(p->ws, tab, gw->group, idx);
        } else {
            switch (zone_of(gw->content, x, y)) {
            case 1: ex_ws_split(p->ws, gw->group, EX_SPLIT_HORIZONTAL, 0, tab); break;
            case 2: ex_ws_split(p->ws, gw->group, EX_SPLIT_HORIZONTAL, 1, tab); break;
            case 3: ex_ws_split(p->ws, gw->group, EX_SPLIT_VERTICAL, 0, tab); break;
            case 4: ex_ws_split(p->ws, gw->group, EX_SPLIT_VERTICAL, 1, tab); break;
            default: ex_ws_move_tab(p->ws, tab, gw->group, (size_t)-1); break;
            }
        }
    }
    return 1;
}

static void close_click(ExWidget *w, void *user) {
    ExPanes *p = user;
    if (p->hooks.close_tab && !p->hooks.close_tab(p, w->tag, p->user)) return;
    ex_ws_close_tab(p->ws, w->tag);
}

static void new_click(ExWidget *w, void *user) {
    ExPanes *p = user;
    if (p->hooks.new_tab) p->hooks.new_tab(p, w->tag, p->user);
}

/* A press anywhere in a pane gives it the focus; the press itself is handled as usual. */
static int group_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    ExPanes *p = user;
    (void)x; (void)y; (void)under;
    if (kind == EX_MOUSE_DOWN && ex_ws_active_group(p->ws) != w->tag) {
        ex_ws_set_active_group(p->ws, w->tag);
        if (p->hooks.tab_shown && ex_ws_group_active(p->ws, w->tag)) p->hooks.tab_shown(p, ex_ws_group_active(p->ws, w->tag), p->user);
    }
    return 0;
}

static void apply_ratios(ExPanes *p, ExWidget *split_w) {
    ExId s = split_w->tag;
    size_t i = 0;
    ExWidget *c;
    for (c = split_w->first_child; c && i < ex_ws_split_count(p->ws, s); c = c->next, i++) {
        if (ex_ws_split_dir(p->ws, s) == EX_SPLIT_HORIZONTAL) c->width = ex_size_percent(ex_ws_split_ratio(p->ws, s, i));
        else c->height = ex_size_percent(ex_ws_split_ratio(p->ws, s, i));
    }
}

static int split_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    ExPanes *p = user;
    ExRenderer *r = ex_ui_renderer(p->ui);
    int horiz = ex_ws_split_dir(p->ws, w->tag) == EX_SPLIT_HORIZONTAL;
    (void)under;
    if (kind == EX_MOUSE_DOWN) {
        size_t i = 0;
        ExWidget *c;
        float reach = (float)ex_px(r, 4);
        for (c = w->first_child; c && c->next; c = c->next, i++) {
            float edge = horiz ? c->x + c->w : c->y + c->h, pos = horiz ? x : y;
            if (fabsf(pos - edge) <= reach) { p->div.active = 1; p->div.split = w->tag; p->div.index = i; return 1; }
        }
        return 0;
    }
    if (!p->div.active) return 1;
    if (kind == EX_MOUSE_DRAG) {
        ExWidget *c = w->first_child;
        size_t i;
        float extent = horiz ? w->w : w->h, start;
        for (i = 0; c && i < p->div.index; i++) c = c->next;
        if (!c || extent <= 0) return 1;
        start = horiz ? c->x : c->y;
        {   /* a pane keeps room for its tab strip: narrower than this the strip would spill over the next pane */
            float want = ((horiz ? x : y) - start) / extent;
            float min = (float)ex_px(r, 160) / extent, pair = ex_ws_split_ratio(p->ws, p->div.split, p->div.index) + ex_ws_split_ratio(p->ws, p->div.split, p->div.index + 1);
            if (min > pair / 2.0f) min = pair / 2.0f;
            if (want < min) want = min;
            if (want > pair - min) want = pair - min;
            ex_ws_set_ratio(p->ws, p->div.split, p->div.index, want);
        }
        apply_ratios(p, w);
    } else p->div.active = 0;
    return 1;
}

/* ---- drops from the desktop ---- */

static int group_drop(ExWidget *w, int is_file, const char *data, float x, float y, void *user) {
    ExPanes *p = user;
    ExId g = w->tag;
    GroupWidgets *gw = group_widgets(p, g);
    if (is_file) return p->hooks.drop_file ? p->hooks.drop_file(p, g, data, p->user) : 0;
    if (gw && gw->content && gw->content->first_child) {
        ExWidget *c = gw->content->first_child;
        if (c->type == EX_WIDGET_EDITOR && c->editor && !ex_editor_readonly(c->editor) && within(c, x, y)) {
            ex_editor_drop_text(c->editor, ex_ui_renderer(p->ui), x, y, data, strlen(data));
            ex_ws_set_active_group(p->ws, g);
            return 1;
        }
    }
    return 0;
}

/* ---- building ---- */

static void add_group(ExPanes *p, GroupWidgets gw) {
    if (p->ngroups == p->capgroups) {
        size_t nc = p->capgroups ? p->capgroups * 2 : 8;
        GroupWidgets *ng = realloc(p->groups, nc * sizeof *ng);
        if (!ng) return;
        p->groups = ng;
        p->capgroups = nc;
    }
    p->groups[p->ngroups++] = gw;
}

static void set_name(ExWidget *w, const char *name) { free(w->name); w->name = dup_str(name); }

static ExWidget *build_tab_button(ExPanes *p, const ExTab *t, int active_tab, int active_group) {
    char title[256], label[300], name[320];
    ExWidget *b;
    ex_panes_tab_title(t, title, sizeof title);
    snprintf(label, sizeof label, "%s%s", t->pinned ? "\xE2\x80\xA2 " : "", title);   /* a bullet marks a pinned tab until icons are drawn */
    b = ex_widget_button(p->ui, label, NULL, NULL);
    if (!b) return NULL;
    snprintf(name, sizeof name, "Tab: %s%s%s", title, t->pinned ? " (pinned)" : "", t->stacked ? " (stacked)" : "");
    set_name(b, name);
    b->bg_token = EX_COLOR_BACKGROUND_ALT;
    b->border_token = -1;
    b->pad_x = 1.0f;
    b->pad_y = 0.5f;
    b->selected = active_tab;
    b->text_token = active_tab && active_group ? EX_COLOR_TEXT : EX_COLOR_TEXT_MUTED;
    b->tag = t->id;
    b->on_mouse = tab_mouse;
    b->on_mouse_user = p;
    return b;
}

static ExWidget *build_group(ExPanes *p, ExId g) {
    ExWidget *box = ex_widget_box(p->ui, 1, "Pane"), *strip = ex_widget_box(p->ui, 0, "Tabs"), *content = ex_widget_box(p->ui, 1, "Content");
    size_t i, n = ex_ws_group_tab_count(p->ws, g);
    int active_group = ex_ws_active_group(p->ws) == g;
    GroupWidgets gw;
    ExId active = ex_ws_group_active(p->ws, g);
    if (!box || !strip || !content) return box;
    box->tag = g; box->width = ex_size_grow(); box->height = ex_size_grow();
    box->bg_token = EX_COLOR_BACKGROUND; box->border_token = EX_COLOR_BORDER; box->clip = 1;
    box->on_mouse = group_mouse; box->on_mouse_user = p;
    box->on_drop = group_drop; box->on_drop_user = p;
    strip->tag = g; strip->width = ex_size_grow(); strip->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f);
    strip->bg_token = EX_COLOR_BACKGROUND_ALT; strip->pad_x = 0.5f; strip->gap = 0.25f;
    content->tag = g; content->width = ex_size_grow(); content->height = ex_size_grow(); content->clip = 1;
    for (i = 0; i < n; i++) {
        const ExTab *t = ex_ws_tab(p->ws, ex_ws_group_tab(p->ws, g, i));
        ExWidget *b;
        if (!t) continue;
        b = build_tab_button(p, t, t->id == active, active_group);
        if (b) ex_widget_add(strip, b);
        if (b && !t->pinned) {
            char cn[320];
            ExWidget *x = ex_widget_button(p->ui, "\xC3\x97", close_click, p);
            if (!x) continue;
            snprintf(cn, sizeof cn, "Close %s", b->name + 5);
            set_name(x, cn);
            x->tag = t->id; x->bg_token = EX_COLOR_BACKGROUND_ALT; x->border_token = -1; x->pad_x = 0.5f; x->pad_y = 0.5f;
            x->text_token = EX_COLOR_TEXT_MUTED;
            ex_widget_add(strip, x);
        }
    }
    if (p->hooks.new_tab) {
        ExWidget *plus = ex_widget_button(p->ui, "+", new_click, p);
        if (plus) {
            set_name(plus, "New tab");
            plus->tag = g; plus->bg_token = EX_COLOR_BACKGROUND_ALT; plus->border_token = -1; plus->pad_x = 1.0f; plus->pad_y = 0.5f;
            ex_widget_add(strip, plus);
        }
    }
    if (active) {
        Cached *c = cached(p, active);
        if (!c && p->hooks.make_content) {
            const ExTab *t = ex_ws_tab(p->ws, active);
            ExWidget *w = t ? p->hooks.make_content(p, t, p->user) : NULL;
            if (w) {
                if (p->ncache == p->capcache) {
                    size_t nc = p->capcache ? p->capcache * 2 : 8;
                    Cached *nc2 = realloc(p->cache, nc * sizeof *nc2);
                    if (nc2) { p->cache = nc2; p->capcache = nc; }
                }
                if (p->ncache < p->capcache) { p->cache[p->ncache].tab = active; p->cache[p->ncache].w = w; p->ncache++; c = &p->cache[p->ncache - 1]; }
                else ex_widget_free(w);
            }
        }
        if (c) ex_widget_add(content, c->w);
    }
    if (!content->first_child) {
        ExWidget *hint = ex_widget_label(p->ui, "No tab is open here. Open a note from the file explorer.");
        if (hint) { hint->text_token = EX_COLOR_TEXT_MUTED; hint->pad_x = 2; hint->pad_y = 2; ex_widget_add(content, hint); }
    }
    ex_widget_add(box, strip);
    ex_widget_add(box, content);
    gw.group = g; gw.box = box; gw.strip = strip; gw.content = content;
    add_group(p, gw);
    return box;
}

static ExWidget *build_node(ExPanes *p, ExId node, int parent_dir, float share) {
    ExWidget *w;
    if (ex_ws_node_kind(p->ws, node) == EX_NODE_GROUP) w = build_group(p, node);
    else {
        size_t i, n = ex_ws_split_count(p->ws, node);
        w = ex_widget_box(p->ui, ex_ws_split_dir(p->ws, node) == EX_SPLIT_VERTICAL, "Split");
        if (!w) return NULL;
        w->tag = node; w->width = ex_size_grow(); w->height = ex_size_grow();
        w->on_mouse = split_mouse; w->on_mouse_user = p;
        for (i = 0; i < n; i++) {
            ExWidget *c = build_node(p, ex_ws_split_child(p->ws, node, i), ex_ws_split_dir(p->ws, node) == EX_SPLIT_HORIZONTAL ? 1 : 0, ex_ws_split_ratio(p->ws, node, i));
            if (c) ex_widget_add(w, c);
        }
    }
    if (w && parent_dir == 1) w->width = ex_size_percent(share);
    else if (w && parent_dir == 0) w->height = ex_size_percent(share);
    return w;
}

static void drop_stale_content(ExPanes *p) {
    size_t i;
    for (i = 0; i < p->ncache; i++) {
        if (ex_ws_tab(p->ws, p->cache[i].tab)) continue;
        ex_ui_release(p->ui, p->cache[i].w);
        if (p->cache[i].w->parent) ex_widget_remove(p->cache[i].w->parent, p->cache[i].w);
        if (p->hooks.content_closed) p->hooks.content_closed(p, p->cache[i].tab, p->cache[i].w, p->user);
        ex_widget_free(p->cache[i].w);
        p->cache[i] = p->cache[--p->ncache];
        i--;
    }
}

/* The header sits in the active group, between its tab strip and its content. */
static void place_header(ExPanes *p) {
    size_t i;
    ExId ag = ex_ws_active_group(p->ws);
    if (!p->header) return;
    if (p->header->parent) ex_widget_remove(p->header->parent, p->header);
    for (i = 0; i < p->ngroups; i++) {
        if (p->groups[i].group != ag) continue;
        ex_widget_remove(p->groups[i].box, p->groups[i].content);
        ex_widget_add(p->groups[i].box, p->header);
        ex_widget_add(p->groups[i].box, p->groups[i].content);
        return;
    }
}

void ex_panes_set_header(ExPanes *p, ExWidget *header) { p->header = header; place_header(p); }

static void rebuild(ExPanes *p) {
    ExWidget *old, *next;
    size_t i;
    ExId main_root = ex_ws_window_root(p->ws, ex_ws_main_window(p->ws));
    if (p->header && p->header->parent) { ex_ui_release(p->ui, p->header); ex_widget_remove(p->header->parent, p->header); }   /* the header is not freed with the groups */
    for (i = 0; i < p->ncache; i++)                      /* keep the contents out of the tree that is freed */
        if (p->cache[i].w->parent) ex_widget_remove(p->cache[i].w->parent, p->cache[i].w);
    drop_stale_content(p);
    for (old = p->root->first_child; old; old = next) {
        next = old->next;
        ex_ui_release(p->ui, old);
        ex_widget_remove(p->root, old);
        ex_widget_free(old);
    }
    p->ngroups = 0;
    if (main_root) {
        ExWidget *top = build_node(p, main_root, -1, 1.0f);
        if (top) ex_widget_add(p->root, top);
    }
    p->built_layout = ex_ws_layout_revision(p->ws);
    p->shown_active = ex_ws_active_group(p->ws);
    place_header(p);
}

static void refresh_active(ExPanes *p) {
    size_t i;
    ExId ag = ex_ws_active_group(p->ws);
    for (i = 0; i < p->ngroups; i++) {
        ExWidget *c;
        int is_active = p->groups[i].group == ag;
        for (c = p->groups[i].strip->first_child; c; c = c->next)
            if (c->on_mouse == tab_mouse) c->text_token = c->selected && is_active ? EX_COLOR_TEXT : EX_COLOR_TEXT_MUTED;
    }
    p->shown_active = ag;
    place_header(p);
}

void ex_panes_update(ExPanes *p) {
    if (p->built_layout != ex_ws_layout_revision(p->ws) || p->root->first_child == NULL) rebuild(p);
    else if (p->shown_active != ex_ws_active_group(p->ws)) refresh_active(p);
}

ExPanes *ex_panes_create(ExUi *ui, ExWorkspace *ws, const ExPanesHooks *hooks, void *user) {
    ExPanes *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->ui = ui; p->ws = ws; p->user = user;
    if (hooks) p->hooks = *hooks;
    p->root = ex_widget_box(ui, 0, "Panes");
    if (!p->root) { free(p); return NULL; }
    p->root->width = ex_size_grow();
    p->root->height = ex_size_grow();
    p->built_layout = (unsigned long)-1;
    return p;
}

ExWidget *ex_panes_root(ExPanes *p) { return p->root; }

void ex_panes_free(ExPanes *p) {
    size_t i;
    if (!p) return;
    if (p->header && p->header->parent) ex_widget_remove(p->header->parent, p->header);      /* its owner frees it */
    for (i = 0; i < p->ncache; i++) {            /* detach so freeing the tree does not free them twice */
        if (p->cache[i].w->parent) ex_widget_remove(p->cache[i].w->parent, p->cache[i].w);
        if (p->hooks.content_closed) p->hooks.content_closed(p, p->cache[i].tab, p->cache[i].w, p->user);
        ex_widget_free(p->cache[i].w);
    }
    /* the root widget belongs to whoever put it in a window; if it never was, free it here */
    if (!p->root->parent) ex_widget_free(p->root);
    free(p->groups); free(p->cache);
    free(p);
}

/* ---- sidebars ---- */

typedef struct SidebarState { ExUi *ui; ExWorkspace *ws; ExSide side; int dragging; } SidebarState;

void ex_sidebar_sync(ExWidget *sb) {
    SidebarState *st = sb->owned;
    sb->hidden = !ex_ws_sidebar_visible(st->ws, st->side);
    sb->width = ex_size_px(st->ui, (float)ex_ws_sidebar_width(st->ws, st->side));
}

static int sidebar_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    SidebarState *st = user;
    ExRenderer *r = ex_ui_renderer(st->ui);
    float reach = (float)ex_px(r, 4), scale = ex_renderer_scale(r);
    (void)y; (void)under;
    if (kind == EX_MOUSE_DOWN) {
        float edge = st->side == EX_SIDE_LEFT ? w->x + w->w : w->x;
        if (fabsf(x - edge) <= reach) { st->dragging = 1; return 1; }
        return 0;
    }
    if (!st->dragging) return 1;
    if (kind == EX_MOUSE_DRAG) {
        float px = (st->side == EX_SIDE_LEFT ? x - w->x : w->x + w->w - x) / (scale > 0 ? scale : 1.0f);
        ex_ws_set_sidebar(st->ws, st->side, 1, (int)px);
        w->width = ex_size_px(st->ui, (float)ex_ws_sidebar_width(st->ws, st->side));
    } else st->dragging = 0;
    return 1;
}

static void free_sidebar_state(void *p) { free(p); }

ExWidget *ex_sidebar_create(ExUi *ui, ExWorkspace *ws, ExSide side, ExWidget *content, const char *name) {
    ExWidget *sb = ex_widget_box(ui, 1, name);
    SidebarState *st = calloc(1, sizeof *st);
    if (!sb || !st) { free(st); return sb; }
    st->ui = ui; st->ws = ws; st->side = side;
    sb->owned = st; sb->free_owned = free_sidebar_state;
    sb->height = ex_size_grow();
    sb->clip = 1;
    sb->on_mouse = sidebar_mouse; sb->on_mouse_user = st;
    if (content) ex_widget_add(sb, content);
    ex_sidebar_sync(sb);
    return sb;
}
