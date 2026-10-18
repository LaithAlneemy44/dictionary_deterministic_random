# Primary author: Laith
add_library(randomized_map INTERFACE)
add_library(dict::randomized ALIAS randomized_map)
target_include_directories(randomized_map INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(randomized_map INTERFACE dict::core)
