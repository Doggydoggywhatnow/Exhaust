#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static char *join(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    char *p = malloc(na + nb + 2);
    if (!p) return NULL;
    memcpy(p, a, na);
    if (na && a[na - 1] != '/' && a[na - 1] != '\\') p[na++] = '/';
    memcpy(p + na, b, nb + 1);
    return p;
}

static const char *env(const char *name) {
    const char *v = SDL_getenv(name);
    return v && *v ? v : NULL;
}

static char *home_dir(void) {
    const char *h = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (h && *h) return dup_str(h);
#ifdef _WIN32
    if (env("USERPROFILE")) return dup_str(env("USERPROFILE"));
#else
    if (env("HOME")) return dup_str(env("HOME"));
#endif
    return NULL;
}

char *ex_platform_cache_dir(void) {
#if defined(_WIN32)
    const char *base = env("LOCALAPPDATA");
    char *tmp, *r;
    if (!base) return NULL;
    tmp = join(base, "Exhaust");
    r = tmp ? join(tmp, "Cache") : NULL;
    free(tmp);
    return r;
#elif defined(__APPLE__)
    char *home = home_dir(), *tmp, *r;
    if (!home) return NULL;
    tmp = join(home, "Library/Caches");
    r = tmp ? join(tmp, "Exhaust") : NULL;
    free(home);
    free(tmp);
    return r;
#else
    const char *xdg = env("XDG_CACHE_HOME");
    char *home, *tmp, *r;
    if (xdg && xdg[0] == '/') return join(xdg, "Exhaust");
    home = home_dir();
    if (!home) return NULL;
    tmp = join(home, ".cache");
    r = tmp ? join(tmp, "Exhaust") : NULL;
    free(home);
    free(tmp);
    return r;
#endif
}

static int add_dir(char ***list, int *n, char *path) {
    char **nl;
    if (!path) return 0;
    nl = realloc(*list, (size_t)(*n + 1) * sizeof **list);
    if (!nl) { free(path); return 0; }
    *list = nl;
    nl[(*n)++] = path;
    return 1;
}

int ex_platform_font_dirs(char ***out) {
    char **list = NULL;
    int n = 0;
    char *home = home_dir();
#if defined(_WIN32)
    const char *windir = env("WINDIR");
    const char *local = env("LOCALAPPDATA");
    add_dir(&list, &n, join(windir ? windir : "C:/Windows", "Fonts"));
    if (local) {
        char *t = join(local, "Microsoft");
        char *t2 = t ? join(t, "Windows") : NULL;
        add_dir(&list, &n, t2 ? join(t2, "Fonts") : NULL);
        free(t);
        free(t2);
    }
#elif defined(__APPLE__)
    add_dir(&list, &n, dup_str("/System/Library/Fonts"));
    add_dir(&list, &n, dup_str("/Library/Fonts"));
    if (home) add_dir(&list, &n, join(home, "Library/Fonts"));
#else
    const char *xdg_data = env("XDG_DATA_HOME");
    add_dir(&list, &n, dup_str("/usr/share/fonts"));
    add_dir(&list, &n, dup_str("/usr/local/share/fonts"));
    if (home) add_dir(&list, &n, join(home, ".fonts"));
    if (xdg_data && xdg_data[0] == '/') add_dir(&list, &n, join(xdg_data, "fonts"));
    else if (home) {
        char *t = join(home, ".local/share");
        add_dir(&list, &n, t ? join(t, "fonts") : NULL);
        free(t);
    }
#endif
    free(home);
    *out = list;
    return n;
}

void ex_platform_free_list(char **list, int count) {
    int i;
    for (i = 0; i < count; i++) free(list[i]);
    free(list);
}

/* ---- directory walking ---- */

typedef struct Entry { char *name; } Entry;
typedef struct Names { char **v; int n, cap; } Names;

