/* theme_manager (sub-slice 7d): finding theme and snippet files, the override order (built-in, theme file, accent colour, snippets), the colour
 * scheme and a theme file's own base, unknown keys kept when a token is edited, the contrast check, and live reload: a changed file reaches the
 * theme within a few polls of the worker (a quarter of a second each), a deleted one drops back to the built-in theme. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_themes.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define V "theme_manager_test/vault/.exhaust"
#define G "theme_manager_test/global"

static void put(const char *path, const char *text) {
    char dir[512], *slash;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash) { *slash = 0; ex_platform_make_dirs(dir); }
    CHECK(ex_platform_write_file_atomic(path, text, strlen(text)) == 0);
}
static char *get(const char *path) { size_t n; return ex_platform_read_file(path, &n); }

/* Sets the inputs, waits for the theme that follows and takes it. */
static int build_with(ExThemes *t, const char *scheme, int sys, const char *theme, const char *accent, const char *snippets, ExTheme *out) {
    Uint64 t0 = SDL_GetTicks();
    ex_themes_set_inputs(t, scheme, sys, theme, accent, snippets);
    while (SDL_GetTicks() - t0 < 10000) {                            /* take returns only a theme built for these inputs */
        if (ex_themes_take(t, out, NULL)) return 1;
        SDL_Delay(5);
    }
    return 0;
}

static int has_extra(const ExTheme *t, const char *sec, const char *key) {
    size_t i;
    for (i = 0; i < t->nextra; i++) if (!strcmp(t->extra[i].section, sec) && !strcmp(t->extra[i].key, key)) return 1;
    return 0;
}

