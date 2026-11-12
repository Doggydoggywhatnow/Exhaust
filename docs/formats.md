# Exhaust file formats and names

This file is the source of truth for every name that `AGENTS.md` freezes after release: setting keys, theme tokens, snippet grammar, hotkey, workspace and plugin files, slash commands and the Lua API. **Add a key or name here before adding it to the code.** After a release, never rename or remove one; to change it, add a new name and migrate the old value on load. Unknown keys are kept and written back unchanged.

Each item has a name, a type, a default and the version it first appeared in ("since"). Defaults for settings follow the states shown in `screenshots/settings/` where a screenshot shows one; values marked *TODO-verify* could not be confirmed from the screenshots or the Obsidian Help pages and must be decided before the key is implemented.

*TODO-verify* items are assigned: `default_file` to 7a; the Canvas, Note composer and Quick switcher values to 7b; the command-list boundaries and the macOS defaults to 7c. The slash-command list is done (see Slash commands), ahead of 3d.

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
| `window_width` | int | `1100` | 0.1.0 |
| `window_height` | int | `750` | 0.1.0 |

### [interface]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `show_tab_title_bar` | bool | `true` | 0.1.0 |
| `show_ribbon` | bool | `true` | 0.1.0 |
| `ribbon_items` | list | empty (all) | 0.1.0 |
| `zoom_percent` | percent | `100` | 0.1.0 |
| `native_menus` | bool | `false` | 0.1.0 |
| `window_frame` | enum(custom\|native\|hidden) | `hidden` | 0.1.0 |
| `settings_in_new_window` | bool | `false` | 0.1.0 |

### [editor]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `default_view` | enum(editing\|reading) | `editing` | 0.1.0 |
| `default_mode` | enum(live_preview\|source) | `live_preview` | 0.1.0 |
| `show_mode_in_status_bar` | bool | `true` | 0.1.0 |
| `inline_title` | bool | `true` | 0.1.0 |
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
| `default_file` | enum(last_opened\|...) | `last_opened` (*TODO-verify*: the other values) | 0.1.0 |
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
| `use_wikilinks` | bool | `true` | 0.1.0 |
| `show_all_file_types` | bool | `false` | 0.1.0 |
| `confirm_delete` | bool | `true` | 0.1.0 |
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
| `wheel` | enum(pan\|zoom) | `pan` (*TODO-verify*: full value set) | 0.1.0 |
| `ctrl_drag` | enum(menu\|...) | `menu` (*TODO-verify*: other values) | 0.1.0 |
| `show_card_names` | enum(always\|...) | `always` (*TODO-verify*: other values) | 0.1.0 |
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
| `text_after_extract` | enum(link\|...) | `link` (*TODO-verify*: other values) | 0.1.0 |
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

*TODO-verify*: the screenshot is cropped and may omit other options on this page.

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
- Since: 0.1.0.

## Commands

Every command in the command registry, read from the nine hotkeys screenshots (`screenshots/settings/hotkeys-1.png` to `hotkeys-9.png`). **Id** is Exhaust's id (`area.action`, lowercase, hyphens; commands with no area are `app.` or, for text-editing commands, `editor.`). **Default** is the shortcut shown in the screenshots for Windows and Linux; a blank means no default shortcut. **Editor** is *yes* for commands that act on the text at the cursor: those are exactly the commands the slash menu lists. The Editor column is Exhaust's own classification, not read from any screenshot. Since: 0.1.0 for every command.

The code's table of commands (`ex_command_table`) is generated from this table at build time (`tools/gen_tables.c`), so a command exists in the code only if it is listed here. A command is *declared* by this table and *bound* when the code that runs it exists; an unbound command, a command whose feature is switched off in `[features]`, and a command of a stub feature are not available (they are shown disabled, never hidden from the hotkeys screen). A command's feature is its area with hyphens turned into underscores (`daily-notes.` belongs to `daily_notes`), except `graph-view.` (`graph`) and `unique-note-creator.` (`unique_note`); `app.`, `editor.`, `table.` and `format-converter.` commands have no feature switch.

