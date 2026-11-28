/* hunt_fs.h: a scratch vault on disk for the tests that need files. Link exhaust_platform.
 * Every test works in a folder of its own, `hunt_work/<test name>` below the working directory (ctest runs each test in the build folder),
 * emptied at the start. Nothing is ever written anywhere else. */
#ifndef HUNT_FS_H
#define HUNT_FS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exhaust_platform.h"
#include "hunt.h"

static char hunt_root_[400];       /* the scratch folder (absolute) */

HUNT_UNUSED_FN const char *hunt_root(void) { return hunt_root_; }

/* Empties and creates the folder `hunt_work/<name>`; returns its absolute path. */
HUNT_UNUSED_FN const char *hunt_fs_init(const char *name) {
    char rel[300], *abs;
    snprintf(rel, sizeof rel, "hunt_work/%s", name);
    ex_platform_remove_tree(rel);
    if (ex_platform_make_dirs(rel) != 0) { fprintf(stderr, "HUNT cannot create %s\n", rel); exit(2); }
    abs = ex_platform_realpath(rel);
    if (!abs) { fprintf(stderr, "HUNT cannot resolve %s\n", rel); exit(2); }
    snprintf(hunt_root_, sizeof hunt_root_, "%s", abs);
    free(abs);
    return hunt_root_;
}

/* Writes root/rel (parents made). Returns 0 on success. */
HUNT_UNUSED_FN int hunt_write_in(const char *root, const char *rel, const void *data, size_t len) {
    char path[1200], dir[1200], *sl;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '/');
    if (sl) { *sl = 0; ex_platform_make_dirs(dir); }
    f = fopen(path, "wb");
    if (!f) return -1;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
HUNT_UNUSED_FN int hunt_write(const char *rel, const void *data, size_t len) { return hunt_write_in(hunt_root_, rel, data, len); }
HUNT_UNUSED_FN int hunt_write_str(const char *rel, const char *s) { return hunt_write_in(hunt_root_, rel, s, strlen(s)); }

/* Reads root/rel; malloc'ed with a trailing NUL, NULL if missing. */
HUNT_UNUSED_FN char *hunt_read_in(const char *root, const char *rel, size_t *len) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    return ex_platform_read_file(path, len);
}
HUNT_UNUSED_FN char *hunt_read(const char *rel, size_t *len) { return hunt_read_in(hunt_root_, rel, len); }
HUNT_UNUSED_FN int hunt_exists(const char *rel) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", hunt_root_, rel);
    return ex_platform_file_exists(path);
}
HUNT_UNUSED_FN void hunt_remove(const char *rel) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", hunt_root_, rel);
    ex_platform_remove_tree(path);
}

#endif
