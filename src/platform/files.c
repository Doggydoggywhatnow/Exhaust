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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
static wchar_t *to_wide(const char *utf8);
#endif

/* Reads a whole file: one allocation, and on POSIX systems just open, fstat, read and close (searching reads thousands of notes). */
char *ex_platform_read_file(const char *path, size_t *len) {
#ifdef _WIN32
    wchar_t *wp = to_wide(path);
    HANDLE h;
    LARGE_INTEGER size;
    char *out = NULL;
    size_t got = 0;
    if (!wp) return NULL;
    h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart < 0x7FFFFFFF) {
        out = malloc((size_t)size.QuadPart + 1);
        while (out && got < (size_t)size.QuadPart) {
            DWORD r = 0;
            if (!ReadFile(h, out + got, (DWORD)((size_t)size.QuadPart - got), &r, NULL) || r == 0) break;
            got += r;
        }
        if (out) { out[got] = 0; if (len) *len = got; }
    }
    CloseHandle(h);
    return out;
#else
    struct stat st;
    char *out;
    size_t got = 0, cap;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    cap = (size_t)st.st_size;
    out = malloc(cap + 1);
    if (!out) { close(fd); return NULL; }
    while (got < cap) {
        ssize_t r = read(fd, out + got, cap - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    out[got] = 0;
    if (len) *len = got;
    return out;
#endif
}

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

/* ---- local data folder ---- */

char *ex_platform_local_data_dir(void) {
    const char *base;
    char *tmp, *r;
#if defined(_WIN32)
    base = SDL_getenv("LOCALAPPDATA");
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
    base = SDL_getenv("XDG_DATA_HOME");
    if (base && base[0] == '/') return join2(base, "Exhaust");
    base = SDL_GetUserFolder(SDL_FOLDER_HOME);
    if (!base || !*base) return NULL;
    tmp = join2(base, ".local/share");
    r = tmp ? join2(tmp, "Exhaust") : NULL;
    free(tmp);
    return r;
#endif
}

uint64_t ex_platform_now_ms(void) { return SDL_GetTicks(); }
int64_t ex_platform_unix_time(void) { return (int64_t)time(NULL); }

/* ---- saving ---- */

static char g_fault_message[160];
static int g_fault_code, g_fault_on;

void ex_platform_set_save_fault(const char *message, int error_code) {
    if (message) { snprintf(g_fault_message, sizeof g_fault_message, "%s", message); g_fault_code = error_code; g_fault_on = 1; }
    else g_fault_on = 0;
}

static void fail(ExSaveResult *res, int code, const char *what) {
    if (!res) return;
    res->ok = 0;
    res->error_code = code;
    snprintf(res->message, sizeof res->message, "%s", what);
}

#ifdef _WIN32

int ex_platform_save_file(const char *path, const void *data, size_t len, ExSaveResult *res) {
    char *real = ex_platform_realpath(path), *target = real ? real : dup_str(path), *tmp;
    wchar_t *wt, *wp;
    HANDLE h;
    DWORD written = 0;
    int rc = -1;
    if (res) memset(res, 0, sizeof *res);
    if (g_fault_on) { fail(res, g_fault_code, g_fault_message); free(target); return -1; }
    if (!target) { fail(res, 1, "out of memory"); return -1; }
    tmp = malloc(strlen(target) + 40);
    if (!tmp) { fail(res, 1, "out of memory"); free(target); return -1; }
    sprintf(tmp, "%s.exhaust-tmp-%lu", target, (unsigned long)GetCurrentProcessId());
    wt = to_wide(tmp); wp = to_wide(target);
    if (!wt || !wp) { fail(res, 1, "out of memory"); goto done; }
    h = CreateFileW(wt, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { fail(res, (int)GetLastError(), "cannot create the temporary file"); goto done; }
    if (len > 0 && (!WriteFile(h, data, (DWORD)len, &written, NULL) || written != (DWORD)len)) {
        fail(res, (int)GetLastError(), "cannot write (disk full?)");
        CloseHandle(h); DeleteFileW(wt); goto done;
    }
    if (!FlushFileBuffers(h)) { fail(res, (int)GetLastError(), "cannot flush to disk"); CloseHandle(h); DeleteFileW(wt); goto done; }
    CloseHandle(h);
    if (!MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail(res, (int)GetLastError(), "cannot replace the file (is it open in another program?)");
        DeleteFileW(wt);
        goto done;
    }
    if (res) { res->ok = 1; res->error_code = 0; }
    rc = 0;
done:
    free(wt); free(wp); free(tmp); free(target);
    return rc;
}

#else

int ex_platform_save_file(const char *path, const void *data, size_t len, ExSaveResult *res) {
    char *real = ex_platform_realpath(path), *target, *tmp;
    struct stat st;
    int have_st = 0, fd, rc = -1;
    size_t done = 0;
    if (res) memset(res, 0, sizeof *res);
    if (g_fault_on) { fail(res, g_fault_code, g_fault_message); free(real); return -1; }
    target = real ? real : dup_str(path); /* a missing file is created; a symbolic link is resolved so the link survives */
    if (!target) { fail(res, ENOMEM, "out of memory"); return -1; }
    tmp = malloc(strlen(target) + 40);
    if (!tmp) { fail(res, ENOMEM, "out of memory"); free(target); return -1; }
    sprintf(tmp, "%s.exhaust-tmp-%ld", target, (long)getpid());
    have_st = stat(target, &st) == 0;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, have_st ? (st.st_mode & 07777) : 0644);
    if (fd < 0 && errno == EEXIST) { unlink(tmp); fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, have_st ? (st.st_mode & 07777) : 0644); }
    if (fd < 0) { fail(res, errno, strerror(errno)); goto done; }
    while (done < len) {
        ssize_t n = write(fd, (const char *)data + done, len - done);
        if (n < 0) { if (errno == EINTR) continue; fail(res, errno, strerror(errno)); close(fd); unlink(tmp); goto done; }
        done += (size_t)n;
    }
    if (have_st) (void)fchmod(fd, st.st_mode & 07777);          /* the umask must not narrow the original permissions */
    if (fsync(fd) != 0) { fail(res, errno, strerror(errno)); close(fd); unlink(tmp); goto done; }
    if (close(fd) != 0) { fail(res, errno, strerror(errno)); unlink(tmp); goto done; }
    if (rename(tmp, target) != 0) { fail(res, errno, strerror(errno)); unlink(tmp); goto done; }
    {   /* make the rename itself durable */
        char *dir = dup_str(target), *slash = dir ? strrchr(dir, '/') : NULL;
        if (slash) {
            int dfd;
            if (slash == dir) slash[1] = 0; else *slash = 0;
            dfd = open(dir, O_RDONLY);
            if (dfd >= 0) { (void)fsync(dfd); close(dfd); }
        }
        free(dir);
    }
    if (res) { res->ok = 1; res->error_code = 0; }
    rc = 0;
done:
    free(tmp); free(target);
    return rc;
}

