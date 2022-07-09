#pragma once
#include "./Constraints.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/SmallVector.h>

// The goal here:
// this Simplex struct will orchestrate search through the solution space
// it will add constraints as it goes, e.g. corresponding to desired properties
// or as we move up loop levels to maintain independence from previous ones.
struct Simplex {
    // mapped to a PtrMatrix tableau
    // row 0: indicator indicating whether that column (variable) is basic, and
    //        if so which row (constraint) is the basic one.
    // row 1: cost numerators remaining rows: tableau numerators
    // column 0: indicates whether that row (constraint) is basic,
    //           and if so which one
    // column 1: denominator for the row
    // column 2: constraint values
    Matrix<int64_t, 0, 0, 0> tableau;
    size_t numSlackVar;
    bool inCanonicalForm;
    // NOTE: all methods resizing the tableau may invalidate references to it
    void resize(size_t numCon, size_t numVar) {
        tableau.resize(numCon + 2, numVar + 2);
    }
    void resize(size_t numCon, size_t numVar, size_t stride) {
        tableau.resize(numCon + 2, numVar + 2, stride);
    }
    void addVars(size_t numVars) {
        tableau.resize(tableau.numRow(), numVars + 2,
                       std::max(numVars + 2, tableau.rowStride()));
    }
    void truncateVars(size_t numVars) { tableau.truncateCols(numVars + 2); }
    void truncateConstraints(size_t numVars) {
        tableau.truncateRows(numVars + 2);
    }
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
    PtrMatrix<int64_t> getCostsAndConstraints() {
        return tableau.view(1, tableau.numRow(), 2, tableau.numCol());
    }
    PtrMatrix<int64_t> getConstraints() {
        return tableau.view(2, tableau.numRow(), 2, tableau.numCol());
    }
    // note that this is 1 more than the actual number of variables
    // as it includes the constants
    size_t getNumVar() const { return tableau.numCol() - 2; }
    size_t getNumConstraints() const { return tableau.numRow() - 2; }

