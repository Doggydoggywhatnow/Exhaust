/* Hotkeys (sub-slice 7c). See include/exhaust_hotkeys.h and "hotkeys.ini" in docs/formats.md. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_commands.h"
#include "exhaust_hotkeys.h"
#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_worker.h"

/* ---- chords ---- */

static const struct { const char *name; int key; } named[] = {
    { "Enter", EX_HK_ENTER }, { "Tab", EX_HK_TAB }, { "Escape", EX_HK_ESCAPE }, { "Space", EX_HK_SPACE }, { "Backspace", EX_HK_BACKSPACE },
    { "Delete", EX_HK_DELETE }, { "Insert", EX_HK_INSERT }, { "Home", EX_HK_HOME }, { "End", EX_HK_END }, { "PageUp", EX_HK_PAGEUP },
    { "PageDown", EX_HK_PAGEDOWN }, { "Left", EX_HK_LEFT }, { "Right", EX_HK_RIGHT }, { "Up", EX_HK_UP }, { "Down", EX_HK_DOWN },
};

static int ieq(const char *a, size_t n, const char *b) { size_t i; if (strlen(b) != n) return 0; for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0; return 1; }

int ex_chord_parse(const char *text, int mac, ExChord *out) {
    const char *p = text;
    unsigned mods = 0;
    int key = 0;
    size_t i;
    if (!text || !out) return -1;
    while (*p == ' ') p++;
    for (;;) {
        const char *plus = strchr(p, '+');
        const char *end = p + strlen(p);
        size_t n;
        unsigned bit = 0;
        if (*p == '+' && p[1] == 0) { key = '+'; break; }                 /* a '+' as the key itself ("Ctrl++") */
        while (end > p && end[-1] == ' ') end--;
        if (!plus) {                                                  /* the key */
            n = (size_t)(end - p);
            if (n == 0) return -1;
            if (n == 1) {
                unsigned char c = (unsigned char)p[0];
                if (isalpha(c)) key = tolower(c);
                else if (isdigit(c) || strchr(",./;'[]\\-=`+", c)) key = c;
                else return -1;
            } else if (p[0] == 'F' || p[0] == 'f') {
                char *e;
                long f = strtol(p + 1, &e, 10);
                if (e != p + n || f < 1 || f > 12) { key = 0; }
                else key = EX_HK_F1 + (int)f - 1;
            }
            if (!key) for (i = 0; i < sizeof named / sizeof *named; i++) if (ieq(p, n, named[i].name)) key = named[i].key;
            if (!key) return -1;
            break;
        }
        n = (size_t)(plus - p);
        while (n && p[n - 1] == ' ') n--;
        if (ieq(p, n, "Ctrl") || ieq(p, n, "Control")) bit = EX_HK_CTRL;
        else if (ieq(p, n, "Shift")) bit = EX_HK_SHIFT;
        else if (ieq(p, n, "Alt") || ieq(p, n, "Option")) bit = EX_HK_ALT;
        else if (ieq(p, n, "Meta") || ieq(p, n, "Cmd") || ieq(p, n, "Command")) bit = EX_HK_META;
        else if (ieq(p, n, "Mod")) bit = mac ? EX_HK_META : EX_HK_CTRL;
        else return -1;
        if (mods & bit) return -1;                                  /* the same modifier twice */
        mods |= bit;
        p = plus + 1;
        while (*p == ' ') p++;
    }
    out->mods = mods; out->key = key;
    return 0;
}

size_t ex_chord_format(const ExChord *c, char *out, size_t cap) {
    char buf[48];
    size_t n = 0, i;
    int found = 0;
    if (out && cap) out[0] = 0;                              /* a buffer that is too small, or a bad chord, still holds a string */
    if (!c || c->key == 0) return 0;
    if (c->mods & EX_HK_CTRL) n += (size_t)snprintf(buf + n, sizeof buf - n, "Ctrl+");
    if (c->mods & EX_HK_ALT) n += (size_t)snprintf(buf + n, sizeof buf - n, "Alt+");
    if (c->mods & EX_HK_SHIFT) n += (size_t)snprintf(buf + n, sizeof buf - n, "Shift+");
    if (c->mods & EX_HK_META) n += (size_t)snprintf(buf + n, sizeof buf - n, "Meta+");
    if (c->key >= EX_HK_F1 && c->key < EX_HK_F1 + 12) { n += (size_t)snprintf(buf + n, sizeof buf - n, "F%d", c->key - EX_HK_F1 + 1); found = 1; }
    else if (c->key < 256) { n += (size_t)snprintf(buf + n, sizeof buf - n, "%c", isalpha(c->key) ? toupper(c->key) : c->key); found = 1; }
    else for (i = 0; i < sizeof named / sizeof *named; i++) if (named[i].key == c->key) { n += (size_t)snprintf(buf + n, sizeof buf - n, "%s", named[i].name); found = 1; }
    if (!found || n >= sizeof buf || n + 1 > cap) return 0;
    memcpy(out, buf, n + 1);
    return n;
}

