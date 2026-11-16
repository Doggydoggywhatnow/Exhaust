/* The operating system's screen reader interface for one window (sub-slice 7h): AT-SPI on Linux, UI Automation on Windows, NSAccessibility on
 * macOS, through AccessKit. One adapter belongs to one window; each window has its own accessibility tree.
 *
 * Threads. The operating system calls into AccessKit from its own threads: to ask for the tree when a screen reader starts (activation) and to
 * ask for an action (press, focus). Neither touches the widgets: the adapter keeps the latest snapshot of the tree behind a lock, and puts the
 * actions in a queue that the interface thread empties once a frame (ex_a11y_adapter_poll). The tree is built on the interface thread, only while a
 * screen reader is attached (ex_a11y_adapter_wants_update), and at most a few times a second, so a program nobody is reading costs nothing.
 *
 * Without a platform adapter (the dummy video driver, a build without AccessKit, no session bus) the adapter is inert: it never becomes active,
 * but the queue still works, so the same code path can be tested (ex_a11y_adapter_inject plays the part of the operating system). */
#ifndef EXHAUST_A11Y_ADAPTER_H
#define EXHAUST_A11Y_ADAPTER_H

#include <stdint.h>
#include "exhaust_a11y.h"

struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExA11yAdapter ExA11yAdapter;

/* The adapter for a window. Inert on the dummy video driver (unless EXHAUST_A11Y_ADAPTER is set, which a test of the real adapter does) and when the environment variable EXHAUST_NO_A11Y is set. On Windows the window must
 * not have been shown yet. Returns NULL only when out of memory. */
ExA11yAdapter *ex_a11y_adapter_create(struct SDL_Window *window);
/* An adapter with no platform part, for tests. */
ExA11yAdapter *ex_a11y_adapter_create_inert(void);
void ex_a11y_adapter_free(ExA11yAdapter *a);

int ex_a11y_adapter_active(const ExA11yAdapter *a);               /* a screen reader is attached */
ExA11yTree *ex_a11y_adapter_tree(ExA11yAdapter *a);               /* the tree to bring up to date (interface thread) */
/* Is it time to bring the tree up to date and call ex_a11y_adapter_commit? True while a screen reader is attached, at most every 250 ms (now_ms). */
int ex_a11y_adapter_wants_update(ExA11yAdapter *a, uint64_t now_ms);
/* Sends the tree to the operating system (and keeps a snapshot for the next activation). */
void ex_a11y_adapter_commit(ExA11yAdapter *a);
/* The window's place on the screen in pixels, for the screen reader's idea of where things are (X11). */
void ex_a11y_adapter_set_window_bounds(ExA11yAdapter *a, int x, int y, int w, int h);

typedef struct ExA11yActionReq { uint64_t id; unsigned action; } ExA11yActionReq;   /* EX_A11Y_ACTION_* */
/* The next action the operating system asked for (interface thread). Returns 0 when there is none. */
int ex_a11y_adapter_poll(ExA11yAdapter *a, ExA11yActionReq *out);
/* Queues an action as the operating system's thread would. */
void ex_a11y_adapter_inject(ExA11yAdapter *a, uint64_t id, unsigned action);

/* How many updates were sent, for tests. */
unsigned long ex_a11y_adapter_commits(const ExA11yAdapter *a);

#ifdef __cplusplus
}
#endif
#endif
