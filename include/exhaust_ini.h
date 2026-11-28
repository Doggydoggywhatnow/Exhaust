/* A small INI model: sections and key = value lines in file order. Reading keeps every
 * section and key, including ones this version does not know, and writing puts them all
 * back, so a file saved by a newer version loses nothing when an older one rewrites it
 * (comments are not kept). Used by the session file, the settings file and others. */
#ifndef EXHAUST_INI_H
#define EXHAUST_INI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExIni ExIni;

ExIni *ex_ini_new(void);
ExIni *ex_ini_parse(const char *text, size_t len); /* never fails on bad lines: they are skipped */
/* The same, but " #" in a value is part of the value, not the start of a comment: for files that only Exhaust writes (session.ini), whose values
 * are names and queries a person typed. */
ExIni *ex_ini_parse_plain(const char *text, size_t len);
void ex_ini_free(ExIni *ini);
/* The file text (malloc'ed, NUL terminated). Sections and keys come out in the order they were read or added. */
char *ex_ini_write(const ExIni *ini, size_t *len);

const char *ex_ini_get(const ExIni *ini, const char *section, const char *key); /* NULL if absent */
int ex_ini_set(ExIni *ini, const char *section, const char *key, const char *value); /* adds the section if needed; 0 on success */
int ex_ini_set_int(ExIni *ini, const char *section, const char *key, long long value);
int ex_ini_has_section(const ExIni *ini, const char *section);
void ex_ini_remove_section(ExIni *ini, const char *section);
void ex_ini_remove_key(ExIni *ini, const char *section, const char *key);

size_t ex_ini_section_count(const ExIni *ini);
const char *ex_ini_section_name(const ExIni *ini, size_t i);
size_t ex_ini_key_count(const ExIni *ini, const char *section);
const char *ex_ini_key_name(const ExIni *ini, const char *section, size_t i);

/* Typed reads with defaults (a missing or unreadable value gives the default). */
long long ex_ini_get_int(const ExIni *ini, const char *section, const char *key, long long def);
int ex_ini_get_bool(const ExIni *ini, const char *section, const char *key, int def); /* true, false, yes, no, on, off, 1, 0 */
double ex_ini_get_float(const ExIni *ini, const char *section, const char *key, double def);

/* Changes one value in the text of an INI file and leaves everything else byte for byte as it was:
 * comments, blank lines, the order of sections and keys, keys this version does not know, line
 * endings (LF or CRLF, as the file already uses) and a byte-order mark. An existing key keeps its
 * line (a trailing " # comment" stays); a new key goes at the end of its section; a new section goes
 * at the end of the file. Returns a malloc'ed NUL terminated text, or NULL when out of memory. */
char *ex_ini_patch(const char *text, size_t len, const char *section, const char *key, const char *value, size_t *out_len);

/* A value INI cannot hold as it is (INI trims a value, ends it at " #" and has one line for it): `%` and `#`, every byte under 0x20 and 0x7F, and a space at either
 * end are written as %XX; the reader decodes a valid %XX and leaves any other `%` as it is. ex_ini_encode_value returns a malloc'ed text (length in *outlen);
 * ex_ini_decode_value writes the bytes (and a NUL) into out, which must hold strlen(v) + 1, and returns their number. */
char *ex_ini_encode_value(const char *v, size_t n, size_t *outlen);
size_t ex_ini_decode_value(const char *v, char *out);

#ifdef __cplusplus
}
#endif
#endif
