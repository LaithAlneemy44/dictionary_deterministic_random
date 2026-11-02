# Timing build: counters compiled out.
add_executable(dict_bench bench_main.cpp)
target_link_libraries(dict_bench PRIVATE dict::core dict::red_black_tree dict::skip_list)

# Counting build: same source, DICT_INSTRUMENT on, also writes per-op histograms.
add_executable(dict_bench_instrumented bench_main.cpp)
target_link_libraries(dict_bench_instrumented PRIVATE dict::core dict::red_black_tree dict::skip_list)
target_compile_definitions(dict_bench_instrumented PRIVATE DICT_INSTRUMENT)

# Comparison: red-black tree vs skip list, insert/search/erase over int, str10
# and str30 keys, n = 2^10..2^23. Timing only (no counters).
add_executable(dict_compare compare_main.cpp)
target_link_libraries(dict_compare PRIVATE dict::core dict::red_black_tree dict::skip_list)
