cmake_minimum_required(VERSION 3.20)
project(exhaust VERSION 0.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Hardening and warnings, where the compiler supports them.
if(NOT MSVC)
  add_compile_options(-Wall -Wextra -fstack-protector-strong
    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
else()
  add_compile_options(/W3 /GS)
endif()

configure_file(include/exhaust_version.h.in include/exhaust_version.h)

add_library(exhaust_core STATIC src/version.c)
target_include_directories(exhaust_core PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)

add_executable(exhaust src/main.c)
target_link_libraries(exhaust PRIVATE exhaust_core)
install(TARGETS exhaust RUNTIME DESTINATION bin)

enable_testing()
add_executable(test_version tests/test_version.c)
target_link_libraries(test_version PRIVATE exhaust_core)
add_test(NAME version COMMAND test_version)
