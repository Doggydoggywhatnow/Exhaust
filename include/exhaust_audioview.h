/* The audio tab (6c-ii): the controls of the player for one audio file. The sound itself is the player's (include/exhaust_audio.h); this widget only
 * shows the file's name and format, a play and pause button, a position bar with the times, and a volume bar.
 *
 * The player plays one file at a time. A tab shows the controls live while the player holds its file; otherwise the first press of Play (or opening the
 * tab) asks the application to load it (the `load` handler: a service-thread job reads the file and opens the decoder), and the answer attaches it to
 * the player. A file that cannot be played says why; for the formats the built-in decoders do not take, the tab offers "Open externally".
 * Mouse: the button, a press or drag on the position bar and on the volume bar. Keys: Space plays and pauses, Left and Right move 5 seconds, Up and Down
 * change the volume, Home goes to the start. */
#ifndef EXHAUST_AUDIOVIEW_H
#define EXHAUST_AUDIOVIEW_H
#include "exhaust_audio.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExAudioViewHandlers {
    void *user;
    void (*load)(void *user, const char *path);              /* asks for the file to be read and attached to the player */
    void (*open_external)(void *user, const char *path);     /* the file in the system's own program */
} ExAudioViewHandlers;

ExWidget *ex_widget_audioview(ExUi *ui, ExPlayer *player, const char *path, const ExAudioViewHandlers *h);
void ex_audioview_draw(ExUi *ui, ExWidget *w);
/* The application's answer to `load`: the player now holds the file (error NULL), or the file could not be played. */
void ex_audioview_loaded(ExWidget *w, const char *error);

typedef struct ExAudioViewStats {
    int loading, failed, attached;       /* attached: the player holds this tab's file */
    int playing;
    double position, duration;
    unsigned loads_asked;
    double own_ms;
    float btn_x, btn_y, bar_x, bar_y, bar_w, vol_x, vol_y, vol_w, ext_x, ext_y;     /* where the controls are in the window (for the tests and the screen reader) */
} ExAudioViewStats;
void ex_audioview_stats(const ExWidget *w, ExAudioViewStats *out);
/* The path the tab plays. */
const char *ex_audioview_path(const ExWidget *w);

#ifdef __cplusplus
}
#endif
#endif
