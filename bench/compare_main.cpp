// Comparison benchmark: red-black tree vs skip list.
//
// For each key type (int, str10, str30) and each size n = 2^min-exp .. 2^max-exp:
//   insert  n distinct keys, in pseudo-random order
//   search  n lookups, half hits (uniform over present keys) and half misses, shuffled
//   erase   all n keys, in a fresh random order
//
// Both structures are driven through the set-style calls in
// adapters/include/dict/set_drivers.hpp and replay exactly the same keys in the
// same order. Keys and orders are generated before any timing starts.
//
// Two kinds of repetition per (type, n, input seed):
//   batch reps    each phase timed as a whole with steady_clock -> mean ns/op
//   latency reps  every operation timed with the CPU timestamp counter -> per-op
//                 distribution (percentiles and a log-binned histogram)
// Per-op timing adds overhead and perturbs the pipeline, so the two are never
// mixed in one repetition. The measured timer overhead (median of empty timed
// regions) is subtracted from every per-op sample.
//
// The order of the two implementations alternates between repetitions so
// neither always runs on a warmer or cooler machine.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include "dict/key_gen.hpp"
#include "dict/rng.hpp"
#include "dict/set_drivers.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::vector<std::string> impls{"red_black_tree", "skip_list"};
    std::vector<std::string> types{"int", "str10", "str30"};
    int min_exp = 10;
    int max_exp = 23;
    std::uint64_t seeds = 3;
    int reps = 3;
    int latency_reps = 1;
    int warmup = 1;
    std::string out = "compare.csv";
    std::string lat_out = "compare_latency.csv";
    std::string hist_out = "compare_hist.csv";
};

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, ',');)
        if (!item.empty()) parts.push_back(item);
    return parts;
}

// Prints usage and exits; msg == nullptr means --help (exit 0).
[[noreturn]] void usage(const char* msg) {
    if (msg) std::fprintf(stderr, "error: %s\n\n", msg);
    std::fprintf(stderr,
                 "usage: dict_compare [options]\n"
                 "  --impl LIST          comma list of red_black_tree,skip_list (default both)\n"
                 "  --types LIST         comma list of int,str10,str30 (default all)\n"
                 "  --min-exp E          smallest size is 2^E (default 10)\n"
                 "  --max-exp E          largest size is 2^E (default 23)\n"
                 "  --seeds K            number of input seeds (default 3)\n"
                 "  --reps R             batch-timed repetitions per seed (default 3)\n"
                 "  --latency-reps L     per-op-timed repetitions per seed (default 1)\n"
                 "  --warmup W           discarded repetitions per seed (default 1)\n"
                 "  --out FILE           per-phase throughput CSV (default compare.csv)\n"
                 "  --lat-out FILE       per-op latency percentiles CSV (default compare_latency.csv)\n"
                 "  --hist-out FILE      per-op latency histogram CSV (default compare_hist.csv)\n");
    std::exit(msg ? 2 : 0);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage("missing value after flag");
            return argv[++i];
        };
        if (a == "--help" || a == "-h") usage(nullptr);
        else if (a == "--impl") o.impls = split(next());
        else if (a == "--types") o.types = split(next());
        else if (a == "--min-exp") o.min_exp = std::stoi(next());
        else if (a == "--max-exp") o.max_exp = std::stoi(next());
        else if (a == "--seeds") o.seeds = std::stoull(next());
        else if (a == "--reps") o.reps = std::stoi(next());
        else if (a == "--latency-reps") o.latency_reps = std::stoi(next());
        else if (a == "--warmup") o.warmup = std::stoi(next());
        else if (a == "--out") o.out = next();
        else if (a == "--lat-out") o.lat_out = next();
        else if (a == "--hist-out") o.hist_out = next();
        else usage(("unknown flag: " + std::string(a)).c_str());
    }
    if (o.min_exp < 1 || o.max_exp > 26 || o.min_exp > o.max_exp) usage("need 1 <= min-exp <= max-exp <= 26");
    return o;
}

bool selected(const std::vector<std::string>& list, std::string_view name) {
    return std::find(list.begin(), list.end(), name) != list.end();
}

// ---- Timestamp counter -----------------------------------------------------

