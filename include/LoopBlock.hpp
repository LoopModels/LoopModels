#pragma once

#include "./ArrayReference.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Schedule.hpp"
#include "./Symbolics.hpp"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>




// A loop block is a block of the program that may include multiple loops.
// These loops are either all executed (note iteration count may be 0, or
// loops may be in rotated form and the guard prevents execution; this is okay
// and counts as executed for our purposes here ), or none of them are.
// That is, the LoopBlock does not contain divergent control flow, or guards
// unrelated to loop bounds.
// The loops within a LoopBlock are optimized together, so we can consider
// optimizations such as reordering or fusing them together as a set.
//
//
// Initially, the `LoopBlock` is initialized as a set of
// `Read` and `Write`s, without any dependence polyhedra.
// Then, it builds `DependencePolyhedra`.
// These can be used to construct an ILP.
struct LoopBlock {

    struct MemoryAccess {
        ArrayReference *ref;
        llvm::User *src; // null if store
        llvm::User *dst; // null if load
        // unsigned (instead of ptr) as we build up edges
        // and I don't want to relocate pointers when resizing vector
	Schedule schedule;
        llvm::SmallVector<unsigned> edgesIn;
        llvm::SmallVector<unsigned> edgesOut;
    };
    struct Edge {
        DependencePolyhedra poly;
        MemoryAccess *in;  // memory access in
        MemoryAccess *out; // memory access out
    };

    llvm::SmallVector<ArrayReference, 0> refs;
    llvm::SmallVector<MemoryAccess, 0> memory;
    llvm::SmallVector<Edge, 0> edges;
    llvm::SmallVector<bool> visited; // visited, for traversing graph
    llvm::DenseMap<llvm::User *, MemoryAccess *> userToMemory;
};
