# tests/hunt: tests written to find bugs

These tests are different from the ones in `tests/`. The tests in `tests/` say "the code does what its slice promised" and stay green in CI. The tests here try to **break** the code: random and hostile input, models that are slower and simpler than the code, the promises of `docs/formats.md` and the README held up one by one, and the hard rules of `AGENTS.md` (never write outside the vault, never replace a note, typing is never lost, no network) tried from the outside.

**They were written without being run.** Another agent runs them. Some will fail the first time: that is what they are for. A failure is either a bug in Exhaust, a mistake in a test (a wrong expectation), or a place where the documentation and the code disagree; all three are worth a line in `docs/ISSUES.md`.

Nothing here is part of CI or of a normal `ctest`. They are off unless `-DEXHAUST_HUNT_TESTS=ON`.

## Build and run

```bash
cmake -S . -B build-hunt -DEXHAUST_HUNT_TESTS=ON -DEXHAUST_A11Y=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-hunt -j4 --target hunt_all
ctest --test-dir build-hunt -L hunt --output-on-failure
```

One test at a time, with all its output:

```bash
cd build-hunt/tests/hunt && ./hunt_buffer
```

Under the sanitizers (most of the memory bugs these tests can find only show there, because the inputs are copied to exactly sized blocks):

```bash
cmake -S . -B build-hunt-asan -DEXHAUST_HUNT_TESTS=ON -DEXHAUST_A11Y=OFF -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-hunt-asan -j3 --target hunt_buffer hunt_journal hunt_unicode hunt_markdown hunt_html hunt_ini hunt_datefmt hunt_expr hunt_canvas_yaml hunt_find hunt_extract
ASAN_OPTIONS=detect_leaks=0 HUNT_TIME_SCALE=10 HUNT_SCALE=1 ctest --test-dir build-hunt-asan -L hunt --output-on-failure
```

(A full ASan build of everything is very large, see AGENTS.md: build only the targets you need, delete the folder afterwards.) `hunt_queue` is worth a run under ThreadSanitizer (`-fsanitize=thread`, clang) too: a data race may not change any count.

The scratch folders of the tests that need files are `hunt_work/<name>` in the folder the test runs in (`build-hunt/tests/hunt` under ctest). They are emptied at the start of each test and never used for anything else. `hunt_work/` is in `.gitignore`.

## What a line of output means

```
HUNT hunt_buffer seed=24329716349599982 scale=1/1          the test started; the seed repeats the random work
HUNT FAIL    file.c:123: expr [step=...]                    an invariant broke: a crash, a lost byte, a broken round trip
HUNT SPEC    file.c:123: expr [step=...]                    a rule that the documentation states did not hold
    documented in docs/formats.md, Markdown tokens: tags
HUNT SUSPECT file.c:123: expr [step=...]                    the test author is not sure the expectation is right: read it by hand
HUNT CRASH signal=11 ... seed=... step=...                  the program died: the step and (when known) the input, in hex
HUNT HANG (timeout) ...                                     the test took longer than HUNT_TIMEOUT seconds (default 300)
HUNT-RESULT hunt_buffer FAILED checks=... failures=... suspects=... seed=...
```

* **FAIL** and **SPEC** make the test exit 1. **SUSPECT** is printed and counted but only fails the test with `HUNT_STRICT=1`. A SUSPECT is either a questionable expectation or a real problem that the documentation does not settle (a limit, a Windows trap, an edge of a format): it is a question for a human.
* Every failure line says what the test was doing (`step=`). A crash prints the same, with the seed and the input that was being tried.
* To repeat a failure: `HUNT_SEED=<seed> ./hunt_x` (or `./hunt_x --seed <seed>`). The default seed is fixed, so two runs of the same build do the same work. `HUNT_SEED=random` picks a new one and prints it.

### Environment

