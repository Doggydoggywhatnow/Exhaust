/* The vault manager's model: the list of remembered vaults (vaults.ini) plus the few things that
 * change folders on disk, all of them only on an explicit user action:
 *   create      a new, empty folder (never an existing one) that becomes a vault;
 *   open_folder an existing folder of notes becomes a vault; nothing in it is touched;
 *   rename      the name shown in the list; the folder keeps its name (so nothing is orphaned);
 *   move        the vault's folder moves to another folder on the same drive; the vault's journal and
 *               session state (keyed by the vault's id, which comes from its path) move with it;
 *   remove      forgets the vault in the list; no file is touched.
 * Choosing folders (the operating system's dialog) is the caller's job. */
#ifndef EXHAUST_VAULTMGR_H
#define EXHAUST_VAULTMGR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExVaultManager ExVaultManager;

typedef struct ExVaultInfo {
    const char *id, *name, *path;
    int64_t opened;           /* seconds since the Unix epoch, 0 never */
    int exists;               /* the folder is there */
} ExVaultInfo;

typedef enum ExVmResult {
    EX_VM_OK = 0,
    EX_VM_INVALID_NAME,       /* the name cannot be a folder name */
    EX_VM_EXISTS,             /* the target already exists: nothing was replaced */
    EX_VM_NOT_FOUND,          /* no such vault in the list, or no such folder */
    EX_VM_NOT_A_FOLDER,
    EX_VM_INTO_ITSELF,        /* a vault cannot move into itself */
    EX_VM_IO,                 /* the OS refused (another drive, no permission, ...) */
    EX_VM_DATA_NOT_MOVED,     /* the folder moved and the list is updated, but its recovery data stayed under the old id */
    EX_VM_SAME_VAULT          /* the source and the target are the same vault */
} ExVmResult;

typedef struct ExTransferResult {
    int files_copied;         /* theme, snippet and hotkey files that were not in the target and were copied */
    int keys_added;           /* settings and hotkeys the target did not have, added to its files */
    int skipped;              /* files and keys the target already had: they are left exactly as they are */
} ExTransferResult;

/* registry_path NULL: vaults.ini in the OS config folder. data_root NULL: the OS local-data folder
 * (a vault's journal and session are `<data_root>/vaults/<id>`). */
ExVaultManager *ex_vaultmgr_create(const char *registry_path, const char *data_root);
void ex_vaultmgr_free(ExVaultManager *m);
unsigned long ex_vaultmgr_revision(const ExVaultManager *m);          /* grows with every change of the list */

size_t ex_vaultmgr_count(const ExVaultManager *m);                     /* most recently opened first */
int ex_vaultmgr_get(const ExVaultManager *m, size_t i, ExVaultInfo *out); /* valid until the next change */
const char *ex_vaultmgr_last(const ExVaultManager *m);               /* id of the vault opened last, or NULL */

/* out_id (may be NULL) receives the vault's id, valid until the next change. */
ExVmResult ex_vaultmgr_create_vault(ExVaultManager *m, const char *parent_folder, const char *name, const char **out_id);
ExVmResult ex_vaultmgr_open_folder(ExVaultManager *m, const char *folder, const char **out_id);
ExVmResult ex_vaultmgr_rename(ExVaultManager *m, const char *id, const char *new_name);
ExVmResult ex_vaultmgr_move(ExVaultManager *m, const char *id, const char *new_parent_folder, const char **out_id);
/* Copies the settings of one vault to another: `.exhaust/settings.ini` and `hotkeys.ini` (every key the
 * target does not set is added, with the target's own text kept as it is), and the files of `themes` and
 * `snippets` the target does not have. Anything the target already has is never replaced. The source is
 * only read. Plugins and workspaces are not copied. */
ExVmResult ex_vaultmgr_transfer_settings(ExVaultManager *m, const char *from_id, const char *to_id, ExTransferResult *out);
ExVmResult ex_vaultmgr_remove(ExVaultManager *m, const char *id);
ExVmResult ex_vaultmgr_touch(ExVaultManager *m, const char *id, int64_t when);
const char *ex_vm_message(ExVmResult r);                               /* a sentence for the user */

/* Is this a name Exhaust accepts for a new vault's folder? */
int ex_vault_folder_name_valid(const char *name);

#ifdef __cplusplus
}
#endif
#endif
