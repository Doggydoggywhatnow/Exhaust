/* The rows of the settings screen (sub-slice 7a): one for every key of the sections the Options pages show. The names and the explanations
 * are Exhaust's own words. The layout of the pages follows screenshots/settings/ (general, appearance, interface, editor, files and links). */
#include <string.h>

#include "exhaust_settingsview.h"

#define T(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_TOGGLE, name, desc, 0, 0, 0, NULL }
#define S(page, group, sec, key, name, desc, choices)         { page, group, sec, key, EX_ROW_SELECT, name, desc, 0, 0, 0, choices }
#define A(page, group, name, desc, label_and_id)              { page, group, NULL, NULL, EX_ROW_ACTION, name, desc, 0, 0, 0, label_and_id }
#define N(page, group, sec, key, name, desc, lo, hi, st)      { page, group, sec, key, EX_ROW_NUMBER, name, desc, lo, hi, st, NULL }
#define X(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_TEXT, name, desc, 0, 0, 0, NULL }
#define C(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_COLOR, name, desc, 0, 0, 0, NULL }
#define U(page, group, sec, key, name, desc)                  { page, group, sec, key, EX_ROW_STUB, name, desc, 0, 0, 0, NULL }

static const ExSettingsRow rows[] = {
    /* ---- General ---- */
    { "general", "Exhaust", NULL, NULL, EX_ROW_INFO, "Version", "Exhaust never checks for updates on its own. A button to check by hand is planned.", 0, 0, 0, "Check for updates" },
    U("general", "Exhaust", "general", "language", "Language", "The language of the menus and messages, as a language code. Only English (en) comes with this version. Not in this version."),
    U("general", "Advanced", "general", "startup_notice", "Tell me if starting is slow", "Show a notice when opening a vault takes longer than expected, with what took the time. Not in this version."),
    U("general", "Advanced", "general", "command_line", "Command line", "Control Exhaust from a terminal. Not in this version."),
    U("general", "Advanced", "general", "hardware_acceleration", "Hardware acceleration", "Draw with the graphics card. Turning it off makes drawing slower. Not in this version."),

    /* ---- Export to PDF (6e-iv) ---- */
    S("export_pdf", "Export to PDF", "export_pdf", "page_size", "Page size", "The size of the pages of an exported PDF.", "A3|A4|A5|Legal|Letter|Tabloid"),
    T("export_pdf", "Export to PDF", "export_pdf", "landscape", "Landscape", "Turn the pages on their side."),
    S("export_pdf", "Export to PDF", "export_pdf", "margin", "Margins", "How much white space is left around the text.", "Default|Minimal|None"),
    N("export_pdf", "Export to PDF", "export_pdf", "downscale_percent", "Downscale", "Shrinks the text and pictures, in percent, so more fits on a page. 100 is the normal size.", 10, 200, 5),
    T("export_pdf", "Export to PDF", "export_pdf", "include_title", "File name as title", "Put the name of the note above its text."),

    /* ---- Appearance ---- */
    S("appearance", "Colors", "appearance", "color_scheme", "Base color scheme", "Dark, light, or follow the setting of the operating system.", "Dark|Light|Follow the system"),
    S("appearance", "Colors", "appearance", "high_contrast", "High contrast", "Stronger colors that are easy to tell apart. When it is on, the theme and the accent color are not used. The system choice follows the operating system where it can be asked.", "Off|On|Follow the system"),
    C("appearance", "Colors", "appearance", "accent_color", "Accent color", "A color written as # and six hex digits, for buttons, links and the focus ring. Empty uses the theme's own."),
    X("appearance", "Colors", "appearance", "theme", "Theme", "The name of the theme in use (a file in the themes folder of the vault, or the built-in one)."),
    X("appearance", "Colors", "appearance", "snippets_enabled", "Style snippets", "The style snippets that are switched on, separated by commas."),
    U("appearance", "Fonts", "appearance", "font_interface", "Interface font", "The font of menus, buttons and lists. A name of an installed font. Not in this version."),
    U("appearance", "Fonts", "appearance", "font_text", "Text font", "The font of notes. Not in this version."),
    U("appearance", "Fonts", "appearance", "font_mono", "Monospace font", "The font of code, in notes and elsewhere. Not in this version."),
    N("appearance", "Fonts", "appearance", "font_size", "Font size", "The size of text in points, for the whole program. Applies when Exhaust starts again.", 8, 48, 1),
    U("appearance", "Fonts", "appearance", "quick_font_size", "Change the font size with Ctrl and the mouse wheel", "Hold Ctrl and turn the wheel over a note to make its text larger or smaller. Not in this version."),
    { "appearance", "Language", NULL, NULL, EX_ROW_INFO, "Language", "Interface translations (later). Not in this version: Exhaust is in English (US).", 0, 0, 0, "Choose language" },
    { "appearance", "Community directory", NULL, NULL, EX_ROW_INFO, "Community themes", "Browsing themes from a public directory (optional, later) needs the network and is off. Not in this version: nothing is downloaded.", 0, 0, 0, "Browse themes" },
    U("appearance", "Window", "appearance", "app_icon", "Program icon", "Use another icon for the program. Not in this version."),
    U("appearance", "Window", "appearance", "translucent_window", "Translucent window", "Let the desktop show through the window. macOS only. Not in this version."),
    N("appearance", "Window", "appearance", "window_width", "Width of a new window", "The width, in pixels, of the window when no earlier size is remembered.", 600, 4000, 50),
    N("appearance", "Window", "appearance", "window_height", "Height of a new window", "The height, in pixels, of the window when no earlier size is remembered.", 400, 3000, 50),
    A("appearance", "Window", "Use the size of this window", "Make the size of the window you are looking at the size of a new window.", "Use this size|window_size"),

    /* ---- Interface ---- */
    T("interface", "Window", "interface", "show_tab_title_bar", "Show the title bar of tabs", "Under the tabs of each pane: back and forward, the note's name, the reading switch and a menu."),
    T("interface", "Window", "interface", "show_ribbon", "Show the ribbon", "The strip of buttons on the far left."),
    X("interface", "Window", "interface", "ribbon_items", "Ribbon buttons", "The commands whose buttons the ribbon shows, separated by commas. Empty shows all of them and none shows none. Use the button below to choose from a list."),
    A("interface", "Window", "Choose ribbon buttons", "A list of the ribbon buttons: choose one to show or hide it.", "Choose...|ribbon_items"),
    N("interface", "Window", "interface", "zoom_percent", "Zoom", "How large the whole interface is, in percent of its normal size.", 50, 400, 5),
    U("interface", "Window", "interface", "native_menus", "Native menus", "Use the operating system's own menus where there are any. Not in this version."),
    U("interface", "Window", "interface", "window_frame", "Window frame", "Draw the title bar yourself, use the system's, or hide it. Not in this version."),
    U("interface", "Window", "interface", "settings_in_new_window", "Open settings in a new window", "Show the settings in a window of their own and not in a tab. Not in this version."),

    /* ---- Editor ---- */
    S("editor", "Opening", "editor", "default_view", "Default view for new tabs", "The view a note opens in.", "Editing|Reading"),
    S("editor", "Opening", "editor", "default_mode", "Default editing mode", "The mode a note starts in when it is edited.", "Live preview|Source"),
    T("editor", "Opening", "editor", "show_mode_in_status_bar", "Show the editing mode in the status bar", "A switch at the bottom of the window for live preview and source mode."),
    T("editor", "Display", "editor", "inline_title", "Inline title", "Show the note's name as a title you can edit above its text."),
    T("editor", "Display", "editor", "readable_line_length", "Readable line length", "Keep lines of text to a width that is easy to read, even in a wide pane."),
    T("editor", "Display", "editor", "strict_line_breaks", "Strict line breaks", "Join lines that follow each other in reading view, as Markdown does. Off shows every line break."),
    T("editor", "Display", "editor", "live_preview_blocks", "Draw blocks in live preview", "Show tables, equations, diagrams, pictures and the properties as they look in reading mode while you are not in them. Off shows their text."),
    S("editor", "Display", "editor", "properties_in_document", "Properties in a note", "How the properties at the top of a note are shown. Source shows the YAML text.", "Visible|Hidden|Source"),
    T("editor", "Display", "editor", "fold_heading", "Fold headings", "Let a heading hide the lines under it."),
    T("editor", "Display", "editor", "fold_indent", "Fold lists", "Let a list item hide the items inside it."),
    T("editor", "Display", "editor", "line_numbers", "Line numbers", "Show the number of each line in the margin."),
    T("editor", "Display", "editor", "indent_guides", "Indent guides", "Draw a faint vertical line for each level of a nested list."),
    U("editor", "Display", "editor", "rtl", "Right-to-left text", "Write from right to left, for Arabic and Hebrew. Not in this version."),
    T("editor", "Spelling", "editor", "spellcheck", "Check spelling", "Underline words the dictionary does not know."),
    U("editor", "Spelling", "editor", "spellcheck_languages", "Spelling languages", "The dictionaries to use, separated by commas. This version has en_US. Not in this version."),
    T("editor", "Typing", "editor", "auto_pair_brackets", "Pair brackets and quotes", "Type the closing bracket or quote with the opening one."),
    T("editor", "Typing", "editor", "auto_pair_markdown", "Pair Markdown marks", "Type the closing mark of bold, italic and code with the opening one."),
    T("editor", "Typing", "editor", "smart_lists", "Smart lists", "Enter continues a list, and Tab and Shift+Tab change the level of an item."),
    T("editor", "Typing", "editor", "indent_with_tabs", "Indent with tabs", "Use a tab character to indent. Off uses spaces."),
    N("editor", "Typing", "editor", "indent_width", "Width of an indent", "How many characters wide one level of indent is.", 1, 8, 1),
    T("editor", "Typing", "editor", "paste_html_as_markdown", "Paste web text as Markdown", "Turn formatted text you paste from a web page into Markdown."),
    T("editor", "Advanced", "editor", "paste_url_on_selection", "Paste a link over selected words", "Pasting a web address while words are selected turns them into a link to it."),
    U("editor", "Typing", "editor", "vim_mode", "Vim key bindings", "Edit with Vim's modes and keys. Not in this version."),

    /* ---- Files and links ---- */
    S("files", "Opening", "files", "default_file", "Default note to open", "What is open when you open a vault.", "Reopen the last session|An empty tab|A chosen note|Today's daily note"),
    X("files", "Opening", "files", "default_file_note", "The chosen note", "The note to open when the choice above is a chosen note, as a path in the vault."),
    U("files", "Opening", "files", "always_focus_new_tabs", "Focus new tabs", "Switch to a tab as soon as it is opened. Not in this version."),
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
    U("files", "Links", "files", "link_format", "New link format", "How a link to a note names it. Not in this version."),
    T("files", "Links", "files", "auto_update_links", "Keep links working when a note is renamed", "Rename the links that point at a note when the note is renamed or moved."),
    S("files", "Links", "files", "link_update", "Updating links", "Ask first, update the links without asking, or leave them.", "Ask|Update automatically|Do not update"),
    U("files", "Links", "files", "use_wikilinks", "Use [[wikilinks]]", "Write new links as [[Note]]. Off writes [Note](Note.md). Not in this version."),
    T("files", "Deleting", "files", "confirm_delete", "Ask before deleting", "Show a question before a file is deleted."),
    U("files", "Deleting", "files", "delete_attachments", "Attachments of a deleted note", "What to do with the images and files only that note used. Not in this version."),
    S("files", "Deleting", "files", "trash_mode", "Deleted files go to", "Where a deleted file goes.", "The system trash|The .trash folder of the vault|Nowhere: delete for good"),
    N("files", "Saving", "files", "autosave_idle_ms", "Save after a pause", "Save a note when you have stopped typing for this long, in milliseconds.", 200, 10000, 100),
    N("files", "Saving", "files", "autosave_max_ms", "Longest time between saves", "While you keep typing, save at least this often, in milliseconds.", 1000, 60000, 500),
    N("files", "Saving", "files", "journal_flush_ms", "Recovery journal", "How soon each edit is written to the recovery journal, in milliseconds.", 50, 1000, 50),
    T("files", "Advanced", "files", "allow_uri_callbacks", "Allow links from other programs", "Let exhaust:// links open notes and run actions. Not in this version."),
    U("files", "Advanced", "files", "config_folder", "Settings folder", "The name of the folder inside a vault that holds its settings. Takes effect when the vault is opened again. Not in this version."),

    /* ---- Plugins (8b): the master switch; the plugins found are cards the page builds itself ---- */
    T("plugins", "Plugins", "plugins", "allow", "Allow plugins", "Plugins are Lua programs in the .exhaust/plugins folder of the vault. Nothing runs while this is off. A plugin runs only after you turn it on and approve what it may do. Exhaust's plugins never use the network in this version."),

    { "plugins", "Community directory", NULL, NULL, EX_ROW_INFO, "Community plugins", "Browsing and installing plugins from a public directory (optional, later) needs the network and is off. Not in this version: nothing is downloaded.", 0, 0, 0, "Browse plugins" },
    { "plugins", "Web clipper", NULL, NULL, EX_ROW_INFO, "Web clipper", "A small extension for your own browser that saves pages into the vault (later). Not in this version: see extras/clipper in the source.", 0, 0, 0, "Get the extension" },

    /* ---- Keychain ---- */
    { "keychain", "Keychain", NULL, NULL, EX_ROW_INFO, "Saved secrets", "Exhaust keeps no passwords or tokens. Plugins that need one will ask you to store it here. Not in this version.", 0, 0, 0, "Add a secret" },

    /* ---- Core plugins: the list of switches (one for every key of [features]) ---- */
    T("features", "Panels", "features", "files", "File explorer", "The list of the vault's folders and notes."),
    T("features", "Panels", "features", "search", "Search", "Find text in every note."),
    T("features", "Panels", "features", "bookmarks", "Bookmarks", "Keep notes, searches and headings you come back to."),
    T("features", "Panels", "features", "backlinks", "Backlinks", "Show the notes that link to the open note."),
    T("features", "Panels", "features", "outgoing_links", "Outgoing links", "Show the links in the open note."),
    T("features", "Panels", "features", "tags_view", "Tags view", "List every tag with how often it is used."),
    T("features", "Panels", "features", "properties_view", "Properties view", "List every property name in the vault."),
    T("features", "Panels", "features", "outline", "Outline", "The headings of the open note, to jump between."),
    T("features", "Panels", "features", "footnotes", "Footnotes view", "List the footnotes of the open note."),
    T("features", "Views", "features", "graph", "Graph view", "See the notes and the links between them as a graph."),
    T("features", "Views", "features", "canvas", "Canvas", "An endless board of cards, notes and arrows."),
    T("features", "Views", "features", "bases", "Bases", "Tables, lists and cards built from the properties of your notes."),
    T("features", "Views", "features", "slides", "Slides", "Present a note as slides."),
    T("features", "Writing", "features", "slash_commands", "Slash commands", "Type / to pick an editing command from a menu."),
    T("features", "Writing", "features", "templates", "Templates", "Insert the text of a template note."),
    T("features", "Writing", "features", "daily_notes", "Daily notes", "Open today's note, made from a template."),
    T("features", "Vaults and files", "features", "importer", "Importer", "Bring notes in from Markdown, text, HTML, CSV and Textbundle sources."),
    T("features", "Writing", "features", "unique_note", "Unique note creator", "Make a note whose name is the time it was made."),
    T("features", "Writing", "features", "note_composer", "Note composer", "Merge notes and move text into a new note."),
    T("features", "Writing", "features", "random_note", "Random note", "Open a note at random."),
    T("features", "Writing", "features", "word_count", "Word count", "Count the words and characters of the open note."),
    T("features", "Moving around", "features", "quick_switcher", "Quick switcher", "Jump to a note by typing part of its name."),
    T("features", "Moving around", "features", "command_palette", "Command palette", "Run any command by typing its name."),
    T("features", "Moving around", "features", "page_preview", "Page preview", "Show a note in a small window when you point at a link to it."),
    T("features", "Moving around", "features", "workspaces", "Workspaces", "Save and restore whole layouts of panes."),
    T("features", "Moving around", "features", "file_recovery", "File recovery", "Keep earlier versions of your notes so you can go back."),
    T("features", "Not in this version", "features", "audio_recorder", "Audio recorder", "Record sound straight into a note. Not in this version."),
    T("features", "Not in this version", "features", "sync", "Sync", "Keep a vault the same on several devices. Not in this version."),
    T("features", "Not in this version", "features", "publish", "Publish", "Put notes on the web. Not in this version."),

    /* ---- the pages of the features that have options ---- */
    T("backlinks", "Backlinks", "backlinks", "show_at_bottom", "Backlinks at the bottom of a note", "Show the backlinks under the text of a note, as well as in the panel."),

    S("canvas", "New canvases", "canvas", "new_file_location", "Where new canvases go", "The folder a new canvas is created in.", "Vault folder|The folder below|Same folder as the open note|A subfolder of the open note's folder"),
    S("canvas", "Moving around", "canvas", "wheel", "Mouse wheel", "What the wheel does over a canvas.", "Pan|Zoom"),
    S("canvas", "Moving around", "canvas", "ctrl_drag", "Dragging with Ctrl", "What dragging with Ctrl held does.", "Show a menu|Select an area"),
    S("canvas", "Cards", "canvas", "show_card_names", "Names of cards", "When the name bar above a note or file card shows.", "Always|Only on the selected card|Never"),
    T("canvas", "Cards", "canvas", "snap_to_grid", "Snap to the grid", "Move and resize cards in steps of a grid."),
    T("canvas", "Cards", "canvas", "snap_to_objects", "Snap to other cards", "Line cards up with the edges of the cards near them."),
    N("canvas", "Cards", "canvas", "zoom_hide_threshold", "Hide the text of cards below this zoom", "Below this zoom, in percent, cards show only their outline, which keeps a large canvas fast.", 10, 400, 10),

    X("command_palette", "Command palette", "command_palette", "pinned", "Pinned commands", "The ids of the commands that are always listed first, separated by commas."),

    X("daily_notes", "Daily notes", "daily_notes", "date_format", "Date format", "How the date in a daily note's name is written, for example YYYY-MM-DD."),
    X("daily_notes", "Daily notes", "daily_notes", "folder", "New daily notes go in", "A folder in the vault. Empty uses the top folder."),
    X("daily_notes", "Daily notes", "daily_notes", "template", "Template", "The note whose text starts every daily note, as a path in the vault."),

    N("file_recovery", "File recovery", "file_recovery", "interval_minutes", "Take a snapshot every", "How often to keep a copy of a note you are changing, in minutes.", 1, 60, 1),
    N("file_recovery", "File recovery", "file_recovery", "history_days", "Keep snapshots for", "How long a snapshot is kept, in days.", 1, 90, 1),
    A("file_recovery", "File recovery", "Clear history", "Delete the saved versions of every note. Copies kept when a note was changed on disk and in Exhaust at the same time stay. To look at the versions of a note and bring one back, use the command File recovery: Open local history.", "Clear history|clear_history"),

    S("note_composer", "Note composer", "note_composer", "text_after_extract", "Text moved to a new note is replaced by", "What stays in the old note where the text was.", "A link to the new note|An embed of the new note|Nothing"),
    X("note_composer", "Note composer", "note_composer", "template", "Template", "The note whose text starts every note the composer makes, as a path in the vault."),
    T("note_composer", "Note composer", "note_composer", "confirm_merge", "Ask before merging", "Show what will happen and ask before a note is merged into another."),

    T("importer", "Importer", "importer", "keep_folders", "Keep the source's folders", "Make the same folders below the destination folder."),
    X("importer", "Importer", "importer", "folder", "Imported notes go in", "A folder in the vault. Empty uses the top folder."),
    X("importer", "Importer", "importer", "note_name", "Name of a note", "How a note is named: {{title}}, {{filename}}, {{date}}, {{time}} and, for a CSV file, {{Column name}}."),
    X("importer", "Importer", "importer", "template", "Importer template", "A note of the vault whose text wraps every imported note, as a path. Put {{content}} where the imported text goes."),

    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_links_panels", "Backlinks and outgoing links", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_reading_view", "Reading view", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_editing_view", "Editing view", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_tab_header", "Title of a tab", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_files", "File explorer", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_properties_view", "Properties view", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_bookmarks", "Bookmarks", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_outline", "Outline", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_bases", "Bases", "Needs Ctrl held to show the preview."),
    T("page_preview", "Hold Ctrl to preview", "page_preview", "ctrl_required_graph", "Graph view", "Needs Ctrl held to show the preview."),

    T("quick_switcher", "Quick switcher", "quick_switcher", "show_existing_only", "Show only notes that exist", "Leave out the names that links point to but no note answers to."),
    T("quick_switcher", "Quick switcher", "quick_switcher", "show_attachments", "Show attachments", "List images and other files as well as notes."),

    { "sync", "Sync", NULL, NULL, EX_ROW_INFO, "Sync", "Keeping a vault the same on several devices is not in this version. Your notes stay in plain files that any sync program can copy.", 0, 0, 0, "Set up sync" },
    { "sync", "Sharing and publishing", NULL, NULL, EX_ROW_INFO, "Shared vaults", "Inviting collaborators with owner and editor roles (optional, later). Not in this version.", 0, 0, 0, "Invite people" },
    { "sync", "Sharing and publishing", NULL, NULL, EX_ROW_INFO, "Publish", "Turning chosen notes into a website (optional, later). Not in this version.", 0, 0, 0, "Set up publishing" },

    X("templates", "Templates", "templates", "folder", "Template folder", "The folder that holds your template notes. Empty lists every note."),
    X("templates", "Templates", "templates", "date_format", "Date format", "How {{date}} is written in a template, for example YYYY-MM-DD."),
    X("templates", "Templates", "templates", "time_format", "Time format", "How {{time}} is written in a template, for example HH:mm."),

    X("unique_note", "Unique note creator", "unique_note", "folder", "New unique notes go in", "A folder in the vault. Empty uses the top folder."),
    X("unique_note", "Unique note creator", "unique_note", "template", "Template", "The note whose text starts every unique note, as a path in the vault."),
    X("unique_note", "Unique note creator", "unique_note", "prefix_format", "Name format", "How the time in the note's name is written, for example YYYYMMDDHHmm."),
};

