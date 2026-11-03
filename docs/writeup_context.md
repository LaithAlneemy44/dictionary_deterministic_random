# Write-up context: red-black tree vs skip list

A briefing for an AI assistant helping with the group report. It collects the design, method, results, theory and open questions from the experiment work on branch `key-value`, so you can help without re-deriving them. Numbers are copied from files in the repo; the source file is named with each set so you can check it.

Written 2026-11-04, at commit `68504d3`.

## Ground rules

- **Who owns what.** Aaron Falco wrote the red-black tree (`red_black_tree/`). Laith wrote the skip list (`randomised/skip_list.hpp`). Each is individually assessed on their own algorithm through the code, a recorded video and a written reflection. Don't write either person's reflection or video script, and don't edit the other person's implementation.
- **Fact vs inference.** The results files label claims as **measured**, **verdict** or **speculation**. Keep those labels when you carry a claim into the report. Several plausible explanations were tested and refuted (see "Refuted explanations"), so don't revive them.
- **Style.** Australian English. Plain, direct wording.
- **AI disclosure.** The brief needs per-member disclosure with specific examples of AI being wrong. Laith's examples are in `ai-failures.md`. If you make a mistake that matters, tell your user so they can log it.
- **Predictions.** `docs/predictions.md` is an empty template. The pre-registered predictions are the five hypotheses in `tests/hypotheses.md`, committed in `0dcadf9` before the benchmark results (`412cd3c`). Don't rewrite them; report verdicts against them.

## Where things are

| What | Where |
|---|---|
| Hypotheses (pre-registered) | `tests/hypotheses.md` |
| Explanations for the hypothesis results, with the tests designed to check them | `tests/theories.md` |
| Outcomes of those tests, two follow-up rounds, and what is still open | `tests/theory_results.md` |
| AI mistakes log | `ai-failures.md` |
| Build, workloads, metrics, how to run | `README.md` |
| Raw results | `results/*.csv` |
| Summary tables | `results/summary/*.csv` |
| Plots | `results/plots/*.png` |
| Experiment runner, groups A1–H | `scripts/run_experiments.ps1` |
| Plotting and summary tables | `scripts/plot_results.py` |
| Simulation of the skip list model (not the C++ code) | `scripts/simulate_skiplist.py` |

## The problem and the two structures

**Problem.** An ordered dictionary: map unique keys to values, with insert, lookup (`exists`/`find`/`operator[]`) and erase. Inserting an existing key overwrites its value. Both structures implement the same interface (`core/include/dict/ordered_map.hpp`) with identical semantics, so the comparison is like for like. `std::map` (also a red-black tree) is a reference baseline and test oracle, not one of the compared algorithms.

**Red-black tree** (deterministic, Aaron). Worst-case O(log n) height (at most 2·log₂(n+1)). Insert does at most 2 rotations, erase at most 3; recolouring can propagate upwards (O(log n) worst case, amortised O(1) per insert). Its `find` stops early when it reaches the key. 578 lines.

**Skip list** (randomised, Laith). 293 lines.
- A sorted linked list where each node has a tower of forward pointers. The level-k pointer goes to the next node in key order whose tower reaches level k. If none does, it is null.
- Heights are geometric with p = ½ (a fair coin: promote on heads, stop at the first tails), capped at 32, drawn from a seeded xoshiro256** generator. Counting trailing zero bits of a random integer gives the same distribution up to the cap.
- Levels are zero-indexed: a node of height h is on levels 0 to h − 1.
- The head is a sentinel with a full-height tower and no key.
- Nodes and their towers are one heap allocation of 32 + 8h bytes for int keys (measured).
- Search moves right while the next key is less than the target, drops a level otherwise, records the predecessor at each level and never returns early. Callers check the level-0 successor of the level-0 predecessor.

## Skip list theory (checked in discussion)

