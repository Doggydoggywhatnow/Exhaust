/* The image tab: see include/exhaust_imageview.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_imageview.h"
#include "exhaust_render.h"

#define ZOOM_MIN 0.02f
#define ZOOM_MAX 32.0f
#define DOUBLE_MS 400
#define DRAG_PX 4.0f

typedef struct View {
    ExUi *ui;
    ExImageCache *cache;
    char *path;
    int fitted;
    float zoom;                   /* screen pixels per picture pixel (of the file's size) when not fitted */
    float cx, cy;                 /* the point of the picture (in file pixels) at the middle of the pane */
    int pressed; float px, py, pcx, pcy, moved; uint32_t last_click_ms; float last_x, last_y;
    ExImageViewStats st;
    int shown_w;                  /* the width an SVG was last asked for */
} View;

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_IMAGE ? (View *)w->owned : NULL; }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static float fit_scale(const View *v, const ExWidget *w, int sw, int sh, float room_h) {
    float a = (w->w - 24.0f) / (float)sw, b = (room_h - 24.0f) / (float)sh, s = a < b ? a : b;
    (void)v;
    if (s > 1.0f) s = 1.0f;                                                         /* a small picture is not blown up when fitted */
    return s > 0.01f ? s : 0.01f;
}

void ex_imageview_stats(const ExWidget *w, ExImageViewStats *out) { const View *v = view_of(w); if (v) *out = v->st; else memset(out, 0, sizeof *out); }

