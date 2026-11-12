# Vendored dependencies

Everything the core build needs is in this folder; the core build downloads
nothing. Each entry gives the pinned version, where it came from, its license
and what was left out of the vendored copy.

| Library | Version | Source | License file | License |
| --- | --- | --- | --- | --- |
| SDL3 | 3.2.24 (tag `release-3.2.24`, commit `a8589a84226a6202831a3d49ff4edda4acab9acd`) | https://github.com/libsdl-org/SDL | `SDL/LICENSE.txt` | zlib |
| FreeType | 2.13.3 (tag `VER-2-13-3`, commit `42608f77f20749dd6ddc9e0536788eaad70ea4b5`) | https://github.com/freetype/freetype | `freetype/docs/FTL.TXT`, `freetype/LICENSE.TXT` | FreeType License (FTL) |
| Atkinson Hyperlegible (Regular, Bold, Italic, Bold Italic) | commit `1cb311624b2ddf88e9e37873999d165a8cd28b46` | https://github.com/googlefonts/atkinson-hyperlegible | `fonts/atkinson-hyperlegible/OFL.txt` | SIL Open Font License 1.1 |
| Atkinson Hyperlegible Mono (Regular, Bold, Italic, Bold Italic) | commit `154d50362016cc3e873eb21d242cd0772384c8f9` | https://github.com/googlefonts/atkinson-hyperlegible-next-mono | `fonts/atkinson-hyperlegible-mono/OFL.txt` | SIL Open Font License 1.1 |
| HarfBuzz | 15.1.0 (commit `3df40d9b98d6b32f0e4288d4ecfbb158ddb3a372`) | https://github.com/harfbuzz/harfbuzz | `harfbuzz/COPYING` | MIT (the "Old MIT" license) |
| SheenBidi | 3.0.0 (commit `cfe430e7375a7845b679adae9d51dac6deaa8858`) | https://github.com/Tehreer/SheenBidi | `SheenBidi/LICENSE` | Apache-2.0 |
| zlib | 1.3.1 (commit `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`) | https://github.com/madler/zlib | `zlib/LICENSE` | zlib |
| libpng | 1.6.59 (commit `cd952f49f95bb27154ae77dbb103032d95f6e580`) | https://github.com/pnggroup/libpng | `libpng/LICENSE` | libpng License |
| AccessKit C bindings (accesskit-c 0.23.2; Rust crates accesskit 0.25.1, accesskit_windows 0.35.1, accesskit_macos 0.27.1, accesskit_unix 0.24.0, and their dependencies) | commit `99023c87eca49531a8e1efc4657e35779257a463` of https://github.com/AccessKit/accesskit-c | https://github.com/AccessKit/accesskit-c | `accesskit-c/LICENSE-MIT`, `LICENSE-APACHE`, `LICENSE.chromium` | MIT or Apache-2.0 (parts derived from Chromium: BSD-style) |
| Clay (layout engine, single header) | commit `e6cc36941ab2af5d81107617039d6f527a1c660b` (no release tag; main branch, May 2026) | https://github.com/nicbarker/clay | `clay/LICENSE.md` | zlib/libpng license |
| md4c (Markdown parser, for reading mode and exports) | 0.5.2 (tag `release-0.5.2`, commit `729e6b8b320caa96328968ab27d7db2235e4fb47`) | https://github.com/mity/md4c | `md4c/LICENSE.md` | MIT |
| PCRE2 (regular expressions, for find and replace now and search later) | 10.44 (tarball `pcre2-10.44.tar.gz`, sha256 `86b9cb0aa3bcb7994faa88018292bc704cdbb708e785f7c74352ff6ea7d3175b`, computed on the download from the project's GitHub release) | https://github.com/PCRE2Project/pcre2 | `pcre2/LICENCE` | BSD-3-Clause (with the project's exemption for binary redistribution) |
| English (US) Hunspell dictionary (`en_US.aff`, `en_US.dic`, built into the program) | version 2020.12.07 of the LibreOffice dictionaries' `en/` folder (affix file changed 2024-01-29), downloaded from the `master` branch of https://github.com/LibreOffice/dictionaries on 2026-11-12; sha256 `en_US.aff` `e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e`, `en_US.dic` `f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647` | https://github.com/LibreOffice/dictionaries/tree/master/en (derived from SCOWL, http://wordlist.sourceforge.net) | `dictionaries/en_US/README_en_US.txt` (Kevin Atkinson's SCOWL license, with the Ispell BSD and WordNet notices it carries) | SCOWL (permissive, MIT-style) |
| Unicode data (grapheme break, emoji, Indic conjunct break) | 17.0.0 | https://github.com/unicode-org/unicodetools (`unicodetools/data/ucd/17.0.0`) | `unicode/LICENSE.txt` | Unicode License v3 |
| Test fonts (Noto subsets, tests only) | see `tests/fonts/README.md` | notofonts, googlefonts/noto-emoji | `tests/fonts/OFL-Noto.txt` | SIL Open Font License 1.1 |

