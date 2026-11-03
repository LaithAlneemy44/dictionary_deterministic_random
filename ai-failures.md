# AI failures

Examples of AI suggestions that were wrong or misleading, for the AI-use disclosure.

## 2026-11-02: Ambiguous description of what the skip list search returns

**Person:** Laith

**Tool:** Claude Code (Claude Opus 5.5)

**Context:** Replacing the `node->data == element` check in `SkipList::insert` with a comparison that only uses less than.

**What the AI said:** "The node it stops in front of at level 0 ... is the first key that is not less than `element`." It did not say that `search` itself returns the predecessor, or that `insert` moves one node forward (`node = node->next()[0]`) before the check.

**What was wrong:** The wording blurred two different nodes:
- `search` returns the *last* node whose key is less than `element` (or the head).
- The node checked in `insert` is the next node after that, which is the first key not less than `element`.

Reading the explanation as describing `search`'s return value made it look incorrect. The conclusion about the node being checked was right, but the explanation did not make clear which node it meant.

**How it was resolved:** Laith challenged it, saying the search returns a node less than the element. The AI checked `skip_list.hpp` and confirmed:
- `search` returns the last node less than `element`, not the first.
- Line 66 then moves one step forward, to the first key not less than `element`.
- At that node, `!less_(element, *node->data)` is the only check still needed for equality.

**Lesson:** When an explanation depends on which variable or node is meant, check it against the code and name it exactly, e.g. "the value `search` returns" or "`node` after line 66".

## 2026-11-03: Wrong explanation for the skip list's expensive writes

**Person:** Laith

**Tool:** Claude Code (Claude Opus 5.5)

**Context:** Explaining why, in the `steady` workload, the skip list's cost of a write beyond its search was much higher than the red-black tree's from n = 3·10⁵ (insert 227 vs 134 ns, erase 139 vs 42 ns at 10⁶). Written into section 3 of `tests/theory_results.md`.

**What the AI said:** A skip list write touches the predecessor at every level of the new node's tower, some of which "are not on the cached search path", so each is a cache miss once the structure outgrows the cache.

**What was wrong:** The predecessor at each level is, by definition, the node where the search dropped down a level. The search has just read that node's key and its forward pointer at that level. So every pointer a write changes was read moments before and should still be in cache. The explanation contradicted how the search works.

**How it was resolved:** The AI noticed the contradiction itself when asked how to test the inconclusive results, before any test was run. Section 3 was rewritten to name allocation and freeing (variable-size skip list nodes vs fixed-size tree nodes) as the remaining suspect, marked as untested, and a test was written for it (allocation counts per operation and `dict_alloc_bench`).

**Lesson:** Check a performance explanation against the algorithm's own invariants before writing it down. "Not on the search path" was ruled out by the definition of a predecessor.

## 2026-11-03: Simulation regression crashed on mismatched columns

**Person:** Laith

**Tool:** Claude Code (Claude Opus 5.5)

**Context:** `scripts/simulate_skiplist.py`, written by the AI, regresses each seed's search cost on the node counts at the top levels of the skip list, to see how much of the spread across seeds they explain.

**What the AI did:** Named the count columns by absolute level (level 9, level 10, ...). The top levels differ with n, so each n got a different set of columns. After the results were combined, the missing columns were NaN, and the least-squares fit failed with `LinAlgError: SVD did not converge`.

**What was wrong:** The columns had to mean the same thing at every n. Columns named by absolute level don't, because a skip list over 10³ keys has fewer levels than one over 3·10⁵.

**How it was resolved:** The script crashed on its first run, before any numbers were reported. The AI renamed the columns by offset from log₂n (`count_log2n_minus_k`) and re-ran it.

**Lesson:** When results from different n are combined into one table, check that every column means the same thing at every n.

## 2026-11-03: Wrong date on the follow-up results

**Person:** Laith

**Tool:** Claude Code (Claude Opus 5.5)

**What the AI did:** Wrote "Run on 2026-11-04" in `tests/theory_results.md` and its memory notes. The tests ran on 2026-11-03.

**How it was resolved:** The AI noticed when the result file timestamps showed 3 November, and corrected the date before pushing.

**Lesson:** Take dates from the system or file timestamps, not from the AI's assumption.