void ex_imageview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    ExRenderer *r;
    const ExTheme *t;
    ExImageRef ref;
    double t0;
    float sc, info_h, area_h, scale, dw, dh, x, y;
    ExFont *f;
    ExFontMetrics fm;
    char line[700];
    ExColor c_bg, c_text, c_muted, c_surface, c_border;
    if (!v) return;
    t0 = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
    r = ex_ui_renderer(ui); t = ex_ui_theme(ui);
    sc = ex_renderer_scale(r);
    c_bg = ex_theme_color(t, EX_COLOR_BACKGROUND); c_text = ex_theme_color(t, EX_COLOR_TEXT); c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    c_surface = ex_theme_color(t, EX_COLOR_SURFACE); c_border = ex_theme_color(t, EX_COLOR_BORDER);
    f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0);
    ex_fill_rect(r, w->x, w->y, w->w, w->h, c_bg);
    ex_text_metrics(r, f, ex_ui_base_size(ui), &fm);
    info_h = (float)fm.line_height + 12.0f * sc;
    area_h = w->h - info_h;
    ex_ui_clip_to(ui, w->x, w->y, w->w, area_h);
    {   /* an SVG is asked for at the width it is drawn at */
        int want_w = v->shown_w > 0 ? v->shown_w : 1024;
        ex_imagecache_get(v->cache, r, v->path, want_w, &ref);
    }
    v->st.state = ref.state; v->st.w = ref.w; v->st.h = ref.h; v->st.src_w = ref.src_w; v->st.src_h = ref.src_h; v->st.has_alpha = ref.has_alpha; v->st.fitted = v->fitted;
    if (ref.state != EX_IMAGE_READY) {
        const char *msg = ref.state == EX_IMAGE_FAILED ? (ref.err ? ref.err : "This picture cannot be opened.") : "Loading...";
        ex_text_draw(r, f, ex_ui_base_size(ui), w->x + 16.0f * sc, w->y + 16.0f * sc, msg, strlen(msg), ref.state == EX_IMAGE_FAILED ? ex_theme_color(t, EX_COLOR_ERROR) : c_muted);
        v->st.scale = 0.0f;
        ex_ui_clip_restore(ui);
        snprintf(line, sizeof line, "%s", v->path);
    } else {
        int sw = ref.src_w > 0 ? ref.src_w : ref.w, sh = ref.src_h > 0 ? ref.src_h : ref.h;
        float tex_per_src = (float)ref.w / (float)sw;                            /* the texture may be a reduced picture */
        if (v->fitted) scale = fit_scale(v, w, sw, sh, area_h); else scale = v->zoom;
        dw = (float)sw * scale; dh = (float)sh * scale;
        if (v->fitted) { x = w->x + (w->w - dw) / 2.0f; y = w->y + (area_h - dh) / 2.0f; v->cx = (float)sw / 2.0f; v->cy = (float)sh / 2.0f; }
        else {
            v->cx = clampf(v->cx, 0.0f, (float)sw); v->cy = clampf(v->cy, 0.0f, (float)sh);
            x = w->x + w->w / 2.0f - v->cx * scale; y = w->y + area_h / 2.0f - v->cy * scale;
        }
        v->st.scale = scale;
        {   /* the visible part: only what is in the pane is drawn (a picture at 32 times is a few texture pixels) */
            float vx0 = x < w->x ? w->x : x, vy0 = y < w->y ? w->y : y, vx1 = x + dw > w->x + w->w ? w->x + w->w : x + dw, vy1 = y + dh > w->y + area_h ? w->y + area_h : y + dh;
            if (vx1 > vx0 && vy1 > vy0) {
                float sx = (vx0 - x) / scale * tex_per_src, sy = (vy0 - y) / scale * tex_per_src, sw2 = (vx1 - vx0) / scale * tex_per_src, sh2 = (vy1 - vy0) / scale * tex_per_src;
                if (ref.has_alpha) {                                               /* a checkerboard under what shows through */
                    float cell = 12.0f * sc, yy, xx;
                    ExColor a = c_surface, b = c_border;
                    int row = 0;
                    ex_fill_rect(r, vx0, vy0, vx1 - vx0, vy1 - vy0, a);
                    for (yy = vy0; yy < vy1; yy += cell, row++) {
                        int col = row & 1;
                        for (xx = vx0; xx < vx1; xx += cell, col++) if (col & 1) {
                            float cw = xx + cell > vx1 ? vx1 - xx : cell, ch = yy + cell > vy1 ? vy1 - yy : cell;
                            ex_fill_rect(r, xx, yy, cw, ch, b);
                        }
                    }
                }
                ex_texture_draw(r, ref.tex, sx, sy, sw2, sh2, vx0, vy0, vx1 - vx0, vy1 - vy0, 255, scale < 3.0f);
            }
        }
        ex_ui_clip_restore(ui);
        {   /* an SVG is drawn again when the width it is shown at needs a bigger raster */
            size_t n = strlen(v->path);
            if (n > 4 && !strcmp(v->path + n - 4, ".svg")) { int need = (int)ceilf(dw), cur = v->shown_w > 0 ? v->shown_w : 1024; if (need > cur) v->shown_w = need; }
        }
        {   /* the format name from the extension */
            const char *dot = strrchr(v->path, '.');
            char fmt[16] = "";
            size_t k = 0;
            if (dot) for (dot++; *dot && k + 1 < sizeof fmt; dot++) fmt[k++] = (*dot >= 'a' && *dot <= 'z') ? (char)(*dot - 32) : *dot;
            fmt[k] = 0;
            snprintf(line, sizeof line, "%s   %d \xc3\x97 %d   %s   %d%%", v->path, sw, sh, fmt, (int)(scale * 100.0f + 0.5f));
        }
    }
    {   /* the line at the bottom */
        char fit[700];
        ex_fill_rect(r, w->x, w->y + area_h, w->w, info_h, c_surface);
        ex_fill_rect(r, w->x, w->y + area_h, w->w, 1.0f, c_border);
        ex_text_fit(r, f, ex_ui_base_size(ui), line, strlen(line), w->w - 20.0f * sc, fit, sizeof fit);
        ex_text_draw(r, f, ex_ui_base_size(ui), w->x + 10.0f * sc, w->y + area_h + (info_h - (float)fm.line_height) / 2.0f, fit, strlen(fit), c_text);
    }
    v->st.own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
}

static void zoom_about(View *v, ExWidget *w, float factor, float px, float py) {
    float scale = v->st.scale > 0.0f ? v->st.scale : 1.0f, ns, area_h = w->h;
    float mx, my;                                                  /* the picture point under the pointer */
    if (v->fitted) v->zoom = scale;
    mx = v->cx + (px - (w->x + w->w / 2.0f)) / v->zoom; my = v->cy + (py - (w->y + area_h / 2.0f)) / v->zoom;
    ns = clampf(v->zoom * factor, ZOOM_MIN, ZOOM_MAX);
    v->cx = mx - (px - (w->x + w->w / 2.0f)) / ns; v->cy = my - (py - (w->y + area_h / 2.0f)) / ns;
    v->zoom = ns; v->fitted = 0;
}

