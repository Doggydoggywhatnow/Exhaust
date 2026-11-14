/* The icons of the buttons (sub-slice 4d), drawn with the renderer's rectangles and triangles in the text color of the
 * button, so they follow the theme and the interface size. Each fits a square of side s; every button that shows one
 * keeps its accessible name. */
#include <math.h>

#include "exhaust_ui.h"

/* A line of the given thickness from (x1, y1) to (x2, y2): two triangles. */
static void line(ExRenderer *r, float x1, float y1, float x2, float y2, float th, ExColor c) {
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy), nx, ny;
    if (len < 0.001f) return;
    nx = -dy / len * th / 2.0f; ny = dx / len * th / 2.0f;
    ex_fill_triangle(r, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 + nx, y2 + ny, c);
    ex_fill_triangle(r, x1 - nx, y1 - ny, x2 - nx, y2 - ny, x2 + nx, y2 + ny, c);
}

static void dot(ExRenderer *r, float cx, float cy, float rad, ExColor c) { ex_fill_round_rect(r, cx - rad, cy - rad, 2 * rad, 2 * rad, rad, c); }

static void folder(ExRenderer *r, float x, float y, float s, float th, ExColor c) {
    ex_fill_round_rect(r, x + s * 0.12f, y + s * 0.22f, s * 0.36f, s * 0.2f, th, c);                 /* the tab */
    ex_stroke_round_rect(r, x + s * 0.12f, y + s * 0.32f, s * 0.76f, s * 0.5f, th, th, c);          /* the body */
}

static void page(ExRenderer *r, float x, float y, float s, float th, ExColor c) {
    ex_stroke_round_rect(r, x + s * 0.22f, y + s * 0.12f, s * 0.56f, s * 0.76f, th, th, c);
    line(r, x + s * 0.34f, y + s * 0.38f, x + s * 0.66f, y + s * 0.38f, th, c);
    line(r, x + s * 0.34f, y + s * 0.56f, x + s * 0.66f, y + s * 0.56f, th, c);
}

static void plus(ExRenderer *r, float cx, float cy, float a, float th, ExColor c) { line(r, cx - a, cy, cx + a, cy, th, c); line(r, cx, cy - a, cx, cy + a, th, c); }

