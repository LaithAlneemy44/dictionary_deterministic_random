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

**Speculation.** The pointers a write changes are probably not the cause. The predecessor at each level is the node where the search dropped down, and the search has just read its key and its forward pointer at that level, so they should still be in cache. That leaves allocation and freeing as the main suspects: skip list nodes vary in size with their height, while tree nodes are all the same size. This has not been tested. (An earlier version of this paragraph blamed predecessors off the search path, which cannot happen.)

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

## Follow-up tests

| Open question | Test | Run with | Output |
|---|---|---|---|
| Section 6: is the seed spread pattern just noise? | 200 algorithm seeds, 1 input seed, skip list only, with bootstrap 95% intervals on p95 ÷ p5 | `-Groups D2` | `seeds_spread_ci.png`, `summary/seed_spread_ci.csv` |
| Section 6: does the top of the structure set a seed's cost? | Simulation of the skip list model (not the C++ code): spread across 200 seeds, and how much of it the node counts on the top levels explain (R²) | `python scripts/simulate_skiplist.py` | `sim_seeds.png`, `summary/sim_seeds_explained.csv` |
| Section 2: why 1.38–1.45 and not 1.5? | The same simulation, comparing distinct nodes per search with the formula | `python scripts/simulate_skiplist.py` | `sim_distinct.png`, `summary/sim_distinct.csv` |
| Section 4: do sorted builds have better locality? | Probe keys now also count the distinct cache lines and 4 KiB pages holding the key bytes each operation reads | `-Groups E` | `locality.png`, `summary/locality.csv` |
| Section 2: what else adds to the large-string ratio? | A large-string probe key that also counts the lines of each string's character buffer | `-Groups E` | `ratio_sources.png` (cache lines line) |
| Section 3: is allocation the cause? | Heap allocations per operation and their sizes (every instrumented run), plus `dict_alloc_bench`, which times allocation and freeing alone with block sizes modelled on each node | `-Groups C1,G` | `alloc_vs_write.png`, `summary/alloc.csv`, `alloc_size` in `count_percentiles.csv` |

Limits:
- Lines and pages count only key bytes. Links and towers are read too but the probe cannot see them, so these counts are lower bounds.
- Allocation counts miss allocations made inside the libstdc++ DLL, so they are only complete for int and probe keys.
- `dict_alloc_bench` assumes 48-byte tree nodes and 32 + 8h-byte skip list nodes for int keys, worked out from the headers. Check them against the `alloc_size` histogram from group C1.

