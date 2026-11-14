#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L   /* popen and setenv, for ex_platform_match_cursor_size */
#endif
/* File type and URI scheme registration for the current user. Nothing here runs unless the user
 * asks for it in Settings, and nothing here touches a note. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"

static void fail(char *err, size_t cap, const char *msg) { if (err && cap) snprintf(err, cap, "%s", msg); }

#if defined(_WIN32)

#include <windows.h>

static wchar_t *to_wide_(const char *utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)(n + 1) * sizeof *w) : NULL;
    if (w) { MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n); w[n] = 0; }
    return w;
}

static int set_value(const wchar_t *key, const wchar_t *name, const wchar_t *value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_SZ, value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS ? 0 : -1;
}

static int key_exists(const wchar_t *key) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    RegCloseKey(h);
    return 1;
}

int ex_platform_integration_set(ExIntegration what, int enable, const char *exe_path, char *err, size_t err_cap) {
    if (what == EX_INTEGRATION_FILE_TYPES) {
        if (!enable) {
            RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Exhaust.Markdown");
            RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.md\\OpenWithProgids", L"Exhaust.Markdown");
            return 0;
        }
        {
            wchar_t *exe = exe_path ? to_wide_(exe_path) : NULL;
            wchar_t cmd[2048];
            int rc;
            if (!exe) { fail(err, err_cap, "no program path"); return -1; }
            swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" \"%%1\"", exe);
            rc = set_value(L"Software\\Classes\\Exhaust.Markdown", NULL, L"Markdown note")
              || set_value(L"Software\\Classes\\Exhaust.Markdown\\shell\\open\\command", NULL, cmd)
              || set_value(L"Software\\Classes\\.md\\OpenWithProgids", L"Exhaust.Markdown", L"");
            free(exe);
            if (rc) fail(err, err_cap, "the registry refused the change");
            return rc ? -1 : 0;
        }
    }
    if (!enable) { RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\exhaust"); return 0; }
    {
        wchar_t *exe = exe_path ? to_wide_(exe_path) : NULL;
        wchar_t cmd[2048];
        int rc;
        if (!exe) { fail(err, err_cap, "no program path"); return -1; }
        swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" \"%%1\"", exe);
        rc = set_value(L"Software\\Classes\\exhaust", NULL, L"URL:Exhaust")
          || set_value(L"Software\\Classes\\exhaust", L"URL Protocol", L"")
          || set_value(L"Software\\Classes\\exhaust\\shell\\open\\command", NULL, cmd);
        free(exe);
        if (rc) fail(err, err_cap, "the registry refused the change");
        return rc ? -1 : 0;
    }
}

int ex_platform_integration_registered(ExIntegration what) {
    return key_exists(what == EX_INTEGRATION_FILE_TYPES ? L"Software\\Classes\\Exhaust.Markdown" : L"Software\\Classes\\exhaust");
}

#elif defined(__APPLE__)

int ex_platform_integration_set(ExIntegration what, int enable, const char *exe_path, char *err, size_t err_cap) {
    (void)what; (void)enable; (void)exe_path;
    fail(err, err_cap, "On macOS file types and the exhaust: scheme are declared in the app bundle; there is nothing to register at run time.");
    return -2;
}
int ex_platform_integration_registered(ExIntegration what) { (void)what; return 0; }

#else

static char *applications_dir(void) {
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    char *p;
    if (x && x[0] == '/') { p = malloc(strlen(x) + 16); if (p) sprintf(p, "%s/applications", x); return p; }
    if (!h) return NULL;
    p = malloc(strlen(h) + 32);
    if (p) sprintf(p, "%s/.local/share/applications", h);
    return p;
}

static char *desktop_path(ExIntegration what) {
    char *dir = applications_dir(), *p;
    if (!dir) return NULL;
    p = malloc(strlen(dir) + 40);
    if (p) sprintf(p, "%s/%s", dir, what == EX_INTEGRATION_FILE_TYPES ? "exhaust.desktop" : "exhaust-url-handler.desktop");
    free(dir);
    return p;
}

int ex_platform_integration_set(ExIntegration what, int enable, const char *exe_path, char *err, size_t err_cap) {
    char *path = desktop_path(what), *dir, *text;
    int rc;
    if (!path) { fail(err, err_cap, "no home folder"); return -1; }
    if (!enable) {
        ExPathInfo info;
        rc = ex_platform_path_info(path, &info) == 0 ? ex_platform_remove_tree(path) : 0;
        if (rc) fail(err, err_cap, "the .desktop file could not be removed");
        free(path);
        return rc ? -1 : 0;
    }
    if (!exe_path || !exe_path[0] || strpbrk(exe_path, "\"\n\r`$\\")) { fail(err, err_cap, "the program path cannot be written to a .desktop file"); free(path); return -1; }
    text = malloc(strlen(exe_path) + 400);
    if (!text) { free(path); return -1; }
    if (what == EX_INTEGRATION_FILE_TYPES)
        sprintf(text, "[Desktop Entry]\nType=Application\nName=Exhaust\nComment=Notes and knowledge base\nExec=\"%s\" %%f\nMimeType=text/markdown;text/x-markdown;\nCategories=Office;TextEditor;\nTerminal=false\n", exe_path);
    else
        sprintf(text, "[Desktop Entry]\nType=Application\nName=Exhaust (links)\nExec=\"%s\" %%u\nMimeType=x-scheme-handler/exhaust;\nNoDisplay=true\nTerminal=false\n", exe_path);
    dir = applications_dir();
    if (dir) { ex_platform_make_dirs(dir); free(dir); }
    rc = ex_platform_write_file_atomic(path, text, strlen(text));
    if (rc) fail(err, err_cap, "the .desktop file could not be written");
    free(text);
    free(path);
    return rc ? -1 : 0;
}

int ex_platform_integration_registered(ExIntegration what) {
    char *path = desktop_path(what);
    ExPathInfo info;
    int r = path && ex_platform_path_info(path, &info) == 0;
    free(path);
    return r;
}

#endif

/* ---- the pointer's size (Linux) ---- */

void ex_platform_match_cursor_size(void) {
#if defined(__linux__)
    FILE *p;
    char line[64];
    long n = 0;
    if (getenv("XCURSOR_SIZE") && *getenv("XCURSOR_SIZE")) return;          /* the user's own choice wins */
    p = popen("gsettings get org.gnome.desktop.interface cursor-size 2>/dev/null", "r");
    if (!p) return;
    if (fgets(line, sizeof line, p)) n = strtol(line, NULL, 10);
    pclose(p);
    if (n >= 8 && n <= 256) {
        char v[16];
        snprintf(v, sizeof v, "%ld", n);
        setenv("XCURSOR_SIZE", v, 0);
    }
#endif
}
