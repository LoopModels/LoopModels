#pragma once

#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./POSet.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Symbolics.hpp"
#include "Orthogonalize.hpp"
#include "Simplex.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <utility>

// for i = 1:N, j = 1:i
//     A[i,j] = foo(A[i,i])
// labels: 0           1
//
// Dependence Poly:
// 1 <= i_0 <= N
// 1 <= j_0 <= i_0
// 1 <= i_1 <= N
// 1 <= j_1 <= i_1
// i_0 == i_1
// j_0 == i_1
struct DependencePolyhedra : SymbolicEqPolyhedra {
    // size_t numLoops;
    size_t numDep0Var; // loops dep 0
    // size_t numDep1Var; // loops dep 1
    llvm::SmallVector<int64_t, 2> nullStep;
    llvm::SmallVector<Polynomial::Monomial> symbols;
    inline size_t getTimeDim() const { return nullStep.size(); }
    inline size_t getDim0() const { return numDep0Var; }
    inline size_t getNumSymbols() const { return 1 + symbols.size(); }
    inline size_t getDim1() const {
        return getNumVar() - numDep0Var - nullStep.size() - getNumSymbols();
    }
    inline size_t getNumScheduleCoefficients() const {
        return getNumVar() - nullStep.size() + 1 - getNumSymbols();
    }
    MutPtrVector<int64_t> getSymbols(size_t i) {
        return A(i, _(begin, getNumSymbols()));
    }
    PtrVector<int64_t> getSymbols(size_t i) const {
        return A(i, _(begin, getNumSymbols()));
    }
    llvm::Optional<int64_t> getCompTimeOffset(size_t i) const {
        for (size_t j = 1; j < getNumSymbols(); ++j)
            if (A(i, j))
                return {};
        return A(i, 0);
    }

    static llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
    matchingStrideConstraintPairs(const ArrayReference &ar0,
                                  const ArrayReference &ar1) {
#ifndef NDEBUG
        std::cout << "ar0 = \n" << ar0 << "\nar1 = " << ar1 << std::endl;
#endif
        // fast path; most common case
        if (ar0.stridesMatch(ar1)) {
            llvm::SmallVector<std::pair<int, int>, 4> dims;
            size_t numDims = ar0.arrayDim();
            dims.reserve(numDims);
            for (size_t i = 0; i < numDims; ++i)
                dims.emplace_back(i, i);
            return dims;
        }
        // Farkas: psi(x) >= 0 iff
        // psi(x) = l_0 + lambda' * (b - A'*x) for some l_0, lambda >= 0
        // psi(x) is an affine function.
        // Here, we assume that function is either...
        // if (boundAbove) {
        //   w + u'N + alpha_delta + alpha_t'i_t - alpha_s'i_s
        // else {
        //   alpha_delta + alpha_t'i_t - alpha_s'i_s
        // }
        // N are the symbolic variables, like loop bounds.
        // u and w are introduced variables.
        //
        // x = [i_s..., i_t...]
        //
        // or swap alpha signs if subInd < 0
        //
        // Returns an IntegerEqPolyhedra C'*y <= d
        // where
        // y = [alpha_delta, alpha_s..., alpha_t..., w, u...]
        // for our cost function, we want to set `sum(u)` to zero
        // Note y >= 0
        //
        // This is useful for eliminating indVars as well as for eliminating `N`
        // We have, for example...
        // b = [I-1, 0, J-1, 0]
        // A = [ 1  -1   0   0
        //       0   0   1  -1 ]
        // N = [I, J]
        // x = [i_s, j_s, i_t, j_t]
        //
        // w + u'N + alpha_delta + alpha_t'i_t - alpha_s'i_s =
        // l_0 + lambda' * (b - A'*x)
        // w + alpha_delta + u_1 * I + u_2 * J + alpha_t_i * i_t + alpha_t_j *
        // j_t - alpha_s_i * i_s - alpha_s_j * j_s = l_0 + lambda_0 * (I - 1 -
        // i_s) + lambda_1
        // * (j_s) + lambda_2 * (J-1 - i_t) + lambda_3 * j_t
        //
        // (w + alpha_delta - l_0 + lambda_0 + lambda_2) + I*(u_1 - lambda_0) +
        // J*(u_2 - lambda_2) + i_t*(alpha_t_i + lambda_2) + j_t *
        // (alpha_t_j-lambda_3) + i_s * (lambda_0 -alpha_s_i) + j_s *
        // (-alpha_s_j-lambda_1) = 0
        //
        // Now...we assume that it is valid to transform this into a system of
        // equations 0 = w + alpha_delta - l_0 + lambda_0 + lambda_2 0 = u_1 -
        // lambda_0 0 = u_2 - lambda_2 0 = alpha_t_i + lambda_2 0 = alpha_t_j -
        // lambda_3 0 = lambda_0 - alpha_s_i 0 = -alpha_s_j - lambda_1
        //
        // A[w*i + x*j]
        // w*(i...)
        // x*(j...)
        // Delinearization seems like the weakest conditions...
        //
        // what about
        // x is symbol, i and j are indvars
        // A[i,j]
        // A[i,x]
        //
        // if (!ar0.allConstantStrides())
        //     return {};
        // if (!ar1.allConstantStrides())
        //     return {};
        // if (ar0.stridesMatch(ar1)) {
        //     return ar0.dim();
        // }
        // TODO: handle these examples that fail above but can be matched:
        // A[0, i, 0, j], A[k, 0, l, 0]
        // B[i, k], B[i, K] // k = 0:K-1
        // B[i, k], B[i, J] // J's relation to k??? -- split loop?
        // size_t dim = 0;
        // auto axesix = ar0.axes.begin();
        // auto axesiy = ar1.axes.begin();

        return {};
    }

