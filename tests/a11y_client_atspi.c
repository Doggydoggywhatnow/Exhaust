/* A screen reader's side of the Linux accessibility interface (AT-SPI), for the CI job that runs Exhaust with the real adapter (sub-slice 7h): it finds
 * the application on the accessibility bus, its window and its root, the widget that has the keyboard focus (by name), presses the Settings button
 * through the action interface, and sees the settings page appear. Exits 0 when all of that worked, else 1 with what was missing on stderr.
 * Build: cc a11y_client_atspi.c $(pkg-config --cflags --libs atspi-2) */
#include <atspi/atspi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define MAXDEPTH 40

static int contains_ci(const char *s, const char *what) { return s && strcasestr(s, what) != NULL; }

/* The first descendant (depth first) with this name and role; role -1: any. */
static AtspiAccessible *find_named(AtspiAccessible *a, const char *name, int role, int depth) {
    gint n, i;
    gchar *nm;
    if (!a || depth > MAXDEPTH) return NULL;
    nm = atspi_accessible_get_name(a, NULL);
    if (nm && !strcmp(nm, name) && (role < 0 || (int)atspi_accessible_get_role(a, NULL) == role)) { g_free(nm); return g_object_ref(a); }
    g_free(nm);
    n = atspi_accessible_get_child_count(a, NULL);
    for (i = 0; i < n; i++) {
        AtspiAccessible *c = atspi_accessible_get_child_at_index(a, i, NULL), *f;
        if (!c) continue;
        f = find_named(c, name, role, depth + 1);
        g_object_unref(c);
        if (f) return f;
    }
    return NULL;
}

/* A descendant that has the keyboard focus. */
static AtspiAccessible *find_focused(AtspiAccessible *a, int depth) {
    AtspiStateSet *st;
    gint n, i;
    if (!a || depth > MAXDEPTH) return NULL;
    st = atspi_accessible_get_state_set(a);
    if (st) {
        gboolean f = atspi_state_set_contains(st, ATSPI_STATE_FOCUSED);
        g_object_unref(st);
        if (f) return g_object_ref(a);
    }
    n = atspi_accessible_get_child_count(a, NULL);
    for (i = 0; i < n; i++) {
        AtspiAccessible *c = atspi_accessible_get_child_at_index(a, i, NULL), *f;
        if (!c) continue;
        f = find_focused(c, depth + 1);
        g_object_unref(c);
        if (f) return f;
    }
    return NULL;
}

static AtspiAccessible *find_app(void) {
    AtspiAccessible *desktop = atspi_get_desktop(0);
    gint n = atspi_accessible_get_child_count(desktop, NULL), i;
    for (i = 0; i < n; i++) {
        AtspiAccessible *app = atspi_accessible_get_child_at_index(desktop, i, NULL);
        gchar *nm = app ? atspi_accessible_get_name(app, NULL) : NULL;
        int is = contains_ci(nm, "exhaust");
        g_free(nm);
        if (is) return app;
        if (app) g_object_unref(app);
    }
    return NULL;
}

int main(int argc, char **argv) {
    AtspiAccessible *app = NULL, *win = NULL, *focused, *button, *page;
    int tries, failed = 0;
    gchar *nm;
    const char *press = argc > 1 ? argv[1] : "Settings", *after = argc > 2 ? argv[2] : "Appearance";
    if (atspi_init() > 1) { fprintf(stderr, "no accessibility bus\n"); return 1; }
    for (tries = 0; tries < 60 && !app; tries++) { app = find_app(); if (!app) sleep(1); }
    if (!app) { fprintf(stderr, "the application never appeared on the accessibility bus\n"); return 1; }
    printf("found the application\n");
    /* the window and its root */
    for (tries = 0; tries < 20 && !win; tries++) {
        gint n = atspi_accessible_get_child_count(app, NULL), i;
        for (i = 0; i < n && !win; i++) {
            AtspiAccessible *c = atspi_accessible_get_child_at_index(app, i, NULL);
            if (c && atspi_accessible_get_role(c, NULL) == ATSPI_ROLE_WINDOW) win = c; else if (c) g_object_unref(c);
        }
        if (!win) sleep(1);
    }
    if (!win) { fprintf(stderr, "the application has no window\n"); return 1; }
    nm = atspi_accessible_get_name(win, NULL);
    printf("found the window \"%s\"\n", nm ? nm : "");
    g_free(nm);
    /* the widget that has the focus, by name */
    focused = find_focused(win, 0);
    if (!focused) { fprintf(stderr, "no widget has the keyboard focus\n"); failed = 1; }
    else {
        nm = atspi_accessible_get_name(focused, NULL);
        printf("the focused widget is \"%s\"\n", nm ? nm : "");
        if (!nm || !*nm) { fprintf(stderr, "the focused widget has no name\n"); failed = 1; }
        g_free(nm);
        g_object_unref(focused);
    }
    /* a press reaches a button */
    button = find_named(win, press, ATSPI_ROLE_PUSH_BUTTON, 0);
    if (!button) { fprintf(stderr, "no button named \"%s\"\n", press); failed = 1; }
    else {
        AtspiAction *act = atspi_accessible_get_action_iface(button);
        if (!act || atspi_action_get_n_actions(act, NULL) < 1) { fprintf(stderr, "the button has no action\n"); failed = 1; }
        else if (!atspi_action_do_action(act, 0, NULL)) { fprintf(stderr, "the action failed\n"); failed = 1; }
        else printf("pressed \"%s\"\n", press);
        if (act) g_object_unref(act);
        g_object_unref(button);
        for (tries = 0, page = NULL; tries < 20 && !page; tries++) { page = find_named(win, after, -1, 0); if (!page) sleep(1); }
        if (!page) { fprintf(stderr, "after the press there is no \"%s\"\n", after); failed = 1; }
        else { printf("the press worked: \"%s\" is there\n", after); g_object_unref(page); }
    }
    g_object_unref(win); g_object_unref(app);
    return failed;
}
