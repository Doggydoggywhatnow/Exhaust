/* theme: the four built-in themes load without complaint, every token is set,
 * the documented contrast ratios hold, and layering, base themes and unknown keys behave. */
#include <stdio.h>
#include <string.h>

#include "exhaust_theme.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void) {
    int k, i;
    static const double min_contrast[4] = {4.5, 4.5, 7.0, 7.0};
    for (k = 0; k < EX_THEME_BUILTIN_COUNT; k++) {
        ExTheme t;
        CHECK(ex_theme_init_builtin(&t, (ExBuiltinTheme)k) == 0);
        for (i = 0; i < EX_COLOR_COUNT; i++) CHECK(t.colors[i].a != 0);
        CHECK(ex_theme_size(&t, EX_SIZE_RADIUS) == 6 && ex_theme_size(&t, EX_SIZE_LINE_HEIGHT) == 150);
        CHECK(ex_theme_size(&t, EX_SIZE_RIBBON_WIDTH) == 44 && ex_theme_size(&t, EX_SIZE_TAB_HEIGHT) == 36);
        /* the palette claims: text pairs meet WCAG, hints and controls meet 3:1 */
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_TEXT), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= min_contrast[k]);
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_TEXT_MUTED), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= min_contrast[k]);
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_LINK), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= min_contrast[k]);
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_ERROR), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= min_contrast[k]);
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_TEXT_FAINT), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= 3.0);
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_ACCENT), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= 3.0);
        CHECK(ex_contrast_ratio(ex_theme_color(&t, EX_COLOR_LINK_UNRESOLVED), ex_theme_color(&t, EX_COLOR_BACKGROUND)) >= 3.0);
        CHECK(t.is_dark == (k == EX_THEME_DARK || k == EX_THEME_HC_DARK));
        ex_theme_free(&t);
    }

    /* values straight from the palette table */
    {
        ExTheme d, l;
        ExColor c;
        ex_theme_init_builtin(&d, EX_THEME_DARK);
        ex_theme_init_builtin(&l, EX_THEME_LIGHT);
        c = ex_theme_color(&d, EX_COLOR_BACKGROUND);
        CHECK(c.r == 0x1e && c.g == 0x1f && c.b == 0x2d && c.a == 0xff);
        c = ex_theme_color(&d, EX_COLOR_SELECTION);
        CHECK(c.r == 0x81 && c.g == 0x67 && c.b == 0xeb && c.a == 0x59);
        c = ex_theme_color(&l, EX_COLOR_ACCENT);
        CHECK(c.r == 0x5b && c.g == 0x3f && c.b == 0xd1);
        ex_theme_free(&d); ex_theme_free(&l);
    }

    /* a user theme: base, overrides, bad values, unknown keys kept */
    {
        const char *text =
            "# comment\n[theme]\nname = Mine\nauthor = Me\nbase = light   # the base\n"
            "[colors]\naccent = #ff8800\nlink = nonsense\nfuture_color = #123456\n"
            "[fonts]\ntext = Some Font\n[sizes]\nradius = 10\nspacing = abc\n[future]\nthing = 1\n";
        ExTheme t, light;
        ExThemeDiag diag;
        ExColor c;
        ex_theme_init_builtin(&light, EX_THEME_LIGHT);
        CHECK(ex_theme_load_text(&t, text, strlen(text), &diag) == 0);
        CHECK(strcmp(t.name, "Mine") == 0 && strcmp(t.author, "Me") == 0);
        c = ex_theme_color(&t, EX_COLOR_ACCENT);
        CHECK(c.r == 0xff && c.g == 0x88 && c.b == 0x00 && c.a == 0xff);
        CHECK(ex_theme_color(&t, EX_COLOR_BACKGROUND).r == ex_theme_color(&light, EX_COLOR_BACKGROUND).r); /* from the base */
        CHECK(ex_theme_color(&t, EX_COLOR_LINK).r == ex_theme_color(&light, EX_COLOR_LINK).r);              /* bad value ignored */
        CHECK(ex_theme_size(&t, EX_SIZE_RADIUS) == 10 && ex_theme_size(&t, EX_SIZE_SPACING) == 8);
        CHECK(diag.bad_values == 2 && diag.unknown_keys == 2 && t.nextra == 2);
        CHECK(t.fonts[EX_FONT_TOKEN_TEXT] && strcmp(t.fonts[EX_FONT_TOKEN_TEXT], "Some Font") == 0 && !t.fonts[EX_FONT_TOKEN_MONO]);
        ex_theme_free(&t); ex_theme_free(&light);
    }
    {
        ExTheme t;
        ExColor c;
        const char *text = "[colors]\nselection = #00ff0080\n";
        CHECK(ex_theme_load_text(&t, text, strlen(text), NULL) == 0);   /* no base: dark */
        c = ex_theme_color(&t, EX_COLOR_SELECTION);
        CHECK(c.g == 0xff && c.a == 0x80 && ex_theme_color(&t, EX_COLOR_BACKGROUND).r == 0x1e);
        ex_theme_free(&t);
    }
    CHECK(ex_color_token_by_name("accent") == EX_COLOR_ACCENT && ex_color_token_by_name("nope") == -1);
    CHECK(strcmp(ex_color_token_name(EX_COLOR_CALLOUT_QUOTE), "callout_quote") == 0);
    {
        ExColor c;
        CHECK(ex_parse_color("#abc", &c) != 0 && ex_parse_color("abcdef", &c) != 0 && ex_parse_color("#12345g", &c) != 0);
        CHECK(ex_parse_color("#0a0B0c", &c) == 0 && c.r == 10 && c.g == 11 && c.b == 12 && c.a == 255);
    }
    CHECK(ex_contrast_ratio((ExColor){0, 0, 0, 255}, (ExColor){255, 255, 255, 255}) > 20.9);
    return failures ? 1 : 0;
}
