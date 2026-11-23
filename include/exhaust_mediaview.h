/* The media tab (6c-iii): a video, or an audio file the built-in decoders do not take, played by libmpv (include/exhaust_mpv.h). The widget draws the
 * latest frame the player's thread has finished, as a texture, fitted into the pane with its shape kept, above a bar with a play and pause button, the
 * position and the volume. The player is the application's (one at a time, opened on the service thread); this widget asks for it with the `load`
 * handler and shows what it is given. Without libmpv it says so and offers "Open externally".
 * Mouse: the button, a press or drag on the position bar and on the volume bar, a double click on the picture toggles play. Keys: Space, Left and Right
 * (5 seconds), Up and Down (volume), Home. */
#ifndef EXHAUST_MEDIAVIEW_H
#define EXHAUST_MEDIAVIEW_H
#include "exhaust_mpv.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExMediaViewHandlers {
    void *user;
    void (*load)(void *user, const char *path);              /* asks for the file to be opened by the player */
    ExMpv *(*current)(void *user, const char *path);         /* the application's player if it holds this path, else NULL */
    void (*open_external)(void *user, const char *path);
} ExMediaViewHandlers;

ExWidget *ex_widget_mediaview(ExUi *ui, const char *path, const ExMediaViewHandlers *h);
void ex_mediaview_draw(ExUi *ui, ExWidget *w);
/* The application's answer to `load` (error NULL: the player is ready). */
void ex_mediaview_loaded(ExWidget *w, const char *error);
const char *ex_mediaview_path(const ExWidget *w);

typedef struct ExMediaViewStats {
    int loading, failed, attached, playing, has_picture;
    unsigned loads_asked;
    unsigned long frames_uploaded;
    float btn_x, btn_y, bar_x, bar_y, bar_w, vol_x, vol_y, vol_w, ext_x, ext_y;
    double own_ms;
} ExMediaViewStats;
void ex_mediaview_stats(const ExWidget *w, ExMediaViewStats *out);

#ifdef __cplusplus
}
#endif
#endif
