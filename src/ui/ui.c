#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "exhaust_ui.h"

struct ExUi {
    ExRenderer *r;
    const ExTheme *theme;
    float base_size;
    void *clay_memory;
    Clay_Context *clay;
    ExWidget *focus;
    ExWidget *pressed;
    uint32_t next_id;
    ExWidget *last_root;
    int clay_errors;
    int tab_leaves_editor;   /* Escape was pressed in an editor: the next Tab leaves it */
};

/* ---- construction ---- */

static void clay_error(Clay_ErrorData e) { ExUi *ui = e.userData; if (ui) ui->clay_errors++; }

static Clay_Dimensions measure(Clay_StringSlice text, Clay_TextElementConfig *cfg, void *user) {
    ExUi *ui = user;
    Clay_Dimensions d;
    ExFontMetrics m;
    float scale = ex_renderer_scale(ui->r), size = (float)cfg->fontSize / (scale > 0 ? scale : 1.0f);
    ExFont *f = ex_ui_font(ui, cfg->fontId >> 1, cfg->fontId & 1);
    d.width = ex_text_measure(ui->r, f, size, text.chars, (size_t)text.length);
    d.height = ex_text_metrics(ui->r, f, size, &m) == 0 ? (float)m.line_height : (float)cfg->fontSize;
    return d;
}

ExUi *ex_ui_create(ExRenderer *r, const ExTheme *theme, float base_text_size) {
    ExUi *ui = calloc(1, sizeof *ui);
    Clay_Arena arena;
    Clay_Dimensions dim;
    Clay_ErrorHandler eh;
    if (!ui) return NULL;
    ui->r = r;
    ui->theme = theme;
    ui->base_size = base_text_size > 0 ? base_text_size : 16.0f;
    ui->next_id = 1;
    Clay_SetCurrentContext(NULL); /* a context from an earlier, freed UI must not be read */
    ui->clay_memory = malloc(Clay_MinMemorySize());
    if (!ui->clay_memory) { free(ui); return NULL; }
    arena = Clay_CreateArenaWithCapacityAndMemory(Clay_MinMemorySize(), ui->clay_memory);
    dim.width = 100; dim.height = 100;
    eh.errorHandlerFunction = clay_error;
    eh.userData = ui;
    ui->clay = Clay_Initialize(arena, dim, eh);
    Clay_SetMeasureTextFunction(measure, ui);
    return ui;
}

void ex_ui_free(ExUi *ui) {
    if (!ui) return;
    if (Clay_GetCurrentContext() == ui->clay) Clay_SetCurrentContext(NULL);
    free(ui->clay_memory);
    free(ui);
}

void ex_ui_set_theme(ExUi *ui, const ExTheme *theme) { ui->theme = theme; }
const ExTheme *ex_ui_theme(const ExUi *ui) { return ui->theme; }
ExRenderer *ex_ui_renderer(ExUi *ui) { return ui->r; }

ExFont *ex_ui_font(ExUi *ui, int token, int bold) {
    ExBuiltinFont f = token == EX_FONT_TOKEN_MONO ? (bold ? EX_FONT_MONO_BOLD : EX_FONT_MONO_REGULAR)
                                                  : (bold ? EX_FONT_UI_BOLD : EX_FONT_UI_REGULAR);
    return ex_font_builtin(ex_renderer_fonts(ui->r), f);
}

/* ---- widgets ---- */

static char *dup_str(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = malloc(n + 1);
    if (p) { if (n) memcpy(p, s, n); p[n] = 0; }
    return p;
}

ExAxis ex_size_grow(void) { ExAxis a = {EX_SIZING_GROW, -1, 0}; return a; }
ExAxis ex_size_fit(void) { ExAxis a = {EX_SIZING_FIT, -1, 0}; return a; }
ExAxis ex_size_token(int token, float multiple) { ExAxis a; a.mode = EX_SIZING_FIXED; a.size_token = token; a.value = multiple; return a; }
ExAxis ex_size_percent(float f) { ExAxis a; a.mode = EX_SIZING_PERCENT; a.size_token = -1; a.value = f; return a; }