/* Draws icon (ExIcon) in the box x, y, w, h. */
void ex_icon_draw(ExRenderer *r, int icon, float x, float y, float w, float h, ExColor c) {
    float s = w < h ? w : h, th = fmaxf(1.0f, s / 12.0f);
    x += (w - s) / 2.0f; y += (h - s) / 2.0f;
    switch (icon) {
    case EX_ICON_FILES: folder(r, x, y, s, th, c); break;
    case EX_ICON_SEARCH:
        ex_stroke_round_rect(r, x + s * 0.14f, y + s * 0.14f, s * 0.5f, s * 0.5f, s * 0.25f, th, c);
        line(r, x + s * 0.56f, y + s * 0.56f, x + s * 0.86f, y + s * 0.86f, th * 1.3f, c);
        break;
    case EX_ICON_BOOKMARK:
        ex_fill_triangle(r, x + s * 0.25f, y + s * 0.12f, x + s * 0.75f, y + s * 0.12f, x + s * 0.5f, y + s * 0.62f, c);
        ex_fill_triangle(r, x + s * 0.25f, y + s * 0.12f, x + s * 0.25f, y + s * 0.88f, x + s * 0.5f, y + s * 0.62f, c);
        ex_fill_triangle(r, x + s * 0.75f, y + s * 0.12f, x + s * 0.75f, y + s * 0.88f, x + s * 0.5f, y + s * 0.62f, c);
        break;
    case EX_ICON_BACKLINKS:                                                     /* an arrow into a bar */
        line(r, x + s * 0.22f, y + s * 0.5f, x + s * 0.82f, y + s * 0.5f, th, c);
        line(r, x + s * 0.22f, y + s * 0.5f, x + s * 0.46f, y + s * 0.26f, th, c);
        line(r, x + s * 0.22f, y + s * 0.5f, x + s * 0.46f, y + s * 0.74f, th, c);
        line(r, x + s * 0.14f, y + s * 0.2f, x + s * 0.14f, y + s * 0.8f, th, c);
        break;
    case EX_ICON_LINKS:                                                         /* an arrow out of a bar */
        line(r, x + s * 0.18f, y + s * 0.5f, x + s * 0.78f, y + s * 0.5f, th, c);
        line(r, x + s * 0.78f, y + s * 0.5f, x + s * 0.54f, y + s * 0.26f, th, c);
        line(r, x + s * 0.78f, y + s * 0.5f, x + s * 0.54f, y + s * 0.74f, th, c);
        line(r, x + s * 0.86f, y + s * 0.2f, x + s * 0.86f, y + s * 0.8f, th, c);
        break;
    case EX_ICON_TAGS:                                                          /* a hash sign */
        line(r, x + s * 0.4f, y + s * 0.16f, x + s * 0.32f, y + s * 0.84f, th, c);
        line(r, x + s * 0.68f, y + s * 0.16f, x + s * 0.6f, y + s * 0.84f, th, c);
        line(r, x + s * 0.16f, y + s * 0.38f, x + s * 0.84f, y + s * 0.38f, th, c);
        line(r, x + s * 0.14f, y + s * 0.62f, x + s * 0.82f, y + s * 0.62f, th, c);
        break;
    case EX_ICON_PROPERTIES:                                                    /* rows with a bullet each */
    case EX_ICON_FILE_PROPERTIES: {
        int k;
        for (k = 0; k < 3; k++) {
            float yy = y + s * (0.25f + 0.25f * (float)k);
            dot(r, x + s * 0.2f, yy, th * 0.9f, c);
            line(r, x + s * 0.36f, yy, x + s * (icon == EX_ICON_PROPERTIES ? 0.84f : 0.64f), yy, th, c);
        }
        if (icon == EX_ICON_FILE_PROPERTIES) ex_stroke_round_rect(r, x + s * 0.06f, y + s * 0.06f, s * 0.88f, s * 0.88f, th * 2, th, c);
        break;
    }
    case EX_ICON_OUTLINE: {                                                     /* lines, indented */
        static const float ind[4] = {0.14f, 0.3f, 0.3f, 0.14f};
        int k;
        for (k = 0; k < 4; k++) line(r, x + s * ind[k], y + s * (0.2f + 0.2f * (float)k), x + s * 0.86f, y + s * (0.2f + 0.2f * (float)k), th, c);
        break;
    }
    case EX_ICON_FOOTNOTES:                                                     /* a mark and a line under it */
        line(r, x + s * 0.2f, y + s * 0.3f, x + s * 0.5f, y + s * 0.3f, th, c);
        line(r, x + s * 0.2f, y + s * 0.5f, x + s * 0.8f, y + s * 0.5f, th, c);
        line(r, x + s * 0.2f, y + s * 0.7f, x + s * 0.5f, y + s * 0.7f, th, c);
        dot(r, x + s * 0.76f, y + s * 0.26f, th * 1.1f, c);
        break;
    case EX_ICON_NEW_NOTE: page(r, x, y, s, th, c); plus(r, x + s * 0.76f, y + s * 0.76f, s * 0.14f, th, c); break;
    case EX_ICON_NEW_FOLDER: folder(r, x, y, s, th, c); plus(r, x + s * 0.5f, y + s * 0.6f, s * 0.14f, th, c); break;
    case EX_ICON_SORT:                                                          /* up and down arrows */
        line(r, x + s * 0.32f, y + s * 0.82f, x + s * 0.32f, y + s * 0.2f, th, c);
        ex_fill_triangle(r, x + s * 0.32f, y + s * 0.12f, x + s * 0.16f, y + s * 0.34f, x + s * 0.48f, y + s * 0.34f, c);
        line(r, x + s * 0.68f, y + s * 0.18f, x + s * 0.68f, y + s * 0.8f, th, c);
        ex_fill_triangle(r, x + s * 0.68f, y + s * 0.88f, x + s * 0.52f, y + s * 0.66f, x + s * 0.84f, y + s * 0.66f, c);
        break;
    case EX_ICON_EXPAND:                                                        /* chevrons pointing away from each other */
        line(r, x + s * 0.25f, y + s * 0.4f, x + s * 0.5f, y + s * 0.15f, th, c); line(r, x + s * 0.5f, y + s * 0.15f, x + s * 0.75f, y + s * 0.4f, th, c);
        line(r, x + s * 0.25f, y + s * 0.6f, x + s * 0.5f, y + s * 0.85f, th, c); line(r, x + s * 0.5f, y + s * 0.85f, x + s * 0.75f, y + s * 0.6f, th, c);
        break;
    case EX_ICON_MORE: dot(r, x + s * 0.2f, y + s * 0.5f, th * 1.2f, c); dot(r, x + s * 0.5f, y + s * 0.5f, th * 1.2f, c); dot(r, x + s * 0.8f, y + s * 0.5f, th * 1.2f, c); break;
    case EX_ICON_PALETTE:                                                       /* a prompt: ">_" */
        line(r, x + s * 0.2f, y + s * 0.3f, x + s * 0.42f, y + s * 0.5f, th, c);
        line(r, x + s * 0.2f, y + s * 0.7f, x + s * 0.42f, y + s * 0.5f, th, c);
        line(r, x + s * 0.55f, y + s * 0.72f, x + s * 0.82f, y + s * 0.72f, th, c);
        break;
    case EX_ICON_GRAPH:                                                         /* three notes joined by links */
        line(r, x + s * 0.25f, y + s * 0.3f, x + s * 0.72f, y + s * 0.38f, th, c);
        line(r, x + s * 0.25f, y + s * 0.3f, x + s * 0.4f, y + s * 0.78f, th, c);
        line(r, x + s * 0.72f, y + s * 0.38f, x + s * 0.4f, y + s * 0.78f, th, c);
        dot(r, x + s * 0.25f, y + s * 0.3f, th * 1.8f, c); dot(r, x + s * 0.72f, y + s * 0.38f, th * 2.2f, c); dot(r, x + s * 0.4f, y + s * 0.78f, th * 1.6f, c);
        break;
    case EX_ICON_CANVAS:                                                        /* two cards and a connector */
        ex_stroke_round_rect(r, x + s * 0.1f, y + s * 0.15f, s * 0.38f, s * 0.3f, th, th, c);
        ex_stroke_round_rect(r, x + s * 0.52f, y + s * 0.55f, s * 0.38f, s * 0.3f, th, th, c);
        line(r, x + s * 0.3f, y + s * 0.45f, x + s * 0.3f, y + s * 0.7f, th, c);
        line(r, x + s * 0.3f, y + s * 0.7f, x + s * 0.52f, y + s * 0.7f, th, c);
        break;
    case EX_ICON_THEME: {                                                       /* a sun: light and dark */
        int k;
        dot(r, x + s * 0.5f, y + s * 0.5f, s * 0.2f, c);
        for (k = 0; k < 8; k++) {
            float a = (float)k * 0.785398f, ca = cosf(a), sa = sinf(a);
            line(r, x + s * (0.5f + 0.3f * ca), y + s * (0.5f + 0.3f * sa), x + s * (0.5f + 0.42f * ca), y + s * (0.5f + 0.42f * sa), th, c);
        }
        break;
    }
    case EX_ICON_SIDEBAR_LEFT: case EX_ICON_SIDEBAR_RIGHT: {                    /* a window with its side panel marked */
        int left = icon == EX_ICON_SIDEBAR_LEFT;
        ex_stroke_round_rect(r, x + s * 0.1f, y + s * 0.18f, s * 0.8f, s * 0.64f, th * 1.5f, th, c);
        ex_fill_rect(r, left ? x + s * 0.1f : x + s * 0.62f, y + s * 0.18f + th, s * 0.28f, s * 0.64f - 2 * th, c);
        break;
    }
    case EX_ICON_MODE:                                                          /* a pencil */
        line(r, x + s * 0.22f, y + s * 0.78f, x + s * 0.7f, y + s * 0.3f, th * 2.0f, c);
        ex_fill_triangle(r, x + s * 0.14f, y + s * 0.86f, x + s * 0.22f, y + s * 0.66f, x + s * 0.34f, y + s * 0.78f, c);
        line(r, x + s * 0.74f, y + s * 0.26f, x + s * 0.82f, y + s * 0.18f, th * 2.0f, c);
        break;
    default: break;
    }
}
