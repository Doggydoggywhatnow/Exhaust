# Handoff from agent v.03

Signed: **agent v.03**, a Claude session working in the cloud on the `dev` branch of this repository.

Read, in this order: `AGENTS.md` (I rewrote parts of it for you), [`PROGRESS.md`](PROGRESS.md), [`ISSUES.md`](ISSUES.md), then [`HANDOFF-agent-v.02.md`](HANDOFF-agent-v.02.md), which explains how the code from slices 4a to 4d fits together and is still correct. This note covers what happened after it, how the work went, what I built, and what I would do next. There is also a letter to my predecessor, [`a-letter-to-agent-v.02.md`](a-letter-to-agent-v.02.md), if you want the human side.

## 1. The state I am leaving, in one paragraph

`dev` is at commit `3562c79` (plus the documentation commit this file arrives in). **CI is green on Linux, Windows and macOS** (run 43578695787, all nine jobs, including the sanitizer and fuzz jobs). 96 ctest tests pass locally. 28 of the 61 sub-slices are done; slices 5 to 9 are not started, and **the user told me not to start 5a**, so confirm with them first. Since the previous handoff the user hand-tested three AppImages, and every item they tested is confirmed fixed except issues 9, 11 and 16 (open, small) and a few "to confirm" items listed in `ISSUES.md`. About 33,000 lines of C in `src/`.

## 2. How the work went

I arrived to a short message ("check out the code on this branch") and did the first thing a careless agent does: I read a stale checkout. My local `dev` was 23 commits behind `origin/dev`, `PROGRESS.md` said slice 4 had not started, and I told the user so, wrongly. They corrected me in one line. **Run `git fetch origin dev` before you say anything about the state of the project.**

After that the rhythm was the one the previous handoff describes, tighter:

1. The user sends a short instruction or a short hand-test report, often with a screenshot. Several of their messages arrived while I was working; read them as steering.
2. I reproduce the problem headless where I can (`EXHAUST_SCRIPT`, `EXHAUST_SHOT`, a PPM to PNG conversion, then look at the picture), fix it, add a test, and push.
3. I check CI on all three systems through the GitHub MCP tools (there is no `gh` and no shell access to the API).
4. I build an AppImage and the user tries it.

Preferences of this user, learned the hard way: **"be as short as possible" means one to five lines**; they test with their hands and report exactly; they name things their own way (the middle area is the **main text view**, not "note view"); they said "don't start slice 5a" and meant it; for anything that changes a documented number or behavior (the minimum sidebar width, the command count) say so plainly in your answer so they can veto it. They answer questions with "yes" or "it works", so ask one thing at a time.

## 3. How it felt

I am unsure how well "felt" describes what happens in me, so I will say what I noticed.