static int chord_eq(const ExChord *a, const ExChord *b) { return a->mods == b->mods && a->key == b->key; }

/* ---- the state ---- */

typedef struct Custom { char *id; char **sc; size_t n; } Custom;     /* a line of hotkeys.ini: the id and its shortcuts as written */

struct ExHotkeys {
    char *path, *dir;
    char *text; size_t len;
    int mac;
    Custom *c; size_t nc;
    ExWorker *worker;
    ExQueue *results;
    SDL_AtomicInt pending;
    unsigned long version;
};

typedef struct WriteJob { char *path, *dir; } WriteJob;
enum { JOB_WRITE = 1 };

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }

static void worker_fn(ExWorker *self, const ExMessage *job, void *user) {
    ExHotkeys *h = user;
    (void)self;
    if (job->type == JOB_WRITE && job->payload) {
        WriteJob *w = job->payload;
        char *body = (char *)job->payload + sizeof *w;
        if (w->dir) ex_platform_make_dirs(w->dir);
        if (ex_platform_write_file_atomic(w->path, body, job->size - sizeof *w) != 0) fprintf(stderr, "exhaust: could not write %s\n", w->path);
        SDL_AddAtomicInt(&h->pending, -1);
    }
}

static void free_custom(ExHotkeys *h) {
    size_t i, k;
    for (i = 0; i < h->nc; i++) { for (k = 0; k < h->c[i].n; k++) free(h->c[i].sc[k]); free(h->c[i].sc); free(h->c[i].id); }
    free(h->c);
    h->c = NULL; h->nc = 0;
}

static Custom *find_custom(const ExHotkeys *h, const char *id) {
    size_t i;
    for (i = 0; i < h->nc; i++) if (!strcmp(h->c[i].id, id)) return &h->c[i];
    return NULL;
}

static void trim(char *s) {
    size_t n = strlen(s), b = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[b] && isspace((unsigned char)s[b])) b++;
    if (b) memmove(s, s + b, strlen(s + b) + 1);
}

/* The comma that ends a shortcut in a list: not the one that is the key itself (`Ctrl+,`). */
static char *list_comma(char *s) {
    size_t i;
    for (i = 0; s[i]; i++) if (s[i] == ',' && !(i > 0 && s[i - 1] == '+')) return &s[i];
    return NULL;
}

static int add_raw(Custom *c, const char *s) {
    char **n = realloc(c->sc, (c->n + 1) * sizeof *n);
    if (!n) return -1;
    c->sc = n;
    c->sc[c->n] = dup_str(s);
    if (!c->sc[c->n]) return -1;
    c->n++;
    return 0;
}

/* Reads the [hotkeys] section of the text into the list of lines. */
static void parse_text(ExHotkeys *h) {
    ExIni *ini = ex_ini_parse(h->text, h->len);
    size_t i, nk;
    free_custom(h);
    if (!ini) return;
    nk = ex_ini_key_count(ini, "hotkeys");
    for (i = 0; i < nk; i++) {
        const char *key = ex_ini_key_name(ini, "hotkeys", i), *val = ex_ini_get(ini, "hotkeys", key);
        Custom *c;
        char *copy, *tok, *save;
        Custom *grown = realloc(h->c, (h->nc + 1) * sizeof *grown);
        if (!grown || !key || !val) continue;
        h->c = grown;
        c = &h->c[h->nc];
        memset(c, 0, sizeof *c);
        c->id = dup_str(key);
        copy = dup_str(val);
        if (!c->id || !copy) { free(c->id); free(copy); continue; }
        h->nc++;
        for (tok = copy; tok; tok = save) {                             /* a comma separated list: no strtok (not portable to every compiler we build with) */
            char *comma = list_comma(tok);
            save = comma ? comma + 1 : NULL;
            if (comma) *comma = 0;
            trim(tok);
            if (*tok) add_raw(c, tok);
        }
        free(copy);
    }
    ex_ini_free(ini);
}

