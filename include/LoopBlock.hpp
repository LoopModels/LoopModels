#pragma once

#include "./ArrayReference.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Symbolics.hpp"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/User.h>

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

    struct Edge {
        Dependence dependence;
        MemoryAccess *in;  // memory access in
        MemoryAccess *out; // memory access out
    };

    llvm::SmallVector<ArrayReference, 0> refs;
    llvm::SmallVector<MemoryAccess, 0> memory;
    llvm::SmallVector<Edge, 0> edges;
    llvm::SmallVector<bool> visited; // visited, for traversing graph
    llvm::DenseMap<llvm::User *, MemoryAccess *> userToMemory;
    // fills all the edges between memory accesses, checking for
    // dependencies.
    void fillEdges() {
        for (size_t i = 1; i < memory.size(); ++i) {
            MemoryAccess &mai = memory[i];
            for (size_t j = 0; j < i; ++j) {
                MemoryAccess &maj = memory[j];
                if (mai.ref->arrayID != maj.ref->arrayID)
                    continue;
		if (llvm::Optional<Dependence> dep = Dependence::check(mai, maj)){
		    size_t numEdges = edges.size();
		    Dependence& d(dep.getValue());
		    MemoryAccess* pin, *pout;
		    if (d.isForward()){
			pin = &mai;
			pout = &maj;
		    } else {
			pin = &maj;
			pout = &mai;
		    }
		    edges.emplace_back(std::move(dep.getValue()), pin, pout);
		    // input's out-edge goes to output's in-edge
		    pin->addEdgeOut(numEdges);
		    pout->addEdgeIn(numEdges);
		}
            }
        }
    }
};
