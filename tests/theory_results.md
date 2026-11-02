# Theory results: what the new tests showed

Outcomes of the tests proposed in [`theories.md`](theories.md), one section per theory, in the same order. Numbers are from the run of groups B, C1, D and E (GCC 16.2 -O3, Ryzen 5 5600G, 19.3 minutes). Plots are in `results/plots/` and summary tables are in `results/summary/`.

Labels used below:
- **Measured:** observed in the data.
- **Verdict:** how the measurement compares with the prediction.
- **Speculation:** a possible explanation that has not been tested.

## Summary

| Theory | Verdict |
|---|---|
| 1. The skip list takes about twice as many steps by design | Supported |
| 2. Time follows distinct nodes loaded, not steps | Mostly supported |
| 3. Rebalancing is cheap, and writes cost about the same beyond their search | Supported up to 10⁵ only |
| 4. Input order changes speed through cache reuse | Supported, with a new finding |
| 5. The tree's zipf win comes from the workload | Supported |
| 6. Seed effects shrink as n grows | Not supported as stated |

## 1. Steps per lookup (`path_length.png`)

**Measured.** Uniform find, mean cost per lookup ÷ log₂n, n = 10³ to 10⁶:

| | Steps (node visits) | Comparisons |
|---|---|---|
| Skip list | 1.88–2.04 | 1.89–2.00 |
| Red-black tree | 0.98–1.00 | 1.48–1.51 |
| `std::map` | | 1.07–1.12 |

**Verdict: supported.** The skip list stays close to the predicted 2·log₂n and the tree close to log₂n at every size.

## 2. Distinct nodes read (`ratio_sources.png`)

**Measured.** Distinct stored keys compared per lookup ÷ log₂n (probe keys, group E):
- Skip list: 1.38–1.45, against a predicted 1.5.
- Red-black tree: 0.98–1.00. This equals its node visits, as the unit tests require.

Skip list ÷ tree ratios for uniform find:

| n | int time | small_string time | large_string time | Distinct keys | Comparisons |
|---|---|---|---|---|---|
| 10³ | 1.26 | 1.42 | 1.35 | 1.47 | 1.34 |
| 10⁴ | 1.34 | 1.43 | 1.41 | 1.45 | 1.32 |
| 10⁵ | 1.24 | 1.48 | 1.38 | 1.39 | 1.26 |
| 10⁶ | 1.24 | 1.40 | 1.51 | 1.45 | 1.30 |
| 3·10⁶ | 1.27 | 1.49 | 1.56 | | |
| 10⁷ | 1.32 | 1.44 | 1.57 | | |

The probe-key runs stop at 10⁶, so there is no distinct-key ratio for the two largest sizes.

**Verdict: mostly supported.**
- The int time ratio sits near the comparison ratio (about 1.3), as predicted.
- The distinct-key count is about 4–8% below the formula. The smoke run's 1.50 was one seed at one size.
- The large_string ratio does rise with n, but from 10⁶ it reaches 1.51–1.57, above the distinct-key ratio of about 1.45. Distinct keys explain most of the gap but not all of it.

**Speculation.** The skip list's nodes vary in size with their height, so they may be spread over more cache lines than the tree's fixed-size nodes. That would add misses that the distinct-key count does not see.

## 3. Write cost beyond the search (`write_cost.png`, `summary/write_cost.csv`)

**Measured.** `steady` workload, int keys, ns per operation:

| n | Tree insert extra | Skip list insert extra | `std::map` insert extra | Tree erase extra | Skip list erase extra |
|---|---|---|---|---|---|
| 10³ | 42 | 44 | 50 | 16 | 19 |
| 10⁴ | 47 | 46 | 57 | 17 | 22 |
| 10⁵ | 54 | 63 | 76 | 19 | 29 |
| 3·10⁵ | 88 | 160 | 143 | 48 | 75 |
| 10⁶ | 134 | 227 | 191 | 42 | 139 |

Insert extra = insert_new − find_miss. Erase extra = erase_new − find_new.

**Verdict: supported up to 10⁵, not beyond.**
- Up to 10⁵ the extra cost of an insert is nearly the same for both structures, as predicted.
- From 3·10⁵ the skip list's writes cost much more than the tree's: about 1.7× for insert and over 3× for erase at 10⁶.
- The conclusion against H5 still stands: the tree's rebalancing never makes its writes expensive. Its insert extra stays below `std::map`'s at every size.

**Speculation.** A skip list write touches the predecessor at every level of the new node's tower. Some of those predecessors are not on the cached search path, or their towers sit on a different cache line from their keys. Once the structure no longer fits in cache, each of those is a miss. This has not been tested.

