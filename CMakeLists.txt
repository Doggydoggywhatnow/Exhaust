cmake_minimum_required(VERSION 3.20)
project(exhaust VERSION 0.0.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC) # the macOS trash call (src/platform/trash_mac.m, under 150 lines)
endif()


# ---- Vendored dependencies (third_party/, see third_party/VERSIONS.md) ----
# SDL3: static, only what Exhaust uses. Audio stays for lib-Peep output (6c-ii).
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

# FreeType: no optional libraries (zlib, bzip2, png, harfbuzz, brotli).
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)

# Exhaust's own code: C11, no extensions, hardening and warnings. Set after the
# vendored libraries so their own build settings are left alone.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# zlib and libpng (FreeType needs libpng to read color emoji bitmaps), built
# from third_party/ with their own warnings left alone.
add_library(zlib_static STATIC
  third_party/zlib/adler32.c third_party/zlib/crc32.c third_party/zlib/deflate.c third_party/zlib/infback.c
  third_party/zlib/inffast.c third_party/zlib/inflate.c third_party/zlib/inftrees.c third_party/zlib/trees.c
  third_party/zlib/zutil.c)
target_include_directories(zlib_static PUBLIC third_party/zlib)
target_compile_definitions(zlib_static PRIVATE _LARGEFILE64_SOURCE=1)
if(NOT MSVC)
  target_compile_options(zlib_static PRIVATE -w)
