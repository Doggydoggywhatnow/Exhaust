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

int ex_mods_from_sdl(void) {
    SDL_Keymod m = SDL_GetModState();
    int mods = 0;
    if (m & SDL_KMOD_SHIFT) mods |= EX_MOD_SHIFT;
    if (m & SDL_KMOD_ALT) mods |= EX_MOD_ALT;
#ifdef __APPLE__
    if (m & SDL_KMOD_GUI) mods |= EX_MOD_PRIMARY;
#else
    if (m & SDL_KMOD_CTRL) mods |= EX_MOD_PRIMARY;
#endif
    return mods;
}

int ex_key_from_sdl(const SDL_Event *e, ExKey *key, int *mods) {
    ExKey k = EX_KEY_NONE;
    if (e->type != SDL_EVENT_KEY_DOWN) return 0;
    switch (e->key.key) {
    case SDLK_LEFT: k = EX_KEY_LEFT; break;
    case SDLK_RIGHT: k = EX_KEY_RIGHT; break;
    case SDLK_UP: k = EX_KEY_UP; break;
    case SDLK_DOWN: k = EX_KEY_DOWN; break;
    case SDLK_HOME: k = EX_KEY_HOME; break;
    case SDLK_END: k = EX_KEY_END; break;
    case SDLK_PAGEUP: k = EX_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: k = EX_KEY_PAGE_DOWN; break;
    case SDLK_BACKSPACE: k = EX_KEY_BACKSPACE; break;
    case SDLK_DELETE: k = EX_KEY_DELETE; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: k = EX_KEY_ENTER; break;
    case SDLK_TAB: k = EX_KEY_TAB; break;
    case SDLK_ESCAPE: k = EX_KEY_ESCAPE; break;
    case SDLK_A: k = EX_KEY_A; break;
    case SDLK_C: k = EX_KEY_C; break;
    case SDLK_V: k = EX_KEY_V; break;
    case SDLK_X: k = EX_KEY_X; break;
    case SDLK_Y: k = EX_KEY_Y; break;
    case SDLK_Z: k = EX_KEY_Z; break;
    default: break;
    }
    if (k == EX_KEY_NONE) return 0;
    /* plain letters are typed text, not commands: only pass them with the primary modifier */
    *mods = ex_mods_from_sdl();
    if (k >= EX_KEY_A && !(*mods & EX_MOD_PRIMARY)) return 0;
    *key = k;
    return 1;
}
