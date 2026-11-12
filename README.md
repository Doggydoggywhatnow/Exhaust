# Exhaust 0.0.0

A robust and personable notepad that doesn't get in your way. Notes, journals, knowledge bases and project plans, linked together.

Exhaust's goal is feature parity with Obsidian's documented feature set, in a private, native program. This README is the spec. Features marked *(later)* may be stubs in the first demo. Items marked *(optional)* use the network and are off by default. The technical design is at the end.

## Values:

- **Customizable, but not overwhelmingly**: Exhaust offers settings to change the workflow of the notepad to fit your needs. You can even set the default size of a new exhaust window. Despite this, it is not overloaded with features. We include a robust kit of text editing features, some quality of life things, and allow you to control which features you want and where you want them. As well as what you want everything included to look like.
- **Aesthetic**: The design of Exhaust is meant to be simple and pleasant to the eye. It is inspired by the design of Midori MD notebooks.
- **Simple**: Exhaust provides a simple environment in which to use complex features. And where features present in other apps do not go directly towards text, we will drop those features.
- **Small, but not at the cost of features**: Exhaust is as small as it can be without dropping anything in this README. The program may be as large as it needs to be to fit all of these features.
- **Private**: What you write is your business. Simple as.

## Rules

- Notes are plain Markdown files in a folder you choose. Exhaust never moves, renames or reorganizes them behind your back.
- No network requests, telemetry or accounts unless you turn on an *(optional)* feature.
- Every feature below can be switched off. Built-in features such as Graph, Canvas and Bases are separate toggles in Settings.

## Vaults and files

- **Vaults**: a vault is a folder of Markdown files. A vault button at the bottom of the file tree switches between vaults. Create, open and remember several.
- **File explorer**: a tree of folders and notes. Create, rename, move, delete and sort. Folders shown here are the real folders on disk.
- **Open anything**: Markdown, plain text, images, audio, video, PDFs, canvases and bases. Other file types are left alone.
- **Excluded files**: patterns that hide files from search, links and the graph.
- **Link-safe renames**: renaming or moving a note updates every link to it. It asks first by default; you can make it automatic or off.
- **Symbolic links**: followed, with loops detected.
- **Configuration folder**: settings live in a `.exhaust` folder inside the vault, in plain text.
- **Import**: Markdown and plain text folders, HTML, CSV and Textbundle. Other apps' exports are *(later)*.
- **Backups**: because everything is plain files, any backup tool works.

## Writing

- **Markdown**: headings, bold, italic, strikethrough, highlight, lists, task lists, quotes, code blocks, tables, horizontal rules, footnotes, comments, math, and Mermaid diagrams (the common types).
- **Callouts**: typed, titled and foldable blocks, nestable.
- **Live preview**: formatting shows as you type, with the syntax revealed under the cursor.
- **Source mode and reading mode**: switch per note.
- **Folding** of headings and lists.
- **Find and replace** inside a note, with regular expressions.
- **Multiple cursors**.
- **Editing shortcuts**: the usual text-editing keys, plus smart list continuation and indent.
- **Spell check** (own dictionary checker) and a **word count** in the status bar.
- **Embedded content**: notes, headings, blocks, images, audio, video and PDFs. Web links open in the system browser; Exhaust has no embedded browser.
- **HTML** in notes: a safe subset of tags, drawn by Exhaust itself.
- **Slash commands**: type `/` for a menu of commands.
- **Audio recorder**: record straight into a note *(later)*.
- **Vim mode**, optional.

## Links

- **Wikilinks**: `[[Note]]`, `[[Note|alias]]`, `[[Note#Heading]]`, `[[Note#^block]]`, plus ordinary Markdown links. Suggestions appear as you type.
- **Backlinks**: a panel of notes that point at the open note, including unlinked mentions.
- **Outgoing links**: a panel of everything the open note points at.
- **Aliases**: alternate names for a note that links can use.
- **Page preview**: hover a link to see the note.
- **Tags**: `#tag` and `#nested/tags`, a tag pane, and search by tag.
- **Properties**: typed metadata at the top of a note (text, lists, numbers, checkboxes, dates, links) kept as plain YAML. A properties pane shows every property in the vault.

## Graph