    // static bool check(const ArrayReference &ar0, const ArrayReference &ar1) {
    static size_t findFirstNonEqualEven(PtrVector<int64_t> x,
                                        PtrVector<int64_t> y) {
        const size_t M = std::min(x.size(), y.size());
        for (size_t i = 0; i < M; i += 2)
            if (x[i] != y[i])
                return i;
        return M;
    }
    static IntMatrix nullSpace(const MemoryAccess &x, const MemoryAccess &y) {
        const size_t numLoopsCommon =
            findFirstNonEqualEven(x.schedule.getOmega(),
                                  y.schedule.getOmega()) >>
            1;
        const size_t xDim = x.ref.arrayDim();
        const size_t yDim = y.ref.arrayDim();
        IntMatrix A(numLoopsCommon, xDim + yDim);
        if (numLoopsCommon) {
            PtrMatrix<int64_t> indMatX = x.ref.indexMatrix();
            PtrMatrix<int64_t> indMatY = y.ref.indexMatrix();
            for (size_t i = 0; i < numLoopsCommon; ++i) {
                A(i, _(begin, xDim)) = indMatX(i, _);
                A(i, _(xDim, end)) = indMatY(i, _);
            }
            // returns rank x num loops
            return orthogonalNullSpace(std::move(A));
        } else {
            return A;
        }
    }
    //     // TODO: two steps:
    //     // 1: gcd test
    //     // 2: check polyhedra volume
    //     // step 1

    //     // step 2
    //     const llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
    //         maybeDims = matchingStrideConstraintPairs(ar0, ar1);

    //     return true;
    // }

    //   DependencePolyhedra(aln0, aln1, ar0, ar1)
    //
    // dependence from between ma0 and ma1
    // Produces
    // A*x <= b
    // Where x = [inds0..., inds1..., time..]

