#pragma once
#include "./math.hpp"
#include <tuple>

template <typename T> struct Tree {
    T *ptr;          // stride is `stride`
    size_t *offsets; // stride is `stride + 1`
    size_t breadth;  // number of terms
    size_t depth; // number of loops + 1 (last indicates order w/in final loop
                  // nest)
    size_t stride;

    struct Iterator;
    Iterator begin();
    size_t end() { return breadth; };
};

template <typename T>
std::pair<Vector<T, 0>, Tree<T>> subTree(Tree<T> t, size_t i) {
#ifndef DONOTBOUNDSCHECK
    assert((0 <= i) & (i < t.breadth));
    // assert((0 <= i) & (i < t.branches));
    assert(t.depth > 0);
#endif
    size_t base = t.offsets[i];
    size_t len = t.offsets[i + 1] - base;
    Vector<T, 0> v = Vector<T, 0>(t.ptr + base, len);
    Tree<T> ts = Tree<T>{
        .ptr = t.ptr + t.stride,
        .offsets = t.offsets + base + t.stride + 1,
        .breadth = len,
        .depth = t.depth - 1,
        .stride = t.stride
        // .breadth = t.breadth, .branches = t.branches
    };
    return std::make_pair(v, ts);
}

template <typename T> struct Tree<T>::Iterator {
    Tree<T> tree;
    size_t position;
    bool dobreak;

    std::tuple<size_t, Vector<T, 0>, Tree<T>> operator*() {
        auto [v, t] = subTree(tree, position);
        dobreak = length(v) == 0;
        return std::make_tuple(position, v, t);
    }
    Tree<T>::Iterator operator++() {
        ++position;
        return *this;
    }

    bool operator!=(size_t x) {
        return ((!dobreak) & (x != position));
    } // false means stop
    bool operator==(size_t x) {
        return (dobreak | (x == position));
    } // true means stop
    bool operator!=(Tree<T>::Iterator x) {
        return (!dobreak) & (x.position != position);
    }
    bool operator==(Tree<T>::Iterator x) {
        return dobreak | (x.position == position);
    }
};

template <typename T> typename Tree<T>::Iterator Tree<T>::begin() {
    return Tree<T>::Iterator{*this, 0, false};
}

// Look up the position of an element in a tree.
struct InvTree { // basically, a depth x breadth matrix
    size_t *ptr;
    size_t breadth; // number of terms
    size_t depth;   // number of loops + 1
    size_t &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < depth));
        assert((0 <= j) & (j < breadth));
#endif
        return ptr[i + j * depth];
    }
    Vector<size_t, 0> operator()(size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= j) & (j < breadth));
#endif
        return Vector<size_t, 0>(ptr + j * depth, depth);
    }
};

struct IndexTree {
    size_t *ptr;
    size_t breadth; // number of terms
    size_t depth;   // number of loops + 1

    operator Tree<size_t>() {
        size_t *ptrOffsets = ptr + breadth * depth;
        return Tree<size_t>{ptr, ptrOffsets, breadth, depth, breadth};
    }
    operator InvTree() {
        size_t *ptrInvTree = ptr + 2 * breadth * depth + depth;
        return InvTree{ptrInvTree, breadth, depth};
    }
};

void fillInvTree(Tree<size_t> t, InvTree it, size_t depth = 0) {
    size_t nextDepth = depth + 1;
    for (Tree<size_t>::Iterator I = t.begin(); I != t.end(); ++I) {
        auto [p, v, t] = *I;
        for (size_t j = 0; j < length(v); ++j) {
            it(depth, v(j)) = p;
        }
        if (nextDepth < it.depth) {
            fillInvTree(t, it, nextDepth);
        }
    }
}
void fillInvTree(IndexTree t) { fillInvTree(Tree<size_t>(t), InvTree(t)); }