Two tests need changes to the skip list, so they are left to Laith: a read-only count of nodes per level (to test section 6 on the real structure rather than the model), and a pool allocator shared by both structures (also needs Aaron's agreement).

## Follow-up results

Run on 2026-11-03: groups C1, D2, E and G (17.5 minutes), plus `scripts/simulate_skiplist.py`. All 34 unit tests passed first.

### Section 6: the seed spread does shrink (`seeds_spread_ci.png`, `sim_seeds.png`)

**Measured.** Skip list, 200 algorithm seeds, one input seed. Spread of mean node visits per operation (p95 ÷ p5) with 95% bootstrap intervals:

| n | find | insert | erase |
|---|---|---|---|
| 10³ | 1.34 (1.31–1.39) | 1.28 (1.22–1.32) | 1.29 (1.24–1.35) |
| 10⁴ | 1.24 (1.20–1.30) | 1.19 (1.16–1.22) | 1.20 (1.18–1.24) |
| 10⁵ | 1.21 (1.18–1.23) | 1.16 (1.15–1.20) | 1.15 (1.14–1.17) |
| 3·10⁵ | 1.16 (1.15–1.18) | 1.14 (1.12–1.17) | 1.13 (1.12–1.16) |

The model simulation gives the same picture for comparisons per search: 1.25, 1.17, 1.15 and 1.15 at n = 10³ to 10⁶.

**Verdict: the original theory 6 is supported, but the spread shrinks slowly.**
- With 200 seeds, the spread falls steadily at every step, and the intervals barely overlap. The up-and-down pattern from 20 seeds (1.32, 1.11, 1.24, 1.17) was sampling noise.
- The shrinking slows down: the model's spread is almost flat from 10⁵ to 10⁶, at about 1.15.
- In the model, the node counts on the top levels explain 40–57% of the spread across seeds (R²). So the revised theory is partly right: the top of the structure sets a large share of a seed's cost, and that share doesn't fall as n grows. It doesn't explain everything.
- The **time** spreads in D2 are not usable. At 10⁵ and above they reach 1.34–1.69, far wider than the node-visit spreads, and wider than group D's 1.10–1.28 with the same settings. D2 timed one repetition per seed, so this is most likely noise on the machine during the run.

### Section 2: why 1.4 and not 1.5 (`sim_distinct.png`)

**Measured.** The model simulation, with no C++ code involved, gives 1.38–1.42·log₂n distinct nodes per search and 1.87–1.90·log₂n comparisons. The implementation measured 1.38–1.45 and 1.89–2.00.

**Verdict: the formula is off, not the implementation.** The ((1−p²)/p)·log₁/ₚn formula overestimates by about 6%. A likely reason is that it ignores the top levels, where the search starts and makes fewer moves than the formula assumes. That reason is a guess.

### Section 4: sorted builds have better page locality (`locality.png`)

**Measured.** Probe keys, mean per lookup at n = 10⁶:

| | Distinct keys | Cache lines of key bytes | Pages of key bytes |
|---|---|---|---|
| Skip list, uniform build | 28.9 | 28.9 | 25.3 |
| Skip list, sorted build | 29.2 | 29.1 | 15.2 |
| Skip list, reverse build | 29.4 | 29.4 | 15.1 |
| Tree, uniform build | 19.9 | 19.9 | 17.0 |
| Tree, sorted build | 19.8 | 19.8 | 14.4 |

**Verdict: supported, at the level of pages, not cache lines.**
- Every key read sits on its own cache line whatever the build order, so cache lines show no difference.
- After a sorted or reverse build, the skip list's keys share pages: 15 pages per lookup instead of 25, 40% fewer. The tree gains much less, 15% fewer.
- This matches the times: after an ordered build, the skip list's lookups were 17–20% faster, and the tree's barely changed. The mechanism is probably fewer TLB misses (page-translation misses), but the pages are counted here, not the misses.

### Section 2: the large-string gap is in pages (`ratio_sources.png`)

**Measured.** Large-string probe keys, uniform find, skip list ÷ tree:

| n | Distinct keys | Cache lines (node + string buffer) | Pages |
|---|---|---|---|
| 10⁴ | 1.45 | 1.46 | 2.18 |
| 10⁵ | 1.40 | 1.39 | 2.12 |
| 10⁶ | 1.45 | 1.46 | 2.15 |

At 10⁶, the tree reads 1.2 pages per key it compares against, and the skip list 1.8.

**Verdict: the extra cost is explained by pages, not cache lines.**
- Cache lines follow distinct keys exactly, so they don't explain the time ratio (1.51–1.57) being above the distinct-key ratio (about 1.45).
- Pages do: the skip list touches more than twice as many. For the tree, a node and its string buffer are usually on the same page. For the skip list, they usually are not.
- **Speculation:** the Windows heap groups blocks of the same size together. A tree node with a string key (72 bytes) and its 65-byte string buffer probably fall in the same size class, so they end up next to each other. Skip list nodes come in many sizes, one per tower height, so most land in a different size class from their buffer, on a different page. This hasn't been tested.

### Section 3: allocation does not explain the write cost (`alloc_vs_write.png`)

**Measured.**
- Allocation sizes for int keys match the model exactly: every tree node is 48 bytes, and skip list nodes are 32 + 8h bytes (half are 40, a quarter 48, and so on up to 192).
- Every insert allocates exactly once and every erase frees exactly once, in both structures.
- In the steady workload, each write does the same search work as its matching lookup: the comparisons, distinct keys and pages are within 1% of each other.
- The allocator alone (`dict_alloc_bench`):

| n | Allocate, 48-byte blocks | Allocate, tower-sized blocks | Free, 48-byte | Free, tower-sized |
|---|---|---|---|---|
| 10⁴ | 40 ns | 42 ns | 11 ns | 15 ns |
| 10⁵ | 41 ns | 42 ns | 15 ns | 16 ns |
| 10⁶ | 43 ns | 42 ns | 28 ns | 35 ns |

**Verdict: refuted.**
- Allocation alone costs the same for both node shapes, about 42 ns. That matches the extra cost of an insert at small n for both structures, so at small n the extra cost is mostly allocation.
- At 10⁶ the skip list's insert costs 227 ns beyond its search, and the tree's 134 ns. Allocation alone accounts for about 42 ns of each. The 93 ns gap is not allocation in isolation, and not extra search.
- The erase gap (139 vs 42 ns) is likewise far larger than the difference in free times (35 vs 28 ns).

**What is left (speculation).** The benchmark allocates in a tight loop, so the allocator's own bookkeeping stays in cache. In the real workload each allocation follows a search that misses cache. The tree always uses one size class, which stays hot. The skip list spreads its nodes over about 15 size classes, so its allocations may miss on the allocator's bookkeeping. A test: interleave the allocation benchmark with random memory reads to evict the cache, or give both structures the same pool allocator.

### What changed

- **H4 / section 6:** the seed spread does shrink with n, but slowly. The earlier "not supported" verdict came from too few seeds.
- **Section 2:** the formula overestimates by about 6%, and the implementation is consistent with the model.
- **Section 2 (large strings) and section 4:** page counts explain both the sorted-build speed-up and the large-string gap. The heap layout of variable-size nodes looks like the common cause, which is untested.
- **Section 3:** allocation is ruled out as a cost in isolation. The write gap at large n is still unexplained.

## Second follow-up: heap layout and allocator misses

Run on 2026-11-03: groups H (heap layout, 1.8 minutes) and G2 (allocator after cache-evicting reads, 0.7 minutes). All 34 unit tests passed first. These test the two guesses left open above. G2 was re-run with the PC idle, because the first run may have overlapped other use of the machine. The figures below are from the re-run; the first run reached the same conclusion.

### Heap layout explains the page results (`layout.png`, `summary/layout_page_gap.csv`, `summary/layout_strings.csv`)

**Measured.**
- **Where consecutive allocations land.** This is the share of allocations, while inserting int keys, that are within one page of the allocation before. It is the same for every build order and every n from 10⁴:
  - tree: 99.8%
  - skip list: 40%
- **Large strings, lookups at 10⁶, uniform build.** This is the share of keys read whose string buffer is on the same page as the key in its node:
  - tree: 64%
  - skip list: 6%

  Splitting the pages each lookup touches shows why the skip list touches so many:

| | Node pages | Buffer pages | All pages |
|---|---|---|---|
| Tree | 17.9 | 17.8 | 23.6 |
| Skip list | 26.7 | 29.1 | 53.0 |

The tree's node and buffer pages are mostly the same pages. The skip list's barely overlap.

**Verdict: supported.**
- The tree's nodes are all one size, so the heap places them one after another. The skip list's nodes come in many sizes, and a node is only near the one allocated before it 40% of the time. That is close to the 1-in-3 chance that two random towers have the same height.
- This is consistent with the heap keeping blocks of each size in a separate area. The placement is measured; that the Windows heap groups blocks by size is an inference from it, not something read from the heap itself.
- It explains the large-string gap: a skip list key and its buffer are almost always on different pages, so each key read costs about two pages instead of one.
- It probably explains why sorted builds help the skip list more (section 4). After a sorted build, nodes of the same height sit in key order within their area, so neighbouring keys share pages. After a uniform build, nothing does. Skip list node pages per lookup at 10⁶: 26.7 uniform, 14.6 sorted. The link from layout to speed is an inference.

### Allocator misses do not explain the write cost (`alloc_evict.png`, `summary/alloc_evict.csv`)

**Measured.** `dict_alloc_bench --evict R` does R dependent reads through a 64 MiB buffer before each allocation and each free, standing in for a write's search. It then subtracts the time of the reads alone. Median ns per operation at 10⁶:

| Reads before each operation | Allocate, 48-byte | Allocate, tower-sized | Free, 48-byte | Free, tower-sized |
|---|---|---|---|---|
| 0 (group G) | 43 | 42 | 28 | 35 |
| 4 | 38 | 43 | 6 | 7 |
| 16 | 44 | 58 | 8 | 20 |

**Verdict: refuted.**
- With the cache evicted, allocation costs 18–72 ns across all n, and freeing −5 to 44 ns. Neither node shape is consistently dearer: at 10⁶ with 16 reads, tower-sized blocks cost 15 ns more to allocate, but with 4 reads only 6 ns more, and at other n the order flips. The run-to-run noise is about ±20 ns.
- The skip list's write gap at 10⁶ (93 ns for inserts, 97 ns for erases) is far larger than any difference here.
- **Caveats:**
  - Subtracting the reads is approximate, because the CPU can overlap some allocator work with them.
  - The reads go to a separate buffer, not through a real structure.

### What is left

The skip list's write cost at n ≥ 3·10⁵ is still unexplained. These causes are ruled out:
- extra search
- allocation in isolation
- allocation after cache eviction

I don't have a candidate I can defend. The pointers a write changes belong to the predecessors the search just read, and the benchmark already includes writing the new node. Two tests would settle it:
- **Hardware counters.** Count cache and TLB misses per operation with a profiler such as Intel VTune or AMD uProf, whichever matches the CPU.
- **Same pool allocator for both structures.** This means changing both implementations, so it needs Aaron's agreement.
