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
        bool isSatisfied() {
            IntegerPolyhedra &sat = dependence.dependenceSatisfaction;
            llvm::SmallVector<intptr_t, 16> schv;
            schv.resize_for_overwrite(sat.getNumVar());
            auto &schIn = in->schedule;
            auto &schOut = out->schedule;
            size_t numLoopsIn = in->getNumLoops();
            size_t numLoopsOut = out->getNumLoops();
            size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
            /*
            for (size_t i = 0; i <= numLoopsCommon; ++i) {
                if (intptr_t o2idiff = yOmega[2 * i] - xOmega[2 * i]) {
                if (o2idiff < 0) {
                    dxy.forward = false;
                    fyx.A.reduceNumRows(numLoopsTotal + 1);
                    // y then x
                    return Dependence{dxy, fyx, fxy};
                } else {
                    fxy.A.reduceNumRows(numLoopsTotal + 1);
                    // x then y
                    return Dependence{dxy, fxy, fyx};
                }
            }
            // we should not be able to reach `numLoopsCommon`
            // because at the very latest, this last schedule value
            // should be different, because either:
            // if (numLoopsX == numLoopsY){
            //   we're at the inner most loop, where one of the instructions
            //   must have appeared before the other.
            // } else {
            //   the loop nests differ in depth, in which case the deeper loop
            //   must appear either above or below the instructions present
            //   at that level
            // }
            assert(i != numLoopsCommon);
            for (size_t j = 0; j < numLoopsX; ++j) {
                sch[j] = xPhi(j, i);
            }
            for (size_t j = 0; j < numLoopsY; ++j) {
                sch[j + numLoopsX] = yPhi(j, i);
            }
            intptr_t yO = yOmega[2 * i + 1], xO = xOmega[2 * i + 1];
            // forward means offset is 2nd - 1st
            sch[numLoopsTotal] = yO - xO;
            if (!fxy.knownSatisfied(sch)) {
                dxy.forward = false;
                fyx.A.reduceNumRows(numLoopsTotal + 1);
                // y then x
                return Dependence{dxy, fyx, fxy};
            }
            // backward means offset is 1st - 2nd
            sch[numLoopsTotal] = xO - yO;
            if (!fyx.knownSatisfied(sch)) {
                fxy.A.reduceNumRows(numLoopsTotal + 1);
                return Dependence{dxy, fxy, fyx};
            }
            */
            return false;
        }
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
                if (llvm::Optional<Dependence> dep =
                        Dependence::check(mai, maj)) {
                    size_t numEdges = edges.size();
                    Dependence &d(dep.getValue());
                    MemoryAccess *pin, *pout;
                    if (d.isForward()) {
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
