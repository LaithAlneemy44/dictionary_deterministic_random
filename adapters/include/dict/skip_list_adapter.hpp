#pragma once

// Adapter: Laith's SkipList<T> (randomised/skip_list.hpp) behind the shared
// dict::OrderedSet interface used by tests/ and bench/.
//
// Primary author of the skip list: Laith. This file is integration glue only:
// every method forwards to the skip list, and the seed passed by the benchmark
// seeds its tower heights.

#include <cstddef>
#include <cstdint>
#include <utility>

#include "dict/counters.hpp"
#include "skip_list.hpp"

namespace dict {

template <class K>
class SkipListAdapter {
public:
    using key_type = K;

    static constexpr const char* name = "skip_list";
    static constexpr bool implemented = true;

    SkipListAdapter() = default;
    explicit SkipListAdapter(std::uint64_t seed) : list_(seed) {}

    bool insert(const K& key) { return list_.insert(key); }
    bool contains(const K& key) const { return list_.exists(key); }
    bool erase(const K& key) { return list_.erase(key); }

    std::size_t size() const { return list_.size(); }
    bool empty() const { return list_.empty(); }
    void clear() { list_.clear(); }

    template <class F>
    void for_each(F&& visit) const { list_.for_each(std::forward<F>(visit)); }

    bool check_invariants() const { return list_.check_invariants(); }

    const OpCounters& counters() const { return list_.counters(); }
    void reset_counters() { list_.reset_counters(); }

private:
    SkipList<K> list_;
};

}  // namespace dict
