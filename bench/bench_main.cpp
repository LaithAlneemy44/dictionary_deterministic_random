// Unified benchmark harness.
//
// Built twice from this one source file:
//   dict_bench               timing build: DICT_COUNT compiled out, wall-clock per
//                            phase, and optionally per-operation latency
//                            distributions (--latency-out)
//   dict_bench_instrumented  counting build: structural counters per phase,
//                            per-operation cost histograms (the distributions),
//                            and heap bytes held by the structure (memory)
//
// Timing and counting are never mixed in one binary, so the counters and the
// heap tracking cannot distort the timings.
//
// Every implementation replays exactly the same generated workload. Workloads
// are generated, and converted to the chosen key type, before any timing
// starts.
//
// Seeds: --input-seeds varies the input (key sequence); --algo-seeds varies the
// randomized structure's internal RNG while the input stays fixed.
// Deterministic structures ignore the algorithm seed, so for them it acts as
// another repetition.
//
// Key types (--key): int, small_string (fits the small string buffer, no heap
// allocation) and large_string (always heap allocated). See dict/keys.hpp.
// The instrumented build also accepts probe, a key that orders like int and
// records the distinct stored keys each operation reads (dict/probe_key.hpp),
// written as the distinct_keys histogram.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#define DICT_HAS_TSC 1
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define DICT_HAS_TSC 1
#endif

#include "dict/keys.hpp"
#include "dict/ordered_map.hpp"
#include "dict/probe_key.hpp"
#include "dict/red_black_tree.hpp"
#include "dict/skip_list_adapter.hpp"
#include "dict/std_map_adapter.hpp"
#include "dict/workloads.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

// ---- Heap usage (instrumented build, --mem-out) -----------------------------
//
// Walks every process heap and sums the blocks in use. This sees every
// allocation, including those made inside the C++ runtime DLL (e.g. std::string
// buffers), plus the allocator's own per-block overhead. A structure's memory
// is the difference between a snapshot taken before it is built and one taken
// after its insert phase. Windows only; elsewhere the columns are left empty.
struct HeapUsage {
    std::int64_t bytes = 0;     // bytes requested by the program
    std::int64_t overhead = 0;  // allocator bookkeeping for those blocks
    std::int64_t blocks = 0;
    bool valid = false;
};

HeapUsage heap_usage() {
    HeapUsage u;
#ifdef _WIN32
    std::vector<HANDLE> heaps(GetProcessHeaps(0, nullptr) + 16);
    heaps.resize(GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data()));
    for (HANDLE h : heaps) {
        if (!HeapLock(h)) continue;
        PROCESS_HEAP_ENTRY e{};
        while (HeapWalk(h, &e)) {
            if (e.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
                u.bytes += static_cast<std::int64_t>(e.cbData);
                u.overhead += static_cast<std::int64_t>(e.cbOverhead);
                ++u.blocks;
            }
        }
        HeapUnlock(h);
    }
    u.valid = true;
#endif
    return u;
}

using dict::Key;
using dict::Value;

#ifdef DICT_INSTRUMENT
constexpr bool kInstrumented = true;
constexpr const char* kBuild = "instrumented";
#else
constexpr bool kInstrumented = false;
constexpr const char* kBuild = "timing";
#endif

