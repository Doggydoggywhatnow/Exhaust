/* The feature-toggle registry and the command registry.
 *
 * Features: one switch per built-in feature, stored in `[features]` of settings.ini (write-through,
 * see exhaust_settings.h). A feature registers itself with a label and a callback that is called when
 * its switch changes; a stub feature (Sync, Publish) can never be turned on.
 *
 * Commands: the full table of commands is generated from docs/formats.md (id, name, default shortcut,
 * whether it acts on the text at the cursor), so it is the one list the palette, the hotkeys screen
 * and the slash menu read. A command is *bound* when the code that runs it exists and calls
 * ex_commands_bind; until then it is declared but not available. A command whose feature is switched
 * off is not available either (its area, with hyphens as underscores, names the feature: the area
 * `daily-notes` belongs to the feature `daily_notes`). */
#ifndef EXHAUST_COMMANDS_H
#define EXHAUST_COMMANDS_H

#include <stddef.h>

#include "exhaust_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExCommandDef {
    const char *id, *name;
    const char *default_keys;   /* Windows and Linux, "" for none */
    int editor;                 /* acts on the text at the cursor (listed in the slash menu) */
} ExCommandDef;

extern const ExCommandDef ex_command_table[]; /* generated */
extern const size_t ex_command_table_count;

/* ---- features ---- */

typedef struct ExFeatures ExFeatures;
typedef void (*ExFeatureFn)(const char *id, int enabled, void *user);

ExFeatures *ex_features_create(ExSettings *settings);  /* settings are not owned and must outlive it */
void ex_features_free(ExFeatures *f);
/* id must be a key of [features] in docs/formats.md. Returns 0, or -1 for an unknown or already registered id. */
int ex_features_register(ExFeatures *f, const char *id, const char *label, ExFeatureFn on_change, void *user);
int ex_features_enabled(const ExFeatures *f, const char *id);          /* the switch, registered or not */
/* Changes the switch (written to settings.ini at once) and calls the feature's callback if the value
 * changed. Returns 0, or -1 for an unknown id or a stub turned on. */
int ex_features_set(ExFeatures *f, const char *id, int enabled);
size_t ex_features_count(const ExFeatures *f);                         /* registered features, in order */
const char *ex_features_id(const ExFeatures *f, size_t i);
const char *ex_features_label(const ExFeatures *f, size_t i);
int ex_features_is_stub(const ExFeatures *f, size_t i);

/* ---- commands ---- */

typedef struct ExCommands ExCommands;

ExCommands *ex_commands_create(ExFeatures *features);   /* features may be NULL: nothing is then switched off */
void ex_commands_free(ExCommands *c);
/* Binds the code of a command. check, if given, says whether the command can run right now (for
 * example "a note is open"). Returns 0, or -1 if the id is not in the table. A second bind replaces the first. */
int ex_commands_bind(ExCommands *c, const char *id, void (*run)(void *user), int (*check)(void *user), void *user);
const ExCommandDef *ex_commands_find(const ExCommands *c, const char *id);
/* The feature a command belongs to, or NULL (app and editor commands). */
const char *ex_command_feature(const char *id);
int ex_commands_bound(const ExCommands *c, const char *id);
int ex_commands_available(const ExCommands *c, const char *id);       /* bound, feature on, check passes */
/* 0 ran; -1 unknown id; -2 not bound; -3 not available now (its feature is off, or check failed) */
int ex_commands_run(ExCommands *c, const char *id);
size_t ex_commands_count(const ExCommands *c);                          /* the whole table */
const ExCommandDef *ex_commands_at(const ExCommands *c, size_t i);

#ifdef __cplusplus
}
#endif
#endif
