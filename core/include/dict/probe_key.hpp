#pragma once

// Probe key for the instrumented benchmark: records which stored keys each
// operation reads.
//
// A ProbeKey orders exactly like the 64-bit value it wraps, so a structure
// built from probe keys has the same shape as one built from int keys with the
// same seeds. Every comparison notes the addresses of both operands. The
// harness calls probe::begin with the address of the operation's own key, so
// afterwards probe::read holds the distinct keys stored in the structure that
// the operation compared against, i.e. the nodes whose key it had to load.
//
// This differs from the node_visits counter: a skip list search often compares
// against the same node again after dropping a level, which counts as a visit
// each time but loads that node only once.

#include <algorithm>
#include <compare>
#include <cstdint>
#include <vector>

namespace dict {

namespace probe {

inline const void* search_key = nullptr;
inline std::vector<const void*> read;

inline void note(const void* key) {
    if (key != search_key && std::find(read.begin(), read.end(), key) == read.end()) read.push_back(key);
}

// Starts recording an operation whose own key is at `key`.
inline void begin(const void* key) {
    search_key = key;
    read.clear();
}

}  // namespace probe

class ProbeKey {
public:
    explicit ProbeKey(std::uint64_t value) : value_(value) {}

    std::uint64_t value() const { return value_; }

    friend std::strong_ordering operator<=>(const ProbeKey& a, const ProbeKey& b) {
        probe::note(&a);
        probe::note(&b);
        return a.value_ <=> b.value_;
    }

    friend bool operator==(const ProbeKey& a, const ProbeKey& b) {
        probe::note(&a);
        probe::note(&b);
        return a.value_ == b.value_;
    }

private:
    std::uint64_t value_;
};

}  // namespace dict
