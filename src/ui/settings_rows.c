/* The rows of the settings screen (sub-slice 7a): one for every key of the sections the Options pages show. The names and the explanations
 * are Exhaust's own words. The layout of the pages follows screenshots/settings/ (general, appearance, interface, editor, files and links). */
#include <string.h>

#include "exhaust_settingsview.h"

#define T(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_TOGGLE, name, desc, 0, 0, 0, NULL }
#define S(page, group, sec, key, name, desc, choices)         { page, group, sec, key, EX_ROW_SELECT, name, desc, 0, 0, 0, choices }
#define N(page, group, sec, key, name, desc, lo, hi, st)      { page, group, sec, key, EX_ROW_NUMBER, name, desc, lo, hi, st, NULL }
#define X(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_TEXT, name, desc, 0, 0, 0, NULL }
#define C(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_COLOR, name, desc, 0, 0, 0, NULL }
#define U(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_STUB, name, desc, 0, 0, 0, NULL }

static const ExSettingsRow rows[] = {
    /* ---- General ---- */
    { "general", "Exhaust", NULL, NULL, EX_ROW_INFO, "Version", "Exhaust never checks for updates on its own. A button to check by hand is planned.", 0, 0, 0, "Check for updates" },
    X("general", "Exhaust", "general", "language", "Language", "The language of the menus and messages, as a language code. Only English (en) comes with this version."),
    T("general", "Advanced", "general", "startup_notice", "Tell me if starting is slow", "Show a notice when opening a vault takes longer than expected, with what took the time."),
    U("general", "Advanced", "general", "command_line", "Command line", "Control Exhaust from a terminal. Not in this version."),
    T("general", "Advanced", "general", "hardware_acceleration", "Hardware acceleration", "Draw with the graphics card. Turning it off makes drawing slower."),

    /* ---- Appearance ---- */
    S("appearance", "Colors", "appearance", "color_scheme", "Base color scheme", "Dark, light, or follow the setting of the operating system.", "Dark|Light|Follow the system"),
    C("appearance", "Colors", "appearance", "accent_color", "Accent color", "A color written as # and six hex digits, for buttons, links and the focus ring. Empty uses the theme's own."),
    X("appearance", "Colors", "appearance", "theme", "Theme", "The name of the theme in use (a file in the themes folder of the vault, or the built-in one)."),
    X("appearance", "Colors", "appearance", "snippets_enabled", "Style snippets", "The style snippets that are switched on, separated by commas."),
    X("appearance", "Fonts", "appearance", "font_interface", "Interface font", "The font of menus, buttons and lists. A name of an installed font."),
    X("appearance", "Fonts", "appearance", "font_text", "Text font", "The font of notes."),
    X("appearance", "Fonts", "appearance", "font_mono", "Monospace font", "The font of code, in notes and elsewhere."),
    N("appearance", "Fonts", "appearance", "font_size", "Font size", "The size of text in points, for the whole program. Applies when Exhaust starts again.", 8, 48, 1),
    T("appearance", "Fonts", "appearance", "quick_font_size", "Change the font size with Ctrl and the mouse wheel", "Hold Ctrl and turn the wheel over a note to make its text larger or smaller."),
    U("appearance", "Window", "appearance", "app_icon", "Program icon", "Use another icon for the program. Not in this version."),
    T("appearance", "Window", "appearance", "translucent_window", "Translucent window", "Let the desktop show through the window. macOS only."),
    N("appearance", "Window", "appearance", "window_width", "Width of a new window", "The width, in pixels, of the window when no earlier size is remembered.", 600, 4000, 50),
    N("appearance", "Window", "appearance", "window_height", "Height of a new window", "The height, in pixels, of the window when no earlier size is remembered.", 400, 3000, 50),

    /* ---- Interface ---- */
    T("interface", "Window", "interface", "show_tab_title_bar", "Show the title bar of tabs", "Under the tabs of each pane: back and forward, the note's name, the reading switch and a menu."),
    T("interface", "Window", "interface", "show_ribbon", "Show the ribbon", "The strip of buttons on the far left."),
    X("interface", "Window", "interface", "ribbon_items", "Ribbon buttons", "The buttons the ribbon shows, separated by commas. Empty shows all of them."),
    N("interface", "Window", "interface", "zoom_percent", "Zoom", "How large the whole interface is, in percent of its normal size.", 50, 400, 5),
    T("interface", "Window", "interface", "native_menus", "Native menus", "Use the operating system's own menus where there are any."),
    S("interface", "Window", "interface", "window_frame", "Window frame", "Draw the title bar yourself, use the system's, or hide it.", "Custom|Native|Hidden"),
    T("interface", "Window", "interface", "settings_in_new_window", "Open settings in a new window", "Show the settings in a window of their own and not in a tab."),

    /* ---- Editor ---- */
    S("editor", "Opening", "editor", "default_view", "Default view for new tabs", "The view a note opens in.", "Editing|Reading"),
    S("editor", "Opening", "editor", "default_mode", "Default editing mode", "The mode a note starts in when it is edited.", "Live preview|Source"),
    T("editor", "Opening", "editor", "show_mode_in_status_bar", "Show the editing mode in the status bar", "A switch at the bottom of the window for live preview and source mode."),
    T("editor", "Display", "editor", "inline_title", "Inline title", "Show the note's name as a title you can edit above its text."),
    T("editor", "Display", "editor", "readable_line_length", "Readable line length", "Keep lines of text to a width that is easy to read, even in a wide pane."),
    T("editor", "Display", "editor", "strict_line_breaks", "Strict line breaks", "Join lines that follow each other in reading view, as Markdown does. Off shows every line break."),
    S("editor", "Display", "editor", "properties_in_document", "Properties in a note", "How the properties at the top of a note are shown. Source shows the YAML text.", "Visible|Hidden|Source"),
    T("editor", "Display", "editor", "fold_heading", "Fold headings", "Let a heading hide the lines under it."),
    T("editor", "Display", "editor", "fold_indent", "Fold lists", "Let a list item hide the items inside it."),
    T("editor", "Display", "editor", "line_numbers", "Line numbers", "Show the number of each line in the margin."),
    T("editor", "Display", "editor", "indent_guides", "Indent guides", "Draw a faint vertical line for each level of a nested list."),
    T("editor", "Display", "editor", "rtl", "Right-to-left text", "Write from right to left, for Arabic and Hebrew."),
    T("editor", "Spelling", "editor", "spellcheck", "Check spelling", "Underline words the dictionary does not know."),
    X("editor", "Spelling", "editor", "spellcheck_languages", "Spelling languages", "The dictionaries to use, separated by commas. This version has en_US."),
    T("editor", "Typing", "editor", "auto_pair_brackets", "Pair brackets and quotes", "Type the closing bracket or quote with the opening one."),
    T("editor", "Typing", "editor", "auto_pair_markdown", "Pair Markdown marks", "Type the closing mark of bold, italic and code with the opening one."),
    T("editor", "Typing", "editor", "smart_lists", "Smart lists", "Enter continues a list, and Tab and Shift+Tab change the level of an item."),
    T("editor", "Typing", "editor", "indent_with_tabs", "Indent with tabs", "Use a tab character to indent. Off uses spaces."),
    N("editor", "Typing", "editor", "indent_width", "Width of an indent", "How many characters wide one level of indent is.", 1, 8, 1),
    T("editor", "Typing", "editor", "paste_html_as_markdown", "Paste web text as Markdown", "Turn formatted text you paste from a web page into Markdown."),
    U("editor", "Typing", "editor", "vim_mode", "Vim key bindings", "Edit with Vim's modes and keys. Not in this version."),

    /* ---- Files and links ---- */
    S("files", "Opening", "files", "default_file", "Default note to open", "What is open when you open a vault.", "Reopen the last session|An empty tab|A chosen note|Today's daily note"),
    X("files", "Opening", "files", "default_file_note", "The chosen note", "The note to open when the choice above is a chosen note, as a path in the vault."),
    T("files", "Opening", "files", "always_focus_new_tabs", "Focus new tabs", "Switch to a tab as soon as it is opened."),
    S("files", "Explorer", "files", "sort_by", "Sort files by", "The order of the files in the explorer. Sorting never moves a file.", "Name|Date modified|Date created"),
    T("files", "Explorer", "files", "sort_descending", "Sort in reverse", "Put the last first."),
    T("files", "Explorer", "files", "show_all_file_types", "Show all file types", "List files Exhaust cannot open too."),
    T("files", "Explorer", "files", "rename_folder_on_double_click", "Rename a folder by double-clicking it", "Double-click the name of a folder to rename it where it is. A note is never renamed this way."),
    X("files", "Explorer", "files", "excluded", "Excluded files", "Folders and files to leave out of the explorer, search and graph, separated by commas."),
    S("files", "New files", "files", "new_note_location", "Where new notes go", "The folder a new note is created in.", "Vault folder|The folder below|Same folder as the open note|A subfolder of the open note's folder"),
    X("files", "New files", "files", "new_note_folder", "Folder for new notes", "Used when new notes go to the folder below."),
    S("files", "New files", "files", "attachment_location", "Where attachments go", "The folder a pasted or dropped file is saved in.", "Vault folder|The folder below|Same folder as the open note|A subfolder of the open note's folder"),
    X("files", "New files", "files", "attachment_folder", "Folder for attachments", "Used when attachments go to the folder below."),
    X("files", "New files", "files", "attachment_subfolder", "Name of the attachment subfolder", "Used when attachments go to a subfolder of the open note's folder."),
    S("files", "Links", "files", "link_format", "New link format", "How a link to a note names it.", "Shortest path when possible|Relative path from the note|Absolute path in the vault"),
    T("files", "Links", "files", "auto_update_links", "Keep links working when a note is renamed", "Rename the links that point at a note when the note is renamed or moved."),
    S("files", "Links", "files", "link_update", "Updating links", "Ask first, update the links without asking, or leave them.", "Ask|Update automatically|Do not update"),
    T("files", "Links", "files", "use_wikilinks", "Use [[wikilinks]]", "Write new links as [[Note]]. Off writes [Note](Note.md)."),
    T("files", "Deleting", "files", "confirm_delete", "Ask before deleting", "Show a question before a file is deleted."),
    S("files", "Deleting", "files", "delete_attachments", "Attachments of a deleted note", "What to do with the images and files only that note used.", "Ask|Always delete them|Never delete them"),
    S("files", "Deleting", "files", "trash_mode", "Deleted files go to", "Where a deleted file goes.", "The system trash|The .trash folder of the vault|Nowhere: delete for good"),
    N("files", "Saving", "files", "autosave_idle_ms", "Save after a pause", "Save a note when you have stopped typing for this long, in milliseconds.", 200, 10000, 100),
    N("files", "Saving", "files", "autosave_max_ms", "Longest time between saves", "While you keep typing, save at least this often, in milliseconds.", 1000, 60000, 500),
    N("files", "Saving", "files", "journal_flush_ms", "Recovery journal", "How soon each edit is written to the recovery journal, in milliseconds.", 50, 1000, 50),
    X("files", "Advanced", "files", "config_folder", "Settings folder", "The name of the folder inside a vault that holds its settings. Takes effect when the vault is opened again."),
    T("files", "Advanced", "files", "allow_uri_callbacks", "Allow links from other programs", "Let exhaust:// links open notes and run actions. Takes effect when the vault is opened again."),

    /* ---- Keychain ---- */
    { "keychain", "Keychain", NULL, NULL, EX_ROW_INFO, "Saved secrets", "Exhaust keeps no passwords or tokens. Plugins that need one will ask you to store it here. Not in this version.", 0, 0, 0, "Add a secret" },
};

static const struct { const char *id, *name; } pages[] = {
    { "general", "General" }, { "appearance", "Appearance" }, { "interface", "Interface" }, { "editor", "Editor" },
    { "files", "Files and links" }, { "keychain", "Keychain" },
};

size_t ex_settings_row_count(void) { return sizeof rows / sizeof *rows; }
const ExSettingsRow *ex_settings_row(size_t i) { return i < sizeof rows / sizeof *rows ? &rows[i] : NULL; }
const ExSettingsRow *ex_settings_row_find(const char *section, const char *key) {
    size_t i;
    for (i = 0; i < sizeof rows / sizeof *rows; i++)
        if (rows[i].key && !strcmp(rows[i].section, section) && !strcmp(rows[i].key, key)) return &rows[i];
    return NULL;
}
size_t ex_settings_page_count(void) { return sizeof pages / sizeof *pages; }
const char *ex_settings_page_id(size_t i) { return i < sizeof pages / sizeof *pages ? pages[i].id : NULL; }
const char *ex_settings_page_name(size_t i) { return i < sizeof pages / sizeof *pages ? pages[i].name : NULL; }
