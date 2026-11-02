// Allocation-only benchmark: how much of a write's cost could the allocator
// explain on its own?
//
// Mirrors the steady workload without any data structure. For each n it
// allocates n live blocks (the built structure), then times m = n/4 further
// allocations followed by freeing those m blocks in the same order, as
// insert_new and erase_new do. Every new block is written once, as a node
// constructor would.
//
// Block sizes come from a model of each structure's node:
//   fixed:B       every block is B bytes (red-black tree node)
//   tower:B:L     B + L*h bytes, h ~ Geometric(1/2) capped at 32 (skip list node
//                 with an h-level tower)
// The defaults, fixed:48 and tower:32:8, are the int-key node sizes worked out
// from the two headers. Check them against the alloc_size histogram of an
// instrumented run before relying on them.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dict/rng.hpp"

namespace {

struct Model {
    std::string name;
    std::size_t base = 0;
    std::size_t per_level = 0;  // 0 = fixed size
};

struct Options {
    std::vector<std::size_t> sizes{1'000, 10'000, 100'000, 1'000'000};
    std::vector<Model> models{{"fixed:48", 48, 0}, {"tower:32:8", 32, 8}};
    int reps = 5;
    std::uint64_t seed = 1;
    std::string out = "alloc.csv";
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, sep);)
        if (!item.empty()) parts.push_back(item);
    return parts;
}

[[noreturn]] void usage(const char* msg) {
    if (msg) std::fprintf(stderr, "error: %s\n\n", msg);
    std::fprintf(stderr,
                 "usage: dict_alloc_bench [options]\n"
                 "  --n LIST       comma list of live block counts (default 1000,10000,100000,1000000)\n"
                 "  --model LIST   comma list of fixed:B or tower:B:L (default fixed:48,tower:32:8)\n"
                 "  --reps R       repetitions per (model, n) (default 5)\n"
                 "  --seed S       RNG seed for tower heights (default 1)\n"
                 "  --out FILE     CSV output (default alloc.csv)\n");
    std::exit(msg ? 2 : 0);
}

Model parse_model(const std::string& s) {
    const auto f = split(s, ':');
    if (f.size() == 2 && f[0] == "fixed") return {s, std::stoull(f[1]), 0};
    if (f.size() == 3 && f[0] == "tower") return {s, std::stoull(f[1]), std::stoull(f[2])};
    usage(("bad model: " + s).c_str());
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
        else if (a == "--n") {
            o.sizes.clear();
            for (const auto& s : split(next(), ',')) o.sizes.push_back(std::stoull(s));
        } else if (a == "--model") {
            o.models.clear();
            for (const auto& s : split(next(), ',')) o.models.push_back(parse_model(s));
        } else if (a == "--reps") o.reps = std::stoi(next());
        else if (a == "--seed") o.seed = std::stoull(next());
        else if (a == "--out") o.out = next();
        else usage(("unknown flag: " + std::string(a)).c_str());
    }
    return o;
}

std::size_t block_size(const Model& m, dict::Xoshiro256& rng) {
    if (m.per_level == 0) return m.base;
    std::size_t h = 1;
    while (h < 32 && (rng.next() & 1)) ++h;
    return m.base + m.per_level * h;
}

void* alloc_and_write(std::size_t bytes) {
    void* p = ::operator new(bytes);
    std::memset(p, 0, bytes);
    return p;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);
    std::ofstream csv(opt.out);
    if (!csv) usage(("cannot open " + opt.out).c_str());
    csv << "model,n,rep,phase,ops,ns,ns_per_op,checksum\n";

    using clock = std::chrono::steady_clock;
    for (const Model& model : opt.models) {
        for (std::size_t n : opt.sizes) {
            const std::size_t m = std::max<std::size_t>(1, n / 4);
            for (int rep = 0; rep < opt.reps; ++rep) {
                std::fprintf(stderr, "%s n=%zu rep=%d\n", model.name.c_str(), n, rep);
                dict::Xoshiro256 rng(opt.seed + static_cast<std::uint64_t>(rep));
                std::vector<void*> live(n), fresh(m);
                std::vector<std::size_t> fresh_sizes(m);
                for (auto& p : live) p = alloc_and_write(block_size(model, rng));
                for (auto& s : fresh_sizes) s = block_size(model, rng);

                const auto t0 = clock::now();
                for (std::size_t i = 0; i < m; ++i) fresh[i] = alloc_and_write(fresh_sizes[i]);
                const auto t1 = clock::now();
                std::uint64_t checksum = 0;
                for (void* p : fresh) checksum += reinterpret_cast<std::uintptr_t>(p) & 0xFFF;
                for (std::size_t i = 0; i < m; ++i) ::operator delete(fresh[i]);
                const auto t2 = clock::now();

                auto row = [&](const char* phase, clock::duration d) {
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
                    csv << model.name << ',' << n << ',' << rep << ',' << phase << ',' << m << ',' << ns << ','
                        << static_cast<double>(ns) / static_cast<double>(m) << ',' << checksum << '\n';
                };
                row("alloc", t1 - t0);
                row("free", t2 - t1);
                for (void* p : live) ::operator delete(p);
            }
        }
    }
    std::fprintf(stderr, "wrote %s\n", opt.out.c_str());
    return 0;
}
