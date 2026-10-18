#pragma once

// Randomized ordered dictionary.
//
// Primary author: Laith
//
// STATUS: stub. The specific structure (e.g. skip list or treap) is still to
// be decided. Once chosen, rename the class, `name`, this file and the module
// folder to match (see README "Implementing a module").
//
// Every operation throws until implemented. When the implementation is
// complete, set `implemented = true` so the tests and benchmarks pick it up.
//
// Randomness must come only from `rng_`, seeded through the constructor, so a
// run is exactly reproducible from (input seed, algorithm seed). The benchmark
// varies the algorithm seed while holding the input fixed to measure the
// distribution caused by the algorithm's own randomness.
//
// Counting expectations (see dict/counters.hpp):
//   comparisons  - every key comparison
//   node_visits  - every node (or level step) on a search path
//   structural   - every restructuring step on insert/erase
//                  (e.g. pointer splices for a skip list, rotations for a treap)
//   allocations  - every node allocated

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

#include "dict/counters.hpp"
#include "dict/rng.hpp"

namespace dict {

template <class K, class V, class Compare = std::less<K>>
class RandomizedMap {
public:
    using key_type = K;
    using mapped_type = V;

    static constexpr const char* name = "randomized";
    static constexpr bool implemented = false;

    RandomizedMap() : RandomizedMap(0x5EED5EED5EED5EEDull) {}
    explicit RandomizedMap(std::uint64_t seed) : rng_(seed) {}

    bool insert(const K& /*key*/, const V& /*value*/) { not_implemented("insert"); }
    const V* find(const K& /*key*/) const { not_implemented("find"); }
    bool contains(const K& key) const { return find(key) != nullptr; }
    bool erase(const K& /*key*/) { not_implemented("erase"); }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { not_implemented("clear"); }

    template <class F>
    void for_each(F&& /*visit*/) const { not_implemented("for_each"); }

    template <class F>
    void for_each_in_range(const K& /*lo*/, const K& /*hi*/, F&& /*visit*/) const {
        not_implemented("for_each_in_range");
    }

    // Should verify every structural property of the chosen structure
    // (ordering, level/priority/heap properties, links) and that the element
    // count matches size().
    bool check_invariants() const { not_implemented("check_invariants"); }

    const OpCounters& counters() const { return counters_; }
    void reset_counters() { counters_ = OpCounters{}; }

private:
    [[noreturn]] static void not_implemented(const char* op) {
        throw std::logic_error(std::string("RandomizedMap::") + op + " not implemented");
    }

    std::size_t size_ = 0;
    Compare less_{};
    Xoshiro256 rng_;
    mutable OpCounters counters_{};
};

}  // namespace dict
