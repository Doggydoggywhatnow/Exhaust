/* The media tab: see include/exhaust_mediaview.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_mediaview.h"
#include "exhaust_render.h"

typedef struct View {
    ExUi *ui;
    char *path;
    ExMediaViewHandlers h;
    int loading, failed, play_when_ready;
    char err[300];
    unsigned loads_asked;
    int drag;
    float volume;
    ExTexture *tex; int tw, th; unsigned long serial; unsigned long uploaded;
    float bar_x, bar_w, bar_y, bar_h, vol_x, vol_w, vol_y, vol_h, btn_x, btn_y, btn_r, ext_x, ext_y, ext_w, ext_h;
    uint32_t last_click_ms;
    double own_ms;
} View;

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_MEDIA ? (View *)w->owned : NULL; }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static ExMpv *player(const View *v) { return v->h.current ? v->h.current(v->h.user, v->path) : NULL; }

static void ask_load(View *v) {
    if (v->loading || !v->h.load) return;
    v->loading = 1; v->failed = 0; v->err[0] = 0; v->loads_asked++;
    v->h.load(v->h.user, v->path);
}

void ex_mediaview_loaded(ExWidget *w, const char *error) {
    View *v = view_of(w);
    ExMpv *m;
    if (!v) return;
    v->loading = 0;
    if (error) { v->failed = 1; v->play_when_ready = 0; snprintf(v->err, sizeof v->err, "%s", error); return; }
    v->failed = 0;
    m = player(v);
    if (m) ex_mpv_set_volume(m, v->volume);
    if (m && v->play_when_ready) { v->play_when_ready = 0; ex_mpv_play(m); }
}
const char *ex_mediaview_path(const ExWidget *w) { const View *v = view_of(w); return v ? v->path : ""; }

void ex_mediaview_stats(const ExWidget *w, ExMediaViewStats *out) {
    const View *v = view_of(w);
    ExMpv *m;
    memset(out, 0, sizeof *out);
    if (!v) return;
    m = player(v);
    out->loading = v->loading; out->failed = v->failed; out->attached = m != NULL; out->playing = m && ex_mpv_state(m) == EX_MPV_PLAYING;
    out->has_picture = v->tex != NULL; out->loads_asked = v->loads_asked; out->frames_uploaded = v->uploaded; out->own_ms = v->own_ms;
    out->btn_x = v->btn_x; out->btn_y = v->btn_y; out->bar_x = v->bar_x; out->bar_y = v->bar_y + v->bar_h / 2.0f; out->bar_w = v->bar_w;
    out->vol_x = v->vol_x; out->vol_y = v->vol_y + v->vol_h / 2.0f; out->vol_w = v->vol_w; out->ext_x = v->ext_x + 4.0f; out->ext_y = v->ext_y + 4.0f;
}

static void time_text(double s, char *out, size_t cap) {
    long t = (long)(s + 0.5);
    if (t < 0) t = 0;
    if (t >= 3600) snprintf(out, cap, "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60); else snprintf(out, cap, "%ld:%02ld", t / 60, t % 60);
}

void ex_mediaview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    ExRenderer *r;
    const ExTheme *t;
    ExFont *f, *fb;
    ExFontMetrics fm;
    ExMpv *m;
    float sc, size, bar_h, area_h;
    ExColor c_bg, c_text, c_muted, c_surface, c_border, c_accent;
    double t0, pos = 0, dur = 0;
    char line[300], fit[600];
    if (!v) return;
    t0 = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
    r = ex_ui_renderer(ui); t = ex_ui_theme(ui);
    sc = ex_renderer_scale(r); size = ex_ui_base_size(ui);
    c_bg = ex_theme_color(t, EX_COLOR_BACKGROUND); c_text = ex_theme_color(t, EX_COLOR_TEXT); c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    c_surface = ex_theme_color(t, EX_COLOR_SURFACE); c_border = ex_theme_color(t, EX_COLOR_BORDER); c_accent = ex_theme_color(t, EX_COLOR_ACCENT);
    f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0); fb = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 1);
    ex_text_metrics(r, f, size, &fm);
    ex_fill_rect(r, w->x, w->y, w->w, w->h, c_bg);
    m = player(v);
    if (!m && !v->loading && !v->failed && v->loads_asked == 0) ask_load(v);
    bar_h = 64.0f * sc;
    area_h = w->h - bar_h;
    v->ext_w = 0; v->btn_r = 0;
    if (m) { pos = ex_mpv_position(m); dur = ex_mpv_duration(m); }
    if (v->failed || (m && ex_mpv_state(m) == EX_MPV_FAILED)) {
        const char *msg = v->failed ? v->err : (ex_mpv_error(m)[0] ? ex_mpv_error(m) : "This file cannot be played.");
        float y = w->y + 24.0f * sc;
        ex_text_fit(r, f, size, msg, strlen(msg), w->w - 40.0f * sc, fit, sizeof fit);
        ex_text_draw(r, f, size, w->x + 20.0f * sc, y, fit, strlen(fit), ex_theme_color(t, EX_COLOR_ERROR));
        if (v->h.open_external) {
            const char *label = "Open externally";
            float bw = ex_text_measure(r, f, size, label, strlen(label)) + 24.0f * sc, bh = (float)fm.line_height + 14.0f * sc;
            v->ext_x = w->x + 20.0f * sc; v->ext_y = y + (float)fm.line_height + 16.0f * sc; v->ext_w = bw; v->ext_h = bh;
            ex_fill_round_rect(r, v->ext_x, v->ext_y, bw, bh, 6.0f * sc, c_surface);
            ex_stroke_round_rect(r, v->ext_x, v->ext_y, bw, bh, 6.0f * sc, 1.0f * sc, c_border);
            ex_text_draw(r, f, size, v->ext_x + 12.0f * sc, v->ext_y + 7.0f * sc, label, strlen(label), c_text);
        }
        v->own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
        return;
    }
    {   /* the picture: the frame at the size it is shown at, uploaded when it is new */
        int vw = 0, vh = 0;
        if (m) ex_mpv_video_size(m, &vw, &vh);
        if (m && vw > 0 && vh > 0) {
            float k = fminf((w->w - 16.0f * sc) / (float)vw, (area_h - 16.0f * sc) / (float)vh), dw, dh;
            int fw = 0, fh = 0;
            const uint8_t *px;
            unsigned long serial = 0;
            if (k <= 0.0f) k = 0.1f;
            dw = (float)vw * k; dh = (float)vh * k;
            ex_mpv_set_target_size(m, (int)dw, (int)dh);
            px = ex_mpv_frame(m, &fw, &fh, &serial);
            if (px && serial != v->serial && fw > 0 && fh > 0) {
                if (!v->tex || v->tw != fw || v->th != fh) { ex_texture_free(v->tex); v->tex = ex_texture_create(r, fw, fh); v->tw = fw; v->th = fh; }
                if (v->tex && ex_texture_update_rows(v->tex, px, 0, fh) == 0) { v->serial = serial; v->uploaded++; }
            }
            if (v->tex) ex_texture_draw(r, v->tex, 0, 0, 0, 0, w->x + (w->w - dw) / 2.0f, w->y + (area_h - dh) / 2.0f, dw, dh, 255, 1);
            else ex_fill_rect(r, w->x + (w->w - dw) / 2.0f, w->y + (area_h - dh) / 2.0f, dw, dh, c_surface);
        } else {                                                                              /* no picture (audio, or not known yet): the name */
            const char *base = strrchr(v->path, '/');
            base = base ? base + 1 : v->path;
            ex_text_fit(r, fb, size * 1.3f, base, strlen(base), w->w - 40.0f * sc, fit, sizeof fit);
            ex_text_draw(r, fb, size * 1.3f, w->x + 20.0f * sc, w->y + area_h * 0.4f, fit, strlen(fit), c_text);
            if (v->loading) ex_text_draw(r, f, size, w->x + 20.0f * sc, w->y + area_h * 0.4f + (float)fm.line_height * 2.0f, "Loading...", 10, c_muted);
        }
    }
    {   /* the control bar */
        float by = w->y + area_h, rad = 18.0f * sc, bx = w->x + 16.0f * sc + rad, cy = by + bar_h / 2.0f;
        float px = bx + rad + 14.0f * sc, pw = w->x + w->w - 190.0f * sc - px, ph = 6.0f * sc;
        char a[32], b[32];
        ex_fill_rect(r, w->x, by, w->w, bar_h, c_surface);
        ex_fill_rect(r, w->x, by, w->w, 1.0f, c_border);
        v->btn_x = bx; v->btn_y = cy; v->btn_r = rad;
        ex_fill_round_rect(r, bx - rad, cy - rad, rad * 2.0f, rad * 2.0f, rad, v->loading ? c_border : c_accent);
        if (!v->loading) ex_icon_draw(r, m && ex_mpv_state(m) == EX_MPV_PLAYING ? EX_ICON_PAUSE : EX_ICON_PLAY, bx - rad * 0.7f, cy - rad * 0.7f, rad * 1.4f, rad * 1.4f, c_bg);
        if (pw < 40.0f * sc) pw = 40.0f * sc;
        v->bar_x = px; v->bar_w = pw; v->bar_y = cy - 14.0f * sc; v->bar_h = 28.0f * sc;
        ex_fill_round_rect(r, px, cy - ph / 2.0f - 6.0f * sc, pw, ph, ph / 2.0f, c_bg);
        if (m && dur > 0.0) {
            float fx = clampf((float)(pos / dur), 0.0f, 1.0f) * pw;
            ex_fill_round_rect(r, px, cy - ph / 2.0f - 6.0f * sc, fx < ph ? ph : fx, ph, ph / 2.0f, c_accent);
            ex_fill_round_rect(r, px + fx - 6.0f * sc, cy - 6.0f * sc - 6.0f * sc, 12.0f * sc, 12.0f * sc, 6.0f * sc, c_text);
        }
        time_text(pos, a, sizeof a); time_text(dur, b, sizeof b);
        snprintf(line, sizeof line, "%s / %s", a, m && dur > 0.0 ? b : "--:--");
        ex_text_draw(r, f, size * 0.85f, px, cy + 2.0f * sc, line, strlen(line), c_muted);
        {
            float vx = w->x + w->w - 130.0f * sc, vw2 = 100.0f * sc, vy = cy - 3.0f * sc, vh = 6.0f * sc, fx = v->volume * vw2;
            ex_icon_draw(r, EX_ICON_VOLUME, vx - 28.0f * sc, cy - 11.0f * sc, 22.0f * sc, 22.0f * sc, c_muted);
            v->vol_x = vx; v->vol_w = vw2; v->vol_y = vy - 10.0f * sc; v->vol_h = vh + 20.0f * sc;
            ex_fill_round_rect(r, vx, vy, vw2, vh, vh / 2.0f, c_bg);
            ex_fill_round_rect(r, vx, vy, fx < vh ? vh : fx, vh, vh / 2.0f, c_accent);
            ex_fill_round_rect(r, vx + fx - 6.0f * sc, vy + vh / 2.0f - 6.0f * sc, 12.0f * sc, 12.0f * sc, 6.0f * sc, c_text);
        }
    }
    v->own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
}

