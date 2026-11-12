# Exhaust file formats and names

This file is the source of truth for every name that `AGENTS.md` freezes after release: setting keys, theme tokens, snippet grammar, hotkey, workspace and plugin files, slash commands and the Lua API. **Add a key or name here before adding it to the code.** After a release, never rename or remove one; to change it, add a new name and migrate the old value on load. Unknown keys are kept and written back unchanged.

Each item has a name, a type, a default and the version it first appeared in ("since"). Defaults for settings follow the states shown in `screenshots/settings/` where a screenshot shows one; values marked *TODO-verify* could not be confirmed from the screenshots or the Obsidian Help pages and must be decided before the key is implemented.

Types: `bool`, `int`, `percent`, `string`, `path` (relative to the vault), `color` (`#rrggbb` or `#rrggbbaa`), `list` (comma separated), `enum(a|b|c)`.

## settings.ini

A plain text INI file in the vault's `.exhaust` folder (and a global copy in the OS config folder). Sections in square brackets, `key = value` lines, `#` comments. Keys are lowercase with underscores.

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
| `color_scheme` | enum(dark|light|system) | `system` | 0.1.0 |
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
| `window_frame` | enum(custom|native|hidden) | `hidden` | 0.1.0 |
| `settings_in_new_window` | bool | `false` | 0.1.0 |

### [editor]

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `default_view` | enum(editing|reading) | `editing` | 0.1.0 |
| `default_mode` | enum(live_preview|source) | `live_preview` | 0.1.0 |
| `show_mode_in_status_bar` | bool | `true` | 0.1.0 |
| `inline_title` | bool | `true` | 0.1.0 |
| `readable_line_length` | bool | `true` | 0.1.0 |
| `strict_line_breaks` | bool | `false` | 0.1.0 |
| `properties_in_document` | enum(visible|hidden|source) | `visible` | 0.1.0 |
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
| `default_file` | enum(last_opened|...) | `last_opened` (*TODO-verify*: the other values) | 0.1.0 |
| `always_focus_new_tabs` | bool | `true` | 0.1.0 |
| `sort_by` | enum(name\|modified\|created) | `name` | 0.1.0 |
| `sort_descending` | bool | `false` | 0.1.0 |
| `new_note_location` | enum(vault|folder|same_folder|subfolder) | `vault` | 0.1.0 |
| `new_note_folder` | path | empty | 0.1.0 |
| `attachment_location` | enum(vault|folder|same_folder|subfolder) | `vault` | 0.1.0 |
| `attachment_folder` | path | empty | 0.1.0 |
| `attachment_subfolder` | string | empty | 0.1.0 |
| `link_format` | enum(shortest|relative|absolute) | `shortest` | 0.1.0 |
| `auto_update_links` | bool | `true` | 0.1.0 |
| `use_wikilinks` | bool | `true` | 0.1.0 |
| `show_all_file_types` | bool | `false` | 0.1.0 |
| `confirm_delete` | bool | `true` | 0.1.0 |
| `delete_attachments` | enum(ask|always|never) | `ask` | 0.1.0 |
| `trash_mode` | enum(system|vault_trash|permanent) | `system` | 0.1.0 |
| `excluded` | list | empty | 0.1.0 |
| `config_folder` | string | `.exhaust` | 0.1.0 |
| `allow_uri_callbacks` | bool | `false` | 0.1.0 |

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
| `new_file_location` | enum(vault|folder|same_folder|subfolder) | `vault` | 0.1.0 |
| `wheel` | enum(pan|zoom) | `pan` (*TODO-verify*: full value set) | 0.1.0 |
| `ctrl_drag` | enum(menu|...) | `menu` (*TODO-verify*: other values) | 0.1.0 |
| `show_card_names` | enum(always|...) | `always` (*TODO-verify*: other values) | 0.1.0 |
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
| `text_after_extract` | enum(link|...) | `link` (*TODO-verify*: other values) | 0.1.0 |
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
| `page_size` | enum(A3|A4|A5|Legal|Letter|Tabloid) | `A4` | 0.1.0 |
| `landscape` | bool | `false` | 0.1.0 |
| `margin` | enum(default|minimal|none) | `default` | 0.1.0 |
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

## workspaces.ini

