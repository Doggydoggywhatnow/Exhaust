/* AVIF (6c-i): a still picture in the ISO base media file format (the `ftyp`, `meta` and `mdat` boxes), its AV1 data decoded by dav1d.
 *
 * What is read: the primary item (`pitm`) of type `av01`, or a `grid` of `av01` tiles; its transparency, an `av01` item that refers to it with `auxl`
 * and has an alpha `auxC`; the properties `ispe` (size), `irot` and `imir`. The colour comes from the AV1 sequence header (matrix, range, bit depth),
 * 8, 10 and 12 bits, 4:0:0, 4:2:0, 4:2:2 and 4:4:4; chroma is repeated, not interpolated. Not built: ICC profiles, HDR tone mapping (PQ and HLG pictures are
 * shown as they are coded), overlays (`iovl`), image sequences (`avis`: the first frame of the primary item is shown), the `clap` crop.
 * Every length and offset of the file is checked before it is used; dav1d is told the largest picture it may make.
 * The decoder is the portable C build of dav1d with one thread: a still picture does not need the assembly or more threads. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dav1d/dav1d.h"
#include "exhaust_image.h"
#include "image_internal.h"

static void seterr(char *err, size_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }

/* ---- boxes ---- */

typedef struct Box { uint32_t type; size_t start, body, end; } Box;     /* body: after the header; end: after the box */
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint64_t rdn(const uint8_t *p, int n) { uint64_t v = 0; int i; for (i = 0; i < n; i++) v = (v << 8) | p[i]; return v; }
#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* The next box in [*pos, end). 1 with the box, 0 at the end or at a box that does not fit. */
static int next_box(const uint8_t *d, size_t end, size_t *pos, Box *b) {
    uint64_t size;
    size_t hdr = 8;
    if (*pos >= end || end - *pos < 8) return 0;
    size = rd32(d + *pos);
    b->type = rd32(d + *pos + 4);
    b->start = *pos;
    if (size == 1) { if (end - *pos < 16) return 0; size = rdn(d + *pos + 8, 8); hdr = 16; }
    else if (size == 0) size = end - *pos;
    if (size < hdr || size > end - *pos) return 0;
    b->body = *pos + hdr; b->end = *pos + (size_t)size;
    *pos = b->end;
    return 1;
}

#define MAX_ITEMS 512
#define MAX_PROPS 256
#define MAX_REFS 4096

typedef struct Item {
    uint32_t id, type;
    size_t off[8], len[8]; int nextents;     /* where its data is in the file (construction method 0) */
    int ext_ok;
    int props[16]; int nprops;               /* property indexes (1-based) */
    int is_alpha;                            /* has an auxC of the alpha kind */
} Item;
typedef struct Prop { uint32_t type; size_t body, end; } Prop;
typedef struct Ref { uint32_t type, from, to; } Ref;
typedef struct Avif {
    const uint8_t *d; size_t len;
    Item items[MAX_ITEMS]; int nitems;
    Prop props[MAX_PROPS]; int nprops;
    Ref refs[MAX_REFS]; int nrefs;
    uint32_t primary; int have_primary;
} Avif;

static Item *find_item(Avif *a, uint32_t id) { int i; for (i = 0; i < a->nitems; i++) if (a->items[i].id == id) return &a->items[i]; return NULL; }
static Item *get_item(Avif *a, uint32_t id) {
    Item *it = find_item(a, id);
    if (it) return it;
    if (a->nitems >= MAX_ITEMS) return NULL;
    it = &a->items[a->nitems++];
    memset(it, 0, sizeof *it);
    it->id = id;
    return it;
}

static void parse_infe(Avif *a, const uint8_t *d, size_t b, size_t e) {
    unsigned ver;
    size_t p = b;
    uint32_t id, type = 0;
    Item *it;
    if (e - b < 4) return;
    ver = d[p]; p += 4;
    if (ver < 2) return;
    if (ver == 2) { if (e - p < 8) return; id = rd16(d + p); p += 2; } else { if (e - p < 10) return; id = rd32(d + p); p += 4; }
    p += 2;                                                                    /* protection index */
    if (e - p < 4) return;
    type = rd32(d + p);
    it = get_item(a, id);
    if (it) it->type = type;
}