static ExWidget *new_widget(ExUi *ui, ExWidgetType type, ExRole role, const char *name) {
    ExWidget *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->type = type;
    w->role = role;
    w->name = dup_str(name);
    w->id = ui->next_id++;
    w->width = ex_size_fit();
    w->height = ex_size_fit();
    w->bg_token = w->border_token = w->radius_token = w->text_token = -1;
    w->font_token = EX_FONT_TOKEN_INTERFACE;
    w->text_scale = 1.0f;
    return w;
}

ExWidget *ex_widget_box(ExUi *ui, int vertical, const char *name) {
    ExWidget *w = new_widget(ui, EX_WIDGET_BOX, EX_ROLE_GROUP, name);
    if (w) w->vertical = vertical;
    return w;
}

ExWidget *ex_widget_label(ExUi *ui, const char *text) {
    ExWidget *w = new_widget(ui, EX_WIDGET_LABEL, EX_ROLE_LABEL, text);
    if (w) { w->text = dup_str(text); w->text_token = EX_COLOR_TEXT; }
    return w;
}

ExWidget *ex_widget_button(ExUi *ui, const char *text, ExClickFn fn, void *user) {
    ExWidget *w = new_widget(ui, EX_WIDGET_BUTTON, EX_ROLE_BUTTON, text);
    if (!w) return NULL;
    w->text = dup_str(text);
    w->text_token = EX_COLOR_TEXT;
    w->bg_token = EX_COLOR_SURFACE;
    w->border_token = EX_COLOR_BORDER;
    w->radius_token = EX_SIZE_RADIUS;
    w->pad_x = 1.5f;
    w->pad_y = 0.75f;
    w->focusable = 1;
    w->on_click = fn;
    w->on_click_user = user;
    return w;
}

ExWidget *ex_widget_editor(ExUi *ui, ExDoc *doc, const char *name) {
    ExWidget *w = new_widget(ui, EX_WIDGET_EDITOR, EX_ROLE_TEXT_INPUT, name);
    if (!w) return NULL;
    w->editor = ex_editor_create(doc);
    if (!w->editor) { free(w->name); free(w); return NULL; }
    w->width = ex_size_grow();
    w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->font_token = EX_FONT_TOKEN_TEXT;
    w->focusable = 1;
    w->clip = 1;
    return w;
}

void ex_widget_add(ExWidget *parent, ExWidget *child) {
    child->parent = parent;
    child->next = NULL;
    if (parent->last_child) parent->last_child->next = child; else parent->first_child = child;
    parent->last_child = child;
}

void ex_widget_set_text(ExWidget *w, const char *text) { free(w->text); w->text = dup_str(text); }

void ex_widget_free(ExWidget *w) {
    while (w) {
        ExWidget *n = w->next;
        ex_widget_free(w->first_child);
        ex_editor_free(w->editor);
        free(w->name);
        free(w->text);
        free(w);
        w = n;
    }
}

size_t ex_widget_count(const ExWidget *w) {
    size_t n = 0;
    for (; w; w = w->next) n += 1 + ex_widget_count(w->first_child);
    return n;
}

/* ---- layout ---- */

static Clay_Color dummy_color(void) { Clay_Color c; c.r = c.g = c.b = c.a = 1.0f; return c; } /* "has a background"; the real color comes from the theme when drawing */

static float token_px(const ExUi *ui, int size_token) { return (float)ex_theme_size(ui->theme, (ExSizeToken)size_token); }

static float axis_px(const ExUi *ui, const ExAxis *a) {
    float unit = (a->size_token >= 0 ? token_px(ui, a->size_token) : token_px(ui, EX_SIZE_SPACING));
    return unit * a->value * ex_renderer_scale(ui->r);
}

static Clay_SizingAxis sizing(const ExUi *ui, const ExAxis *a) {
    switch (a->mode) {
    case EX_SIZING_GROW: return CLAY_SIZING_GROW(0);
    case EX_SIZING_FIXED: return CLAY_SIZING_FIXED(axis_px(ui, a));
    case EX_SIZING_PERCENT: return CLAY_SIZING_PERCENT(a->value);
    default: return CLAY_SIZING_FIT(0);
    }
}

