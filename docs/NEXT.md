# Exhaust: what to build next

Written 2026-11-28 by agent v.08 from `PROGRESS.md`, `ISSUES.md`, `HANDOFF-agent-v.07.md`, the README, `0.1.0.md`, `tests/hunt/README.md` and the CI runs of `4d17c58`, `cb8955f` and `62ad5d3`. It keeps the build rules of `AGENTS.md` and `0.1.0.md` and changes only **the order** (slice 9 moves forward and is split) and **what comes after 0.1.0**. **Nothing here is started.** The user says "go" to a slice and only that slice is built. A slice that needs the owner's yes is marked **owner**.

## 1. Where things stand

- 62 of 67 rows are built, 175 tests, about 66,000 lines of C, a stripped program of about 17 MB (budget 25 MB for the install).
- **Nobody has used most of it.** The user hand-tested slices up to 4d and a few things after. Everything from 5b on (graph options and dragging, canvas, Bases, media, math, Mermaid, PDF export, importers, settings, hotkeys, themes, workspaces, pop-outs, plugins) and all of 2026-11-28 (typing helpers, drawn blocks, the writing area) has been seen only through scripted runs and screenshots.
- **Windows and macOS have only ever run in CI.** The native watchers (7i), the screen-reader adapters (7h), the clipboard, drops, file dialogs, trash and fonts folders there are unseen on a real machine.
- **Two README promises have no test:** warm start under 1 second and memory under 200 MB on the performance vault. `release_checks` (0.1.0.md, 9d) does not exist.
- **Unrecorded until now:** CI's `fuzz_base` job found a heap overflow in `ex_base_run` on `4d17c58` (a repeated entry in a base's `groupOrder`; ISSUES 93, slice T1). The same run's `sanitizers` job ended without finishing its build step (third time: ISSUES 84, 92); `plugins_page` still fails rarely under the sanitizers for a reason nobody has found (ISSUES 92). `cb8955f` was green on all ten jobs; `62ad5d3` was still running when read.
- **The writing area is the product and is the thinnest part:** no right-click menu, no way to pick a spelling suggestion, tab strips that clip when full, fonts that do nothing, lists that do not renumber, tables and properties that cannot be edited where they are drawn (ISSUES 43, 47, 85, 88, 89).
- `tests/hunt/` (17 programs, written to break things) has never been run.

## 2. How the order was chosen

1. **Trust before features.** A crash in a file format and a README promise nobody measured come before anything new.
2. **Put it in the user's hands early.** The user finds the real bugs by hand. A build to try (T2), and installable builds for all three systems (M2) come before the long editor work, so the user's long feedback loops run while the next slice is built.
3. **The writing area before the long tail.** Typing is most of what anyone does. Slices are ordered by how soon a writer hits the gap, then by cost.
4. **Release when it is good to write in, not when the table is empty.** 0.1.0 is M1 to M4. What the README promises but the program only half does goes into 0.2.0; features marked *(later)* or *(optional)* into 0.3.0.
5. **Detail near, sketch far.** M1 to M3 are specified like `0.1.0.md`; M5 and M6 are one line each and are re-planned at the review points (section 8).

## 3. At a glance

| Milestone | What it gives | Slices | Rough size |
| --- | --- | --- | --- |
| **M1 Trusted** | what exists is true: fuzz crash fixed, a build for the user to try, hunt tests run, CI steady, the two promises measured | T1 to T5 | 8 to 12 sittings |
| **M2 Installable** | AppImage, Windows zip and macOS dmg from CI, tried on real machines | I1 to I5 | 6 to 9 |
| **M3 Good to write in** | the editor gaps a writer meets in the first hour | W1 to W9 | 15 to 20 |
| **M4 0.1.0** | add-on package against real PDFium and libmpv, screen reader and IME passes, checks, tag | R1 to R5 | 6 to 8 |
| **M5 0.2.0 Depth** | the README's features, made as good as their wording | D1 to D12 | many |
| **M6 0.3.0 Reach** | the *(later)* and *(optional)* features | X1 to X9 | many |

