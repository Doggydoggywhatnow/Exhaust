/* The vault manager screen (layout of screenshots/vault-manager.png, drawn and worded as Exhaust's own):
 * on the left the list of remembered vaults, each with a menu (Rename, Move, Remove from list); on the
 * right the title and a card with Create (a new vault in a folder you choose), Open (an existing folder
 * as a vault) and a disabled, labelled stub for synced vaults. Choosing a folder is the application's
 * job (the operating system's dialog, or a mock in tests): the screen asks through a hook and is told
 * the answer with ex_vaultscreen_folder_chosen. Opening a vault is also the application's job. */
#ifndef EXHAUST_VAULTSCREEN_H
#define EXHAUST_VAULTSCREEN_H

#include "exhaust_ui.h"
#include "exhaust_vaultmgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExPickPurpose {
    EX_PICK_CREATE_PARENT,   /* the folder the new vault goes in */
    EX_PICK_OPEN_FOLDER,     /* an existing folder to open as a vault */
    EX_PICK_MOVE_TARGET      /* the folder a vault is moved into (vault_id says which) */
} ExPickPurpose;

typedef struct ExVaultScreenHooks {
    void (*pick_folder)(void *user, ExPickPurpose purpose, const char *vault_id);
    void (*open_vault)(void *user, const char *folder);
} ExVaultScreenHooks;

typedef struct ExVaultScreen ExVaultScreen;

ExVaultScreen *ex_vaultscreen_create(ExUi *ui, ExVaultManager *mgr, const ExVaultScreenHooks *hooks, void *user, const char *version);
void ex_vaultscreen_free(ExVaultScreen *s);
ExWidget *ex_vaultscreen_root(ExVaultScreen *s);
/* Rebuilds the widgets if something changed; call once per frame before layout. */
void ex_vaultscreen_update(ExVaultScreen *s);
/* The answer to pick_folder; path NULL means the user cancelled. */
void ex_vaultscreen_folder_chosen(ExVaultScreen *s, ExPickPurpose purpose, const char *vault_id, const char *path);
/* The last message shown under the card (for tests). */
const char *ex_vaultscreen_message(const ExVaultScreen *s);

#ifdef __cplusplus
}
#endif
#endif
