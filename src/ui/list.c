/* The list widget: rows of text with a selection, scrolling, keyboard and mouse. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ui.h"

typedef struct Row { ExListRow r; char *text, *detail, *action, *key; } Row;

struct ExListData {
    ExUi *ui;
    Row *rows; size_t n, cap;
    size_t selected;
    float scroll;
    ExListHandlers h;
    char *empty;
    long drag_from, drag_to;      /* a row being dragged, and where it would go; -1 none */
    int dragging;
    long click_pending;           /* a click on a row of a list that can be dragged: it activates on release if it was not a drag */
    float press_y;
    long hover;
};

static char *dup_str(const char *s) { size_t n; char *p; if (!s) return NULL; n = strlen(s); p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

static void free_rows(struct ExListData *d) {
    size_t i;
    for (i = 0; i < d->n; i++) { free(d->rows[i].text); free(d->rows[i].detail); free(d->rows[i].action); free(d->rows[i].key); }
    d->n = 0;
}

static void free_list(void *p) {
    struct ExListData *d = p;
    free_rows(d);
    free(d->rows); free(d->empty); free(d);
}

static int list_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    (void)under; (void)user;
    ex_list_mouse(NULL, w, kind, x, y, 1);
    return 0;
}

ExWidget *ex_widget_list(ExUi *ui, const char *name) {
    ExWidget *w = ex_widget_box(ui, 1, name);
    struct ExListData *d;
    if (!w) return NULL;
    d = calloc(1, sizeof *d);
    if (!d) { ex_widget_free(w); return NULL; }
    d->ui = ui; d->drag_from = d->drag_to = -1; d->hover = -1; d->click_pending = -1;
    w->type = EX_WIDGET_LIST;
    w->role = EX_ROLE_LIST;
    w->list = d; w->owned = d; w->free_owned = free_list;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->focusable = 1; w->clip = 1;
    (void)list_hook;
    return w;
}

void ex_list_clear(ExWidget *w) { free_rows(w->list); w->list->selected = 0; w->list->scroll = 0; }
size_t ex_list_count(const ExWidget *w) { return w->list->n; }
size_t ex_list_selected(const ExWidget *w) { return w->list->selected; }
const ExListRow *ex_list_row(const ExWidget *w, size_t i) { return i < w->list->n ? &w->list->rows[i].r : NULL; }
void ex_list_set_handlers(ExWidget *w, const ExListHandlers *h) { w->list->h = *h; }
void ex_list_set_empty_text(ExWidget *w, const char *text) { free(w->list->empty); w->list->empty = dup_str(text); }

size_t ex_list_add(ExWidget *w, const ExListRow *in) {
    struct ExListData *d = w->list;
    Row *r;
    if (d->n == d->cap) { size_t nc = d->cap ? d->cap * 2 : 32; Row *nv = realloc(d->rows, nc * sizeof *nv); if (!nv) return (size_t)-1; d->rows = nv; d->cap = nc; }
    r = &d->rows[d->n];
    r->r = *in;
    r->text = dup_str(in->text ? in->text : ""); r->detail = dup_str(in->detail); r->action = dup_str(in->action); r->key = dup_str(in->key);
    r->r.text = r->text; r->r.detail = r->detail; r->r.action = r->action; r->r.key = r->key;
    return d->n++;
}

float ex_list_row_height(const ExWidget *w) {
    ExFontMetrics m;
    ExFont *f = ex_ui_font(w->list->ui, w->font_token, 0);
    if (ex_text_metrics(ex_ui_renderer(w->list->ui), f, ex_ui_base_size(w->list->ui), &m)) return 24.0f;
    return (float)m.line_height * 1.35f;
}

static void clamp_scroll(ExWidget *w) {
    struct ExListData *d = w->list;
    float total = (float)d->n * ex_list_row_height(w), max = total > w->h ? total - w->h : 0;
    if (w->h <= 0) return;                                  /* not laid out yet: the position is kept */
    if (d->scroll > max) d->scroll = max;
    if (d->scroll < 0) d->scroll = 0;
}