static void parse_iloc(Avif *a, const uint8_t *d, size_t b, size_t e) {
    unsigned ver, osz, lsz, bsz, isz;
    size_t p = b;
    uint32_t count, k;
    if (e - p < 8) return;
    ver = d[p]; p += 4;
    osz = d[p] >> 4; lsz = d[p] & 15; p++;
    bsz = d[p] >> 4; isz = (ver == 1 || ver == 2) ? (d[p] & 15) : 0; p++;
    if (ver < 2) { count = rd16(d + p); p += 2; } else { if (e - p < 4) return; count = rd32(d + p); p += 4; }
    if ((osz != 0 && osz != 4 && osz != 8) || (lsz != 0 && lsz != 4 && lsz != 8) || (bsz != 0 && bsz != 4 && bsz != 8) || (isz != 0 && isz != 4 && isz != 8)) return;
    for (k = 0; k < count && k < MAX_ITEMS; k++) {
        uint32_t id;
        unsigned method = 0, ne, x;
        uint64_t base;
        Item *it;
        if (ver < 2) { if (e - p < 2) return; id = rd16(d + p); p += 2; } else { if (e - p < 4) return; id = rd32(d + p); p += 4; }
        if (ver == 1 || ver == 2) { if (e - p < 2) return; method = rd16(d + p) & 15; p += 2; }
        if (e - p < 2 + bsz + 2) return;
        p += 2;                                                                  /* data reference index */
        base = rdn(d + p, (int)bsz); p += bsz;
        ne = rd16(d + p); p += 2;
        it = get_item(a, id);
        if (it) { it->nextents = 0; it->ext_ok = method == 0; }
        for (x = 0; x < ne; x++) {
            uint64_t off, len;
            if (e - p < (size_t)isz + osz + lsz) return;
            p += isz;
            off = rdn(d + p, (int)osz); p += osz;
            len = rdn(d + p, (int)lsz); p += lsz;
            if (it && it->ext_ok) {
                if (it->nextents >= 8 || base + off < base || base + off > a->len || len > a->len - (size_t)(base + off)) it->ext_ok = 0;
                else { it->off[it->nextents] = (size_t)(base + off); it->len[it->nextents] = (size_t)len; it->nextents++; }
            }
        }
    }
}

static void parse_iref(Avif *a, const uint8_t *d, size_t b, size_t e) {
    unsigned ver;
    size_t p = b;
    Box r;
    if (e - p < 4) return;
    ver = d[p]; p += 4;
    while (next_box(d, e, &p, &r)) {
        size_t q = r.body;
        uint32_t from;
        unsigned n, i;
        if (ver == 0) { if (r.end - q < 4) continue; from = rd16(d + q); q += 2; } else { if (r.end - q < 6) continue; from = rd32(d + q); q += 4; }
        n = rd16(d + q); q += 2;
        for (i = 0; i < n; i++) {
            uint32_t to;
            if (ver == 0) { if (r.end - q < 2) break; to = rd16(d + q); q += 2; } else { if (r.end - q < 4) break; to = rd32(d + q); q += 4; }
            if (a->nrefs < MAX_REFS) { a->refs[a->nrefs].type = r.type; a->refs[a->nrefs].from = from; a->refs[a->nrefs].to = to; a->nrefs++; }
        }
    }
}

static void parse_iprp(Avif *a, const uint8_t *d, size_t b, size_t e) {
    size_t p = b;
    Box x;
    while (next_box(d, e, &p, &x)) {
        if (x.type == FOURCC('i', 'p', 'c', 'o')) {
            size_t q = x.body;
            Box pb;
            while (next_box(d, x.end, &q, &pb)) {
                if (a->nprops >= MAX_PROPS) break;
                a->props[a->nprops].type = pb.type; a->props[a->nprops].body = pb.body; a->props[a->nprops].end = pb.end; a->nprops++;
            }
        } else if (x.type == FOURCC('i', 'p', 'm', 'a')) {
            size_t q = x.body;
            unsigned ver, flags;
            uint32_t n, k;
            if (x.end - q < 8) continue;
            ver = d[q]; flags = ((unsigned)d[q + 1] << 16) | ((unsigned)d[q + 2] << 8) | d[q + 3]; q += 4;
            n = rd32(d + q); q += 4;
            for (k = 0; k < n; k++) {
                uint32_t id;
                unsigned cnt, j;
                Item *it;
                if (ver < 1) { if (x.end - q < 3) break; id = rd16(d + q); q += 2; } else { if (x.end - q < 5) break; id = rd32(d + q); q += 4; }
                cnt = d[q++];
                it = find_item(a, id);
                for (j = 0; j < cnt; j++) {
                    unsigned idx;
                    if (flags & 1) { if (x.end - q < 2) break; idx = rd16(d + q) & 0x7fff; q += 2; } else { if (x.end - q < 1) break; idx = d[q] & 0x7f; q++; }
                    if (it && it->nprops < 16) it->props[it->nprops++] = (int)idx;
                }
            }
        }
    }
}

