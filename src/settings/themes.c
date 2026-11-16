/* Themes and style snippets (sub-slice 7d). See include/exhaust_themes.h and "Loading themes and snippets" in docs/formats.md. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_themes.h"

typedef struct Entry { char *name, *path; } Entry;
typedef struct Stamp { char *path; int64_t mtime, size; int exists; } Stamp;

struct ExThemes {
    char *vdir, *gdir;
    SDL_Mutex *mu;
    SDL_Thread *thr;
    SDL_AtomicInt stop;
    /* inputs (under mu) */
    char scheme[16]; int system_dark, hc; char theme[128]; char accent[16]; char snippets[400];
    unsigned long in_gen;
    /* output (under mu) */
    ExTheme built; ExThemeDiag diag; int have; unsigned long out_gen, taken_gen, built_in_gen;      /* built_in_gen: the inputs the theme was built for */
    Entry *themes; size_t nthemes;                     /* index 0: the built-in Exhaust */
    Entry *snips; size_t nsnips;
    SDL_AtomicInt polls;
    /* the worker's own */
    Stamp *used; size_t nused;
    char *built_path;                                   /* the theme file the last build used (NULL: the built-in theme) */
    SDL_AtomicInt rescan_now;                          /* set when this program wrote a theme or snippet file: list the folders at once */
};

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static char *join(const char *a, const char *b) { char *p = malloc(strlen(a) + strlen(b) + 2); if (p) sprintf(p, "%s/%s", a, b); return p; }
static int ieq(const char *a, const char *b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; return *a == *b; }

/* ---- lists ---- */

static void free_entries(Entry *e, size_t n) { size_t i; for (i = 0; i < n; i++) { free(e[i].name); free(e[i].path); } free(e); }

typedef struct Scan { Entry *list; size_t n, cap; const char *ext; size_t extlen; } Scan;

