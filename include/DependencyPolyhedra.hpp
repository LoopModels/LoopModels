#pragma once

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
#include <utility>

struct DependencePolyhedra : SymbolicPolyhedra {
    size_t numDep0Var;
    bool forward; // if (forward){ dep0 -> dep1; } else { dep1 -> depo; }

    static llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
    matchingStrideConstraintPairs(const ArrayReference &ar0,
                                  const ArrayReference &ar1) {
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
        // Returns an IntegerPolyhedra C'*y <= d
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

    static bool check(const ArrayReference &ar0, const ArrayReference &ar1) {

        // TODO: two steps:
        // 1: gcd test
        // 2: check polyhedra volume
        // step 1

        // step 2
        const llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
            maybeDims = matchingStrideConstraintPairs(ar0, ar1);

        return true;
    }

    //   DependencePolyhedra(aln0, aln1, ar0, ar1)
    //
    // dependence from aln0 (src) -> aln1 (tgt)
    // Produces
    // A'*x <= b
    // Where x = [c_src, c_tgt, beta_src..., beta_tgt]
    // layout of constraints (based on Farkas equalities):
    // comp time constant, indVars0, indVars1, loop constants
    DependencePolyhedra(const ArrayReference &ar0, const ArrayReference &ar1)
        : SymbolicPolyhedra(Matrix<intptr_t, 0, 0, 0>(),
                            llvm::SmallVector<MPoly, 8>(), ar0.loop->poset) {

        const llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
            maybeDims = matchingStrideConstraintPairs(ar0, ar1);

        assert(maybeDims.hasValue());
        const llvm::SmallVector<std::pair<int, int>, 4> &dims =
            maybeDims.getValue();

        auto [nv0, nc0] = ar0.loop->A.size();
        auto [nv1, nc1] = ar1.loop->A.size();

        const size_t nc = nc0 + nc1;
        A.resize(nv0 + nv1, nc + 2 * dims.size());
        for (size_t i = 0; i < nc0; ++i) {
            for (size_t j = 0; j < nv0; ++j) {
                A(j, i) = ar0.loop->A(j, i);
            }
            b.push_back(ar0.loop->b[i]);
        }
        for (size_t i = 0; i < nc1; ++i) {
            for (size_t j = 0; j < nv1; ++j) {
                A(nv0 + j, nc0 + i) = ar0.loop->A(j, i);
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
                        intptr_t c = coef.getCompileTimeConstant().getValue();
                        // id gives the loop, which yields the Farkas constraint
                        // it contributed to, i.e. the column of `As` to store
                        // into. `i`, the dim number, yields the associated
                        // labmda.
                        A(id, nc + (i << 1)) = c;
                        A(id, nc + (i << 1) + 1) = -c;
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
            b.push_back(-bound);
            b.push_back(std::move(bound));
        }
        pruneBounds();
    }
    IntegerPolyhedra farkasScheduleDifference(bool boundAbove) {
        return farkasScheduleDifference(boundAbove, forward);
    }
    // `direction = true` means second dep follow first
    // order of variables:
    // [ schedule coefs on loops, const schedule coef, bounding coefs ]
    //
    // Order of constraints:
    // constant eq
    IntegerPolyhedra farkasScheduleDifference(bool boundAbove, bool direction) {

        llvm::DenseMap<Polynomial::Monomial, size_t> constantTerms;
        for (auto &bi : b) {
            for (auto &t : bi) {
                if (!t.isCompileTimeConstant()) {
                    constantTerms.insert(
                        std::make_pair(t.exponent, constantTerms.size()));
                }
            }
        }
        auto [numVarOld, numContraintsOld] = A.size();
        // delta + 1 coef per
        size_t numScheduleCoefs = 1 + numVarOld;
        size_t numLambda = 1 + numContraintsOld;
        size_t numConstantTerms = constantTerms.size();
        size_t numBoundingCoefs = boundAbove ? 1 + numConstantTerms : 0;
        // var order
        size_t numVarKeep = numScheduleCoefs + numBoundingCoefs;
        size_t numVarNew = numVarKeep + numLambda;
        // constraint order
        size_t numNonLambdaConstraint =
            2 * (1 + numVarOld + numConstantTerms) + numBoundingCoefs;
        size_t numConstraintsNew = numNonLambdaConstraint + numLambda;

        Matrix<intptr_t, 0, 0, 0> Af(numVarNew, numConstraintsNew);
        llvm::SmallVector<intptr_t, 8> bf(numConstraintsNew);

        // lambda_0 + lambda' * (b - A*i) == psi
        // we represent equal constraint as
        // lambda_0 + lambda' * (b - A*i) - psi <= 0
        // -lambda_0 - lambda' * (b - A*i) + psi <= 0
        for (size_t c = 0; c < numContraintsOld; ++c) {
            size_t lambdaInd = numScheduleCoefs + numBoundingCoefs + c;
            for (size_t v = 0; v < numVarOld; ++v) {
                Af(lambdaInd, 2 + (v << 1)) = -A(v, c);
                Af(lambdaInd, 3 + (v << 1)) = A(v, c);
            }
            for (auto &t : b[c]) {
                if (auto c = t.getCompileTimeConstant()) {
                    Af(lambdaInd, 0) = c.getValue();
                    Af(lambdaInd, 1) = -c.getValue();
                } else {
                    size_t constraintInd =
                        2 * (constantTerms[t.exponent] + numVarOld + 1);
                    Af(lambdaInd, constraintInd) = t.coefficient;
                    Af(lambdaInd, constraintInd + 1) = -t.coefficient;
                }
            }
        }
        // schedule
        // direction = true (and forward=true)
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
        intptr_t sign = 2 * (direction ^ boundAbove) - 1;
        for (size_t i = 0; i < numVarOld; ++i) {
            intptr_t s = (2 * (i < numDep0Var) - 1) * sign;
            Af(i, 2 + 2 * i) = s;
            Af(i, 3 + 2 * i) = -s;
        }
        Af(numVarOld, 0) = -sign;
        Af(numVarOld, 1) = sign;
        // boundAbove
        if (boundAbove) {
            // note we'll generally call this function twice, first with
            // 1. `boundAbove = false`
            // 2. `boundAbove = true`
            // boundAbove means we have
            // ... == w + u'*N + psi
            for (size_t i = 0; i < numConstantTerms; ++i) {
                size_t constraintInd = 2 * (i + numVarOld + 1);
                Af(i + numScheduleCoefs, constraintInd) = -1;
                Af(i + numScheduleCoefs, constraintInd + 1) = 1;
            }
        }
        // all lambda > 0
        for (size_t i = 0; i < numLambda; ++i) {
            Af(numVarKeep + i, numNonLambdaConstraint + i) = -1;
        }
        IntegerPolyhedra ipoly(std::move(Af), std::move(bf));
        // remove lambdas
        for (size_t i = numVarKeep; i < numVarNew; ++i) {
            ipoly.removeVariable(i);
        }
        Matrix<intptr_t, 0, 0, 0> As(numVarKeep, ipoly.getNumConstraints());
        for (size_t c = 0; c < ipoly.getNumConstraints(); ++c) {
            for (size_t v = 0; v < numVarKeep; ++v) {
                As(v, c) = ipoly.A(v, c);
            }
        }
        return IntegerPolyhedra(std::move(As), std::move(ipoly.b));
    }
    // TODO: how about always boundAbove, and set all variables to `0`
    // when doing the checks? Because in the end we do want both directions...
    // Depending on the chosing direction, discard the programing variable
    // from the associated constraint.
    // Then this function cal return all three Polyhedra associated
    // with the constraint
    // Perhaps this should move to a dedicated `Edge` type
    static llvm::Optional<std::pair<DependencePolyhedra, IntegerPolyhedra>>
    checkDependence(const ArrayReference &x, const Schedule &sx,
                    const ArrayReference &y, const Schedule &sy) {
        if (x.gcdKnownIndependent(y)) {
            return {};
        }
        DependencePolyhedra dxy(x, y);
        if (dxy.isEmpty()) {
            return {};
        }
        // x then y
        IntegerPolyhedra fxy(dxy.farkasScheduleDifference(false, true));
        // y then x
        IntegerPolyhedra fyx(dxy.farkasScheduleDifference(false, false));
        const size_t numLoopsX = x.getNumLoops();
        const size_t numLoopsY = y.getNumLoops();
        const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
        const size_t numLoopsTotal = numLoopsX + numLoopsY;
        SquarePtrMatrix<const intptr_t> xPhi = sx.getPhi();
        SquarePtrMatrix<const intptr_t> yPhi = sy.getPhi();
        PtrVector<const intptr_t, 0> xOmega = sx.getOmega();
        PtrVector<const intptr_t, 0> yOmega = sy.getOmega();
        llvm::SmallVector<intptr_t, 16> sch;
        sch.resize_for_overwrite(numLoopsTotal + 1);
        for (size_t i = 0; i <= numLoopsCommon; ++i) {
            if (intptr_t o2idiff = yOmega[2 * i] - xOmega[2 * i]) {
                if (o2idiff < 0) {
                    dxy.forward = false;
                    // y then x
                    return std::make_pair(dxy, fyx);
                } else {
                    // x then y
                    return std::make_pair(dxy, fxy);
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
                // y then x
                return std::make_pair(dxy, fyx);
            }
            // backward means offset is 1st - 2nd
            sch[numLoopsTotal] = xO - yO;
            if (!fyx.knownSatisfied(sch)) {
                return std::make_pair(dxy, fxy);
            }
        }
        return {};
    }

}; // namespace Dependence