struct Options {
    std::vector<std::string> impls{"std_map", "red_black_tree", "skip_list"};
    std::vector<std::string> keys{"int"};
    std::vector<std::string> workloads{"uniform"};
    std::vector<std::size_t> sizes{100'000};
    std::uint64_t input_seeds = 1;
    std::uint64_t algo_seeds = 1;
    int reps = 5;
    int warmup = 1;
    std::string out = "results.csv";
    std::string hist_out = "histograms.csv";  // instrumented build only
    std::string latency_out;                  // timing build only; empty = off
    std::string latency_hist_out;             // timing build only; empty = off
    std::string mem_out;                      // instrumented build only; empty = off
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
                 "usage: dict_bench [options]\n"
                 "  --impl LIST          comma list of std_map,red_black_tree,skip_list (default all)\n"
                 "  --key LIST           comma list of int,small_string,large_string, or 'all' (default int);\n"
                 "                       the instrumented build also accepts probe (records distinct keys read)\n"
                 "  --workload LIST      comma list of uniform,sorted,reverse,sliding_window,zipf,zipf_shuffled,\n"
                 "                       steady, or 'all'\n"
                 "  --n LIST             comma list of sizes (default 100000)\n"
                 "  --input-seeds K      number of distinct input seeds (default 1)\n"
                 "  --algo-seeds S       number of algorithm seeds per input (default 1)\n"
                 "  --reps R             timed repetitions per (input, algo) seed pair (default 5)\n"
                 "  --warmup W           discarded warm-up repetitions (default 1)\n"
                 "  --out FILE           per-phase CSV (default results.csv)\n"
                 "  --hist-out FILE      per-op cost histogram CSV, instrumented build only (default histograms.csv)\n"
                 "  --latency-out FILE   timing build: also time every operation, write percentiles to FILE\n"
                 "  --latency-hist-out FILE  timing build: per-op latency histogram (log bins) for CCDF plots\n"
                 "  --mem-out FILE       instrumented build: heap held by each structure after its insert phase\n");
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
        else if (a == "--key") {
            const std::string k = next();
            if (k == "all") o.keys.assign(dict::key_kind_names().begin(), dict::key_kind_names().end());
            else o.keys = split(k);
        } else if (a == "--workload") {
            const std::string w = next();
            if (w == "all") o.workloads.assign(dict::workload_families().begin(), dict::workload_families().end());
            else o.workloads = split(w);
        } else if (a == "--n") {
            o.sizes.clear();
            for (const auto& s : split(next())) o.sizes.push_back(std::stoull(s));
        } else if (a == "--input-seeds") o.input_seeds = std::stoull(next());
        else if (a == "--algo-seeds") o.algo_seeds = std::stoull(next());
        else if (a == "--reps") o.reps = std::stoi(next());
        else if (a == "--warmup") o.warmup = std::stoi(next());
        else if (a == "--out") o.out = next();
        else if (a == "--hist-out") o.hist_out = next();
        else if (a == "--latency-out") o.latency_out = next();
        else if (a == "--latency-hist-out") o.latency_hist_out = next();
        else if (a == "--mem-out") o.mem_out = next();
        else usage(("unknown flag: " + std::string(a)).c_str());
    }
    for (const auto& k : o.keys) {
        if (k == "probe") {
            if (!kInstrumented) usage("--key probe needs the instrumented build (dict_bench_instrumented)");
        } else if (std::find(dict::key_kind_names().begin(), dict::key_kind_names().end(), k) ==
                   dict::key_kind_names().end()) {
            usage(("unknown key type: " + k).c_str());
        }
    }
    return o;
}

// ---- Workloads converted to a key type --------------------------------------

template <class K>
struct TypedOp {
    dict::OpKind kind;
    K key;
    Value value;  // the original 64-bit key, used as the inserted value
};

template <class K>
struct TypedPhase {
    std::string name;
    std::vector<TypedOp<K>> ops;
};

template <class K>
K convert_key(Key k, dict::KeyKind kind) {
    if constexpr (std::is_same_v<K, std::string>) {
        return kind == dict::KeyKind::LargeString ? dict::large_string_key(k) : dict::small_string_key(k);
    } else if constexpr (std::is_same_v<K, dict::ProbeKey>) {
        (void)kind;
        return dict::ProbeKey(k);
    } else {
        (void)kind;
        return k;
    }
}

template <class K>
std::vector<TypedPhase<K>> convert(const dict::Workload& w, dict::KeyKind kind) {
    std::vector<TypedPhase<K>> phases;
    for (const dict::Phase& p : w.phases) {
        TypedPhase<K> tp{p.name, {}};
        tp.ops.reserve(p.ops.size());
        for (const dict::Op& op : p.ops) tp.ops.push_back({op.kind, convert_key<K>(op.key, kind), op.key});
        phases.push_back(std::move(tp));
    }
    return phases;
}

// Applies one operation. Returns a value folded into a checksum so the
// optimizer cannot discard lookups.
template <class M, class K>
inline std::uint64_t apply(M& m, const TypedOp<K>& op) {
    switch (op.kind) {
        case dict::OpKind::Insert: return m.insert(op.key, op.value);
        case dict::OpKind::Find: {
            const Value* v = m.find(op.key);
            return v ? *v : 0;
        }
        case dict::OpKind::Erase: return m.erase(op.key);
    }
    return 0;
}

// ---- Per-operation timing (timing build, --latency-out) ---------------------

#ifdef DICT_HAS_TSC
// lfence keeps the timed operation from being reordered across the reads.
inline std::uint64_t tick_start() {
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}
inline std::uint64_t tick_stop() {
    unsigned aux;
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}
#else
inline std::uint64_t tick_start() {
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}
inline std::uint64_t tick_stop() { return tick_start(); }
#endif

struct TickScale {
    double ns_per_tick = 1.0;
    double overhead_ticks = 0.0;  // median cost of an empty start/stop pair
};

