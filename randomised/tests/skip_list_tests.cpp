// Unit tests for SkipList<K, V>: insert, erase, exists, find and operator[].
//
// The randomised tests compare against std::map, which is the reference answer.
// Inputs use a fixed seed so a failure reproduces on every run, and each list is
// seeded so its tower heights reproduce too.

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dict/counters.hpp"
#include "skip_list.hpp"

// ---- Empty list ------------------------------------------------------------

TEST(SkipListEmpty, ExistsIsFalse) {
    SkipList<int, int> list;
    EXPECT_FALSE(list.exists(0));
    EXPECT_FALSE(list.exists(-1));
    EXPECT_FALSE(list.exists(42));
    EXPECT_EQ(list.find(42), nullptr);
}

TEST(SkipListEmpty, EraseIsNoOp) {
    SkipList<int, int> list;
    list.erase(5);
    EXPECT_FALSE(list.exists(5));
}

// ---- Insert ----------------------------------------------------------------

TEST(SkipListInsert, SingleElement) {
    SkipList<int, int> list;
    list.insert(7, 70);
    EXPECT_TRUE(list.exists(7));
    EXPECT_FALSE(list.exists(6));
    EXPECT_FALSE(list.exists(8));
}

TEST(SkipListInsert, NewMinimumAndMaximum) {
    SkipList<int, int> list;
    list.insert(50, 5);
    list.insert(10, 1);  // goes before every existing node
    list.insert(90, 9);  // goes after every existing node
    list.insert(30, 3);  // goes in the middle
    for (int k : {10, 30, 50, 90}) {
        ASSERT_NE(list.find(k), nullptr) << "missing " << k;
        EXPECT_EQ(*list.find(k), k / 10) << "wrong value for " << k;
    }
    for (int k : {0, 20, 40, 60, 100}) {
        EXPECT_FALSE(list.exists(k)) << "unexpected " << k;
    }
}

TEST(SkipListInsert, AscendingOrder) {
    SkipList<int, int> list;
    for (int k = 0; k < 1000; ++k) list.insert(k, k);
    for (int k = 0; k < 1000; ++k) EXPECT_TRUE(list.exists(k)) << "missing " << k;
    EXPECT_FALSE(list.exists(-1));
    EXPECT_FALSE(list.exists(1000));
}

TEST(SkipListInsert, DescendingOrder) {
    SkipList<int, int> list;
    for (int k = 999; k >= 0; --k) list.insert(k, k);
    for (int k = 0; k < 1000; ++k) EXPECT_TRUE(list.exists(k)) << "missing " << k;
    EXPECT_FALSE(list.exists(-1));
    EXPECT_FALSE(list.exists(1000));
}

TEST(SkipListInsert, DuplicateOverwritesValue) {
    // Unique keys: a second insert must overwrite, not add a second node.
    // If it added one, a single erase would leave a copy behind.
    SkipList<int, int> list;
    EXPECT_TRUE(list.insert(4, 1));
    EXPECT_FALSE(list.insert(4, 2));
    EXPECT_FALSE(list.insert(4, 3));
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(*list.find(4), 3);

    list.erase(4);
    EXPECT_FALSE(list.exists(4));
}

// ---- Erase -----------------------------------------------------------------

TEST(SkipListErase, OnlyElement) {
    SkipList<int, int> list;
    list.insert(1, 1);
    list.erase(1);
    EXPECT_FALSE(list.exists(1));
}

TEST(SkipListErase, KeepsNeighbours) {
    SkipList<int, int> list;
    for (int k : {1, 2, 3, 4, 5}) list.insert(k, k * 10);

    list.erase(3);  // middle
    list.erase(1);  // first
    list.erase(5);  // last

    EXPECT_FALSE(list.exists(1));
    EXPECT_EQ(*list.find(2), 20);
    EXPECT_FALSE(list.exists(3));
    EXPECT_EQ(*list.find(4), 40);
    EXPECT_FALSE(list.exists(5));
}

TEST(SkipListErase, AbsentKeyIsNoOp) {
    SkipList<int, int> list;
    for (int k : {10, 20, 30}) list.insert(k, k);

    list.erase(5);   // smaller than everything
    list.erase(25);  // between keys
    list.erase(35);  // larger than everything

    for (int k : {10, 20, 30}) EXPECT_TRUE(list.exists(k)) << "missing " << k;
}