static SDL_EnumerationResult collect(void *ud, const char *dirname, const char *fname) {
    Names *nm = ud;
    (void)dirname;
    if (nm->n == nm->cap) {
        int nc = nm->cap ? nm->cap * 2 : 32;
        char **nv = realloc(nm->v, (size_t)nc * sizeof *nv);
        if (!nv) return SDL_ENUM_FAILURE;
        nm->v = nv;
        nm->cap = nc;
    }
    nm->v[nm->n] = dup_str(fname);
    if (!nm->v[nm->n]) return SDL_ENUM_FAILURE;
    nm->n++;
    return SDL_ENUM_CONTINUE;
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int walk_dir(const char *dir, ExWalkFn fn, void *user, int depth) {
    Names nm = {0};
    int i, stop = 0;
    if (!SDL_EnumerateDirectory(dir, collect, &nm)) {
        for (i = 0; i < nm.n; i++) free(nm.v[i]);
        free(nm.v);
        return -1;
    }
    if (nm.n > 1) qsort(nm.v, (size_t)nm.n, sizeof *nm.v, cmp_names); /* nm.v is NULL for an empty folder */
    for (i = 0; i < nm.n && !stop; i++) {
        char *full = join(dir, nm.v[i]);
        SDL_PathInfo info;
        if (!full) continue;
        if (SDL_GetPathInfo(full, &info)) {
            int is_dir = info.type == SDL_PATHTYPE_DIRECTORY;
            if (info.type == SDL_PATHTYPE_DIRECTORY || info.type == SDL_PATHTYPE_FILE) {
                stop = fn(full, is_dir, (int64_t)info.modify_time, user);
                if (!stop && is_dir && depth < 24) stop = walk_dir(full, fn, user, depth + 1) == -2; /* the depth limit also ends symbolic link loops */
            }
        }
        free(full);
    }
    for (i = 0; i < nm.n; i++) free(nm.v[i]);
    free(nm.v);
    return stop ? -2 : 0;
}

int ex_platform_walk(const char *dir, ExWalkFn fn, void *user) {
    SDL_PathInfo info;
    int r;
    if (!SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) return -1;
    if (fn(dir, 1, (int64_t)info.modify_time, user)) return 0;
    r = walk_dir(dir, fn, user, 0);
    return r == -1 ? -1 : 0;
}

int ex_platform_make_dirs(const char *path) { return SDL_CreateDirectory(path) ? 0 : -1; }

int ex_platform_file_exists(const char *path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

char *ex_platform_read_file(const char *path, size_t *len) {
    size_t n = 0;
    void *data = SDL_LoadFile(path, &n);
    char *out;
    if (!data) return NULL;
    out = malloc(n + 1);
    if (out) { memcpy(out, data, n); out[n] = 0; if (len) *len = n; }
    SDL_free(data);
    return out;
}

int ex_platform_write_file_atomic(const char *path, const void *data, size_t len) {
    size_t n = strlen(path);
    char *tmp = malloc(n + 8);
    SDL_IOStream *io;
    int ok = 0;
    if (!tmp) return -1;
    memcpy(tmp, path, n);
    memcpy(tmp + n, ".tmp", 5);
    io = SDL_IOFromFile(tmp, "wb");
    if (io) {
        ok = SDL_WriteIO(io, data, len) == len;
        ok = SDL_CloseIO(io) && ok;
    }
    if (ok) ok = SDL_RenamePath(tmp, path);
    if (!ok) SDL_RemovePath(tmp);
    free(tmp);
    return ok ? 0 : -1;
}

/* ---- clipboard ---- */

char *ex_clipboard_get_text(void) {
    char *t = SDL_GetClipboardText(), *out;
    if (!t || !*t) { SDL_free(t); return NULL; }
    out = dup_str(t);
    SDL_free(t);
    return out;
}

int ex_clipboard_set_text(const char *utf8) { return SDL_SetClipboardText(utf8) ? 0 : -1; }
int ex_clipboard_has_text(void) { return SDL_HasClipboardText() ? 1 : 0; }
