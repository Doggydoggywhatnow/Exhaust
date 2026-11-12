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
