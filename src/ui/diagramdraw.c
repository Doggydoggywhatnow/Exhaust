/* Draws a diagram layout (6d-ii): shapes with fills and outlines, polylines with dashes and heads, wedges and text. Outlines and lines go through one
 * ExGeom that is submitted after each item, so later fills cover earlier lines the way the layout means. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_diagramdraw.h"

typedef struct Dc { ExRenderer *r; const ExTheme *t; ExGeom *g; float em, x0, y0; float scale; } Dc;

static ExColor col(const Dc *c, int k) {
    ExColor o;
    switch (k) {
    case EXD_C_TEXT: return ex_theme_color(c->t, EX_COLOR_TEXT);
    case EXD_C_MUTED: return ex_theme_color(c->t, EX_COLOR_TEXT_MUTED);
    case EXD_C_NODE_FILL: return ex_theme_color(c->t, EX_COLOR_SURFACE);
    case EXD_C_NODE_STROKE: return ex_theme_color(c->t, EX_COLOR_ACCENT);
    case EXD_C_ACCENT: return ex_theme_color(c->t, EX_COLOR_ACCENT);
    case EXD_C_ACCENT_FILL: o = ex_theme_color(c->t, EX_COLOR_ACCENT); o.a = 70; return o;
    case EXD_C_PANEL: return ex_theme_color(c->t, EX_COLOR_BACKGROUND_ALT);
    case EXD_C_LINE: return ex_theme_color(c->t, EX_COLOR_TEXT_MUTED);
    case EXD_C_LABEL_BG: return ex_theme_color(c->t, EX_COLOR_BACKGROUND);
    case EXD_C_NOTE_FILL: o = ex_theme_color(c->t, EX_COLOR_HIGHLIGHT); if (o.a > 120) o.a = 120; return o;
    case EXD_C_ERROR: return ex_theme_color(c->t, EX_COLOR_ERROR);
    default:
        if (k >= EXD_C_CAT) return ex_theme_color(c->t, (ExColorToken)(EX_COLOR_CALLOUT_NOTE + (k - EXD_C_CAT) % 13));
        return ex_theme_color(c->t, EX_COLOR_TEXT);
    }
}

static void poly_fill(Dc *c, const float *xy, int n, ExColor fill) {
    int i;
    for (i = 1; i + 1 < n; i++) ex_fill_triangle(c->r, xy[0], xy[1], xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], fill);
}

static void poly_line(Dc *c, const float *xy, int n, int closed, float w, ExColor color) {
    int i;
    ex_geom_reset(c->g);
    for (i = 0; i + 1 < n; i++) ex_geom_line(c->g, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w, color);
    if (closed && n > 2) ex_geom_line(c->g, xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], w, color);
    ex_geom_submit(c->r, c->g);
}

static int ellipse_pts(float cx, float cy, float rx, float ry, float *out, int seg) {
    int i;
    for (i = 0; i < seg; i++) { float a = 6.2831853f * (float)i / (float)seg; out[2 * i] = cx + rx * cosf(a); out[2 * i + 1] = cy + ry * sinf(a); }
    return seg;
}

static void shape(Dc *c, const ExDItem *it) {
    float x = c->x0 + it->x * c->em, y = c->y0 + it->y * c->em, w = it->w * c->em, h = it->h * c->em, lw = it->width * c->em, cx = x + w / 2.0f, cy = y + h / 2.0f;
    ExColor fill = col(c, it->fill), line = col(c, it->stroke);
    int has_fill = it->fill != EXD_C_NONE, has_line = it->stroke != EXD_C_NONE;
    float p[96];
    int n = 0;
    if (lw < 1.0f) lw = 1.0f;
    switch (it->kind) {
    case EXD_RECT:
        if (has_fill) ex_fill_rect(c->r, x, y, w, h, fill);
        if (has_line) { ex_geom_reset(c->g); ex_geom_line(c->g, x, y, x + w, y, lw, line); ex_geom_line(c->g, x + w, y, x + w, y + h, lw, line); ex_geom_line(c->g, x + w, y + h, x, y + h, lw, line); ex_geom_line(c->g, x, y + h, x, y, lw, line); ex_geom_submit(c->r, c->g); }
        return;
    case EXD_ROUND:
        if (has_fill) ex_fill_round_rect(c->r, x, y, w, h, 0.5f * c->em, fill);
        if (has_line) ex_stroke_round_rect(c->r, x, y, w, h, 0.5f * c->em, lw, line);
        return;
    case EXD_STADIUM:
        if (has_fill) ex_fill_round_rect(c->r, x, y, w, h, h / 2.0f, fill);
        if (has_line) ex_stroke_round_rect(c->r, x, y, w, h, h / 2.0f, lw, line);
        return;
    case EXD_SUBROUTINE:
        if (has_fill) ex_fill_rect(c->r, x, y, w, h, fill);
        if (has_line) {
            ex_geom_reset(c->g);
            ex_geom_line(c->g, x, y, x + w, y, lw, line); ex_geom_line(c->g, x + w, y, x + w, y + h, lw, line); ex_geom_line(c->g, x + w, y + h, x, y + h, lw, line); ex_geom_line(c->g, x, y + h, x, y, lw, line);
            ex_geom_line(c->g, x + 0.4f * c->em, y, x + 0.4f * c->em, y + h, lw, line); ex_geom_line(c->g, x + w - 0.4f * c->em, y, x + w - 0.4f * c->em, y + h, lw, line);
            ex_geom_submit(c->r, c->g);
        }
        return;
    case EXD_DIAMOND: p[0] = cx; p[1] = y; p[2] = x + w; p[3] = cy; p[4] = cx; p[5] = y + h; p[6] = x; p[7] = cy; n = 4; break;
    case EXD_ELLIPSE: n = ellipse_pts(cx, cy, w / 2.0f, h / 2.0f, p, 40); break;
    case EXD_DOUBLE_CIRCLE: {
        float q[96];
        int m = ellipse_pts(cx, cy, w / 2.0f, h / 2.0f, q, 40);
        if (has_fill) poly_fill(c, q, m, fill);
        if (has_line) poly_line(c, q, m, 1, lw, line);
        n = ellipse_pts(cx, cy, w / 2.0f - 0.3f * c->em, h / 2.0f - 0.3f * c->em, p, 40);
        has_fill = 0;
        break;
    }
    case EXD_HEXAGON: { float k = 0.7f * c->em; p[0] = x + k; p[1] = y; p[2] = x + w - k; p[3] = y; p[4] = x + w; p[5] = cy; p[6] = x + w - k; p[7] = y + h; p[8] = x + k; p[9] = y + h; p[10] = x; p[11] = cy; n = 6; break; }
    case EXD_PARALLELOGRAM: { float k = 0.7f * c->em; p[0] = x + k; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w - k; p[5] = y + h; p[6] = x; p[7] = y + h; n = 4; break; }
    case EXD_PARALLELOGRAM_ALT: { float k = 0.7f * c->em; p[0] = x; p[1] = y; p[2] = x + w - k; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x + k; p[7] = y + h; n = 4; break; }
    case EXD_TRAPEZOID: { float k = 0.7f * c->em; p[0] = x + k; p[1] = y; p[2] = x + w - k; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x; p[7] = y + h; n = 4; break; }
    case EXD_TRAPEZOID_ALT: { float k = 0.7f * c->em; p[0] = x; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w - k; p[5] = y + h; p[6] = x + k; p[7] = y + h; n = 4; break; }
    case EXD_FLAG: { float k = 0.8f * c->em; p[0] = x + k; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x + k; p[7] = y + h; p[8] = x; p[9] = cy; n = 5; break; }
    case EXD_CYLINDER: {
        float ry = 0.45f * c->em, q[2 * 34];
        int m;
        if (has_fill) { ex_fill_rect(c->r, x, y + ry, w, h - 2.0f * ry, fill); m = ellipse_pts(cx, y + h - ry, w / 2.0f, ry, q, 32); poly_fill(c, q, m, fill); m = ellipse_pts(cx, y + ry, w / 2.0f, ry, q, 32); poly_fill(c, q, m, fill); }
        if (has_line) {
            ex_geom_reset(c->g);
            ex_geom_line(c->g, x, y + ry, x, y + h - ry, lw, line); ex_geom_line(c->g, x + w, y + ry, x + w, y + h - ry, lw, line);
            ex_geom_submit(c->r, c->g);
            m = ellipse_pts(cx, y + ry, w / 2.0f, ry, q, 32); poly_line(c, q, m, 1, lw, line);
            m = ellipse_pts(cx, y + h - ry, w / 2.0f, ry, q, 32);
            q[2 * 32] = q[0]; q[2 * 32 + 1] = q[1];                    /* the front half of the bottom ellipse: points 16 to 32 (32 is 0 again) */
            poly_line(c, q + 2 * 16, 17, 0, lw, line);
        }
        return;
    }
    default: return;
    }
    if (has_fill && n) poly_fill(c, p, n, fill);
    if (has_line && n) poly_line(c, p, n, 1, lw, line);
}