// lfence on both sides keeps the operation from being reordered across the read.
inline std::uint64_t tsc() {
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

struct TscInfo {
    double ghz = 0;              // timestamp ticks per nanosecond
    std::uint64_t overhead = 0;  // median ticks of an empty timed region
};

TscInfo calibrate_tsc() {
    TscInfo info;
    const auto c0 = Clock::now();
    const std::uint64_t t0 = tsc();
    while (Clock::now() - c0 < std::chrono::milliseconds(300)) {}
    const std::uint64_t t1 = tsc();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - c0).count();
    info.ghz = static_cast<double>(t1 - t0) / static_cast<double>(ns);

    std::vector<std::uint64_t> empty(1'000'000);
    for (auto& e : empty) {
        const std::uint64_t a = tsc();
        const std::uint64_t b = tsc();
        e = b - a;
    }
    std::nth_element(empty.begin(), empty.begin() + empty.size() / 2, empty.end());
    info.overhead = empty[empty.size() / 2];
    return info;
}

// ---- Phases ----------------------------------------------------------------

enum class Phase { Insert, Search, Erase };

constexpr std::string_view phase_name(Phase p) {
    switch (p) {
        case Phase::Insert: return "insert";
        case Phase::Search: return "search";
        case Phase::Erase: return "erase";
    }
    return "?";
}

// One operation. Search results are summed into a checksum, which both stops
// the optimizer discarding lookups and is checked against the expected hits.
template <Phase P, class S, class K>
inline std::uint64_t apply(S& s, const K& key) {
    if constexpr (P == Phase::Insert) {
        s.insert(key);
        return 0;
    } else if constexpr (P == Phase::Search) {
        return s.search(key) ? 1 : 0;
    } else {
        s.erase(key);
        return 0;
    }
}

template <Phase P, class S, class K>
std::uint64_t run_batch(S& s, const std::vector<K>& keys, const std::vector<std::uint32_t>& order,
                        std::int64_t& ns) {
    std::uint64_t sum = 0;
    const auto t0 = Clock::now();
    for (const std::uint32_t i : order) sum += apply<P>(s, keys[i]);
    const auto t1 = Clock::now();
    ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    return sum;
}

template <Phase P, class S, class K>
std::uint64_t run_latency(S& s, const std::vector<K>& keys, const std::vector<std::uint32_t>& order,
                          std::vector<std::uint32_t>& ticks) {
    std::uint64_t sum = 0;
    ticks.resize(order.size());
    for (std::size_t j = 0; j < order.size(); ++j) {
        const std::uint64_t t0 = tsc();
        sum += apply<P>(s, keys[order[j]]);
        const std::uint64_t t1 = tsc();
        const std::uint64_t d = t1 - t0;
        ticks[j] = static_cast<std::uint32_t>(std::min<std::uint64_t>(d, std::numeric_limits<std::uint32_t>::max()));
    }
    return sum;
}

// ---- Output ----------------------------------------------------------------

struct Context {
    const Options& opt;
    TscInfo tsc;
    std::ofstream& csv;
    std::ofstream& lat;
    std::ofstream& hist;
};

struct RunId {
    std::string_view impl, type;
    std::size_t n;
    int log2n;
    std::uint64_t seed;
    int rep;
};

void write_prefix(std::ostream& os, const RunId& id) {
    os << id.impl << ',' << id.type << ',' << id.n << ',' << id.log2n << ',' << id.seed;
}

