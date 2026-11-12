#include <stdlib.h>
#include <string.h>

#include "exhaust_notes.h"
#include "exhaust_util.h"

int ex_utf8_valid(const char *s, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < len) {
        unsigned c = p[i];
        size_t need;
        uint32_t cp, min;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; min = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; min = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; min = 0x10000; }
        else return 0;
        if (len - i <= need) return 0;
        {
            size_t k;
            for (k = 1; k <= need; k++) {
                if ((p[i + k] & 0xC0) != 0x80) return 0;
                cp = (cp << 6) | (p[i + k] & 0x3F);
            }
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
        i += need + 1;
    }
    return 1;
}

int ex_note_decode(const uint8_t *bytes, size_t len, char **text, size_t *text_len, ExNoteFormat *fmt) {
    size_t start = 0, i, crlf = 0, lf = 0, cr = 0, out = 0;
    char *t;
    ExNoteFormat f;
    memset(&f, 0, sizeof f);
    if (len >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) { f.bom = 1; start = 3; }
    if (!ex_utf8_valid((const char *)bytes + start, len - start)) return -1;
    for (i = start; i < len; i++) {
        if (bytes[i] == '\r') { if (i + 1 < len && bytes[i + 1] == '\n') { crlf++; i++; } else cr++; }
        else if (bytes[i] == '\n') lf++;
    }
    f.eol = (cr > 0 || (crlf > 0 && lf > 0)) ? EX_EOL_MIXED : (crlf > 0 ? EX_EOL_CRLF : EX_EOL_LF);
    t = malloc(len - start + 1);
    if (!t) return -1;
    if (f.eol == EX_EOL_CRLF) {
        for (i = start; i < len; i++) { if (bytes[i] == '\r') continue; t[out++] = (char)bytes[i]; }
    } else {
        memcpy(t, bytes + start, len - start);
        out = len - start;
    }
    t[out] = 0;
    *text = t;
    if (text_len) *text_len = out;
    if (fmt) *fmt = f;
    return 0;
}

uint8_t *ex_note_encode(const char *text, size_t len, const ExNoteFormat *fmt, size_t *out_len) {
    size_t i, n = 0, extra = 0;
    uint8_t *out;
    if (fmt->eol == EX_EOL_CRLF) for (i = 0; i < len; i++) if (text[i] == '\n') extra++;
    out = malloc(len + extra + 4);
    if (!out) return NULL;
    if (fmt->bom) { out[n++] = 0xEF; out[n++] = 0xBB; out[n++] = 0xBF; }
    if (fmt->eol == EX_EOL_CRLF) {
        for (i = 0; i < len; i++) {
            if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') continue; /* already a pair: keep one */
            if (text[i] == '\n') out[n++] = '\r';
            out[n++] = (uint8_t)text[i];
        }
    } else {
        memcpy(out + n, text, len);
        n += len;
    }
    *out_len = n;
    return out;
}

void ex_hash16(const void *bytes, size_t len, uint8_t out[16]) {
    uint8_t d[32];
    ex_sha256(bytes, len, d);
    memcpy(out, d, 16);
}