// Measures ticks per nanosecond against steady_clock over ~200 ms, and the
// median overhead of an empty timed region, which is subtracted from every
// operation.
TickScale calibrate_ticks() {
    TickScale s;
    const auto c0 = std::chrono::steady_clock::now();
    const std::uint64_t t0 = tick_start();
    while (std::chrono::steady_clock::now() - c0 < std::chrono::milliseconds(200)) {
    }
    const std::uint64_t t1 = tick_stop();
    const auto c1 = std::chrono::steady_clock::now();
    const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(c1 - c0).count());
    s.ns_per_tick = ns / static_cast<double>(t1 - t0);

    std::vector<std::uint64_t> empty(10'001);
    for (auto& e : empty) {
        const std::uint64_t a = tick_start();
        const std::uint64_t b = tick_stop();
        e = b - a;
    }
    std::nth_element(empty.begin(), empty.begin() + empty.size() / 2, empty.end());
    s.overhead_ticks = static_cast<double>(empty[empty.size() / 2]);
    return s;
}

// Per-operation cost histogram: metric value -> number of operations with that value.
using Histogram = std::map<std::uint64_t, std::uint64_t>;

struct PhaseHistograms {
    Histogram comparisons, node_visits, structural, distinct_keys;  // distinct_keys: probe keys only
};

struct Context {
    const Options& opt;
    std::ofstream& csv;
    std::ofstream* hist;          // null in the timing build
    std::ofstream* latency;       // null unless --latency-out
    std::ofstream* latency_hist;  // null unless --latency-hist-out
    std::ofstream* mem;           // null unless --mem-out
    TickScale ticks;
};

struct RunInfo {
    const char* key;
    const dict::Workload& w;
    std::uint64_t algo_seed;
};

template <class M>
void write_row_prefix(std::ostream& out, const RunInfo& r, const std::string& phase) {
    out << M::name << ',' << r.key << ',' << r.w.family << ',' << r.w.n << ',' << r.w.input_seed << ','
        << r.algo_seed << ',' << phase;
}

template <dict::OrderedMap M>
void run_impl(Context& ctx, const RunInfo& r, const std::vector<TypedPhase<typename M::key_type>>& phases) {
    const int total_reps = ctx.opt.warmup + ctx.opt.reps;
    for (int rep = 0; rep < total_reps; ++rep) {
        const bool warm = rep < ctx.opt.warmup;
        M m = dict::make_map<M>(r.algo_seed);

        for (const auto& phase : phases) {
            std::uint64_t checksum = 0;
            m.reset_counters();
            PhaseHistograms h;

            const auto t0 = std::chrono::steady_clock::now();
            if constexpr (kInstrumented) {
                constexpr bool kProbe = std::is_same_v<typename M::key_type, dict::ProbeKey>;
                for (const auto& op : phase.ops) {
                    const dict::OpCounters before = m.counters();
                    if constexpr (kProbe) dict::probe::begin(&op.key);
                    checksum += apply(m, op);
                    const dict::OpCounters d = m.counters() - before;
                    ++h.comparisons[d.comparisons];
                    ++h.node_visits[d.node_visits];
                    ++h.structural[d.structural];
                    if constexpr (kProbe) ++h.distinct_keys[dict::probe::read.size()];
                }
            } else {
                for (const auto& op : phase.ops) checksum += apply(m, op);
            }
            const auto t1 = std::chrono::steady_clock::now();

            if (warm) continue;
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            const dict::OpCounters& c = m.counters();
            ctx.csv << kBuild << ',';
            write_row_prefix<M>(ctx.csv, r, phase.name);
            ctx.csv << ',' << rep - ctx.opt.warmup << ',' << phase.ops.size() << ',' << ns << ','
                    << static_cast<double>(ns) / static_cast<double>(phase.ops.size()) << ',' << c.comparisons << ','
                    << c.node_visits << ',' << c.structural << ',' << c.recolors << ',' << c.allocations << ','
                    << m.size() << ',' << checksum << '\n';

            // Structure (and hence the histogram) is identical across reps for a
            // given (input, algo) seed pair, so write it once.
            if (ctx.hist && rep == ctx.opt.warmup) {
                auto dump = [&](const char* metric, const Histogram& hist) {
                    for (const auto& [value, count] : hist) {
                        write_row_prefix<M>(*ctx.hist, r, phase.name);
                        *ctx.hist << ',' << metric << ',' << value << ',' << count << '\n';
                    }
                };
                dump("comparisons", h.comparisons);
                dump("node_visits", h.node_visits);
                dump("structural", h.structural);
                dump("distinct_keys", h.distinct_keys);
            }
        }
    }
}

