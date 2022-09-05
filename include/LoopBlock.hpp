#pragma once

#include "./ArrayReference.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Simplex.hpp"
#include "./Symbolics.hpp"
#include "LinearAlgebra.hpp"
#include "Macro.hpp"
#include "NormalForm.hpp"
#include "Orthogonalize.hpp"
#include <cstddef>
#include <limits>
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
    // llvm::SmallVector<ArrayReference, 0> refs;
    // TODO: figure out how to handle the graph's dependencies based on
    // operation/instruction chains.
    // Perhaps implicitly via the graph when using internal orthogonalization
    // and register tiling methods, and then generate associated constraints
    // or aliasing between schedules when running the ILP solver?
    // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
    // and all other other shared schedule parameters are aliases (i.e.,
    // identical)?
    llvm::SmallVector<MemoryAccess, 0> memory;

    llvm::SmallVector<Dependence, 0> edges;
    llvm::SmallVector<bool> visited; // visited, for traversing graph
    llvm::DenseMap<llvm::User *, MemoryAccess *> userToMemory;
    llvm::SmallVector<Polynomial::Monomial> symbols;
    Simplex omniSimplex;
    // Simplex activeSimplex;
    // we may turn off edges because we've exceeded its loop depth
    // or because the dependence has already been satisfied at an
    // earlier level.
    // llvm::SmallVector<bool, 256> doNotAddEdge;
    llvm::SmallVector<bool, 256> scheduled;
    size_t numPhiCoefs{0};
    size_t numLambda{0};
    size_t numBounding{0};
    size_t numConstraints{0};
    // Simplex simplex;
    // ArrayReference &ref(MemoryAccess &x) { return refs[x.ref]; }
    // ArrayReference &ref(MemoryAccess *x) { return refs[x->ref]; }
    // const ArrayReference &ref(const MemoryAccess &x) const {
    //     return refs[x.ref];
    // }
    // const ArrayReference &ref(const MemoryAccess *x) const {
    //     return refs[x->ref];
    // }
    [[nodiscard]] size_t calcMaxDepth() const {
        size_t d = 0;
        for (auto &mem : memory)
            d = std::max(d, mem.getNumLoops());
        return d;
    }
    bool isSatisfied(const Dependence &e) const {
        const Simplex &sat = e.dependenceSatisfaction;
        Schedule &schIn = e.in->schedule;
        Schedule &schOut = e.out->schedule;
        const ArrayReference &refIn = e.in->ref;
        const ArrayReference &refOut = e.out->ref;
        size_t numLoopsIn = refIn.getNumLoops();
        size_t numLoopsOut = refOut.getNumLoops();
        size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
        size_t numLoopsTotal = numLoopsIn + numLoopsOut;
        Vector<int64_t> schv;
        schv.resizeForOverwrite(sat.getNumVar());
        const SquarePtrMatrix<int64_t> inPhi = schIn.getPhi();
        const SquarePtrMatrix<int64_t> outPhi = schOut.getPhi();
        llvm::ArrayRef<int64_t> inOmega = schIn.getOmega();
        llvm::ArrayRef<int64_t> outOmega = schOut.getOmega();
        const size_t numLambda = e.getNumLambda();
        // when i == numLoopsCommon, we've passed the last loop
        for (size_t i = 0; i <= numLoopsCommon; ++i) {
            if (int64_t o2idiff = outOmega[2 * i] - inOmega[2 * i]) {
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
            const size_t offIn = e.forward ? 0 : numLoopsOut;
            const size_t offOut = e.forward ? numLoopsIn : 0;
            for (size_t j = 0; j < numLoopsIn; ++j) {
                schv[j + offIn] = inPhi(j, i);
            }
            for (size_t j = 0; j < numLoopsOut; ++j) {
                schv[j + offOut] = outPhi(j, i);
            }
            int64_t inO = inOmega[2 * i + 1], outO = outOmega[2 * i + 1];
            // forward means offset is 2nd - 1st
            schv[numLoopsTotal] = outO - inO;
            // dependenceSatisfaction is phi_t - phi_s >= 0
            // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
            // we implicitly 0-out `w` and `u` here,
            if (sat.satisfiable(schv, numLambda)) {
                if (e.dependenceBounding.unSatisfiable(schv, numLambda)) {
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
    // void pushReductionEdges(MemoryAccess &x, MemoryAccess &y) {
    //     if (!x.fusedThrough(y)) {
    //         return;
    //     }
    //     ArrayReference &refX = x.ref;
    //     ArrayReference &refY = y.ref;
    //     const size_t numLoopsX = refX.getNumLoops();
    //     const size_t numLoopsY = refY.getNumLoops();
    //     const size_t numAxes = refX.dim();
    //     // we preprocess to delinearize all, including linear indexing
    //     assert(numAxes == refY.dim());
    //     const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
    //     for (size_t i = numAxes; i < numLoopsCommon; ++i) {
    //         // push both edge directions
    //     }
    // }
    void addEdge(MemoryAccess &mai, MemoryAccess &maj) {
        // note, axes should be fully delinearized, so should line up
        // as a result of preprocessing.
        if (size_t numDeps = Dependence::check(edges, mai, maj)) {
            size_t numEdges = edges.size();
            size_t e = numEdges - numDeps;
            do {
                edges[e].in->addEdgeOut(e);
                edges[e].out->addEdgeIn(e);
            } while (++e < numEdges);
            //             Dependence &d(dep.getValue());
            // #ifndef NDEBUG
            //             if (d.isForward()) {
            //                 std::cout << "dep direction: x -> y" <<
            //                 std::endl;
            //             } else {
            //                 std::cout << "dep direction: y -> x" <<
            //                 std::endl;
            //             }
            // #endif
            //             MemoryAccess *pin, *pout;
            //             if (d.isForward()) {
            //                 pin = &mai;
            //                 pout = &maj;
            //             } else {
            //                 pin = &maj;
            //                 pout = &mai;
            //             }
            //             edges.emplace_back(std::move(d), pin, pout);
            //             // input's out-edge goes to output's in-edge
            //             pin->addEdgeOut(numEdges);
            //             pout->addEdgeIn(numEdges);
            //             // pushReductionEdges(mai, maj);
        }
    }
    // fills all the edges between memory accesses, checking for
    // dependencies.
    void fillEdges() {
        for (size_t i = 1; i < memory.size(); ++i) {
            MemoryAccess &mai = memory[i];
            ArrayReference &refI = mai.ref;
            SHOWLN(i);
            for (size_t j = 0; j < i; ++j) {
                MemoryAccess &maj = memory[j];
                ArrayReference &refJ = maj.ref;
                CSHOW(j);
                CSHOW(refI.arrayID);
                CSHOW(refJ.arrayID);
                CSHOW(mai.isLoad);
                CSHOW(maj.isLoad);
                if ((refI.arrayID != refJ.arrayID) ||
                    ((mai.isLoad) && (maj.isLoad)))
                    std::cout << std::endl;
                if ((refI.arrayID != refJ.arrayID) ||
                    ((mai.isLoad) && (maj.isLoad)))
                    continue;
                addEdge(mai, maj);
                CSHOWLN(edges.size());
            }
        }
    }
    // TODO: we need to rotate via setting the schedule, not instantiating
    // the rotated array!
    static llvm::IntrusiveRefCntPtr<AffineLoopNest>
    getBang(llvm::DenseMap<const AffineLoopNest *,
                           llvm::IntrusiveRefCntPtr<AffineLoopNest>> &map,
            SquarePtrMatrix<int64_t> K, const AffineLoopNest *aln) {
        auto p = map.find(aln);
        llvm::IntrusiveRefCntPtr<AffineLoopNest> newp;
        if (p != map.end()) {
            return p->second;
        } else {
            const size_t numVar = aln->getNumLoops();
            const size_t numTransformed = K.numCol();
            const size_t numPeeled = numVar - numTransformed;
            // A = aln->A*K';
            // IntMatrix A(IntMatrix::uninitialized(numConstraints, numVar));
            // for (size_t j = 0; j < numPeeled; ++j) {
            //     for (size_t k = 0; k < numConstraints; ++k) {
            //         A(k, j) = aln->A(k, j);
            //     }
            // }
            // for (size_t j = numPeeled; j < numVar; ++j) {
            //     for (size_t k = 0; k < numConstraints; ++k) {
            //         int64_t Akj = 0;
            //         for (size_t l = 0; l < numTransformed; ++l) {
            //             Akj += aln->A(k, l) * K(j - numPeeled, l);
            //         }
            //         A(k, j) = Akj;
            //     }
            // }
            auto alshr = aln->rotate(K, numPeeled);
            // auto alshr = llvm::makeIntrusiveRefCnt<AffineLoopNest>(
            //     std::move(A), aln->b, aln->poset);
            map.insert(std::make_pair(aln, alshr));
            return alshr;
        }
    }
    //    bool ilpOpt() { return false; }

    void orthogonalizeStoresOld() {
        llvm::SmallVector<bool, 256> visited(memory.size());
        for (size_t i = 0; i < memory.size(); ++i) {
            if (visited[i])
                continue;
            MemoryAccess &mai = memory[i];
            if (mai.isLoad)
                continue;
            visited[i] = true;
            ArrayReference &refI = mai.ref;
            size_t dimI = refI.arrayDim();
            auto indMatI = refI.indexMatrix();
            size_t numLoopsI = indMatI.numRow();
            size_t multiInds = 0;
            size_t multiLoops = 0;
            for (size_t j = 0; j < dimI; ++j) {
                size_t count = 0;
                size_t loopsJ = 0;
                for (size_t k = 0; k < numLoopsI; ++k) {
                    if (indMatI(k, j)) {
                        ++count;
                        loopsJ |= (size_t(1) << k);
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
            size_t numRow = refI.arrayDim();
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
                ArrayReference &refJ = maj.ref;
                numLoops = std::max(numLoops, refJ.getNumLoops());
                numRow += refJ.arrayDim();
                // numLoad += maj.isLoad;
                numStore += (!maj.isLoad);
                // TODO: maybe don't set so aggressive, e.g.
                // if orth fails we could still viably set a narrower subset
                // or if it succeeds, perhaps a wider one.
                // So the item here is to adjust peelOuter.
                orthInds.push_back(j);
            }
            IntMatrix S(numLoops - peelOuter, numRow);
            size_t rowStore = 0;
            size_t rowLoad = numStore;
            bool dobreakj = false;
            for (auto j : orthInds) {
                MemoryAccess &maj = memory[j];
                ArrayReference &refJ = maj.ref;
                size_t row = maj.isLoad ? rowLoad : rowStore;
                auto indMatJ = refJ.indexMatrix();
                for (size_t l = peelOuter; l < indMatJ.numRow(); ++l) {
                    for (size_t k = 0; k < indMatJ.numCol(); ++k) {
                        S(l - peelOuter, row + k) = indMatJ(l, k);
                    }
                }
                row += indMatJ.numCol();
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
                IntMatrix KS{K * S};
                llvm::DenseMap<const AffineLoopNest *,
                               llvm::IntrusiveRefCntPtr<AffineLoopNest>>
                    loopMap;
                rowStore = 0;
                rowLoad = numStore;
                for (unsigned j : orthInds) {
                    visited[j] = true;
                    MemoryAccess &maj = memory[j];
                    // unsigned oldRefID = maj.ref;
                    // if (refMap[oldRefID] >= 0) {
                    //     maj.ref = oldRefID;
                    //     continue;
                    // }
                    ArrayReference oldRef = std::move(maj.ref);
                    maj.ref = {oldRef.arrayID,
                               getBang(loopMap, K, oldRef.loop.get())};
                    // refMap[oldRefID] = maj.ref = refs.size();
                    // refs.emplace_back(
                    size_t row = maj.isLoad ? rowLoad : rowStore;
                    auto indMatJ = oldRef.indexMatrix();
                    for (size_t l = peelOuter; l < indMatJ.numRow(); ++l)
                        for (size_t k = 0; k < indMatJ.numCol(); ++k)
                            indMatJ(l, k) = KS(l - peelOuter, row + k);
                    row += indMatJ.numCol();
                    rowLoad = maj.isLoad ? row : rowLoad;
                    rowStore = maj.isLoad ? rowStore : row;
                    // set maj's schedule to rotation
                    // phi * L = (phi * K) * J
                    // NOTE: we're assuming the schedule is the identity
                    // otherwise, new schedule = old schedule * K
                    MutSquarePtrMatrix<int64_t> Phi = maj.schedule.getPhi();
                    // size_t phiDim = Phi.numCol();
                    Phi = K(_(peelOuter, end), _(peelOuter, end));
                }
            }
        }
    }
    // NOTE: requires omniSimplex to be instantiated
    bool orthogonalizeStores() {
        // as a first optimization, we just look for stores with lower
        // access rank than the associated loop nest depth
        // Note that we can handle loads later, e.g. once we
        // hit the rank (and things have been feasible)
        // we can try adding extra constraints to
        // orthogonalize loads with respect to stores
        //
        // stores id, rank pairs
        llvm::SmallVector<unsigned> orthCandidates;
        scheduled.clear();
        scheduled.resize(memory.size()); // should be all `false`
        // iterate over stores
        for (unsigned i = 0; i < unsigned(memory.size()); ++i) {
            MemoryAccess &mai = memory[i];
            if (mai.isLoad)
                continue;
            mai.ref.rank = NormalForm::rank(mai.indexMatrix());
            if (mai.ref.rank < mai.getNumLoops())
                orthCandidates.emplace_back(i);
        }
        if (orthCandidates.size() == 0)
            return false;
        // now, the approach for orthogonalizing is to
        // first try orthogonalizing all, then, if that fails
        // we (as a first implementation) don't orthogonalize.
        // in the future, we could try stepwise, perhaps
        // using some cost model to prioritize.
        for (auto i : orthCandidates) {
            MemoryAccess &mem = memory[i];
            // now we set the schedule to the memory accesses
            auto indMat = mem.indexMatrix(); // numLoops x arrayDim
            auto phi = mem.schedule.getPhi();
            // we flip order, to try and place higher stride
            // dimensions in the outer loops
            for (size_t r = 0; r < mem.ref.rank; ++r)
                phi(r, _) = indMat(_, mem.ref.rank - 1 - r);
        }
        // TODO: probably have this be a separate driver instead?
        return false;
    }

    void countNumPhiCoefs(size_t depth) {
        size_t c = 0;
        for (auto &m : memory)
            c += ((m.phiIsScheduled() && (m.getNumLoops() > depth))
                      ? m.getNumLoops()
                      : 0);
        numPhiCoefs = c;
    }
    size_t countNumLambdas() const {
        size_t c = 0;
        for (auto &e : edges)
            c += (e.isInactive() ? 0 : e.getNumLambda());
        return c;
    }
    size_t countNumBoundingCoefs() const {
        size_t c = 0;
        for (auto &e : edges)
            c += (e.isInactive() ? 0 : e.getNumSymbols());
        return c;
    }
    void countAuxParamsAndConstraints() {
        size_t a = 0, b = 0, c = 0;
        for (auto &e : edges) {
            if (e.isInactive())
                continue;
            a += e.getNumLambda();
            b += e.getNumSymbols();
            c += e.getNumConstraints();
        }
        numLambda = a;
        numBounding = b;
        numConstraints = c;
    }
    void countNumParams(size_t depth) {
        countNumPhiCoefs(depth);
        countAuxParamsAndConstraints();
    }
    // assemble omni-simplex
    // we want to order variables to be
    // us, ws, Phi^-, Phi^+, omega, lambdas
    // this gives priority for minimization

    // bounding, scheduled coefs, lambda
    // matches lexicographical ordering of minimization
    // bounding, however, is to be favoring minimizing `u` over `w`
    size_t getLambdaOffset() const {
        return numBounding + 2 * numPhiCoefs + memory.size();
    }
    void instantiateOmniSimplex(size_t depth) {
        // defines numScheduleCoefs, numLambda, numBounding, and numConstraints
        const size_t numOmegaCoefs = memory.size();
        omniSimplex.resizeForOverwrite(numConstraints,
                                       numBounding + numPhiCoefs +
                                           numOmegaCoefs + numLambda);
        auto C{omniSimplex.getConstraints()};
        C = 0;
        // layout of omniSimplex:
        // Order: C, then priority to minimize
        // all : C, u, w, Phis^-, Phis^+, omegas, lambdas
        // rows give constraints; each edge gets its own
        // constexpr size_t numOmega =
        //     DependencePolyhedra::getNumOmegaCoefficients();
        size_t u = 0, w = numBounding - edges.size();
        size_t c = 0, p = numBounding, o = numBounding + 2 * numPhiCoefs,
               l = getLambdaOffset();
        // TODO: develop actual map going from
        for (size_t e = 0; e < edges.size(); ++e) {
            Dependence &edge = edges[e];
            if (edge.isInactive())
                continue;
            o = edge.out->updateOmegaOffset(o);
            o = edge.in->updateOmegaOffset(o);
            const auto [satC, satL, satPp, satPc, satO] =
                edge.splitSatisfaction();
            const auto [bndC, bndL, bndPp, bndPc, bndO, bndWU] =
                edge.splitBounding();

            const size_t numSatConstraints = satC.size();
            const size_t numBndConstraints = bndC.size();
            size_t cc = c + numSatConstraints;
            size_t ccc = cc + numBndConstraints;

            size_t ll = l + satL.numCol();
            size_t lll = ll + bndL.numCol();
            C(_(c, cc), _(l, ll)) = satL;
            C(_(cc, ccc), _(ll, lll)) = bndL;
            l = lll;

            // bounding
            C(_(cc, ccc), w++) = bndWU(_, 0);
            size_t uu = u + bndWU.numCol() - 1;
            C(_(cc, ccc), _(u, uu)) = bndWU(_, _(1, end));
            u = uu;

            C(_(c, cc), 0) = satC;
            C(_(cc, ccc), 0) = bndC;
            // now, handle Phi and Omega
            if (depth & 1) {
                size_t d = depth >> 1;
                // phis are not constrained to be 0
                if (d >= edge.out->getNumLoops()) {
                } else if (edge.out->phiIsScheduled()) {
                    // add it constants
                    auto sch = edge.out->getSchedule(d);
                    C(_(c, cc), 0) -= satPc * sch;
                    C(_(cc, ccc), 0) -= bndPc * sch;
                } else {
                    // add it to C
		    p = edge.out->updatePhiOffset(p);
                    auto phiChild = edge.out->getPhiOffset();
                    C(_(c, cc), phiChild) = -satPc;
                    C(_(c, cc), phiChild + satPc.numCol()) = satPc;
                    C(_(cc, ccc), phiChild) = -bndPc;
                    C(_(cc, ccc), phiChild + bndPc.numCol()) = bndPc;
                }
                if (d >= edge.in->getNumLoops()) {
                } else if (edge.in->phiIsScheduled()) {
                    // add it to constants
                    auto sch = edge.in->getSchedule(d);
                    C(_(c, cc), 0) -= satPp * sch;
                    C(_(cc, ccc), 0) -= bndPp * sch;
                } else {
                    // add it to C
		    p = edge.in->updatePhiOffset(p);
                    auto phiParent = edge.in->getPhiOffset();
                    C(_(c, cc), phiParent) = -satPp;
                    C(_(c, cc), phiParent + satPp.numCol()) = satPp;
                    C(_(cc, ccc), phiParent) = -bndPp;
                    C(_(cc, ccc), phiParent + bndPp.numCol()) = bndPp;
                }
            }
            // Omegas are included regardless of rotation
            C(_(c, cc), edge.out->omegaOffset) = satO(_, !edge.forward);
            C(_(cc, ccc), edge.out->omegaOffset) = bndO(_, !edge.forward);
            C(_(c, cc), edge.in->omegaOffset) = satO(_, edge.forward);
            C(_(cc, ccc), edge.in->omegaOffset) = bndO(_, edge.forward);

            c = ccc;
        }
    }
    void updateSchedules(PtrVector<Rational> sol, size_t depth) {
        for (auto &&mem : memory) {
            if (depth >= mem.getNumLoops())
                continue;
            mem.schedule.getOmega()(depth) = sol(mem.omegaOffset);
            if (mem.scheduleFlag())
                continue;
            mem.schedule.getPhi()(_, depth) = sol(mem.getPhiOffset());
        }
    }
    static int64_t lexSign(PtrVector<int64_t> x) {
        for (auto y : x)
            if (y)
                return 2 * (y > 0) - 1;
        return 0;
    }
    void addIndependentSolutionConstraints(size_t depth) {
        omniSimplex.reserveExtraRows(memory.size());
        if (depth == 0) {
            // add ones >= 0
            for (auto &&mem : memory) {
                if (mem.phiIsScheduled())
                    continue;
                auto c{omniSimplex.addConstraint()};
                c(0) = 1;
                c(mem.getPhiOffset() + 1) = 1;
                c(end) = -1; // for >=
            }
            return;
        }
        IntMatrix A, N;
        for (auto &&mem : memory) {
            if (mem.phiIsScheduled() || (depth >= mem.getNumLoops()))
                continue;
            A = mem.schedule.getPhi()(_(0, depth - 1), _).transpose();
            NormalForm::nullSpace11(N, A);
            auto c{omniSimplex.addConstraintAndVar()};
            c(0) = 1;
            auto cc{c(mem.getPhiOffset() + 1)};
            // sum(N,dims=1) >= 1 after flipping row signs to be lex > 0
            for (size_t m = 0; m < N.numRow(); ++m)
                cc += N(m, _) * lexSign(N(m, _));
            c(end) = -1; // for >=
        }
    }
    void resetPhiOffsets() {
        for (auto &&mem : memory)
            mem.phiOffset = std::numeric_limits<unsigned>::max();
    }
    // returns true on failure
    bool optimize() {
        fillEdges();
        const size_t maxDepth = calcMaxDepth();
        Vector<Rational> sol;
        for (size_t d = 0; d < 2 * maxDepth + 1; ++d) {
            countAuxParamsAndConstraints();
            numPhiCoefs = 0;
            if (d & 1)
                countNumPhiCoefs(d);
            instantiateOmniSimplex(d);
            addIndependentSolutionConstraints(d);
            if (omniSimplex.initiateFeasible())
                return true;
            omniSimplex.lexMinimize(sol, getLambdaOffset());
            // TODO: deactivate edges of satisfied dependencies
            updateSchedules(sol, d);
        }
        return false;
    }
};

std::ostream &operator<<(std::ostream &os, const MemoryAccess &m) {
    if (m.isLoad)
        os << "= ";
    os << "ArrayReference:\n" << m.ref;
    if (!m.isLoad)
        os << " =";
    return os;
}
