1. Red black tree will perform worse on average than skip list due to higher implementation overhead of recolouring and rebalancing the tree after insert/erase. However, since both are built upon linkedlists, have the same average and worst-case time complexity, the gap between the two will stay similar.

2. Since skip lists' performance requires a good distribution of levels, which is determined by randomness. Predicting that skip list performs better at CI 50, 99, but not at 99.9.

3. Input order matters more for the red-black tree, as suboptimal order creates more rotations, which may have a cascading effect up the tree for large heights.

4. Seed has a larger effect on the skip list's performance rather than input order, as suboptimal randomness can cause more memory allocations, as well as poor height distribution, causing the time complexity to be closer to O(n) rather than O(logn). Input order has a miniscule effect given well distributed heights.

5. The gap for insertions and erase is larger than for exists. This is because most of the overhead in the red-black tree is due to rotations when inserting or erasing keys.

