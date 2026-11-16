#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_panes.h"

typedef struct GroupWidgets { ExId group; ExWidget *box, *strip, *content; ExWidget *hdr, *back, *fwd, *title, *read, *more; } GroupWidgets;
typedef struct Cached { ExId tab; ExWidget *w; unsigned long epoch; } Cached;     /* epoch: the workspace's, when the content was made */

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
    ExId window;                 /* the workspace window these panes show; 0: the main window (and the right sidebar's group) */
    struct ExPanes *sibling[8]; size_t nsibling;   /* the panes of the other windows, for finding a tab's content (7g) */
    ExPanesHooks hooks;
    void *user;
    ExWidget *root;
    ExWidget *side_root;         /* the right sidebar's group of tabs (5e) */
    GroupWidgets *groups; size_t ngroups, capgroups;
    Cached *cache; size_t ncache, capcache;
    unsigned long built_layout;
    int header_visible;          /* the pane header strip is shown (5d) */
    int building_side;           /* the group being built is the right sidebar's (5e): its tabs are narrower */
    ExId shown_active;           /* the active group the highlighting was last set for */
    Drag drag;
    Divider div;
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

void ex_panes_tab_title(const ExTab *tab, char *out, size_t cap) {
    const char *base = tab->path, *slash, *dot;
    size_t n;
    if (!base || !base[0]) {
        snprintf(out, cap, "%s", strcmp(tab->view, "graph") == 0 ? "Graph" : strcmp(tab->view, "localgraph") == 0 ? "Local graph" : strcmp(tab->view, "canvas") == 0 ? "Canvas" : strcmp(tab->view, "settings") == 0 ? "Settings" : "New tab");
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
    if (!c && content) {                                         /* the tab may be shown in another window */
        size_t i;
        for (i = 0; i < p->nsibling; i++) if (cached(p->sibling[i], tab)) { ex_panes_replace_content(p->sibling[i], tab, content); return; }
    }
    if (!c || !content) { ex_widget_free(content); return; }
    ex_ui_release(p->ui, c->w);
    if (c->w->parent) ex_widget_remove(c->w->parent, c->w);
    ex_widget_free(c->w);
    c->w = content;
    p->built_layout = (unsigned long)-1;                       /* the next update puts the new widget in place */
}

ExWidget *ex_panes_content(ExPanes *p, ExId tab) {
    Cached *c = cached(p, tab);
    size_t i;
    if (c) return c->w;
    for (i = 0; i < p->nsibling; i++) { Cached *s = cached(p->sibling[i], tab); if (s) return s->w; }
    return NULL;
}

void ex_panes_link(ExPanes *p, ExPanes *other, int link) {
    size_t i;
    if (!p || !other || p == other) return;
    for (i = 0; i < p->nsibling; i++) if (p->sibling[i] == other) { if (!link) p->sibling[i] = p->sibling[--p->nsibling]; return; }
    if (link && p->nsibling < 8) p->sibling[p->nsibling++] = other;
}

ExUi *ex_panes_ui(ExPanes *p) { return p ? p->ui : NULL; }

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

/* ---- the pane header (5d) ---- */

static void set_name(ExWidget *w, const char *name);

static void back_click(ExWidget *w, void *user) { ExPanes *p = user; if (p->hooks.navigate) p->hooks.navigate(p, w->tag, -1, p->user); }
static void fwd_click(ExWidget *w, void *user) { ExPanes *p = user; if (p->hooks.navigate) p->hooks.navigate(p, w->tag, 1, p->user); }
static void read_click(ExWidget *w, void *user) { ExPanes *p = user; if (p->hooks.toggle_reading) p->hooks.toggle_reading(p, w->tag, p->user); }
static void more_click(ExWidget *w, void *user) { ExPanes *p = user; if (p->hooks.pane_menu) p->hooks.pane_menu(p, w->tag, w->x, w->y + w->h, p->user); }

static ExWidget *header_button(ExPanes *p, ExId g, const char *name, int icon, ExClickFn fn) {
    ExWidget *b = ex_widget_button(p->ui, name, fn, p);
    if (!b) return NULL;
    ex_widget_set_icon(b, icon);
    b->tag = g; b->bg_token = EX_COLOR_BACKGROUND; b->border_token = -1;
    b->width = ex_size_token(EX_SIZE_SPACING, 3.0f); b->height = ex_size_token(EX_SIZE_SPACING, 3.0f);
    return b;
}

/* Brings the headers up to date with the workspace: the title, whether back and forward have somewhere to go, the reading switch. */
static void sync_headers(ExPanes *p) {
    size_t i;
    for (i = 0; i < p->ngroups; i++) {
        GroupWidgets *gw = &p->groups[i];
        const ExTab *t = ex_ws_tab(p->ws, ex_ws_group_active(p->ws, gw->group));
        char title[300] = "";
        if (!gw->hdr) continue;
        gw->hdr->hidden = !p->header_visible;
        if (t) ex_panes_tab_title(t, title, sizeof title);
        if (!gw->title->name || strcmp(gw->title->name, title[0] ? title : "No tab")) set_name(gw->title, title[0] ? title : "No tab");   /* the screen reader gets the whole title */
        {                                                                                  /* what is drawn is cut to the room it had at the last layout, with an ellipsis */
            char shown[320] = "";
            float sp = (float)ex_theme_size(ex_ui_theme(p->ui), EX_SIZE_SPACING) * ex_renderer_scale(ex_ui_renderer(p->ui));
            float room = gw->hdr->w - (gw->back->w + gw->fwd->w + (gw->read->hidden ? 0.0f : gw->read->w) + gw->more->w) - 6.0f * 0.25f * sp - 2.0f * 0.5f * sp;   /* the strip less its buttons, gaps and padding */
            if (room > 8.0f && title[0]) {                                                  /* the room is known from the last layout; until then the title waits */
                size_t fl = ex_text_fit(ex_ui_renderer(p->ui), ex_ui_font(p->ui, gw->title->font_token, 0), ex_ui_base_size(p->ui), title, strlen(title), room, shown, sizeof shown);
                shown[fl < sizeof shown ? fl : sizeof shown - 1] = 0;
            }
            if (!gw->title->text || strcmp(gw->title->text, shown)) ex_widget_set_text(gw->title, shown);
        }
        gw->back->disabled = !ex_ws_history_can(p->ws, gw->group, -1);
        gw->fwd->disabled = !ex_ws_history_can(p->ws, gw->group, 1);
        gw->read->hidden = !(t && t->view && !strcmp(t->view, "note"));
        gw->read->selected = t && t->mode && !strcmp(t->mode, "reading");
        gw->read->disabled = 0;
    }
}

void ex_panes_set_header_visible(ExPanes *p, int visible) { p->header_visible = visible != 0; sync_headers(p); }
ExWidget *ex_panes_header(ExPanes *p, ExId group) { size_t i; for (i = 0; i < p->ngroups; i++) if (p->groups[i].group == group) return p->groups[i].hdr; return NULL; }
ExWidget *ex_panes_header_button(ExPanes *p, ExId group, const char *name) {
    size_t i;
    for (i = 0; i < p->ngroups; i++) if (p->groups[i].group == group) {
        const GroupWidgets *gw = &p->groups[i];
        ExWidget *b[4] = {gw->back, gw->fwd, gw->read, gw->more};
        size_t k;
        for (k = 0; k < 4; k++) if (b[k] && b[k]->name && !strcmp(b[k]->name, name)) return b[k];
    }
    return NULL;
}
const char *ex_panes_header_title(ExPanes *p, ExId group) { size_t i; for (i = 0; i < p->ngroups; i++) if (p->groups[i].group == group && p->groups[i].title) return p->groups[i].title->text; return NULL; }

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
    {   /* a tab is never wider than 200 px at 100%: a long name ends in "..." (the accessible name below keeps all of it) */
        char fitted[340];
        ExRenderer *r = ex_ui_renderer(p->ui);
        ex_text_fit(r, ex_ui_font(p->ui, b->font_token, b->bold), ex_ui_base_size(p->ui) * b->text_scale, label, strlen(label), (float)ex_px(r, p->building_side ? 110 : 200) - 2.0f * (float)ex_px(r, 12), fitted, sizeof fitted);
        ex_widget_set_text(b, fitted);
    }
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
    ExWidget *hdr = ex_widget_box(p->ui, 0, "Pane header");
    size_t i, n = ex_ws_group_tab_count(p->ws, g);
    int active_group = ex_ws_active_group(p->ws) == g;
    GroupWidgets gw;
    ExId active = ex_ws_group_active(p->ws, g);
    if (!box || !strip || !content) return box;
    box->tag = g; box->width = ex_size_grow(); box->height = ex_size_grow();
    box->bg_token = EX_COLOR_BACKGROUND; box->border_token = EX_COLOR_BORDER;   /* not clipped itself: Clay lets the children of a clipped box grow to the size of their contents, so the pane could not shrink (GitHub issue 2); the tab strip and the content are clipped instead */
    box->on_mouse = group_mouse; box->on_mouse_user = p;
    box->on_drop = group_drop; box->on_drop_user = p;
    strip->tag = g; strip->width = ex_size_grow(); strip->height = ex_size_token(EX_SIZE_TAB_HEIGHT, 1.0f);
    strip->bg_token = EX_COLOR_BACKGROUND_ALT; strip->pad_x = 0.5f; strip->gap = 0.25f; strip->clip = 1;
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
                if (p->ncache < p->capcache) { p->cache[p->ncache].tab = active; p->cache[p->ncache].w = w; p->cache[p->ncache].epoch = ex_ws_epoch(p->ws); p->ncache++; c = &p->cache[p->ncache - 1]; }
                else ex_widget_free(w);
            }
        }
        if (c) ex_widget_add(content, c->w);
    }
    if (!content->first_child) {
        ExWidget *hint = ex_widget_label(p->ui, p->building_side ? "No tab here." : "No tab is open here. Open a note from the file explorer.");
        if (hint) { hint->text_token = EX_COLOR_TEXT_MUTED; hint->pad_x = 2; hint->pad_y = 2; ex_widget_add(content, hint); }
    }
    memset(&gw, 0, sizeof gw);
    if (hdr) {                                                       /* the header: back, forward, the title, the reading switch, the menu */
        gw.back = header_button(p, g, "Navigate back", EX_ICON_ARROW_LEFT, back_click);
        gw.fwd = header_button(p, g, "Navigate forward", EX_ICON_ARROW_RIGHT, fwd_click);
        gw.title = ex_widget_label(p->ui, "");
        gw.read = header_button(p, g, "Toggle reading view", EX_ICON_BOOK, read_click);
        gw.more = header_button(p, g, "Pane menu", EX_ICON_MORE, more_click);
        if (gw.back && gw.fwd && gw.title && gw.read && gw.more) {
            hdr->tag = g; hdr->width = ex_size_grow(); hdr->height = ex_size_token(EX_SIZE_SPACING, 3.5f);
            hdr->bg_token = EX_COLOR_BACKGROUND; hdr->pad_x = 0.5f; hdr->gap = 0.25f; hdr->align_y = 1; hdr->clip = 1;
            gw.title->text_token = EX_COLOR_TEXT_MUTED; gw.title->width = ex_size_grow();
            ex_widget_add(hdr, gw.back); ex_widget_add(hdr, gw.fwd); ex_widget_add(hdr, gw.title); ex_widget_add(hdr, gw.read); ex_widget_add(hdr, gw.more);
            gw.hdr = hdr;
        } else { ex_widget_free(gw.back); ex_widget_free(gw.fwd); ex_widget_free(gw.title); ex_widget_free(gw.read); ex_widget_free(gw.more); ex_widget_free(hdr); memset(&gw, 0, sizeof gw); }
    }
    ex_widget_add(box, strip);
    if (gw.hdr) ex_widget_add(box, gw.hdr);
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

