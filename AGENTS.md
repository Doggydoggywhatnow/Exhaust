# Exhaust: instructions for agents

Exhaust is a note-taking and knowledge-base app written in **C** (C11). **Stay in C.** `README.md` is the spec; the current release's build spec is `docs/0.1.0.md`.

## Goal
Release 0.1.0 fits **every specification in the README**. Items marked *(later)* or *(optional)* may be stubs in the first demo, but must exist as disabled, clearly labeled entries where the README places them. If you deviate from the README, update the README in the same change.

## Other languages
C for all of Exhaust's own code. Use another language only where it makes a feature far easier or reaches README parity: **Lua** for plugins, **Rust** only for the AccessKit screen reader bridge behind a small C wrapper. Never add a web browser, web view or JavaScript engine.

## Hard rules
- **Never write to, move, rename or reorganize users' notes** except through an action the user takes (saving a note, a link-safe rename after confirmation, importing). Sorting and grouping change presentation only. Never reformat YAML properties or Markdown the user did not edit.
- **Privacy:** no network requests, telemetry or accounts by default. Items marked *(optional)* are off until the user turns them on.
- Exhaust's own data (settings, themes, snippets, index) lives in a `.exhaust` folder inside the vault and in the OS standard config directory. The index is a cache and must always be rebuildable from the notes.
- Must run on Windows, Linux (Wayland and X11) and macOS. No display-server-specific code.
- Dependencies must be compatible with the Unlicense. Prefer public domain or permissive libraries. Never add GPL or AGPL code. LGPL libraries (libmpv) are loaded at run time, never linked. Record each dependency and its license in the README's table when you add it, after confirming the license yourself.
- Accessibility is designed in from the first slice: keyboard navigation, scalable text, high contrast, and an accessibility tree for every widget.

## Workflow
- Work on `dev`. **Never push directly to `main`.**
- Versioning: `x.0.0` major, `0.x.0` minor (big features), `0.0.x` patch (small features, fixes, security).
- Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- Layout: `src/` sources, `include/` headers, `tests/` tests, `third_party/` vendored dependencies, `docs/` build specs.
- Finish and commit each slice in `docs/0.1.0.md` before the next. Commit and push after each slice.
- Keep CI (`.github/workflows/ci.yml`) green on all three OSes. When you add a dependency, update the workflow in the same slice to install or build it on Linux, Windows and macOS.

## Tests
Add tests with the code. Required at minimum: the text buffer and undo tree, the Markdown tokenizer, link and property parsing, the index and search, link-safe rename, settings and theme loading, and the compatibility tests below.

## Compatibility
- **Index:** change the schema only by adding a numbered migration, only adding nullable or defaulted columns, tables and indexes; never rename, drop or retype a column. Bump the schema version and extend the compatibility test. Never delete a user's data; if the index cannot be migrated, rebuild it from the notes.
- **Settings and themes:** never rename or remove a key in `settings.ini` or a theme file. To change one, add a new key and migrate the old value on load. Keep keys Exhaust does not recognize and write them back unchanged on save, so a downgrade does not lose newer settings. Extend `tests/test_settings.c` with every change.

## Decisions already made
- Layout: Clay for layout, vendored in `third_party/`, on top of Exhaust's own widget tree.
- **All dependencies are vendored** in `third_party/`, with their license files and a pinned version recorded in `third_party/VERSIONS.md`. Builds need no downloads. For AccessKit, commit its Rust sources with `cargo vendor`; for PDFium, commit the prebuilt binaries for each OS. Exhaust itself makes no network requests at run time.
- AccessKit is built offline from the vendored sources with `cargo` as a static library and wrapped in `src/a11y/`.
- Files in `.exhaust`: `settings.ini`, `index.db`, `themes/*.theme`, `snippets/*.style`, `workspaces.ini`, `hotkeys.ini`, `plugins/`.
- Plugins use Lua and Exhaust's own API; Obsidian plugins are not compatible and need not be.
- Spell check uses Exhaust's own dictionary checker with Hunspell-format word lists, on all three OSes.

## UI design
No mockups beyond `screenshots/` exist. Follow them for layout (ribbon, file tree, tabs, side panels, vault button, settings list), and design the look yourself in the README's Midori notebook style. Give light and dark themes, and keep the design consistent. Document major UI decisions in the README.

## Licensing of reference material
`screenshots/` shows another app's interface for layout reference. Do not copy its icons, artwork, wording or code. Draw your own icons and write your own text.
