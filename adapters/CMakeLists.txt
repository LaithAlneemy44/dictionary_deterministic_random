# Integration glue: exposes Laith's header-only skip list (randomised/) to the
# shared tests and benchmarks through the OrderedMap interface.
#
# randomised/ keeps its own standalone CMake project (with GoogleTest) for
# Laith's unit tests; it is deliberately not add_subdirectory'd here.
add_library(skip_list_adapter INTERFACE)
add_library(dict::skip_list ALIAS skip_list_adapter)
target_include_directories(skip_list_adapter INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/randomised)
target_link_libraries(skip_list_adapter INTERFACE dict::core)
