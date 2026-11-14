# Exhaust: issues

Problems noticed while building and testing, in the order they were noticed. Add an entry as soon as something is seen, with what was observed, who or what noticed it, and (when known) the cause. Move an entry to **Fixed** with the commit when it is done. A fix does not need its own sub-slice; it can go in with the sub-slice being built, and is then named in that row of [`PROGRESS.md`](PROGRESS.md).

Status: `open`, `diagnosed` (cause found, not fixed), `fixed`.

## Open

Found by hand-testing the AppImage built from `dev` after sub-slice 4d (2026-11-14).

| # | Status | Observed | Cause, or what to check |
| --- | --- | --- | --- |
| 1 | diagnosed | Resizing the window is laggy. | The main loop handles one event and then draws a whole frame; presenting a frame waits for the screen (about 16 ms), so the hundreds of resize and mouse-motion events a second queue up and the window falls seconds behind. Fix: handle every waiting event, then draw once (measured with a frame timer: layout 0.1 ms, draw under 1 ms, present about 16 ms when idle). |
| 2 | diagnosed | A new file takes about a second to show in the left panel. | A new note is not written until the autosave idle time (1 s) has passed after the first edit, and the explorer only learns of it from the file watcher after that. Fix: write a new note at its first edit and rescan the tree when the first save of a note the tree does not know finishes. |
| 3 | diagnosed | Input takes about a second to work on most things, most of all a right click on a file in the left panel. | The same event backlog as 1: the click waits behind the queued mouse-motion frames. |
| 4 | open | The Enter key is sometimes slow to start a new line while typing. | Probably the same backlog (1); if it remains after that is fixed, profile Enter in a long note (live-preview re-tokenizing, outline and footnote jobs copy the whole text 300 ms after an edit). |
| 5 | diagnosed | Typing `[[` shows no popup of notes to link to (the feature of 4c). | `sync_ui` calls `ex_editor_set_link_suggest` on every frame and that function closes the popup (`link_open = 0`), so the popup is shut a frame after it opens. Fix: do nothing when the function and its user data are unchanged, as `ex_editor_set_slash` does. The headless tests did not catch it because they never run a frame between typing and Enter; add a test that does. |
| 6 | open | A link to a note that does not exist yet cannot be turned into the file from the editor. | Only the Outgoing links panel creates the missing note. Needed: a *Create "Name"* row in the `[[` popup when nothing matches, and following an unresolved link in the editor, reading view or live preview (click, `Ctrl+Enter`) creates and opens the note, as the README says. |
| 7 | open | The buttons at the very left (the ribbon) "work very oddly", and it is not clear what the **Md** button is for. | The ribbon is still the four text buttons of the first window: **Aa** switches light and dark, the two arrows show or hide the sidebars, **Md** switches the open note between live preview and source mode. They have no icons, no tooltips or state, and none of the ribbon actions the README and the screenshots show (quick switcher, graph, canvas, command palette, settings). Needed: drawn icons (`ex_widget_set_icon`), a pressed state for the mode and sidebar buttons, tooltips, and the README's ribbon entries (the later ones as disabled, labelled entries). |
| 8 | open | Missing feature: a note has no title at the top. In Obsidian every note shows its name as a large heading above the text, and typing in it names the file ([`screenshots/new-note.png`](../screenshots/new-note.png): a new note with only the title written). Also, headings written in the text (`# Heading`) were hardly larger than other text; Obsidian's sizes are 2, 1.6, 1.37, 1.25, 1.12 and 1 times. | Add the inline title: the note's name (without `.md`) as a large heading above the editor, reading view and live preview, which is not part of the file's text. Editing it renames the file through the link-safe rename (with the `files.link_update` question); an empty or invalid name is refused with a message; a new note starts as "Untitled" with the title focused, and the first typed title becomes the file name. A setting hides it (the README lists "inline title" under Editor, Display). Heading sizes: set to Obsidian's in the working tree, not yet committed. The first line of the text is never rewritten as `# Heading` by the app. |

## Fixed

Nothing yet.