| Variable | Meaning |
| --- | --- |
| `HUNT_SEED` | the seed (a number, or `random`) |
| `HUNT_SCALE` | multiplies the amount of random work (default 1; `HUNT_SCALE=10` for a long soak) |
| `HUNT_QUICK=1` | divides the amount of random work by 10 (a smoke run) |
| `HUNT_TIME_SCALE` | multiplies every time limit (use 5 to 20 under the sanitizers and on slow machines) |
| `HUNT_TIMEOUT` | seconds before a hang is reported (default 300) |
| `HUNT_STRICT=1` | SUSPECT lines fail the test too |
| `HUNT_VERBOSE=1` | print every step as it starts (find the input that kills the program) |
| `HUNT_MAXPRINT` | how many failures are printed in full (default 40; the rest are only counted) |

Time limits use CPU time, not wall-clock time (`clock()`), as AGENTS.md asks of performance tests; they are still limits on **this** machine, so judge a timing SUSPECT against a Release build.

## The tests

| Test | Looks at | The kinds of bug it goes after |
| --- | --- | --- |
| `hunt_buffer` | `src/buffer/` the piece table, the document, selections, undo tree, edit stream | a lost or moved byte under any mix of typing, multiple cursors, undo, redo, jumps through the undo tree and bad edits; line index arithmetic; a listener stream that does not rebuild the text; typing coalescing across branches; a 150,000-line text |
| `hunt_journal` | the recovery journal format, CRC-32, SHA-256 | every cut and every flipped bit of a journal; records with a correct checksum and lying fields (offsets that overflow, lengths past the record); the documented rule that an unknown record type is skipped; replay against a model |
| `hunt_unicode` | UTF-8, grapheme clusters, NFC, the name key | all 1-, 2- and 3-byte strings against a strict decoder; `encode` of every code point; `prev` against `next`; NFC and full case folding on known pairs (sharp s, final sigma, dotted I, Angstrom); reordering that is quadratic |
| `hunt_markdown` | the tokenizer and its incremental update | span invariants on random and hostile text; the rules of the documentation one note each; **every single-byte edit at every offset** against a full tokenization; 38 kinds of text made to be slow; LF against CRLF |
| `hunt_html` | the HTML subset cleaner | URL tricks (`javascript:` with tabs and entities), character references, a corpus of 150 hostile fragments, tag soup, mutations; stability of cleaning; depth and size |
| `hunt_ini` | `src/util/ini.c` | the patcher against the parser as an oracle on generated files (CRLF, BOM, duplicates, garbage); values and keys the writer cannot represent (line injection!); typed getters on awkward numbers |
| `hunt_datefmt` | calendar, Moment-style format and parse | 2.2 million days round trip; `gmtime_r` as an oracle in every zone; month arithmetic in the zone's calendar; every token; ISO and en-US weeks around New Year; `-00:30` zones; the `snprintf` contract for every buffer size |
| `hunt_expr` | the Bases expression engine | arithmetic against C doubles on random trees; number-to-text against JavaScript's rules (shortest round-trip digits); `toFixed` and `round` traps; all 72 functions on every kind of receiver and argument (UTF-8 validity, determinism, time); laws of value order; nesting a million deep; formula cycles; the clock in a time zone |
| `hunt_canvas_yaml` | JSON Canvas and the YAML tree | byte-exact round trip; unknown keys; hostile JSON; edit sequences keeping ids unique and edges attached; hit testing against brute force; YAML scalars through the emitter and parser (quoting of `yes`, `null`, `1:30`...); aliases that must not expand; nesting deeper than the stack |
| `hunt_find` | find and replace, the fuzzy matcher | plain search, case, whole word, next, prev, count and replace-all against a naive oracle; regex consistency; empty matches advancing by a character; patterns that backtrack for ever; the replacement syntax; Unicode and broken UTF-8 |
| `hunt_extract` | link, heading, task, tag, property extraction; property text surgery | structure of every extraction; the documented rules; random property edits that must change only the property's own lines; quoting; CRLF and BOM; invalid YAML left alone |
| `hunt_vault` | name rules, excluded patterns, vault actions, registry, snapshots | names and Windows traps; glob against a DP matcher and `*a*a*a*b` in exponential time; create, rename, move, delete with `..`, absolute paths, symlinks, the empty path; registry paths with ` #`; snapshot names, replacement, pruning, clock changes (DST) |
| `hunt_index` | index drift, search, link-safe rename | a long random life of the vault (create, edit, delete, move, hidden files, Unicode names) with an incremental index against one built from nothing (the digests must match); 1,500 random queries against an evaluator written from the documentation; rename and move of notes and folders where every link must keep leading to the same file |
| `hunt_import` | CSV, ZIP, HTML to Markdown, attachments, importers | RFC 4180 round trips; ZIPs built byte by byte (bomb, lying sizes, Zip64, `..` names), cut and bit-flipped; importers walked afterwards for files **outside the vault**; attachment folders and names; HTML text that would become Markdown syntax |
| `hunt_settings` | settings, hotkeys, session | the settings table against itself; one-line patches; **a value with a newline must not write `[plugins] allow = true`**; chords; `hotkeys.ini` kept byte for byte; random workspaces serialized to a fixed point; hostile session files (cycles, ids at the edge, 40,000 splits deep); the damaged file moved aside |
| `hunt_plugins` | the Lua sandbox | a checklist of escapes (globals, metatables, bytecode, paths with every spelling of `..` and `.exhaust`, symlinks, self-approval), the permission matrix, endless loops, memory, stack, a pattern stuck in C, settings keys that look like INI, command floods, `commands.run` as a way around permissions, hostile manifests, toggling in a tight loop |
| `hunt_queue` | the lock-free queue, workers, snapshots | up to 32 producers; ordering, loss, repeats; payloads released exactly once; many workers; reference counting from many threads |
| `hunt_static.sh` | the sources | a grep audit (unbounded string calls, unchecked `realloc`, `atoi`, POSIX calls outside `src/platform`, `SDL_Delay` in `src/ui`, network calls, GPL text). Candidates, not bugs: `hunt_work/static_audit.txt` |

