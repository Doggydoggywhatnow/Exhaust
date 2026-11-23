/* The rules of the shell: see include/exhaust_shell.h. */
#include <stdlib.h>
#include <string.h>

#include "exhaust_shell.h"

static const int k_levels[] = { 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400 };
#define NLEVELS (sizeof k_levels / sizeof k_levels[0])

int ex_zoom_clamp(long percent) { return percent < 50 ? 50 : percent > 400 ? 400 : (int)percent; }

int ex_zoom_step(long percent, int dir) {
    size_t i;
    percent = ex_zoom_clamp(percent);
    if (dir == 0) return 100;
    if (dir > 0) { for (i = 0; i < NLEVELS; i++) if (k_levels[i] > percent) return k_levels[i]; return k_levels[NLEVELS - 1]; }
    for (i = NLEVELS; i-- > 0;) if (k_levels[i] < percent) return k_levels[i];
    return k_levels[0];
}

size_t ex_zoom_level_count(void) { return NLEVELS; }
int ex_zoom_level(size_t i) { return i < NLEVELS ? k_levels[i] : 0; }

int ex_window_size_clamp(long px) { return px < 400 ? 400 : px > 10000 ? 10000 : (int)px; }

static const ExRibbonItem k_items[] = {
    { "quick-switcher.open-quick-switcher", "Quick switcher", 1 },
    { "graph-view.open-graph-view", "Graph view", 1 },
    { "canvas.create-new-canvas", "Canvas", 1 },
    { "command-palette.open-command-palette", "Command palette", 1 },
    { "app.toggle-live-preview-source-mode", "Live preview or source mode of this note", 0 },
    { "app.toggle-left-sidebar", "Show or hide the left sidebar", 0 },
    { "app.toggle-right-sidebar", "Show or hide the right sidebar", 0 },
    { "app.change-theme", "Switch between light and dark", 0 },
    { "app.open-settings", "Settings", 0 },
};
#define NITEMS (sizeof k_items / sizeof k_items[0])

size_t ex_ribbon_item_count(void) { return NITEMS; }
const ExRibbonItem *ex_ribbon_item(size_t i) { return i < NITEMS ? &k_items[i] : NULL; }

/* Walks a comma separated list; calls fn(token, user) for each trimmed non-empty token. */
static void each_token(const char *list, void (*fn)(const char *tok, size_t len, void *u), void *u) {
    const char *s = list ? list : "";
    while (*s) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s), b = 0;
        while (b < n && (s[b] == ' ' || s[b] == '\t')) b++;
        while (n > b && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
        if (n > b) fn(s + b, n - b, u);
        if (!e) break;
        s = e + 1;
    }
}

typedef struct Find { const char *id; int found; int none; int any; } Find;
static void find_cb(const char *tok, size_t len, void *u) {
    Find *f = u;
    f->any = 1;
    if (len == 4 && !memcmp(tok, "none", 4)) f->none = 1;
    if (len == strlen(f->id) && !memcmp(tok, f->id, len)) f->found = 1;
}

int ex_ribbon_shown(const char *list, const char *id) {
    Find f;
    f.id = id; f.found = f.none = f.any = 0;
    each_token(list, find_cb, &f);
    if (!f.any) return 1;                      /* empty: all */
    if (f.none && !f.found) return 0;
    return f.found;
}

typedef struct Build { char *out; size_t len, cap; } Build;
static void add_tok(Build *b, const char *tok, size_t len) {
    if (b->len + len + 3 > b->cap) { b->cap = (b->len + len + 3) * 2; b->out = realloc(b->out, b->cap); if (!b->out) return; }
    if (b->len) { b->out[b->len++] = ','; b->out[b->len++] = ' '; }
    memcpy(b->out + b->len, tok, len);
    b->len += len;
    b->out[b->len] = 0;
}
static int known(const char *tok, size_t len) {
    size_t i;
    for (i = 0; i < NITEMS; i++) if (strlen(k_items[i].id) == len && !memcmp(k_items[i].id, tok, len)) return 1;
    return 0;
}
static int is_none(const char *tok, size_t len) { return len == 4 && !memcmp(tok, "none", 4); }
static void unknown_cb(const char *tok, size_t len, void *u) { if (!known(tok, len) && !is_none(tok, len)) add_tok(u, tok, len); }
static void count_unknown_cb(const char *tok, size_t len, void *u) { if (!known(tok, len) && !is_none(tok, len)) (*(size_t *)u)++; }

char *ex_ribbon_set(const char *list, const char *id, int show) {
    Build b = { NULL, 0, 0 };
    size_t i, shown = 0, unknown = 0;
    int want[16];
    for (i = 0; i < NITEMS; i++) {
        want[i] = ex_ribbon_shown(list, k_items[i].id);
        if (!strcmp(k_items[i].id, id)) want[i] = show != 0;
        shown += (size_t)want[i];
    }
    each_token(list, count_unknown_cb, &unknown);
    b.cap = 64; b.out = calloc(b.cap, 1);
    if (!b.out) return NULL;
    if (shown == NITEMS && unknown == 0) return b.out;                  /* everything shown: the empty value */
    if (shown == 0) add_tok(&b, "none", 4);
    else for (i = 0; i < NITEMS; i++) if (want[i]) add_tok(&b, k_items[i].id, strlen(k_items[i].id));
    each_token(list, unknown_cb, &b);                                    /* ids this version does not know stay */
    return b.out;
}

int ex_high_contrast_resolve(const char *setting, int system_on) {
    if (!setting) return 0;
    if (!strcmp(setting, "on")) return 1;
    if (!strcmp(setting, "system")) return system_on != 0;
    return 0;
}
