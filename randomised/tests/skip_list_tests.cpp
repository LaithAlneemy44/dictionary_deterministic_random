// Unit tests for SkipList<T>: insert, erase and exists.
//
// The randomised tests compare against std::set, which is the reference answer.
// Inputs use a fixed seed so a failure reproduces on every run. The skip list's
// own tower heights are still random, so a failure that only shows up sometimes
// points at a height-dependent bug.

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "skip_list.hpp"

// ---- Empty list ------------------------------------------------------------

TEST(SkipListEmpty, ExistsIsFalse) {
    SkipList<int> list;
    EXPECT_FALSE(list.exists(0));
    EXPECT_FALSE(list.exists(-1));
    EXPECT_FALSE(list.exists(42));
}

TEST(SkipListEmpty, EraseIsNoOp) {
    SkipList<int> list;
    list.erase(5);
    EXPECT_FALSE(list.exists(5));
}

// ---- Insert ----------------------------------------------------------------

TEST(SkipListInsert, SingleElement) {
    SkipList<int> list;
    list.insert(7);
    EXPECT_TRUE(list.exists(7));
    EXPECT_FALSE(list.exists(6));
    EXPECT_FALSE(list.exists(8));
}

TEST(SkipListInsert, NewMinimumAndMaximum) {
    SkipList<int> list;
    list.insert(50);
    list.insert(10);  // goes before every existing node
    list.insert(90);  // goes after every existing node
    list.insert(30);  // goes in the middle
    for (int k : {10, 30, 50, 90}) {
        EXPECT_TRUE(list.exists(k)) << "missing " << k;
    }
    for (int k : {0, 20, 40, 60, 100}) {
        EXPECT_FALSE(list.exists(k)) << "unexpected " << k;
    }
}

TEST(SkipListInsert, AscendingOrder) {
    SkipList<int> list;
    for (int k = 0; k < 1000; ++k) list.insert(k);
    for (int k = 0; k < 1000; ++k) EXPECT_TRUE(list.exists(k)) << "missing " << k;
    EXPECT_FALSE(list.exists(-1));
    EXPECT_FALSE(list.exists(1000));
}

TEST(SkipListInsert, DescendingOrder) {
    SkipList<int> list;
    for (int k = 999; k >= 0; --k) list.insert(k);
    for (int k = 0; k < 1000; ++k) EXPECT_TRUE(list.exists(k)) << "missing " << k;
    EXPECT_FALSE(list.exists(-1));
    EXPECT_FALSE(list.exists(1000));
}

TEST(SkipListInsert, DuplicateIsIgnored) {
    // Unique keys: a second insert must not add a second node.
    // If it did, one erase would leave a copy behind.
    SkipList<int> list;
    list.insert(4);
    list.insert(4);
    list.insert(4);
    list.erase(4);
    EXPECT_FALSE(list.exists(4));
}

// ---- Erase -----------------------------------------------------------------

TEST(SkipListErase, OnlyElement) {
    SkipList<int> list;
    list.insert(1);
    list.erase(1);
    EXPECT_FALSE(list.exists(1));
}

TEST(SkipListErase, KeepsNeighbours) {
    SkipList<int> list;
    for (int k : {1, 2, 3, 4, 5}) list.insert(k);

    list.erase(3);  // middle
    list.erase(1);  // first
    list.erase(5);  // last

    EXPECT_FALSE(list.exists(1));
    EXPECT_TRUE(list.exists(2));
    EXPECT_FALSE(list.exists(3));
    EXPECT_TRUE(list.exists(4));
    EXPECT_FALSE(list.exists(5));
}

TEST(SkipListErase, AbsentKeyIsNoOp) {
    SkipList<int> list;
    for (int k : {10, 20, 30}) list.insert(k);

    list.erase(5);   // smaller than everything
    list.erase(25);  // between keys
    list.erase(35);  // larger than everything

    for (int k : {10, 20, 30}) EXPECT_TRUE(list.exists(k)) << "missing " << k;
}