- **Graph view**: notes are circles, links are lines, busier notes are bigger. Hover to highlight neighbours, click to open, right-click for actions, scroll to zoom, drag to pan, arrow keys and `+`/`-` too.
- **Local graph** for the open note, with adjustable depth.
- **Filters and groups**: by search, tag, folder and property, with colors. Toggle orphans, unresolved links and attachments.
- **Forces**: sliders for centering, repulsion, link strength and distance.
- **Animate**: replay the vault growing over time.
- **Keyboard route**: every graph action also works from a list, so nothing depends on a mouse.

## Finding things

- **Search**: full text over the vault with operators for file, path, folder, tag, property, line, section, task and case, plus regular expressions, boolean logic and quotes. Results show context, can be sorted, and can be saved as a query.
- **Quick switcher**: `Ctrl+O`, fuzzy match by name or alias, `Enter` to create a note that doesn't exist.
- **Command palette**: `Ctrl+P`, fuzzy match every command, with pinned commands.
- **Outline**: the headings of the open note, clickable and draggable to reorder sections.
- **Bookmarks**: notes, headings, searches and graphs, in groups.
- **Random note**.
- **Footnotes pane**.

## Organizing and structure

- **Canvas**: an infinite board for notes, images, web links, text cards and groups, joined by labeled arrows. Saved as `.canvas` in the open JSON Canvas format.
- **Bases**: database-style views of your notes and their properties, with filters, sorting, formulas and functions. Table, list, cards, kanban and map layouts. Saved as `.base` files, or embedded in a note.
- **Daily notes**: a note per day from a template, with an optional calendar.
- **Templates**: insert a template with date, time and title variables.
- **Unique note creator**: timestamp-named notes for Zettelkasten work.
- **Note composer**: merge two notes, or split a selection into a new note, with links updated.
- **Slides**: present any note, with `---` between slides.
- **Format converter** between link styles.

## Workspace

- **Tabs, splits and pop-out windows**: drag to rearrange, stack tabs, link panes so they follow each other, pin tabs.
- **Sidebars** left and right, collapsible, with a ribbon of shortcuts.
- **Status bar**.
- **Workspaces**: save and restore whole layouts for journaling, reading or writing.
- **Drag and drop** files and text.
- **Hotkeys**: every command is bindable, with conflicts shown.
- **File recovery**: automatic snapshots (every 5 minutes, kept 7 days by default, both adjustable) with restore and diff.
- **Command line** and a **URI scheme** to open notes, search and create from other programs *(later)*.

## Look and feel

