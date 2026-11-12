# Exhaust: instructions for agents

Exhaust is a note-taking and knowledge-base app written in **C** (C11). **Stay in C.** `README.md` is the spec; the current release's build spec is `docs/0.1.0.md`.

## Goal
Release 0.1.0 fits **every specification in the README**. Items marked *(later)* or *(optional)* may be stubs in the first demo, but must exist as disabled, clearly labeled entries where the README places them. Things listed under the README's **Non-goals** are not built. `*(optional)*` means the feature uses the network and is off by default. Other features that are off by default, such as Vim mode, are not marked `*(optional)*`. If you deviate from the README, update the README in the same change.

## Other languages
C for all of Exhaust's own code, with these exceptions only:
- **Lua** for plugins.
- **Rust** only for the AccessKit screen reader bridge (`src/a11y/`), behind a small C wrapper.
- **Objective-C**, at most 150 lines, only in `src/platform/` for macOS (trash and similar).
- **Assembly**, only the vendored lib-Peep (NASM for x86-64, the GNU assembler for ARM64). No assembly in Exhaust's own sources.
- **JavaScript** only for the web clipper browser extension in `extras/clipper/`. It runs in the user's own browser, not in Exhaust.

Never add a web browser, web view or JavaScript engine to Exhaust itself.

