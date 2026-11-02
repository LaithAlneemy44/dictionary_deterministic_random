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