static int scan_cb(const char *path, int is_dir, int64_t mtime, void *user) {
    Scan *s = user;
    size_t len = strlen(path);
    const char *base;
    (void)mtime;
    if (is_dir || len <= s->extlen || strcmp(path + len - s->extlen, s->ext) != 0) return 0;
    if (s->n == s->cap) {
        Entry *ne = realloc(s->list, (s->cap ? s->cap * 2 : 16) * sizeof *ne);
        if (!ne) return 1;
        s->list = ne; s->cap = s->cap ? s->cap * 2 : 16;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    s->list[s->n].path = dup_str(path);
    s->list[s->n].name = malloc(strlen(base) + 1);
    if (!s->list[s->n].path || !s->list[s->n].name) { free(s->list[s->n].path); free(s->list[s->n].name); return 1; }
    memcpy(s->list[s->n].name, base, strlen(base) - s->extlen);
    s->list[s->n].name[strlen(base) - s->extlen] = 0;                 /* the file's name without its extension; a theme's own name comes from its file */
    s->n++;
    return 0;
}

/* Files in `<dir>/<sub>` with an extension, the vault's before the global ones; a name the vault has hides the global one. */
static void scan_two(const char *vdir, const char *gdir, const char *sub, const char *ext, Entry **out, size_t *nout) {
    Scan s;
    const char *dirs[2] = { vdir, gdir };
    size_t d, i, k;
    memset(&s, 0, sizeof s);
    s.ext = ext; s.extlen = strlen(ext);
    for (d = 0; d < 2; d++) {
        char *p;
        size_t before = s.n;
        if (!dirs[d]) continue;
        p = join(dirs[d], sub);
        if (p) { ex_platform_walk(p, scan_cb, &s); free(p); }
        for (i = before; i < s.n; i++) {                              /* hidden by a file of the same name in the vault's folder */
            for (k = 0; k < before; k++) if (ieq(s.list[i].name, s.list[k].name)) break;
            if (k < before) { free(s.list[i].name); free(s.list[i].path); s.list[i] = s.list[s.n - 1]; s.n--; i--; }
        }
    }
    *out = s.list; *nout = s.n;
}

static void rescan(ExThemes *t) {
    Entry *th = NULL, *sn = NULL, *all;
    size_t nth = 0, nsn = 0, i;
    scan_two(t->vdir, t->gdir, "themes", ".theme", &th, &nth);
    scan_two(t->vdir, t->gdir, "snippets", ".style", &sn, &nsn);
    all = calloc(nth + 1, sizeof *all);
    if (!all) { free_entries(th, nth); free_entries(sn, nsn); return; }
    all[0].name = dup_str("Exhaust"); all[0].path = NULL;
    for (i = 0; i < nth; i++) {                                     /* a theme is listed by its own name when the file has one */
        size_t len;
        char *text = ex_platform_read_file(th[i].path, &len);
        all[i + 1].path = th[i].path;
        all[i + 1].name = th[i].name;
        if (text) {
            ExTheme tmp;
            ExThemeDiag dg;
            if (ex_theme_load_text(&tmp, text, len, &dg) == 0) {
                if (tmp.name[0]) { free(all[i + 1].name); all[i + 1].name = dup_str(tmp.name); }
                ex_theme_free(&tmp);
            }
            free(text);
        }
    }
    free(th);
    SDL_LockMutex(t->mu);
    free_entries(t->themes, t->nthemes);
    free_entries(t->snips, t->nsnips);
    t->themes = all; t->nthemes = nth + 1;
    t->snips = sn; t->nsnips = nsn;
    SDL_UnlockMutex(t->mu);
}

/* ---- snippets ---- */

static void apply_token(ExTheme *t, const char *name, const char *val) {
    ExColor c;
    if (!strncmp(name, "colors.", 7)) {
        int id = ex_color_token_by_name(name + 7);
        if (id >= 0 && ex_parse_color(val, &c) == 0) t->colors[id] = c;
    } else if (!strncmp(name, "sizes.", 6)) {
        static const char *names[EX_SIZE_COUNT] = { "radius", "radius_large", "spacing", "border_width", "ribbon_width", "tab_height", "line_height" };
        int k;
        char *end;
        long v = strtol(val, &end, 10);
        for (k = 0; k < EX_SIZE_COUNT; k++) if (!strcmp(names[k], name + 6) && *val && *end == 0 && v >= 0 && v <= 10000) t->sizes[k] = (int)v;
    } else if (!strncmp(name, "fonts.", 6)) {
        static const char *names[EX_FONT_TOKEN_COUNT] = { "interface", "text", "mono" };
        int k;
        for (k = 0; k < EX_FONT_TOKEN_COUNT; k++) if (!strcmp(names[k], name + 6) && *val) { free(t->fonts[k]); t->fonts[k] = dup_str(val); }
    }
}

/* A snippet is a list of rules `selector { property: value; }`. `@tokens` sets theme tokens by name; `callout.<type> { color: ... }` sets that callout's
 * colour. Every other rule is read and left alone (it is for later versions; see formats.md). */
static void apply_snippet(ExTheme *t, const char *text, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t s, b, e;
        char sel[96];
        while (i < len && isspace((unsigned char)text[i])) i++;
        if (i < len && text[i] == '#') { while (i < len && text[i] != '\n') i++; continue; }          /* a comment line */
        s = i;
        while (i < len && text[i] != '{') i++;
        if (i >= len) return;
        b = i + 1;
        e = s; { size_t k = i; while (k > s && isspace((unsigned char)text[k - 1])) k--; e = k; }
        snprintf(sel, sizeof sel, "%.*s", (int)(e - s < sizeof sel - 1 ? e - s : sizeof sel - 1), text + s);
        while (i < len && text[i] != '}') i++;
        {
            size_t p = b;
            while (p < i) {                                           /* property: value; */
                size_t ps, colon, pe;
                char prop[80], val[160];
                while (p < i && isspace((unsigned char)text[p])) p++;
                ps = p;
                while (p < i && text[p] != ':' && text[p] != ';') p++;
                if (p >= i || text[p] != ':') { p++; continue; }
                colon = p;
                p++;
                while (p < i && text[p] != ';') p++;
                pe = p;
                p++;
                {
                    size_t a = ps, z = colon, va = colon + 1, vz = pe;
                    while (z > a && isspace((unsigned char)text[z - 1])) z--;
                    while (va < vz && isspace((unsigned char)text[va])) va++;
                    while (vz > va && isspace((unsigned char)text[vz - 1])) vz--;
                    snprintf(prop, sizeof prop, "%.*s", (int)(z - a < sizeof prop - 1 ? z - a : sizeof prop - 1), text + a);
                    snprintf(val, sizeof val, "%.*s", (int)(vz - va < sizeof val - 1 ? vz - va : sizeof val - 1), text + va);
                }
                if (!strcmp(sel, "@tokens")) apply_token(t, prop, val);
                else if (!strncmp(sel, "callout.", 8) && !strcmp(prop, "color")) { char name[96]; snprintf(name, sizeof name, "colors.callout_%s", sel + 8); apply_token(t, name, val); }
            }
        }
        i++;
    }
}

