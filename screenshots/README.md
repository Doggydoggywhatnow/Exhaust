# Screenshots

Reference screenshots of the layout Exhaust aims for.

## Graph view

![Graph view](graph-view.webp)

On the far left, a bar of utilities. Next to it, the files as a tree of the vault's real folders, but sorting and grouping happen inside the app and never change anything on disk. In the middle, a graph view showing the links between notes. On the right, an open note for reference.

## Editing

![Editing](editing.webp)

Everything is the same, except the graph view is now an active note being edited, and the note on the right serves as a reference.

## Vaults

The vault button at the bottom left of the file tree lets the user switch between which set of Markdown files to use.

## Settings

![Settings](settings.png)

The settings menu to follow. A search box sits at the top. Below it, Options: General, Appearance, Interface, Editor, Files and links, Hotkeys, Keychain, Core plugins and Community plugins. Below that, one entry for each core plugin: Backlinks, Canvas, Command palette, Daily notes, File recovery, Note composer, Page preview, Quick switcher, Sync, Templates and Unique note creator.

## Hotkeys

![Hotkeys](hotkeys.png)

The hotkeys list to follow. One row per command, in alphabetical order, with the command name on the left. On the right of each row is a shortcut chip, or "Blank" when none is set, and a plus button to add another shortcut. A chip with a shortcut has a cross to remove it. Examples in the screenshot: Toggle reading view (Ctrl + E), Undo close tab (Ctrl + Shift + T), and many commands with no shortcut, such as Toggle numbered list, the highlight colors, Toggle spellcheck, Toggle stacked tabs, Unique note creator and Zoom in and out.

## Settings pages

Full-page screenshots of 28 settings pages, in `settings/`. **These are the guideline for what Exhaust's settings should look like**: follow their layout, grouping, order and the options they offer, as in the README's Settings section. As with the other screenshots, copy the layout and the list of options, not the artwork or text.

- **Options:** `general-settings.png`, `appearance-settings.png`, `interface-settings.png`, `editor-settings-pt-1.png`, `editor-settings-part-2.png`, `files-and-links-settings-pt-1.png`, `files-and-links-pt-2.png`.
- **Hotkeys:** `hotkeys-1.png` to `hotkeys-9.png`, the complete list across nine pages, in alphabetical order.
- **Built-in features:** `core-plugins-pt-1-settings.png` and `core-plugins-pt-2.png` (the list of features with their switches), then one page each for `backlinks-settings.png`, `canvas-settings.png`, `command-pallete-settings.png`, `daily-notes-settings.png`, `file-recovery-settings.png`, `new-note-creator-settings.png`, `note-composer-settings.png`, `page-preview-settings.png`, `quick-switcher-settings.png` and `template-settings.png`.

## Vault manager

![Vault manager](vault-manager.png)

This is what managing the vault should be like in Exhaust. A list of known vaults sits on the left, each with its name, its folder and a three-dot menu for actions such as rename, move and remove. On the right are the ways to add one: create a new vault under a folder, open an existing folder of Markdown files as a vault, and (in Obsidian) open a vault from its sync service, which in Exhaust is an *(optional, later)* stub. A language choice sits below them.

## Unresolved link

![Unresolved link](unresolved-link.png)

An example of a simple link on the graph view and the text view, without the link being to a fully created file. In the note "Welcome", the text `[[create a link]]` is a wikilink to a note that does not exist yet. In the text view the link is shown dimmed, with its brackets visible while the cursor is on it. In the graph view it appears as a small, faint node labeled "create a link", joined by a line to the "Welcome" node. Creating the note later turns it into a normal node. The status bar at the bottom right shows the backlink count, word count and character count.

## Resolved link

![Resolved link](resolved-link.png)

The same note compared with the unresolved link above: here the link is to an existing file. The note "create a link" now exists and appears in the file tree beside "Welcome". In the text, the link shows as underlined, brightly colored link text without visible brackets, instead of the dimmed `[[create a link]]`. In the graph, its node is brighter than the faint unresolved node, still joined to "Welcome". Compare this with the unresolved link to see how Exhaust should tell the two apart.
