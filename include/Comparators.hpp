#pragma once

#include "./POSet.hpp"
#include "Constraints.hpp"
#include "EmptyArrays.hpp"
#include "Math.hpp"
#include "NormalForm.hpp"
#include "Simplex.hpp"
#include "Symbolics.hpp"
#include "llvm/ADT/Optional.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <ostream>

// For `== 0` constraints
struct EmptyComparator {
    static constexpr size_t getNumConstTerms() { return 0; }
    static constexpr bool greaterEqual(PtrVector<int64_t>, PtrVector<int64_t>) {
        return true;
    }
    static constexpr bool greater(PtrVector<int64_t>, PtrVector<int64_t>) {
        return false;
    }
    static constexpr bool lessEqual(PtrVector<int64_t>, PtrVector<int64_t>) {
        return true;
    }
    static constexpr bool less(PtrVector<int64_t>, PtrVector<int64_t>) {
        return false;
    }
    static constexpr bool equal(PtrVector<int64_t>, PtrVector<int64_t>) {
        return true;
    }
    static constexpr bool greaterEqual(PtrVector<int64_t>) { return true; }
    static constexpr bool greater(PtrVector<int64_t>) { return false; }
    static constexpr bool lessEqual(PtrVector<int64_t>) { return true; }
    static constexpr bool less(PtrVector<int64_t>) { return false; }
    static constexpr bool equal(PtrVector<int64_t>) { return true; }
    static constexpr bool equalNegative(PtrVector<int64_t>,
                                        PtrVector<int64_t>) {
        return true;
    }
    static constexpr bool lessEqual(PtrVector<int64_t>, int64_t x) {
        return 0 <= x;
    }
};