## 4. Input order (`time_sorted.png`, `time_reverse.png`)

**Measured.** Find, int keys, ns per lookup, with misses now in the gaps between keys:

| Workload | n | Tree | Skip list | `std::map` |
|---|---|---|---|---|
| uniform | 10⁶ | 544 | 676 | 546 |
| sorted | 10⁶ | 587 | 564 | 551 |
| reverse | 10⁶ | 542 | 543 | 533 |
| uniform | 10⁵ | 167 | 207 | 198 |
| sorted | 10⁵ | 154 | 189 | 168 |
| reverse | 10⁵ | 142 | 180 | 163 |

Tree node visits per find at 10⁶ are 19.8 for sorted and reverse, against 19.9 for uniform.

**Verdict: supported.**
- The tree's sorted lookups dropped from 263 ns to 587 ns at 10⁶ and now match uniform. The old "2× faster" result came from the workload flaw.
- Node visits are the same for every order, so the tree's shape after sorted inserts is not the cause of any difference.

**New finding.** At 10⁶, the skip list's lookups on a structure built in sorted or reverse order are 17–20% faster than on one built in random order (564 and 543 vs 676 ns). There they match or beat the tree.

**Speculation.** Nodes allocated in key order probably sit next to their neighbours in memory, so moving right along a level often stays within nearby cache lines. The tree also benefits a little at 10⁵ but not at 10⁶. Testing this needs the node addresses, which the harness does not record.

## 5. Zipf and insertion order (`zipf_order.png`)

**Measured.** n = 10⁶, int keys:

| | zipf | zipf_shuffled | uniform |
|---|---|---|---|
| Tree node visits | 12.8 | 19.4 | 19.9 |
| Skip list node visits | 38.7 | 38.4 | 38.5 |
| Tree find (ns) | 166 | 304 | 544 |
| Skip list find (ns) | 404 | 414 | 676 |
| Skip list ÷ tree time | 2.43 | 1.36 | 1.24 |

**Verdict: supported.**
- Once popularity is unrelated to insertion order, the tree's node visits return to about log₂n.
- Its lead falls from 2.4× to 1.36×, close to its uniform lead.
- The skip list is unaffected by the order, as predicted.
- Both structures are still faster under zipf_shuffled than under uniform, because the hot keys stay in cache.

## 6. Seed effects (`seeds_spread_vs_n.png`, `summary/seed_variance.csv`)

**Measured.** Spread of mean cost per operation (p95 ÷ p5), uniform int:

| n | Node visits, across algorithm seeds | Node visits, across input seeds | Time, across algorithm seeds | Time, across input seeds |
|---|---|---|---|---|
| 10³ | 1.26–1.32 | 1.06–1.10 | 1.09–1.20 | 1.06–1.17 |
| 10⁴ | 1.11 | 1.04–1.07 | 1.04–1.07 | 1.04–1.06 |
| 10⁵ | 1.15–1.24 | 1.04–1.06 | 1.10–1.16 | 1.06–1.10 |
| 3·10⁵ | 1.13–1.17 | 1.04–1.05 | 1.25–1.28 | 1.12–1.21 |

Ranges cover the insert, find and erase phases.

**Verdict: not supported as stated.**
- The spread across algorithm seeds is larger than the spread across input seeds at every size. H4's main claim holds.
- The spread across input seeds shrinks with n, as predicted.
- The spread across algorithm seeds does not shrink steadily: it falls from 10³ to 10⁴, then rises again at 10⁵.
- The time spreads grow at 3·10⁵ for both kinds of seed. This is probably cache and memory noise rather than the algorithm.

**Revised theory (untested).** The theory confused two kinds of averaging.
- Within one structure, a lookup's cost is a sum of many random steps, so per-lookup tails narrow as n grows. This part was supported in the earlier run (p99.9 ÷ p50 fell from 1.84 to 1.54).
- Across seeds, every search passes through the top few levels, which hold only a handful of nodes at any n. How those few nodes are spaced is fixed for the whole run by the seed, and it does not average out as n grows. So the mean cost of one structure can differ from another's by a similar amount at any size.
- A test: record the number of nodes at each of the top levels per seed and check whether it predicts that seed's mean cost.

## What these results change

- H5 is refuted for the right reason: rebalancing is cheap. But the claim that writes cost the same beyond their search only holds while the structure fits in cache.
- H3 needs restating. Insertion order changes speed for both structures, and after a sorted build it can favour the skip list.
- The zipf comparison should use zipf_shuffled, or report both workloads and explain the difference.
- The large-string and large-n write results leave a cost the current counters do not explain. Cache-miss counts from a profiler would test the speculation in sections 2 and 3.
