#pragma once
#include "./Constraints.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include <cstddef>
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
    Matrix<int64_t, 0, 0, 0> tableau;
    // llvm::SmallVector<int64_t, 0> data{};
    // size_t numVariables;
    // size_t numConstraints;
    // size_t stride{};

    void resizeForOverwrite(size_t numCon, size_t numVar) {
        tableau.resizeForOverwrite(numCon + 2, numVar + 2);
    }
    void resizeForOverwrite(size_t numCon, size_t numVar, size_t stride) {
        tableau.resizeForOverwrite(numCon + 2, numVar + 2, stride);
    }
    // PtrMatrix<int64_t> tableau() {
    //     return PtrMatrix<int64_t>(data.data(), numConstraints + 2,
    //                               numVariables + 2, stride);
    // }
    PtrMatrix<int64_t> constraints() {
        return tableau.view(2, tableau.numRow(), 2, tableau.numCol());
    }
    size_t getNumVar() const { return tableau.numCol() - 2; }
    size_t getNumConstraints() const { return tableau.numRow() - 2; }

    void simplifyConstraints() {
        tableau.truncateRows(
            NormalForm::simplifyEqualityConstraintsImpl(constraints()) + 2);
    }
    llvm::MutableArrayRef<int64_t> getTableauRow(size_t i) {
        return llvm::MutableArrayRef<int64_t>(
            tableau.data() + 2 + i * tableau.rowStride(), getNumConstraints());
    }
    llvm::MutableArrayRef<int64_t> basicConstraints() {
        return getTableauRow(0);
    }
    llvm::MutableArrayRef<int64_t> cost() { return getTableauRow(1); }
    StridedVector<int64_t> getTableauCol(size_t i) {
        return StridedVector<int64_t>{tableau.data() + i +
                                          2 * tableau.rowStride(),
                                      getNumVar(), tableau.rowStride()};
    }
    StridedVector<int64_t> basicVariables() { return getTableauCol(0); }
    StridedVector<int64_t> constraintDenominators() { return getTableauCol(1); }
    bool initiateFeasible() {
        // remove trivially redundant constraints
        simplifyConstraints();
        // original number of variables
        const size_t numVar = getNumVar();
        const size_t equalityIndex = numVar - 1;
        PtrMatrix<int64_t> C{constraints()};
        auto basicCons = basicConstraints();
        for (auto &&x : basicCons)
            x = -2;

        // first pass, we make sure the equalities are >= 0
        // and we greedily try and find columns with
        // only a single non-0 element.
        for (size_t c = 0; c < C.numRow(); ++c) {
            int64_t &Ceq = C(c, equalityIndex);
            if (Ceq >= 0) {
                for (size_t v = 0; v < equalityIndex; ++v) {
                    if (int64_t Ccv = C(c, v)) {
                        if (((basicCons[v] == -2) && (Ccv > 0))) {
                            basicCons[v] = c;
                        } else {
                            basicCons[v] = -1;
                        }
                    }
                }
            } else {
                Ceq *= -1;
                for (size_t v = 0; v < equalityIndex; ++v) {
                    if (int64_t Ccv = -C(c, v)) {
                        if (((basicCons[v] == -2) && (Ccv > 0))) {
                            basicCons[v] = c;
                        } else {
                            basicCons[v] = -1;
                        }
                        C(c, v) = Ccv;
                    }
                }
            }
        }
        // basicCons should now contain either `-1` or an integer >= 0
        // indicating which row contains the only non-zero element; we'll now
        // fill basicVars.
        auto basicVars = basicVariables();
        for (auto &&x : basicVars)
            x = -1;
        for (size_t v = 0; v < equalityIndex; ++v) {
            int64_t r = basicCons[v];
            if (r >= 0)
                basicVars[r] = v;
        }
        size_t numAugmentVars = 0;
        // for (size_t c = 0; c < C.numRow(); ++c) {
        //     int64_t &basicVar{basicVars[c]};
        //     basicVar = -1;
        // }

        // now, every
        return false;
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
        // llvm::SmallVector<unsigned> negativeInequalities;
        size_t extraStride = 0;
        for (unsigned i = 0; i < numSlack; ++i)
            extraStride += A(i, numVar - 1) < 0;
        // if (A(i,numVar-1)<0)
        // 	negativeInequalities.push_back(i);

        size_t stride = numVar + numCon + extraStride + 2;
        simplex.resizeForOverwrite(numCon, numVar + numSlack, stride);
        // initial construction:
        // [ I A 0
        //   0 B I ]
        // then drop the extra variables
        slackEqualityConstraints(simplex.constraints(), A, B);
        simplex.initiateFeasible();

        return simplex;
    }
};
