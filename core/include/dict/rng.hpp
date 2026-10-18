#pragma once

// Small, fast, fully specified PRNG shared by the workload generators and any
// randomized structure. Using our own generator (rather than std::mt19937 or
// rand()) keeps results identical across compilers and standard libraries, so
// a (seed, workload) pair reproduces exactly on any machine.
//
// xoshiro256** by Blackman & Vigna, seeded through splitmix64.

#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace dict {

class Xoshiro256 {
public:
    using result_type = std::uint64_t;

    explicit Xoshiro256(std::uint64_t seed = 0x9E3779B97F4A7C15ull) {
        for (auto& s : state_) s = splitmix64(seed);
    }

    std::uint64_t next() {
        const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
        const std::uint64_t t = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);
        return result;
    }

    std::uint64_t operator()() { return next(); }

    // Uniform integer in [0, bound). Lemire's multiply-shift; the tiny bias is
    // irrelevant for workload generation.
    std::uint64_t below(std::uint64_t bound) {
#if defined(_MSC_VER) && !defined(__clang__)
        return __umulh(next(), bound);
#else
        return static_cast<std::uint64_t>(
            (static_cast<unsigned __int128>(next()) * bound) >> 64);
#endif
    }

    // Uniform double in [0, 1).
    double uniform01() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

    static constexpr std::uint64_t min() { return 0; }
    static constexpr std::uint64_t max() { return ~std::uint64_t{0}; }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    static std::uint64_t splitmix64(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t state_[4];
};

}  // namespace dict
