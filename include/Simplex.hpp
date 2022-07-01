#pragma once
#include "./AbstractEqualityPolyhedra.hpp"
#include "./Math.hpp"
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

// The goal here:
// this Simplex struct will orchestrate search through the solution space
// it will add constraints as it goes, e.g. corresponding to desired properties
// or as we move up loop levels to maintain independence from previous ones.
struct Simplex {
    // mapped to a PtrMatrix tableau
    // row 0: indicator indicating whether that column (variable) is basic, and if so which row (constraint) is the basic one.
    // row 1: cost numerators
    // remaining rows: tableau numerators
    // column 0: indicates whether that row (constraint) is basic, and if so which one
    // column 1: denominator for the row
    llvm::SmallVector<int64_t, 0> data{};
    // length equal to the number of variables, indicating which constraint is
    // non-zero
    llvm::SmallVector<intptr_t, 0> basicConstraints{};
    // length equal to the number of constraints, indicating which variable is
    // basic
    // llvm::SmallVector<int, 0> basicVariables;
    llvm::SmallVector<std::pair<int64_t, intptr_t>, 0> basicVariablesDenom{};
    // Simplex() : data({}), basicConstraints({}), basicVariablesDenom({}) {}
    static Simplex positiveVariables(PtrMatrix<const int64_t> A,
                                     llvm::ArrayRef<int64_t> b,
                                     PtrMatrix<const int64_t> E,
                                     llvm::ArrayRef<int64_t> q) {
        Simplex simplex{};

        return simplex;
    }
};