TEST(SkipListErase, TwiceIsNoOp) {
    SkipList<int> list;
    list.insert(1);
    list.insert(2);
    list.erase(1);
    list.erase(1);
    EXPECT_FALSE(list.exists(1));
    EXPECT_TRUE(list.exists(2));
}

TEST(SkipListErase, EverythingThenReuse) {
    // Erasing all nodes must leave the head in a usable state.
    SkipList<int> list;
    for (int k = 0; k < 500; ++k) list.insert(k);
    for (int k = 0; k < 500; ++k) list.erase(k);
    for (int k = 0; k < 500; ++k) EXPECT_FALSE(list.exists(k)) << "still present " << k;

    for (int k = 0; k < 500; k += 2) list.insert(k);
    for (int k = 0; k < 500; ++k) EXPECT_EQ(list.exists(k), k % 2 == 0) << "key " << k;
}

TEST(SkipListErase, ReinsertAfterErase) {
    SkipList<int> list;
    list.insert(8);
    list.erase(8);
    list.insert(8);
    EXPECT_TRUE(list.exists(8));
}

// ---- Randomised against std::set ------------------------------------------

namespace {

// Applies a random mix of operations to both structures and checks every lookup.
void run_against_set(unsigned seed, int operations, int key_range) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> key(0, key_range - 1);
    std::uniform_int_distribution<int> op(0, 2);

    SkipList<int> list;
    std::set<int> reference;

    for (int i = 0; i < operations; ++i) {
        const int k = key(rng);
        switch (op(rng)) {
        case 0:
            list.insert(k);
            reference.insert(k);
            break;
        case 1:
            list.erase(k);
            reference.erase(k);
            break;
        default:
            ASSERT_EQ(list.exists(k), reference.count(k) == 1)
                << "seed " << seed << ", operation " << i << ", key " << k;
        }
    }

    for (int k = 0; k < key_range; ++k) {
        ASSERT_EQ(list.exists(k), reference.count(k) == 1) << "seed " << seed << ", final key " << k;
    }
}

}  // namespace

TEST(SkipListRandomised, SmallKeyRangeManyCollisions) {
    // Small range so inserts and erases often hit existing keys.
    for (unsigned seed = 1; seed <= 20; ++seed) run_against_set(seed, 2000, 50);
}

TEST(SkipListRandomised, LargeKeyRange) {
    run_against_set(12345, 200000, 20000);
}

TEST(SkipListRandomised, ShuffledInsertThenShuffledErase) {
    std::vector<int> keys(5000);
    std::iota(keys.begin(), keys.end(), 0);
    std::mt19937 rng(99);

    SkipList<int> list;
    std::shuffle(keys.begin(), keys.end(), rng);
    for (int k : keys) list.insert(k);
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

// ---- Other element types ---------------------------------------------------

TEST(SkipListTypes, Strings) {
    SkipList<std::string> list;
    for (const char* s : {"pear", "apple", "fig", "banana"}) list.insert(s);

    EXPECT_TRUE(list.exists("apple"));
    EXPECT_TRUE(list.exists("pear"));
    EXPECT_FALSE(list.exists("grape"));

    list.erase("fig");
    EXPECT_FALSE(list.exists("fig"));
    EXPECT_TRUE(list.exists("banana"));
}

namespace {

// No default constructor: the head must not need to build a T.
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
    SkipList<NoDefault> list;
    list.insert(NoDefault(3));
    list.insert(NoDefault(1));
    EXPECT_TRUE(list.exists(NoDefault(1)));
    EXPECT_TRUE(list.exists(NoDefault(3)));
    EXPECT_FALSE(list.exists(NoDefault(2)));

    list.erase(NoDefault(1));
    EXPECT_FALSE(list.exists(NoDefault(1)));
}
