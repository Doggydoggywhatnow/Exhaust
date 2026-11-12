#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L /* setenv and realpath under -std=c11 */
#define _XOPEN_SOURCE 700
#endif
/* platform_integration: file type and URI scheme registration for the current user.
 * On Linux it runs for real against a temporary XDG_DATA_HOME; on macOS it checks that the call
 * says "nothing to do"; on Windows the registry is not touched by a test (verified by hand). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void) {
    char err[200] = "";
#if defined(__linux__)
    char *t;
    int rc;
    const char *home = "integration_test_home";
    ex_platform_remove_tree(home);
    ex_platform_make_dirs(home);
    {   /* the folder must be absolute for XDG_DATA_HOME to be honoured */
        char abs[4096];       /* PATH_MAX: _FORTIFY_SOURCE checks realpath buffers */
        if (!realpath(home, abs)) { fprintf(stderr, "no realpath\n"); return 1; }
        setenv("XDG_DATA_HOME", abs, 1);
    }
    CHECK(!ex_platform_integration_registered(EX_INTEGRATION_FILE_TYPES) && !ex_platform_integration_registered(EX_INTEGRATION_URI_SCHEME));
    CHECK(ex_platform_integration_set(EX_INTEGRATION_FILE_TYPES, 0, "/usr/bin/exhaust", err, sizeof err) == 0);      /* removing what is not there succeeds */
    rc = ex_platform_integration_set(EX_INTEGRATION_FILE_TYPES, 1, "/opt/Exhaust App/exhaust", err, sizeof err);
    CHECK(rc == 0);
    CHECK(ex_platform_integration_registered(EX_INTEGRATION_FILE_TYPES) && !ex_platform_integration_registered(EX_INTEGRATION_URI_SCHEME));
    t = ex_platform_read_file("integration_test_home/applications/exhaust.desktop", NULL);
    CHECK(t && strstr(t, "Exec=\"/opt/Exhaust App/exhaust\" %f") && strstr(t, "MimeType=text/markdown;") && strstr(t, "[Desktop Entry]"));
    free(t);
    CHECK(ex_platform_integration_set(EX_INTEGRATION_URI_SCHEME, 1, "/usr/bin/exhaust", err, sizeof err) == 0);
    t = ex_platform_read_file("integration_test_home/applications/exhaust-url-handler.desktop", NULL);
    CHECK(t && strstr(t, "x-scheme-handler/exhaust;") && strstr(t, "%u"));
    free(t);
    /* a path that cannot be put in a .desktop file is refused, and nothing is written */
    CHECK(ex_platform_integration_set(EX_INTEGRATION_FILE_TYPES, 1, "/bad\"path", err, sizeof err) == -1 && err[0]);
    CHECK(ex_platform_integration_set(EX_INTEGRATION_FILE_TYPES, 0, NULL, err, sizeof err) == 0);
    CHECK(!ex_platform_integration_registered(EX_INTEGRATION_FILE_TYPES) && ex_platform_integration_registered(EX_INTEGRATION_URI_SCHEME));
    CHECK(ex_platform_integration_set(EX_INTEGRATION_URI_SCHEME, 0, NULL, err, sizeof err) == 0 && !ex_platform_integration_registered(EX_INTEGRATION_URI_SCHEME));
    ex_platform_remove_tree(home);
#elif defined(__APPLE__)
    CHECK(ex_platform_integration_set(EX_INTEGRATION_FILE_TYPES, 1, "/Applications/Exhaust.app", err, sizeof err) == -2 && err[0]);
    CHECK(!ex_platform_integration_registered(EX_INTEGRATION_URI_SCHEME));
#else
    (void)err;   /* Windows: the registry is not changed by tests */
#endif
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("platform_integration: ok");
    return 0;
}