static void head(Dc *c, int kind, float tx, float ty, float dx, float dy, float lw, ExColor line, ExColor bg) {
    float len = sqrtf(dx * dx + dy * dy), px, py, s = 0.75f * c->em, w = 0.32f * c->em;
    if (len < 1e-4f || kind == EXD_H_NONE) return;
    dx /= len; dy /= len;   /* the direction the line is travelling, towards the tip */
    px = -dy; py = dx;
    switch (kind) {
    case EXD_H_ARROW: ex_fill_triangle(c->r, tx, ty, tx - dx * s + px * w, ty - dy * s + py * w, tx - dx * s - px * w, ty - dy * s - py * w, line); break;
    case EXD_H_OPEN: {
        float a[6] = { tx - dx * s + px * w, ty - dy * s + py * w, tx, ty, tx - dx * s - px * w, ty - dy * s - py * w };
        poly_line(c, a, 3, 0, lw, line);
        break;
    }
    case EXD_H_CROSS: {
        float a[4], b[4], k = 0.35f * c->em;
        float mx = tx - dx * k, my = ty - dy * k;
        a[0] = mx - dx * k + px * k; a[1] = my - dy * k + py * k; a[2] = mx + dx * k - px * k; a[3] = my + dy * k - py * k;
        b[0] = mx - dx * k - px * k; b[1] = my - dy * k - py * k; b[2] = mx + dx * k + px * k; b[3] = my + dy * k + py * k;
        poly_line(c, a, 2, 0, lw * 1.4f, line); poly_line(c, b, 2, 0, lw * 1.4f, line);
        break;
    }
    case EXD_H_CIRCLE: {
        float q[48], r0 = 0.28f * c->em;
        int m = ellipse_pts(tx - dx * r0, ty - dy * r0, r0, r0, q, 16);
        poly_fill(c, q, m, bg); poly_line(c, q, m, 1, lw, line);
        break;
    }
    case EXD_H_TRIANGLE: {
        float a[6] = { tx, ty, tx - dx * s + px * w * 1.2f, ty - dy * s + py * w * 1.2f, tx - dx * s - px * w * 1.2f, ty - dy * s - py * w * 1.2f };
        poly_fill(c, a, 3, bg); poly_line(c, a, 3, 1, lw, line);
        break;
    }
    case EXD_H_DIAMOND: case EXD_H_DIAMOND_FILLED: {
        float a[8] = { tx, ty, tx - dx * s * 0.6f + px * w, ty - dy * s * 0.6f + py * w, tx - dx * s * 1.2f, ty - dy * s * 1.2f, tx - dx * s * 0.6f - px * w, ty - dy * s * 0.6f - py * w };
        poly_fill(c, a, 4, kind == EXD_H_DIAMOND_FILLED ? line : bg); poly_line(c, a, 4, 1, lw, line);
        break;
    }
    case EXD_H_ONE: case EXD_H_ONLY_ONE: case EXD_H_ZERO_ONE: case EXD_H_MANY: case EXD_H_ONE_MANY: case EXD_H_ZERO_MANY: {
        float k = 0.45f * c->em;
        int bar = kind == EXD_H_ONE || kind == EXD_H_ONLY_ONE || kind == EXD_H_ZERO_ONE || kind == EXD_H_ONE_MANY;
        int ring = kind == EXD_H_ZERO_ONE || kind == EXD_H_ZERO_MANY;
        int crow = kind == EXD_H_MANY || kind == EXD_H_ONE_MANY || kind == EXD_H_ZERO_MANY;
        float bx = tx - dx * (crow ? 1.0f : 0.45f) * k * 1.6f, by = ty - dy * (crow ? 1.0f : 0.45f) * k * 1.6f;
        if (crow) {
            float a[4], b[4], cc[4];
            float fx = tx - dx * k * 1.8f, fy = ty - dy * k * 1.8f;
            a[0] = fx; a[1] = fy; a[2] = tx + px * k; a[3] = ty + py * k;
            b[0] = fx; b[1] = fy; b[2] = tx - px * k; b[3] = ty - py * k;
            cc[0] = fx; cc[1] = fy; cc[2] = tx; cc[3] = ty;
            poly_line(c, a, 2, 0, lw, line); poly_line(c, b, 2, 0, lw, line); poly_line(c, cc, 2, 0, lw, line);
            bx = tx - dx * k * 2.1f; by = ty - dy * k * 2.1f;
        }
        if (bar) {
            float a[4];
            if (kind == EXD_H_ONLY_ONE) { float bx2 = bx - dx * k * 0.5f, by2 = by - dy * k * 0.5f; a[0] = bx2 + px * k; a[1] = by2 + py * k; a[2] = bx2 - px * k; a[3] = by2 - py * k; poly_line(c, a, 2, 0, lw, line); }
            a[0] = bx + px * k; a[1] = by + py * k; a[2] = bx - px * k; a[3] = by - py * k;
            poly_line(c, a, 2, 0, lw, line);
        }
        if (ring) {
            float q[48], r0 = 0.28f * c->em;
            int m = ellipse_pts(bx - dx * (crow ? 0.6f : 1.4f) * k, by - dy * (crow ? 0.6f : 1.4f) * k, r0, r0, q, 16);
            poly_fill(c, q, m, bg); poly_line(c, q, m, 1, lw, line);
        }
        break;
    }
    default: break;
    }
}

