// Correctness tests, written once against dict::OrderedSet and run for every
// implementation. The oracle is std::set.
//
// Usage: dict_tests [std_set|red_black_tree|skip_list|all]
//
// An implementation with `implemented == false` is reported as SKIPPED.

#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "dict/ordered_set.hpp"
#include "dict/red_black_tree.hpp"
#include "dict/rng.hpp"
#include "dict/skip_list_adapter.hpp"
#include "dict/std_set_adapter.hpp"
#include "dict/workloads.hpp"

namespace {

using dict::Key;

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

template <class S>
std::vector<Key> contents(const S& s) {
    std::vector<Key> out;
    s.for_each([&](const Key& k) { out.push_back(k); });
    return out;
}

std::vector<Key> contents(const std::set<Key>& s) {
    return {s.begin(), s.end()};
}

template <class S>
S fresh() {
    return dict::make_set<S>(12345);
}

// ---- Tests -----------------------------------------------------------------

template <class S>
void test_empty() {
    S s = fresh<S>();
    CHECK(s.empty());
    CHECK(s.size() == 0);
    CHECK(!s.contains(42));
    CHECK(!s.erase(42));
    CHECK(contents(s).empty());
    CHECK(s.check_invariants());
}

template <class S>
void test_insert_duplicate() {
    S s = fresh<S>();
    CHECK(s.insert(5));
    CHECK(s.insert(3));
    CHECK(s.insert(8));
    CHECK(s.size() == 3);
    CHECK(s.contains(3));
    CHECK(!s.contains(4));
    CHECK(!s.insert(3));  // duplicate returns false and changes nothing
    CHECK(s.size() == 3);
    CHECK(s.contains(3));
    CHECK(contents(s) == std::vector<Key>({3, 5, 8}));
    CHECK(s.check_invariants());
}

template <class S>
void test_erase() {
    S s = fresh<S>();
    for (Key k = 0; k < 100; ++k) s.insert(k);
    for (Key k = 0; k < 100; k += 2) CHECK(s.erase(k));
    CHECK(!s.erase(0));
    CHECK(s.size() == 50);
    for (Key k = 0; k < 100; ++k) CHECK(s.contains(k) == (k % 2 == 1));
    CHECK(s.check_invariants());
    s.clear();
    CHECK(s.empty());
    CHECK(s.check_invariants());
}

// Random insert/contains/erase over a small key range (so duplicates and
// erase-of-absent are frequent), compared step by step against std::set.
template <class S>
void test_differential_random() {
    S s = fresh<S>();
    std::set<Key> oracle;
    dict::Xoshiro256 rng(2024);
    for (int i = 0; i < 50'000; ++i) {
        const Key k = rng.below(2'000);
        switch (rng.below(3)) {
            case 0:
                CHECK(s.insert(k) == oracle.insert(k).second);
                break;
            case 1:
                CHECK(s.contains(k) == oracle.contains(k));
                break;
            default:
                CHECK(s.erase(k) == (oracle.erase(k) == 1));
        }
        CHECK(s.size() == oracle.size());
        if (i % 1'000 == 0) CHECK(s.check_invariants());
    }
    CHECK(s.check_invariants());
    CHECK(contents(s) == contents(oracle));
}

// Replays every benchmark workload family at small n against the oracle,
// checking invariants at each phase boundary.
template <class S>
void test_workloads() {
    for (std::string_view family : dict::workload_families()) {
        const dict::Workload w = dict::make_workload(family, 3'000, 99);
        S s = fresh<S>();
        std::set<Key> oracle;
        for (const dict::Phase& phase : w.phases) {
            for (const dict::Op& op : phase.ops) {
                switch (op.kind) {
                    case dict::OpKind::Insert:
                        CHECK(s.insert(op.key) == oracle.insert(op.key).second);
                        break;
                    case dict::OpKind::Find:
                        CHECK(s.contains(op.key) == oracle.contains(op.key));
                        break;
                    case dict::OpKind::Erase:
                        CHECK(s.erase(op.key) == (oracle.erase(op.key) == 1));
                        break;
                }
            }
            CHECK(s.size() == oracle.size());
            CHECK(s.check_invariants());
        }
        CHECK(contents(s) == contents(oracle));
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

    std::set<Key> seen;
    for (const dict::Op& op : w1.phases[0].ops) CHECK(seen.insert(op.key).second);  // distinct keys
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

template <dict::OrderedSet S>
void run_suite(Totals& t) {
    std::printf("[%s]\n", S::name);
    if constexpr (!S::implemented) {
        ++t.skipped;
        std::printf("  SKIP  not implemented yet (set `implemented = true` when ready)\n");
    } else {
        run_one(t, S::name, "empty", test_empty<S>);
        run_one(t, S::name, "insert_duplicate", test_insert_duplicate<S>);
        run_one(t, S::name, "erase", test_erase<S>);
        run_one(t, S::name, "differential_random", test_differential_random<S>);
        run_one(t, S::name, "workloads", test_workloads<S>);
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
    if (which == "std_set" || which == "all") run_suite<dict::StdSetAdapter<Key>>(t);
    if (which == "red_black_tree" || which == "all") run_suite<dict::RedBlackTree<Key>>(t);
    if (which == "skip_list" || which == "all") run_suite<dict::SkipListAdapter<Key>>(t);

    std::printf("\n%d passed, %d failed, %d skipped\n", t.passed, t.failed, t.skipped);
    if (t.failed > 0) return 1;
    return (t.passed == 0 && t.skipped > 0) ? kSkipReturnCode : 0;
}
