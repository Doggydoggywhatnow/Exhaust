/* File operations that differ between operating systems. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* realpath, lstat, link and localtime_r under -std=c11 */
#define _XOPEN_SOURCE 700
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static char *join2(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    char *p = malloc(na + nb + 2);
    if (!p) return NULL;
    memcpy(p, a, na);
    if (na && a[na - 1] != '/' && a[na - 1] != '\\') p[na++] = '/';
    memcpy(p + na, b, nb + 1);
    return p;
}

#ifdef _WIN32
static wchar_t *to_wide(const char *utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)(n + 2) * sizeof *w) : NULL;
    if (w) { MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n); w[n] = 0; w[n + 1] = 0; }
    return w;
}
static char *from_wide(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = n > 0 ? malloc((size_t)n) : NULL;
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
#endif

/* ---- config folder ---- */

char *ex_platform_config_dir(void) {
    const char *base;
    char *tmp, *r;
#if defined(_WIN32)
    base = SDL_getenv("APPDATA");
    if (!base || !*base) return NULL;
    return join2(base, "Exhaust");
#elif defined(__APPLE__)
    base = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!base || !*base) return NULL;
    tmp = join2(base, "Library/Application Support");
    r = tmp ? join2(tmp, "Exhaust") : NULL;
    free(tmp);
    return r;
#else
    base = SDL_getenv("XDG_CONFIG_HOME");
    if (base && base[0] == '/') return join2(base, "Exhaust");
    base = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!base || !*base) return NULL;
    tmp = join2(base, ".config");
    r = tmp ? join2(tmp, "Exhaust") : NULL;
    free(tmp);
    return r;
#endif
}

/* ---- realpath ---- */

char *ex_platform_realpath(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path), *out;
    HANDLE h;
    DWORD n;
    char *r = NULL;
    if (!w) return NULL;
    h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    n = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED);
    out = n ? malloc((size_t)(n + 1) * sizeof *out) : NULL;
    if (out && GetFinalPathNameByHandleW(h, out, n + 1, FILE_NAME_NORMALIZED)) {
        const wchar_t *p = out;
        size_t i;
        if (wcsncmp(p, L"\\\\?\\UNC\\", 8) == 0) { /* keep UNC paths as \\server\share */
            out[6] = L'\\';
            p = out + 6;
        } else if (wcsncmp(p, L"\\\\?\\", 4) == 0) {
            p += 4;
        }
        r = from_wide(p);
        if (r) for (i = 0; r[i]; i++) if (r[i] == '\\') r[i] = '/';
    }
    free(out);
    CloseHandle(h);
    return r;
#else
    char buf[PATH_MAX];
    if (!realpath(path, buf)) return NULL;
    return dup_str(buf);
#endif
}

int ex_platform_path_info(const char *path, ExPathInfo *out) {
    SDL_PathInfo i;
    memset(out, 0, sizeof *out);
    if (!SDL_GetPathInfo(path, &i)) return -1;
    out->exists = 1;
    out->is_dir = i.type == SDL_PATHTYPE_DIRECTORY;
    out->is_file = i.type == SDL_PATHTYPE_FILE;
    out->size = (int64_t)i.size;
    out->mtime = (int64_t)i.modify_time;
    out->ctime = (int64_t)i.create_time;
    return 0;
}

/* ---- rename without replacing ---- */

int ex_platform_rename_noreplace(const char *from, const char *to) {
#ifdef _WIN32
    wchar_t *a = to_wide(from), *b = to_wide(to);
    int ok = 0;
    if (a && b) ok = MoveFileExW(a, b, 0) != 0; /* fails if the target exists */
    free(a); free(b);
    return ok ? 0 : -1;
#else
    ExPathInfo info;
    struct stat st;
    if (ex_platform_path_info(to, &info) == 0) return -1;
    if (lstat(to, &st) == 0) return -1;              /* a dangling symbolic link is still in the way */
    if (lstat(from, &st) != 0) return -1;
    if (S_ISREG(st.st_mode)) {
        /* link() fails if the target exists, so there is no race with another creator */
        if (link(from, to) != 0) return rename(from, to) == 0 && errno == 0 ? 0 : -1;
        return unlink(from) == 0 ? 0 : -1;
    }
    return rename(from, to) == 0 ? 0 : -1;           /* folders and links: checked above */
#endif
}

/* ---- removing ---- */

typedef struct Names { char **v; int n, cap; } Names;

