# Bug-hunting tests. Not part of the normal test run and not in CI: configure with -DEXHAUST_HUNT_TESTS=ON.
# They are written to FIND bugs, so some of them are expected to fail when they are first run; read tests/hunt/README.md before you do.
#
#   cmake -S . -B build-hunt -DEXHAUST_HUNT_TESTS=ON -DEXHAUST_A11Y=OFF -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-hunt -j4 --target hunt_all
#   ctest --test-dir build-hunt -L hunt --output-on-failure
#
# Every test is one source file and tests/hunt/hunt.h. They run in the build folder of this directory and write only below `hunt_work/` there.

function(hunt_test name)
  add_executable(${name} ${name}.c)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE ${ARGN})
  if(NOT MSVC)
    # strict C11 declares no POSIX functions: alarm, signal handlers, setenv, symlink and gmtime_r are used by the harness and a few tests
    target_compile_definitions(${name} PRIVATE _POSIX_C_SOURCE=200809L)
    if(NOT WIN32)
      target_link_libraries(${name} PRIVATE m)
    endif()
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "hunt" TIMEOUT 1500 ENVIRONMENT "HUNT_TIMEOUT=1200")
  list(APPEND HUNT_ALL ${name})
  set(HUNT_ALL ${HUNT_ALL} PARENT_SCOPE)
endfunction()

set(HUNT_ALL "")
#         name               libraries
hunt_test(hunt_buffer        exhaust_buffer)
hunt_test(hunt_journal       exhaust_buffer)
hunt_test(hunt_unicode       exhaust_unicode)
hunt_test(hunt_markdown      exhaust_markdown)
hunt_test(hunt_html          exhaust_markdown)
hunt_test(hunt_ini           exhaust_ini)
hunt_test(hunt_datefmt       exhaust_datefmt)
hunt_test(hunt_expr          exhaust_base exhaust_datefmt)
hunt_test(hunt_canvas_yaml   exhaust_canvas exhaust_base)
hunt_test(hunt_find          exhaust_find exhaust_nav)
hunt_test(hunt_extract       exhaust_index exhaust_platform)
hunt_test(hunt_index         exhaust_index exhaust_platform)
hunt_test(hunt_vault         exhaust_vault exhaust_notes exhaust_platform)
hunt_test(hunt_import        exhaust_import exhaust_vault exhaust_platform zlib_static)
hunt_test(hunt_settings      exhaust_settings exhaust_workspace exhaust_vault exhaust_platform)
hunt_test(hunt_plugins       exhaust_plugins exhaust_settings exhaust_platform SDL3::SDL3-static)
hunt_test(hunt_queue         exhaust_buffer SDL3::SDL3-static)

# a few of them walk big folders or wait for threads: do not run them next to each other, or one test's CPU time becomes another's timeout
set_tests_properties(hunt_plugins hunt_queue hunt_index hunt_expr hunt_markdown PROPERTIES RUN_SERIAL TRUE)

add_custom_target(hunt_all DEPENDS ${HUNT_ALL})

# the grep audit of the sources: a list of candidates, it never fails (use --strict by hand to make a hard rule fail)
add_test(NAME hunt_static COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/hunt_static.sh ${CMAKE_SOURCE_DIR})
set_tests_properties(hunt_static PROPERTIES LABELS "hunt" TIMEOUT 300)
