cmake_minimum_required(VERSION 3.20)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(dictionary_deterministic_random LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DICT_BUILD_TESTS "Build the correctness tests" ON)
option(DICT_BUILD_BENCH "Build the benchmark harness" ON)

# Warnings applied to every target we own.
add_library(dict_warnings INTERFACE)
if (MSVC)
  target_compile_options(dict_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(dict_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---- Modules ---------------------------------------------------------------
# Shared interface, counters, RNG and the std::map reference adapter.
add_subdirectory(core)
# Deterministic: red-black tree.
add_subdirectory(red_black_tree)
# Randomised: skip list (primary author: Laith). Header lives in randomised/,
# exposed to the shared harness through adapters/.
add_subdirectory(adapters)

# ---- Tests / benchmarks ----------------------------------------------------
if (DICT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if (DICT_BUILD_BENCH)
  add_subdirectory(bench)
endif()
