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

/* ---- patching the text of a file ---- */

typedef struct Buf { char *p; size_t n, cap; int fail; } Buf;

static void put(Buf *b, const char *s, size_t n) {
    if (b->fail) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = (b->n + n + 1) * 2;
        char *np = realloc(b->p, nc);
        if (!np) { b->fail = 1; return; }
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

/* The section name of a header line, or NULL. Writes into out. */
static int header_name(const char *l, size_t n, char *out, size_t cap) {
    size_t i = 0, e;
    while (i < n && (l[i] == ' ' || l[i] == '\t')) i++;
    if (i >= n || l[i] != '[') return 0;
    for (e = i + 1; e < n && l[e] != ']'; e++) {}
    if (e >= n) return 0;
    snprintf(out, cap, "%.*s", (int)(e - i - 1), l + i + 1);
    return 1;
}

/* Is this line "key = ..." for the key? Returns the offset of the value start (after "="), or 0. */
static size_t key_line(const char *l, size_t n, const char *key) {
    size_t i = 0, ks, ke, kl = strlen(key);
    while (i < n && (l[i] == ' ' || l[i] == '\t')) i++;
    if (i >= n || l[i] == '#' || l[i] == ';' || l[i] == '[') return 0;
    ks = i;
    while (i < n && l[i] != '=') i++;
    if (i >= n) return 0;
    ke = i;
    while (ke > ks && (l[ke - 1] == ' ' || l[ke - 1] == '\t')) ke--;
    if (ke - ks != kl || memcmp(l + ks, key, kl) != 0) return 0;
    return i + 1;
}

char *ex_ini_patch(const char *text, size_t len, const char *section, const char *key, const char *value, size_t *out_len) {
    Buf b = {0};
    size_t pos = 0, insert_at = (size_t)-1, bom = 0, last_content_end = 0;
    int in_section = 0, found_section = 0, done = 0, crlf = 0;
    char name[256];
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) bom = 3;
    { const char *nl = memchr(text, '\n', len); crlf = nl && nl > text && nl[-1] == '\r'; }
    pos = bom;
    /* pass 1: where the value goes */
    {
        size_t p = bom, key_start = (size_t)-1, key_end = 0, val_off = 0;
        last_content_end = bom;
        in_section = section[0] == 0;      /* keys before any header belong to the unnamed section */
        found_section = section[0] == 0;
        while (p < len) {
            const char *l = text + p, *nl = memchr(l, '\n', len - p);
            size_t line_len = nl ? (size_t)(nl - l) + 1 : len - p, content = line_len;
            while (content > 0 && (l[content - 1] == '\n' || l[content - 1] == '\r')) content--;
            if (header_name(l, content, name, sizeof name)) {
                if (in_section && found_section) insert_at = last_content_end;      /* the last block of this section wins */
                in_section = strcmp(name, section) == 0;
                if (in_section) { found_section = 1; last_content_end = p + line_len; }
            } else if (in_section) {
                size_t vo = key_line(l, content, key);
                if (vo) { key_start = p;      /* the last line wins, as when the file is read */ key_end = p + line_len; val_off = vo; }
                {
                    size_t i = 0;
                    while (i < content && (l[i] == ' ' || l[i] == '\t')) i++;
                    if (i < content) last_content_end = p + line_len;   /* not blank */
                }
            }
            p += line_len;
        }
        if (in_section && found_section) insert_at = last_content_end;
        if (key_start != (size_t)-1) {
            /* rewrite that one line */
            const char *l = text + key_start;
            size_t content = key_end - key_start, eol = content, keep_from = 0, i;
            while (eol > 0 && (l[eol - 1] == '\n' || l[eol - 1] == '\r')) eol--;
            for (i = val_off; i + 1 < eol; i++) if (l[i] == ' ' && l[i + 1] == '#') { while (i > val_off + 1 && l[i - 1] == ' ') i--; keep_from = i; break; }   /* a trailing comment stays, with its spacing */
            put(&b, text, key_start);
            {
                size_t k = 0, ke;
                while (k < eol && (l[k] == ' ' || l[k] == '\t')) k++;
                for (ke = k; ke < val_off - 1 && l[ke] != ' ' && l[ke] != '\t' && l[ke] != '='; ke++) {}
                put(&b, l, ke);              /* indentation and the key as written */
                put(&b, " = ", 3);
                put(&b, value, strlen(value));
                if (keep_from) put(&b, l + keep_from, eol - keep_from);
                put(&b, l + eol, content - eol);   /* the line ending as it was */
                if (content == eol && key_end == len) {}   /* last line without a line ending stays without */
            }
            put(&b, text + key_end, len - key_end);
            done = 1;
        }
    }
    if (!done) {
        const char *eol = crlf ? "\r\n" : "\n";
        if (insert_at != (size_t)-1 && found_section) {
            put(&b, text, insert_at);
            if (insert_at > 0 && text[insert_at - 1] != '\n') put(&b, eol, strlen(eol));   /* the last line had no line ending */
            put(&b, key, strlen(key)); put(&b, " = ", 3); put(&b, value, strlen(value)); put(&b, eol, strlen(eol));
            put(&b, text + insert_at, len - insert_at);
        } else {
            put(&b, text, len);
            if (len > bom && text[len - 1] != '\n') put(&b, eol, strlen(eol));
            if (len > bom) put(&b, eol, strlen(eol));
            if (section[0]) { put(&b, "[", 1); put(&b, section, strlen(section)); put(&b, "]", 1); put(&b, eol, strlen(eol)); }
            put(&b, key, strlen(key)); put(&b, " = ", 3); put(&b, value, strlen(value)); put(&b, eol, strlen(eol));
        }
    }
    (void)pos;
    if (b.fail) { free(b.p); return NULL; }
    if (!b.p) { b.p = malloc(1); if (!b.p) return NULL; b.p[0] = 0; }
    if (out_len) *out_len = b.n;
    return b.p;
}