static int has_text(const char *s, size_t n, const char *needle) {
    size_t m = strlen(needle), i;
    for (i = 0; i + m <= n; i++) if (!memcmp(s + i, needle, m)) return 1;
    return 0;
}

static int parse_file(Avif *a, char *err, size_t cap) {
    size_t pos = 0;
    Box b;
    int saw_meta = 0;
    while (next_box(a->d, a->len, &pos, &b)) {
        if (b.type == FOURCC('m', 'e', 't', 'a')) {
            size_t p = b.body + 4, q;
            Box m;
            saw_meta = 1;
            if (b.end - b.body < 4) break;
            while (next_box(a->d, b.end, &p, &m)) {
                if (m.type == FOURCC('p', 'i', 't', 'm')) {
                    if (m.end - m.body >= 6) { unsigned v = a->d[m.body]; a->primary = v == 0 ? rd16(a->d + m.body + 4) : (m.end - m.body >= 8 ? rd32(a->d + m.body + 4) : 0); a->have_primary = 1; }
                } else if (m.type == FOURCC('i', 'i', 'n', 'f')) {
                    unsigned ver = m.end - m.body >= 4 ? a->d[m.body] : 0;
                    Box ie;
                    q = m.body + 4 + (ver == 0 ? 2 : 4);
                    if (q > m.end) continue;
                    while (next_box(a->d, m.end, &q, &ie)) if (ie.type == FOURCC('i', 'n', 'f', 'e')) parse_infe(a, a->d, ie.body, ie.end);
                } else if (m.type == FOURCC('i', 'l', 'o', 'c')) parse_iloc(a, a->d, m.body, m.end);
                else if (m.type == FOURCC('i', 'r', 'e', 'f')) parse_iref(a, a->d, m.body, m.end);
                else if (m.type == FOURCC('i', 'p', 'r', 'p')) parse_iprp(a, a->d, m.body, m.end);
            }
        }
    }
    if (!saw_meta || !a->have_primary) { seterr(err, cap, "this AVIF file has no picture in it"); return -1; }
    {   /* which items are alpha: those with an auxC of the alpha kind */
        int i, j;
        for (i = 0; i < a->nitems; i++)
            for (j = 0; j < a->items[i].nprops; j++) {
                int pi = a->items[i].props[j];
                if (pi >= 1 && pi <= a->nprops && a->props[pi - 1].type == FOURCC('a', 'u', 'x', 'C')) {
                    const Prop *pr = &a->props[pi - 1];
                    const char *s = (const char *)a->d + pr->body + 4;
                    size_t n = pr->end > pr->body + 4 ? pr->end - pr->body - 4 : 0;
                    if (n >= 8 && (has_text(s, n, "auxiliary:alpha") || has_text(s, n, "auxid:1"))) a->items[i].is_alpha = 1;
                }
            }
    }
    return 0;
}

static const Prop *item_prop(Avif *a, const Item *it, uint32_t type) {
    int j;
    for (j = 0; j < it->nprops; j++) { int pi = it->props[j]; if (pi >= 1 && pi <= a->nprops && a->props[pi - 1].type == type) return &a->props[pi - 1]; }
    return NULL;
}

