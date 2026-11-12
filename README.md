# Exhaust 0.0.0

A robust and personable notepad that doesn't get in your way. Notes, journals, knowledge bases and project plans, linked together.

Exhaust's goal is feature parity with Obsidian's documented feature set, in a private, native program. This README is the spec. Features marked *(later)* may be stubs in the first demo. Items marked *(optional)* use the network and are off by default.

## Values:

- **Customizable, but not overwhelmingly**: Exhaust offers settings to change the workflow of the notepad to fit your needs. You can even set the default size of a new exhaust window. Despite this, it is not overloaded with features. We include a robust kit of text editing features, some quality of life things, and allow you to control which features you want and where you want them. As well as what you want everything included to look like.
- **Aesthetic**: The design of Exhaust is meant to be simple and pleasant to the eye. It is inspired by the design of Midori MD notebooks.
- **Simple**: Exhaust provides a simple environment in which to use complex features. And where features present in other apps do not go directly towards text, we will drop those features.
- **Small, but not at the cost of features**: Exhaust is as small as it can be without dropping anything in this README. The program may be as large as it needs to be to fit all of these features.
- **Private**: What you write is your business. Simple as.

## Rules

- Notes are plain Markdown files in a folder you choose. Exhaust never moves, renames or reorganizes them behind your back.
- No network requests, telemetry or accounts unless you turn on an *(optional)* feature.
- Every feature below can be switched off. Core plugins are separate toggles in Settings.

## Vaults and files

- **Vaults**: a vault is a folder of Markdown files. A vault button at the bottom of the file tree switches between vaults. Create, open and remember several.
- **File explorer**: a tree of folders and notes. Create, rename, move, delete and sort. Folders shown here are the real folders on disk.
- **Open anything**: Markdown, plain text, images, audio, video, PDFs, canvases and bases. Other file types are left alone.
- **Excluded files**: patterns that hide files from search, links and the graph.
- **Link-safe renames**: renaming or moving a note updates every link to it, or asks first, as you choose.
- **Symbolic links**: followed, with loops detected.
- **Configuration folder**: settings live in a `.exhaust` folder inside the vault, in plain text.
- **Import**: Markdown and plain text folders, HTML, CSV and Textbundle. Other apps' exports are *(later)*.
- **Backups**: because everything is plain files, any backup tool works.

## Writing

- **Markdown**: headings, bold, italic, strikethrough, highlight, lists, task lists, quotes, code blocks, tables, horizontal rules, footnotes, comments, math, and Mermaid diagrams.
- **Callouts**: typed, titled and foldable blocks, nestable.
- **Live preview**: formatting shows as you type, with the syntax revealed under the cursor.
- **Source mode and reading mode**: switch per note.
- **Folding** of headings and lists.
- **Find and replace** inside a note, with regular expressions.
- **Multiple cursors**.
- **Editing shortcuts**: the usual text-editing keys, plus smart list continuation and indent.
- **Spell check** and a **word count** in the status bar.
- **Embedded content**: notes, headings, blocks, images, audio, video, PDFs and web pages.
- **HTML** in notes, displayed safely.
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

- **Canvas**: an infinite board for notes, images, web pages, text cards and groups, joined by labeled arrows. Saved as `.canvas` in the open JSON Canvas format.
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

- **Themes**: light, dark and your own. Accent color, fonts, font size and interface zoom.
- **CSS snippets** to restyle anything, hot reloaded.
- **Languages**: interface translations *(later)*.
- **Accessibility**: full keyboard use, scalable text, high contrast and screen reader support *(later)*.
- **Window**: set the default size of a new Exhaust window.

## Extending

- **Plugins**: an open API for new commands, panes, views and file types, with a community directory *(later)*. Plugins are off until you allow them and declare what they can touch.
- **Web viewer**: open links in a tab, with a reader view and save to vault *(later)*.
- **Web clipper**: save pages into the vault from a browser *(later)*.

## Sync, sharing and publishing *(optional, later)*

- **Sync**: encrypted end-to-end, per-vault, selective by folder, file type and setting. Version history per note, and a log of what synced.
- **Shared vaults**: invite collaborators with owner and editor roles.
- **Publish**: turn chosen notes into a website with themes, custom domains, permalinks, passwords and SEO controls.
- Because plain files are the format, **any** file-sync tool can already sync a vault. These features are a convenience, not a requirement.

## Screenshots

Reference screenshots of the layout and settings Exhaust aims for are in [`screenshots`](screenshots).

## License

Exhaust's own source code is intended to be released under [the Unlicense](https://unlicense.org).