static const struct { const char *id, *name, *group; } pages[] = {
    { "general", "General", "Options" }, { "appearance", "Appearance", "Options" }, { "theme", "Theme", "Options" }, { "interface", "Interface", "Options" }, { "editor", "Editor", "Options" },
    { "files", "Files and links", "Options" }, { "export_pdf", "Export to PDF", "Options" }, { "hotkeys", "Hotkeys", "Options" }, { "plugins", "Plugins", "Options" }, { "keychain", "Keychain", "Options" }, { "features", "Core plugins", "Options" },
    { "backlinks", "Backlinks", "Core plugins" }, { "canvas", "Canvas", "Core plugins" }, { "command_palette", "Command palette", "Core plugins" },
    { "daily_notes", "Daily notes", "Core plugins" }, { "file_recovery", "File recovery", "Core plugins" }, { "importer", "Importer", "Core plugins" }, { "note_composer", "Note composer", "Core plugins" },
    { "page_preview", "Page preview", "Core plugins" }, { "quick_switcher", "Quick switcher", "Core plugins" }, { "sync", "Sync", "Core plugins" },
    { "templates", "Templates", "Core plugins" }, { "unique_note", "Unique note creator", "Core plugins" },
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
const char *ex_settings_page_group(size_t i) { return i < sizeof pages / sizeof *pages ? pages[i].group : NULL; }