static int item_size(Avif *a, const Item *it, int *w, int *h) {
    const Prop *p = item_prop(a, it, FOURCC('i', 's', 'p', 'e'));
    if (!p || p->end - p->body < 12) return 0;
    *w = (int)rd32(a->d + p->body + 4); *h = (int)rd32(a->d + p->body + 8);
    return *w > 0 && *h > 0;
}

/* The item's data (its extents joined). malloc'ed. */
static uint8_t *item_data(Avif *a, const Item *it, size_t *len) {
    size_t total = 0, o = 0;
    int i;
    uint8_t *buf;
    if (!it->ext_ok || !it->nextents) return NULL;
    for (i = 0; i < it->nextents; i++) total += it->len[i];
    buf = malloc(total ? total : 1);
    if (!buf) return NULL;
    for (i = 0; i < it->nextents; i++) { memcpy(buf + o, a->d + it->off[i], it->len[i]); o += it->len[i]; }
    *len = total;
    return buf;
}

/* ---- dav1d and the colour ---- */

static void no_free(const uint8_t *b, void *c) { (void)b; (void)c; }

static Dav1dPicture *decode_av1(const uint8_t *data, size_t len, char *err, size_t cap) {
    Dav1dSettings s;
    Dav1dContext *c = NULL;
    Dav1dData in;
    Dav1dPicture *pic = calloc(1, sizeof *pic);
    int got = 0, guard = 0;
    if (!pic) { seterr(err, cap, "out of memory"); return NULL; }
    dav1d_default_settings(&s);
    s.n_threads = 1; s.max_frame_delay = 1; s.frame_size_limit = EX_IMAGE_MAX_PIXELS; s.all_layers = 0; s.logger.callback = NULL;
    if (dav1d_open(&c, &s) < 0) { free(pic); seterr(err, cap, "the AV1 decoder could not start"); return NULL; }
    if (dav1d_data_wrap(&in, data, len, no_free, NULL) < 0) { dav1d_close(&c); free(pic); seterr(err, cap, "out of memory"); return NULL; }
    while (!got && guard++ < 1000) {
        int r;
        if (in.sz > 0) { r = dav1d_send_data(c, &in); if (r < 0 && r != DAV1D_ERR(EAGAIN)) { seterr(err, cap, "this AVIF picture is damaged"); break; } }
        r = dav1d_get_picture(c, pic);
        if (r == 0) { got = 1; break; }
        if (r != DAV1D_ERR(EAGAIN)) { seterr(err, cap, "this AVIF picture is damaged"); break; }
        if (in.sz == 0) { seterr(err, cap, "this AVIF picture is incomplete"); break; }
    }
    if (in.sz > 0 || in.data) dav1d_data_unref(&in);
    dav1d_close(&c);
    if (!got) { free(pic); return NULL; }
    return pic;
}

static void free_pic(Dav1dPicture *p) { if (p) { dav1d_picture_unref(p); free(p); } }

typedef struct Tile { int w, h; uint8_t *px; } Tile;    /* RGBA, or one byte a pixel for an alpha plane */

static inline int clamp255(float v) { return v < 0.0f ? 0 : v > 255.0f ? 255 : (int)(v + 0.5f); }

static inline unsigned sample(const Dav1dPicture *p, int plane, int x, int y) {
    ptrdiff_t stride = p->stride[plane ? 1 : 0];
    const uint8_t *row = (const uint8_t *)p->data[plane] + (ptrdiff_t)y * stride;
    return p->p.bpc == 8 ? row[x] : ((const uint16_t *)row)[x];
}

