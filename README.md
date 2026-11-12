# Exhaust 0.0.0

A robust and personable notepad that doesn't get in your way. Notes, journals, knowledge bases and project plans, linked together.

Exhaust's goal is feature parity with Obsidian's documented feature set, in a private, native program. This README is the spec. Features marked *(later)* may be stubs in the first demo. Items marked *(optional)* use the network and are off by default. Other features that are off by default, such as Vim mode, are not marked *(optional)*. The technical design is at the end.

## Values:

- **Customizable, but not overwhelmingly**: Exhaust offers settings to change the workflow of the notepad to fit your needs. You can even set the default size of a new exhaust window. Despite this, it is not overloaded with features. We include a robust kit of text editing features, some quality of life things, and allow you to control which features you want and where you want them. As well as what you want everything included to look like.
- **Aesthetic**: The design of Exhaust is meant to be simple and pleasant to the eye. It is inspired by the design of Midori MD notebooks.
- **Simple**: Exhaust provides a simple environment in which to use complex features. And where features present in other apps do not go directly towards text, we will drop those features.
- **Small, but not at the cost of features**: Exhaust is as small as it can be without dropping anything in this README. The program may be as large as it needs to be to fit all of these features, but the core install stays within a fixed size budget (see Technical design); optional libraries ship separately.
- **Private**: What you write is your business. Simple as.

## Rules

- Notes are plain Markdown files in a folder you choose. Exhaust never moves, renames or reorganizes them behind your back.
- No network requests, telemetry or accounts unless you turn on an *(optional)* feature.
- **Updates**: there is no auto-updater. An optional manual "check for updates" button *(optional)* asks the network only when you press it.
- Every feature below can be switched off. Built-in features such as Graph, Canvas and Bases are separate toggles in Settings.
- Notes are not encrypted by Exhaust; use full-disk encryption.

## Vaults and files

- **Vaults**: a vault is a folder of Markdown files. A vault button at the bottom of the file tree switches between vaults. Create, open and remember several. Manage them from the vault list: rename, move, remove from the list (never deleting files), create a vault from an existing folder, and transfer settings from one vault to another. Choosing a folder uses the operating system's native folder-picker dialog (SDL3 provides one on Windows, Linux and macOS), and on first launch, when no vault is known, Exhaust shows the vault manager.
- **File explorer**: a tree of folders and notes. Create, rename, move, delete and sort. Folders shown here are the real folders on disk. Sorting and grouping are done inside the app, stored in settings, and never rename, move or reorder anything on disk. Only create, rename, move and delete, which you do yourself, change files. Auto-reveal the open note in the tree, and expand or collapse all folders. Each note's More-options menu offers open in default app, show in file manager, copy path (from the vault, or from the system root) and export to PDF.
- **Open anything**, with exactly these file types: Markdown (`.md`), bases (`.base`), JSON Canvas (`.canvas`), images (`.avif`, `.bmp`, `.gif`, `.jpeg`, `.jpg`, `.png`, `.svg`, `.webp`), audio (`.flac`, `.m4a`, `.mp3`, `.ogg`, `.wav`, `.webm`, `.3gp`), video (`.mkv`, `.mov`, `.mp4`, `.ogv`, `.webm`) and PDF (`.pdf`). Other file types are left alone. Audio, video and PDF need the optional media and PDF libraries; without them Exhaust offers "Open externally". Playback depends on the codecs available.
- **Excluded files**: patterns that hide files from search, links and the graph.
- **Link-safe renames**: renaming or moving a note updates every link to it. It asks first by default; you can make it automatic or off. It covers wikilinks, embeds, Markdown links (URL-encoded and relative), links inside properties, `#heading` and `#^block` references, `.canvas` files, `.base` files and bookmarks.
- **Note names**: a name cannot contain `# ^ [ ] |` or `:`, a `%%` pair, or a Windows reserved name (such as `CON` or `NUL`). Links match names without regard to case and after Unicode (NFC) normalization.
- **Saving**: notes autosave after a short idle. If a file changes on disk while the editor has unsaved changes, Exhaust keeps the editor's text, shows a conflict banner with a diff, and takes a snapshot in file recovery first. Line endings (CRLF or LF) and a byte-order mark are preserved. Notes are UTF-8.
- **Trash**: deleted notes go to the system trash, to a `.trash` folder in the vault, or are deleted permanently, as you choose, with an optional confirmation.
- **Symbolic links**: followed, with loops detected.
- **Configuration folder**: settings, themes, snippets, hotkeys and workspaces live in a `.exhaust` folder inside the vault, in plain text. The search index and other caches live in the operating system's cache folder, keyed by vault, never inside the vault, so file-sync tools cannot corrupt them. Settings has a *Rebuild vault cache* button.
- **Import**: Markdown and plain text folders, HTML, CSV and Textbundle. Importer templates control how imported notes are named and laid out. Importers for Apple Journal, Apple Notes, Bear, Craft, Evernote, Google Keep, Logseq, OneNote, Notion, Roam, Tomboy/Gnote and Airtable are listed as disabled *(later)* entries.
- **Backups**: because everything is plain files, any backup tool works.

