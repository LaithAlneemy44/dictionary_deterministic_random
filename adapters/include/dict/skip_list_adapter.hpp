#pragma once

// Adapter: Laith's SkipList<T> (randomised/skip_list.hpp) behind the shared
// dict::OrderedMap interface used by tests/ and bench/.
//
// Primary author of the skip list: Laith. This file is integration glue only;
// it does not modify the skip list.
//
// STATUS: stub (`implemented = false`). SkipList<T> currently exposes a set API
// (insert / erase / exists). The shared harness additionally needs:
//   - key -> value storage, and find() returning a pointer to the value
//   - insert/erase reporting whether the key was new / present
//   - size()
//   - ordered iteration and range iteration (for_each, for_each_in_range)
//   - check_invariants()
//   - a constructor taking a seed (currently seeded from std::random_device,
//     so runs are not reproducible)
//   - counter hooks (DICT_COUNT) for the instrumented benchmark
// Once SkipList provides these, forward to it here and set `implemented = true`.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

#include "dict/counters.hpp"
#include "skip_list.hpp"

namespace dict {

template <class K, class V, class Compare = std::less<K>>
class SkipListAdapter {
public:
    using key_type = K;
    using mapped_type = V;

    static constexpr const char* name = "skip_list";
    static constexpr bool implemented = false;

    SkipListAdapter() = default;
    explicit SkipListAdapter(std::uint64_t /*seed*/) {}

    bool insert(const K& /*key*/, const V& /*value*/) { not_implemented("insert"); }
    const V* find(const K& /*key*/) const { not_implemented("find"); }
    bool contains(const K& /*key*/) const { not_implemented("contains"); }
    bool erase(const K& /*key*/) { not_implemented("erase"); }

    std::size_t size() const { not_implemented("size"); }
    bool empty() const { not_implemented("empty"); }
    void clear() { not_implemented("clear"); }

    template <class F>
    void for_each(F&& /*visit*/) const { not_implemented("for_each"); }

    template <class F>
    void for_each_in_range(const K& /*lo*/, const K& /*hi*/, F&& /*visit*/) const {
        not_implemented("for_each_in_range");
    }

    bool check_invariants() const { not_implemented("check_invariants"); }

    const OpCounters& counters() const { return counters_; }
    void reset_counters() { counters_ = OpCounters{}; }

private:
    [[noreturn]] static void not_implemented(const char* op) {
        throw std::logic_error(std::string("SkipListAdapter::") + op + " not implemented");
    }

    SkipList<K> list_;
    mutable OpCounters counters_{};
};

}  // namespace dict
