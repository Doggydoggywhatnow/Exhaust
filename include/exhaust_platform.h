/* Platform layer: folders, directory walking, clipboard. Everything that
 * differs between Windows, Linux and macOS lives in src/platform/. Strings
 * returned are malloc'ed UTF-8 and owned by the caller. */
#ifndef EXHAUST_PLATFORM_H
#define EXHAUST_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The OS cache folder for Exhaust (created on demand by ex_platform_make_dirs):
 * $XDG_CACHE_HOME/Exhaust or ~/.cache/Exhaust, %LOCALAPPDATA%\Exhaust\Cache,
 * ~/Library/Caches/Exhaust. Caches here may be deleted by the OS or by the
 * user; everything in them can be rebuilt. NULL if no home folder is known. */
char *ex_platform_cache_dir(void);

/* The folders where the OS keeps fonts, existing or not. Returns the count;
 * free with ex_platform_free_list. */
int ex_platform_font_dirs(char ***out);
void ex_platform_free_list(char **list, int count);

/* Callback for ex_platform_walk. is_dir is nonzero for directories. mtime is the
 * modification time in nanoseconds since the Unix epoch. Return 0 to go on and
 * nonzero to stop the walk. Directories are reported before their contents. */
typedef int (*ExWalkFn)(const char *path, int is_dir, int64_t mtime, void *user);

/* Walks a folder recursively (symbolic links to folders are not followed).
 * Returns 0, or -1 if the folder cannot be read. The root itself is reported
 * first. Entries within a folder come in sorted order, so a walk is repeatable. */
int ex_platform_walk(const char *dir, ExWalkFn fn, void *user);

/* Creates a folder and its parents. Returns 0 on success. */
int ex_platform_make_dirs(const char *path);
/* Atomically writes a file: a temporary file in the same folder, then a rename.
 * Returns 0 on success; on failure the old file is left untouched. */
int ex_platform_write_file_atomic(const char *path, const void *data, size_t len);
/* Reads a whole file into a malloc'ed buffer with a trailing NUL. NULL on error. */
char *ex_platform_read_file(const char *path, size_t *len);
int ex_platform_file_exists(const char *path);

/* Clipboard, UTF-8 text. get returns NULL when empty or not text. */
char *ex_clipboard_get_text(void);
int ex_clipboard_set_text(const char *utf8);
int ex_clipboard_has_text(void);

#ifdef __cplusplus
}
#endif
#endif