/* Does the tab belong to the window these panes show? The main panes also hold the right sidebar's group, which is in no window. */
static int tab_here(const ExPanes *p, ExId tab) {
    const ExTab *t = ex_ws_tab(p->ws, tab);
    ExId w;
    if (!t) return 0;
    w = ex_ws_window_of_node(p->ws, t->group);
    if (p->window == 0 || p->window == ex_ws_main_window(p->ws)) return w == 0 || w == ex_ws_main_window(p->ws);
    return w == p->window;
}

static void drop_stale_content(ExPanes *p) {
    size_t i;
    for (i = 0; i < p->ncache; i++) {
        if (ex_ws_tab(p->ws, p->cache[i].tab) && p->cache[i].epoch == ex_ws_epoch(p->ws) && tab_here(p, p->cache[i].tab)) continue;      /* the state was replaced: the ids mean other tabs now; or the tab went to another window */
        ex_ui_release(p->ui, p->cache[i].w);
        if (p->cache[i].w->parent) ex_widget_remove(p->cache[i].w->parent, p->cache[i].w);
        if (p->hooks.content_closed) p->hooks.content_closed(p, p->cache[i].tab, p->cache[i].w, p->user);
        ex_widget_free(p->cache[i].w);
        p->cache[i] = p->cache[--p->ncache];
        i--;
    }
}