    DependencePolyhedra(const MemoryAccess &ma0, const MemoryAccess &ma1)
        : Polyhedra<IntMatrix, LinearSymbolicComparator>{} {

        const ArrayReference &ar0 = ma0.ref;
        const ArrayReference &ar1 = ma1.ref;
        const llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
            maybeDims = matchingStrideConstraintPairs(ar0, ar1);

        assert(maybeDims.hasValue());
        const llvm::SmallVector<std::pair<int, int>, 4> &dims =
            maybeDims.getValue();

        auto [nc0, nv0] = ar0.loop->A.size();
        auto [nc1, nv1] = ar1.loop->A.size();
        numDep0Var = ar0.loop->getNumLoops();
        size_t numDep1Var = ar1.loop->getNumLoops();

        std::pair<llvm::SmallVector<unsigned int>,
                  llvm::SmallVector<unsigned int>>
            oldToNewMaps{merge(symbols, ar0.loop->symbols, ar1.loop->symbols)};
        auto &oldToNewMap0 = oldToNewMaps.first;
        auto &oldToNewMap1 = oldToNewMaps.second;
        assert(oldToNewMap0.size() == ar0.loop->symbols.size());
        assert(oldToNewMap1.size() == ar1.loop->symbols.size());
        // numDep1Var = nv1;
        const size_t nc = nc0 + nc1;
        IntMatrix NS{nullSpace(ma0, ma1)};
        const size_t nullDim{NS.numRow()};
        const size_t indexDim{dims.size()};
        nullStep.resize_for_overwrite(nullDim);
        for (size_t i = 0; i < nullDim; ++i) {
            int64_t s = 0;
            for (size_t j = 0; j < NS.numCol(); ++j)
                s += NS(i, j) * NS(i, j);
            nullStep[i] = s;
        }
        //           column meansing in in order
        A.resize(nc, 1 + symbols.size() + numDep0Var + numDep1Var + nullDim);
        E.resize(indexDim + nullDim, A.numCol());
        const size_t numSymbols = getNumSymbols();
        // ar0 loop
        for (size_t i = 0; i < nc0; ++i) {
            A(i, 0) = ar0.loop->A(i, 0);
            for (size_t j = 0; j < oldToNewMap0.size(); ++j)
                A(i, 1 + oldToNewMap0[j]) = ar0.loop->A(i, 1 + j);
            for (size_t j = 0; j < numDep0Var; ++j)
                A(i, j + numSymbols) =
                    ar0.loop->A(i, j + ar0.loop->getNumSymbols());
        }
        for (size_t i = 0; i < nc1; ++i) {
            A(nc0 + i, 0) = ar1.loop->A(i, 0);
            for (size_t j = 0; j < oldToNewMap1.size(); ++j)
                A(nc0 + i, 1 + oldToNewMap1[j]) = ar1.loop->A(i, 1 + j);
            for (size_t j = 0; j < numDep1Var; ++j)
                A(nc0 + i, j + numSymbols + numDep0Var) =
                    ar0.loop->A(i, j + ar1.loop->getNumSymbols());
        }
        PtrMatrix<int64_t> A0 = ar0.indexMatrix();
        PtrMatrix<int64_t> A1 = ar1.indexMatrix();
        PtrMatrix<int64_t> O0 = ar0.offsetMatrix();
        PtrMatrix<int64_t> O1 = ar1.offsetMatrix();
        SHOWLN(O0.numRow());
        // printMatrix(std::cout << "A0 =\n", A0);
        // printMatrix(std::cout << "\nA1 =\n", A1) << std::endl;
        // std::cout << "indexDim = " << indexDim << std::endl;
        // E(i,:)* indVars = q[i]
        // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
        // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
        for (size_t i = 0; i < indexDim; ++i) {
            auto [d0, d1] = dims[i];
            // std::cout << "d0 = " << d0 << "; d1 = " << d1 << std::endl;
            if (d0 >= 0) {
                E(i, 0) = O0(i, 0);
                for (size_t j = 0; j < O0.numCol() - 1; ++j)
                    E(i, 1 + oldToNewMap0[j]) = O0(d0, 1 + j);
                for (size_t j = 0; j < numDep0Var; ++j)
                    E(i, j + numSymbols) = A0(j, d0);
            }
            if (d1 >= 0) {
                E(i, 0) -= O1(i, 0);
                for (size_t j = 0; j < O1.numCol() - 1; ++j)
                    E(i, 1 + oldToNewMap1[j]) -= O1(d1, 1 + j);
                for (size_t j = 0; j < numDep0Var; ++j)
                    E(i, j + numSymbols + numDep0Var) = -A1(j, d1);
            }
        }
        for (size_t i = 0; i < nullDim; ++i) {
            for (size_t j = 0; j < NS.numCol(); ++j) {
                int64_t nsij = NS(i, j);
                E(indexDim + i, j + numSymbols) = nsij;
                E(indexDim + i, j + numSymbols + numDep0Var) = -nsij;
            }
            E(indexDim + i, numSymbols + numDep0Var + numDep1Var + i) = 1;
        }
        SHOWLN(A);
        SHOWLN(E);
        C.init(A, E);
        SHOWLN(*this);
        pruneBounds();
    }
    static size_t getNumLambda(size_t numIneq, size_t numEq) {
        return 1 + numIneq + 2 * numEq;
    }
    size_t getNumLambda() const { return getNumLambda(A.numRow(), E.numRow()); }
    // `direction = true` means second dep follow first
    // lambda_0 + lambda*A*x = delta + c'x
    // x = [s, i]

