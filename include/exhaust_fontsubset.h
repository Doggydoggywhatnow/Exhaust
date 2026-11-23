/* Cutting a font down to the glyphs a document uses (sub-slice 6e-iv), with HarfBuzz's subsetter. The PDF export embeds only what it draws.
 * FreeType cannot write fonts; hb-subset can. Glyph ids are kept (HB_SUBSET_FLAGS_RETAIN_GIDS), so the numbers the page's text uses stay valid and the
 * glyphs that are not kept are empty. Any thread: nothing is shared. */
#ifndef EXHAUST_FONTSUBSET_H
#define EXHAUST_FONTSUBSET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `data` is a TrueType or OpenType file. Keeps the glyphs listed in `glyphs` (and .notdef), and, if `unicodes` is not NULL, the cmap entries of those code
 * points (and the glyphs they map to). The result is malloc'ed (*out_len bytes), or NULL when the font cannot be cut. */
unsigned char *ex_font_subset(const unsigned char *data, size_t len, const uint32_t *glyphs, size_t nglyphs, const uint32_t *unicodes, size_t nunicodes, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
