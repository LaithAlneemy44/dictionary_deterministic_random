# CLAUDE.md — Skip list (41052 Assignment 2)

## Context

- UTS 41052 Advanced Algorithms, Programming Assignment 2: a group comparison of different algorithms for the same problem.
- Problem: ordered dictionary (search, insert, erase by key).
- Comparison: deterministic balanced search tree vs randomised structures.
- Laith is the **primary author of the skip list**. He is individually assessed on it through the code, a recorded video deep-dive, and a written reflection.
- Other group members own the other implementations. Do not edit their modules unless asked.
- TODO: partner algorithms and their authors, repo layout, build system, compiler and flags.

## How to help — read this first

Laith is writing the skip list himself to understand it deeply. Default behaviour in `skiplist/`:

- **Do not write or rewrite skip list implementation code** unless he explicitly asks for code.
- Review code he writes: point out bugs, undefined behaviour, and design problems, with the reason for each.
- Explain concepts from first principles, including why alternatives are worse.
- Prioritise issues by severity, correctness first.
- Small syntax illustrations using generic types are fine. Solutions to his skip list are not.

Writing code is fine for supporting work when asked: benchmarking harness, input generators, test scaffolding, plotting scripts, build files.

## Design decisions already made

- **Language:** C++, templated as `SkipList<T>`. Assumes C++20.
- **Semantics:** set or map, unique keys. Inserting an existing key does nothing or assigns. This must match the BST so the comparison is fair.
- **Storage:** nodes own their data by value. Keys are immutable to protect the ordering invariant.
- **Node:** a struct with public members, declared in the private section of `SkipList`. Each node has a tower of forward pointers whose size is its height.
- **Head:** a sentinel holding only a tower, with no `T`. Approaches discussed: a base struct holding the tower with nodes deriving from it, or recording forward-pointer slots instead of nodes. The head is never compared and never pointed to.
- **Levels:** `static constexpr` maximum level of 32, p = 1/2. Track the current level count separately and start searches there. Shrink it on erase when the top level empties. Level 0 always exists.
- **Search:** move right while the next key is strictly less than the target, record the predecessor at every level before dropping down, never return early. Callers check the level-0 successor of the level-0 predecessor.
- **Predecessors:** fixed-size array indexed by level, on the stack. Not `std::stack`, which allocates.
- **Memory:** nodes allocated with `new` because their lifetime spans calls. The destructor walks level 0 only. Copy operations deleted or deep-copied, per the rule of three/five.
- **Invalid heights:** internal bug, so use `assert`, not exceptions.

## Known pitfalls to watch for

- `const` vs pointer-to-const on forward pointers. Links must be mutable.
- `constexpr` on non-static members and `constinit` on members are invalid.
- Signed/unsigned mixing in level counters, and `size_t` loops with `>= 0`.
- String literal plus integer is pointer arithmetic, not concatenation.
- Erase must only unlink at levels where the predecessor actually points to the target.
- No `using namespace std` in headers. Include what is used.

## Benchmarking notes

- Same input instances for every structure.
- Random keys collide by the birthday bound, roughly n² / 2M pairs. Either generate distinct keys or treat collisions as updates, consistently across structures, and state the choice in the report.
- Seed the RNG for reproducibility. Measure distributions, not just means, since the skip list's cost is random.
- Measure the operations, not I/O or generation.

## AI-use disclosure

The assignment requires per-member disclosure and specific examples of AI being wrong or misleading. When an AI suggestion turns out to be wrong, note it in `AI_LOG.md` with the date, what was suggested, what was wrong, and how it was resolved.

## Style

- Australian English. Plain, direct wording.
- Distinguish facts, assumptions and speculation.
