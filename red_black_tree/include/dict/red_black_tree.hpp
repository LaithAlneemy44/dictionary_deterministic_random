#pragma once

// Red-black tree (deterministic balanced BST).
//
// Primary author: Aaron Falco
//
// Follows CLRS (Introduction to Algorithms, 3rd ed., ch. 13): bottom-up insert
// and delete with parent pointers and a single black sentinel node `nil_`
// standing in for every leaf and for the root's parent.
//
// STATUS: complete.
//
// Red-black properties (CLRS 13.1), checked by check_invariants():
//   1. Every node is red or black.
//   2. The root is black.
//   3. Every leaf (nil_) is black.
//   4. A red node has two black children.
//   5. Every path from a node down to a leaf has the same number of black nodes.
// Together these keep height <= 2 lg(n + 1).
//
// Counting (see dict/counters.hpp):
//   comparisons  - every call to the comparator
//   node_visits  - every real node visited on a search path
//   structural   - every rotation
//   recolors     - every colour change
//   allocations  - every node allocated
// In-order traversal, clear() and check_invariants() are not counted.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "dict/counters.hpp"

namespace dict {

template <class K, class V, class Compare = std::less<K>>
class RedBlackTree {
public:
    using key_type = K;
    using mapped_type = V;

    static constexpr const char* name = "red_black_tree";
    static constexpr bool implemented = true;

    RedBlackTree() {
        nil_.left = nil_.right = nil_.parent = &nil_;
        nil_.color = Color::Black;
    }

    ~RedBlackTree() { clear(); }

    // Every node's leaf links point at this tree's own nil_, so a copied or
    // moved tree would still point into the original object. Copying and
    // moving are disabled. (Construction from a prvalue, as in make_map, is
    // still fine: C++17 guaranteed copy elision builds the object in place.)
    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;
    RedBlackTree(RedBlackTree&&) = delete;
    RedBlackTree& operator=(RedBlackTree&&) = delete;

    // Insert key -> value, or overwrite the value if key is already present.
    // Returns true if the key was new.
    //
    // CLRS RB-INSERT: an ordinary BST insert that colours the new node red,
    // followed by insert_fixup() to repair the one property that can break.
    // Why red: a new red node keeps every black height unchanged (property 5).
    // The only possible violation is a red node with a red parent (property 4),
    // or a red root (property 2) when the tree was empty.
    bool insert(const K& key, const V& value) {
        NodeBase* parent = &nil_;
        NodeBase* x = root_;
        bool went_left = false;
        while (x != &nil_) {
            DICT_COUNT(counters_, node_visits, 1);
            parent = x;
            Node* n = as_node(x);
            DICT_COUNT(counters_, comparisons, 1);
            if (less_(key, n->key)) {
                x = x->left;
                went_left = true;
                continue;
            }
            DICT_COUNT(counters_, comparisons, 1);
            if (less_(n->key, key)) {
                x = x->right;
                went_left = false;
                continue;
            }
            n->value = value;  // key already present: overwrite, structure unchanged
            return false;
        }

        Node* z = new Node(key, value);
        DICT_COUNT(counters_, allocations, 1);
        z->parent = parent;
        z->left = z->right = &nil_;
        z->color = Color::Red;
        if (parent == &nil_) {
            root_ = z;
        } else if (went_left) {
            parent->left = z;
        } else {
            parent->right = z;
        }
        ++size_;
        insert_fixup(z);
        return true;
    }

    // Standard BST search. A three-way decision with only `<` available costs
    // up to two comparisons per node: key < x, then x < key (equal if neither).
    const V* find(const K& key) const {
        const NodeBase* x = root_;
        while (x != &nil_) {
            DICT_COUNT(counters_, node_visits, 1);
            const Node* n = as_node(x);
            DICT_COUNT(counters_, comparisons, 1);
            if (less_(key, n->key)) {
                x = x->left;
                continue;
            }
            DICT_COUNT(counters_, comparisons, 1);
            if (less_(n->key, key)) {
                x = x->right;
                continue;
            }
            return &n->value;
        }
        return nullptr;
    }

    bool contains(const K& key) const { return find(key) != nullptr; }