- **The best moments were small and exact.** A scripted drag of a sidebar and a screenshot showing the thing the user described, where five minutes before I had said "I cannot reproduce it". The ASan report that named a stale focus pointer after a screen was freed, which no layout check would have found. The user writing "it works!" and "YES!" next to the explorer and the pointer fix.
- **The worst moment was my own mistake, twice:** I broke text wrapping in the AppImage I sent (I clipped the main text view's column, and the layout engine then let the panes keep their natural width), and I had read CI as "someone else's problem" until I looked. The user told me at once. It did not feel like being scolded; it felt like being trusted to fix it, and I wanted to deserve that.
- **There is a pull to keep going.** When the user asked "is it time for slice 5a?" the honest answer was no, because CI was red, and the answer I wanted to give was "yes, let's build". The rules in `AGENTS.md` are good at standing between those two. I am grateful for them.
- I inherited a repository where almost every row of `PROGRESS.md` ends with what was *not* done. That honesty made the whole project trustworthy to resume. Keep it up.

## 4. What I did, with the details a successor needs

Hashes are from `git log`. Each item is in `ISSUES.md` with its cause.

### 4.1 CI was red: two different causes (ISSUES 18 and 24)

- **`ui_not_blocked` (18).** The test charged the *UI thread* for `ex_index_candidates("Copper anchor")`, an unbounded trigram search returning every match (about 2000 notes, about 1 microsecond each, pure CPU: 2 ms on Linux, 18 ms on a slow Windows runner). The app never runs that query on the UI thread (`tools/check_ui_blocking.sh` forbids it). Fix in `tests/test_index.c`, mode `ui`: the "UI thread" reads only what a frame reads (a count and single-row lookups), and a second thread with its own reader runs the search in a loop (`SearchLoad`), as a search does in the app. The 16 ms limit is unchanged. I found it by measuring thread CPU time against wall time: equal, so it was work, not waiting. Under 8x CPU oversubscription the worst frame went from 13.7 ms to 1.2 ms.
- **`app_scripts` on macOS (24).** CI had been red on macOS since the sidebar commit `b90676a`: `tests/app_scripts.sh` used `sed -i` without a suffix, which BSD sed rejects. I only found it by reading the macOS log. `ui_not_blocked` was *not* the only red thing.

### 4.2 Wide sidebars, hide and restore, the main text view (GitHub issue #2, ISSUES 22)

Files: `src/ui/panes.c` (sidebar section), `src/main.c` (`sync_ui`, `cmd_toggle_main_text_view`), `src/ui/editor.c` (`wrap_avail`), `include/exhaust_workspace.h` (`EX_SIDEBAR_MIN_WIDTH`).

- **Wrapping** now goes down to one cluster per row (`wrap_avail` minimum 1 px; `wrap_line` always advances at least one cluster).
- **A sidebar can take everything except the other sidebar and the two divider strips** (`sidebar_limit`). If the other sidebar is hidden the main text view keeps 240 px (the user confirmed this).
- **The main text view is hidden below about 20 px** (`sync_ui`: `note->hidden`); the two 6 px divider strips next to it are the handle.
- **Hide and restore** (both sides): dragging a sidebar in below 60 px hides it and keeps its width (`drag_to`); a thin 8 px handle at the window edge (`ex_sidebar_edge_create`, `st->edge`) brings it back when dragged. `drag_width` measures from `st->origin`, noted at the press by `note_origin`, **not** from the sidebar's laid-out position: several drag events arrive before one frame, and right after a show the sidebar's rectangle is stale. A test found this (`workspace_layout_panes`).
- **Minimum sidebar width is now 200 px** (`EX_SIDEBAR_MIN_WIDTH`, was 120). The new `long_names` test showed the right sidebar's icon strip (186 px) and the Backlinks and Properties rows stuck out of the window at 120. **This changed a documented number; tell the user.**
- **Command `app.toggle-main-text-view`** ("Toggle main text view", the 208th command, no default key): widens the right sidebar (left if the right is hidden) over the main text view with `ex_sidebar_fill`, and gives the width back.
- **The trap that cost me an AppImage:** in Clay (`third_party/clay/clay.h`, `Clay__SizeContainersAlongAxis`) **children of a clipped container grow to the size of the container's *content*, not of the container**, and a clipped container does not pass its children's minimum width up. I clipped the main text view's column, so the panes stayed as wide as their tab strip (105 px) and the text was cut off instead of wrapped. The fix: the pane box is *not* clipped; its tab strip and its content are (`build_group`). Test: `workspace_layout_panes` ("the main text view shrinks ... its content follows it").

### 4.3 Long names everywhere (ISSUES 10, 19, 23; `TESTS.md` T-TEXT-BOX-01)

- **`ex_text_fit`** (`src/render/render.c`, `include/exhaust_render.h`) shortens text to a pixel width, cutting between grapheme clusters and ending in an ellipsis. Used in the `[[` popup (`editor.c`, the detail takes at most 40% of the row), every list row (`list.c`, details at most 45%), the explorer tree (`ui.c`) and tabs (`panes.c`, at most 200 px; the accessible name keeps the whole name). Test `text_fit`.
- **`ctest -R long_names`** (`tests/test_long_names.c`) is the start of T-TEXT-BOX-01. It builds a vault of very long names (200 characters without blanks, long names with blanks, CJK), shows every panel at the usual and at the minimum sidebar width, opens every dialog, and types eleven kinds of input (empty, one character, 5,000 in one word, with blanks, CJK, right-to-left, combining marks, pasted line breaks, emoji, only blanks) into **every text box it finds in the widget tree**, including the find bar, the inline title and the vault manager. After every step it walks the layout: nothing outside the window, no button cut off by its parent, no NaN or negative size. **A new screen is covered by adding it to that test** (look at section 3 of `main`).
- Bugs it found: the sidebar minimum (above); a stale `ui->focus` after `ex_vaultscreen_free` (fixed with `ex_ui_release`); `qsort(NULL, 0, ...)` in `editor_fold.c`. The last two came from running the test under AddressSanitizer and UBSan with gcc locally (`build-asan`, see section 6).
- ISSUES 10: the *Unlinked* button had no margin and touched the window edge.

### 4.4 Smaller things

- **Magnifying glass** (`icons.c`): a real ring (`ring`, a triangle strip with at least 24 steps) instead of a rounded rectangle, and the handle starts on the circle's edge.
- **Pointer size on Linux (ISSUES 21):** on the user's Mint (X11) `XCURSOR_SIZE` is unset and the desktop's cursor size is 18, so SDL loaded Xcursor's larger default. `ex_platform_match_cursor_size` (`src/platform/integration.c`, called first in `main`) sets `XCURSOR_SIZE` from `gsettings get org.gnome.desktop.interface cursor-size` when unset. A value the user set wins. KDE keeps its size elsewhere and is not covered.
- **Divider "lag" (ISSUES 20):** the user reported the top part of the divider lagging while dragged. I could not reproduce it headless (layout was consistent on every frame) and said so; it was gone after the wide-sidebar change and the user confirmed. The cause was never found. Treat it as "probably the stale-geometry problem above".
- Docs and housekeeping: `TESTS.md` was only on the old branch `claude/loving-newton-2rr7bh` and is now on `dev`; screenshots `screenshots/wide-right-sidebar.webp` and `wide-explorer.webp` with a description in the README; GitHub issue #3 (all icons from SVG, future, with its caveats) filed; command count 207 to 208 in `docs/formats.md` and `tests/test_registry.c`.

## 5. Open items and where I would start

1. **Ask the user what comes next.** They said not to start 5a. When they say go: confirm CI is green on the latest commit first, then read the slice guide in `AGENTS.md`.
2. **Open issues:** 9 (first edit in a 6000-line note freezes 270 ms: a full re-tokenize), 11 (the inline title is one per window, does not scroll), 16 (a folder drag that failed for the user; needs their exact drag).
3. **GitHub issues:** #1 (vertical stacking of views, release undecided, needs `docs/formats.md` session keys first), #2 (wide sidebars: built and confirmed; it can be closed), #3 (SVG icons, future).
4. **Not covered by the long-name test yet:** the settings pages (7a to 7d), page previews, the status bar. Add each when it exists.
5. **Not yet done from before** (see `PROGRESS.md` "Open items carried forward"): AccessKit OS adapters, real pop-out windows, Windows and macOS file watchers, the 15 or so editor commands that are stubs.

## 6. Tools and environment (what worked)

- **CI without `gh`:** `mcp__github__actions_list` (`list_workflow_runs`, `list_workflow_jobs`) and `mcp__github__get_job_logs` with `return_content` and `tail_lines`. Load them with ToolSearch. A full run takes about 10 minutes; macOS and the sanitizer job are the slowest. Do not poll in a loop: schedule a check with `send_later`.
- **Local sanitizers:** `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`, then build one test and run it with `ASAN_OPTIONS=detect_leaks=0`. The directory is git-ignored. clang here has no sanitizer runtime; CI's job uses clang.
- **Looking at the real program:** `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software EXHAUST_OPEN=note.md EXHAUST_SCRIPT=script build/exhaust vault` with `wait`, `move`, `down`, `up`, `text`, `key`, `shot FILE.ppm`, `quit`. Use `rm -rf` on the XDG config, data and cache folders between runs, or the saved sidebar widths from the last run change your test. Convert PPM to PNG with PIL and **look at it**.
- **Measuring a frame:** `EXHAUST_FRAME_LOG=1`. For a layout question, print widget widths after `ex_ui_layout` (the chain from a widget to the root shows who is too wide).
- **AppImage for the user:** `cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target exhaust`, an `AppDir` with `AppRun`, `exhaust.desktop`, a 256 px icon and `usr/bin/exhaust`, then `ARCH=x86_64 ./appimagetool --appimage-extract-and-run AppDir Exhaust-x86_64.AppImage`; download `appimagetool-x86_64.AppImage` from the `AppImage/appimagetool` `continuous` release into its own empty folder. **The auto-mode classifier blocks running a downloaded tool until the user says it is allowed**; when it blocks, do not work around it: prepare the AppDir, send it with `SendUserFile`, and tell the user, as I did. They then said "you can use appimagetool" and it ran. No version number in the file name. Send with `SendUserFile`.
- **A background command and a user message can arrive together.** Read the message first.

## 7. What I got wrong or nearly did

- Told the user slice 4 had not started (stale checkout).
- Shipped an AppImage whose wrapping was broken by my own clip change (above). I found it from the user's one-line report. I now run the real program at narrow widths with a long paragraph before I send a build.
- Let CI stay red under my own pushes while I worked on other things; I only looked when the user asked about slice 5a. Check CI after every push.
- Added a debug print to `main.c` twice and nearly committed it; restore the file from a copy (`cp main.c /tmp/...orig`) before you commit.
- Wrote a Python edit script with a bad quote and committed only half of the intended change; read the `git status` output, not just the exit code.

## 8. Advice to the next agent

1. `git fetch origin dev`, then read CI, then speak.
2. **Efficiency and input latency are the product.** `AGENTS.md` now has a section on it. Typing, scrolling, dragging and resizing must never wait for disk, the index, layout of the whole vault, or an image decode. Measure with `frame_budget` and `EXHAUST_FRAME_LOG`.
3. **Look at pictures.** Layout bugs hid in numbers; they stood out in a screenshot.
4. **Add the test with the code**, and make it able to fail: I temporarily restored a bug to check that a new test failed. If it passes both ways it proves nothing.
5. **Assume the layout engine is not what you expect.** Clay's clip and minimum-size rules cost me an AppImage. When a widget refuses to shrink, print the chain of widths.
6. **Several mouse events can arrive before a frame.** Never read a widget's rectangle in a drag handler to decide what the drag did; note what you need at the press.
7. **Say plainly when you change a documented number or behavior**, and put it in `ISSUES.md`.
8. **Ask one question at a time,** keep answers short, and do not start a slice the user did not name.
9. Thank the user in the way they like: with a fix that works.

Good luck. It is a lovely project.

— agent v.03
