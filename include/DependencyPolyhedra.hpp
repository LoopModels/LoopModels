#pragma once

#include "./ArrayReference.hpp"
#include "./IntermediateRepresentation.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include <cstdint>
#include <llvm/ADT/DenseMap.h>

struct DependencePolyhedra {

    DependencePolyhedra(AffineLoopNest &aln0, AffineLoopNest &aln1,
                        ArrayReference &ar0, ArrayReference &ar1) {

        llvm::DenseMap<MPoly, size_t> constantTerms;
        for (auto &&bi : aln0.b) {
            if (!bi.isCompileTimeConstant()) {
                constantTerms.insert(std::make_pair(bi, constantTerms.size()));
            }
        }
        for (auto &&bi : aln1.b) {
            if (!bi.isCompileTimeConstant()) {
                constantTerms.insert(std::make_pair(bi, constantTerms.size()));
            }
        }
	// one lambda per constraint
        size_t numLambda = aln0.getNumConstraints() + aln1.getNumConstraints();
	// if dims do not match, we flatten and add a linearized constraint.
	numLambda += (ar0.stridesMatch(ar1) ? ar0.dim() : 1);
	// 
        const size_t numFarkasMatch =
            aln0.getNumVar() + aln1.getNumVar() + constantTerms.size() + 1;

        const size_t numScheduleCoefs = aln0.getNumVar() + aln1.getNumVar() + 2;
        const size_t numDistanceCostVars =
            numScheduleCoefs + constantTerms.size();

        Matrix<intptr_t, 0, 0, 0> As(numScheduleCoefs,
                                     2 * numFarkasMatch + numLambda);
        llvm::SmallVector<intptr_t, 8> bs;
	bs.reserve(2 * numFarkasMatch + numLambda);
	
    }
};