/* Where a head ends the line: the line stops before the tip so a filled arrow does not poke through. */
static void line_item(Dc *c, const ExDiagram *d, const ExDItem *it) {
    size_t np = it->npt, i, nf = 0;
    float *pts = malloc(np * 2 * sizeof(float));
    const float *src;
    float lw = it->width * c->em;
    ExColor line = col(c, it->stroke), bg = col(c, EXD_C_LABEL_BG);
    size_t total;
    if (!pts) return;
    src = ex_diagram_points(d, &total);
    for (i = 0; i < np; i++) { pts[2 * i] = c->x0 + src[it->pt0 + 2 * i] * c->em; pts[2 * i + 1] = c->y0 + src[it->pt0 + 2 * i + 1] * c->em; }
    if (lw < 1.0f) lw = 1.0f;
    (void)nf;
    if (it->dash == 0) poly_line(c, pts, (int)np, 0, lw, line);
    else {   /* dashes along the polyline */
        float on = (it->dash == 1 ? 0.55f : 0.12f) * c->em, off = (it->dash == 1 ? 0.4f : 0.22f) * c->em;
        float carry = 0.0f;
        int drawing = 1;
        ex_geom_reset(c->g);
        for (i = 0; i + 1 < np; i++) {
            float x1 = pts[2 * i], y1 = pts[2 * i + 1], x2 = pts[2 * i + 2], y2 = pts[2 * i + 3], dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy), pos = 0;
            if (len < 1e-4f) continue;
            while (pos < len) {
                float remain = (drawing ? on : off) - carry, step = remain < len - pos ? remain : len - pos;
                if (drawing) ex_geom_line(c->g, x1 + dx * pos / len, y1 + dy * pos / len, x1 + dx * (pos + step) / len, y1 + dy * (pos + step) / len, lw, line);
                pos += step; carry += step;
                if (carry >= (drawing ? on : off) - 1e-4f) { drawing = !drawing; carry = 0; }
            }
        }
        ex_geom_submit(c->r, c->g);
    }
    if (np >= 2) {
        if (it->head_end) head(c, it->head_end, pts[2 * np - 2], pts[2 * np - 1], pts[2 * np - 2] - pts[2 * np - 4], pts[2 * np - 1] - pts[2 * np - 3], lw, line, bg);
        if (it->head_start) head(c, it->head_start, pts[0], pts[1], pts[0] - pts[2], pts[1] - pts[3], lw, line, bg);
    }
    free(pts);
}