void ex_imageview_wheel(ExWidget *w, float dy, float x, float y) {
    View *v = view_of(w);
    if (!v || v->st.state != EX_IMAGE_READY) return;
    zoom_about(v, w, powf(1.2f, dy), x, y);
}

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    if (kind == EX_MOUSE_DOWN) {
        uint32_t now = SDL_GetTicks();
        if (under != w) return 0;
        ex_ui_set_focus(v->ui, w);
        if (v->last_click_ms && now - v->last_click_ms < DOUBLE_MS && fabsf(x - v->last_x) < 6.0f && fabsf(y - v->last_y) < 6.0f && v->st.state == EX_IMAGE_READY) {
            v->last_click_ms = 0;
            if (v->fitted) { zoom_about(v, w, 1.0f / (v->st.scale > 0.0f ? v->st.scale : 1.0f), x, y); }       /* to the own size about the pointer */
            else v->fitted = 1;
            v->pressed = 0;
            return 1;
        }
        v->last_click_ms = now; v->last_x = x; v->last_y = y;
        v->pressed = 1; v->px = x; v->py = y; v->pcx = v->cx; v->pcy = v->cy; v->moved = 0;
        return 1;
    }
    if (kind == EX_MOUSE_DRAG && v->pressed) {
        float scale = v->st.scale > 0.0f ? v->st.scale : 1.0f;
        if (fabsf(x - v->px) + fabsf(y - v->py) > DRAG_PX) v->moved = 1;
        if (v->moved && v->st.state == EX_IMAGE_READY) {
            if (v->fitted) { v->zoom = scale; v->fitted = 0; }
            v->cx = v->pcx - (x - v->px) / v->zoom; v->cy = v->pcy - (y - v->py) / v->zoom;
        }
        return 1;
    }
    if (kind == EX_MOUSE_UP) { v->pressed = 0; return 1; }
    return 0;
}

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    float step = 60.0f / (v->st.scale > 0.0f ? v->st.scale : 1.0f);
    (void)mods;
    switch (key) {
    case EX_KEY_HOME: v->fitted = 1; return 1;
    case EX_KEY_LEFT: case EX_KEY_RIGHT: case EX_KEY_UP: case EX_KEY_DOWN:
        if (v->st.state != EX_IMAGE_READY) return 1;
        if (v->fitted) { v->zoom = v->st.scale > 0.0f ? v->st.scale : 1.0f; v->fitted = 0; }
        v->cx += key == EX_KEY_LEFT ? -step : key == EX_KEY_RIGHT ? step : 0; v->cy += key == EX_KEY_UP ? -step : key == EX_KEY_DOWN ? step : 0;
        return 1;
    default: (void)w; return 0;
    }
}

static int text_hook(ExWidget *w, const char *s, size_t len, void *user) {
    View *v = user;
    if (len != 1 || v->st.state != EX_IMAGE_READY) return 0;
    if (s[0] == '+' || s[0] == '=') { zoom_about(v, w, 1.25f, w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    if (s[0] == '-') { zoom_about(v, w, 0.8f, w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    if (s[0] == '0') { v->fitted = 1; return 1; }
    if (s[0] == '1') { zoom_about(v, w, 1.0f / (v->st.scale > 0.0f ? v->st.scale : 1.0f), w->x + w->w / 2.0f, w->y + w->h / 2.0f); return 1; }
    return 0;
}

static void view_free(void *p) { View *v = p; if (v) { free(v->path); free(v); } }

ExWidget *ex_widget_imageview(ExUi *ui, ExImageCache *cache, const char *path) {
    ExWidget *w = ex_widget_box(ui, 0, path && *path ? path : "Picture");
    View *v;
    size_t n;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    n = strlen(path ? path : "");
    v->path = malloc(n + 1);
    if (v->path) memcpy(v->path, path ? path : "", n + 1);
    v->ui = ui; v->cache = cache; v->fitted = 1; v->zoom = 1.0f;
    w->type = EX_WIDGET_IMAGE;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1; w->clip = 1; w->vertical = 1;
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    w->on_text = text_hook; w->on_text_user = v;
    return w;
}