    // order of variables:
    // [ lambda, schedule coefs on loops, const schedule coef, w, u ]
    //
    //
    // constraint order corresponds to old variables, will be in same order
    //
    // Time parameters are carried over into farkas polys
    std::pair<Simplex, Simplex> farkasPair() const {

        const size_t numEqualityConstraintsOld = E.numRow();
        const size_t numInequalityConstraintsOld = A.numRow();

        const size_t numScheduleCoefs = getNumScheduleCoefficients();
        const size_t numBoundingCoefs = getNumSymbols();

        const size_t numConstraintsNew = A.numCol();
        const size_t numVarInterest = numScheduleCoefs + numBoundingCoefs;

        // PtrMatrix<int64_t> bC{bw.getCostsAndConstraints()};
        // lambda_0 + lambda' * (b - A*i) == psi
        // we represent equal constraint as
        // lambda_0 + lambda' * (b - A*i) - psi <= 0
        // -lambda_0 - lambda' * (b - A*i) + psi <= 0
        // first, lambda_0:
        const size_t ineqEnd = 1 + numInequalityConstraintsOld;
        const size_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
        const size_t numLambda = posEqEnd + numEqualityConstraintsOld;
        const size_t numVarNew = numVarInterest + numLambda;
        assert(numVarNew == posEqEnd + numEqualityConstraintsOld);
        std::pair<Simplex, Simplex> pair;
        Simplex &fw(pair.first);
        fw.resize(numConstraintsNew, numVarNew);
        MutPtrMatrix<int64_t> fC{fw.getCostsAndConstraints()};
        fC(0, 0) = 1; // lambda_0
        fC(_, _(1, ineqEnd)) = A.transpose();
        // fC(_, _(ineqEnd, posEqEnd)) = E.transpose();
        // fC(_, _(posEqEnd, numVarNew)) = -E.transpose();
        // loading from `E` is expensive
        // NOTE: if optimizing expression templates, should also
        // go through and optimize loops like this
        for (size_t j = 0; j < numConstraintsNew; ++j) {
            for (size_t i = 0; i < numEqualityConstraintsOld; ++i) {
                int64_t Eji = E(j, i);
                fC(j, i + ineqEnd) = Eji;
                fC(j, i + posEqEnd) = -Eji;
            }
        }
        // schedule
        // direction = true (aka forward=true)
        // mean x -> y, hence schedule y - schedule x >= 0
        //
        // if direction==true (corresponds to forward==true),
        // [numDep0Var...numVar) - [0...numDep0Var) + offset
        // else
        // [0...numDep0Var) - [numDep0Var...numVar) - offset
        // aka, we have
        // if direction
        // lambda_0 + lambda' * (b - A*i) + [0...numDep0Var) -
        // [numDep0Var...numVar) - offset == 0
        // else
        // lambda_0 + lambda' * (b - A*i) - [0...numDep0Var) +
        // [numDep0Var...numVar) + offset == 0
        //
        // if (direction==true & boundAbove == false){
        //   sign = 1
        // } else {
        //   sign = -1
        // }
        //
        // boundAbove
        // note we'll generally call this function twice, first with
        // 1. `boundAbove = false`
        // 2. `boundAbove = true`
        // boundAbove means we have
        // ... == w + u'*N + psi
        for (size_t i = 0; i < numBoundingCoefs; ++i)
            fC(i, i + numScheduleCoefs + numLambda) = -1;

        // so far, both have been identical
        Simplex &bw(pair.second);
        bw.resize(numConstraintsNew, numVarNew);
        MutPtrMatrix<int64_t> bC{bw.getCostsAndConstraints()};
        for (size_t i = 0; i < numConstraintsNew; ++i)
            for (size_t j = 0; j < numVarNew; ++j)
                bC(i, j) = fC(i, j);

        // equality constraints get expanded into two inequalities
        // a == 0 ->
        // even row: a <= 0
        // odd row: -a <= 0
        // fw means x'Al = x'(depVar1 - depVar0)
        // x'Al + x'(depVar0 - depVar1) = 0
        // so, for fw, depVar0 is positive and depVar1 is negative
        for (size_t i = 0; i < numScheduleCoefs; ++i) {
            int64_t s = (2 * (i < numDep0Var) - 1);
            fC(i + numBoundingCoefs, i + numLambda) = s;
            bC(i + numBoundingCoefs, i + numLambda) = -s;
        }
        // note that delta/constant coef is handled as last `s`
        return pair;
        // fw.removeExtraVariables(numVarKeep);
        // bw.removeExtraVariables(numVarKeep);
        // assert(fw.E.numRow() == fw.q.size());
        // assert(bw.E.numRow() == bw.q.size());
        // return pair;
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    const DependencePolyhedra &p) {
        return printConstraints(
            printConstraints(os << "\n", p.A, p.getNumSymbols()), p.E,
            p.getNumSymbols(), false);
    }

}; // namespace DependencePolyhedra