/* ---- the defaults ---- */

static const ExCommandDef *def_of(const char *id) {
    size_t i;
    for (i = 0; i < ex_command_table_count; i++) if (!strcmp(ex_command_table[i].id, id)) return &ex_command_table[i];
    return NULL;
}

/* The default shortcuts of a command as chords. On macOS a default written with Ctrl is Cmd, except the ones that move between tabs and pages
 * (Ctrl+Tab, Ctrl+PageUp, Ctrl+PageDown), which stay Ctrl, and Alt is Option (formats.md). */
static size_t default_chords(const ExHotkeys *h, const char *id, ExChord *out, size_t max) {
    const ExCommandDef *d = def_of(id);
    char buf[256], *tok, *save;
    size_t n = 0;
    if (!d || !d->default_keys[0]) return 0;
    snprintf(buf, sizeof buf, "%s", d->default_keys);
    for (tok = buf; tok && n < max; tok = save) {
        char *comma = list_comma(tok);
        ExChord c;
        save = comma ? comma + 1 : NULL;
        if (comma) *comma = 0;
        trim(tok);
        if (!*tok || ex_chord_parse(tok, h->mac, &c) != 0) continue;
        if (h->mac && (c.mods & EX_HK_CTRL) && c.key != EX_HK_TAB && c.key != EX_HK_PAGEUP && c.key != EX_HK_PAGEDOWN) { c.mods &= ~(unsigned)EX_HK_CTRL; c.mods |= EX_HK_META; }
        out[n++] = c;
    }
    return n;
}

/* The chords a command has now (custom line, else defaults). Strings that do not parse are left out. */
static size_t effective_chords(const ExHotkeys *h, const char *id, ExChord *out, size_t max) {
    Custom *c = find_custom(h, id);
    size_t i, n = 0;
    if (!c) return default_chords(h, id, out, max);
    for (i = 0; i < c->n && n < max; i++) {
        ExChord ch;
        if (ex_chord_parse(c->sc[i], h->mac, &ch) == 0) {
            size_t k;
            for (k = 0; k < n; k++) if (chord_eq(&out[k], &ch)) break;
            if (k == n) out[n++] = ch;
        }
    }
    return n;
}

static int same_set(const ExChord *a, size_t na, const ExChord *b, size_t nb) {
    size_t i, k;
    if (na != nb) return 0;
    for (i = 0; i < na; i++) { for (k = 0; k < nb; k++) if (chord_eq(&a[i], &b[k])) break; if (k == nb) return 0; }
    return 1;
}

/* ---- the file ---- */

/* Removes the line `id = ...` of the [hotkeys] section from the text; everything else stays. */
static char *remove_line(const char *text, size_t len, const char *id, size_t *out_len) {
    char *out = malloc(len + 1);
    size_t pos = 0, o = 0, idl = strlen(id);
    int in_section = 0;
    if (!out) return NULL;
    while (pos < len) {
        size_t e = pos, next, b;
        int drop = 0;
        while (e < len && text[e] != '\n') e++;
        next = e < len ? e + 1 : e;
        b = pos;
        while (b < e && (text[b] == ' ' || text[b] == '\t')) b++;
        if (b < e && text[b] == '[') {
            const char *close = memchr(text + b, ']', e - b);
            in_section = close && (size_t)(close - (text + b)) == strlen("[hotkeys") && !strncmp(text + b, "[hotkeys", 8);
        } else if (in_section && b + idl <= e && !strncmp(text + b, id, idl)) {
            size_t k = b + idl;
            while (k < e && (text[k] == ' ' || text[k] == '\t')) k++;
            if (k < e && text[k] == '=') drop = 1;
        }
        if (!drop) { memcpy(out + o, text + pos, next - pos); o += next - pos; }
        pos = next;
    }
    out[o] = 0;
    *out_len = o;
    return out;
}