TEST(SkipListErase, TwiceIsNoOp) {
    SkipList<int, int> list;
    list.insert(1, 1);
    list.insert(2, 2);
    list.erase(1);
    list.erase(1);
    EXPECT_FALSE(list.exists(1));
    EXPECT_TRUE(list.exists(2));
}

TEST(SkipListErase, EverythingThenReuse) {
    // Erasing all nodes must leave the head in a usable state.
    SkipList<int, int> list;
    for (int k = 0; k < 500; ++k) list.insert(k, k);
    for (int k = 0; k < 500; ++k) list.erase(k);
    for (int k = 0; k < 500; ++k) EXPECT_FALSE(list.exists(k)) << "still present " << k;

    for (int k = 0; k < 500; k += 2) list.insert(k, k);
    for (int k = 0; k < 500; ++k) EXPECT_EQ(list.exists(k), k % 2 == 0) << "key " << k;
}

TEST(SkipListErase, ReinsertAfterErase) {
    SkipList<int, int> list;
    list.insert(8, 1);
    list.erase(8);
    list.insert(8, 2);
    EXPECT_EQ(*list.find(8), 2);
}

// ---- find and operator[] ---------------------------------------------------

TEST(SkipListLookup, FindReturnsValueOrNull) {
    SkipList<int, int> list;
    list.insert(1, 100);
    list.insert(2, 200);
    ASSERT_NE(list.find(1), nullptr);
    EXPECT_EQ(*list.find(1), 100);
    EXPECT_EQ(list.find(3), nullptr);
}

TEST(SkipListLookup, SubscriptReadsAndWritesExistingValue) {
    SkipList<int, int> list;
    list.insert(1, 10);
    EXPECT_EQ(list[1], 10);

    list[1] = 15;
    EXPECT_EQ(*list.find(1), 15);

    const SkipList<int, int>& view = list;
    EXPECT_EQ(view[1], 15);
}

TEST(SkipListLookup, SubscriptThrowsAndNeverInserts) {
    SkipList<int, int> list;
    list.insert(1, 10);
    EXPECT_THROW((void)list[2], std::out_of_range);

    const SkipList<int, int>& view = list;
    EXPECT_THROW((void)view[2], std::out_of_range);

    EXPECT_EQ(list.size(), 1u);
    EXPECT_FALSE(list.exists(2));
}

// ---- Randomised against std::map ------------------------------------------

namespace {

// Applies a random mix of operations to both structures and checks every
// result, the size, and periodically the skip list's invariants. The skip list
// is seeded with the same seed, so each seed also tries different tower heights.
void run_against_map(unsigned seed, int operations, int key_range) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> key(0, key_range - 1);
    std::uniform_int_distribution<int> op(0, 2);

    SkipList<int, int> list(seed);
    std::map<int, int> reference;

    for (int i = 0; i < operations; ++i) {
        const int k = key(rng);
        switch (op(rng)) {
        case 0: {
            const int v = static_cast<int>(rng());
            ASSERT_EQ(list.insert(k, v), reference.insert_or_assign(k, v).second)
                << "seed " << seed << ", operation " << i << ", insert " << k;
            break;
        }
        case 1:
            ASSERT_EQ(list.erase(k), reference.erase(k) == 1)
                << "seed " << seed << ", operation " << i << ", erase " << k;
            break;
        default: {
            const int* got = list.find(k);
            const auto it = reference.find(k);
            ASSERT_EQ(got != nullptr, it != reference.end())
                << "seed " << seed << ", operation " << i << ", key " << k;
            if (got) {
                ASSERT_EQ(*got, it->second) << "seed " << seed << ", operation " << i << ", key " << k;
            }
        }
        }
        ASSERT_EQ(list.size(), reference.size()) << "seed " << seed << ", operation " << i;
        if (i % 500 == 0) {
            ASSERT_TRUE(list.check_invariants()) << "seed " << seed << ", operation " << i;
        }
    }

    ASSERT_TRUE(list.check_invariants()) << "seed " << seed;
    std::vector<std::pair<int, int>> contents;
    list.for_each([&](int k, int v) { contents.emplace_back(k, v); });
    ASSERT_EQ(contents, (std::vector<std::pair<int, int>>(reference.begin(), reference.end()))) << "seed " << seed;
}

}  // namespace

TEST(SkipListRandomised, SmallKeyRangeManyCollisions) {
    // Small range so inserts and erases often hit existing keys.
    for (unsigned seed = 1; seed <= 20; ++seed) run_against_map(seed, 2000, 50);
}