**Levels are counted from 0 here.** P(a node reaches level k) = (½)ᵏ, so about n/2ᵏ nodes are on level k. The level-k list is a random half of the level-(k−1) list.

**Expected O(log n) search**, in three steps:
1. **About log₂ n levels.** n/2ᵏ falls to 1 at k = log₂ n. Rigorous version: by the union bound, P(level k is non-empty) ≤ n/2ᵏ, so P(anything above log₂ n + c) ≤ 2⁻ᶜ.
2. **About 2 moves per level** (Pugh's backwards analysis). Trace the search path in reverse from where it ended. At each node, if its tower reaches the level above, the forward search came down from it, so the reverse path goes up; otherwise it came from the left. Each node reaches the next level with probability ½, independently (the geometric distribution is memoryless). So the left moves before each up move are geometric with mean (1 − p)/p = 1. Per level: exactly one down move, plus a random number of right moves with mean 1. That is at most 2 in expectation, since the reverse path can reach the head early. Counting comparisons gives the same: one per right move, plus the failed comparison that causes each drop.
3. **Multiply:** about 2·log₂ n moves in expectation. For general p: (1/p)·log₁/ₚ n.

The expectation is over the coin flips, for every input order. That contrasts with the tree's worst-case guarantee. The extra levels above log₂ n add only a constant.

**Binary search analogy, stated precisely.** Each level narrows an interval known to contain the target, like one binary search comparison. The differences: the split points are whichever nodes were promoted, not the middle, so halving holds only in expectation; and a level takes a random number of right moves, not exactly one. With deterministic promotion (every second node to level 1, every fourth to level 2, ...) each level would need at most one right move and the search would be binary search exactly. Randomness avoids having to maintain that structure on insert and erase. The pointers stand in for binary search's random access.

**Distinct nodes per search.** After a drop, the next node compared is often the one just rejected, which is already loaded. Formula: ((1 − p²)/p)·log₁/ₚ n, which is 1.5·log₂ n for p = ½. Measured and simulated values are about 6% lower (see results, section 2).

**Writes per update.** An insert reads a predecessor at every level but only writes to the predecessors at the new node's own levels, 0 to h − 1. That is 1/(1 − p) = 2 pointer changes in expectation. Erase is the same. The tree also does O(1) writes per update on average (amortised). So the number of writes does not separate the two structures.

## Method

- **Hardware:** AMD Ryzen 5 5600G (6 cores / 12 threads, 32 KB L1d per core, 512 KB L2 per core, 16 MB shared L3), 16 GB DDR4-3200 (2 × 8 GB), Windows 11 Pro.
- **Build:** GCC 16.2 (MSYS2 MinGW), C++20, `-O3 -DNDEBUG`, CMake + Ninja.
- **Two binaries.** `dict_bench` times phases with all counters compiled out. `dict_bench_instrumented` records counters and per-operation histograms; its times are not used.
- **Same input for everyone.** Every structure replays exactly the same operations, generated from a seed before timing starts. Input seeds vary the keys; algorithm seeds vary only the skip list's coin flips. This separates randomness in the algorithm from randomness in the input.
- **Running.** The benchmark is pinned to one core at High priority. Medians with p5–p95 bands are reported, not just means.
- **Sizes:** n = 10³ to 10⁷ for timing (probe-key runs stop at 10⁶).
- **Keys:** int, small_string (fits the small-string buffer, no extra allocation) and large_string (a separate heap buffer per key).
- **Workloads** (`core/include/dict/workloads.hpp`):

| Family | Phases |
|---|---|
| `uniform` | insert n random keys → n lookups (about 50% hits) → erase all in random order |
| `sorted` / `reverse` | insert 0, 2, …, 2n−2 ascending / descending → n lookups (misses fall in the gaps) → erase in the same order |
| `sliding_window` | insert n random keys → n rounds of (insert new, erase oldest) |
| `zipf` | Zipf(s ≈ 1) lookups; the oldest keys are the hottest |
| `zipf_shuffled` | as zipf, but popularity is unrelated to insertion order |
| `steady` | build n keys; with n/4 fresh keys: look up (miss) → insert → look up (hit) → erase. `insert_new − find_miss` and `erase_new − find_new` give a write's cost beyond its search |

- **Duplicate keys.** Random keys can collide (birthday bound); a repeated insert is an update, the same for every structure.
- **Instrumentation:**
  - counters: comparisons, node visits, rotations/recolours
  - probe keys: the distinct keys, cache lines and 4 KiB pages each operation reads, split into node pages and string-buffer pages
  - allocations: count, sizes and page gaps
  - `dict_alloc_bench`: times the allocator alone, optionally after cache-evicting reads (`--evict`)
- **Workload flaw found and fixed.** Sorted and reverse originally used keys 0…n−1 with random 64-bit misses that were all above every key, so every miss took the same cached path. That made the tree's sorted lookups look twice as fast. Results in this document are after the fix.

## Headline results

Median ns per operation, int keys (`results/summary/timing.csv`). Ratio = skip list ÷ tree.

| Workload, phase | n | Tree | Skip list | `std::map` | Ratio |
|---|---|---|---|---|---|
| uniform find | 10³ | 37.9 | 47.9 | 37.4 | 1.26 |
| uniform find | 10⁶ | 544 | 676 | 546 | 1.24 |
| uniform find | 10⁷ | 1002 | 1319 | 966 | 1.32 |
| uniform insert | 10⁶ | 498 | 568 | 523 | 1.14 |
| uniform insert | 10⁷ | 952 | 1187 | 999 | 1.25 |
| uniform erase | 10⁶ | 592 | 665 | 720 | 1.12 |
| uniform erase | 10⁷ | 1123 | 1389 | 1296 | 1.24 |
| sorted find | 10⁶ | 587 | 564 | 551 | 0.96 |
| reverse find | 10⁶ | 542 | 543 | 533 | 1.00 |
| reverse insert | 10⁶ | 70.9 | 48.5 | 69.2 | 0.68 |
| reverse erase | 10⁶ | 45.8 | 82.3 | 44.2 | 1.80 |
| sliding_window churn | 10⁶ | 704 | 765 | 795 | 1.09 |
| zipf find | 10⁶ | 166 | 404 | 284 | 2.44 |
| zipf_shuffled find | 10⁶ | 304 | 414 | 325 | 1.36 |

String keys, uniform find, skip list ÷ tree:
- small_string: 1.42 at 10³, 1.40 at 10⁶, 1.44 at 10⁷
- large_string: 1.35 at 10³, 1.51 at 10⁶, 1.57 at 10⁷

**Tails** (uniform find, int, n = 10⁶):
- Comparisons per lookup (`count_percentiles.csv`):

  | | p50 | p99 | p99.9 | max |
  |---|---|---|---|---|
  | Tree | 30 | 37 | 40 | 46 |
  | Skip list | 39 | 55 | 60 | 74 |
  | `std::map` | 21 | 24 | 24 | 25 |

  The tree's node visits are bounded at 24. The skip list's worst lookup is about 3.7·log₂ n comparisons.
- Latency (`latency.csv`):

  | | p50 | p99 | p99.9 |
  |---|---|---|---|
  | Tree | 566 ns | 1047 ns | 1267 ns |
  | Skip list | 691 ns | 1663 ns | 2249 ns |

  The skip list is slower at every percentile.

**Restructuring per insert** (`restructuring.csv`):

| | Uniform insert | Sorted or reverse insert |
|---|---|---|
| Tree | 0.58 rotations, 2.3 recolours | 1.0 rotation, 5.0 recolours |
| Skip list | 2.0 pointer splices | 2.0 pointer splices |

**Memory** (`memory.csv`, int keys):
- Node bytes per key: 48 for both (tree nodes are a fixed 48 bytes; skip list nodes average 32 + 8·2).
- The harness's estimate including allocator overhead: tree 64, skip list 58.7. Large strings: 160 vs 165.3.

**Code size:** skip list 293 lines, tree 578 (whole headers, including comments).

## Hypotheses and verdicts

Hypotheses from `tests/hypotheses.md`. Verdicts summarise `tests/theories.md` and `tests/theory_results.md`.

1. **"The tree is slower because of recolouring and rebalancing; the gap stays similar."** Refuted on the first part: the tree is faster in most regimes.
   - The skip list takes about 2·log₂ n steps by design, against about log₂ n for the tree.
   - The int time gap stays roughly flat (1.24–1.32).
   - The large-string gap grows with n.
2. **"The skip list is better at p50 and p99, not p99.9."** Refuted. It is worse at every percentile. Its median is set by the 2·log₂ n expectation.
3. **"Input order matters more for the tree, through rotations."** Refuted as stated; restated.
   - Order matters a lot for both structures, through cache reuse of the search path, not rotations.
   - Sorted inserts give the tree its most rotations, yet are about 6× faster than uniform inserts (80 vs 498 ns at 10⁶).
   - New finding: after a sorted or reverse build, the skip list's lookups are 17–20% faster than after a random build, and match or beat the tree.
4. **"Seed matters more than input order for the skip list."** Main claim supported; the stated mechanism was wrong.
   - The spread across algorithm seeds exceeds the spread across input seeds at every n.
   - Each node is exactly one allocation whatever the seed, so "more allocations" is not a mechanism.
   - With 200 seeds, the spread shrinks with n but slowly: p95 ÷ p5 of mean node visits per find is 1.34 at 10³ and 1.16 at 3·10⁵.
   - In the simulation, the node counts on the top few levels explain 40–57% of a seed's cost (R²).
5. **"The gap is larger for insert and erase than for lookups."** Refuted. Up to 10⁶ the gap is smallest on writes (insert 1.14, erase 1.12, find 1.24 at 10⁶).
   - The tree's rebalancing is cheap: it touches nodes on the path just searched, which are still in cache.
   - Allocation (about 42 ns for both) dilutes the relative gap.

## What the follow-up tests established

1. **Steps.** Per lookup ÷ log₂ n, uniform:
   - skip list: 1.88–2.04 node visits
   - tree: 0.98–1.00 node visits, 1.48–1.51 comparisons. The tree compares `<` then `>`, so it makes up to two comparisons per node.
   - `std::map`: 1.07–1.12 comparisons. It descends to a leaf with one comparison per node.
2. **Time follows distinct nodes loaded, not steps.**
   - The skip list loads 1.38–1.45·log₂ n distinct nodes. The model simulation, with no C++ code involved, gives 1.38–1.42. So the formula's 1.5 overestimates by about 6% and the implementation matches the model. A likely reason is that the formula ignores the top levels; that reason is a guess.
   - The int time ratio (about 1.3) sits near the comparison ratio.
3. **Heap layout explains the page-level results.** Measured via allocation page gaps:
   - Consecutive allocations land within one page of each other 99.8% of the time for the tree, 40% for the skip list. Skip list nodes come in many sizes, one per tower height.
   - Inference, not read from the heap: the Windows heap keeps each block size in its own area.
   - This explains two results:
     - **The large-string gap.** A key's buffer shares a page with its node 64% of the time for the tree, 6% for the skip list. Pages per lookup at 10⁶: tree 23.6, skip list 53.0. Cache lines track distinct keys exactly, so the extra cost is in pages, probably TLB misses (pages are counted; misses are not).
     - **The sorted-build speed-up.** Skip list node pages per lookup at 10⁶: 26.7 after a uniform build, 14.6 after a sorted build. The link from layout to speed is an inference.
4. **Zipf.** The tree's 2.4× win came from the workload. The hottest keys were the oldest, and old keys sit near the root, where the tree's early-exit `find` stops. Under `zipf_shuffled` the tree's node visits return to about log₂ n and its lead falls to 1.36×, close to its uniform lead. The skip list can't benefit because heights don't depend on insertion time. Report both workloads, or `zipf_shuffled` as the fair one.

## Refuted explanations

Tested and ruled out. Don't use them in the report except as refuted hypotheses.
- **"Skip list writes touch predecessors off the cached search path."** Impossible: every predecessor is a node where the search dropped down, so it was just read. This was an AI mistake, logged in `ai-failures.md`.
- **"The skip list's extra write cost at large n is allocation."** The allocator alone costs the same for both node shapes (about 42 ns to allocate). Every insert allocates once and every erase frees once.
- **"It is allocator bookkeeping missing cache after the search."** With cache-evicting reads before each operation (`--evict` 4 and 16), neither node shape is consistently dearer, within about ±20 ns noise. That is far below the 93 ns gap.
- **"Writes do more search than lookups."** In `steady`, a write's comparisons, distinct keys and pages are within 1% of its matching lookup.

## Still open

1. **The skip list's write cost at n ≥ 3·10⁵.**
   - Up to 10⁵ both structures' writes cost about the same beyond their search: insert extra 54 vs 63 ns at 10⁵.
   - At 10⁶ it is 134 ns for the tree vs 227 ns for the skip list (insert), and 42 vs 139 ns (erase).
   - Unexplained after the tests above.
   - **Speculation, unchecked:** the jump is around where the structure outgrows the 16 MB L3.
   - **Would settle it:** hardware counters (cache and TLB misses per operation, AMD uProf on this CPU), or the same pool allocator for both structures. The pool allocator changes both implementations and needs both authors' agreement.
2. **Is the tree's cache advantage the algorithm or the allocator?** Layout is measured to differ. A shared pool or arena allocator would separate the two.
3. **The skip list's tail within a run** (99.9th percentile) and how fast the seed spread shrinks. It is almost flat from 10⁵ to 10⁶ in the simulation, at about 1.15.
4. **Whether the sorted-build advantage holds** for nearly-sorted or append-heavy inputs.

**Limits to state in the report:**
- Everything ran on one machine.
- Probe-key line and page counts include key bytes only, so they are lower bounds.
- Allocation counts miss allocations inside the libstdc++ DLL, so they are complete only for int and probe keys.
- The D2 timing spreads (one repetition per seed) are too noisy to use; its node-visit spreads are fine.
- `std::map` is in the timing, counts and memory results, but not in the layout (H) or allocator (G, G2) tests.

## Possible extensions (future work)

Label these as untested.
- **Concurrency.** The main reason skip lists are used in practice (Java's `ConcurrentSkipListMap`, LevelDB/RocksDB memtables). Both structures do O(1) writes per update on average. The difference is their shape: a skip list's changes are independent single-pointer swaps, and the node can be linked bottom-up with compare-and-swap; it is in the set once linked at level 0. A tree rotation changes several pointers that must change together, and the fix-up location isn't known in advance. Single-threaded results say nothing about this.
- **Tuning p.** Per-level cost 1/p, levels log₁/ₚ n, pointers per node 1/(1 − p). The distinct-node formula predicts p = ¼ reads more distinct nodes (1.875·log₂ n vs 1.5).
- **Skipping repeated comparisons.** After a drop, the skip list often compares against the node it just rejected. Remembering it would save about 0.5·log₂ n comparisons per lookup, which matters most for string keys.
- **Cache-conscious variants.** Unrolled skip lists, or a B-tree such as Abseil's `btree_map`, to test the finding that layout matters more than step count.
- **Ordered operations not benchmarked:** range queries, in-order iteration, rank queries (an indexable skip list).
- **Other hardware**, since sections 2–4 and the write cost are cache-dependent.