// for non-symbolic constraints
struct LiteralComparator {
    static constexpr size_t getNumConstTerms() { return 1; }
    static inline bool greaterEqual(PtrVector<int64_t> x,
                                    PtrVector<int64_t> y) {
        return x[0] >= y[0];
    }
    static inline bool greater(PtrVector<int64_t> x, PtrVector<int64_t> y) {
        return x[0] > y[0];
    }
    static inline bool lessEqual(PtrVector<int64_t> x, PtrVector<int64_t> y) {
        return x[0] <= y[0];
    }
    static inline bool less(PtrVector<int64_t> x, PtrVector<int64_t> y) {
        return x[0] < y[0];
    }
    static inline bool equal(PtrVector<int64_t> x, PtrVector<int64_t> y) {
        return x[0] == y[0];
    }
    static inline bool greaterEqual(PtrVector<int64_t> x) { return x[0] >= 0; }
    static inline bool greater(PtrVector<int64_t> x) { return x[0] > 0; }
    static inline bool lessEqual(PtrVector<int64_t> x) { return x[0] <= 0; }
    static inline bool less(PtrVector<int64_t> x) { return x[0] < 0; }
    static inline bool equal(PtrVector<int64_t> x) { return x[0] == 0; }
    static inline bool equalNegative(PtrVector<int64_t> x,
                                     PtrVector<int64_t> y) {
        // this version should return correct results for
        // `std::numeric_limits<int64_t>::min()`
        return (x[0] + y[0]) == 0;
    }
    static inline bool lessEqual(PtrVector<int64_t> y, int64_t x) {
        return y[0] <= x;
    }
};
// BaseComparator defines all other comparator methods as a function of
// `greaterEqual`, so that `greaterEqual` is the only one that needs to be
// implemented.
// An assumption is that index `0` is a literal constant, and only indices >0
// are symbolic. Thus, we can shift index-0 to swap between `(>/<)=` and ``>/<`
// comparisons.
//
// Note: only allowed to return `true` if known
// therefore, `a > b -> false` does not imply `a <= b`
template <typename T> struct BaseComparator {
    inline size_t getNumConstTerms() const {
        return static_cast<const T *>(this)->getNumConstTerms();
    }
    inline bool greaterEqual(MutPtrVector<int64_t> delta, PtrVector<int64_t> x,
                             PtrVector<int64_t> y) const {
        const size_t N = getNumConstTerms();
        assert(delta.size() >= N);
        assert(x.size() >= N);
        assert(y.size() >= N);
        for (size_t n = 0; n < N; ++n)
            delta[n] = x[n] - y[n];
        return static_cast<const T *>(this)->greaterEqual(delta);
    }
    inline bool greaterEqual(PtrVector<int64_t> x, PtrVector<int64_t> y) const {
        llvm::SmallVector<int64_t> delta(getNumConstTerms());
        return greaterEqual(delta, x, y);
    }
    inline bool less(PtrVector<int64_t> x, PtrVector<int64_t> y) const {
        return greater(y, x);
    }
    inline bool greater(PtrVector<int64_t> x, PtrVector<int64_t> y) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        assert(N <= y.size());
        llvm::SmallVector<int64_t> delta(N);
        for (size_t n = 0; n < N; ++n)
            delta[n] = x[n] - y[n];
        --delta[0];
        return static_cast<const T *>(this)->greaterEqual(delta);
    }
    inline bool lessEqual(PtrVector<int64_t> x, PtrVector<int64_t> y) const {
        return static_cast<const T *>(this)->greaterEqual(y, x);
    }
    inline bool equal(PtrVector<int64_t> x, PtrVector<int64_t> y) const {
        // check cheap trivial first
        if (x == y)
            return true;
        llvm::SmallVector<int64_t> delta(getNumConstTerms());
        return (greaterEqual(delta, x, y) && greaterEqual(delta, y, x));
    }
    inline bool greaterEqual(PtrVector<int64_t> x) const {
        return static_cast<const T *>(this)->greaterEqual(x);
    }
    inline bool lessEqual(llvm::SmallVectorImpl<int64_t> &x) const {
        return lessEqual(view(x));
    }
    inline bool lessEqual(MutPtrVector<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        for (size_t n = 0; n < N; ++n)
            x[n] *= -1;
        bool ret = static_cast<const T *>(this)->greaterEqual(x);
        for (size_t n = 0; n < N; ++n)
            x[n] *= -1;
        return ret;
    }
    inline bool lessEqual(PtrVector<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 16> y{x.begin(), x.begin() + N};
        return lessEqual(view(y));
    }
    inline bool lessEqual(MutPtrVector<int64_t> x, int64_t y) const {
        int64_t x0 = x[0];
        x[0] = x0 - y;
        bool ret = lessEqual(x);
        x[0] = x0;
        return ret;
    }
    inline bool lessEqual(PtrVector<int64_t> x, int64_t y) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 16> z{x.begin(), x.begin() + N};
        return lessEqual(z, y);
    }
    inline bool less(MutPtrVector<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        int64_t x0 = x[0];
        x[0] = -x0 - 1;
        for (size_t i = 1; i < N; ++i)
            x[i] *= -1;
        bool ret = static_cast<const T *>(this)->greaterEqual(x);
        x[0] = x0;
        for (size_t i = 1; i < N; ++i)
            x[i] *= -1;
        return ret;
    }
    inline bool less(PtrVector<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 16> y{x.begin(), x.begin() + N};
        return less(view(y));
    }
    inline bool greater(MutPtrVector<int64_t> x) const {
        int64_t x0 = x[0]--;
        bool ret = static_cast<const T *>(this)->greaterEqual(x);
        x[0] = x0;
        return ret;
    }
    inline bool greater(PtrVector<int64_t> x) const {
        // TODO: avoid this needless memcopy and (possible) allocation?
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 8> xm{x.begin(), x.begin() + N};
        return greater(view(xm));
    }
    inline bool greater(Vector<int64_t> &x) const { return greater(x.view()); }
    inline bool less(Vector<int64_t> &x) const { return less(x.view()); }
    inline bool lessEqual(Vector<int64_t> &x) const {
        return lessEqual(x.view());
    }
    inline bool lessEqual(Vector<int64_t> &x, int64_t y) const {
        return lessEqual(x.view(), y);
    }

    inline bool equal(PtrVector<int64_t> x) const {
        // check cheap trivial first
        return allZero(x) ||
               (static_cast<const T *>(this)->greaterEqual(x) && lessEqual(x));
    }
    inline bool equalNegative(PtrVector<int64_t> x,
                              PtrVector<int64_t> y) const {
        const size_t N = getNumConstTerms();
        assert(x.size() >= N);
        assert(y.size() >= N);
        bool allEqual = true;
        for (size_t i = 0; i < N; ++i)
            allEqual &= (x[i] + y[i]) == 0;
        if (allEqual)
            return true;
        llvm::SmallVector<int64_t, 8> delta(N);
        for (size_t i = 0; i < N; ++i)
            delta[i] = x[i] + y[i];
        return equal(delta);
    }
};

