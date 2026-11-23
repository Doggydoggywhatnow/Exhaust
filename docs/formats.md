# Exhaust file formats and names

This file is the source of truth for every name that `AGENTS.md` freezes after release: setting keys, theme tokens, snippet grammar, hotkey, workspace and plugin files, slash commands and the Lua API. **Add a key or name here before adding it to the code.** After a release, never rename or remove one; to change it, add a new name and migrate the old value on load. Unknown keys are kept and written back unchanged.

Each item has a name, a type, a default and the version it first appeared in ("since"). Defaults for settings follow the states shown in `screenshots/settings/` where a screenshot shows one; values marked *TODO-verify* could not be confirmed from the screenshots or the Obsidian Help pages and must be decided before the key is implemented.

*TODO-verify* items are assigned: none for the settings keys and commands any more (7c settled the command list and the macOS defaults). The slash-command list is done (see Slash commands), ahead of 3d.

Types: `bool`, `int`, `percent`, `string`, `path` (relative to the vault), `color` (`#rrggbb` or `#rrggbbaa`), `list` (comma separated), `enum(a|b|c)`.

## settings.ini

A plain text INI file in the vault's `.exhaust` folder (and a global copy in the OS config folder). Sections in square brackets, `key = value` lines, `#` comments. Keys are lowercase with underscores.

**How the file is used (sub-slice 2d).**
- *Layers.* A key is read from the vault's `<vault>/.exhaust/settings.ini`; if that file does not set it, from the global `settings.ini` in the OS config folder; if that does not either, the default in the tables below. With no vault open, changes go to the global file.
- *Generated table.* The table of known keys in the code is generated from the tables in this section at build time (`tools/gen_tables.c`), so a key exists in the code only if it is here. A key's type decides what is accepted: `bool` is `true` or `false` (`yes`, `no`, `on`, `off`, `1` and `0` are read too, and written back as `true` or `false` when Exhaust changes the key), `int` and `percent` are whole numbers, `color` is empty or `#rrggbb` or `#rrggbbaa`, an `enum` is one of its listed values (an enum whose list says `...` is read as free text until its values are decided). A stub (`false (stub)`, for Sync and Publish) cannot be turned on.
- *Write-through.* Every change is written at once (on a worker thread, as an atomic replace), not when Exhaust exits.
- *Nothing the user wrote is lost.* A change edits the text of the file in place: comments, blank lines, the order of sections and keys, keys and sections Exhaust does not know, line endings (LF or CRLF), a byte-order mark and a trailing `# comment` after a value all stay exactly as they were; a new key goes at the end of its section and a new section at the end of the file. When a key appears twice the last line counts (as when reading) and is the one changed. A value that does not fit its type is ignored when reading (the next layer applies) and is left in the file untouched.
- *Reading never writes.* The `.exhaust` folder is created by the first change, not when a vault is opened.
- *Unknown keys* can be read through the settings API and are never created by it; plugins and later versions own them.
- Since: 0.1.0.

### [general]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `language` | string | `en` | 0.1.0 |
| `startup_notice` | bool | `false` | 0.1.0 |
| `command_line` | bool | `false` (stub) | 0.1.0 |
| `hardware_acceleration` | bool | `true` | 0.1.0 |

