add_executable(dict_tests test_main.cpp)
target_link_libraries(dict_tests PRIVATE dict::core dict::red_black_tree dict::randomized)
# Count operations in tests too, so the instrumentation paths are compiled and exercised.
target_compile_definitions(dict_tests PRIVATE DICT_INSTRUMENT)

foreach (suite infrastructure std_map red_black_tree randomized)
  add_test(NAME ${suite} COMMAND dict_tests ${suite})
  set_tests_properties(${suite} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
