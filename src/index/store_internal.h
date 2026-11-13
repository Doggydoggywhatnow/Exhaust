#ifndef EXHAUST_STORE_INTERNAL_H
#define EXHAUST_STORE_INTERNAL_H
#include <stdint.h>
#include "exhaust_extract.h"
#include "index_internal.h"

/* indexer.c */
int ex_store_note(ExIndex *ix, int64_t id, const char *text, size_t len);
int ex_store_clear(ExIndex *ix, int64_t id);
/* store_extra.c: what later sub-slices store about a note (links, names, tags, properties), cleared and written with it. */
int ex_store_extra(ExIndex *ix, int64_t id, const ExExtract *x);
int ex_store_clear_extra(ExIndex *ix, int64_t id);
int ex_store_names(ExIndex *ix, int64_t id, const char *rel, int kind);
int ex_store_clear_names(ExIndex *ix, int64_t id);
int ex_clear_extra_tables(ExIndex *ix);
int ex_store_extra_v3(ExIndex *ix, int64_t id, const ExExtract *x);
int ex_store_clear_extra_v3(ExIndex *ix, int64_t id);
int ex_clear_extra_tables_v3(ExIndex *ix);
char *ex_link_name_key(const char *target);     /* the key of the last part of a link target, without ".md" */
char *ex_link_target_key(const char *target);   /* the key of the whole target, without ".md" */
char *ex_path_normalize(const char *dir, size_t dlen, const char *rel);   /* "a/b/../c" -> "a/c"; NULL if it leaves the vault */
extern const char *const ex_digest_queries[];
long ex_index_note_reads(void);
#endif
