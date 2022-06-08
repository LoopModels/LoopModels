#pragma once

#include "./ArrayReference.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Symbolics.hpp"
#include "LinearAlgebra.hpp"
#include "Orthogonalize.hpp"
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
// Should be in same block:
// s = 0
// for (i = eachindex(x)){
//   s += x[i]; // Omega = [0, _, 0]
// }
// m = s / length(x); // Omega = [1]
// for (i = eachindex(y)){
//   f(m, ...); // Omega = [2, _, 0]
// }
struct LoopBlock {
    llvm::SmallVector<ArrayReference, 0> refs;
    // TODO: figure out how to handle the graph's dependencies based on
    // operation/instruction chains.
    // Perhaps implicitly via the graph when using internal orthogonalization
    // and register tiling methods, and then generate associated constraints
    // or aliasing between schedules when running the ILP solver?
    // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
    // and all other other shared schedule parameters are aliases (i.e.,
    // identical)?
    struct MemoryAccess {
        unsigned ref; // index to ArrayReference
        llvm::User *user;
        // unsigned (instead of ptr) as we build up edges
        // and I don't want to relocate pointers when resizing vector
        Schedule schedule;
        llvm::SmallVector<unsigned> edgesIn;
        llvm::SmallVector<unsigned> edgesOut;
        const bool isLoad;
        MemoryAccess(unsigned ref, llvm::User *user, Schedule schedule,
                     bool isLoad)
            : ref(ref), user(user), schedule(schedule),
              edgesIn(llvm::SmallVector<unsigned>()),
              edgesOut(llvm::SmallVector<unsigned>()), isLoad(isLoad){};

        void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
        void addEdgeOut(unsigned i) { edgesOut.push_back(i); }
        // size_t getNumLoops() const { return ref->getNumLoops(); }
        // size_t getNumAxes() const { return ref->axes.size(); }
        // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
        bool fusedThrough(MemoryAccess &x) {
            // originally separate loops could be fused
            // if (loop() != x.loop()){ return false; }
            return schedule.fusedThrough(x.schedule);
        }
    };
    llvm::SmallVector<MemoryAccess, 0> memory;

    struct Edge {
        Dependence dependence;
        MemoryAccess *in;  // memory access in
        MemoryAccess *out; // memory access out
        Edge(Dependence dependence, MemoryAccess *in, MemoryAccess *out)
            : dependence(dependence), in(in), out(out) {}
    };
    llvm::SmallVector<Edge, 0> edges;
    llvm::SmallVector<bool> visited; // visited, for traversing graph
    llvm::DenseMap<llvm::User *, MemoryAccess *> userToMemory;

