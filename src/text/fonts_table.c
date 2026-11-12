#include "fonts_data.h"

#define FONT(name) extern const unsigned char name[]; extern const unsigned long name##_len;
FONT(ex_font_ui_regular) FONT(ex_font_ui_bold) FONT(ex_font_ui_italic) FONT(ex_font_ui_bold_italic)
FONT(ex_font_mono_regular) FONT(ex_font_mono_bold) FONT(ex_font_mono_italic) FONT(ex_font_mono_bold_italic)

#define CASE(id, name) case id: e.data = name; e.size = (size_t)name##_len; break;

ExEmbeddedFont ex_embedded_font(ExBuiltinFont which) {
    ExEmbeddedFont e = {0, 0};
    switch (which) {
    CASE(EX_FONT_UI_REGULAR, ex_font_ui_regular)
    CASE(EX_FONT_UI_BOLD, ex_font_ui_bold)
    CASE(EX_FONT_UI_ITALIC, ex_font_ui_italic)
    CASE(EX_FONT_UI_BOLD_ITALIC, ex_font_ui_bold_italic)
    CASE(EX_FONT_MONO_REGULAR, ex_font_mono_regular)
    CASE(EX_FONT_MONO_BOLD, ex_font_mono_bold)
    CASE(EX_FONT_MONO_ITALIC, ex_font_mono_italic)
    CASE(EX_FONT_MONO_BOLD_ITALIC, ex_font_mono_bold_italic)
    default: break;
    }
    return e;
}
