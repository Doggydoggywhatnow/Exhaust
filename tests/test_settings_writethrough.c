/* settings_writethrough: a process changes settings, is killed (kill -9, TerminateProcess) without any
 * chance to clean up, and the next launch finds every change in settings.ini.
 * Usage: test_settings_writethrough <path-of-settings_child> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_settings.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define BASE "settings_wt_test"

int main(int argc, char **argv) {
    const char *child = argc > 1 ? argv[1] : "./settings_child";
    const char *args[6];
    SDL_Process *p;
    uint64_t start = SDL_GetTicks();
    int marker = 0;
    ExSettings *s;
    ex_platform_remove_tree(BASE);
    ex_platform_make_dirs(BASE "/vault");
    ex_platform_make_dirs(BASE "/global");
    ex_platform_write_file_atomic(BASE "/vault/note.md", "keep me\n", 8);
    args[0] = child; args[1] = BASE "/vault/.exhaust"; args[2] = BASE "/global"; args[3] = BASE "/marker.txt"; args[4] = NULL;
    p = SDL_CreateProcess(args, false);
    if (!p) { fprintf(stderr, "cannot start %s: %s\n", child, SDL_GetError()); return 1; }
    while (SDL_GetTicks() - start < 30000) {
        char *m = ex_platform_read_file(BASE "/marker.txt", NULL);
        if (m) { free(m); marker = 1; break; }
        SDL_Delay(10);
    }
    CHECK(marker);
    CHECK(SDL_KillProcess(p, true));
    SDL_WaitProcess(p, true, NULL);
    SDL_DestroyProcess(p);

    s = ex_settings_open(BASE "/vault/.exhaust", BASE "/global");
    CHECK(s != NULL);
    CHECK(ex_settings_get_bool(s, "editor", "vim_mode") == 1);
    CHECK(ex_settings_get_int(s, "appearance", "font_size") == 24);
    CHECK(strcmp(ex_settings_get(s, "files", "trash_mode"), "vault_trash") == 0);
    ex_settings_close(s);
    {   /* and nothing else in the vault was touched */
        char *n = ex_platform_read_file(BASE "/vault/note.md", NULL);
        CHECK(n && strcmp(n, "keep me\n") == 0);
        free(n);
    }
    ex_platform_remove_tree(BASE);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("settings_writethrough: ok");
    return 0;
}
