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
//
//
// NOTE: w/ respect to index linearization (e.g., going from Cartesian indexing
// to linear indexing), the current behavior will be to fully delinearize as a
// preprocessing step. Linear indexing may be used later as an optimization.
// This means that not only do we want to delinearize
// for (n = 0; n < N; ++n){
//   for (m = 0; m < M; ++m){
//      C(m + n*M)
//   }
// }
// we would also want to delinearize
// for (i = 0; i < M*N; ++i){
//   C(i)
// }
// into
// for (n = 0; n < N; ++n){
//   for (m = 0; m < M; ++m){
//      C(m, n)
//   }
// }
// and then relinearize as an optimization later.
// Then we can compare fully delinearized loop accesses.
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
    // NOTE: this relies on two important assumptions:
    // 1. Code has been fully delinearized, so that axes all match
    //    (this means that even C[i], 0<=i<M*N -> C[m*M*n])
    //    (TODO: what if we have C[n+N*m] and C[m+M*n]???)
    //    (this of course means we have to see other uses in
    //     deciding whether to expand `C[i]`, and what to expand
    //     it into.)
    // 2. Reduction targets have been orthogonalized, so that
    //     the number of axes reflects the number of loops they
    //     depend on.
    // if we have
    // for (i = I, j = J, m = M, n = N) {
    //   C(m,n) = foo(C(m,n), ...)
    // }
    // then we have dependencies that
    // the load C(m,n) [ i = x, j = y ]
    // happens after the store C(m,n) [ i = x-1, j = y], and
    // happens after the store C(m,n) [ i = x, j = y-1]
    // and that the store C(m,n) [ i = x, j = y ]
    // happens after the load C(m,n) [ i = x-1, j = y], and
    // happens after the load C(m,n) [ i = x, j = y-1]
    //
    // so, `pushReductionEdges` will
    void pushReductionEdges(MemoryAccess &x, MemoryAccess &y) {
	if (!x.sameLoop(y)){ return; }
        const size_t numLoopsX = x.getNumLoops();
        const size_t numLoopsY = y.getNumLoops();
        const size_t numAxes = x.getNumAxes();
	// we preprocess to delinearize all, including linear indexing
	assert(numAxes == y.getNumAxes());
	const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
	for (size_t i = numAxes; i < numLoopsCommon; ++i){
	    // push both edge directions 
	}
    }
    void addEdge(MemoryAccess &mai, MemoryAccess &maj) {
        // note, axes should be fully delinearized, so should line up
	// as a result of preprocessing.
        if (llvm::Optional<Dependence> dep = Dependence::check(mai, maj)) {
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
            pushReductionEdges(mai, maj);
        }
    }
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
                addEdge(mai, maj);
            }
        }
    }
};