### [appearance]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `color_scheme` | enum(dark\|light\|system) | `system` | 0.1.0 |
| `accent_color` | color | empty (the theme's accent) | 0.1.0 |
| `theme` | string | `exhaust` | 0.1.0 |
| `snippets_enabled` | list | empty | 0.1.0 |
| `font_interface` | string | `Atkinson Hyperlegible` | 0.1.0 |
| `font_text` | string | `Atkinson Hyperlegible` | 0.1.0 |
| `font_mono` | string | `Atkinson Hyperlegible Mono` | 0.1.0 |
| `font_size` | int | `16` | 0.1.0 |
| `quick_font_size` | bool | `true` | 0.1.0 |
| `app_icon` | path | empty (stub) | 0.1.0 |
| `translucent_window` | bool | `false` (macOS only) | 0.1.0 |
| `high_contrast` | enum(off\|on\|system) | `off` | 0.1.0 (7f). See *Shell: status bar, ribbon, zoom, high contrast and window size (7f)*. |
| `window_width` | int | `1100` | 0.1.0. Since 7f the size of a window that has no saved size, between 400 and 10000. |
| `window_height` | int | `750` | 0.1.0. Since 7f as `window_width`. |

### [interface]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `show_tab_title_bar` | bool | `true` | 0.1.0. **Works since 5d:** it shows or hides the pane header, the strip of back and forward arrows, the title, the reading switch and the three-dot menu under the tab strip of every pane (the plan of 5d called the switch `show_pane_header`; this key, from the settings page "Show tab title bar", is the same thing and is used instead of a second name). |
| `show_ribbon` | bool | `true` | 0.1.0. Works since 7f. |
| `ribbon_items` | list | empty (all) | 0.1.0. Works since 7f: the command ids whose buttons the ribbon shows; see *Shell*. |
| `zoom_percent` | percent | `100` | 0.1.0. Works while the program runs since 7f: 50 to 400. |
| `native_menus` | bool | `false` | 0.1.0 |
| `window_frame` | enum(custom\|native\|hidden) | `hidden` | 0.1.0 |
| `settings_in_new_window` | bool | `false` | 0.1.0 |

### [editor]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `default_view` | enum(editing\|reading) | `editing` | 0.1.0 |
| `default_mode` | enum(live_preview\|source) | `live_preview` | 0.1.0 |
| `show_mode_in_status_bar` | bool | `true` | 0.1.0 |
| `inline_title` | bool | `true` | 0.1.0 (works since 4d: the note's name as a title above the text, in every pane, scrolling with the text; Enter renames the file) |
| `readable_line_length` | bool | `true` | 0.1.0 |
| `strict_line_breaks` | bool | `false` | 0.1.0 |
| `properties_in_document` | enum(visible\|hidden\|source) | `visible` | 0.1.0 |
| `fold_heading` | bool | `true` | 0.1.0 |
| `fold_indent` | bool | `true` | 0.1.0 |
| `line_numbers` | bool | `false` | 0.1.0 |
| `indent_guides` | bool | `true` | 0.1.0 |
| `rtl` | bool | `false` | 0.1.0 |
| `spellcheck` | bool | `true` | 0.1.0 |
| `spellcheck_languages` | list | `en_US` | 0.1.0 |
| `auto_pair_brackets` | bool | `true` | 0.1.0 |
| `auto_pair_markdown` | bool | `true` | 0.1.0 |
| `smart_lists` | bool | `true` | 0.1.0 |
| `indent_with_tabs` | bool | `true` | 0.1.0 |
| `indent_width` | int | `4` | 0.1.0 |
| `paste_html_as_markdown` | bool | `true` | 0.1.0 |
| `vim_mode` | bool | `false` | 0.1.0 |

### [files]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `default_file` | enum(last_opened\|empty\|note\|daily_note) | `last_opened` | 0.1.0. Decided in 7a: what is open when a vault is opened and nothing was asked for (an `EXHAUST_OPEN` or a note given on the command line wins): `last_opened` the tabs of the last session, `empty` one empty tab, `note` the note named by `default_file_note` (an empty tab if it does not exist), `daily_note` today's daily note (Daily notes is slice 6e-i, not built: until then it behaves as `last_opened`). |
| `default_file_note` | path | empty | 0.1.0 (7a). The note `default_file = note` opens. |
| `always_focus_new_tabs` | bool | `true` | 0.1.0 |
| `sort_by` | enum(name\|modified\|created) | `name` | 0.1.0 |
| `sort_descending` | bool | `false` | 0.1.0 |
| `new_note_location` | enum(vault\|folder\|same_folder\|subfolder) | `vault` | 0.1.0 |
| `new_note_folder` | path | empty | 0.1.0 |
| `attachment_location` | enum(vault\|folder\|same_folder\|subfolder) | `vault` | 0.1.0 |
| `attachment_folder` | path | empty | 0.1.0 |
| `attachment_subfolder` | string | empty | 0.1.0 |
| `link_format` | enum(shortest\|relative\|absolute) | `shortest` | 0.1.0 |
| `auto_update_links` | bool | `true` | 0.1.0 |
| `link_update` | enum(ask\|automatic\|off) | `ask` | 0.1.0 (4c) |
| `use_wikilinks` | bool | `true` | 0.1.0 |
| `show_all_file_types` | bool | `false` | 0.1.0 |
| `confirm_delete` | bool | `true` | 0.1.0 |
| `rename_folder_on_double_click` | bool | `true` | 0.1.0 (4d): double-clicking the name of a folder in the file explorer edits it in place; never for notes. The switch is on the *Files and links* page of the settings screens (7a) |
| `delete_attachments` | enum(ask\|always\|never) | `ask` | 0.1.0 |
| `trash_mode` | enum(system\|vault_trash\|permanent) | `system` | 0.1.0 |
| `excluded` | list | empty | 0.1.0 |
| `config_folder` | string | `.exhaust` | 0.1.0 |
| `allow_uri_callbacks` | bool | `false` | 0.1.0 |
| `autosave_idle_ms` | int | `1000` | 0.1.0 |
| `autosave_max_ms` | int | `5000` | 0.1.0 |
| `journal_flush_ms` | int | `200` | 0.1.0 |

### [features]

One switch per built-in feature, `true` or `false`. Defaults follow the Core plugins screenshots. Sync and Publish are stubs and stay disabled.

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `audio_recorder` | bool | `false` | 0.1.0 |
| `backlinks` | bool | `true` | 0.1.0 |
| `bases` | bool | `true` | 0.1.0 |
| `bookmarks` | bool | `true` | 0.1.0 |
| `canvas` | bool | `true` | 0.1.0 |
| `command_palette` | bool | `true` | 0.1.0 |
| `daily_notes` | bool | `true` | 0.1.0 |
| `file_recovery` | bool | `true` | 0.1.0 |
| `files` | bool | `true` | 0.1.0 |
| `footnotes` | bool | `false` | 0.1.0 |
| `graph` | bool | `true` | 0.1.0 |
| `note_composer` | bool | `true` | 0.1.0 |
| `outgoing_links` | bool | `true` | 0.1.0 |
| `outline` | bool | `true` | 0.1.0 |
| `page_preview` | bool | `true` | 0.1.0 |
| `properties_view` | bool | `true` | 0.1.0 |
| `publish` | bool | `false` (stub) | 0.1.0 |
| `quick_switcher` | bool | `true` | 0.1.0 |
| `random_note` | bool | `false` | 0.1.0 |
| `search` | bool | `true` | 0.1.0 |
| `slash_commands` | bool | `false` | 0.1.0 |
| `slides` | bool | `false` | 0.1.0 |
| `sync` | bool | `false` (stub) | 0.1.0 |
| `tags_view` | bool | `true` | 0.1.0 |
| `templates` | bool | `true` | 0.1.0 |
| `unique_note` | bool | `true` | 0.1.0 |
| `word_count` | bool | `true` | 0.1.0 |
| `workspaces` | bool | `false` | 0.1.0 |

### [backlinks]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `show_at_bottom` | bool | `false` | 0.1.0 |

### [canvas]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `new_file_location` | enum(vault\|folder\|same_folder\|subfolder) | `vault` | 0.1.0 |
| `wheel` | enum(pan\|zoom) | `pan` | 0.1.0. Decided in 7b: the mouse wheel pans the canvas, or zooms it (those two). |
| `ctrl_drag` | enum(menu\|select) | `menu` | 0.1.0. Decided in 7b: dragging with Ctrl held shows a menu of what to create where it ends, or draws a box that selects the cards inside it. |
| `show_card_names` | enum(always\|selected\|never) | `always` | 0.1.0. Decided in 7b: the name bar above a card of a note or file is shown always, only on the selected card, or never. |
| `snap_to_grid` | bool | `true` | 0.1.0 |
| `snap_to_objects` | bool | `true` | 0.1.0 |
| `zoom_hide_threshold` | percent | `100` | 0.1.0 |

### [command_palette]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `pinned` | list | empty (command ids) | 0.1.0 |

### [daily_notes]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `date_format` | string | `YYYY-MM-DD` | 0.1.0 |
| `folder` | path | empty | 0.1.0 |
| `template` | path | empty | 0.1.0 |

### [file_recovery]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `interval_minutes` | int | `5` | 0.1.0 |
| `history_days` | int | `7` | 0.1.0 |

### [note_composer]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `text_after_extract` | enum(link\|embed\|none) | `link` | 0.1.0. Decided in 7b: what replaces text moved into a new note: a link to it, an embed of it, or nothing. |
| `template` | path | empty | 0.1.0 |
| `confirm_merge` | bool | `true` | 0.1.0 |

### [page_preview]

One `ctrl_required_<place>` key per place, all `bool`.

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `ctrl_required_links_panels` | bool | `true` | 0.1.0 |
| `ctrl_required_reading_view` | bool | `false` | 0.1.0 |
| `ctrl_required_editing_view` | bool | `true` | 0.1.0 |
| `ctrl_required_tab_header` | bool | `true` | 0.1.0 |
| `ctrl_required_files` | bool | `true` | 0.1.0 |
| `ctrl_required_properties_view` | bool | `true` | 0.1.0 |
| `ctrl_required_bookmarks` | bool | `true` | 0.1.0 |
| `ctrl_required_outline` | bool | `true` | 0.1.0 |
| `ctrl_required_bases` | bool | `true` | 0.1.0 |
| `ctrl_required_graph` | bool | `true` | 0.1.0 |

### [quick_switcher]

Decided in 7b: the page has these two options and no others.

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `show_existing_only` | bool | `false` | 0.1.0 |
| `show_attachments` | bool | `true` | 0.1.0 |

### [templates]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `folder` | path | empty | 0.1.0 |
| `date_format` | string | `YYYY-MM-DD` | 0.1.0 |
| `time_format` | string | `HH:mm` | 0.1.0 |

### [unique_note]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `folder` | path | empty | 0.1.0 |
| `template` | path | empty | 0.1.0 |
| `prefix_format` | string | `YYYYMMDDHHmm` | 0.1.0 |

### [export_pdf]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `page_size` | enum(A3\|A4\|A5\|Legal\|Letter\|Tabloid) | `A4` | 0.1.0 |
| `landscape` | bool | `false` | 0.1.0 |
| `margin` | enum(default\|minimal\|none) | `default` | 0.1.0 |
| `downscale_percent` | percent | `100` | 0.1.0 |
| `include_title` | bool | `false` | 0.1.0 |

### [plugins]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `allow` | bool | `false` | 0.1.0 |

### [keychain]

Reserved *(later)*. Secrets are never stored in `settings.ini`; they go in the OS credential store.

| Name | Type | Default | Since |
| --- | --- | --- | --- |

## hotkeys.ini

Plain text. Only changes from the built-in defaults are stored. One section, `[hotkeys]`, with one line per command that has been changed:

```
[hotkeys]
command.id = Mod+E, Ctrl+Shift+T
other.command =
```

- The key is a command id from the command registry (lowercase letters, digits, dots and hyphens).
- The value is a comma separated list of shortcuts; an empty value means the command has no shortcut ("Blank").
- A shortcut is modifiers joined with `+` and then a key: `Ctrl`, `Shift`, `Alt`, `Meta` and `Mod` (`Mod` is Ctrl on Windows and Linux and Cmd on macOS), then a key name such as `A`, `F5`, `Enter`, `Left`.
- Duplicates are allowed and are shown as conflicts in Settings.
- *Where.* `hotkeys.ini` is in the vault's `.exhaust` folder, next to `settings.ini` (with no vault open, in the OS config folder).
- *Key names.* A key is a letter, a digit, `F1` to `F12`, one of `Enter Tab Escape Space Backspace Delete Insert Home End PageUp PageDown Left Right Up Down`, or one of `, . / ; ' [ ] \ - = ` + ` (a comma after a `+` is the key itself, so `Ctrl+,` and a list `Ctrl+,, Ctrl+K` both read right). Modifiers may come in any order and any case; `Mod` is Ctrl, or Cmd on macOS; `Option` is `Alt` and `Cmd` is `Meta`. Shortcuts are compared as chords, so `Shift+Ctrl+t` is `Ctrl+Shift+T`; Exhaust writes them in the canonical form `Ctrl+Alt+Shift+Meta+Key`.
- *What is stored.* A command's line replaces its defaults. A command whose list goes back to the defaults loses its line (only changes are kept). A line for an id this version does not know, a shortcut it cannot read, comments and other sections are kept exactly as they were.
- *macOS defaults.* Decided in 7c: on macOS a default written with `Ctrl` in the Commands table is `Cmd`, except the ones with `Tab`, `PageUp` or `PageDown`, which stay `Ctrl`; `Alt` is Option.
- *What runs.* A key press runs the first command that has its shortcut and can run now (its feature is on and its check passes); two commands with the same shortcut show as a conflict, in red, on the Hotkeys page.
- Since: 0.1.0 (7c).

## Commands

Every command in the command registry, read from the nine hotkeys screenshots (`screenshots/settings/hotkeys-1.png` to `hotkeys-9.png`). **Id** is Exhaust's id (`area.action`, lowercase, hyphens; commands with no area are `app.` or, for text-editing commands, `editor.`). **Default** is the shortcut shown in the screenshots for Windows and Linux; a blank means no default shortcut. **Editor** is *yes* for commands that act on the text at the cursor: those are exactly the commands the slash menu lists. The Editor column is Exhaust's own classification, not read from any screenshot. Since: 0.1.0 for every command.

The code's table of commands (`ex_command_table`) is generated from this table at build time (`tools/gen_tables.c`), so a command exists in the code only if it is listed here. A command is *declared* by this table and *bound* when the code that runs it exists; an unbound command, a command whose feature is switched off in `[features]`, and a command of a stub feature are not available (they are shown disabled, never hidden from the hotkeys screen). A command's feature is its area with hyphens turned into underscores (`daily-notes.` belongs to `daily_notes`), except `graph-view.` (`graph`) and `unique-note-creator.` (`unique_note`); `app.`, `editor.`, `table.` and `format-converter.` commands have no feature switch.

218 commands (203 read from the screenshots, four added in 6a (*Canvas: Add text card*, *Add note or file...*, *Add group* and *Zoom to fit*; the screenshots list no commands for working inside a canvas), three added in 7e (*Workspaces: Save, Load and Manage workspace layout*; the screenshots list the feature but no command for it), two added in 5e (*Move the current tab to the right sidebar* and *to the main area*), one added for GitHub issue 2 (*Toggle main text view*, which collapses the main text view to its handle and brings it back), two added for sub-slice 3e and two for sub-slice 4c (*Random note: Open random note* and *Footnotes: Show footnotes*, which the Core plugins screenshots list as features but give no command for), one added in 6e-iii (*Slides: Start presentation*, the same), *Spelling: Add word to dictionary* and *Spelling: Replace with the next suggestion*, because Exhaust has no context menu yet to pick a suggestion from). Left out on purpose: *Configure mobile Quick Action* (mobile is a non-goal), *Open sandbox vault* and *Show release notes* (Obsidian-specific), and *Copy Obsidian URL for current file*, which is renamed *Copy URL for current file*. Commands for stubbed features (Sync) are present and disabled; *Download attachments for current file* needs the network and is *(optional)*. Checked in 7c: the rows at the top and bottom of each of the nine screenshots are all in the table and in order, with nothing missing between two pages; the macOS defaults are the rule under *hotkeys.ini* above; the shortcuts shown may still be one person's settings rather than defaults.

| Id | Name | Default | Editor |
| --- | --- | --- | --- |
| `app.add-alias` | Add alias |  |  |
| `editor.add-cursor-above` | Add cursor above |  | yes |
| `editor.add-cursor-below` | Add cursor below |  | yes |
| `editor.add-embed` | Add embed |  | yes |
| `app.add-file-property` | Add file property | `Ctrl+;` |  |
| `editor.add-internal-link` | Add internal link |  | yes |
| `editor.add-tag` | Add tag |  | yes |
| `backlinks.open-backlinks-for-the-current-note` | Backlinks: Open backlinks for the current note |  |  |
| `backlinks.show-backlinks` | Backlinks: Show backlinks |  |  |
| `backlinks.toggle-backlinks-in-document` | Backlinks: Toggle backlinks in document |  |  |
| `bases.add-item` | Bases: Add item |  |  |
| `bases.add-view` | Bases: Add view |  |  |
| `bases.copy-table-to-clipboard` | Bases: Copy table to clipboard |  |  |
| `bases.create-new-base` | Bases: Create new base |  |  |
| `bases.insert-new-base` | Bases: Insert new base |  | yes |
| `bases.switch-view` | Bases: Switch view... |  |  |
| `bookmarks.bookmark-all-tabs` | Bookmarks: Bookmark all tabs... |  |  |
| `bookmarks.bookmark-block-under-cursor` | Bookmarks: Bookmark block under cursor... |  |  |
| `bookmarks.bookmark-current-search` | Bookmarks: Bookmark current search... |  |  |
| `bookmarks.bookmark-heading-under-cursor` | Bookmarks: Bookmark heading under cursor... |  |  |
| `bookmarks.bookmark` | Bookmarks: Bookmark... |  |  |
| `bookmarks.remove-bookmark-for-the-current-file` | Bookmarks: Remove bookmark for the current file |  |  |
| `bookmarks.show-bookmarks` | Bookmarks: Show bookmarks |  |  |
| `canvas.convert-to-file` | Canvas: Convert to file... |  |  |
| `canvas.create-new-canvas` | Canvas: Create new canvas |  |  |
| `canvas.export-as-image` | Canvas: Export as image |  |  |
| `canvas.jump-to-group` | Canvas: Jump to group |  |  |
| `canvas.add-text-card` | Canvas: Add text card |  |  |
| `canvas.add-note-card` | Canvas: Add note or file... |  |  |
| `canvas.add-group` | Canvas: Add group |  |  |
| `canvas.zoom-to-fit` | Canvas: Zoom to fit |  |  |
| `app.change-theme` | Change theme... |  |  |
| `app.change-vault` | Change vault... |  |  |
| `app.clear-file-properties` | Clear file properties |  |  |
| `editor.clear-formatting` | Clear formatting |  | yes |
| `app.close-all-other-tabs` | Close all other tabs |  |  |
| `app.close-current-tab` | Close current tab | `Ctrl+W` |  |
| `app.close-others-in-tab-group` | Close others in tab group |  |  |
| `app.close-this-tab-group` | Close this tab group |  |  |
| `app.close-window` | Close window | `Ctrl+Shift+W` |  |
| `command-palette.open-command-palette` | Command palette: Open command palette | `Ctrl+P` |  |
| `app.copy-current-file-path-from-system-root` | Copy current file path from system root |  |  |
| `app.copy-current-file-path-from-vault-folder` | Copy current file path from vault folder |  |  |
| `app.copy-url-for-current-file` | Copy URL for current file |  |  |
| `app.create-new-note` | Create new note | `Ctrl+N` |  |
| `app.create-new-note-in-current-tab` | Create new note in current tab |  |  |
| `app.create-note-to-the-right` | Create note to the right | `Ctrl+Shift+N` |  |
| `editor.cycle-bullet-checkbox` | Cycle bullet/checkbox |  | yes |
| `daily-notes.open-next-daily-note` | Daily notes: Open next daily note |  |  |
| `daily-notes.open-previous-daily-note` | Daily notes: Open previous daily note |  |  |
| `daily-notes.open-today-s-daily-note` | Daily notes: Open today's daily note |  |  |
| `app.decrease-font-size` | Decrease font size |  |  |
| `app.delete-current-file` | Delete current file |  |  |
| `editor.delete-paragraph` | Delete paragraph | `Ctrl+D` | yes |
| `app.download-attachments-for-current-file` | Download attachments for current file |  |  |
| `app.export-to-pdf` | Export to PDF... |  |  |
| `file-recovery.open-local-history` | File recovery: Open local history |  |  |
| `files.create-new-folder` | Files: Create new folder |  |  |
| `files.reveal-current-file-in-navigation` | Files: Reveal current file in navigation |  |  |
| `files.show-file-explorer` | Files: Show file explorer |  |  |
| `app.focus-on-last-note` | Focus on last note |  |  |
| `app.focus-on-tab-group-above` | Focus on tab group above |  |  |
| `app.focus-on-tab-group-below` | Focus on tab group below |  |  |
| `app.focus-on-tab-group-to-the-left` | Focus on tab group to the left |  |  |
| `app.focus-on-tab-group-to-the-right` | Focus on tab group to the right |  |  |
| `editor.fold-all-headings-and-lists` | Fold all headings and lists |  | yes |
| `editor.fold-less` | Fold less |  | yes |
| `editor.fold-more` | Fold more |  | yes |
| `app.follow-link-under-cursor` | Follow link under cursor | `Alt+Enter` |  |
| `format-converter.frontmatter-migration` | Format converter: Frontmatter migration |  |  |
| `app.go-to-last-tab` | Go to last tab | `Ctrl+9` |  |
| `app.go-to-next-tab` | Go to next tab | `Ctrl+Tab`, `Ctrl+PageDown` |  |
| `app.go-to-previous-tab` | Go to previous tab | `Ctrl+Shift+Tab`, `Ctrl+PageUp` |  |
| `app.go-to-tab-1` | Go to tab #1 | `Ctrl+1` |  |
| `app.go-to-tab-2` | Go to tab #2 | `Ctrl+2` |  |
| `app.go-to-tab-3` | Go to tab #3 | `Ctrl+3` |  |
| `app.go-to-tab-4` | Go to tab #4 | `Ctrl+4` |  |
| `app.go-to-tab-5` | Go to tab #5 | `Ctrl+5` |  |
| `app.go-to-tab-6` | Go to tab #6 | `Ctrl+6` |  |
| `app.go-to-tab-7` | Go to tab #7 | `Ctrl+7` |  |
| `app.go-to-tab-8` | Go to tab #8 | `Ctrl+8` |  |
| `graph-view.open-graph-view` | Graph view: Open graph view | `Ctrl+G` |  |
| `graph-view.open-local-graph` | Graph view: Open local graph |  |  |
| `graph-view.start-graph-timelapse-animation` | Graph view: Start graph timelapse animation |  |  |
| `app.increase-font-size` | Increase font size |  |  |
| `editor.indent-list-item` | Indent list item |  | yes |
| `editor.insert-attachment` | Insert attachment |  | yes |
| `editor.insert-callout` | Insert callout |  | yes |
| `editor.insert-code-block` | Insert code block |  | yes |
| `editor.insert-footnote` | Insert footnote |  | yes |
| `editor.insert-horizontal-rule` | Insert horizontal rule |  | yes |
| `editor.insert-markdown-link` | Insert Markdown link | `Ctrl+K` | yes |
| `editor.insert-math-block` | Insert math block |  | yes |
| `editor.insert-table` | Insert table |  | yes |
| `app.make-a-copy-of-the-current-file` | Make a copy of the current file |  |  |
| `app.manage-vaults` | Manage vaults |  |  |
| `app.move-current-file-to-another-folder` | Move current file to another folder |  |  |
| `app.move-current-tab-to-new-window` | Move current tab to new window |  |  |
| `editor.move-line-down` | Move line down |  | yes |
| `editor.move-line-up` | Move line up |  | yes |
| `app.navigate-back` | Navigate back | `Ctrl+Alt+Left` |  |
| `app.navigate-forward` | Navigate forward | `Ctrl+Alt+Right` |  |
| `workspace.move-to-right-sidebar` | Move the current tab to the right sidebar |  |  |
| `workspace.move-to-main-area` | Move the current tab to the main area |  |  |
| `app.new-tab` | New tab | `Ctrl+T` |  |
| `app.new-window` | New window |  |  |
| `note-composer.extract-current-selection` | Note composer: Extract current selection... |  | yes |
| `note-composer.extract-this-heading` | Note composer: Extract this heading... |  | yes |
| `note-composer.merge-current-file-with-another-file` | Note composer: Merge current file with another file... |  |  |
| `app.open-current-tab-in-new-window` | Open current tab in new window |  |  |
| `app.open-file-from-outside-the-vault` | Open file from outside the vault... |  |  |
| `app.open-help` | Open help | `F1` |  |
| `app.open-in-default-app` | Open in default app |  |  |
| `app.open-link-under-cursor-in-new-tab` | Open link under cursor in new tab | `Ctrl+Enter` |  |
| `app.open-link-under-cursor-in-new-window` | Open link under cursor in new window | `Ctrl+Alt+Shift+Enter` |  |
| `app.open-link-under-cursor-to-the-right` | Open link under cursor to the right | `Ctrl+Alt+Enter` |  |
| `app.open-settings` | Open settings | `Ctrl+,` |  |
| `app.open-vault` | Open vault... |  |  |
| `outgoing-links.open-outgoing-links-for-the-current-file` | Outgoing links: Open outgoing links for the current file |  |  |
| `outgoing-links.show-outgoing-links` | Outgoing links: Show outgoing links |  |  |
| `outline.open-outline-of-the-current-file` | Outline: Open outline of the current file |  |  |
| `outline.show-outline` | Outline: Show outline |  |  |
| `random-note.open-random-note` | Random note: Open random note |  |  |
| `footnotes.show-footnotes` | Footnotes: Show footnotes |  |  |
| `properties-view.show-all-properties` | Properties view: Show all properties |  |  |
| `properties-view.show-file-properties` | Properties view: Show file properties |  |  |
| `quick-switcher.open-quick-switcher` | Quick switcher: Open quick switcher | `Ctrl+O` |  |
| `app.reload-app-without-saving` | Reload app without saving |  |  |
| `editor.remove-heading` | Remove heading |  | yes |
| `app.rename-file` | Rename file | `F2` |  |
| `editor.rename-this-heading` | Rename this heading... |  | yes |
| `app.reset-zoom` | Reset zoom | `Ctrl+0` |  |
| `app.save-current-file` | Save current file | `Ctrl+S` |  |
| `app.search-and-replace-in-current-file` | Search & replace in current file | `Ctrl+H` |  |
| `app.search-current-file` | Search current file | `Ctrl+F` |  |
| `search.search-in-all-files` | Search: Search in all files | `Ctrl+Shift+F` |  |
| `editor.set-as-heading-1` | Set as heading 1 |  | yes |
| `editor.set-as-heading-2` | Set as heading 2 |  | yes |
| `editor.set-as-heading-3` | Set as heading 3 |  | yes |
| `editor.set-as-heading-4` | Set as heading 4 |  | yes |
| `editor.set-as-heading-5` | Set as heading 5 |  | yes |
| `editor.set-as-heading-6` | Set as heading 6 |  | yes |
| `app.show-context-menu-under-cursor` | Show context menu under cursor |  |  |
| `app.show-debug-info` | Show debug info |  |  |
| `app.show-in-system-explorer` | Show in system explorer |  |  |
| `app.show-trash` | Show trash |  |  |
| `slides.start-presentation` | Slides: Start presentation |  |  |
| `editor.spelling-add-word-to-dictionary` | Spelling: Add word to dictionary |  | yes |
| `editor.spelling-replace-with-suggestion` | Spelling: Replace with the next suggestion |  | yes |
| `app.split-down` | Split down |  |  |
| `app.split-right` | Split right |  |  |
| `sync.open-activity-log` | Sync: Open activity log |  |  |
| `sync.open-version-history-for-the-current-file` | Sync: Open version history for the current file |  |  |
| `sync.set-up-sync` | Sync: Set up Sync |  |  |
| `sync.show-sync-history` | Sync: Show Sync history |  |  |
| `table.add-column-to-the-left` | Table: Add column to the left |  | yes |
| `table.add-column-to-the-right` | Table: Add column to the right |  | yes |
| `table.add-row-above` | Table: Add row above |  | yes |
| `table.add-row-below` | Table: Add row below |  | yes |
| `table.align-center` | Table: Align center |  | yes |
| `table.align-left` | Table: Align left |  | yes |
| `table.align-right` | Table: Align right |  | yes |
| `table.delete-column` | Table: Delete column |  | yes |
| `table.delete-row` | Table: Delete row |  | yes |
| `table.duplicate-column` | Table: Duplicate column |  | yes |
| `table.duplicate-row` | Table: Duplicate row |  | yes |
| `table.move-column-left` | Table: Move column left |  | yes |
| `table.move-column-right` | Table: Move column right |  | yes |
| `table.move-row-down` | Table: Move row down |  | yes |
| `table.move-row-up` | Table: Move row up |  | yes |
| `tags-view.show-tags` | Tags view: Show tags |  |  |
| `templates.insert-current-date` | Templates: Insert current date |  | yes |
| `templates.insert-current-time` | Templates: Insert current time |  | yes |
| `templates.insert-template` | Templates: Insert template |  | yes |
| `editor.toggle-blockquote` | Toggle blockquote |  | yes |
| `editor.toggle-blue-highlight` | Toggle blue highlight |  | yes |
| `editor.toggle-bold` | Toggle bold | `Ctrl+B` | yes |
| `editor.toggle-bullet-list` | Toggle bullet list |  | yes |
| `editor.toggle-checkbox-status` | Toggle checkbox status | `Ctrl+L` | yes |
| `editor.toggle-code` | Toggle code |  | yes |
| `editor.toggle-comment` | Toggle comment | `Ctrl+/` | yes |
| `app.toggle-default-mode-for-new-tabs` | Toggle default mode for new tabs |  |  |
| `editor.toggle-fold-on-the-current-line` | Toggle fold on the current line |  | yes |
| `app.toggle-fold-properties-in-current-file` | Toggle fold properties in current file |  |  |
| `editor.toggle-green-highlight` | Toggle green highlight |  | yes |
| `editor.toggle-heading` | Toggle heading |  | yes |
| `editor.toggle-highlight` | Toggle highlight |  | yes |
| `editor.toggle-inline-math` | Toggle inline math |  | yes |
| `editor.toggle-italic` | Toggle italic | `Ctrl+I` | yes |
| `app.toggle-left-sidebar` | Toggle left sidebar |  |  |
| `app.toggle-light-dark-mode` | Toggle light/dark mode |  |  |
| `app.toggle-line-numbers` | Toggle line numbers |  |  |
| `app.toggle-live-preview-source-mode` | Toggle Live Preview/Source mode |  |  |
| `editor.toggle-numbered-list` | Toggle numbered list |  | yes |
| `app.toggle-main-text-view` | Toggle main text view |  |  |
| `editor.toggle-orange-highlight` | Toggle orange highlight |  | yes |
| `app.toggle-pin` | Toggle pin |  |  |
| `editor.toggle-purple-highlight` | Toggle purple highlight |  | yes |
| `app.toggle-readable-line-length` | Toggle readable line length |  |  |
| `app.toggle-reading-view` | Toggle reading view | `Ctrl+E` |  |
| `editor.toggle-red-highlight` | Toggle red highlight |  | yes |
| `app.toggle-ribbon` | Toggle ribbon |  |  |
| `app.toggle-right-sidebar` | Toggle right sidebar |  |  |
| `app.toggle-spellcheck` | Toggle spellcheck |  |  |
| `app.toggle-stacked-tabs` | Toggle stacked tabs |  |  |
| `editor.toggle-strikethrough` | Toggle strikethrough |  | yes |
| `app.toggle-window-always-on-top` | Toggle window always on top |  |  |
| `editor.toggle-yellow-highlight` | Toggle yellow highlight |  | yes |
| `app.undo-close-tab` | Undo close tab | `Ctrl+Shift+T` |  |
| `editor.unfold-all-headings-and-lists` | Unfold all headings and lists |  | yes |
| `editor.unindent-list-item` | Unindent list item |  | yes |
| `unique-note-creator.add-unique-internal-link` | Unique note creator: Add unique internal link |  | yes |
| `unique-note-creator.create-new-unique-note` | Unique note creator: Create new unique note |  |  |
| `workspaces.save-workspace-layout` | Workspaces: Save workspace layout |  |  |
| `workspaces.load-workspace-layout` | Workspaces: Load workspace layout |  |  |
| `workspaces.manage-workspace-layouts` | Workspaces: Manage workspace layouts |  |  |
| `app.zoom-in` | Zoom in | `Ctrl+=, Ctrl++` |  |
| `app.zoom-out` | Zoom out | `Ctrl+-` |  |

## workspaces.ini

*Rewritten in 7e* (the earlier table of `left_visible`, `tab.<n>` and so on could not hold splits, and nothing had used it). Plain text, in `.exhaust`, written by the Workspaces feature (`[features] workspaces`, off by default) and by hand if you like. A **layout** is saved under a name: the panes of the main window as they are in the session file (see *Layout tree*), without the window's position, the graph's options or the search text, which are not part of a layout.

The file has one section `[workspace.<name>]` for each layout and, for each, the sections of the session file under the same name, written `[workspace.<name>/<section>]` (for example `[workspace.Writing/tab.3]`):

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `[workspace.<name>]` `saved` | int (Unix seconds) | the time it was saved | 0.1.0 (7e) |
| `[workspace.<name>/layout]` | the keys of the session file's `[layout]` | | 0.1.0 (7e). `left_visible`, `right_visible`, `left_width`, `right_width`, `open_panels`, `right_group`, `active_group`, `active_tab` |
| `[workspace.<name>/window.<n>]`, `/split.<id>`, `/group.<id>`, `/tab.<id>` | the keys of the same sections of the session file | | 0.1.0 (7e). A window's `x`, `y`, `width`, `height` and `maximized` are not saved |

A **name** is 1 to 60 bytes of UTF-8 with no control character and none of `[`, `]` and `/`, and does not start or end with a space; two names that differ only in the case of ASCII letters are the same name. *Save* with a name that is taken asks before it replaces the layout. *Load* replaces the main window's panes with the layout: tabs of notes that no longer exist are dropped, pop-out windows in the layout open as windows (7g), the left and right sidebars take the layout's widths and panels, and nothing is written to any note (an open note keeps its unsaved text; its tab only goes away). A layout that cannot be read is skipped and the rest of the file is kept; unknown sections and keys are kept and written back. The file is read once at launch and written, atomically, by a worker thread after every change; it is Exhaust's file, so comments you add by hand are not kept when it is written.

Commands (declared in the Commands table, gated by the `workspaces` feature): `workspaces.save-workspace-layout` asks for a name; `workspaces.load-workspace-layout` lists the names; `workspaces.manage-workspace-layouts` lists them and offers *Replace with the current layout* and *Delete*.

## Journal and session state

Exhaust saves continuously (see the README's Saving). Two things make that safe: the **recovery journal**, which holds every edit that is not yet in a note file (crash safety), and the **session state**, which holds what was open (restore on launch). Both are user data: never delete them except after the text is safely in the note file, and never rebuild them from scratch. File recovery snapshots (history of saved versions) are separate and live in the file recovery store (see slice 7e).

### Where they live

In the OS local-data folder, keyed by vault, **not** in the OS cache folder (the OS may clear a cache) and **not** inside the vault (sync tools would copy them between devices). The folder for a vault is `<local-data>/Exhaust/vaults/<vault-id>/`:

| OS | `<local-data>` |
| --- | --- |
| Linux | `$XDG_DATA_HOME`, or `~/.local/share` when unset |
| Windows | `%LOCALAPPDATA%` (not `%APPDATA%`, which roams) |
| macOS | `~/Library/Application Support` |

`<vault-id>` is the first 32 hexadecimal digits of the SHA-256 of the vault's canonical absolute path (symbolic links resolved). The search index and other caches use the same id under the OS cache folder. *TODO-verify*: whether SDL3's preferences-folder function returns these folders on all three OSes (on Windows it uses the roaming folder); the platform layer should compute the folders itself.

### Journal file

`journal/<n>.jrn` in the vault's folder, binary, little-endian, append-only. A new file starts after 64 MB or at launch.

| Field | Type | Meaning |
| --- | --- | --- |
| magic | 4 bytes | `EXJ1` |
| version | u32 | `1` |

Then records until the end of the file. Each record:

| Field | Type | Meaning |
| --- | --- | --- |
| length | u32 | bytes in the record after this field |
| type | u8 | `B` base, `E` edit, `S` saved |
| timestamp | u64 | milliseconds since the Unix epoch |
| path_length | u16 | length of the path in bytes |
| path | bytes | vault-relative path, UTF-8, with `/` separators |
| offset | u64 | byte offset of the edit in the note's text as Exhaust edits it: the byte-order mark removed and, for a file that uses CRLF throughout, line breaks as a single LF (edit records) |
| removed | u64 | number of bytes removed at the offset (edit records) |
| inserted_length | u32 | number of bytes inserted (edit records) |
| inserted | bytes | the inserted text, UTF-8 (edit records) |
| base_size, base_hash | u64, 16 bytes | size and the first 16 bytes of the SHA-256 of the file as saved, written when a file is opened (base records) and after every successful save (saved records) |
| checksum | u32 | CRC-32 of the record from `type` to the last field before the checksum |

Rules:
- A record with a bad checksum, or a short record at the end of the file (a power cut in mid-write), ends replay at that point; everything before it is replayed.
- Replay applies the edits since the last saved record for a path onto the file as it is on disk. If the file's size and hash no longer match the last base or saved record, the conflict banner appears and both versions are kept.
- After an atomic save succeeds, a saved record is appended. Records before the last saved record for a path may then be removed when a journal file is compacted. Nothing is removed before its saved record exists.
- Since: 0.1.0, frozen after release.

### Session state file

`session.ini` in the same folder, plain text, INI style, rewritten atomically on every change (write-through). It is separate from `workspaces.ini`, which holds layouts you saved by name.

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `[session]` `version` | int | `1` | 0.1.0 |
| `[window.<n>]` `x`, `y`, `width`, `height` | int | the last position and size | 0.1.0 |
| `[window.<n>]` `maximized` | bool | `false` | 0.1.0 |
| `[layout]` `left_visible`, `right_visible` | bool | `true` | 0.1.0 |
| `[layout]` `left_width`, `right_width` | int (pixels at 100% zoom, kept between 120 and 1200) | `260`, `300` | 0.1.0 |
| `[layout]` `open_panels` | list of `left:<name>` or `right:<name>` | empty | 0.1.0 |
| `[layout]` `right_group` | int | none | 0.1.0 (5e). The id of the group of tabs the right sidebar holds (notes, a graph): its `[group.<id>]` section and the `[tab.<id>]` sections whose `group` is that id belong to it. The sidebar shows its tabs when it has some and the panels otherwise, or when the user pressed a view button of the sidebar's strip; the group has no window and no parent, and is never split. |
| `[layout]` `active_tab` | int | empty (the active tab of the active group; written for readers, the group's own `active` is what is restored) | 0.1.0 |
| `[layout]` `active_group` | int | the first group of the main window | 0.1.0 (added with 2c) |
| `[tab.<id>]` `path`, `view`, `mode` | string | required | 0.1.0 |
| `[tab.<id>]` `pinned`, `stacked` | bool | `false` | 0.1.0 |
| `[tab.<id>]` `group` | int | `0` (the split it is in) | 0.1.0 |
| `[tab.<id>]` `cursor_line`, `cursor_column` | int | `0` | 0.1.0; `cursor_line` counts from 0, `cursor_column` is the byte offset in that line (from 0); both are clamped to the text on restore and never land inside a character |
| `[tab.<id>]` `scroll_y` | float (pixels at 100% zoom, one decimal) | `0` | 0.1.0 |
| `[graph]` `open` | bool | the last state | 0.1.0; 5a: that a graph tab is open (as a tab with view `graph`); pan and zoom are not saved |
| `[graph]` `local_depth` | int 1 to 5 | `1` | 0.1.0 (5c). How many links away from the open note the local graph reaches (a link either way counts). Used by tabs with view `localgraph`; the local graph also uses the filters, groups, forces and display options above. |
| `[tab.<id>]` `view` = `localgraph` | string | | 0.1.0 (5c). The local graph tab (`graph-view.open-local-graph`): the open note and the notes within `local_depth` links of it, following whichever note is open in another tab. `path` is empty. |
| `[graph]` `search` | string | empty | 0.1.0 (5b). Filter: a query in the search language of the README (terms, `path:`, `file:`, `tag:`, `[property:value]`); only the notes it finds stay in the graph, with the attachments and unresolved names they link to. At most 200 bytes. |
| `[graph]` `show_orphans` | bool | `true` | 0.1.0 (5b). Off removes the nodes that no edge touches (after the other filters). |
| `[graph]` `show_unresolved` | bool | `true` | 0.1.0 (5b). Off removes the nodes for names no note answers to. |
| `[graph]` `show_attachments` | bool | `true` | 0.1.0 (5b). Off removes the nodes for files that are not notes. (5a showed them always; Obsidian's default is off, ours stays on so a graph does not change under a user.) |
| `[graph]` `group_1_query` .. `group_6_query` | string | empty | 0.1.0 (5b). A group colours the nodes of the notes its query finds (same language as `search`; at most 200 bytes). A node in several groups takes the lowest number. An empty query is no group. |
| `[graph]` `group_1_color` .. `group_6_color` | int 0 to 5 | `n - 1` | 0.1.0 (5b). The colour slot of the group: 0 `colors.accent`, 1 `colors.success`, 2 `colors.warning`, 3 `colors.error`, 4 `colors.tag`, 5 `colors.link` (existing theme tokens; no new token). |
| `[graph]` `center_force` | float 0 to 1 | `0.3` | 0.1.0 (5b). The pull of every node to the middle (a force of `center_force * 0.05` of the distance, times the temperature). |
| `[graph]` `repel_force` | float 0 to 20 | `10` | 0.1.0 (5b). The push between nodes: the charge is `-3 * repel_force` (10 gives the 5a value, -30). |
| `[graph]` `link_force` | float 0 to 1 | `1` | 0.1.0 (5b). The spring's strength. Since 5g the strength is this value **to the fifth power** times 5a's value (1 gives 5a's picture; 0.5 a thirty-second of it; 0 no links): a spring's pull is balanced by the repulsion, so the length of a link changes with the logarithm of the strength, and a straight scale showed nothing over most of the slider. The stored value is unchanged, so older files read the same. |
| `[graph]` `link_distance` | float 10 to 200 | `30` | 0.1.0 (5b). The spring's length, in layout units. |
| `[graph]` `arrows` | bool | `false` | 0.1.0 (5b). An arrow head on every line, at the note that is linked to. |
| `[graph]` `text_fade` | float -3 to 3 | `0` | 0.1.0 (5b). Labels appear above a zoom of `0.9 * 2^text_fade` (a higher value needs more zoom, so fewer labels). |
| `[graph]` `node_size` | float 0.3 to 3 | `1` | 0.1.0 (5b). A multiple of the circle's radius. |
| `[graph]` `text_size` | float 0.5 to 3 | `1` | 0.1.0 (5g). A multiple of the size of the labels, on top of the growth with the zoom (see Graph view zoom and dragging). |
| `[graph]` `link_thickness` | float 0.3 to 4 | `1` | 0.1.0 (5b). A multiple of the line's width. |
| `[search]` `text` | string | empty | 0.1.0 |
| `[canvas]` `view.<path>` | string `x y zoom` | none | 0.1.0 (6a). The view of the canvas open at `<path>` (the path from the vault, with `=` and `%` written as `%3D` and `%25`): the canvas point in the middle of the pane (whole numbers) and the zoom (0.02 to 6, three decimals). Written now and then while a canvas is open and read when its tab is made; a value that is not three finite numbers in range is ignored and the canvas is fitted to the pane. |

**Graph view zoom and dragging (5g).** *Zoom* runs from 0.01 to 200 pixels per layout unit (5a stopped at 8). A node's circle grows as the square root of the zoom between 0.7 and the zoom's own square root (5a capped the growth at 1.8 times), so the circles keep growing when you zoom in. A label appears at the zoom set by `text_fade` and, past it, grows as the square root of the zoom over that zoom, in steps of a quarter octave (so the glyph cache stays small), up to 6 times its size and times `text_size`; the width a label may take grows with it. *Dragging.* Pressing on a node and moving the pointer more than 5 pixels drags it (a press that does not move is a click that opens the note; a press on empty space pans). The node is pulled towards the pointer by a spring, not pinned to it: while it is dragged the layout keeps running at a temperature of at least 0.25 and its links, the repulsion and the pull to the middle still act on it, so it follows the pointer most of the way and a node dragged far from its links is held back by them. When released the layout is warmed to 0.3 and the node settles where the forces hold it (a place the forces accept stays; one they do not is pulled back into its cluster). `Alt+Left/Right/Up/Down` on the selected node pulls it 40 pixels that way for 0.4 s. Dragging changes the view of the graph only, never a note or any file.

**Graph view keys (5c).** With the graph focused (click it, or Tab): `Left Right Up Down` pan; `+` `-` zoom and `0` or `Home` fit; `Shift+Left/Right/Up/Down` select the nearest node in that direction (from the selected one, or the node nearest the middle); `Page Down` and `Page Up` select the next and previous node; `Enter` opens the selected node, `Ctrl+Enter` (`Cmd+Enter` on a Mac) in a new tab; `m` opens the node's menu at the node; `p` shows the page preview of the node (`Escape` closes it, and a second `Escape` clears the selection); `a` starts or stops the animation; `g` opens or closes the gear panel; `[` and `]` change the depth of a local graph; `Escape` clears the selection. The gear and the wand are buttons: `Tab` reaches them. The wand starts the animation: the notes appear in the order they were created (`files.ctime`; a note whose ctime is unknown counts as the oldest), a name or file that is not a note appears with the first note that links to it, over about ten seconds, and the animation ends when all are shown or when it is stopped.

**Layout tree (added with sub-slice 2c, since 0.1.0).** The window's panes form a tree of *groups* (a strip of tabs with one active tab) and *splits* (a row or a column of groups and splits). Ids are positive integers, written in values as `g<id>` for a group and `s<id>` for a split.

| Name | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[window.<n>]` `main` | bool | `false` | exactly one window is the main window; the others are pop-out windows |
| `[window.<n>]` `root` | node | required | the top of the window's layout tree, for example `s1` |
| `[split.<id>]` `direction` | enum(horizontal\|vertical) | `horizontal` | `horizontal` places the children side by side, `vertical` stacks them |
| `[split.<id>]` `children` | list of nodes | required | in order, at least two |
| `[split.<id>]` `ratios` | list of float | equal shares | one share for each child; they are normalized when read |
| `[group.<id>]` `tabs` | list of ints | empty | tab ids in display order (pinned tabs come first) |
| `[group.<id>]` `active` | int | the first tab | id of the active tab |
| `[group.<id>]` `history_pos` | int | `-1` (no history) | 0.1.0 (5d). The index of the place the pane shows now in its history (the pane's list of places, newest last, at most 50). Back and forward move it. |
| `[group.<id>]` `history_<n>` | string | none | 0.1.0 (5d). Place `n` (from 0): `path\|view\|cursor_line\|cursor_column\|scroll_y`, the path from the vault (empty for a graph), the view (`note`, `graph`, `localgraph`, ...), and where the note's cursor and scroll were when the pane left it. A place whose note is gone is skipped by back and forward and dropped from the list. A value that does not parse is ignored. |
| `[group.<id>]` `linked` | list of groups | empty | linked panes follow each other: opening a note in one opens it in the others |
| `[tab.<id>]` `group` | int | required | the group the tab is in (replaces the earlier "the split it is in") |

A file without a layout tree (an older session file) opens as one group holding all its tabs. Unknown sections and keys are kept.

Settings changes are not stored here: they are written to `settings.ini` at once (see slice 2d). Unknown keys are kept and written back unchanged.

## themes/*.theme

A plain text file in `.exhaust/themes` (per vault) or the global config folder. INI style, `#` comments.

```
[theme]
name = My theme
author = Someone
base = dark          # light or dark: the built-in theme that supplies missing values

[colors]
accent = #8167eb

[fonts]
text = Atkinson Hyperlegible

[sizes]
radius = 6
```

Sections: `[theme]` (name, author, base), `[colors]`, `[fonts]`, `[sizes]`. Any token not set comes from `base`. Values: colors as `#rrggbb` or `#rrggbbaa`, fonts as a font family name, sizes as integers in pixels at 100% zoom.

### Palette

The built-in color values. **Exhaust Dark** takes its values from the colors measured in `screenshots/` (the navy background, the lilac text, the purple accent); the screenshots' graph colors come from the second, neutral-grey screenshot set. **Exhaust Light**, **Exhaust High Contrast Dark** and **Exhaust High Contrast Light** are not in any screenshot: their values are derived here by hand and are Exhaust's own choices. Every text pair was checked against WCAG contrast: 4.5:1 (normal text) for Dark and Light, 7:1 for the high contrast themes; `text_faint`, `link_unresolved` and `accent` are held to 3:1 because they are for hints and controls. Alpha is the last two hex digits.

| Token | Dark | Light | High contrast dark | High contrast light | Source |
| --- | --- | --- | --- | --- | --- |
| `colors.background` | `#1e1f2d` | `#f7f8fc` | `#000000` | `#ffffff` | Dark measured; others derived |
| `colors.background_alt` | `#181924` | `#eceef6` | `#000000` | `#ffffff` | Dark measured; others derived |
| `colors.surface` | `#181924` | `#ffffff` | `#0b0b14` | `#ffffff` | Dark measured; others derived |
| `colors.surface_hover` | `#2b2d3c` | `#e3e6f2` | `#1c1c2e` | `#e8e8f4` | Dark measured; others derived |
| `colors.border` | `#323343` | `#cfd3e4` | `#ffffff` | `#000000` | Dark measured; others derived |
| `colors.text` | `#cad1f2` | `#1e1f2d` | `#ffffff` | `#000000` | Dark measured; others derived |
| `colors.text_muted` | `#9196b1` | `#4b4f6b` | `#e6e8ff` | `#1a1a2e` | Dark measured; others derived |
| `colors.text_faint` | `#7f839b` | `#5d6180` | `#c8cbe8` | `#3a3a52` | Dark measured; others derived |
| `colors.accent` | `#8167eb` | `#5b3fd1` | `#b6a5ff` | `#3a1fb0` | Dark measured; others derived |
| `colors.accent_hover` | `#9580f0` | `#4a2fb8` | `#d0c4ff` | `#2a1090` | derived |
| `colors.link` | `#a191f2` | `#4a35c4` | `#c9bdff` | `#2a10a0` | Dark measured; others derived |
| `colors.link_unresolved` | `#796eb2` | `#7a6fb8` | `#a99fe0` | `#5a4a9a` | Dark measured; others derived |
| `colors.selection` | `#8167eb59` | `#5b3fd140` | `#b6a5ff80` | `#3a1fb040` | derived |
| `colors.cursor` | `#cad1f2` | `#1e1f2d` | `#ffffff` | `#000000` | Dark measured; others derived |
| `colors.highlight` | `#e8b58d40` | `#e8a16466` | `#ffd23f80` | `#ffd23f99` | derived |
| `colors.error` | `#dc91a8` | `#b3304f` | `#ff9fb8` | `#a0002a` | Dark measured; others derived |
| `colors.warning` | `#e8b58d` | `#9a5a10` | `#ffc78f` | `#7a3f00` | Dark measured; others derived |
| `colors.success` | `#a6d9a0` | `#2e7d32` | `#9dffb0` | `#00602a` | derived |
| `colors.code_background` | `#181924` | `#eceef6` | `#0b0b14` | `#f0f0f8` | Dark measured; others derived |
| `colors.code_text` | `#cad1f2` | `#1e1f2d` | `#ffffff` | `#000000` | Dark measured; others derived |
| `colors.tag` | `#a191f2` | `#4a35c4` | `#c9bdff` | `#2a10a0` | follows link |
| `colors.graph_node` | `#aaaab2` | `#6b6e86` | `#ffffff` | `#000000` | Dark measured; others derived |
| `colors.graph_node_unresolved` | `#414141` | `#c4c7d8` | `#8a8a9a` | `#8a8a9a` | Dark measured; others derived |
| `colors.graph_link` | `#3f3f3f` | `#c4c7d8` | `#9a9ab0` | `#6a6a80` | Dark measured; others derived |
| `colors.graph_focus` | `#a190ea` | `#5b3fd1` | `#d0c4ff` | `#2a1090` | Dark measured; others derived |
| `colors.canvas_grid` | `#323343` | `#dfe2ee` | `#4a4a60` | `#c8c8d8` | follows border |
| `colors.callout_note` | `#89a4f2` | `#3556c8` | `#a9c0ff` | `#1a3a9a` | derived |
| `colors.callout_abstract` | `#7fc8e8` | `#1d7a99` | `#9fe0ff` | `#005a78` | derived |
| `colors.callout_info` | `#7fb5f2` | `#2a6fc0` | `#9fd0ff` | `#0a4a98` | derived |
| `colors.callout_todo` | `#7fb5f2` | `#2a6fc0` | `#9fd0ff` | `#0a4a98` | derived |
| `colors.callout_tip` | `#7fe0d0` | `#14766a` | `#9ff5e8` | `#005548` | derived |
| `colors.callout_success` | `#a6d9a0` | `#2e7d32` | `#9dffb0` | `#00602a` | derived |
| `colors.callout_question` | `#e8d27f` | `#8a6d00` | `#fff09f` | `#6a5200` | derived |
| `colors.callout_warning` | `#e8b58d` | `#9a5a10` | `#ffc78f` | `#7a3f00` | derived |
| `colors.callout_failure` | `#dc91a8` | `#b3304f` | `#ff9fb8` | `#a0002a` | derived |
| `colors.callout_danger` | `#e87f8f` | `#b01f3a` | `#ff9fa8` | `#900020` | derived |
| `colors.callout_bug` | `#e87fb0` | `#b01f6a` | `#ff9fd0` | `#900050` | derived |
| `colors.callout_example` | `#b99ff2` | `#6a40c0` | `#d0baff` | `#4a1a98` | derived |
| `colors.callout_quote` | `#9196b1` | `#5d6180` | `#c8cbe8` | `#3a3a52` | derived |

In Dark, `background_alt`, `surface` and `code_background` share the darker `#181924` (cards, inputs and the title strip), and `background` is the lighter `#1e1f2d`. These values replace "from the built-in theme" in the token table below; a theme file overrides any of them.

### Theme tokens

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `colors.background` | color | see Palette | 0.1.0 |
| `colors.background_alt` | color | see Palette | 0.1.0 |
| `colors.surface` | color | see Palette | 0.1.0 |
| `colors.surface_hover` | color | see Palette | 0.1.0 |
| `colors.border` | color | see Palette | 0.1.0 |
| `colors.text` | color | see Palette | 0.1.0 |
| `colors.text_muted` | color | see Palette | 0.1.0 |
| `colors.text_faint` | color | see Palette | 0.1.0 |
| `colors.accent` | color | see Palette | 0.1.0 |
| `colors.accent_hover` | color | see Palette | 0.1.0 |
| `colors.link` | color | see Palette | 0.1.0 |
| `colors.link_unresolved` | color | see Palette | 0.1.0 |
| `colors.selection` | color | see Palette | 0.1.0 |
| `colors.cursor` | color | see Palette | 0.1.0 |
| `colors.highlight` | color | see Palette | 0.1.0 |
| `colors.error` | color | see Palette | 0.1.0 |
| `colors.warning` | color | see Palette | 0.1.0 |
| `colors.success` | color | see Palette | 0.1.0 |
| `colors.code_background` | color | see Palette | 0.1.0 |
| `colors.code_text` | color | see Palette | 0.1.0 |
| `colors.tag` | color | see Palette | 0.1.0 |
| `colors.graph_node` | color | see Palette | 0.1.0 |
| `colors.graph_node_unresolved` | color | see Palette | 0.1.0 |
| `colors.graph_link` | color | see Palette | 0.1.0 |
| `colors.graph_focus` | color | see Palette | 0.1.0 |
| `colors.canvas_grid` | color | see Palette | 0.1.0 |
| `colors.callout_note` | color | see Palette | 0.1.0 |
| `colors.callout_abstract` | color | see Palette | 0.1.0 |
| `colors.callout_info` | color | see Palette | 0.1.0 |
| `colors.callout_todo` | color | see Palette | 0.1.0 |
| `colors.callout_tip` | color | see Palette | 0.1.0 |
| `colors.callout_success` | color | see Palette | 0.1.0 |
| `colors.callout_question` | color | see Palette | 0.1.0 |
| `colors.callout_warning` | color | see Palette | 0.1.0 |
| `colors.callout_failure` | color | see Palette | 0.1.0 |
| `colors.callout_danger` | color | see Palette | 0.1.0 |
| `colors.callout_bug` | color | see Palette | 0.1.0 |
| `colors.callout_example` | color | see Palette | 0.1.0 |
| `colors.callout_quote` | color | see Palette | 0.1.0 |
| `fonts.interface` | string | see `settings.ini` | 0.1.0 |
| `fonts.text` | string | see `settings.ini` | 0.1.0 |
| `fonts.mono` | string | see `settings.ini` | 0.1.0 |
| `sizes.radius` | int | `6` | 0.1.0 |
| `sizes.radius_large` | int | `12` | 0.1.0 |
| `sizes.spacing` | int | `8` | 0.1.0 |
| `sizes.border_width` | int | `1` | 0.1.0 |
| `sizes.ribbon_width` | int | `44` | 0.1.0 |
| `sizes.tab_height` | int | `36` | 0.1.0 |
| `sizes.line_height` | percent | `150` | 0.1.0 |

### Loading themes and snippets (7d)

*Since 0.1.0.* The theme in use is built from the settings in this order, each step replacing only what it sets:
1. the built-in theme of the colour scheme: `[appearance] color_scheme` is `dark`, `light`, or `system` (what the operating system says);
2. the theme file named by `[appearance] theme` (`exhaust` or empty: none). A theme file is found by its `[theme] name` or, without one, its file name without `.theme`, in `<vault>/.exhaust/themes` and then the global `themes` folder; a vault file hides a global one with the same file name; names are compared without regard to case. A theme with a `[theme] base` of its own is built over that built-in theme whatever the colour scheme is (so it can be dark only or light only); without one it follows the colour scheme. A name with no file is the built-in theme;
3. `[appearance] accent_color` (when it is a colour): `colors.accent`, and `colors.accent_hover` as the same colour 15 percent lighter;
4. the snippets named in `[appearance] snippets_enabled`, in that order, from `<vault>/.exhaust/snippets/<name>.style` (then the global `snippets` folder); a name with no file is skipped.

A snippet's `@tokens` block sets tokens by name (`colors.*`, `sizes.*`, `fonts.*`, as in the table above; a bad value is ignored), and `callout.<type> { color: #rrggbb; }` sets `colors.callout_<type>`. Every other rule of a snippet (the widgets, states and properties of the list below) is read and left alone in 0.1.0; it is kept in the file and will take effect in a later version. Unknown keys of a theme file are kept in the theme and written back unchanged when the settings editor changes another token (comments, other sections and the order of the file stay too).

*Live reload.* A worker thread looks at the theme file and the enabled snippet files about four times a second, and lists the folders every two seconds. When one changed, was added or was deleted, the theme is built again and replaces the one on screen within a second; a deleted theme file falls back to the built-in theme. The interface thread never reads the files.

*The settings editor.* The page *Theme* of the settings (7d) shows the theme in use, a list to choose another, the snippets with a switch each, and every token as a row with its colour (or value) in a text field. Changing a token writes it into the user's theme file: into the file of the chosen theme, or, with the built-in one, into a new `custom.theme` in the vault's themes folder (named Custom, with the base of the colour scheme) that then becomes the chosen theme. The built-in themes are never changed.

*The contrast check.* The editor warns when a pair of colours is hard to read (WCAG contrast ratio): `colors.text` on `colors.background`, on `colors.surface`, `colors.text_muted`, `colors.link` and `colors.error` on `colors.background` and `colors.code_text` on `colors.code_background` need 4.5 to 1; `colors.text_faint` and `colors.accent` on `colors.background` need 3 to 1. The built-in themes pass. A warning never stops a theme from being used.

## snippets/*.style

A plain text file in `.exhaust/snippets`, switched on and off in Settings. It does not use CSS. A file is a list of rules:

```
# comment
@tokens {
  colors.accent: #ff8800;
}

callout.my-type {
  color: #3a7;
  icon: lightbulb;
}

tab.active {
  background: #222;
  radius: 4;
}
```

- `@tokens { name: value; }` overrides theme tokens (the names above).
- `widget[.state] { property: value; }` styles a widget. Widgets: `editor`, `file-tree`, `tab`, `ribbon`, `sidebar`, `status-bar`, `graph`, `canvas`, `callout.<type>`, `code-block`, `settings`, `hotkeys`, `menu`, `popover`. States: `active`, `hover`, `focus`, `disabled`.
- Properties: `color`, `background`, `border`, `radius`, `padding`, `font`, `size`, `icon` (callouts only).
- Unknown widgets, states and properties are ignored and kept. Since: 0.1.0.

## Plugin manifest

`.exhaust/plugins/<id>/manifest.ini`:

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `id` | string | required (lowercase letters, digits, hyphens) | 0.1.0 |
| `name` | string | required | 0.1.0 |
| `version` | string | required (`x.y.z`) | 0.1.0 |
| `api` | int | required (Exhaust Lua API version, `1`) | 0.1.0 |
| `author` | string | empty | 0.1.0 |
| `description` | string | empty | 0.1.0 |
| `entry` | path | `main.lua` | 0.1.0 |
| `permissions` | list | empty | 0.1.0 |

### Permission names

`vault.read`, `vault.write`, `editor.read`, `editor.write`, `commands.register`, `ui.pane`, `ui.notice`, `settings.own`, `network`. A plugin can use only what its manifest lists, and the user approves the list when the plugin is turned on. `network` is always off until the user also turns on network use.

## Lua API (API version 1)

Everything is in the `exhaust` table. A function that needs a permission fails with an error if the plugin lacks it.

| Function | Permission |
| --- | --- |
| `exhaust.version()` | none |
| `exhaust.commands.register(id, name, fn, opts)` | `commands.register` |
| `exhaust.commands.run(id)` | `commands.register` |
| `exhaust.vault.read(path)` | `vault.read` |
| `exhaust.vault.list(dir)` | `vault.read` |
| `exhaust.vault.exists(path)` | `vault.read` |
| `exhaust.vault.write(path, text)` | `vault.write` |
| `exhaust.vault.rename(from, to)` | `vault.write` |
| `exhaust.editor.text()` | `editor.read` |
| `exhaust.editor.selection()` | `editor.read` |
| `exhaust.editor.cursor()` | `editor.read` |
| `exhaust.editor.replace_selection(text)` | `editor.write` |
| `exhaust.editor.set_text(text)` | `editor.write` |
| `exhaust.ui.notice(text)` | `ui.notice` |
| `exhaust.ui.add_pane(id, opts)` | `ui.pane` |
| `exhaust.settings.get(key)` | `settings.own` |
| `exhaust.settings.set(key, value)` | `settings.own` |
| `exhaust.events.on(name, fn)` | none |
| `exhaust.http.get(url)` | `network` |

Event names for `exhaust.events.on`: `vault-open`, `file-open`, `file-save`, `file-rename`, `editor-change`. Since: 0.1.0.

## Canvas (6a)

A canvas is a `.canvas` file in the JSON Canvas 1.0 format (https://jsoncanvas.org): a JSON object with `nodes` and `edges`. Exhaust reads and writes all of it and keeps what it does not know.

**Reading.** Cards (`nodes`) are `text` (Markdown in `text`), `file` (`file` is a path from the vault, `subpath` a `#heading` or `#^block`), `link` (`url`) and `group` (`label`, `background`, `backgroundStyle`); each has `id`, `x`, `y`, `width`, `height` and an optional `color`. A card of another `type`, or an element of `nodes` that is not an object, is kept as it is and not drawn. Edges have `id`, `fromNode`, `fromSide`, `fromEnd`, `toNode`, `toSide`, `toEnd`, `color` and `label`; sides are `top`, `right`, `bottom`, `left` (without one the sides that face each other are used), ends are `none` and `arrow` (the start has none and the end an arrow when the file does not say). A card without an `id`, or with one another card has, gets a new one (the file is only written when the user changes something). A file that is not a JSON object is shown as a message and never written. An empty file is an empty canvas.

**Keeping.** Keys Exhaust does not know are kept, with their values as written (numbers keep their spelling: `1e2`, `2.50`), on the canvas, on every card and on every edge, and so is the order of the keys of every object. A canvas is written only after the user changed it.

**Writing.** The layout of the JSON Canvas samples and of Obsidian: a tab for each level, `"nodes":[` and `"edges":[` with one card or edge on each line, no space after `:` or `,`, an empty list as `[]`, a line break at the end only if the file had one. A file in this layout is written back byte for byte. A file in another layout is rewritten in this one after the first change (same content). New ids are 16 lowercase hexadecimal digits.

**Colors.** `"1"` to `"6"` are theme colors, no new tokens: 1 red `colors.error`, 2 orange `colors.warning`, 3 yellow `colors.callout_question`, 4 green `colors.success`, 5 cyan `colors.callout_tip`, 6 purple `colors.callout_example`. `"#rrggbb"` (or `#rgb`) is the file's own color and is used as it is. The grid is `colors.canvas_grid`.

**Settings** are the `[canvas]` keys above: `wheel` (the mouse wheel pans or zooms; Ctrl turns one into the other), `ctrl_drag` (`menu`: Ctrl-drag draws a box and then asks what to make there; `select`: it selects the cards in the box; Shift-drag always selects), `show_card_names` (the file name above a file card: `always`, `selected` or `never`), `snap_to_grid` (a grid of 20 canvas units for moving and resizing), `snap_to_objects` (moving lines the box up with the edges and middles of other cards within 6 pixels), `zoom_hide_threshold` (in percent: below this zoom cards are outlines; the default 100 hides the text of every card when the canvas is zoomed out from its natural size) and `new_file_location` (`vault`: the vault folder; `same_folder`: the folder of the open note; the other two values, which need a folder name no setting holds yet, behave as `vault` for now).

**Working in a canvas.** Mouse: a press on empty space and a drag pans; a click selects a card or an edge (Shift adds); a drag moves the selected cards (the cards inside a group move with it); a drag of the border of the selected card resizes it (at least 40 units); a drag from a dot beside the selected card to another card makes an edge (to empty space: to a new text card); a double click on empty space makes a text card and edits it, on a text card edits it, on a file card opens the file, on a link card opens the address in the default browser, on an edge asks for its label, on a group asks for its name; the right button opens a menu. Keyboard (the canvas has the focus): arrows pan, or move the selected cards by 20 units (Shift: 100); `+` `-` zoom, `0` fits everything, `1` zooms to the natural size, Home fits the selection; Enter edits or opens the selected card (Shift: in a new tab) or asks for the selected edge's label; Delete and Backspace remove the selection; Ctrl+A selects everything; Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste cards (as JSON Canvas text on the clipboard, so they can go to another canvas; text that is not a canvas becomes a text card); Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z undo and redo (the document's undo); Escape clears the selection, or ends editing a card.

**Editing and saving.** Every change goes into the note's document as one edit when the gesture ends (so one Undo takes it back), which puts it in the recovery journal and the autosave like typing. While a card's text is edited it is a Markdown editor over the card; it ends with Escape or a click elsewhere. What a file card shows is the start of the note (about 1500 bytes, read on the service thread; the part after `subpath` when there is one); a file that is not a note shows its name until the viewers of 6c exist.

**Link-safe rename.** Renaming or moving a file rewrites the `file` of every card that points at it and the links in the Markdown of text cards (like the links of a note); the canvas that moves keeps its cards' paths (they are from the vault). A canvas that cannot be read is left as it is and counted as one that could not be handled.

## Bases expressions (6b-i)

The language of filters, formulas and custom summaries of `.base` files and `base` code blocks. It follows the Bases help of Obsidian (the page on functions, the syntax and the formulas) and JavaScript where those say "follows JavaScript". It is `include/exhaust_base_expr.h`; nothing in it reads a file.

**Syntax.** Numbers (`1`, `2.5`, `.5`, `1e3`), text in single or double quotes (escapes `\n \t \r \b \f \0 \\ \' \" \uXXXX`), `true`, `false`, `null`, lists `[1, "a"]`, regular expressions `/pattern/flags` (flags `g i m s`; a `/` after an operand is a division), names, `.name`, `[index or name]` and calls `f(args)` and `value.f(args)`. Operators by precedence, tightest first: `!` and unary `-`; `* / %`; `+ -`; `< > <= >=`; `== !=` (also written `===` and `!==`); `&&`; `||`. Parentheses group. A name is letters, digits, `_`, `$` and non-ASCII characters, and may contain a `-` that is followed by a letter and has no space around it: `image-property` is one name, `price - age` and `price-5` are subtractions. Nesting is limited to 120 levels.

**Names.** `value`, `index` and `acc` inside `filter`, `map` and `reduce`; `values` in a custom summary; `file` (the row's file), `this` (the file the base is shown for), `note` and `formula` (followed by `.name` or `["name"]`); any other name is a property of the row's note (`price` is `note.price`), or null. A file value answers its fields (`file.name`, `basename`, `path`, `folder`, `ext`, `size`, `properties`, `tags`, `links`, `embeds`, `backlinks`, `ctime`, `mtime`), `.file` (itself) and, for any other name, the property of that note (`this.status`, `link("x").asFile().status`).

**Types.** null, boolean, number, string, date, duration, list, object, link, URL, file, regular expression, and the render values html, image and icon. An error is a value (shown in a cell); nothing throws. A missing value (null) makes the result of arithmetic and of a function called on it missing too, and is nothing in a text (`"a" + null + "b"` is `"ab"`); `==` between different types is false; `<` and the other orderings need two numbers, two texts, two dates or two durations (a missing value compares false with everything).

**Truth.** False: `false`, `0`, `NaN`, `""`, `null`, an empty list, an empty object. Everything else is true. `&&` and `||` return the operand that decided, as in JavaScript.

**Dates.** A date is a moment (`date("2025-01-01")` has no time of day, `date("2025-01-01 12:00:00")` has one); text is read as `YYYY-MM-DD`, with `HH:mm`, `:ss` and `.SSS` after it, `T` for the space and `Z` or `+hh:mm` at the end. Dates are in the system time zone. `date + "1M"`, `date - "2h"` and `date + duration("1d")` add or take away a duration; whole days keep a date without a time a date without a time; months are calendar months (January 31 plus a month is the last day of February); `date - date` is a duration of exact time; `date.year .month .day .hour .minute .second .millisecond`. A duration is written `1y`, `2M`, `1w`, `3d`, `4h`, `5m`, `6s`, `7ms` (also `year(s)`, `month(s)`, `week(s)`, `day(s)`, `hour(s)`, `minute(s)`, `second(s)`, `millisecond(s)`, with a space allowed after the number, decimals allowed and several amounts in a row: `1y 2M`); `duration * number`, `duration / number`, `duration + duration` and `duration - duration` work with the duration on the left; a duration has `.days .hours .minutes .seconds .milliseconds .weeks .months .years` (whole numbers, a month counting as 30.436875 days). Dates are shown as `YYYY-MM-DD` or `YYYY-MM-DD HH:mm:ss`.

**Numbers as text** are written as JavaScript writes them (`1e+21`, `1e-7`, `0.30000000000000004`). `round` rounds halves up; `toFixed` writes the exact decimal value of the number.

**The 72 functions** (the 68 of the help, and the four list statistics `sum`, `mean`, `median`, `stddev` that the help's own summary example uses; `stddev` is the population standard deviation): global `escapeHTML date duration file html if image icon link list max min now number today random`; on any value `isTruthy isType toString`; date `date format time relative isEmpty`; string `contains containsAll containsAny endsWith isEmpty lower replace repeat reverse slice split startsWith title trim`; number `abs ceil floor isEmpty round toFixed`; list `contains containsAll containsAny filter flat isEmpty join map reduce reverse slice sort unique sum mean median stddev`; link `asFile linksTo`; file `asLink hasLink hasProperty hasTag inFolder`; object `isEmpty keys values`; regular expression `matches`. Choices where the help does not say: `title()` capitalises the first letter of each word and lowers the rest; `flat()` goes one level deep; `reverse()` returns a reversed copy; a text pattern in `replace` is not a regular expression and every occurrence goes; `link("[[Note#Head|Shown]]")` is a link to `Note`, heading `Head`, shown as `Shown`; `file(text)` is null when no file answers; `hasTag` ignores case and a leading `#` and matches nested tags (`x` matches `#x/y`); `sort()` puts null, then booleans, numbers, texts, dates and durations; `now()` and `today()` take the clock the program gives; a function called on null gives null (except `isTruthy`, `isType`, `toString` and `isEmpty`); URLs have the string functions. `max` and `min` take numbers or one list and skip nulls.

**Limits.** Evaluating one expression stops with an error after three million steps; formulas that use each other nest at most 12 deep and a circle is an error naming it (`circular reference: a -> b -> a`); a text made by `repeat` or `+` is at most one megabyte; regular expressions run with a match limit.

**Dates in text (Moment tokens).** `format` and the daily notes use `YYYY YY Y`, `M MM MMM MMMM Mo`, `D DD Do DDD DDDD`, `d dd ddd dddd do e E`, `w ww wo` (weeks start on Sunday and week 1 holds January 1) and `W WW Wo` (ISO weeks), `gggg gg GGGG GG`, `Q Qo`, `H HH h hh k kk`, `m mm`, `s ss`, `S` to `SSSSSSSSS`, `a A`, `Z ZZ`, `X x`, the long forms `L LL LLL LLLL l ll lll llll LT LTS` (English, US), text in `[square brackets]` and `\` before a character. Names are English. Reading a text back into a date (`ex_date_parse_format`, for daily notes) understands `YYYY YY M MM MMM MMMM D DD Do DDD DDDD ddd dddd H HH h hh m mm s ss S.. a A` and `[text]`.

## Bases files and views (6b-ii, 6b-iii)

*Since 0.1.0.* A base is YAML (`include/exhaust_yaml.h` over the vendored libyaml; `include/exhaust_base.h`), in a `.base` file or in a fenced block with the language `base`. **The file belongs to Exhaust**, but it is only written when the user changes the base (the view menu, the settings of a view, the source editor); opening, drawing and running never write it. The writer keeps every key it does not know (at any depth), the order of the keys, the style of each value (plain, quoted, flow or block) and anchors; **comments are not kept** (libyaml drops them). A file that is not valid YAML is shown as a message and never written.

**Keys.** At the top: `filters` (a filter for every view), `formulas` (name: expression), `properties` (a property's `displayName`), `summaries` (name: expression over `values`) and `views`. A filter is a string (an expression of *Bases expressions*) or a mapping with `and`, `or` or `not` holding a list of filters. A view has `type` (`table`, `list`, `cards`, `kanban`, `map`; another value is shown as a table), `name`, `filters`, `groupBy` (`property`, `direction`: `ASC` or `DESC`), `groupOrder` (the order of the groups; a group not listed follows, `null` is the group of rows with no value), `order` (the columns: a list of property ids, `file.name`, `note.price`, `formula.ppu`, or a bare name for a property of the note), `sort` (a list of `property` and `direction`), `limit`, and `summaries` (property id: a built-in summary name or a custom one from the top). Layout keys: table `rowHeight` (`short`, `medium`, `tall`, `extra tall`); list `markers` (`bullet`, `number`, `none`), `separator` (the text between the columns of an item, `, ` by default) and `indentProperties` (`true`: one column to a line); cards `cardSize` (the width in pixels at 100% zoom, 160 to 400), `image` (the property that holds a `#rrggbb` color or a text) and `imageAspectRatio` (height over width, 0.2 to 3), `imageFit`; kanban `columnWidth`; map `coordinates` (the property with `[latitude, longitude]`, `note.coordinates` by default). A key a layout does not use is kept and ignored.

**Data.** The files a view runs over come from the index on the service thread (`src/index/base_data.c`): every file of the vault with its frontmatter properties (typed by the index), tags, links and embeds, and its creation and modification time. Results are made there too (`ex_base_run`) and handed to the interface as one finished result; the interface thread never runs a view.

**In a note.** A fenced block `` ```base `` and a line that is only `![[File.base]]` or `![[File.base#View]]` are shown in reading mode as the Markdown of the view (a table, or a list for a list view), with `this` the note. The note's text is never changed.

**The view (6b-iii).** A `.base` file opens in a tab of its own (view `base`). The toolbar has the view menu (the views of the base, add a view of a type, rename, delete), *Results* (copy to the clipboard as a Markdown table, export as CSV, limit, run again), *Settings* (the layout keys above, as a menu) and *Edit source* (the YAML in an editor; typing there is the same as editing the file). Every change from a menu is **one edit of the document** (so the recovery journal, the autosave and Undo cover it; Undo gives back the very same text). The view runs again about a second after the vault changed and at once after the base changed. A group heading collapses and opens its group (not saved). A file name, a link or an address in a cell opens on a click; right click: open, open in a new tab, copy, export. The table draws only the rows in view and keeps its header and the summary row; the list, cards and kanban layouts skip by arithmetic the rows above and below the view, so a frame costs the same for 20,000 rows as for 20 (test `frame_budget`). *Map:* the pins are drawn on a plain grid of longitude and latitude; map tiles need the network and are not built in 0.1.0 (the README's optional tile address has no setting yet: ISSUES 59).

**Export.** *Export as CSV* writes `<base name> - <view name>.csv` beside the base (a number is added when the name is taken); a save dialog comes with the PDF export (6e). The CSV has the headings, then one line per row, quoted as RFC 4180.

**Commands.** `bases.create-new-base` (a new `Untitled.base` with one table view, beside the open note), `bases.add-view`, `bases.switch-view` (opens the view menu), `bases.copy-table-to-clipboard`, `bases.add-item` (a new note beside the base; the view's filters are not applied to it), `bases.insert-new-base` (a `base` block with one table view, in the editor). The switch is `[features] bases`.

## Pictures (6c-i)

*Since 0.1.0.* `include/exhaust_image.h` decodes PNG, JPEG, GIF and BMP (stb_image), WebP (libwebp, decoder only), AVIF (an own reader of the ISO base media file format and dav1d) and SVG (nanosvg) into 8-bit RGBA with straight alpha. The format is found from the first bytes (the extension is only used by the file explorer). **Limits:** a side over 16384 pixels or more than 100 million pixels is refused before anything is allocated; a file over 64 MB is not read; a picture bigger than 4096 on its long side is reduced (a box filter weighted by alpha) and keeps the size the file says (`src_w`, `src_h`); an SVG is drawn at the width asked for (its own width when none, at most 4096). **JPEG** is turned by its EXIF orientation (values 1 to 8). **GIF** gives its first frame; **WebP** has no animation; **AVIF**: the primary item (`av01`, or a `grid` of `av01` tiles), an alpha item that refers to it with `auxl`, the properties `ispe`, `irot` and `imir`; the colour comes from the AV1 sequence header (matrix, range, 8, 10 or 12 bits, 4:0:0, 4:2:0, 4:2:2, 4:4:4; chroma is repeated, not interpolated); ICC profiles, HDR tone mapping, `iovl`, `clap` and image sequences are not built. **SVG:** nanosvg has no CSS, text or filters.

**The picture cache** (`include/exhaust_imagecache.h`, one for the window): textures found by path (and for an SVG by the width it is drawn at: 256, 512 ... 4096); the first `get` for a path asks the application's loader (a service-thread job, `QJ_IMAGE`, which reads and decodes); the pixels go to the video card about a million pixels a frame; the textures are kept under 128 MB, the one drawn longest ago dropped first; a change of the vault's files drops every entry (the pictures open are read again). A texture belongs to one window: pop-out windows show a message instead. **The image tab** (`include/exhaust_imageview.h`): tab view `image`; mouse wheel zooms about the pointer (0.02 to 32 times), a press and drag pans, a double click toggles fitted and the picture's own size; keys `+`, `-`, `0` (fit), `1` (own size), arrows, Home. Nothing here writes a file.

## Audio (6c-ii)

*Since 0.1.0.* `include/exhaust_audio.h`. **Decoders** (found from the first bytes: `RIFF...WAVE`, `fLaC`, `OggS`, an `ID3` tag or an MP3 frame header): WAV and FLAC by lib-Peep, MP3 by dr_mp3, Ogg Vorbis by stb_vorbis. lib-Peep reads from memory and allocates nothing; for a FLAC file the decoder lends it a workspace (64 KB, more when lib-Peep says so, at most 64 MB). Every decoder gives interleaved stereo 16-bit at the file's own rate: mono is duplicated, three or more channels use the first two, a source wider than 16 bits is truncated (lib-Peep's default: bit-exact for 16-bit sources). A file is read whole into memory, at most 256 MB, by a service-thread job (`QJ_AUDIO`) that also opens the decoder (an MP3's frames are counted: this is why it is not done on the UI thread) and the player's output device. **The player** (`ExPlayer`): one SDL3 audio stream for the window; its callback (the audio thread) reads the state and a seek request from atomics, decodes at most 1024 frames at a time into a buffer on its own stack, writes the position to atomics and gives silence when paused or at the end; it takes no lock of ours, allocates nothing and reads no file. Attaching a decoder locks the stream for an instant (SDL does not run the callback meanwhile), switches the stream's input format to the new file's rate (SDL converts rate and channels to the device) and starts paused at the beginning. Volume is SDL's stream gain (0 to 1). **The tab** (`include/exhaust_audioview.h`, tab view `audio`): opening it asks for the file (paused); the player holds one file at a time, so a tab whose file is not the one held shows "loading" and Play loads it and plays when it arrives; Space, Left and Right (5 seconds), Up and Down (5% of the volume), Home. A file that cannot be played says why; for `.m4a`, `.webm` and `.3gp` (libmpv's, 6c-iii) it says the media add-on is not installed and offers *Open externally* (the system's own program). Nothing here writes a file.

## libmpv (6c-iii)

*Since 0.1.0.* `include/exhaust_mpv.h`. **Loading.** libmpv is opened with `SDL_LoadObject` the first time a media file is opened, from (in this order) the path in the environment variable `EXHAUST_MPV_LIB` (for tests and the add-on's installer), `addons/<name>` next to the program, then the system's names: `libmpv.so.2`, `libmpv.so.1`, `libmpv.so` (Linux), `libmpv.2.dylib`, `libmpv.dylib`, `/opt/homebrew/lib/libmpv.dylib`, `/usr/local/lib/libmpv.dylib` (macOS), `libmpv-2.dll`, `mpv-2.dll`, `mpv-1.dll`, `libmpv.dll` (Windows). 15 functions are resolved by name and the client API's major version must be 2; a library that fails either test is unloaded and the reason kept. Nothing is linked. **A player** (`ExMpv`) is one libmpv handle and one thread: the thread reads libmpv's events and properties (`time-pos`, `duration`, `pause`, `width`, `height`) into atomics and, for video, draws each new frame with the **software renderer** (`MPV_RENDER_API_TYPE_SW`, format `rgb0`) at the size the widget asked for into a buffer; two buffers are swapped under a short lock, and the interface takes the latest with `ex_mpv_frame` and uploads it as a texture. The interface thread never calls libmpv; opening (a service-thread job, `QJ_MPV`) and closing (`ex_mpv_close_async`: a detached thread) can take a moment. Options set: `config=no`, `load-scripts=no`, `ytdl=no`, `terminal=no`, `input-default-bindings=no`, `osc=no`, `load-unsafe-playlists=no`, `access-references=no`, `use-filedir-conf=no`, `pause=yes`, `keep-open=yes`, `idle=yes`, `vo=libmpv` (`vo=null` and `vid=no` for audio only), `audio-display=no`. Only a local file path is passed. **The tab** (`include/exhaust_mediaview.h`, tab view `media`): the picture fitted in the pane, a bar below with play and pause, the position, and the volume; mouse and keys as the audio tab; a double click on the picture toggles play. One libmpv player at a time; starting another closes the first and pauses the built-in player (one sound at a time). A player whose file fails shows libmpv's error and *Open externally*. **Tests** use a stand-in library (`tests/stub_mpv.c`, built as a shared library) that behaves as a very small player; no real libmpv is used in CI (the add-on package, 9d, is checked by the owner by hand).

## PDF (6c-iv)

*Since 0.1.0.* `include/exhaust_pdf.h`. **Loading:** `SDL_LoadObject` from `EXHAUST_PDFIUM_LIB` (tests), `addons/pdfium.dll`, `addons/libpdfium.dylib` or `addons/libpdfium.so` next to the program, then the system's name; 38 functions of the public API are resolved by name (a library that lacks one is refused). The library is started once with the configuration of version 2 and no isolate. **One thread a document:** PDFium may be used by one thread at a time, so each open document has a thread and every call happens there; the interface posts requests (`render` a page at a pixel size, `text` of a page with the box of every character, `links` of a page, `search` of the whole document) and polls results; a request of the same kind for the same page replaces one still waiting; a search replaces a running search and stops at its next page; results of a search arrive in pieces (the hits of some pages), the last has `done`. Pages are drawn white with PDFium's flags 0 (no annotations or form widgets), as BGRA, and handed on as opaque RGBA. **Privacy:** Exhaust never calls PDFium's form-fill environment, JavaScript, save, print or network functions; a link is reported as `page` (inside the document) or `uri` (an address), a launch action, a remote or embedded go-to and a link with no target are dropped; the address is only given to the application, which asks the user and then opens it in the system browser through the path of an external link in a note. **The tab** (tab view `pdf`, `include/exhaust_pdfview.h`): pages in a column, fit width (the default), fit page or a zoom of 0.05 to 8 times; only the pages near the window are rendered and kept (4 on each side of it); the outline (bookmarks, up to 16 deep) opens on the left; search is typed into the box (it searches as you type); selection is within one page, by character; Ctrl+C copies the selection as text. **Links to a selection:** the fragment of a link to a PDF is `#page=N&selection=S,E` (the page counted from 1; S the index of the first character and E the index after the last, among the characters PDFium reports for that page's text); `ex_pdf_parse_fragment` reads it; Ctrl+Shift+C copies `[[name.pdf#page=N&selection=S,E]]` (`[[name.pdf#page=N]]` with no selection). **Tests** use a stand-in library (`tests/stub_pdfium.c`: three pages, an outline, links of every kind, text and search); the real PDFium is the owner's check with the add-on package.

## Embeds (6c-v)

*Since 0.1.0.* **Notes** (`include/exhaust_embed.h`, `src/index/embed_expand.c`, run by the service thread after queries and bases): in the text reading mode shows, a line that is only `![[name]]`, `![[name#Heading]]` or `![[name#^block]]`, where the name has no extension or `.md` and resolves through the index like a link, is replaced by `> *name*`, `>` and the embedded text (at most 3000 bytes, as the page preview gives it) with every line quoted; a heading adds ` > Heading` to the title. Not inside a code fence; not for names with another extension; not for a name that does not resolve; the embedded text is not expanded again; at most 8 embeds a note. The note is never changed. **Media** (the reading view, `src/ui/reading_view.c`): a paragraph whose only content is an embed or a Markdown image whose address is a file of the vault. Written as `name|size#fragment`: `size` is `640x480` or `100` (a width in points); `#fragment` follows the name. Pictures (kind by extension: `.avif .bmp .gif .jpeg .jpg .png .svg .webp`) are drawn from the picture cache at the size written (the natural size, at most the column's width, when none; a picture that is not loaded yet is a 16:9 box until it is, and the layout is made again when it arrives); audio (`.flac .m4a .mp3 .ogg .wav .3gp`), video (`.mkv .mov .mp4 .ogv .webm`), PDFs and canvases are cards (a rounded box with the kind, the file name and for a PDF its fragment); a click gives the link handler `path#fragment` as an embed link: an audio file the built-in decoders take plays or pauses on the one player, anything else opens in a tab and a PDF goes to `page=N` (and selects `selection=S,E`). An address (`http://` or `https://`) is a placeholder card, nothing is fetched. A name that does not resolve is shown as `[embedded: name]`. **Links:** `[[f.pdf#page=3&selection=120,148]]`, and links to pictures, audio, video and canvases, open that file (a PDF at the page and the text) instead of making a note.

## Math (6d-i)

*Since 0.1.0.* **Where it shows:** the reading view draws `$$ ... $$` blocks centered in display style and `$...$` in the text style, with STIX Two Math (built in, SIL OFL 1.1, `third_party/fonts/stix-two-math/`, version 2.13b171, 838 KB); the editor (source mode, live preview) shows the source as before. **How:** `include/exhaust_math.h`, `src/text/math.c`: a parser for the LaTeX subset below, then TeX's layout rules (Appendix G) with the numbers of the font's OpenType `MATH` table read through HarfBuzz's `hb-ot-math` (the constants, size variants and glyph assemblies of stretchy signs, italic corrections, accent positions). The result is a list of glyphs (font glyph index, position, size) and rules, in units of the font size (em), so the layout does not depend on the pixel size, and a cache keyed by a hash of the source, the style and the font keeps layouts between frames. Nothing is laid out on a worker: one equation takes well under a millisecond, and the reading view makes the layouts while it lays out the page, at most 24 new ones in a pass (the others show as source until the next frame, which lays out again), so the cost of a frame does not grow with the number of equations; layouts are kept in a cache keyed by a hash of the source and the style and dropped when a pass does not ask for them. No setting; the text color is the note's text color. **Supported:** letters (italic, as TeX does), digits, `+ - = < > ( ) [ ] / | ! , ; : . ' *`; `^` and `_` (also both, and `'` for a prime); `{...}` groups; `\frac`, `\dfrac`, `\tfrac`, `\binom`; `\sqrt` and `\sqrt[n]`; `\left` ... `\right` with `( ) [ ] \{ \} | \| \langle \rangle \lfloor \rfloor \lceil \rceil .` (stretched to the content); big operators `\sum \prod \coprod \int \iint \iiint \oint \bigcup \bigcap \bigvee \bigwedge \bigoplus \bigotimes` (larger in display style, limits above and below for the sums, products and unions in display style, `\limits` and `\nolimits` change that), the functions `\sin \cos \tan \cot \sec \csc \arcsin \arccos \arctan \sinh \cosh \tanh \log \ln \lg \exp \det \dim \ker \gcd \deg \arg \hom \Pr` and `\lim \limsup \liminf \max \min \sup \inf`, `\operatorname{name}`; Greek letters (`\alpha` ... `\omega`, the capitals that differ from Latin ones, `\varepsilon \vartheta \varphi \varpi \varrho \varsigma`); about 130 symbols (relations, arrows, operators, `\infty \partial \nabla \forall \exists \emptyset \ldots \cdots \vdots \ddots` and more: the table is `symtab` in `src/text/math.c`); accents `\hat \check \tilde \bar \vec \dot \ddot \widehat \widetilde \overline \underline`; `\overset`, `\underset`, `\stackrel`; fonts `\mathrm \mathit \mathbf \boldsymbol \mathbb \mathcal \mathfrak \mathsf \mathtt \text \textbf \textit` ; spaces `\, \: \> \; \! \  \quad \qquad ~`; `\displaystyle \textstyle \scriptstyle \scriptscriptstyle`; the environments `matrix pmatrix bmatrix Bmatrix vmatrix Vmatrix cases aligned` (rows end with `\\`, cells with `&`). **Spacing** follows TeX's table of atom classes (ordinary, operator, binary, relation, opening, closing, punctuation, inner) and drops the thick and medium spaces in script styles. **Errors:** an unknown command, an unmatched brace or `\left`, an unknown environment or an input over 8192 bytes or 64 levels deep gives no layout: the reading view shows the source as it was written, in the error color. Math is never changed in the note. Not built: `\substack`, `\not`, `\boxed`, `\color`, `\xrightarrow`, over- and underbraces, `array` with column specifications, `\tag`, equation numbering, `align`, `\newcommand`, math in the editor's live preview.

## Mermaid (6d-ii, 6d-iii)

*Since 0.1.0.* **Where it shows:** the reading view draws a fenced code block whose language is `mermaid` as a diagram, centered, shrunk to the width of the page (never enlarged) and in the theme's colors; the editor shows the source. A diagram that cannot be parsed or laid out is shown as its source in the error color with one muted line under it that says why (`Mermaid: ...`). Nothing is fetched or run: Exhaust has its own parsers and layouts for the types below, no JavaScript and no web view. **How:** `include/exhaust_diagram.h` and `src/diagram/` turn the text into a list of items (shapes with a box, polylines with heads at their ends, texts, pie slices) in units of the font size, with no renderer involved; `src/ui/diagramdraw.c` draws the items. `ex_diagram_dump` prints the whole layout, one item a line with numbers to two decimals: that listing is the *golden render output* of the tests (`tests/data/mermaid/*.golden`, compared token by token with a tolerance of 0.03 so rounding on another system cannot fail it; `test_mermaid DATA_DIR --update` writes them again after the pictures have been looked at). Text widths are the sum of the advances of the built-in interface font (no kerning), so a layout never depends on the system's fonts. A layout is cached by a hash of the source; the reading view makes at most 3 new layouts in a pass (the others show as source until the next frame, which lays out again). Limits: 40,000 bytes of source, 600 nodes (flowchart), 40 participants and 1,500 events (sequence), 300 classes, 400 states, 300 entities, 100 slices, 300 tasks; more gives an error. A diagram may start with front matter (`---` ... `---`) and `%%` comment lines, which are skipped; `%%{init}%%` directives, `classDef`, `class`, `style`, `linkStyle`, `click`, `accTitle`, `accDescr` and `title` are read and ignored (colors, styles and links of a diagram are not built).

**Flowchart** (`flowchart` or `graph`, then `TD`, `TB`, `BT`, `LR` or `RL`; default top to bottom; statements are separated by lines or `;`): nodes `A`, `A[text]` (rectangle), `A(text)` (rounded), `A([text])` (stadium), `A[[text]]` (subroutine), `A[(text)]` (cylinder), `A((text))` (circle), `A(((text)))` (double circle), `A{text}` (diamond), `A{{text}}` (hexagon), `A>text]` (flag), `A[/text/]` and `A[\text\]` (parallelograms), `A[/text\]` and `A[\text/]` (trapezoids); text may be quoted (`"..."`, so it can hold brackets), `<br/>` and `\n` break lines, `#quot; #amp; #lt; #gt;` are decoded and other tags are dropped; `:::class` is skipped. Links: `-->`, `---`, `-.->`, `-.-`, `==>`, `===`, `~~~` (takes part in the layout, not drawn), `--o`, `--x`, `<-->`, `o--o`, `x--x`, with text as `-->|text|` or `-- text -->` (`-. text .->`, `== text ==>`); chains (`A --> B --> C`), groups (`A & B --> C & D`), a link to the node itself (a loop on its right). `subgraph id [Title]` ... `end`, nested, with the box drawn around its nodes (nodes belong to the subgraph in which they are first written); `direction` inside a subgraph is ignored. **Layout:** layered: cycles are broken by turning the links that close them, ranks follow the longest path, every long link has a point on every rank, the order in a rank is improved by barycenter sweeps and the position across by isotonic regression (nodes keep their order and a minimum gap), the rank between two node ranks holds the link labels. The same input always gives the same picture. Links are straight lines through their points (no curves), ending on the outline of the node (an ellipse and a diamond by their own equation, the rest by their box).

**Sequence diagram** (`sequenceDiagram`): `participant A`, `participant A as Alice`, `actor A as Bob` (an actor has rounded ends), `create participant` (like `participant`); messages `->>` and `-->>` (arrow head; dashed for `--`), `->` and `-->` (no head), `-x` and `--x` (cross), `-)` and `--)` (open head), `A->>B: text`, `A->>+B` activates B, `B-->>-A` deactivates B; `activate A` and `deactivate A`; `Note over A`, `Note over A,B`, `Note left of A`, `Note right of A`; `autonumber` (a number before the text); blocks `loop`, `alt`/`else`, `opt`, `par`/`and`, `critical`/`option`, `break` and `rect` (a frame without a title, colors are not built), nested; `box ... end` groups are accepted and not drawn; `link`, `links`, `destroy` and `title` are skipped. Participants are drawn at the top and the bottom with dashed lifelines; the columns are spread so every message and note fits between its participants; a message to oneself is a loop; an activation is a thin box on the lifeline, nested ones are shifted. Errors: a message line without a known arrow, a note without `over`, `left of` or `right of`, `else`, `and` or `option` outside a block, `end` without a block, a block not closed.

**Class diagram** (`classDiagram`): `class Name`, `class Name { ... }` with one member a line (a member with `(` is a method and goes below the line, the others are attributes), `<<annotation>>` inside the body or as `<<interface>> Name`, `Name : member`, `class A["Shown name"]`, generics written `List~int~` shown as `List<int>`, `direction`; relations `<|--` `--|>` (inheritance, a triangle at the parent), `*--` `--*` (composition, filled diamond), `o--` `--o` (aggregation), `<--` `-->` (association, open arrow), `<..` `..>` (dependency, dashed), `<|..` `..|>` (realization, dashed triangle), `--` `..` (links), each with cardinalities in quotes on both sides (`A "1" --> "0..*" B`) and a label after a colon. The class on the left of a relation is above the one on the right. A class box has the annotation in guillemets, the name in bold, then the attributes and the methods, separated by lines. `namespace`, `note`, `style`, `classDef`, `cssClass`, `click` and `link` are read and ignored.

**State diagram** (`stateDiagram` and `stateDiagram-v2`): states `A`, `state "Long name" as A`, `A : description` (more lines add up; a state with a description shows its name in bold above a line), `[*]` as the start (a dot) when it is written first in a transition and as the end (a circle with a dot) when it is last, one of each in every composite state; transitions `A --> B : label`; `state X { ... }` composite states, laid out on their own and shown as a box with the title and the inside (a transition between states of different composites is drawn to the box of the composite at the level where they meet); `state X <<choice>>` (a diamond), `<<fork>>` and `<<join>>` (a bar); `note right of A : text`, `note left of A : text` and `note right of A` ... `end note` (a note box tied to the state by a dashed line; the side is not kept); `direction`; `--` (the divider of concurrent regions), `classDef`, `class`, `style`, `click`, `hide` and `scale` are read and ignored.

**Entity-relationship diagram** (`erDiagram`): entities written in relationships or with a body `NAME { type name PK, FK "comment" }` (keys `PK`, `FK`, `UK`; the columns are type, name, keys, comment), an alias `p[Person]`, relationships `A ||--o{ B : label` where each side is one of `||` (exactly one), `|o` / `o|` (zero or one), `}|` / `|{` (one or more) and `}o` / `o{` (zero or more), the line `--` (solid, identifying) or `..` (dashed, non-identifying), the label plain or in quotes; `direction`. The marks are drawn as crow's feet and bars at the ends of the line. An entity name may hold `-`.

**Pie chart** (`pie`, then `showData` and `title text` on that line or on lines of their own, then `"label" : value` lines): slices run clockwise from the top, the largest first (equal ones in the order written), in the callout colors in an order that keeps neighbours apart (blue, green, yellow, orange, red, purple, teal, pink, ...); the percentage (a whole number) is inside each slice of 3% or more, in whichever of the text and background colors reads better on it; the legend is on the right and shows `label [value]` with showData. A value cannot be negative; the total must be above zero; at most 100 slices.

**Gantt chart** (`gantt`): `title`, `dateFormat YYYY-MM-DD` (the only format; another is an error), `axisFormat` with `%Y %y %m %d %e %b %B %a %A %H %M %j %%` (default `%Y-%m-%d`), `section Name`, and tasks `Name : [tags,] [id,] [start,] end-or-duration`. Tags `done` (grey), `active` (solid accent), `crit` (red outline; solid red when also active) and `milestone` (a diamond). The start is a date, `after id [id...]` (the latest end) or, when it is left out, the end of the task before (the first task needs a start); the end is a date, a duration (`30d`, `2w`, `6h`, `90m`, `30s`, `1M` = 30 days, `1y` = 365 days) or `until id` (the start of that task); an id must be written before it is used. A date that looks like YYYY-MM-DD but is not one is an error, and so is a task that ends before it starts. The axis is along the top with a tick every 1, 2, 5, 7, 14, 30, 60, 91, 182, 365 ... days (the smallest step that gives at most 11 ticks), the chart is 44 em wide whatever the span, sections are bands with their names at the left, the name of a task is inside its bar when it fits and right of it when not. `excludes`, `includes`, `todayMarker`, `tickInterval`, `weekday`, `inclusiveEndDates`, `topAxis` and `click` are read and ignored (weekends are not skipped).

**Items and color roles.** `ExDItem` (`include/exhaust_diagram.h`): a kind (the shapes `rect round stadium diamond ellipse hexagon parallelogram parallelogram_alt trapezoid trapezoid_alt cylinder subroutine flag double_circle`, `line`, `text`, `wedge`), a fill and a stroke role, a dash (solid, dashed, dotted), heads at the start and the end of a line (`arrow`, `open`, `cross`, `circle`, and for the later types `triangle`, `diamond`, `diamond_filled` and the cardinality marks `one`, `zero_one`, `many`, `one_many`, `zero_many`, `only_one`), a box or an anchor, a stroke width and a text size in em. The roles map to theme tokens: text `text`, muted text and lines `text_muted`, node fill `surface`, node outline and accent `accent`, panel `background_alt`, label background `background`, note fill `highlight`, error `error`, and `EXD_C_CAT + n` the n-th color of the categories (the callout colors in the order given under *Pie chart*), and `EXD_C_BEST` (text only) the text or the background color, whichever contrasts more with the fill role the item names.

## Templates and daily notes (6e-i)

*Since 0.1.0.* `include/exhaust_templates.h`, `src/notes/templates.c`; the commands are in `src/main.c`. **Variables** in a template (and in the template of a daily note): `{{title}}` (the name of the note it goes into, without `.md`), `{{date}}` and `{{time}}` (formatted with `[templates] date_format` and `time_format`), `{{date:FORMAT}}` and `{{time:FORMAT}}` (any Moment format, see *Dates in text*), and for the note composer (6e-ii) `{{content}}`, `{{fromTitle}}` and `{{newTitle}}`; spaces inside the braces are allowed; anything else, an unknown name included, stays as written; `{{` without `}}` on the same line stays. **Inserting a template** (`templates.insert-template`: a list of the notes in `[templates] folder`, in name order, then the choice): the template's properties are merged into the note's own by text surgery and its text is inserted where the cursor is, all as one undo step. *Merge:* the note's own lines are never rewritten; a property the note lacks is appended at the end of its properties as the template wrote it (several lines when it has them); a property both have keeps the note's value, except lists: the items the template has and the note lacks are added (a block list gets `- item` lines with the note's own indent after its last item, a flow list `[a, b]` gets `, item` before the `]`, an empty one takes the template's items in the template's form, a value that is a scalar is not a list and is kept), items are compared without their quotes; a note without properties gets a block `---` ... `---` at its start (after a byte order mark); the closing line may be `...`; Windows line breaks in the note are kept (the lines added use `\n`); a block that has no closing line is not properties. *Text:* the template without its properties block, inserted at the cursor, or at the start of the text after the properties when the cursor is inside them; the caret ends after it. `templates.insert-current-date` and `templates.insert-current-time` insert the date and the time (the formats of `[templates]`) at every cursor.

**Daily notes** (`[daily_notes]`): the note of a day is `<folder>/<date_format of the day>.md`; the format may hold `/` (`YYYY/MM/YYYY-MM-DD` makes folders) and any Moment tokens. A format that gives a name a note cannot have (`:`, `#`, `^`, `[`, `]`, `|`, a backslash, `..`, a name that starts or ends with `/`) makes no note and the status line says so. `daily-notes.open-today-s-daily-note` opens today's note, `open-next-daily-note` and `open-previous-daily-note` the day after and before the daily note that is open (when the open note is not one: tomorrow and yesterday); a note that is not there is made: the folders, then the file, created exclusively so a file that exists (also one that appeared a moment ago) is never replaced or touched, with the text of `template` (a note of the vault, `.md` added when it has no extension; a template that is missing gives an empty note) with its variables filled in: `{{date}}` and `{{title}}` are the day of the note, `{{time}}` the time it was made. A note counts as a daily note of a day when its path follows the folder and the format, the text reads back as a date with the format (`ex_date_parse_format`) and formats to the same text again. Days are calendar days in the system's time zone.

## Unique notes and the note composer (6e-ii)

*Since 0.1.0.* `include/exhaust_templates.h` (unique notes), `include/exhaust_compose.h`, `src/index/compose.c`, the job `QJ_MERGE` in `src/ui/service.c`, the commands in `src/main.c`. **Unique notes** (`[unique_note]`): `unique-note-creator.create-new-unique-note` makes a note named by `prefix_format` (Moment tokens, default `YYYYMMDDHHmm`; the name must be one a note can have: no `/`, `:`, `#`, `^`, `[`, `]`, `|`, a backslash) in `folder` (made when missing; `..` is refused) with the text of `template` (variables as in *Templates and daily notes*, `{{title}}` is the new name) and opens it; if a note of that name exists, `-2`, `-3` ... is added until one is free (a note is never replaced or touched; the creation is exclusive); `unique-note-creator.add-unique-internal-link` makes the note the same way and puts `[[name]]` at every cursor without opening it. **Extract** (`note-composer.extract-current-selection`, `extract-this-heading`): asks for the name of the new note (offered: the first line of the text, or the heading, without the characters a name cannot have, at most 60 characters), makes the note beside the source (never replacing one; a taken or invalid name changes nothing and the status line says so) and replaces the extracted text by what `[note_composer] text_after_extract` says: `link` (`[[name]]`), `embed` (`![[name]]`) or `none`. The new note's text is the extracted text as it is; with `[note_composer] template`, the template's text with `{{content}}` (the extracted text), `{{fromTitle}}` (the source), `{{newTitle}}` and the variables of Templates (a template without `{{content}}` gets the text after it). The section of a heading runs from the heading's line to the next heading of the same or a higher level (or the end), headings being `#` lines outside code fences and outside the properties, found from the cursor (the nearest heading above it); the new note holds the section without its heading line and the link replaces the whole section (with a line break after it when the section had one). Both are one undo step in the source. **Merge** (`note-composer.merge-current-file-with-another-file`): asks for the note to merge into (its name, or `Folder/Name`; one that matches more than one is refused), asks to confirm unless `confirm_merge` is off, saves the open notes, and then on the service thread: the properties of the merged note are merged into the other's (as a template's are), its text is added at the end after a blank line (or the template's text with `{{content}}` = its text without its properties, `{{fromTitle}}` and `{{newTitle}}` = the name of the note it goes into; the user's own text is never searched for variables), the index learns the new text, and every link to the merged note (wikilinks with an alias, `#heading` or `#^block`, embeds, Markdown links and links in properties and canvases; the parts after `#` are kept, a Markdown link's text is not changed) is made to point at the other note (`ex_retarget_links`: link-safe rename's rewriting, without treating the target as a moved note); only when that has gone well is the merged note deleted by `files.trash_mode` (its tabs close) and the other note opened. A note cannot be merged into itself.

## Slides (6e-iii)

*Since 0.1.0.* `include/exhaust_slides.h`, `src/markdown/slides.c`, the tab in `src/main.c`. The feature `[features] slides` is off by default (*Core plugins*); with it on, `slides.start-presentation` (*Slides: Start presentation*) opens the open note as slides in a tab of its own, next to the note's own tab (a second tab for the same note; closing it ends the presentation). **Splitting:** a note is cut at the lines that hold three or more hyphens and nothing else but spaces (up to three spaces of indent before them); the line before must be blank (or the separator is the first line), so `---` under a line of text is a heading's underline, not a separator, `--`, `***`, `- - -` and `--- text` are not separators and a line indented by four spaces is code. Not looked into: the properties at the start of the note (the first line `---` up to the next `---` or `...`), fenced code blocks (``` or ~~~, closed by a fence of the same character at least as long), `$$` math blocks and `<!-- -->` comments. The blank lines round a slide are cut away (the spaces of its first line stay), a slide with nothing in it is left out, a note with no separator is one slide, an empty note has none (the status line says what to do). **Showing:** one slide at a time, drawn like reading mode (callouts, math, diagrams, pictures, embeds and links work) with the text 1.8 times as large and the page as wide as that makes it; a slide taller than the pane scrolls with the wheel. Keys: Right, Space, Enter, Page Down: next; Left, Backspace, Page Up: back; Home and End: first and last; Escape ends the presentation (closes the tab). The status line shows `Slide n of m` and the keys. Nothing is written: the note is only read (the slides follow the text the note has when the presentation starts). Not built: a full-screen window, speaker notes, slide transitions, vertical slides, a slide overview.

## Screen reader interface (7h)

Each window of the program (the main window, every pop-out, the vault manager) has its own *accessibility tree* (see `exhaust_a11y.h`) and, through AccessKit, its own connection to the operating system's screen reader interface: AT-SPI on Linux (X11 and Wayland; AccessKit talks to the accessibility bus over D-Bus), UI Automation on Windows and NSAccessibility on macOS (both attached to the native window handle that SDL gives).

- *Costs nothing without a screen reader.* The tree is built only while a screen reader is attached (the system asked for the tree), and then at most four times a second. A program nobody reads does not build it.
- *Threads.* The operating system's calls arrive on its own threads. They never touch widgets: the first tree comes from the latest snapshot kept behind a lock, and actions go into a queue of 64 that the interface thread empties every pass.
- *Actions.* `Click` presses the button with that node id (the same handler as a mouse click; a hidden or disabled button, or a widget that is gone, does nothing); `Focus` gives a focusable widget the keyboard focus. Rows of lists and of the file tree are in the tree (the first 2000 of each) but are not pressed through the interface yet.
- *Names.* A node is named by its widget's accessible name (or its text); the focused widget is the one with the keyboard focus in that window; the window's node is the root.
- *Windows.* On Windows the adapter is attached before the window is shown (the windows are created hidden and shown after it). On Linux under X11 the window's place is given to the screen reader; under Wayland an application cannot know it.
- *Headless.* Under the dummy video driver the adapter is inert (no D-Bus, no operating system): the queue and the actions still work, which is what the test `a11y_adapters` uses. `EXHAUST_A11Y_ADAPTER=1` makes it real there (the AT-SPI test of CI does), `EXHAUST_NO_A11Y=1` turns it off.

## Pop-out windows (7g)

A window of the workspace model other than the main one is an operating-system window of its own. It has its own renderer and its own tab strip and panes (the panes of that workspace window; there is no ribbon, no sidebar, no status bar, no banner and no find bar in it); everything else is shared with the main window: the workspace, the notes and their buffers (one buffer for each note, so an edit in either window is at once in the other), the index, the settings and the theme.

- *Opening one.* A tab dragged out of the window (let go outside the panes) or *Open in new window* in a pane's menu moves that tab into a new window. The new window has the size of a new window (`[appearance] window_width` and `window_height`, 7f) and is placed a little off from the main window.
- *Closing one.* The window's close button, or closing its last tab, ends it: its tabs go to the active pane of the main window, and nothing is lost (each note's buffer stays open and is saved as always). Closing the main window ends the program and every other window with it.
- *Session.* Every pop-out is in `session.ini` as `[window.<n>]` with its `x`, `y`, `width` and `height` (screen units; kept up to date while it is moved or resized) and its own tree of panes (see *Layout tree*). At the next start the windows open again. A window that was on a screen that is not there any more opens on the primary screen instead (its middle must be on some screen).
- *Which window is active.* The pane last clicked in is the active pane of the workspace, in whichever window it is: commands (shortcuts, the palette) act on its note, the sidebar panels show that note, and the status bar describes it. A shortcut that is pressed in a pop-out works on the program as a whole.
- *Frames.* Every window is handled on the interface thread by the same loop: all waiting events, then one frame for each window that needs one. A pop-out costs about what a pane of the main window costs, and `ctest -R frame_budget` has a case with two windows.
- *What a pop-out does not have yet.* The pane menu (the three dots), dialogs and the page preview (they would open in the main window), the find bar, and dragging a tab from one window to another. Each is in `docs/ISSUES.md`.
- *Script.* The developer's script of `EXHAUST_SCRIPT` has `window N` (the next events and `shot` are for the N-th pop-out, 0 for the main window) and `closewindow N` (its close button); see `tests/popout_scripts.sh`.

## Shell: status bar, ribbon, zoom, high contrast and window size (7f)

**Status bar.** The line at the bottom of the window. On the left, the last message (a save that failed, a layout saved, a spelling hint) or, when there is none, nothing. On the right, from left to right, only the items that apply:

| Item | Shown when | Text |
| --- | --- | --- |
| Editing mode | the active tab is a note and `[editor] show_mode_in_status_bar` is on | `Live preview`, `Source mode` or `Reading view` |
| Save state | the active tab is a note | `Saved`; `Editing` while there are edits not yet on disk or a save is running; `Not saved` when the last save failed (the text is safe in the journal) |
| File watching | the system gave no change notices and Exhaust looks at the files by polling | `Checking for outside changes by polling` |
| Word count | the feature `word_count` is on and the active tab is a note | as before: `n words, m characters`, or `k of n words, m characters` for a selection |

Each item has an accessible name that is its text, and the bar is a single row that never wraps: when the window is narrower than the items, the left message is cut with an ellipsis first.

**Ribbon.** `[interface] show_ribbon = false` hides the whole ribbon (the commands stay in the palette and on their shortcuts). `[interface] ribbon_items` is a comma separated list of command ids; the ribbon shows the buttons of the commands in the list, in its own fixed order, and an empty list shows all of them. An id this version does not know is ignored and kept. The buttons (id, name, what it does):

| Id | Button | Place |
| --- | --- | --- |
| `quick-switcher.open-quick-switcher` | Quick switcher | top |
| `graph-view.open-graph-view` | Graph view | top (also needs the feature `graph`) |
| `canvas.create-new-canvas` | Canvas | top (also needs the feature `canvas`); a stub until 6a, a working button since then |
| `command-palette.open-command-palette` | Command palette | top |
| `app.toggle-live-preview-source-mode` | Live preview or source mode | bottom |
| `app.toggle-left-sidebar` | Show or hide the left sidebar | bottom |
| `app.toggle-right-sidebar` | Show or hide the right sidebar | bottom |
| `app.change-theme` | Switch between light and dark | bottom |
| `app.open-settings` | Settings | bottom |

The Interface page of the settings has a button *Choose ribbon items*: a list of the nine with `Shown` or `Hidden` before each; choosing one flips it and the list opens again, Cancel closes it. A list that shows all nine is stored as an empty value. The change applies at once.

**Zoom.** `[interface] zoom_percent` is the interface zoom, 50 to 400. The commands `app.zoom-in`, `app.zoom-out` and `app.reset-zoom` (default shortcuts `Ctrl+=` and `Ctrl++`, `Ctrl+-`, `Ctrl+0`; `Cmd` on macOS) move to the next or previous *level* and back to 100; the levels are 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300 and 400 percent. From a value between two levels (the slider on the Appearance page writes any whole number) zoom in goes to the next level above it and zoom out to the next level below it. Every change is written to `settings.ini` at once and applies at once, also when it is made on the settings page. (Before 7f the program read the keys `Ctrl+=` and `Ctrl+-` itself and stepped by ten percent; that is gone.)

**High contrast.** `[appearance] high_contrast`: `off`; `on`, which uses the built-in *Exhaust High Contrast Dark* or *Light* (the colour scheme picks which; see the Palette table) instead of the built-in dark or light theme; `system`, which is `on` when the operating system asks for high contrast (Windows: the High Contrast setting; Linux: GNOME's `org.gnome.desktop.a11y.interface high-contrast`; macOS: not detected in 0.1.0, so `system` is `off` there) and `off` otherwise. With high contrast on, the theme file named by `theme` and `accent_color` are **not applied** (they could lower the contrast) and the Theme page says so; the enabled snippets still apply. The change applies at once.

**Size of a window.** `window_width` and `window_height` (400 to 10000, in pixels at 100% zoom) are the size of a window that has no size of its own: the first window of a vault, and, from 7g, a new pop-out window. The Appearance page has a button *Use the size of this window* that writes the current size into both keys.

## Settings screen

*Since 0.1.0 (7a).* The command `app.open-settings` (`Ctrl+,`) opens the settings screen in a tab (view `settings`; at most one). On the left a search box and the list of pages (the *Options* group: General, Appearance, Interface, Editor, Files and links, Keychain; the pages of 7b to 7f are added to the list as they are built); on the right the page, in cards of rows: a name, a line of explanation under it and the control at the right. The page scrolls with the mouse wheel, `PageUp`/`PageDown`, and when the focus moves to a control that is out of view.

- *Which keys.* One page for each section of the `settings.ini` tables above: General is `[general]`, Appearance `[appearance]`, Interface `[interface]`, Editor `[editor]`, Files and links `[files]`. Every key of those sections has a row on its page (a stub key has a disabled row that says it is not in this version); the test `settings_pages` walks the generated table of keys and fails for a key without a row.
- *Controls.* `bool` is an On/Off switch; an `enum` is a button showing the choice that opens a menu of the choices; an `int` or `percent` is a slider with its value shown; `string`, `path`, `list` and `color` are one-line text fields. A change is written at once (write-through, as always) and the program applies it without a restart wherever it already reads the key each frame; a text field is written when you press Enter or leave the field, and a value the key refuses (a color that is not `#rrggbb`) is put back.
- *Search.* Typing in the search box shows the rows of every page whose name or explanation contains the words (case-insensitive, every word must match), each under the name of its page; the box emptied shows the page again.
- *Not on a page.* Nothing in these sections is left off; `config_folder` and `allow_uri_callbacks` carry a note that they take effect when the vault is opened again.
- *Core plugins (7b).* The list *Core plugins* is a page of the *Options* group: one switch for every key of `[features]` (a stub feature, Sync and Publish, has a disabled switch that says it is not in this version), in groups. Below it, in the group *Core plugins* of the page list, one page for each feature that has options of its own: Backlinks (`[backlinks]`), Canvas (`[canvas]`), Command palette (`[command_palette]`), Daily notes (`[daily_notes]`), File recovery (`[file_recovery]`), Note composer (`[note_composer]`), Page preview (`[page_preview]`), Quick switcher (`[quick_switcher]`), Templates (`[templates]`), Unique note creator (`[unique_note]`) and Sync (a stub page). A switch of the list goes through the feature registry, so the feature's menus, panels and commands follow it at once. The test `feature_pages` walks these sections of the table.
- *File recovery page (7e).* Below its two numbers the page has a button row, *Clear history*, which asks for a confirmation and then deletes every automatic snapshot (see *File recovery snapshots*). A row with a button (`EX_ROW_ACTION`) has no setting; the application does what its id says.
- *Hotkeys (7c).* The page *Hotkeys* (in the *Options* group, after Files and links) lists every command of the Commands table in table order: its name, its shortcuts as chips (a chip with a cross removes that shortcut; a chip in red has a shortcut another command has too), a `+` button that waits for the next key press and adds it (Escape cancels; a modifier alone is waited out) and, for a command that differs from its defaults, a *Reset* button. A filter field above the list narrows it by name, id or shortcut, and *Customized only* shows just the commands with a line in `hotkeys.ini`. Only the rows near the view are built (the list has 200 rows). The test `hotkeys` covers the file and chord rules, `hotkeys_screen` the page.
- *Theme (7d).* The page *Theme* of the *Options* group, after Appearance: see "Loading themes and snippets" above.
- *Names.* Each control's accessible name is its row's name followed by its state (`Spell check: on`).

## Table editing

The 15 `table.*` commands (and `editor.insert-table`) of the Commands table work on the Markdown table the primary caret is in. Since: 0.1.0. They are *available* (not greyed in the palette or the slash menu) only where they apply, and each is one text edit, so one undo step.

- *What is a table.* A header row, then a delimiter row with the same number of cells (each cell dashes with an optional colon at either end), then the rows after it, up to a blank line, a line without an unescaped `|`, or a line that starts another block (a quote `>`, a code fence, a heading). The outer pipes are optional; `\|` is a pipe inside a cell. A table inside a fenced code block, or in a quote or a list item, is not edited (the quote and list cases are ISSUES 45).
- *Where the caret is.* The column is the cell the caret is in (a caret on a pipe belongs to the cell on its left); the row is the line. The header and the delimiter row are not body rows.
- *Availability.* All commands need a caret in a table. `table.add-row-above`, `table.delete-row` and `table.duplicate-row` need a body row; `table.move-row-up` a body row that is not the first, `table.move-row-down` one that is not the last; `table.add-row-below` works from any row (from the header or the delimiter row it adds the first body row); `table.move-column-left` and `-right` need a column on that side; `table.delete-column` needs more than one column; the `table.align-*` commands need a delimiter cell in the caret's column.
- *What changes.* Only the table's lines. The column commands (add, delete, duplicate, move) write the whole table again with every column as wide as its widest cell (at least 3), the outer pipes added, and cells aligned by their column's alignment; a cell's text is copied unchanged. The row commands add, copy, swap or remove whole lines; a new row is written with the column widths, a copied or swapped row keeps its text exactly, and the other lines are not touched. The align commands change only the caret's delimiter cell (`:---`, `:-:`, `---:`; its width is kept, at least 3), and do nothing (no undo step) when it already has that alignment. A row with more cells than the header keeps them: the table is given that many columns, so no cell's text is lost. Line breaks (CRLF or LF) and the text before and after the table are kept. A width is counted in code points, so wide characters can look misaligned.
- *The caret* goes to the start of the cell the command is about (the new, copied or moved cell or row; the same column of the next row after a delete).

## Slash commands

The slash menu opens when you type `/` at the start of a line or after a blank space. It lists every command registered with the `editor` flag, in command palette order, matched with the same fuzzy matching as the command palette (`scf` finds *Save current file*). `Esc` or `Space` closes it. The list itself is therefore the set of editor commands in the command registry; it is not a separate list.

The Obsidian Help pages do not list the default slash commands, so Exhaust defines its own list: the commands in the Commands table marked *Editor: yes*. They are, in table order:

- `editor.add-cursor-above`: Add cursor above
- `editor.add-cursor-below`: Add cursor below
- `editor.add-embed`: Add embed
- `editor.add-internal-link`: Add internal link
- `editor.add-tag`: Add tag
- `bases.insert-new-base`: Bases: Insert new base
- `editor.clear-formatting`: Clear formatting
- `editor.cycle-bullet-checkbox`: Cycle bullet/checkbox
- `editor.delete-paragraph`: Delete paragraph
- `editor.fold-all-headings-and-lists`: Fold all headings and lists
- `editor.fold-less`: Fold less
- `editor.fold-more`: Fold more
- `editor.indent-list-item`: Indent list item
- `editor.insert-attachment`: Insert attachment
- `editor.insert-callout`: Insert callout
- `editor.insert-code-block`: Insert code block
- `editor.insert-footnote`: Insert footnote
- `editor.insert-horizontal-rule`: Insert horizontal rule
- `editor.insert-markdown-link`: Insert Markdown link
- `editor.insert-math-block`: Insert math block
- `editor.insert-table`: Insert table
- `editor.move-line-down`: Move line down
- `editor.move-line-up`: Move line up
- `note-composer.extract-current-selection`: Note composer: Extract current selection...
- `note-composer.extract-this-heading`: Note composer: Extract this heading...
- `editor.remove-heading`: Remove heading
- `editor.rename-this-heading`: Rename this heading...
- `editor.set-as-heading-1`: Set as heading 1
- `editor.set-as-heading-2`: Set as heading 2
- `editor.set-as-heading-3`: Set as heading 3
- `editor.set-as-heading-4`: Set as heading 4
- `editor.set-as-heading-5`: Set as heading 5
- `editor.set-as-heading-6`: Set as heading 6
- `editor.spelling-add-word-to-dictionary`: Spelling: Add word to dictionary
- `editor.spelling-replace-with-suggestion`: Spelling: Replace with the next suggestion
- `table.add-column-to-the-left`: Table: Add column to the left
- `table.add-column-to-the-right`: Table: Add column to the right
- `table.add-row-above`: Table: Add row above
- `table.add-row-below`: Table: Add row below
- `table.align-center`: Table: Align center
- `table.align-left`: Table: Align left
- `table.align-right`: Table: Align right
- `table.delete-column`: Table: Delete column
- `table.delete-row`: Table: Delete row
- `table.duplicate-column`: Table: Duplicate column
- `table.duplicate-row`: Table: Duplicate row
- `table.move-column-left`: Table: Move column left
- `table.move-column-right`: Table: Move column right
- `table.move-row-down`: Table: Move row down
- `table.move-row-up`: Table: Move row up
- `templates.insert-current-date`: Templates: Insert current date
- `templates.insert-current-time`: Templates: Insert current time
- `templates.insert-template`: Templates: Insert template
- `editor.toggle-blockquote`: Toggle blockquote
- `editor.toggle-blue-highlight`: Toggle blue highlight
- `editor.toggle-bold`: Toggle bold
- `editor.toggle-bullet-list`: Toggle bullet list
- `editor.toggle-checkbox-status`: Toggle checkbox status
- `editor.toggle-code`: Toggle code
- `editor.toggle-comment`: Toggle comment
- `editor.toggle-fold-on-the-current-line`: Toggle fold on the current line
- `editor.toggle-green-highlight`: Toggle green highlight
- `editor.toggle-heading`: Toggle heading
- `editor.toggle-highlight`: Toggle highlight
- `editor.toggle-inline-math`: Toggle inline math
- `editor.toggle-italic`: Toggle italic
- `editor.toggle-numbered-list`: Toggle numbered list
- `editor.toggle-orange-highlight`: Toggle orange highlight
- `editor.toggle-purple-highlight`: Toggle purple highlight
- `editor.toggle-red-highlight`: Toggle red highlight
- `editor.toggle-strikethrough`: Toggle strikethrough
- `editor.toggle-yellow-highlight`: Toggle yellow highlight
- `editor.unfold-all-headings-and-lists`: Unfold all headings and lists
- `editor.unindent-list-item`: Unindent list item
- `unique-note-creator.add-unique-internal-link`: Unique note creator: Add unique internal link

75 commands.


## Default editing shortcuts

Copied as facts (key combinations) from the Obsidian Help page "Editing shortcuts". Exhaust's text editing must support these defaults. Customizable command shortcuts are in `hotkeys.ini`.

### Windows and Linux shortcuts

#### Common actions

| Action | Shortcut |
|-|-|
| Copy | `Ctrl+C` |
| Cut | `Ctrl+X` |
| Paste | `Ctrl+V` |
| Paste without formatting | `Ctrl+Shift+V` |
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Shift+Z` or `Ctrl+Y` |
| Copy paragraph | `Ctrl+C` (with no selected text) |
| Cut paragraph | `Ctrl+X` (with no selected text) |

#### Text editing

| Action | Shortcut |
|-|-|
| Insert new line| `Enter` |
| Delete the previous character | `Backspace` |
| Delete the next character | `Delete` |
| Delete the previous word | `Ctrl+Backspace` |
| Delete the next word | `Ctrl+Delete` |
| Delete the current line | `Ctrl+Shift+K` (with no selected text) |

#### Text navigation

| Action | Shortcut |
|-|-|
| Move the cursor one character | `Left/→` |
| Move the cursor to the beginning of the previous word | `Ctrl+←` |
| Move the cursor to the end of the next word | `Ctrl+→` |
| Move the cursor to the beginning of the current line | `Home` |
| Move the cursor to the end of the current line | `End` |
| Move the cursor to the previous line | `↑` |
| Move the cursor to the next line | `↓` |
| Move the cursor to the beginning of the note | `Ctrl+Home` |
| Move the cursor to the end of the note | `Ctrl+End` |
| Move the cursor up one page | `Page up` |
| Move the cursor down one page | `Page down` |

#### Text selection

| Action | Shortcut |
|-|-|
| Simplify selection | `Escape` |
| Select all | `Ctrl+A` |
| Extend selection one character | `Shift+Left/→` |
| Extend selection to the beginning of the previous word | `Ctrl+Shift+←` |
| Extend selection to the end of the next word | `Ctrl+Shift+→` |
| Extend selection to the beginning of the current line | `Shift+Home` |
| Extend selection to the end of the current line | `Shift+End` |
| Extend selection to the beginning of the note | `Ctrl+Shift+Home` |
| Extend selection to the end of the note | `Ctrl+Shift+End` |
| Extend selection one page up | `Shift+Page up` |
| Extend selection one page down | `Shift+Page down` |

### macOS shortcuts

#### Common actions

| Action | Shortcut |
|-|-|
| Copy | `Cmd+C` |
| Cut | `Cmd+X` |
| Paste | `Cmd+V` |
| Paste without formatting | `Cmd+Shift+V` |
| Undo | `Cmd+Z` |
| Redo | `Cmd+Shift+Z` |
| Copy paragraph | `Cmd+C` (with no selected text) |
| Cut paragraph | `Cmd+X` (with no selected text) |

#### Text formatting

| Action | Shortcut |
| ---- | ---- |
| Bold text | `Cmd+B` |
| Italic text | `Cmd+I` |

#### Text editing

| Action | Shortcut |
|-|-|
| Insert new line| `Enter` |
| Delete the previous character | `Backspace` |
| Delete the next character | `Delete` |
| Delete the previous word | `Option+Backspace` |
| Delete the next word | `Option+Delete` |
| Delete to the beginning of the current line | `Cmd+Backspace` |
| Delete to the end of the current line | `Cmd+Delete` |
| Delete the current line | `Cmd+Shift+K` (with no selected text) |

#### Text navigation

| Action | Shortcut |
|-|-|
| Move the cursor one character | `Left/→` |
| Move the cursor to the beginning of the previous word | `Option+←` |
| Move the cursor to the end of the next word | `Option+→` |
| Move the cursor to the beginning of the current line | `Cmd+←` |
| Move the cursor to the end of the current line | `Cmd+→` |
| Move the cursor to the previous line | `↑` |
| Move the cursor to the next line | `↓` |
| Move the cursor to the beginning of the note | `Cmd+↑` |
| Move the cursor to the end of the note | `Cmd+↓` |
| Move the cursor up one page | `Fn+↑` |
| Move the cursor down one page | `Fn+↓` |

#### Text selection

| Action                                                 | Shortcut                   |
| ------------------------------------------------------ | -------------------------- |
| Simplify selection                                     | `Escape`                   |
| Select all                                             | `Cmd+A`                    |
| Extend selection one character                         | `Shift+Left/→`   |
| Extend selection to the beginning of the previous word | `Option+Shift+←`  |
| Extend selection to the end of the next word           | `Option+Shift+→` |
| Extend selection to the beginning of the current line  | `Cmd+Shift+←`              |
| Extend selection to the end of the current line        | `Cmd+Shift+→`    |
| Extend selection to the beginning of the note          | `Cmd+Shift+↑`       |
| Extend selection to the end of the note                | `Cmd+Shift+↓`     |
| Extend selection one page up                           | `Ctrl+Shift+↑`      |
| Extend selection one page down                         | `Ctrl+Shift+↓`    |

## Markdown tokens and the HTML subset

*Since 0.1.0 (sub-slice 3a).* The tokenizer (`include/exhaust_markdown.h`) gives every construct of a note a *span*: a kind, a byte range in the note's text, a level or flags and the span it sits inside. It never changes the text. The kinds are named here because the editor, reading mode, the index, Lua and style snippets refer to them.

**Block kinds:** `frontmatter` (a `---` block on the first line, ended by `---` or `...`), `heading` (level 1 to 6; ATX `#` and setext underlines), `paragraph`, `blockquote`, `callout`, `list` and `list_item` (level 1 for a bullet, 2 for an ordered item), `task` (the `[ ]` of a task: flags 0 open, 1 done, 2 any other character), `code_block` (flags 0 fenced, 1 indented; `code_info` is the text after the opening fence, `code_body` the code), `math_block` (`$$ ... $$`), `table`, `table_row` (flags 1 header, 2 delimiter row), `table_cell` (level is the column, flags the alignment: 0 none, 1 left, 2 center, 3 right), `hr`, `footnote_def`, `comment_block` (`%% ... %%` over several lines) and `html_block`.

**Inline kinds:** `emphasis` (`*a*`, `_a_`), `strong` (`**a**`), `strike` (`~~a~~`), `highlight` (`==a==`), `code_span`, `math_inline` (`$a$`, `$$a$$`), `link` and `image` (with `link_text` and `link_dest` and `link_title`), `wikilink` and `embed` (`![[...]]`; with `link_target`, `link_heading`, `link_block` and `link_alias`), `autolink`, `footnote_ref` (`[^id]`), `inline_footnote` (`^[text]`), `tag` (`#tag`, `#nested/tag`), `block_id` (`^id` at the end of a line), `comment_inline` (`%%text%%`), `html_inline`, `escape` (`\*`) and `hard_break`. Every construct also has `marker` spans for its syntax characters (the `#` of a heading, the `**` of strong text, the brackets of a link): live preview hides markers away from the cursor.

**Rules that decide the shape.** A tag starts after the start of a line, a space or an opening bracket, may contain letters, digits, `_`, `-` and `/`, and must contain a character that is not a digit. A block ID is `^` followed by letters, digits and `-`, after a space or at the start of a line, at the end of the line. Inline math opens with a `$` that is not followed by a space and closes with a `$` that is not preceded by a space and not followed by a digit. A list item may start a list only when it is not empty or, for an ordered list interrupting a paragraph, starts at 1. A table needs a delimiter row with as many columns as the header. Links and wikilinks do not cross a blank line; a wikilink does not cross a line break.

**Callouts.** A block quote whose first line is `[!type]`, optionally followed by `+` (open) or `-` (closed) and a title, is a callout. The type is not case-sensitive. The canonical types are `note`, `abstract` (aliases `summary`, `tldr`), `info`, `todo`, `tip` (`hint`, `important`), `success` (`check`, `done`), `question` (`help`, `faq`), `warning` (`caution`, `attention`), `failure` (`fail`, `missing`), `danger` (`error`), `bug`, `example` and `quote` (`cite`). The callout span's level is the canonical type (an unknown type is `note`) and its flags are the fold state (0 none, 1 open, 2 closed); `callout_type` and `callout_title` spans hold the text, so a custom callout defined by a style snippet can be matched by its own name. Callouts nest.

**HTML subset (`include/exhaust_html.h`).** HTML in a note is parsed by Exhaust itself, never rendered by a browser, and only this subset is kept:
- *Tags:* `a`, `abbr`, `b`, `blockquote`, `br`, `caption`, `code`, `del`, `details`, `div`, `em`, `h1` to `h6`, `hr`, `i`, `img`, `ins`, `kbd`, `li`, `mark`, `ol`, `p`, `pre`, `q`, `s`, `samp`, `small`, `span`, `strong`, `sub`, `summary`, `sup`, `table`, `tbody`, `td`, `tfoot`, `th`, `thead`, `tr`, `u`, `ul`, `var`.
- *Attributes:* `href` and `src` (only `http`, `https`, `mailto`, a relative path or a `#` fragment; never `javascript:` or `data:`), `alt`, `title`, `width` and `height` (whole numbers), `colspan` and `rowspan` (whole numbers), `align` (`left`, `center`, `right`), `open` (on `details`) and `id`. Every other attribute, including `style`, `class` and the `on...` handlers, is dropped.
- *Everything else* (`iframe`, `object`, `embed`, `form`, `font`, unknown tags and so on) loses its tags and keeps its text; `script` and `style` lose their content as well. Comments are dropped. Character references (`&amp;`, `&lt;`, `&#65;`, `&#x41;` and the common named ones) are decoded. Nesting deeper than 64 levels is flattened. A tag that is not closed is closed at the end of its parent. Cleaning is stable: cleaning the cleaned text changes nothing.

**Live preview and source mode (sub-slice 3b).** The editor has three display modes: *plain* (one font; used for fields and diffs), *source* (the Markdown unchanged, its syntax in a quieter color, headings, code and links styled) and *live preview*. A tab's `mode` in the session file is `live`, `source` or `reading` (sub-slice 3c), the setting `[editor] default_mode` (`live_preview` or `source`) decides the editing mode of new tabs and `[editor] default_view` (`editing` or `reading`) whether a new tab opens editing or reading. In live preview the `marker` spans of a construct are hidden unless a cursor or selection touches the construct (its closed range, so the edge counts); for block quotes, callouts and list items the line the cursor is on is what counts. Stand-ins are drawn in place of syntax: a bullet for `-`, `*` and `+` (ordered numbers stay), a box for `[ ]` and `[x]`, a bar for each `>`, ` > ` for the `#` of `[[Page#Heading]]`; `[[Page|alias]]` shows the alias, `[text](url)` shows the text, a callout with a title shows the title instead of `[!type]`, and a fence shows only its language. Math, embeds, images, tables, comments, HTML and front matter stay as source until their renderers exist. The text is never changed by the mode.

**Reading mode and folding (sub-slice 3c).** Reading mode shows the note as formatted pages and cannot edit; it is the `reading` tab mode, switched by the command `app.toggle-reading-view` (`Ctrl+E`) and back to the editing mode the tab had before (the setting's default if it had none). It is built from the same text with md4c plus Exhaust's own handling of wikilinks, embeds, tags, highlights, comments, callouts, footnotes and math; the text in a reading view is the note as plain text for a screen reader. Clicking an external link (`http`, `https`, `mailto`) opens the system's handler (never an embedded browser); an internal link opens the note when its name matches (full link resolution is sub-slice 4a); a callout marked `+` or `-` can be folded by clicking its title (a view state that is not saved). Folding (editor, source mode and live preview): a heading folds the lines up to the next heading of the same or a higher level (only headings at the top of the note), a list item folds its continuation lines and nested items; `[editor] fold_heading` and `fold_indent` switch the two kinds, and turning a kind off shows its folded regions again. A triangle in the left margin of a header line folds or opens it (open ones show on the caret's line, folded ones always, with `...` after the header). Commands: `editor.fold-all-headings-and-lists`, `editor.unfold-all-headings-and-lists`, `editor.toggle-fold-on-the-current-line` (the header on the line, or else the innermost region around it), `editor.fold-more` (folds the deepest level of regions that are shown and still open) and `editor.fold-less` (opens the shallowest level of folded regions). Folds are a view and never change the text: a caret that moves into hidden text opens the fold (Up, Down, Left and Right skip hidden lines), folded regions follow the edits made above them, and a region that no longer exists (its header was deleted or changed) is shown again. They are not saved in the session file (a later minor version may add that as a new key).

**Editing (sub-slice 3d).** *Keys.* The editor reads key presses through one function, `ex_edit_action_for(key, modifiers, mac)`, which maps every default shortcut of "Default editing shortcuts" below to an action; a test reads the tables of this file, so a row added there needs an expectation in the test. On macOS Cmd is the primary modifier and Option moves by words, Cmd+Left and Cmd+Right go to the ends of the line, Cmd+Up and Cmd+Down to the ends of the note, Cmd+Backspace and Cmd+Delete delete to the start and end of the line, and Ctrl+Shift+Up and Ctrl+Shift+Down extend the selection by a page; Page Up and Page Down are the Fn+arrow keys. Letter shortcuts follow the key's label on Latin layouts (QWERTZ, AZERTY, Dvorak) and the key's position on a US keyboard on layouts with other letters (Cyrillic, Greek, Hebrew, Arabic). Copy and cut with nothing selected act on the whole line (a paragraph is a line of the note); *Delete the current line* (Ctrl+Shift+K or Cmd+Shift+K) works with nothing selected. *Rectangular selection.* Dragging with Alt and Shift held makes one selection on every line between the line where the drag started and the line it is on, each covering the same horizontal span; a line that is shorter gets a caret (or a shorter selection) at its end; a drag that does not move sideways makes a column of carets. *Find and replace.* `app.search-current-file` (`Ctrl+F`) and `app.search-and-replace-in-current-file` (`Ctrl+H`) open the find bar for the note being edited, with the selected text (one line) as the pattern. Options: match case (off by default, Unicode simple case folding, no accent folding), whole word (a match must not touch a letter, digit or underscore), regular expression (PCRE2 syntax, UTF-8 and Unicode properties, `^` and `$` match at every line, a line break is `\n`, `\r\n` or `\r`). In plain mode the pattern is literal. In a replacement of a regular expression `$0` to `$9` and `${n}` are groups, `$$` is a dollar sign and `\n`, `\t`, `\r`, `\\` and `\$` are escapes; in plain mode the replacement is literal. A pattern that needs more than two million match steps stops with a message instead of hanging. Replace all is one undo step and leaves matches whose replacement is the same text alone. *Text formatting commands* (all are one undo step and act on every cursor): the inline toggles (`bold` `**`, `italic` `*`, `strikethrough` `~~`, `highlight` `==`, `code` `` ` ``, `inline math` `$`, `comment` `%%`) wrap the selection or put a pair of markers around the caret, and take them off if they are already there (also when the selection includes them); `toggle-heading` cycles none, 1 to 6 and none, `set-as-heading-N` sets level N (again on level N removes it) and `remove-heading` removes it; the list, quote and checkbox commands act on every line the selections touch (blank lines are skipped by the list commands): *Toggle bullet list* and *Toggle numbered list* switch off when every line already has it, *Toggle checkbox status* flips `[ ]` and `[x]` and makes plain text and bullets into open tasks, *Cycle bullet/checkbox* goes plain, bullet, task, plain; *Indent list item* and *Unindent list item* use `[editor] indent_with_tabs` and `indent_width` and only act on list items; *Move line up/down* moves the lines the selections touch as one block; *Delete paragraph* deletes the run of non-blank lines around the caret and the blank line after it; the insert commands add a horizontal rule, a fenced code block, a callout (the lines become its text), a `$$` math block, a three-cell table, a footnote (`[^n]` at the caret and its definition at the end of the note), a Markdown link, an internal link, an embed or a tag, around the selection where that makes sense; *Clear formatting* removes the markers of bold, italic, strikethrough, highlight, code and comments and the heading, quote and list marker of the lines. Commands for features that are not built yet (tables, templates, the note composer, bases, colored highlights, unique notes, properties) stay unbound, so the slash menu lists them disabled. *Slash menu.* `[features] slash_commands` (off by default) turns it on; it opens when `/` is typed at the start of a line or after a space or tab, lists the commands marked *Editor: yes* in table order, narrowed by fuzzy matching of the text after the slash against the command name (all the letters in order; word starts, runs of letters and the start of the name rank higher), closes with Escape, a space, a line break, the caret leaving the typed text or a click elsewhere, and runs the selected command with Enter, Tab or a click after removing the `/` and the typed text.

**Spell check and word count (sub-slice 3e).** *Spell check* is `[editor] spellcheck` (on by default; the command `app.toggle-spellcheck` flips it) and `[editor] spellcheck_languages` (0.1.0 knows `en_US` only). The checker is Exhaust's own and reads Hunspell dictionaries (`.aff` and `.dic`): `SET UTF-8`, `FLAG` in its default form (one character), `PFX` and `SFX` (strip, add, a condition of characters, `.`, `[...]` and `[^...]`, the cross-product switch), `REP` (with `^` and `$` anchors and `_` for a space), `KEY`, `TRY`, `ICONV`, `NOSUGGEST` and `ONLYINCOMPOUND`; everything else, compounding included, is ignored, and a dictionary with another `FLAG` form or character set is refused with a message. A word is right when it is a stem, or a stem with a suffix, a prefix or both (both only when the groups allow crossing); a stem in lower case also accepts a capital first letter and all capitals, a stem with a capital accepts capitals; words with digits, single letters and runs over 60 bytes are never flagged. English (US) is built in (`third_party/dictionaries/en_US`, SCOWL, a permissive license). Unknown words get a wavy line in the error color in source mode and live preview, except in code, math, front matter, HTML, comments, tags, the address and target of links, embeds and the word the caret is in. Suggestions are tried in this order: a `REP` pair, two letters swapped, a neighbouring key from `KEY` (a qwerty keyboard when the file has none), a letter too many, a wrong letter (from `TRY`), a letter missing, and a space that makes two words. *Spelling: Replace with the next suggestion* (`editor.spelling-replace-with-suggestion`) replaces the word at the caret with the first suggestion and, run again, with the next ones, and the status line lists them; *Spelling: Add word to dictionary* (`editor.spelling-add-word-to-dictionary`) adds it to the vault's own list, `<vault>/.exhaust/dictionaries/custom.txt` (UTF-8, one word on each line, created when the first word is added, read at start; a word is accepted as written and in lower case). *Word count* is the feature `word_count` (on by default) and shows `N words, M characters` at the right of the status line for the note being edited, and `X of N words, M characters` with a selection. A word is a run of letters, digits and combining marks, with apostrophes (`'` and `’`) and hyphens inside it (`don't`, `well-known`), and each Han, Hiragana and Katakana character is a word of its own; underscores and other punctuation separate words. Front matter, comments, the syntax characters of Markdown (the `**` of bold text, the `#` of a heading, the brackets and bar of a link), a link's address, a code fence's language, images, embeds and HTML tags are not counted; link text, wikilink targets and aliases, tags and code are. Characters are Unicode characters of the same text without line breaks.

## Font cache

*Since 0.1.0. A cache, not user data: it may be deleted at any time and is rebuilt by scanning the OS font folders.* `fonts.cache` in the OS cache folder (`<cache>/Exhaust/`, see the cache folder table of the platform layer: `$XDG_CACHE_HOME` or `~/.cache` on Linux, `%LOCALAPPDATA%\Exhaust\Cache` on Windows, `~/Library/Caches` on macOS). It is not stored in the vault.

Plain text, written atomically. Line 1 is `EXHAUST-FONTS 1` (the format version; a different version is ignored and rescanned). Line 2 is `sig <16 hex digits>`: a hash of the path and modification time of every folder visited under the OS font folders. If the signature of a new walk differs, the cache is ignored. Then one line per font face, with fields separated by tabs: file path, face index, flags (1 bold, 2 italic, 4 monospaced, 8 color), family name, style name, and the coverage as 384 hexadecimal digits (192 bytes: one bit for each page of 128 code points below U+30000; a set bit means the face has at least one glyph in that page). Files with tabs or line breaks in a path or family name are left out.

## vaults.ini

*Since 0.1.0.* The list of vaults Exhaust remembers. One file for the user, in the OS config folder, not in any vault: `<config>/Exhaust/vaults.ini` with `<config>` = `$XDG_CONFIG_HOME` or `~/.config` on Linux, `%APPDATA%` on Windows, `~/Library/Application Support` on macOS. Plain text, INI style, written atomically. Removing a vault from the list never touches its files.

```
[vaults]
last = 3f2a...          # id of the vault opened last (empty: none)

[vault.3f2a...]
path = /home/me/Notes   # canonical absolute path (symbolic links resolved), UTF-8
name = Notes            # the name shown in the vault list; defaults to the folder's name
opened = 1731400000     # when it was last opened, seconds since the Unix epoch
```

The section name carries the vault id: the first 32 hexadecimal digits of the SHA-256 of the canonical absolute path (see "Journal and session state"). When a vault is moved on disk and re-added, it gets a new id and a new section. Unknown keys and sections are kept and written back unchanged.

## Index

*Since 0.1.0 (sub-slice 4a-i).* The index is a SQLite database (`include/exhaust_index.h`) at `<cache>/vaults/<vault-id>/index.db`, where `<cache>` is the OS cache folder of "Where they live" below and `<vault-id>` the vault's id. It is never inside the vault. It is a cache: Settings has a *Rebuild vault cache* button that empties it and reads every note again, and a file that cannot be opened, is damaged or has a newer `user_version` than the program knows is deleted and made again. The database is in WAL mode: one writer connection (the indexer thread) and one read-only connection for each other thread that needs it.

**Schema.** `PRAGMA user_version` holds the schema version. A migration only adds tables, indexes and nullable or defaulted columns; no column is renamed, dropped or retyped. A migration that adds something filled from the notes also sets `files.indexed` to 0, so the next sync reads every note again. Notes are read as editing text: without the byte-order mark and, for files with CRLF throughout, with LF, so every offset in the index is an offset into what the editor holds.

| Version | Tables added |
| --- | --- |
| 1 (0.1.0, 4a-i) | `files`, `fts`, `lines`, `sections`, `blocks`, `tasks` |
| 2 (0.1.0, 4a-ii) | `names`, `links` |
| 3 (0.1.0, 4a-iii) | `tags`, `props` |
| 4 (0.1.0, 4d) | column `files.ctime` (creation time, default 0), for sorting by created |

- `files(id, path, kind, size, mtime, hash, indexed)`: every file of the vault that is not hidden (no name starting with a dot) and not excluded. `path` is relative to the vault with `/` separators; `kind` is the `ExFileKind` of the file name (1 Markdown note); `size` and `mtime` (nanoseconds) are what the sync compares; `hash` is the first 16 bytes of the SHA-256 of a note's text (NULL for other files and for a note that is not valid UTF-8); `indexed` is 0 until the note has been read.
- `fts`: an FTS5 table with the `trigram` tokenizer and `detail=none`, contentless; its rowid is `files.id` and its one column the text of the note. It serves only to find the notes that can hold a text: the search takes the three-character pieces of the query and asks for the notes that contain all of them, then checks those notes themselves (so a phrase, a regular expression or `line:` is always decided on the real text). A query under three characters scans every note.
- `lines(file_id, count, offsets)`: for each note, the number of lines and the start of every line as a list of variable-length integers (the difference to the line before).
- `sections(file_id, ord, level, title, start, end_, section_end, line)`: the headings of a note; `section_end` is where the next heading of the same or a higher level starts (or the end of the note).
- `blocks(file_id, block_id, start, end_, line)`: the paragraph, list item or quote that ends in a `^block-id`.
- `tasks(file_id, start, end_, line, state, text)`: a task line (`- [ ]`); state 0 open, 1 done, 2 any other character.

- `names(file_id, kind, key, text)`: what a link can call a file. Kind 0 is the file name (a note without `.md`, any other file with its extension), kind 1 the path from the vault (the same rule), kind 2 an alias (the `aliases` property of a note, a list or a text separated by commas). `key` is the name key; `text` the name as written.
- `links(file_id, ord, kind, target, target_key, name_key, heading, block, alias, start, end_, line)`: every link of a note in the order of the note: wikilinks (kind 0), embeds (1), Markdown links (2), Markdown images (3) and wikilinks inside properties (4). `target` is as written (decoded for Markdown links, `./` removed), without `#heading`, `#^block` and alias; it is empty for a link inside the same note. Web links and other links with a scheme are not stored. `name_key` is the key of the last part of `target` without `.md`: backlinks look for the links whose `name_key` is one of a note's names and then resolve each one.
- `tags(file_id, ord, start, tag, tag_key, line, in_props)`: a tag in the text (`#tag`) or in the `tags` property; `tag` without the `#`, `tag_key` its name key.
- `props(file_id, ord, name, name_key, type, value, start, end_, line)`: the properties of the frontmatter, in order; `type` is 0 text, 1 list, 2 number, 3 checkbox, 4 date, 5 datetime, 6 links; `value` is the text of a scalar or the items of a list joined by a line break; `start` and `end_` are the bytes of the lines the property occupies. A frontmatter that is not valid YAML (or that uses a flow mapping at the top, a key that is not at the start of a line, or a key twice) has no properties.

**Links resolve** by name key. `[[Note]]` finds the file whose name key is that of `Note` (a file name without `.md`, or with the extension for other files), then a note with that alias; `[[Folder/Note]]` finds a path that equals or ends with it; if several fit, the one in the folder of the note that holds the link wins, then the shorter path, then the path that sorts first. A Markdown link `[t](a/b.md)` is first tried as a path from its own note's folder (also `../`), then from the vault.

The trigram tables (`fts_*`) are at most 4 times the size of the notes' text (`ctest -R index_size` prints the ratio, 0.56 on the generated performance vault). Names are compared by a *name key*: Unicode NFC, then full case folding, then NFC again (`ex_name_key`).

## Properties

*Since 0.1.0 (sub-slice 4a-iii).* The frontmatter of a note (the first lines, between two lines of `---`) is YAML, read with libyaml. A property is a key at the start of a line; its value is a scalar, a list or something else (a nested mapping reads as text). Types: **text**, **list** (a YAML sequence), **number** (a plain integer or decimal), **checkbox** (plain `true` or `false`), **date** (`YYYY-MM-DD`), **datetime** (`YYYY-MM-DDTHH:MM`, seconds and a zone are allowed, a space may replace the `T`) and **links** (a value that holds `[[...]]`). Quoted values are text (or links). `tags`, `aliases` and `cssclasses` have meaning: tags and aliases may be a list or a text separated by commas (and for tags spaces); `cssclasses` become style classes of the note.

Editing is text surgery: changing, adding, removing, renaming or retyping a property replaces only the lines of that property (a new property goes before the closing `---`; a note without frontmatter gets one at the top); no other byte moves. A note whose frontmatter is not valid YAML is never edited. Values that would change meaning without quotes (`true`, numbers, dates, text with `: ` or ` #`, a leading `-`, `[`, `{`, `&`...) are written in double quotes. A list is written as a block list. Property names cannot contain `: # " ' [ ] { } ,` or a control or line-break character.

**Vault-wide types.** `.exhaust/types.ini` holds `[types]` with one line `name = type` (`text`, `list`, `number`, `checkbox`, `date`, `datetime` or `links`) for each property whose type the user changed; without a line the type is read from the values (the most common one). *Change type* converts the value of every note that has the property when it fits the new type (a text becomes a one item list, a list becomes its items joined with `, `, a text becomes `[[text]]` links, numbers, dates and checkboxes are only taken when the text already has that form) and leaves the others as they are. *Rename* changes the key in every note that has it and moves the type entry.

## Search

*Since 0.1.0 (sub-slice 4b-i).* The query language (`include/exhaust_search.h`). Terms are separated by spaces (all must match); `OR` between terms; `-` in front of a term, operator or group excludes the notes that match it; `( )` groups; `"..."` is a phrase; `/.../` is a regular expression (PCRE2 with Unicode properties). A plain term matches on parts of words, without regard to case, in the text or in the name of the note; in a result the name match has no position. Operators (each takes a term, a phrase, a regular expression or a group in parentheses): `file:` (the file name), `path:` (the path from the vault), `content:` (the text only), `match-case:` and `ignore-case:` (the case rule of the terms inside; the default is the option `match_case`, off), `tag:` (a tag; `#a` also finds `#a/b`), `line:` (all terms on one line), `block:` (in one block: a paragraph, or one list item), `section:` (under one heading, up to the next heading of any level; the text before the first heading is a section), `task:` (in the line of a task), `task-todo:` (an open task, `[ ]`) and `task-done:` (`[x]`). Property filters: `[name]` (the note has it), `[name:value]` (equal, without regard to case, to the value or to one of the items of a list), `[name:null]` (empty), `[name:>5]`, `[name:<=2024-06-01]` (`>`, `>=`, `<`, `<=`, numbers when both sides are numbers, else text order, which is date order for ISO dates), `[name:a OR b]`. The result of a note is its matches (positions in the editing text and 1-based lines), the count, and whether the name matched. Sort: name A to Z (the default; the name without folder), Z to A, modified new to old, old to new, most matches, fewest matches. At most `max_results` (1000) notes are listed; when the order does not depend on the matches, notes are looked at in that order and the search stops there. The text of the active search is saved in the session file (`[search] text`).

## Saved queries, query blocks and bookmarks

*Since 0.1.0 (sub-slices 4b-ii and 4b-iv).* **Saved queries** are in `.exhaust/queries.ini`: one section `[query.<n>]` for each, in the order they are shown, with `name` (unique without regard to case), `text` (the search, `docs/formats.md#search`), `sort` (`path_az`, `path_za`, `modified_new`, `modified_old`, `matches_most`, `matches_least`, and since 0.1.0 (4d) `created_new`, `created_old`; default `path_az`), and the bool keys `collapse`, `more_context` and `match_case` (default `false`, written only when true). In `name`, `text` and the values of bookmarks, `%`, `#` and line breaks are written as `%25`, `%23`, `%0A` and `%0D` (an INI value loses ` #`). Unknown sections and keys are kept.

**Query blocks.** A fenced code block (backticks or tildes) whose info string is `query` holds a search; its lines are joined with spaces. Reading mode and live preview show, where the block is, the list of results of that search: for each note a list item `- [[Folder/Note|Note]] (n)` with n matches, and unless collapsed the lines of the first five matches (`max_matches`) as quotes with the match marked `==like this==`, `and k more` for the rest; "more context" adds a line before and after each match. A search that cannot be read shows `*Query error: ...*`, no result `*No results.*`. The options (collapse, more context, copy) belong to the displayed list and are not stored in the note.

**Bookmarks** are in `.exhaust/bookmarks.ini` and are written at once after every change (by a worker thread in the application). `[bookmarks]` has `next_id` (ids are never reused). Each bookmark is `[bookmark.<id>]` with `type` (`note`, `heading`, `block`, `search`, `graph`, `group`), `title`, `parent` (the id of the group it is in; absent for the top), and by type `path` (a path from the vault), `heading`, `block` (the id without `^`), `query` (the search text, or the graph settings written as `key=value` pairs separated by `;`). The order of the sections is the order of display; the bookmarks of a group are the sections whose `parent` is its id. Renaming or moving a note or folder updates `path` of its bookmarks. Unknown sections and keys are kept.

## Panels and dialogs

*Since 0.1.0 (sub-slice 4c).* The panel shown on each side is kept in the session file as `[layout] open_panels`: `left:files`, `left:search`, `left:bookmarks`, `right:backlinks`, `right:outgoing`, `right:tags`, `right:properties`, `right:fileprops` (since 4d), `right:outline`, `right:footnotes` (one for each side). A panel whose feature switch (`[features]`: `files`, `search`, `bookmarks`, `backlinks`, `outgoing_links`, `tags_view`, `properties_view`, `outline`, `footnotes`) is off has no tab on the strip. The quick switcher uses `[quick_switcher] show_existing_only` (no "Create" row, Enter creates nothing) and `show_attachments` (other files than notes are listed); the command palette lists every command of the table, pinned ones (`[command_palette] pinned`, command ids separated by commas, in that order) first when the query is empty and above the others when it is not, and commands that cannot run now in the muted color. The page preview (`[page_preview]`) shows the section under a heading, the block of a `^id`, or the start of the note, after the pointer rests for 450 ms; the `ctrl_required_*` switches say where Ctrl must be held (`links_panels`: Backlinks and Outgoing links, `bookmarks`, `reading_view`, `editing_view`). `[backlinks] show_at_bottom` shows the linked mentions under the open note.

**Renaming** (`app.rename-file`, `F2`; `app.move-current-file-to-another-folder`). `[files] link_update` says what happens to the links that point at a renamed or moved note or folder: `ask` (a dialog says how many links in how many notes will change and offers *Update links*, *Don't update* and *Cancel*), `automatic` (they are updated without asking) or `off`. When the key is not set, `auto_update_links = false` means `off`, anything else `ask`. Links are rewritten after the move, from the index as it was before it (docs: "Index"); the change is made in the files, and notes that are open pick it up the way they pick up any outside change.

*Since 0.1.0 (sub-slice 4d).* **The explorer menu** is opened by a right click on a row of the file explorer (an empty area is the vault folder) and by the *More* button of its toolbar (the selected row). For a note: *Open in new tab*, *Rename...*, *Move to folder...*, *Delete*, *Copy path from the vault*, *Copy path from the system root*, *Open in default app*, *Show in file manager*; for a folder: *New note in this folder*, *New folder in this folder* and the same actions except *Open in new tab* and *Open in default app*; for the vault folder: the two *New* entries, *Copy path from the system root* and *Show in file manager*. *Delete* asks first when `[files] confirm_delete` is true (the question says where the file goes: `[files] trash_mode`: the system's trash, `.trash` in the vault, or nowhere) and closes the tabs that show the file; the explorer never deletes anything without it. *Rename*, *Move* and a **drag** of a row onto a folder row (or onto the empty area below the rows, the vault folder) use the link-safe flow above, so `[files] link_update` applies; a drag onto the row's own folder or into itself does nothing, and a press without movement is a click. The page preview works on notes of the explorer (`[page_preview] ctrl_required_files`), on the rows of the outline (`ctrl_required_outline`) and on tab headers (`ctrl_required_tab_header`). **`](` suggestions:** typing `(` right after `]` lists the notes and files whose name fits what is typed; accepting one writes the path from the vault, with space, `(`, `)`, `%`, `<` and `>` written `%20`, `%28`, `%29`, `%25`, `%3C` and `%3E`, and the closing `)`. **Created sorts:** the explorer's *Sort* button also cycles through creation time (`sort_by = created`); search and saved queries sort by `created_new` and `created_old` (the file's creation time where the file system keeps one, else equal and the path order). **The properties of the open note** are the panel `fileprops` (command `properties-view.show-file-properties`, feature `properties_view`): *Add* (name, then value; the type is the vault's for that name, else text), *Edit* (a checkbox flips, a list takes items separated by commas), *Type* (also written to `.exhaust/types.ini`) and *Remove*; each is a text edit of the note's front matter that changes only the lines of that property, as one undo step.

**Shortcuts of the dialogs.** Up and Down move in the list of a dialog, Enter accepts, Escape closes; in the quick switcher Shift+Enter always creates the note named by the text; in the command palette Ctrl+Space pins or unpins the selected command. In a list: Up, Down, Home, End, Page Up, Page Down, Enter or Space activate, Left and Right close and open a row with an arrow.

## Vault folders that Exhaust owns

Inside a vault, `.exhaust` holds Exhaust's own plain-text files (settings, themes, snippets, hotkeys, workspaces, plugins) and `.trash` holds notes deleted with `trash_mode = vault_trash` (the deleted file keeps its path relative to the vault). Both are hidden from the file explorer, search, links and the graph. Other names that start with a dot are hidden from the explorer and never indexed.

## File recovery snapshots (7e)

Snapshots are the *history* of a note: earlier saved versions you can look at and restore. They are separate from the recovery journal (crash safety). Both are user data in the OS local-data folder, never in the vault and never in the cache.

**Where and how named.** `<local-data>/Exhaust/vaults/<vault-id>/recovery/`, one file per snapshot, named `<note>.<YYYYMMDD-HHMMSS>.<kind>`:
- `<note>` is the vault-relative path with each `/` written `__` and each `_` written `_u` (so the name tells one note from another);
- the stamp is the local time the snapshot was taken;
- `<kind>` is `snap` for a snapshot taken by the interval and `bak` for the other side of a conflict (the text of the file that was on disk, or the editor's text, copied before the user's choice replaced it; see the conflict banner). The bytes are those of the file, never altered.

A file in this folder whose name does not have this shape is ignored. (Before 7e a conflict copy was named with a single `_` for `/` and no `_u`; those files stay where they are and are not listed.)

**When a snapshot is taken.** On the thread that saves notes, after a note was written to disk: if File recovery is on (`[features] file_recovery`) and the note has no snapshot newer than `[file_recovery] interval_minutes`, the text just written is copied. The first time a note with no snapshot at all is saved, the text it had on disk *before* the save is copied first, so there is always an earlier version to go back to. Nothing is copied when the text is the same as the newest snapshot's. A new note nothing was typed into has none.

**Pruning.** When a vault opens and then once an hour, snapshots of kind `snap` older than `[file_recovery] history_days` days are deleted (a value below 1 counts as 1). Snapshots of kind `bak` are never deleted automatically: they hold text the user may not have decided about. *Clear history* on the File recovery page (after a confirmation) deletes every `snap`; it says that conflict copies stay.

**Seeing and restoring.** `file-recovery.open-local-history` lists the snapshots of the active note, newest first, each with its date and time, its size and how many lines it differs from the note now (a conflict copy says so). Choosing one shows the changes (a unified diff of that version against the note, in the editor, read only) with *Restore this version* and *Back*. *Restore* first takes a snapshot of the note as it is now (kind `snap`), then replaces the buffer's text as one edit (Undo brings the text back, and the note is saved by the usual autosave); it never writes the note file itself.

## Note text on disk

A note is a UTF-8 file. Exhaust refuses to open a file that is not valid UTF-8 (it shows a message and never writes to it). A leading UTF-8 byte-order mark and the line-ending style are remembered per note and written back unchanged: a file whose line breaks are all CRLF is edited with LF in memory and saved with CRLF; a file that mixes line-ending styles (or contains lone CR characters) is edited and saved byte for byte, with the line-ending characters left alone. Saving writes a temporary file next to the note, flushes it to disk and renames it over the note; if the note is a symbolic link, the file it points to is replaced and the link is kept; file permissions are kept.
