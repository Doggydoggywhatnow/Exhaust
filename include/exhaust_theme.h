/* Theme tokens: every color, size and font name the interface uses comes from
 * a theme, never from a literal in the code. A theme is an INI file (format in
 * docs/formats.md, "themes"); the four built-in themes are compiled in from
 * data/themes (the four .theme files). A theme file layers over its base theme, so a file need
 * only list what it changes; keys this version does not know are kept so a
 * theme saved by a newer version loses nothing. */
#ifndef EXHAUST_THEME_H
#define EXHAUST_THEME_H

#include <stddef.h>
#include "exhaust_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* X(identifier, "name in the theme file"): the order is the order of ExColorToken. */
#define EX_COLOR_TOKENS(X) \
    X(BACKGROUND, "background") \
    X(BACKGROUND_ALT, "background_alt") \
    X(SURFACE, "surface") \
    X(SURFACE_HOVER, "surface_hover") \
    X(BORDER, "border") \
    X(TEXT, "text") \
    X(TEXT_MUTED, "text_muted") \
    X(TEXT_FAINT, "text_faint") \
    X(ACCENT, "accent") \
    X(ACCENT_HOVER, "accent_hover") \
    X(LINK, "link") \
    X(LINK_UNRESOLVED, "link_unresolved") \
    X(SELECTION, "selection") \
    X(CURSOR, "cursor") \
    X(HIGHLIGHT, "highlight") \
    X(ERROR, "error") \
    X(WARNING, "warning") \
    X(SUCCESS, "success") \
    X(CODE_BACKGROUND, "code_background") \
    X(CODE_TEXT, "code_text") \
    X(TAG, "tag") \
    X(GRAPH_NODE, "graph_node") \
    X(GRAPH_NODE_UNRESOLVED, "graph_node_unresolved") \
    X(GRAPH_LINK, "graph_link") \
    X(GRAPH_FOCUS, "graph_focus") \
    X(CANVAS_GRID, "canvas_grid") \
    X(CALLOUT_NOTE, "callout_note") \
    X(CALLOUT_ABSTRACT, "callout_abstract") \
    X(CALLOUT_INFO, "callout_info") \
    X(CALLOUT_TODO, "callout_todo") \
    X(CALLOUT_TIP, "callout_tip") \
    X(CALLOUT_SUCCESS, "callout_success") \
    X(CALLOUT_QUESTION, "callout_question") \
    X(CALLOUT_WARNING, "callout_warning") \
    X(CALLOUT_FAILURE, "callout_failure") \
    X(CALLOUT_DANGER, "callout_danger") \
    X(CALLOUT_BUG, "callout_bug") \
    X(CALLOUT_EXAMPLE, "callout_example") \
    X(CALLOUT_QUOTE, "callout_quote")

typedef enum ExColorToken {
#define X(id, name) EX_COLOR_##id,
    EX_COLOR_TOKENS(X)
#undef X
    EX_COLOR_COUNT
} ExColorToken;

typedef enum ExSizeToken {
    EX_SIZE_RADIUS,
    EX_SIZE_RADIUS_LARGE,
    EX_SIZE_SPACING,
    EX_SIZE_BORDER_WIDTH,
    EX_SIZE_RIBBON_WIDTH,
    EX_SIZE_TAB_HEIGHT,
    EX_SIZE_LINE_HEIGHT, /* percent of the font's line height */
    EX_SIZE_COUNT
} ExSizeToken;

typedef enum ExFontToken { EX_FONT_TOKEN_INTERFACE, EX_FONT_TOKEN_TEXT, EX_FONT_TOKEN_MONO, EX_FONT_TOKEN_COUNT } ExFontToken;

typedef enum ExBuiltinTheme {
    EX_THEME_DARK, EX_THEME_LIGHT, EX_THEME_HC_DARK, EX_THEME_HC_LIGHT, EX_THEME_BUILTIN_COUNT
} ExBuiltinTheme;

typedef struct ExThemeExtra { char *section, *key, *value; } ExThemeExtra;

typedef struct ExTheme {
    char name[64];
    char author[64];
    ExColor colors[EX_COLOR_COUNT];
    int sizes[EX_SIZE_COUNT];          /* pixels at 100% zoom; EX_SIZE_LINE_HEIGHT is a percent */
    char *fonts[EX_FONT_TOKEN_COUNT];  /* family names; NULL: use the setting */
    ExThemeExtra *extra;               /* unknown keys, kept for writing back */
    size_t nextra;
    int is_dark;                       /* from the base theme or the background's lightness */
} ExTheme;

typedef struct ExThemeDiag {
    int unknown_keys;   /* kept in extra */
    int bad_values;     /* ignored: the base theme's value stays */
} ExThemeDiag;

/* Starts a theme from a built-in one (all tokens set). */
int ex_theme_init_builtin(ExTheme *t, ExBuiltinTheme which);
/* Reads a theme file: starts from its `[theme] base` (dark or light, default
 * dark) and applies the file over it. Returns 0 on success. */
int ex_theme_load_text(ExTheme *t, const char *text, size_t len, ExThemeDiag *diag);
void ex_theme_free(ExTheme *t);

ExColor ex_theme_color(const ExTheme *t, ExColorToken c);
int ex_theme_size(const ExTheme *t, ExSizeToken s);
const char *ex_color_token_name(ExColorToken c);
int ex_color_token_by_name(const char *name); /* -1 if unknown */
/* Parses "#rrggbb" or "#rrggbbaa". Returns 0 on success. */
int ex_parse_color(const char *s, ExColor *out);
/* WCAG contrast ratio of two colors (alpha ignored), 1.0 to 21.0. */
double ex_contrast_ratio(ExColor a, ExColor b);

#ifdef __cplusplus
}
#endif
#endif