struct SymbolicComparator : BaseComparator<SymbolicComparator> {
    PartiallyOrderedSet POSet;
    llvm::SmallVector<Polynomial::Monomial> monomials;
    static SymbolicComparator construct(PartiallyOrderedSet poset) {
        SymbolicComparator sc{.POSet = std::move(poset),
                              .monomials =
                                  llvm::SmallVector<Polynomial::Monomial>(0)};

        return sc;
    }
    static SymbolicComparator construct(PtrVector<MPoly> x,
                                        PartiallyOrderedSet poset) {
        SymbolicComparator sc{SymbolicComparator::construct(poset)};
        for (auto &p : x)
            for (auto &t : p)
                if (t.exponent.degree())
                    addTerm(sc.monomials, t.exponent);
        return sc;
    }
    size_t getNumConstTerms() const { return 1 + monomials.size(); }
    MPoly getPoly(PtrVector<int64_t> x) const {
        MPoly delta;
        assert(x.size() >= 1 + monomials.size());
        for (size_t i = 0; i < monomials.size(); ++i)
            if (int64_t d = x[i + 1])
                delta.terms.emplace_back(d, monomials[i]);
        if (int64_t d = x[0])
            delta.terms.emplace_back(d);
        return delta;
    }
    bool greaterEqual(PtrVector<int64_t> x, PtrVector<int64_t> y) const {
        MPoly delta;
        assert(x.size() >= 1 + monomials.size());
        assert(y.size() >= 1 + monomials.size());
        for (size_t i = 0; i < monomials.size(); ++i)
            if (int64_t d = x[i + 1] - y[i + 1])
                delta.terms.emplace_back(d, monomials[i]);
        if (int64_t d = x[0] - y[0])
            delta.terms.emplace_back(d);
        return POSet.knownGreaterEqualZero(delta);
    }
    bool greaterEqual(PtrVector<int64_t> x) const {
        return POSet.knownGreaterEqualZero(getPoly(x));
    }
    std::ostream &printSymbol(std::ostream &os, PtrVector<int64_t> x,
                              int64_t mul = 1) const {
        os << mul * x[0];
        for (size_t i = 1; i < x.size(); ++i)
            if (int64_t xi = x[i] * mul)
                os << (xi > 0 ? " + " : " - ")
                   << Polynomial::Term{std::abs(xi), monomials[i - 1]};
        return os;
    }
};

