/* Dropping a file into a note (docs/formats.md, "Dropping files into a note (attachments)"): the file is copied into the vault where
 * [files] attachment_location says, never replacing a file, and the text of a link to the copy is made. No interface code here:
 * the copy may be large, so the application runs ex_attach_copy on a thread of its own. */
#ifndef EXHAUST_ATTACH_H
#define EXHAUST_ATTACH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExAttachLocation { EX_ATTACH_VAULT = 0, EX_ATTACH_FOLDER, EX_ATTACH_SAME_FOLDER, EX_ATTACH_SUBFOLDER } ExAttachLocation;

typedef struct ExAttachConfig {
    const char *vault_root;       /* absolute, no trailing slash */
    const char *note_rel;         /* the open note, from the vault; may be empty (the location then falls back to the top folder) */
    ExAttachLocation location;
    const char *folder;           /* attachment_folder */
    const char *subfolder;        /* attachment_subfolder */
} ExAttachConfig;

typedef struct ExAttachResult {
    char rel[2200];               /* where the copy is, from the vault ('/' separators) */
    char link[2300];              /* "![[name.png]]" or "[[name.ext]]" */
    char error[300];              /* set when the copy failed (the return value is -1) */
} ExAttachResult;

/* The folder (from the vault, '' for the top folder) the settings and the note choose. Always a safe path. */
void ex_attach_folder(const ExAttachConfig *c, char *out, size_t cap);
/* Copies `source` (an absolute path to a file) and fills `out`. Returns 0, or -1 with out->error. */
int ex_attach_copy(const ExAttachConfig *c, const char *source, ExAttachResult *out);
/* The link text for a vault path, as ex_attach_copy writes it. */
void ex_attach_link(const char *rel, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
