/* A pop-out window (sub-slice 7g): an operating-system window of its own for one of the windows of the workspace model. It has its own renderer, its
 * own ExUi and its own panes (a strip of tabs and the panes of that workspace window); it shares everything else with the main window: the
 * workspace, the notes and their buffers, the settings and the theme. The application keeps the list of these, hands every event of the window to
 * its ExUi and draws it with ex_popwin_frame. See "Pop-out windows" in docs/formats.md. */
#ifndef EXHAUST_POPWIN_H
#define EXHAUST_POPWIN_H

#include <stdint.h>
#include "exhaust_fontdb.h"
#include "exhaust_panes.h"
#include "exhaust_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExPopWin ExPopWin;

/* Opens the window for workspace window `window`: x, y, w, h are its saved place and size in screen units (w and h 0: the default size; x, y both
 * INT32_MIN: where the system puts it). A place that is on no screen any more is moved onto the one the pointer is on (or the first). The theme and
 * the font database are not copied: the window reads them while it is open. */
ExPopWin *ex_popwin_create(ExWorkspace *ws, ExId window, const ExTheme *theme, float base_size, float zoom, const ExPanesHooks *hooks, void *hooks_user,
                           int x, int y, int w, int h, const char *title);
void ex_popwin_free(ExPopWin *pw);

ExId ex_popwin_window(const ExPopWin *pw);                /* the workspace window */
uint32_t ex_popwin_sdl_id(const ExPopWin *pw);            /* SDL_GetWindowID of the window, to find it from an event */
ExRenderer *ex_popwin_renderer(ExPopWin *pw);
ExUi *ex_popwin_ui(ExPopWin *pw);
ExPanes *ex_popwin_panes(ExPopWin *pw);
ExWidget *ex_popwin_root(ExPopWin *pw);

/* Shares the font database of the main window (not owned; it must outlive the window). */
void ex_popwin_set_font_db(ExPopWin *pw, ExFontDb *db);
/* The window's place and size in screen units, as the system has them now. */
void ex_popwin_get_rect(const ExPopWin *pw, int *x, int *y, int *w, int *h);
/* Updates the panes, lays out, draws and presents one frame. `blink`: the caret is shown. Returns the milliseconds the program's own work took (without the wait for the screen). */
double ex_popwin_frame(ExPopWin *pw, int blink);

#ifdef __cplusplus
}
#endif
#endif
