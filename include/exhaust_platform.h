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

/* ---- more folders and file operations (src/platform/files.c) ---- */

/* The OS config folder for Exhaust: $XDG_CONFIG_HOME or ~/.config, %APPDATA%, or
 * ~/Library/Application Support, plus "/Exhaust". */
char *ex_platform_config_dir(void);
/* Absolute path with symbolic links resolved, "." and ".." removed. NULL if the
 * path does not exist. */
char *ex_platform_realpath(const char *path);

typedef struct ExPathInfo {
    int exists, is_dir, is_file;
    int64_t size, mtime, ctime; /* bytes; nanoseconds since the Unix epoch (ctime is the creation time where the OS keeps one) */
} ExPathInfo;
/* Follows symbolic links. Returns 0 if the path exists. */
int ex_platform_path_info(const char *path, ExPathInfo *out);

/* Renames a file or folder but never replaces an existing target: returns -1 and
 * leaves both untouched if `to` exists. (Moving across drives is not supported.) */
int ex_platform_rename_noreplace(const char *from, const char *to);
/* Deletes a file, or a folder with everything in it. Symbolic links are removed,
 * never followed. */
int ex_platform_remove_tree(const char *path);
/* Moves to the operating system's trash (freedesktop spec on Linux, the Recycle
 * Bin on Windows, NSFileManager on macOS). Returns 0, or -1 and leaves the item
 * where it was. On Linux the trash is under $XDG_DATA_HOME. */
int ex_platform_trash_system(const char *path);
/* Opens a file with its default application, or shows its folder in the file manager. */
int ex_platform_open_default(const char *path);
int ex_platform_show_in_file_manager(const char *path);

/* Clipboard, UTF-8 text. get returns NULL when empty or not text. */
char *ex_clipboard_get_text(void);
int ex_clipboard_set_text(const char *utf8);
int ex_clipboard_has_text(void);

#ifdef __cplusplus
}
#endif
#endif
