/* Child of settings_writethrough: changes settings, waits until they are on disk (and only then
 * writes the marker), and then waits to be killed. Usage: settings_child VAULT_CONFIG GLOBAL MARKER */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "exhaust_platform.h"
#include "exhaust_settings.h"

int main(int argc, char **argv) {
    ExSettings *s;
    if (argc < 4) return 2;
    s = ex_settings_open(argv[1], argv[2]);
    if (!s) return 3;
    ex_settings_set_bool(s, "editor", "vim_mode", 1);
    ex_settings_set_int(s, "appearance", "font_size", 24);
    ex_settings_set(s, "files", "trash_mode", "vault_trash");
    ex_settings_flush(s);                                    /* write-through: they are on disk now, nothing waits for an exit */
    ex_platform_write_file_atomic(argv[3], "done", 4);
    SDL_Delay(60000);                                        /* never closes the settings: the parent kills the process */
    return 0;
}