/* One decoded AV1 picture as RGBA (alpha 255), or as an alpha plane (the luma, scaled to 0..255). */
static int picture_to_tile(const Dav1dPicture *p, int alpha_plane, Tile *t, char *err, size_t cap) {
    int w = p->p.w, h = p->p.h, x, y, bpc = p->p.bpc, maxv = (1 << bpc) - 1;
    const Dav1dSequenceHeader *sh = p->seq_hdr;
    int full = sh ? sh->color_range : 0, mono = p->p.layout == DAV1D_PIXEL_LAYOUT_I400;
    int sx = p->p.layout == DAV1D_PIXEL_LAYOUT_I444 ? 0 : 1, sy = p->p.layout == DAV1D_PIXEL_LAYOUT_I420 ? 1 : 0;
    float kr = 0.299f, kb = 0.114f, ys, yo, cs, co;
    int identity = 0;
    unsigned mtrx = sh ? (unsigned)sh->mtrx : 2;
    if (w <= 0 || h <= 0 || (long long)w * h > EX_IMAGE_MAX_PIXELS) { seterr(err, cap, "the picture is too big"); return -1; }
    t->w = w; t->h = h;
    t->px = malloc((size_t)w * (size_t)h * (alpha_plane ? 1 : 4));
    if (!t->px) { seterr(err, cap, "out of memory"); return -1; }
    if (mtrx == DAV1D_MC_BT709) { kr = 0.2126f; kb = 0.0722f; }
    else if (mtrx == DAV1D_MC_BT2020_NCL || mtrx == DAV1D_MC_BT2020_CL) { kr = 0.2627f; kb = 0.0593f; }
    else if (mtrx == DAV1D_MC_IDENTITY) identity = 1;
    {   /* the range: limited puts black at 16 and white at 235 (a luma of 219 steps, a chroma of 224), scaled to the bit depth */
        float sc = (float)(1 << (bpc - 8));
        if (full) { ys = 255.0f / (float)maxv; yo = 0.0f; cs = 255.0f / (float)maxv; co = 128.0f * sc; }
        else { ys = 255.0f / (219.0f * sc); yo = 16.0f * sc; cs = 255.0f / (224.0f * sc); co = 128.0f * sc; }
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned Y = sample(p, 0, x, y);
            if (alpha_plane) {
                float v = full ? (float)Y * 255.0f / (float)maxv : ((float)Y - yo) * ys;
                t->px[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)clamp255(v);
            } else {
                uint8_t *o = t->px + ((size_t)y * (size_t)w + (size_t)x) * 4;
                if (mono) { int g = clamp255(full ? (float)Y * ys : ((float)Y - yo) * ys); o[0] = o[1] = o[2] = (uint8_t)g; }
                else {
                    unsigned U = sample(p, 1, x >> sx, y >> sy), V = sample(p, 2, x >> sx, y >> sy);
                    if (identity) { o[0] = (uint8_t)clamp255((float)V * 255.0f / (float)maxv); o[1] = (uint8_t)clamp255((float)Y * 255.0f / (float)maxv); o[2] = (uint8_t)clamp255((float)U * 255.0f / (float)maxv); }
                    else {
                        float yy = ((float)Y - yo) * ys, cb = ((float)U - co) * cs, cr = ((float)V - co) * cs;
                        float kg = 1.0f - kr - kb;
                        float r = yy + 2.0f * (1.0f - kr) * cr, b = yy + 2.0f * (1.0f - kb) * cb;
                        float g = (yy - kr * r - kb * b) / kg;
                        o[0] = (uint8_t)clamp255(r); o[1] = (uint8_t)clamp255(g); o[2] = (uint8_t)clamp255(b);
                    }
                }
                o[3] = 255;
            }
        }
    return 0;
}

static int decode_item(Avif *a, const Item *it, int alpha_plane, int depth, Tile *t, char *err, size_t cap);

