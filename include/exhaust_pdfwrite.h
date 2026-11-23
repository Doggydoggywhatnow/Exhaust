/* Exhaust's own PDF writer (sub-slice 6e-iv). It writes a PDF 1.5 file with the objects of one flat cross-reference table: pages whose content streams
 * are deflated, fonts embedded as subsets (the glyphs used, cut with HarfBuzz's subsetter, glyph ids kept) with a ToUnicode map so the text can be
 * selected and searched, images as RGB streams with a soft mask for transparency, vector shapes, and links to web addresses. It draws nothing itself that
 * the caller did not ask for: no background, no colors of its own.
 *
 * Coordinates are in points with the origin at the top left of the page and y growing downwards (the writer turns them into PDF's). Any thread: a
 * document is not shared. See docs/formats.md (Export to PDF). */
#ifndef EXHAUST_PDFWRITE_H
#define EXHAUST_PDFWRITE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExPdfDoc ExPdfDoc;
typedef struct ExPdfColor { float r, g, b; } ExPdfColor;   /* 0 to 1 */

ExPdfDoc *ex_pdfw_create(void);
void ex_pdfw_free(ExPdfDoc *d);
void ex_pdfw_set_title(ExPdfDoc *d, const char *utf8);

/* A font file (TrueType or OpenType; `data` must stay valid until ex_pdfw_finish). `adv(user, glyph)` gives the advance of a glyph in thousandths of the
 * font size, the same numbers the layout used. Returns the font's number, or -1. */
typedef float (*ExPdfAdvanceFn)(void *user, uint32_t glyph);
/* `name` is the PostScript name (letters and digits are kept); ascent and descent are in thousandths of the font size (descent positive). */
int ex_pdfw_font_add(ExPdfDoc *d, const char *name, const unsigned char *data, size_t len, ExPdfAdvanceFn adv, void *user, int ascent, int descent, int fixed_pitch, int italic);

/* Pages: begin one, draw into it, begin the next. */
int ex_pdfw_page_begin(ExPdfDoc *d, float width, float height);
size_t ex_pdfw_page_count(const ExPdfDoc *d);

/* `n` glyphs of font `font` starting at the baseline point (x, y). cps[i] is the Unicode code point glyph i stands for (for the ToUnicode map), 0 for none. */
void ex_pdfw_text(ExPdfDoc *d, int font, float size, float x, float y, const uint32_t *glyphs, const uint32_t *cps, size_t n, ExPdfColor c);
/* A rectangle: fill and/or stroke (NULL for none). Corner radius r (0 for square). */
void ex_pdfw_rect(ExPdfDoc *d, float x, float y, float w, float h, float r, const ExPdfColor *fill, const ExPdfColor *stroke, float stroke_w);
/* A polyline or polygon through `n` points (x, y pairs). dash: 0 solid, 1 dashed, 2 dotted. */
void ex_pdfw_poly(ExPdfDoc *d, const float *pts, size_t n, int closed, const ExPdfColor *fill, const ExPdfColor *stroke, float stroke_w, int dash);
void ex_pdfw_ellipse(ExPdfDoc *d, float cx, float cy, float rx, float ry, const ExPdfColor *fill, const ExPdfColor *stroke, float stroke_w);
/* Clip to a rectangle until ex_pdfw_clip_end. */
void ex_pdfw_clip_begin(ExPdfDoc *d, float x, float y, float w, float h);
void ex_pdfw_clip_end(ExPdfDoc *d);

/* An image from 8-bit RGBA pixels (straight alpha): returns its number, or -1. Drawn any number of times. */
int ex_pdfw_image_add(ExPdfDoc *d, const uint8_t *rgba, int w, int h);
void ex_pdfw_image_draw(ExPdfDoc *d, int image, float x, float y, float w, float h);
/* A link to a web address over a rectangle of the page. */
void ex_pdfw_link(ExPdfDoc *d, float x, float y, float w, float h, const char *uri);

/* Writes the file: *out is malloc'ed (*len bytes). Fonts are cut down here. 0, or -1 (out of memory, no pages, a font that cannot be cut). */
int ex_pdfw_finish(ExPdfDoc *d, unsigned char **out, size_t *len);

/* ---- the structure check (tests and the export's own verification) ---- */
typedef struct ExPdfCheck {
    int ok;                 /* the cross-reference table, trailer and every object it lists are consistent */
    int pages;
    int fonts, fonts_embedded, fonts_subset, fonts_tounicode;
    int images;
    int dark_fills;         /* rectangles of 200 by 200 points or more filled with a dark color (luminance under 0.5): a dark page or panel; an export is light-styled */
    char *text;             /* the text of every page recovered through the fonts' ToUnicode maps, one page after another, malloc'ed */
    char err[160];
} ExPdfCheck;
int ex_pdfw_check(const unsigned char *pdf, size_t len, ExPdfCheck *out);
void ex_pdfw_check_free(ExPdfCheck *c);

#ifdef __cplusplus
}
#endif
#endif
