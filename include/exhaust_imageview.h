/* The image tab (6c-i): a picture shown fitted to the pane or at a zoom, panned with the mouse. The pixels come from the picture cache
 * (include/exhaust_imagecache.h); nothing here reads or decodes.
 *
 * Mouse: the wheel zooms about the pointer; a press and drag pans; a double click toggles between fitted and the picture's own size (one picture
 * pixel to one screen pixel). Keyboard: + and - zoom, 0 fits, 1 shows the own size, the arrows pan, Home fits. A picture with transparency is drawn over
 * a checkerboard. An SVG is drawn again at the size it is shown (in steps of a power of two), so it stays sharp when zoomed. */
#ifndef EXHAUST_IMAGEVIEW_H
#define EXHAUST_IMAGEVIEW_H
#include "exhaust_imagecache.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

ExWidget *ex_widget_imageview(ExUi *ui, ExImageCache *cache, const char *path);
void ex_imageview_draw(ExUi *ui, ExWidget *w);
void ex_imageview_wheel(ExWidget *w, float dy, float x, float y);

typedef struct ExImageViewStats {
    int state;                 /* an ExImageState */
    int w, h, src_w, src_h;    /* the texture's size and the file's */
    float scale;               /* screen pixels per picture pixel, as drawn (of the file's size) */
    int fitted;
    int has_alpha;
    double own_ms;
} ExImageViewStats;
void ex_imageview_stats(const ExWidget *w, ExImageViewStats *out);

#ifdef __cplusplus
}
#endif
#endif