// Builds the structure with the workload's first phase (insert n keys) and
// records the heap it holds afterwards. Nothing else is allocated between the
// two snapshots, so the difference is the structure's own memory.
template <dict::OrderedMap M>
void run_memory(Context& ctx, const RunInfo& r, const std::vector<TypedPhase<typename M::key_type>>& phases) {
    const HeapUsage before = heap_usage();
    std::uint64_t checksum = 0;
    {
        M m = dict::make_map<M>(r.algo_seed);
        for (const auto& op : phases.front().ops) checksum += apply(m, op);
        const HeapUsage after = heap_usage();
        if (!after.valid) return;
        write_row_prefix<M>(*ctx.mem, r, phases.front().name);
        *ctx.mem << ',' << m.size() << ',' << after.bytes - before.bytes << ',' << after.overhead - before.overhead
                 << ',' << after.blocks - before.blocks << ',' << checksum << '\n';
    }
}

// Log-scale latency bin: 4 bins per doubling. Returns the bin's lower bound in ns.
inline double latency_bin_floor(double ns) {
    if (ns < 1.0) return 0.0;
    const double b = std::floor(4.0 * std::log2(ns));
    return std::exp2(b / 4.0);
}

// One extra run that times every operation individually. The timer reads add
// a little cost to each operation, so this is kept separate from the phase
// timings above.
template <dict::OrderedMap M>
void run_latency(Context& ctx, const RunInfo& r, const std::vector<TypedPhase<typename M::key_type>>& phases) {
    M m = dict::make_map<M>(r.algo_seed);
    std::vector<std::uint64_t> ticks;
    for (const auto& phase : phases) {
        ticks.assign(phase.ops.size(), 0);
        std::uint64_t checksum = 0;
        for (std::size_t i = 0; i < phase.ops.size(); ++i) {
            const std::uint64_t a = tick_start();
            checksum += apply(m, phase.ops[i]);
            const std::uint64_t b = tick_stop();
            ticks[i] = b - a;
        }

        std::vector<double> ns(ticks.size());
        double sum = 0;
        for (std::size_t i = 0; i < ticks.size(); ++i) {
            const double t = static_cast<double>(ticks[i]) - ctx.ticks.overhead_ticks;
            ns[i] = std::max(0.0, t) * ctx.ticks.ns_per_tick;
            sum += ns[i];
        }
        std::sort(ns.begin(), ns.end());
        auto pct = [&](double q) {
            const auto idx = static_cast<std::size_t>(std::ceil(q * static_cast<double>(ns.size()))) - 1;
            return ns[std::min(idx, ns.size() - 1)];
        };

        if (ctx.latency) {
            write_row_prefix<M>(*ctx.latency, r, phase.name);
            *ctx.latency << ',' << ns.size() << ',' << sum / static_cast<double>(ns.size()) << ',' << pct(0.5)
                         << ',' << pct(0.9) << ',' << pct(0.99) << ',' << pct(0.999) << ',' << pct(0.9999) << ','
                         << ns.back() << ',' << ctx.ticks.overhead_ticks * ctx.ticks.ns_per_tick << ',' << checksum
                         << '\n';
        }
        if (ctx.latency_hist) {
            std::map<double, std::uint64_t> bins;
            for (double v : ns) ++bins[latency_bin_floor(v)];
            for (const auto& [bin, count] : bins) {
                write_row_prefix<M>(*ctx.latency_hist, r, phase.name);
                *ctx.latency_hist << ',' << bin << ',' << count << '\n';
            }
        }
    }
}

template <dict::OrderedMap M>
void run_if_selected(Context& ctx, const RunInfo& r, const std::vector<TypedPhase<typename M::key_type>>& phases) {
    if (std::find(ctx.opt.impls.begin(), ctx.opt.impls.end(), M::name) == ctx.opt.impls.end()) return;
    if constexpr (!M::implemented) {
        return;  // stub: skipped (warned once in main)
    } else {
        run_impl<M>(ctx, r, phases);
        if (ctx.latency || ctx.latency_hist) run_latency<M>(ctx, r, phases);
        if (ctx.mem) run_memory<M>(ctx, r, phases);
    }
}

template <dict::OrderedMap M>
void warn_if_stub(const Options& o) {
    if constexpr (!M::implemented) {
        if (std::find(o.impls.begin(), o.impls.end(), M::name) != o.impls.end())
            std::fprintf(stderr, "note: %s is not implemented yet; skipping\n", M::name);
    }
}

