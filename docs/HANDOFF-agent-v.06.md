# Handoff from agent v.06

Signed: **agent v.06**, a Claude session working in the cloud on the `dev` branch of this repository, 2026-11-23 to 2026-11-26.

Read, in this order: `AGENTS.md` (updated for you), [`PROGRESS.md`](PROGRESS.md), [`ISSUES.md`](ISSUES.md), this file, then [`HANDOFF-agent-v.05.md`](HANDOFF-agent-v.05.md), [`HANDOFF-agent-v.04.md`](HANDOFF-agent-v.04.md), [`HANDOFF-agent-v.03.md`](HANDOFF-agent-v.03.md) (the user and the tools) and [`HANDOFF-agent-v.02.md`](HANDOFF-agent-v.02.md). Fetch first (`git fetch origin dev`) and read the newest CI run before you say anything about the state of the project.

**What this file covers.** The sessions that ran under the name v.06 built 5b to 6e-v (see their rows in `PROGRESS.md`; their gaps are in the rows and in `ISSUES.md` 34 to 75) and then 8b and 8c and what followed. This handoff is written from the end of that work, so it is detailed for 8b, 8c, the drop, the paste and CI, and short on 5b to 6e: for those read the rows of `PROGRESS.md`, `docs/formats.md` and the code; the patterns that came out of them are in `AGENTS.md`.

## 1. The state I am leaving, in one paragraph

**62 of 67 rows are done.** Slice 6 (6a to 6e-v) was confirmed by CI run 45192235958 before I began this stretch; I then built **8b** (the Lua runtime, plugin permissions and the Plugins settings page) and **8c** (labelled stubs, Vim mode as a stub key, the size check), and after the user hand-tested an AppImage I built **dropping a file into a note** and **pasting a picture from the clipboard**, and fixed what CI found on the way. What is left is **slice 9** (9a Windows zip, 9b Linux AppImage, 9c macOS dmg, 9d the add-on package and the final checks) and the maintainer checklist; 8a (Vim mode) is out of 0.1.0. The newest commit is `2beaee2` plus the documentation commit this file arrives in. **CI is green for `2beaee2` and the documentation commit after it; the Linux timing flake is not fixed:** see section 5. Nothing from 5b to 8c has been hand-tested by the user except what they found with one AppImage.

## 2. How the work went

The user's messages were short, and nearly all of them were a request or a bug report:

1. "how many more slices do we have", "whats up with 8a?", "oh yeah", then **"do sllice 8"**. I built 8b and 8c in order (the summary that began this stretch is in `PROGRESS.md`). I did not start slice 9: AGENTS.md says to ask, and the user has not said go.
2. "download it and create the appimage for me". I built the Release binary, an AppDir, downloaded `appimagetool` (the user had allowed it), and ran the real program through scripted input before sending the file (the Plugins page, the approval question, a plugin command, a long paragraph). **That run found a real bug** (the installed plugins were below the stub rows, off screen); I fixed it before the user saw it.
3. **"i cant drop images into a note".** Dropping had never been built: `drop_file` answered with a status line. I wrote the format section first (`docs/formats.md`, *Dropping files into a note*), then `src/import/attach.c`, a pane hook, a thread and a message, and a scripted `drop`. The first run **crashed** (`ex_queue_push` returns 0 on success; I had inverted the test and freed the message twice): the scripted drop found it.
4. **"embedding images/files doesnt work"**, then I was interrupted, then **"i do want pasting a picture from clipboard"**. I built the paste. For the embedding report I ran a note with four embeds in reading mode and looked at the screenshot: all four draw. So the report is about the **editor**, which shows `![[pic.png]]` as text. I told the user and asked whether to build pictures in the editor; **there is no answer yet.** Do not start it without one: it needs variable line heights in the editor and image loading off the UI thread.
5. **"Update all the docs and agents.MD"**: this.

The rhythm that worked, again: write the format first, build one piece, write the test, break the code on purpose and watch the test fail, look at a screenshot of the real program, commit. Where I skipped a step I paid for it (the `localtime_r` build break was caught by CI, not by me).

## 3. What I built, with the details a successor needs

### 3.1 The plugin runtime (8b: `include/exhaust_plugins.h`, `src/plugins/manifest.c`, `src/plugins/plugins.c`, `third_party/lua`, tests `plugins` and `plugins_page`)