else()
  target_compile_definitions(zlib_static PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()
add_library(png_static STATIC
  third_party/libpng/png.c third_party/libpng/pngerror.c third_party/libpng/pngget.c third_party/libpng/pngmem.c
  third_party/libpng/pngpread.c third_party/libpng/pngread.c third_party/libpng/pngrio.c
  third_party/libpng/pngrtran.c third_party/libpng/pngrutil.c third_party/libpng/pngset.c
  third_party/libpng/pngtrans.c third_party/libpng/pngwio.c third_party/libpng/pngwrite.c
  third_party/libpng/pngwtran.c third_party/libpng/pngwutil.c)
target_include_directories(png_static PUBLIC third_party/libpng)
# No SIMD filter code: the portable C paths are used on every CPU.
target_compile_definitions(png_static PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0 PNG_MIPS_MSA_OPT=0
  PNG_POWERPC_VSX_OPT=0 PNG_LOONGARCH_LSX_OPT=0)
target_link_libraries(png_static PUBLIC zlib_static)
if(NOT MSVC)
  target_compile_options(png_static PRIVATE -w)
else()
  target_compile_definitions(png_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
# FreeType reads PNG-compressed bitmaps (CBDT, sbix) through libpng.
target_compile_definitions(freetype PRIVATE FT_CONFIG_OPTION_USE_PNG)
target_link_libraries(freetype PRIVATE $<BUILD_INTERFACE:png_static>)

# HarfBuzz: the single-file build, OpenType shaping with FreeType table access.
add_library(harfbuzz STATIC third_party/harfbuzz/src/harfbuzz.cc)
target_include_directories(harfbuzz PUBLIC third_party/harfbuzz/src)
target_compile_definitions(harfbuzz PRIVATE HAVE_FREETYPE=1 HAVE_FT_GET_VAR_BLEND_COORDINATES=1
  HAVE_FT_SET_VAR_BLEND_COORDINATES=1 HAVE_FT_DONE_MM_VAR=1 HAVE_FT_GET_TRANSFORM=1 HB_NO_PRAGMA_GCC_DIAGNOSTIC)
target_link_libraries(harfbuzz PUBLIC freetype)
set_target_properties(harfbuzz PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
  target_compile_options(harfbuzz PRIVATE -fno-exceptions -fno-rtti -w)
else()
  target_compile_options(harfbuzz PRIVATE /utf-8 /wd4244 /wd4267 /wd4996)
endif()

# SheenBidi: Unicode bidirectional algorithm, single-file (unity) build.
add_library(sheenbidi STATIC third_party/SheenBidi/Source/SheenBidi.c)
target_include_directories(sheenbidi PUBLIC third_party/SheenBidi/Headers PRIVATE third_party/SheenBidi/Source)
target_compile_definitions(sheenbidi PRIVATE SB_CONFIG_UNITY)
if(NOT MSVC)
  target_compile_options(sheenbidi PRIVATE -w)
else()
  target_compile_options(sheenbidi PRIVATE /utf-8 /wd4244 /wd4267 /wd4996)
endif()

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -fstack-protector-strong
    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
else()
  # MSVC flags the standard C library (fopen, getenv, fscanf) as "unsafe"; the
  # portable C calls are intended, so the deprecation noise is switched off.
  add_compile_options(/W3 /GS)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

configure_file(include/exhaust_version.h.in include/exhaust_version.h)

# ---- AccessKit: the screen reader bridge (Rust, built offline from third_party) ----
# The only Rust in Exhaust. cargo builds a static library from the vendored crates
# (third_party/accesskit-c/vendor, see tools/vendor_a11y.sh); no network is used.
# -DEXHAUST_A11Y=OFF skips it for a build on a machine without cargo; the program
# then has no accessibility tree, so the official builds always have it on.
option(EXHAUST_A11Y "Build the AccessKit screen reader bridge (needs cargo)" ON)
if(EXHAUST_A11Y)
  find_program(CARGO cargo)
  if(NOT CARGO)
    message(FATAL_ERROR "cargo was not found. Install Rust (https://rustup.rs) or configure with -DEXHAUST_A11Y=OFF.")
  endif()
  set(ACCESSKIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/accesskit-c)
  set(ACCESSKIT_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/accesskit-target)
  if(MSVC)
    set(ACCESSKIT_LIB ${ACCESSKIT_TARGET_DIR}/release/accesskit.lib)
  else()
    set(ACCESSKIT_LIB ${ACCESSKIT_TARGET_DIR}/release/libaccesskit.a)
  endif()
  file(GLOB_RECURSE ACCESSKIT_SOURCES ${ACCESSKIT_DIR}/src/*.rs)
  add_custom_command(OUTPUT ${ACCESSKIT_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${ACCESSKIT_TARGET_DIR} MAKEFLAGS=
            CARGO_PROFILE_RELEASE_DEBUG=false   # accesskit-c's own profile keeps debug info: it would bloat the program
            ${CARGO} build --release --offline --locked
    WORKING_DIRECTORY ${ACCESSKIT_DIR}   # so cargo finds .cargo/config.toml (the vendored sources)
    DEPENDS ${ACCESSKIT_DIR}/Cargo.toml ${ACCESSKIT_DIR}/Cargo.lock ${ACCESSKIT_SOURCES}
    COMMENT "Building AccessKit with cargo (offline)")
  add_custom_target(accesskit_build DEPENDS ${ACCESSKIT_LIB})
  add_library(accesskit STATIC IMPORTED GLOBAL)
  set_target_properties(accesskit PROPERTIES IMPORTED_LOCATION ${ACCESSKIT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${ACCESSKIT_DIR}/include)
  add_dependencies(accesskit accesskit_build)
  # system libraries the Rust code needs (from `cargo rustc -- --print native-static-libs`)
  if(WIN32)
    set_property(TARGET accesskit PROPERTY INTERFACE_LINK_LIBRARIES kernel32 ntdll userenv ws2_32 dbghelp)
  elseif(APPLE)
    set_property(TARGET accesskit PROPERTY INTERFACE_LINK_LIBRARIES "-framework AppKit" "-framework Foundation" objc m)
  else()
    set_property(TARGET accesskit PROPERTY INTERFACE_LINK_LIBRARIES pthread m dl rt util)
  endif()
endif()

# ---- Built-in fonts, embedded as C arrays by tools/embed.c ----
add_executable(embed tools/embed.c)
set(FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts)
set(FONTS_C ${CMAKE_CURRENT_BINARY_DIR}/fonts_embedded.c)
set(FONT_FILES
  ex_font_ui_regular          ${FONT_DIR}/atkinson-hyperlegible/AtkinsonHyperlegible-Regular.ttf
  ex_font_ui_bold             ${FONT_DIR}/atkinson-hyperlegible/AtkinsonHyperlegible-Bold.ttf
  ex_font_ui_italic           ${FONT_DIR}/atkinson-hyperlegible/AtkinsonHyperlegible-Italic.ttf
  ex_font_ui_bold_italic      ${FONT_DIR}/atkinson-hyperlegible/AtkinsonHyperlegible-BoldItalic.ttf
  ex_font_mono_regular        ${FONT_DIR}/atkinson-hyperlegible-mono/AtkinsonHyperlegibleMono-Regular.ttf
  ex_font_mono_bold           ${FONT_DIR}/atkinson-hyperlegible-mono/AtkinsonHyperlegibleMono-Bold.ttf
  ex_font_mono_italic         ${FONT_DIR}/atkinson-hyperlegible-mono/AtkinsonHyperlegibleMono-Italic.ttf
  ex_font_mono_bold_italic    ${FONT_DIR}/atkinson-hyperlegible-mono/AtkinsonHyperlegibleMono-BoldItalic.ttf)
set(FONT_DEPS "")
foreach(item IN LISTS FONT_FILES)
  if(item MATCHES "\\.ttf$")
    list(APPEND FONT_DEPS ${item})
  endif()
endforeach()
add_custom_command(OUTPUT ${FONTS_C}
  COMMAND embed ${FONTS_C} ${FONT_FILES}
  DEPENDS embed ${FONT_DEPS}
  COMMENT "Embedding built-in fonts")

set(THEME_C ${CMAKE_CURRENT_BINARY_DIR}/themes_embedded.c)
set(THEME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/themes)
add_custom_command(OUTPUT ${THEME_C}
  COMMAND embed ${THEME_C}
    ex_theme_dark ${THEME_DIR}/exhaust-dark.theme ex_theme_light ${THEME_DIR}/exhaust-light.theme
    ex_theme_hc_dark ${THEME_DIR}/exhaust-hc-dark.theme ex_theme_hc_light ${THEME_DIR}/exhaust-hc-light.theme
  DEPENDS embed ${THEME_DIR}/exhaust-dark.theme ${THEME_DIR}/exhaust-light.theme
    ${THEME_DIR}/exhaust-hc-dark.theme ${THEME_DIR}/exhaust-hc-light.theme
  COMMENT "Embedding built-in themes")

# ---- Libraries ----
add_library(exhaust_core STATIC src/version.c)
target_include_directories(exhaust_core PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)

add_library(exhaust_buffer STATIC src/buffer/buffer.c src/buffer/doc.c src/buffer/journal.c src/util/worker.c src/util/sha256.c)
target_include_directories(exhaust_buffer PUBLIC include)
target_link_libraries(exhaust_buffer PUBLIC SDL3::SDL3-static)

add_library(exhaust_notes STATIC src/notes/note_codec.c src/notes/diff.c src/notes/watcher.c src/notes/journal_writer.c src/notes/notes.c)
target_include_directories(exhaust_notes PUBLIC include)
target_link_libraries(exhaust_notes PUBLIC exhaust_vault exhaust_buffer exhaust_platform)

add_library(exhaust_workspace STATIC src/workspace/workspace.c src/workspace/session.c src/util/ini.c)
target_include_directories(exhaust_workspace PUBLIC include)
target_link_libraries(exhaust_workspace PUBLIC exhaust_vault exhaust_buffer exhaust_platform SDL3::SDL3-static)

# ---- Settings: the table of known keys is generated from docs/formats.md ----
add_executable(gen_tables tools/gen_tables.c)
set(SETTINGS_SCHEMA_C ${CMAKE_CURRENT_BINARY_DIR}/settings_schema.c)
set(COMMAND_TABLE_C ${CMAKE_CURRENT_BINARY_DIR}/command_table.c)
add_custom_command(
  OUTPUT ${SETTINGS_SCHEMA_C} ${COMMAND_TABLE_C}
  COMMAND gen_tables ${CMAKE_CURRENT_SOURCE_DIR}/docs/formats.md ${SETTINGS_SCHEMA_C} ${COMMAND_TABLE_C}
  DEPENDS gen_tables ${CMAKE_CURRENT_SOURCE_DIR}/docs/formats.md
  COMMENT "Generating the settings and command tables from docs/formats.md")
add_library(exhaust_settings STATIC src/settings/settings.c src/settings/registry.c src/settings/apply.c ${SETTINGS_SCHEMA_C} ${COMMAND_TABLE_C})
target_include_directories(exhaust_settings PUBLIC include)
target_link_libraries(exhaust_settings PUBLIC exhaust_workspace exhaust_buffer exhaust_platform SDL3::SDL3-static)

add_library(exhaust_vault STATIC src/vault/vault.c src/vault/registry.c)
target_include_directories(exhaust_vault PUBLIC include)
target_link_libraries(exhaust_vault PUBLIC exhaust_platform exhaust_buffer)

add_library(exhaust_platform STATIC src/platform/paths.c src/platform/input.c src/platform/files.c src/platform/integration.c)
target_include_directories(exhaust_platform PUBLIC include)
target_link_libraries(exhaust_platform PUBLIC SDL3::SDL3-static)
if(APPLE)
  target_sources(exhaust_platform PRIVATE src/platform/trash_mac.m)
  target_link_libraries(exhaust_platform PUBLIC "-framework Foundation")
elseif(WIN32)
  target_link_libraries(exhaust_platform PUBLIC shell32 advapi32)
endif()

# UTF-8 and grapheme clusters: no dependencies, so the fuzz targets can link just this.
add_library(exhaust_unicode STATIC src/text/utf8.c src/text/grapheme.c src/text/grapheme_data.c)
target_include_directories(exhaust_unicode PUBLIC include PRIVATE src/text)

add_library(exhaust_text STATIC src/text/font.c
  src/text/fontdb.c src/text/shape.c src/text/fonts_table.c ${FONTS_C})
target_include_directories(exhaust_text PUBLIC include PRIVATE src/text)
target_link_libraries(exhaust_text PUBLIC exhaust_unicode freetype harfbuzz sheenbidi exhaust_platform)

add_library(exhaust_render STATIC src/render/render.c src/render/atlas.c)
target_include_directories(exhaust_render PUBLIC include)
target_link_libraries(exhaust_render PUBLIC exhaust_text SDL3::SDL3-static)
if(NOT MSVC)
  target_link_libraries(exhaust_render PUBLIC m)
endif()

# Clay (layout), compiled as its own unit without our warning flags.
add_library(clay_impl STATIC src/ui/clay_impl.c)
target_include_directories(clay_impl PUBLIC third_party/clay)
if(NOT MSVC)
  target_compile_options(clay_impl PRIVATE -w)
endif()

add_library(exhaust_a11y STATIC src/a11y/a11y_tree.c src/a11y/a11y_snapshot.c)
target_include_directories(exhaust_a11y PUBLIC include)
target_link_libraries(exhaust_a11y PUBLIC SDL3::SDL3-static)
if(EXHAUST_A11Y)
  target_compile_definitions(exhaust_a11y PUBLIC EX_HAVE_A11Y)
  target_link_libraries(exhaust_a11y PUBLIC accesskit)
endif()

add_library(exhaust_ui STATIC src/ui/explorer.c src/ui/sample_window.c src/ui/ui_a11y.c src/ui/theme.c src/ui/theme_data.c src/ui/editor.c src/ui/ui.c src/ui/panes.c ${THEME_C})
target_include_directories(exhaust_ui PUBLIC include PRIVATE src/ui)
target_link_libraries(exhaust_ui PUBLIC exhaust_render exhaust_buffer exhaust_vault exhaust_a11y exhaust_workspace clay_impl)
if(NOT MSVC)
  target_link_libraries(exhaust_ui PUBLIC m)
endif()

add_executable(exhaust src/main.c)
target_link_libraries(exhaust PRIVATE exhaust_core exhaust_render exhaust_ui exhaust_notes exhaust_settings)
# Release builds are stripped of symbols (the size budget is for the installed program).
if(NOT MSVC AND NOT APPLE)
  target_link_options(exhaust PRIVATE $<$<CONFIG:Release>:-s>)
endif()
install(TARGETS exhaust RUNTIME DESTINATION bin)
# License texts of the code and fonts linked or embedded into the executable.
install(FILES
  third_party/SDL/LICENSE.txt
  DESTINATION share/exhaust/licenses RENAME SDL3-LICENSE.txt)
install(FILES third_party/freetype/docs/FTL.TXT
  DESTINATION share/exhaust/licenses RENAME FreeType-FTL.txt)
if(EXHAUST_A11Y)
  install(FILES third_party/accesskit-c/LICENSE-MIT third_party/accesskit-c/LICENSE-APACHE
          third_party/accesskit-c/LICENSE.chromium DESTINATION share/exhaust/licenses/accesskit)
  install(DIRECTORY third_party/accesskit-c/vendor/ DESTINATION share/exhaust/licenses/rust
          FILES_MATCHING PATTERN "LICENSE*" PATTERN "COPYING*" PATTERN "UNLICENSE*")
endif()
install(FILES third_party/clay/LICENSE.md
  DESTINATION share/exhaust/licenses RENAME Clay-LICENSE.txt)
install(FILES third_party/harfbuzz/COPYING
  DESTINATION share/exhaust/licenses RENAME HarfBuzz-COPYING.txt)
install(FILES third_party/SheenBidi/LICENSE
  DESTINATION share/exhaust/licenses RENAME SheenBidi-LICENSE.txt)
install(FILES third_party/zlib/LICENSE
  DESTINATION share/exhaust/licenses RENAME zlib-LICENSE.txt)
install(FILES third_party/libpng/LICENSE
  DESTINATION share/exhaust/licenses RENAME libpng-LICENSE.txt)
install(FILES third_party/unicode/LICENSE.txt
  DESTINATION share/exhaust/licenses RENAME Unicode-LICENSE.txt)
install(FILES third_party/fonts/atkinson-hyperlegible/OFL.txt
  DESTINATION share/exhaust/licenses RENAME AtkinsonHyperlegible-OFL.txt)
install(FILES third_party/fonts/atkinson-hyperlegible-mono/OFL.txt
  DESTINATION share/exhaust/licenses RENAME AtkinsonHyperlegibleMono-OFL.txt)

# ---- Tests ----
enable_testing()
add_executable(test_version tests/test_version.c)
target_link_libraries(test_version PRIVATE exhaust_core)
add_test(NAME version COMMAND test_version)

add_executable(test_utf8 tests/test_utf8.c)
target_link_libraries(test_utf8 PRIVATE exhaust_text)
add_test(NAME utf8 COMMAND test_utf8)

add_executable(test_buffer tests/test_buffer.c)
target_link_libraries(test_buffer PRIVATE exhaust_buffer)
add_test(NAME buffer COMMAND test_buffer)

add_executable(test_journal_replay tests/test_journal_replay.c)
target_link_libraries(test_journal_replay PRIVATE exhaust_buffer)
add_test(NAME journal_replay COMMAND test_journal_replay)

add_executable(test_worker_queue tests/test_worker_queue.c)
target_link_libraries(test_worker_queue PRIVATE exhaust_buffer)
add_test(NAME worker_queue COMMAND test_worker_queue)

add_executable(test_theme tests/test_theme.c)
target_link_libraries(test_theme PRIVATE exhaust_ui)
add_test(NAME theme COMMAND test_theme)

add_executable(test_no_color_literals tests/test_no_color_literals.c)
target_link_libraries(test_no_color_literals PRIVATE exhaust_platform)
add_test(NAME no_color_literals COMMAND test_no_color_literals ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_editor_widget tests/test_editor_widget.c)
target_link_libraries(test_editor_widget PRIVATE exhaust_ui)
add_test(NAME editor_widget COMMAND test_editor_widget)

if(EXHAUST_A11Y)
  add_executable(test_a11y_link tests/test_a11y_link.c)
  target_link_libraries(test_a11y_link PRIVATE accesskit)
  add_test(NAME a11y_link COMMAND test_a11y_link)
endif()

# libFuzzer targets (clang only): cmake -DEXHAUST_FUZZ=ON -DCMAKE_C_COMPILER=clang
option(EXHAUST_FUZZ "Build the libFuzzer targets in tests/fuzz (clang)" OFF)
if(EXHAUST_FUZZ)
  target_compile_options(exhaust_unicode PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
  add_executable(fuzz_utf8 tests/fuzz/fuzz_utf8.c)
  target_compile_options(fuzz_utf8 PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_utf8 PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_libraries(fuzz_utf8 PRIVATE exhaust_unicode)
endif()

add_executable(test_a11y_wrapper tests/test_a11y_wrapper.c)
target_link_libraries(test_a11y_wrapper PRIVATE exhaust_ui)
add_test(NAME a11y_wrapper COMMAND test_a11y_wrapper)

add_executable(test_a11y_dump tests/test_a11y_dump.c)
target_link_libraries(test_a11y_dump PRIVATE exhaust_ui)
add_test(NAME a11y_dump COMMAND test_a11y_dump ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/a11y_sample.txt)

add_executable(test_keyboard_nav tests/test_keyboard_nav.c)
target_link_libraries(test_keyboard_nav PRIVATE exhaust_ui)
add_test(NAME keyboard_nav COMMAND test_keyboard_nav ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/focus_order.txt)

add_executable(test_workspace_layout tests/test_workspace_layout.c)
target_link_libraries(test_workspace_layout PRIVATE exhaust_workspace)
if(NOT MSVC)
  target_link_libraries(test_workspace_layout PRIVATE m)
endif()
add_test(NAME workspace_layout COMMAND test_workspace_layout layout)
add_test(NAME session_restore COMMAND test_workspace_layout session)

add_executable(test_session tests/test_session.c)
target_link_libraries(test_session PRIVATE exhaust_workspace)
add_test(NAME session_restore_files COMMAND test_session)

add_executable(test_panes tests/test_panes.c)
target_link_libraries(test_panes PRIVATE exhaust_ui)
add_test(NAME workspace_layout_panes COMMAND test_panes)

add_executable(test_settings tests/test_settings.c)
target_link_libraries(test_settings PRIVATE exhaust_settings)
target_compile_definitions(test_settings PRIVATE DOC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/docs/formats.md")
add_test(NAME settings COMMAND test_settings)

add_executable(test_registry tests/test_registry.c)
target_link_libraries(test_registry PRIVATE exhaust_settings)
target_compile_definitions(test_registry PRIVATE DOC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/docs/formats.md")
add_test(NAME registry COMMAND test_registry)

add_executable(test_integration tests/test_integration.c)
target_link_libraries(test_integration PRIVATE exhaust_platform)
add_test(NAME platform_integration COMMAND test_integration)

add_executable(settings_child tests/settings_child.c)
target_link_libraries(settings_child PRIVATE exhaust_settings)
add_executable(test_settings_writethrough tests/test_settings_writethrough.c)
target_link_libraries(test_settings_writethrough PRIVATE exhaust_settings)
add_dependencies(test_settings_writethrough settings_child)
add_test(NAME settings_writethrough COMMAND test_settings_writethrough $<TARGET_FILE:settings_child>)

add_executable(test_vault_tree tests/test_vault_tree.c)
target_link_libraries(test_vault_tree PRIVATE exhaust_vault)
add_test(NAME vault_tree COMMAND test_vault_tree)

add_executable(test_explorer tests/test_explorer.c)
target_link_libraries(test_explorer PRIVATE exhaust_ui)
add_test(NAME explorer COMMAND test_explorer)

foreach(t file_safety write_failure watch_fallback)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE exhaust_notes)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

add_executable(autosave_child tests/autosave_child.c)
target_link_libraries(autosave_child PRIVATE exhaust_notes)
add_executable(test_autosave_crash tests/test_autosave_crash.c)
target_link_libraries(test_autosave_crash PRIVATE exhaust_notes)
add_dependencies(test_autosave_crash autosave_child)
add_test(NAME autosave_crash COMMAND test_autosave_crash $<TARGET_FILE:autosave_child>)

add_executable(test_shaping tests/test_shaping.c)
target_link_libraries(test_shaping PRIVATE exhaust_text)
add_test(NAME shaping COMMAND test_shaping ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unicode
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/fonts)

add_executable(test_font_cache tests/test_font_cache.c)
target_link_libraries(test_font_cache PRIVATE exhaust_text)
add_test(NAME font_cache COMMAND test_font_cache ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts)

add_executable(test_renderer_smoke tests/test_renderer_smoke.c)
target_link_libraries(test_renderer_smoke PRIVATE exhaust_render)
add_test(NAME renderer_smoke
  COMMAND test_renderer_smoke ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden)
set_tests_properties(renderer_smoke PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