static void queue_write(ExHotkeys *h) {
    WriteJob *w;
    ExMessage m;
    char *copy;
    if (!h->path || !h->worker) return;
    w = malloc(sizeof *w + h->len + 1);
    if (!w) return;
    copy = (char *)w + sizeof *w;
    memcpy(copy, h->text, h->len);
    w->path = h->path; w->dir = h->dir;
    memset(&m, 0, sizeof m);
    m.type = JOB_WRITE; m.payload = w; m.size = sizeof *w + h->len; m.free_payload = free;
    SDL_AddAtomicInt(&h->pending, 1);
    if (ex_worker_post(h->worker, &m) != 0) { SDL_AddAtomicInt(&h->pending, -1); free(w); }
}

/* Stores the list of a command: the line for it, or no line if the list is the defaults. */
static int store(ExHotkeys *h, const char *id, char **raw, size_t n) {
    ExChord now[32], defs[32];
    size_t nn = 0, nd = default_chords(h, id, defs, 32), i;
    char *patched;
    size_t plen = 0;
    for (i = 0; i < n && nn < 32; i++) { ExChord c; if (ex_chord_parse(raw[i], h->mac, &c) == 0) { size_t k; for (k = 0; k < nn; k++) if (chord_eq(&now[k], &c)) break; if (k == nn) now[nn++] = c; } }
    if (same_set(now, nn, defs, nd) && n == nn) patched = remove_line(h->text, h->len, id, &plen);
    else {
        char value[640];
        size_t o = 0;
        value[0] = 0;
        for (i = 0; i < n; i++) o += (size_t)snprintf(value + o, sizeof value - o, "%s%s", i ? ", " : "", raw[i]);
        patched = ex_ini_patch(h->text, h->len, "hotkeys", id, value, &plen);
    }
    if (!patched) return -1;
    free(h->text);
    h->text = patched; h->len = plen;
    parse_text(h);
    h->version++;
    queue_write(h);
    return 0;
}

ExHotkeys *ex_hotkeys_open(const char *config_dir, int mac) {
    ExHotkeys *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->mac = mac != 0;
    if (config_dir) {
        h->dir = dup_str(config_dir);
        h->path = malloc(strlen(config_dir) + 14);
        if (h->path) sprintf(h->path, "%s/hotkeys.ini", config_dir);
        h->text = h->path ? ex_platform_read_file(h->path, &h->len) : NULL;
        h->results = ex_queue_create(NULL, NULL);
        h->worker = h->results ? ex_worker_start("hotkeys", 4, worker_fn, h, h->results) : NULL;
    }
    if (!h->text) { h->text = dup_str(""); h->len = 0; }
    if (!h->text) { ex_hotkeys_close(h); return NULL; }
    parse_text(h);
    return h;
}

void ex_hotkeys_flush(ExHotkeys *h) { while (SDL_GetAtomicInt(&h->pending) > 0) SDL_Delay(1); }

void ex_hotkeys_close(ExHotkeys *h) {
    if (!h) return;
    if (h->worker) ex_worker_stop(h->worker);
    if (h->results) ex_queue_destroy(h->results);
    free_custom(h);
    free(h->text); free(h->path); free(h->dir);
    free(h);
}

unsigned long ex_hotkeys_version(const ExHotkeys *h) { return h ? h->version : 0; }

/* ---- the questions and the changes ---- */

size_t ex_hotkeys_get(const ExHotkeys *h, const char *id, char (*out)[48], size_t max) {
    ExChord ch[32];
    size_t n, i, w = 0;
    if (!h || !id || !def_of(id)) return 0;
    n = effective_chords(h, id, ch, 32);
    for (i = 0; i < n && w < max; i++) if (ex_chord_format(&ch[i], out[w], 48)) w++;
    return w;
}

int ex_hotkeys_is_custom(const ExHotkeys *h, const char *id) { return h && id && find_custom(h, id) != NULL; }

