/* The settings screen (sub-slice 7a): a search box and the list of pages on the left, the page in cards of rows on the right (a name, a line
 * of explanation and the control). The rows are a table (ex_settings_rows): one for every key of the [general], [appearance], [interface],
 * [editor] and [files] sections of settings.ini, with a name and an explanation written for Exhaust; a control reads and writes its key
 * through ExSettings at once. See "Settings screen" in docs/formats.md.
 *
 * The screen is one widget (a box) that frees what it owns with it. It never writes a file itself: every change goes through
 * ex_settings_set, which writes through. Everything it needs from the application comes through ExSettingsHost. */
#ifndef EXHAUST_SETTINGSVIEW_H
#define EXHAUST_SETTINGSVIEW_H

#include <stddef.h>
#include "exhaust_hotkeys.h"
#include "exhaust_settings.h"
#include "exhaust_themes.h"
#include "exhaust_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExRowKind { EX_ROW_TOGGLE, EX_ROW_SELECT, EX_ROW_NUMBER, EX_ROW_TEXT, EX_ROW_COLOR, EX_ROW_STUB, EX_ROW_INFO, EX_ROW_ACTION } ExRowKind;   /* ACTION: a button; `choices` is "Label|id" and the host's action(id) runs */

typedef struct ExSettingsRow {
    const char *page;            /* "general", "appearance", "interface", "editor", "files", "keychain" */
    const char *group;           /* the heading of the card the row is in */
    const char *section, *key;   /* the setting (NULL for an information row) */
    ExRowKind kind;
    const char *name, *desc;     /* the row's name and its explanation */
    float min, max, step;        /* EX_ROW_NUMBER */
    const char *choices;         /* EX_ROW_SELECT: the labels of the enum's values in order, separated by | */
} ExSettingsRow;

size_t ex_settings_row_count(void);
const ExSettingsRow *ex_settings_row(size_t i);
const ExSettingsRow *ex_settings_row_find(const char *section, const char *key);
/* The pages in the order of the list: id and name; n is the count. */
size_t ex_settings_page_count(void);
const char *ex_settings_page_id(size_t i);
const char *ex_settings_page_name(size_t i);
const char *ex_settings_page_group(size_t i);        /* the heading of the part of the list the page is in: "Options" or "Core plugins" */

typedef struct ExSettingsHost {
    void *user;
    const char *version;         /* the program's version, for the General page (may be NULL) */
    /* A menu of choices at x, y (window pixels): labels[n], the one now chosen first. When the user picks one, call chosen(arg, index). */
    void (*choose)(void *user, const char *const *labels, size_t n, size_t current, float x, float y, void (*chosen)(void *arg, size_t index), void *arg);
    /* A setting was written (section and key): the application applies it if it does not read the key each frame. May be NULL. */
    void (*changed)(void *user, const char *section, const char *key);
    /* A feature switch of the Core plugins page: the application turns the feature on or off (menus, panels and commands follow) and writes the key. May be NULL: the key is written directly. Returns 0, or -1 if refused. */
    int (*set_feature)(void *user, const char *key, int enabled);
    /* The Hotkeys page (7c): the shortcuts, and a way to wait for the next key press the user makes (the "+" button): the application calls
     * done(arg, text) with the shortcut as text, or NULL if the user pressed Escape. Both may be NULL: the page then has no shortcuts to show. */
    ExHotkeys *hotkeys;
    void (*capture)(void *user, void (*done)(void *arg, const char *shortcut), void *arg);
    /* The Theme page (7d): the themes and snippets that were found, and the theme on screen now (for the values and the contrast warnings). */
    ExThemes *themes;
    const ExTheme *(*current_theme)(void *user);
    /* A button row of a page (7e: Clear history): the application does what the id says. May be NULL. */
    void (*action)(void *user, const char *id);
} ExSettingsHost;

typedef struct ExSettingsView ExSettingsView;

/* settings must outlive the widget. */
ExSettingsView *ex_settingsview_create(ExUi *ui, ExSettings *settings, const ExSettingsHost *host);
ExWidget *ex_settingsview_widget(ExSettingsView *sv);
/* Shows a page by id (the search is cleared); returns 0, or -1 for an unknown page. */
int ex_settingsview_show(ExSettingsView *sv, const char *page);
const char *ex_settingsview_page(const ExSettingsView *sv);
/* Sets the text of the search box as if it were typed; "" shows the page again. */
void ex_settingsview_search(ExSettingsView *sv, const char *query);
size_t ex_settingsview_rows_shown(const ExSettingsView *sv);
/* The control of the row for a key in what is shown now (a switch, a button, a slider or a text field), or NULL. For tests and the keyboard. */
ExWidget *ex_settingsview_control(const ExSettingsView *sv, const char *section, const char *key);
/* Called once a frame by the widget itself; exposed so a test can run it without drawing. */
void ex_settingsview_tick(ExSettingsView *sv);

#ifdef __cplusplus
}
#endif
#endif