template <typename T>
concept Comparator = requires(T t, PtrVector<int64_t> x, int64_t y) {
    { t.getNumConstTerms() } -> std::convertible_to<size_t>;
    { t.greaterEqual(x) } -> std::convertible_to<bool>;
    { t.lessEqual(x) } -> std::convertible_to<bool>;
    { t.greater(x) } -> std::convertible_to<bool>;
    { t.less(x) } -> std::convertible_to<bool>;
    { t.equal(x) } -> std::convertible_to<bool>;
    { t.greaterEqual(x, x) } -> std::convertible_to<bool>;
    { t.lessEqual(x, x) } -> std::convertible_to<bool>;
    { t.greater(x, x) } -> std::convertible_to<bool>;
    { t.less(x, x) } -> std::convertible_to<bool>;
    { t.equal(x, x) } -> std::convertible_to<bool>;
    { t.equalNegative(x, x) } -> std::convertible_to<bool>;
    { t.lessEqual(x, y) } -> std::convertible_to<bool>;
};

static_assert(Comparator<SymbolicComparator>);

struct LinearSymbolicComparator : BaseComparator<LinearSymbolicComparator> {
    IntMatrix U;
    IntMatrix V;
    Vector<int64_t> d;
    size_t numRowDiff; // This variable stores the different row size of H
                       // matrix and truncated H matrix
    using BaseComparator<LinearSymbolicComparator>::greaterEqual;
    void init(PtrMatrix<int64_t> Ap,
              EmptyMatrix<int64_t> = EmptyMatrix<int64_t>{}) {
        const auto [numCon, numVar] = Ap.size();
        auto &A = V;
        A.resizeForOverwrite(numVar + numCon, 2 * numCon);
        A = 0;
        // A = [Ap' 0
        //      S   I]
        A(_(begin, numVar), _(begin, numCon)) = Ap.transpose();
        for (size_t j = 0; j < numCon; ++j) {
            A(j + numVar, j) = -1;
            A(j + numVar, j + numCon) = 1;
        }
        initCore();
    }
    void init(PtrMatrix<int64_t> Ap, PtrMatrix<int64_t> Ep) {
        const auto [numInEqCon, numVar] = Ap.size();
        const size_t numEqCon = Ep.numRow();
        IntMatrix A(numVar + numInEqCon, 2 * numInEqCon + numEqCon);
        // A = [Ap' Ep' 0
        //      S   0   I]
        A(_(begin, numVar), _(begin, numInEqCon)) = Ap.transpose();
        A(_(begin, numVar), _(numInEqCon, numInEqCon + numEqCon)) =
            Ep.transpose();

        for (size_t j = 0; j < numInEqCon; ++j) {
            A(j + numVar, j) = -1;
            A(j + numVar, j + numInEqCon) = 1;
        }
        initCore();
    }
    void initCore() {
        auto &A = V;
        size_t numCon = A.numRow();
        U.resizeForOverwrite(numCon, numCon);
        U = 0;
        for (size_t i = 0; i < numCon; ++i)
            U(i, i) = 1;
        // We will have query of the form Ax = q;
        NormalForm::simplifySystemImpl(A, U);
        auto &H = A;
        size_t R = H.numRow();
        size_t numRowPre = R;
        while ((R > 0) && allZero(H.getRow(R - 1)))
            --R;
        H.truncateRows(R);
        numRowDiff = numRowPre - R;
        if (H.isSquare()) {
            d.clear();
            return;
        }
        IntMatrix Ht = H.transpose();
        auto Vt = IntMatrix::identity(Ht.numRow());
        NormalForm::solveSystem(Ht, Vt);
        d = Ht.diag();
        std::cout << "D matrix:" << d << std::endl;
        V = Vt.transpose();
    }

    static LinearSymbolicComparator
    construct(PtrMatrix<int64_t> Ap,
              EmptyMatrix<int64_t> = EmptyMatrix<int64_t>{}) {
        LinearSymbolicComparator cmp;
        cmp.init(Ap);
        return cmp;
    };
    static LinearSymbolicComparator construct(PtrMatrix<int64_t> Ap,
                                              PtrMatrix<int64_t> Ep) {
        LinearSymbolicComparator cmp;
        cmp.init(Ap, Ep);
        return cmp;
    };

