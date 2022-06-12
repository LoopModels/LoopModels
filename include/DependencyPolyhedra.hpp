#pragma once

#include "./AbstractEqualityPolyhedra.hpp"
#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Symbolics.hpp"
#include <cstdint>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/User.h>
#include <utility>

struct DependencePolyhedra : SymbolicEqPolyhedra {
    size_t numDep0Var;
    bool forward; // if (forward){ dep0 -> dep1; } else { dep1 -> depo; }

    size_t getNumEqualityConstraints() const { return q.size(); }
    static llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
    matchingStrideConstraintPairs(const ArrayReference &ar0,
                                  const ArrayReference &ar1) {
#ifndef NDEBUG
        std::cout << "ar0 = \n" << ar0 << "\nar1 = " << ar1 << std::endl;
#endif
        // fast path; most common case
        if (ar0.stridesMatchAllConstant(ar1)) {
            llvm::SmallVector<std::pair<int, int>, 4> dims;
            size_t numDims = ar0.dim();
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
    // dependence from aln0 (src) -> aln1 (tgt)
    // Produces
    // A'*x <= b
    // Where x = [c_src, c_tgt, beta_src..., beta_tgt]
    // layout of constraints (based on Farkas equalities):
    // comp time constant, indVars0, indVars1, loop constants
    DependencePolyhedra(const ArrayReference &ar0, const ArrayReference &ar1)
        : SymbolicEqPolyhedra(Matrix<int64_t, 0, 0, 0>(),
                              llvm::SmallVector<MPoly, 8>(),
                              Matrix<int64_t, 0, 0, 0>(),
                              llvm::SmallVector<MPoly, 8>(), ar0.loop->poset) {

        const llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
            maybeDims = matchingStrideConstraintPairs(ar0, ar1);

        assert(maybeDims.hasValue());
        const llvm::SmallVector<std::pair<int, int>, 4> &dims =
            maybeDims.getValue();

        auto [nv0, nc0] = ar0.loop->A.size();
        auto [nv1, nc1] = ar1.loop->A.size();
        numDep0Var = nv0;
        const size_t nc = nc0 + nc1;
        A.resize(nv0 + nv1, nc);
        E.resize(nv0 + nv1, dims.size());
        // ar0 loop
        for (size_t i = 0; i < nc0; ++i) {
            for (size_t j = 0; j < nv0; ++j) {
                A(j, i) = ar0.loop->A(j, i);
            }
            b.push_back(ar0.loop->b[i]);
        }
        // ar1 loop
        for (size_t i = 0; i < nc1; ++i) {
            for (size_t j = 0; j < nv1; ++j) {
                A(nv0 + j, nc0 + i) = ar1.loop->A(j, i);
            }
            b.push_back(ar1.loop->b[i]);
        }
        for (size_t i = 0; i < dims.size(); ++i) {
            auto [d0, d1] = dims[i];
            Stride delta;
            if (d0 < 0) {
                delta = ar1.axes[d1];
            } else if (d1 < 0) {
                delta = ar0.axes[d0];
            } else {
                // TODO: check if strides match
                // or perhaps, edit `matchingStrideConstraintPairs` so that
                // it mutates the reference into matching strides?
                // but, then how to represent?
                // Probably simpler to always have `VarType::Constant`
                // correspond to `1` and then do a gcd/div on strides here.
                delta = ar1.axes[d1];
                // offset all loop indvars for ar1
                for (auto &&ind : delta) {
                    auto &[coef, var] = ind;
                    if (var.isIndVar()) {
                        var.id += ar0.loop->getNumVar();
                    }
                }
                delta -= ar0.axes[d0];
            }
            // now we add delta >= 0 and -delta >= 0
            MPoly bound;
            for (auto &ind : delta) {
                auto &[coef, var] = ind;
                auto [typ, id] = var.getTypeAndId();
                switch (typ) {
                case VarType::LoopInductionVariable:
                    // need this to be a compile time constant
                    // TODO: handle the case gracefully where it isn't!!!
                    { // limit scope of `c`
                        int64_t c = coef.getCompileTimeConstant().getValue();
                        // id gives the loop, which yields the Farkas constraint
                        // it contributed to, i.e. the column of `As` to store
                        // into. `i`, the dim number, yields the associated
                        // labmda.
                        E(id, i) = c;
                        // A(id, nc + (i << 1)) = c;
                        // A(id, nc + (i << 1) + 1) = -c;
                    }
                    break;
                case VarType::Constant: {
                    bound += coef;
                    break;
                }
                default:
                    // break;
                    assert(false);
                }
            }
            q.push_back(-std::move(bound));
            // b.push_back(-bound);
            // b.push_back(std::move(bound));
        }
#ifndef NDEBUG
        printConstraints(printConstraints(std::cout, A, b, true), E, q, false)
            << std::endl;
#endif
        if (pruneBounds()) {
            A.clear();
            b.clear();
            E.clear();
            q.clear();
        }
    }
    IntegerEqPolyhedra farkasScheduleDifference(bool boundAbove) {
        return farkasScheduleDifference(boundAbove, forward);
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
    IntegerEqPolyhedra farkasScheduleDifference(bool boundAbove,
                                                bool direction) {

        llvm::DenseMap<Polynomial::Monomial, size_t> constantTerms;
        for (auto &bi : b) {
            for (auto &t : bi) {
                if (!t.isCompileTimeConstant()) {
                    constantTerms.insert(
                        std::make_pair(t.exponent, constantTerms.size()));
                }
            }
        }
        auto [numVarOld, numInequalityContraintsOld] = A.size();
        // delta + 1 coef per
        size_t numScheduleCoefs = 1 + numVarOld;
        size_t numEqualityConstraintsOld = E.numCol();
        size_t numLambda =
            1 + numInequalityContraintsOld + 2 * numEqualityConstraintsOld;
        size_t numConstantTerms = constantTerms.size();
        size_t numBoundingCoefs = boundAbove ? 1 + numConstantTerms : 0;
        // var order
        // FIXME: seems to be broken; too many constraints and variables
        // appear to be created here.
        // TODO: Add some explicit Farkas tests for easier debugging/
        // finer grained testing.
        size_t numVarKeep = numScheduleCoefs + numBoundingCoefs;
        size_t numVarNew = numVarKeep + numLambda;
        // constraint order

        size_t numInequalityConstraints = numBoundingCoefs + numLambda;
        size_t numEqualityConstraints = 1 + numVarOld + numConstantTerms;

        Matrix<int64_t, 0, 0, 0> Af(numVarNew, numInequalityConstraints);
        llvm::SmallVector<int64_t, 8> bf(numInequalityConstraints);
        Matrix<int64_t, 0, 0, 0> Ef(numVarNew, numEqualityConstraints);
        llvm::SmallVector<int64_t, 8> qf(numEqualityConstraints);

        // lambda_0 + lambda' * (b - A*i) == psi
        // we represent equal constraint as
        // lambda_0 + lambda' * (b - A*i) - psi <= 0
        // -lambda_0 - lambda' * (b - A*i) + psi <= 0
        // first, lambda_0:
        Ef(numVarKeep, 0) = 1;
        for (size_t c = 0; c < numInequalityContraintsOld; ++c) {
            size_t lambdaInd = numScheduleCoefs + numBoundingCoefs + c + 1;
            for (size_t v = 0; v < numVarOld; ++v) {
                Ef(lambdaInd, 1 + v) = -A(v, c);
            }
            for (auto &t : b[c]) {
                if (auto c = t.getCompileTimeConstant()) {
                    Ef(lambdaInd, 0) = c.getValue();
                } else {
                    size_t constraintInd =
                        constantTerms[t.exponent] + numVarOld + 1;
                    Ef(lambdaInd, constraintInd) = t.coefficient;
                }
            }
        }
        for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
            // each of these actually represents 2 inds
            size_t lambdaInd = numScheduleCoefs + numBoundingCoefs +
                               numInequalityContraintsOld + 2 * c;
            for (size_t v = 0; v < numVarOld; ++v) {
                Ef(lambdaInd + 1, 1 + v) = -E(v, c);
                Ef(lambdaInd + 2, 1 + v) = E(v, c);
            }
            for (auto &t : q[c]) {
                if (auto c = t.getCompileTimeConstant()) {
                    Ef(lambdaInd + 1, 0) = c.getValue();
                    Ef(lambdaInd + 2, 0) = -c.getValue();
                } else {
                    size_t constraintInd =
                        constantTerms[t.exponent] + numVarOld + 1;
                    Ef(lambdaInd + 1, constraintInd) = t.coefficient;
                    Ef(lambdaInd + 2, constraintInd) = -t.coefficient;
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
        int64_t sign = 2 * (direction ^ boundAbove) - 1;
        for (size_t i = 0; i < numVarOld; ++i) {
            int64_t s = (2 * (i < numDep0Var) - 1) * sign;
            Ef(i, 1 + i) = s;
        }
        // delta/constant coef at ind numVarOld
        Ef(numVarOld, 0) = -sign;
        // boundAbove
        if (boundAbove) {
            // note we'll generally call this function twice, first with
            // 1. `boundAbove = false`
            // 2. `boundAbove = true`
            // boundAbove means we have
            // ... == w + u'*N + psi
            Ef(numScheduleCoefs, 0) = -1;
            Af(numScheduleCoefs, 0) = -1;
            for (size_t i = 0; i < numConstantTerms; ++i) {
                size_t ip1 = i + 1;
                size_t constraintInd = (ip1 + numVarOld);
                Ef(i + numScheduleCoefs + 1, constraintInd) = -1;
                Af(numScheduleCoefs + ip1, ip1) = -1;
            }
        }
        // all lambda > 0
        for (size_t i = 0; i < numLambda; ++i) {
            Af(numVarKeep + i, numBoundingCoefs + i) = -1;
        }
        //#ifndef NDEBUG
        //        std::cout << "Af = \n" << Af << std::endl;
        //#endif
        removeExtraVariables(Af, bf, Ef, qf, numVarKeep);
        IntegerEqPolyhedra ipoly(std::move(Af), std::move(bf), std::move(Ef),
                                 std::move(qf));
        ipoly.pruneBounds();
        assert(ipoly.E.numCol() == ipoly.q.size());
        return ipoly;
    }

}; // namespace DependencePolyhedra

struct Dependence {
    DependencePolyhedra depPoly;
    IntegerEqPolyhedra dependenceSatisfaction;
    IntegerEqPolyhedra dependenceBounding;
    bool isForward() const { return depPoly.forward; }
    // static llvm::Optional<Dependence> check(MemoryAccess &x, MemoryAccess &y)
    // {
    //     return check(*x.ref, x.schedule, *y.ref, y.schedule);
    // }
    static void check(llvm::SmallVectorImpl<Dependence> deps,
                      const ArrayReference &x, const Schedule &sx,
                      const ArrayReference &y, const Schedule &sy) {
        if (x.gcdKnownIndependent(y))
            return;
        DependencePolyhedra dxy(x, y);
        if (dxy.isEmpty())
            return;
            // note that we set boundAbove=true, so we reverse the dependence
            // direction for the dependency we week, we'll discard the program
            // variables x then y
#ifndef NDEBUG
        std::cout << "x = " << x << "\ny = " << y << "\ndxy = \n"
                  << dxy << std::endl;
#endif
        IntegerEqPolyhedra fxy(dxy.farkasScheduleDifference(true, false));
        // y then x
        IntegerEqPolyhedra fyx(dxy.farkasScheduleDifference(true, true));
        const size_t numLoopsX = x.getNumLoops();
        const size_t numLoopsY = y.getNumLoops();
        const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
        const size_t numLoopsTotal = numLoopsX + numLoopsY;
        SquarePtrMatrix<const int64_t> xPhi = sx.getPhi();
        SquarePtrMatrix<const int64_t> yPhi = sy.getPhi();
        PtrVector<const int64_t, 0> xOmega = sx.getOmega();
        PtrVector<const int64_t, 0> yOmega = sy.getOmega();
        llvm::SmallVector<int64_t, 16> sch;
        sch.resize_for_overwrite(numLoopsTotal + 1);
        // sch.resize_for_overwrite(fxy.getNumVar());
        // for (size_t i = numLoopsTotal + 1; i < fxy.getNumVar(); ++i) {
        //     sch[i] = 0;
        // }
        for (size_t i = 0; i <= numLoopsCommon; ++i) {
            if (int64_t o2idiff = yOmega[2 * i] - xOmega[2 * i]) {

                if ((dxy.forward = o2idiff > 0)) {
                    std::swap(fxy, fyx);
                    // fxy.A.reduceNumRows(numLoopsTotal + 1);
                    // fxy.E.reduceNumRows(numLoopsTotal + 1);
                    // fxy.dropEmptyConstraints();
                    // // x then y
                    // return Dependence{dxy, fxy, fyx};
#ifndef NDEBUG
                    std::cout << "dep order 0; i = " << i << std::endl;
                } else {
                    std::cout << "dep order 1; i = " << i << std::endl;
#endif
                }
                fyx.A.reduceNumRows(numLoopsTotal + 1);
                fyx.E.reduceNumRows(numLoopsTotal + 1);
                fyx.dropEmptyConstraints();
                // x then y
                deps.emplace_back(std::move(dxy), std::move(fyx),
                                  std::move(fxy));
                return;
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
            int64_t yO = yOmega[2 * i + 1], xO = xOmega[2 * i + 1];
            // forward means offset is 2nd - 1st
            sch[numLoopsTotal] = yO - xO;
            if (!fxy.knownSatisfied(sch)) {
                dxy.forward = false;
                fyx.A.reduceNumRows(numLoopsTotal + 1);
                fyx.E.reduceNumRows(numLoopsTotal + 1);
                fyx.dropEmptyConstraints();
#ifndef NDEBUG
                std::cout << "dep order 2; i = " << i << std::endl;
#endif
                // y then x
                deps.emplace_back(std::move(dxy), std::move(fyx),
                                  std::move(fxy));
                return;
            }
            // backward means offset is 1st - 2nd
            sch[numLoopsTotal] = xO - yO;
            if (!fyx.knownSatisfied(sch)) {
                dxy.forward = true;
                fxy.A.reduceNumRows(numLoopsTotal + 1);
                fxy.E.reduceNumRows(numLoopsTotal + 1);
                fxy.dropEmptyConstraints();
#ifndef NDEBUG
                std::cout << "dep order 3; i= " << i << std::endl;
#endif
                deps.emplace_back(std::move(dxy), std::move(fxy),
                                  std::move(fyx));
                return;
            }
        }
        return;
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
