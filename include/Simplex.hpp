#pragma once
#include "./Constraints.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

// The goal here:
// this Simplex struct will orchestrate search through the solution space
// it will add constraints as it goes, e.g. corresponding to desired properties
// or as we move up loop levels to maintain independence from previous ones.
struct Simplex {
    // mapped to a PtrMatrix tableau
    // row 0: indicator indicating whether that column (variable) is basic, and
    // if so which row (constraint) is the basic one. row 1: cost numerators
    // remaining rows: tableau numerators
    // column 0: indicates whether that row (constraint) is basic, and if so
    // which one column 1: denominator for the row
    llvm::SmallVector<int64_t, 0> data{};
    size_t numVariables;
    size_t numConstraints;
    size_t stride{};
    void resizeForOverwrite(size_t numCon, size_t numVar) {
        return resizeForOverwrite(numCon, numVar, std::max(stride, numVar + 2));
    }
    void resizeForOverwrite(size_t numCon, size_t numVar, size_t stride) {
        assert(stride >= numVar + 2);
        numVariables = numVar;
        numConstraints = numCon;
        size_t newSize = stride * (numCon + 2);
        if (newSize > data.size())
            data.resize_for_overwrite(newSize);
    }
    PtrMatrix<int64_t> tableau() {
        return PtrMatrix<int64_t>(data.data(), numConstraints + 2,
                                  numVariables + 2, stride);
    }
    PtrMatrix<int64_t> constraints() {
        return PtrMatrix<int64_t>(data.data() + 2 + 2 * stride, numConstraints,
                                  numVariables, stride);
    }
    // A*x >= 0
    // B*x == 0
    static Simplex positiveVariables(PtrMatrix<const int64_t> A,
                                     PtrMatrix<const int64_t> B) {
        size_t numVar = A.numCol();
        assert(numVar == B.numCol());
        Simplex simplex{};
        size_t numSlack = A.numRow();
        size_t numStrict = B.numRow();
        size_t numCon = numSlack + numStrict;
	size_t stride = numVar + numCon + 2;
	    simplex.resizeForOverwrite(numCon, numVar + numSlack, stride);
        auto C{simplex.constraints()};
	// preferred:
        // [ I A 0
        //   0 B I ]
	// then drop the extra augments
        // [ I 0 A
        //   0 I B ]
        slackEqualityConstraints(C, A, B);
        simplex.numVariables = NormalForm::simplifyEqualityConstraintsImpl(C);
        return simplex;
    }
    bool isFeasible() { return false; }
};