static void text_item(Dc *c, const ExDiagram *d, const ExDItem *it) {
    ExFont *font = ex_diagram_font(d), *bold = ex_font_builtin(ex_renderer_fonts(c->r), EX_FONT_UI_BOLD);
    const char *s = ex_diagram_text(d, it);
    float size = c->scale * (it->size > 0 ? it->size : 1.0f), pitch = 1.25f * (it->size > 0 ? it->size : 1.0f) * c->em;
    float x = c->x0 + it->x * c->em, yc = c->y0 + it->y * c->em;
    ExFontMetrics m;
    ExColor color = col(c, it->stroke);
    size_t pos = 0, len = it->tlen;
    ExFont *f = it->bold && bold ? bold : font;
    if (ex_text_metrics(c->r, f, size, &m)) return;
    while (pos <= len) {
        size_t e = pos;
        float w;
        while (e < len && s[e] != '\n') e++;
        w = ex_text_measure(c->r, f, size, s + pos, e - pos);
        {
            float lx = it->align == 1 ? x : (it->align == 2 ? x - w : x - w / 2.0f);
            ex_text_draw(c->r, f, size, lx, yc - (float)m.line_height / 2.0f, s + pos, e - pos, color);
        }
        yc += pitch;
        if (e >= len) break;
        pos = e + 1;
    }
}

