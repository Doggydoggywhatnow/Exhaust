#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "exhaust_ui.h"
#include "reading_view.h"

struct ExUi {
    ExRenderer *r;
    const ExTheme *theme;
    float base_size;
    void *clay_memory;
    Clay_Context *clay;
    ExWidget *focus;
    ExWidget *pressed;
    ExWidget *captured;      /* took the press through on_mouse; gets the drag and release */
    ExWidget *tree_drag;     /* a tree a row of which is being pressed or dragged: gets the drag and release wherever the pointer is */
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
ExAxis ex_size_px(const ExUi *ui, float px) {
    ExAxis a;
    float unit = (float)ex_theme_size(ui->theme, EX_SIZE_SPACING);
    a.mode = EX_SIZING_FIXED;
    a.size_token = -1;
    a.value = unit > 0 ? px / unit : px;
    return a;
}
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

void ex_widget_set_icon(ExWidget *b, int icon) {
    if (!b) return;
    b->icon = icon;
    b->width = ex_size_token(EX_SIZE_SPACING, 3.5f);
    b->height = ex_size_token(EX_SIZE_SPACING, 3.5f);
    b->pad_x = b->pad_y = 0;
    if (icon) { char *e = dup_str(""); if (e) { free(b->text); b->text = e; } }
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

static int reading_mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    (void)under; (void)user;
    return w->reading ? ex_readingview_mouse(w->reading, kind, x, y) : 0;
}

ExWidget *ex_widget_reading(ExUi *ui, const char *text, size_t len, const char *name) {
    ExWidget *w = new_widget(ui, EX_WIDGET_READING, EX_ROLE_LABEL, name);
    if (!w) return NULL;
    w->reading = ex_readingview_create(text, len);
    if (!w->reading) { free(w->name); free(w); return NULL; }
    w->width = ex_size_grow();
    w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1;
    w->clip = 1;
    w->on_mouse = reading_mouse_hook;
    return w;
}
void ex_reading_set_text(ExWidget *w, const char *text, size_t len) { if (w->reading) ex_readingview_set_text(w->reading, text, len); }
void ex_reading_set_link_handler(ExWidget *w, void (*fn)(ExRSpanKind kind, const char *target, void *user), void *user) { if (w->reading) ex_readingview_set_link_handler(w->reading, fn, user); }
int ex_reading_internal_link_at(ExWidget *w, float x, float y, char *target, size_t cap) { return w->reading ? ex_readingview_internal_link_at(w->reading, x, y, target, cap) : 0; }

void ex_reading_scroll(ExWidget *w, float dy) { if (w->reading) ex_readingview_scroll(w->reading, dy); }
float ex_reading_scroll_y(const ExWidget *w) { return w->reading ? ex_readingview_scroll_y(w->reading) : 0; }
void ex_reading_set_scroll(ExWidget *w, float y) { if (w->reading) ex_readingview_set_scroll(w->reading, y); }

void ex_widget_add(ExWidget *parent, ExWidget *child) {
    child->parent = parent;
    child->next = NULL;
    if (parent->last_child) parent->last_child->next = child; else parent->first_child = child;
    parent->last_child = child;
}

void ex_widget_set_text(ExWidget *w, const char *text) { free(w->text); w->text = dup_str(text); }

float ex_ui_base_size(const ExUi *ui) { return ui->base_size; }

void ex_widget_free(ExWidget *w) {
    while (w) {
        ExWidget *n = w->next;
        ex_widget_free(w->first_child);
        ex_editor_free(w->editor);
        ex_readingview_free(w->reading);
        if (w->owned && w->free_owned) w->free_owned(w->owned);
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

ExWidget *ex_widget_tree(ExUi *ui, ExVault *vault, const char *name,
                         void (*on_open)(ExWidget *w, const char *rel, void *user), void *user) {
    ExWidget *w = new_widget(ui, EX_WIDGET_TREE, EX_ROLE_TREE, name);
    if (!w) return NULL;
    w->vault = vault;
    w->tree_drag_row = -1; w->tree_drop_row = -1;
    w->on_tree_open = on_open;
    w->on_tree_open_user = user;
    w->width = ex_size_grow();
    w->height = ex_size_grow();
    w->focusable = 1;
    w->clip = 1;
    w->font_token = EX_FONT_TOKEN_INTERFACE;
    return w;
}

void ex_widget_remove(ExWidget *parent, ExWidget *child) {
    ExWidget *p;
    if (parent->first_child == child) parent->first_child = child->next;
    else {
        for (p = parent->first_child; p && p->next != child; p = p->next) {}
        if (p) p->next = child->next;
    }
    if (parent->last_child == child) {
        for (p = parent->first_child; p && p->next; p = p->next) {}
        parent->last_child = p;
    }
    child->next = NULL;
    child->parent = NULL;
}

void ex_widget_insert_before(ExWidget *parent, ExWidget *child, ExWidget *before) {
    ExWidget *p;
    child->parent = parent;
    if (!before) { ex_widget_add(parent, child); return; }
    if (parent->first_child == before) { child->next = before; parent->first_child = child; return; }
    for (p = parent->first_child; p && p->next != before; p = p->next) {}
    if (p) { child->next = before; p->next = child; } else ex_widget_add(parent, child);
}

const ExNode *ex_tree_selected(ExWidget *w) {
    if (!w->vault || w->tree_unselected) return NULL;
    return ex_vault_row(w->vault, w->tree_selected, NULL);
}

void ex_tree_select_path(ExWidget *w, const char *rel) {
    long row;
    if (!w->vault) return;
    row = ex_vault_reveal(w->vault, rel);
    if (row >= 0) { w->tree_selected = (size_t)row; w->tree_unselected = 0; }
}

/* What a row shows: notes without ".md", as in the screenshots. */
static void tree_label(const ExNode *n, char *out, size_t cap) {
    size_t len = strlen(n->name);
    if (n->kind == EX_KIND_MARKDOWN && len > 3 && !strcmp(n->name + len - 3, ".md")) len -= 3;
    snprintf(out, cap, "%.*s%s", (int)len, n->name, n->link_loop ? "  (loop)" : "");
}

void ex_tree_set_rename_handler(ExWidget *w, int (*fn)(ExWidget *, const char *, float, float, float, float, void *), void *user) {
    w->on_tree_rename = fn;
    w->on_tree_rename_user = user;
}

static float tree_row_h(ExUi *ui, const ExWidget *w) {
    ExFontMetrics m;
    ExFont *f = ex_ui_font(ui, w->font_token, 0);
    if (ex_text_metrics(ui->r, f, ui->base_size, &m)) return 24.0f;
    return (float)m.line_height * 1.45f;
}

static void tree_ensure_visible(ExUi *ui, ExWidget *w) {
    float rh = tree_row_h(ui, w), top = (float)w->tree_selected * rh, bottom = top + rh;
    size_t rows = w->vault ? ex_vault_row_count(w->vault) : 0;
    float total = (float)rows * rh, max_scroll = total > w->h ? total - w->h : 0;
    if (top < w->tree_scroll) w->tree_scroll = top;
    else if (bottom > w->tree_scroll + w->h) w->tree_scroll = bottom - w->h;
    if (w->tree_scroll > max_scroll) w->tree_scroll = max_scroll;
    if (w->tree_scroll < 0) w->tree_scroll = 0;
}

static void tree_clamp(ExWidget *w) {
    size_t rows = w->vault ? ex_vault_row_count(w->vault) : 0;
    if (rows == 0) w->tree_selected = 0;
    else if (w->tree_selected >= rows) w->tree_selected = rows - 1;
}

static void tree_activate(ExWidget *w) {
    const ExNode *n;
    tree_clamp(w);
    n = ex_tree_selected(w);
    if (!n) return;
    if (n->is_dir) ex_vault_toggle(w->vault, n->rel);
    else if (w->on_tree_open) w->on_tree_open(w, n->rel, w->on_tree_open_user);
}

static void tree_key(ExUi *ui, ExWidget *w, ExKey key) {
    size_t rows;
    const ExNode *n;
    if (!w->vault) return;
    if (w->tree_unselected && (key == EX_KEY_UP || key == EX_KEY_DOWN || key == EX_KEY_HOME || key == EX_KEY_END)) w->tree_unselected = 0;
    rows = ex_vault_row_count(w->vault);
    tree_clamp(w);
    n = ex_tree_selected(w);
    switch (key) {
    case EX_KEY_UP: if (w->tree_selected > 0) w->tree_selected--; break;
    case EX_KEY_DOWN: if (w->tree_selected + 1 < rows) w->tree_selected++; break;
    case EX_KEY_HOME: w->tree_selected = 0; break;
    case EX_KEY_END: if (rows) w->tree_selected = rows - 1; break;
    case EX_KEY_PAGE_UP: { size_t page = (size_t)(w->h / tree_row_h(ui, w)); w->tree_selected = w->tree_selected > page ? w->tree_selected - page : 0; break; }
    case EX_KEY_PAGE_DOWN: { size_t page = (size_t)(w->h / tree_row_h(ui, w)); w->tree_selected = w->tree_selected + page < rows ? w->tree_selected + page : (rows ? rows - 1 : 0); break; }
    case EX_KEY_RIGHT:
        if (n && n->is_dir && !n->expanded) ex_vault_toggle(w->vault, n->rel);
        else if (w->tree_selected + 1 < rows && n && n->is_dir) w->tree_selected++;   /* into the folder */
        break;
    case EX_KEY_LEFT:
        if (n && n->is_dir && n->expanded) ex_vault_toggle(w->vault, n->rel);
        else if (n && n->parent && n->parent->parent) ex_tree_select_path(w, n->parent->rel);   /* to its folder */
        break;
    case EX_KEY_ENTER:
    case EX_KEY_SPACE: tree_activate(w); break;
    default: break;
    }
    tree_clamp(w);
    tree_ensure_visible(ui, w);
}

static long tree_row_at(ExUi *ui, const ExWidget *w, float y) {
    float rh = tree_row_h(ui, w);
    long row = (long)((y - w->y + w->tree_scroll) / rh);
    if (y < w->y || row < 0 || !w->vault || (size_t)row >= ex_vault_row_count(w->vault)) return -1;
    return row;
}

const ExNode *ex_tree_node_at(ExUi *ui, ExWidget *w, float y, int select) {
    long row = tree_row_at(ui, w, y);
    if (row < 0) return NULL;
    if (select) w->tree_selected = (size_t)row;
    return ex_vault_row(w->vault, (size_t)row, NULL);
}

void ex_tree_set_drop_handler(ExWidget *w, void (*fn)(ExWidget *, const char *, const char *, void *), void *user) {
    w->on_tree_drop = fn;
    w->on_tree_drop_user = user;
}

/* The folder a row dragged to the row `over` would go into: that folder, or the folder of the file; "" for the empty area. NULL: nowhere. */
static const char *drop_folder(ExWidget *w, long over) {
    const ExNode *from = w->tree_drag_row >= 0 ? ex_vault_row(w->vault, (size_t)w->tree_drag_row, NULL) : NULL, *n;
    const char *dest;
    size_t fl;
    if (!from) return NULL;
    if (over < 0) dest = "";
    else {
        n = ex_vault_row(w->vault, (size_t)over, NULL);
        if (!n) return NULL;
        dest = n->is_dir ? n->rel : (n->parent ? n->parent->rel : "");
    }
    fl = strlen(from->rel);
    if (!strcmp(dest, from->rel)) return NULL;                                        /* onto itself */
    if (from->is_dir && !strncmp(dest, from->rel, fl) && dest[fl] == '/') return NULL; /* into its own folder */
    {
        const char *slash = strrchr(from->rel, '/');
        size_t pl = slash ? (size_t)(slash - from->rel) : 0;
        if (strlen(dest) == pl && !strncmp(dest, from->rel, pl)) return NULL;          /* already there */
    }
    return dest;
}

static void tree_drag_event(ExUi *ui, ExWidget *w, ExMouse kind, float x, float y) {
    long over;
    if (!w->vault || w->tree_drag_row < 0) { ui->tree_drag = NULL; return; }
    over = tree_row_at(ui, w, y);
    if (x < w->x || x > w->x + w->w || y > w->y + w->h || y < w->y) over = -2;     /* outside the tree: no drop */
    w->tree_cur_x = x; w->tree_cur_y = y;
    if (kind == EX_MOUSE_DRAG) {
        float dx = x - w->tree_drag_x, dy = y - w->tree_drag_y;
        if (!w->tree_dragging && dx * dx + dy * dy > (float)(ex_px(ui->r, 6) * ex_px(ui->r, 6))) w->tree_dragging = 1;
        w->tree_drop_row = -1;
        if (w->tree_dragging && over != -2 && drop_folder(w, over)) w->tree_drop_row = over < 0 ? -3 : over;   /* -3: the vault folder */
        return;
    }
    /* release */
    if (w->tree_dragging && over != -2 && w->on_tree_drop) {
        const char *dest = drop_folder(w, over);
        const ExNode *from = ex_vault_row(w->vault, (size_t)w->tree_drag_row, NULL);
        if (dest && from) {
            char *src = dup_str(from->rel), *dst = dup_str(dest);
            if (src && dst) w->on_tree_drop(w, src, dst, w->on_tree_drop_user);
            free(src); free(dst);
        }
    }
    w->tree_dragging = 0; w->tree_drag_row = -1; w->tree_drop_row = -1;
    ui->tree_drag = NULL;
}

static void tree_mouse(ExUi *ui, ExWidget *w, ExMouse kind, float x, float y, int clicks) {
    float rh = tree_row_h(ui, w);
    long row;
    int depth = 0;
    const ExNode *n;
    if (kind != EX_MOUSE_DOWN || !w->vault) return;
    row = (long)((y - w->y + w->tree_scroll) / rh);
    if (row < 0 || (size_t)row >= ex_vault_row_count(w->vault)) { w->tree_unselected = 1; return; }   /* the empty space: nothing is selected, so new items go to the top level */
    w->tree_selected = (size_t)row;
    w->tree_unselected = 0;
    n = ex_vault_row(w->vault, (size_t)row, &depth);
    if (n && n->is_dir) {
        float indent = (float)ex_px(ui->r, 16);
        float chevron_end = w->x + (float)ex_px(ui->r, 8) + (float)depth * indent + indent;
        if (clicks >= 2 && w->on_tree_rename && x >= chevron_end) {                 /* a double click on the name of a folder edits it in place */
            char label[320];
            ExFont *f = ex_ui_font(ui, w->font_token, 0);
            float tw;
            tree_label(n, label, sizeof label);
            tw = ex_text_measure(ui->r, f, ui->base_size, label, strlen(label));
            if (x <= chevron_end + tw + (float)ex_px(ui->r, 6) &&
                w->on_tree_rename(w, n->rel, chevron_end - (float)ex_px(ui->r, 4), w->y + (float)row * rh - w->tree_scroll, w->x + w->w - chevron_end + (float)ex_px(ui->r, 4) - (float)ex_px(ui->r, 6), rh, w->on_tree_rename_user)) {
                tree_clamp(w);
                return;
            }
        }
        if (x < chevron_end || clicks >= 2) ex_vault_toggle(w->vault, n->rel);     /* the chevron, or a double click */
    } else if (n && clicks >= 2) {
        tree_activate(w);
    }
    tree_clamp(w);
    if (w->on_tree_drop && n && n->rel[0]) {                                        /* a press may become a drag */
        w->tree_drag_row = row; w->tree_drop_row = -1; w->tree_dragging = 0; w->tree_drag_x = x; w->tree_drag_y = y;
        ui->tree_drag = w;
    }
}

static void tree_draw(ExUi *ui, ExWidget *w) {
    const ExTheme *t = ui->theme;
    ExRenderer *r = ui->r;
    ExFont *f = ex_ui_font(ui, w->font_token, 0);
    ExFontMetrics m;
    float rh = tree_row_h(ui, w), indent = (float)ex_px(r, 16), pad = (float)ex_px(r, 8);
    size_t rows, i, first, last;
    ExColor text = ex_theme_color(t, EX_COLOR_TEXT), muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    if (!w->vault || ex_text_metrics(r, f, ui->base_size, &m)) return;
    rows = ex_vault_row_count(w->vault);
    tree_clamp(w);
    first = (size_t)(w->tree_scroll / rh);
    last = first + (size_t)(w->h / rh) + 1;
    for (i = first; i <= last && i < rows; i++) {
        int depth = 0;
        const ExNode *n = ex_vault_row(w->vault, i, &depth);
        float y = w->y + (float)i * rh - w->tree_scroll, x = w->x + pad + (float)depth * indent;
        float cy = y + rh / 2.0f, s = (float)ex_px(r, 3);
        if (!n) break;
        if (w->tree_dragging && (long)i == w->tree_drop_row)
            ex_stroke_round_rect(r, w->x + (float)ex_px(r, 2), y, w->w - (float)ex_px(r, 4), rh, (float)ex_px(r, ex_theme_size(t, EX_SIZE_RADIUS)), (float)ex_px(r, 1) + 1.0f, ex_theme_color(t, EX_COLOR_ACCENT));
        if (i == w->tree_selected && !w->tree_unselected) {
            ExColor sel = ex_theme_color(t, ui->focus == w ? EX_COLOR_SELECTION : EX_COLOR_SURFACE_HOVER);
            ex_fill_round_rect(r, w->x + (float)ex_px(r, 2), y, w->w - (float)ex_px(r, 4), rh, (float)ex_px(r, ex_theme_size(t, EX_SIZE_RADIUS)), sel);
        }
        (void)s;
        if (n->is_dir) {                                  /* a chevron: pointing right when closed, down when open */
            float side = indent * 0.9f;
            ex_icon_draw(r, n->expanded ? EX_ICON_CHEVRON_DOWN : EX_ICON_CHEVRON_RIGHT, x + indent / 2.0f - side / 2.0f, cy - side / 2.0f, side, side, ex_theme_color(t, EX_COLOR_TEXT_MUTED));
        }
        {
            char label[320];
            tree_label(n, label, sizeof label);
            ex_text_draw(r, f, ui->base_size, x + indent, y + (rh - (float)m.line_height) / 2.0f, label, strlen(label),
                         n->excluded || n->link_loop ? muted : text);
        }
    }
    if (w->tree_dragging && w->tree_drag_row >= 0) {                    /* the dragged name follows the pointer; the vault folder is marked when it is the target */
        const ExNode *dn = ex_vault_row(w->vault, (size_t)w->tree_drag_row, NULL);
        if (w->tree_drop_row == -3)
            ex_stroke_round_rect(r, w->x + (float)ex_px(r, 1), w->y + (float)ex_px(r, 1), w->w - (float)ex_px(r, 2), w->h - (float)ex_px(r, 2), (float)ex_px(r, ex_theme_size(t, EX_SIZE_RADIUS)), (float)ex_px(r, 1) + 1.0f, ex_theme_color(t, EX_COLOR_ACCENT));
        if (dn) {
            char dl[320];
            float tw, gx = w->tree_cur_x + (float)ex_px(r, 12), gy = w->tree_cur_y + (float)ex_px(r, 8), pd = (float)ex_px(r, 6);
            tree_label(dn, dl, sizeof dl);
            tw = ex_text_measure(r, f, ui->base_size, dl, strlen(dl));
            ex_fill_round_rect(r, gx, gy, tw + 2 * pd, (float)m.line_height + pd, (float)ex_px(r, ex_theme_size(t, EX_SIZE_RADIUS)), ex_theme_color(t, EX_COLOR_SURFACE_HOVER));
            ex_text_draw(r, f, ui->base_size, gx + pd, gy + pd / 2, dl, strlen(dl), text);
        }
    }
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
    d.layout.childAlignment.x = w->align_x == 1 ? CLAY_ALIGN_X_CENTER : w->align_x == 2 ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT;
    d.layout.childAlignment.y = w->align_y == 1 ? CLAY_ALIGN_Y_CENTER : w->align_y == 2 ? CLAY_ALIGN_Y_BOTTOM : CLAY_ALIGN_Y_TOP;
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
    for (c = w->first_child; c; c = c->next) if (!c->hidden) build(ui, c);
    Clay__CloseElement();
}

static void store_rects(ExUi *ui, ExWidget *w) {
    for (; w; w = w->next) {
        Clay_ElementData d = Clay_GetElementData(id_of(w));
        if (w->hidden) { w->x = w->y = w->w = w->h = 0; store_rects(ui, w->first_child); continue; }
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
                if (e->type == EX_WIDGET_READING && e->reading) ex_readingview_set_viewport(e->reading, e->x, e->y, e->w, e->h);
                if (e->type == EX_WIDGET_EDITOR && e->editor) {
                    ExFont *f = ex_ui_font(ui, e->font_token, e->bold);
                    ex_editor_set_font(e->editor, f, ui->base_size * (e->text_scale > 0 ? e->text_scale : 1.0f), ex_theme_size(ui->theme, EX_SIZE_LINE_HEIGHT));
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
    if (w->hidden) return;
    if (w->bg_token >= 0) {
        int token = w->bg_token;
        if (w->type == EX_WIDGET_BUTTON) token = w->pressed ? EX_COLOR_ACCENT : (w->hover ? EX_COLOR_SURFACE_HOVER : w->bg_token);
        if (w->selected && !w->pressed) token = EX_COLOR_BACKGROUND;
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
            if (w->type == EX_WIDGET_BUTTON && w->menu_item) { x = w->x + (float)ex_px(r, 12) + (float)m.line_height + (float)ex_px(r, 12); y = w->y + (w->h - (float)m.line_height) / 2.0f; }
            else if (w->type == EX_WIDGET_BUTTON) { x = w->x + (w->w - tw) / 2.0f; y = w->y + (w->h - (float)m.line_height) / 2.0f; }
            ex_text_draw(r, f, size, x, y, w->text, strlen(w->text), ex_theme_color(t, (ExColorToken)w->text_token));
        }
    }
    if (w->type == EX_WIDGET_BUTTON && w->menu_item) {                  /* a popup menu entry: the icon in its own column, a chevron for a submenu */
        ExFontMetrics m;
        ExFont *f = ex_ui_font(ui, w->font_token, 0);
        if (ex_text_metrics(r, f, ui->base_size * w->text_scale, &m) == 0) {
            float is = (float)m.line_height, ix = w->x + (float)ex_px(r, 12), iy = w->y + (w->h - is) / 2.0f;
            ExColor ic = ex_theme_color(t, (ExColorToken)(w->text_token >= 0 ? w->text_token : EX_COLOR_TEXT));
            if (w->icon) ex_icon_draw(r, w->icon, ix, iy, is, is, ic);
            if (w->submenu) {
                float cx = w->x + w->w - (float)ex_px(r, 16), cy = w->y + w->h / 2.0f, q = (float)ex_px(r, 4);
                ex_fill_triangle(r, cx - q / 2, cy - q, cx - q / 2, cy + q, cx + q, cy, ex_theme_color(t, EX_COLOR_TEXT_MUTED));
            }
        }
    } else if (w->type == EX_WIDGET_BUTTON && w->icon) {
        float pad = w->w * 0.14f;
        ex_icon_draw(r, w->icon, w->x + pad, w->y + pad, w->w - 2 * pad, w->h - 2 * pad, ex_theme_color(t, (ExColorToken)(w->disabled ? EX_COLOR_TEXT_FAINT : w->pressed ? EX_COLOR_BACKGROUND : w->selected ? EX_COLOR_ACCENT : w->text_token >= 0 ? w->text_token : EX_COLOR_TEXT)));
    }
    if (w->type == EX_WIDGET_EDITOR && w->editor) {
        ex_editor_draw(w->editor, r, t, ui->focus == w, blink_on);
    }
    if (w->type == EX_WIDGET_READING && w->reading) ex_readingview_draw(w->reading, r, t, ui->base_size);
    if (w->type == EX_WIDGET_TREE) tree_draw(ui, w);
    if (w->type == EX_WIDGET_LIST) ex_list_draw(ui, w);
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
    if (!w || w->hidden || x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h) return NULL;
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

static int inside(const ExWidget *root, const ExWidget *w) {
    for (; w; w = w->parent) if (w == root) return 1;
    return 0;
}

void ex_ui_release(ExUi *ui, ExWidget *subtree) {
    if (ui->focus && inside(subtree, ui->focus)) ui->focus = NULL;
    if (ui->pressed && inside(subtree, ui->pressed)) ui->pressed = NULL;
    if (ui->captured && inside(subtree, ui->captured)) ui->captured = NULL;
    if (ui->tree_drag && inside(subtree, ui->tree_drag)) ui->tree_drag = NULL;
    if (ui->last_root && inside(subtree, ui->last_root)) ui->last_root = NULL;
}

int ex_ui_drop(ExUi *ui, ExWidget *root, int is_file, const char *data, float x, float y) {
    ExWidget *w = ex_ui_widget_at(root, x, y);
    (void)ui;
    for (; w; w = w->parent)
        if (w->on_drop && w->on_drop(w, is_file, data, x, y, w->on_drop_user)) return 1;
    return 0;
}

int ex_ui_cursor_at(ExUi *ui, ExWidget *root, float x, float y) {
    ExWidget *w = ui->captured ? ui->captured : ex_ui_widget_at(root, x, y);
    for (; w; w = w->parent) if (w->cursor) return w->cursor;
    return 0;
}

void ex_ui_mouse(ExUi *ui, ExWidget *root, ExMouse kind, float x, float y, int mods, int clicks) {
    ExWidget *hit, *target, *hook;
    if (ui->tree_drag && kind != EX_MOUSE_DOWN) { tree_drag_event(ui, ui->tree_drag, kind, x, y); return; }
    hit = ex_ui_widget_at(root, x, y); target = hit;
    if (kind == EX_MOUSE_DOWN) {
        ui->captured = NULL;
        for (hook = hit; hook; hook = hook->parent)
            if (hook->on_mouse && hook->on_mouse(hook, kind, x, y, hit, hook->on_mouse_user)) { ui->captured = hook; return; }
    } else if (ui->captured) {
        ExWidget *c = ui->captured;
        if (kind == EX_MOUSE_UP) ui->captured = NULL;
        c->on_mouse(c, kind, x, y, hit, c->on_mouse_user);
        return;
    }
    while (target && !target->focusable && target->type != EX_WIDGET_BUTTON) target = target->parent;
    clear_hover(root);
    if (target && target->type == EX_WIDGET_BUTTON) target->hover = 1;
    if (target && target->disabled) target = NULL;                       /* a button that does nothing yet ignores the pointer */
    if (kind == EX_MOUSE_DOWN) {
        ui->focus = target && target->focusable ? target : NULL;
        if (target && target->type == EX_WIDGET_BUTTON) { target->pressed = 1; ui->pressed = target; }
    } else if (kind == EX_MOUSE_UP && ui->pressed) {
        ExWidget *p = ui->pressed;
        p->pressed = 0;
        ui->pressed = NULL;
        if (target == p && p->on_click) p->on_click(p, p->on_click_user);
    }
    if (target && target->type == EX_WIDGET_TREE) tree_mouse(ui, target, kind, x, y, clicks);
    if (target && target->type == EX_WIDGET_LIST) ex_list_mouse(ui, target, kind, x, y, clicks);
    if (ui->focus && ui->focus->type == EX_WIDGET_EDITOR && ui->focus->editor)
        if (kind == EX_MOUSE_DRAG || kind == EX_MOUSE_UP || (kind == EX_MOUSE_DOWN && target == ui->focus))
            ex_editor_mouse(ui->focus->editor, ui->r, kind, x, y, mods, clicks);
}

static size_t collect_focusable(ExWidget *w, ExWidget **out, size_t n, size_t cap) {
    for (; w; w = w->next) {
        if (w->hidden) continue;
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
    if (f && f->on_key && f->on_key(f, key, mods, f->on_key_user)) return;
    if (key == EX_KEY_TAB && (!in_editor || prim || ui->tab_leaves_editor)) {
        ex_ui_focus_move(ui, shift ? -1 : 1);
        return;
    }
    if (f && f->type == EX_WIDGET_TREE) { tree_key(ui, f, key); return; }
    if (f && f->type == EX_WIDGET_LIST) { ex_list_key(ui, f, key); return; }
    if (f && f->type == EX_WIDGET_READING && f->reading) { ex_readingview_key(f->reading, key); return; }
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
    if (f && f->on_text && f->on_text(f, utf8, len, f->on_text_user)) return;
    if (f && f->type == EX_WIDGET_EDITOR) { ui->tab_leaves_editor = 0; ex_editor_text_input(f->editor, utf8, len); }
}
