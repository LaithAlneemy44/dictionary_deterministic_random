// Correctness tests, written once against dict::OrderedMap and run for every
// implementation. The oracle is std::map.
//
// Usage: dict_tests [std_map|red_black_tree|skip_list|all]
//
// An implementation with `implemented == false` is reported as SKIPPED.

#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dict/ordered_map.hpp"
#include "dict/red_black_tree.hpp"
#include "dict/rng.hpp"
#include "dict/skip_list_adapter.hpp"
#include "dict/std_map_adapter.hpp"
#include "dict/workloads.hpp"

namespace {

using dict::Key;
using dict::Value;

// Returned when everything selected was skipped; CTest reports it as "Skipped" (see tests/CMakeLists.txt).
constexpr int kSkipReturnCode = 77;

struct TestFailure {
    std::string message;
};

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) throw TestFailure{std::string(__FILE__ ":") + std::to_string(__LINE__) + \
                                       ": CHECK(" #cond ") failed"};                        \
    } while (0)

template <class M>
std::vector<std::pair<Key, Value>> contents(const M& m) {
    std::vector<std::pair<Key, Value>> out;
    m.for_each([&](const Key& k, const Value& v) { out.emplace_back(k, v); });
    return out;
}

std::vector<std::pair<Key, Value>> contents(const std::map<Key, Value>& m) {
    return {m.begin(), m.end()};
}

template <class M>
M fresh() {
    return dict::make_map<M>(12345);
}

// ---- Tests -----------------------------------------------------------------

template <class M>
void test_empty() {
    M m = fresh<M>();
    CHECK(m.empty());
    CHECK(m.size() == 0);
    CHECK(m.find(42) == nullptr);
    CHECK(!m.contains(42));
    CHECK(!m.erase(42));
    CHECK(contents(m).empty());
    CHECK(m.check_invariants());
}

template <class M>
void test_insert_find_update() {
    M m = fresh<M>();
    CHECK(m.insert(5, 50));
    CHECK(m.insert(3, 30));
    CHECK(m.insert(8, 80));
    CHECK(m.size() == 3);
    CHECK(m.find(3) != nullptr && *m.find(3) == 30);
    CHECK(m.find(4) == nullptr);
    CHECK(!m.insert(3, 31));  // overwrite returns false
    CHECK(m.size() == 3);
    CHECK(*m.find(3) == 31);
    CHECK(m.check_invariants());
}

template <class M>
void test_erase() {
    M m = fresh<M>();
    for (Key k = 0; k < 100; ++k) m.insert(k, k * 10);
    for (Key k = 0; k < 100; k += 2) CHECK(m.erase(k));
    CHECK(!m.erase(0));
    CHECK(m.size() == 50);
    for (Key k = 0; k < 100; ++k) CHECK(m.contains(k) == (k % 2 == 1));
    CHECK(m.check_invariants());
    m.clear();
    CHECK(m.empty());
    CHECK(m.check_invariants());
}

