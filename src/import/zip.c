#include "exhaust_import.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_ENTRY (64u * 1024u * 1024u)
#define MAX_ENTRIES 20000

typedef struct ZEntry { char *name; uint32_t method, csize, usize, offset; int is_dir; } ZEntry;
struct ExZip { const unsigned char *d; size_t len; ZEntry *e; size_t n; };

static uint32_t u16(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t u32(const unsigned char *p) { return u16(p) | (u16(p + 2) << 16); }

static int safe_name(const char *n) {
    size_t i, len = strlen(n);
    if (len == 0 || n[0] == '/' || strchr(n, '\\') || (len >= 2 && n[1] == ':')) return 0;
    for (i = 0; i < len; i++) {
        if ((unsigned char)n[i] < 32) return 0;
        if ((i == 0 || n[i - 1] == '/') && n[i] == '.' && n[i + 1] == '.' && (n[i + 2] == '/' || n[i + 2] == 0)) return 0;
    }
    return 1;
}

ExZip *ex_zip_open(const unsigned char *data, size_t len) {
    ExZip *z;
    size_t i, eocd = (size_t)-1, count, cd_off, cd_size, pos;
    if (!data || len < 22) return NULL;
    for (i = len - 22 + 1; i-- > 0 && len - i <= 22 + 65535;) if (!memcmp(data + i, "PK\x05\x06", 4)) { eocd = i; break; }
    if (eocd == (size_t)-1) return NULL;
    count = u16(data + eocd + 10); cd_size = u32(data + eocd + 12); cd_off = u32(data + eocd + 16);
    if (count > MAX_ENTRIES || cd_off > len || cd_size > len - cd_off) return NULL;
    z = calloc(1, sizeof *z);
    if (!z) return NULL;
    z->d = data; z->len = len;
    z->e = calloc(count ? count : 1, sizeof *z->e);
    if (!z->e) { free(z); return NULL; }
    pos = cd_off;
    for (i = 0; i < count; i++) {
        size_t nl, el, cl;
        ZEntry *e = &z->e[z->n];
        if (pos + 46 > cd_off + cd_size || memcmp(data + pos, "PK\x01\x02", 4)) break;
        nl = u16(data + pos + 28); el = u16(data + pos + 30); cl = u16(data + pos + 32);
        if (pos + 46 + nl + el + cl > cd_off + cd_size) break;
        e->method = u16(data + pos + 10); e->csize = u32(data + pos + 20); e->usize = u32(data + pos + 24); e->offset = u32(data + pos + 42);
        e->name = malloc(nl + 1);
        if (!e->name) break;
        memcpy(e->name, data + pos + 46, nl); e->name[nl] = 0;
        e->is_dir = nl > 0 && e->name[nl - 1] == '/';
        if (safe_name(e->name)) z->n++; else free(e->name);
        pos += 46 + nl + el + cl;
    }
    return z;
}

void ex_zip_close(ExZip *z) {
    size_t i;
    if (!z) return;
    for (i = 0; i < z->n; i++) free(z->e[i].name);
    free(z->e); free(z);
}
size_t ex_zip_count(const ExZip *z) { return z ? z->n : 0; }
const char *ex_zip_name(const ExZip *z, size_t i) { return z && i < z->n ? z->e[i].name : NULL; }
int ex_zip_is_dir(const ExZip *z, size_t i) { return z && i < z->n && z->e[i].is_dir; }

unsigned char *ex_zip_extract(const ExZip *z, size_t i, size_t *len) {
    const ZEntry *e;
    const unsigned char *h, *src;
    size_t nl, el;
    unsigned char *out;
    if (len) *len = 0;
    if (!z || i >= z->n) return NULL;
    e = &z->e[i];
    if (e->is_dir || e->usize > MAX_ENTRY || (size_t)e->offset + 30 > z->len) return NULL;
    h = z->d + e->offset;
    if (memcmp(h, "PK\x03\x04", 4)) return NULL;
    nl = u16(h + 26); el = u16(h + 28);
    if ((size_t)e->offset + 30 + nl + el + e->csize > z->len) return NULL;
    src = h + 30 + nl + el;
    out = malloc((size_t)e->usize + 1);
    if (!out) return NULL;
    if (e->method == 0) {
        if (e->csize != e->usize) { free(out); return NULL; }
        memcpy(out, src, e->usize);
    } else if (e->method == 8) {
        z_stream zs;
        int rc;
        memset(&zs, 0, sizeof zs);
        if (inflateInit2(&zs, -15) != Z_OK) { free(out); return NULL; }
        zs.next_in = (Bytef *)src; zs.avail_in = e->csize; zs.next_out = out; zs.avail_out = e->usize;
        rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != e->usize) { free(out); return NULL; }
    } else { free(out); return NULL; }
    out[e->usize] = 0;
    if (len) *len = e->usize;
    return out;
}
