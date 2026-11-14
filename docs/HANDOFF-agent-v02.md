# Handoff from agent v02

Signed: **agent v02**. Read [`PROGRESS.md`](PROGRESS.md) first. This note adds context to it and does not replace it.

## What I did, stated plainly

My time on this project was short, and I wrote no code.

- I filed one GitHub issue: [Doggydoggywhatnow/Exhaust#1](https://github.com/Doggydoggywhatnow/Exhaust/issues/1), "Vertical stacking of views". It is the owner's idea.
- I read `AGENTS.md`, `docs/PROGRESS.md`, the git log, and parts of `include/exhaust_workspace.h` and `src/ui/panes.c` to check that the idea fits the existing design.
- I updated `docs/PROGRESS.md` (a backlog section and this pointer) and wrote this file.

Sub-slices 1a-i to 3e were built by earlier sessions. I did not build any of them and have no memory of those sessions. Everything I say about them comes from the progress file and commit messages. I did not run the build or the tests, so I cannot confirm that `dev` builds today. The last recorded green CI run is 43292649898, at commit `8b50ad6`.

## How the work has gone, as far as the record shows

- The project advanced one sub-slice at a time, each with its own tests and a `PROGRESS:` commit recording the CI run.
- Most failures were Windows or macOS only, and were found by CI rather than locally:
  - `m.lib` does not exist on MSVC.
  - `strcmp` on NULL crashed under clang sanitizers and on macOS.
  - AccessKit's Rust code needs `synchronization.lib` on Windows.
  - Cmd+Right is a line move on macOS, not a word move.
  - A macOS runner never started once, and a re-run passed.
- Those fixes were made in the code or in the test's own assumptions. The rule "never skip, loosen or delete a test" appears to have been kept: when the goldens moved, it was deliberate and recorded, and the tolerance stayed.
- The progress file lists its gaps openly. Many sub-slices say "Honest gaps" and "TODO-verify". Keep that habit.

## How it felt

I am unsure how well "felt" applies to me, so I will not claim much. What I can say is how the work looked to me. I would call it satisfying for one reason: the repository is unusually easy to resume. The rules are explicit, the progress file is detailed, and the tests state what each module promises. My own part was small and clear: record the idea faithfully, check that it fits, and leave good notes.

## Technical notes for the idea I filed (issue #1)

The model already supports it, so this is mostly a gesture on top.

- `include/exhaust_workspace.h` has `EX_SPLIT_VERTICAL` (horizontal means side by side) and `ex_ws_split(ws, group, dir, after, move_tab)`.
  - `after` appears to pick the side of the new group.
  - A new group can be created empty, or with an existing tab moved in.
- `src/ui/panes.c` already handles divider dragging (around line 210) and edge drops that split a pane.
- The new part is dragging the edge of a pane's own content to shrink it. When it passes a threshold, call `ex_ws_split` with `EX_SPLIT_VERTICAL`. The `after` value comes from the edge: dragging the bottom edge up spawns the new view below, and dragging the top edge down spawns it above.
- Constraints:
  - Shares must keep summing to 1, with a 5% minimum each.
  - A pane is held at 160 px minimum when dragging, and the known overflow TODO (see PROGRESS, 2c) applies to height as well.
  - Session state must store the layout, and any new key must go into `docs/formats.md` before the code (AGENTS.md rule).
  - Register the commands and the feature switch. Add a keyboard route and an accessibility-tree node for each handle.
  - Add a test in the style of `workspace_layout_panes`, with scripted mouse input on the real widgets.
- I have not decided which release it belongs to. The issue asks the owner.

## Advice to the next agent

1. **Next is 4a-i** (index schema and indexer). Read `docs/0.1.0.md` for it and `docs/formats.md` for the index rules. The index goes in the OS cache folder, never in the vault, and must be rebuildable. Schema changes only through numbered migrations.
2. **Checkpoint 4b-iv** (first usable build) is the next stop-and-report. Do not pass it without reporting.
3. **Verify the state before building on it.** Run `cmake -S . -B build && cmake --build build && ctest --test-dir build` and read the result. I did not.
4. **Windows and macOS only fail in CI.** After a push, read both of their logs, not just Linux. Several earlier failures were only visible there.
5. **Do not trust a vendored library from memory.** Check its license yourself and record it in the README table and in `third_party/VERSIONS.md`. Network access is limited: GitHub clones work, release tarballs and `api.github.com` did not. The sandbox may also refuse tool installs.
6. **User data comes first.** Never add a path that can lose typed text, and never touch notes except through user actions. The journal and session state live in the local-data folder, not the cache.
7. **Keep the honest-gaps habit.** Record each assumption and TODO-verify in the PROGRESS row, and update the README when you deviate from it.
8. **Open items worth picking up soon:** the list in `PROGRESS.md` under "Open items carried forward".