static uint16_t units_px(const ExUi *ui, float units) {
    float v = units * token_px(ui, EX_SIZE_SPACING) * ex_renderer_scale(ui->r);
    return (uint16_t)(v < 0 ? 0 : (v > 60000 ? 60000 : lroundf(v)));
}

static Clay_ElementId id_of(const ExWidget *w) {
    Clay_String s;
    s.isStaticallyAllocated = true;
    s.length = 1;
    s.chars = "w";
    return Clay__HashStringWithOffset(s, w->id, 0);
}

static void build(ExUi *ui, ExWidget *w) {
    Clay_ElementDeclaration d;
    ExWidget *c;
    memset(&d, 0, sizeof d);
    d.layout.layoutDirection = w->vertical ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;
    d.layout.sizing.width = sizing(ui, &w->width);
    d.layout.sizing.height = sizing(ui, &w->height);
    d.layout.padding.left = d.layout.padding.right = units_px(ui, w->pad_x);
    d.layout.padding.top = d.layout.padding.bottom = units_px(ui, w->pad_y);
    d.layout.childGap = units_px(ui, w->gap);
    if (w->bg_token >= 0) d.backgroundColor = dummy_color();
    if (w->radius_token >= 0) {
        float rad = token_px(ui, w->radius_token) * ex_renderer_scale(ui->r);
        d.cornerRadius.topLeft = d.cornerRadius.topRight = d.cornerRadius.bottomLeft = d.cornerRadius.bottomRight = rad;
    }
    if (w->border_token >= 0) {
        uint16_t bw = (uint16_t)ex_px(ui->r, token_px(ui, EX_SIZE_BORDER_WIDTH));
        d.border.color = dummy_color();
        d.border.width.left = d.border.width.right = d.border.width.top = d.border.width.bottom = bw;
    }
    if (w->clip) { d.clip.horizontal = true; d.clip.vertical = true; }
    d.userData = w;
    if (w->type == EX_WIDGET_EDITOR) d.custom.customData = w;

    Clay__OpenElementWithId(id_of(w));
    Clay__ConfigureOpenElement(d);
    if ((w->type == EX_WIDGET_LABEL || w->type == EX_WIDGET_BUTTON) && w->text) {
        Clay_String s;
        Clay_TextElementConfig tc;
        memset(&tc, 0, sizeof tc);
        s.isStaticallyAllocated = false;
        s.length = (int32_t)strlen(w->text);
        s.chars = w->text;
        tc.userData = w;
        tc.fontId = (uint16_t)(w->font_token * 2 + (w->bold ? 1 : 0));
        tc.fontSize = (uint16_t)ex_px(ui->r, ui->base_size * w->text_scale);
        tc.wrapMode = CLAY_TEXT_WRAP_NONE;
        tc.textColor = dummy_color();
        Clay__OpenTextElement(s, tc);
    }
    for (c = w->first_child; c; c = c->next) build(ui, c);
    Clay__CloseElement();
}

static void store_rects(ExUi *ui, ExWidget *w) {
    for (; w; w = w->next) {
        Clay_ElementData d = Clay_GetElementData(id_of(w));
        w->x = d.boundingBox.x; w->y = d.boundingBox.y; w->w = d.boundingBox.width; w->h = d.boundingBox.height;
        store_rects(ui, w->first_child);
    }
}

void ex_ui_layout(ExUi *ui, ExWidget *root, float width, float height) {
    Clay_Dimensions dim;
    ExWidget *e;
    Clay_SetCurrentContext(ui->clay);
    dim.width = width; dim.height = height;
    Clay_SetLayoutDimensions(dim);
    Clay_BeginLayout();
    build(ui, root);
    (void)Clay_EndLayout(0.0f);
    root->x = 0; root->y = 0;
    store_rects(ui, root);
    ui->last_root = root;
    /* editors need their text area */
    {
        ExWidget *stack[64];
        int sp = 0;
        stack[sp++] = root;
        while (sp > 0) {
            e = stack[--sp];
            for (; e; e = e->next) {
                if (e->type == EX_WIDGET_EDITOR && e->editor) {
                    ExFont *f = ex_ui_font(ui, e->font_token, 0);
                    ex_editor_set_font(e->editor, f, ui->base_size, ex_theme_size(ui->theme, EX_SIZE_LINE_HEIGHT));
                    ex_editor_set_viewport(e->editor, e->x, e->y, e->w, e->h);
                }
                if (e->first_child && sp < 63) stack[sp++] = e->first_child;
            }
        }
    }
}

