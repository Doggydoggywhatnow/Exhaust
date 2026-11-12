/* A vault: a folder of notes. This module is the file explorer's model and the
 * only code that creates, renames, moves or deletes the user's files, and only
 * when the user asks. Sorting, grouping, expanding and revealing change what is
 * shown, never what is on disk.
 *
 * Paths inside a vault are "relative paths": UTF-8, "/" separators, no leading
 * slash, no "." or ".." parts; "" is the vault folder itself. */
#ifndef EXHAUST_VAULT_H
#define EXHAUST_VAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExFileKind {
    EX_KIND_OTHER, EX_KIND_MARKDOWN, EX_KIND_BASE, EX_KIND_CANVAS, EX_KIND_IMAGE, EX_KIND_AUDIO, EX_KIND_VIDEO, EX_KIND_PDF
} ExFileKind;

/* The kind from the file name's extension (the types Exhaust opens, and no others). */
ExFileKind ex_file_kind(const char *name);

typedef enum ExTrashMode { EX_TRASH_SYSTEM, EX_TRASH_VAULT, EX_TRASH_PERMANENT } ExTrashMode; /* files.trash_mode */
typedef enum ExSortBy { EX_SORT_NAME, EX_SORT_MODIFIED, EX_SORT_CREATED } ExSortBy;          /* files.sort_by */

typedef enum ExVaultResult {
    EX_VAULT_OK = 0,
    EX_VAULT_INVALID = -1,    /* a bad name or path */
    EX_VAULT_EXISTS = -2,     /* something is already there: nothing was changed */
    EX_VAULT_NOT_FOUND = -3,
    EX_VAULT_IO = -4          /* the operating system refused; nothing was lost */
} ExVaultResult;

/* A note, folder or other file in the explorer. */
typedef struct ExNode {
    char *name;               /* file name */
    char *rel;                /* path relative to the vault */
    int is_dir;
    int link_loop;            /* a symbolic link back into its own parents: shown, not entered */
    int excluded;             /* matches an excluded pattern: hidden from search, links and the graph */
    ExFileKind kind;
    int64_t size, mtime, ctime;
    int expanded;             /* folders: shown open (a view state, not stored on disk) */
    struct ExNode *parent;
    struct ExNode **kids;
    size_t nkids;
} ExNode;

typedef struct ExVaultOptions {
    int show_all_files;       /* files.show_all_file_types */
    ExTrashMode trash_mode;   /* files.trash_mode */
    ExSortBy sort_by;
    int sort_descending;
    const char *const *excluded; /* files.excluded patterns */
    size_t nexcluded;
} ExVaultOptions;

typedef struct ExVault ExVault;

/* Opens a folder as a vault: finds its canonical path and id and reads the tree.
 * NULL if the folder cannot be read. */
ExVault *ex_vault_open(const char *folder);
void ex_vault_close(ExVault *v);
const char *ex_vault_path(const ExVault *v);   /* canonical absolute path */
const char *ex_vault_id(const ExVault *v);     /* 32 hex digits */
const char *ex_vault_name(const ExVault *v);   /* the folder's name */

/* Changes options and rebuilds the tree; expanded folders stay expanded. The
 * strings of excluded are copied. */
void ex_vault_set_options(ExVault *v, const ExVaultOptions *o);
void ex_vault_get_options(const ExVault *v, ExVaultOptions *o);
/* Reads the folders again (after changes made outside Exhaust). */
int ex_vault_rescan(ExVault *v);

const ExNode *ex_vault_root(const ExVault *v);
const ExNode *ex_vault_find(const ExVault *v, const char *rel);
char *ex_vault_abs_path(const ExVault *v, const char *rel); /* malloc'ed, "/" separators */

/* The rows the explorer shows: the tree flattened, folders only when expanded. */
size_t ex_vault_row_count(ExVault *v);
const ExNode *ex_vault_row(ExVault *v, size_t i, int *depth);
void ex_vault_toggle(ExVault *v, const char *rel);
void ex_vault_expand_all(ExVault *v);
void ex_vault_collapse_all(ExVault *v);
/* Opens every folder above rel (auto-reveal of the open note); returns its row or -1. */
long ex_vault_reveal(ExVault *v, const char *rel);

/* True if rel matches one of the excluded patterns. A pattern is matched against the
 * whole relative path and, when it has no "/", against every part of it; "*" matches
 * within a name, "**" across folders, "?" one character; a trailing "/" means a folder
 * and everything in it. Matching ignores case. */
int ex_vault_is_excluded(const ExVault *v, const char *rel);
int ex_glob_match(const char *pattern, const char *text);

/* Is this a valid name for a new file or folder? No separators, control characters,
 * "# ^ [ ] |" or ":", no "%%", no Windows reserved names (CON, NUL, COM1 and so on),
 * no trailing dot or space, at most 255 bytes. */
int ex_name_valid(const char *name);

/* The actions the user takes. Each leaves everything as it was if it fails; none
 * replaces an existing file. out_rel (optional) gets the new relative path (malloc'ed). */
ExVaultResult ex_vault_create_note(ExVault *v, const char *parent_rel, const char *name, char **out_rel); /* adds ".md" if the name has no extension */
ExVaultResult ex_vault_create_folder(ExVault *v, const char *parent_rel, const char *name, char **out_rel);
ExVaultResult ex_vault_rename(ExVault *v, const char *rel, const char *new_name, char **out_rel);
ExVaultResult ex_vault_move(ExVault *v, const char *rel, const char *new_parent_rel, char **out_rel);
ExVaultResult ex_vault_delete(ExVault *v, const char *rel); /* by options.trash_mode */

/* "More options": open with the default program, show in the file manager, copy the path. */
int ex_vault_open_default(const ExVault *v, const char *rel);
int ex_vault_show_in_file_manager(const ExVault *v, const char *rel);
int ex_vault_copy_path(const ExVault *v, const char *rel, int absolute); /* to the clipboard */

/* ---- the list of remembered vaults (vaults.ini, docs/formats.md) ---- */

typedef struct ExRegistry ExRegistry;
typedef struct ExRegistryEntry { const char *id, *path, *name; int64_t opened; } ExRegistryEntry;

/* Loads the file (path NULL: the OS config folder); a missing file is an empty list. */
ExRegistry *ex_registry_load(const char *path);
int ex_registry_save(const ExRegistry *r);
void ex_registry_free(ExRegistry *r);
size_t ex_registry_count(const ExRegistry *r);
int ex_registry_get(const ExRegistry *r, size_t i, ExRegistryEntry *out);
/* Remembers a folder (by its canonical path); returns its id (valid until the next change)
 * or NULL. Adding a folder that is already in the list changes nothing. */
const char *ex_registry_add(ExRegistry *r, const char *folder);
/* Removes from the list only; no file is touched. */
int ex_registry_remove(ExRegistry *r, const char *id);
int ex_registry_rename(ExRegistry *r, const char *id, const char *name);
/* The vault moved on disk: remembers the new folder (new id) and forgets the old. */
const char *ex_registry_move(ExRegistry *r, const char *id, const char *new_folder);
int ex_registry_touch(ExRegistry *r, const char *id, int64_t when); /* last opened; also sets [vaults] last */
const char *ex_registry_last(const ExRegistry *r);

#ifdef __cplusplus
}
#endif
#endif
