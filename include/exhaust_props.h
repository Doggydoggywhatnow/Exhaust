/* Tags and properties (sub-slice 4a-iii).
 *
 * Properties are the YAML of the frontmatter, read with libyaml and edited as text: every change below names the lines
 * of one property and replaces only those; no other byte of the note moves. A note whose frontmatter is not valid YAML
 * is left alone. The vault knows one type for each property name (the file .exhaust/types.ini, written when the user
 * changes a type); without an entry the type is read from the value. Types: text, list, number, checkbox, date,
 * datetime and links (docs/formats.md, "Properties"). */
#ifndef EXHAUST_PROPS_H
#define EXHAUST_PROPS_H
#include <stddef.h>
#include <stdint.h>
#include "exhaust_extract.h"
#include "exhaust_index.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *ex_prop_type_name(int type);               /* "text", "list", ... */
int ex_prop_type_from_name(const char *name);          /* -1 if unknown */

/* ---- tags ---- */
typedef struct ExTagInfo { char *tag; long notes; } ExTagInfo;       /* tag without '#', as first written; notes = notes that use exactly this tag */
/* Every distinct tag (compared without regard to case), sorted. */
int ex_tags_all(ExIndex *r, ExTagInfo **out, size_t *n);
void ex_tags_free(ExTagInfo *t, size_t n);
/* Paths of the notes that use a tag; with nested also those that use a tag below it (tag "a" finds "a/b"). Sorted. */
int ex_tag_notes(ExIndex *r, const char *tag, int nested, char ***paths, size_t *n);
void ex_paths_free(char **paths, size_t n);

/* ---- properties of a note and of the vault ---- */
typedef struct ExNoteProp { char *name; int type; char *value; size_t start, end; uint32_t line; } ExNoteProp;
int ex_note_props(ExIndex *r, const char *path, ExNoteProp **out, size_t *n);
void ex_note_props_free(ExNoteProp *p, size_t n);
typedef struct ExPropInfo { char *name; int type; long notes; } ExPropInfo;      /* type: the vault's, from types.ini, else the most common in the notes */
int ex_props_all(ExIndex *r, const char *vault_root, ExPropInfo **out, size_t *n);
void ex_props_all_free(ExPropInfo *p, size_t n);
/* The style classes of a note (the property cssclasses), malloc'ed strings. */
int ex_note_cssclasses(ExIndex *r, const char *path, char ***classes, size_t *n);

/* ---- the vault's property types (.exhaust/types.ini) ---- */
int ex_prop_type_get(const char *vault_root, const char *name);                   /* -1 if the vault has none for it */
int ex_prop_type_set(const char *vault_root, const char *name, int type);        /* 0 on success */

/* ---- edits of a note's text. Each returns the new text (malloc'ed, *out_len set), or NULL if it cannot be done (no such
 * property, not valid YAML, a name that cannot be a key, a value that does not fit the type). value of a list: the items
 * joined by "\n". ---- */
char *ex_prop_text_set(const char *text, size_t len, const char *name, int type, const char *value, size_t *out_len);
char *ex_prop_text_remove(const char *text, size_t len, const char *name, size_t *out_len);
char *ex_prop_text_rename(const char *text, size_t len, const char *from, const char *to, size_t *out_len);
char *ex_prop_text_retype(const char *text, size_t len, const char *name, int type, size_t *out_len);

/* ---- how notes are read and written by actions that change many notes (the application reads and writes open notes
 * through their buffers; ex_note_io_files works on the files) ---- */
typedef struct ExNoteIO {
    char *(*read)(void *user, const char *rel, size_t *len);                      /* the editing text, malloc'ed */
    int (*write)(void *user, const char *rel, const char *text, size_t len);       /* 0 on success */
    void *user;
} ExNoteIO;
/* Files of a vault: reading drops the byte-order mark and CRLF the way the editor does; writing puts them back (each
 * note keeps its own) and replaces the file atomically. free_user with ex_note_io_free. */
ExNoteIO ex_note_io_files(const char *vault_root);
void ex_note_io_free(ExNoteIO *io);

/* The user's actions over the whole vault. *changed counts the notes that were rewritten. */
int ex_props_rename_everywhere(ExIndex *r, const ExNoteIO *io, const char *vault_root, const char *from, const char *to, size_t *changed);
int ex_props_retype_everywhere(ExIndex *r, const ExNoteIO *io, const char *vault_root, const char *name, int type, size_t *changed);

#ifdef __cplusplus
}
#endif
#endif
