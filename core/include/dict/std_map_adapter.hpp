#pragma once

// Reference implementation: std::map (a red-black tree in every major standard
// library) behind the OrderedMap interface.
//
// Used as (1) the oracle in differential tests and (2) an "industrial" baseline
// in benchmarks. It is NOT one of the two algorithms being compared.
//
// Only comparisons can be counted (via a counting comparator); std::map does
// not expose node visits or rotations.

#include <cstddef>
#include <functional>
#include <map>
#include <memory>

#include "dict/counters.hpp"

namespace dict {

template <class K, class V, class Compare = std::less<K>>
class StdMapAdapter {
public:
    using key_type = K;
    using mapped_type = V;

    static constexpr const char* name = "std_map";
    static constexpr bool implemented = true;

    StdMapAdapter() : counters_(std::make_unique<OpCounters>()), map_(CountingLess{counters_.get(), {}}) {}

    bool insert(const K& key, const V& value) { return map_.insert_or_assign(key, value).second; }

    const V* find(const K& key) const {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    bool contains(const K& key) const { return map_.find(key) != map_.end(); }

    bool erase(const K& key) { return map_.erase(key) != 0; }

    std::size_t size() const { return map_.size(); }
    bool empty() const { return map_.empty(); }
    void clear() { map_.clear(); }

    template <class F>
    void for_each(F&& visit) const {
        for (const auto& [k, v] : map_) visit(k, v);
    }

    template <class F>
    void for_each_in_range(const K& lo, const K& hi, F&& visit) const {
        auto last = map_.upper_bound(hi);
        for (auto it = map_.lower_bound(lo); it != last; ++it) visit(it->first, it->second);
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
    std::map<K, V, CountingLess> map_;
};

}  // namespace dict
