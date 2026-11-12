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

/* The OS local-data folder for Exhaust (journal, session state, recovery): $XDG_DATA_HOME
 * or ~/.local/share, %LOCALAPPDATA%, or ~/Library/Application Support, plus "/Exhaust".
 * Not the cache folder (the OS may clear a cache) and not the roaming folder. */
char *ex_platform_local_data_dir(void);

/* ---- Operating system integration. Only ever done when the user asks for it in Settings. ---- */
typedef enum ExIntegration {
    EX_INTEGRATION_FILE_TYPES,   /* Exhaust is offered for Markdown files */
    EX_INTEGRATION_URI_SCHEME    /* the exhaust:// scheme opens Exhaust (needs the allow_uri_callbacks setting) */
} ExIntegration;
/* Registers (enable != 0) or removes the integration for the current user: a .desktop file under
 * $XDG_DATA_HOME/applications on Linux, keys under HKEY_CURRENT_USER\Software\Classes on Windows. It
 * never changes which application is the default for a file type. exe_path is the running program.
 * Returns 0, -1 on failure (err gets a short reason), or -2 where there is nothing to do at run time
 * (on macOS file types and the scheme are declared in the app bundle). Removing something that is not
 * registered succeeds. */
int ex_platform_integration_set(ExIntegration what, int enable, const char *exe_path, char *err, size_t err_cap);
int ex_platform_integration_registered(ExIntegration what);   /* 1 or 0 */

typedef struct ExSaveResult {
    int ok;
    int error_code;          /* the OS error number, 0 when ok */
    char message[160];       /* short reason, for example "permission denied" */
} ExSaveResult;
/* Replaces a file safely: writes a temporary file next to it, flushes it to disk, keeps the
 * permissions, and renames it over the file. A symbolic link is followed so the link stays.
 * On failure the old file is untouched and the temporary file is removed. */
int ex_platform_save_file(const char *path, const void *data, size_t len, ExSaveResult *res);
/* Test hook: while set, every ex_platform_save_file fails with this message and error code
 * (set to NULL to turn it off). Lets tests simulate a full disk or a locked file on any OS. */
void ex_platform_set_save_fault(const char *message, int error_code);

/* An append-only file (the recovery journal). */
typedef struct ExAppendFile ExAppendFile;
ExAppendFile *ex_append_open(const char *path);
int ex_append_write(ExAppendFile *f, const void *data, size_t len);  /* handed to the OS before it returns */
int ex_append_sync(ExAppendFile *f);                                 /* flushed to the disk */
long long ex_append_size(const ExAppendFile *f);
void ex_append_close(ExAppendFile *f);

/* Monotonic milliseconds, for timers. */
uint64_t ex_platform_now_ms(void);
/* Seconds since the Unix epoch. */
int64_t ex_platform_unix_time(void);

/* Clipboard, UTF-8 text. get returns NULL when empty or not text. */
char *ex_clipboard_get_text(void);
int ex_clipboard_set_text(const char *utf8);
int ex_clipboard_has_text(void);

#ifdef __cplusplus
}
#endif
#endif
