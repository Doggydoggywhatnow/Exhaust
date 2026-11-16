/* Saved workspace layouts (sub-slice 7e): the panes of the main window kept under a name in `<vault>/.exhaust/workspaces.ini`, and put back
 * later. The format is in "workspaces.ini" in docs/formats.md. The file is read once when this is created; every change is written, atomically,
 * by a worker thread, so nothing here touches the disk on the interface thread after that. A layout never holds text: loading one changes only
 * which tabs are open and where, never a note. */
#ifndef EXHAUST_LAYOUTS_H
#define EXHAUST_LAYOUTS_H

#include <stddef.h>
#include <stdint.h>
#include "exhaust_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExLayouts ExLayouts;

/* path: the file (`.../workspaces.ini`; it may not exist yet). NULL path: kept in memory only. */
ExLayouts *ex_layouts_open(const char *path);
void ex_layouts_close(ExLayouts *l);                          /* waits for the last write */
void ex_layouts_flush(ExLayouts *l);                          /* waits until every change is on disk (for tests) */

/* A valid name: 1 to 60 bytes of UTF-8, no control character, none of [ ] /, not starting or ending with a space. */
int ex_layouts_name_valid(const char *name);

size_t ex_layouts_count(const ExLayouts *l);
const char *ex_layouts_name(const ExLayouts *l, size_t i);    /* in order of the name, ignoring the case of ASCII letters; NULL out of range */
/* The saved name that is the same as `name` (ASCII case ignored), or NULL. */
const char *ex_layouts_find(const ExLayouts *l, const char *name);
int64_t ex_layouts_saved_time(const ExLayouts *l, const char *name);   /* Unix seconds, 0 if unknown */

/* Saves the main window's panes and the sidebars of ws under `name` (replacing a layout of the same name, which keeps the spelling it had).
 * Returns 0, or -1 for a name that is not valid. */
int ex_layouts_save(ExLayouts *l, const char *name, const ExWorkspace *ws, int64_t now);
int ex_layouts_delete(ExLayouts *l, const char *name);        /* 0, or -1 if there is no such layout */

/* Puts a layout in ws: its panes and sidebars replace the present ones, the window's place, the graph's options and the search text stay.
 * Tabs whose note does not exist (exists(user, path) returns 0) are dropped; pop-out windows are not closed here. Returns 0, or -1 if there
 * is no such layout or it cannot be read (ws is then unchanged). */
int ex_layouts_load(const ExLayouts *l, const char *name, ExWorkspace *ws, int (*exists)(void *user, const char *path), void *user);

#ifdef __cplusplus
}
#endif
#endif
