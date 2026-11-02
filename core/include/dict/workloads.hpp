#pragma once

// Input families shared by the tests and the benchmark.
//
// A workload is a fixed sequence of phases, each a vector of operations. It is
// generated entirely up front (outside any timed region) from an input seed,
// so every implementation replays exactly the same operations.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dict/rng.hpp"

namespace dict {

using Key = std::uint64_t;
using Value = std::uint64_t;

enum class OpKind : std::uint8_t { Insert, Find, Erase };

struct Op {
    OpKind kind;
    Key key;
};

struct Phase {
    std::string name;
    std::vector<Op> ops;
};

struct Workload {
    std::string family;
    std::size_t n = 0;
    std::uint64_t input_seed = 0;
    std::vector<Phase> phases;
};

namespace detail {

// splitmix64 finalizer: a bijection on 64-bit integers, so distinct inputs
// give distinct, well-scrambled keys without needing a set to deduplicate.
inline Key scramble(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// n distinct pseudo-random keys.
inline std::vector<Key> distinct_random_keys(std::size_t n, std::uint64_t seed) {
    std::vector<Key> keys(n);
    const std::uint64_t base = scramble(seed);
    for (std::size_t i = 0; i < n; ++i) keys[i] = scramble(base + i);
    return keys;
}

inline void shuffle(std::vector<Key>& v, Xoshiro256& rng) {
    for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[rng.below(i)]);
}

inline std::vector<Op> ops_of(OpKind kind, const std::vector<Key>& keys) {
    std::vector<Op> ops;
    ops.reserve(keys.size());
    for (Key k : keys) ops.push_back({kind, k});
    return ops;
}

// n lookups: half hits (uniform over present keys), half misses.
inline std::vector<Op> mixed_lookups(const std::vector<Key>& present, std::size_t n, Xoshiro256& rng) {
    std::vector<Op> ops;
    ops.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Key k = (i % 2 == 0) ? present[rng.below(present.size())] : rng.next();
        ops.push_back({OpKind::Find, k});
    }
    return ops;
}

// Approximate Zipf(s = 1) rank in [0, n): inverts the continuous CDF
// F(x) = ln(x) / ln(n + 1). Cheap and O(1) memory, good enough to create a
// skewed access pattern (rank 0 is the hottest key).
inline std::size_t zipf_rank(std::size_t n, Xoshiro256& rng) {
    const double x = std::exp(rng.uniform01() * std::log(static_cast<double>(n) + 1.0));
    const auto r = static_cast<std::size_t>(x) - 1;
    return r < n ? r : n - 1;
}

}  // namespace detail

inline const std::vector<std::string_view>& workload_families() {
    static const std::vector<std::string_view> families = {
        "uniform", "sorted", "reverse", "sliding_window", "zipf"};
    return families;
}

// Build a workload of the given family over n keys.
//
//   uniform        insert n random keys in random order; n lookups (50% hits); erase all in random order
//   sorted         insert keys in ascending order;       n lookups;            erase all in ascending order
//   reverse        insert keys in descending order;      n lookups;            erase all in descending order
//   sliding_window insert n random keys; then n rounds of (insert a new key, erase the oldest key)
//   zipf           insert n random keys; n Zipf-skewed lookups (all hits)
inline Workload make_workload(std::string_view family, std::size_t n, std::uint64_t input_seed) {
    if (n == 0) throw std::invalid_argument("workload size must be > 0");

    Workload w{std::string(family), n, input_seed, {}};
    Xoshiro256 rng(input_seed);
    std::vector<Key> keys = detail::distinct_random_keys(n, input_seed);

    if (family == "uniform") {
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, keys)});
        w.phases.push_back({"find", detail::mixed_lookups(keys, n, rng)});
        detail::shuffle(keys, rng);
        w.phases.push_back({"erase", detail::ops_of(OpKind::Erase, keys)});
    } else if (family == "sorted" || family == "reverse") {
        std::vector<Key> ordered(n);
        for (std::size_t i = 0; i < n; ++i) ordered[i] = (family == "sorted") ? i : n - 1 - i;
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, ordered)});
        w.phases.push_back({"find", detail::mixed_lookups(ordered, n, rng)});
        w.phases.push_back({"erase", detail::ops_of(OpKind::Erase, ordered)});
    } else if (family == "sliding_window") {
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, keys)});
        // Keys n..2n-1 of the same bijective sequence are guaranteed fresh.
        const std::vector<Key> fresh = detail::distinct_random_keys(2 * n, input_seed);
        std::vector<Op> churn;
        churn.reserve(2 * n);
        for (std::size_t i = 0; i < n; ++i) {
            churn.push_back({OpKind::Insert, fresh[n + i]});
            churn.push_back({OpKind::Erase, keys[i]});
        }
        w.phases.push_back({"churn", std::move(churn)});
    } else if (family == "zipf") {
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, keys)});
        std::vector<Op> lookups;
        lookups.reserve(n);
        for (std::size_t i = 0; i < n; ++i) lookups.push_back({OpKind::Find, keys[detail::zipf_rank(n, rng)]});
        w.phases.push_back({"find", std::move(lookups)});
    } else {
        throw std::invalid_argument("unknown workload family: " + std::string(family));
    }
    return w;
}

}  // namespace dict
