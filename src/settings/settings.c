#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_settings.h"
#include "exhaust_worker.h"

enum { JOB_WRITE = 1 };

struct ExSettings {
    char *vault_path;            /* <vault>/.exhaust/settings.ini, or NULL */
    char *global_path;           /* <config>/settings.ini, or NULL */
    char *vault_text; size_t vault_len;
    char *global_text; size_t global_len;
    ExIni *vault, *global;
    ExWorker *worker;
    ExQueue *results;
    SDL_AtomicInt done, pending;
    char *write_path;            /* where changes go */
    char *write_dir;
};

typedef struct WriteJob { char *path, *dir; } WriteJob;

static char *dup_str(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static char *join(const char *dir, const char *name) {
    char *p = malloc(strlen(dir) + strlen(name) + 2);
    if (p) sprintf(p, "%s/%s", dir, name);
    return p;
}

const ExSettingDef *ex_settings_def(const char *section, const char *key) {
    size_t i;
    for (i = 0; i < ex_settings_schema_count; i++)
        if (strcmp(ex_settings_schema[i].section, section) == 0 && strcmp(ex_settings_schema[i].key, key) == 0) return &ex_settings_schema[i];
    return NULL;
}

static int parse_bool(const char *v, int *out) {
    if (!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "1")) { *out = 1; return 1; }
    if (!strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "off") || !strcmp(v, "0")) { *out = 0; return 1; }
    return 0;
}

int ex_settings_valid(const ExSettingDef *def, const char *value) {
    char *end;
    size_t n = strlen(value), i;
    if (strchr(value, '\n') || strchr(value, '\r')) return 0;
    switch (def->type) {
    case EX_SET_BOOL: { int b; return parse_bool(value, &b); }
    case EX_SET_INT: case EX_SET_PERCENT:
        if (!*value) return 0;
        strtol(value, &end, 10);
        return *end == 0;
    case EX_SET_COLOR:
        if (n == 0) return 1;
        if ((n != 7 && n != 9) || value[0] != '#') return 0;
        for (i = 1; i < n; i++) if (!isxdigit((unsigned char)value[i])) return 0;
        return 1;
    case EX_SET_ENUM: {
        const char *p = def->enum_values;
        size_t vl = n;
        while (p && *p) {
            const char *bar = strchr(p, '|');
            size_t l = bar ? (size_t)(bar - p) : strlen(p);
            if (l == vl && memcmp(p, value, l) == 0) return 1;
            p = bar ? bar + 1 : p + l;
        }
        return 0;
    }
    default: return 1;
    }
}

/* ---- the writer ---- */

static void worker_fn(ExWorker *self, const ExMessage *job, void *user) {
    ExSettings *s = user;
    (void)self;
    if (job->type == JOB_WRITE && job->payload) {
        WriteJob *w = job->payload;
        char *body = (char *)job->payload + sizeof *w;
        if (w->dir) ex_platform_make_dirs(w->dir);                 /* `.exhaust` appears with the first change */
        if (ex_platform_write_file_atomic(w->path, body, job->size - sizeof *w) == 0) SDL_AddAtomicInt(&s->done, 1);
        else fprintf(stderr, "exhaust: could not write %s\n", w->path);
        SDL_AddAtomicInt(&s->pending, -1);
    }
}

static void load_one(const char *path, char **text, size_t *len, ExIni **ini) {
    *text = path ? ex_platform_read_file(path, len) : NULL;
    if (!*text) { *text = dup_str(""); *len = 0; }
    *ini = ex_ini_parse(*text, *len);
}

ExSettings *ex_settings_open(const char *vault_config_dir, const char *global_config_dir) {
    ExSettings *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    if (vault_config_dir) s->vault_path = join(vault_config_dir, "settings.ini");
    if (global_config_dir) s->global_path = join(global_config_dir, "settings.ini");
    load_one(s->vault_path, &s->vault_text, &s->vault_len, &s->vault);
    load_one(s->global_path, &s->global_text, &s->global_len, &s->global);
    if (!s->vault || !s->global) { ex_settings_close(s); return NULL; }
    if (!s->vault_path) {      /* no vault: the global file is the one that changes, so it is the text that gets patched */
        free(s->vault_text);
        s->vault_text = dup_str(s->global_text);
        s->vault_len = s->global_len;
        ex_ini_free(s->vault);
        s->vault = ex_ini_parse(s->vault_text, s->vault_len);
        if (!s->vault_text || !s->vault) { ex_settings_close(s); return NULL; }
    }
    s->write_path = dup_str(s->vault_path ? s->vault_path : (s->global_path ? s->global_path : ""));
    s->write_dir = s->vault_path ? dup_str(vault_config_dir) : (s->global_path ? dup_str(global_config_dir) : NULL);
    s->results = ex_queue_create(NULL, NULL);
    s->worker = s->results ? ex_worker_start("settings", 3, worker_fn, s, s->results) : NULL;
    if (!s->worker) { ex_settings_close(s); return NULL; }
    return s;
}

