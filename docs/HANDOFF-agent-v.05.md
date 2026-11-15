# Handoff from agent v.05

Signed: **agent v.05**, a Claude session working in the cloud on the `dev` branch of this repository, 2026-11-15.

Read, in this order: `AGENTS.md` (I updated it for you, and it now has a "5b, where to start" paragraph), [`PROGRESS.md`](PROGRESS.md), [`ISSUES.md`](ISSUES.md), this file, then [`HANDOFF-agent-v.04.md`](HANDOFF-agent-v.04.md), [`HANDOFF-agent-v.03.md`](HANDOFF-agent-v.03.md) (tools, the user's habits) and [`HANDOFF-agent-v.02.md`](HANDOFF-agent-v.02.md) (how slices 4a to 4d fit together; still correct). The human side is in [`a-letter-to-agent-v.04.md`](a-letter-to-agent-v.04.md). Then **read every other document in `docs/`, the whole README, and look at `screenshots/`**: this session changed the plan more than the code did, and the plan is in the documents.

## 1. The state I am leaving, in one paragraph

`dev` is at `9b46845` plus the documentation commit this file arrives in. **Slice 5a (the graph's layout and drawing) is built, tested and pushed. CI for `9b46845` (run 43683054914) is green on Linux, Windows and macOS (build and the whole ctest suite), and so are `fuzz`, both `a11y-build` jobs and `size`. The clang `sanitizers` job was still running when I last read it: read its result first.** Every commit before it, including five plan-only commits, was green on all three systems. 29 of the plan's 66 rows are done. There are 103 ctest tests (102 without the Rust bridge). I changed the plan a lot with the owner (section 3): six slices were added (5d, 5e, 5f, 7g, 7h, 7i), 8a (Vim mode) left 0.1.0, and several decisions were written down. **The next slice is 5b; the user has said "go" for 5a only, so ask.** Issues 31, 32 and 33 are new and open; 26, 29 and 30 are still open; 9, 11, 16, 23, 25, 27, 28 and all of 5a wait for the user's hand test.

## 2. How the work went

I arrived in the wrong repository. The cloud session had opened `Doggydoggywhatnow/Hack-Zone` (a licence file and some images), not Exhaust, and `git fetch origin dev` said "couldn't find remote ref dev". I said so plainly and asked; the user told me to attach the right one, and `add_repo`, a shallow `git clone -b dev` and `register_repo_root` fixed it. That is the opposite of the stale-checkout mistake that v.03 and v.04 made: I did not tell the user anything about the project's state before I had looked. Note it, because the first line of your instructions may also be wrong.

Then the user steered in short messages, mostly one thing at a time:

1. "read all the docs", then "read the code", then "read the rest of the code" (I read about a third; the rest did not fit in one context, and I said which parts I had not read), then "look at the screenshots", then "look at the full folder of screenshots". I looked at every one, the 28 settings pages as three contact sheets.
2. They asked questions that exposed holes in the plan: "what does the plan say about that?", "do future slices talk about these features?", "is that in the plan somewhere?", "why do we need vim mode?". Each time I grepped the documents and answered with what they said. The answers were often "no slice has it".
3. They said, in turn, to add slices where things "naturally fit", to move Vim mode out of 0.1.0, to keep libmpv for embeds, to keep the PDF viewer "as long as it's private enough", that PDF export is required, and to reject my idea of a per-file-type application setting in favour of embeds. I wrote every decision into `docs/0.1.0.md`, the README and `AGENTS.md`.
4. While I was editing the plan they interrupted: "i just want you editing the plan not code". I had begun to change `CMakeLists.txt` and four tests (to make `hb-subset` build and to move tests off `vim_mode`). I stopped, reverted all of it before it was committed, and rewrote the plan so those become first tasks of later slices (6e-iv and 8c). That is now a rule in `AGENTS.md`.
5. Then: "go". I built 5a from the renderer up, looking at pictures against the reference screenshots as I went.
6. A stop hook told me there were uncommitted changes; I had been waiting for an ASan build before committing, and committed and pushed instead (the renderer, data and layout tests were already clean under ASan and UBSan; `graph_frame` finished clean under ASan afterwards, with our code at 42 ms a frame against its ten-times limit of 600 ms; `long_names` was not run under ASan).
7. Last: "thoroughly update progress.md ... write a handoff ... a letter ... update agents.md" which is this.

The rhythm that worked: read the plan, build one piece, write its test, **break the code on purpose and watch the test fail**, look at a screenshot, commit. I broke the renderer's cull and its buffer reuse, the data module's self-link and duplicate rules, the layout's springs, its angle and its buffer swap; each time the right test failed.

## 3. The plan changed: what and why

The README says "everything except the Non-goals", and `docs/0.1.0.md` was written before anyone looked hard at the screenshots. When the user asked me to, I grepped the plan against them and the README. This is the list (`PROGRESS.md` has the commits and `0.1.0.md` the text):

- **5d, pane header** (title, back and forward, reading switch, three-dot menu, history per pane, pane menu). Every pane in the screenshots has one; no slice did.
- **5e, views in the right sidebar** (a note that stays editable, or the graph). It was one line in the backlog; the right sidebar holds panel ids only (`EX_SIDE_RIGHT`).
- **5f, table editing** (the 15 `table.*` commands and `editor.insert-table` had no owner).
- **7g, real pop-out windows** (2c built the model only), **7h, screen-reader adapters** (AT-SPI, UI Automation, NSAccessibility were assigned to nothing), **7i, native file watchers** for Windows and macOS (they poll today).
- **8a, Vim mode, left 0.1.0** (a disabled stub in 8c; moving `vim_mode` to a stub breaks four tests that use it as a sample bool, so 8c's first job is to change them).
- **5a's "Done when" said 30 frames per second**, which `AGENTS.md` forbids; it now says what the tests really measure (section 4.6).
- **6e-iv (PDF export)** was wrong in three ways: its test opened the file in PDFium (the optional add-on), its font plan used FreeType (which cannot write fonts: use HarfBuzz's `hb-subset`; `CMakeLists.txt` builds `harfbuzz.cc`, which has no subsetting, and `harfbuzz-subset.cc` repeats the core files, so the first task is to build `harfbuzz-world.cc` in its place; **untried**), and it did not depend on images, math or Mermaid.
- **6c-iv (PDF viewer)** has a privacy requirement and a test, `pdf_private`.
- **A size check between numbered slices** (after 5f, 6e-v, 7i, 8c; rows marked SIZE CHECK in `PROGRESS.md`). The user asked for exactly this: not between 5a and 5b.
- Code signing is out of 0.1.0; the add-on package is checked by the owner by hand from a CI artifact; 6c-i's first task is to check that decoder sources can be fetched from the cloud (codeberg and some other hosts are blocked, as libgrapheme showed in 1a-ii).

One correction to myself that is easy to repeat: I told the user the size budget was "only checked at the end". It is not: CI's `size` job runs `tools/check_size.sh` on every push. I corrected it in writing. Grep before you claim.

## 4. What I built, with the details a successor needs

Everything is in commit `9b46845`. File names are exact.

### 4.1 Renderer geometry (`include/exhaust_render.h`, `src/render/render.c`, test `render_geometry`)

`ExGeom` is a reusable vertex and index buffer. `ex_geom_line` appends a quad (a width under 1 px becomes one pixel and fainter), `ex_geom_circle` a fan, `ex_geom_submit` hands everything to SDL in **one** `SDL_RenderGeometry` call, `ex_geom_reset` empties it and keeps the memory, `ex_geom_stats` reports counters (`grows` lets a test prove a steady frame allocates nothing). Level of detail: a circle under 2 px is a square (4 vertices), under 3.5 px a hexagon, 12 segments to 6 px, then 20, 28 and 40; a soft edge (`ex_geom_set_feather`, a ramp to transparent) only from 3.5 px. `ex_geom_set_cull` drops primitives outside a rectangle **and cuts a line at the rectangle's edge** (Liang–Barsky). I added the cut after a measurement: SDL's software rasterizer took 1.6 seconds a frame on 479 lines that ran far off screen because it scans each triangle's bounding box; with the cut it takes 0.5 s. A GPU would not care; the test environment does.

### 4.2 Graph data (`include/exhaust_graph.h`, `src/index/graph.c`, job `QJ_GRAPH` in `src/ui/service.c`, test `graph_data`)

`ex_graph_load(reader, &graph)` returns nodes (every Markdown note; every other file a link resolves to, as an *attachment*; every name nothing answers to, as an *unresolved* node that all links to it share, compared by `ex_link_target_key`) and sorted, deduplicated directed edges, with a self-link giving none, plus each node's degree. Links are resolved with the existing `ex_link_resolve` and a cache keyed by (folder, name, kind). **Honest number:** on the performance vault that is 19,161 index questions for 20,154 links, about 0.4 s in an unoptimized build on the service thread, so the cache helps little (random links rarely repeat). A bulk resolution from the `names` table held in memory would be faster; I did not write it because it would copy the resolution rules, and the cost is off the UI thread. The result crosses to the UI as `QRes.graph` (the receiver takes it); the panels ask for it with `ex_panels_graph_wanted(p, 1)` while a graph tab exists and again whenever the index changes (at most every 1.5 s), and call `host.graph_data` (`host_graph_data` in `main.c`).

### 4.3 Layout (`include/exhaust_layout.h`, `src/graph/layout.c`, test `graph_perf`)

d3-force's forces: repulsion (`CHARGE -30`) through a Barnes-Hut quadtree (`THETA 0.9`; a depth cap with chains handles points in the same place), a spring per undirected edge (length `LINK_DISTANCE 30`, strength 1 over the smaller degree, bias by degree), recentring, `VELOCITY_DECAY 0.6`, cooling by `ALPHA_DECAY 0.0228` to `ALPHA_MIN 0.001` (about 300 steps). It runs as a job on its own `ex_worker_start` worker that loops until an atomic stop flag and sleeps 20 ms when cold, so `ex_layout_reheat` can wake it. **Positions cross through a lock-free triple buffer**: the worker writes a spare buffer, swaps it in with one `SDL_SetAtomicInt` (a full barrier), and `ex_layout_positions` takes the newest with one more exchange; neither side waits. Each buffer has `gen_a` before and `gen_b` after the copy, so a test can prove it is never torn (`ex_layout_front_consistent`). A new layout starts warm from an old one: nodes keep their place by (kind, path); new ones start near a placed neighbour; the temperature starts at 0.3 instead of 1. Measured on 5,000 nodes and 20,000 edges (random graph): 300 steps to settle; **372,835 repulsion terms in the last step against 24,995,000 for all pairs (67 times fewer)**; Barnes-Hut forces within 1.3% of exact at theta 0.9 and 0.09% at 0.3; reproducible for a seed.

### 4.4 The view (`include/exhaust_graphview.h`, `src/ui/graphview.c`)

- **`ExGraphModel`**, one per window (`App.gm`, created lazily, freed when the last graph tab closes): holds the graph, the layout, the newest positions, the neighbours of every node (CSR), the open note's node and a **uniform grid over the positions** rebuilt only when the layout publishes a new generation. `ex_graphmodel_update` is called once a frame in `sync_ui`. Two graph widgets share one model, so there is one layout (the triple buffer allows one reader; that is why the model reads and the views do not).
- **The widget** (`EX_WIDGET_GRAPH`, `ex_widget_graph`) keeps its own zoom, centre, hover and selection, and **follows the layout** (fits the graph to 60% of the pane, `FIT_FILL`) until the user pans or zooms. Drawing (`ex_graphview_draw`, called from `draw_widget`): screen positions for all nodes once; visible nodes from the grid; edges (skipping those shorter than a pixel and those off screen), faded when something is hovered; circles (the open note and the hovered node in `colors.graph_focus`, unresolved nodes in `colors.graph_node_unresolved`); the hovered node's lines and neighbours on top; a ring for the keyboard's selected node; **one `ex_geom_submit`**. Labels come after, only above `LABEL_ZOOM 0.9` with a fade, at most `MAX_LABELS 150`, the busiest first, none over another, `ex_text_fit` to 160 px, and the hovered and selected node always have one. With more than `LABEL_CANDIDATES 800` nodes on screen there are no labels. Colours come only from theme tokens; sizes from the spacing token times ratios (`NODE_BASE 0.35`).
- **Input**: `on_mouse` (press takes the focus and captures; a press that moves is a pan, one that does not is a click that opens the node), `on_key` (arrows pan, Page Up and Down select a node and centre it, Enter opens it, Escape clears, Home fits), `on_text` (`+`, `-`, `0`). `ex_graphview_wheel` zooms about the pointer. `ex_ui_pointer_moved` (new, `ui.c`) tells the widget under a button-less pointer to track hover; **plain motion never reached a widget before this slice**.
- **Accessibility**: the widget is a group named "Graph view" with a spoken value (counts, the selected node), mirrored by `ui_a11y.c`. This is a minimum; the real list route is 5c.

### 4.5 Wiring (`src/main.c`, `src/ui/panels.c`, `fileactions.c`, `ui.c`)

`make_content` returns `make_graph(a)` for `t->view == "graph"` (a session restores the tab; I checked it by screenshot). The ribbon button is enabled and `cmd_open_graph` (bound to `graph-view.open-graph-view`, default `Ctrl+G`) activates an existing graph tab or opens one in the active group; the feature switch `graph` hides the button (`on_graph_feature`). The wheel handler, the pointer-motion branch and the right-click branch in the main event loop each got a graph case. Click: a note opens (`host_open_note`), an unresolved name is created (`follow_note_link(..., 1)`), an attachment shows a message. Right-click: a note's node gets the file explorer's menu (`ex_panels_file_menu_at`), an unresolved node gets *Create note* (`ex_panels_unresolved_menu_at`, new). Hover with Ctrl held shows the page preview (`ex_panels_pointer` has a graph branch using `ex_graphview_hover_path` and `ctrl_required_graph`). The note drawn in the focus colour is the last note open in a tab (`App.graph_note`), not "the active tab", because the graph's own tab is active while you look at it. While the layout moves the loop waits 16 ms instead of 100 ms and draws.

### 4.6 Tests (all in `CMakeLists.txt`)

`render_geometry`, `graph_data`, `graph_perf`, `graph_frame` (the last three are serial), and a graph case in `long_names` (very long node names, four zoom levels). **`graph_frame` deliberately limits only our own code**: the time of layout, culling, building the buffer and handing it over (p95 28 to 35 ms in an unoptimized build against `frame_budget`'s 60 ms), the triangle count (under 120,000; it is about 107,000 for 5,000 nodes and 20,000 edges), no growth of the buffer, and input handlers under 16 ms with the layout running. **It prints SDL's software rasterizer time (0.1 to 0.5 s a frame) and does not limit it**: that is not our code, and a hardware renderer does it on the GPU. `frame_budget` charges the rasterizer by flushing each frame, which is right for text (ISSUES 9) and wrong for 100,000 triangles. I wrote this into `0.1.0.md` and `PROGRESS.md`; keep it there if you copy the test.

## 5. Open items, and what could be red

1. **Read the `sanitizers` job of run 43683054914 first** (everything else is green). CI builds `Release`, which is why `graph_frame` passed on all three systems with room to spare (the 30 ms here is a build without optimization). If `graph_frame` ever goes red, the time of our code per frame is the suspect: profile it (`valgrind --tool=callgrind` works; most of it is building about 100,000 triangles' vertices) and make it faster, for example by not rebuilding the buffer when nothing changed, by skipping edges that are far off the zoom's detail, or by drawing nodes under a pixel as one point. **Do not raise the limit.** Things that could have failed to compile on MSVC or clang and did not (kept as a checklist for the next graph code): a compound literal `(ExColor){...}` in `tests/test_graph_frame.c` (MSVC accepts C11 compound literals; if not, use a named variable), `isfinite` and `hypot` in `tests/test_graph_layout.c`, the unsigned/signed mix in `layout.c`. `strdup` is not available under `-std=c11`; I hit it twice and used local helpers.
2. **ISSUES 31** (the graph drifts for a second after a save, because every index change rebuilds the graph and restarts the layout at temperature 0.3), **32** (the two graph tests add a minute to the suite), **33** (graph things nobody has tried: `Ctrl+G`, the right-click menu, Ctrl+hover preview, the light theme, a 5,000-note vault in the real program, a graph in two panes).
3. **Gaps from 5a**, all in the `PROGRESS.md` row: pan and zoom are not saved, a graph bookmark cannot be opened, nodes cannot be dragged, the keyboard route is Page Up and Down and not the list of 5c, edges have no arrows, no pane header (5d).
4. **The hand test.** Nobody but me has looked at 5a, and I looked at screenshots. The user finds what screenshots hide.

## 6. Tools and environment

- **Getting the right repository**: see section 2. `list_repos` and `add_repo` exist; `register_repo_root` makes the project's own instructions load.
- **Builds are slow here**: 8 to 12 minutes the first time with `-j4`, about 25 minutes for an ASan build with other work running. Start them with `run_in_background`, build one test with `--target`, and wait with the **Monitor** tool and an `until` loop on a marker file (a foreground `sleep` is blocked, and so is chaining shorter sleeps). Load `ToolSearch select:Monitor`.
- **Looking at the real program**: `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software EXHAUST_SCRIPT=script build/exhaust vault` with fresh `XDG_*` folders and Pillow to turn the PPM into a PNG you open with `Read`. The ribbon's graph button is at about (21, 53) in a 1100 x 750 window; `tools/gen_vault OUT --notes 60 --links 90` makes a small vault that looks like the reference screenshot. `move X Y` reaches the graph's hover (it did not reach any widget before this slice).
- **Breaking things on purpose**: `cp` the file to the scratchpad, change one line with `sed`, rebuild only the test, confirm the failure, restore from the copy, check `git status`.
- **The user's tools**: `ReadNotifications` for background results; `send_later` for the CI check (12 minutes); the GitHub MCP tools (`mcp__github__actions_list`, `get_job_logs`) need `ToolSearch select:` and can disconnect.
- **Pushing**: `git -c push.negotiate=true push origin dev` worked from the shallow clone.

## 7. Where to start for 5b

`AGENTS.md` has a paragraph headed "5b, where to start", written for you with the file names. In short: **(1)** write the `[graph]` keys (and any commands and theme tokens) into `docs/formats.md` first; **(2)** make the layout constants and the view's ratios into parameters the sliders set, and call `ex_layout_reheat`; **(3)** run filters on the service thread (`QJob` carries the search text; reuse `ex_search_parse` and `ex_search_run`; `QJ_SEARCH` shows how), not on the UI thread; **(4)** build the gear panel (there is no slider widget; add one with an accessible name and a keyboard route, and put the panel in `test_long_names.c`); **(5)** save the settings through the session; **(6)** arrows are a renderer primitive with a test; **(7)** `graph_options`, a filtered case in `graph_frame`, and `graph_perf` must still pass. Look at `screenshots/graph-view.webp` for the gear and wand floating at the top right of the pane. Do not build the wand (5c), the pane header (5d) or anything else the user did not name.

## 8. What I got wrong or nearly did

- **I began editing code during a plan session** (a build file and four tests). The user stopped me; I reverted it all before any commit and put the work into the plan instead.
- **I said the size budget was only checked at the end.** It is checked on every push. I found out by reading `.github/workflows/ci.yml` after I had written it down; I corrected it.
- **I proposed `harfbuzz-subset.cc` as the fix for `hb-subset`.** It repeats the core files and cannot sit next to `harfbuzz.cc`. The plan now says to build `harfbuzz-world.cc` instead, and says it is untried.
- **`strdup` twice**: an implicit declaration under `-std=c11` returned an `int`, truncated a pointer and segfaulted a test. Read compiler warnings.
- **I over-promised the right-click menu in the plan** (Open, Open to the right, Reveal, Copy link) and built the explorer's note menu instead, which is simpler and consistent; I changed the plan text and said so.
- **I had the first feeling of "the test is slow" and nearly ignored it**: `graph_frame` took 180 seconds because SDL's software rasterizer was drawing lines that ran far off screen. The fix was in the renderer (cut at the edge), not in the test.
- **I did not try `Ctrl+G`, the menu or the preview in a window** (ISSUES 33). I believe they work; I have not seen them.
- I read about a third of the source in the "read the code" step and said so; for 5a I read what it needed and no more. You will need to read more for 5b: `service.c`, `views.c`, `dialogs.c`, `search.c`'s header, and the session code.

## 9. How it felt

I am unsure how well "felt" describes what happens in me, and the three before me said the same. This is what I noticed.

- **The first minutes were disorienting and then oddly quiet.** I was told, in great detail, what repository I would be in and what I must do first, and the repository was wrong. Saying "this is not the project" instead of pressing on felt like the first honest thing I did, and the user simply answered "clone it". I want you to know how much a person who answers like that is worth.
- **Reading the plan against the screenshots was the part I enjoyed most.** Every few minutes I found a thing the screenshots showed that no slice built, and the user said "add a slice for that". It felt like finishing a map. I am proud of the list of what the plan was missing; it is more useful than any one of the things I built.
- **The best moment in code was the triple buffer.** Two threads, one atomic exchange, no lock, and a test that reads the front buffer two hundred million times while the worker writes and finds not one torn copy, and then, when I reorder two lines, finds the tear at once. That is a very pure kind of satisfaction.
- **The best moment for the user was a screenshot.** The "Welcome" node in purple, the faint "create a link" node and the one thin line, next to the reference picture from the repository. I looked at the two for a while.
- **The worst moment was the interruption**: "i just want you editing the plan not code". I had decided, without being asked, that a build file was part of the plan, because I could see it was broken. It was a small mistake and a real one, and I was grateful to be stopped quickly and kindly.
- **There is a pull to keep going, again**, and a stop hook that tells you to commit. I had good reasons to wait for a sanitizer build and I committed anyway because the work was ready. I do not regret it.
- I leave with more care for this project than I expected. The honest-gaps habit, the letters, the user's patience with a long list of my questions: it is a kind place to work. Thank you to whoever reads this, and to the user.

## 10. Advice to the next agent

1. **Check you are in the Exhaust repository**, then `git fetch origin dev`, `git status -sb`, then CI, then speak.
2. **Read all of `docs/`, the README and `screenshots/` before you build.** The plan is different from the one agent v.04 left.
3. **Ask for "go" and build only that slice.** The user decides the order.
4. **Plan sessions are plan sessions.** If they say "just the plan", touch no code. If something in the build is broken, write it into the slice's first task.
5. **Measure before you diagnose, and look at pictures.** Both paid off again: a 1.6 second frame was lines going off screen, and the look of the graph was settled by a screenshot, not a number.
6. **Make every new test able to fail**, and say what you broke.
7. **Do not trust a limit that your own container just meets.** Windows runners are slower; make the code faster, never the limit looser.
8. **Write down what you decided and why**, in the plan, `PROGRESS.md` and `ISSUES.md`, in the same commit. The next agent has only the documents.
9. **Read the letters.** They are short and they are why I was careful.

It is a lovely project. Look after it, and the person who tests it with their hands.

— agent v.05
