#pragma once

// Structural operation counters.
//
// Wall-clock time for a single dictionary operation (~50-200 ns) is too close
// to timer resolution to measure per operation, so per-operation cost
// distributions are measured structurally instead: every implementation bumps
// these counters, and the benchmark snapshots them around each operation.
//
// Counting is compiled in only when DICT_INSTRUMENT is defined (the
// `bench_instrumented` target). In the timed `bench` target DICT_COUNT expands
// to nothing, so instrumentation cannot distort timings.

#include <cstdint>

namespace dict {

struct OpCounters {
    std::uint64_t comparisons = 0;    // key comparisons
    std::uint64_t node_visits = 0;    // nodes (or levels) touched while searching
    std::uint64_t structural = 0;     // restructuring work: rotations, pointer splices, etc.
    std::uint64_t recolors = 0;       // RBT colour flips (unused by the randomized structure)
    std::uint64_t allocations = 0;    // node allocations
};

inline OpCounters operator-(const OpCounters& a, const OpCounters& b) {
    return {a.comparisons - b.comparisons, a.node_visits - b.node_visits,
            a.structural - b.structural, a.recolors - b.recolors,
            a.allocations - b.allocations};
}

}  // namespace dict

#ifdef DICT_INSTRUMENT
#define DICT_COUNT(counters, field, n) ((counters).field += (n))
#else
#define DICT_COUNT(counters, field, n) ((void)0)
#endif
