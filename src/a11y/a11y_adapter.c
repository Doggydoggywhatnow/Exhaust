/* The adapter between the accessibility tree and the operating system: see include/exhaust_a11y_adapter.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_a11y_adapter.h"

#ifdef EX_HAVE_A11Y
#include "accesskit.h"
#if defined(__linux__)
#define EX_ADAPTER_UNIX 1
#elif defined(_WIN32)
#define EX_ADAPTER_WIN 1
#elif defined(__APPLE__)
#define EX_ADAPTER_MAC 1
#endif
#endif

#define QUEUE_MAX 64

struct ExA11yAdapter {
    ExA11yTree *tree;
    SDL_Mutex *mu;
    ExA11ySnapshot *snapshot;           /* the latest tree sent; read by the activation callback on the operating system's thread */
    ExA11yActionReq queue[QUEUE_MAX]; size_t nq;
    SDL_AtomicInt active;
    uint64_t last_update_ms;
    unsigned long commits;
    int platform;
    int focus_known, focused;           /* the window focus last told to the operating system (Linux only: Windows and macOS see the window's own focus) */
#if defined(EX_ADAPTER_UNIX)
    struct accesskit_unix_adapter *ak;
#elif defined(EX_ADAPTER_WIN)
    struct accesskit_windows_subclassing_adapter *ak;
#elif defined(EX_ADAPTER_MAC)
    struct accesskit_macos_subclassing_adapter *ak;
#endif
};

static void push_action(ExA11yAdapter *a, uint64_t id, unsigned action) {
    SDL_LockMutex(a->mu);
    if (a->nq < QUEUE_MAX) { a->queue[a->nq].id = id; a->queue[a->nq].action = action; a->nq++; }
    SDL_UnlockMutex(a->mu);
}

void ex_a11y_adapter_inject(ExA11yAdapter *a, uint64_t id, unsigned action) { if (a) push_action(a, id, action); }

int ex_a11y_adapter_poll(ExA11yAdapter *a, ExA11yActionReq *out) {
    int got = 0;
    if (!a) return 0;
    SDL_LockMutex(a->mu);
    if (a->nq) { *out = a->queue[0]; memmove(a->queue, a->queue + 1, (a->nq - 1) * sizeof *a->queue); a->nq--; got = 1; }
    SDL_UnlockMutex(a->mu);
    return got;
}

ExA11yTree *ex_a11y_adapter_tree(ExA11yAdapter *a) { return a ? a->tree : NULL; }
int ex_a11y_adapter_active(const ExA11yAdapter *a) { return a && SDL_GetAtomicInt((SDL_AtomicInt *)&a->active) != 0; }
unsigned long ex_a11y_adapter_commits(const ExA11yAdapter *a) { return a ? a->commits : 0; }

int ex_a11y_adapter_wants_update(ExA11yAdapter *a, uint64_t now_ms) {
    if (!ex_a11y_adapter_active(a)) return 0;
    if (a->last_update_ms && now_ms - a->last_update_ms < 250) return 0;
    a->last_update_ms = now_ms ? now_ms : 1;
    return 1;
}

#if defined(EX_HAVE_A11Y) && (defined(EX_ADAPTER_UNIX) || defined(EX_ADAPTER_WIN) || defined(EX_ADAPTER_MAC))
/* A tree update of the latest snapshot; if none was made yet, a window with nothing in it. */
static struct accesskit_tree_update *update_from_snapshot(ExA11yAdapter *a) {
    struct accesskit_tree_update *u = NULL;
    SDL_LockMutex(a->mu);
    if (a->snapshot) u = ex_a11y_build_update(a->snapshot);
    SDL_UnlockMutex(a->mu);
    if (!u) {
        struct accesskit_node *root = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
        struct accesskit_tree_info *info;
        u = accesskit_tree_update_with_capacity_and_focus(1, 1);
        if (root) accesskit_node_set_label(root, "Exhaust");
        if (u && root) accesskit_tree_update_push_node(u, 1, root);
        info = accesskit_tree_info_new(1);
        if (u && info) accesskit_tree_update_set_tree_info(u, info);
    }
    return u;
}

static struct accesskit_tree_update *on_activate(void *user) {
    ExA11yAdapter *a = user;
    SDL_SetAtomicInt(&a->active, 1);                  /* the interface thread starts building the tree from now on */
    return update_from_snapshot(a);
}

static void on_deactivate(void *user) { SDL_SetAtomicInt(&((ExA11yAdapter *)user)->active, 0); }

static void on_action(struct accesskit_action_request *req, void *user) {
    ExA11yAdapter *a = user;
    if (req->action == ACCESSKIT_ACTION_CLICK || req->action == ACCESSKIT_ACTION_FOCUS)
        push_action(a, (uint64_t)req->target_node, req->action == ACCESSKIT_ACTION_CLICK ? EX_A11Y_ACTION_CLICK : EX_A11Y_ACTION_FOCUS);
    accesskit_action_request_free(req);
}