- **Themes**: light, dark, high contrast and your own; see [Themes](#themes).
- **Fonts and size**: separate fonts for the interface, the editor and code, a text size, and an interface zoom.
- **Accent color**: pick any color; Exhaust adjusts shades so text stays readable.
- **Languages**: interface translations *(later)*.
- **Accessibility**: full keyboard use, scalable text, high contrast and screen reader support, designed in from the first slice.
- **Window**: set the default size of a new Exhaust window.

### Themes

Themes can be changed at any time, in Settings, without restarting.

- **Switching**: choose Light, Dark or Follow system. A theme can also be set for each vault.
- **Built in**: Exhaust Light and Exhaust Dark (the Midori notebook look), plus high contrast versions of both.
- **Your own**: a theme is a plain text file in the `.exhaust/themes` folder of a vault, or in Exhaust's own settings folder to use it in every vault. It names colors, fonts, sizes, spacing, corner shapes and icons for each part of the interface: the editor, file tree, tabs, sidebars, graph, canvas, callouts and code blocks.
- **Settings editor**: every theme value can also be changed from Settings with live preview and a color picker, so no file editing is needed. Changes you make are saved to a copy of the theme, never to a built-in one.
- **Style snippets**: small extra files in Exhaust's own style format that tweak any part of the interface without replacing the theme. They can be switched on and off one by one.
- **Live reload**: save a theme or snippet file in any text editor and Exhaust updates at once.
- **Contrast check**: Exhaust warns when a color pairing is hard to read.
- **Sharing**: copy a theme file to share it. Nothing is downloaded for you unless you turn on the *(optional)* community directory.
- **Keeping settings**: unknown theme keys are kept and written back unchanged, so themes from newer versions survive a downgrade.

## Extending

- **Plugins**: an open API for new commands, panes, views and file types, written in Lua *(later)*. Plugins are off until you allow them and declare what they can touch. A community directory *(optional, later)* needs the network.
- **Web clipper** *(later)*: a small extension for your own browser that saves pages into the vault. Exhaust itself has no browser.

## Sync, sharing and publishing *(optional, later)*

- **Sync**: encrypted end-to-end, per-vault, selective by folder, file type and setting. Version history per note, and a log of what synced.
- **Shared vaults**: invite collaborators with owner and editor roles.
- **Publish**: turn chosen notes into a website with themes, custom domains, permalinks, passwords and SEO controls.
- Because plain files are the format, **any** file-sync tool can already sync a vault. These features are a convenience, not a requirement.

## Technical design

Exhaust is written in C (C11). There is no web browser or web view anywhere in it. Other languages are used only where they make a feature far easier or reach parity: Lua for plugins and Rust for the screen reader bridge. All dependencies must be compatible with the Unlicense; prefer public domain or permissive libraries, and never add GPL or AGPL code. Each dependency and its license is recorded in the table below; confirm the license when the dependency is added.

| Area | Choice | License |
| --- | --- | --- |
| Window, input, IME, clipboard | SDL3 | zlib |
| UI | Own widget tree, with Clay for layout, built around accessibility from the start | Unlicense (own), zlib (Clay) |
| Accessibility | AccessKit (Rust) behind a small C wrapper; the only Rust in the build | MIT or Apache-2.0 |
| Text | HarfBuzz, FreeType, SheenBidi and libgrapheme; font fallback and color emoji | MIT, FreeType License, Apache-2.0, ISC |
| Default font | Atkinson Hyperlegible | SIL Open Font License 1.1 |
| Text buffer | Piece table with an undo tree | Unlicense (own) |
| Markdown | Own tokenizer with source ranges for live preview; md4c for reading mode and exports | Unlicense (own), MIT |
| Properties | Read with libyaml; edited as text so files are never reformatted | MIT |
| Index | SQLite with FTS5 for links, tags, properties and search; always rebuildable from the files | Public domain |
| File watching | efsw or per-OS watchers, to notice edits made outside Exhaust | MIT |
| Search regex | PCRE2 | BSD |
| JSON | yyjson | MIT |
| Images | stb_image, with WebP and AVIF decoders | Public domain, BSD |
| Audio and video embeds | lib-Peep and runtime-loaded libmpv, as in THC | Unlicense, LGPL or GPL (loaded at run time, never linked) |
| PDF | PDFium prebuilt binaries | BSD-3 or Apache-2.0 |
| Plugins | Lua, sandboxed, with Exhaust's own API | MIT |
| Math | Own LaTeX-subset renderer | Unlicense (own) |
| Diagrams | Own renderer: flowchart and sequence first, other types later | Unlicense (own) |
| Spell check | Own dictionary checker using Hunspell-format word lists, identical on all three OSes | Unlicense (own); each word list keeps its own license |
| Sync *(later)* | libsodium | ISC |

Build order (finish and commit each slice before the next):

1. Window, text rendering, buffer and accessibility tree, together
2. Vault, tabs, file tree and saving
3. Tokenizer and live preview
4. Index: links, backlinks, tags, properties and search
5. Graph view
6. Canvas, bases, templates and daily notes
7. Settings, hotkeys and themes
8. Plugins, then the optional network features

Exhaust must run on Windows, Linux (Wayland and X11) and macOS, with no display-server-specific code.

## Screenshots

The screenshots in [`screenshots`](screenshots) are the skeleton of how Exhaust should look: Exhaust's layout follows them.

- **Ribbon**: a thin bar of utility buttons down the far left.
- **File tree**: next to it, the vault's files as a tree of folders, with the vault button at the bottom.
- **Tabs**: a row of tabs across the top of the main area.
- **Main area**: panes that can each show a note being edited, the graph view, a canvas or a reference note, side by side.
- **Settings**: a searchable list with Options first, then one entry for each built-in feature that can be switched on or off.

Exhaust keeps this arrangement but has its own look: the Midori notebook style, its own icons, its own colors and wording, and light and dark themes. The screenshots are a reference for placement, not artwork to copy.

## License

Exhaust's own source code is intended to be released under [the Unlicense](https://unlicense.org).