Plain text, one section per saved layout, `[workspace.<name>]`:

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `left_visible` | bool | `true` | 0.1.0 |
| `right_visible` | bool | `true` | 0.1.0 |
| `left_width` | int | `260` | 0.1.0 |
| `right_width` | int | `300` | 0.1.0 |
| `active` | string | empty (the active tab id) | 0.1.0 |
| `tab.<n>` | string | none: `path|view|mode|pinned` for each open tab, `n` from 0 | 0.1.0 |

## themes/*.theme

A plain text file in `.exhaust/themes` (per vault) or the global config folder. INI style, `#` comments.

```
[theme]
name = My theme
author = Someone
base = dark          # light or dark: the built-in theme that supplies missing values

[colors]
accent = #7c5cbf

[fonts]
text = Atkinson Hyperlegible

[sizes]
radius = 6
```

Sections: `[theme]` (name, author, base), `[colors]`, `[fonts]`, `[sizes]`. Any token not set comes from `base`. Values: colors as `#rrggbb` or `#rrggbbaa`, fonts as a font family name, sizes as integers in pixels at 100% zoom.

### Theme tokens

| Name | Type | Default | Since |
| --- | --- | --- | --- |
| `colors.background` | color | from the built-in theme | 0.1.0 |
| `colors.background_alt` | color | from the built-in theme | 0.1.0 |
| `colors.surface` | color | from the built-in theme | 0.1.0 |
| `colors.surface_hover` | color | from the built-in theme | 0.1.0 |
| `colors.border` | color | from the built-in theme | 0.1.0 |
| `colors.text` | color | from the built-in theme | 0.1.0 |
| `colors.text_muted` | color | from the built-in theme | 0.1.0 |
| `colors.text_faint` | color | from the built-in theme | 0.1.0 |
| `colors.accent` | color | from the built-in theme | 0.1.0 |
| `colors.accent_hover` | color | from the built-in theme | 0.1.0 |
| `colors.link` | color | from the built-in theme | 0.1.0 |
| `colors.link_unresolved` | color | from the built-in theme | 0.1.0 |
| `colors.selection` | color | from the built-in theme | 0.1.0 |
| `colors.cursor` | color | from the built-in theme | 0.1.0 |
| `colors.highlight` | color | from the built-in theme | 0.1.0 |
| `colors.error` | color | from the built-in theme | 0.1.0 |
| `colors.warning` | color | from the built-in theme | 0.1.0 |
| `colors.success` | color | from the built-in theme | 0.1.0 |
| `colors.code_background` | color | from the built-in theme | 0.1.0 |
| `colors.code_text` | color | from the built-in theme | 0.1.0 |
| `colors.tag` | color | from the built-in theme | 0.1.0 |
| `colors.graph_node` | color | from the built-in theme | 0.1.0 |
| `colors.graph_node_unresolved` | color | from the built-in theme | 0.1.0 |
| `colors.graph_link` | color | from the built-in theme | 0.1.0 |
| `colors.graph_focus` | color | from the built-in theme | 0.1.0 |
| `colors.canvas_grid` | color | from the built-in theme | 0.1.0 |
| `colors.callout_note` | color | from the built-in theme | 0.1.0 |
| `colors.callout_abstract` | color | from the built-in theme | 0.1.0 |
| `colors.callout_info` | color | from the built-in theme | 0.1.0 |
| `colors.callout_todo` | color | from the built-in theme | 0.1.0 |
| `colors.callout_tip` | color | from the built-in theme | 0.1.0 |
| `colors.callout_success` | color | from the built-in theme | 0.1.0 |
| `colors.callout_question` | color | from the built-in theme | 0.1.0 |
| `colors.callout_warning` | color | from the built-in theme | 0.1.0 |
| `colors.callout_failure` | color | from the built-in theme | 0.1.0 |
| `colors.callout_danger` | color | from the built-in theme | 0.1.0 |
| `colors.callout_bug` | color | from the built-in theme | 0.1.0 |
| `colors.callout_example` | color | from the built-in theme | 0.1.0 |
| `colors.callout_quote` | color | from the built-in theme | 0.1.0 |
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

*TODO-verify*: the Obsidian Help pages do not list the default slash commands, and the nine hotkeys screenshots have not been read to extract command ids. Before 3d is finished, list the `editor` commands here from the command registry.


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