A sitting is one agent session. Slice 9 of `0.1.0.md` becomes **I1 (9b), I2 (9a), I3 (9c)** and **R1 and R4 (9d)**; its "Done when" lines stay the contract.

## 4. M1 Trusted

### T1. The Bases `groupOrder` overflow (ISSUES 93)
- **Scope:** `src/base/base.c` near line 638. ASan: heap-buffer-overflow, write of 8 bytes, 0 bytes after the 96-byte `ordered` array (12 rows), `base.c:652`, found by `fuzz_base` (crash input `crash-6a130743...`, artifact `fuzz-crashes` of run 46106714545). The code copies a group's rows every time a `groupOrder` entry matches it, so an entry named twice (or `null` twice) copies them twice, `at` passes `nrows`, and then `nrows = at` is used for `rows` as well. Show each group once; read the rest of `ex_base_run` and the layouts for the same shape.
- **Done when:** a new case in the base tests with a repeated entry fails with the fix taken out and passes with it; `fuzz_base` runs 60 s clean; ISSUES 93 is moved to fixed.
- **Size:** S.

### T2. A build to try, round 1 (the user's hand test) **owner**
- **Scope:** a Release AppImage of `dev` (recipe in `AGENTS.md`; the owner has to allow the downloaded `appimagetool`) and a short list for the user, each item "do this, expect that":
  1. New note: Ctrl+N, a name, Enter, a few lines.
  2. Write a page: lists with Enter and Tab, brackets and quotes, a long heading that wraps, scroll with the wheel and the bar.
  3. A table, a `$$` equation, a `mermaid` block, a picture on its own line and the properties, drawn in live preview; click into each.
  4. Drop a file and paste a picture into a note.
  5. Graph: open it, the gear panel, drag a node, the local graph.
  6. A canvas with a card, an arrow and a group; a base with two views.
  7. Settings: search, three pages, Hotkeys, Theme, Plugins; Export to PDF; an import.
  8. Split a pane, open a pop-out window, save a workspace, move a note to the right sidebar.
  9. Open a `tools/gen_vault` vault (5,000 notes): how do start, typing, search and the graph feel?
- **Done when:** the user's findings are numbered in `ISSUES.md` and this plan has had its first review (section 8).
- **Size:** S for the agent, the user's own time for the list. Run T3 to T5 while the user tests.

### T3. Run `tests/hunt/`
- **Scope:** build and run as in `tests/hunt/README.md`, in three sittings: **(a) data safety** (`hunt_buffer`, `hunt_journal`, `hunt_vault`, `hunt_settings`, `hunt_ini`, `hunt_index`, `hunt_import`: the hard rules of `AGENTS.md`), **(b) formats** (`hunt_markdown`, `hunt_unicode`, `hunt_find`, `hunt_extract`, `hunt_html`, `hunt_canvas_yaml`, `hunt_expr`, `hunt_datefmt`), **(c) the rest** (`hunt_plugins`, `hunt_queue` under ThreadSanitizer, `hunt_static.sh`). Release first, then the ASan subset. Each FAIL or SPEC is one of three things (README of the hunt tests): fix the bug in the owning code and copy the case into `tests/`; fix a wrong expectation and say why in the commit; ask the owner when the documents and the code disagree. A bug that can lose typed text or write outside the vault is fixed before the next sitting.
- **Done when:** every hunt test has been run; every FAIL and SPEC is resolved; every SUSPECT is dropped or made a FAIL; findings are in `ISSUES.md` with their seeds; the tests that pass join `ctest` with `HUNT_QUICK=1` so they stay passing.
- **Size:** L (three sittings).