// Sorts `ticks`, then writes percentiles and a histogram with 8 log-spaced bins
// per octave of nanoseconds.
void write_latency(Context& ctx, const RunId& id, Phase p, std::vector<std::uint32_t>& ticks) {
    std::sort(ticks.begin(), ticks.end());
    const double per_ns = ctx.tsc.ghz;
    const auto to_ns = [&](std::uint32_t t) {
        return t > ctx.tsc.overhead ? static_cast<double>(t - ctx.tsc.overhead) / per_ns : 0.0;
    };
    const auto pct = [&](double q) {  // nearest-rank percentile
        const auto rank = static_cast<std::size_t>(std::ceil(q * static_cast<double>(ticks.size())));
        return to_ns(ticks[std::clamp<std::size_t>(rank, 1, ticks.size()) - 1]);
    };
    double total = 0;
    for (const std::uint32_t t : ticks) total += to_ns(t);

    write_prefix(ctx.lat, id);
    ctx.lat << ',' << id.rep << ',' << phase_name(p) << ',' << ticks.size() << ',' << ctx.tsc.ghz << ','
            << ctx.tsc.overhead << ',' << total / static_cast<double>(ticks.size()) << ',' << pct(0.50) << ','
            << pct(0.90) << ',' << pct(0.99) << ',' << pct(0.999) << ',' << pct(0.9999) << ','
            << to_ns(ticks.back()) << '\n';

    constexpr double kBinsPerOctave = 8;
    const auto bin_of = [&](double ns) {
        return ns < 1.0 ? -1 : static_cast<int>(std::floor(kBinsPerOctave * std::log2(ns)));
    };
    const auto bin_lo = [&](int b) { return b < 0 ? 0.0 : std::exp2(b / kBinsPerOctave); };
    std::size_t i = 0;
    while (i < ticks.size()) {
        const int b = bin_of(to_ns(ticks[i]));
        std::size_t j = i;
        while (j < ticks.size() && bin_of(to_ns(ticks[j])) == b) ++j;
        write_prefix(ctx.hist, id);
        ctx.hist << ',' << id.rep << ',' << phase_name(p) << ',' << bin_lo(b) << ',' << bin_lo(b + 1) << ','
                 << (j - i) << '\n';
        i = j;
    }
}

// ---- Driver ----------------------------------------------------------------

[[noreturn]] void fail_checksum(const RunId& id, std::uint64_t got, std::uint64_t want) {
    std::fprintf(stderr, "error: %.*s %.*s n=%zu: search found %llu keys, expected %llu\n", int(id.impl.size()),
                 id.impl.data(), int(id.type.size()), id.type.data(), id.n, static_cast<unsigned long long>(got),
                 static_cast<unsigned long long>(want));
    std::exit(1);
}

template <class K>
struct Inputs {
    const std::vector<K>& keys;  // 2n keys: [0, n) are inserted, [n, 2n) never are
    const std::vector<std::uint32_t>& insert_order;
    const std::vector<std::uint32_t>& search_order;
    const std::vector<std::uint32_t>& erase_order;
    std::uint64_t expected_hits;
};

template <class S, class K>
void run_once(Context& ctx, const Inputs<K>& in, const RunId& id, bool latency, bool record) {
    S s;
    if (latency) {
        std::vector<std::uint32_t> ticks;
        run_latency<Phase::Insert>(s, in.keys, in.insert_order, ticks);
        if (record) write_latency(ctx, id, Phase::Insert, ticks);
        const std::uint64_t hits = run_latency<Phase::Search>(s, in.keys, in.search_order, ticks);
        if (hits != in.expected_hits) fail_checksum(id, hits, in.expected_hits);
        if (record) write_latency(ctx, id, Phase::Search, ticks);
        run_latency<Phase::Erase>(s, in.keys, in.erase_order, ticks);
        if (record) write_latency(ctx, id, Phase::Erase, ticks);
        return;
    }

    std::array<std::int64_t, 3> ns{};
    run_batch<Phase::Insert>(s, in.keys, in.insert_order, ns[0]);
    const std::uint64_t hits = run_batch<Phase::Search>(s, in.keys, in.search_order, ns[1]);
    if (hits != in.expected_hits) fail_checksum(id, hits, in.expected_hits);
    run_batch<Phase::Erase>(s, in.keys, in.erase_order, ns[2]);
    if (!record) return;

    const std::array<std::size_t, 3> ops{in.insert_order.size(), in.search_order.size(), in.erase_order.size()};
    for (int p = 0; p < 3; ++p) {
        write_prefix(ctx.csv, id);
        ctx.csv << ',' << id.rep << ',' << phase_name(static_cast<Phase>(p)) << ',' << ops[p] << ',' << ns[p] << ','
                << static_cast<double>(ns[p]) / static_cast<double>(ops[p]) << '\n';
    }
}