struct Dependence {
    // Plan here is...
    // depPoly gives the constraints
    // dependenceFwd gives forward constraints
    // dependenceBwd gives forward constraints
    // isForward() indicates whether forward is non-empty
    // isBackward() indicates whether backward is non-empty
    // bounding constraints, used for ILP solve, are reverse,
    // i.e. fwd uses dependenceBwd and bwd uses dependenceFwd.
    //
    // Consider the following simple example dependencies:
    // for (k = 0; k < K; ++k)
    //   for (i = 0; i < I; ++i)
    //     for (j = 0; j < J; ++j)
    //       for (l = 0; l < L; ++l)
    //         A(i, j) = f(A(i+1, j), A(i, j-1), A(j, j), A(j, i), A(i, j -
    //         k))
    // label:     0             1        2          3        4        5
    // We have...
    ////// 0 <-> 1 //////
    // i_0 = i_1 + 1
    // j_0 = j_1
    // null spaces: [k_0, l_0], [k_1, l_1]
    // forward:  k_0 <= k_1 - 1
    //           l_0 <= l_1 - 1
    // backward: k_0 >= k_1
    //           l_0 >= l_1
    //
    //
    ////// 0 <-> 2 //////
    // i_0 = i_1
    // j_0 = j_1 - 1
    // null spaces: [k_0, l_0], [k_1, l_1]
    // forward:  k_0 <= k_1 - 1
    //           l_0 <= l_1 - 1
    // backward: k_0 >= k_1
    //           l_0 >= l_1
    //
    ////// 0 <-> 3 //////
    // i_0 = j_1
    // j_0 = j_1
    // null spaces: [k_0, l_0], [i_1, k_1, l_1]
    // forward:  k_0 <= k_1 - 1
    //           l_0 <= l_1 - 1
    // backward: k_0 >= k_1
    //           l_0 >= l_1
    //
    // i_0 = j_1, we essentially lose the `i` dimension.
    // Thus, to get fwd/bwd, we take the intersection of nullspaces to get
    // the time dimension?
    // TODO: try and come up with counter examples where this will fail.
    //
    ////// 0 <-> 4 //////
    // i_0 = j_1
    // j_0 = i_1
    // null spaces: [k_0, l_0], [k_1, l_1]
    // if j_0 > i_0) [store first]
    //   forward:  k_0 >= k_1
    //             l_0 >= l_1
    //   backward: k_0 <= k_1 - 1
    //             l_0 <= l_1 - 1
    // else (if j_0 <= i_0) [load first]
    //   forward:  k_0 <= k_1 - 1
    //             l_0 <= l_1 - 1
    //   backward: k_0 >= k_1
    //             l_0 >= l_1
    //
    // Note that the dependency on `l` is broken when we can condition on
    // `i_0
    // != j_0`, meaning that we can fully reorder interior loops when we can
    // break dependencies.
    //
    //
    ////// 0 <-> 5 //////
    // i_0 = i_1
    // j_0 = j_1 - k_1
    //
    //
    //
    DependencePolyhedra depPoly;
    Simplex dependenceSatisfaction;
    Simplex dependenceBounding;
    MemoryAccess *in;
    MemoryAccess *out;
    const bool forward;
    // Dependence(DependencePolyhedra depPoly,
    //            IntegerEqPolyhedra dependenceSatisfaction,
    //            IntegerEqPolyhedra dependenceBounding, MemoryAccess *in,
    //            MemoryAccess *out, const bool forward)
    //     : depPoly(std::move(depPoly)),
    //       dependenceSatisfaction(std::move(dependenceSatisfaction)),
    //       dependenceBounding(std::move(dependenceBounding)), in(in),
    //       out(out), forward(forward){};
    // if there is no time dimension, it returns a 0xdim matrix and `R == 0`
    // else, it returns a square matrix, where the first `R` rows correspond
    // to time-axis.
    // static std::pair<IntMatrix, int64_t>
    // transformationMatrix(const ArrayReference &xRef, const ArrayReference
    // &yRef,
    //                      const size_t numLoopsCommon) {
    //     const size_t xDim = xRef.arrayDim();
    //     const size_t yDim = yRef.arrayDim();
    //     PtrMatrix<const int64_t> indMatX = xRef.indexMatrix();
    //     PtrMatrix<const int64_t> indMatY = yRef.indexMatrix();
    //     IntMatrix A(numLoopsCommon, xDim + yDim);
    //     for (size_t i = 0; i < numLoopsCommon; ++i) {
    //         for (size_t j = 0; j < xDim; ++j) {
    //             A(i, j) = indMatX(i, j);
    //         }
    //         for (size_t j = 0; j < yDim; ++j) {
    //             A(i, j + xDim) = indMatY(i, j);
    //         }
    //     }
    //     IntMatrix N = NormalForm::nullSpace(A);
    //     const auto [R, D] = N.size();
    //     if (R) {
    //         N.resizeRows(D);
    //         A = NormalForm::removeRedundantRows(A.transpose());
    //         assert(D - R == A.numRow());
    //         for (size_t r = R; r < D; ++r) {
    //             for (size_t d = 0; d < D; ++d) {
    //                 N(r, d) = A(r - R, d);
    //             }
    //         }
    //     }
    //     return std::make_pair(N, R);
    //     // IntMatrix B = NormalForm::removeRedundantRows(A.transpose());
    //     // const auto [R, D] = B.size();
    //     // if (R < D) {
    //     //     IntMatrix N = NormalForm::nullSpace(A.transpose());
    //     //     assert(N.numRow() == D - R);
    //     //     A.resizeRows(D);
    //     //     for (size_t r = R; r < D; ++r) {
    //     //         for (size_t d = 0; d < D; ++d) {
    //     //             A(r, d) = N(r - R, d);
    //     //         }
    //     //     }
    //     // }
    //     // return std::make_pair(A, R);
    // }