static SDL_EnumerationResult collect(void *ud, const char *dirname, const char *fname) {
    Names *nm = ud;
    (void)dirname;
    if (nm->n == nm->cap) {
        int nc = nm->cap ? nm->cap * 2 : 16;
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

static int is_real_dir(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode); /* a symbolic link to a folder is not a folder here */
#endif
}

int ex_platform_remove_tree(const char *path) {
    int rc = 0;
    if (is_real_dir(path)) {
        Names nm = {0};
        int i;
        if (!SDL_EnumerateDirectory(path, collect, &nm)) rc = -1;
        for (i = 0; i < nm.n; i++) {
            char *child = join2(path, nm.v[i]);
            if (!child || ex_platform_remove_tree(child) != 0) rc = -1;
            free(child);
            free(nm.v[i]);
        }
        free(nm.v);
    }
    if (!SDL_RemovePath(path)) rc = -1;
    return rc;
}

/* ---- copying (for moving to a trash on another drive) ---- */

static int copy_tree(const char *from, const char *to) {
    if (is_real_dir(from)) {
        Names nm = {0};
        int i, rc = 0;
        if (SDL_CreateDirectory(to) == 0) return -1;
        if (!SDL_EnumerateDirectory(from, collect, &nm)) rc = -1;
        for (i = 0; i < nm.n; i++) {
            char *a = join2(from, nm.v[i]), *b = join2(to, nm.v[i]);
            if (!a || !b || copy_tree(a, b) != 0) rc = -1;
            free(a); free(b); free(nm.v[i]);
        }
        free(nm.v);
        return rc;
    }
    return SDL_CopyFile(from, to) ? 0 : -1;
}

/* ---- trash ---- */

#if defined(_WIN32)

int ex_platform_trash_system(const char *path) {
    wchar_t *w = to_wide(path);
    SHFILEOPSTRUCTW op;
    int rc;
    if (!w) return -1;
    memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = w; /* double-NUL terminated by to_wide */
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    rc = SHFileOperationW(&op);
    free(w);
    return rc == 0 && !op.fAnyOperationsAborted ? 0 : -1;
}

#elif defined(__APPLE__)
/* ex_platform_trash_system is in trash_mac.m */
#else

static void url_encode(const char *s, char *out) {
    static const char H[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
            *out++ = (char)c;
        else { *out++ = '%'; *out++ = H[c >> 4]; *out++ = H[c & 15]; }
    }
    *out = 0;
}

int ex_platform_trash_system(const char *path) {
    const char *data = SDL_getenv("XDG_DATA_HOME");
    char *base = NULL, *trash = NULL, *files = NULL, *info = NULL, *abs = NULL, *name, *target = NULL, *info_file = NULL;
    char *enc = NULL;
    int rc = -1, n;
    time_t now = time(NULL);
    struct tm tmv;
    char date[32];
    FILE *f;

    abs = ex_platform_realpath(path);
    if (!abs) return -1;
    if (data && data[0] == '/') base = dup_str(data);
    else {
        const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
        char *t = home ? join2(home, ".local/share") : NULL;
        base = t;
    }
    if (!base) goto done;
    trash = join2(base, "Trash");
    files = trash ? join2(trash, "files") : NULL;
    info = trash ? join2(trash, "info") : NULL;
    if (!files || !info || !SDL_CreateDirectory(files) || !SDL_CreateDirectory(info)) goto done;
    name = strrchr(path, '/');
    name = name ? name + 1 : (char *)path;
    /* claim a name: the .trashinfo file is created exclusively, so two deletions cannot collide */
    for (n = 0; n < 10000; n++) {
        char suffix[16], *candidate;
        size_t len;
        if (n == 0) suffix[0] = 0; else snprintf(suffix, sizeof suffix, ".%d", n + 1);
        len = strlen(name) + strlen(suffix) + 16;
        candidate = malloc(len);
        if (!candidate) goto done;
        snprintf(candidate, len, "%s%s", name, suffix);
        free(info_file);
        info_file = malloc(strlen(info) + len + 16);
        if (!info_file) { free(candidate); goto done; }
        sprintf(info_file, "%s/%s.trashinfo", info, candidate);
        f = fopen(info_file, "wx");
        if (f) {
            enc = malloc(strlen(abs) * 3 + 1);
            if (!enc) { fclose(f); remove(info_file); free(candidate); goto done; }
            url_encode(abs, enc);
            localtime_r(&now, &tmv);
            strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tmv);
            fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc, date);
            if (fclose(f) != 0) { remove(info_file); free(candidate); goto done; }
            target = join2(files, candidate);
            free(candidate);
            break;
        }
        free(candidate);
    }
    if (!target) goto done;
    if (rename(abs, target) == 0) rc = 0;
    else if (errno == EXDEV && copy_tree(abs, target) == 0 && ex_platform_remove_tree(abs) == 0) rc = 0; /* another drive */
    if (rc != 0) remove(info_file);
done:
    free(abs); free(base); free(trash); free(files); free(info); free(target); free(info_file); free(enc);
    return rc;
}

#endif

/* ---- opening in other programs ---- */

static char *file_url(const char *path) {
    static const char H[] = "0123456789ABCDEF";
    size_t n = strlen(path);
    char *url = malloc(n * 3 + 16), *o;
    size_t i;
    if (!url) return NULL;
    o = url + sprintf(url, "file://");
    if (path[0] != '/') *o++ = '/'; /* Windows drive paths */
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)path[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':')
            *o++ = (char)c;
        else { *o++ = '%'; *o++ = H[c >> 4]; *o++ = H[c & 15]; }
    }
    *o = 0;
    return url;
}

int ex_platform_open_default(const char *path) {
    char *url = file_url(path);
    int ok;
    if (!url) return -1;
    ok = SDL_OpenURL(url);
    free(url);
    return ok ? 0 : -1;
}

int ex_platform_show_in_file_manager(const char *path) {
    ExPathInfo info;
    char *dir = dup_str(path), *slash;
    int rc;
    if (!dir) return -1;
    if (ex_platform_path_info(path, &info) == 0 && !info.is_dir) {
        slash = strrchr(dir, '/');
        if (slash && slash != dir) *slash = 0;
    }
    rc = ex_platform_open_default(dir);
    free(dir);
    return rc;
}