/* ---- drawing ---- */

static const ExTheme *t_theme(const ExUi *ui) { return ui->theme; }

static void draw_widget(ExUi *ui, ExWidget *w, int blink_on) {
    const ExTheme *t = ui->theme;
    ExRenderer *r = ui->r;
    float rad = w->radius_token >= 0 ? token_px(ui, w->radius_token) * ex_renderer_scale(r) : 0.0f;
    ExWidget *c;
    if (w->bg_token >= 0) {
        int token = w->bg_token;
        if (w->type == EX_WIDGET_BUTTON) token = w->pressed ? EX_COLOR_ACCENT : (w->hover ? EX_COLOR_SURFACE_HOVER : w->bg_token);
        ex_fill_round_rect(r, w->x, w->y, w->w, w->h, rad, ex_theme_color(t, (ExColorToken)token));
    }
    if (w->border_token >= 0) {
        ExColor bc = ex_theme_color(t, (ExColorToken)w->border_token);
        if (w->focusable && ui->focus == w) bc = ex_theme_color(t, EX_COLOR_ACCENT);
        ex_stroke_round_rect(r, w->x, w->y, w->w, w->h, rad, (float)ex_px(r, token_px(ui, EX_SIZE_BORDER_WIDTH)), bc);
    }
    if (w->clip) ex_set_clip(r, (int)w->x, (int)w->y, (int)ceilf(w->w), (int)ceilf(w->h));
    if ((w->type == EX_WIDGET_LABEL || w->type == EX_WIDGET_BUTTON) && w->text && w->text_token >= 0) {
        ExFont *f = ex_ui_font(ui, w->font_token, w->bold);
        ExFontMetrics m;
        float size = ui->base_size * w->text_scale, tw = ex_text_measure(r, f, size, w->text, strlen(w->text)), x = w->x, y = w->y;
        if (ex_text_metrics(r, f, size, &m) == 0) {
            if (w->type == EX_WIDGET_BUTTON) { x = w->x + (w->w - tw) / 2.0f; y = w->y + (w->h - (float)m.line_height) / 2.0f; }
            ex_text_draw(r, f, size, x, y, w->text, strlen(w->text), ex_theme_color(t, (ExColorToken)w->text_token));
        }
    }
    if (w->type == EX_WIDGET_EDITOR && w->editor) {
        ex_editor_draw(w->editor, r, t, ui->focus == w, blink_on);
    }
    for (c = w->first_child; c; c = c->next) draw_widget(ui, c, blink_on);
    if (w->clip) ex_set_clip(r, 0, 0, -1, -1);
    if (w->focusable && ui->focus == w) { /* the visible focus ring, drawn last so nothing covers it */
        float t = 2.0f * (float)ex_px(r, token_px(ui, EX_SIZE_BORDER_WIDTH));
        ex_stroke_round_rect(r, w->x, w->y, w->w, w->h, rad, t, ex_theme_color(t_theme(ui), EX_COLOR_ACCENT));
    }
}

void ex_ui_draw(ExUi *ui, ExWidget *root, int blink_on) { draw_widget(ui, root, blink_on); }

/* ---- input ---- */

ExWidget *ex_ui_widget_at(ExWidget *w, float x, float y) {
    ExWidget *best = NULL, *c;
    if (!w || x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h) return NULL;
    best = w;
    for (c = w->first_child; c; c = c->next) {
        ExWidget *hit = ex_ui_widget_at(c, x, y);
        if (hit) best = hit; /* later siblings are drawn on top */
    }
    return best;
}

ExWidget *ex_ui_focus(const ExUi *ui) { return ui->focus; }
void ex_ui_set_focus(ExUi *ui, ExWidget *w) { ui->focus = w && w->focusable ? w : NULL; }

static void clear_hover(ExWidget *w) {
    for (; w; w = w->next) { w->hover = 0; clear_hover(w->first_child); }
}

