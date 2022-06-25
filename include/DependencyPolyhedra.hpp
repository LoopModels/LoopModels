#pragma once

#include "./AbstractEqualityPolyhedra.hpp"
#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./POSet.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Symbolics.hpp"
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
    size_t numDep0Var, numDep1Var;
    size_t getTimeDim() const { return getNumVar() - numDep0Var - numDep1Var; }
    size_t getNumEqualityConstraints() const { return q.size(); }
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
            for (size_t i = 0; i < numDims; ++i) {
                dims.emplace_back(i, i);
            }
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
    static size_t findFirstNonEqualEven(llvm::ArrayRef<int64_t> x,
                                        llvm::ArrayRef<int64_t> y) {
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
            PtrMatrix<const int64_t> indMatX = x.ref.indexMatrix();
            PtrMatrix<const int64_t> indMatY = y.ref.indexMatrix();
            for (size_t i = 0; i < numLoopsCommon; ++i) {
                for (size_t j = 0; j < xDim; ++j) {
                    A(i, j) = indMatX(i, j);
                }
                for (size_t j = 0; j < yDim; ++j) {
                    A(i, j + xDim) = indMatY(i, j);
                }
            }
            // returns rank x num loops
            return NormalForm::nullSpace(std::move(A));
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
        : SymbolicEqPolyhedra(IntMatrix(), llvm::SmallVector<MPoly, 8>(),
                              IntMatrix(), llvm::SmallVector<MPoly, 8>(),
                              ma0.ref.loop->poset) {

        const ArrayReference &ar0 = ma0.ref;
        const ArrayReference &ar1 = ma0.ref;
        const llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
            maybeDims = matchingStrideConstraintPairs(ar0, ar1);

        assert(maybeDims.hasValue());
        const llvm::SmallVector<std::pair<int, int>, 4> &dims =
            maybeDims.getValue();

        auto [nc0, nv0] = ar0.loop->A.size();
        auto [nc1, nv1] = ar1.loop->A.size();
        numDep0Var = nv0;
        numDep1Var = nv1;
        const size_t nc = nc0 + nc1;
        IntMatrix NS(nullSpace(ma0, ma1));
        const size_t nullDim(NS.numRow());
        const size_t indexDim(dims.size());
        A.resize(nc, nv0 + nv1 + nullDim);
        E.resize(indexDim + nullDim, nv0 + nv1 + nullDim);
        q.resize(indexDim + nullDim);
        // ar0 loop
        for (size_t i = 0; i < nc0; ++i) {
            for (size_t j = 0; j < nv0; ++j) {
                A(i, j) = ar0.loop->A(i, j);
            }
            b.push_back(ar0.loop->b[i]);
        }
        // ar1 loop
        for (size_t i = 0; i < nc1; ++i) {
            for (size_t j = 0; j < nv1; ++j) {
                A(nc0 + i, nv0 + j) = ar1.loop->A(i, j);
            }
            b.push_back(ar1.loop->b[i]);
        }
        auto A0 = ar0.indexMatrix();
        auto A1 = ar1.indexMatrix();
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
                for (size_t j = 0; j < nv0; ++j) {
                    E(i, j) = A0(j, d0);
                }
                q[i] = -ar0.stridesOffsets[d0].second;
            }
            if (d1 >= 0) {
                for (size_t j = 0; j < nv1; ++j) {
                    E(i, j + nv0) = -A1(j, d1);
                }
                q[i] += ar1.stridesOffsets[d1].second;
            }
        }
        for (size_t i = 0; i < nullDim; ++i) {
            for (size_t j = 0; j < NS.numCol(); ++j) {
                int64_t nsij = NS(i, j);
                E(indexDim + i, j) = nsij;
                E(indexDim + i, j + nv0) = -nsij;
            }
            E(indexDim + i, nv0 + nv1 + i) = 1;
        }
#ifndef NDEBUG
        std::cout << "Assembling constraint matrices:" << std::endl;
        printConstraints(printConstraints(std::cout, A, b, true), E, q, false)
            << std::endl;
        std::cout << "Done printing assembled, pre-pruned, matrices."
                  << std::endl;
