# Timing build: counters compiled out.
add_executable(dict_bench bench_main.cpp)
target_link_libraries(dict_bench PRIVATE dict::core dict::red_black_tree dict::skip_list)

# Counting build: same source, DICT_INSTRUMENT on, also writes per-op histograms.
add_executable(dict_bench_instrumented bench_main.cpp)
target_link_libraries(dict_bench_instrumented PRIVATE dict::core dict::red_black_tree dict::skip_list)
target_compile_definitions(dict_bench_instrumented PRIVATE DICT_INSTRUMENT)

# Allocator on its own, with block sizes modelled on each structure's nodes.
add_executable(dict_alloc_bench alloc_bench.cpp)
target_link_libraries(dict_alloc_bench PRIVATE dict::core)
