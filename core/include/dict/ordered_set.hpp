#pragma once

// The common interface every dictionary implementation must satisfy.
//
// Implementations are ordered sets of unique keys: inserting a key that is
// already present changes nothing.
//
// Tests and benchmarks are written once against this concept and instantiated
// for each implementation, so both structures are exercised by identical code
// on identical inputs.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "dict/counters.hpp"

namespace dict {

// Concrete visitor type used only to check the concept (a lambda cannot
// appear in a requires-expression portably).
template <class K>
struct NoopVisitor {
    void operator()(const K&) const {}
};

template <class S>
concept OrderedSet =
    requires(S s, const S cs, const typename S::key_type& k, NoopVisitor<typename S::key_type> visit) {
        typename S::key_type;

        // Human-readable name used in CSV output, e.g. "red_black_tree".
        { S::name } -> std::convertible_to<const char*>;
        // False while the module is still a stub; tests and benchmarks skip it.
        { S::implemented } -> std::convertible_to<bool>;

        // Insert k. Returns true if k was new, false if it was already present (nothing changes).
        { s.insert(k) } -> std::same_as<bool>;
        { cs.contains(k) } -> std::same_as<bool>;
        // Remove k. Returns true if it was present.
        { s.erase(k) } -> std::same_as<bool>;

        { cs.size() } -> std::same_as<std::size_t>;
        { cs.empty() } -> std::same_as<bool>;
        { s.clear() };

        // Visit every key in ascending order. Used by tests only.
        { cs.for_each(visit) };

        // Full structural self-check (e.g. red-black properties, heap or level ordering). Used by tests only.
        { cs.check_invariants() } -> std::same_as<bool>;

        { cs.counters() } -> std::same_as<const OpCounters&>;
        { s.reset_counters() };
    };

// Construct an implementation, passing a seed to those that take one
// (randomized structures). Deterministic structures ignore it.
template <class S>
S make_set(std::uint64_t seed) {
    if constexpr (std::is_constructible_v<S, std::uint64_t>) {
        return S(seed);
    } else {
        return S();
    }
}

}  // namespace dict