#endif
        if (pruneBounds()) {
            A.clear();
            b.clear();
            E.clear();
            q.clear();
        }
    }
    // `direction = true` means second dep follow first
    // order of variables:
    // [ schedule coefs on loops, const schedule coef, bounding coefs ]
    //
    // Order of constraints:
    // a) constant eq
    // b) old vars eq
    // c) constant terms eq
    // d) bound above eq
    //
    // Time parameters are carried over into faras polies
    std::pair<IntegerEqPolyhedra, IntegerEqPolyhedra> farkasPair() const {

        llvm::DenseMap<Polynomial::Monomial, unsigned> constantTerms;
        for (auto &bi : b) {
            for (auto &t : bi) {
                if (!t.isCompileTimeConstant()) {
                    constantTerms.insert(
                        std::make_pair(t.exponent, constantTerms.size()));
                }
            }
        }
        auto [numInequalityContraintsOld, numVarOld] = A.size();
        // delta + 1 coef per
        size_t timeDim = getTimeDim();
        size_t numScheduleCoefs = 1 + numVarOld - timeDim;
        size_t numEqualityConstraintsOld = E.numRow();
        size_t numLambda =
            1 + numInequalityContraintsOld + 2 * numEqualityConstraintsOld;
        size_t numConstantTerms = constantTerms.size();
        size_t numBoundingCoefs = 1 + numConstantTerms;
        size_t numVarKeep = numScheduleCoefs + numBoundingCoefs;
        size_t numVarNew = numVarKeep + timeDim + numLambda;
        // constraint order
        // t_0 = either -1, 0, or 1
        // d + p_0*k_0 - p_1*k_1 = l_0 + l_1 * (k_0 - k_1 + t_0)
        size_t numInequalityConstraints = numBoundingCoefs + numLambda;
        size_t numEqualityConstraints = 1 + numVarOld + numConstantTerms;

        std::pair<IntegerEqPolyhedra, IntegerEqPolyhedra> pair(std::make_pair(
            IntegerEqPolyhedra(numInequalityConstraints, numEqualityConstraints,
                               numVarNew),
            IntegerEqPolyhedra(numInequalityConstraints, numEqualityConstraints,
                               numVarNew)));

        IntegerEqPolyhedra &fw(pair.first);
        IntegerEqPolyhedra &bw(pair.second);
        // lambda_0 + lambda' * (b - A*i) == psi
        // we represent equal constraint as
        // lambda_0 + lambda' * (b - A*i) - psi <= 0
        // -lambda_0 - lambda' * (b - A*i) + psi <= 0
        // first, lambda_0:
        fw.E(0, numVarKeep) = 1;
        bw.E(0, numVarKeep) = 1;
        for (size_t c = 0; c < numInequalityContraintsOld; ++c) {
            size_t lambdaInd = numScheduleCoefs + numBoundingCoefs + c + 1;
            for (size_t v = 0; v < numVarOld; ++v) {
                fw.E(1 + v, lambdaInd) = -A(c, v);
                bw.E(1 + v, lambdaInd) = -A(c, v);
            }
            for (auto &t : b[c]) {
                if (auto c = t.getCompileTimeConstant()) {
                    fw.E(0, lambdaInd) = c.getValue();
                    bw.E(0, lambdaInd) = c.getValue();
                } else {
                    size_t constraintInd =
                        constantTerms[t.exponent] + numVarOld + 1;
                    fw.E(constraintInd, lambdaInd) = t.coefficient;
                    bw.E(constraintInd, lambdaInd) = t.coefficient;
                }
            }
        }
        for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
            // each of these actually represents 2 inds
            size_t lambdaInd = numScheduleCoefs + numBoundingCoefs +
                               numInequalityContraintsOld + 2 * c;
            for (size_t v = 0; v < numVarOld; ++v) {
                fw.E(1 + v, lambdaInd + 1) = -E(c, v);
                fw.E(1 + v, lambdaInd + 2) = E(c, v);
                bw.E(1 + v, lambdaInd + 1) = -E(c, v);
                bw.E(1 + v, lambdaInd + 2) = E(c, v);
            }
            for (auto &t : q[c]) {
                if (auto c = t.getCompileTimeConstant()) {
                    fw.E(0, lambdaInd + 1) = c.getValue();
                    fw.E(0, lambdaInd + 2) = -c.getValue();
                    bw.E(0, lambdaInd + 1) = c.getValue();
                    bw.E(0, lambdaInd + 2) = -c.getValue();
                } else {
                    size_t constraintInd =
                        constantTerms[t.exponent] + numVarOld + 1;
                    fw.E(constraintInd, lambdaInd + 1) = t.coefficient;
                    fw.E(constraintInd, lambdaInd + 2) = -t.coefficient;
                    bw.E(constraintInd, lambdaInd + 1) = t.coefficient;
                    bw.E(constraintInd, lambdaInd + 2) = -t.coefficient;
                }
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
        // equality constraints get expanded into two inequalities
        // a == 0 ->
        // even row: a <= 0
        // odd row: -a <= 0
        for (size_t i = 0; i < numVarOld; ++i) {
            int64_t s = (2 * (i < numDep0Var) - 1);
            fw.E(1 + i, i) = s;
            bw.E(1 + i, i) = -s;
        }
        // delta/constant coef at ind numVarOld
        fw.E(0, numVarOld) = -1;
        bw.E(0, numVarOld) = 1;
        // boundAbove
        // note we'll generally call this function twice, first with
        // 1. `boundAbove = false`
        // 2. `boundAbove = true`
        // boundAbove means we have
        // ... == w + u'*N + psi
        fw.E(0, numScheduleCoefs) = -1;
        fw.A(0, numScheduleCoefs) = -1;
        bw.E(0, numScheduleCoefs) = -1;
        bw.A(0, numScheduleCoefs) = -1;
        for (size_t i = 0; i < numConstantTerms; ++i) {
            size_t ip1 = i + 1;
            size_t constraintInd = (ip1 + numVarOld);
            fw.E(constraintInd, i + numScheduleCoefs + 1) = -1;
            fw.A(ip1, numScheduleCoefs + ip1) = -1;
            bw.E(constraintInd, i + numScheduleCoefs + 1) = -1;
            bw.A(ip1, numScheduleCoefs + ip1) = -1;
        }
        // all lambda > 0
        for (size_t i = 0; i < numLambda; ++i) {
            fw.A(numBoundingCoefs + i, numVarKeep + i) = -1;
            bw.A(numBoundingCoefs + i, numVarKeep + i) = -1;
        }
        fw.removeExtraVariables(numVarKeep);
        bw.removeExtraVariables(numVarKeep);
        assert(fw.E.numRow() == fw.q.size());
        assert(bw.E.numRow() == bw.q.size());
        return pair;
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
    //         A(i, j) = f(A(i+1, j), A(i, j-1), A(j, j), A(j, i), A(i, j - k))
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
    // Thus, to get fwd/bwd, we take the intersection of nullspaces to get the
    // time dimension?
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
    // Note that the dependency on `l` is broken when we can condition on `i_0
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
    IntegerEqPolyhedra dependenceSatisfaction;
    IntegerEqPolyhedra dependenceBounding;
    MemoryAccess *in;  // memory access in
    MemoryAccess *out; // memory access out
    uint8_t direction;
    static constexpr uint8_t forwardFlag = 0x01;
    static constexpr uint8_t backwardFlag = 0x02;
    bool isForward() const { return direction & forwardFlag; }
    bool isBackward() const { return direction & backwardFlag; }

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
    static size_t timelessCheck(llvm::SmallVectorImpl<Dependence> &deps,
                                DependencePolyhedra dxy, MemoryAccess &x,
                                MemoryAccess &y) {
        IntegerEqPolyhedra fxy(dxy.farkasScheduleDifference(true, false));
        // y then x
        IntegerEqPolyhedra fyx(dxy.farkasScheduleDifference(true, true));
        const size_t numLoopsX = x.ref.getNumLoops();
        const size_t numLoopsY = y.ref.getNumLoops();
        const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
        const size_t numLoopsTotal = numLoopsX + numLoopsY;
        SquarePtrMatrix<const int64_t> xPhi = x.schedule.getPhi();
        SquarePtrMatrix<const int64_t> yPhi = y.schedule.getPhi();
        llvm::ArrayRef<int64_t> xOmega = x.schedule.getOmega();
        llvm::ArrayRef<int64_t> yOmega = y.schedule.getOmega();
        llvm::SmallVector<int64_t, 16> sch;
        sch.resize_for_overwrite(numLoopsTotal + 1);
        for (size_t i = 0; i <= numLoopsCommon; ++i) {
            if (int64_t o2idiff = yOmega[2 * i] - xOmega[2 * i]) {
                MemoryAccess *input = &x;
                MemoryAccess *output = &y;
                uint8_t direction = forwardFlag;
                if ((o2idiff < 0)) {
                    // backward
                    direction <<= 1;
                    std::swap(fxy, fyx);
                    std::swap(input, output);

                    // fxy.A.truncateCols(numLoopsTotal + 1);
                    // fxy.E.truncateCols(numLoopsTotal + 1);
                    // fxy.dropEmptyConstraints();
                    // // x then y
                    // return Dependence{dxy, fxy, fyx};
#ifndef NDEBUG
                    std::cout << "dep order 0; i = " << i << std::endl;
                } else {
                    std::cout << "dep order 1; i = " << i << std::endl;
#endif
                }
                fxy.A.truncateCols(numLoopsTotal + 1);
                fxy.E.truncateCols(numLoopsTotal + 1);
                fxy.dropEmptyConstraints();
                // x then y
                Dependence dep{std::move(dxy), std::move(fxy), std::move(fyx),
                               input,          output,         direction};
                deps.push_back(std::move(dep));
                // deps.emplace_back(std::move(dxy), std::move(fyx),
                //                   std::move(fxy), input, output);
                return 1;
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
                sch[j] = xPhi(i, j);
            }
            for (size_t j = 0; j < numLoopsY; ++j) {
                sch[j + numLoopsX] = yPhi(i, j);
            }
            int64_t yO = yOmega[2 * i + 1], xO = xOmega[2 * i + 1];
            // forward means offset is 2nd - 1st
            sch[numLoopsTotal] = yO - xO;
            printVector(std::cout << "fxy =\n"
                                  << fxy << "Schedule = ",
                        sch)
                << std::endl
                << std::endl;
            if (!fxy.knownSatisfied(sch)) {
                fyx.A.truncateCols(numLoopsTotal + 1);
                fyx.E.truncateCols(numLoopsTotal + 1);
                fyx.dropEmptyConstraints();
#ifndef NDEBUG
                std::cout << "dep order 2; i = " << i << std::endl;
#endif
                // y then x
                Dependence dep{
                    std::move(dxy), std::move(fyx), std::move(fxy), &y, &x,
                    backwardFlag};
                deps.push_back(std::move(dep));
                // deps.emplace_back(std::move(dxy), std::move(fyx),
                //                   std::move(fxy), &y, &x);
                return 1;
            }
            // backward means offset is 1st - 2nd
            sch[numLoopsTotal] = xO - yO;
            if (!fyx.knownSatisfied(sch)) {
                fxy.A.truncateCols(numLoopsTotal + 1);
                fxy.E.truncateCols(numLoopsTotal + 1);
                fxy.dropEmptyConstraints();
#ifndef NDEBUG
                std::cout << "dep order 3; i= " << i << std::endl;
#endif
                Dependence dep{
                    std::move(dxy), std::move(fxy), std::move(fyx), &x, &y,
                    forwardFlag};
                deps.push_back(std::move(dep));
                // deps.emplace_back(std::move(dxy), std::move(fxy),
                //                   std::move(fyx), &x, &y);
                return 1;
            }
        }
        return 0;
    }

    // emplaces dependencies with repeat accesses to the same memory across time
    static size_t timeCheck(llvm::SmallVectorImpl<Dependence> &deps,
                            DependencePolyhedra dxy, IntMatrix R,
                            size_t nullDims, MemoryAccess &x, MemoryAccess &y) {
        // first nullDims of `R` are nullDims
        assert(false);
        return 2;
    }

    static size_t check(llvm::SmallVectorImpl<Dependence> &deps,
                        MemoryAccess &x, MemoryAccess &y) {
        // static void check(llvm::SmallVectorImpl<Dependence> deps,
        //                   const ArrayReference &x, const Schedule &sx,
        //                   const ArrayReference &y, const Schedule &sy) {
        if (x.ref.gcdKnownIndependent(y.ref))
            return 0;
#ifndef NDEBUG
        std::cout << "&x = " << &x << std::endl;
        std::cout << "&x.ref = " << &x.ref << std::endl;
        std::cout << "x.ref.loop = " << *(x.ref.loop) << std::endl;
        std::cout << "x.ref.loop.get() = " << x.ref.loop.get() << std::endl;
        std::cout << "x.ref.loop->poset.delta.size() = "
                  << x.ref.loop->poset.delta.size() << std::endl;
#endif
        DependencePolyhedra dxy(x.ref, y.ref);
        if (dxy.isEmpty())
            return 0;
            // note that we set boundAbove=true, so we reverse the dependence
            // direction for the dependency we week, we'll discard the program
            // variables x then y
#ifndef NDEBUG
        std::cout << "x = " << x.ref << "\ny = " << y.ref << "\ndxy = \n"
                  << dxy << std::endl;
#endif
        // auto [R, nullDim] = transformationMatrix(x, y);
        // if (nullDim) {
        //    return timeCheck(deps, std::move(dxy), std::move(R), nullDim, x,
        //    y);
        // } else {
        return timelessCheck(deps, std::move(dxy), x, y);
        //}
    }

    friend std::ostream &operator<<(std::ostream &os, Dependence &d) {
        os << "Dependence Poly ";
        if (d.isForward()) {
            os << "x -> y:\n";
        } else {
            os << "y -> x:\n";
        }
        return os << d.depPoly << "\nSchedule Constraints:\n"
                  << d.dependenceSatisfaction << "\nBounding Constraints:\n"
                  << d.dependenceBounding << std::endl;
    }
};
