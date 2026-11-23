/* The audio tab: see include/exhaust_audioview.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_audioview.h"
#include "exhaust_render.h"

typedef struct View {
    ExUi *ui;
    ExPlayer *player;
    char *path;
    ExAudioViewHandlers h;
    int loading, failed, play_when_ready;
    char err[300];
    unsigned loads_asked;
    int drag;                             /* 0 none, 1 position bar, 2 volume bar */
    float bar_x, bar_w, bar_y, bar_h, vol_x, vol_w, vol_y, vol_h, btn_x, btn_y, btn_r, ext_x, ext_y, ext_w, ext_h;
    double own_ms;
} View;

static View *view_of(const ExWidget *w) { return w && w->type == EX_WIDGET_AUDIO ? (View *)w->owned : NULL; }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static int attached(const View *v) { return ex_player_has_device(v->player) && ex_player_duration(v->player) >= 0.0 && ex_player_state(v->player) != EX_PLAYER_IDLE && !strcmp(ex_player_path(v->player), v->path); }

static void ask_load(View *v) {
    if (v->loading || !v->h.load) return;
    v->loading = 1; v->failed = 0; v->err[0] = 0; v->loads_asked++;
    v->h.load(v->h.user, v->path);
}

void ex_audioview_loaded(ExWidget *w, const char *error) {
    View *v = view_of(w);
    if (!v) return;
    v->loading = 0;
    if (error) { v->failed = 1; v->play_when_ready = 0; snprintf(v->err, sizeof v->err, "%s", error); return; }
    v->failed = 0;
    if (v->play_when_ready) { v->play_when_ready = 0; ex_player_play(v->player); }
}

void ex_audioview_stats(const ExWidget *w, ExAudioViewStats *out) {
    const View *v = view_of(w);
    memset(out, 0, sizeof *out);
    if (!v) return;
    out->loading = v->loading; out->failed = v->failed; out->attached = attached(v);
    out->playing = out->attached && ex_player_state(v->player) == EX_PLAYER_PLAYING;
    out->position = out->attached ? ex_player_position(v->player) : 0.0; out->duration = out->attached ? ex_player_duration(v->player) : 0.0;
    out->loads_asked = v->loads_asked; out->own_ms = v->own_ms;
    out->btn_x = v->btn_x; out->btn_y = v->btn_y; out->bar_x = v->bar_x; out->bar_y = v->bar_y + v->bar_h / 2.0f; out->bar_w = v->bar_w;
    out->vol_x = v->vol_x; out->vol_y = v->vol_y + v->vol_h / 2.0f; out->vol_w = v->vol_w; out->ext_x = v->ext_x + 4.0f; out->ext_y = v->ext_y + 4.0f;
}
const char *ex_audioview_path(const ExWidget *w) { const View *v = view_of(w); return v ? v->path : ""; }

