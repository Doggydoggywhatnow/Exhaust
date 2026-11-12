#include "exhaust_text.h"

uint32_t ex_utf8_decode(const char *s, size_t len, size_t *i) {
    const unsigned char *p = (const unsigned char *)s + *i;
    size_t left = len - *i;
    unsigned c = p[0];
    uint32_t cp;
    size_t need, k;
    uint32_t min;

    if (c < 0x80) { *i += 1; return c; }
    if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; min = 0x10000; }
    else { *i += 1; return 0xFFFD; }

    for (k = 1; k <= need; k++) {
        if (k >= left || (p[k] & 0xC0) != 0x80) { *i += k; return 0xFFFD; }
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    *i += need + 1;
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}

int ex_utf8_encode(uint32_t cp, char *out) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
