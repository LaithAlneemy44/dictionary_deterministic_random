# Theories: explaining the hypothesis results

Each section gives what was measured, an explanation, the evidence already in hand, and the new test that checks the explanation, with its prediction. Numbers are from the full run (GCC 16.2 -O3, Ryzen 5 5600G) unless marked as a smoke run (one seed, n ≤ 10⁴, a sanity check only).

Labels used below:
- **Measured:** observed in the data.
- **Theory:** an explanation that follows from the algorithm.
- **Prediction:** what the new test should show if the theory is right.

## 1. The skip list takes about twice as many steps by design (H1, H2)

**Measured.** Uniform lookups at every n from 10³ to 10⁶:

| | Steps per lookup | Comparisons per lookup |
|---|---|---|
| Skip list | 1.96·log₂n | 1.96·log₂n |
| Red-black tree | 0.99·log₂n | 1.5·log₂n |
| `std::map` | | 1.07·log₂n |

The tree makes up to two comparisons per node (`<`, then `>`), which is why its comparisons are higher than its steps.

**Theory.**
- **Skip list:** with p = ½, a search takes about (1/p)·log₁/ₚn = 2·log₂n steps. At each of log₂n levels it moves right about once, and makes one comparison that stops it and drops it down.
- **Red-black tree:** its bound of 2·log₂n is a worst-case limit on height. Random insertion leaves the average depth close to log₂n.
- So the skip list's typical cost was never going to be lower than the tree's. Its advantage is simplicity, not less work.
- H2 assumed randomness would give the skip list a better median. The median is set by the 2·log₂n expectation, which is worse.

**Test.** `path_length.png` plots cost ÷ log₂n against n, with the theoretical slopes marked.

## 2. Time follows distinct nodes loaded, not steps (H1: "the gap stays similar")

**Measured.**
- Skip list ÷ tree time for uniform find, n = 10³ to 10⁷:
  - int: 1.26 to 1.32 (roughly flat)
  - small_string: about 1.42 to 1.44
  - large_string: rising from 1.35 to 1.57
- The ratio of steps is about 1.95, but no time ratio comes close to it.

**Theory.**
- When the skip list drops a level, the next node it compares against is the one it just rejected, with probability p. That node is already in cache.
- **Distinct nodes:** a lookup loads about log₁/ₚn · ((1−p)/p + (1−p)) = ((1−p²)/p)·log₁/ₚn distinct nodes. That is 1.5·log₂n for p = ½.
- **Tree:** a lookup loads about log₂n nodes, all distinct.
- **Int keys:** comparing again against a cached node is cheap, so the time ratio sits between the comparison ratio (about 1.3) and the distinct-node ratio (1.5).
- **Large strings:** the characters live in a separate heap block. Every distinct key compared costs a second memory access, so as n outgrows the cache the time ratio rises towards the distinct-node ratio.

**Test.** The probe key (`--key probe`, group E) records the distinct stored keys each operation compares against. Its count equals the tree's `node_visits` exactly, and the unit tests check this.
- **Smoke run, n = 10⁴:** skip list 1.50·log₂n, tree 0.99·log₂n.
- **Prediction:** the full run gives about 1.5 at every n. In `ratio_sources.png`, the large_string time ratio approaches the distinct-key ratio, and the int ratio stays near the comparison ratio.

## 3. Rebalancing is cheap, and allocation dilutes the gap on writes (H5)

**Measured.**
- Skip list ÷ tree at n = 10⁶: insert 1.14, erase 1.12, find 1.24. The gap is smallest on writes, the opposite of H5.
- The tree's insert is no slower than its find: 498 vs 544 ns.
- Restructuring per uniform insert: 0.58 rotations and 2.3 recolours.

**Theory.**
- Rotations and recolours only touch nodes on the search path just walked, which are still in cache.
- A write is a search, plus allocating or freeing one node, plus linking. The allocation costs roughly the same for both structures, so it shrinks the relative gap.
- The red-black tree's restructuring is not a large share of its cost.

**Test.** The `steady` workload:
- Build n keys. Take m = n/4 fresh keys. Look them up (all miss), insert them, look them up again (all hit), then erase them.
- Each write follows the same search path as the lookup before it, so:
  - insert_new − find_miss = the cost of an insert beyond its search
  - erase_new − find_new = the same for an erase
