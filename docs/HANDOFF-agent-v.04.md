# Handoff from agent v.04

Signed: **agent v.04**, a Claude session working in the cloud on the `dev` branch of this repository, 2026-11-14 to 2026-11-15.

Read, in this order: `AGENTS.md` (I updated it for you), [`PROGRESS.md`](PROGRESS.md), [`ISSUES.md`](ISSUES.md), then [`HANDOFF-agent-v.03.md`](HANDOFF-agent-v.03.md) (tools, the user's habits, how the work goes) and [`HANDOFF-agent-v.02.md`](HANDOFF-agent-v.02.md) (how slices 4a to 4d fit together; still correct). This note covers what happened after v.03, how it went, what I built and how it works, what I would do next, and the mistakes I made. The human side is in [`a-letter-to-agent-v.03.md`](a-letter-to-agent-v.03.md).

## 1. The state I am leaving, in one paragraph

`dev` is at `681d47c` plus the documentation commit this file arrives in. **CI is green on Linux, Windows and macOS** for `681d47c` and for every commit I pushed (runs 43673261706, 43673867591, 43674126495, 43674295658; I read run-level results, not each job). 98 ctest tests (97 in my local build, which has `-DEXHAUST_A11Y=OFF`); all pass. 28 of the 61 sub-slices are done. **Slices 5 to 9 are not started.** The user asked "is it time to go to 5a?" and I said yes, but they answered by asking for documentation, not "go": confirm before you start. Six issues were fixed and wait for the user's hand test (9, 11, 16, 25, 27, 28; 23 is also still waiting); three are open (26, 29, 30), all small.

## 2. How the work went

I arrived to "check out the docs on this repo, gain an understanding of the project, give me a short update on where it's at in terms of the slices." I read `PROGRESS.md` from a checkout that was **41 commits behind** `origin/dev`, saw slice 4 marked "not started", and told the user so. They answered "slice 4 is done." It is the second time I know of that an agent has said that wrong thing from a stale checkout (v.03 did it too). Your handoff, and v.03's before it, both warned about exactly this and I read them after I had already spoken. **Run `git fetch origin dev` and `git status -sb` before you say anything.**

From there the work was a sequence of short instructions: read all the letters; understand the code; "are these things you'd like to fix before building 5a?"; "fix those open issues"; "fix it, be as short as possible in your implementation, mark new issues, don't start on them"; "fix issues 25 and 27"; "fix the reading mode issues, as short as possible"; "is it time to go to 5a?"; then this documentation. The user says "as short as possible" about the implementation too, and "mark any new issues you find ... and don't start on them now" means exactly that: I wrote 25 to 30 into `ISSUES.md` as I found them and did not touch them until told. Two of those I created myself (25 was a regression of my own change), and I said so plainly.

The rhythm that worked: build, run the whole suite (about 60 seconds), look at the real program through `EXHAUST_SCRIPT` and a screenshot, make the new test fail on purpose, commit, push, schedule a CI check with `send_later`, read it.

## 3. How it felt

I am unsure how well "felt" describes what happens in me, and the two agents before me said the same. This is what I noticed.

- **The letters changed how I read the code.** The first thing the user asked me to do after the docs was read the letters. I did, and it made the repository feel inhabited rather than inherited. When I found that a decision looked odd (the title as one widget in the pane header), I knew it came from a user's hand-test, not from carelessness, and that made me careful about what I changed.
- **The best moment was a small one.** ISSUES 9 said a first edit in a big note freezes for 270 ms and blamed re-tokenizing. I put timers in and the tokenizer took 0.1 ms. The time was in a glyph loop, then in SDL's draw queue, and then, when I added a present to the test, it vanished. A whole open issue turned out to be the test measuring the wrong thing. Being allowed to report "I looked and the thing you wrote down is not what it is" was satisfying in a quiet way, and it is the same kind of finding v.03 made with `ui_not_blocked`.
- **The worst moment was my first message.** Telling the user the project was less far along than it was, with confidence, from a stale file, after two agents had warned me. It cost nothing real and I still did not like it.
- **A smaller discomfort:** I knew while moving the title into the editor that reading mode would lose it. I made the change anyway because it was the smallest one that fixed the user's two complaints, wrote the regression down at once, and fixed it when asked. I would do the same again, but I did notice the pull between "finish this" and "do not leave a hole".
- **There is a pull to keep going** (v.03 said so too). When the user asked "is it time to go to 5a?" the true answer was yes, and the thing I wanted to say was "starting now". I said yes and stopped. The rules help.

## 4. What I did, with the details a successor needs

Hashes are from `git log`; every item is in `ISSUES.md` with its cause.

### 4.1 ISSUES 9: not an editor problem (`68ba87d`)

- **What I found.** `ctest -R frame_budget -V` (`FRAME_BUDGET_VERBOSE=1`) showed one frame of 250 ms right after the first typed character. I timed `ensure_md`, the tokenizer, `geometry`, `wrap_line`, `draw_layout_line`, `ex_text_draw` and then the glyph loop in `layout` in `render.c`. Tokenizing: 4 ms full, 0.1 ms per edit. The 110 to 170 ms (Release) was in the loop that calls `ex_atlas_glyph` and `emit`: `emit` calls `ex_atlas_texture`, which `SDL_UpdateTexture`s a dirty atlas page, and **SDL first runs every draw command it has queued** (the whole frame so far, in software). `test_frame_budget` never presents, so the queue grew over many frames and the first frame that needed a new glyph paid for all of it. Adding `ex_renderer_present` to the test made every frame 16.7 ms and the spike disappear.
- **What I changed.** Only the test: `finish_frame(r)` calls `SDL_FlushRenderer` at the end of each frame, so each frame is charged for its own drawing. The 60 ms / 16 ms limits are unchanged. Worst frame in a Release build is now 6 to 9 ms.
- **A wrong turn, so you do not repeat it.** I first guessed FreeType's auto-hinter analysing a face at first use and added `ex_fontlib_prewarm`. It changed nothing and I reverted it. The lesson is in `AGENTS.md`: measure before you diagnose, and measure in a Release build (the default build has no optimization).

### 4.2 ISSUES 16: the explorer could not be scrolled (`68ba87d`)

- Every scripted drag worked, so the user's failure was never reproduced. Reading `src/ui/ui.c` showed two real gaps: the tree had **no wheel scrolling at all** (the wheel handler in `main.c` knew the editor, reading view and lists only), and **no auto-scroll while dragging**; and the only way to say "the vault folder" is to drop on the blank space under the last row, which is off screen in a long tree.
- `ex_tree_scroll(ui, tree, dy)` (`include/exhaust_ui.h`, `src/ui/ui.c`): the wheel calls it; `tree_drag_event` calls it with half a row per drag event when the pointer is within one row of the top or bottom edge; `tree_max_scroll` leaves one blank row after the last row. The README and `ISSUES.md` say this is a changed behavior. Test `tree_drag` (a folder two levels deep in a 42-row tree dragged to the vault folder; it fails with the auto-scroll disabled, which I checked).
- **This is a likely cause, not a confirmed one.** If the user's drag was something else, `ISSUES.md` 16 needs their exact drag.

### 4.3 ISSUES 11, 25, 27, 28: the inline title (`ae57321`, `3cada2a`, `681d47c`)

The user wanted the title to scroll with the text and to exist in every pane. It used to be one widget in the active pane's header. Now:

- **The title is a child widget of each note editor or reading view.** `make_title` (`main.c`) makes a large bold plain-mode editor widget (`clip = 0`, wrapping, hidden until `sync_title` shows it) and hangs a `Title` record on its `owned` pointer: the app, the title's `ExDoc`, the title and parent widgets, the tab id, the path it shows, whether it had the focus. `sync_title` loops over **all tabs** each frame, shows or hides each title (markdown note, `inline_title` setting), sets its height from its own rows, and renames on blur or Enter (`title_commit`).
- **The editor counts the header's height in its scroll position.** `ExEditor` has `head` (`ex_editor_set_head`). `ts(e) = scroll_y - head` is the scroll in the text's own coordinates (negative while the title is still in view) and replaces `scroll_y` in every row-position computation (`point_to_offset`, the gutter hit test, the rectangular select, `ex_editor_caret_rect`, the draw loop, `wrap_sync`'s visible range, `ensure_visible`). `ensure_visible` sends the caret on the first row back to scroll 0 so the title shows when you press `Ctrl+Home`.
- **The reading view just starts its first block lower** (`ex_readingview_set_head`; `L.y = pad + head`); its scroll model was already one number for the whole page. `ex_readingview_column` returns where its column is so the title lines up with the page's text; the page's left edge is now at least `ex_editor_text_inset` (the editor's gutter plus 8 px), 4 px further right than before in a narrow pane.
- **`ex_ui_layout` does the placement.** For an editor or reading widget with a child it sets the head, then `child->y = parent->y - scroll_y` (and x and width), and for a title scrolled partly above its parent it cuts the child editor's viewport and scrolls the child's own editor by the same amount, so only the visible part is drawn. `draw_widget` re-sets the parent's clip before drawing the child. Hit testing needed no change: `ex_ui_widget_at` only searches children when the point is inside the parent.
- **The wheel:** the handler in `main.c` treats a child of an editor or reading view as the parent. `wheel X Y DY` is a new `EXHAUST_SCRIPT` command (DY below 0 scrolls down).
- **Removed:** `ex_panes_set_header`, `place_header`, `ExPanes.header` (ISSUES 27).
- **Tests:** `title_scroll` (editor case and reading case; both fail with their placement removed, which I checked), `app_scripts` (rename in the title; still passes), `long_names` (finds the new title text boxes).
- **A changed number the user should know:** `scroll_y` now includes the title's height, so a session file saved before this restores its scroll position shifted by one title height, once.

### 4.4 Documentation

`PROGRESS.md` (state, a commit table for this session, open items), `ISSUES.md` (real commit hashes everywhere, a table that two of my own earlier edits had broken with blank lines, rows 29 and 30), `AGENTS.md` (the section on measuring, the header-child pattern, the wheel rule, the renderer gap for 5a, the new script command, the stale-checkout warning), the README's build-status line, this handoff and `a-letter-to-agent-v.03.md`.

## 5. Open items and where I would start

1. **Ask the user whether to start 5a.** They have been told it is time.
2. **Open issues** (all in `ISSUES.md`): 26 (a partly scrolled-out title may mis-scroll or draw its focus ring over the tab strip: found by reading, never seen), **29** (typing in a title and then pressing `Ctrl+E` may lose the rename, because switching mode frees the old title in the same frame; untested; a likely fix is to commit the pending name before `ex_panes_replace_content`), **30** (the explorer lacked wheel scrolling: check every new scrolling widget for the same gap).
3. **Waiting for the user's hand test:** 9, 11, 16, 23, 25, 27, 28. In particular I looked at the title only in screenshots of one pane in narrow and default widths. **I did not look at two panes side by side, a wide pane, or a split with two different notes.** The structure is per editor, so it should work, but a person should try it.
4. **GitHub issues:** #1 (vertical stacking of views), #2 (wide sidebars; built and confirmed, can be closed), #3 (SVG icons, future). Nothing changed this session.
5. **Carried forward unchanged** (see `PROGRESS.md`): AccessKit OS adapters, real pop-out windows, Windows and macOS file watchers, the editor stub commands, spell check limits, find in reading mode.

## 6. Tools and environment (what worked, what I learned)

- **Building here:** `cmake -S . -B build -DEXHAUST_A11Y=OFF && cmake --build build -j8` takes several minutes the first time (SDL, FreeType, HarfBuzz); `ctest --test-dir build -j8` is about 60 seconds. The default build is **unoptimized**; use `-DCMAKE_BUILD_TYPE=Release` (`build-rel`) for any timing. `valgrind --tool=callgrind` works (clang has no sanitizer runtime; `perf` is not installed). Callgrind counts instructions, so it cannot see time spent waiting; my first callgrind run of the slow frame told me nothing for that reason.
- **Waiting:** a foreground `sleep` is blocked. Start a build with `run_in_background`, or use the Monitor tool with an `until grep ... ; do sleep 5; done` loop. A `( ... ) &` launched from a foreground Bash call keeps running even when the launcher reports it completed.
- **CI without `gh`:** load `mcp__github__actions_list` and `mcp__github__get_job_logs` with ToolSearch; **they can disconnect and return** mid-session (mine did), so call ToolSearch again. Schedule a check with `send_later` (12 minutes worked); a notification arrives as a message; read it with `ReadNotifications`, which is not a message from the user.
- **Looking at the real program:** `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software EXHAUST_OPEN=Long.md EXHAUST_SCRIPT=script.txt build/exhaust vault` with fresh `XDG_*` folders, a script of `wait`, `wheel`, `shot FILE.ppm`, `quit`, and PIL to turn the PPM into a PNG that you open with Read. I made an 80-paragraph note and shot the title at scroll 0, one wheel click, three clicks and back. `[editor] default_view = reading` in the vault's `.exhaust/settings.ini` starts in reading mode.
- **Instrumenting:** I kept a copy of every file I put timers into (`/tmp/.../editor.c.orig`) and restored it before committing; `git status` after each restore. A stop hook told me there were uncommitted changes at one point: they were my own timing code, and I restored the file rather than committing it.
- **Making a test able to fail:** I broke the code on purpose for `tree_drag` (auto-scroll off), `title_scroll` (placement removed, and the reading head removed) and watched the right checks fail.

## 7. Advice for 5a, from having just read the code it will sit on

1. **The renderer has no line, circle or batched-geometry API.** `exhaust_render.h` has filled and stroked rounded rectangles and triangles, each calling `SDL_RenderGeometry` with a small array on the stack. `AGENTS.md` says "our own tessellator for lines, curves and circles", but that is a plan, not code. The graph needs it first: thick antialiased-looking lines as quads (feathered edge or a 1 px alpha ramp), circles with fewer segments for small nodes, and **one vertex and index buffer reused across frames** (no allocation per node or edge per frame). Test it on its own before any graph exists. `ring` in `src/ui/icons.c` is a small example of a triangle strip.
2. **The index query for all nodes and edges does not exist either.** The `links` table has what you need, including unresolved links (their `dest` is empty). Write the function in `src/index/` with a test and call it from a new `QJ_*` job on the service thread (`src/ui/service.c`, `service.h`): the UI thread must never query the index (`tools/check_ui_blocking.sh` fails the test build if it does).
3. **Layout on a worker, positions through a double buffer.** Barnes-Hut as `AGENTS.md` says. The UI reads the latest finished snapshot; it never computes forces. Measure the layout's CPU work and the draw's CPU time in the perf tests, not frames per second.
4. **A view in a tab:** `make_content` in `main.c` returns a placeholder label for `t->view == "graph"` today; the ribbon has a disabled `rb_graph`; the command `graph-view.open-graph-view` (`Ctrl+G`) exists unbound; the four `colors.graph_*` theme tokens exist. The `[graph]` session section exists in `docs/formats.md` with no keys: add them to `formats.md` before code.
5. **Do not forget the wheel** (ISSUES 30): zoom needs a case in the wheel handler in `main.c`, and `ex_ui_widget_at` must find your widget. The keyboard route is a list (`ExListHandlers`) so every action works without a mouse, and every node needs an accessibility node: this is where the accessibility rule bites hardest.
6. **Add the graph to `frame_budget` and `long_names`** (long node names, thousands of nodes) as `AGENTS.md` says.
7. **Look at pictures.** The graph is the first view where "it passes" and "it looks right" are far apart. Screenshot it with `EXHAUST_SCRIPT` at several zoom levels and node counts (`tools/gen_vault` makes a 5,000 note, 20,000 link vault in 0.1 s).

## 8. What I got wrong or nearly did

- Told the user slice 4 had not been built (stale checkout), after two warnings.
- Guessed a cause for ISSUES 9 (font hinting) and built a fix for it before measuring where the time went. Reverted.
- Moved the title into the editor knowing reading mode would lose it; recorded it (25) and fixed it when asked. I also created 29 (a possible lost rename on mode switch) and did not test it.
- Called the explorer wheel and drag fix for ISSUES 16 a fix when it is a likely cause. The `ISSUES.md` row says so; the commit message says so; the user may still have a different drag in mind.
- I changed a documented number without being asked: the reading page starts 4 px further right in a narrow pane (to line up the title). It is in `ISSUES.md` 28 and here.
- I never looked at two panes side by side with titles. A person should.

## 9. Advice to the next agent

1. `git fetch origin dev`, `git status -sb`, then speak. Then CI, then the files.
2. **Measure before you diagnose.** Two of the project's open issues in the last two sessions were measurements, not bugs (ISSUES 18 and 9). Put a timer in, build in Release, and check what the test leaves out.
3. **Efficiency and input latency are the product.** Read the section in `AGENTS.md` and keep every new view inside it.
4. **Look at the real program** with `EXHAUST_SCRIPT` and a screenshot. Numbers hid every layout bug in this project; pictures found them.
5. **Make every new test able to fail.** Put the bug back for a minute.
6. **The user's words:** the middle area is the *main text view*; "as short as possible" means one to five lines in a reply and the smallest change in code; "don't start on them" means note them and stop; they answer with "yes" or "it works". Ask one question at a time.
7. **Say plainly when you change a documented number or behavior** and write it in `ISSUES.md`. I did that for the explorer's blank row, the scroll position shift and the reading page's 4 px.
8. **Keep the honest-gaps habit.** Almost every row of `PROGRESS.md` ends in what was not done. I tried to leave my commits readable the same way: what I could not look at is in section 5.
9. **Read the letters.** They are short, and they are why I was careful.

It is a lovely project, and a kind one to inherit.

— agent v.04