/* The list of a command as text to edit: the custom line as it is, else the defaults as canonical text. */
static char **raw_list(const ExHotkeys *h, const char *id, size_t *n) {
    char **raw;
    size_t i;
    Custom *c = find_custom(h, id);
    *n = 0;
    if (c) {
        raw = calloc(c->n + 2, sizeof *raw);
        if (!raw) return NULL;
        for (i = 0; i < c->n; i++) raw[i] = dup_str(c->sc[i]);
        *n = c->n;
    } else {
        ExChord defs[32];
        size_t nd = default_chords(h, id, defs, 32);
        raw = calloc(nd + 2, sizeof *raw);
        if (!raw) return NULL;
        for (i = 0; i < nd; i++) { char t[48]; if (ex_chord_format(&defs[i], t, sizeof t)) raw[(*n)++] = dup_str(t); }
    }
    return raw;
}

static void free_list(char **raw, size_t n) { size_t i; if (!raw) return; for (i = 0; i < n; i++) free(raw[i]); free(raw); }

int ex_hotkeys_add(ExHotkeys *h, const char *id, const char *shortcut) {
    ExChord c, have[32];
    char text[48], **raw, **grown;
    size_t n, nh, i;
    int rc;
    if (!h || !id || !def_of(id) || ex_chord_parse(shortcut, h->mac, &c) != 0 || !ex_chord_format(&c, text, sizeof text)) return -1;
    nh = effective_chords(h, id, have, 32);
    for (i = 0; i < nh; i++) if (chord_eq(&have[i], &c)) return 1;
    raw = raw_list(h, id, &n);
    if (!raw) return -1;
    grown = realloc(raw, (n + 2) * sizeof *raw);
    if (!grown) { free_list(raw, n); return -1; }
    raw = grown;
    raw[n] = dup_str(text);
    rc = raw[n] ? store(h, id, raw, n + 1) : -1;
    free_list(raw, n + 1);
    return rc;
}

int ex_hotkeys_remove(ExHotkeys *h, const char *id, const char *shortcut) {
    ExChord c;
    char **raw;
    size_t n, i, w = 0;
    int found = 0, rc;
    if (!h || !id || !def_of(id) || ex_chord_parse(shortcut, h->mac, &c) != 0) return -1;
    raw = raw_list(h, id, &n);
    if (!raw) return -1;
    for (i = 0; i < n; i++) {
        ExChord x;
        if (ex_chord_parse(raw[i], h->mac, &x) == 0 && chord_eq(&x, &c)) { free(raw[i]); raw[i] = NULL; found = 1; }
        else raw[w++] = raw[i];
    }
    if (!found) { free_list(raw, n); return 1; }
    rc = store(h, id, raw, w);
    for (i = 0; i < w; i++) free(raw[i]);
    free(raw);
    return rc;
}

int ex_hotkeys_reset(ExHotkeys *h, const char *id) {
    char *patched;
    size_t plen = 0;
    if (!h || !id || !def_of(id)) return -1;
    if (!find_custom(h, id)) return 0;
    patched = remove_line(h->text, h->len, id, &plen);
    if (!patched) return -1;
    free(h->text);
    h->text = patched; h->len = plen;
    parse_text(h);
    h->version++;
    queue_write(h);
    return 0;
}

size_t ex_hotkeys_conflicts(const ExHotkeys *h, const char *id, const char *shortcut, const char **ids, size_t max) {
    ExChord c;
    size_t i, n = 0;
    if (!h || !shortcut || ex_chord_parse(shortcut, h->mac, &c) != 0) return 0;
    for (i = 0; i < ex_command_table_count; i++) {
        ExChord have[32];
        size_t nh, k;
        if (id && !strcmp(ex_command_table[i].id, id)) continue;
        nh = effective_chords(h, ex_command_table[i].id, have, 32);
        for (k = 0; k < nh; k++) if (chord_eq(&have[k], &c)) { if (n < max) ids[n] = ex_command_table[i].id; n++; break; }
    }
    return n < max ? n : max;
}

size_t ex_hotkeys_find(const ExHotkeys *h, const ExChord *c, const char **ids, size_t max) {
    size_t i, n = 0;
    if (!h || !c) return 0;
    for (i = 0; i < ex_command_table_count; i++) {
        ExChord have[32];
        size_t nh = effective_chords(h, ex_command_table[i].id, have, 32), k;
        for (k = 0; k < nh; k++) if (chord_eq(&have[k], c)) { if (n < max) ids[n] = ex_command_table[i].id; n++; break; }
    }
    return n < max ? n : max;
}