### T4. CI you can trust
- **Scope:** **(a)** ISSUES 92: build `test_plugins_page` with ASan and with TSan and run it 200 times; read `do_enable` and `apply_state` in `src/plugins/plugins.c` for a race between the plugin thread's state and the snapshot the page reads (do not lengthen the wait). **(b)** The `sanitizers` job ends without finishing its build on some pushes (ISSUES 84, 92): build only the tests that gain from it, or split it in two jobs, or use `-g0`; print `df` and memory. **(c)** The test step takes about 14 min on Linux and 23 min on macOS (4 on Windows): find the slowest tests (`graph_frame` 45 s, `graph_perf` 15 s, ISSUES 32) and give them a label so a quick job runs without them. A slice's CI result arrives faster, and every later slice benefits.
- **Done when:** `plugins_page` passes 200 runs in a row under ASan and the cause is written in ISSUES 92; the `sanitizers` job finishes on five pushes in a row; the macOS test step is under 15 min or the reason it cannot be is written down.
- **Size:** M.

### T5. Measure what the README promises
- **Scope:** write `release_checks` now (`0.1.0.md` 9d asks for it and nothing exists): start `exhaust` on the default `tools/gen_vault` vault with a warm cache through `EXHAUST_SCRIPT`, and report time to the first frame, peak memory after a note and the graph are open, CPU used over 5 idle seconds (caret blink and wake-ups), the cold index build time, and the size of a Release install. Run it on a 20,000-note vault too, to learn the slope. Linux gates on the limits (1 s, 200 MB); Windows and macOS only report until their first numbers have been read.
- **Done when:** the numbers are written in `PROGRESS.md`; `ctest -R release_checks` passes on Linux. **If a number is over its limit, a slice that fixes it goes in front of M2:** this is the one place where this plan says stop and re-order.
- **Size:** M.

## 5. M2 Installable (slice 9 of `0.1.0.md`) **owner** (slice 9 needs the user's go)

Packaging is mostly CI plumbing, and it is the cheapest way to learn what Windows and macOS do. Each job builds from a clean checkout, uploads the package as an artifact the user can download, and runs a smoke test.