static void ensure_visible(ExWidget *w) {
    struct ExListData *d = w->list;
    if (w->h <= 0) return;
    float rh = ex_list_row_height(w), top = (float)d->selected * rh, bottom = top + rh, total = (float)d->n * rh, max = total > w->h ? total - w->h : 0;
    if (top < d->scroll) d->scroll = top;
    else if (bottom > d->scroll + w->h) d->scroll = bottom - w->h;
    if (d->scroll > max) d->scroll = max;
    if (d->scroll < 0) d->scroll = 0;
}

void ex_list_select(ExWidget *w, size_t i) {
    struct ExListData *d = w->list;
    d->selected = d->n ? (i < d->n ? i : d->n - 1) : 0;
    ensure_visible(w);
}

void ex_list_scroll(ExWidget *w, float dy) {
    w->list->scroll += dy;
    if (w->list->scroll < 0) w->list->scroll = 0;
    clamp_scroll(w);
}

float ex_list_scroll_y(const ExWidget *w) { return w->list->scroll; }
void ex_list_set_scroll_y(ExWidget *w, float y) { w->list->scroll = 0; ex_list_scroll(w, y); }

long ex_list_row_at(const ExWidget *w, float x, float y) {
    const struct ExListData *d = w->list;
    float rh = ex_list_row_height(w);
    long row;
    clamp_scroll((ExWidget *)w);
    if (x < w->x || x >= w->x + w->w || y < w->y || y >= w->y + w->h) return -1;
    row = (long)floorf((y - w->y + d->scroll) / rh);
    return row >= 0 && (size_t)row < d->n ? row : -1;
}

static float action_width(ExUi *ui, const Row *r) {
    ExFont *f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
    return r->action ? ex_text_measure(ex_ui_renderer(ui), f, ex_ui_base_size(ui) * 0.85f, r->action, strlen(r->action)) + (float)ex_px(ex_ui_renderer(ui), 16) : 0.0f;
}

