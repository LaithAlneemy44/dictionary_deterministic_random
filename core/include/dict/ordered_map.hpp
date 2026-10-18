#pragma once

// The common interface every dictionary implementation must satisfy.
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
template <class K, class V>
struct NoopVisitor {
    void operator()(const K&, const V&) const {}
};

template <class M>
concept OrderedMap =
    requires(M m, const M cm, const typename M::key_type& k,
             const typename M::mapped_type& v,
             NoopVisitor<typename M::key_type, typename M::mapped_type> visit) {
        typename M::key_type;
        typename M::mapped_type;

        // Human-readable name used in CSV output, e.g. "red_black_tree".
        { M::name } -> std::convertible_to<const char*>;
        // False while the module is still a stub; tests and benchmarks skip it.
        { M::implemented } -> std::convertible_to<bool>;

        // Insert k -> v. Returns true if k was new, false if an existing value was overwritten.
        { m.insert(k, v) } -> std::same_as<bool>;
        // Pointer to the value for k, or nullptr if absent.
        { cm.find(k) } -> std::same_as<const typename M::mapped_type*>;
        { cm.contains(k) } -> std::same_as<bool>;
        // Remove k. Returns true if it was present.
        { m.erase(k) } -> std::same_as<bool>;

        { cm.size() } -> std::same_as<std::size_t>;
        { cm.empty() } -> std::same_as<bool>;
        { m.clear() };

        // Visit every (key, value) in ascending key order.
        { cm.for_each(visit) };
        // Visit every (key, value) with lo <= key <= hi in ascending order.
        { cm.for_each_in_range(k, k, visit) };

        // Full structural self-check (e.g. red-black properties, heap or level ordering). Used by tests only.
        { cm.check_invariants() } -> std::same_as<bool>;

        { cm.counters() } -> std::same_as<const OpCounters&>;
        { m.reset_counters() };
    };

// Construct an implementation, passing a seed to those that take one
// (randomized structures). Deterministic structures ignore it.
template <class M>
M make_map(std::uint64_t seed) {
    if constexpr (std::is_constructible_v<M, std::uint64_t>) {
        return M(seed);
    } else {
        return M();
    }
}

}  // namespace dict
