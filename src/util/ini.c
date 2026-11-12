#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_ini.h"

typedef struct Pair { char *key, *value; } Pair;
typedef struct Section { char *name; Pair *pairs; size_t n, cap; } Section;
struct ExIni { Section *sec; size_t n, cap; };

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

ExIni *ex_ini_new(void) { return calloc(1, sizeof(ExIni)); }

static Section *find(const ExIni *ini, const char *name) {
    size_t i;
    for (i = 0; i < ini->n; i++) if (strcmp(ini->sec[i].name, name) == 0) return &ini->sec[i];
    return NULL;
}

static Section *add_section(ExIni *ini, const char *name) {
    Section *s;
    if (ini->n == ini->cap) {
        size_t nc = ini->cap ? ini->cap * 2 : 8;
        Section *ns = realloc(ini->sec, nc * sizeof *ns);
        if (!ns) return NULL;
        ini->sec = ns;
        ini->cap = nc;
    }
    s = &ini->sec[ini->n];
    memset(s, 0, sizeof *s);
    s->name = dup_str(name);
    if (!s->name) return NULL;
    ini->n++;
    return s;
}

static void free_section(Section *s) {
    size_t i;
    for (i = 0; i < s->n; i++) { free(s->pairs[i].key); free(s->pairs[i].value); }
    free(s->pairs);
    free(s->name);
}

void ex_ini_free(ExIni *ini) {
    size_t i;
    if (!ini) return;
    for (i = 0; i < ini->n; i++) free_section(&ini->sec[i]);
    free(ini->sec);
    free(ini);
}

int ex_ini_set(ExIni *ini, const char *section, const char *key, const char *value) {
    Section *s = find(ini, section);
    size_t i;
    char *v;
    if (!s) s = add_section(ini, section);
    if (!s) return -1;
    v = dup_str(value);
    if (!v) return -1;
    for (i = 0; i < s->n; i++) if (strcmp(s->pairs[i].key, key) == 0) { free(s->pairs[i].value); s->pairs[i].value = v; return 0; }
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 8;
        Pair *np = realloc(s->pairs, nc * sizeof *np);
        if (!np) { free(v); return -1; }
        s->pairs = np;
        s->cap = nc;
    }
    s->pairs[s->n].key = dup_str(key);
    s->pairs[s->n].value = v;
    if (!s->pairs[s->n].key) { free(v); return -1; }
    s->n++;
    return 0;
}

int ex_ini_set_int(ExIni *ini, const char *section, const char *key, long long value) {
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", value);
    return ex_ini_set(ini, section, key, buf);
}

const char *ex_ini_get(const ExIni *ini, const char *section, const char *key) {
    const Section *s = find(ini, section);
    size_t i;
    if (!s) return NULL;
    for (i = 0; i < s->n; i++) if (strcmp(s->pairs[i].key, key) == 0) return s->pairs[i].value;
    return NULL;
}

int ex_ini_has_section(const ExIni *ini, const char *section) { return find(ini, section) != NULL; }

void ex_ini_remove_section(ExIni *ini, const char *section) {
    Section *s = find(ini, section);
    size_t i;
    if (!s) return;
    i = (size_t)(s - ini->sec);
    free_section(s);
    memmove(ini->sec + i, ini->sec + i + 1, (ini->n - i - 1) * sizeof *ini->sec);
    ini->n--;
}

void ex_ini_remove_key(ExIni *ini, const char *section, const char *key) {
    Section *s = find(ini, section);
    size_t i;
    if (!s) return;
    for (i = 0; i < s->n; i++)
        if (strcmp(s->pairs[i].key, key) == 0) {
            free(s->pairs[i].key); free(s->pairs[i].value);
            memmove(s->pairs + i, s->pairs + i + 1, (s->n - i - 1) * sizeof *s->pairs);
            s->n--;
            return;
        }
}

size_t ex_ini_section_count(const ExIni *ini) { return ini->n; }
const char *ex_ini_section_name(const ExIni *ini, size_t i) { return i < ini->n ? ini->sec[i].name : NULL; }
size_t ex_ini_key_count(const ExIni *ini, const char *section) { const Section *s = find(ini, section); return s ? s->n : 0; }
const char *ex_ini_key_name(const ExIni *ini, const char *section, size_t i) { const Section *s = find(ini, section); return s && i < s->n ? s->pairs[i].key : NULL; }

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

ExIni *ex_ini_parse(const char *text, size_t len) {
    ExIni *ini = ex_ini_new();
    char *copy = malloc(len + 1), *line, *next;
    char cur[256] = "";
    int have_section = 0;
    if (!ini || !copy) { free(copy); ex_ini_free(ini); return NULL; }
    memcpy(copy, text, len);
    copy[len] = 0;
    if (len >= 3 && (unsigned char)copy[0] == 0xEF && (unsigned char)copy[1] == 0xBB && (unsigned char)copy[2] == 0xBF) memmove(copy, copy + 3, len - 2);
    for (line = copy; line; line = next) {
        char *l, *eq;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        l = trim(line);
        if (!*l || *l == '#' || *l == ';') continue;
        if (*l == '[') {
            char *close = strchr(l, ']');
            if (!close) continue;
            *close = 0;
            snprintf(cur, sizeof cur, "%s", l + 1);
            have_section = 1;
            if (!find(ini, cur) && !add_section(ini, cur)) break;
            continue;
        }
        eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        {
            char *key = trim(l), *val = trim(eq + 1), *hash = strstr(val, " #");
            if (hash) { *hash = 0; val = trim(val); }
            if (*key) ex_ini_set(ini, have_section ? cur : "", key, val);
        }
    }
    free(copy);
    return ini;
}

char *ex_ini_write(const ExIni *ini, size_t *len) {
    size_t i, k, cap = 256, n = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (i = 0; i < ini->n; i++) {
        const Section *s = &ini->sec[i];
        size_t need = strlen(s->name) + 8;
        for (k = 0; k < s->n; k++) need += strlen(s->pairs[k].key) + strlen(s->pairs[k].value) + 8;
        if (n + need + 2 > cap) { char *nb; cap = (n + need + 2) * 2; nb = realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
        if (s->name[0]) n += (size_t)sprintf(buf + n, "[%s]\n", s->name);
        for (k = 0; k < s->n; k++) n += (size_t)sprintf(buf + n, "%s = %s\n", s->pairs[k].key, s->pairs[k].value);
        buf[n++] = '\n';
    }
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

long long ex_ini_get_int(const ExIni *ini, const char *section, const char *key, long long def) {
    const char *v = ex_ini_get(ini, section, key);
    char *end;
    long long x;
    if (!v || !*v) return def;
    x = strtoll(v, &end, 10);
    return *end ? def : x;
}

double ex_ini_get_float(const ExIni *ini, const char *section, const char *key, double def) {
    const char *v = ex_ini_get(ini, section, key);
    char *end;
    double x;
    if (!v || !*v) return def;
    x = strtod(v, &end);
    return *end ? def : x;
}

int ex_ini_get_bool(const ExIni *ini, const char *section, const char *key, int def) {
    const char *v = ex_ini_get(ini, section, key);
    if (!v) return def;
    if (!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "1")) return 1;
    if (!strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "off") || !strcmp(v, "0")) return 0;
    return def;
}
