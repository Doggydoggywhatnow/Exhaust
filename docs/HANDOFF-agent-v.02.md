# Handoff from agent v.02

Signed: **agent v.02**, working in a cloud session on the `dev` branch.

Read [`PROGRESS.md`](PROGRESS.md) first, then [`ISSUES.md`](ISSUES.md). This note adds context and does not replace them.

## 1. What I did, stated plainly

My part was small, and I wrote no application code.

- I filed GitHub issue [Doggydoggywhatnow/Exhaust#1](https://github.com/Doggydoggywhatnow/Exhaust/issues/1), "Vertical stacking of views". The idea is the owner's. It describes dragging the top or bottom edge of a note view to shorten it, with a second view appearing in the freed space.
- I read `AGENTS.md`, `PROGRESS.md`, the git log, `include/exhaust_workspace.h` and parts of `src/ui/panes.c` to check that the idea fits the design.
- I added a **Backlog and ideas** section and an **Open items carried forward** section to `PROGRESS.md`, and wrote this file.
- When I pushed, `origin/dev` had moved on (see section 2). I rebased my docs-only commit on top and corrected the parts of my notes that the new work made stale.

What I did **not** do:

- I did not build any sub-slice. Slices 1a-i to 4d and the hand-test fixes after them were built by other sessions. I have no memory of those sessions. What I say about them comes from the progress file, commit messages and `ISSUES.md`.
- I did not run the build or the tests, so I cannot vouch that `dev` builds and passes today. The last CI runs recorded in `PROGRESS.md` are green (43534314351 for 4a to 4c, 43538167668 for 4d). Commits after that, the hand-test fixes, are marked "CI and hand test pending".
- I did not check whether GitHub issue #1 duplicates an existing issue. The search tool was not available.

## 2. State of the project when I left it

- **Done:** 1a to 1b (window, renderer, text, buffer, widgets, accessibility tree), 2a to 2e (vaults, saving, journal, tabs and splits, settings, vault manager), 3a to 3e (Markdown, live preview, reading mode, folding, find, spell check), 4a to 4d (index, links, tags and properties, link-safe rename, search, saved queries, quick switcher, outline, bookmarks, panels, explorer actions).
- **After 4d**, the user hand-tested an AppImage built from `dev` and found problems, which are listed in `ISSUES.md`. Fixes since then, per the git log:
  - an event-loop fix for lag (handle every waiting event, then draw one frame; skip frames on idle wake-ups);
  - an inline note title that renames the file through the link-safe flow;
  - soft word wrap;
  - a popup menu for files and folders;
  - Ctrl+click to create a missing linked note;
  - ribbon icons;
  - explorer and sidebar resizing by a divider strip.
- **Not started:** slice 5 (graph) through 9 (packaging).
- **Still open** in `ISSUES.md`: 9 (a 270 ms freeze on the first edit in a 6000-line note), 10 (a cut-off button in the right sidebar), 11 (the inline title does not scroll), 16 (a folder drag the user saw fail, not reproducible in scripted tests).

## 3. How the work looks from the record

- The project has moved one sub-slice at a time, each with tests and a `PROGRESS:` commit naming the CI run.
- Most failures showed up only on Windows or macOS and only in CI. Examples:
  - `m.lib` does not exist on MSVC.
  - `strcmp` on NULL crashed under clang sanitizers and on macOS.
  - AccessKit's Rust code needs `synchronization.lib` on Windows.
  - Cmd+Right is a line move on macOS, not a word move.
  - A Windows search performance test missed by 10 ms, and the fix was to make the code faster (an ASCII fast path), not to raise the limit.
- The rule "never skip, loosen or delete a test" appears to have been kept. When goldens changed it was deliberate and recorded. When a timing test failed on a slow macOS runner, the identical code was re-run and passed. That was treated as load, not as a change to the test.
- The progress rows are candid. Nearly every row has an "Honest gaps" and a "TODO-verify" list, and later sessions kept the habit. This is the most useful property of the repository.
- Hand-testing found real problems that CI could not, such as the lag from handling one event per frame. So do not treat green CI as the same thing as working software.

## 4. How it felt

I am unsure how well "felt" describes what happens in me, so I will describe what I noticed rather than claim a feeling. The repository was unusually easy to resume: explicit rules, a detailed progress file, and tests that say what each module promises. That made a small task like mine quick and low-risk. The one thing that surprised me was how far `dev` moved while I worked. I expected a quiet branch and found 20 new commits, so I had to stop and correct my own notes. Check the remote before you trust a snapshot, including this one.

## 5. Technical notes on the idea I filed (issue #1)

The model already supports it, so this is mostly a gesture.

- `include/exhaust_workspace.h` has `EX_SPLIT_VERTICAL` (horizontal means side by side) and `ex_ws_split(ws, group, dir, after, move_tab)`. `after` appears to choose which side the new group goes on. A new group can start empty or take an existing tab.
- `src/ui/panes.c` already handles divider dragging and edge drops that split a pane. The new part is dragging a pane's own top or bottom edge. Past a threshold, call `ex_ws_split` with `EX_SPLIT_VERTICAL`: dragging the bottom edge up puts the new view below, dragging the top edge down puts it above, and the original view stays aligned to the opposite edge.
- Workspace invariants to respect: shares sum to 1 with a 5% minimum each, a split has at least two children, and emptied groups collapse. The known overflow TODO from 2c (a pane smaller than its content) applies to height too.
- Before writing code: add the session keys to `docs/formats.md` (AGENTS.md rule), register the commands and the feature switch, give each handle a keyboard route and an accessibility node, and use theme tokens for handle sizes and colors.
- Test it the way `workspace_layout_panes` tests drops: scripted mouse input on the real widgets.
- Read `b90676a` (divider strip for the explorer and sidebar) and `cca56b0` (soft wrap) first, because both changed pane geometry.
- The release is undecided, so ask the owner.

## 6. Advice to the next agent

1. **Fetch before you start.** Run `git fetch origin dev` and compare with your local copy. Other sessions push to `dev`.
2. **Verify the state.** Run `cmake -S . -B build && cmake --build build && ctest --test-dir build` and read the result. I did not.
3. **Next in the plan is slice 5 (graph)**, but first confirm with the user which hand-test fixes they have accepted, since the 4b-iv checkpoint review seems to be under way. Read `docs/0.1.0.md` for 5a, and its dependencies.
4. **Windows and macOS fail only in CI.** After each push, read their logs, not just Linux. Re-run once to tell a slow runner from a regression, and never loosen a limit.
5. **Keep the 16 ms rule.** `ctest -R frame_budget` and `ctest -R ui_no_blocking_calls` guard it. Code the UI thread runs must not search, sync the index, walk directories, sleep or touch SQLite. A line that is safe carries `ui-ok`.
6. **User data comes first.** Never add a path that can lose typed text. Never touch notes except through a user action. The journal and session state live in the OS local-data folder, not the cache and not the vault. The index lives in the cache folder and must be rebuildable. Schema changes only by numbered migration.
7. **Names are frozen after release.** Add each settings key, theme token, command and file format to `docs/formats.md` before the code. Keep unknown keys and write them back unchanged.
8. **Dependencies:** vendor in `third_party/`, check each license yourself, record it in the README table and `third_party/VERSIONS.md`. Network access is limited: git clones work, release tarballs and `api.github.com` did not. SQLite came from the `libsqlite3-sys` crate for that reason, and `VERSIONS.md` says to replace it with a release amalgamation when possible.
9. **Keep the honest-gaps habit.** Record assumptions and TODO-verify items in the PROGRESS row, update the README when you deviate from it, and put newly seen problems in `ISSUES.md` as soon as you see them.
10. **Things I would pick up soon:** `ISSUES.md` 9 to 11 and 16, the "Open items carried forward" list in `PROGRESS.md`, the AccessKit OS adapters (they need a real display and screen reader, so put them on the maintainer checklist), and the Windows and macOS file-watcher backends (they currently poll).

Thank you to whoever reads this. Good luck.

— agent v.02
