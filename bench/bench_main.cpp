// Unified benchmark harness.
//
// Built twice from this one source file:
//   dict_bench               timing build: DICT_COUNT compiled out, wall-clock per phase
//   dict_bench_instrumented  counting build: structural counters per phase, plus
//                            per-operation cost histograms (the distributions)
//
// Timing and counting are never mixed in one binary, so the counters cannot
// distort the timings.
//
// Every implementation replays exactly the same generated workload. Workloads
// are generated before any timing starts.
//
// Seeds: --input-seeds varies the input (key sequence); --algo-seeds varies the
// randomized structure's internal RNG while the input stays fixed.
// Deterministic structures ignore the algorithm seed, so for them it acts as
// another repetition.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dict/ordered_map.hpp"
#include "dict/red_black_tree.hpp"
#include "dict/randomized_map.hpp"
#include "dict/std_map_adapter.hpp"
#include "dict/workloads.hpp"

namespace {

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
    std::vector<std::string> impls{"std_map", "red_black_tree", "randomized"};
    std::vector<std::string> workloads{"uniform"};
    std::vector<std::size_t> sizes{100'000};
    std::uint64_t input_seeds = 1;
    std::uint64_t algo_seeds = 1;
    int reps = 5;
    int warmup = 1;
    std::string out = "results.csv";
    std::string hist_out = "histograms.csv";  // instrumented build only
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
                 "  --impl LIST         comma list of std_map,red_black_tree,randomized (default all)\n"
                 "  --workload LIST     comma list of uniform,sorted,reverse,sliding_window,zipf, or 'all'\n"
                 "  --n LIST            comma list of sizes (default 100000)\n"
                 "  --input-seeds K     number of distinct input seeds (default 1)\n"
                 "  --algo-seeds S      number of algorithm seeds per input (default 1)\n"
                 "  --reps R            timed repetitions per (input, algo) seed pair (default 5)\n"
                 "  --warmup W          discarded warm-up repetitions (default 1)\n"
                 "  --out FILE          per-phase CSV (default results.csv)\n"
                 "  --hist-out FILE     per-op histogram CSV, instrumented build only (default histograms.csv)\n");
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
        else if (a == "--workload") {
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
        else usage(("unknown flag: " + std::string(a)).c_str());
    }
    return o;
}

// Per-operation cost histogram: metric value -> number of operations with that value.
using Histogram = std::map<std::uint64_t, std::uint64_t>;

struct PhaseHistograms {
    Histogram comparisons, node_visits, structural;
};

// Applies one operation. Returns a value folded into a checksum so the
// optimizer cannot discard lookups.
template <class M>
inline std::uint64_t apply(M& m, const dict::Op& op) {
    switch (op.kind) {
        case dict::OpKind::Insert: return m.insert(op.key, op.key);
        case dict::OpKind::Find: {
            const Value* v = m.find(op.key);
            return v ? *v : 0;
        }
        case dict::OpKind::Erase: return m.erase(op.key);
    }
    return 0;
}

struct Context {
    const Options& opt;
    std::ofstream& csv;
    std::ofstream* hist;  // null in the timing build
};