TEST(SkipListRandomised, LargeKeyRange) {
    run_against_map(12345, 200000, 20000);
}

TEST(SkipListRandomised, ShuffledInsertThenShuffledErase) {
    std::vector<int> keys(5000);
    std::iota(keys.begin(), keys.end(), 0);
    std::mt19937 rng(99);

    SkipList<int, int> list;
    std::shuffle(keys.begin(), keys.end(), rng);
    for (int k : keys) list.insert(k, k);
    for (int k : keys) ASSERT_TRUE(list.exists(k)) << "missing " << k;

    std::shuffle(keys.begin(), keys.end(), rng);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        list.erase(keys[i]);
        ASSERT_FALSE(list.exists(keys[i])) << "still present " << keys[i];
        // Spot-check that erasing didn't break the rest of the list.
        if (i + 1 < keys.size()) {
            ASSERT_TRUE(list.exists(keys[i + 1])) << "lost " << keys[i + 1] << " after erasing " << keys[i];
        }
    }
}

// ---- Other key and value types ---------------------------------------------

TEST(SkipListTypes, Strings) {
    SkipList<std::string, std::string> list;
    list.insert("pear", "green");
    list.insert("apple", "red");
    list.insert("fig", "purple");
    list.insert("banana", "yellow");

    EXPECT_EQ(list["apple"], "red");
    EXPECT_EQ(list["pear"], "green");
    EXPECT_FALSE(list.exists("grape"));

    list.erase("fig");
    EXPECT_FALSE(list.exists("fig"));
    EXPECT_EQ(list["banana"], "yellow");
}

namespace {

// No default constructor: the head must not need to build a K or a V.
// Defines every comparison so it works whichever ones the skip list uses;
// [[maybe_unused]] silences warnings for the ones it doesn't.
struct NoDefault {
    explicit NoDefault(int v) : value(v) {
    }
    int value;

    [[maybe_unused]] friend bool operator<(const NoDefault& a, const NoDefault& b) {
        return a.value < b.value;
    }
    [[maybe_unused]] friend bool operator>(const NoDefault& a, const NoDefault& b) {
        return b < a;
    }
    [[maybe_unused]] friend bool operator<=(const NoDefault& a, const NoDefault& b) {
        return !(b < a);
    }
    [[maybe_unused]] friend bool operator>=(const NoDefault& a, const NoDefault& b) {
        return !(a < b);
    }
    [[maybe_unused]] friend bool operator==(const NoDefault& a, const NoDefault& b) {
        return a.value == b.value;
    }
    [[maybe_unused]] friend bool operator!=(const NoDefault& a, const NoDefault& b) {
        return !(a == b);
    }
};

}  // namespace

TEST(SkipListTypes, NotDefaultConstructible) {
    SkipList<NoDefault, NoDefault> list;
    list.insert(NoDefault(3), NoDefault(30));
    list.insert(NoDefault(1), NoDefault(10));
    EXPECT_EQ(list[NoDefault(1)].value, 10);
    EXPECT_EQ(list[NoDefault(3)].value, 30);
    EXPECT_FALSE(list.exists(NoDefault(2)));

    list.insert(NoDefault(1), NoDefault(11));
    EXPECT_EQ(list[NoDefault(1)].value, 11);

    list.erase(NoDefault(1));
    EXPECT_FALSE(list.exists(NoDefault(1)));
}

// ---- Counters --------------------------------------------------------------
// Built with DICT_INSTRUMENT (see CMakeLists.txt), so the counters are live.

TEST(SkipListCounters, EmptyLookupCostsNothing) {
    SkipList<int, int> list;
    EXPECT_FALSE(list.exists(5));
    EXPECT_EQ(list.counters().comparisons, 0u);
    EXPECT_EQ(list.counters().node_visits, 0u);
}

TEST(SkipListCounters, ResetZeroesEverything) {
    SkipList<int, int> list;
    for (int i = 0; i < 50; ++i) {
        list.insert(i, i);
    }
    EXPECT_GT(list.counters().comparisons, 0u);

    list.reset_counters();
    EXPECT_EQ(list.counters().comparisons, 0u);
    EXPECT_EQ(list.counters().node_visits, 0u);
    EXPECT_EQ(list.counters().structural, 0u);
    EXPECT_EQ(list.counters().allocations, 0u);
}

