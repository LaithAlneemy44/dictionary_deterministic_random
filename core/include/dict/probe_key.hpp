#pragma once

// Probe keys for the instrumented benchmark: record which stored keys each
// operation reads, and which memory those reads touch.
//
// A ProbeKey orders exactly like the 64-bit value it wraps, so a structure
// built from probe keys has the same shape as one built from int keys with the
// same seeds. A ProbeString orders exactly like the std::string it wraps (used
// with large_string keys). Every comparison notes both operands. The harness
// calls probe::begin with the address of the operation's own key, so afterwards:
//
//   probe::read   the distinct keys stored in the structure that the operation
//                 compared against, i.e. the nodes whose key it had to load
//   probe::lines  the distinct 64-byte cache lines holding the bytes of those
//                 keys that the comparisons read
//   probe::pages  the distinct 4 KiB pages holding those bytes
//
// This differs from the node_visits counter: a skip list search often compares
// against the same node again after dropping a level, which counts as a visit
// each time but loads that node only once.
//
// Limits: only key bytes are seen. Links (tree child pointers, skip list
// towers) are read too but are invisible here, so lines and pages are lower
// bounds on what an operation touches. For strings, the bytes read are taken to
// run up to the first mismatch; memcmp may read a few bytes further.

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dict {

namespace probe {

inline constexpr std::uintptr_t kLineBytes = 64;
inline constexpr std::uintptr_t kPageBytes = 4096;

inline const void* search_key = nullptr;
inline std::vector<const void*> read;
inline std::vector<std::uintptr_t> lines;
inline std::vector<std::uintptr_t> pages;

template <class T>
inline void add_unique(std::vector<T>& v, T x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
}

// Records the lines and pages spanned by `len` bytes at `p`.
inline void touch(const void* p, std::size_t len) {
    if (len == 0) return;
    const auto first = reinterpret_cast<std::uintptr_t>(p);
    const std::uintptr_t last = first + len - 1;
    for (std::uintptr_t l = first / kLineBytes; l <= last / kLineBytes; ++l) add_unique(lines, l);
    for (std::uintptr_t g = first / kPageBytes; g <= last / kPageBytes; ++g) add_unique(pages, g);
}

// Notes a comparison operand: the key object at `key` (`key_len` bytes read)
// and, for keys that own a separate buffer, `data_len` bytes read at `data`.
inline void note(const void* key, std::size_t key_len, const void* data = nullptr, std::size_t data_len = 0) {
    if (key == search_key) return;
    add_unique(read, key);
    touch(key, key_len);
    touch(data, data_len);
}

// Starts recording an operation whose own key is at `key`.
inline void begin(const void* key) {
    search_key = key;
    read.clear();
    lines.clear();
    pages.clear();
}

}  // namespace probe

class ProbeKey {
public:
    explicit ProbeKey(std::uint64_t value) : value_(value) {}

    std::uint64_t value() const { return value_; }

    friend std::strong_ordering operator<=>(const ProbeKey& a, const ProbeKey& b) {
        probe::note(&a, sizeof a);
        probe::note(&b, sizeof b);
        return a.value_ <=> b.value_;
    }

    friend bool operator==(const ProbeKey& a, const ProbeKey& b) {
        probe::note(&a, sizeof a);
        probe::note(&b, sizeof b);
        return a.value_ == b.value_;
    }

private:
    std::uint64_t value_;
};

class ProbeString {
public:
    explicit ProbeString(std::string s) : s_(std::move(s)) {}

    const std::string& value() const { return s_; }

    friend std::strong_ordering operator<=>(const ProbeString& a, const ProbeString& b) {
        note_pair(a, b);
        return a.s_ <=> b.s_;
    }

    friend bool operator==(const ProbeString& a, const ProbeString& b) {
        note_pair(a, b);
        return a.s_ == b.s_;
    }

private:
    // Both operands are read up to and including the first differing character.
    static void note_pair(const ProbeString& a, const ProbeString& b) {
        const std::size_t common = std::min(a.s_.size(), b.s_.size());
        const auto diff = std::mismatch(a.s_.begin(), a.s_.begin() + static_cast<std::ptrdiff_t>(common), b.s_.begin());
        const auto read_len = static_cast<std::size_t>(diff.first - a.s_.begin()) + 1;
        probe::note(&a, sizeof a.s_, a.s_.data(), std::min(read_len, a.s_.size()));
        probe::note(&b, sizeof b.s_, b.s_.data(), std::min(read_len, b.s_.size()));
    }

    std::string s_;
};

}  // namespace dict
