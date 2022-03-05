#pragma once
#include "./IntermediateRepresentation.hpp"
#include "./Math.hpp"
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <memory>
#include <tuple>
#include <variant>

struct Tree {
    llvm::SmallVector<
        std::unique_ptr<std::variant<std::pair<Tree, llvm::Loop *>, Term>>>
        branches;

    auto begin() { return branches.begin(); }
    auto end() { return branches.end(); }
    auto begin() const { return branches.begin(); }
    auto end() const { return branches.end(); }
    void emplace_back(llvm::Loop *LP, size_t numOuter) {
        std::unique_ptr<std::variant<std::pair<Tree, llvm::Loop *>, Term>> p =
            std::make_unique<
                std::variant<std::pair<Tree, llvm::Loop *>, Term>>();
        *p = Term(LP, numOuter);
        branches.push_back(std::move(p));
    }
};

// // Underlying data represents the tree as a matrix
// // for i in I0
// //   // op 8
// //   for j in J0
// //     // op 0 // op0 contains index to struct represeting {I0, J0}
// //   end
// //   // op 9
// // end
// // for i in I1
// //   // op 7
// //   for j in J1
// //     for k in K0
// //       // op 1
// //       // op 2
// //     end
// //   end
// //   for j in J2
// //     for k in K1
// //       // op 3
// //     end
// //     for k in K2
// //       // op 4
// //     end
// //   end
// //   for j in J3
// //     // op 5
// //   end
// // end
// // for i in I2
// //   // op 6
// // end
// // Last column are leaves
// // // we're going with, where we need to look at the associated nest to
// // determine the actual depth of the given nest.
// // [ 0 0 0 1 1 1 1 1 1 2   // top level loop
// //   0 1 2 0 1 1 2 2 3 0   // position within first nest
// //   0 0 0 0 0 0 0 1 0 0   // position within second nest
// //   8 0 9 7 1 2 3 4 5 6 ] // op num
// // offsets:
// // [ 0 3 9 10 10
// //   0 1 2  2  3
// // offets[0, 0] : offsets[0, 1]
// // [0 : 3)
// // offsets[0,2] : offsets[0,3]
// // [9, 10)
// //
// template <typename T> struct FakeTree {
//     T *ptr;
//     size_t depth;
//     size_t numLeaves;
//     size_t stride;
//
//     struct Iterator;
//     Iterator begin();
//     size_t end() { return breadth; };
// };
// template <typename T>
// struct RootTree {
//     llvm::SmallVector<T,0> data;
//     Tree<T> tree;
// };
//
// // index with `i`, returning the sub-tree...
// //
// template <typename T>
// std::pair<llvm::ArrayRef<T>, Tree<T>> subTree(Tree<T> t, size_t i) {
// #ifndef DONOTBOUNDSCHECK
//     assert(i < t.breadth);
//     // assert((0 <= i) & (i < t.branches));
//     assert(t.depth > 0);
// #endif
//     size_t base = t.offsets[i];
//     size_t len = t.offsets[i + 1] - base;
//
//     llvm::ArrayRef<T> v = llvm::ArrayRef<T>(t.ptr + base, len);
//     Tree<T> ts = Tree<T>{
//         .ptr = t.ptr + t.stride,
//         .offsets = t.offsets + base + t.stride + 1,
//         .breadth = len,
//         .depth = t.depth - 1,
//         .stride = t.stride
//         // .breadth = t.breadth, .branches = t.branches
//     };
//     return std::make_pair(v, ts);
// }
//
// template <typename T> struct Tree<T>::Iterator {
//     Tree<T> tree;
//     size_t position;
//     bool dobreak;
//
//     std::tuple<size_t, llvm::ArrayRef<T>, Tree<T>> operator*() {
//         auto [v, t] = subTree(tree, position);
//         dobreak = length(v) == 0;
//         return std::make_tuple(position, v, t);
//     }
//     Tree<T>::Iterator operator++() {
//         ++position;
//         return *this;
//     }
//
//     bool operator!=(size_t x) {
//         return ((!dobreak) & (x != position));
//     } // false means stop
//     bool operator==(size_t x) {
//         return (dobreak | (x == position));
//     } // true means stop
//     bool operator!=(Tree<T>::Iterator x) {
//         return (!dobreak) & (x.position != position);
//     }
//     bool operator==(Tree<T>::Iterator x) {
//         return dobreak | (x.position == position);
//     }
// };
//
// template <typename T> typename Tree<T>::Iterator Tree<T>::begin() {
//     return Tree<T>::Iterator{*this, 0, false};
// }
//
// // Look up the position of an element in a tree.
// struct InvTree { // basically, a depth x breadth matrix
//     size_t *ptr;
//     size_t breadth; // number of terms
//     size_t depth;   // number of loops + 1
//     size_t &operator()(size_t i, size_t j) {
// #ifndef DONOTBOUNDSCHECK
//         assert(i < depth);
//         assert(j < breadth);
// #endif
//         return ptr[i + j * depth];
//     }
//     llvm::ArrayRef<size_t> operator()(size_t j) {
// #ifndef DONOTBOUNDSCHECK
//         assert(j < breadth);
// #endif
//         return llvm::ArrayRef<size_t>(ptr + j * depth, depth);
//     }
// };
//
// struct IndexTree {
//     size_t *ptr;
//     size_t breadth; // number of terms
//     size_t depth;   // number of loops + 1
//
//     operator Tree<size_t>() {
//         size_t *ptrOffsets = ptr + breadth * depth;
//         return Tree<size_t>{ptr, ptrOffsets, breadth, depth, breadth};
//     }
//     operator InvTree() {
//         size_t *ptrInvTree = ptr + 2 * breadth * depth + depth;
//         return InvTree{ptrInvTree, breadth, depth};
//     }
// };
//
// void fillInvTree(Tree<size_t> t, InvTree it, size_t depth = 0) {
//     size_t nextDepth = depth + 1;
//     for (Tree<size_t>::Iterator I = t.begin(); I != t.end(); ++I) {
//         auto [p, v, t] = *I;
//         for (size_t j = 0; j < length(v); ++j) {
//             it(depth, v[j]) = p;
//         }
//         if (nextDepth < it.depth) {
//             fillInvTree(t, it, nextDepth);
//         }
//     }
// }
// void fillInvTree(IndexTree t) { fillInvTree(Tree<size_t>(t), InvTree(t)); }
