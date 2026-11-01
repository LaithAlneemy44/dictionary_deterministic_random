cmake_minimum_required(VERSION 3.20)

# Standalone build for the skip list and its unit tests.
#   cmake -S randomised -B randomised/build
#   cmake --build randomised/build
#   ctest --test-dir randomised/build --output-on-failure

project(skip_list LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Debug so assert() is active while developing.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Header-only library: just exposes this folder as an include path.
add_library(skip_list INTERFACE)
target_include_directories(skip_list INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
  set(SKIP_LIST_WARNINGS /W4 /permissive-)
else()
  set(SKIP_LIST_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# ---- GoogleTest ------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Match the MSVC runtime of the tests, and don't install gtest with the project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# ---- Tests -----------------------------------------------------------------
add_executable(skip_list_tests tests/skip_list_tests.cpp)
target_link_libraries(skip_list_tests PRIVATE skip_list GTest::gtest_main)
target_compile_options(skip_list_tests PRIVATE ${SKIP_LIST_WARNINGS})
gtest_discover_tests(skip_list_tests)

# ---- include-what-you-use --------------------------------------------------
# Reports missing and unneeded #includes in skip_list.hpp during the build.
# Set on the test target only, so it doesn't analyse GoogleTest's sources.
# Turn off with -DSKIP_LIST_IWYU=OFF.
option(SKIP_LIST_IWYU "Run include-what-you-use on skip_list.hpp" ON)
if (SKIP_LIST_IWYU)
  find_program(IWYU_EXE NAMES include-what-you-use iwyu)
  if (IWYU_EXE)
    set(IWYU_COMMAND "${IWYU_EXE};-Xiwyu;--check_also=*skip_list.hpp")

    # IWYU parses with clang and needs clang's built-in headers. The MSYS2
    # build looks for them at the packager's path, so ask the installed
    # clang where they really are.
    find_program(CLANG_EXE NAMES clang)
    if (CLANG_EXE)
      execute_process(COMMAND "${CLANG_EXE}" -print-resource-dir
                      OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
      list(APPEND IWYU_COMMAND "-resource-dir" "${CLANG_RESOURCE_DIR}")
    else()
      message(WARNING "clang not found; include-what-you-use may fail to find its built-in headers")
    endif()

    set_target_properties(skip_list_tests PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${IWYU_COMMAND}")
  else()
    message(STATUS "include-what-you-use not found; skipping include checks")
  endif()
endif()
