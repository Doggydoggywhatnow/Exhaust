#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_fontdb.h"
#include "exhaust_input.h"
#include "exhaust_platform.h"
#include "exhaust_render.h"

/* TEMPORARY (sub-slice 1a-i): the demo window needs two colors before theme
 * tokens exist. Sub-slice 1a-iv replaces these with tokens from the built-in
 * theme files and the no_color_literals test then forbids literals here. */
static const ExColor DEMO_BACKGROUND = {0x1e, 0x1f, 0x2d, 0xff};
static const ExColor DEMO_TEXT = {0xca, 0xd1, 0xf2, 0xff};

/* The system font scan runs on a worker thread; it hands the result to the UI
 * thread with a custom SDL event, so the UI never waits for the disk. */
static Uint32 g_fonts_ready_event;

static int font_scan_thread(void *unused) {
    SDL_Event e;
    ExFontDb *db = ex_fontdb_scan(NULL, 0, NULL, NULL);
    (void)unused;
    SDL_zero(e);
    e.type = g_fonts_ready_event;
    e.user.data1 = db;
    if (!SDL_PushEvent(&e)) ex_fontdb_free(db);
    return 0;
}

typedef struct Demo {
    char typed[4096];
    char composing[512];
} Demo;

static void draw(ExRenderer *r, const Demo *d) {
    ExFontLib *lib = ex_renderer_fonts(r);
    ExFont *ui = ex_font_builtin(lib, EX_FONT_UI_REGULAR);
    ExFont *bold = ex_font_builtin(lib, EX_FONT_UI_BOLD);
    ExFont *mono = ex_font_builtin(lib, EX_FONT_MONO_REGULAR);
    float s = ex_renderer_scale(r);
    char line[600];
    const char *hint = "Type, paste with Ctrl+V; Ctrl+Plus, Ctrl+Minus and Ctrl+0 change the zoom.";
    const char *code = "int main(void) { return 0; }";
    const char *sample = "English, \xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A (Arabic), \xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA (Hebrew), "
                         "\xE4\xBD\xA0\xE5\xA5\xBD (Chinese), \xF0\x9F\x98\x80";
    float w;

    snprintf(line, sizeof line, "Exhaust %s", exhaust_version());
    ex_renderer_clear(r, DEMO_BACKGROUND);
    ex_text_draw(r, bold, 24, 24 * s, 24 * s, line, strlen(line), DEMO_TEXT);
    ex_text_draw(r, ui, 16, 24 * s, 64 * s, hint, strlen(hint), DEMO_TEXT);
    ex_text_draw(r, mono, 14, 24 * s, 96 * s, code, strlen(code), DEMO_TEXT);
    ex_text_draw(r, ui, 16, 24 * s, 128 * s, sample, strlen(sample), DEMO_TEXT);
    w = ex_text_draw(r, ui, 18, 24 * s, 168 * s, d->typed, strlen(d->typed), DEMO_TEXT);
    ex_text_draw(r, ui, 18, 24 * s + w, 168 * s, d->composing, strlen(d->composing), DEMO_TEXT);
    ex_renderer_present(r);
}

int main(int argc, char **argv) {
    ExRenderer *r;
    SDL_Event e;
    Demo demo;
    float zoom = 1.0f;
    int running = 1;

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("Exhaust %s\n", exhaust_version());
        return 0;
    }
    memset(&demo, 0, sizeof demo);
    r = ex_renderer_create("Exhaust", 960, 600);
    if (!r) {
        fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error());
        return 1;
    }
    ex_renderer_set_display_scale(r, ex_renderer_detect_display_scale(r));
    g_fonts_ready_event = SDL_RegisterEvents(1);
    {
        SDL_Thread *t = SDL_CreateThread(font_scan_thread, "font scan", NULL);
        if (t) SDL_DetachThread(t);
    }
    ex_text_input_start(ex_renderer_window(r));
    draw(r, &demo);
    while (running && SDL_WaitEvent(&e)) { /* sleeps until something happens */
        ExTextEvent te;
        if (e.type == g_fonts_ready_event) {
            ex_renderer_set_font_db(r, (ExFontDb *)e.user.data1);
            draw(r, &demo);
            continue;
        }
        if (ex_text_event_decode(&e, &te)) {
            if (te.kind == EX_TEXT_COMMIT) {
                size_t n = strlen(demo.typed), m = strlen(te.text);
                if (n + m < sizeof demo.typed) memcpy(demo.typed + n, te.text, m + 1);
                demo.composing[0] = 0;
            } else {
                memcpy(demo.composing, te.text, strlen(te.text) + 1);
            }
            draw(r, &demo);
            continue;
        }
        switch (e.type) {
        case SDL_EVENT_QUIT: running = 0; break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            ex_renderer_set_display_scale(r, ex_renderer_detect_display_scale(r));
            draw(r, &demo);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: draw(r, &demo); break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.mod & SDL_KMOD_CTRL) {
                if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS) zoom *= 1.1f;
                else if (e.key.key == SDLK_MINUS) zoom /= 1.1f;
                else if (e.key.key == SDLK_0) zoom = 1.0f;
                else if (e.key.key == SDLK_V) {
                    char *t = ex_clipboard_get_text();
                    if (t) {
                        size_t n = strlen(demo.typed), m = strlen(t);
                        if (n + m < sizeof demo.typed) memcpy(demo.typed + n, t, m + 1);
                        free(t);
                    }
                    draw(r, &demo);
                    break;
                } else break;
                ex_renderer_set_zoom(r, zoom);
                draw(r, &demo);
            } else if (e.key.key == SDLK_ESCAPE) {
                running = 0;
            } else if (e.key.key == SDLK_BACKSPACE) {
                size_t n = strlen(demo.typed);
                while (n > 0 && ((unsigned char)demo.typed[n - 1] & 0xC0) == 0x80) n--;
                if (n > 0) n--;
                demo.typed[n] = 0;
                draw(r, &demo);
            }
            break;
        default: break;
        }
    }
    ex_renderer_destroy(r);
    return 0;
}
