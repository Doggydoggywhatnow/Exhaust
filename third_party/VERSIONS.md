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
| SQLite (amalgamation, with FTS5 and the `dbstat` table) | 3.54.1 (`sqlite3.c` and `sqlite3.h` as shipped in the `libsqlite3-sys` 0.40.0 crate on crates.io, fetched with `cargo vendor`; the sqlite.org downloads and the `configure` of the source repository could not be used here) | https://www.sqlite.org | `sqlite/PUBLIC-DOMAIN.txt` | Public domain |
| libyaml | 0.2.5 (tag `0.2.5`, commit `2c891fc7a770e8ba2fec34fc6b545c672beb37e6`) | https://github.com/yaml/libyaml | `libyaml/LICENSE` | MIT |
| yyjson (JSON reader, for `.canvas` files) | 0.10.0 (tag `0.10.0`, commit `9ddba001a4ea88e93b46932e5c5b87b222e19a5f`) | https://github.com/ibireme/yyjson | `yyjson/LICENSE` | MIT |
| stb_image (PNG, JPEG, GIF and BMP decoder, single header) | 2.34 (commit `6b5799bae8a1fe3792691ef4b72ae26434c3e799` of `master`) | https://github.com/nothings/stb | `stb/LICENSE` | public domain (also MIT: the choice is the user's) |
| libwebp (WebP decoder only, pure C) | `main` at commit `f054d0c32b7aef7cf7093c2fab2d2336e68dce31` (no release tag was checked out; `git clone --depth 1`) | https://github.com/webmproject/libwebp | `libwebp/COPYING` (and `PATENTS`) | BSD-3-Clause |
| dav1d (AV1 decoder, for AVIF; portable C, one thread) | 1.5.5 (commit `2a0775ca5ea055450bdfbfe81758a3c9e001f58a`, `NEWS` says 1.5.5) | https://github.com/videolan/dav1d | `dav1d/COPYING` | BSD-2-Clause |
| PDFium public headers (`fpdfview.h`, `fpdf_text.h`, `fpdf_doc.h`; headers only) | `main` at commit `a84323421e94f484faca52dd9d027934eba42ab8` | https://github.com/chromium/pdfium (partial clone, folder `public`) | `pdfium/LICENSE` | BSD-3-Clause (PDFium is Apache-2.0 or BSD-3); the library itself is **never linked**: it is loaded at run time and ships in the add-on package |
| mpv client API headers (`mpv/client.h`, `mpv/render.h`; headers only) | `master` at commit `1eb941fdb974fe25dc0ac68a33652c145ff41f9d` (client API 2.x) | https://github.com/mpv-player/mpv | the license text at the top of each header | ISC (the client API); libmpv itself is LGPL when built without GPL parts and is **never linked**: it is loaded at run time and ships in the add-on package |
| lib-Peep (WAV and FLAC decoders, hand-written assembly) | 0.5.0, `dev` branch at commit `a31b83e659c3ba29031a4c33d0dcee7479efd045` (the `main` branch holds only 0.1.2) | https://github.com/Doggydoggywhatnow/lib-Peep | `libpeep/LICENSE` | Unlicense |
| dr_mp3 (MP3 decoder, single header; includes minimp3) | 0.7.5 (commit `e381981afa52800b9a3864df548353ff67252fbc` of `master` of dr_libs) | https://github.com/mackron/dr_libs | `dr_libs/LICENSE` | public domain or MIT-0 (the choice is the user's); minimp3 inside is CC0 |
| stb_vorbis (Ogg Vorbis decoder, one source file) | 1.25 (commit `6b5799bae8a1fe3792691ef4b72ae26434c3e799` of stb `master`, the one of stb_image) | https://github.com/nothings/stb | `stb/LICENSE` | public domain (also MIT) |
| nanosvg (SVG parser and rasterizer, two headers) | commit `239e102ec2c691f2902e20ace2ed36ee4a35cfe6` | https://github.com/memononen/nanosvg | `nanosvg/LICENSE.txt` | zlib |
| Unicode data (grapheme break, emoji, Indic conjunct break) | 17.0.0 | https://github.com/unicode-org/unicodetools (`unicodetools/data/ucd/17.0.0`) | `unicode/LICENSE.txt` | Unicode License v3 |
| Test fonts (Noto subsets, tests only) | see `tests/fonts/README.md` | notofonts, googlefonts/noto-emoji | `tests/fonts/OFL-Noto.txt` | SIL Open Font License 1.1 |

## Notes