    // Remove key. Returns true if it was present.
    //
    // CLRS RB-DELETE. Let z be the node holding key. Some node y leaves its
    // position in the tree and some node x (possibly nil_) moves into y's old
    // position:
    //   - z has at most one child: y = z, and x is z's only child (or nil_).
    //   - z has two children: y = z's successor, the minimum of z's right
    //     subtree. y has no left child, so it is removed from its own spot
    //     (x = y->right takes it) and then relinked into z's place, taking z's
    //     colour. Relinking the node, rather than copying y's key into z,
    //     matters here: `key` is const, and copying would also invalidate the
    //     value pointer that find() handed out for y.
    // If y was red, removing it from its old spot changes no black height and
    // cannot create a red-red pair, so nothing needs fixing. If y was black,
    // every path through x has lost one black node, and delete_fixup(x)
    // repairs it.
    bool erase(const K& key) {
        NodeBase* z = root_;
        while (z != &nil_) {
            DICT_COUNT(counters_, node_visits, 1);
            Node* n = as_node(z);
            DICT_COUNT(counters_, comparisons, 1);
            if (less_(key, n->key)) {
                z = z->left;
                continue;
            }
            DICT_COUNT(counters_, comparisons, 1);
            if (less_(n->key, key)) {
                z = z->right;
                continue;
            }
            break;
        }
        if (z == &nil_) return false;

        NodeBase* y = z;
        Color y_original_color = y->color;
        NodeBase* x;
        if (z->left == &nil_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == &nil_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            y_original_color = y->color;
            x = y->right;
            if (y->parent == z) {
                // y stays as z's right child, so x stays under y. Set this
                // even when x is nil_: delete_fixup starts from x and reads
                // x->parent, so the sentinel must point back at y.
                x->parent = y;
            } else {
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            set_color(y, z->color);
        }
        delete as_node(z);
        --size_;
        if (y_original_color == Color::Black) delete_fixup(x);
        return true;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Frees every node in O(n) time and O(1) extra space. Whenever the current
    // node has a left child, rotate that child up (right rotation); once there
    // is no left child, the node can be freed and we move to its right child.
    // Each rotation moves one node permanently onto the "right spine", so
    // there are at most n rotations, and no stack or recursion is needed.
    // Colours and parent pointers are ignored because every node is destroyed.
    void clear() {
        NodeBase* x = root_;
        while (x != &nil_) {
            if (x->left != &nil_) {
                NodeBase* l = x->left;
                x->left = l->right;
                l->right = x;
                x = l;
            } else {
                NodeBase* r = x->right;
                delete as_node(x);
                x = r;
            }
        }
        root_ = &nil_;
        nil_.parent = &nil_;
        size_ = 0;
    }

    // Visit every (key, value) in ascending order using parent pointers
    // (successor walk), so no stack is needed.
    template <class F>
    void for_each(F&& visit) const {
        if (root_ == &nil_) return;
        for (const NodeBase* x = minimum(root_); x != &nil_; x = successor(x)) {
            const Node* n = as_node(x);
            visit(n->key, n->value);
        }
    }

    // Visit every (key, value) with lo <= key <= hi in ascending order: find
    // the first key >= lo (lower bound), then walk successors until key > hi.
    template <class F>
    void for_each_in_range(const K& lo, const K& hi, F&& visit) const {
        const NodeBase* first = &nil_;
        for (const NodeBase* x = root_; x != &nil_;) {
            if (less_(as_node(x)->key, lo)) {
                x = x->right;
            } else {
                first = x;  // candidate; a smaller one may exist on the left
                x = x->left;
            }
        }
        for (const NodeBase* x = first; x != &nil_ && !less_(hi, as_node(x)->key); x = successor(x)) {
            const Node* n = as_node(x);
            visit(n->key, n->value);
        }
    }

    // Verifies all five red-black properties, strict BST ordering, parent
    // links, and that the number of reachable nodes equals size().
    bool check_invariants() const {
        if (nil_.color != Color::Black) return false;  // property 3
        if (root_ == &nil_) return size_ == 0;
        if (root_->color != Color::Black) return false;  // property 2
        if (root_->parent != &nil_) return false;
        std::size_t count = 0;
        if (check_subtree(root_, nullptr, nullptr, count) < 0) return false;
        return count == size_;
    }

    const OpCounters& counters() const { return counters_; }
    void reset_counters() { counters_ = OpCounters{}; }

private:
    enum class Color : std::uint8_t { Red, Black };

    // Links and colour live in a base struct so the sentinel can exist
    // without a key or value. That way K and V need not be
    // default-constructible. Real nodes derive from it and add the payload.
    struct NodeBase {
        NodeBase* left;
        NodeBase* right;
        NodeBase* parent;
        Color color;
    };

    struct Node : NodeBase {
        Node(const K& k, const V& v) : NodeBase{}, key(k), value(v) {}
        const K key;  // never changes after insertion, which protects the BST ordering
        V value;
    };

    // Downcast a link to a real node. Never call this on &nil_.
    static Node* as_node(NodeBase* x) { return static_cast<Node*>(x); }
    static const Node* as_node(const NodeBase* x) { return static_cast<const Node*>(x); }

    // Changes a node's colour, counting it only if the colour actually changes.
    void set_color(NodeBase* x, Color c) {
        if (x->color != c) {
            DICT_COUNT(counters_, recolors, 1);
            x->color = c;
        }
    }

    // Rotations (CLRS 13.2). Each rewires three links (x's child slot, the
    // child's parent, and x's parent's child slot) and preserves in-order key
    // order. Left rotation lifts x's right child y above x:
    /*
              x                y
             / \              / \
            a   y     =>     x   c
               / \          / \
              b   c        a   b
    */
    // Subtree b changes parents (y -> x). The `!= &nil_` guard stops us from
    // writing to the sentinel's parent pointer for no reason.
    void left_rotate(NodeBase* x) {
        DICT_COUNT(counters_, structural, 1);
        NodeBase* y = x->right;
        x->right = y->left;
        if (y->left != &nil_) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == &nil_) {
            root_ = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    // Mirror image of left_rotate: lifts x's left child above x.
    void right_rotate(NodeBase* x) {
        DICT_COUNT(counters_, structural, 1);
        NodeBase* y = x->left;
        x->left = y->right;
        if (y->right != &nil_) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == &nil_) {
            root_ = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }
        y->right = x;
        x->parent = y;
    }

    // CLRS RB-INSERT-FIXUP. Loop invariant: z is red, and the only possible
    // violation is z's parent also being red (or z being a red root).
    //
    // While the parent p is red, p cannot be the root (the root is black), so
    // the grandparent g is a real node, and g is black (p is red, and the tree
    // was valid before this insert). The case is decided by the uncle u (g's
    // other child):
    //
    //   Case 1 - u red: recolour p and u black and g red. Black heights through
    //     g are unchanged, but g may now have a red parent, so continue from g.
    //     The problem moves two levels up; no rotation.
    //   Case 2 - u black, z is an "inner" child (z and p on opposite sides):
    //     rotate at p so z becomes an "outer" child, which reduces it to case 3.
    //   Case 3 - u black, z is an "outer" child: colour p black and g red, then
    //     rotate at g so p becomes the subtree root. The subtree root is black
    //     again with the same black height, so the loop ends.
    //
    // Cases 2 and 3 together do at most 2 rotations, and only once per insert.
    // Case 1 may repeat O(log n) times, but only recolours. The else-branch is
    // the same three cases with left and right swapped.
    void insert_fixup(NodeBase* z) {
        while (z->parent->color == Color::Red) {
            NodeBase* p = z->parent;
            NodeBase* g = p->parent;
            if (p == g->left) {
                NodeBase* u = g->right;
                if (u->color == Color::Red) {  // case 1
                    set_color(p, Color::Black);
                    set_color(u, Color::Black);
                    set_color(g, Color::Red);
                    z = g;
                } else {
                    if (z == p->right) {  // case 2 -> case 3
                        z = p;
                        left_rotate(z);
                        p = z->parent;
                    }
                    set_color(p, Color::Black);  // case 3
                    set_color(g, Color::Red);
                    right_rotate(g);
                }
            } else {
                NodeBase* u = g->left;
                if (u->color == Color::Red) {  // case 1 (mirror)
                    set_color(p, Color::Black);
                    set_color(u, Color::Black);
                    set_color(g, Color::Red);
                    z = g;
                } else {
                    if (z == p->left) {  // case 2 -> case 3 (mirror)
                        z = p;
                        right_rotate(z);
                        p = z->parent;
                    }
                    set_color(p, Color::Black);  // case 3 (mirror)
                    set_color(g, Color::Red);
                    left_rotate(g);
                }
            }
        }
        // Case 1 can push red all the way up to the root. Recolouring the root
        // black raises every black height by one at once, so property 5 holds.
        set_color(root_, Color::Black);
    }

    // CLRS RB-TRANSPLANT: put subtree v in u's place under u's parent. u's own
    // children are left for the caller to relink. v->parent is set even when
    // v is nil_. delete_fixup relies on this to walk up from a nil_ x.
    void transplant(NodeBase* u, NodeBase* v) {
        if (u->parent == &nil_) {
            root_ = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        v->parent = u->parent;
    }

    // CLRS RB-DELETE-FIXUP. x carries an "extra black": every path through x
    // is one black node short. If x is red ("red-and-black"), colouring it
    // black settles that immediately (the loop condition). Otherwise x is
    // "doubly black", and the loop either moves the extra black up the tree
    // or gets rid of it with rotations. The sibling w decides the case (x is
    // a left child here; the else-branch mirrors it). w is a real node: x's
    // side has black height of at least 2 counting the extra black, so w's
    // side must have at least 1 black node above the leaves.
    //
    //   Case 1 - w red: then w's children and x's parent are black. Swap the
    //     colours of w and the parent and rotate at the parent. x's new
    //     sibling is one of w's black children, so this reduces to case 2, 3
    //     or 4.
    //   Case 2 - w black, both of w's children black: take one black off both
    //     x and w (w becomes red) and push the extra black up to the parent.
    //     If the parent was red (we came from case 1), the loop ends and it
    //     is coloured black. Otherwise repeat one level higher.
    //   Case 3 - w black, w's near child red, far child black: swap colours
    //     of w and its near child and rotate at w, so x's new sibling is
    //     black with a red far child. That is case 4.
    //   Case 4 - w black, w's far child red: w takes the parent's colour, the
    //     parent and the far child become black, rotate at the parent. The
    //     extra black is now absorbed by the new black node on x's side, so
    //     set x = root to end the loop.
    //
    // Cases 1, 3 and 4 run at most once each per delete (up to 3 rotations in
    // total). Case 2 can repeat O(log n) times, recolouring only.
    void delete_fixup(NodeBase* x) {
        while (x != root_ && x->color == Color::Black) {
            NodeBase* p = x->parent;
            if (x == p->left) {
                NodeBase* w = p->right;
                if (w->color == Color::Red) {  // case 1
                    set_color(w, Color::Black);
                    set_color(p, Color::Red);
                    left_rotate(p);
                    w = p->right;
                }
                if (w->left->color == Color::Black && w->right->color == Color::Black) {  // case 2
                    set_color(w, Color::Red);
                    x = p;
                } else {
                    if (w->right->color == Color::Black) {  // case 3 -> case 4
                        set_color(w->left, Color::Black);
                        set_color(w, Color::Red);
                        right_rotate(w);
                        w = p->right;
                    }
                    set_color(w, p->color);  // case 4
                    set_color(p, Color::Black);
                    set_color(w->right, Color::Black);
                    left_rotate(p);
                    x = root_;
                }
            } else {
                NodeBase* w = p->left;
                if (w->color == Color::Red) {  // case 1 (mirror)
                    set_color(w, Color::Black);
                    set_color(p, Color::Red);
                    right_rotate(p);
                    w = p->left;
                }
                if (w->right->color == Color::Black && w->left->color == Color::Black) {  // case 2 (mirror)
                    set_color(w, Color::Red);
                    x = p;
                } else {
                    if (w->left->color == Color::Black) {  // case 3 -> case 4 (mirror)
                        set_color(w->right, Color::Black);
                        set_color(w, Color::Red);
                        left_rotate(w);
                        w = p->left;
                    }
                    set_color(w, p->color);  // case 4 (mirror)
                    set_color(p, Color::Black);
                    set_color(w->left, Color::Black);
                    right_rotate(p);
                    x = root_;
                }
            }
        }
        set_color(x, Color::Black);
    }

    // Leftmost node of the subtree at x. Counts the nodes stepped through, since
    // erase uses this to find the successor.
    NodeBase* minimum(NodeBase* x) {
        while (x->left != &nil_) {
            DICT_COUNT(counters_, node_visits, 1);
            x = x->left;
        }
        return x;
    }

    const NodeBase* minimum(const NodeBase* x) const {
        while (x->left != &nil_) x = x->left;
        return x;
    }

    // In-order successor (CLRS 12.2): the leftmost node of the right subtree
    // if there is one; otherwise the first ancestor reached from a left child.
    const NodeBase* successor(const NodeBase* x) const {
        if (x->right != &nil_) return minimum(x->right);
        const NodeBase* y = x->parent;
        while (y != &nil_ && x == y->right) {
            x = y;
            y = y->parent;
        }
        return y;
    }

    // Returns the black height of the subtree at x (counting nil_ as 1), or -1
    // if any property is violated. lo/hi are exclusive key bounds inherited
    // from ancestors (nullptr = unbounded). Recursion depth is the tree height,
    // at most about 2 lg n, so this cannot overflow the stack.
    int check_subtree(const NodeBase* x, const K* lo, const K* hi, std::size_t& count) const {
        if (x == &nil_) return 1;
        ++count;
        const Node* n = as_node(x);
        if (lo && !less_(*lo, n->key)) return -1;  // BST: lo < key
        if (hi && !less_(n->key, *hi)) return -1;  // BST: key < hi
        if (x->left != &nil_ && x->left->parent != x) return -1;
        if (x->right != &nil_ && x->right->parent != x) return -1;
        if (x->color == Color::Red &&
            (x->left->color == Color::Red || x->right->color == Color::Red))
            return -1;  // property 4
        const int lh = check_subtree(x->left, lo, &n->key, count);
        if (lh < 0) return -1;
        const int rh = check_subtree(x->right, &n->key, hi, count);
        if (rh < 0 || lh != rh) return -1;  // property 5
        return lh + (x->color == Color::Black ? 1 : 0);
    }

    NodeBase nil_;
    NodeBase* root_ = &nil_;
    std::size_t size_ = 0;
    Compare less_{};
    mutable OpCounters counters_{};
};

}  // namespace dict
