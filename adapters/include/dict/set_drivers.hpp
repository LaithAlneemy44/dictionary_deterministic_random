#pragma once

// Set-style drivers for the comparison benchmark (bench/compare_main.cpp).
//
// Both structures are driven through the same three calls:
//   insert(key)   add the key (no-op if present)
//   search(key)   true if the key is present
//   erase(key)    remove the key (no-op if absent)
//
// This is the common subset of the red-black tree's map API and the
// SkipList<T> set API (randomised/skip_list.hpp), so neither structure does
// work the other skips: no values are stored or returned. Integration glue
// only; neither structure is modified.
//
// Known differences that the report should mention:
//   - SkipList takes keys by value in insert/erase/exists (and again in its
//     internal search), so every call copies the key. For str30 that is a heap
//     allocation per operation. RedBlackTree takes const K&.
//   - SkipList seeds itself from std::random_device, so each instance has a
//     fresh, unreproducible level sequence. The benchmark therefore cannot fix
//     the algorithm seed; each repetition is an independent draw.

#include <string_view>

#include "dict/red_black_tree.hpp"
#include "skip_list.hpp"

namespace dict {

template <class K>
class RbtSet {
public:
    static constexpr std::string_view name = "red_black_tree";

    void insert(const K& key) { tree_.insert(key, Unit{}); }
    bool search(const K& key) const { return tree_.contains(key); }
    void erase(const K& key) { tree_.erase(key); }

private:
    struct Unit {};
    RedBlackTree<K, Unit> tree_;
};

template <class K>
class SkipSet {
public:
    static constexpr std::string_view name = "skip_list";

    void insert(const K& key) { list_.insert(key); }
    bool search(const K& key) { return list_.exists(key); }
    void erase(const K& key) { list_.erase(key); }

private:
    SkipList<K> list_;
};

}  // namespace dict