// Random insert/find/erase over a small key range (so collisions and
// erase-of-absent are frequent), compared step by step against std::map.
template <class M>
void test_differential_random() {
    M m = fresh<M>();
    std::map<Key, Value> oracle;
    dict::Xoshiro256 rng(2024);
    for (int i = 0; i < 50'000; ++i) {
        const Key k = rng.below(2'000);
        switch (rng.below(3)) {
            case 0: {
                const Value v = rng.next();
                const bool fresh_key = oracle.insert_or_assign(k, v).second;
                CHECK(m.insert(k, v) == fresh_key);
                break;
            }
            case 1: {
                auto it = oracle.find(k);
                const Value* got = m.find(k);
                CHECK((got == nullptr) == (it == oracle.end()));
                if (got) CHECK(*got == it->second);
                break;
            }
            default:
                CHECK(m.erase(k) == (oracle.erase(k) == 1));
        }
        CHECK(m.size() == oracle.size());
        if (i % 1'000 == 0) CHECK(m.check_invariants());
    }
    CHECK(m.check_invariants());
    CHECK(contents(m) == contents(oracle));
}

template <class M>
void test_range_queries() {
    M m = fresh<M>();
    std::map<Key, Value> oracle;
    dict::Xoshiro256 rng(7);
    for (int i = 0; i < 5'000; ++i) {
        const Key k = rng.below(100'000);
        m.insert(k, k);
        oracle.insert_or_assign(k, k);
    }
    for (int q = 0; q < 500; ++q) {
        Key lo = rng.below(100'000), hi = rng.below(100'000);
        if (lo > hi) std::swap(lo, hi);
        std::vector<std::pair<Key, Value>> got, want(oracle.lower_bound(lo), oracle.upper_bound(hi));
        m.for_each_in_range(lo, hi, [&](const Key& k, const Value& v) { got.emplace_back(k, v); });
        CHECK(got == want);
    }
}

// Replays every benchmark workload family at small n against the oracle,
// checking invariants at each phase boundary.
template <class M>
void test_workloads() {
    for (std::string_view family : dict::workload_families()) {
        const dict::Workload w = dict::make_workload(family, 3'000, 99);
        M m = fresh<M>();
        std::map<Key, Value> oracle;
        for (const dict::Phase& phase : w.phases) {
            for (const dict::Op& op : phase.ops) {
                switch (op.kind) {
                    case dict::OpKind::Insert:
                        CHECK(m.insert(op.key, op.key) == oracle.insert_or_assign(op.key, op.key).second);
                        break;
                    case dict::OpKind::Find:
                        CHECK(m.contains(op.key) == oracle.contains(op.key));
                        break;
                    case dict::OpKind::Erase:
                        CHECK(m.erase(op.key) == (oracle.erase(op.key) == 1));
                        break;
                }
            }
            CHECK(m.size() == oracle.size());
            CHECK(m.check_invariants());
        }
        CHECK(contents(m) == contents(oracle));
    }
}

// Sanity checks on the shared infrastructure itself (implementation-independent).
void test_infrastructure() {
    dict::Xoshiro256 a(1), b(1), c(2);
    CHECK(a.next() == b.next());
    CHECK(a.next() != c.next());
    for (int i = 0; i < 1'000; ++i) CHECK(a.below(10) < 10);

    const dict::Workload w1 = dict::make_workload("uniform", 1'000, 5);
    const dict::Workload w2 = dict::make_workload("uniform", 1'000, 5);
    CHECK(w1.phases.size() == w2.phases.size());
    for (std::size_t p = 0; p < w1.phases.size(); ++p)
        for (std::size_t i = 0; i < w1.phases[p].ops.size(); ++i)
            CHECK(w1.phases[p].ops[i].key == w2.phases[p].ops[i].key);

    std::map<Key, int> seen;
    for (const dict::Op& op : w1.phases[0].ops) CHECK(++seen[op.key] == 1);  // distinct keys
}

// ---- Runner ----------------------------------------------------------------

struct Totals {
    int passed = 0, failed = 0, skipped = 0;
};

void run_one(Totals& t, std::string_view suite, std::string_view test, const std::function<void()>& fn) {
    try {
        fn();
        ++t.passed;
        std::printf("  PASS  %.*s.%.*s\n", int(suite.size()), suite.data(), int(test.size()), test.data());
    } catch (const TestFailure& f) {
        ++t.failed;
        std::printf("  FAIL  %.*s.%.*s\n        %s\n", int(suite.size()), suite.data(), int(test.size()),
                    test.data(), f.message.c_str());
    } catch (const std::exception& e) {
        ++t.failed;
        std::printf("  FAIL  %.*s.%.*s\n        exception: %s\n", int(suite.size()), suite.data(),
                    int(test.size()), test.data(), e.what());
    }
}

template <dict::OrderedMap M>
void run_suite(Totals& t) {
    std::printf("[%s]\n", M::name);
    if constexpr (!M::implemented) {
        ++t.skipped;
        std::printf("  SKIP  not implemented yet (set `implemented = true` when ready)\n");
    } else {
        run_one(t, M::name, "empty", test_empty<M>);
        run_one(t, M::name, "insert_find_update", test_insert_find_update<M>);
        run_one(t, M::name, "erase", test_erase<M>);
        run_one(t, M::name, "differential_random", test_differential_random<M>);
        run_one(t, M::name, "range_queries", test_range_queries<M>);
        run_one(t, M::name, "workloads", test_workloads<M>);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view which = argc > 1 ? argv[1] : "all";
    Totals t;

    if (which == "infrastructure" || which == "all") {
        std::printf("[infrastructure]\n");
        run_one(t, "infrastructure", "rng_and_workloads", test_infrastructure);
    }
    if (which == "std_map" || which == "all") run_suite<dict::StdMapAdapter<Key, Value>>(t);
    if (which == "red_black_tree" || which == "all") run_suite<dict::RedBlackTree<Key, Value>>(t);
    if (which == "skip_list" || which == "all") run_suite<dict::SkipListAdapter<Key, Value>>(t);

    std::printf("\n%d passed, %d failed, %d skipped\n", t.passed, t.failed, t.skipped);
    if (t.failed > 0) return 1;
    return (t.passed == 0 && t.skipped > 0) ? kSkipReturnCode : 0;
}