static void rebuild(ExPanes *p) {
    ExWidget *old, *next;
    size_t i;
    ExId main_root = ex_ws_window_root(p->ws, p->window ? p->window : ex_ws_main_window(p->ws));
    int is_main = p->window == 0 || p->window == ex_ws_main_window(p->ws);
    for (i = 0; i < p->ncache; i++)                      /* keep the contents out of the tree that is freed */
        if (p->cache[i].w->parent) ex_widget_remove(p->cache[i].w->parent, p->cache[i].w);
    drop_stale_content(p);
    for (old = p->root->first_child; old; old = next) {
        next = old->next;
        ex_ui_release(p->ui, old);
        ex_widget_remove(p->root, old);
        ex_widget_free(old);
    }
    for (old = p->side_root->first_child; old; old = next) {
        next = old->next;
        ex_ui_release(p->ui, old);
        ex_widget_remove(p->side_root, old);
        ex_widget_free(old);
    }
    p->ngroups = 0;
    if (main_root) {
        ExWidget *top = build_node(p, main_root, -1, 1.0f);
        if (top) ex_widget_add(p->root, top);
    }
    if (is_main && ex_ws_side_group(p->ws, EX_SIDE_RIGHT)) {            /* the right sidebar's tabs (5e) */
        ExWidget *sg;
        p->building_side = 1;                                              /* a sidebar is narrow: its tabs are at most 110 px */
        sg = build_node(p, ex_ws_side_group(p->ws, EX_SIDE_RIGHT), -1, 1.0f);
        p->building_side = 0;
        if (sg) ex_widget_add(p->side_root, sg);
    }
    p->built_layout = ex_ws_layout_revision(p->ws);
    p->shown_active = ex_ws_active_group(p->ws);
    sync_headers(p);
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
}

