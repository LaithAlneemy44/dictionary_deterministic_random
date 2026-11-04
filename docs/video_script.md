# Video script (sample)

Target length: about 11:45, under the 12-minute limit. Timings assume speaking at about 140 words a minute.

| Segment | Speaker | Time | Show on screen |
|---|---|---|---|
| 1. Problem and algorithms | Laith | 1:45 | Title slide, then a two-column slide: tree vs skip list |
| 2a. Deep dive: red-black tree | Aaron | 2:30 | Tree diagrams of the insert and delete cases |
| 2b. Deep dive: skip list | Laith | 2:30 | Skip list diagram with the search path highlighted |
| 3. Results | Aaron | 2:45 | `time_uniform.png`, `path_length.png`, `write_cost.png`, `ccdf_latency_uniform_int.png` |
| 4. Most surprising finding | Laith | 1:45 | `locality.png`, `layout.png` |
| 5. Closing | Aaron | 0:30 | One slide |

Segments 1, 3, 4 and 5 are written out in full. Segments 2a and 2b are outlines only: they are where each of us is individually assessed on our own understanding, so each author should put them in their own words. The beats and facts are there to check against.

---

## 1. Problem and algorithms (Laith, 1:45)

> We're comparing two ways to build an ordered dictionary: a structure that stores key–value pairs in sorted order and supports insert, lookup and erase. It's what `std::map` gives you.
>
> The obvious options each fail somewhere. A sorted array gives fast binary search, but inserting means shifting everything: O(n). A linked list inserts in constant time once you're in position, but you can't binary search it, so finding the position is O(n). A binary search tree gets both to O(log n), but only while it stays balanced, and inserting keys in sorted order turns it back into a linked list.
>
> There are two classic fixes, and that's our comparison. A **red-black tree** keeps itself balanced deterministically: it colours each node red or black and repairs the colouring with rotations after every update, so every operation is O(log n) in the worst case. A **skip list** takes a sorted linked list and adds express lanes. Each node gets a random height by flipping a coin, and higher levels skip over more of the list. Nothing is ever rebalanced; randomness keeps it balanced in expectation.
>
> So the question is: deterministic guarantees against randomised simplicity. Both are O(log n) on paper. We wanted to know which is faster in practice, how much their costs vary, and why.
>
> Aaron wrote the red-black tree and I wrote the skip list. Both are textbook versions in C++, behind the same interface, and both replay exactly the same operations in our benchmarks.

---

## 2a. Deep dive: red-black tree (Aaron, 2:30) — outline only

Aim: "here's the invariant, here's why this case is needed, here's what would break without it".

1. **The invariant.** The two properties that matter: no red node has a red child, and every root-to-leaf path has the same number of black nodes. Explain why together they force height ≤ 2·log₂(n+1): the shortest path is all black, and the longest can at most alternate red and black.
2. **Why insert a red node.** A red leaf keeps every black height unchanged, so property 5 survives. The only possible damage is a red–red pair. *What would break:* inserting it black would make one path longer in black nodes than every other path, which is much harder to repair.
3. **The insert cases.** Uncle red → recolour, and the problem moves two levels up. Uncle black → at most two rotations, and it stops. Why the recolour case can climb, and why insert still does at most 2 rotations.
4. **Delete, the hard part.** Removing a black node leaves paths one black short (the "extra black"). The four sibling cases, and which ones end the fix-up. At most 3 rotations.
5. **The sentinel NIL.** The delete fix-up reads the colour of, and sets the parent of, a node that may be a leaf. A shared black sentinel makes that safe without null checks. *What would break:* a null pointer there means dereferencing null, or a special case in every fix-up branch.
6. **How it was checked.** The invariant checker (black heights, ordering, parent links), run after every deletion over all insertion and deletion orders of six keys.

Pick the two or three of these that are most interesting to explain well, rather than all six briefly.

---

## 2b. Deep dive: skip list (Laith, 2:30) — outline only

Aim: the same: invariant, why each case is needed, what breaks without it.

