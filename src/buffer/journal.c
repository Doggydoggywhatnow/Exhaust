#include <stdlib.h>
#include <string.h>

#include "exhaust_buffer.h"

/* ---- CRC-32 (IEEE 802.3, reflected, the one zlib uses) ---- */

uint32_t ex_crc32(uint32_t crc, const void *data, size_t len) {
    static uint32_t table[256];
    static int ready;
    const uint8_t *p = data;
    size_t i;
    if (!ready) {
        uint32_t n, k;
        for (n = 0; n < 256; n++) {
            uint32_t c = n;
            for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ---- byte buffer ---- */

void ex_bytes_free(ExBytes *b) { free(b->data); memset(b, 0, sizeof *b); }

static int put(ExBytes *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        uint8_t *nd;
        while (nc < b->len + n) nc *= 2;
        nd = realloc(b->data, nc);
        if (!nd) return -1;
        b->data = nd;
        b->cap = nc;
    }
    if (n) memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

static void le16(uint8_t *o, uint16_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *o, uint32_t v) { int i; for (i = 0; i < 4; i++) o[i] = (uint8_t)(v >> (8 * i)); }
static void le64(uint8_t *o, uint64_t v) { int i; for (i = 0; i < 8; i++) o[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

int ex_journal_write_header(ExBytes *out) {
    uint8_t h[8] = {'E', 'X', 'J', '1', 0, 0, 0, 0};
    le32(h + 4, 1);
    return put(out, h, 8);
}

/* body = type, timestamp, path_length, path, <fields>; the record is
 * length (u32) + body + crc32(body) where length counts the body and the checksum. */
static int write_record(ExBytes *out, const uint8_t *body, size_t body_len) {
    uint8_t head[4], tail[4];
    if (body_len + 4 > 0xFFFFFFFFu) return -1;
    le32(head, (uint32_t)(body_len + 4));
    le32(tail, ex_crc32(0, body, body_len));
    if (put(out, head, 4) || put(out, body, body_len) || put(out, tail, 4)) return -1;
    return 0;
}

int ex_journal_write_edit(ExBytes *out, uint64_t ts, const char *path, uint64_t offset, uint64_t removed,
                          const char *inserted, size_t inserted_len) {
    size_t plen = strlen(path), blen;
    uint8_t *body;
    int rc;
    if (plen > 0xFFFF || inserted_len > 0xFFFFFFFFu) return -1;
    blen = 1 + 8 + 2 + plen + 8 + 8 + 4 + inserted_len;
    body = malloc(blen);
    if (!body) return -1;
    body[0] = 'E';
    le64(body + 1, ts);
    le16(body + 9, (uint16_t)plen);
    memcpy(body + 11, path, plen);
    le64(body + 11 + plen, offset);
    le64(body + 19 + plen, removed);
    le32(body + 27 + plen, (uint32_t)inserted_len);
    if (inserted_len) memcpy(body + 31 + plen, inserted, inserted_len);
    rc = write_record(out, body, blen);
    free(body);
    return rc;
}

int ex_journal_write_mark(ExBytes *out, char type, uint64_t ts, const char *path, uint64_t size, const uint8_t hash16[16]) {
    size_t plen = strlen(path), blen;
    uint8_t *body;
    int rc;
    if ((type != 'B' && type != 'S') || plen > 0xFFFF) return -1;
    blen = 1 + 8 + 2 + plen + 8 + 16;
    body = malloc(blen);
    if (!body) return -1;
    body[0] = (uint8_t)type;
    le64(body + 1, ts);
    le16(body + 9, (uint16_t)plen);
    memcpy(body + 11, path, plen);
    le64(body + 11 + plen, size);
    memcpy(body + 19 + plen, hash16, 16);
    rc = write_record(out, body, blen);
    free(body);
    return rc;
}

int ex_journal_check_header(const uint8_t *data, size_t len) {
    if (len < 8 || memcmp(data, "EXJ1", 4) != 0 || rd32(data + 4) != 1) return -1;
    return 0;
}

int ex_journal_next(const uint8_t *data, size_t len, size_t *pos, ExJournalRecord *rec) {
    size_t p = *pos, blen, plen;
    const uint8_t *body;
    if (*pos == 0 && ex_journal_check_header(data, len)) return -1;
    if (p == 0) p = 8;
    if (p >= len) return 0;
    if (len - p < 4) return 0;
    blen = rd32(data + p); /* body + checksum */
    if (blen < 4 + 1 + 8 + 2 || blen > len - p - 4) return 0;
    body = data + p + 4;
    blen -= 4;
    if (rd32(body + blen) != ex_crc32(0, body, blen)) return 0;
    memset(rec, 0, sizeof *rec);
    rec->type = (char)body[0];
    rec->timestamp_ms = rd64(body + 1);
    plen = rd16(body + 9);
    if (11 + plen > blen) return 0;
    rec->path = (const char *)body + 11;
    rec->path_len = plen;
    if (rec->type == 'E') {
        size_t q = 11 + plen;
        if (q + 20 > blen) return 0;
        rec->offset = rd64(body + q);
        rec->removed = rd64(body + q + 8);
        rec->inserted_len = rd32(body + q + 16);
        if (q + 20 + rec->inserted_len != blen) return 0;
        rec->inserted = (const char *)body + q + 20;
    } else if (rec->type == 'B' || rec->type == 'S') {
        size_t q = 11 + plen;
        if (q + 24 > blen) return 0;
        rec->base_size = rd64(body + q);
        memcpy(rec->base_hash, body + q + 8, 16);
    } /* any other type is skipped by the caller: it is returned with its type set */
    *pos = p + 4 + blen + 4;
    return 1;
}

int ex_journal_replay(const uint8_t *data, size_t len, const char *path, ExBuffer *buf, size_t *applied) {
    size_t pos = 0, plen = strlen(path), last_saved_end = 8, n = 0;
    ExJournalRecord r;
    int rc;
    if (applied) *applied = 0;
    if (ex_journal_check_header(data, len)) return -1;
    /* pass 1: find where the last saved record for this path ends */
    pos = 8;
    while ((rc = ex_journal_next(data, len, &pos, &r)) == 1)
        if (r.type == 'S' && r.path_len == plen && memcmp(r.path, path, plen) == 0) last_saved_end = pos;
    /* pass 2: apply this path's edits after it */
    pos = last_saved_end;
    while ((rc = ex_journal_next(data, len, &pos, &r)) == 1) {
        if (r.type != 'E' || r.path_len != plen || memcmp(r.path, path, plen) != 0) continue;
        if (r.offset > ex_buffer_size(buf) || r.removed > ex_buffer_size(buf) - r.offset) return -1;
        if (ex_buffer_delete(buf, (size_t)r.offset, (size_t)r.removed) ||
            ex_buffer_insert(buf, (size_t)r.offset, r.inserted, r.inserted_len))
            return -1;
        n++;
    }
    if (applied) *applied = n;
    return 0;
}
