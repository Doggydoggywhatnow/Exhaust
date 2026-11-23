#include "exhaust_fontsubset.h"

#include <stdlib.h>
#include <string.h>

#include <hb.h>
#include <hb-subset.h>

unsigned char *ex_font_subset(const unsigned char *data, size_t len, const uint32_t *glyphs, size_t nglyphs, const uint32_t *unicodes, size_t nunicodes, size_t *out_len) {
    hb_blob_t *blob, *outblob;
    hb_face_t *face, *sub;
    hb_subset_input_t *in;
    unsigned char *out = NULL;
    const char *bytes;
    unsigned n = 0, i;
    if (out_len) *out_len = 0;
    if (!data || len == 0 || len > 0x7fffffffu) return NULL;
    blob = hb_blob_create((const char *)data, (unsigned)len, HB_MEMORY_MODE_READONLY, NULL, NULL);
    face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    if (!face || hb_face_get_glyph_count(face) == 0) { hb_face_destroy(face); return NULL; }
    in = hb_subset_input_create_or_fail();
    if (!in) { hb_face_destroy(face); return NULL; }
    hb_subset_input_set_flags(in, HB_SUBSET_FLAGS_RETAIN_GIDS | HB_SUBSET_FLAGS_NO_HINTING | HB_SUBSET_FLAGS_NOTDEF_OUTLINE);
    hb_set_add(hb_subset_input_glyph_set(in), 0);
    for (i = 0; i < nglyphs; i++) hb_set_add(hb_subset_input_glyph_set(in), glyphs[i]);
    for (i = 0; unicodes && i < nunicodes; i++) hb_set_add(hb_subset_input_unicode_set(in), unicodes[i]);
    sub = hb_subset_or_fail(face, in);
    hb_subset_input_destroy(in);
    hb_face_destroy(face);
    if (!sub) return NULL;
    outblob = hb_face_reference_blob(sub);
    bytes = hb_blob_get_data(outblob, &n);
    if (bytes && n > 0) {
        out = malloc(n);
        if (out) { memcpy(out, bytes, n); if (out_len) *out_len = n; }
    }
    hb_blob_destroy(outblob);
    hb_face_destroy(sub);
    return out;
}
