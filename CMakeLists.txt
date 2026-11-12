cmake_minimum_required(VERSION 3.20)
project(exhaust VERSION 0.0.0 LANGUAGES C)


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
if(NOT MSVC)
  add_compile_options(-Wall -Wextra -fstack-protector-strong
    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
else()
  add_compile_options(/W3 /GS)
endif()

configure_file(include/exhaust_version.h.in include/exhaust_version.h)

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

# ---- Libraries ----
add_library(exhaust_core STATIC src/version.c)
target_include_directories(exhaust_core PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)

add_library(exhaust_text STATIC src/text/utf8.c src/text/font.c src/text/fonts_table.c ${FONTS_C})
target_include_directories(exhaust_text PUBLIC include PRIVATE src/text)
target_link_libraries(exhaust_text PUBLIC freetype)

add_library(exhaust_render STATIC src/render/render.c src/render/atlas.c)
target_include_directories(exhaust_render PUBLIC include)
target_link_libraries(exhaust_render PUBLIC exhaust_text SDL3::SDL3-static)
if(NOT MSVC)
  target_link_libraries(exhaust_render PUBLIC m)
endif()

add_executable(exhaust src/main.c)
target_link_libraries(exhaust PRIVATE exhaust_core exhaust_render)
install(TARGETS exhaust RUNTIME DESTINATION bin)
# License texts of the code and fonts linked or embedded into the executable.
install(FILES
  third_party/SDL/LICENSE.txt
  DESTINATION share/exhaust/licenses RENAME SDL3-LICENSE.txt)
install(FILES third_party/freetype/docs/FTL.TXT
  DESTINATION share/exhaust/licenses RENAME FreeType-FTL.txt)
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

add_executable(test_renderer_smoke tests/test_renderer_smoke.c)
target_link_libraries(test_renderer_smoke PRIVATE exhaust_render)
add_test(NAME renderer_smoke
  COMMAND test_renderer_smoke ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden)
set_tests_properties(renderer_smoke PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
