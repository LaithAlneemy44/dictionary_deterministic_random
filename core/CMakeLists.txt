add_library(dict_core INTERFACE)
add_library(dict::core ALIAS dict_core)
target_include_directories(dict_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dict_core INTERFACE dict_warnings)
