/* Shared between the text modules; not part of the public API. */
#ifndef EXHAUST_TEXT_INTERNAL_H
#define EXHAUST_TEXT_INTERNAL_H

#include <hb.h>
#include "exhaust_text.h"
#include "exhaust_fontdb.h"

/* HarfBuzz font for a face, scaled in font units (use ex_font_upem). */
hb_font_t *ex_font_hb(ExFont *font);
int ex_font_upem(ExFont *font);
ExFontDb *ex_fontlib_db(ExFontLib *lib);
/* The font for database entry idx, opened on first use; NULL if it cannot be opened. */
ExFont *ex_fontlib_db_font(ExFontLib *lib, int idx);
unsigned ex_font_style_flags(const ExFont *font); /* EX_FONT_FLAG_BOLD | EX_FONT_FLAG_ITALIC */

#endif
