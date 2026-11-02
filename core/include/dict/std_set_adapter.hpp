#pragma once

// Reference implementation: std::set (a red-black tree in every major standard
// library) behind the OrderedSet interface.
//
// Used as (1) the oracle in differential tests and (2) an "industrial" baseline
// in benchmarks. It is NOT one of the two algorithms being compared.
//
// Only comparisons can be counted (via a counting comparator); std::set does
// not expose node visits or rotations.

#include <cstddef>
#include <functional>
#include <memory>
#include <set>

#include "dict/counters.hpp"

namespace dict {

template <class K, class Compare = std::less<K>>
class StdSetAdapter {
public:
    using key_type = K;

    static constexpr const char* name = "std_set";
    static constexpr bool implemented = true;

    StdSetAdapter() : counters_(std::make_unique<OpCounters>()), set_(CountingLess{counters_.get(), {}}) {}

    bool insert(const K& key) { return set_.insert(key).second; }

    bool contains(const K& key) const { return set_.find(key) != set_.end(); }

    bool erase(const K& key) { return set_.erase(key) != 0; }

    std::size_t size() const { return set_.size(); }
    bool empty() const { return set_.empty(); }
    void clear() { set_.clear(); }

    template <class F>
    void for_each(F&& visit) const {
        for (const K& k : set_) visit(k);
    }

    template <class F>
    void for_each_in_range(const K& lo, const K& hi, F&& visit) const {
        auto last = set_.upper_bound(hi);
        for (auto it = set_.lower_bound(lo); it != last; ++it) visit(*it);
    }

    bool check_invariants() const { return true; }

    const OpCounters& counters() const { return *counters_; }
    void reset_counters() { *counters_ = OpCounters{}; }

private:
    // Holds a pointer (not a reference) to heap-allocated counters so the
    // adapter stays movable without the comparator dangling.
    struct CountingLess {
        OpCounters* counters;
        Compare less;
        bool operator()(const K& a, const K& b) const {
            DICT_COUNT(*counters, comparisons, 1);
            return less(a, b);
        }
    };

    std::unique_ptr<OpCounters> counters_;
    std::set<K, CountingLess> set_;
};

}  // namespace dict
