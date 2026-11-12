/* Settings: `settings.ini` in the vault's `.exhaust` folder, with the global file in the OS config
 * folder underneath it (a key the vault file does not set falls back to the global file, then to the
 * default in docs/formats.md, from which the table of known keys is generated at build time).
 * Every change is written through: the file text is patched (comments, order, unknown keys, line
 * endings and the byte-order mark stay exactly as they were) and replaced atomically on a worker
 * thread at once, not at exit. Reading never writes, and `.exhaust` is not created until the first
 * change. Values that do not fit their type are refused when set and ignored (the default is used)
 * when read from a file; they stay in the file untouched. */
#ifndef EXHAUST_SETTINGS_H
#define EXHAUST_SETTINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExSettingType { EX_SET_BOOL, EX_SET_INT, EX_SET_PERCENT, EX_SET_STRING, EX_SET_PATH, EX_SET_COLOR, EX_SET_LIST, EX_SET_ENUM } ExSettingType;

typedef struct ExSettingDef {
    const char *section, *key;
    ExSettingType type;
    const char *def;          /* default value as text; "" for an empty default */
    const char *enum_values;  /* "a|b|c" for EX_SET_ENUM, else NULL */
    int stub;                 /* a stub feature: it cannot be turned on */
} ExSettingDef;

extern const ExSettingDef ex_settings_schema[]; /* generated from docs/formats.md */
extern const size_t ex_settings_schema_count;

typedef struct ExSettings ExSettings;

/* vault_config_dir: `<vault>/.exhaust` (NULL for none); global_config_dir: the OS config folder
 * (NULL for none). With no vault folder, changes go to the global file. */
ExSettings *ex_settings_open(const char *vault_config_dir, const char *global_config_dir);
/* Waits for the writes that are queued, then frees. */
void ex_settings_close(ExSettings *s);

const ExSettingDef *ex_settings_def(const char *section, const char *key); /* NULL for an unknown key */

/* The effective value as text: vault file, else global file, else the default. Valid until the next
 * change of that key. NULL for a key that is not in the schema and not in either file. */
const char *ex_settings_get(const ExSettings *s, const char *section, const char *key);
int ex_settings_get_bool(const ExSettings *s, const char *section, const char *key);
long ex_settings_get_int(const ExSettings *s, const char *section, const char *key);
/* True if the vault file (not the default) sets this key. */
int ex_settings_is_set(const ExSettings *s, const char *section, const char *key);

/* Validates the value against the key's type (bool: true or false; int and percent: whole numbers;
 * color: #rrggbb or #rrggbbaa or empty; enum: one of its values; stub features cannot be turned on),
 * updates what get returns, and queues the write. Returns 0, or -1 for an unknown key (unknown keys
 * are only ever kept, not created, by this function) or a refused value. */
int ex_settings_set(ExSettings *s, const char *section, const char *key, const char *value);
int ex_settings_set_bool(ExSettings *s, const char *section, const char *key, int value);
int ex_settings_set_int(ExSettings *s, const char *section, const char *key, long value);
/* Removes nothing from the file; writes the default again so the key reads as default. */

/* Number of writes finished and queued but not finished (for tests and for leaving). */
int ex_settings_writes_done(const ExSettings *s);
int ex_settings_writes_pending(const ExSettings *s);
/* Waits until nothing is pending. */
void ex_settings_flush(ExSettings *s);
const char *ex_settings_write_path(const ExSettings *s);

/* Checks a value without storing it. */
int ex_settings_valid(const ExSettingDef *def, const char *value);

/* Applies the [files] settings that belong to the vault (show_all_file_types, trash_mode, sort_by,
 * sort_descending, excluded) to it. Only the view changes; no file is touched. */
typedef struct ExVault ExVault;
void ex_settings_apply_vault(const ExSettings *s, ExVault *v);

#ifdef __cplusplus
}
#endif
#endif
