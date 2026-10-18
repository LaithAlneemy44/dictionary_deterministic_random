#pragma once

// Red-black tree (deterministic balanced BST).
//
// Primary author: Aaron Falco
//
// STATUS: stub. Every operation throws until implemented. When the
// implementation is complete, set `implemented = true` so the tests and
// benchmarks pick it up.
//
// Counting expectations (see dict/counters.hpp):
//   comparisons  - every key comparison
//   node_visits  - every node visited on a search path
//   structural   - every rotation
//   recolors     - every colour change
//   allocations  - every node allocated

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

#include "dict/counters.hpp"

namespace dict {

template <class K, class V, class Compare = std::less<K>>
class RedBlackTree {
public:
    using key_type = K;
    using mapped_type = V;

    static constexpr const char* name = "red_black_tree";
    static constexpr bool implemented = false;

    RedBlackTree() = default;

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

    // Should verify: root is black, no red node has a red child, every
    // root-to-null path has the same black height, BST ordering, parent
    // pointers consistent, and the node count matches size().
    bool check_invariants() const { not_implemented("check_invariants"); }

    const OpCounters& counters() const { return counters_; }
    void reset_counters() { counters_ = OpCounters{}; }

private:
    [[noreturn]] static void not_implemented(const char* op) {
        throw std::logic_error(std::string("RedBlackTree::") + op + " not implemented");
    }

    std::size_t size_ = 0;
    Compare less_{};
    mutable OpCounters counters_{};
};

}  // namespace dict
