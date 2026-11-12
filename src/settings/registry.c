#include <stdlib.h>
#include <string.h>

#include "exhaust_commands.h"

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

/* ---- features ---- */

typedef struct Feature { char *id, *label; ExFeatureFn fn; void *user; } Feature;
struct ExFeatures { ExSettings *settings; Feature *items; size_t n, cap; };

ExFeatures *ex_features_create(ExSettings *settings) {
    ExFeatures *f = calloc(1, sizeof *f);
    if (f) f->settings = settings;
    return f;
}

void ex_features_free(ExFeatures *f) {
    size_t i;
    if (!f) return;
    for (i = 0; i < f->n; i++) { free(f->items[i].id); free(f->items[i].label); }
    free(f->items);
    free(f);
}

static Feature *feature(const ExFeatures *f, const char *id) {
    size_t i;
    for (i = 0; i < f->n; i++) if (strcmp(f->items[i].id, id) == 0) return &f->items[i];
    return NULL;
}

int ex_features_register(ExFeatures *f, const char *id, const char *label, ExFeatureFn on_change, void *user) {
    Feature *it;
    if (!ex_settings_def("features", id) || feature(f, id)) return -1;
    if (f->n == f->cap) {
        size_t nc = f->cap ? f->cap * 2 : 16;
        Feature *ni = realloc(f->items, nc * sizeof *ni);
        if (!ni) return -1;
        f->items = ni;
        f->cap = nc;
    }
    it = &f->items[f->n];
    it->id = dup_str(id);
    it->label = dup_str(label ? label : id);
    if (!it->id || !it->label) { free(it->id); free(it->label); return -1; }
    it->fn = on_change;
    it->user = user;
    f->n++;
    return 0;
}

int ex_features_enabled(const ExFeatures *f, const char *id) {
    return ex_settings_def("features", id) ? ex_settings_get_bool(f->settings, "features", id) : 0;
}

int ex_features_set(ExFeatures *f, const char *id, int enabled) {
    int was;
    Feature *it;
    if (!ex_settings_def("features", id)) return -1;
    was = ex_features_enabled(f, id);
    if (ex_settings_set_bool(f->settings, "features", id, enabled) != 0) return -1;   /* a stub cannot be turned on */
    it = feature(f, id);
    if (it && it->fn && was != (enabled != 0)) it->fn(it->id, enabled != 0, it->user);
    return 0;
}

size_t ex_features_count(const ExFeatures *f) { return f->n; }
const char *ex_features_id(const ExFeatures *f, size_t i) { return i < f->n ? f->items[i].id : NULL; }
const char *ex_features_label(const ExFeatures *f, size_t i) { return i < f->n ? f->items[i].label : NULL; }
int ex_features_is_stub(const ExFeatures *f, size_t i) {
    const ExSettingDef *d = i < f->n ? ex_settings_def("features", f->items[i].id) : NULL;
    return d ? d->stub : 0;
}

/* ---- commands ---- */

typedef struct Bound { void (*run)(void *); int (*check)(void *); void *user; } Bound;
struct ExCommands { ExFeatures *features; Bound *bound; };

ExCommands *ex_commands_create(ExFeatures *features) {
    ExCommands *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->features = features;
    c->bound = calloc(ex_command_table_count ? ex_command_table_count : 1, sizeof *c->bound);
    if (!c->bound) { free(c); return NULL; }
    return c;
}

void ex_commands_free(ExCommands *c) { if (c) { free(c->bound); free(c); } }

static long index_of(const char *id) {
    size_t i;
    for (i = 0; i < ex_command_table_count; i++) if (strcmp(ex_command_table[i].id, id) == 0) return (long)i;
    return -1;
}

/* Areas whose name is not the feature's name with underscores. */
static const struct { const char *area, *feature; } k_area_alias[] = {{"graph-view", "graph"}, {"unique-note-creator", "unique_note"}};

const char *ex_command_feature(const char *id) {
    static char name[64];
    size_t i, n = strcspn(id, ".");
    if (n == 0 || n >= sizeof name || id[n] != '.') return NULL;
    for (i = 0; i < sizeof k_area_alias / sizeof k_area_alias[0]; i++)
        if (strlen(k_area_alias[i].area) == n && memcmp(k_area_alias[i].area, id, n) == 0) return k_area_alias[i].feature;
    for (i = 0; i < n; i++) name[i] = id[i] == '-' ? '_' : id[i];
    name[n] = 0;
    return ex_settings_def("features", name) ? name : NULL;
}

int ex_commands_bind(ExCommands *c, const char *id, void (*run)(void *), int (*check)(void *), void *user) {
    long i = index_of(id);
    if (i < 0) return -1;
    c->bound[i].run = run;
    c->bound[i].check = check;
    c->bound[i].user = user;
    return 0;
}

const ExCommandDef *ex_commands_find(const ExCommands *c, const char *id) { long i = index_of(id); (void)c; return i < 0 ? NULL : &ex_command_table[i]; }
int ex_commands_bound(const ExCommands *c, const char *id) { long i = index_of(id); return i >= 0 && c->bound[i].run != NULL; }

static int available(const ExCommands *c, long i) {
    const char *feat;
    if (i < 0 || !c->bound[i].run) return 0;
    feat = ex_command_feature(ex_command_table[i].id);
    if (feat && c->features && !ex_features_enabled(c->features, feat)) return 0;
    if (c->bound[i].check && !c->bound[i].check(c->bound[i].user)) return 0;
    return 1;
}

int ex_commands_available(const ExCommands *c, const char *id) { return available(c, index_of(id)); }

int ex_commands_run(ExCommands *c, const char *id) {
    long i = index_of(id);
    if (i < 0) return -1;
    if (!c->bound[i].run) return -2;
    if (!available(c, i)) return -3;
    c->bound[i].run(c->bound[i].user);
    return 0;
}

size_t ex_commands_count(const ExCommands *c) { (void)c; return ex_command_table_count; }
const ExCommandDef *ex_commands_at(const ExCommands *c, size_t i) { (void)c; return i < ex_command_table_count ? &ex_command_table[i] : NULL; }