- **SQLite**: only `sqlite3.c` and `sqlite3.h`, built by `CMakeLists.txt` as `sqlite3_static` with `SQLITE_ENABLE_FTS5`, `SQLITE_ENABLE_DBSTAT_VTAB`, `SQLITE_THREADSAFE=2` (every connection is used by one thread at a time), `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_DQS=0`, `SQLITE_OMIT_DEPRECATED` and `SQLITE_OMIT_SHARED_CACHE`; warnings of the vendored code are switched off. The amalgamation of an exact upstream tag could not be generated here (no network access to the release downloads, and running the upstream `configure` was not permitted), so the crate's copy of the amalgamation is the pinned source; replace it with the amalgamation of a SQLite release tag when one can be built.
- **yyjson**: `src/yyjson.c` and `src/yyjson.h` only, built as `yyjson_static` with warnings off. The canvas code reads with `YYJSON_READ_NUMBER_AS_RAW` (numbers keep their spelling) and writes values it keeps with `yyjson_val_write`; the files are 358 KB and 308 KB of source, about 100 KB of code in the program.
- **libyaml**: `src/*.c`, `src/yaml_private.h` and `include/yaml.h`; the version macros are set by `CMakeLists.txt`. One change to `src/emitter.c` (6b-ii): with `EXHAUST_INDENT_SEQUENCES` defined (the build defines it) a list inside a mapping is written indented under its key (`key:` then `  - item`) as the Bases help writes `.base` files, instead of flush with the key; the emitter is used only to write `.base` files.
- **stb_image, libwebp, dav1d, nanosvg (6c-i)**: all four reached by `git clone` from GitHub. Sizes of the static libraries built here without optimization (an upper bound; the stripped Release numbers are in `docs/PROGRESS.md` row 6c-i): libwebp decoder 380 KB, dav1d 1.1 MB, stb_image and nanosvg are compiled into `exhaust_image` (250 KB). **libwebp**: `src/dec`, `src/dsp` (the decoder's files only: no SIMD variants, no encoder files), `src/utils` (without the encoder's), and the headers `decode.h`, `encode.h` (included by `utils.c`), `mux_types.h`, `format_constants.h`, `types.h`; built with `HAVE_CONFIG_H` and an empty `src/webp/config.h`, which switches every SIMD path off; no animation (`demux` is not vendored), so an animated WebP is an error. **dav1d**: `src/*.c` of the portable build (no assembly, no `arm`, `x86`, `ppc`, `riscv` or `loongarch` folders), the headers, and `include/compat`; `third_party/dav1d/config.h` and `vcs_version.h` are written by hand in place of meson's; the files with `_tmpl` in the name are built twice, for 8 and for 16 bits; `third_party/dav1d` has no change to dav1d's own files. **stb_image**: the one header, built with only the JPEG, PNG, GIF and BMP decoders. **nanosvg**: `nanosvg.h` and `nanosvgrast.h`.
- **lib-Peep, dr_mp3, stb_vorbis (6c-ii)**: all reached by `git clone` from GitHub. **lib-Peep**: `peep.h`, `peep.asm`, `abi.inc`, `flac.asm`, `wav.asm`, `wavc.asm` (x86-64, NASM; format `elf64` on Linux, `win64` with `-DWIN64` on Windows), `arm64/*.s` and `arm64/abi_a64.inc` (ARM64, assembled by the platform's compiler), `LICENSE` and `CHANGELOG.md`. Two changes to the vendored copy, to be given back upstream: (1) `arm64/wavc.s` addressed its three tables, which sit in the `.text` section, with `adrp` and `:lo12:`, which Mach-O does not accept; they are now `adr`; (2) `arm64/abi_a64.inc` includes a `target.inc` that the build chooses (`arm64/linux/target.inc` is empty, `arm64/apple/target.inc` defines `APPLE` for Mach-O's underscore names) in place of `--defsym APPLE=1`. All four ARM64 files were assembled here for `arm64-apple-macos12` and `aarch64-linux-gnu` with clang, and the four x86-64 files for `elf64` and `win64` with NASM 2.16; **none has been linked or run on Windows, macOS or ARM64** (the first CI run says). **Intel Macs are not supported**: the x86-64 files name ELF sections (`.data.rel.ro`, `.note.GNU-stack`) that `macho64` rejects; CMake stops with a message. NASM is a build tool (not shipped): `apt install nasm`, `choco install nasm`; macOS and ARM64 need none. Sizes (stripped static archives, Release): see `docs/PROGRESS.md` row 6c-ii. **dr_mp3**: the header, built with `DR_MP3_IMPLEMENTATION` and `DR_MP3_NO_STDIO`. **stb_vorbis**: the source file, included with `STB_VORBIS_NO_STDIO` and `STB_VORBIS_NO_PUSHDATA_API`.
- **mpv headers (6c-iii)**: reached by a partial `git clone` of `mpv-player/mpv` (`--filter=blob:none --sparse`, folder `include/mpv`); only `client.h` and `render.h` are kept. The program calls 15 functions of libmpv, resolved by name with `SDL_LoadFunction` after `SDL_LoadObject`; a library that lacks one is refused. Nothing of mpv is compiled into Exhaust.
- **dav1d, Windows (6c-i, fixed in 6c-ii)**: `src/win32/thread.c` is built on Windows (the pthread names), and `_GNU_SOURCE` is defined elsewhere (`posix_memalign` under `-std=c11` with GCC 14).
- **PDFium headers (6c-iv)**: three headers of the public API; 38 functions are resolved by name with `SDL_LoadFunction`; a library that lacks one is refused. The add-on package (9d) builds PDFium without V8 and XFA (`pdf_use_v8 = false`, `pdf_enable_xfa = false`) from a pinned, checksummed release and records how.
- **Normalization tables** (`src/text/nfc_data.c`): canonical decompositions, combining classes, primary composites and full case folding, written once by a throwaway script from Python 3.12's `unicodedata` (Unicode 15.1.0) and committed as data; they are used by `ex_nfc` and `ex_name_key` (`src/text/nfc.c`). Normalization is stable, so characters added in Unicode 16 and 17 are the only ones missing.

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