/* ---- building ---- */

static void stamp(Stamp *st, const char *path) {
    ExPathInfo pi;
    st->path = dup_str(path);
    memset(&pi, 0, sizeof pi);
    st->exists = path && ex_platform_path_info(path, &pi) == 0 && pi.is_file;
    st->mtime = st->exists ? pi.mtime : 0;
    st->size = st->exists ? pi.size : 0;
}

static void free_used(ExThemes *t) { size_t i; for (i = 0; i < t->nused; i++) free(t->used[i].path); free(t->used); t->used = NULL; t->nused = 0; }

static void add_used(ExThemes *t, const char *path) {
    Stamp *n = realloc(t->used, (t->nused + 1) * sizeof *n);
    if (!n) return;
    t->used = n;
    stamp(&t->used[t->nused], path);
    t->nused++;
}

static const char *path_of(Entry *list, size_t n, const char *name) {
    size_t i;
    for (i = 0; i < n; i++) if (list[i].path && ieq(list[i].name, name)) return list[i].path;
    return NULL;
}

static void build(ExThemes *t, unsigned long in_gen, const char *scheme, int system_dark, int hc, const char *theme, const char *accent, const char *snippets) {
    ExTheme out;
    ExThemeDiag diag;
    int dark = strcmp(scheme, "light") != 0;
    char *text = NULL;
    size_t len = 0;
    char *tpath = NULL;
    memset(&diag, 0, sizeof diag);
    if (!strcmp(scheme, "system")) dark = system_dark;
    free_used(t);
    SDL_LockMutex(t->mu);
    if (!hc && theme[0] && !ieq(theme, "exhaust")) { const char *p = path_of(t->themes, t->nthemes, theme); if (p) tpath = dup_str(p); }
    SDL_UnlockMutex(t->mu);
    free(t->built_path);
    t->built_path = tpath ? dup_str(tpath) : NULL;
    if (tpath) { add_used(t, tpath); text = ex_platform_read_file(tpath, &len); }
    if (text) {                                                     /* the file's own base decides light or dark; without one the colour scheme does */
        int base = ex_theme_text_base(text, len);
        if (base >= 0) dark = base == 0;
    }
    if (ex_theme_init_builtin(&out, hc ? (dark ? EX_THEME_HC_DARK : EX_THEME_HC_LIGHT) : (dark ? EX_THEME_DARK : EX_THEME_LIGHT)) != 0) { free(text); free(tpath); return; }
    if (text) ex_theme_apply_text(&out, text, len, &diag);
    free(text); free(tpath);
    if (accent[0] && !hc) {                                         /* with high contrast on the theme file and the accent colour are not applied (docs/formats.md) */
                                                                    /* the accent colour of the settings; its hover colour is the accent lightened */
        ExColor c;
        if (ex_parse_color(accent, &c) == 0) {
            out.colors[EX_COLOR_ACCENT] = c;
            out.colors[EX_COLOR_ACCENT_HOVER].r = (uint8_t)(c.r + (255 - c.r) * 15 / 100);
            out.colors[EX_COLOR_ACCENT_HOVER].g = (uint8_t)(c.g + (255 - c.g) * 15 / 100);
            out.colors[EX_COLOR_ACCENT_HOVER].b = (uint8_t)(c.b + (255 - c.b) * 15 / 100);
            out.colors[EX_COLOR_ACCENT_HOVER].a = c.a;
        }
    }
    {   /* the snippets that are on, in the order of the list */
        char list[400], *p;
        snprintf(list, sizeof list, "%s", snippets);
        for (p = list; p && *p;) {
            char *comma = strchr(p, ',');
            char *name = p, *spath, *stext;
            size_t slen;
            if (comma) *comma = 0;
            while (*name == ' ') name++;
            { size_t n = strlen(name); while (n && name[n - 1] == ' ') name[--n] = 0; }
            p = comma ? comma + 1 : NULL;
            if (!*name) continue;
            SDL_LockMutex(t->mu);
            spath = path_of(t->snips, t->nsnips, name) ? dup_str(path_of(t->snips, t->nsnips, name)) : NULL;
            SDL_UnlockMutex(t->mu);
            if (!spath) continue;
            add_used(t, spath);
            stext = ex_platform_read_file(spath, &slen);
            if (stext) { apply_snippet(&out, stext, slen); free(stext); }
            free(spath);
        }
    }
    out.is_dark = dark;
    SDL_LockMutex(t->mu);
    if (t->have) ex_theme_free(&t->built);
    t->built = out; t->diag = diag; t->have = 1;
    t->built_in_gen = in_gen;
    t->out_gen++;
    SDL_UnlockMutex(t->mu);
}

