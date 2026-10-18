# Primary author: Aaron Falco
add_library(red_black_tree INTERFACE)
add_library(dict::red_black_tree ALIAS red_black_tree)
target_include_directories(red_black_tree INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(red_black_tree INTERFACE dict::core)
