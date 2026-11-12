/* Small portable helpers. */
#ifndef EXHAUST_UTIL_H
#define EXHAUST_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 of a byte string. */
void ex_sha256(const void *data, size_t len, uint8_t out[32]);
/* Lowercase hex of the first n bytes. out must hold 2 * n + 1 bytes. */
void ex_hex(const uint8_t *bytes, size_t n, char *out);

#ifdef __cplusplus
}
#endif
#endif