static int files_changed(ExThemes *t) {
    size_t i;
    for (i = 0; i < t->nused; i++) {
        Stamp now;
        int diff;
        stamp(&now, t->used[i].path);
        diff = now.exists != t->used[i].exists || now.mtime != t->used[i].mtime || now.size != t->used[i].size;
        free(now.path);
        if (diff) return 1;
    }
    return 0;
}

static int worker_main(void *arg) {
    ExThemes *t = arg;
    unsigned long built_for = (unsigned long)-1;
    int polls = 0;
    while (!SDL_GetAtomicInt(&t->stop)) {
        char scheme[16], theme[128], accent[16], snippets[400];
        int sys, hc;
        unsigned long gen;
        int relist = polls % 8 == 0, changed, list_changed = 0;
        SDL_LockMutex(t->mu);
        snprintf(scheme, sizeof scheme, "%s", t->scheme); snprintf(theme, sizeof theme, "%s", t->theme);
        snprintf(accent, sizeof accent, "%s", t->accent); snprintf(snippets, sizeof snippets, "%s", t->snippets);
        sys = t->system_dark; hc = t->hc; gen = t->in_gen;
        SDL_UnlockMutex(t->mu);
        if (SDL_SetAtomicInt(&t->rescan_now, 0)) relist = 1;
        if (relist) {                                                /* a new file in a folder shows up within two seconds */
            const char *now_path;
            rescan(t);
            SDL_LockMutex(t->mu);
            now_path = theme[0] && !ieq(theme, "exhaust") ? path_of(t->themes, t->nthemes, theme) : NULL;
            list_changed = (now_path == NULL) != (t->built_path == NULL) || (now_path && t->built_path && strcmp(now_path, t->built_path) != 0);   /* the chosen theme now names another file (or has one) */
            SDL_UnlockMutex(t->mu);
        }
        changed = files_changed(t) || list_changed;
        if (gen != built_for || changed) { build(t, gen, scheme, sys, hc, theme, accent, snippets); built_for = gen; }
        polls++;
        SDL_AddAtomicInt(&t->polls, 1);
        SDL_Delay(250);
    }
    return 0;
}

ExThemes *ex_themes_create(const char *vault_dir, const char *global_dir) {
    ExThemes *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    if (vault_dir) t->vdir = dup_str(vault_dir);
    if (global_dir) t->gdir = dup_str(global_dir);
    t->mu = SDL_CreateMutex();
    snprintf(t->scheme, sizeof t->scheme, "dark");
    t->in_gen = 1;                                                    /* the first build uses the defaults until the first inputs arrive */
    if (!t->mu) { ex_themes_free(t); return NULL; }
    rescan(t);
    t->thr = SDL_CreateThread(worker_main, "themes", t);
    if (!t->thr) { ex_themes_free(t); return NULL; }
    return t;
}

void ex_themes_free(ExThemes *t) {
    if (!t) return;
    if (t->thr) { SDL_SetAtomicInt(&t->stop, 1); SDL_WaitThread(t->thr, NULL); }
    free_used(t);
    free(t->built_path);
    if (t->have) ex_theme_free(&t->built);
    free_entries(t->themes, t->nthemes);
    free_entries(t->snips, t->nsnips);
    if (t->mu) SDL_DestroyMutex(t->mu);
    free(t->vdir); free(t->gdir);
    free(t);
}

void ex_themes_set_high_contrast(ExThemes *t, int on) {
    if (!t) return;
    SDL_LockMutex(t->mu);
    if (t->hc != (on != 0)) { t->hc = on != 0; t->in_gen++; }
    SDL_UnlockMutex(t->mu);
}

