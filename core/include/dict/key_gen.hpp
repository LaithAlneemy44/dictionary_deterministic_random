#pragma once

// Key generators for the comparison benchmark (bench/compare_main.cpp).
//
// Three key types:
//   int    32-bit signed integers
//   str10  std::string of exactly 10 lowercase letters (fits MSVC's small-string buffer: no heap)
//   str30  std::string of exactly 30 lowercase letters (exceeds it: one heap block per string)
//
// make_keys<K>(count, seed) returns `count` DISTINCT keys in pseudo-random
// order, without needing a set to deduplicate. Key i is derived from
// bijection(offset + i), so distinct indices always give distinct keys.
// Callers take the first n as the keys to insert and the rest as keys that are
// guaranteed absent (search misses).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dict/rng.hpp"

namespace dict::keys {

namespace detail {

// "lowbias32" integer hash (C. Wellons): xorshift-multiply rounds, each a
// bijection on 32-bit values, so the whole function is a bijection.
inline std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Bijection on 46-bit values: xorshift and multiply-by-odd mod 2^46 are each
// invertible on the 46-bit domain.
constexpr std::uint64_t kMask46 = (std::uint64_t{1} << 46) - 1;

inline std::uint64_t mix46(std::uint64_t x) {
    x &= kMask46;
    x ^= x >> 23;
    x = (x * 0x9E3779B97F4A7C15ULL) & kMask46;
    x ^= x >> 21;
    x = (x * 0xBF58476D1CE4E5B9ULL) & kMask46;
    x ^= x >> 24;
    return x;
}

// Fixed-length lowercase string whose first 10 characters are the base-26
// digits of a 46-bit value (least significant first, so every position is
// close to uniform). 26^10 > 2^46, so the 10-digit prefix is injective. Any
// further characters are filler drawn from an RNG seeded by the value.
inline std::string encode(std::uint64_t x, std::size_t length) {
    std::string s(length, 'a');
    std::uint64_t v = x;
    for (std::size_t i = 0; i < 10 && i < length; ++i) {
        s[i] = static_cast<char>('a' + v % 26);
        v /= 26;
    }
    if (length > 10) {
        Xoshiro256 rng(x);
        for (std::size_t i = 10; i < length; ++i) s[i] = static_cast<char>('a' + rng.below(26));
    }
    return s;
}

inline std::vector<std::string> make_strings(std::size_t count, std::uint64_t seed, std::size_t length) {
    Xoshiro256 rng(seed);
    const std::uint64_t offset = rng.next() & kMask46;
    std::vector<std::string> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.push_back(encode(mix46(offset + i), length));
    return out;
}

}  // namespace detail

// Tag types naming each key type in the benchmark; `type` is the C++ key type.
struct Int {
    using type = int;
    static constexpr std::string_view name = "int";
    static std::vector<int> make(std::size_t count, std::uint64_t seed) {
        Xoshiro256 rng(seed);
        const auto offset = static_cast<std::uint32_t>(rng.next());
        std::vector<int> out(count);
        for (std::size_t i = 0; i < count; ++i)
            out[i] = static_cast<int>(detail::mix32(offset + static_cast<std::uint32_t>(i)));
        return out;
    }
};

struct Str10 {
    using type = std::string;
    static constexpr std::string_view name = "str10";
    static std::vector<std::string> make(std::size_t count, std::uint64_t seed) {
        return detail::make_strings(count, seed, 10);
    }
};

struct Str30 {
    using type = std::string;
    static constexpr std::string_view name = "str30";
    static std::vector<std::string> make(std::size_t count, std::uint64_t seed) {
        return detail::make_strings(count, seed, 30);
    }
};

}  // namespace dict::keys