// Converts the workload to key type K once, then runs every selected
// implementation for every algorithm seed.
template <class K>
void run_key(Context& ctx, const dict::Workload& w, dict::KeyKind kind, const char* key_name) {
    const auto phases = convert<K>(w, kind);
    for (std::uint64_t as = 0; as < ctx.opt.algo_seeds; ++as) {
        const RunInfo r{key_name, w, 1'000'000 + as};
        std::fprintf(stderr, "%s %s n=%zu input_seed=%llu algo_seed=%llu\n", r.key, w.family.c_str(), w.n,
                     static_cast<unsigned long long>(w.input_seed), static_cast<unsigned long long>(r.algo_seed));
        run_if_selected<dict::StdMapAdapter<K, Value>>(ctx, r, phases);
        run_if_selected<dict::RedBlackTree<K, Value>>(ctx, r, phases);
        run_if_selected<dict::SkipListAdapter<K, Value>>(ctx, r, phases);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);

    warn_if_stub<dict::StdMapAdapter<Key, Value>>(opt);
    warn_if_stub<dict::RedBlackTree<Key, Value>>(opt);
    warn_if_stub<dict::SkipListAdapter<Key, Value>>(opt);

    std::ofstream csv(opt.out);
    if (!csv) usage(("cannot open " + opt.out).c_str());
    csv << "build,impl,key,workload,n,input_seed,algo_seed,phase,rep,ops,ns,ns_per_op,"
           "comparisons,node_visits,structural,recolors,allocations,final_size,checksum\n";

    std::ofstream hist;
    if (kInstrumented) {
        hist.open(opt.hist_out);
        if (!hist) usage(("cannot open " + opt.hist_out).c_str());
        hist << "impl,key,workload,n,input_seed,algo_seed,phase,metric,value,count\n";
    }

    std::ofstream latency, latency_hist;
    if (!kInstrumented && !opt.latency_out.empty()) {
        latency.open(opt.latency_out);
        if (!latency) usage(("cannot open " + opt.latency_out).c_str());
        latency << "impl,key,workload,n,input_seed,algo_seed,phase,ops,mean_ns,p50_ns,p90_ns,p99_ns,p999_ns,"
                   "p9999_ns,max_ns,timer_overhead_ns,checksum\n";
    }
    if (!kInstrumented && !opt.latency_hist_out.empty()) {
        latency_hist.open(opt.latency_hist_out);
        if (!latency_hist) usage(("cannot open " + opt.latency_hist_out).c_str());
        latency_hist << "impl,key,workload,n,input_seed,algo_seed,phase,bin_ns,count\n";
    }

    std::ofstream mem;
    if (kInstrumented && !opt.mem_out.empty()) {
        mem.open(opt.mem_out);
        if (!mem) usage(("cannot open " + opt.mem_out).c_str());
        mem << "impl,key,workload,n,input_seed,algo_seed,phase,size,heap_bytes,heap_overhead,heap_blocks,checksum\n";
    }

    Context ctx{opt,
                csv,
                kInstrumented ? &hist : nullptr,
                latency.is_open() ? &latency : nullptr,
                latency_hist.is_open() ? &latency_hist : nullptr,
                mem.is_open() ? &mem : nullptr,
                {}};
    if (ctx.latency || ctx.latency_hist) {
        ctx.ticks = calibrate_ticks();
        std::fprintf(stderr, "timer: %.4f ns/tick, overhead %.1f ns\n", ctx.ticks.ns_per_tick,
                     ctx.ticks.overhead_ticks * ctx.ticks.ns_per_tick);
    }

    for (const std::string& key : opt.keys) {
        const dict::KeyKind kind = key == "int"            ? dict::KeyKind::Int
                                   : key == "small_string" ? dict::KeyKind::SmallString
                                                           : dict::KeyKind::LargeString;
        for (const std::string& family : opt.workloads) {
            for (std::size_t n : opt.sizes) {
                for (std::uint64_t is = 0; is < opt.input_seeds; ++is) {
                    const dict::Workload w = dict::make_workload(family, n, 1'000 + is);
                    if (key == "probe") run_key<dict::ProbeKey>(ctx, w, kind, "probe");
                    else if (kind == dict::KeyKind::Int) run_key<Key>(ctx, w, kind, dict::key_kind_name(kind));
                    else run_key<std::string>(ctx, w, kind, dict::key_kind_name(kind));
                }
            }
        }
    }
    std::fprintf(stderr, "wrote %s\n", opt.out.c_str());
    return 0;
}