int main(void) {
    ExThemes *t;
    ExTheme th, dark, light;
    char warn[8][160];
    char *name, *text;
    size_t i, n;
    ExColor c;

    ex_platform_remove_tree("theme_manager_test");
    put(V "/themes/mine.theme", "# my theme\n[theme]\nname = My theme\nauthor = Me\nbase = dark   # always dark\n[colors]\naccent = #112233\ntext = #fefefe\nunknown_color = #ffffff\n[sizes]\nradius = 3\n[future]\nthing = 1\n");
    put(V "/themes/pale.theme", "[theme]\nname = Pale\nbase = light\n[colors]\nbackground = #fafafa\n");
    put(V "/themes/shared.theme", "[theme]\nname = Shared vault\n[colors]\naccent = #010101\n");
    put(G "/themes/shared.theme", "[theme]\nname = Shared global\n[colors]\naccent = #020202\n");
    put(G "/themes/globalonly.theme", "[theme]\nname = Global only\n");
    put(V "/themes/faded.theme", "[theme]\nname = Faded\n[colors]\ntext = #202030\ntext_muted = #1f2030\n");
    put(V "/snippets/bright.style", "# a snippet\n@tokens {\n  colors.accent: #778899;\n  sizes.radius: 9;\n}\n\ncallout.tip {\n  color: #00ff00;\n  icon: lightbulb;\n}\n\ntab.active {\n  background: #222;\n}\n");
    put(V "/snippets/other.style", "@tokens {\n  colors.link: #123456;\n}\n");

    t = ex_themes_create(V, G);
    CHECK(t != NULL);
    memset(&light, 0, sizeof light);

    /* the lists: the built-in first, then the vault's files and the global ones; a vault file hides a global one of the same file name */
    n = ex_themes_count(t);
    printf("theme_manager: %zu themes\n", n);
    CHECK(n == 6);                                                  /* Exhaust, Faded, My theme, Pale, Shared vault (the global Shared is hidden), Global only */
    name = ex_themes_name(t, 0); CHECK(name && !strcmp(name, "Exhaust")); free(name);
    { int mine = 0, pale = 0, sv = 0, sg = 0, go = 0; for (i = 0; i < n; i++) { name = ex_themes_name(t, i); if (!strcmp(name, "My theme")) mine = 1; if (!strcmp(name, "Pale")) pale = 1; if (!strcmp(name, "Shared vault")) sv = 1; if (!strcmp(name, "Shared global")) sg = 1; if (!strcmp(name, "Global only")) go = 1; free(name); }
      CHECK(mine && pale && sv && !sg && go); }
    CHECK(ex_themes_snippet_count(t) == 2);

    /* the built-in themes by colour scheme and by the system */
    CHECK(build_with(t, "dark", 0, "exhaust", "", "", &dark) == 1);
    CHECK(dark.is_dark && dark.colors[EX_COLOR_BACKGROUND].r == 0x1e);
    CHECK(build_with(t, "light", 1, "exhaust", "", "", &light) == 1);
    CHECK(!light.is_dark && light.colors[EX_COLOR_BACKGROUND].r == 0xf7);
    ex_theme_free(&light);
    CHECK(build_with(t, "system", 0, "", "", "", &light) == 1 && !light.is_dark);        /* the system is light */
    ex_theme_free(&light);
    CHECK(build_with(t, "system", 1, "", "", "", &light) == 1 && light.is_dark);
    ex_theme_free(&light);

    /* a theme file over the built-in one: what it sets, and the rest from the base; unknown keys are kept in the theme */
    CHECK(build_with(t, "dark", 0, "My theme", "", "", &th) == 1);
    CHECK(!strcmp(th.name, "My theme") && !strcmp(th.author, "Me"));
    c = th.colors[EX_COLOR_ACCENT]; CHECK(c.r == 0x11 && c.g == 0x22 && c.b == 0x33);
    c = th.colors[EX_COLOR_TEXT]; CHECK(c.r == 0xfe);
    CHECK(th.sizes[EX_SIZE_RADIUS] == 3 && th.sizes[EX_SIZE_SPACING] == dark.sizes[EX_SIZE_SPACING]);
    CHECK(th.colors[EX_COLOR_LINK].r == dark.colors[EX_COLOR_LINK].r);                    /* not set: the base's */
    CHECK(has_extra(&th, "colors", "unknown_color") && has_extra(&th, "future", "thing"));
    ex_theme_free(&th);
    /* a file's own base decides light or dark, whatever the colour scheme says */
    CHECK(build_with(t, "dark", 0, "Pale", "", "", &th) == 1 && !th.is_dark && th.colors[EX_COLOR_BACKGROUND].r == 0xfa);
    ex_theme_free(&th);
    CHECK(build_with(t, "light", 0, "My theme", "", "", &th) == 1 && th.is_dark);
    ex_theme_free(&th);
    CHECK(build_with(t, "dark", 0, "No such theme", "", "", &th) == 1 && th.colors[EX_COLOR_BACKGROUND].r == 0x1e);   /* unknown: the built-in */
    ex_theme_free(&th);

    /* the order: theme file, then the accent colour of the settings, then the snippets in the order of the list */
    CHECK(build_with(t, "dark", 0, "My theme", "#445566", "", &th) == 1);
    c = th.colors[EX_COLOR_ACCENT]; CHECK(c.r == 0x44 && c.g == 0x55 && c.b == 0x66);
    CHECK(th.colors[EX_COLOR_ACCENT_HOVER].r > th.colors[EX_COLOR_ACCENT].r);              /* lightened */
    ex_theme_free(&th);
    CHECK(build_with(t, "dark", 0, "My theme", "#445566", "bright", &th) == 1);
    c = th.colors[EX_COLOR_ACCENT]; CHECK(c.r == 0x77 && c.g == 0x88 && c.b == 0x99);      /* the snippet is last */
    CHECK(th.sizes[EX_SIZE_RADIUS] == 9);
    c = th.colors[EX_COLOR_CALLOUT_TIP]; CHECK(c.r == 0x00 && c.g == 0xff && c.b == 0x00);
    c = th.colors[EX_COLOR_LINK]; CHECK(c.r == dark.colors[EX_COLOR_LINK].r);              /* the other snippet is off */
    ex_theme_free(&th);
    CHECK(build_with(t, "dark", 0, "", "", "bright, other", &th) == 1);
    c = th.colors[EX_COLOR_LINK]; CHECK(c.r == 0x12 && c.g == 0x34 && c.b == 0x56);
    ex_theme_free(&th);
    CHECK(build_with(t, "dark", 0, "", "", "missing,bright", &th) == 1 && th.sizes[EX_SIZE_RADIUS] == 9);   /* a name with no file is skipped */
    ex_theme_free(&th);

    /* the contrast check: the built-in themes are fine, a theme that hides its text is not */
    CHECK(ex_theme_contrast_warnings(&dark, warn, 8) == 0);
    CHECK(build_with(t, "light", 0, "exhaust", "", "", &light) == 1 && ex_theme_contrast_warnings(&light, warn, 8) == 0);
    ex_theme_free(&light);
    CHECK(build_with(t, "dark", 0, "Faded", "", "", &th) == 1);
    n = ex_theme_contrast_warnings(&th, warn, 8);
    printf("theme_manager: faded theme: %zu warnings: %s\n", n, n ? warn[0] : "");
    CHECK(n >= 2 && strstr(warn[0], "Text") != NULL);
    ex_theme_free(&th);

    /* editing a token keeps everything else in the file: comments, unknown keys, other sections */
    name = ex_themes_set_token(t, "My theme", "dark", 0, "colors.accent", "#abcdef");
    CHECK(name && !strcmp(name, "My theme"));
    free(name);
    text = get(V "/themes/mine.theme");
    CHECK(text && strstr(text, "# my theme\n") && strstr(text, "unknown_color = #ffffff") && strstr(text, "[future]\nthing = 1") && strstr(text, "accent = #abcdef") && strstr(text, "base = dark   # always dark"));
    free(text);
    CHECK(ex_themes_set_token(t, "My theme", "dark", 0, "colors.accent", "red") == NULL);        /* not a colour */
    CHECK(ex_themes_set_token(t, "My theme", "dark", 0, "colors.no_such_token", "#ffffff") == NULL);
    CHECK(ex_themes_set_token(t, "My theme", "dark", 0, "sizes.radius", "-4") == NULL);
    text = get(V "/themes/mine.theme");
    CHECK(text && strstr(text, "accent = #abcdef") && !strstr(text, "red"));
    free(text);
    /* the built-in theme is never written: a theme of the user's own, Custom, is made, and added to the next time */
    name = ex_themes_set_token(t, "exhaust", "light", 0, "colors.accent", "#aa0000");
    CHECK(name && !strcmp(name, "Custom")); free(name);
    text = get(V "/themes/custom.theme");
    CHECK(text && strstr(text, "name = Custom") && strstr(text, "base = light") && strstr(text, "accent = #aa0000"));
    free(text);
    name = ex_themes_set_token(t, "Custom", "light", 0, "sizes.radius", "2");
    CHECK(name && !strcmp(name, "Custom")); free(name);
    text = get(V "/themes/custom.theme");
    CHECK(text && strstr(text, "accent = #aa0000") && strstr(text, "radius = 2"));
    free(text);

    /* live reload: a changed file reaches the theme within a few polls; a deleted one drops back to the built-in theme */
    CHECK(build_with(t, "dark", 0, "My theme", "", "", &th) == 1);
    c = th.colors[EX_COLOR_ACCENT]; CHECK(c.r == 0xab);
    ex_theme_free(&th);
    {
        unsigned long g = ex_themes_generation(t), p0 = ex_themes_polls(t), p1;
        Uint64 t0 = SDL_GetTicks();
        put(V "/themes/mine.theme", "[theme]\nname = My theme\nbase = dark\n[colors]\naccent = #0a0b0c\n[future]\nthing = 22222\n");
        while (ex_themes_generation(t) <= g && SDL_GetTicks() - t0 < 10000) SDL_Delay(5);
        p1 = ex_themes_polls(t);
        printf("theme_manager: a changed file was built after %lu polls, %u ms\n", p1 - p0, (unsigned)(SDL_GetTicks() - t0));
        CHECK(ex_themes_generation(t) > g);
        CHECK(p1 - p0 <= 3);                                         /* the worker looks every quarter second: well within a second */
        CHECK(ex_themes_take(t, &th, NULL) == 1);
        c = th.colors[EX_COLOR_ACCENT]; CHECK(c.r == 0x0a && c.g == 0x0b && c.b == 0x0c);
        ex_theme_free(&th);
        g = ex_themes_generation(t);
        ex_platform_remove_tree(V "/themes/mine.theme");
        t0 = SDL_GetTicks();
        while (ex_themes_generation(t) <= g && SDL_GetTicks() - t0 < 10000) SDL_Delay(5);
        CHECK(ex_themes_generation(t) > g);
        CHECK(ex_themes_take(t, &th, NULL) == 1);
        c = th.colors[EX_COLOR_ACCENT]; CHECK(c.r == dark.colors[EX_COLOR_ACCENT].r);          /* the built-in accent */
        ex_theme_free(&th);
        /* a snippet file too */
        g = ex_themes_generation(t);
        CHECK(build_with(t, "dark", 0, "", "", "other", &th) == 1);
        ex_theme_free(&th);
        g = ex_themes_generation(t);
        put(V "/snippets/other.style", "@tokens {\n  colors.link: #654321;\n}\n");
        t0 = SDL_GetTicks();
        while (ex_themes_generation(t) <= g && SDL_GetTicks() - t0 < 10000) SDL_Delay(5);
        CHECK(ex_themes_take(t, &th, NULL) == 1);
        c = th.colors[EX_COLOR_LINK]; CHECK(c.r == 0x65 && c.g == 0x43 && c.b == 0x21);
        ex_theme_free(&th);
    }

    ex_theme_free(&dark);
    ex_themes_free(t);
    ex_platform_remove_tree("theme_manager_test");
    if (failures) { fprintf(stderr, "theme_manager: %d failure(s)\n", failures); return 1; }
    printf("theme_manager: ok\n");
    return 0;
}