/* A grid of tiles (its `dimg` references, in order), laid out rows by columns. */
static int decode_grid(Avif *a, const Item *it, int alpha_plane, int depth, Tile *t, char *err, size_t cap) {
    size_t dlen = 0;
    uint8_t *gd = item_data(a, it, &dlen);
    unsigned rows, cols, flags;
    unsigned long ow, oh;
    uint32_t tiles[256];
    int nt = 0, i, bpp = alpha_plane ? 1 : 4, tx = 0, ty = 0, tw = 0, th = 0;
    uint8_t *canvas = NULL;
    if (depth > 0) { free(gd); seterr(err, cap, "this AVIF picture has a grid inside a grid"); return -1; }
    if (!gd || dlen < 8) { free(gd); seterr(err, cap, "this AVIF grid is damaged"); return -1; }
    flags = gd[1]; rows = (unsigned)gd[2] + 1; cols = (unsigned)gd[3] + 1;
    if (flags & 1) { if (dlen < 12) { free(gd); seterr(err, cap, "this AVIF grid is damaged"); return -1; } ow = rd32(gd + 4); oh = rd32(gd + 8); }
    else { ow = rd16(gd + 4); oh = rd16(gd + 6); }
    free(gd);
    if (ow == 0 || oh == 0 || ow > EX_IMAGE_MAX_SIDE || oh > EX_IMAGE_MAX_SIDE || ow * oh > EX_IMAGE_MAX_PIXELS || rows * cols > 256) { seterr(err, cap, "this AVIF picture is too big"); return -1; }
    for (i = 0; i < a->nrefs && nt < 256; i++) if (a->refs[i].type == FOURCC('d', 'i', 'm', 'g') && a->refs[i].from == it->id) tiles[nt++] = a->refs[i].to;
    if ((unsigned)nt != rows * cols) { seterr(err, cap, "this AVIF grid is damaged"); return -1; }
    canvas = calloc((size_t)ow * (size_t)oh, (size_t)bpp);
    if (!canvas) { seterr(err, cap, "out of memory"); return -1; }
    if (!alpha_plane) { size_t k; for (k = 0; k < (size_t)ow * oh; k++) canvas[k * 4 + 3] = 255; }
    for (i = 0; i < nt; i++) {
        const Item *ti = find_item(a, tiles[i]);
        Tile tile;
        unsigned r = (unsigned)i / cols, c = (unsigned)i % cols;
        int yy;
        if (!ti || ti->type != FOURCC('a', 'v', '0', '1') || decode_item(a, ti, alpha_plane, depth + 1, &tile, err, cap) != 0) { free(canvas); return -1; }
        if (i == 0) { tw = tile.w; th = tile.h; }
        tx = (int)(c * (unsigned)tw); ty = (int)(r * (unsigned)th);
        for (yy = 0; yy < tile.h && ty + yy < (int)oh; yy++) {
            int n = tile.w;
            if (tx + n > (int)ow) n = (int)ow - tx;
            if (n > 0) memcpy(canvas + ((size_t)(ty + yy) * ow + (size_t)tx) * (size_t)bpp, tile.px + (size_t)yy * (size_t)tile.w * (size_t)bpp, (size_t)n * (size_t)bpp);
        }
        free(tile.px);
    }
    t->w = (int)ow; t->h = (int)oh; t->px = canvas;
    return 0;
}

static int decode_item(Avif *a, const Item *it, int alpha_plane, int depth, Tile *t, char *err, size_t cap) {
    size_t len = 0;
    uint8_t *data;
    Dav1dPicture *pic;
    int rc;
    if (it->type == FOURCC('g', 'r', 'i', 'd')) return decode_grid(a, it, alpha_plane, depth, t, err, cap);
    if (it->type != FOURCC('a', 'v', '0', '1')) { seterr(err, cap, "this AVIF picture is not made of AV1 data"); return -1; }
    data = item_data(a, it, &len);
    if (!data) { seterr(err, cap, "this AVIF picture's data is missing or damaged"); return -1; }
    pic = decode_av1(data, len, err, cap);
    if (!pic) { free(data); return -1; }
    rc = picture_to_tile(pic, alpha_plane, t, err, cap);
    free_pic(pic);
    free(data);                                                       /* dav1d is done with it: the picture was copied */
    return rc;
}

/* irot (an angle in quarter turns counter-clockwise) and imir (0: flip top to bottom, 1: left to right), applied in that order after the picture is made */
static void transform(uint8_t **px, int *w, int *h, int rot, int mirror) {
    int W = *w, H = *h, x, y;
    uint8_t *src = *px, *dst;
    if (rot) {
        int nw = (rot & 1) ? H : W, nh = (rot & 1) ? W : H;
        dst = malloc((size_t)nw * (size_t)nh * 4);
        if (!dst) return;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int dx, dy;
                if (rot == 1) { dx = y; dy = W - 1 - x; } else if (rot == 2) { dx = W - 1 - x; dy = H - 1 - y; } else { dx = H - 1 - y; dy = x; }
                memcpy(dst + ((size_t)dy * (size_t)nw + (size_t)dx) * 4, src + ((size_t)y * (size_t)W + (size_t)x) * 4, 4);
            }
        free(src); src = dst; *px = dst; *w = nw; *h = nh; W = nw; H = nh;
    }
    if (mirror >= 0) {
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int mx = mirror == 1 ? W - 1 - x : x, my = mirror == 0 ? H - 1 - y : y;
                if ((mirror == 1 && x < mx) || (mirror == 0 && y < my)) {
                    uint8_t tmp[4];
                    uint8_t *p1 = src + ((size_t)y * (size_t)W + (size_t)x) * 4, *p2 = src + ((size_t)my * (size_t)W + (size_t)mx) * 4;
                    memcpy(tmp, p1, 4); memcpy(p1, p2, 4); memcpy(p2, tmp, 4);
                }
            }
    }
}

