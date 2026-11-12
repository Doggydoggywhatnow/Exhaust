#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_render.h"

/* TEMPORARY (sub-slice 1a-i): the demo window needs two colors before theme
 * tokens exist. Sub-slice 1a-iv replaces these with tokens from the built-in
 * theme files and the no_color_literals test then forbids literals here. */
static const ExColor DEMO_BACKGROUND = {0x1e, 0x1f, 0x2d, 0xff};
static const ExColor DEMO_TEXT = {0xca, 0xd1, 0xf2, 0xff};

static void draw(ExRenderer *r) {
    ExFontLib *lib = ex_renderer_fonts(r);
    ExFont *ui = ex_font_builtin(lib, EX_FONT_UI_REGULAR);
    ExFont *bold = ex_font_builtin(lib, EX_FONT_UI_BOLD);
    ExFont *mono = ex_font_builtin(lib, EX_FONT_MONO_REGULAR);
    float s = ex_renderer_scale(r);
    char line[128];
    const char *hint = "Ctrl+Plus, Ctrl+Minus and Ctrl+0 change the interface zoom.";
    const char *code = "int main(void) { return 0; }";

    snprintf(line, sizeof line, "Exhaust %s", exhaust_version());
    ex_renderer_clear(r, DEMO_BACKGROUND);
    ex_text_draw(r, bold, 24, 24 * s, 24 * s, line, strlen(line), DEMO_TEXT);
    ex_text_draw(r, ui, 16, 24 * s, 64 * s, hint, strlen(hint), DEMO_TEXT);
    ex_text_draw(r, mono, 14, 24 * s, 96 * s, code, strlen(code), DEMO_TEXT);
    ex_renderer_present(r);
}

int main(int argc, char **argv) {
    ExRenderer *r;
    SDL_Event e;
    float zoom = 1.0f;
    int running = 1;

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("Exhaust %s\n", exhaust_version());
        return 0;
    }
    r = ex_renderer_create("Exhaust", 960, 600);
    if (!r) {
        fprintf(stderr, "exhaust: cannot open a window: %s\n", ex_render_error());
        return 1;
    }
    ex_renderer_set_display_scale(r, ex_renderer_detect_display_scale(r));
    draw(r);
    while (running && SDL_WaitEvent(&e)) { /* sleeps until something happens */
        switch (e.type) {
        case SDL_EVENT_QUIT: running = 0; break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            ex_renderer_set_display_scale(r, ex_renderer_detect_display_scale(r));
            draw(r);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: draw(r); break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.mod & SDL_KMOD_CTRL) {
                if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS) zoom *= 1.1f;
                else if (e.key.key == SDLK_MINUS) zoom /= 1.1f;
                else if (e.key.key == SDLK_0) zoom = 1.0f;
                else break;
                ex_renderer_set_zoom(r, zoom);
                draw(r);
            } else if (e.key.key == SDLK_ESCAPE) {
                running = 0;
            }
            break;
        default: break;
        }
    }
    ex_renderer_destroy(r);
    return 0;
}