static struct accesskit_tree_update *update_factory(void *user) { return update_from_snapshot(user); }
#endif

static ExA11yAdapter *make(void) {
    ExA11yAdapter *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->tree = ex_a11y_create();
    a->mu = SDL_CreateMutex();
    if (!a->tree || !a->mu) { ex_a11y_adapter_free(a); return NULL; }
    return a;
}

ExA11yAdapter *ex_a11y_adapter_create_inert(void) { return make(); }

ExA11yAdapter *ex_a11y_adapter_create(struct SDL_Window *window) {
    ExA11yAdapter *a;
    const char *driver = SDL_GetCurrentVideoDriver();
    (void)window;
    a = make();
    if (!a) return NULL;
    if ((driver && !strcmp(driver, "dummy") && !getenv("EXHAUST_A11Y_ADAPTER")) || getenv("EXHAUST_NO_A11Y")) return a;   /* headless: no operating system to talk to (unless a test asks for the real adapter) */
#if defined(EX_ADAPTER_UNIX)
    a->ak = accesskit_unix_adapter_new(on_activate, a, on_action, a, on_deactivate, a);
    a->platform = a->ak != NULL;
#elif defined(EX_ADAPTER_WIN)
    {
        void *hwnd = window ? SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
        if (hwnd) a->ak = accesskit_windows_subclassing_adapter_new((HWND)hwnd, on_activate, a, on_action, a);
        a->platform = a->ak != NULL;
    }
#elif defined(EX_ADAPTER_MAC)
    {
        void *nswin = window ? SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL) : NULL;
        if (nswin) a->ak = accesskit_macos_subclassing_adapter_for_window(nswin, on_activate, a, on_action, a);
        a->platform = a->ak != NULL;
    }
#endif
    return a;
}

void ex_a11y_adapter_free(ExA11yAdapter *a) {
    if (!a) return;
#if defined(EX_ADAPTER_UNIX)
    if (a->ak) accesskit_unix_adapter_free(a->ak);
#elif defined(EX_ADAPTER_WIN)
    if (a->ak) accesskit_windows_subclassing_adapter_free(a->ak);
#elif defined(EX_ADAPTER_MAC)
    if (a->ak) accesskit_macos_subclassing_adapter_free(a->ak);
#endif
    if (a->snapshot) ex_a11y_snapshot_unref(a->snapshot);
    ex_a11y_free(a->tree);
    if (a->mu) SDL_DestroyMutex(a->mu);
    free(a);
}

void ex_a11y_adapter_commit(ExA11yAdapter *a) {
    ExA11ySnapshot *s;
    if (!a) return;
    s = ex_a11y_snapshot(a->tree);
    if (!s) return;
    SDL_LockMutex(a->mu);
    if (a->snapshot) ex_a11y_snapshot_unref(a->snapshot);
    a->snapshot = s;
    SDL_UnlockMutex(a->mu);
    a->commits++;
#if defined(EX_ADAPTER_UNIX)
    if (a->ak) accesskit_unix_adapter_update_if_active(a->ak, update_factory, a);
#elif defined(EX_ADAPTER_WIN)
    if (a->ak) { struct accesskit_windows_queued_events *ev = accesskit_windows_subclassing_adapter_update_if_active(a->ak, update_factory, a); if (ev) accesskit_windows_queued_events_raise(ev); }
#elif defined(EX_ADAPTER_MAC)
    if (a->ak) { struct accesskit_macos_queued_events *ev = accesskit_macos_subclassing_adapter_update_if_active(a->ak, update_factory, a); if (ev) accesskit_macos_queued_events_raise(ev); }
#endif
}

void ex_a11y_adapter_set_focus(ExA11yAdapter *a, int focused) {
    focused = focused != 0;
    if (!a || (a->focus_known && a->focused == focused)) return;
    a->focus_known = 1; a->focused = focused;
#if defined(EX_ADAPTER_UNIX)
    if (a->ak) accesskit_unix_adapter_update_window_focus_state(a->ak, focused);
#endif
}

void ex_a11y_adapter_set_window_bounds(ExA11yAdapter *a, int x, int y, int w, int h) {
#if defined(EX_ADAPTER_UNIX)
    if (a && a->ak) {
        struct accesskit_rect r = accesskit_rect_new((double)x, (double)y, (double)(x + w), (double)(y + h));
        accesskit_unix_adapter_set_root_window_bounds(a->ak, r, r);
    }
#else
    (void)a; (void)x; (void)y; (void)w; (void)h;
#endif
}