void ex_ui_mouse(ExUi *ui, ExWidget *root, ExMouse kind, float x, float y, int mods, int clicks) {
    ExWidget *hit = ex_ui_widget_at(root, x, y), *target = hit;
    while (target && !target->focusable && target->type != EX_WIDGET_BUTTON) target = target->parent;
    clear_hover(root);
    if (target && target->type == EX_WIDGET_BUTTON) target->hover = 1;
    if (kind == EX_MOUSE_DOWN) {
        ui->focus = target && target->focusable ? target : NULL;
        if (target && target->type == EX_WIDGET_BUTTON) { target->pressed = 1; ui->pressed = target; }
    } else if (kind == EX_MOUSE_UP && ui->pressed) {
        ExWidget *p = ui->pressed;
        p->pressed = 0;
        ui->pressed = NULL;
        if (target == p && p->on_click) p->on_click(p, p->on_click_user);
    }
    if (ui->focus && ui->focus->type == EX_WIDGET_EDITOR && ui->focus->editor)
        if (kind == EX_MOUSE_DRAG || kind == EX_MOUSE_UP || (kind == EX_MOUSE_DOWN && target == ui->focus))
            ex_editor_mouse(ui->focus->editor, ui->r, kind, x, y, mods, clicks);
}

static size_t collect_focusable(ExWidget *w, ExWidget **out, size_t n, size_t cap) {
    for (; w; w = w->next) {
        if (w->focusable) { if (n < cap) out[n] = w; n++; }
        n = collect_focusable(w->first_child, out, n, cap);
    }
    return n;
}

ExWidget *ex_ui_focus_move(ExUi *ui, int dir) {
    size_t n, i, cur = (size_t)-1;
    ExWidget **list;
    if (!ui->last_root) return NULL;
    n = ex_widget_count(ui->last_root);
    list = malloc((n ? n : 1) * sizeof *list);
    if (!list) return NULL;
    n = collect_focusable(ui->last_root, list, 0, n);
    if (n == 0) { free(list); return NULL; }
    for (i = 0; i < n; i++) if (list[i] == ui->focus) cur = i;
    if (cur == (size_t)-1) cur = dir > 0 ? n - 1 : 0, i = dir > 0 ? 0 : n - 1;
    else i = dir > 0 ? (cur + 1) % n : (cur + n - 1) % n;
    ui->focus = list[i];
    ui->tab_leaves_editor = 0;
    free(list);
    return ui->focus;
}

void ex_ui_key(ExUi *ui, ExKey key, int mods) {
    ExWidget *f = ui->focus;
    int in_editor = f && f->type == EX_WIDGET_EDITOR && f->editor;
    int shift = (mods & EX_MOD_SHIFT) != 0, prim = (mods & EX_MOD_PRIMARY) != 0;
    if (key == EX_KEY_TAB && (!in_editor || prim || ui->tab_leaves_editor)) {
        ex_ui_focus_move(ui, shift ? -1 : 1);
        return;
    }
    if (in_editor) {
        /* Escape in an editor first collapses several cursors or a selection; with a single
         * caret it arms the next Tab to leave the editor instead of typing a tab */
        if (key == EX_KEY_ESCAPE) {
            ExDoc *d = ex_editor_doc(f->editor);
            ExSel s = ex_doc_sel(d, 0);
            ui->tab_leaves_editor = ex_doc_sel_count(d) == 1 && s.anchor == s.head;
        } else if (key != EX_KEY_TAB) {
            ui->tab_leaves_editor = 0;
        }
        if (key != EX_KEY_SPACE) ex_editor_key(f->editor, ui->r, key, mods);
    } else if (f && f->type == EX_WIDGET_BUTTON && (key == EX_KEY_ENTER || key == EX_KEY_SPACE) && f->on_click) {
        f->on_click(f, f->on_click_user);
    }
}

void ex_ui_text_input(ExUi *ui, const char *utf8, size_t len) {
    ExWidget *f = ui->focus;
    if (f && f->type == EX_WIDGET_EDITOR) { ui->tab_leaves_editor = 0; ex_editor_text_input(f->editor, utf8, len); }
}
