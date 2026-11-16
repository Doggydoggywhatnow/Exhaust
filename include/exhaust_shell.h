/* The shell of the window (sub-slice 7f): the rules that are not drawing. Zoom levels, the size of a new window, which buttons the ribbon shows,
 * and whether high contrast is on. Plain functions on values, so they can be tested without a window. See "Shell: status bar, ribbon, zoom, high
 * contrast and window size (7f)" in docs/formats.md. */
#ifndef EXHAUST_SHELL_H
#define EXHAUST_SHELL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- zoom ---- */
int ex_zoom_clamp(long percent);                  /* 50 to 400 */
int ex_zoom_step(long percent, int dir);          /* dir > 0: the next level above; dir < 0: the next level below; the limits stay; 0: 100 */
size_t ex_zoom_level_count(void);
int ex_zoom_level(size_t i);

/* ---- the size of a window ---- */
int ex_window_size_clamp(long px);                /* 400 to 10000 */

/* ---- the ribbon ---- */
typedef struct ExRibbonItem {
    const char *id;           /* the command id */
    const char *name;         /* what the button is called (and its accessible name) */
    int top;                  /* 1: in the top group, 0: in the bottom group */
} ExRibbonItem;
size_t ex_ribbon_item_count(void);
const ExRibbonItem *ex_ribbon_item(size_t i);     /* in the order of the bar: the top group, then the bottom group */
/* Is the button of command `id` shown with the `ribbon_items` value `list`? An empty list shows all, `none` shows none, else only those listed. */
int ex_ribbon_shown(const char *list, const char *id);
/* The value of `ribbon_items` after the button of `id` is shown (show != 0) or hidden: ids this version does not know stay; all shown is "" and
 * all hidden is "none". Returns a malloc'ed string. */
char *ex_ribbon_set(const char *list, const char *id, int show);

/* ---- high contrast ---- */
/* `setting` is off, on or system; system_on says whether the operating system asks for it. */
int ex_high_contrast_resolve(const char *setting, int system_on);

#ifdef __cplusplus
}
#endif
#endif