    // static std::pair<IntMatrix, int64_t>
    // transformationMatrix(const MemoryAccess &x, const MemoryAccess &y) {
    //     return transformationMatrix(
    //         x.ref, y.ref,
    //         findFirstNonEqualEven(x.schedule.getOmega(),
    //                               y.schedule.getOmega()) >>
    //             1);
    // }
    // emplaces dependencies without any repeat accesses to the same memory
    // returns
    size_t getNumLambda() const { return depPoly.getNumLambda() << 1; }
    size_t getNumSymbols() const { return depPoly.getNumSymbols(); }
    size_t getNumConstraints() const {
        return dependenceBounding.getNumConstraints() +
               dependenceSatisfaction.getNumConstraints();
    }
    // order of variables:
    // [ lambda, schedule coefs on loops, const schedule coef, w, u ]
    void copyLambda(MutPtrMatrix<int64_t> A, MutPtrMatrix<int64_t> B,
                    MutPtrMatrix<int64_t> C,
                    MutStridedVector<int64_t> d) const {
        // const size_t numBoundingConstraints =
        //     dependenceBounding.getNumConstraints();
        const size_t numSchedulingConstraints =
            dependenceSatisfaction.getNumConstraints();
        const size_t halfLambda = depPoly.getNumLambda();
        assert(A.numRow() == getNumConstraints());
        assert(A.numCol() == getNumLambda());
        PtrMatrix<int64_t> sC{dependenceSatisfaction.getCostsAndConstraints()};
        PtrMatrix<int64_t> bC{dependenceBounding.getCostsAndConstraints()};
        auto rS = _(begin, numSchedulingConstraints);
        auto rB = _(numSchedulingConstraints, end);
        d(rS) = sC(_, 0);
        d(rB) = bC(_, 0);
        const size_t i = 1 + halfLambda;
        // copyto(A(rS, _(begin, halfLambda)), sC(_, _(1, i)));
        A(rS, _(begin, halfLambda)) = sC(_, _(1, i));
        A(rB, _(halfLambda, end)) = bC(_, _(1, i));
        const size_t numScheduleCoefs = depPoly.getNumScheduleCoefficients();
        const size_t j = i + numScheduleCoefs;
        B(rS, _) = sC(_, _(i, j));
        B(rB, _) = bC(_, _(i, j));
        const size_t numSym = getNumSymbols();
        const size_t k = j + numSym;
        assert(k == sC.numCol());
        assert(k == bC.numCol());
        C(rS, _(begin, numSym)) = sC(_, _(j, k));
        C(rB, _(numSym, end)) = bC(_, _(j, k));
    }

