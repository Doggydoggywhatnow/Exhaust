#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_input.h"

void ex_text_input_start(SDL_Window *window) { SDL_StartTextInput(window); }
void ex_text_input_stop(SDL_Window *window) { SDL_StopTextInput(window); }

void ex_text_input_set_area(SDL_Window *window, int x, int y, int w, int h) {
    SDL_Rect r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    SDL_SetTextInputArea(window, &r, 0);
}

/* Copies text, cutting it before a UTF-8 continuation byte if it must be shortened. */
static void copy_text(char *dst, size_t cap, const char *src) {
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(dst, src, n);
    dst[n] = 0;
}

int ex_text_event_decode(const SDL_Event *e, ExTextEvent *out) {
    memset(out, 0, sizeof *out);
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        out->kind = EX_TEXT_COMMIT;
        copy_text(out->text, sizeof out->text, e->text.text);
        return 1;
    }
    if (e->type == SDL_EVENT_TEXT_EDITING) {
        out->kind = EX_TEXT_COMPOSING;
        copy_text(out->text, sizeof out->text, e->edit.text);
        out->cursor_start = e->edit.start;
        out->cursor_length = e->edit.length;
        return 1;
    }
    return 0;
}
