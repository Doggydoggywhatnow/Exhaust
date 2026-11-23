/* Drawing a diagram (6d-ii): the items of an ExDiagram with the theme's colors. UI thread only. */
#ifndef EXHAUST_DIAGRAMDRAW_H
#define EXHAUST_DIAGRAMDRAW_H

#include "exhaust_diagram.h"
#include "exhaust_render.h"
#include "exhaust_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draws `d` with its top left corner at (x, y) (pixels). `size` is the font size at 100% zoom that one em of the layout is drawn at; the text uses the
 * renderer's built-in interface font (regular, and bold for headings of blocks). */
void ex_diagram_draw(ExRenderer *r, const ExTheme *t, const ExDiagram *d, float size, float x, float y);

#ifdef __cplusplus
}
#endif
#endif