template <class KeyTag>
void run_type(Context& ctx, const Clock::time_point start) {
    using K = typename KeyTag::type;
    using Rbt = dict::RbtSet<K>;
    using Skip = dict::SkipSet<K>;
    const Options& o = ctx.opt;
    const bool want_rbt = selected(o.impls, Rbt::name);
    const bool want_skip = selected(o.impls, Skip::name);

    for (int e = o.min_exp; e <= o.max_exp; ++e) {
        const std::size_t n = std::size_t{1} << e;
        for (std::uint64_t si = 0; si < o.seeds; ++si) {
            const std::uint64_t seed = 1'000 + si;
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            std::fprintf(stderr, "[%7.1fs] %.*s n=2^%d seed=%llu\n", elapsed, int(KeyTag::name.size()),
                         KeyTag::name.data(), e, static_cast<unsigned long long>(seed));

            // Inputs, generated outside every timed region.
            const std::vector<K> keys = KeyTag::make(2 * n, seed);
            dict::Xoshiro256 rng(seed ^ 0x5EA5C4ULL);
            const auto shuffle = [&](std::vector<std::uint32_t>& v) {
                for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[rng.below(i)]);
            };
            std::vector<std::uint32_t> insert_order(n), search_order(n), erase_order(n);
            for (std::size_t i = 0; i < n; ++i) insert_order[i] = static_cast<std::uint32_t>(i);
            for (std::size_t i = 0; i < n; ++i)
                search_order[i] = static_cast<std::uint32_t>(i % 2 == 0 ? rng.below(n) : n + i / 2);
            shuffle(search_order);
            erase_order = insert_order;
            shuffle(erase_order);
            const std::uint64_t expected_hits = (n + 1) / 2;

            const Inputs<K> in{keys, insert_order, search_order, erase_order, expected_hits};

            const int total = o.warmup + o.reps + o.latency_reps;
            for (int r = 0; r < total; ++r) {
                const bool warm = r < o.warmup;
                const bool latency = r >= o.warmup + o.reps;
                const int rep = warm ? -1 : (latency ? r - o.warmup - o.reps : r - o.warmup);
                const RunId rbt_id{Rbt::name, KeyTag::name, n, e, seed, rep};
                const RunId skip_id{Skip::name, KeyTag::name, n, e, seed, rep};
                const auto run_rbt = [&] { if (want_rbt) run_once<Rbt>(ctx, in, rbt_id, latency, !warm); };
                const auto run_skip = [&] { if (want_skip) run_once<Skip>(ctx, in, skip_id, latency, !warm); };
                if (r % 2 == 0) { run_rbt(); run_skip(); }
                else { run_skip(); run_rbt(); }
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);
    for (const auto& t : opt.types)
        if (t != dict::keys::Int::name && t != dict::keys::Str10::name && t != dict::keys::Str30::name)
            usage(("unknown key type: " + t).c_str());
    for (const auto& i : opt.impls)
        if (i != "red_black_tree" && i != "skip_list") usage(("unknown impl: " + i).c_str());

    std::ofstream csv(opt.out), lat(opt.lat_out), hist(opt.hist_out);
    if (!csv) usage(("cannot open " + opt.out).c_str());
    if (!lat) usage(("cannot open " + opt.lat_out).c_str());
    if (!hist) usage(("cannot open " + opt.hist_out).c_str());
    csv << "impl,key_type,n,log2n,input_seed,rep,phase,ops,ns,ns_per_op\n";
    lat << "impl,key_type,n,log2n,input_seed,rep,phase,ops,tsc_ghz,overhead_ticks,"
           "mean_ns,p50_ns,p90_ns,p99_ns,p999_ns,p9999_ns,max_ns\n";
    hist << "impl,key_type,n,log2n,input_seed,rep,phase,bin_lo_ns,bin_hi_ns,count\n";

    const TscInfo info = calibrate_tsc();
    std::fprintf(stderr, "timestamp counter: %.3f GHz, timer overhead %llu ticks (subtracted)\n", info.ghz,
                 static_cast<unsigned long long>(info.overhead));
    Context ctx{opt, info, csv, lat, hist};

    const auto start = Clock::now();
    if (selected(opt.types, dict::keys::Int::name)) run_type<dict::keys::Int>(ctx, start);
    if (selected(opt.types, dict::keys::Str10::name)) run_type<dict::keys::Str10>(ctx, start);
    if (selected(opt.types, dict::keys::Str30::name)) run_type<dict::keys::Str30>(ctx, start);

    std::fprintf(stderr, "done in %.1fs; wrote %s, %s, %s\n",
                 std::chrono::duration<double>(Clock::now() - start).count(), opt.out.c_str(), opt.lat_out.c_str(),
                 opt.hist_out.c_str());
    return 0;
}
