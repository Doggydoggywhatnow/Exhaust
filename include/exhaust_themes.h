/* Themes and style snippets (sub-slice 7d): finds the theme files and snippet files, builds the theme to show from the settings, and keeps it
 * up to date while the files change.
 *
 * Override order (see "Loading themes and snippets" in docs/formats.md): the built-in theme of the colour scheme, then the theme file named by
 * `[appearance] theme`, then `[appearance] accent_color`, then the enabled snippets in the order of `snippets_enabled`; each replaces only what it
 * sets. A theme file with a `[theme] base` of its own decides light or dark itself.
 *
 * Threads. A worker thread does the disk work: it lists the theme files and the snippet files (the vault's `.exhaust` folder first, then the
 * global one), reads the files that are used, builds the theme, and looks at their modification times about four times a second; when a file
 * changes it builds again. The interface thread never touches the disk: it hands the worker the inputs (ex_themes_set_inputs) and, once a frame,
 * asks for a newer theme (ex_themes_take). A change of a file reaches the screen within a second. */
#ifndef EXHAUST_THEMES_H
#define EXHAUST_THEMES_H

#include <stddef.h>
#include "exhaust_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExThemes ExThemes;

/* vault_dir: `<vault>/.exhaust` (NULL for none); global_dir: the OS config folder (NULL for none). Starts the worker. */
ExThemes *ex_themes_create(const char *vault_dir, const char *global_dir);
void ex_themes_free(ExThemes *t);                                /* stops the worker and waits for it */

/* What the theme depends on in the settings: scheme is dark, light or system (system_dark says what the system is); theme is the name in
 * `[appearance] theme` ("exhaust" or empty: the built-in one); accent is `accent_color` ("" for none); snippets is `snippets_enabled` (a comma
 * separated list of snippet names). A change makes the worker build again. */
void ex_themes_set_inputs(ExThemes *t, const char *scheme, int system_dark, const char *theme, const char *accent, const char *snippets);

/* High contrast (7f): on builds from the built-in high contrast theme of the scheme and does not apply the theme file or the accent colour (the
 * snippets still apply). Changing it makes the worker build again. */
void ex_themes_set_high_contrast(ExThemes *t, int on);

/* If a theme newer than the one last taken is ready, copies it into out (free it with ex_theme_free) and returns 1; else 0. diag, if given, says
 * what the files held that could not be used. Never blocks for more than a lock. */
int ex_themes_take(ExThemes *t, ExTheme *out, ExThemeDiag *diag);
/* A number that grows every time the worker has built a theme, so a test can wait for one. */
unsigned long ex_themes_generation(const ExThemes *t);
/* For tests: how many times the worker has looked at the files. */
unsigned long ex_themes_polls(const ExThemes *t);

/* The lists the worker found (valid until the next call to one of these; copies of strings). Index 0 of the themes is the built-in `Exhaust`. */
size_t ex_themes_count(ExThemes *t);
char *ex_themes_name(ExThemes *t, size_t i);                       /* malloc'ed; NULL out of range */
size_t ex_themes_snippet_count(ExThemes *t);
char *ex_themes_snippet_name(ExThemes *t, size_t i);

/* Writes one token into the user's theme file (the one named by `theme`; with the built-in one a new file `custom.theme` is made in the vault's
 * themes folder, named Custom, with the base of `scheme`): key is `colors.accent`, `fonts.text`, `sizes.radius`. Everything else in the file stays
 * as it was (comments, unknown keys). The worker sees the file change and builds again. Returns the name of the theme written (malloc'ed; the
 * caller sets `[appearance] theme` to it), or NULL when the token or the value is not valid. This one does write on the calling thread: a
 * file of a few hundred bytes, on the user's own edit. */
char *ex_themes_set_token(ExThemes *t, const char *theme, const char *scheme, int system_dark, const char *key, const char *value);

/* Pairs of colours that must stay readable, and the warnings for the ones that do not (at most max, each a sentence). Returns how many. */
size_t ex_theme_contrast_warnings(const ExTheme *t, char (*out)[160], size_t max);

#ifdef __cplusplus
}
#endif
#endif