void ex_list_draw(ExUi *ui, ExWidget *w) {
    struct ExListData *d = w->list;
    const ExTheme *t = ex_ui_theme(ui);
    ExRenderer *r = ex_ui_renderer(ui);
    ExFont *f = ex_ui_font(ui, w->font_token, 0), *fb = ex_ui_font(ui, w->font_token, 1);
    ExFontMetrics m;
    float rh = ex_list_row_height(w), indent = (float)ex_px(r, 14), pad = (float)ex_px(r, 8), size = ex_ui_base_size(ui);
    size_t first, i;
    ExColor text = ex_theme_color(t, EX_COLOR_TEXT), muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    if (ex_text_metrics(r, f, size, &m)) return;
    clamp_scroll(w);
    ex_set_clip(r, (int)w->x, (int)w->y, (int)ceilf(w->w), (int)ceilf(w->h));
    if (d->n == 0 && d->empty) ex_text_draw(r, f, size, w->x + pad, w->y + (rh - (float)m.line_height) / 2.0f, d->empty, strlen(d->empty), ex_theme_color(t, EX_COLOR_TEXT_FAINT));
    first = (size_t)(d->scroll / rh);
    for (i = first; i < d->n && (float)i * rh - d->scroll < w->h; i++) {
        const Row *row = &d->rows[i];
        float y = w->y + (float)i * rh - d->scroll, x = w->x + pad + (float)row->r.indent * indent, cy = y + rh / 2.0f, s = (float)ex_px(r, 3), right = w->x + w->w - pad;
        ExColor c = row->r.muted ? muted : text;
        if (d->dragging && (long)i == d->drag_to) ex_fill_rect(r, w->x, y - 1, w->w, 2, ex_theme_color(t, EX_COLOR_ACCENT));
        if (i == d->selected) ex_fill_round_rect(r, w->x + (float)ex_px(r, 2), y, w->w - (float)ex_px(r, 4), rh, (float)ex_px(r, ex_theme_size(t, EX_SIZE_RADIUS)), ex_theme_color(t, ui && ex_ui_focus(ui) == w ? EX_COLOR_SELECTION : EX_COLOR_SURFACE_HOVER));
        else if ((long)i == d->hover) ex_fill_round_rect(r, w->x + (float)ex_px(r, 2), y, w->w - (float)ex_px(r, 4), rh, (float)ex_px(r, ex_theme_size(t, EX_SIZE_RADIUS)), ex_theme_color(t, EX_COLOR_SURFACE_HOVER));
        if (row->r.glyph == EX_GLYPH_CHEVRON_OPEN) ex_fill_triangle(r, x + indent / 2 - s, cy - s / 2, x + indent / 2 + s, cy - s / 2, x + indent / 2, cy + s, muted);
        else if (row->r.glyph == EX_GLYPH_CHEVRON_CLOSED) ex_fill_triangle(r, x + indent / 2 - s / 2, cy - s, x + indent / 2 - s / 2, cy + s, x + indent / 2 + s, cy, muted);
        else if (row->r.glyph == EX_GLYPH_BULLET) ex_fill_round_rect(r, x + indent / 2 - s / 2, cy - s / 2, s, s, s / 2, muted);
        if (row->r.glyph != EX_GLYPH_NONE) x += indent;
        if (row->action) {
            float aw = action_width(ui, row), ax = right - aw + (float)ex_px(r, 8);
            ex_stroke_round_rect(r, ax, y + (float)ex_px(r, 2), aw - (float)ex_px(r, 8), rh - (float)ex_px(r, 4), (float)ex_px(r, 3), 1.0f, ex_theme_color(t, EX_COLOR_BORDER));
            ex_text_draw(r, f, size * 0.85f, ax + (float)ex_px(r, 5), cy - (float)m.line_height * 0.85f / 2.0f, row->action, strlen(row->action), muted);
            right = ax - (float)ex_px(r, 6);
        }
        if (row->detail) {
            float dw = ex_text_measure(r, f, size * 0.85f, row->detail, strlen(row->detail));
            ex_text_draw(r, f, size * 0.85f, right - dw, cy - (float)m.line_height * 0.85f / 2.0f, row->detail, strlen(row->detail), muted);
            right -= dw + (float)ex_px(r, 8);
        }
        ex_set_clip(r, (int)w->x, (int)w->y, (int)ceilf(right > w->x ? right - w->x : 0), (int)ceilf(w->h));
        ex_text_draw(r, row->r.bold ? fb : f, size, x, y + (rh - (float)m.line_height) / 2.0f, row->text, strlen(row->text), c);
        ex_set_clip(r, (int)w->x, (int)w->y, (int)ceilf(w->w), (int)ceilf(w->h));
    }
    ex_set_clip(r, 0, 0, -1, -1);
}

static void activate(ExWidget *w, size_t i) { if (i < w->list->n && w->list->h.activate) w->list->h.activate(w, i, w->list->h.user); }