    bool greaterEqual(PtrVector<int64_t> query) const {
        auto nEqs = V.numCol() / 2;
        // Full column rank case
        if (d.size() == 0) {
            auto b = U(_, _(begin, query.size())) * query;
            for (size_t i = V.numRow(); i < b.size(); ++i) {
                if (b(i) != 0)
                    return false;
            }
            auto H = V;
            auto oldn = H.numCol();
            H.resizeCols(oldn + 1);
            for (size_t i = 0; i < H.numRow(); ++i)
                H(i, oldn) = b(i);
            NormalForm::solveSystem(H);
            for (size_t i = nEqs; i < H.numRow(); ++i) {
                if (auto rhs = H(i, oldn))
                    if ((rhs > 0) != (H(i, i) > 0)) {
                        std::cout << "Wow: " << i << std::endl;
                        return false;
                    }
            }
            return true;
        }
        // Column rank deficient case
        else {
            auto tmpU =
                U(_(begin, U.numRow() - numRowDiff), _(begin, U.numCol()));
            Vector<int64_t> b =
                U(_(begin, tmpU.numRow()), _(begin, query.size())) * query;
            Vector<int64_t> dinv = d; // copy
            auto Dlcm = dinv[0];
            // We represent D martix as a vector, and multiply the lcm to the
            // linear equation to avoid store D^(-1) as rational type
            for (size_t i = 1; i < dinv.size(); ++i)
                Dlcm = lcm(Dlcm, dinv(i));
            for (size_t i = 0; i < dinv.size(); ++i)
                dinv(i) = Dlcm / dinv(i);
            b *= dinv;
            // for (size_t i = 0; i < b.size(); ++i)
            //     b(i) *= dinv(i);
            IntMatrix JV1(nEqs, tmpU.numRow());
            for (size_t i = 0; i < nEqs; ++i)
                for (size_t j = 0; j < tmpU.numRow(); ++j)
                    JV1(i, j) = V(i + nEqs, j);
            auto c = JV1 * b;
            auto NSdim = V.numRow() - tmpU.numRow();
            // expand W stores [c -JV2 JV2]
            //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
            // where y2 = y2+ - y2-
            IntMatrix expandW(nEqs, NSdim * 2 + 1);
            for (size_t i = 0; i < nEqs; ++i) {
                expandW(i, 0) = c(i);
                // expandW(i, 0) *= Dlcm;
                for (size_t j = 0; j < NSdim; ++j) {
                    auto val = V(i + nEqs, tmpU.numRow() + j) * Dlcm;
                    expandW(i, j + 1) = -val;
                    expandW(i, j + NSdim + 1) = val;
                }
            }
            // std::cout <<"expandW =" << expandW << std::endl;
            IntMatrix Wcouple{0, expandW.numCol()};
            llvm::Optional<Simplex> optS{
                Simplex::positiveVariables(expandW, Wcouple)};
            return optS.hasValue();
        }

        return true;
        // return optS.hasValue();
    }
};

static constexpr void moveEqualities(IntMatrix &, EmptyMatrix<int64_t> &,
                                     const Comparator auto &) {}
static inline void moveEqualities(IntMatrix &A, IntMatrix &E,
                                  const Comparator auto &C) {
    const size_t numVar = E.numCol();
    assert(A.numCol() == numVar);
    if (A.numRow() <= 1)
        return;
    for (size_t o = A.numRow() - 1; o > 0;) {
        for (size_t i = o--; i < A.numRow(); ++i) {
            bool isNeg = true;
            for (size_t v = 0; v < numVar; ++v) {
                if (A(i, v) != -A(o, v)) {
                    isNeg = false;
                    break;
                }
            }
            if (isNeg && C.equalNegative(A.getRow(i), A.getRow(o))) {
                size_t e = E.numRow();
                E.resize(e + 1, numVar);
                for (size_t v = 0; v < numVar; ++v)
                    E(e, v) = A(i, v);
                eraseConstraint(A, i, o);
                break;
            }
        }
    }
}
