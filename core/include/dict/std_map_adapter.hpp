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
#include <map>
#include <memory>

#include "dict/counters.hpp"

namespace dict {

template <class K, class V>
class StdMapAdapter {
public:
    using key_type = K;
    using mapped_type = V;

    static constexpr const char* name = "std_map";
    static constexpr bool implemented = true;

    StdMapAdapter() : counters_(std::make_unique<OpCounters>()), map_(CountingLess{counters_.get()}) {}

    bool insert(const K& key, const V& value) { return map_.insert_or_assign(key, value).second; }

    const V* find(const K& key) const {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    bool contains(const K& key) const { return map_.find(key) != map_.end(); }

    // at() throws std::out_of_range for a missing key, matching the interface.
    V& operator[](const K& key) { return map_.at(key); }
    const V& operator[](const K& key) const { return map_.at(key); }

    bool erase(const K& key) { return map_.erase(key) != 0; }

    std::size_t size() const { return map_.size(); }
    bool empty() const { return map_.empty(); }
    void clear() { map_.clear(); }

    template <class F>
    void for_each(F&& visit) const {
        for (const auto& [k, v] : map_) visit(k, v);
    }

    bool check_invariants() const { return true; }

    const OpCounters& counters() const { return *counters_; }
    void reset_counters() { *counters_ = OpCounters{}; }

private:
    // Holds a pointer (not a reference) to heap-allocated counters so the
    // adapter stays movable without the comparator dangling.
    struct CountingLess {
        OpCounters* counters;
        bool operator()(const K& a, const K& b) const {
            DICT_COUNT(*counters, comparisons, 1);
            return a < b;
        }
    };

    std::unique_ptr<OpCounters> counters_;
    std::map<K, V, CountingLess> map_;
};

}  // namespace dict
