/* The slider widget (sub-slice 5b): a track, a filled part, a value. See exhaust_ui.h. Built from two boxes, so it follows the theme, the
 * interface size and the accessibility tree like every other widget; the mouse and keyboard hooks do the rest. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ui.h"

typedef struct Slider {
    float min, max, step, value;
    ExUi *ui;
    ExSliderFn fn;
    void *user;
    ExWidget *fill;
    char *label;                     /* the name without the value */
} Slider;

static Slider *of(const ExWidget *w) { return w ? (Slider *)w->owned : NULL; }

static void slider_free(void *p) { Slider *s = p; if (s) { free(s->label); free(s); } }

static float snap(const Slider *s, float v) {
    if (s->step > 0.0f) v = s->min + roundf((v - s->min) / s->step) * s->step;
    if (!(v >= s->min)) v = s->min;
    if (v > s->max) v = s->max;
    return v;
}

static void refresh(ExWidget *w) {
    Slider *s = of(w);
    char buf[200];
    float f = s->max > s->min ? (s->value - s->min) / (s->max - s->min) : 0.0f;
    s->fill->width = ex_size_percent(f);
    snprintf(buf, sizeof buf, "%s: %.3g", s->label, (double)s->value);
    free(w->name);
    w->name = malloc(strlen(buf) + 1);
    if (w->name) strcpy(w->name, buf);
}

static void set_user(ExWidget *w, float v) {
    Slider *s = of(w);
    v = snap(s, v);
    if (v == s->value) return;
    s->value = v;
    refresh(w);
    if (s->fn) s->fn(w, v, s->user);
}

static int slider_mouse(ExWidget *w, ExMouse kind, float x, float y, ExWidget *under, void *user) {
    Slider *s = user;
    (void)y; (void)under;
    if (kind == EX_MOUSE_DOWN) ex_ui_set_focus(s->ui, w);
    if (kind == EX_MOUSE_UP) return 1;
    if (w->w > 0.0f) {
        float f = (x - w->x) / w->w;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        set_user(w, s->min + f * (s->max - s->min));
    }
    return 1;
}

static int slider_key(ExWidget *w, ExKey key, int mods, void *user) {
    Slider *s = user;
    float st = s->step > 0.0f ? s->step : (s->max - s->min) / 100.0f;
    (void)mods;
    switch (key) {
    case EX_KEY_LEFT: case EX_KEY_DOWN: set_user(w, s->value - st); return 1;
    case EX_KEY_RIGHT: case EX_KEY_UP: set_user(w, s->value + st); return 1;
    case EX_KEY_PAGE_DOWN: set_user(w, s->value - 10.0f * st); return 1;
    case EX_KEY_PAGE_UP: set_user(w, s->value + 10.0f * st); return 1;
    case EX_KEY_HOME: set_user(w, s->min); return 1;
    case EX_KEY_END: set_user(w, s->max); return 1;
    default: return 0;
    }
}

ExWidget *ex_widget_slider(ExUi *ui, const char *name, float min, float max, float step, float value, ExSliderFn fn, void *user) {
    ExWidget *w = ex_widget_box(ui, 0, name), *fill;
    Slider *s;
    if (!w) return NULL;
    s = calloc(1, sizeof *s);
    fill = ex_widget_box(ui, 0, "");
    if (!s || !fill) { free(s); ex_widget_free(fill); ex_widget_free(w); return NULL; }
    s->ui = ui; s->min = min; s->max = max; s->step = step; s->fn = fn; s->user = user; s->fill = fill;
    s->label = malloc(strlen(name) + 1);
    if (s->label) strcpy(s->label, name);
    s->value = snap(s, value);
    w->width = ex_size_grow();
    w->height = ex_size_token(EX_SIZE_SPACING, 1.0f);
    w->bg_token = EX_COLOR_BACKGROUND;
    w->border_token = EX_COLOR_BORDER;
    w->radius_token = EX_SIZE_RADIUS;
    w->focusable = 1;
    w->owned = s; w->free_owned = slider_free;
    w->on_mouse = slider_mouse; w->on_mouse_user = s;
    w->on_key = slider_key; w->on_key_user = s;
    fill->height = ex_size_grow();
    fill->bg_token = EX_COLOR_ACCENT;
    fill->radius_token = EX_SIZE_RADIUS;
    ex_widget_add(w, fill);
    refresh(w);
    return w;
}

void ex_slider_set_value(ExWidget *w, float v) {
    Slider *s = of(w);
    if (!s) return;
    v = snap(s, v);
    if (v == s->value) return;
    s->value = v;
    refresh(w);
}

float ex_slider_value(const ExWidget *w) { Slider *s = of(w); return s ? s->value : 0.0f; }