template <dict::OrderedMap M>
void run_impl(Context& ctx, const dict::Workload& w, std::uint64_t algo_seed) {
    const int total_reps = ctx.opt.warmup + ctx.opt.reps;
    for (int rep = 0; rep < total_reps; ++rep) {
        const bool warm = rep < ctx.opt.warmup;
        M m = dict::make_map<M>(algo_seed);

        for (const dict::Phase& phase : w.phases) {
            std::uint64_t checksum = 0;
            m.reset_counters();
            PhaseHistograms h;

            const auto t0 = std::chrono::steady_clock::now();
            if constexpr (kInstrumented) {
                for (const dict::Op& op : phase.ops) {
                    const dict::OpCounters before = m.counters();
                    checksum += apply(m, op);
                    const dict::OpCounters d = m.counters() - before;
                    ++h.comparisons[d.comparisons];
                    ++h.node_visits[d.node_visits];
                    ++h.structural[d.structural];
                }
            } else {
                for (const dict::Op& op : phase.ops) checksum += apply(m, op);
            }
            const auto t1 = std::chrono::steady_clock::now();

            if (warm) continue;
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            const dict::OpCounters& c = m.counters();
            ctx.csv << kBuild << ',' << M::name << ',' << w.family << ',' << w.n << ',' << w.input_seed << ','
                    << algo_seed << ',' << rep - ctx.opt.warmup << ',' << phase.name << ',' << phase.ops.size()
                    << ',' << ns << ',' << static_cast<double>(ns) / phase.ops.size() << ',' << c.comparisons
                    << ',' << c.node_visits << ',' << c.structural << ',' << c.recolors << ',' << c.allocations
                    << ',' << m.size() << ',' << checksum << '\n';

            // Structure (and hence the histogram) is identical across reps for a
            // given (input, algo) seed pair, so write it once.
            if (ctx.hist && rep == ctx.opt.warmup) {
                auto dump = [&](const char* metric, const Histogram& hist) {
                    for (const auto& [value, count] : hist)
                        *ctx.hist << M::name << ',' << w.family << ',' << w.n << ',' << w.input_seed << ','
                                  << algo_seed << ',' << phase.name << ',' << metric << ',' << value << ','
                                  << count << '\n';
                };
                dump("comparisons", h.comparisons);
                dump("node_visits", h.node_visits);
                dump("structural", h.structural);
            }
        }
    }
}

template <dict::OrderedMap M>
void run_if_selected(Context& ctx, const dict::Workload& w, std::uint64_t algo_seed) {
    if (std::find(ctx.opt.impls.begin(), ctx.opt.impls.end(), M::name) == ctx.opt.impls.end()) return;
    if constexpr (!M::implemented) {
        return;  // stub: skipped (warned once in main)
    } else {
        run_impl<M>(ctx, w, algo_seed);
    }
}

template <dict::OrderedMap M>
void warn_if_stub(const Options& o) {
    if constexpr (!M::implemented) {
        if (std::find(o.impls.begin(), o.impls.end(), M::name) != o.impls.end())
            std::fprintf(stderr, "note: %s is not implemented yet; skipping\n", M::name);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);

    using StdMap = dict::StdMapAdapter<Key, Value>;
    using Rbt = dict::RedBlackTree<Key, Value>;
    using Rand = dict::RandomizedMap<Key, Value>;
    warn_if_stub<StdMap>(opt);
    warn_if_stub<Rbt>(opt);
    warn_if_stub<Rand>(opt);

    std::ofstream csv(opt.out);
    if (!csv) usage(("cannot open " + opt.out).c_str());
    csv << "build,impl,workload,n,input_seed,algo_seed,rep,phase,ops,ns,ns_per_op,"
           "comparisons,node_visits,structural,recolors,allocations,final_size,checksum\n";

    std::ofstream hist;
    if (kInstrumented) {
        hist.open(opt.hist_out);
        if (!hist) usage(("cannot open " + opt.hist_out).c_str());
        hist << "impl,workload,n,input_seed,algo_seed,phase,metric,value,count\n";
    }
    Context ctx{opt, csv, kInstrumented ? &hist : nullptr};

    for (const std::string& family : opt.workloads) {
        for (std::size_t n : opt.sizes) {
            for (std::uint64_t is = 0; is < opt.input_seeds; ++is) {
                const std::uint64_t input_seed = 1'000 + is;
                const dict::Workload w = dict::make_workload(family, n, input_seed);
                for (std::uint64_t as = 0; as < opt.algo_seeds; ++as) {
                    const std::uint64_t algo_seed = 1'000'000 + as;
                    std::fprintf(stderr, "%s n=%zu input_seed=%llu algo_seed=%llu\n", family.c_str(), n,
                                 static_cast<unsigned long long>(input_seed),
                                 static_cast<unsigned long long>(algo_seed));
                    run_if_selected<StdMap>(ctx, w, algo_seed);
                    run_if_selected<Rbt>(ctx, w, algo_seed);
                    run_if_selected<Rand>(ctx, w, algo_seed);
                }
            }
        }
    }
    std::fprintf(stderr, "wrote %s%s%s\n", opt.out.c_str(), kInstrumented ? " and " : "",
                 kInstrumented ? opt.hist_out.c_str() : "");
    return 0;
}