TEST(SkipListCounters, OneAllocationPerNewKeyOnly) {
    SkipList<int, int> list;
    for (int i = 0; i < 100; ++i) {
        list.insert(i, i);
    }
    EXPECT_EQ(list.counters().allocations, 100u);

    const auto structural = list.counters().structural;
    for (int i = 0; i < 100; ++i) {
        list.insert(i, i + 1);
    }
    EXPECT_EQ(list.counters().allocations, 100u);
    EXPECT_EQ(list.counters().structural, structural);
}

// A node is unlinked from exactly the levels it was linked into.
TEST(SkipListCounters, EraseSplicesMatchInsertSplices) {
    SkipList<int, int> list;
    for (int i = 0; i < 500; ++i) {
        list.insert(i, i);
    }
    const auto linked = list.counters().structural;
    EXPECT_GE(linked, 500u);

    list.reset_counters();
    for (int i = 0; i < 500; ++i) {
        list.erase(i);
    }
    EXPECT_EQ(list.counters().structural, linked);
}

// With one node of height h, a lookup of it drops through all h levels,
// comparing against the node at each one, then checks equality once.
TEST(SkipListCounters, SingleNodeLookupCost) {
    SkipList<int, int> list;
    list.insert(7, 70);
    const auto height = list.counters().structural;

    list.reset_counters();
    EXPECT_TRUE(list.exists(7));
    EXPECT_EQ(list.counters().node_visits, height);
    EXPECT_EQ(list.counters().comparisons, height + 1);
}

// ---- Seeding ---------------------------------------------------------------
// The total splices from inserting the same keys is the sum of all tower
// heights, so it acts as a fingerprint of the structure.

namespace {

unsigned long long heights_after_inserts(SkipList<int, int>& list) {
    for (int i = 0; i < 1000; ++i) {
        list.insert(i, i);
    }
    return list.counters().structural;
}

}  // namespace

TEST(SkipListSeed, SameSeedSameStructure) {
    SkipList<int, int> a(42);
    SkipList<int, int> b(42);
    EXPECT_EQ(heights_after_inserts(a), heights_after_inserts(b));
}

TEST(SkipListSeed, DifferentSeedsDifferentStructure) {
    SkipList<int, int> a(1);
    SkipList<int, int> b(2);
    EXPECT_NE(heights_after_inserts(a), heights_after_inserts(b));
}

// ---- Return values, size, clear, for_each ----------------------------------

TEST(SkipListInterface, InsertAndEraseReportChanges) {
    SkipList<int, int> list;
    EXPECT_TRUE(list.insert(5, 1));
    EXPECT_FALSE(list.insert(5, 2));
    EXPECT_TRUE(list.erase(5));
    EXPECT_FALSE(list.erase(5));
}

TEST(SkipListInterface, SizeAndEmpty) {
    SkipList<int, int> list;
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);

    list.insert(3, 3);
    list.insert(1, 1);
    list.insert(3, 4);
    EXPECT_FALSE(list.empty());
    EXPECT_EQ(list.size(), 2u);

    list.erase(1);
    list.erase(4);
    EXPECT_EQ(list.size(), 1u);
}

TEST(SkipListInterface, ForEachIsAscending) {
    SkipList<int, int> list;
    for (int k : {5, 1, 9, 3, 7}) {
        list.insert(k, k * 10);
    }

    std::vector<std::pair<int, int>> seen;
    list.for_each([&](int k, int v) { seen.emplace_back(k, v); });
    EXPECT_EQ(seen, (std::vector<std::pair<int, int>>{{1, 10}, {3, 30}, {5, 50}, {7, 70}, {9, 90}}));
}

TEST(SkipListInterface, ClearThenReuse) {
    SkipList<int, int> list;
    for (int i = 0; i < 1000; ++i) {
        list.insert(i, i);
    }

    list.clear();
    EXPECT_TRUE(list.empty());
    EXPECT_FALSE(list.exists(500));
    EXPECT_TRUE(list.check_invariants());

    EXPECT_TRUE(list.insert(42, 1));
    EXPECT_TRUE(list.exists(42));
    EXPECT_EQ(list.size(), 1u);
    EXPECT_TRUE(list.check_invariants());
}

TEST(SkipListInterface, InvariantsHoldWhenEmpty) {
    SkipList<int, int> list;
    EXPECT_TRUE(list.check_invariants());

    list.insert(1, 1);
    list.erase(1);
    EXPECT_TRUE(list.check_invariants());
}