static void toggle(View *v) {
    ExMpv *m = player(v);
    if (m) { ex_mpv_toggle(m); return; }
    v->play_when_ready = 1;
    ask_load(v);
}

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    ExMpv *m = player(v);
    if (kind == EX_MOUSE_DOWN) {
        uint32_t now = SDL_GetTicks();
        if (under != w) return 0;
        ex_ui_set_focus(v->ui, w);
        if (v->ext_w > 0 && x >= v->ext_x && x <= v->ext_x + v->ext_w && y >= v->ext_y && y <= v->ext_y + v->ext_h) { if (v->h.open_external) v->h.open_external(v->h.user, v->path); return 1; }
        if (v->btn_r > 0 && (x - v->btn_x) * (x - v->btn_x) + (y - v->btn_y) * (y - v->btn_y) <= v->btn_r * v->btn_r) { toggle(v); return 1; }
        if (v->btn_r > 0 && x >= v->bar_x && x <= v->bar_x + v->bar_w && y >= v->bar_y && y <= v->bar_y + v->bar_h && m) v->drag = 1;
        else if (v->btn_r > 0 && x >= v->vol_x - 4.0f && x <= v->vol_x + v->vol_w + 4.0f && y >= v->vol_y && y <= v->vol_y + v->vol_h) v->drag = 2;
        else {
            if (v->last_click_ms && now - v->last_click_ms < 400) { v->last_click_ms = 0; toggle(v); }
            else v->last_click_ms = now;
            return 1;
        }
    }
    if ((kind == EX_MOUSE_DOWN || kind == EX_MOUSE_DRAG) && v->drag) {
        if (v->drag == 1 && m) ex_mpv_seek(m, (double)clampf((x - v->bar_x) / v->bar_w, 0.0f, 1.0f) * ex_mpv_duration(m));
        else if (v->drag == 2) { v->volume = clampf((x - v->vol_x) / v->vol_w, 0.0f, 1.0f); if (m) ex_mpv_set_volume(m, v->volume); }
        return 1;
    }
    if (kind == EX_MOUSE_UP) { v->drag = 0; return 1; }
    return 0;
}

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    ExMpv *m = player(v);
    (void)w; (void)mods;
    switch (key) {
    case EX_KEY_SPACE: toggle(v); return 1;
    case EX_KEY_LEFT: if (m) ex_mpv_seek(m, ex_mpv_position(m) - 5.0); return 1;
    case EX_KEY_RIGHT: if (m) ex_mpv_seek(m, ex_mpv_position(m) + 5.0); return 1;
    case EX_KEY_UP: v->volume = clampf(v->volume + 0.05f, 0.0f, 1.0f); if (m) ex_mpv_set_volume(m, v->volume); return 1;
    case EX_KEY_DOWN: v->volume = clampf(v->volume - 0.05f, 0.0f, 1.0f); if (m) ex_mpv_set_volume(m, v->volume); return 1;
    case EX_KEY_HOME: if (m) ex_mpv_seek(m, 0.0); return 1;
    default: return 0;
    }
}

static void view_free(void *p) { View *v = p; if (v) { ex_texture_free(v->tex); free(v->path); free(v); } }

ExWidget *ex_widget_mediaview(ExUi *ui, const char *path, const ExMediaViewHandlers *h) {
    ExWidget *w = ex_widget_box(ui, 0, path && *path ? path : "Media");
    View *v;
    size_t n;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    n = strlen(path ? path : "");
    v->path = malloc(n + 1);
    if (v->path) memcpy(v->path, path ? path : "", n + 1);
    v->ui = ui; v->volume = 1.0f;
    if (h) v->h = *h;
    w->type = EX_WIDGET_MEDIA;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1; w->clip = 1; w->vertical = 1;
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    return w;
}
