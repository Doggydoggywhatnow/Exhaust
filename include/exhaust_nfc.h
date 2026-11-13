/* Unicode normalization (NFC) and case folding, for matching link names ("Links match case-insensitively after
 * Unicode (NFC) normalization"). Text is UTF-8; bytes that are not valid UTF-8 are passed through unchanged. */
#ifndef EXHAUST_NFC_H
#define EXHAUST_NFC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* malloc'ed, NUL terminated; NULL only if out of memory. */
char *ex_nfc(const char *s, size_t len);
/* The key two names are compared by: NFC, then full case folding, then NFC again. */
char *ex_name_key(const char *s, size_t len);
#ifdef __cplusplus
}
#endif
#endif
