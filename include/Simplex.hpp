#pragma once
#include "./Constraints.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "Macro.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/SmallVector.h>
#include <tuple>

// #define VERBOSESIMPLEX

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
    // column 1: constraint values
    Matrix<int64_t, 0, 0, 0> tableau;
    size_t numSlackVar;
    bool inCanonicalForm;
    static constexpr size_t numExtraRows = 2;
    static constexpr size_t numExtraCols = 1;
    static constexpr size_t numTableauRows(size_t i) {
        return i + numExtraRows;
    }
    static constexpr size_t numTableauCols(size_t j) {
        return j + numExtraCols;
    }
    // NOTE: all methods resizing the tableau may invalidate references to it
    void resize(size_t numCon, size_t numVar) {
        tableau.resize(numTableauRows(numCon), numTableauCols(numVar));
    }
    void resize(size_t numCon, size_t numVar, size_t stride) {
        tableau.resize(numTableauRows(numCon), numTableauCols(numVar), stride);
    }
    void addVars(size_t numVars) {
        size_t numCol = tableau.numCol() + numVars;
        tableau.resize(tableau.numRow(), numCol,
                       std::max(numCol, tableau.rowStride()));
    }
    MutPtrVector<int64_t> addConstraint() {
        tableau.resize(tableau.numRow() + 1, tableau.numCol(),
                       tableau.rowStride());
        tableau(end, _) = 0;
        return tableau(end, _(numExtraCols, end));
    }
    MutPtrVector<int64_t> addConstraintAndVar() {
        tableau.resize(tableau.numRow() + 1, tableau.numCol() + 1);
        tableau(end, _) = 0;
        return tableau(end, _(numExtraCols, end));
    }
    void reserveExtraRows(size_t additionalRows) {
        tableau.reserve(tableau.numRow() + additionalRows, tableau.rowStride());
    }
    void reserveExtra(size_t additionalRows, size_t additionalCols) {
        size_t newStride =
            std::max(tableau.rowStride(), tableau.numCol() + additionalCols);
        tableau.reserve(tableau.numRow() + additionalRows, newStride);
        if (newStride == tableau.rowStride())
            return;
        // copy memory, so that incrementally adding columns is cheap later.
        size_t nC = tableau.numCol();
        tableau.resize(tableau.numRow(), newStride, newStride);
        tableau.truncateCols(nC);
    }
    void reserveExtra(size_t additional) {
        reserveExtra(additional, additional);
    }
    void truncateVars(size_t numVars) {
        tableau.truncateCols(numTableauCols(numVars));
    }
    void truncateConstraints(size_t numCons) {
        tableau.truncateRows(numTableauRows(numCons));
    }
    void resizeForOverwrite(size_t numCon, size_t numVar) {
        tableau.resizeForOverwrite(numTableauRows(numCon),
                                   numTableauCols(numVar));
    }
    void resizeForOverwrite(size_t numCon, size_t numVar, size_t stride) {
        tableau.resizeForOverwrite(numTableauRows(numCon),
                                   numTableauCols(numVar), stride);
    }
    MutPtrMatrix<int64_t> getCostsAndConstraints() {
        return tableau(_(numExtraRows - 1, end), _(numExtraCols, end));
    }
    PtrMatrix<int64_t> getCostsAndConstraints() const {
        return tableau(_(numExtraRows - 1, end), _(numExtraCols, end));
    }
    MutPtrMatrix<int64_t> getConstraints() {
        return tableau(_(numExtraRows, end), _(numExtraCols, end));
    }
    PtrMatrix<int64_t> getConstraints() const {
        return tableau(_(numExtraRows, end), _(numExtraCols, end));
    }
    // note that this is 1 more than the actual number of variables
    // as it includes the constants
    size_t getNumVar() const { return tableau.numCol() - numExtraCols; }
    size_t getNumConstraints() const { return tableau.numRow() - numExtraRows; }

    void hermiteNormalForm() {
        inCanonicalForm = false;
        truncateConstraints(
            NormalForm::simplifySystemImpl(getConstraints(), 1));
    }
    void deleteConstraint(size_t c) {
        eraseConstraintImpl(tableau, numTableauRows(c));
        --tableau.M;
    }
    PtrVector<int64_t> getTableauRow(size_t i) const {
        return tableau(i, _(numExtraCols, numExtraCols + getNumVar()));
    }
    // 1-indexed, 0 returns value for const col
    PtrVector<int64_t> getBasicConstraints() const { return getTableauRow(0); }
    PtrVector<int64_t> getCost() const { return getTableauRow(1); }
    MutPtrVector<int64_t> getTableauRow(size_t i) {
        return tableau(i, _(numExtraCols, numExtraCols + getNumVar()));
    }
    // 1-indexed, 0 returns value for const col
    MutPtrVector<int64_t> getBasicConstraints() { return getTableauRow(0); }
    MutPtrVector<int64_t> getCost() { return getTableauRow(1); }
    StridedVector<int64_t> getTableauCol(size_t i) const {
        return StridedVector<int64_t>{tableau.data() + i +
                                          numExtraRows * tableau.rowStride(),
                                      getNumConstraints(), tableau.rowStride()};
    }
    // 0-indexed
    StridedVector<int64_t> getBasicVariables() const {
        return getTableauCol(0);
    }
    // StridedVector<int64_t> getDenominators() const {
    //     return getTableauCol(1);
    // }
    StridedVector<int64_t> getConstants() const {
        return getTableauCol(numExtraCols);
    }
    MutStridedVector<int64_t> getTableauCol(size_t i) {
        return MutStridedVector<int64_t>{
            tableau.data() + i + numExtraRows * tableau.rowStride(),
            getNumConstraints(), tableau.rowStride()};
    }
    MutStridedVector<int64_t> getBasicVariables() { return getTableauCol(0); }
    // MutStridedVector<int64_t> getDenominators() { return getTableauCol(1); }
    MutStridedVector<int64_t> getConstants() {
        return getTableauCol(numExtraCols);
    }
    // AbstractVector
    struct Solution {
        using eltype = Rational;
        static constexpr bool canResize = false;
        // view of tableau dropping const column
        PtrMatrix<int64_t> tableauView;
        StridedVector<int64_t> consts;
        Rational operator()(size_t i) const {
            int64_t j = tableauView(0, i);
            if (j < 0)
                return 0;
            return Rational::create(tableauView(j + numExtraRows, i),
                                    consts(j));
        }
        template <typename B, typename E> Solution operator()(Range<B, E> r) {
            return Solution{tableauView(_, r), consts};
        }
        size_t size() const { return tableauView.numCol(); }
        auto &view() const { return *this; };
    };
    Solution getSolution() const {
        return Solution{tableau(_, _(numExtraCols, end)), getConstants()};
    }

    // returns `true` if infeasible
    // `false ` if feasible
    bool initiateFeasible() {
        tableau(0, 0) = 0;
        // remove trivially redundant constraints
        hermiteNormalForm();
        // [ I;  X ; b ]
        //
        // original number of variables
        const size_t numVar = getNumVar();
        MutPtrMatrix<int64_t> C{getConstraints()};
#ifdef VERBOSESIMPLEX
        std::cout << "C=" << C << std::endl;
#endif
        MutPtrVector<int64_t> basicCons{getBasicConstraints()};
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
        auto basicVars{getBasicVariables()};
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
        // std::cout << "pre-tableau = \n" << tableau << std::endl;
        llvm::SmallVector<unsigned> augmentVars{};
        for (unsigned i = 0; i < basicVars.size(); ++i)
            if (basicVars[i] == -1)
                augmentVars.push_back(i);
        if (augmentVars.size()) {
            addVars(augmentVars.size()); // NOTE: invalidates all refs
            MutPtrMatrix<int64_t> C{getConstraints()};
            auto basicVars{getBasicVariables()};
            MutPtrVector<int64_t> basicCons{getBasicConstraints()};
            MutPtrVector<int64_t> costs{getCost()};
            // for (auto &&c : costs)
            for (auto &&c : tableau(1, _(0, numExtraCols + getNumVar())))
                c = 0;
#ifdef VERBOSESIMPLEX
            printVector(std::cout << "augmentVars: ", augmentVars) << std::endl;
            std::cout << "costs: " << PtrVector<int64_t>(costs) << std::endl;
            std::cout << "tableau =" << tableau << std::endl;
            std::cout << "numVar = " << numVar << std::endl;
#endif
            for (size_t i = 0; i < augmentVars.size(); ++i) {
                size_t a = augmentVars[i];
                basicVars[a] = i + numVar;
                basicCons[i + numVar] = a;
                C(a, numVar + i) = 1;
                // we now zero out the implicit cost of `1`
                costs(_(begin, numVar)) -= C(a, _(begin, numVar));
            }
            // false/0 means feasible
            // true/non-zero infeasible
#ifdef VERBOSESIMPLEX
            std::cout << "costs: " << PtrVector<int64_t>(costs) << std::endl;
            std::cout << "about to run; tableau =" << tableau << std::endl;
#endif
            if (runCore() != 0)
                return 1;
#ifdef VERBOSESIMPLEX
            std::cout << "initialized tableau =" << tableau << std::endl;
#endif
            // all augment vars are now 0
            truncateVars(numVar);
        }
#ifdef VERBOSESIMPLEX
        std::cout << "final tableau =" << tableau << std::endl;
#endif
        inCanonicalForm = true;
        return 0;
    }
    // 1 based to match getBasicConstraints
    static int getEnteringVariable(PtrVector<int64_t> costs) {
        // Bland's algorithm; guaranteed to terminate
        // std::cout << "costs = " << costs << std::endl;
        for (int i = 1; i < int(costs.size()); ++i)
            if (costs[i] < 0)
                return i;
        return -1;
    }
    static int getLeavingVariable(MutPtrMatrix<int64_t> C,
                                  size_t enteringVariable) {
        // inits guarantee first valid is selected
        int64_t n = -1;
        int64_t d = 0;
        int j = 0;
        for (size_t i = 1; i < C.numRow(); ++i) {
            int64_t Civ = C(i, enteringVariable);
            if (Civ > 0) {
                int64_t Ci0 = C(i, 0);
                if (Ci0 == 0)
                    return --i;
                assert(Ci0 > 0);
                if ((n * Ci0) < (Civ * d)) {
                    n = Civ;
                    d = Ci0;
                    j = i;
                }
            }
        }
        return --j;
    }
    int64_t makeBasic(MutPtrMatrix<int64_t> C, int64_t f,
                      int enteringVariable) {
        int leavingVariable = getLeavingVariable(C, enteringVariable);
#ifdef VERBOSESIMPLEX
        std::cout << "leavingVariable = " << leavingVariable << std::endl;
#endif
        if (leavingVariable == -1)
            return 0; // unbounded
        for (size_t i = 0; i < C.numRow(); ++i)
            if (i != size_t(leavingVariable + 1)) {
                int64_t m = NormalForm::zeroWithRowOperation(
                    C, i, leavingVariable + 1, enteringVariable,
                    i == 0 ? f : 0);
                if (i == 0)
                    f = m;
            }
        // std::cout << "post-removal C =" << C << std::endl;
        // update baisc vars and constraints
        MutStridedVector<int64_t> basicVars{getBasicVariables()};
        int64_t oldBasicVar = basicVars[leavingVariable];
        basicVars[leavingVariable] = enteringVariable;
        MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
        basicConstraints[oldBasicVar] = -1;
        basicConstraints[enteringVariable] = leavingVariable;
        return f;
    }
    // run the simplex algorithm, assuming basicVar's costs have been set to 0
    Rational runCore(int64_t f = 1) {
        //     return runCore(getCostsAndConstraints(), f);
        // }
        // Rational runCore(MutPtrMatrix<int64_t> C, int64_t f = 1) {
        auto C{getCostsAndConstraints()};
        while (true) {
            // entering variable is the column
#ifdef VERBOSESIMPLEX
            std::cout << "C =" << C << std::endl;
#endif
            int enteringVariable = getEnteringVariable(C(0, _));
#ifdef VERBOSESIMPLEX
            std::cout << "enteringVariable = " << enteringVariable << std::endl;
            if (enteringVariable == -1)
                std::cout << "runCore() ret: C(0,0) / f = " << C(0, 0) << " / "
                          << f << std::endl;
#endif
            if (enteringVariable == -1)
                return Rational::create(C(0, 0), f);
            f = makeBasic(C, f, enteringVariable);
            if (f == 0)
                return std::numeric_limits<int64_t>::max(); // unbounded
        }
    }
    // set basicVar's costs to 0, and then runCore()
    Rational run() {
        MutStridedVector<int64_t> basicVars = getBasicVariables();
        MutPtrMatrix<int64_t> C = getCostsAndConstraints();
        int64_t f = 1;
        // zero cost of basic variables to put in canonical form
        for (size_t c = 0; c < basicVars.size();) {
            int64_t v = basicVars[c++];
            // std::cout << "v = " << v << "; C.numRow() = " << C.numRow()
            // << "; C.numCol()  = " << C.numCol() << std::endl;
            if (C(0, v))
                f = NormalForm::zeroWithRowOperation(C, 0, c, v, f);
        }
        return runCore(f);
    }
    // lexicographically minimize vars [0, numVars)
    // false means no problems, true means there was a problem
    void lexMinimize(Vector<Rational> &sol, size_t numVars) {
        MutPtrMatrix<int64_t> C{getCostsAndConstraints()};
        MutStridedVector<int64_t> basicVars{getBasicVariables()};
        MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};

        sol.resizeForOverwrite(numVars);
        for (auto &&r : sol)
            r = Rational{0, 1};
        for (size_t v = 1; v <= numVars; ++v) {
            // if it is already zero (not basic), we can move to the next
            int64_t c = basicConstraints(v);
            if (c < 0)
                continue;
            // C(0, _(v, end)) = 0;
            // we try to zero `v` or at least minimize it.
            // implicitly, set cost to -1, and then see if we can make it
            // basic
            // C(_,0) = C(_,_(1,end)) * vars
            C(0, 0) = C(c, 0);
            C(0, v) = 0;
            C(0, _(v + 1, end)) = C(c, _(v + 1, end));
            // C(0, v) = -1;
            while (true) {
                // get new entering variable
                int enteringVariable = getEnteringVariable(C(0, _(v, end)));
                if (enteringVariable == -1)
                    break;
                enteringVariable += v;
                int leavingVariable = getLeavingVariable(C, enteringVariable);
                if (leavingVariable == -1)
                    break;
                for (size_t i = 0; i < C.numRow(); ++i)
                    if (i != size_t(leavingVariable + 1))
                        NormalForm::zeroWithRowOperation(
                            C, i, leavingVariable + 1, enteringVariable,
                            _(1, v));
                // std::cout << "post-removal C =" << C << std::endl;
                // update baisc vars and constraints
                int64_t oldBasicVar = basicVars[leavingVariable];
                basicVars[leavingVariable] = enteringVariable;
                basicConstraints[oldBasicVar] = -1;
                basicConstraints[enteringVariable] = leavingVariable;
            }
            c = basicConstraints(v);
            if (c >= 0) {
                // C(_,0) = C(_,_(1,end)) * vars
                // we now make `vars[v-1]` a constant
                if ((C(c + 1, v) == 0) && (C(c + 1, 0) != 0)) {
                    SHOWLN(tableau);
                    SHOW(c + 1);
                    CSHOWLN(v);
                }
                assert(!((C(c + 1, v) == 0) && (C(c + 1, 0) != 0)));
                sol(v - 1) = Rational::create(C(c + 1, 0), C(c + 1, v));
                C(c + 1, 0) = 0;
            }
        }
    }
    // A(:,1:end)*x <= A(:,0)
    // B(:,1:end)*x == B(:,0)
    // returns a Simplex if feasible, and an empty `Optional` otherwise
    static llvm::Optional<Simplex> positiveVariables(PtrMatrix<int64_t> A,
                                                     PtrMatrix<int64_t> B) {
        // std::cout << "Entering positive variables!" << std::endl;
        size_t numVar = A.numCol();
        assert(numVar == B.numCol());
        Simplex simplex{};
        size_t numSlack = simplex.numSlackVar = A.numRow();
        size_t numStrict = B.numRow();
        size_t numCon = numSlack + numStrict;
        size_t extraStride = 0;
        // see how many slack vars are infeasible as solution
        for (unsigned i = 0; i < numSlack; ++i)
            extraStride += A(i, 0) < 0;
        // try to avoid reallocating
        size_t stride = numVar + numCon + extraStride + 2;
        simplex.resizeForOverwrite(numCon, numVar + numSlack, stride);
        // construct:
        // [ I A
        //   0 B ]
        // then drop the extra variables
        slackEqualityConstraints(
            simplex.getConstraints()(_(0, numCon), _(1, numVar + numSlack)),
            A(_(0, numSlack), _(1, numVar)), B(_(0, numStrict), _(1, numVar)));
        // std::cout << "before costs simplex.tableau =" << simplex.tableau
        // << std::endl;
        auto consts{simplex.getConstants()};
        for (size_t i = 0; i < numSlack; ++i)
            consts[i] = A(i, 0);
        for (size_t i = 0; i < numStrict; ++i)
            consts[i + numSlack] = B(i, 0);
        // std::cout << "about to initialize simplex.tableau =" <<
        // simplex.tableau
        // << std::endl;
        if (simplex.initiateFeasible())
            return {};
        return simplex;
    }

    void pruneBounds() {
        Simplex simplex;
        for (size_t c = 0; c < getNumConstraints(); ++c) {
            simplex = *this;
            MutPtrMatrix<int64_t> constraints = simplex.getConstraints();
            int64_t bumpedBound = ++constraints(c, 0);
            MutPtrVector<int64_t> cost = simplex.getCost();
            for (size_t v = numSlackVar; v < cost.size(); ++v)
                cost[v] = -constraints(c, v);
            if (simplex.run() != bumpedBound)
                deleteConstraint(c--); // redundant
        }
    }

    void removeVariable(size_t i) {
        // We remove a variable by isolating it, and then dropping the
        // constraint. This allows us to preserve canonical form
        MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
        MutPtrMatrix<int64_t> C{getConstraints()};
        // ensure sure `i` is basic
        if (basicConstraints[i] < 0)
            makeBasic(C, 0, i);
        size_t ind = basicConstraints[i];
        size_t lastRow = C.numRow() - 1;
        // SHOWLN(ind);
        // SHOWLN(C.numRow());
        if (lastRow != ind)
            swapRows(C, ind, lastRow);
        truncateConstraints(lastRow);
    }
    void removeExtraVariables(size_t i) {
        for (size_t j = getNumVar(); j > i;) {
            removeVariable(--j);
            truncateVars(j);
        }
    }
    static uint64_t toMask(PtrVector<int64_t> x) {
        assert(x.size() <= 64);
        uint64_t m = 0;
        for (auto y : x)
            m = ((m << 1) | (y != 0));
        return m;
    }
    uint64_t getBasicTrueVarMask() const {
        const size_t numVarTotal = getNumVar();
        assert(numVarTotal <= 64);
        uint64_t m = 0;
        PtrVector<int64_t> basicCons{getBasicConstraints()};
        for (size_t i = numSlackVar; i < numVarTotal; ++i)
            m = ((m << 1) | (basicCons[i] > 0));
        return m;
    }
    // check if a solution exists such that `x` can be true.
    bool unSatisfiable(PtrVector<int64_t> x, size_t off) const {
        // is it a valid solution to set the first `x.size()` variables to `x`?
        // first, check that >= 0 constraint is satisfied
        for (auto y : x)
            if (y < 0)
                return true;
        // approach will be to move `x.size()` variables into the
        // equality constraints, and then check if the remaining sub-problem is
        // satisfiable.
        Simplex subSimp;
        const size_t numCon = getNumConstraints();
        const size_t numVar = getNumVar();
        const size_t numFix = x.size();
        subSimp.resizeForOverwrite(numCon, numVar - numFix);
        subSimp.tableau(0, 0) = 0;
        subSimp.tableau(0, 1) = 0;
        auto fC{getCostsAndConstraints()};
        auto sC{subSimp.getCostsAndConstraints()};
        sC(_, 0) = fC(_, 0) - fC(_, _(1 + off, 1 + off + numFix)) * x;
        // sC(_, 0) = fC(_, 0);
        // for (size_t i = 0; i < numFix; ++i)
        //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
        sC(_, _(1, 1 + off)) = fC(_, _(1, 1 + off));
        sC(_, _(1 + off, end)) = fC(_, _(1 + off + numFix, end));
        // SHOWLN(off);
        // SHOWLN(fC);
        // SHOWLN(sC);
        // SHOWLN(x);
        // SHOWLN(subSimp);
        return subSimp.initiateFeasible();
    }
    bool satisfiable(PtrVector<int64_t> x, size_t off) const {
        return !unSatisfiable(x, off);
    } // check if a solution exists such that `x` can be true.
    bool unSatisfiableZeroRem(PtrVector<int64_t> x, size_t off,
                              size_t numRow) const {
        // is it a valid solution to set the first `x.size()` variables to `x`?
        // first, check that >= 0 constraint is satisfied
        for (auto y : x)
            if (y < 0)
                return true;
        // approach will be to move `x.size()` variables into the
        // equality constraints, and then check if the remaining sub-problem is
        // satisfiable.
        Simplex subSimp;
        assert(numRow <= getNumConstraints());
        const size_t numFix = x.size();
        subSimp.resizeForOverwrite(numRow, 1 + off);
        subSimp.tableau(0, 0) = 0;
        subSimp.tableau(0, 1) = 0;
        // auto fC{getCostsAndConstraints()};
        // auto sC{subSimp.getCostsAndConstraints()};
        auto fC{getConstraints()};
        auto sC{subSimp.getConstraints()};
        sC(_, 0) = fC(_(begin, numRow), 0) -
                   fC(_(begin, numRow), _(1 + off, 1 + off + numFix)) * x;
        // sC(_, 0) = fC(_, 0);
        // for (size_t i = 0; i < numFix; ++i)
        //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
        sC(_, _(1, 1 + off)) = fC(_(begin, numRow), _(1, 1 + off));
        assert(sC(_, _(1, 1 + off)) == fC(_(begin, numRow), _(1, 1 + off)));
        // SHOWLN(off);
        // SHOWLN(fC);
        // SHOWLN(sC);
        // SHOWLN(x);
        // SHOWLN(subSimp);
        return subSimp.initiateFeasible();
    }
    bool satisfiableZeroRem(PtrVector<int64_t> x, size_t off,
                            size_t numRow) const {
        return !unSatisfiableZeroRem(x, off, numRow);
    }
    void printResult() {
        auto C{getConstraints()};
        auto basicVars{getBasicVariables()};
        // std::cout << "Simplex solution:" << std::endl;
        for (size_t i = 0; i < basicVars.size(); ++i) {
            size_t v = basicVars(i);
            if (v <= numSlackVar)
                continue;
            if (C(i, 0)) {
                if (v < C.numCol()) {
                    std::cout << "v_" << v - numSlackVar << " = " << C(i, 0)
                              << " / " << C(i, v) << std::endl;
                } else {
                    std::cout << "v_" << v << " = " << C(i, 0) << std::endl;
                    assert(false);
                }
            }
        }
    }
    friend std::ostream &operator<<(std::ostream &os, Simplex &s) {
        return os << "\nSimplex; tableau:" << s.tableau;
    }
    /*
    std::tuple<Simplex, IntMatrix, uint64_t> rotate(const IntMatrix &A) const {
        PtrMatrix<const int64_t> C{getConstraints()};
        // C is
        // C(:,0) = C(:,1:numSlackVar)*s_0 + C(:,numSlackVar+1:end)*x
        // we implicitly have additional slack vars `s_1`
        // that define lower bounds of `x` as being 0.
        // 0 = I * s_1 - I * x
        // Calling `rotate(A)` defines `x = A*y`, and returns a simplex
        // in terms of `y`.
        // Thus, we have
        // C(:,0) = C(:,1:numSlackVar)*s_0 + (C(:,numSlackVar+1:end)*A)*y
        // 0 = I * s_1 - A * y
        // The tricky part is that if a row of `A` contains
        // i) more than 1 non-zero, or
        // ii) a negative entry
        // we do not have a clear 0 lower bound on `y`.
        // If we do have a clear 0 lower bound, we can avoid doing work
        // for that row, dropping it.
        // Else, we'll have compute it, calculating the offset needed
        // to lower bound it at 0.
        //
        // Idea for algorithm for getting a lower bound on a var `v`:
        // substitute v_i = v_i^+ - v_i^-
        // then add cost 2v_i^+ - v_i^-; minimize
        // while v_i^- > 0, redefine `v_i` to be offset by the value of `v_i`.

        const size_t numVarTotal = getNumVar();
        const size_t numVar = numVarTotal - numSlackVar;
        assert(A.numCol() == numVar);
        assert(A.numRow() == numVar);
        assert(numVar <= 64);
        uint64_t knownNonNegative = 0;
        // llvm::SmallVector<bool,64> knownNonNegative(numVar);
        for (size_t r = 0; r < numVar; ++r) {
            int nonNegativeIndex = -1;
            for (size_t c = 0; c < numVar; ++c) {
                if (int64_t Arc = A(r, c)) {
                    if ((Arc > 0) && (nonNegativeIndex == -1)) {
                        nonNegativeIndex = c;
                    } else {
                        nonNegativeIndex = -1;
                        break;
                    }
                }
            }
            // `A` is assumed to be full rank, so we can only hit a particular
            // `nonNegativeIndex != -1` once, meaning we do not risk flipping
            // a `true` back off with `^`.
            if (nonNegativeIndex >= 0)
                knownNonNegative ^= (uint64_t(1) << uint64_t(nonNegativeIndex));
            // knownNonNegative[nonNegativeIndex] = true;
        }
        // all `false` indices of `knownNonNegative` indicate
        size_t numPositive = std::popcount(knownNonNegative);
        size_t numUnknownSign = numVar - numPositive;
        // Now, we create structure
        // C(:,0) = C(:,1:numSlackVar)*s_0 + (C(:,numSlackVar+1:end)*A(:,nn))*z
        //  + (C(:,numSlackVar+1:end)*A(:,!nn))*(y^+ - y^-)
        // C(:,0) = C(:,1:numSlackVar)*s_0 + (C(:,numSlackVar+1:end)*A)*z^*
        //  - (C(:,numSlackVar+1:end)*A(:,!nn))*y^-
        // 0 = I(!nn,:) * s_1 - A(!nn,:)*(y^+ - y^-)
        // where `nn` refers to the known non-negative indices
        // and we have separated `y` into `z`, `y^+`, and `y^-`.
        // z = y[nn]
        // y^+ - y^- = y[!nn]
        // y^+ >= 0
        // y^- >= 0
        //
        // Layout is as follows:
        // [1, s_0, s_1, z^*, y^-, aug]
        // where z^* is `z` intermixed with `y^+`
        // We will proceed by trying to maximize `y^-`,
        // shifting it until we get the maximum value
        // to be `0`, in which case we can drop it and let `y^+ -> z`.
        // once all `y^-` are gone, can we drop `s_1`???
        // TODO: see if this is legal, if so we probably want to separate them
        // We can then finally return the simplex as well as the shifts needed
        // for positivity.
        // `aug` are augments to get the simplex into canonical form.
        std::tuple<Simplex, IntMatrix, uint64_t> ret{
            {}, {numUnknownSign, numVar}, knownNonNegative};

        Simplex &simplex{std::get<0>(ret)};
        // IntMatrix &S{std::get<1>(ret)}; // S for Shift
        const size_t numConstraintsOld = getNumConstraints();
        // one additional constraint for each unknown sign
        size_t numConstraintsNew = numConstraintsOld + numUnknownSign;
        // numTrueBasic is the number of non-slack variables in the old simplex
        // that are basic
        // we'll add a temporary slack variable for each of these for sake of
        // initializing the tableau in canonical form.
        uint64_t basicTrueVarMask = getBasicTrueVarMask();
        size_t numTrueBasic = std::popcount(basicTrueVarMask);
        // additional variables are numUnownSign s_1s + numUnknownSign y^-s +
        // numTrueBasic.
        size_t numVarTotalNew = numVarTotal + numUnknownSign + numUnknownSign;
        size_t numVarTotalNewAug = numVarTotalNew + numTrueBasic;
        size_t s1Offset = 1 + numSlackVar;
        size_t zStarOffset = s1Offset + numUnknownSign;
        size_t yMinusOffset = zStarOffset + numVar;
        simplex.numSlackVar = numSlackVar + numUnknownSign;
        // resize instead of resizeForOverwrite because we want lots of 0s
        // maybe we should check if resizeForOverwrite + explicitly writing them
        // is faster
        simplex.resize(numConstraintsNew, numVarTotalNewAug);
        PtrMatrix<int64_t> D{simplex.getConstraints()};
        // first block of `D` corresponds to `s_0`, and is a copy of the slack
        // vars
        for (size_t j = 0; j < numConstraintsOld; ++j)
            for (size_t i = 0; i <= numSlackVar; ++i)
                D(j, i) = C(j, i);
        // next block of `D` is 0 (corresponding to s_1)
        // next block is C(:,trueVars)*A
        matmul(D.view(0, numConstraintsOld, zStarOffset, yMinusOffset),
               C.view(0, numConstraintsOld, zStarOffset, yMinusOffset), A);
        // then we have -C(:,trueVars)*A(:,!nn), corresponding to y^-
        for (size_t j = 0; j < numConstraintsOld; ++j) {
            uint64_t m = knownNonNegative;
            size_t k = numSlackVar + 1;
            for (size_t i = 0; i < numUnknownSign; ++i) {
                uint64_t o = std::countr_one(m);
                k += o;
                m >>= ++o;
                D(j, i + yMinusOffset) = -D(j, k++);
            }
        }
        // the final block corresponds to the augments; first, we set Cons=-1
        // so that we can also set these at the same time.
        PtrVector<int64_t> basicCons{simplex.getBasicConstraints()};
        for (auto &&x : basicCons)
            x = -1;
        MutStridedVector<int64_t> basicVars{simplex.getBasicVariables()};
        PtrVector<int64_t> costs{simplex.getCost()};
        if (numTrueBasic) {
            uint64_t m = basicTrueVarMask;
            size_t k = 0;
            for (size_t i = 0; i < numTrueBasic; ++i) {
                uint64_t o = std::countr_zero(m);
                k += o;
                m >>= ++o;
                for (size_t j = 0; j < numVarTotalNew; ++j)
                    costs[j] -= D(k, j);
                size_t c = numVarTotalNew + i;
                basicCons[c] = k;
                basicVars[k] = c;
                D(k++, c) = 1;
            }
        }
        // now for the new constraints
        // first block, corresponding to `s_0`; it is `0`
        // second block corresponds to `z^*`; we have rows of `A` corresponding
        // to unknown sign.
        // we also handle the y^- block here.
        {
            uint64_t m = knownNonNegative;
            size_t k = 0;
            for (size_t i = 0; i < numUnknownSign; ++i) {
                uint64_t o = std::countr_one(m);
                k += o;
                m >>= ++o;
                // copy A(k,:) into D(numConstraintsOld+i, ...)
                for (size_t j = 0; j < numVar; ++j)
                    D(numConstraintsOld + i, j + zStarOffset) = -A(k, j);
                size_t k2 = 0;
                size_t m2 = knownNonNegative;
                for (size_t j = 0; j < numUnknownSign; ++j) {
                    uint64_t o2 = std::countr_one(m2);
                    k2 += o2;
                    m2 >>= ++o2;
                    D(numConstraintsOld + i, j + yMinusOffset) = A(k, k2++);
                }
                ++k;
            }
        }
        // now the s_1 block is `I`
        for (size_t i = 0; i < numUnknownSign; ++i)
            D(numConstraintsOld + i, s1Offset + i) = 1;
        // finally, the augment block is `0`.

        // now we set the initial basicVars and basicCons
        // we already set the augments, corresponding to old constraints
        // where true variables were basic.
        // Now we set those corresponding to slack variables.
        PtrVector<const int64_t> basicConsOld{getBasicConstraints()};
        for (size_t i = 1; i <= numSlackVar; ++i) {
            int64_t j = basicConsOld[i];
            if (j >= 0) {
                basicCons[i] = j;
                basicVars[j] = i;
            }
        }
        // now we set those corresponding to the new constraints.
        // for the new constraints, it is simply `I` corresponding to `s_1`
        for (size_t i = 0; i < numUnknownSign; ++i) {
            basicCons[numVarTotal + i] = numConstraintsOld + i;
            basicVars[numConstraintsOld + i] = numVarTotal + i;
        }
        // now, initialize costs to remove augment vars
        if (numTrueBasic) {
            int64_t r = simplex.runCore();
            assert(r == 0);
            simplex.truncateVars(numVarTotalNew);
            // we re-zero costs so `numVarTotalNew > yMinusOffset` can
            // assume that costs are 0
            if (numUnknownSign)
                for (auto &&c : costs)
                    c = 0;
        }
        // now our variables are (no augment)
        // [1, s_0, s_1, z^*, y^-]
        // now, our goal is to eliminate `y^-`
        if (numVarTotal) {
            auto CC{simplex.getCostsAndConstraints()};
            while (true) {
                size_t i = numVarTotalNew;
                size_t j = zStarOffset + ((--i) - yMinusOffset);
                costs[i] = -1;
                int64_t c = basicCons[i];
                if (c != -1)
                    NormalForm::zeroWithRowOperation(CC, 0, ++c, j, 0);
                simplex.runCore();
                if ((basicCons[i] == -1) || (D(i, 0) == 0)) {
                    // i == 0
                    numVarTotalNew = i;
                    simplex.truncateVars(i);
                    if (numVarTotalNew == yMinusOffset)
                        break;
                } else {
                    // redefine variable, add offset to `S`
                }
                for (auto &&c : costs)
                    c = 0;
            }
        }
        return ret;
    }
    */
};