void ex_themes_set_inputs(ExThemes *t, const char *scheme, int system_dark, const char *theme, const char *accent, const char *snippets) {
    int same;
    if (!t) return;
    SDL_LockMutex(t->mu);
    same = !strcmp(t->scheme, scheme ? scheme : "dark") && t->system_dark == (system_dark != 0) && !strcmp(t->theme, theme ? theme : "") &&
           !strcmp(t->accent, accent ? accent : "") && !strcmp(t->snippets, snippets ? snippets : "");
    if (!same) {
        snprintf(t->scheme, sizeof t->scheme, "%s", scheme ? scheme : "dark");
        t->system_dark = system_dark != 0;
        snprintf(t->theme, sizeof t->theme, "%s", theme ? theme : "");
        snprintf(t->accent, sizeof t->accent, "%s", accent ? accent : "");
        snprintf(t->snippets, sizeof t->snippets, "%s", snippets ? snippets : "");
        t->in_gen++;
    }
    SDL_UnlockMutex(t->mu);
}

static int copy_theme(const ExTheme *src, ExTheme *dst) {
    size_t i;
    *dst = *src;
    for (i = 0; i < EX_FONT_TOKEN_COUNT; i++) dst->fonts[i] = src->fonts[i] ? dup_str(src->fonts[i]) : NULL;
    dst->extra = NULL; dst->nextra = 0;
    if (src->nextra) {
        dst->extra = calloc(src->nextra, sizeof *dst->extra);
        if (!dst->extra) return -1;
        for (i = 0; i < src->nextra; i++) {
            dst->extra[i].section = dup_str(src->extra[i].section);
            dst->extra[i].key = dup_str(src->extra[i].key);
            dst->extra[i].value = dup_str(src->extra[i].value);
        }
        dst->nextra = src->nextra;
    }
    return 0;
}

int ex_themes_take(ExThemes *t, ExTheme *out, ExThemeDiag *diag) {
    int got = 0;
    if (!t) return 0;
    SDL_LockMutex(t->mu);
    if (t->have && t->out_gen != t->taken_gen && t->built_in_gen == t->in_gen) {          /* never a theme built for inputs that have changed since */
        if (copy_theme(&t->built, out) == 0) { t->taken_gen = t->out_gen; if (diag) *diag = t->diag; got = 1; }
    }
    SDL_UnlockMutex(t->mu);
    return got;
}

unsigned long ex_themes_generation(const ExThemes *t) {
    unsigned long g;
    SDL_LockMutex(t->mu);
    g = t->out_gen;
    SDL_UnlockMutex(t->mu);
    return g;
}
unsigned long ex_themes_polls(const ExThemes *t) { return (unsigned long)SDL_GetAtomicInt((SDL_AtomicInt *)&t->polls); }

size_t ex_themes_count(ExThemes *t) { size_t n; SDL_LockMutex(t->mu); n = t->nthemes; SDL_UnlockMutex(t->mu); return n; }
char *ex_themes_name(ExThemes *t, size_t i) { char *s = NULL; SDL_LockMutex(t->mu); if (i < t->nthemes) s = dup_str(t->themes[i].name); SDL_UnlockMutex(t->mu); return s; }
size_t ex_themes_snippet_count(ExThemes *t) { size_t n; SDL_LockMutex(t->mu); n = t->nsnips; SDL_UnlockMutex(t->mu); return n; }
char *ex_themes_snippet_name(ExThemes *t, size_t i) { char *s = NULL; SDL_LockMutex(t->mu); if (i < t->nsnips) s = dup_str(t->snips[i].name); SDL_UnlockMutex(t->mu); return s; }

/* ---- editing ---- */

static int valid_token(const char *key, const char *value, char *section, size_t sc, char *name, size_t nc) {
    const char *dot = strchr(key, '.');
    ExColor c;
    if (!dot || dot == key) return 0;
    snprintf(section, sc, "%.*s", (int)(dot - key), key);
    snprintf(name, nc, "%s", dot + 1);
    if (!strcmp(section, "colors")) return ex_color_token_by_name(name) >= 0 && ex_parse_color(value, &c) == 0;
    if (!strcmp(section, "fonts")) return (!strcmp(name, "interface") || !strcmp(name, "text") || !strcmp(name, "mono")) && !strchr(value, '\n');
    if (!strcmp(section, "sizes")) {
        static const char *names[] = { "radius", "radius_large", "spacing", "border_width", "ribbon_width", "tab_height", "line_height" };
        char *end;
        long v = strtol(value, &end, 10);
        size_t k;
        if (!*value || *end || v < 0 || v > 10000) return 0;
        for (k = 0; k < sizeof names / sizeof *names; k++) if (!strcmp(names[k], name)) return 1;
    }
    return 0;
}