    ArrayReference &ref(MemoryAccess &x) { return refs[x.ref]; }
    ArrayReference &ref(MemoryAccess *x) { return refs[x->ref]; }
    const ArrayReference &ref(const MemoryAccess &x) const {
        return refs[x.ref];
    }
    const ArrayReference &ref(const MemoryAccess *x) const {
        return refs[x->ref];
    }
    bool isSatisfied(const Edge &e) const {
        const IntegerEqPolyhedra &sat = e.dependence.dependenceSatisfaction;

        auto &schIn = e.in->schedule;
        auto &schOut = e.out->schedule;
        const ArrayReference &refIn = ref(e.in);
        const ArrayReference &refOut = ref(e.out);
        size_t numLoopsIn = refIn.getNumLoops();
        size_t numLoopsOut = refOut.getNumLoops();
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
            const size_t offIn = e.dependence.isForward() ? 0 : numLoopsOut;
            const size_t offOut = e.dependence.isForward() ? numLoopsIn : 0;
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
            if (e.dependence.dependenceSatisfaction.knownSatisfied(schv)) {
                if (!e.dependence.dependenceBounding.knownSatisfied(schv)) {
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
    // so, `pushReductionEdges` will...
    // actually, probably better to put this into dependence checking
    // so that it can add optionally 0, 1, or 2 dependencies
    void pushReductionEdges(MemoryAccess &x, MemoryAccess &y) {
        if (!x.fusedThrough(y)) {
            return;
        }
        ArrayReference &refX = ref(x);
        ArrayReference &refY = ref(y);
        const size_t numLoopsX = refX.getNumLoops();
        const size_t numLoopsY = refY.getNumLoops();
        const size_t numAxes = refX.dim();
        // we preprocess to delinearize all, including linear indexing
        assert(numAxes == refY.dim());
        const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
        for (size_t i = numAxes; i < numLoopsCommon; ++i) {
            // push both edge directions
        }
    }
    void addEdge(MemoryAccess &mai, MemoryAccess &maj) {
        // note, axes should be fully delinearized, so should line up
        // as a result of preprocessing.
        if (llvm::Optional<Dependence> dep = Dependence::check(
                ref(mai), mai.schedule, ref(maj), maj.schedule)) {
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
            ArrayReference &refI = ref(mai);
            for (size_t j = 0; j < i; ++j) {
                MemoryAccess &maj = memory[j];
                ArrayReference &refJ = ref(maj);
                if ((refI.arrayID != refJ.arrayID) ||
                    ((mai.isLoad) && (maj.isLoad)))
                    continue;
                addEdge(mai, maj);
            }
        }
    }
    std::shared_ptr<AffineLoopNest> getBang(
        llvm::DenseMap<const AffineLoopNest *, std::shared_ptr<AffineLoopNest>>
            &map,
        SquarePtrMatrix<intptr_t> K, size_t i) const {
        const AffineLoopNest *aln = ref(memory[i]).loop.get();
        auto p = map.find(aln);
        std::shared_ptr<AffineLoopNest> newp;
        if (p == map.end()) {
            const size_t numVar = aln->getNumVar();
            const size_t numConstraints = aln->getNumConstraints();
            const size_t numTransformed = K.numRow();
            const size_t numPeeled = numVar - numTransformed;
            // DynamicMatrix<intptr_t> A;
            Matrix<intptr_t, 0, 0, 0> A;
            A.resizeForOverwrite(numVar, numConstraints);
            for (size_t k = 0; k < numConstraints; ++k) {
                for (size_t j = 0; j < numPeeled; ++j) {
                    A(j, k) = aln->A(j, k);
                }
                for (size_t j = numPeeled; j < numVar; ++j) {
                    intptr_t Ajk = 0;
                    for (size_t l = 0; l < numTransformed; ++l) {
                        Ajk += K(l, j - numPeeled) * aln->A(l, k);
                    }
                    A(j, k) = Ajk;
                }
            }
            return std::make_shared<AffineLoopNest>(std::move(A), aln->b,
                                                    aln->poset);
        } else {
            return p->second;
        }
    }
    void orthogonalizeStores() {
        llvm::SmallVector<bool, 256> visited(memory.size());
        for (size_t i = 0; i < memory.size(); ++i) {
            if (visited[i])
                continue;
            visited[i] = true;
            MemoryAccess &mai = memory[i];
            if (mai.isLoad)
                continue;
            ArrayReference &refI = ref(mai);
            size_t dimI = refI.dim();
            auto &axesI = refI.axes;
            size_t multiInds = 0;
            size_t multiLoops = 0;
            for (size_t j = 0; j < dimI; ++j) {
                auto &axisJ = axesI[j];
                if (axisJ.size() < 2) {
                    continue;
                }
                size_t count = 0;
                size_t loopsJ = 0;
                for (auto &ax : axisJ) {
                    if (ax.second.getType() == VarType::LoopInductionVariable) {
                        ++count;
                        loopsJ |= (size_t(1) << ax.second.getID());
                    }
                }
                if (count > 1) {
                    multiLoops |= loopsJ;
                    multiInds |= (size_t(1) << j);
                }
            }
            if (multiInds == 0)
                continue;
            // we won't rotate peelOuter loops
            const size_t peelOuter = std::countr_zero(multiLoops);
            size_t numLoops = refI.getNumLoops();
            // size_t numLoad = 0;
            size_t numStore = 1;
            size_t numRow = refI.dim();
            // we prioritize orthogonalizing stores
            // therefore, we sort the loads after
            llvm::SmallVector<unsigned, 16> orthInds;
            orthInds.push_back(i);
            for (size_t j = 0; j < memory.size(); ++j) {
                if (i == j)
                    continue;
                MemoryAccess &maj = memory[j];
                if (!mai.fusedThrough(maj))
                    continue;
                ArrayReference &refJ = ref(maj);
                numLoops = std::max(numLoops, refJ.getNumLoops());
                numRow += refJ.dim();
                // numLoad += maj.isLoad;
                numStore += (!maj.isLoad);
                // TODO: maybe don't set so aggressive, e.g.
                // if orth fails we could still viably set a narrower subset
                // or if it succeeds, perhaps a wider one.
                // So the item here is to adjust peelOuter.
                orthInds.push_back(j);
            }
            Matrix<intptr_t, 0, 0> S(numRow, numLoops - peelOuter);
            size_t rowStore = 0;
            size_t rowLoad = numStore;
            bool dobreakj = false;
            for (auto j : orthInds) {
                MemoryAccess &maj = memory[j];
                ArrayReference &refJ = ref(maj);
                size_t row = maj.isLoad ? rowLoad : rowStore;
                for (auto &axis : refJ.axes) {
                    if (addIndRow(S, axis, row++, peelOuter)) {
                        dobreakj = true;
                        break;
                    }
                }
                if (dobreakj)
                    break;
                rowLoad = maj.isLoad ? row : rowLoad;
                rowStore = maj.isLoad ? rowStore : row;
            }
            if (dobreakj)
                continue;
            auto [K, included] = NormalForm::orthogonalize(S);
            if (included.size()) {
                // L = old inds, J = new inds
                // L = K*J
                // Bounds:
                // A*L <= b
                // (A*J)*J <= b
                // Indices:
                // S*L = (S*K)*J
                // Schedule:
                // Phi*L = (Phi*K)*J
                Matrix<intptr_t, 0, 0> SK(matmul(S, K));
                llvm::DenseMap<const AffineLoopNest *,
                               std::shared_ptr<AffineLoopNest>>
                    map;
                rowStore = 0;
                rowLoad = numStore;
                for (auto &j : orthInds) {
                    visited[j] = true;
                    MemoryAccess &maj = memory[j];
                    ArrayReference &oldRef = ref(maj);
                    maj.ref = refs.size();
                    refs.emplace_back(oldRef.arrayID, getBang(map, K, j));
                    size_t row = maj.isLoad ? rowLoad : rowStore;
                    for (auto &axis : oldRef) {
                        refs.back().pushAffineAxis(axis, SK.getRow(row++),
                                                   peelOuter);
                    }
                    rowLoad = maj.isLoad ? row : rowLoad;
                    rowStore = maj.isLoad ? rowStore : row;
                    // set maj's schedule to rotation
                    // phi * L = (phi * K) * J
                    // NOTE: we're assuming the schedule is the identity
                    // otherwise, new schedule = old schedule * K
                    SquarePtrMatrix<intptr_t> Phi = maj.schedule.getPhi();
                    size_t phiDim = Phi.numCol();
                    for (size_t n = 0; n < phiDim; ++n) {
                        for (size_t m = 0; m < phiDim; ++m) {
                            Phi(m, n) = K(peelOuter + m, peelOuter + n);
                        }
                    }
                }
            }
        }
    }
};

std::ostream &operator<<(std::ostream &os, const LoopBlock::MemoryAccess &m) {
    if (m.isLoad) {
        os << "= ";
    }
    os << "ArrayReference #" << m.ref;
    if (!m.isLoad) {
        os << " =";
    }
    return os;
}
std::ostream &operator<<(std::ostream &os, const LoopBlock::Edge &e) {

    os << "Ref #" << e.in->ref << "-> Ref #" << e.out->ref;
    return os;
}