    static bool checkDirection(const std::pair<Simplex, Simplex> &p,
                               const MemoryAccess &x, const MemoryAccess &y,
                               size_t numLambda) {
        const Simplex &fxy = p.first;
        const Simplex &fyx = p.second;
        const size_t numLoopsX = x.ref.getNumLoops();
        const size_t numLoopsY = y.ref.getNumLoops();
        const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
        const size_t numLoopsTotal = numLoopsX + numLoopsY;
        SquarePtrMatrix<int64_t> xPhi = x.schedule.getPhi();
        SquarePtrMatrix<int64_t> yPhi = y.schedule.getPhi();
        PtrVector<int64_t> xOmega = x.schedule.getOmega();
        PtrVector<int64_t> yOmega = y.schedule.getOmega();
        Vector<int64_t> sch;
        sch.resizeForOverwrite(numLoopsTotal + 1);
        // const size_t numLambda = DependencePolyhedra::getNumLambda();
        for (size_t i = 0; i <= numLoopsCommon; ++i) {
            if (int64_t o2idiff = yOmega[2 * i] - xOmega[2 * i])
                return o2idiff > 0;
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
            for (size_t j = 0; j < numLoopsX; ++j)
                sch[j] = xPhi(i, j);
            for (size_t j = 0; j < numLoopsY; ++j)
                sch[j + numLoopsX] = yPhi(i, j);
            int64_t yO = yOmega[2 * i + 1], xO = xOmega[2 * i + 1];
            // forward means offset is 2nd - 1st
            sch[numLoopsTotal] = yO - xO;
            if (fxy.unSatisfiable(sch, numLambda))
                return false;
            // backward means offset is 1st - 2nd
            sch[numLoopsTotal] = xO - yO;
            if (fyx.unSatisfiable(sch, numLambda))
                return true;
        }
        assert(false);
        return false;
    }
    static void timelessCheck(llvm::SmallVectorImpl<Dependence> &deps,
                              const DependencePolyhedra &dxy, MemoryAccess &x,
                              MemoryAccess &y) {
        std::pair<Simplex, Simplex> pair(dxy.farkasPair());
        const size_t numLambda = 1 + dxy.getNumInequalityConstraints() +
                                 2 * dxy.getNumEqualityConstraints();
        const size_t numVarKeep = pair.first.getNumVar() - numLambda;
        pair.first.removeExtraVariables(numVarKeep);
        pair.second.removeExtraVariables(numVarKeep);
        if (checkDirection(pair, x, y, numLambda)) {
            pair.first.truncateVars(numLambda +
                                    dxy.getNumScheduleCoefficients());
            deps.emplace_back(Dependence{std::move(dxy), std::move(pair.first),
                                         std::move(pair.second), &x, &y, true});
        } else {
            pair.second.truncateVars(numLambda +
                                     dxy.getNumScheduleCoefficients());
            deps.emplace_back(Dependence{std::move(dxy), std::move(pair.second),
                                         std::move(pair.first), &y, &x, false});
        }
    }

    // emplaces dependencies with repeat accesses to the same memory across
    // time
    static void timeCheck(llvm::SmallVectorImpl<Dependence> &deps,
                          DependencePolyhedra dxy, MemoryAccess &x,
                          MemoryAccess &y) {
        std::pair<Simplex, Simplex> pair(dxy.farkasPair());
        // copy backup
        std::pair<Simplex, Simplex> farkasBackups = pair;
        const size_t numInequalityConstraintsOld =
            dxy.getNumInequalityConstraints();
        const size_t numEqualityConstraintsOld =
            dxy.getNumEqualityConstraints();
        const size_t ineqEnd = 1 + numInequalityConstraintsOld;
        const size_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
        const size_t numLambda = posEqEnd + numEqualityConstraintsOld;
        const size_t numVarKeep = pair.first.getNumVar() - numLambda;
        const size_t numScheduleCoefs = dxy.getNumScheduleCoefficients();
        pair.first.removeExtraVariables(numVarKeep);
        pair.second.removeExtraVariables(numVarKeep);
        MemoryAccess *in = &x, *out = &y;
        const bool isFwd = checkDirection(pair, x, y, numLambda);
        if (isFwd) {
            std::swap(farkasBackups.first, farkasBackups.second);
        } else {
            std::swap(in, out);
            std::swap(pair.first, pair.second);
        }
        pair.first.truncateVars(numLambda + numScheduleCoefs);
        // pair.first.removeExtraVariables(numScheduleCoefs);
        deps.emplace_back(Dependence{dxy, std::move(pair.first),
                                     std::move(pair.second), in, out, isFwd});
        // pair is invalid
        const size_t timeDim = dxy.getTimeDim();
        assert(timeDim);
        const size_t numVarOld = dxy.A.numCol();
        const size_t numVar = numVarOld - timeDim;
        // const size_t numBoundingCoefs = numVarKeep - numLambda;
        // remove the time dims from the deps
        deps.back().depPoly.truncateVars(numVar);
        // deps.back().depPoly.removeExtraVariables(numVar);
        // now we need to check the time direction for all times
        // anything approaching 16 time dimensions would be absolutely
        // insane
        llvm::SmallVector<bool, 16> timeDirection(timeDim);
        size_t t = 0;
        auto fE{farkasBackups.first.getCostsAndConstraints()};
        auto sE{farkasBackups.second.getCostsAndConstraints()};
        do {
            // set `t`th timeDim to +1/-1
            size_t v = numVar + t;
            for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
                int64_t Acv = dxy.A(c, v);
                fE(0, 1 + c) -= Acv; // *1
                sE(0, 1 + c) -= Acv; // *1
            }
            for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
                // each of these actually represents 2 inds
                int64_t Ecv = dxy.E(c, v);
#ifndef NDEBUG
                if (Ecv) {
                    std::cout << "Found non-0: E(" << c << ", " << v
                              << ") = " << Ecv << std::endl;
                }
#endif
                fE(0, c + ineqEnd) -= Ecv;
                fE(0, c + posEqEnd) += Ecv;
                sE(0, c + ineqEnd) -= Ecv;
                sE(0, c + posEqEnd) += Ecv;
            }
            pair = farkasBackups;
            pair.first.removeExtraVariables(numVarKeep);
            pair.second.removeExtraVariables(numVarKeep);
            // farkasBackups is swapped with respect to
            // checkDirection(..., *in, *out);
            timeDirection[t] = checkDirection(pair, *out, *in, numLambda);
            // fix
            for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
                int64_t Acv = dxy.A(c, v);
                fE(0, c + 1) += Acv;
                sE(0, c + 1) += Acv;
            }
            for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
                // each of these actually represents 2 inds
                int64_t Ecv = dxy.E(c, v);
                fE(0, c + ineqEnd) += Ecv;
                fE(0, c + posEqEnd) -= Ecv;
                sE(0, c + ineqEnd) += Ecv;
                sE(0, c + posEqEnd) -= Ecv;
            }
        } while (++t < timeDim);
        t = 0;
        do {
            // checkDirection(farkasBackups, x, y, numLambda) == false
            // correct time direction would make it return true
            // thus sign = timeDirection[t] ? 1 : -1
            int64_t step = (2 * timeDirection[t] - 1) * dxy.nullStep[t];
            size_t v = numVar + t;
            for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
                int64_t Acv = dxy.A(c, v) * step;
                dxy.A(c, 0) -= Acv;
                fE(0, c + 1) -= Acv; // *1
                sE(0, c + 1) -= Acv; // *-1
            }
            for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
                // each of these actually represents 2 inds
                int64_t Ecv = dxy.E(c, v) * step;
                dxy.E(c, 0) -= Ecv;
                fE(0, c + ineqEnd) -= Ecv;
                fE(0, c + posEqEnd) += Ecv;
                sE(0, c + ineqEnd) -= Ecv;
                sE(0, c + posEqEnd) += Ecv;
            }
        } while (++t < timeDim);
