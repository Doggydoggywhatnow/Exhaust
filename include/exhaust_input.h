/* Text input (including input method editors, IME) on top of SDL3's text input
 * events. Windowing code feeds SDL events to ex_text_event_decode(); everything
 * after that is plain data. IME and the clipboard need a real display and
 * keyboard, so they are on the maintainer checklist rather than in CI. */
#ifndef EXHAUST_INPUT_H
#define EXHAUST_INPUT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;
union SDL_Event;

typedef enum ExTextEventKind {
    EX_TEXT_NONE = 0,
    EX_TEXT_COMMIT,    /* text to insert at the cursor */
    EX_TEXT_COMPOSING  /* IME composition in progress; empty text means it was cancelled */
} ExTextEventKind;

typedef struct ExTextEvent {
    ExTextEventKind kind;
    char text[512];     /* UTF-8, NUL terminated, cut at a character boundary if too long */
    int cursor_start;   /* for COMPOSING: selected part inside the composition, in characters */
    int cursor_length;
} ExTextEvent;

/* Starts or stops text input for a window (shows or hides the on-screen keyboard
 * and enables IME). The area is the text cursor's rectangle in window
 * coordinates, so the IME can place its candidate list next to it. */
void ex_text_input_start(struct SDL_Window *window);
void ex_text_input_stop(struct SDL_Window *window);
void ex_text_input_set_area(struct SDL_Window *window, int x, int y, int w, int h);

/* Turns an SDL text input or text editing event into an ExTextEvent. Returns 1
 * if the event was one, else 0. */
int ex_text_event_decode(const union SDL_Event *event, ExTextEvent *out);

#ifdef __cplusplus
}
#endif
#endif