static void wedge_item(Dc *c, const ExDItem *it) {
    float cx = c->x0 + it->x * c->em, cy = c->y0 + it->y * c->em, r = it->w * c->em, a0 = it->a0, a1 = it->a1;
    int seg = (int)((a1 - a0) / 0.15f) + 2, i;
    float p[2 * 70];
    ExColor fill = col(c, it->fill), line = col(c, it->stroke);
    if (seg > 66) seg = 66;
    if (a1 - a0 >= 6.2831f) { float q[96]; int m = ellipse_pts(cx, cy, r, r, q, 48); poly_fill(c, q, m, fill); if (it->stroke) poly_line(c, q, m, 1, it->width * c->em, line); return; }
    p[0] = cx; p[1] = cy;
    for (i = 0; i <= seg; i++) { float a = a0 + (a1 - a0) * (float)i / (float)seg; p[2 * (i + 1)] = cx + r * sinf(a); p[2 * (i + 1) + 1] = cy - r * cosf(a); }
    poly_fill(c, p, seg + 2, fill);
    if (it->stroke) poly_line(c, p, seg + 2, 1, it->width * c->em, line);
}

void ex_diagram_draw(ExRenderer *r, const ExTheme *t, const ExDiagram *d, float size, float x, float y) {
    static ExGeom *geom;
    Dc c;
    size_t n, i;
    const ExDItem *it;
    if (!r || !t || !d || ex_diagram_error(d)) return;
    if (!geom) { geom = ex_geom_create(); if (!geom) return; ex_geom_set_feather(geom, 0.7f); }
    c.r = r; c.t = t; c.g = geom; c.scale = size; c.em = size * ex_renderer_scale(r); c.x0 = x; c.y0 = y;
    it = ex_diagram_items(d, &n);
    for (i = 0; i < n; i++) {
        switch (it[i].kind) {
        case EXD_LINE: line_item(&c, d, &it[i]); break;
        case EXD_TEXT: text_item(&c, d, &it[i]); break;
        case EXD_WEDGE: wedge_item(&c, &it[i]); break;
        default: shape(&c, &it[i]); break;
        }
    }
}