char *ex_themes_set_token(ExThemes *t, const char *theme, const char *scheme, int system_dark, const char *key, const char *value) {
    char section[16], name[48], *path = NULL, *text = NULL, *patched, *result = NULL;
    size_t len = 0, plen = 0;
    if (!t || !key || !value || !valid_token(key, value, section, sizeof section, name, sizeof name)) return NULL;
    SDL_LockMutex(t->mu);
    if (theme && theme[0] && !ieq(theme, "exhaust")) { const char *p = path_of(t->themes, t->nthemes, theme); if (p) path = dup_str(p); }
    SDL_UnlockMutex(t->mu);
    if (path) { text = ex_platform_read_file(path, &len); result = dup_str(theme); }
    else {                                                            /* the built-in theme is never changed: a theme file of the user's own is made */
        const char *dir = t->vdir ? t->vdir : t->gdir;
        char *folder, base_text[96];
        if (!dir) return NULL;
        folder = join(dir, "themes");
        if (!folder) return NULL;
        ex_platform_make_dirs(folder);
        path = join(folder, "custom.theme");
        free(folder);
        if (!path) return NULL;
        text = ex_platform_read_file(path, &len);                    /* an earlier Custom is added to, not replaced */
        if (!text) {
            int dark = strcmp(scheme ? scheme : "dark", "light") != 0;
            if (scheme && !strcmp(scheme, "system")) dark = system_dark;
            snprintf(base_text, sizeof base_text, "[theme]\nname = Custom\nbase = %s\n", dark ? "dark" : "light");
            text = dup_str(base_text); len = strlen(base_text);
        }
        result = dup_str("Custom");
    }
    if (!text || !path || !result) { free(text); free(path); free(result); return NULL; }
    patched = ex_ini_patch(text, len, section, name, value, &plen);
    free(text);
    if (!patched || ex_platform_write_file_atomic(path, patched, plen) != 0) { free(patched); free(path); free(result); return NULL; }
    free(patched); free(path);
    SDL_SetAtomicInt(&t->rescan_now, 1);                            /* the new file (or the changed name) shows up at once */
    return result;
}

/* ---- contrast ---- */

size_t ex_theme_contrast_warnings(const ExTheme *t, char (*out)[160], size_t max) {
    static const struct { int fg, bg; double min; const char *what; } pairs[] = {
        { EX_COLOR_TEXT, EX_COLOR_BACKGROUND, 4.5, "Text on the background" },
        { EX_COLOR_TEXT, EX_COLOR_SURFACE, 4.5, "Text on a card" },
        { EX_COLOR_TEXT_MUTED, EX_COLOR_BACKGROUND, 4.5, "Muted text on the background" },
        { EX_COLOR_LINK, EX_COLOR_BACKGROUND, 4.5, "Links on the background" },
        { EX_COLOR_ERROR, EX_COLOR_BACKGROUND, 4.5, "Error text on the background" },
        { EX_COLOR_CODE_TEXT, EX_COLOR_CODE_BACKGROUND, 4.5, "Code text on its background" },
        { EX_COLOR_TEXT_FAINT, EX_COLOR_BACKGROUND, 3.0, "Faint text on the background" },
        { EX_COLOR_ACCENT, EX_COLOR_BACKGROUND, 3.0, "The accent colour on the background" },
    };
    size_t i, n = 0;
    for (i = 0; i < sizeof pairs / sizeof *pairs; i++) {
        double r = ex_contrast_ratio(t->colors[pairs[i].fg], t->colors[pairs[i].bg]);
        if (r + 1e-9 < pairs[i].min) {
            if (n < max) snprintf(out[n], 160, "%s is hard to read: contrast %.1f to 1, at least %.1f to 1 is advised.", pairs[i].what, r, pairs[i].min);
            n++;
        }
    }
    return n < max ? n : max;
}
