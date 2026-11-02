#pragma once

// Adapter: Laith's SkipList (randomised/skip_list.hpp) behind the shared
// dict::OrderedMap interface used by tests/ and bench/.
//
// STATUS: temporary stub while SkipList is converted to key-value storage.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "dict/counters.hpp"
#include "skip_list.hpp"

namespace dict {

template <class K, class V>
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
    V& operator[](const K& /*key*/) { not_implemented("operator[]"); }
    const V& operator[](const K& /*key*/) const { not_implemented("operator[]"); }
    bool erase(const K& /*key*/) { not_implemented("erase"); }

    std::size_t size() const { not_implemented("size"); }
    bool empty() const { not_implemented("empty"); }
    void clear() { not_implemented("clear"); }

    template <class F>
    void for_each(F&& /*visit*/) const { not_implemented("for_each"); }

    bool check_invariants() const { not_implemented("check_invariants"); }

    const OpCounters& counters() const { return counters_; }
    void reset_counters() { counters_ = OpCounters{}; }

private:
    [[noreturn]] static void not_implemented(const char* op) {
        throw std::logic_error(std::string("SkipListAdapter::") + op + " not implemented");
    }

    OpCounters counters_{};
};

}  // namespace dict