## Writing

- **Markdown**: headings, bold, italic, strikethrough, highlight, lists, task lists, quotes, code blocks, tables, horizontal rules, footnotes, comments, math, and Mermaid diagrams (flowchart, sequence, class, state, ER, pie and gantt).
- **Callouts**: typed, titled and foldable (`+` or `-`) blocks, nestable. The types, each with its own color and icon, are `note`, `abstract` (aliases `summary`, `tldr`), `info`, `todo`, `tip` (`hint`, `important`), `success` (`check`, `done`), `question` (`help`, `faq`), `warning` (`caution`, `attention`), `failure` (`fail`, `missing`), `danger` (`error`), `bug`, `example` and `quote` (`cite`). The type is not case-sensitive and unknown types show as `note`. Custom callouts, with your own color and icon, are made with style snippets.
- **Live preview**: formatting shows as you type, with the syntax revealed under the cursor.
- **Source mode and reading mode**: switch per note.
- **Folding** of headings and lists.
- **Find and replace** inside a note, with regular expressions.
- **Multiple cursors** and **rectangular selection**.
- **Editing shortcuts**: the default shortcuts for copying, deleting, moving the cursor and selecting text, for Windows and Linux and for macOS, are listed in [`docs/formats.md`](docs/formats.md#default-editing-shortcuts). Smart list continuation and indent follow the Editor settings. Shortcuts work on non-US keyboard layouts.
- **Spell check** (Exhaust's own checker, English (US) dictionary included, more added as word lists) and a **word count** in the status bar.
- **Export to PDF**: from the command palette or a note's More-options menu. Options: page size (A3, A4, A5, Legal, Letter or Tabloid), landscape, margin (Default, Minimal or None), downscale percent, and include the file name as the title. Exports are always light-styled.
- **Embedded content**: notes, headings, blocks, lists, canvases, images, audio, video and PDFs, with `![[file]]` and Markdown syntax. Images take a size: `![[img.png|640x480]]` or `![[img.png|100]]`. PDFs take a page and height: `![[f.pdf#page=3&height=400]]`. External image URLs need the network, so they are *(optional)* and off by default, showing a placeholder instead. Web links open in the system browser; Exhaust has no embedded browser.
- **HTML** in notes: a safe subset of tags, drawn by Exhaust itself.
- **Slash commands**: type `/` for a menu of commands.
- **Audio recorder**: record straight into a note *(later)*.
- **Vim mode**, off by default.

## Links

- **Wikilinks**: `[[Note]]`, `[[Note|alias]]`, `[[Note#Heading]]`, `[[Note#^block]]`, plus ordinary Markdown links. Suggestions appear as you type.
- **Backlinks**: a panel of notes that point at the open note, including unlinked mentions. Backlinks can also be shown at the bottom of the document.
- **Outgoing links**: a panel of everything the open note points at.
- **Aliases**: alternate names for a note that links can use.
- **Page preview**: hover a link to see the note.
- **Tags**: `#tag` and `#nested/tags`, a tag pane, and search by tag.
- **Properties**: typed metadata at the top of a note (text, lists, numbers, checkboxes, date, date and time, links) kept as plain YAML. The default properties are `tags`, `aliases` and `cssclasses`; `cssclasses` adds style classes to the note. A properties pane shows every property in the vault, and you can rename a property or change its type across the whole vault as an action you choose.

## PDF viewer

PDFs open in a tab and embed in notes (needs the optional PDF library).

- **Navigation**: a table of contents, fit width, fit height, and zoom.
- **Search**: find in the PDF, with match case and highlight all.
- **Copy text** from the page.
- **Links to a selection**: `[[file.pdf#page=1&selection=...]]` opens the PDF at that selected text.
- **Annotations** are not supported (see Non-goals).

## Graph

- **Graph view**: notes are circles, links are lines, busier notes are bigger. Hover to highlight neighbours, click to open, right-click for actions, scroll to zoom, drag to pan, arrow keys and `+`/`-` too.
- **Graph display**: arrows, a text fade threshold, node size and link thickness.
- **Local graph** for the open note, with adjustable depth.
- **Filters and groups**: by search, tag, folder and property, with colors. Toggle orphans, unresolved links and attachments.
- **Forces**: sliders for centering, repulsion, link strength and distance.
- **Animate**: replay the vault growing over time.
- **Keyboard route**: every graph action also works from a list, so nothing depends on a mouse.

## Finding things

- **Search**: full text over the vault with the operators `file:`, `path:`, `content:`, `match-case:`, `ignore-case:`, `tag:`, `line:`, `block:`, `section:`, `task:`, `task-todo:` and `task-done:`, property filters like `[property:value]`, regular expressions, `OR`, `-` for not, parentheses and quotes. Matching works on parts of words. Results show context (with an option for more), can be sorted, copied, collapsed and saved as a query.
- **Embedded search**: a `query` code block in a note shows live search results. The same result options apply.
- **Quick switcher**: `Ctrl+O`, fuzzy match by name or alias, `Enter` to create a note that doesn't exist.
- **Command palette**: `Ctrl+P`, fuzzy match every command, with pinned commands.
- **Outline**: the headings of the open note, clickable and draggable to reorder sections.
- **Bookmarks**: notes, headings, searches and graphs, in groups.
- **Random note**.
- **Footnotes pane**.

## Organizing and structure

- **Canvas**: an infinite board for notes, images, web links, text cards and groups, joined by labeled arrows. Saved as `.canvas` in the open JSON Canvas format.
- **Bases**: database-style views of your notes and their properties, with filters, sorting, grouping, summaries (built-in and custom), formulas and functions (all 72 documented ones). Expressions use the `note.`, `file.` and `formula.` namespaces and the `this` object. A view can have a limit, copy to the clipboard and export CSV. Layouts and their settings: table (row height), list (markers and separators), cards (card size and image property), kanban (columns) and map. Saved as `.base` files, or embedded in a note as a code block. `.base` files belong to Exhaust, so it may rewrite them (keeping any keys it doesn't know); notes are never reformatted. The map layout needs map tiles from the network, so it is *(optional)* and off by default, with a tile address you set; without it, it plots coordinates on a plain grid.
- **Daily notes**: a note per day from a template, with an optional calendar. Date formats use Moment-style tokens, such as `YYYY-MM-DD`.
- **Templates**: insert a template with date, time and title variables, using Moment-style date tokens (`{{date:YYYY-MM-DD}}`). The template's properties are merged into the note's own.
- **Unique note creator**: timestamp-named notes for Zettelkasten work.
- **Note composer**: merge two notes, or split a selection into a new note, with links updated. An optional template file sets the text of the extracted note.
- **Slides**: present any note, with `---` between slides.
- **Format converter**: migrates old frontmatter across the whole vault (`alias` to `aliases`, `tag` to `tags`, `cssclass` to `cssclasses`). It runs only when you ask, after a warning to back up your notes first.

## Workspace

- **Tabs, splits and pop-out windows**: drag to rearrange, stack tabs, link panes so they follow each other, pin tabs.
- **Sidebars** left and right, collapsible, with a ribbon of shortcuts.
- **Status bar**.
- **Workspaces**: save and restore whole layouts for journaling, reading or writing.
- **Drag and drop** files and text.
- **Hotkeys**: every command is bindable, with conflicts shown.
- **File recovery**: automatic snapshots (every 5 minutes, kept 7 days by default, both adjustable) with restore and diff.
- **Command line** and a **URI scheme** to open notes, search and create from other programs *(later)*.

## Settings

A searchable settings screen, laid out as in [`screenshots/settings`](screenshots/settings): a sidebar of pages (Options first, then Built-in features), and on each page a list of rows with a name, a one-line description and one control. Numbers and colors have a reset button. Every option has a default. The key that stores each option is listed in [`docs/formats.md`](docs/formats.md). The pages, in order:

- **General**: version and a manual *check for updates* button *(optional)*; language; help; a notice when startup takes longer than expected; command line interface *(later)*; hardware acceleration. There is no account or license section (see Non-goals) and no automatic updates.
- **Appearance**: color scheme (dark, light or follow the system); accent color; theme (choose one, open the themes folder, manage themes); check installed themes for updates *(optional)*; style snippets (how many are on, manage them); fonts for the interface, for text and for monospace text; font size; quick font size adjustment with Ctrl+scroll or pinch; custom app icon; translucent window (macOS only).
- **Interface**: show the tab title bar; show the ribbon; choose which commands the ribbon shows; zoom level; native menus; window frame style (custom, native or hidden; needs a restart); open settings in a new window.
- **Editor**: default view for new tabs (editing or reading); default editing mode (live preview or source); show the editing mode in the status bar. Display: inline title, readable line length, strict line breaks, properties in document (visible, hidden or source), fold headings, fold indents, line numbers, indentation guides, right-to-left text. Behavior: spell check and its languages, auto-pair brackets, auto-pair Markdown syntax, smart lists, indent using tabs, indent visual width. Advanced: convert pasted HTML to Markdown, Vim key bindings (off by default).
- **Files and links**: default file to open at start; always focus new tabs; default location for new notes; default location for new attachments (vault folder, a set folder, the same folder as the note, or a subfolder of it). Links: new link format (shortest path when possible, relative or absolute), automatically update internal links, use wikilinks, show all file types. Trash: confirm before deleting files, delete attachments when deleting a note (ask each time, always or never), and what happens to deleted files (move to the system trash, to a `.trash` folder in the vault, or delete permanently). Advanced: excluded files, the name of the config folder (default `.exhaust`), allow URI callbacks, and a *Rebuild vault cache* button.
- **Hotkeys**: every command in alphabetical order with a search box. Each row shows its shortcuts as chips (or "Blank" when it has none), a plus button to add a shortcut and a cross on each chip to remove it. Conflicts are shown.
- **Keychain** *(later)*: secrets for plugins and sync, kept in the operating system's credential store and never in plain files.
- **Built-in features**: a searchable list; each feature has a switch, and a gear button that opens its own page when it has options. On by default: Backlinks, Bases, Bookmarks, Canvas, Command palette, Daily notes, File recovery, Files, Graph view, Note composer, Outgoing links, Outline, Page preview, Properties view, Quick switcher, Search, Tags view, Templates, Unique note creator and Word count. Off by default: Audio recorder, Footnotes view, Random note, Slash commands, Slides and Workspaces. Sync and Publish are stubs, off and disabled.
- **Plugins** *(later)*: installed plugins, each with its permissions and an on/off switch. Browsing a directory needs the *(optional)* community directory.

Then one page for each built-in feature that has options:

- **Backlinks**: show backlinks at the bottom of notes, so they are visible in new tabs by default (off by default).
- **Canvas**: default location for new canvas files; default mouse wheel behavior (pan or zoom); default Ctrl+drag behavior (show a menu, or another action); show card names (always, or another setting); snap to grid; snap to objects; zoom threshold for hiding card content (lower values run faster but hide content sooner when zooming out).
- **Command palette**: pinned commands, with a button to add one and a message when there are none.
- **Daily notes**: date format (a list of formats and a custom format with a live preview); folder for new daily notes; template file.
- **File recovery**: snapshot interval in minutes (5 by default); history length in days (7 by default); view and restore snapshots; clear history.
- **Note composer**: what replaces the selected text after extracting it (a link to the new file, or another option); template file for merging and extracting, with the variables `{{content}}`, `{{fromTitle}}`, `{{newTitle}}` and `{{date:FORMAT}}`; confirm before merging two files.
- **Page preview**: for each place a link can be hovered, a switch for whether Ctrl is required to show the preview: Search, Backlinks and Outgoing links; Reading view; Editing view; Tab header; Files; Properties view; Bookmarks; Outline; Bases; Graph view. A filter box narrows the list.
- **Quick switcher**: show existing files only (hides links to notes not created yet); show attachments.
- **Sync** *(stub)*: the page is present and disabled.
- **Templates**: template folder; date format; time format. Each shows a live preview of its syntax and can be overridden once with `{{date:FORMAT}}` or `{{time:FORMAT}}`.
- **Unique note creator**: folder for new unique notes; template file; unique prefix format with a live preview.

## Look and feel

- **Themes**: light, dark, high contrast and your own; see [Themes](#themes).
- **Fonts and size**: separate fonts for the interface, the editor and code, a text size, and an interface zoom. Atkinson Hyperlegible is the default interface and editor font and Atkinson Hyperlegible Mono is the default code font. Any font installed on the system can be chosen. Emoji use the system's emoji font; Exhaust bundles none.
- **Accent color**: pick any color; Exhaust adjusts shades so text stays readable.
- **Languages**: interface translations *(later)*.
- **Accessibility**: full keyboard use, scalable text, high contrast and screen reader support, designed in from the first slice.
- **Window**: set the default size of a new Exhaust window.

### Themes

Themes can be changed at any time, in Settings, without restarting. The theme and snippet file formats and every token name are in [`docs/formats.md`](docs/formats.md).

- **Switching**: choose Light, Dark or Follow system. A theme can also be set for each vault.
- **Built in**: Exhaust Dark, which uses the colors of the reference screenshots (navy background, lilac text, purple accent), Exhaust Light, and a high contrast version of each. The exact color values are in the Palette table in [`docs/formats.md`](docs/formats.md#palette); Light and the high contrast themes are derived from it, since no screenshot shows them.
- **Your own**: a theme is a plain text file in the `.exhaust/themes` folder of a vault, or in Exhaust's own settings folder to use it in every vault. It names colors, fonts, sizes, spacing, corner shapes and icons for each part of the interface: the editor, file tree, tabs, sidebars, graph, canvas, callouts and code blocks.
- **Settings editor**: every theme value can also be changed from Settings with live preview and a color picker, so no file editing is needed. Changes you make are saved to a copy of the theme, never to a built-in one.
- **Style snippets**: small extra files in Exhaust's own style format that tweak any part of the interface without replacing the theme. They can be switched on and off one by one.
- **Live reload**: save a theme or snippet file in any text editor and Exhaust updates at once.
- **Contrast check**: Exhaust warns when a color pairing is hard to read.
- **Sharing**: copy a theme file to share it. Nothing is downloaded for you unless you turn on the *(optional)* community directory.
- **Keeping settings**: unknown theme keys are kept and written back unchanged, so themes from newer versions survive a downgrade.

## Extending

- **Plugins**: an open API for new commands, panes, views and file types, written in Lua *(later)*. Plugins are off until you allow them and declare what they can touch. The manifest format, the Lua API and the permission names are in [`docs/formats.md`](docs/formats.md). A community directory *(optional, later)* needs the network.
- **Web clipper** *(later)*: a small extension for your own browser that saves pages into the vault. Exhaust itself has no browser.

## Sync, sharing and publishing *(optional, later)*

- **Sync**: encrypted end-to-end, per-vault, selective by folder, file type and setting. Version history per note, and a log of what synced.
- **Shared vaults**: invite collaborators with owner and editor roles.
- **Publish**: turn chosen notes into a website with themes, custom domains, permalinks, passwords and SEO controls.
- Because plain files are the format, **any** file-sync tool can already sync a vault. These features are a convenience, not a requirement.

## Technical design

Exhaust is written in C (C11). There is no web browser or web view anywhere in it. Other languages are used only where they make a feature far easier or reach parity: assembly only in the vendored lib-Peep, Lua for plugins, Rust for the screen reader bridge, up to 150 lines of Objective-C for the macOS platform layer, and JavaScript only for the web clipper extension that runs in the user's own browser, never inside Exhaust. All dependencies must be compatible with the Unlicense; prefer public domain or permissive libraries, and never add GPL or AGPL code. Each dependency and its license is recorded in the table below; confirm the license when the dependency is added. Dependencies are vendored in `third_party/` (source and headers). The large optional libraries (PDFium and libmpv) are not committed: only their headers are, and Exhaust loads them at run time (`dlopen` or `LoadLibrary`). They ship as a separate optional add-on package built from pinned, checksummed upstream releases. Without them, Exhaust offers "Open externally" for PDFs and media it cannot play itself.

| Area | Choice | License |
| --- | --- | --- |
| Window, input, IME, clipboard | SDL3 | zlib |
| Rendering | SDL_Renderer with a FreeType glyph atlas and Exhaust's own tessellator for lines, curves and circles (used by the graph and canvas) | zlib, Unlicense (own) |
| UI | Own widget tree, with Clay for layout, built around accessibility from the start; colors, sizes and fonts come from theme tokens, never hard-coded | Unlicense (own), zlib (Clay) |
| Accessibility | AccessKit (Rust) behind a small C wrapper; the only Rust in the build | MIT or Apache-2.0 |
| Text | HarfBuzz, FreeType, SheenBidi and libgrapheme; font fallback; system fonts found by scanning the OS font folders | MIT, FreeType License, Apache-2.0, ISC |
| Fonts | Atkinson Hyperlegible and Atkinson Hyperlegible Mono; STIX Two Math for equations | SIL Open Font License 1.1 |
| Text buffer | Piece table with an undo tree | Unlicense (own) |
| Markdown | Own tokenizer with source ranges for live preview; md4c for reading mode and exports | Unlicense (own), MIT |
| Properties | Read with libyaml; edited as text so files are never reformatted | MIT |
| Index | SQLite with FTS5 using the `trigram` tokenizer (so partial words match), plus tables for lines, blocks, sections and tasks; stored in the OS cache folder, keyed by vault; always rebuildable from the notes | Public domain |
| Search regex | PCRE2, run over the files the index returns | BSD |
| File watching | dmon (single-header C), or Exhaust's own inotify, ReadDirectoryChangesW and FSEvents code | BSD-2-Clause, Unlicense (own) |
| Platform layer (`src/platform/`) | Trash (freedesktop spec on Linux, SHFileOperation on Windows, NSFileManager on macOS), file associations, URI registration, cache and config folders | Unlicense (own) |
| JSON | yyjson | MIT |
| Images | stb_image, with WebP and AVIF decoders; nanosvg for SVG | Public domain, BSD, zlib |
| Audio embeds | [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep) 0.5.0, vendored and linked statically: hand-written assembly for x86-64 and ARM64 with no libc, no `malloc` and no threads. Plays 16-bit PCM WAV built in; other WAV depths and float (`wav` codec), compressed WAV (A-law, mu-law, IMA and Microsoft ADPCM; `wavc` codec) and FLAC 4 to 31 bit (`flac` codec) are separate codecs. Pull API in `peep.h`: `peep_open`, `peep_fill`, `peep_fill32`, `peep_seek`, `peep_close`, `peep_set_gain`, `peep_set_dither`, `peep_flac_metadata`; output is interleaved stereo. Size per its README: about 13 KB for WAV, FLAC and every WAV depth (*TODO-verify*: its own table lists `flac.asm` at 17.8 KB on x86-64 and 9.3 KB on ARM64; measure the vendored build). `peep.h` is on the repo's `dev` branch, `main` has the assembly and docs only (vendor it from the `dev` branch, with the assembly from the same commit). Needs `nasm` (x86-64) or the GNU assembler (ARM64) to build; The first task of 6c-ii is a CI build on Linux, Windows and macOS (its README says macOS and ARM hardware are not fully verified); it has no fallback decoder, so a failing platform is fixed in lib-Peep. dr_mp3 and stb_vorbis decode MP3 and OGG, SDL3 handles output, and other formats go through libmpv | Unlicense, public domain or MIT-0, public domain or MIT, zlib |
| Video embeds | libmpv, loaded at run time | LGPL or GPL (never linked) |
| PDF | PDFium, loaded at run time | BSD-3 or Apache-2.0 |
| PDF export | Own PDF writer: lays pages out with Exhaust's reading-mode renderer, embeds font subsets through FreeType, compresses with miniz | Unlicense (own), public domain or MIT |
| Plugins | Lua, sandboxed, with Exhaust's own API | MIT |
| Bases expressions | Own expression engine module: the documented types and all 72 documented functions, tested against the examples in the docs | Unlicense (own) |
| Math | Own LaTeX-subset layout using HarfBuzz's `hb-ot-math` and STIX Two Math | Unlicense (own), OFL 1.1 |
| Diagrams | Own renderers for the Mermaid types listed above | Unlicense (own) |
| Dates | Own formatter for Moment-style date tokens | Unlicense (own) |
| Spell check | Own checker reading Hunspell `.aff`/`.dic` files, supporting `PFX`, `SFX`, `REP` and `KEY` only (no compounding); English (US) included | Unlicense (own); each word list keeps its own license |
| Sync *(later)* | libsodium | ISC |

### Registries

Two registries exist from sub-slice 2d, and every feature uses them from the start, so the settings and hotkey screens only add interface later:

- **Command registry**: each command has an id, a name, a default key and an enabled predicate.
- **Feature-toggle registry**: each built-in feature registers its on/off switch and options.

### Size, safety and quality gates

- **Size budget**: the core install (without the optional PDF and media add-on) is at most 25 MB, checked in CI.
- **Sanitizers**: a CI job builds and tests with AddressSanitizer and UndefinedBehaviorSanitizer.
- **Fuzzing**: libFuzzer targets for the Markdown tokenizer, frontmatter, HTML subset, canvas JSON, base expressions and importers.
- **Hardening**: builds use `-fstack-protector-strong` and `-D_FORTIFY_SOURCE=2` where the compiler supports them.

### Build order

Each sub-slice is finished, tested and committed before the next. The detailed spec, with scope, dependencies and a "done when" check for each, is in [`docs/0.1.0.md`](docs/0.1.0.md).

1. **1a** i window, renderer and text atlas; ii shaping, fonts, bidi, IME and clipboard; iii buffer, undo tree and selections; iv layout, widgets, theme tokens and editor widget. **1b** i AccessKit vendoring and CI; ii C wrapper; iii tree-dump test; iv keyboard navigation.
2. **2a** vaults, file tree and file actions; **2b** file watching, saving and note names; **2c** tabs, splits, sidebars and drag and drop; **2d** settings file, registries and platform layer; **2e** vault manager and `tools/gen_vault`.
3. **3a** tokenizer, callouts and HTML subset; **3b** source mode and live preview; **3c** reading mode and folding; **3d** find, rectangular selection, shortcuts and slash commands; **3e** spell check and word count.
4. **4a** i index schema and indexer; ii links, backlinks and page preview; iii tags and properties; iv link-safe rename. **4b** i search engine and operators; ii saved queries and query blocks; iii quick switcher and command palette; iv outline, bookmarks, random note and footnotes.
5. **5a** Barnes-Hut layout and rendering; **5b** filters, groups, forces and display; **5c** local graph, animation and keyboard route.
6. **6a** canvas. **6b** i expression engine; ii base core; iii layouts. **6c** i images including SVG; ii audio; iii libmpv loader; iv PDFium loader and PDF viewer; v embed syntax. **6d** i math; ii Mermaid flowchart and sequence; iii Mermaid class, state, ER, pie and gantt. **6e** i daily notes, templates and dates; ii unique notes and note composer; iii slides; iv format converter and PDF export; v importers and stubs.
7. **7a** settings framework and Options pages; **7b** built-in feature pages; **7c** hotkeys; **7d** themes and style snippets; **7e** workspaces and file recovery; **7f** status bar, ribbon, high contrast and zoom.
8. **8a** Vim mode; **8b** Lua runtime and plugin permissions; **8c** labeled stubs.
9. **9a** Windows zip; **9b** Linux AppImage; **9c** macOS dmg; **9d** add-on package and final checks.

Exhaust must run on Windows, Linux (Wayland and X11) and macOS, with no display-server-specific code.

## Non-goals

These Obsidian features are deliberately not part of Exhaust:

- A web viewer and embedded web pages (links open in the system browser).
- Mobile apps.
- Accounts, licenses, payments, credits and the Teams offering.
- A hosted Sync or Publish server. Their settings and interface are stubs; any file-sync tool already syncs a vault.
- Annotating PDFs. Obsidian does not support it either.
- Compatibility with Obsidian's JavaScript plugins. Exhaust has its own Lua plugin API.

## Screenshots

The screenshots in [`screenshots`](screenshots) are the skeleton of how Exhaust should look: Exhaust's layout follows them.

- **Ribbon**: a thin bar of utility buttons down the far left.
- **File tree**: next to it, the vault's files as a tree of folders, with the vault button at the bottom.
- **Tabs**: a row of tabs across the top of the main area.
- **Main area**: panes that can each show a note being edited, the graph view, a canvas or a reference note, side by side.
- **Settings**: a searchable list with Options first, then one entry for each built-in feature that can be switched on or off. The 28 pages in [`screenshots/settings`](screenshots/settings) are the guideline for what every settings page looks like and which options it offers; the Settings section above lists them in my own words.
- **Vault manager**: the list of known vaults with a menu for each, plus buttons to create a vault, open a folder as a vault, and (as a stub) open a synced vault. See `screenshots/vault-manager.png`.
- **Unresolved links**: a link to a note that doesn't exist yet shows dimmed in the text and as a faint node in the graph, joined to the note that links to it. Once the note exists, the link becomes underlined, colored text and the node brightens. See `screenshots/unresolved-link.png` and `screenshots/resolved-link.png`.
- **Hotkeys**: one row per command with its shortcut chip (or "Blank"), a plus button to add a shortcut and a cross to remove one.

Exhaust keeps this arrangement but has its own look: the Midori notebook style, its own icons and wording, and the color palette in [`docs/formats.md`](docs/formats.md#palette) (which follows the screenshots' colors), and light and dark themes. The screenshots are a reference for placement, not artwork to copy; they stay in the repository for now, for development. The UI does not use Obsidian's name or logo.

## License

Exhaust's own source code is intended to be released under [the Unlicense](https://unlicense.org).