1. **The invariant.** Every level is a sorted linked list, and each level is a sub-list of the one below. The level-k pointer goes to the next node whose tower reaches level k. Show a diagram with four levels.
2. **The search and its predecessors.** Move right while the next key is less than the target, otherwise drop a level, and record the node where you dropped at every level. Never stop early. *Why:* insert and erase need the predecessor at every level the node occupies, and search returns the strict predecessor, not the node itself. The duplicate check is then one comparison on the level-0 successor. This links to the AI mistake in the report: the explanation blurred those two nodes.
3. **Erase only unlinks where the predecessor points to the target.** *What would break:* at a level above the target's height, the predecessor points to some other node. Unlinking there would cut that node out of the level.
4. **Why it's O(log n) without rebalancing.** About log₂n levels, because each level holds about half the nodes of the one below. About two moves per level, by the backwards analysis: tracing the path in reverse, each node's coin flip decides up or left with probability ½. So about 2·log₂n steps, which is exactly what we measured (1.88–2.04·log₂n).
5. **The worst case.** O(n), but only with bad coin flips, and no input order can cause it, because heights don't depend on the keys. One caveat: heights follow insertion order and our seed is fixed, so someone who knew the seed could choose which keys get tall towers.

Again, two or three of these done well beats all five rushed. Points 2–3 cover "what breaks without it", and point 4 covers the theory.

---

## 3. Results (Aaron, 2:45)

**[Show `time_uniform.png`]**

> Our first finding contradicted our main prediction. We expected the red-black tree's rebalancing to make it the slower structure. Instead the tree is faster on uniform random input in every phase, at every size and with every key type. At a million integer keys, the skip list is 1.24 times slower on lookups, 1.14 on inserts and 1.12 on erases.

**[Show `path_length.png`]**

> The step counts explain why. A tree lookup visits about log₂n nodes, which is what a balanced tree should do. The skip list visits about twice that, 2·log₂n, at every size. That isn't bad luck; it's the design. A skip list search moves right about once per level before dropping down, so twice as many steps is built in.
>
> But the time gap is only about 1.3×, not 2×. Two things close it. The tree, with only a less-than comparison, makes up to two comparisons per node. And when the skip list drops a level, the next node it compares is often the one it just rejected, which is already in cache. So it only loads about 1.45·log₂n distinct nodes.

**[Show `write_cost.png`]**

> Our second prediction was that the gap would be biggest on inserts and erases, where the tree rebalances. It's the opposite: the gap is smallest on writes. A random insert causes only 0.58 rotations and 2.3 recolourings on average, and they all touch nodes the search has just visited, so they're already in cache. Rebalancing is cheap.
>
> There's one exception, which we'll come back to. From about 300,000 keys, the skip list's writes get much more expensive beyond their search: 227 nanoseconds against the tree's 134 at a million keys.

**[Show `ccdf_latency_uniform_int.png`]**

> Third, the tails. We expected the skip list to win at the median and lose only in the extreme tail. The tree is faster at every percentile: 566 against 691 nanoseconds at the median, and 1,267 against 2,249 at the 99.9th. The skip list's worst lookup made 74 comparisons, about 3.7·log₂n. That's a heavier tail, but nothing close to the O(n) worst case.
>
> And our prediction about randomness held, but for a different reason. The skip list's own random seed does change its cost more than the input does. With 200 seeds, that spread shrinks as n grows, because a search is a sum of many small random steps, and those average out.

---

## 4. Most surprising finding (Laith, 1:45)

**[Show `locality.png`]**

> The most surprising result has nothing to do with either algorithm's textbook analysis.
>
> If you build the skip list by inserting keys in sorted order, its lookups get 17–20% faster than after a random build, fast enough to match the tree. But it reads exactly the same number of nodes either way: about 29 per lookup. So the work is the same, and the time isn't.
>
> We counted the memory each lookup touches. Cache lines didn't change. Memory pages did: after a sorted build, a lookup touches 15 pages instead of 25.

**[Show `layout.png`]**

> The cause is node size. Every tree node is 48 bytes, and 99.8% of tree allocations land right next to the previous one. A skip list node's size depends on its random height, so the nodes come in many sizes, and only 40% land next to the previous one. That's close to the one-in-three chance that two random towers have the same height. The heap seems to keep each size in its own area, so after a sorted build, nodes of the same height sit in key order and neighbours share pages.
>
> The same effect explains why long string keys hurt the skip list most: a tree node usually shares a page with its string, and a skip list node almost never does.
>
> So the random heights that make the skip list simple also scatter it through memory. We measured the placement directly. That the speed difference comes from page-translation misses is our best explanation, not something we measured.

---

## 5. Closing (Aaron, 0:30)

> One cost is still unexplained: the skip list's expensive writes beyond 300,000 keys, after we ruled out extra searching and allocation. Next, we'd count cache and TLB misses with hardware counters, and give both structures the same allocator, to separate the algorithm from how its memory is laid out. Thanks for watching.