void ex_panes_update(ExPanes *p) {
    if (p->built_layout != ex_ws_layout_revision(p->ws) || p->root->first_child == NULL) rebuild(p);
    else if (p->shown_active != ex_ws_active_group(p->ws)) refresh_active(p);
    sync_headers(p);
}

ExPanes *ex_panes_create_window(ExUi *ui, ExWorkspace *ws, const ExPanesHooks *hooks, void *user, ExId window);
ExPanes *ex_panes_create(ExUi *ui, ExWorkspace *ws, const ExPanesHooks *hooks, void *user) { return ex_panes_create_window(ui, ws, hooks, user, 0); }

ExPanes *ex_panes_create_window(ExUi *ui, ExWorkspace *ws, const ExPanesHooks *hooks, void *user, ExId window) {
    ExPanes *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->ui = ui; p->ws = ws; p->user = user; p->window = window;
    if (hooks) p->hooks = *hooks;
    p->root = ex_widget_box(ui, 0, "Panes");
    p->side_root = ex_widget_box(ui, 1, "Sidebar tabs");
    if (!p->root || !p->side_root) { ex_widget_free(p->root); ex_widget_free(p->side_root); free(p); return NULL; }
    p->side_root->width = ex_size_grow(); p->side_root->height = ex_size_grow();
    p->root->width = ex_size_grow();
    p->root->height = ex_size_grow();
    p->built_layout = (unsigned long)-1;
    p->header_visible = 1;
    return p;
}

ExWidget *ex_panes_root(ExPanes *p) { return p->root; }
ExWidget *ex_panes_side_root(ExPanes *p) { return p->side_root; }

