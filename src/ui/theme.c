/* The theme token loader: the only place that turns text into colors. The
 * no_color_literals test forbids color literals everywhere else. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_theme.h"
#include "theme_data.h"

static const char *const COLOR_NAMES[EX_COLOR_COUNT] = {
#define X(id, name) name,
    EX_COLOR_TOKENS(X)
#undef X
};

static const char *const SIZE_NAMES[EX_SIZE_COUNT] = {"radius", "radius_large", "spacing", "border_width",
                                                       "ribbon_width", "tab_height", "line_height"};
static const char *const FONT_NAMES[EX_FONT_TOKEN_COUNT] = {"interface", "text", "mono"};

const char *ex_color_token_name(ExColorToken c) { return (int)c >= 0 && c < EX_COLOR_COUNT ? COLOR_NAMES[c] : ""; }

int ex_color_token_by_name(const char *name) {
    int i;
    for (i = 0; i < EX_COLOR_COUNT; i++) if (strcmp(COLOR_NAMES[i], name) == 0) return i;
    return -1;
}

ExColor ex_theme_color(const ExTheme *t, ExColorToken c) { return t->colors[c]; }
int ex_theme_size(const ExTheme *t, ExSizeToken s) { return t->sizes[s]; }

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ex_parse_color(const char *s, ExColor *out) {
    size_t n = strlen(s), i;
    int v[8];
    if ((n != 7 && n != 9) || s[0] != '#') return -1;
    for (i = 1; i < n; i++) if ((v[i - 1] = hexval(s[i])) < 0) return -1;
    out->r = (uint8_t)(v[0] * 16 + v[1]);
    out->g = (uint8_t)(v[2] * 16 + v[3]);
    out->b = (uint8_t)(v[4] * 16 + v[5]);
    out->a = n == 9 ? (uint8_t)(v[6] * 16 + v[7]) : 255;
    return 0;
}

static double lin(double c) { c /= 255.0; return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double luminance(ExColor c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }

double ex_contrast_ratio(ExColor a, ExColor b) {
    double x = luminance(a), y = luminance(b);
    if (x < y) { double t = x; x = y; y = t; }
    return (x + 0.05) / (y + 0.05);
}

static char *dup_n(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = 0; }
    return p;
}

void ex_theme_free(ExTheme *t) {
    size_t i;
    for (i = 0; i < EX_FONT_TOKEN_COUNT; i++) free(t->fonts[i]);
    for (i = 0; i < t->nextra; i++) { free(t->extra[i].section); free(t->extra[i].key); free(t->extra[i].value); }
    free(t->extra);
    memset(t, 0, sizeof *t);
}

static int add_extra(ExTheme *t, const char *sec, const char *key, const char *val) {
    ExThemeExtra *ne = realloc(t->extra, (t->nextra + 1) * sizeof *ne);
    if (!ne) return -1;
    t->extra = ne;
    ne[t->nextra].section = dup_n(sec, strlen(sec));
    ne[t->nextra].key = dup_n(key, strlen(key));
    ne[t->nextra].value = dup_n(val, strlen(val));
    t->nextra++;
    return 0;
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* Applies an INI text over t. If base_out is not NULL, only reads `[theme] base`. */
static int apply(ExTheme *t, const char *text, size_t len, ExThemeDiag *diag, int *base_out) {
    char *copy = dup_n(text, len), *line, *next;
    char section[32] = "";
    if (!copy) return -1;
    for (line = copy; line; line = next) {
        char *eq, *key, *val, *hash;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        line = trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (*line == '[') {
            char *close = strchr(line, ']');
            size_t n;
            if (!close) continue;
            n = (size_t)(close - line - 1);
            if (n >= sizeof section) n = sizeof section - 1;
            memcpy(section, line + 1, n);
            section[n] = 0;
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        key = trim(line);
        val = trim(eq + 1);
        if ((hash = strchr(val, '#')) && hash != val && (hash[-1] == ' ' || hash[-1] == '\t')) *hash = 0; /* trailing comment */
        val = trim(val);

        if (base_out) {
            if (!strcmp(section, "theme") && !strcmp(key, "base")) *base_out = !strcmp(val, "light") ? 1 : 0;
            continue;
        }
        if (!strcmp(section, "theme")) {
            if (!strcmp(key, "name")) { strncpy(t->name, val, sizeof t->name - 1); t->name[sizeof t->name - 1] = 0; }
            else if (!strcmp(key, "author")) { strncpy(t->author, val, sizeof t->author - 1); t->author[sizeof t->author - 1] = 0; }
            else if (strcmp(key, "base") != 0) { add_extra(t, section, key, val); if (diag) diag->unknown_keys++; }
        } else if (!strcmp(section, "colors")) {
            int id = ex_color_token_by_name(key);
            ExColor c;
            if (id < 0) { add_extra(t, section, key, val); if (diag) diag->unknown_keys++; }
            else if (ex_parse_color(val, &c) == 0) t->colors[id] = c;
            else if (diag) diag->bad_values++;
        } else if (!strcmp(section, "sizes")) {
            int k, found = -1;
            char *end;
            long v = strtol(val, &end, 10);
            for (k = 0; k < EX_SIZE_COUNT; k++) if (!strcmp(SIZE_NAMES[k], key)) found = k;
            if (found < 0) { add_extra(t, section, key, val); if (diag) diag->unknown_keys++; }
            else if (*val && *end == 0 && v >= 0 && v <= 10000) t->sizes[found] = (int)v;
            else if (diag) diag->bad_values++;
        } else if (!strcmp(section, "fonts")) {
            int k, found = -1;
            for (k = 0; k < EX_FONT_TOKEN_COUNT; k++) if (!strcmp(FONT_NAMES[k], key)) found = k;
            if (found < 0) { add_extra(t, section, key, val); if (diag) diag->unknown_keys++; }
            else if (*val) { free(t->fonts[found]); t->fonts[found] = dup_n(val, strlen(val)); }
        } else {
            add_extra(t, section, key, val);
            if (diag) diag->unknown_keys++;
        }
    }
    free(copy);
    return 0;
}

int ex_theme_init_builtin(ExTheme *t, ExBuiltinTheme which) {
    const char *text = ex_builtin_theme_text(which);
    ExThemeDiag diag;
    memset(t, 0, sizeof *t);
    memset(&diag, 0, sizeof diag);
    if (!text) return -1;
    if (apply(t, text, strlen(text), &diag, NULL) != 0 || diag.bad_values || diag.unknown_keys) {
        ex_theme_free(t);
        return -1;
    }
    t->is_dark = which == EX_THEME_DARK || which == EX_THEME_HC_DARK;
    return 0;
}

int ex_theme_load_text(ExTheme *t, const char *text, size_t len, ExThemeDiag *diag) {
    int base = 0;
    ExThemeDiag local;
    if (!diag) diag = &local;
    memset(diag, 0, sizeof *diag);
    apply(t, text, len, NULL, &base); /* find the base first: only dark and light are bases */
    if (ex_theme_init_builtin(t, base ? EX_THEME_LIGHT : EX_THEME_DARK) != 0) return -1;
    {
        /* the file's own [theme] name and author replace the base's */
        int rc = apply(t, text, len, diag, NULL);
        return rc;
    }
}
