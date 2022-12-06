#pragma once

#include "./Constraints.hpp"
#include "./EmptyArrays.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Simplex.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>

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
/// BaseComparator defines all other comparator methods as a function of
/// `greaterEqual`, so that `greaterEqual` is the only one that needs to be
/// implemented.
/// An assumption is that index `0` is a literal constant, and only indices >0
/// are symbolic. Thus, we can shift index-0 to swap between `(>/<)=` and ``>/<`
/// comparisons.
///
/// Note: only allowed to return `true` if known
/// therefore, `a > b -> false` does not imply `a <= b`
template <typename T> struct BaseComparator {
    inline size_t getNumConstTerms() const {
        return static_cast<const T *>(this)->getNumConstTermsImpl();
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
    inline bool greater(Vector<int64_t> &x) const {
        return greater(MutPtrVector<int64_t>(x));
    }
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

template <typename T>
concept Comparator = requires(T t, PtrVector<int64_t> x, int64_t y) {
                         {
                             t.getNumConstTerms()
                             } -> std::convertible_to<size_t>;
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

struct LinearSymbolicComparator : BaseComparator<LinearSymbolicComparator> {
    [[no_unique_address]] IntMatrix U;
    [[no_unique_address]] IntMatrix V;
    [[no_unique_address]] Vector<int64_t> d;
    [[no_unique_address]] size_t numVar;
    [[no_unique_address]] size_t numEquations;
    using BaseComparator<LinearSymbolicComparator>::greaterEqual;
    size_t getNumConstTermsImpl() const { return numVar; }
    void init(PtrMatrix<int64_t> A,
              EmptyMatrix<int64_t> = EmptyMatrix<int64_t>{}, bool pos0 = true) {
        const size_t numCon = A.numRow() + pos0;
        numVar = A.numCol();
        V.resizeForOverwrite(numVar + numCon, 2 * numCon);
        V = 0;
        V(0, 0) = pos0;
        // V = [A' 0
        //      S  I]
        V(_(begin, numVar), _(pos0, numCon)) = A.transpose();
        for (size_t j = 0; j < numCon; ++j) {
            V(j + numVar, j) = -1;
            V(j + numVar, j + numCon) = 1;
        }
        numEquations = numCon;
        initCore();
    }
    inline void initNonNegative(PtrMatrix<int64_t> A, EmptyMatrix<int64_t>,
                                size_t numNonNegative) {
        initNonNegative(A, numNonNegative);
    }
    void initNonNegative(PtrMatrix<int64_t> A, size_t numNonNegative) {
        // we have an additional numNonNegative x numNonNegative identity matrix
        // as the lower right block of `A`.
        const size_t numConExplicit = A.numRow() + 1;
        const size_t numConTotal = numConExplicit + numNonNegative;
        numVar = A.numCol();
        V.resizeForOverwrite(numVar + numConTotal, 2 * numConTotal);
        V = 0;
        V(0, 0) = 1;
        // B = [ A_0 A_1
        //        0   I  ]
        // V = [B' 0
        //      S   I]
        // V = [A_0'  0  0
        //      A_1'  I  0
        //      S_0  S_1 I]
        V(_(begin, numVar), _(1, numConExplicit)) = A.transpose();
        for (size_t j = 0; j < numNonNegative; ++j)
            V(j + numVar - numNonNegative, numConExplicit + j) = 1;
        for (size_t j = 0; j < numConTotal; ++j) {
            V(j + numVar, j) = -1;
            V(j + numVar, j + numConTotal) = 1;
        }
        numEquations = numConTotal;
        initCore();
    }
    void initNonNegative(PtrMatrix<int64_t> A, PtrMatrix<int64_t> E,
                         size_t numNonNegative) {
        // we have an additional numNonNegative x numNonNegative identity matrix
        // as the lower right block of `A`.
        const size_t numInEqConExplicit = A.numRow() + 1;
        const size_t numInEqConTotal = numInEqConExplicit + numNonNegative;
        const size_t numEqCon = E.numRow();
        numVar = A.numCol();
        V.resizeForOverwrite(numVar + numInEqConTotal,
                             2 * numInEqConTotal + numEqCon);
        V = 0;
        V(0, 0) = 1;
        // B = [ A_0 A_1
        //        0   I  ]
        // V = [B' E' 0
        //      S  0  I]
        // V = [A_0'  0  E_0' 0
        //      A_1'  I  E_1' 0
        //      S_0  S_1  0   I]
        numEquations = numInEqConTotal + numEqCon;
        V(_(begin, numVar), _(1, numInEqConExplicit)) = A.transpose();
        V(_(begin, numVar), _(numInEqConTotal, numInEqConTotal + numEqCon)) =
            E.transpose();
        for (size_t j = 0; j < numNonNegative; ++j)
            V(j + numVar - numNonNegative, numInEqConExplicit + j) = 1;
        for (size_t j = 0; j < numInEqConTotal; ++j) {
            V(j + numVar, j) = -1;
            V(j + numVar, j + numEquations) = 1;
        }
        initCore();
    }
    void init(PtrMatrix<int64_t> A, PtrMatrix<int64_t> E, bool pos0 = true) {
        const size_t numInEqCon = A.numRow() + pos0;
        numVar = A.numCol();
        const size_t numEqCon = E.numRow();
        V.resizeForOverwrite(numVar + numInEqCon, 2 * numInEqCon + numEqCon);
        V = 0;
        // V = [A' E' 0
        //      S  0  I]
        V(0, 0) = pos0;
        V(_(begin, numVar), _(pos0, numInEqCon)) = A.transpose();
        // A(_, _(pos0, end)).transpose();
        V(_(begin, numVar), _(numInEqCon, numInEqCon + numEqCon)) =
            E.transpose();

        numEquations = numInEqCon + numEqCon;
        for (size_t j = 0; j < numInEqCon; ++j) {
            V(j + numVar, j) = -1;
            V(j + numVar, j + numEquations) = 1;
        }
        initCore();
    }
    void initCore() {
        auto &A = V;
        size_t R = V.numRow();
        U.resizeForOverwrite(R, R);
        U = 0;
        for (size_t i = 0; i < R; ++i)
            U(i, i) = 1;
        // We will have query of the form Ax = q;
        NormalForm::simplifySystemImpl(A, U);
        auto &H = A;
        while ((R) && allZero(H(R - 1, _)))
            --R;
        H.truncateRows(R);
        U.truncateRows(R);
        // numRowTrunc = R;
        if (H.isSquare()) {
            d.clear();
            return;
        }
        IntMatrix Ht = H.transpose();
        auto Vt = IntMatrix::identity(Ht.numRow());
        NormalForm::solveSystem(Ht, Vt);
        d = Ht.diag();
        V = Vt.transpose();
    }

    static LinearSymbolicComparator
    construct(PtrMatrix<int64_t> Ap,
              EmptyMatrix<int64_t> = EmptyMatrix<int64_t>{}, bool pos0 = true) {
        LinearSymbolicComparator cmp;
        cmp.init(Ap, EmptyMatrix<int64_t>{}, pos0);
        return cmp;
    };
    static LinearSymbolicComparator construct(PtrMatrix<int64_t> Ap,
                                              bool pos0) {
        return construct(Ap, EmptyMatrix<int64_t>{}, pos0);
    };
    static LinearSymbolicComparator
    construct(PtrMatrix<int64_t> Ap, PtrMatrix<int64_t> Ep, bool pos0 = true) {
        LinearSymbolicComparator cmp;
        cmp.init(Ap, Ep, pos0);
        return cmp;
    };
    // Note that this is only valid when the comparator was constructed
    // with index `0` referring to >= 0 constants (i.e., the default).
    bool isEmpty() {
        StridedVector<int64_t> b{StridedVector<int64_t>(U(_, 0))};
        if (d.size() == 0) {
            for (size_t i = V.numRow(); i < b.size(); ++i)
                if (b(i))
                    return false;
            auto H = V;
            auto oldn = H.numCol();
            H.resizeCols(oldn + 1);
            for (size_t i = 0; i < H.numRow(); ++i)
                H(i, oldn) = -b(i);
            NormalForm::solveSystem(H);
            for (size_t i = numEquations; i < H.numRow(); ++i)
                if (auto rhs = H(i, oldn))
                    if ((rhs > 0) != (H(i, i) > 0))
                        return false;
            return true;
        }
        // Column rank deficient case
        else {
            size_t numSlack = V.numRow() - numEquations;
            // Vector<int64_t> dinv = d; // copy
            auto Dlcm = d[0];
            // We represent D martix as a vector, and multiply the lcm to the
            // linear equation to avoid store D^(-1) as rational type
            for (size_t i = 1; i < d.size(); ++i)
                Dlcm = lcm(Dlcm, d(i));
            Vector<int64_t> b2;
            b2.resizeForOverwrite(d.size());
            for (size_t i = 0; i < d.size(); ++i)
                b2(i) = -b(i) * Dlcm / d(i);
            size_t numRowTrunc = U.numRow();
            Vector<int64_t> c =
                V(_(numEquations, end), _(begin, numRowTrunc)) * b2;
            auto NSdim = V.numCol() - numRowTrunc;
            // expand W stores [c -JV2 JV2]
            //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
            // where y2 = y2+ - y2-
            IntMatrix expandW(numSlack, NSdim * 2 + 1);
            for (size_t i = 0; i < numSlack; ++i) {
                expandW(i, 0) = c(i);
                // expandW(i, 0) *= Dlcm;
                for (size_t j = 0; j < NSdim; ++j) {
                    auto val = V(i + numEquations, numRowTrunc + j) * Dlcm;
                    expandW(i, j + 1) = -val;
                    expandW(i, j + NSdim + 1) = val;
                }
            }
            IntMatrix Wcouple{0, expandW.numCol()};
            llvm::Optional<Simplex> optS{
                Simplex::positiveVariables(expandW, Wcouple)};
            // if (optS.hasValue())
            //     optS->printResult();
            return optS.hasValue();
        }
        return true;
    }
    bool greaterEqual(PtrVector<int64_t> query) const {
        Vector<int64_t> b = U(_, _(begin, query.size())) * query;
        // Full column rank case
        if (d.size() == 0) {
            for (size_t i = V.numRow(); i < b.size(); ++i)
                if (b(i))
                    return false;
            auto H = V;
            auto oldn = H.numCol();
            H.resizeCols(oldn + 1);
            for (size_t i = 0; i < H.numRow(); ++i)
                H(i, oldn) = b(i);
            NormalForm::solveSystem(H);
            for (size_t i = numEquations; i < H.numRow(); ++i)
                if (auto rhs = H(i, oldn))
                    if ((rhs > 0) != (H(i, i) > 0))
                        return false;
            return true;
        }
        // Column rank deficient case
        else {
            size_t numSlack = V.numRow() - numEquations;
            Vector<int64_t> dinv = d; // copy
            auto Dlcm = dinv[0];
            // We represent D martix as a vector, and multiply the lcm to the
            // linear equation to avoid store D^(-1) as rational type
            for (size_t i = 1; i < dinv.size(); ++i)
                Dlcm = lcm(Dlcm, dinv(i));
            for (size_t i = 0; i < dinv.size(); ++i)
                dinv(i) = Dlcm / dinv(i);
            b *= dinv;
            size_t numRowTrunc = U.numRow();
            Vector<int64_t> c =
                V(_(numEquations, end), _(begin, numRowTrunc)) * b;
            auto NSdim = V.numCol() - numRowTrunc;
            // expand W stores [c -JV2 JV2]
            //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
            // where y2 = y2+ - y2-
            IntMatrix expandW(numSlack, NSdim * 2 + 1);
            for (size_t i = 0; i < numSlack; ++i) {
                expandW(i, 0) = c(i);
                // expandW(i, 0) *= Dlcm;
                for (size_t j = 0; j < NSdim; ++j) {
                    auto val = V(i + numEquations, numRowTrunc + j) * Dlcm;
                    expandW(i, j + 1) = -val;
                    expandW(i, j + NSdim + 1) = val;
                }
            }
            IntMatrix Wcouple{0, expandW.numCol()};
            llvm::Optional<Simplex> optS{
                Simplex::positiveVariables(expandW, Wcouple)};
            // if (optS.hasValue())
            //     optS->printResult();
            return optS.hasValue();
        }
    }
};

static_assert(Comparator<LinearSymbolicComparator>);

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
            if (isNeg && C.equalNegative(A(i, _), A(o, _))) {
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