void ex_panes_free(ExPanes *p) {
    size_t i;
    if (!p) return;
    for (i = 0; i < p->ncache; i++) {            /* detach so freeing the tree does not free them twice */
        if (p->cache[i].w->parent) ex_widget_remove(p->cache[i].w->parent, p->cache[i].w);
        if (p->hooks.content_closed) p->hooks.content_closed(p, p->cache[i].tab, p->cache[i].w, p->user);
        ex_widget_free(p->cache[i].w);
    }
    /* the root widget belongs to whoever put it in a window; if it never was, free it here */
    if (!p->root->parent) ex_widget_free(p->root);
    if (!p->side_root->parent) ex_widget_free(p->side_root);
    free(p->groups); free(p->cache);
    free(p);
}

/* ---- sidebars ---- */

typedef struct SidebarState { ExUi *ui; ExWorkspace *ws; ExSide side; int dragging, keep_width; float origin; ExWidget *sidebar, *divider, *line, *edge; } SidebarState;

/* The widest the sidebar can be, in pixels at 100%: the window minus the ribbon and the other sidebar. When the other sidebar is
 * shown, the main text view between them may shrink all the way to its two divider strips (its text wraps down to one character a
 * row, then only the handle is left; GitHub issue 2). When the other sidebar is hidden, the main text view keeps 240 px. */
static float sidebar_limit(const SidebarState *st) {
    ExRenderer *r = ex_ui_renderer(st->ui);
    int ww, wh;
    float scale = ex_renderer_scale(r), other = 0.0f, ribbon, room, keep;
    ExSide o = st->side == EX_SIDE_LEFT ? EX_SIDE_RIGHT : EX_SIDE_LEFT;
    ex_renderer_size(r, &ww, &wh);
    ribbon = (float)ex_theme_size(ex_ui_theme(st->ui), EX_SIZE_RIBBON_WIDTH);
    if (ex_ws_sidebar_visible(st->ws, o)) { other = (float)ex_ws_sidebar_width(st->ws, o); keep = 12.0f; }
    else keep = 240.0f;
    room = (float)ww / (scale > 0 ? scale : 1.0f) - ribbon - other - keep;
    return room < (float)EX_SIDEBAR_MIN_WIDTH ? (float)EX_SIDEBAR_MIN_WIDTH : room;
}

int ex_sidebar_fill(ExWidget *sb) {
    SidebarState *st = sb ? sb->owned : NULL;
    int old;
    if (!st) return 0;
    old = ex_ws_sidebar_width(st->ws, st->side);
    ex_ws_set_sidebar(st->ws, st->side, 1, (int)sidebar_limit(st));
    ex_sidebar_sync(sb);
    return old;
}

void ex_sidebar_sync(ExWidget *sb) {
    SidebarState *st = sb->owned;
    float w = (float)ex_ws_sidebar_width(st->ws, st->side), lim = sidebar_limit(st);
    int visible = ex_ws_sidebar_visible(st->ws, st->side);
    sb->hidden = !visible;
    sb->width = ex_size_px(st->ui, w > lim ? lim : w);                 /* a window that shrinks does not push the text out; the width comes back when it grows */
    if (st->divider) st->divider->hidden = !visible;
    if (st->edge) st->edge->hidden = visible;                       /* a hidden sidebar leaves a handle at the window's edge to pull it back */
    if (st->line) st->line->bg_token = st->dragging ? EX_COLOR_ACCENT : EX_COLOR_BORDER;
}

/* Where a drag has reached, as the sidebar's width in pixels at 100%. It is measured from the sidebar's outer edge (the window's edge
 * on the right, the left end of the sidebar on the left), which is noted when the press happens: the layout is up to date then, but
 * several drag events can arrive before the next frame, when the sidebar's own position is not. */
static float drag_width(const SidebarState *st, float x) {
    float scale = ex_renderer_scale(ex_ui_renderer(st->ui));
    if (scale <= 0) scale = 1.0f;
    return (st->side == EX_SIDE_RIGHT ? st->origin - x : x - st->origin) / scale;
}