#ifndef NDEBUG
        std::cout << "time dxy = \n" << dxy << std::endl;
#endif
        dxy.truncateVars(numVar);
#ifndef NDEBUG
        std::cout << "after 0ing, time dxy = \n" << dxy << std::endl;
#endif
        farkasBackups.first.truncateVars(numLambda + numScheduleCoefs);
        deps.emplace_back(
            Dependence{std::move(dxy), std::move(farkasBackups.first),
                       std::move(farkasBackups.second), out, in, !isFwd});
    }

    static size_t check(llvm::SmallVectorImpl<Dependence> &deps,
                        MemoryAccess &x, MemoryAccess &y) {
        // static void check(llvm::SmallVectorImpl<Dependence> deps,
        //                   const ArrayReference &x, const Schedule &sx,
        //                   const ArrayReference &y, const Schedule &sy) {
        if (x.ref.gcdKnownIndependent(y.ref))
            return 0;
        DependencePolyhedra dxy(x, y);
        if (dxy.isEmpty())
            return 0;
            // note that we set boundAbove=true, so we reverse the
            // dependence direction for the dependency we week, we'll
            // discard the program variables x then y
#ifndef NDEBUG
        std::cout << "x = " << x.ref << "\ny = " << y.ref << "\ndxy = \n"
                  << dxy << std::endl;
#endif
        if (dxy.getTimeDim()) {
            timeCheck(deps, std::move(dxy), x, y);
            return 2;
        } else {
            timelessCheck(deps, std::move(dxy), x, y);
            return 1;
        }
        // auto [R, nullDim] = transformationMatrix(x, y);
        // if (nullDim) {
        //    return timeCheck(deps, std::move(dxy), std::move(R), nullDim,
        //    x, y);
        // } else {

        //}
    }

    friend std::ostream &operator<<(std::ostream &os, Dependence &d) {
        os << "Dependence Poly ";
        if (d.forward) {
            os << "x -> y:\n";
        } else {
            os << "y -> x:\n";
        }
        return os << d.depPoly << std::endl;
        // << "\nSchedule Constraints:\n"
        //           << d.dependenceSatisfaction << "\nBounding Constraints:\n"
        //           << d.dependenceBounding << std::endl;
    }
};