## Hard rules
- **Never write to, move, rename or reorganize users' notes** except through an action the user takes (typing in a note, which Exhaust saves continuously; a link-safe rename after confirmation; importing). Sorting and grouping change presentation only. Never reformat YAML properties or Markdown the user did not edit. `.base` files belong to Exhaust and may be rewritten, but unknown keys must be kept.
- **Privacy:** no network requests, telemetry or accounts by default. Items marked *(optional)* are off until the user turns them on.
- **Where data lives:** settings, themes, snippets, hotkeys and workspaces are plain text in a `.exhaust` folder inside the vault. The search index and other caches live in the **OS cache folder, keyed by vault id, never inside the vault** (a SQLite file in a synced folder corrupts). The index is a cache and must always be rebuildable from the notes.
- **Recovery journal and session state are user data.** They live in the OS local-data folder, keyed by vault, **not** the OS cache folder (the OS may clear a cache) and **not** inside the vault (sync tools would copy them between devices); the exact folders are in `docs/formats.md`. Never delete them except after the text is safely in the note file, and never rebuild them from scratch. Exhaust saves continuously (see the README's Saving): never add a code path that can lose typed text.
- Must run on Windows, Linux (Wayland and X11) and macOS. No display-server-specific code. Platform code lives in `src/platform/`.
- Dependencies must be compatible with the Unlicense. Prefer public domain or permissive libraries. Never add GPL or AGPL code. LGPL libraries (libmpv) are loaded at run time, never linked. Record each dependency and its license in the README's table when you add it, after confirming the license yourself.
- Accessibility is designed in from the first slice: keyboard navigation, scalable text, high contrast, and an accessibility tree for every widget. No hard-coded colors, sizes or fonts: use theme tokens from slice 1a-iv.
- Every feature registers its commands in the command registry and its switch in the feature-toggle registry from sub-slice 2d onward.

## Dependencies
- **Vendor everything** in `third_party/`: source and headers, with each license file, and a pinned version recorded in `third_party/VERSIONS.md`. Core builds need no downloads. Commit AccessKit's Rust sources with `cargo vendor`.
- **Exception, large optional libraries (PDFium, libmpv):** vendor only their headers. Load them at run time (`dlopen` or `LoadLibrary`). Without them show "Open externally". They ship only in a separate optional add-on package, which the packaging job builds from pinned, checksummed upstream releases listed in `third_party/VERSIONS.md`.
- **[lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep)** (Unlicense, version 0.5.0) is vendored and linked statically. It is hand-written assembly for x86-64 and ARM64 with no libc or `malloc`; it plays 16-bit PCM WAV built in, and the `wav` (other bit depths and float), `wavc` (A-law, mu-law, IMA and Microsoft ADPCM) and `flac` (4 to 31 bit) codecs are separate. The pull API in `peep.h` is `peep_open`, `peep_fill`, `peep_fill32`, `peep_seek`, `peep_close`, `peep_set_gain`, `peep_set_dither` and `peep_flac_metadata`. Its `main` branch has the assembly and docs only and `peep.h` is on its `dev` branch; building it needs `nasm` (x86-64) or the GNU assembler (ARM64) in CI. Read its README and source before using it. Vendor `peep.h` from its `dev` branch, with the assembly from the same commit. The first task of sub-slice 6c-ii is a CI build of it on Linux, Windows and macOS (install NASM and the GNU assembler in CI as needed). lib-Peep is the only WAV and FLAC decoder and has no fallback: if a platform fails to build, fix it in lib-Peep. The real vendored size is still to be measured. Note what you assumed in your commit message and in the README. MP3 and OGG use dr_mp3 and stb_vorbis. Other audio and video go through libmpv.
- File watching uses dmon (single-header C) or our own inotify, ReadDirectoryChangesW and FSEvents code. Do not use efsw (C++).

## Workflow
- Work on `dev`. **Never push directly to `main`.** (The repository owner may tell you otherwise for a specific push.)
- Versioning: `x.0.0` major, `0.x.0` minor (big features), `0.0.x` patch (small features, fixes, security).
- Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- Layout: `src/` sources (`src/platform/`, `src/a11y/`), `include/` headers, `tests/` tests (`tests/fuzz/` fuzz targets), `third_party/` vendored dependencies, `tools/` scripts, `extras/clipper/` the web clipper extension, `docs/` build specs.
- Finish and commit each sub-slice (for example 2a) in `docs/0.1.0.md` before the next; the table order is the build order and each sub-slice lists what it depends on. Commit and push after each sub-slice.
- Keep CI (`.github/workflows/ci.yml`) green on all three OSes. When you add a dependency, update the workflow in the same slice.

## Quality gates (CI)
- **Size budget:** the core install is at most 25 MB (`tools/check_size.sh`).
- **ASan and UBSan** job.
- **libFuzzer targets** for the Markdown tokenizer, frontmatter, HTML subset, canvas JSON, base expressions and importers. Add each when its module is written.
- **Hardening flags:** `-fstack-protector-strong -D_FORTIFY_SOURCE=2` where supported.
- **Accessibility:** an automated accessibility-tree dump test, because a screen reader cannot run in CI. Reading with a real screen reader is on the maintainer checklist in `docs/0.1.0.md`.
- CI gains the cargo setup and a libFuzzer job in slice 1b-i.

## Tests
Add tests with the code. Required at minimum: the text buffer and undo tree, the Markdown tokenizer, link and property parsing, the index and search, link-safe rename (all the cases in the README), the Bases expression engine against the documented examples, settings and theme loading, and the compatibility tests below.

## Compatibility
- **Journal and session state:** their file formats (in `docs/formats.md`) are frozen after release like every other name. A newer journal must replay in an older version or be left untouched, never discarded.
- **`docs/formats.md`** is the source of truth for every name that is frozen after release: settings keys, theme tokens, the snippet grammar, `hotkeys.ini`, `workspaces.ini`, the plugin manifest, slash commands and the Lua API. **Add a key or name to `docs/formats.md` before adding it to the code**, with its type, default and the version it first appears in. Resolve every *TODO-verify* there before implementing the item.
- **Index:** change the schema only by adding a numbered migration, only adding nullable or defaulted columns, tables and indexes; never rename, drop or retype a column. Bump the schema version and extend the compatibility test. If the cached index cannot be migrated, delete the cache and rebuild it from the notes. Never delete or alter a user's notes.
- **Settings and themes:** never rename or remove a key in `settings.ini` or a theme file. To change one, add a new key and migrate the old value on load. Keep keys Exhaust does not recognize and write them back unchanged on save, so a downgrade does not lose newer settings. Extend `tests/test_settings.c` with every change.

## Decisions already made
- Threads: the UI thread does input and drawing only and never blocks on disk, the index or layout for more than 16 ms; save and journal I/O, the file watcher, the indexer and graph layout run on worker threads that talk to the UI through a message queue; SQLite uses WAL mode with one writer connection and one reader per thread that needs one.
- Rendering: SDL_Renderer, a FreeType glyph atlas and our own tessellator for lines, curves and circles.
- Layout: Clay, vendored, on top of Exhaust's own widget tree.
- Search: SQLite FTS5 with the `trigram` tokenizer (queries under three characters fall back to a scan), PCRE2 over the candidate files, and tables for lines, blocks, sections and tasks so `line:`, `block:`, `section:`, `task:`, `task-todo:` and `task-done:` work.
- Files in `.exhaust`: `settings.ini`, `themes/*.theme`, `snippets/*.style`, `workspaces.ini`, `hotkeys.ini`, `plugins/`; their formats are in `docs/formats.md`. The index (`index.db`) is in the OS cache folder.
- Note names forbid `# ^ [ ] |` and `:`, `%%`, and Windows reserved names. Links match case-insensitively after NFC normalization.
- Saving: every edit goes to the append-only recovery journal within 200 ms, and the note file is written atomically after 1 second idle or every 5 seconds at most (`autosave_idle_ms`, `autosave_max_ms`, `journal_flush_ms`); on an outside change with unsaved edits, keep the buffer, show a conflict banner with a diff, and snapshot to file recovery first. Preserve CRLF and BOM. UTF-8 only.
- Plugins use Lua and Exhaust's own API; Obsidian plugins are not compatible.
- Math uses STIX Two Math with `hb-ot-math`. Mermaid in 0.1.0: flowchart, sequence, class, state, ER, pie and gantt.
- Spell check: own checker, `PFX`, `SFX`, `REP` and `KEY` only, no compounding, English (US) only in 0.1.
- Fonts: Atkinson Hyperlegible, Atkinson Hyperlegible Mono for code, system fonts discovered by scanning the OS font folders, no bundled emoji font.
- Date formatting uses an own formatter for Moment-style tokens.
- PDF export uses an own PDF writer (fonts subset through FreeType, deflate from miniz); exports are always light-styled.
- There is no auto-updater. A manual update check is optional and off until the user presses it.

## UI design
The screenshots in `screenshots/` are the **skeleton of how Exhaust should look**. Build the layout to match them: ribbon on the far left, file tree with the vault button at its bottom, tabs across the top, side-by-side panes for notes, graph, canvas and reference notes, the vault manager (`screenshots/vault-manager.png`: a list of vaults with a menu each, and buttons to create a vault, open a folder and open a synced vault as a stub), the hotkeys list (`screenshots/hotkeys.png`), and how unresolved and resolved links look in text and in the graph (`screenshots/unresolved-link.png`, `screenshots/resolved-link.png`). The 28 pages in `screenshots/settings/` are the guideline for Exhaust's settings screens: follow their layout, grouping and options, which the README's Settings section lists in words. Colors come from the Palette table in `docs/formats.md` (Exhaust Dark is the screenshots' palette; use those values, do not sample or invent colors); design the rest of the look (shapes, spacing, icons) yourself in the README's Midori notebook style. Do not copy pixels, icons or text from the screenshots. Give light and dark themes, and document major UI decisions in the README.

## Licensing of reference material
`screenshots/` shows another app's interface for layout reference. Do not copy its icons, artwork, wording or code. Draw your own icons and write your own text. The UI must not use Obsidian's name or logo, or present itself as Obsidian; Exhaust's colors are the Palette in `docs/formats.md`. The screenshots stay in the repository for now, for development.