205 commands (203 read from the screenshots and two added for sub-slice 3e, *Spelling: Add word to dictionary* and *Spelling: Replace with the next suggestion*, because Exhaust has no context menu yet to pick a suggestion from). Left out on purpose: *Configure mobile Quick Action* (mobile is a non-goal), *Open sandbox vault* and *Show release notes* (Obsidian-specific), and *Copy Obsidian URL for current file*, which is renamed *Copy URL for current file*. Commands for stubbed features (Sync) are present and disabled; *Download attachments for current file* needs the network and is *(optional)*. *TODO-verify*: the screenshots are cropped at page edges, so a command may be missing at a boundary between two pages; the macOS defaults are not shown; and the shortcuts shown may be one person's settings rather than defaults.

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
| `properties-view.show-all-properties` | Properties view: Show all properties |  |  |
| `properties-view.show-file-properties` | Properties view: Show file properties |  |  |
| `quick-switcher.open-quick-switcher` | Quick switcher: Open quick switcher | `Ctrl+O` |  |
| `app.reload-app-without-saving` | Reload app without saving |  |  |
| `editor.remove-heading` | Remove heading |  | yes |
| `app.rename-file` | Rename file | `F2` |  |
| `editor.rename-this-heading` | Rename this heading... |  | yes |
| `app.reset-zoom` | Reset zoom |  |  |
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
| `app.zoom-in` | Zoom in |  |  |
| `app.zoom-out` | Zoom out |  |  |

## workspaces.ini

Plain text, one section per saved layout, `[workspace.<name>]`:

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `left_visible` | bool | `true` | 0.1.0 |
| `right_visible` | bool | `true` | 0.1.0 |
| `left_width` | int | `260` | 0.1.0 |
| `right_width` | int | `300` | 0.1.0 |
| `active` | string | empty (the active tab id) | 0.1.0 |
| `tab.<n>` | string | none: `path\|view\|mode\|pinned` for each open tab, `n` from 0 | 0.1.0 |

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
| `[layout]` `active_tab` | int | empty (the active tab of the active group; written for readers, the group's own `active` is what is restored) | 0.1.0 |
| `[layout]` `active_group` | int | the first group of the main window | 0.1.0 (added with 2c) |
| `[tab.<id>]` `path`, `view`, `mode` | string | required | 0.1.0 |
| `[tab.<id>]` `pinned`, `stacked` | bool | `false` | 0.1.0 |
| `[tab.<id>]` `group` | int | `0` (the split it is in) | 0.1.0 |
| `[tab.<id>]` `cursor_line`, `cursor_column` | int | `0` | 0.1.0; `cursor_line` counts from 0, `cursor_column` is the byte offset in that line (from 0); both are clamped to the text on restore and never land inside a character |
| `[tab.<id>]` `scroll_y` | float (pixels at 100% zoom, one decimal) | `0` | 0.1.0 |
| `[graph]` | keys of the graph settings, plus `open` (bool) | the last state | 0.1.0 |
| `[search]` `text` | string | empty | 0.1.0 |

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

## Vault folders that Exhaust owns

Inside a vault, `.exhaust` holds Exhaust's own plain-text files (settings, themes, snippets, hotkeys, workspaces, plugins) and `.trash` holds notes deleted with `trash_mode = vault_trash` (the deleted file keeps its path relative to the vault). Both are hidden from the file explorer, search, links and the graph. Other names that start with a dot are hidden from the explorer and never indexed.

## Recovery snapshots of the other side of a conflict

*Since 0.1.0.* When a note was changed on disk while it had unsaved edits (the conflict banner), and the user keeps their own version, Exhaust first copies the version that is on disk to `<local-data>/Exhaust/vaults/<vault-id>/recovery/<vault-relative path, "/" replaced by "__">.<YYYYMMDD-HHMMSS>.bak` (same bytes as the file, never altered). File recovery (sub-slice 7e) later takes these over as its first snapshots. They are user data like the journal: never deleted by Exhaust automatically in 0.1.0 before 7e defines the retention.

## Note text on disk

A note is a UTF-8 file. Exhaust refuses to open a file that is not valid UTF-8 (it shows a message and never writes to it). A leading UTF-8 byte-order mark and the line-ending style are remembered per note and written back unchanged: a file whose line breaks are all CRLF is edited with LF in memory and saved with CRLF; a file that mixes line-ending styles (or contains lone CR characters) is edited and saved byte for byte, with the line-ending characters left alone. Saving writes a temporary file next to the note, flushes it to disk and renames it over the note; if the note is a symbolic link, the file it points to is replaced and the link is kept; file permissions are kept.