- **I1. Linux AppImage (9b).** CI job `package-linux`: the manual recipe made repeatable, built on an older Ubuntu so it runs on older glibc; a `.desktop` file with the file types and no change of the default application (`ex_platform_integration_set` never sets one); the smoke test runs it with the dummy video driver, opens a vault and quits. Also run once on Wayland and on X11 by hand. **Done when:** 0.1.0.md 9b. S to M.
- **I2. Windows zip (9a).** Job `package-windows`: check that the program is a GUI program with no console window (1a-i noted a console would show; no `WIN32` or subsystem setting was found in `CMakeLists.txt`), a manifest for high DPI and UTF-8, the file association through the platform layer, the smoke test. Unsigned (SmartScreen will warn; the README says so). **Done when:** 0.1.0.md 9a. M.
- **I3. macOS dmg (9c).** Job `package-macos`: an `.app` bundle with `Info.plist` (document types; the `exhaust:` scheme only if decision 3 says so), Apple Silicon only unless decision 4 changes that (lib-Peep's x86-64 code is ELF only and CMake refuses Intel Macs), unsigned (Gatekeeper will warn). **Done when:** 0.1.0.md 9c. M.
- **I4. A build to try, round 2: Windows and macOS (the user's hand test).** One list of the things only a real system shows: a change made outside is noticed within a second (watchers), drop a file and paste a picture and rich text, the folder and file dialogs, delete to the system trash, the system fonts and an emoji, a high-DPI screen, Ctrl against Cmd, typing with an IME, the screen reader's first words (Narrator or NVDA; VoiceOver). **Done when:** findings are in `ISSUES.md`.
- **I5. Platform fix round.** Fix what I4 found, starting with ISSUES 54 (the first run of the native watchers) and lib-Peep on Windows and macOS (ISSUES 61). Sized by the findings; a bug in lib-Peep is fixed there and given back (AGENTS.md).

## 6. M3 Good to write in

Ordered by how soon a writer meets the gap. Each slice adds a case to `long_names` for any new screen, a case to `frame_budget` for anything that can be big, and updates the accessibility dump when widgets change. Anything that changes how the text looks is shown to the user as a screenshot first (ISSUES 89 was not).

| Id | Slice | Scope | Done when | Size |
| --- | --- | --- | --- | --- |
| W1 | **Headings and rows** | A heading that wraps to a second row is too tight (ISSUES 89); a heading whose status changes by an edit of another line (a fence opened above it) keeps its old rows; the placeholder and scroll bar leftovers of ISSUES 89. | `test_wrap` cases for a wrapped level 1 to 3 heading and for the fence case; the user sees a screenshot | S |
| W2 | **Tabs that cope with many tabs** | A full tab strip clips (ISSUES 43): scroll it, shrink tabs to a minimum, a chevron menu of the hidden ones, the active tab always in view, a keyboard route (switch tab by number and by menu). | `workspace_layout_panes` and `long_names` with 30 tabs in a 400 px pane; accessibility dump lists every tab | M |
| W3 | **The editor's context menu and spelling suggestions** | A right-click menu in the editor, the reading view and the title: cut, copy, paste, paste as plain text, select all; spelling suggestions, *Add to dictionary*, *Ignore*; for a link: open, open in a new tab, copy; reached from the keyboard too (Menu key, Shift+F10). The dictionary lacks "callout" and similar. | scripted right-click test; the menu is in the accessibility tree; `spell_editor` has the pick case | M |
| W4 | **Lists and typing leftovers** (ISSUES 85) | Renumber a numbered list after Enter, indent, outdent and delete; Enter with several cursors; `'` rules; the caret after an unclosed fence is code; an IME commit is never paired. | `smart_typing` cases that fail without the change | S to M |
| W5 | **Tables you can edit where they are drawn** (ISSUES 88) | Click into a drawn table and type in a cell; Tab, Shift+Tab and Enter move between cells (a new row at the end); a find match or selection is highlighted over a drawn block; Up and Down pass through a block line by line; line numbers for a block's lines. The text on disk changes only by the edit. | `live_blocks` and `app_scripts` cases for each; `frame_budget` 750 blocks still inside its limits | L |
| W6 | **Properties you can use** (ISSUES 88; README Properties) | Typed editing of a property in live preview (text, list as chips, number, checkbox, date, date and time, link) with a small calendar widget (D7 reuses it); add and remove a property; suggest keys and values from the vault; show the properties in reading mode. YAML is edited as text and never reformatted (hard rule). | `props` and `live_blocks` cases; `hunt_extract` stays green | L |
| W7 | **Fonts that work** (ISSUES 47) | `font_size` applies live (today only at start); the three font settings and the theme's `fonts.*` tokens reach the editor, the reading view, the widgets and code (system fonts, fallback, metrics, shaping cache keyed by font); *Quick font size* with Ctrl+wheel. Remove the *Not in this version* rows as each works. | a test that changes each setting and checks the next frame's glyph metrics; `frame_budget` unchanged; the README's "any font installed on the system" is true | L |
| W8 | **Links the way you ask** (ISSUES 47, 83) | `use_wikilinks`, `link_format` (shortest, relative, absolute) honoured everywhere a link is written (the `[[` list, drops, pastes, the composer, the Backlinks *Link* button); `always_focus_new_tabs`; `delete_attachments`; bind *Insert attachment* with a file dialog. | `link_format` test over every writer; `app_scripts` cases | M |
| W9 | **View state that should stay** | Find in reading mode (the README says it is missing); folds saved in the session (3c said "a later version may add a key": add it to `formats.md` first); a callout's fold state. | `find_reading`; `session_restore` with folds | M |

## 7. M4 0.1.0

| Id | Slice | Scope | Done when |
| --- | --- | --- | --- |
| R1 | **Add-on package and the real libraries (9d, first half) owner** | CI builds PDFium and libmpv from pinned, checksummed upstream releases and uploads the package; `pdf_viewer`, `pdf_private` and `mpv_loader` run against the real libraries there; the owner installs it on top of a build and checks a PDF (and that it makes no request) and a video by hand. Then the fix round for ISSUES 62 and 63: both were written from headers and tried only against stand-ins written by the same hand. | the three tests pass against the real libraries on all three systems; the owner's check is recorded |
| R2 | **Screen reader pass (owner and agent)** | The user reads the program with Orca, NVDA and VoiceOver from a short script the agent writes; fix round for ISSUES 53 and 75: rows that can be pressed, set text and scroll routed, the quadratic node search. | the maintainer checklist line is ticked for each system |
| R3 | **Input method and clipboard pass (owner and agent)** | Type Chinese, Japanese and Korean with an IME in the editor and the title on each system; copy and paste with other programs (text, rich text, pictures); fix round. | the checklist line is ticked; `smart_typing` has the IME-commit case |
| R4 | **Release checks and the facts (9d, second half)** | `release_checks` becomes a gate on all three systems; the install size is written down (the last figure, 15,020 KB, is from before 2026-11-28); dependency licenses confirmed in the README table; the version moves off 0.0.0 (`CMakeLists.txt`, README title); `docs/0.1.0.md` brought up to date (it was not touched in v.07); the known gaps become a list in the release notes. | 0.1.0.md 9d; `tools/check_size.sh` under 25 MB |
| R5 | **Tag v0.1.0** | The checklist in `0.1.0.md` ticked; a pull request from `dev` to `main` (only the owner merges; never push to `main`). | tag exists; the pull request is open |

## 8. Review points

The plan is wrong where it guesses. Stop and re-plan at: **after T2** (the user's first list changes the order of M3), **after T5** (a missed budget goes first), **after I4** (a bad platform day moves I5 up), **after M3** (what was learned about the editor decides D and X), **after R5**. Re-planning is editing this file in the same commit as the slice that caused it.

## 9. M5 0.2.0 Depth

The README's features, made as good as their wording. Sketches; each gets a full spec at the review after M4. Roughly in the order a user meets the gap.

| Id | Slice | Sketch (ISSUES) |
| --- | --- | --- |
| D1 | PDF export that prints every script | fallback fonts for CJK, emoji, Arabic and Hebrew, HarfBuzz shaping and kerning, an outline, internal links, page numbers, repeated table header (72, 73); a multilingual note exports with no `?` |
| D2 | Bases without YAML | Sort, Group, Filter and Properties menus, a search box, *New* with the view's defaults, drag to reorder views and columns, resize columns, editable cells, keep YAML comments by editing the text in place (58, 59) |
| D3 | Embeds that show their content | a PDF page or a video frame inside the note, an audio position bar in the note, list embeds, nested quoting; optional external images stay off by default (64) |
| D4 | Living in a synced folder | the README says any file-sync tool syncs a vault: ignore `.sync-conflict`, `.tmp` and `~` files, conflict copies, a note replaced by a sync tool mid-edit, tested with a tool that renames over files; journal and session stay out of the vault (already) |
| D5 | Math and diagrams in a paragraph | inline math drawn in live preview with a line that grows (66); equation numbers, `align`, `\tag`; Mermaid curved links, `classDef`, `style`, `linkStyle`, `title`, gantt date formats and weekends (67, 68) |
| D6 | Plugins grow up | read the index, links and properties; `vault.rename` that updates links (77); shortcuts for plugin commands; panes saved with the session (78); plugin settings pages; views and file types as the README promises; two example plugins; every function in `formats.md` first with its permission test |
| D7 | Daily notes calendar and pickers | the calendar view the README lists, `{{date}}` offsets, a searchable template picker shared with the composer's "note to merge into" (69, 70), reusing W6's date widget |
| D8 | Canvas to the end | export as image, group background, follow a card's `subpath`, Markdown in text cards, the ISSUES 56 list |
| D9 | Importers you can trust | a preview of what an import will do, an undo (a list of what it made), a single-file chooser, encodings (74); then the *(later)* importers by demand (Evernote, Notion, Google Keep, Apple Notes) |
| D10 | Panes and pop-outs to parity | pop-outs with the pane menu, find bar, dialogs and status bar, tabs between windows (52); stacking views vertically (GitHub issue 1); *Move to right sidebar* in the tab menu (44); the main window's place restored |
| D11 | A design pass | every screen in light, dark and both high-contrast themes at 100, 150 and 200 percent and at narrow widths against `screenshots/`; the Midori notebook look the README names; icons from SVG (GitHub issue 3); the light theme of ISSUES 33 and 38 |
| D12 | Scale | a 50,000-note vault: start, search that is bounded (18), graph layout and draw, the status bar's word count on a worker (51); only if T5's slope says it matters, otherwise later |

## 10. M6 0.3.0 Reach

Things the README marks *(later)* or *(optional)*. Each is a feature of its own; the order is by value. Every network feature is off by default and listed by name in `stubs_disabled`.

| Id | Slice | Sketch |
| --- | --- | --- |
| X1 | Command line and `exhaust://` | open a note, search, create; **a link that would write a note asks first** (hard rule: notes are written only by a user action) |
| X2 | Languages | a strings table and a `language` setting, English plus one translation, right-to-left layout and caret (`rtl`), more spelling dictionaries |
| X3 | Vim mode (8a) | key-sequence tests, off by default, no added latency for normal typing |
| X4 | Web clipper (`extras/clipper/`) | a JavaScript extension for the user's own browser; how it reaches the vault needs a design (a URI from X1, or a file written to a folder) |
| X5 | Audio recorder | record into a note; microphone permission on macOS; WAV written by our own code |
| X6 | The window shell | window frame styles, native menus, translucency on macOS, settings in a new window, a custom program icon, the slow-start notice, hardware acceleration |
| X7 | Optional network features | manual update check, the community directory, external images, map tiles for Bases, each behind its own switch with a test that allows exactly those calls |
| X8 | Emoji and scripts | COLR v1 color emoji (not drawn today), complex-script checks |
| X9 | Signing and notarization | Windows code signing, macOS signing and notarization; needs the owner's certificates |

## 11. Decisions for the owner

Each has a recommendation; the plan assumes it until told otherwise.

1. **When to cut 0.1.0.** After M3 (recommended: the editor gaps are what a new user meets first), or after M2 (a rougher release sooner: move M4 up; M3 becomes 0.1.1).
2. **Start slice 9 (I1 to I3) now?** Recommended yes, right after T2: it is plumbing, and it is the only way to learn about Windows and macOS.
3. **`exhaust://` registration.** `0.1.0.md` 9a to 9c say "file association and URI registration", but 8c made the URI a stub that does nothing. Recommended: register file types now (no change of the default application), register the scheme only when X1 exists.
4. **Intel Macs.** lib-Peep's x86-64 code is ELF only, so CMake refuses them. Recommended: Apple Silicon only in 0.1.0, written in the release notes; fix lib-Peep's Mach-O build later.
5. **Fonts before 0.1.0 (W7).** The README says any installed font can be chosen. Recommended: yes, W7 is in M3.

## 12. Left out on purpose

The README's **Non-goals** (web viewer, mobile, accounts and licenses, a hosted Sync or Publish server, PDF annotation, Obsidian's JavaScript plugins) and **Sync, shared vaults and publishing**, which stay labelled stubs: any file-sync tool already syncs a vault, and D4 makes that claim true. Nothing here adds a web view, a JavaScript engine in Exhaust, or a network request that is on by default.

## 13. Rules that do not change

`AGENTS.md` stands: C only, one slice at a time, a test that is seen to fail first, ASan and UBSan with leak checking before a push, CI green on all three systems before the next slice, `formats.md` before a new name, the 16 ms input and frame budgets, nothing typed is ever lost, nothing in a note is rewritten that the user did not edit. Each slice gets a row in `PROGRESS.md` when it starts and its honest gaps when it ends.
