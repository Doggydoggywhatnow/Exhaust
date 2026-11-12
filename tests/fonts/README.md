# Test fonts

Tiny subsets of Noto fonts, used only by `ctest -R shaping` and `ctest -R font_cache`
so the tests give the same result on every machine. They are not installed and not
part of the program. Made with `pyftsubset` (fontTools), keeping the layout tables
and only the glyphs the tests use:

| File | From | Kept |
| --- | --- | --- |
| `ExhaustTest-Arabic.ttf` | Noto Sans Arabic Regular (notofonts/notofonts.github.io) | the letters of "مرحبا بالعالم", digits, parentheses |
| `ExhaustTest-Hebrew.ttf` | Noto Sans Hebrew Regular (same) | the letters of "שלום עולם", parentheses |
| `ExhaustTest-CJK.otf` | Noto Sans SC Regular (notofonts/noto-cjk) | "你好世界日本語漢字" |
| `ExhaustTest-Emoji.ttf` | Noto Color Emoji, bitmap (CBDT) version (Debian `fonts-noto-color-emoji`) | U+1F600, U+1F44D, regional indicators U+1F1FA and U+1F1F8, ZWJ, VS16 |

License: SIL Open Font License 1.1, see `OFL-Noto.txt`.