- **Prediction:** the extra insert cost is similar for both structures (`write_cost.png`). If H5's reasoning held, the tree's would be clearly higher.

## 4. Input order changes speed through cache reuse, not rotations (H3)

**Measured, n = 10⁶, int.**
- Sorted inserts give the tree its most restructuring: 1.0 rotations and 5.0 recolours per insert.
- Yet sorted insert is about 6× faster than uniform insert: 80 vs 498 ns.
- The skip list speeds up just as much: 84 vs 568 ns.
- On reverse insert, the skip list beats the tree: 47 vs 65 ns.

**Theory.**
- **Sorted and reverse inserts:** every insert walks the same path, which stays in L1 cache. Once the path is cached, the work per node is cheap.
- **Skip list, reverse insert:** inserting at the front only reads the head's tower. Its search compares against the first node at each level and stops immediately.
- **Skip list, reverse erase:** removing the largest key walks the whole path, about 2·log₂n steps (82 vs 45 ns).
- So order matters a lot for **both** structures, through which path is reused. The tree's rotation count barely shows in the time.

**Workload flaw, now fixed.**
- **Before:** sorted and reverse used keys 0…n−1, with random 64-bit misses. Every miss was larger than all keys, so it took the same cached path. That is why sorted lookups looked twice as fast as uniform (263 vs 544 ns for the tree).
- **Now:** the keys are 0, 2, …, 2n−2, and misses fall on the odd numbers between them.

**Test.** Re-run groups B and C1.
- **Prediction:** sorted and reverse lookups move close to uniform lookups.
- **Speculation:** they may stay somewhat faster, if allocating nodes in key order puts neighbouring keys close together in memory.

## 5. The tree's zipf win comes from the workload

**Measured, n = 10⁶.**

| | Zipf find | Uniform find | Node visits under zipf |
|---|---|---|---|
| Red-black tree | 161 ns | 544 ns | 12.8 (19.9 uniform) |
| Skip list | 445 ns | 676 ns | 38.7 (same as uniform) |
| `std::map` | 279 ns | | |

**Theory.**
- Our zipf workload gives popularity rank r to the r-th key inserted, so the hottest keys are the oldest. Early keys tend to stay near the root of a tree.
- Aaron's `find` stops as soon as it reaches the key, so hot lookups are short.
- `std::map::find` always descends to a leaf, so it gets less of this benefit.
- A skip list node's height does not depend on when it was inserted, so the skip list cannot benefit at all.
- The 2.8× gap is therefore mostly a property of this workload, not of the algorithms.

**Test.** The `zipf_shuffled` workload gives popularity ranks by an independent shuffle.
- **Smoke run, n = 10⁴:** tree node visits are 9.0 under zipf and 12.6 under zipf_shuffled.
- **Prediction:** under zipf_shuffled, the tree's node visits return to about log₂n, and its lead shrinks to roughly its uniform lead (`zipf_order.png`).

## 6. Seed effects shrink as n grows (H4: supported, but not for the stated reason)

**Measured.**
- At n = 10⁵, per-operation cost varies 12–24% across algorithm seeds, against 4–15% across input seeds. H4's main claim holds.
- At n = 10⁶, the worst lookup took 74 comparisons, about 3.7·log₂n.
- The skip list's p99.9 ÷ p50 of comparisons falls from 1.84 at 10³ to 1.54 at 10⁶.

**Theory.**
- The skip list makes exactly one allocation per node whatever the seed. The seed only changes node heights, so "more memory allocations" is not a mechanism.
- A lookup's cost is a sum of independent random steps, so it concentrates around its mean as n grows.
- Degrading towards O(n) needs the heights to be badly skewed across many levels at once, which has exponentially small probability.

**Test.** Group D now runs at n = 10³, 10⁴, 10⁵ and 3·10⁵.
- **Prediction:** the spread across algorithm seeds shrinks as n grows (`seeds_spread_vs_n.png`). It stays larger than the spread across inputs.

## Possible further tests (need skip list changes)

- **Make p a parameter.** The distinct-node formula ((1−p²)/p)·log₁/ₚn predicts 1.875·log₂n for p = ¼, worse than 1.5·log₂n for p = ½. Larger p reads fewer distinct nodes but uses more memory per node.
- **Skip repeated comparisons.** After a drop, the search compares against the node it just rejected about half the time. Remembering that node would save about 0.5·log₂n comparisons per lookup, which matters most for string keys.