static void time_text(double s, char *out, size_t cap) {
    long t = (long)(s + 0.5);
    if (t < 0) t = 0;
    if (t >= 3600) snprintf(out, cap, "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60); else snprintf(out, cap, "%ld:%02ld", t / 60, t % 60);
}

void ex_audioview_draw(ExUi *ui, ExWidget *w) {
    View *v = view_of(w);
    ExRenderer *r;
    const ExTheme *t;
    ExFont *f, *fb;
    ExFontMetrics fm;
    float sc, col_w, x0, y, size;
    ExColor c_bg, c_text, c_muted, c_surface, c_border, c_accent;
    char line[700], fit[700];
    int att;
    double t0, pos = 0, dur = 0;
    if (!v) return;
    t0 = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
    r = ex_ui_renderer(ui); t = ex_ui_theme(ui);
    sc = ex_renderer_scale(r); size = ex_ui_base_size(ui);
    c_bg = ex_theme_color(t, EX_COLOR_BACKGROUND); c_text = ex_theme_color(t, EX_COLOR_TEXT); c_muted = ex_theme_color(t, EX_COLOR_TEXT_MUTED);
    c_surface = ex_theme_color(t, EX_COLOR_SURFACE); c_border = ex_theme_color(t, EX_COLOR_BORDER); c_accent = ex_theme_color(t, EX_COLOR_ACCENT);
    f = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 0); fb = ex_ui_font(ui, EX_FONT_TOKEN_INTERFACE, 1);
    ex_text_metrics(r, f, size, &fm);
    ex_fill_rect(r, w->x, w->y, w->w, w->h, c_bg);
    att = attached(v);
    if (!att && !v->loading && !v->failed && v->loads_asked == 0) ask_load(v);                      /* opening the tab loads the file, paused */
    if (att) { pos = ex_player_position(v->player); dur = ex_player_duration(v->player); }
    col_w = w->w - 48.0f * sc < 560.0f * sc ? w->w - 48.0f * sc : 560.0f * sc;
    if (col_w < 120.0f * sc) col_w = w->w - 8.0f * sc;
    x0 = w->x + (w->w - col_w) / 2.0f;
    y = w->y + (w->h > 360.0f * sc ? w->h * 0.28f : 24.0f * sc);
    {   /* the name and the format */
        const char *base = strrchr(v->path, '/');
        base = base ? base + 1 : v->path;
        ex_text_fit(r, fb, size * 1.3f, base, strlen(base), col_w, fit, sizeof fit);
        ex_text_draw(r, fb, size * 1.3f, x0, y, fit, strlen(fit), c_text);
        y += (float)fm.line_height * 1.5f + 6.0f * sc;
        if (att) {
            snprintf(line, sizeof line, "%s", ex_player_info(v->player));
            if (line[0]) { ex_text_fit(r, f, size, line, strlen(line), col_w, fit, sizeof fit); ex_text_draw(r, f, size, x0, y, fit, strlen(fit), c_muted); }
        }
        y += (float)fm.line_height + 22.0f * sc;
    }
    if (v->failed) {
        ex_text_fit(r, f, size, v->err, strlen(v->err), col_w, fit, sizeof fit);
        ex_text_draw(r, f, size, x0, y, fit, strlen(fit), ex_theme_color(t, EX_COLOR_ERROR));
        if (v->h.open_external) {
            const char *label = "Open externally";
            float bw = ex_text_measure(r, f, size, label, strlen(label)) + 24.0f * sc, bh = (float)fm.line_height + 14.0f * sc;
            v->ext_x = x0; v->ext_y = y + (float)fm.line_height + 16.0f * sc; v->ext_w = bw; v->ext_h = bh;
            ex_fill_round_rect(r, v->ext_x, v->ext_y, bw, bh, 6.0f * sc, c_surface);
            ex_stroke_round_rect(r, v->ext_x, v->ext_y, bw, bh, 6.0f * sc, 1.0f * sc, c_border);
            ex_text_draw(r, f, size, v->ext_x + 12.0f * sc, v->ext_y + 7.0f * sc, label, strlen(label), c_text);
        } else v->ext_w = 0;
        v->btn_r = 0;
    } else {
        float rad = 26.0f * sc, bx = x0 + rad, by = y + rad;
        v->ext_w = 0;
        v->btn_x = bx; v->btn_y = by; v->btn_r = rad;
        ex_fill_round_rect(r, bx - rad, by - rad, rad * 2.0f, rad * 2.0f, rad, v->loading ? c_surface : c_accent);
        if (v->loading) ex_text_draw(r, f, size * 0.8f, bx - rad * 0.55f, by - (float)fm.line_height * 0.4f, "...", 3, c_muted);
        else ex_icon_draw(r, att && ex_player_state(v->player) == EX_PLAYER_PLAYING ? EX_ICON_PAUSE : EX_ICON_PLAY, bx - rad * 0.7f, by - rad * 0.7f, rad * 1.4f, rad * 1.4f, ex_theme_color(t, EX_COLOR_BACKGROUND));
        {   /* the position bar and the times */
            float px = bx + rad + 18.0f * sc, pw = x0 + col_w - px, ph = 6.0f * sc, py = by - 10.0f * sc;
            char a[32], b[32];
            v->bar_x = px; v->bar_w = pw > 40.0f * sc ? pw : 40.0f * sc; v->bar_y = py - 10.0f * sc; v->bar_h = ph + 20.0f * sc;
            ex_fill_round_rect(r, px, py, v->bar_w, ph, ph / 2.0f, c_surface);
            if (att && dur > 0.0) {
                float fx = (float)clampf((float)(pos / dur), 0.0f, 1.0f) * v->bar_w;
                ex_fill_round_rect(r, px, py, fx < ph ? ph : fx, ph, ph / 2.0f, c_accent);
                ex_fill_round_rect(r, px + fx - 7.0f * sc, py + ph / 2.0f - 7.0f * sc, 14.0f * sc, 14.0f * sc, 7.0f * sc, c_text);
            }
            time_text(pos, a, sizeof a); time_text(dur, b, sizeof b);
            snprintf(line, sizeof line, "%s / %s", a, att && dur > 0.0 ? b : "--:--");
            ex_text_draw(r, f, size * 0.9f, px, py + ph + 12.0f * sc, line, strlen(line), c_muted);
        }
        {   /* the volume */
            float vx = x0 + 34.0f * sc, vw = 140.0f * sc < col_w - 40.0f * sc ? 140.0f * sc : col_w - 40.0f * sc, vy = by + rad + 36.0f * sc, vh = 6.0f * sc, fx;
            ex_icon_draw(r, EX_ICON_VOLUME, x0, vy - 9.0f * sc, 24.0f * sc, 24.0f * sc, c_muted);
            v->vol_x = vx; v->vol_w = vw > 20.0f * sc ? vw : 20.0f * sc; v->vol_y = vy - 10.0f * sc; v->vol_h = vh + 20.0f * sc;
            ex_fill_round_rect(r, vx, vy, v->vol_w, vh, vh / 2.0f, c_surface);
            fx = ex_player_volume(v->player) * v->vol_w;
            ex_fill_round_rect(r, vx, vy, fx < vh ? vh : fx, vh, vh / 2.0f, c_accent);
            ex_fill_round_rect(r, vx + fx - 6.0f * sc, vy + vh / 2.0f - 6.0f * sc, 12.0f * sc, 12.0f * sc, 6.0f * sc, c_text);
        }
    }
    v->own_ms = (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency() - t0;
}

static void toggle(View *v) {
    if (attached(v)) { ex_player_toggle(v->player); return; }
    v->play_when_ready = 1;
    ask_load(v);
}

static int mouse_hook(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    View *v = user;
    if (kind == EX_MOUSE_DOWN) {
        if (under != w) return 0;
        ex_ui_set_focus(v->ui, w);
        if (v->ext_w > 0 && x >= v->ext_x && x <= v->ext_x + v->ext_w && y >= v->ext_y && y <= v->ext_y + v->ext_h) { if (v->h.open_external) v->h.open_external(v->h.user, v->path); return 1; }
        if (v->btn_r > 0 && (x - v->btn_x) * (x - v->btn_x) + (y - v->btn_y) * (y - v->btn_y) <= v->btn_r * v->btn_r) { toggle(v); return 1; }
        if (v->btn_r > 0 && x >= v->bar_x && x <= v->bar_x + v->bar_w && y >= v->bar_y && y <= v->bar_y + v->bar_h && attached(v)) { v->drag = 1; }
        else if (v->btn_r > 0 && x >= v->vol_x - 4.0f && x <= v->vol_x + v->vol_w + 4.0f && y >= v->vol_y && y <= v->vol_y + v->vol_h) v->drag = 2;
        else return 1;
    }
    if ((kind == EX_MOUSE_DOWN || kind == EX_MOUSE_DRAG) && v->drag) {
        if (v->drag == 1 && attached(v)) { double dur = ex_player_duration(v->player); ex_player_seek(v->player, (double)clampf((x - v->bar_x) / v->bar_w, 0.0f, 1.0f) * dur); }
        else if (v->drag == 2) ex_player_set_volume(v->player, clampf((x - v->vol_x) / v->vol_w, 0.0f, 1.0f));
        return 1;
    }
    if (kind == EX_MOUSE_UP) { v->drag = 0; return 1; }
    return 0;
}

static int key_hook(ExWidget *w, ExKey key, int mods, void *user) {
    View *v = user;
    (void)w; (void)mods;
    switch (key) {
    case EX_KEY_SPACE: toggle(v); return 1;
    case EX_KEY_LEFT: if (attached(v)) ex_player_seek(v->player, ex_player_position(v->player) - 5.0); return 1;
    case EX_KEY_RIGHT: if (attached(v)) ex_player_seek(v->player, ex_player_position(v->player) + 5.0); return 1;
    case EX_KEY_UP: ex_player_set_volume(v->player, ex_player_volume(v->player) + 0.05f); return 1;
    case EX_KEY_DOWN: ex_player_set_volume(v->player, ex_player_volume(v->player) - 0.05f); return 1;
    case EX_KEY_HOME: if (attached(v)) ex_player_seek(v->player, 0.0); return 1;
    default: return 0;
    }
}

static void view_free(void *p) { View *v = p; if (v) { free(v->path); free(v); } }

ExWidget *ex_widget_audioview(ExUi *ui, ExPlayer *player, const char *path, const ExAudioViewHandlers *h) {
    ExWidget *w = ex_widget_box(ui, 0, path && *path ? path : "Audio");
    View *v;
    size_t n;
    if (!w) return NULL;
    v = calloc(1, sizeof *v);
    if (!v) { ex_widget_free(w); return NULL; }
    n = strlen(path ? path : "");
    v->path = malloc(n + 1);
    if (v->path) memcpy(v->path, path ? path : "", n + 1);
    v->ui = ui; v->player = player;
    if (h) v->h = *h;
    w->type = EX_WIDGET_AUDIO;
    w->width = ex_size_grow(); w->height = ex_size_grow();
    w->bg_token = EX_COLOR_BACKGROUND;
    w->focusable = 1; w->clip = 1; w->vertical = 1;
    w->owned = v; w->free_owned = view_free;
    w->on_mouse = mouse_hook; w->on_mouse_user = v;
    w->on_key = key_hook; w->on_key_user = v;
    return w;
}
