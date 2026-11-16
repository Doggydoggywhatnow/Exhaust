/* A pop-out window: see include/exhaust_popwin.h. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_a11y_adapter.h"
#include "exhaust_popwin.h"

struct ExPopWin {
    ExWorkspace *ws;
    ExId window;
    ExRenderer *r;
    ExUi *ui;
    ExPanes *panes;
    ExWidget *root;
    ExA11yAdapter *a11y;               /* the screen reader interface of this window (7h) */
};

/* Is the rectangle's middle on a screen that exists? */
static int on_a_screen(int x, int y, int w, int h) {
    int n = 0, i, ok = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    for (i = 0; ids && i < n; i++) {
        SDL_Rect b;
        if (SDL_GetDisplayBounds(ids[i], &b) && x + w / 2 >= b.x && x + w / 2 < b.x + b.w && y + h / 4 >= b.y && y + h / 4 < b.y + b.h) { ok = 1; break; }
    }
    SDL_free(ids);
    return ok;
}

ExPopWin *ex_popwin_create(ExWorkspace *ws, ExId window, const ExTheme *theme, float base_size, float zoom, const ExPanesHooks *hooks, void *hooks_user,
                           int x, int y, int w, int h, const char *title) {
    ExPopWin *pw = calloc(1, sizeof *pw);
    if (!pw) return NULL;
    pw->ws = ws; pw->window = window;
    pw->r = ex_renderer_create_hidden(title ? title : "Exhaust", w > 0 ? w : 800, h > 0 ? h : 600);
    if (!pw->r) { free(pw); return NULL; }
    ex_renderer_set_display_scale(pw->r, ex_renderer_detect_display_scale(pw->r));
    ex_renderer_set_zoom(pw->r, zoom);
    pw->ui = ex_ui_create(pw->r, theme, base_size);
    pw->panes = pw->ui ? ex_panes_create_window(pw->ui, ws, hooks, hooks_user, window) : NULL;
    pw->root = ex_widget_box(pw->ui, 1, "Pop-out window");
    if (!pw->ui || !pw->panes || !pw->root) { ex_popwin_free(pw); return NULL; }
    pw->root->width = ex_size_grow(); pw->root->height = ex_size_grow();
    pw->root->bg_token = EX_COLOR_BACKGROUND;
    ex_widget_add(pw->root, ex_panes_root(pw->panes));
    if (x != INT_MIN && y != INT_MIN) {
        int ww = w > 0 ? w : 800, wh = h > 0 ? h : 600;
        if (!on_a_screen(x, y, ww, wh)) {                          /* its screen is gone: put it on a screen that is there */
            SDL_Rect b;
            SDL_DisplayID d = SDL_GetPrimaryDisplay();
            if (d && SDL_GetDisplayBounds(d, &b)) { x = b.x + (b.w - ww) / 2; y = b.y + (b.h - wh) / 3; }
            else { x = 50; y = 50; }
        }
        SDL_SetWindowPosition(ex_renderer_window(pw->r), x, y);
    }
    pw->a11y = ex_a11y_adapter_create(ex_renderer_window(pw->r));      /* before the window is shown (Windows needs that) */
    ex_renderer_show(pw->r);
    return pw;
}

void ex_popwin_free(ExPopWin *pw) {
    if (!pw) return;
    if (pw->panes) ex_panes_free(pw->panes);                   /* the contents are closed; their notes stay with the program. The panes' own box is in our root and goes with it. */
    if (pw->root) ex_widget_free(pw->root);
    ex_a11y_adapter_free(pw->a11y);
    ex_ui_free(pw->ui);
    ex_renderer_destroy(pw->r);
    free(pw);
}

ExId ex_popwin_window(const ExPopWin *pw) { return pw->window; }
uint32_t ex_popwin_sdl_id(const ExPopWin *pw) { return (uint32_t)SDL_GetWindowID(ex_renderer_window(pw->r)); }
ExRenderer *ex_popwin_renderer(ExPopWin *pw) { return pw->r; }
ExUi *ex_popwin_ui(ExPopWin *pw) { return pw->ui; }
ExPanes *ex_popwin_panes(ExPopWin *pw) { return pw->panes; }
ExWidget *ex_popwin_root(ExPopWin *pw) { return pw->root; }
ExA11yAdapter *ex_popwin_a11y(ExPopWin *pw) { return pw->a11y; }

void ex_popwin_set_font_db(ExPopWin *pw, ExFontDb *db) { ex_renderer_set_font_db_shared(pw->r, db); }

void ex_popwin_get_rect(const ExPopWin *pw, int *x, int *y, int *w, int *h) {
    SDL_Window *win = ex_renderer_window(pw->r);
    int xx = 0, yy = 0, ww = 0, hh = 0;
    SDL_GetWindowPosition(win, &xx, &yy);
    SDL_GetWindowSize(win, &ww, &hh);
    if (x) *x = xx;
    if (y) *y = yy;
    if (w) *w = ww;
    if (h) *h = hh;
}

double ex_popwin_frame(ExPopWin *pw, int blink) {
    int w, h;
    Uint64 t0 = SDL_GetPerformanceCounter(), t1;
    ex_panes_update(pw->panes);
    ex_renderer_size(pw->r, &w, &h);
    ex_ui_layout(pw->ui, pw->root, (float)w, (float)h);
    ex_renderer_clear(pw->r, ex_theme_color(ex_ui_theme(pw->ui), EX_COLOR_BACKGROUND));
    ex_ui_draw(pw->ui, pw->root, blink);
    t1 = SDL_GetPerformanceCounter();
    ex_renderer_present(pw->r);
    return (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}