void ex_list_key(ExUi *ui, ExWidget *w, ExKey key) {
    struct ExListData *d = w->list;
    size_t page = (size_t)(w->h / ex_list_row_height(w));
    (void)ui;
    if (page < 1) page = 1;
    switch (key) {
    case EX_KEY_UP: if (d->selected > 0) d->selected--; break;
    case EX_KEY_DOWN: if (d->selected + 1 < d->n) d->selected++; break;
    case EX_KEY_HOME: d->selected = 0; break;
    case EX_KEY_END: if (d->n) d->selected = d->n - 1; break;
    case EX_KEY_PAGE_UP: d->selected = d->selected > page ? d->selected - page : 0; break;
    case EX_KEY_PAGE_DOWN: d->selected = d->selected + page < d->n ? d->selected + page : (d->n ? d->n - 1 : 0); break;
    case EX_KEY_LEFT: case EX_KEY_RIGHT:
        if (d->selected < d->n && d->rows[d->selected].r.glyph >= EX_GLYPH_CHEVRON_CLOSED && d->rows[d->selected].r.glyph <= EX_GLYPH_CHEVRON_OPEN && d->h.toggle) {
            int open = d->rows[d->selected].r.glyph == EX_GLYPH_CHEVRON_OPEN;
            if ((key == EX_KEY_LEFT && open) || (key == EX_KEY_RIGHT && !open)) d->h.toggle(w, d->selected, d->h.user);
        }
        break;
    case EX_KEY_ENTER: case EX_KEY_SPACE: activate(w, d->selected); return;    /* the handler may close the dialog that holds this list */
    default: break;
    }
    ensure_visible(w);
}

void ex_list_mouse(ExUi *ui, ExWidget *w, ExMouse kind, float x, float y, int clicks) {
    struct ExListData *d = w->list;
    long row = ex_list_row_at(w, x, y);
    float rh = ex_list_row_height(w);
    if (kind == EX_MOUSE_DOWN && row >= 0) {
        const Row *rr = &d->rows[row];
        float indent = (float)ex_px(ex_ui_renderer(ui), 14), cx0 = w->x + (float)ex_px(ex_ui_renderer(ui), 8) + (float)rr->r.indent * indent;
        d->selected = (size_t)row;
        d->drag_from = row; d->drag_to = -1; d->dragging = 0; d->press_y = y;
        if (rr->action && x >= w->x + w->w - action_width(ui, rr)) { d->drag_from = -1; if (d->h.action) d->h.action(w, (size_t)row, d->h.user); return; }
        if (rr->r.glyph >= EX_GLYPH_CHEVRON_CLOSED && rr->r.glyph <= EX_GLYPH_CHEVRON_OPEN && x < cx0 + indent && d->h.toggle) { d->drag_from = -1; d->h.toggle(w, (size_t)row, d->h.user); return; }
        d->click_pending = -1;
        if (d->h.single_click && d->h.drag) d->click_pending = row;
        else if (d->h.single_click) { d->drag_from = -1; activate(w, (size_t)row); return; }     /* the handler may have closed the dialog that holds this list */
        else if (clicks >= 2) { d->drag_from = -1; activate(w, (size_t)row); return; }
    } else if (kind == EX_MOUSE_DRAG && d->drag_from >= 0 && d->h.drag) {
        if (fabsf(y - d->press_y) > rh / 2) d->dragging = 1;
        if (d->dragging) {
            long over = (long)floorf((y - w->y + d->scroll) / rh);
            if (over < 0) over = 0;
            if ((size_t)over > d->n) over = (long)d->n;
            d->drag_to = over;
        }
    } else if (kind == EX_MOUSE_UP) {
        if (d->dragging && d->drag_from >= 0 && d->drag_to >= 0 && d->drag_to != d->drag_from && d->drag_to != d->drag_from + 1 && d->h.move) {
            long from = d->drag_from, to = d->drag_to;
            d->dragging = 0; d->drag_from = d->drag_to = -1;
            d->h.move(w, (size_t)from, (size_t)to, d->h.user);
            return;
        }
        if (!d->dragging && d->click_pending >= 0 && d->click_pending == row) { long r = d->click_pending; d->click_pending = -1; d->drag_from = d->drag_to = -1; activate(w, (size_t)r); return; }
        d->click_pending = -1;
        d->dragging = 0; d->drag_from = d->drag_to = -1;
    }
    if (kind != EX_MOUSE_DRAG && d->hover != row) { d->hover = row; if (d->h.hover) d->h.hover(w, row, x, y, d->h.user); }
}
