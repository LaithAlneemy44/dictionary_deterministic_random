#pragma once

// Input families shared by the tests and the benchmark.
//
// A workload is a fixed sequence of phases, each a vector of operations. It is
// generated entirely up front (outside any timed region) from an input seed,
// so every implementation replays exactly the same operations.

#include <algorithm>
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

// n lookups over the keys 0, 2, ..., 2n-2: half hits, half misses on the odd
// numbers between them, so misses land in random gaps rather than all beyond
// the largest key.
inline std::vector<Op> gap_lookups(std::size_t n, Xoshiro256& rng) {
    std::vector<Op> ops;
    ops.reserve(n);
    for (std::size_t i = 0; i < n; ++i) ops.push_back({OpKind::Find, 2 * rng.below(n) + (i % 2)});
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
        "uniform", "sorted", "reverse", "sliding_window", "zipf", "zipf_shuffled", "steady"};
    return families;
}

// Build a workload of the given family over n keys.
//
//   uniform        insert n random keys in random order; n lookups (50% hits); erase all in random order
//   sorted         insert 0, 2, ..., 2n-2 in ascending order;  n lookups (50% hits, misses in the gaps);
//                  erase all in ascending order
//   reverse        as sorted, but inserted and erased in descending order
//   sliding_window insert n random keys; then n rounds of (insert a new key, erase the oldest key)
//   zipf           insert n random keys; n Zipf-skewed lookups (all hits). The r-th key inserted has
//                  popularity rank r, so the hottest keys are the oldest.
//   zipf_shuffled  as zipf, but popularity ranks are an independent shuffle of the keys, unrelated to
//                  insertion order
//   steady         insert n random keys; then, with m = n/4 fresh keys: m lookups of the fresh keys
//                  (all misses), insert them, m lookups of them again (all hits), erase them. Size stays
//                  in [n, 1.25n] and each write retraces its matching lookup's search path, so
//                  insert_new - find_miss and erase_new - find_new are the cost of the write itself.
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
        for (std::size_t i = 0; i < n; ++i) ordered[i] = 2 * ((family == "sorted") ? i : n - 1 - i);
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, ordered)});
        w.phases.push_back({"find", detail::gap_lookups(n, rng)});
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
    } else if (family == "zipf" || family == "zipf_shuffled") {
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, keys)});
        std::vector<Key> by_rank = keys;
        if (family == "zipf_shuffled") detail::shuffle(by_rank, rng);
        std::vector<Op> lookups;
        lookups.reserve(n);
        for (std::size_t i = 0; i < n; ++i) lookups.push_back({OpKind::Find, by_rank[detail::zipf_rank(n, rng)]});
        w.phases.push_back({"find", std::move(lookups)});
    } else if (family == "steady") {
        const std::size_t m = std::max<std::size_t>(1, n / 4);
        const std::vector<Key> all = detail::distinct_random_keys(n + m, input_seed);
        const std::vector<Key> fresh(all.begin() + static_cast<std::ptrdiff_t>(n), all.end());
        w.phases.push_back({"insert", detail::ops_of(OpKind::Insert, keys)});
        w.phases.push_back({"find_miss", detail::ops_of(OpKind::Find, fresh)});
        w.phases.push_back({"insert_new", detail::ops_of(OpKind::Insert, fresh)});
        w.phases.push_back({"find_new", detail::ops_of(OpKind::Find, fresh)});
        w.phases.push_back({"erase_new", detail::ops_of(OpKind::Erase, fresh)});
    } else {
        throw std::invalid_argument("unknown workload family: " + std::string(family));
    }
    return w;
}

}  // namespace dict