## Reading a result

1. Run the test alone, find the first `HUNT FAIL`/`SPEC` line, read its `step=`.
2. Decide which of the three it is:
   * **a bug in Exhaust**: add it to `docs/ISSUES.md` (what noticed it: `tests/hunt/<test>`, the seed and the line); fix it in the slice that owns the code, and keep the hunt test as the regression test (or copy its case into the matching test in `tests/`);
   * **a wrong expectation**: fix the test; say in the commit why (the documentation, or the behaviour that is right);
   * **the documentation and the code disagree**: ask the owner which is right, and change the other in the same commit (README and `docs/formats.md` first, per AGENTS.md).
3. A SUSPECT is not a verdict. Read the note under it; if it is a real problem make it a FAIL (change `HUNT_SUSPECT` to `HUNT_CHECK`) so it cannot be ignored again.
4. Never loosen a limit or delete a check to make a hunt test pass (AGENTS.md); find the slow or wrong code.

## Writing more

`hunt.h` is the whole harness: `HUNT_CHECK`, `HUNT_SPEC`, `HUNT_SUSPECT`, `HUNT_EQ_STR/INT/BYTES`, a seeded random generator and a text generator that mixes Markdown syntax, Unicode traps and line endings, `hunt_mutate`, `hunt_set_input` (the input that is printed if the program crashes), `hunt_step`, `hunt_iters` (scaled amount of work) and `hunt_time_limit`. `hunt_fs.h` is a scratch vault. Copy a small test (`hunt_ini.c`) and add it to `CMakeLists.txt`.

Three habits that find the hard bugs: give the code **an exact-size heap copy of its input** (a read past the end is then an AddressSanitizer error, not a silent success); compare it with **a model that is too slow to ship** (a plain array, a brute-force evaluator, a second index built from nothing); and **hold the documentation up** (every sentence in `docs/formats.md` that says "never", "always" or "only" is a test waiting to be written).

## Known gaps

Not covered here: the editor widget and every view (they need a window: use `EXHAUST_SCRIPT` and `tests/app_scripts.sh`), rendering and fonts, PDF writing and viewing, Mermaid and math (they have fuzz targets in `tests/fuzz/`), audio and image decoders (also fuzzed), the OS integration, and the screen reader bridge. A file watcher test with real directory events is the next thing worth writing.
