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
//
// That is:
// fields that must be provided/filled:
//  - refs
//  - memory
//  - userToMemory
// fields it self-initializes:
struct LoopBlock {
    // TODO: figure out how to handle the graph's dependencies based on
    // operation/instruction chains.
    // Perhaps implicitly via the graph when using internal orthogonalization
    // and register tiling methods, and then generate associated constraints
    // or aliasing between schedules when running the ILP solver?
    // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
    // and all other other shared schedule parameters are aliases (i.e.,
    // identical)?
    struct Edge {
        Dependence dependence;
        MemoryAccess *in;  // memory access in
        MemoryAccess *out; // memory access out
        Edge(Dependence dependence, MemoryAccess *in, MemoryAccess *out)
            : dependence(dependence), in(in), out(out) {}
        bool isSatisfied() {
            IntegerEqPolyhedra &sat = dependence.dependenceSatisfaction;
            auto &schIn = in->schedule;
            auto &schOut = out->schedule;
            size_t numLoopsIn = in->getNumLoops();
            size_t numLoopsOut = out->getNumLoops();
            size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
            size_t numLoopsTotal = numLoopsIn + numLoopsOut;
            llvm::SmallVector<intptr_t, 16> schv;
            schv.resize_for_overwrite(sat.getNumVar());
            const SquarePtrMatrix<intptr_t> inPhi = schIn.getPhi();
            const SquarePtrMatrix<intptr_t> outPhi = schOut.getPhi();
            const PtrVector<intptr_t, 0> inOmega = schIn.getOmega();
            const PtrVector<intptr_t, 0> outOmega = schOut.getOmega();

	    // when i == numLoopsCommon, we've passed the last loop
            for (size_t i = 0; i <= numLoopsCommon; ++i) {
                if (intptr_t o2idiff = outOmega[2 * i] - inOmega[2 * i]) {
                    return (o2idiff > 0);
                }

                // we should not be able to reach `numLoopsCommon`
                // because at the very latest, this last schedule value
                // should be different, because either:
                // if (numLoopsX == numLoopsY){
                //   we're at the inner most loop, where one of the instructions
                //   must have appeared before the other.
                // } else {
                //   the loop nests differ in depth, in which case the deeper
                //   loop must appear either above or below the instructions
                //   present at that level
                // }
                assert(i != numLoopsCommon);
                size_t offIn = dependence.isForward() ? 0 : numLoopsOut;
                size_t offOut = dependence.isForward() ? numLoopsIn : 0;
                for (size_t j = 0; j < numLoopsIn; ++j) {
                    schv[j + offIn] = inPhi(j, i);
                }
                for (size_t j = 0; j < numLoopsOut; ++j) {
                    schv[j + offOut] = outPhi(j, i);
                }
                intptr_t inO = inOmega[2 * i + 1], outO = outOmega[2 * i + 1];
                // forward means offset is 2nd - 1st
                schv[numLoopsTotal] = outO - inO;
                // dependenceSatisfaction is phi_t - phi_s >= 0
                // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
                // we implicitly 0-out `w` and `u` here,
                if (dependence.dependenceSatisfaction.knownSatisfied(schv)) {
                    if (!dependence.dependenceBounding.knownSatisfied(schv)) {
                        // if zerod-out bounding not >= 0, then that means
                        // phi_t - phi_s > 0, so the dependence is satisfied
                        return true;
                    }
                } else {
                    // if not satisfied, false
                    return false;
                }
            }
	    assert(false);
	    return false;
        }
        friend std::ostream &operator<<(std::ostream &os, const Edge &e) {

            os << *(e.in->ref) << "-> \n" << *(e.out->ref);
            return os;
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
                if ((mai.ref->arrayID != maj.ref->arrayID) ||
                    ((mai.isLoad) && (maj.isLoad)))
                    continue;
                if (llvm::Optional<Dependence> dep =
                        Dependence::check(mai, maj)) {
                    size_t numEdges = edges.size();
                    Dependence &d(dep.getValue());
#ifndef NDEBUG
                    if (d.isForward()) {
                        std::cout << "dep direction: x -> y" << std::endl;
                    } else {
                        std::cout << "dep direction: y -> x" << std::endl;
                    }
#endif
                    MemoryAccess *pin, *pout;
                    if (d.isForward()) {
                        pin = &mai;
                        pout = &maj;
                    } else {
                        pin = &maj;
                        pout = &mai;
                    }
                    edges.emplace_back(std::move(d), pin, pout);
                    // input's out-edge goes to output's in-edge
                    pin->addEdgeOut(numEdges);
                    pout->addEdgeIn(numEdges);
                }
            }
        }
    }
};