/* The outer edge of the sidebar, or of its handle when it is hidden. */
static void note_origin(SidebarState *st) {
    int visible = ex_ws_sidebar_visible(st->ws, st->side);
    ExWidget *w = visible ? st->sidebar : st->edge;
    st->keep_width = ex_ws_sidebar_width(st->ws, st->side);
    if (!w) { st->origin = 0; return; }
    st->origin = st->side == EX_SIDE_RIGHT ? w->x + w->w : w->x;
}

/* A drag of a sidebar's edge: wider or narrower as far as the window allows; pulled in nearly all the way it hides (its width is
 * kept for when it comes back), and pulled out again from the window's edge it shows. */
static void drag_to(SidebarState *st, float px) {
    float lim = sidebar_limit(st);
    if (px < 60.0f) {
        if (ex_ws_sidebar_visible(st->ws, st->side)) ex_ws_set_sidebar(st->ws, st->side, 0, st->keep_width);
        return;
    }
    if (px > lim) px = lim;
    ex_ws_set_sidebar(st->ws, st->side, 1, (int)px);
    st->sidebar->width = ex_size_px(st->ui, (float)ex_ws_sidebar_width(st->ws, st->side));
}

/* The strip between a sidebar and the text, and the handle at the window's edge of a hidden sidebar: press and drag. */
static int divider_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    SidebarState *st = user;
    (void)y; (void)under; (void)w;
    if (kind == EX_MOUSE_DOWN) {
        st->dragging = 1;
        note_origin(st);
        return 1;
    }
    if (!st->dragging) return 1;
    if (kind == EX_MOUSE_DRAG) drag_to(st, drag_width(st, x));
    else st->dragging = 0;
    return 1;
}

/* The inner edge of the sidebar itself also grabs (4 px), as before the divider strip existed. */
static int sidebar_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    SidebarState *st = user;
    ExRenderer *r = ex_ui_renderer(st->ui);
    float reach = (float)ex_px(r, 4);
    (void)y; (void)under;
    if (kind == EX_MOUSE_DOWN) {
        float edge = st->side == EX_SIDE_LEFT ? w->x + w->w : w->x;
        if (fabsf(x - edge) <= reach) { st->dragging = 1; note_origin(st); return 1; }
        return 0;
    }
    if (!st->dragging) return 1;
    if (kind == EX_MOUSE_DRAG) drag_to(st, drag_width(st, x));
    else st->dragging = 0;
    return 1;
}

ExWidget *ex_sidebar_divider_create(ExWidget *sidebar) {
    SidebarState *st = sidebar ? sidebar->owned : NULL;
    ExWidget *d, *line;
    if (!st) return NULL;
    d = ex_widget_box(st->ui, 1, "Resize sidebar");
    line = ex_widget_box(st->ui, 1, "Resize line");
    if (!d || !line) return d;
    d->width = ex_size_px(st->ui, 6); d->height = ex_size_grow(); d->align_x = 1;
    d->cursor = 1;                                                      /* the pointer shows it can be dragged sideways */
    d->on_mouse = divider_mouse; d->on_mouse_user = st;
    line->width = ex_size_px(st->ui, 1); line->height = ex_size_grow(); line->bg_token = EX_COLOR_BORDER;
    ex_widget_add(d, line);
    st->divider = d; st->line = line;
    return d;
}

/* The handle of a hidden sidebar, at the window's edge (put it last in the row for the right sidebar, before the left sidebar for the left one). */
ExWidget *ex_sidebar_edge_create(ExWidget *sidebar) {
    SidebarState *st = sidebar ? sidebar->owned : NULL;
    ExWidget *e;
    if (!st) return NULL;
    e = ex_widget_box(st->ui, 1, st->side == EX_SIDE_RIGHT ? "Show right sidebar" : "Show left sidebar");
    if (!e) return NULL;
    e->width = ex_size_px(st->ui, 8); e->height = ex_size_grow();
    e->bg_token = EX_COLOR_BORDER;
    e->cursor = 1;
    e->on_mouse = divider_mouse; e->on_mouse_user = st;
    e->hidden = ex_ws_sidebar_visible(st->ws, st->side);
    st->edge = e;
    return e;
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
    st->sidebar = sb;
    sb->on_mouse = sidebar_mouse; sb->on_mouse_user = st;
    if (content) ex_widget_add(sb, content);
    ex_sidebar_sync(sb);
    return sb;
}