## Notes

- **SDL3**: the whole source tree except `test/`, `examples/`, `docs/`,
  `android-project/`, `Xcode/`, `VisualC*/`, `build-scripts/` and `.github/`.
  Built as a static library by `third_party/SDL/CMakeLists.txt` with joystick,
  haptic, HIDAPI, sensor, camera, GPU and Vulkan switched off (see the top-level
  `CMakeLists.txt`). Its Objective-C files are SDL's own, compiled on macOS only;
  they are not Exhaust code and do not count against the 150-line Objective-C
  limit for `src/platform/`.
- **FreeType**: `src/`, `include/`, `builds/` (CMake, unix and windows pieces),
  `modules.cfg`, `CMakeLists.txt` and the license files. Built with zlib, bzip2,
  PNG, HarfBuzz and Brotli disabled. FreeType is dual licensed (FTL or GPLv2);
  Exhaust uses it under the **FTL only**, and the GPLv2 text is not vendored.
  The FTL requires crediting FreeType in documentation: see the README's
  credits and the About page (7a).
- **Fonts**: Atkinson Hyperlegible is the original 2020 release (family name
  "Atkinson Hyperlegible", which is what `font_interface` names); the Mono font
  is from the Atkinson Hyperlegible Mono project. Only the static TrueType files
  for the four styles are vendored. They are embedded into the executable by
  `tools/embed.c` at build time. OFL 1.1 allows embedding; the license text is
  shipped with the program.
- **HarfBuzz**: `src/` minus tests, Python scripts, Ragel sources, the subsetter
  and the fuzzing code, plus `COPYING`. Built as the single file `harfbuzz.cc`
  (a C++ compiler is needed to build Exhaust; Exhaust's own code stays C), with
  FreeType table access (`hb-ft`). C++ exceptions and RTTI are off.
- **SheenBidi**: `Headers/` and `Source/`, built as one unity file.
- **zlib and libpng**: C sources and headers only; `libpng/pnglibconf.h` is
  upstream's `scripts/pnglibconf.h.prebuilt`. libpng's SIMD code is off. They exist
  so FreeType can read PNG-compressed emoji bitmaps (CBDT, sbix).
- **Unicode data**: `GraphemeBreakProperty.txt`, `GraphemeBreakTest.txt`,
  `emoji-data.txt`, and `InCB.txt`, an extract of the `InCB` lines of
  `DerivedCoreProperties.txt` (the full file is 1 MB). `src/text/grapheme_data.c`
  is generated from them by `tools/gen_grapheme.c`; the generated file is committed.
  libgrapheme (ISC) was not vendored because its hosts were unreachable.
- Licenses of HarfBuzz, SheenBidi, zlib, libpng and the Unicode data are
  installed with the program under `share/exhaust/licenses/`.
- **Clay**: `clay.h` and its license only. It is compiled once, in `src/ui/clay_impl.c`,
  without Exhaust's warning flags. Exhaust uses Clay through its own widget tree
  (`include/exhaust_ui.h`); only `src/ui/ui.c` calls Clay.
- **AccessKit**: `src/`, `include/accesskit.h`, `Cargo.toml`, `Cargo.lock` and the
  licenses of accesskit-c, plus `vendor/` (the crates from `Cargo.lock`, filtered
  by `tools/vendor_a11y.sh`; crates that are not built on a shipping target keep
  only their `Cargo.toml`, and `.cargo-checksum.json` files list no files).
  `.cargo/config.toml` points cargo at `vendor/` and makes it offline. accesskit-c's
  Meson build files (LGPL-2.1) are not vendored. The Rust crates are all MIT,
  Apache-2.0, Unlicense, Zlib or dual licenses of those (`r-efi` lists LGPL-2.1+ as
  a third option; Exhaust uses it under MIT); their license files are installed
  under `share/exhaust/licenses/rust/`.

- **md4c**: only `src/md4c.c`, `src/md4c.h` and `LICENSE.md` (the HTML renderer, the
  command line tool, the tests and the build files were left out). Compiled as C in its default
  UTF-8 mode (its own Unicode tables are inside `md4c.c`; it needs no other library).
- **PCRE2**: the 8-bit library without JIT, the POSIX wrapper, the test programs and the build
  files left out: `src/pcre2_*.c` for the library, `pcre2_chartables.c.dist` as `pcre2_chartables.c`,
  `pcre2.h.generic` as `pcre2.h`, `config.h.generic` as `config.h`, and the three internal headers.
  Built as `pcre2_8` with `HAVE_CONFIG_H`, `SUPPORT_UNICODE`, `PCRE2_CODE_UNIT_WIDTH=8` and
  `PCRE2_STATIC`; warnings of the vendored code are switched off.
- **English (US) dictionary**: the two files as downloaded, unchanged, with the README that holds the license; the build embeds them with `tools/embed.c` (about 550 KB).
