#pragma once

// Key types used by the benchmark and tests, each derived from the 64-bit keys
// the workload generator produces, so every key type replays the same
// operations in the same order.
//
//   int            the 64-bit key itself
//   small_string   11 characters: fits the small string buffer of libstdc++,
//                  MSVC (15) and libc++ (22), so std::string never allocates
//   large_string   64 characters: always heap allocated
//
// Both string encodings preserve order: a < b as integers exactly when their
// strings compare a < b, so "sorted" and "reverse" workloads stay sorted. The
// first 11 characters encode the key in base 64 (4 bits, then 10 x 6 bits)
// using an alphabet in ascending ASCII order. Large strings append 53
// characters derived from the key, so two large keys always differ within the
// first 11 characters. Comparisons therefore cost about the same for both
// string types, and the difference measured is the heap allocation, not
// longer comparisons.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dict {

inline constexpr std::size_t kSmallStringLength = 11;
inline constexpr std::size_t kLargeStringLength = 64;

namespace detail {

// 64 characters in ascending ASCII order: . / 0-9 A-Z a-z
inline constexpr std::string_view kOrderedAlphabet =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

inline void append_ordered(std::string& out, std::uint64_t key) {
    out.push_back(kOrderedAlphabet[key >> 60]);  // top 4 bits
    for (int shift = 54; shift >= 0; shift -= 6) out.push_back(kOrderedAlphabet[(key >> shift) & 63]);
}

}  // namespace detail

inline std::string small_string_key(std::uint64_t key) {
    std::string s;
    s.reserve(kSmallStringLength);
    detail::append_ordered(s, key);
    return s;
}

inline std::string large_string_key(std::uint64_t key) {
    std::string s;
    s.reserve(kLargeStringLength);
    detail::append_ordered(s, key);
    std::uint64_t x = key;
    while (s.size() < kLargeStringLength) {
        // splitmix64 step: deterministic, varied padding characters.
        x += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        for (int i = 0; i < 10 && s.size() < kLargeStringLength; ++i, z >>= 6)
            s.push_back(detail::kOrderedAlphabet[z & 63]);
    }
    return s;
}

enum class KeyKind { Int, SmallString, LargeString };

inline const std::vector<std::string_view>& key_kind_names() {
    static const std::vector<std::string_view> names = {"int", "small_string", "large_string"};
    return names;
}

inline const char* key_kind_name(KeyKind kind) {
    switch (kind) {
        case KeyKind::Int: return "int";
        case KeyKind::SmallString: return "small_string";
        case KeyKind::LargeString: return "large_string";
    }
    return "?";
}

}  // namespace dict