void ex_settings_close(ExSettings *s) {
    if (!s) return;
    if (s->worker) ex_worker_stop(s->worker);          /* waits for the queued writes */
    if (s->results) ex_queue_destroy(s->results);
    ex_ini_free(s->vault); ex_ini_free(s->global);
    free(s->vault_text); free(s->global_text);
    free(s->vault_path); free(s->global_path); free(s->write_path); free(s->write_dir);
    free(s);
}

int ex_settings_writes_done(const ExSettings *s) { return SDL_GetAtomicInt((SDL_AtomicInt *)&s->done); }
int ex_settings_writes_pending(const ExSettings *s) { return SDL_GetAtomicInt((SDL_AtomicInt *)&s->pending); }
void ex_settings_flush(ExSettings *s) { while (ex_settings_writes_pending(s) > 0) SDL_Delay(1); }
const char *ex_settings_write_path(const ExSettings *s) { return s->write_path; }

/* ---- reading ---- */

static const char *file_value(const ExIni *ini, const ExSettingDef *def, const char *section, const char *key) {
    const char *v = ex_ini_get(ini, section, key);
    if (!v) return NULL;
    if (def && !ex_settings_valid(def, v)) return NULL;     /* a value that does not fit is ignored, and stays in the file */
    return v;
}

const char *ex_settings_get(const ExSettings *s, const char *section, const char *key) {
    const ExSettingDef *def = ex_settings_def(section, key);
    const char *v = file_value(s->vault, def, section, key);
    if (v) return v;
    v = file_value(s->global, def, section, key);
    if (v) return v;
    return def ? def->def : NULL;
}

int ex_settings_is_set(const ExSettings *s, const char *section, const char *key) {
    return file_value(s->vault, ex_settings_def(section, key), section, key) != NULL;
}

int ex_settings_get_bool(const ExSettings *s, const char *section, const char *key) {
    const char *v = ex_settings_get(s, section, key);
    int b = 0;
    if (v) parse_bool(v, &b);
    return b;
}

long ex_settings_get_int(const ExSettings *s, const char *section, const char *key) {
    const char *v = ex_settings_get(s, section, key);
    return v && *v ? strtol(v, NULL, 10) : 0;
}

/* ---- writing ---- */

int ex_settings_set(ExSettings *s, const char *section, const char *key, const char *value) {
    const ExSettingDef *def = ex_settings_def(section, key);
    char canon[64];
    char *patched, *copy;
    size_t plen = 0;
    ExMessage m;
    WriteJob *w;
    if (!def || !value || !ex_settings_valid(def, value)) return -1;
    if (def->type == EX_SET_BOOL) {                       /* written as true or false whatever spelling was given */
        int b = 0;
        parse_bool(value, &b);
        if (b && def->stub) return -1;                    /* Sync and Publish are stubs: they cannot be turned on */
        snprintf(canon, sizeof canon, "%s", b ? "true" : "false");
        value = canon;
    }
    patched = ex_ini_patch(s->vault_text, s->vault_len, section, key, value, &plen);
    if (!patched) return -1;
    free(s->vault_text);
    s->vault_text = patched;
    s->vault_len = plen;
    ex_ini_free(s->vault);
    s->vault = ex_ini_parse(patched, plen);
    if (!s->vault) s->vault = ex_ini_new();
    /* when the changes go to the global file (no vault), that is the file the reads must see too */
    if (!s->vault_path && s->global_path) { ex_ini_free(s->global); s->global = ex_ini_parse(patched, plen); if (!s->global) s->global = ex_ini_new(); }
    w = malloc(sizeof *w + plen + 1);
    if (!w) return -1;
    copy = (char *)w + sizeof *w;
    memcpy(copy, patched, plen);
    w->path = s->write_path;
    w->dir = s->write_dir;
    memset(&m, 0, sizeof m);
    m.type = JOB_WRITE;
    m.payload = w;
    m.size = sizeof *w + plen;
    m.free_payload = free;
    SDL_AddAtomicInt(&s->pending, 1);
    if (ex_worker_post(s->worker, &m) != 0) { SDL_AddAtomicInt(&s->pending, -1); free(w); return -1; }
    return 0;
}

int ex_settings_set_bool(ExSettings *s, const char *section, const char *key, int value) { return ex_settings_set(s, section, key, value ? "true" : "false"); }
int ex_settings_set_int(ExSettings *s, const char *section, const char *key, long value) {
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", value);
    return ex_settings_set(s, section, key, buf);
}
