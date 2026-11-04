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
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "dict/keys.hpp"
#include "dict/ordered_map.hpp"
#include "dict/probe_key.hpp"
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

// True if evaluating f() throws std::out_of_range.
template <class F>
bool throws_out_of_range(F&& f) {
    try {
        f();
    } catch (const std::out_of_range&) {
        return true;
    }
    return false;
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

// operator[] reads and writes existing values, throws for missing keys, and
// never inserts.
template <class M>
void test_subscript() {
    M m = fresh<M>();
    m.insert(1, 10);
    m.insert(2, 20);
    CHECK(m[1] == 10);
    m[2] = 25;
    CHECK(*m.find(2) == 25);

    const M& cm = m;
    CHECK(cm[2] == 25);

    CHECK(throws_out_of_range([&] { (void)m[3]; }));
    CHECK(throws_out_of_range([&] { (void)cm[3]; }));
    CHECK(m.size() == 2);
    CHECK(!m.contains(3));
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

// Replays every workload family with string keys (as the benchmark converts
// them) against a std::map oracle. M must have std::string keys.
template <class M>
void test_workloads_strings(dict::KeyKind kind) {
    auto to_key = [&](Key k) {
        return kind == dict::KeyKind::LargeString ? dict::large_string_key(k) : dict::small_string_key(k);
    };
    for (std::string_view family : dict::workload_families()) {
        const dict::Workload w = dict::make_workload(family, 2'000, 77);
        M m = fresh<M>();
        std::map<std::string, Value> oracle;
        for (const dict::Phase& phase : w.phases) {
            for (const dict::Op& op : phase.ops) {
                const std::string k = to_key(op.key);
                switch (op.kind) {
                    case dict::OpKind::Insert:
                        CHECK(m.insert(k, op.key) == oracle.insert_or_assign(k, op.key).second);
                        break;
                    case dict::OpKind::Find: {
                        const Value* got = m.find(k);
                        auto it = oracle.find(k);
                        CHECK((got == nullptr) == (it == oracle.end()));
                        if (got) CHECK(*got == it->second);
                        break;
                    }
                    case dict::OpKind::Erase:
                        CHECK(m.erase(k) == (oracle.erase(k) == 1));
                        break;
                }
            }
            CHECK(m.size() == oracle.size());
            CHECK(m.check_invariants());
        }
    }
}

// The string encodings must preserve integer order and have the stated lengths.
void test_key_encodings() {
    dict::Xoshiro256 rng(3);
    for (int i = 0; i < 10'000; ++i) {
        const Key a = rng.next(), b = rng.next();
        CHECK((a < b) == (dict::small_string_key(a) < dict::small_string_key(b)));
        CHECK((a < b) == (dict::large_string_key(a) < dict::large_string_key(b)));
    }
    CHECK(dict::small_string_key(0) < dict::small_string_key(1));
    CHECK(dict::small_string_key(~Key{0}).size() == dict::kSmallStringLength);
    CHECK(dict::large_string_key(12345).size() == dict::kLargeStringLength);
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

// The workload families built to test specific explanations do what they claim.
void test_workload_shapes() {
    const std::size_t n = 4'000;

    // sorted / reverse: exactly half the lookups hit, and misses fall inside the key range.
    for (std::string_view family : {"sorted", "reverse"}) {
        const dict::Workload w = dict::make_workload(family, n, 7);
        std::set<Key> present;
        for (const dict::Op& op : w.phases[0].ops) present.insert(op.key);
        std::size_t hits = 0;
        for (const dict::Op& op : w.phases[1].ops) {
            CHECK(op.key < 2 * n);
            hits += present.contains(op.key);
        }
        CHECK(hits == n / 2);
    }

    // zipf: hot keys are the oldest, so looked-up keys sit near the start of the
    // insertion order. zipf_shuffled: popularity is unrelated to it.
    auto mean_insert_position = [&](std::string_view family) {
        const dict::Workload w = dict::make_workload(family, n, 7);
        std::map<Key, std::size_t> position;
        for (std::size_t i = 0; i < n; ++i) position[w.phases[0].ops[i].key] = i;
        double sum = 0;
        for (const dict::Op& op : w.phases[1].ops) sum += static_cast<double>(position.at(op.key));
        return sum / static_cast<double>(w.phases[1].ops.size() * n);
    };
    CHECK(mean_insert_position("zipf") < 0.2);
    const double shuffled = mean_insert_position("zipf_shuffled");
    CHECK(shuffled > 0.35 && shuffled < 0.65);

    // steady: n/4 fresh keys, absent until inserted, replayed in the same order by every phase.
    const dict::Workload s = dict::make_workload("steady", n, 7);
    const std::vector<std::pair<std::string_view, dict::OpKind>> expected = {
        {"insert", dict::OpKind::Insert}, {"find_miss", dict::OpKind::Find}, {"insert_new", dict::OpKind::Insert},
        {"find_new", dict::OpKind::Find}, {"erase_new", dict::OpKind::Erase}};
    CHECK(s.phases.size() == expected.size());
    for (std::size_t p = 0; p < expected.size(); ++p) {
        CHECK(s.phases[p].name == expected[p].first);
        for (const dict::Op& op : s.phases[p].ops) CHECK(op.kind == expected[p].second);
    }
    std::set<Key> built;
    for (const dict::Op& op : s.phases[0].ops) built.insert(op.key);
    CHECK(s.phases[1].ops.size() == n / 4);
    for (std::size_t i = 0; i < s.phases[1].ops.size(); ++i) {
        CHECK(!built.contains(s.phases[1].ops[i].key));
        for (std::size_t p = 2; p < s.phases.size(); ++p) CHECK(s.phases[p].ops[i].key == s.phases[1].ops[i].key);
    }
}

// Cache lines and pages are counted from byte ranges, including ranges that
// straddle a boundary.
void test_probe_touch() {
    dict::probe::begin(nullptr);
    alignas(4096) static unsigned char buf[3 * 4096];
    dict::probe::touch(buf + 60, 8);  // straddles lines 0 and 1
    CHECK(dict::probe::lines.size() == 2);
    CHECK(dict::probe::pages.size() == 1);
    dict::probe::touch(buf + 4090, 8);  // straddles pages 0 and 1
    CHECK(dict::probe::lines.size() == 4);
    CHECK(dict::probe::pages.size() == 2);
    dict::probe::touch(buf + 64, 4);  // line 1 again
    CHECK(dict::probe::lines.size() == 4);
    dict::probe::note(buf, 8);
    dict::probe::note(buf, 8);
    CHECK(dict::probe::read.size() == 1);
    dict::probe::begin(buf);
    dict::probe::note(buf, 8);  // the operation's own key is never counted
    CHECK(dict::probe::read.empty() && dict::probe::lines.empty());

    // Key objects and buffers are split by page; same_page counts each key once.
    dict::probe::begin(nullptr);
    dict::probe::note(buf, 8, buf + 100, 4);       // buffer on the key's page
    dict::probe::note(buf, 8, buf + 100, 4);       // same key again
    dict::probe::note(buf + 64, 8, buf + 8192, 4);  // buffer two pages away
    CHECK(dict::probe::read.size() == 2);
    CHECK(dict::probe::same_page == 1);
    CHECK(dict::probe::key_pages.size() == 1);
    CHECK(dict::probe::data_pages.size() == 2);
    CHECK(dict::probe::pages.size() == 2);
}

template <class PK>
PK make_probe(Key k) {
    if constexpr (std::is_same_v<PK, dict::ProbeString>) return PK(dict::large_string_key(k));
    else return PK(k);
}

// Probe keys give the same answers as std::map, and the distinct keys an
// operation reads are consistent with the structure's own counters. The
// red-black tree compares the key of every node it visits, so for its lookups
// the two counts must be equal.
template <class MP>
void test_probe_keys() {
    using PK = typename MP::key_type;
    const dict::Workload w = dict::make_workload("uniform", 2'000, 31);
    MP m = fresh<MP>();
    std::map<Key, Value> oracle;
    for (const dict::Phase& phase : w.phases) {
        for (const dict::Op& op : phase.ops) {
            const PK k = make_probe<PK>(op.key);
            const dict::OpCounters before = m.counters();
            dict::probe::begin(&k);
            switch (op.kind) {
                case dict::OpKind::Insert:
                    CHECK(m.insert(k, op.key) == oracle.insert_or_assign(op.key, op.key).second);
                    break;
                case dict::OpKind::Find:
                    CHECK((m.find(k) != nullptr) == oracle.contains(op.key));
                    break;
                case dict::OpKind::Erase:
                    CHECK(m.erase(k) == (oracle.erase(op.key) == 1));
                    break;
            }
            const dict::OpCounters d = m.counters() - before;
            const std::size_t distinct = dict::probe::read.size();
            CHECK(distinct <= d.comparisons);
            if (op.kind == dict::OpKind::Find && !m.empty()) CHECK(distinct >= 1);
            if (op.kind == dict::OpKind::Find && std::string_view(MP::name) == "red_black_tree")
                CHECK(distinct == d.node_visits);
            const std::size_t lines = dict::probe::lines.size();
            CHECK(dict::probe::pages.size() <= lines);
            CHECK((distinct == 0) == (lines == 0));
            CHECK(dict::probe::key_pages.size() <= dict::probe::pages.size());
            CHECK(dict::probe::data_pages.size() <= dict::probe::pages.size());
            CHECK(dict::probe::pages.size() <= dict::probe::key_pages.size() + dict::probe::data_pages.size());
            CHECK(dict::probe::same_page <= distinct);
            // An aligned 8-byte key never straddles a line.
            if constexpr (std::is_same_v<PK, dict::ProbeKey>) CHECK(lines <= distinct);
        }
        CHECK(m.size() == oracle.size());
        CHECK(m.check_invariants());
    }
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
        run_one(t, M::name, "subscript", test_subscript<M>);
        run_one(t, M::name, "erase", test_erase<M>);
        run_one(t, M::name, "differential_random", test_differential_random<M>);
        run_one(t, M::name, "workloads", test_workloads<M>);
    }
}

// String-key versions of the same implementation, MS = M<std::string, Value>.
template <dict::OrderedMap MS>
void run_string_suite(Totals& t) {
    if constexpr (MS::implemented) {
        run_one(t, MS::name, "workloads_small_string",
                [] { test_workloads_strings<MS>(dict::KeyKind::SmallString); });
        run_one(t, MS::name, "workloads_large_string",
                [] { test_workloads_strings<MS>(dict::KeyKind::LargeString); });
    }
}

// Probe-key versions of the same implementation, MP = M<dict::ProbeKey, Value>
// and MPS = M<dict::ProbeString, Value>.
template <dict::OrderedMap MP, dict::OrderedMap MPS>
void run_probe_suite(Totals& t) {
    if constexpr (MP::implemented) {
        run_one(t, MP::name, "probe_keys", test_probe_keys<MP>);
        run_one(t, MP::name, "probe_strings", test_probe_keys<MPS>);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view which = argc > 1 ? argv[1] : "all";
    Totals t;

    if (which == "infrastructure" || which == "all") {
        std::printf("[infrastructure]\n");
        run_one(t, "infrastructure", "rng_and_workloads", test_infrastructure);
        run_one(t, "infrastructure", "workload_shapes", test_workload_shapes);
        run_one(t, "infrastructure", "key_encodings", test_key_encodings);
        run_one(t, "infrastructure", "probe_touch", test_probe_touch);
    }
    if (which == "std_map" || which == "all") {
        run_suite<dict::StdMapAdapter<Key, Value>>(t);
        run_string_suite<dict::StdMapAdapter<std::string, Value>>(t);
        run_probe_suite<dict::StdMapAdapter<dict::ProbeKey, Value>, dict::StdMapAdapter<dict::ProbeString, Value>>(t);
    }
    if (which == "red_black_tree" || which == "all") {
        run_suite<dict::RedBlackTree<Key, Value>>(t);
        run_string_suite<dict::RedBlackTree<std::string, Value>>(t);
        run_probe_suite<dict::RedBlackTree<dict::ProbeKey, Value>, dict::RedBlackTree<dict::ProbeString, Value>>(t);
    }
    if (which == "skip_list" || which == "all") {
        run_suite<dict::SkipListAdapter<Key, Value>>(t);
        run_string_suite<dict::SkipListAdapter<std::string, Value>>(t);
        run_probe_suite<dict::SkipListAdapter<dict::ProbeKey, Value>, dict::SkipListAdapter<dict::ProbeString, Value>>(t);
    }

    std::printf("\n%d passed, %d failed, %d skipped\n", t.passed, t.failed, t.skipped);
    if (t.failed > 0) return 1;
    return (t.passed == 0 && t.skipped > 0) ? kSkipReturnCode : 0;
}
