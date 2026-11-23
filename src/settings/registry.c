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
/* a command added while the program runs (a plugin's, 8b): after the table, in the order added */
typedef struct Dyn { ExCommandDef def; char *id, *name; Bound b; } Dyn;
struct ExCommands { ExFeatures *features; Bound *bound; Dyn **dyn; size_t ndyn, capdyn; };

ExCommands *ex_commands_create(ExFeatures *features) {
    ExCommands *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->features = features;
    c->bound = calloc(ex_command_table_count ? ex_command_table_count : 1, sizeof *c->bound);
    if (!c->bound) { free(c); return NULL; }
    return c;
}

void ex_commands_free(ExCommands *c) {
    size_t i;
    if (!c) return;
    for (i = 0; i < c->ndyn; i++) { free(c->dyn[i]->id); free(c->dyn[i]->name); free(c->dyn[i]); }
    free(c->dyn); free(c->bound); free(c);
}

/* the index in the table, or table_count + the index among the added ones, or -1 */
static long index_of(const ExCommands *c, const char *id) {
    size_t i;
    for (i = 0; i < ex_command_table_count; i++) if (strcmp(ex_command_table[i].id, id) == 0) return (long)i;
    for (i = 0; c && i < c->ndyn; i++) if (strcmp(c->dyn[i]->id, id) == 0) return (long)(ex_command_table_count + i);
    return -1;
}
static const ExCommandDef *def_at(const ExCommands *c, long i) { return (size_t)i < ex_command_table_count ? &ex_command_table[i] : &c->dyn[(size_t)i - ex_command_table_count]->def; }
static Bound *bound_at(ExCommands *c, long i) { return (size_t)i < ex_command_table_count ? &c->bound[i] : &c->dyn[(size_t)i - ex_command_table_count]->b; }

int ex_commands_add_dynamic(ExCommands *c, const char *id, const char *name, void (*run)(void *), int (*check)(void *), void *user, int editor) {
    Dyn *d;
    if (!c || !id || !*id || !name || !run || index_of(c, id) >= 0) return -1;
    if (c->ndyn == c->capdyn) {
        size_t nc = c->capdyn ? c->capdyn * 2 : 16;
        Dyn **nd = realloc(c->dyn, nc * sizeof *nd);
        if (!nd) return -1;
        c->dyn = nd; c->capdyn = nc;
    }
    d = calloc(1, sizeof *d);
    if (!d) return -1;
    d->id = malloc(strlen(id) + 1); d->name = malloc(strlen(name) + 1);
    if (!d->id || !d->name) { free(d->id); free(d->name); free(d); return -1; }
    strcpy(d->id, id); strcpy(d->name, name);
    d->def.id = d->id; d->def.name = d->name; d->def.default_keys = ""; d->def.editor = editor != 0;
    d->b.run = run; d->b.check = check; d->b.user = user;
    c->dyn[c->ndyn++] = d;
    return 0;
}

size_t ex_commands_remove_dynamic(ExCommands *c, const char *prefix) {
    size_t i = 0, w = 0, removed = 0, pl = prefix ? strlen(prefix) : 0;
    if (!c || !pl) return 0;
    for (i = 0; i < c->ndyn; i++) {
        if (strncmp(c->dyn[i]->id, prefix, pl) == 0) { free(c->dyn[i]->id); free(c->dyn[i]->name); free(c->dyn[i]); removed++; }
        else c->dyn[w++] = c->dyn[i];
    }
    c->ndyn = w;
    return removed;
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
    long i = index_of(c, id);
    if (i < 0) return -1;
    bound_at(c, i)->run = run;
    bound_at(c, i)->check = check;
    bound_at(c, i)->user = user;
    return 0;
}

const ExCommandDef *ex_commands_find(const ExCommands *c, const char *id) { long i = index_of(c, id); return i < 0 ? NULL : def_at(c, i); }
int ex_commands_bound(const ExCommands *c, const char *id) { long i = index_of(c, id); return i >= 0 && bound_at((ExCommands *)c, i)->run != NULL; }

static int available(const ExCommands *c, long i) {
    const char *feat;
    Bound *b;
    if (i < 0) return 0;
    b = bound_at((ExCommands *)c, i);
    if (!b->run) return 0;
    feat = ex_command_feature(def_at(c, i)->id);
    if (feat && c->features && !ex_features_enabled(c->features, feat)) return 0;
    if (b->check && !b->check(b->user)) return 0;
    return 1;
}

int ex_commands_available(const ExCommands *c, const char *id) { return available(c, index_of(c, id)); }

int ex_commands_run(ExCommands *c, const char *id) {
    long i = index_of(c, id);
    Bound b;
    if (i < 0) return -1;
    if (!bound_at(c, i)->run) return -2;
    if (!available(c, i)) return -3;
    b = *bound_at(c, i);                                  /* a command may remove itself (a plugin turned off) */
    b.run(b.user);
    return 0;
}

size_t ex_commands_count(const ExCommands *c) { return ex_command_table_count + (c ? c->ndyn : 0); }
const ExCommandDef *ex_commands_at(const ExCommands *c, size_t i) { return i < ex_commands_count(c) ? def_at(c, (long)i) : NULL; }