#endif

/* ---- append-only files ---- */

struct ExAppendFile {
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    long long size;
};

ExAppendFile *ex_append_open(const char *path) {
    ExAppendFile *f = calloc(1, sizeof *f);
    if (!f) return NULL;
#ifdef _WIN32
    {
        wchar_t *w = to_wide(path);
        LARGE_INTEGER sz;
        f->h = w ? CreateFileW(w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
        free(w);
        if (f->h == INVALID_HANDLE_VALUE) { free(f); return NULL; }
        f->size = GetFileSizeEx(f->h, &sz) ? (long long)sz.QuadPart : 0;
    }
#else
    f->fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (f->fd < 0) { free(f); return NULL; }
    f->size = (long long)lseek(f->fd, 0, SEEK_END);
#endif
    return f;
}

int ex_append_write(ExAppendFile *f, const void *data, size_t len) {
#ifdef _WIN32
    DWORD w = 0;
    if (len && (!WriteFile(f->h, data, (DWORD)len, &w, NULL) || w != (DWORD)len)) return -1;
#else
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(f->fd, (const char *)data + done, len - done);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        done += (size_t)n;
    }
#endif
    f->size += (long long)len;
    return 0;
}

int ex_append_sync(ExAppendFile *f) {
#ifdef _WIN32
    return FlushFileBuffers(f->h) ? 0 : -1;
#else
    return fsync(f->fd) == 0 ? 0 : -1;
#endif
}

long long ex_append_size(const ExAppendFile *f) { return f->size; }

void ex_append_close(ExAppendFile *f) {
    if (!f) return;
#ifdef _WIN32
    CloseHandle(f->h);
#else
    close(f->fd);
#endif
    free(f);
}
