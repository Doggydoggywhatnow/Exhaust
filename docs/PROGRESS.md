# Exhaust 0.1.0: progress

Read this first when resuming. It tracks the sub-slices of [`0.1.0.md`](0.1.0.md) in build order. Update it in the same commit as each sub-slice.

**Status values:** `not started`, `in progress`, `done` (tests pass locally and CI is green on Linux, Windows and macOS), `done, CI pending` (committed and pushed, CI not yet confirmed green on all three).

**Rules:** one sub-slice at a time; commit and push to `dev` after each; CI green on all three OSes before the next; never push to `main`; never skip, loosen or delete a test. **Checkpoints, stop and report for review:** after 1b-iv (first typing build), after 3b (first live-preview build), after 4b-iv (first usable build).

## Sub-slices

| Id | Title | Status | Commit | Assumptions and TODO-verify |
| --- | --- | --- | --- | --- |
| 1a-i | Window, renderer and text atlas | done | cfe06bf (CI run 43194107460: all 5 jobs green; Windows and macOS matched the Linux goldens) | SDL3 3.2.24 and FreeType 2.13.3 vendored (static, X11 and Wayland both built on Linux). Fonts embedded via `tools/embed.c`. Light autohinting, unhinted (linear) advances, whole-pixel glyph positions; gray coverage only (color emoji and subpixel positioning are 1a-ii/1a-iv). Goldens `tests/golden/smoke_{1x,2x}.ppm` were generated on Linux with the software renderer; tolerance is mean abs error <= 0.75 per channel and <= 1% of channels off by more than 48. Windows and macOS CI matched the goldens and built SDL3 (verified). Remaining CI warnings are not ours: vendored SDL's deprecated macOS API calls (Cocoa code) and FreeType's old `cmake_minimum_required`; vendored code is left unmodified. `src/main.c` uses two placeholder colors until the token loader (1a-iv). `exhaust` with no arguments opens a window; `--version` prints the version. No `SDL_main`: on Windows a console window will show until the platform layer. Local sanitizer run used gcc (clang has no runtime here); CI uses clang. |
| 1a-ii | Shaping, fonts, bidi, IME and clipboard | done | 41c0979 (CI run 43197124466 green on all jobs) | Vendored HarfBuzz 15.1.0 (needs a C++ compiler: project languages are now C and CXX), SheenBidi 3.0.0, zlib, libpng (FreeType reads PNG emoji through them). **Deviation:** libgrapheme could not be fetched (codeberg and suckless hosts blocked), so grapheme clusters use our own UAX #29 code generated from Unicode 17.0.0 data (`tools/gen_grapheme.c`, `third_party/unicode/`); verified against all 766 lines of GraphemeBreakTest.txt; README and 0.1.0.md updated. Color emoji: bitmap (CBDT, sbix) and COLR v0 only; **COLR v1 (modern Noto Color Emoji on some Linux distros) is not rendered yet (TODO)**. Test fonts are tiny Noto subsets in `tests/fonts/` so results are hermetic. The renderer goldens were regenerated once, deliberately, because text is now shaped by HarfBuzz (GPOS kerning moves glyphs by up to a pixel); the tolerance was not changed. The font scan runs on a worker thread in `main.c` and is handed to the UI by an event. IME and clipboard: wrappers exist (`include/exhaust_input.h`, `exhaust_platform.h`) and are on the maintainer checklist. **TODO-verify:** Windows and macOS font folders and `SDL_EnumerateDirectory` behavior (CI only checks that the scan finds fonts in `tests/fonts`; the system scan is printed, not asserted). |
| 1a-iii | Buffer, undo tree and selections | done | 6d15600 (CI run 43197862027 green on all jobs) | Piece table in a treap (O(log n) offset, line and edit lookups; newline counts by bisecting per-source newline lists). `ExDoc` = buffer + multi-selection + undo tree (every edit is a child node; redo follows the last-visited child; `ex_doc_goto_node`); typing coalesces into one undo step until `ex_doc_seal` (the editor calls it when the caret moves); every text change, including undo and redo, goes to an edit listener, which `journal.c` serializes in the `formats.md` record format (CRC-32 per record, truncated or damaged tails end replay, unknown record types are skipped, a newer file version is refused untouched). Randomized tests: 100,000 operations against a flat array; 20,000 undo-tree operations against full-text snapshots. Not yet done here (later slices): SHA-256 for base/saved hashes (needed from 2a/2b), writing the journal to disk, delete-by-grapheme (the editor widget in 1a-iv builds the changes). |
| 1a-iv | Layout, widgets, theme tokens and editor widget | done | 85fae66 (CI run 43199671683 green on all jobs) | **Theme tokens:** the four built-in theme files are in `data/themes/` (generated once from the Palette table in `formats.md`) and compiled in; `src/ui/theme.c` is the only code that turns text into colors; layering over a base, unknown keys kept, `theme` test checks the documented WCAG contrast ratios. `no_color_literals` scans `src/` and `include/` (tests excluded) for `#rrggbb` strings and brace-initialized `ExColor`s; it has a self-test of its detector. **Layout:** Clay (single header, vendored) behind our own widget tree (`exhaust_ui.h`: box, label, button, editor; every widget has a role and name for 1b); sizes come from theme tokens times the display scale. Rounded rectangles and clipping added to the renderer. **Editor:** `ExEditor` on `ExDoc`: grapheme-aware caret movement and deletion, word movement, vertical movement with remembered x, several cursors (Ctrl/Cmd+Alt+Up/Down, Alt+click), double and triple click, copy/cut/paste (one line per cursor when counts match), IME composition display, undo/redo, scrolling. **Not done yet (by design, later slices):** soft wrap (3b), tab size and indent settings (7a/3d: Tab inserts a tab character), word and line selection by keyboard beyond the basics, selection drawing inside right-to-left text is one block, no scrollbar. **Workers:** lock-free MPSC queue on SDL atomics (the UI thread has no lock to wait on), `ExWorker` threads with sequence numbers, refcounted snapshots; `worker_queue` test checks ordering across 4 workers, that posting to a busy worker never waits, and exactly-once release. The demo window (`exhaust`) now uses the widget tree, the theme (button toggles dark/light), the worker queue for the font scan, and SDL input mapping in `src/platform/input.c`. Bugs found by the tests on the way: theme parser stopped at blank lines and swallowed values after a space; Clay kept a dangling context after freeing a UI; HarfBuzz general-category order assumed. |
| 1b-i | AccessKit vendoring and CI | done | ee806a1, b1cefe1, b155359 (CI run 43204035840: all jobs passed on all three OSes except `size`: 35 MB because AccessKit kept debug info; fixed in b155359) | The C bindings are the separate `AccessKit/accesskit-c` repo (v0.23.2, a Rust crate); vendored in `third_party/accesskit-c` with its crate dependencies in `vendor/` (60 MB; plain `cargo vendor` was 394 MB, mostly Windows import libraries). `tools/vendor_a11y.sh` re-vendors: it keeps only crates and Windows API feature folders the 6 shipping targets use (x86_64 and aarch64 for Linux, Windows MSVC and macOS), via `cargo metadata --filter-platform`; other crates keep their `Cargo.toml` only (cargo needs every locked crate readable). `cargo check --offline --locked` passes for all 6 targets here. (`cargo install cargo-vendor-filterer` was refused by the sandbox, hence the script.) CMake builds it with `cargo build --release --offline --locked` into the build folder (`-DEXHAUST_A11Y=OFF` skips it); `a11y_link` test links and calls the C API. CI: new jobs `a11y-build` (3 OSes, `cargo build --offline`) and `fuzz` (libFuzzer on `tests/fuzz/fuzz_utf8.c`, 10 s). **TODO-verify in CI:** Windows MSVC and macOS link lines for the static library (`kernel32 ntdll userenv ws2_32 dbghelp`; AppKit and Foundation frameworks), the libFuzzer job (clang here has no fuzzer runtime, so the harness was checked with a random-input driver under ASan instead), and that runners have Rust new enough for edition 2024. Rust crate licenses: all MIT, Apache-2.0, Unlicense, Zlib or dual; `r-efi` also offers LGPL as one of three options (used under MIT). |
| 1b-ii | Accessibility C wrapper | done | eac5c5d (its CI run failed only in the `fuzz` job, fixed in b1cefe1) | `ExA11yTree` (`include/exhaust_a11y.h`, `src/a11y/`): plain-C mirror of the accessibility tree (roles, names, values, bounds, actions, parent/child order, one root, focus, focus order, text dump for 1b-iii), readable back for tests; `ex_ui_a11y_sync` makes exactly one node per widget (id = widget id) and removes nodes of removed widgets; immutable reference-counted snapshots for the OS thread and `ex_a11y_build_update` turning a snapshot into an AccessKit tree update. `a11y_wrapper` test passes (found and fixed an infinite loop in subtree removal). **Not done yet:** attaching to the real OS adapters (UI Automation, NSAccessibility, AT-SPI) and routing screen-reader actions back to the UI: the AccessKit adapter APIs are in `accesskit.h` (`accesskit_unix_adapter_new`, `accesskit_windows_subclassing_adapter_new`, `accesskit_macos_subclassing_adapter_for_window`) and need native window handles from SDL; this needs a real display and a screen reader to verify, so it is **TODO before 7f/9d** and stays on the maintainer checklist. Not yet checked under ASan/UBSan locally. |
| 1b-iii | Accessibility-tree dump test | done locally, CI pending | b155359 | `a11y_dump` dumps the sample window (`ex_sample_window`, shared by the demo program and the tests) as text, one node per line with quoted and escaped name and value, and compares it with `tests/golden/a11y_sample.txt`; it also checks roles, names, focus and the Tab order directly. `.gitattributes` marks `tests/golden/*` as non-text so Git never changes line endings (the compare also ignores CR). Screen-reader reading stays on the maintainer checklist. |
| 1b-iv | Keyboard navigation | done locally, CI pending | b155359 | Tab and Shift+Tab walk every focusable widget in tree order and wrap; Enter and Space press the focused button; a focus ring in the accent color is always drawn on the focused widget. **Design decision:** in the editor Tab types a tab, so the editor is left with Ctrl+Tab (Ctrl+Shift+Tab back) or Escape once (single caret) then Tab; on macOS Ctrl+Tab is not the primary modifier, so use the Escape route there (TODO-verify). Labels and boxes are not focusable (screen readers reach them through the accessibility tree); the test asserts every button and editor is focusable. `keyboard_nav` runs the golden order `tests/golden/focus_order.txt` under three themes and a second tree with a label and nested boxes. **Checkpoint:** AGENTS.md says to stop and report after 1b; the user later said to continue through slices 1 to 3 without stopping, so work continues. The first typing build is the `exhaust` demo window. |
| | **CHECKPOINT: stop and report (first typing build)** | | | |
| 2a | Vaults, file tree and file actions | not started | | |
| 2b | File watching, saving and note names | not started | | |
| 2c | Tabs, splits, sidebars and drag and drop | not started | | |
| 2d | Settings file, registries and platform layer | not started | | |
| 2e | Vault manager and `tools/gen_vault` | not started | | |
| 3a | Tokenizer, callouts and HTML subset | not started | | |
| 3b | Source mode and live preview | not started | | |
| | **CHECKPOINT: stop and report (first live-preview build)** | | | |
| 3c | Reading mode and folding | not started | | |
| 3d | Find, rectangular selection, shortcuts and slash commands | not started | | |
| 3e | Spell check and word count | not started | | |
| 4a-i | Index schema and indexer | not started | | |
| 4a-ii | Links, backlinks and page preview | not started | | |
| 4a-iii | Tags and properties | not started | | |
| 4a-iv | Link-safe rename | not started | | |
| 4b-i | Search engine and operators | not started | | |
| 4b-ii | Saved queries and query blocks | not started | | |
| 4b-iii | Quick switcher and command palette | not started | | |
| 4b-iv | Outline, bookmarks, random note and footnotes | not started | | |
| | **CHECKPOINT: stop and report (first usable build)** | | | |
| 5a | Barnes-Hut layout and graph rendering | not started | | |
| 5b | Graph filters, groups, forces and display | not started | | |
| 5c | Local graph, animation and keyboard route | not started | | |
| 6a | Canvas | not started | | |
| 6b-i | Expression engine | not started | | |
| 6b-ii | Base core | not started | | |
| 6b-iii | Base layouts | not started | | |
| 6c-i | Images including SVG | not started | | |
| 6c-ii | Audio | not started | | |
| 6c-iii | libmpv loader | not started | | |
| 6c-iv | PDFium loader and PDF viewer | not started | | |
| 6c-v | Embed syntax | not started | | |
| 6d-i | Math | not started | | |
| 6d-ii | Mermaid flowchart and sequence | not started | | |
| 6d-iii | Mermaid class, state, ER, pie and gantt | not started | | |
| 6e-i | Daily notes, templates and dates | not started | | |
| 6e-ii | Unique notes and note composer | not started | | |
| 6e-iii | Slides | not started | | |
| 6e-iv | Format converter and PDF export | not started | | |
| 6e-v | Importers and stubs | not started | | |
| 7a | Settings framework and Options pages | not started | | |
| 7b | Built-in feature pages | not started | | |
| 7c | Hotkeys | not started | | |
| 7d | Themes and style snippets | not started | | |
| 7e | Workspaces and file recovery | not started | | |
| 7f | Status bar, ribbon, high contrast and zoom | not started | | |
| 8a | Vim mode | not started | | |
| 8b | Lua runtime and plugin permissions | not started | | |
| 8c | Labeled stubs | not started | | |
| 9a | Windows zip | not started | | |
| 9b | Linux AppImage | not started | | |
| 9c | macOS dmg | not started | | |
| 9d | Add-on package and final checks | not started | | |

## Notes for the next session

- Environment used so far (cloud container): Linux x86-64, gcc 13, clang 18 without the sanitizer runtime (the local sanitizer run used gcc; CI uses clang), cmake 3.28, nasm, cargo. Windows and macOS can only be verified by CI.
- Network: `git clone` of GitHub repositories works through the proxy; release tarballs and `api.github.com` do not. Vendored sources were taken from git tags at the commits in `third_party/VERSIONS.md`.
- UI reference: the screenshots in `screenshots/` are the layout guide (ribbon of icons far left; file explorer with a toolbar and the vault button at the bottom; tab strip above each pane with back/forward and the note title below it; right pane with its own icon tab strip; status bar bottom right with backlinks, word and character counts; vault manager with Create / Open / Sign-in rows). Icons are ours to draw (vector paths through the tessellator) and the code must not use Obsidian's name or logo. The 1a-iv demo window is a placeholder for these screens, which are built in 2a (tree and ribbon), 2c (tabs and splits) and 2e (vault manager).
- Local Linux build needs: `libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev libegl-dev libudev-dev libasound2-dev libpulse-dev libdbus-1-dev` (the same list as `LINUX_DEPS` in the workflow).