    void hermiteNormalForm() {
        inCanonicalForm = false;
        truncateConstraints(
            NormalForm::simplifyEqualityConstraintsImpl(getConstraints(), 1));
    }
    void deleteConstraint(size_t c) {
        eraseConstraintImpl(tableau, c + 2);
        --tableau.M;
    }
    llvm::MutableArrayRef<int64_t> getTableauRow(size_t i) {
        return llvm::MutableArrayRef<int64_t>(
            tableau.data() + 2 + i * tableau.rowStride(), getNumVar());
    }
    llvm::MutableArrayRef<int64_t> getBasicConstraints() {
        return getTableauRow(0);
    }
    llvm::MutableArrayRef<int64_t> getCost() { return getTableauRow(1); }
    StridedVector<int64_t> getTableauCol(size_t i) {
        return StridedVector<int64_t>{tableau.data() + i +
                                          2 * tableau.rowStride(),
                                      getNumConstraints(), tableau.rowStride()};
    }
    StridedVector<int64_t> getBasicVariables() { return getTableauCol(0); }
    StridedVector<int64_t> getConstraintDenominators() {
        return getTableauCol(1);
    }
    StridedVector<int64_t> getConstants() { return getTableauCol(2); }
    bool initiateFeasible() {
        // remove trivially redundant constraints
        hermiteNormalForm();
        // [ I;  X ; b ]
        //
        // original number of variables
        const size_t numVar = getNumVar();
        PtrMatrix<int64_t> C{getConstraints()};
        auto basicCons = getBasicConstraints();
        for (auto &&x : basicCons)
            x = -2;
        // first pass, we make sure the equalities are >= 0
        // and we eagerly try and find columns with
        // only a single non-0 element.
        for (size_t c = 0; c < C.numRow(); ++c) {
            int64_t &Ceq = C(c, 0);
            if (Ceq >= 0) {
                // close-open and close-close are out, open-open is in
                for (size_t v = 1; v < numVar; ++v) {
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
                for (size_t v = 1; v < numVar; ++v) {
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
        //
        auto basicVars = getBasicVariables();
        for (auto &&x : basicVars)
            x = -1;
        for (size_t v = 1; v < numVar; ++v) {
            int64_t r = basicCons[v];
            if (r >= 0) {
                if (basicVars[r] == -1) {
                    basicVars[r] = v;
                } else {
                    // this is reachable, as we could have
                    // [ 1  1  0
                    //   0  0  1 ]
                    // TODO: is their actual harm in having multiple basicCons?
                    basicCons[v] = -1;
                }
            }
        }
        llvm::SmallVector<unsigned> augmentVars{};
        for (unsigned i = 0; i < basicVars.size(); ++i)
            if (basicVars[i] == -1)
                augmentVars.push_back(i);
        addVars(augmentVars.size()); // NOTE: invalidates all refs
        for (auto &&d : getConstraintDenominators())
            d = 1;
        {
            PtrMatrix<int64_t> C{getConstraints()};
            auto costs{getCost()};
            for (auto &&c : costs)
                c = 0;
            for (size_t i = 0; i < augmentVars.size(); ++i) {
                size_t a = augmentVars[i];
                C(a, numVar + i) = 1;
                // we now zero out the implicit cost of `1`
                for (size_t j = 0; j < numVar; ++j)
                    costs[j] -= C(a, j);
            }
            // false/0 means feasible
            // true/non-zero infeasible
            if (int64_t r = runCore())
                return r;
            // all augment vars are now 0
            truncateVars(numVar);
        }
        inCanonicalForm = true;
        return 0;
    }
    static int getEnteringVariable(llvm::ArrayRef<int64_t> costs) {
        // Bland's algorithm; guaranteed to terminate
        for (int i = 1; i < int(costs.size()); ++i)
            if (costs[i] < 0)
                return i;
        return -1;
    }
    static int getLeavingVariable(PtrMatrix<int64_t> C,
                                  size_t enteringVariable) {
        // inits guarantee first valid is selected
        int64_t n = 0;
        int64_t d = -1;
        int j = -1;
        for (size_t i = 0; i < C.numRow(); ++i) {
            int64_t Civ = C(i, enteringVariable);
            if (Civ > 0) {
                int64_t Ci0 = C(i, 0);
                assert(Ci0 >= 0);
                if (Civ * d < n * Ci0) {
                    n = Civ;
                    d = Ci0;
                    j = i;
                }
            }
        }
        return j;
    }
    // run the simplex algorithm, assuming basicVar's costs have been set to 0
    int64_t runCore(int64_t f = 1) {
        PtrMatrix<int64_t> C{getCostsAndConstraints()};
        while (true) {
            // entering variable is the row
            int enteringVariable = getEnteringVariable(C.getRow(0));
            if (enteringVariable == -1)
                return C[0] / f;
            // leaving variable is the column
            int leavingVariable = getLeavingVariable(C, enteringVariable);
            if (leavingVariable == -1)
                return std::numeric_limits<int64_t>::max(); // unbounded
            ++leavingVariable;
            for (size_t i = 0; i < C.numRow(); ++i)
                if (i != size_t(leavingVariable)) {
                    int64_t m = NormalForm::zeroWithRowOperation(
                        C, i, leavingVariable, enteringVariable);
                    if (i == 0)
                        f *= m;
                }
        }
    }
    // set basicVar's costs to 0, and then runCore()
    int64_t run() {
        StridedVector<int64_t> basicVars = getBasicVariables();
        PtrMatrix<int64_t> C = getCostsAndConstraints();
        int64_t f = 1;
        for (size_t c = 0; c < basicVars.size();) {
            int64_t v = basicVars[c++];
            if (int64_t cost = C(0, v)) {
                f *= NormalForm::zeroWithRowOperation(C, 0, c, v);
            }
        }
        return runCore(f);
    }
    // A*x >= 0
    // B*x == 0
    // returns a Simplex if feasible, and an empty `Optional` otherwise
    static llvm::Optional<Simplex>
    positiveVariables(PtrMatrix<const int64_t> A, PtrMatrix<const int64_t> B) {
        size_t numVar = A.numCol();
        assert(numVar == B.numCol());
        Simplex simplex{};
        size_t numSlack = A.numRow();
        simplex.numSlackVar = numSlack;
        size_t numStrict = B.numRow();
        size_t numCon = numSlack + numStrict;
        size_t extraStride = 0;
        for (unsigned i = 0; i < numSlack; ++i)
            extraStride += A(i, numVar - 1) < 0;
        // try to avoid reallocating
        size_t stride = numVar + numCon + extraStride + 2;
        simplex.resizeForOverwrite(numCon, numVar + numSlack, stride);
        // construct:
        // [ I A
        //   0 B ]
        // then drop the extra variables
        slackEqualityConstraints(simplex.getConstraints(), A, B);
        if (simplex.initiateFeasible())
            return {};
        return simplex;
    }

    void pruneBounds() {
        Simplex simplex;
        for (size_t c = 0; c < getNumConstraints(); ++c) {
            simplex = *this;
            PtrMatrix<int64_t> constraints = simplex.getConstraints();
            int64_t bumpedBound = ++constraints(c, 0);
            llvm::MutableArrayRef<int64_t> cost = simplex.getCost();
            for (size_t v = numSlackVar; v < cost.size(); ++v)
                cost[v] = -constraints(c, v);
            if (simplex.run() != bumpedBound)
                deleteConstraint(c--); // redundant
        }
    }

    void removeVariable(size_t i) {
        // TODO: can you maintain state?
        inCanonicalForm = false;
        PtrMatrix<int64_t> C{getConstraints()};
        size_t j = C.numRow();
        while (C(--j, i) == 0)
            if (j == 0)
                return;
        for (size_t k = 0; k < j; ++k)
            if (C(k, i))
                NormalForm::zeroWithRowOperation(C, k, j, i);
        size_t lastRow = C.numRow() - 1;
        if (lastRow != j)
            swapRows(C, j, lastRow);
        truncateConstraints(lastRow);
    }
    void removeExtraVariables(size_t i) {
        for (size_t j = getNumVar(); j > i;) {
            removeVariable(--j);
            truncateVars(j);
        }
    }
};