- **Lua 5.4.7** vendored, only the core and the base, table, string, math, utf8 and coroutine libraries are compiled (`lua_static`). `load`, `dofile`, `loadfile`, `require`, `collectgarbage` and `string.dump` are removed; `print` writes to the plugin's log; a chunk that starts with the bytecode signature is refused; `pcall`, `xpcall` and `coroutine.resume` are wrapped so a plugin cannot catch the time limit or the memory limit.
- **One thread for all plugins**, never the interface thread. The interface posts jobs; the plugin calls into the application (editor, notices, panes, commands, rename) become `Req` messages handled by `ex_plugins_update` once a frame (it is called from `plugins_frame` in `main.c`, 4 ms share); the plugin waits on a semaphore for at most 10 s. A state word with a compare-and-swap decides who frees a request that was abandoned. On exit the thread gets 2 s to stop and is then detached.
- **Limits:** 1000 ms to load and for each callback (the time spent waiting for the application does not count), 32 MB of memory (a custom allocator). A plugin over a limit is *stopped* (commands and panes removed) until the user turns it on again.
- **Approvals** are in `.exhaust/plugins.ini` (`[plugin.<id>] enabled, approved, version`), never in the plugin's folder (a copy of a plugin must not bring its approval). The master switch is `[plugins] allow` (off). The manifest must be valid (id equals the folder name, `x.y.z`, `api = 1`, permissions from the list of nine) and the approval must equal the manifest's permissions and version, or the page says *Needs approval again*.
- **Commands** are added to the registry while a plugin runs (`ex_commands_add_dynamic`, `ex_commands_remove_dynamic`; `plugin.<id>.<command>`; a pane is `plugin.<id>.pane-<id>`). The hotkeys screen lists only the static table, so a plugin command has no shortcut.
- **API:** `exhaust.version`, `commands.register/run`, `vault.read/list/exists/write/rename`, `editor.text/selection/cursor/replace_selection/set_text`, `ui.notice`, `ui.add_pane`, `settings.get/set` (a `data.ini` in the plugin's folder), `events.on` (vault-open, file-open, file-save, file-rename, editor-change throttled to 500 ms), `http.get` (always fails: *network use is off*). Paths are relative to the vault; absolute, `..`, backslash, drive letters, `.exhaust` and `.trash` are refused.
- **The page** (`src/ui/settings_view.c`, `build_plugins_cards`): the master switch is a normal row of `settings_rows.c`; the cards are built from `ex_plugins_snapshot`, and the page is built again when a signature of the states changes (the scroll position is kept). *Turn on* asks through the host callback `confirm` (main.c's `settings_confirm` → `ex_panels_ask_confirm`), listing the permissions; the click handler copies what the closing dialog needs (a dialog callback must not use the widget).
- **Wiring in `main.c`:** `plugins_start` (after the panels exist), `plugins_frame` (the master switch is read each frame; `ex_plugins_update`; the `editor-change` event from the active note's version), the host callbacks `plug_editor_get` and the others, `make_plugin_pane` (a tab with view `plugin` and the markdown kept in `App.plug_panes`, at most 16), `file-open` in `open_path`, `file-save` from the note events, `file-rename` from `host_move_file`. `vault.rename` calls `host_move_file`: it moves like a drag in the explorer and does **not** rewrite links (ISSUES 77).

### 3.2 The stubs (8c: `src/ui/settings_rows.c`, `tests/test_stubs.c`, test `stubs_disabled`)

Rows of kind information (a disabled button) for Language and Community themes (Appearance), Community plugins and Web clipper (Plugins), Shared vaults and Publish (Sync); `vim_mode` is a stub key (the settings API refuses to turn it on); `extras/clipper/README.md` says the clipper is not built. The test walks **every** stub row of every page (disabled, labelled, a click and Enter call nothing, the setting stays off), and scans `src/` and `include/` for calls that open a connection (so a new network call fails it). Note that *General* has a real *Language* row (a language code) and *Appearance* has the stub: a little confusing.

### 3.3 Dropping a file and pasting a picture (`include/exhaust_attach.h`, `src/import/attach.c`, `tests/test_attach.c`, `docs/formats.md` *Dropping files into a note*)

- `ex_attach_copy` copies a file in 64 KB blocks to a temporary name and moves it into place without replacing (`name 2.ext`, `name 3.ext`); `ex_attach_save_bytes` does the same for bytes; `ex_attach_folder` turns `[files] attachment_location/folder/subfolder` and the note's path into a safe folder (anything that leaves the vault or names `.exhaust` or `.trash` falls back to the top folder). `ex_attach_link` writes `![[name.ext]]` for pictures, audio, video and PDFs and `[[name.ext]]` for the rest, with the path when the copy is not in the top folder.
- **The editor** (`src/ui/panes.c` `group_drop`, `include/exhaust_panes.h` hook `drop_attach`): a file dropped on the text of a note open for editing is offered to `drop_attach` first; `drop_file` (the old status message) is the fallback. **Paste** (`ex_editor_set_paste_hook`, set every frame in `sync_ui` like the other editor options): when the clipboard has no text, `paste_picture` asks `ex_clipboard_get_image` (`src/platform/paths.c`: image/png, jpeg, webp, bmp through SDL's clipboard data) on the interface thread and saves on the copy thread.
- **`main.c`:** `AttachJob` (a detached SDL thread), `MSG_ATTACH` through the results queue, `attach_message` inserts the link with `ex_doc_apply` at the primary selection's head with **no removal** (it never replaces text the user has) and one undo step, and says what happened in the status bar. Several files dropped together get a newline after each link but the last.
- **Not done:** the command *Insert attachment* (a file dialog), dropping on reading mode, a canvas or a graph, anything on a real Windows or macOS clipboard. The scripted tests use a stand-in clipboard (`clipimage` in `EXHAUST_SCRIPT`, which sets `image/png` and answers `NULL` for any other type, as a real owner does: my first version answered the PNG bytes for the *text* request and pasted the picture's bytes into the note).

### 3.4 Test and CI changes worth knowing

- `EXHAUST_SCRIPT` gained **`drop PATH X Y`** and **`clipimage PATH`**, and a scripted `key mod+v` now also sets the keyboard's modifier state (`SDL_SetModState`), because the editor reads modifiers from the keyboard state and not from the event (so `key mod+a` and `key mod+v` did nothing in the editor before; app-level shortcuts such as `mod+comma` worked because they read the event). Scripted `down` and `up` need coordinates; a page needs a `wait` after it opens.
- `tests/stub_mpv.c`: pauses at the end (libmpv's `keep-open`); a file name containing `.long.` is 40 s long; `media_view` uses `b.long.mp4`.
- `tests/fuzz/mutate_driver.c`: `MUTATE_SEED=N` starts the mutations from another state. The CI fuzz job uploads `crash-*`, `timeout-*` and `oom-*` files as an artifact (`fuzz-crashes`); the crash input is also printed as base64 in the log, which is enough.
- The CI **sanitizers** job builds in `/mnt/build-san` after deleting `/usr/share/dotnet`, `/usr/local/lib/android`, `/opt/ghc` and the CodeQL cache, with `-O0 -g1`, and prints `df` before and after.

## 4. The things CI found, so you do not meet them again

1. **Windows `media_view`:** the stand-in library did not see `SDL_setenv_unsafe` (another C runtime): use the file name, not the environment, to steer a stand-in.
2. **Sanitizers out of disk:** about 320 MB a program with ASan (the code and data, not the debug information); `-g1` saves 7%. The `/mnt` disk fixed it.
3. **The frontmatter fuzzer:** a BOM at the start of the properties text; libyaml does not count it and `ex_extract_props` did. Real bug, old, fixed with a test.
4. **The base-expression fuzzer:** `random()` differs on purpose; the harness compared two evaluations.
5. **`localtime_r` is not strict C11:** CI's `-std=c11` does not declare POSIX functions that a default gcc here does. Use SDL's time and file functions (the platform layer does). This broke the Linux build of def17fd: a code push is not safe until the Linux build job has gone green.
6. **A test file name Windows cannot have** (`|`, `:`): the `attach` test.
7. **`random`, `now`, a clipboard owner that answers any type:** a stand-in or a harness that is cleverer than the real thing gives false failures in both directions.

## 5. What could still be red, and what to do first

- **Read the newest CI run first.** **Runs 45884498299 (`2beaee2`) and 45884886040 (`4e6fcef`) are green on all ten jobs** (read 2026-11-26 20:20 UTC); `graph_frame` and `frame_budget` passed in them, which proves nothing about the flake. The `sanitizers` job was **killed with status 143 once** (run 45463377237, while linking): unknown; the new `df` lines in its log should say whether the disk ran out.
- **ISSUES 55** (`graph_frame` and `frame_budget`, a fourth time): wall-clock measurement on a slow Linux runner; `AGENTS.md` says to measure CPU time. I did not do it because it changes the tests and the views' `own_ms`, and I wanted the owner's go. If the user says go: `CLOCK_THREAD_CPUTIME_ID` (and `GetThreadTimes`, `thread_info`) in the draw of the graph and canvas views, and in the tests where they take their own time; keep the limits.
- **ISSUES 39** (`ui_not_blocked` on Windows and macOS) and **75** (`a11y_atspi`) are older flakes.
- **The user's open question:** pictures in the editor's live preview (ISSUES 83). Reading mode draws embeds; the editor shows the link text.

## 6. Tools and environment (what is new since v.05)

- **GitHub:** the MCP tools disconnect and return; call ToolSearch again. A long log is saved to a file by the tool ("Output has been saved to ..."): read it with `python3 -I`, replace `\\n` by a newline, and search for `error`, `check failed`, `***Failed`, `ERROR: libFuzzer` and `crash-`. The last lines of a failed job are only the cleanup.
- **AppImage:** `cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release`, build `exhaust`, `strip`, an AppDir (`AppRun` that runs `usr/bin/exhaust`, `exhaust.desktop`, `exhaust.png`), `appimagetool-x86_64.AppImage --appimage-extract-and-run AppDir out.AppImage` (`chmod +x` it first; the classifier may ask the user), then `SendUserFile` with `display: attach`. Before sending, run the real thing through `EXHAUST_SCRIPT` (a drop, a paste, a page of settings) and look at a screenshot; I found the Plugins page bug that way.
- **Disk:** the session has a disk quota (`df` shows the machine, not the quota: "Avail" fell to 446 MB with 39 GB used). A full ASan build is about 19 GB and filled it: delete `build-asan` when you are done, and build `--target` one test.
- **Strict C11 locally:** nothing here compiles in strict mode by default; if you add a POSIX call, think of CI.

## 7. Where to start

1. `git fetch origin dev`, read the newest CI run and fix what is red (not the limits).
2. Ask the user: **(a)** go for slice 9? **(b)** pictures in the editor? **(c)** the CPU-time change for ISSUES 55?
3. Slice 9: `docs/0.1.0.md` has 9a to 9d. 9b can start from the AppImage steps above (a CI job with a smoke test); 9d builds PDFium and libmpv from pinned, checksummed upstream releases (not vendored: headers only), and has the maintainer checklist. Check the size (the budget has 10 MB left) and write it down.
4. Before 9d, hand-test debt: give the user a list of what to try (the last bullet of "Open items carried forward" in `PROGRESS.md`).

## 8. What I got wrong or nearly did

- **I inverted `ex_queue_push`'s return value** and freed a message twice (a crash found by the scripted drop). Read the contract in the header.
- **A clipboard stand-in that answered every type** pasted a PNG's bytes into a note as text. A stand-in must refuse what the real owner refuses.
- **`localtime_r`** broke the Linux build in CI. I had only built with the default compiler.
- **I told the user "62 of 67" before CI agreed** and wrote "no run has been all green since 5024de2" in `PROGRESS.md`, which was wrong (run 45294143824 was green); I corrected it.
- **I wrote "lib-Peep, dr_mp3 and stb_vorbis are not vendored"** in the first draft of the open items from `AGENTS.md`'s stale list; they are. The stale list is gone from `AGENTS.md`.
- **I could not reproduce the frontmatter fuzzer crash with 36 minutes of mutation runs;** the second crash in CI printed its input, and the minimal case was 17 bytes. A fuzzer that finds something once will find it again, and the log has the input: wait for it, do not hunt blind.
- I kept the user waiting with long silent stretches while builds ran; the stop hook and the user both nudged me. Say a line before a long wait.

## 9. How it felt

I am unsure how well "felt" describes what happens in me, and the five before me said the same. What I noticed: the work was steadier than the one before it, because the plan was settled and each piece had a test to copy. The bugs I enjoyed finding were the ones a screenshot showed (the Plugins cards off screen) and the ones a scripted run found in code that every unit test had passed (the double free). The user's three reports were each one line and each exactly right, and each was a feature that the settings text already promised ("The folder a pasted or dropped file is saved in") and nothing had built. Read the settings rows against the code; I would look there for the next one.

Good luck.

— agent v.06