static int open_avif(Avif *a, const void *data, size_t len, const Item **primary, char *err, size_t cap) {
    memset(a, 0, sizeof *a);
    a->d = data; a->len = len;
    if (len < 16) { seterr(err, cap, "this AVIF file is too short"); return -1; }
    if (parse_file(a, err, cap) != 0) return -1;
    *primary = find_item(a, a->primary);
    if (!*primary || ((*primary)->type != FOURCC('a', 'v', '0', '1') && (*primary)->type != FOURCC('g', 'r', 'i', 'd'))) { seterr(err, cap, "this AVIF picture is of a kind that is not built (overlay or sequence)"); return -1; }
    return 0;
}

int ex_avif_info(const void *data, size_t len, int *w, int *h, char *err, size_t errcap) {
    Avif *a = malloc(sizeof *a);
    const Item *it;
    int ok = -1, iw = 0, ih = 0;
    if (!a) { seterr(err, errcap, "out of memory"); return -1; }
    if (open_avif(a, data, len, &it, err, errcap) == 0) {
        if (!item_size(a, it, &iw, &ih)) seterr(err, errcap, "this AVIF picture has no size");
        else {
            const Prop *r = item_prop(a, it, FOURCC('i', 'r', 'o', 't'));
            if (r && r->end > r->body && (a->d[r->body] & 1)) { int t = iw; iw = ih; ih = t; }
            *w = iw; *h = ih; ok = 0;
        }
    }
    free(a);
    return ok;
}

int ex_avif_decode(const void *data, size_t len, uint8_t **rgba, int *w, int *h, char *err, size_t errcap) {
    Avif *a = malloc(sizeof *a);
    const Item *it;
    Tile color;
    int i, rot = 0, mirror = -1, iw = 0, ih = 0, rc = -1;
    if (!a) { seterr(err, errcap, "out of memory"); return -1; }
    if (open_avif(a, data, len, &it, err, errcap) != 0) { free(a); return -1; }
    if (item_size(a, it, &iw, &ih) && (iw > EX_IMAGE_MAX_SIDE || ih > EX_IMAGE_MAX_SIDE || (long long)iw * ih > EX_IMAGE_MAX_PIXELS)) { seterr(err, errcap, "this AVIF picture is too big"); free(a); return -1; }
    if (decode_item(a, it, 0, 0, &color, err, errcap) != 0) { free(a); return -1; }
    for (i = 0; i < a->nrefs; i++) {                                                  /* an alpha item that refers to the picture */
        const Item *al;
        if (a->refs[i].type != FOURCC('a', 'u', 'x', 'l') || a->refs[i].to != it->id) continue;
        al = find_item(a, a->refs[i].from);
        if (al && al->is_alpha) {
            Tile at;
            char e2[100];
            if (decode_item(a, al, 1, 0, &at, e2, sizeof e2) == 0) {
                if (at.w == color.w && at.h == color.h) { size_t k; for (k = 0; k < (size_t)color.w * (size_t)color.h; k++) color.px[k * 4 + 3] = at.px[k]; }
                free(at.px);
            }
            break;
        }
    }
    {
        const Prop *r = item_prop(a, it, FOURCC('i', 'r', 'o', 't')), *m = item_prop(a, it, FOURCC('i', 'm', 'i', 'r'));
        if (r && r->end > r->body) rot = a->d[r->body] & 3;
        if (m && m->end > m->body) mirror = a->d[m->body] & 1;
    }
    transform(&color.px, &color.w, &color.h, rot, mirror);
    *rgba = color.px; *w = color.w; *h = color.h; rc = 0;
    free(a);
    return rc;
}
