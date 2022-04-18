#pragma once

#include "./ArrayReference.hpp"
#include "./IntermediateRepresentation.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include <cstdint>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <utility>

struct DependencePolyhedra {
    //   DependencePolyhedra(aln0, aln1, ar0, ar1)
    //
    // dependence from aln0 (src) -> aln1 (tgt)
    // Produces
    // A'*x <= b
    // Where x = [c_src, c_tgt, beta_src..., beta_tgt]
    // layout of constraints (based on Farkas equalities):
    // comp time constant, indVars0, indVars1, loop constants
    DependencePolyhedra(AffineLoopNest &aln0, AffineLoopNest &aln1,
                        ArrayReference &ar0, ArrayReference &ar1) {

        const static llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
            maybeDims = matchingStrideConstraintPairs(aln0, aln1, ar0, ar1);

        assert(maybeDims.hasValue());
        const llvm::SmallVector<std::pair<int, int>, 4> &dims =
            maybeDims.getValue();

        llvm::DenseMap<Polynomial::Monomial, size_t> constantTerms;
        for (auto &&bi : aln0.b) {
            for (auto &&t : bi) {
                if (!t.isCompileTimeConstant()) {
                    constantTerms.insert(
                        std::make_pair(t.exponent, constantTerms.size()));
                }
            }
        }
        for (auto &&bi : aln1.b) {
            for (auto &&t : bi) {
                if (!t.isCompileTimeConstant()) {
                    constantTerms.insert(
                        std::make_pair(t.exponent, constantTerms.size()));
                }
            }
        }
        // one lambda per constraint
        const size_t numLoopConstraints =
            aln0.getNumConstraints() + aln1.getNumConstraints();
        size_t numLambda = numLoopConstraints + 1;
        // if dims do not match, we flatten and add a linearized constraint.

        // delinearized dependence constraints; we require dims match
        numLambda += dims.size();

        //
        const size_t numFarkasMatch =
            aln0.getNumVar() + aln1.getNumVar() + constantTerms.size() + 1;

        const size_t numScheduleCoefs = aln0.getNumVar() + aln1.getNumVar() + 2;
        // const size_t numDistanceCostVars =
        //     numScheduleCoefs + constantTerms.size();

        Matrix<intptr_t, 0, 0, 0> As(
            numScheduleCoefs, 2 * numFarkasMatch + 2 * dims.size() + numLambda);
        llvm::SmallVector<intptr_t, 8> bs;
        bs.reserve(2 * numFarkasMatch + numLambda);
        // we add a lot of equality constraints
        // we add them in positive-negative pairs
        // (0):  1*coef, -1 * lambda
        // (1): -1*coef,  1 * lambda

        // First, we insert the constant terms
        As(0, 0) = -1; // src
        As(0, 0) = 1;  // src
        As(1, 0) = 1;  // tgt
        As(1, 0) = -1; // tgt
        // for source, coefs are negative, so -1 * 1 = -1
        for (size_t j = 0; j < aln0.getNumVar(); ++j) {
            // schedule coefficient
            As(j + 2, (j << 1) + 1) = -1;
            As(j + 2, (j << 1) + 2) = 1;
            // i corresponds to lambda
            // it is (b - A'i), so -1 * -1 = 1 for first
            for (size_t i = 0; i < aln0.getNumConstraints(); ++i) {
                if (intptr_t Aji = aln0.A(j, i)) {
                    As(numScheduleCoefs + 2 + i, (j << 1) + 1) = Aji;
                    As(numScheduleCoefs + 2 + i, (j << 1) + 2) = -Aji;
                }
            }
        }
        for (size_t i = 0; i < aln0.b.size(); ++i) {
            if (!isZero(aln0.b[i])) {
                for (auto &t : aln0.b[i]) {
                    if (auto c = t.getCompileTimeConstant()) {
                        As(numScheduleCoefs + 2 + i, 0) = -c.getValue();
                        As(numScheduleCoefs + 2 + i, 0) = c.getValue();
                    } else {
                        size_t j = constantTerms[t.exponent];
                        As(numScheduleCoefs + 2 + i, j + numLoopConstraints) =
                            -t.coefficient;
                        As(numScheduleCoefs + 2 + i, j + numLoopConstraints) =
                            t.coefficient;
                    }
                }
            }
        }
        size_t iOff = numScheduleCoefs + 2 + aln0.getNumConstraints();
        // for target, coefs are positive, so 1 * 1 = 1
        for (size_t _j = 0; _j < aln1.getNumVar(); ++_j) {
            size_t j = _j + aln0.getNumVar();
            As(j + 2, (j << 1) + 1) = 1;
            As(j + 2, (j << 1) + 2) = -1;
            for (size_t i = 0; i < aln1.getNumConstraints(); ++i) {
                if (intptr_t Aji = aln1.A(j, i)) {
                    size_t var = iOff + i;
                    As(var, (j << 1) + 1) = Aji;
                    As(var, (j << 1) + 2) = -Aji;
                }
            }
        }
        for (size_t i = 0; i < aln1.b.size(); ++i) {
            if (!isZero(aln1.b[i])) {
                for (auto &t : aln1.b[i]) {
                    if (auto c = t.getCompileTimeConstant()) {
                        As(numScheduleCoefs + 2 + i, 0) = -c.getValue();
                        As(numScheduleCoefs + 2 + i, 0) = c.getValue();
                    } else {
                        size_t j = constantTerms[t.exponent];
                        As(iOff + i, j + numLoopConstraints) = -t.coefficient;
                        As(iOff + i, j + numLoopConstraints) = t.coefficient;
                    }
                }
            }
        }
        size_t eqConstraintOffset = iOff + aln1.getNumConstraints();
        // now, insert the constraints corresponding to the matching axes
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
                        var.id += aln0.getNumVar();
                    }
                }
                delta -= ar0.axes[d0];
            }
            // now we add delta >= 0 and -delta >= 0
            for (auto &ind : delta) {
                auto &[coef, var] = ind;
                auto [t, id] = var.getTypeAndId();
                switch (t) {
                case VarType::LoopInductionVariable:
                    // need this to be a compile time constant
                    // TODO: handle the case gracefully where it isn't!!!
                    intptr_t c = coef.getCompileTimeConstant().getValue();
                    // id gives the loop, which yields the Farkas constraint it
                    // contributed to, i.e. the column of `As` to store into.
		    // `i`, the dim number, yields the associated labmda.
                    As(i+eqConstraintOffset, id+1) = c;
                    As(i+eqConstraintOffset, id+2) = -c;
                    break;
                }
            }
        }
        // 	Stride &arx = ar0.axes[d0];
        // 	Stride &ary = ar1.axes[d1];
        // 	for (auto &ind : arx.indices){
        // 	    auto& [coef, var] = ind;
        // 	    auto [t, id] = var.getTypeAndId();
        // 	    switch (t) {
        // 	    case VarType::LoopInductionVariable:
        // 		A(id, eqConstraintOffset + (i<<1));
        // 		A(id, eqConstraintOffset + (i<<1) + 1);
        // 		break;
        // 	    case VarType::Constant:
        // 		break;
        // 	    default:
        // 		assert("not yet supported");
        // 	    }
        // }

        // A(, eqConstraintOffset + (i<<1));
        // A(, eqConstraintOffset + (i<<1) + 1);
        // arx == ary
    }

    static llvm::Optional<llvm::SmallVector<std::pair<int, int>, 4>>
    matchingStrideConstraintPairs(const AffineLoopNest &aln0,
                                  const AffineLoopNest &aln1,
                                  const ArrayReference &ar0,
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
        size_t dim = 0;
        auto axesix = ar0.axes.begin();
        auto axesiy = ar1.axes.begin();

        return {};
    }
};
