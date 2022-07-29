#pragma once

#include "./POSet.hpp"
#include "Simplex.hpp"
#include "Constraints.hpp"
#include "Math.hpp"
#include "NormalForm.hpp"
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
    static constexpr bool greaterEqual(llvm::ArrayRef<int64_t>,
                                       llvm::ArrayRef<int64_t>) {
        return true;
    }
    static constexpr bool greater(llvm::ArrayRef<int64_t>,
                                  llvm::ArrayRef<int64_t>) {
        return false;
    }
    static constexpr bool lessEqual(llvm::ArrayRef<int64_t>,
                                    llvm::ArrayRef<int64_t>) {
        return true;
    }
    static constexpr bool less(llvm::ArrayRef<int64_t>,
                               llvm::ArrayRef<int64_t>) {
        return false;
    }
    static constexpr bool equal(llvm::ArrayRef<int64_t>,
                                llvm::ArrayRef<int64_t>) {
        return true;
    }
    static constexpr bool greaterEqual(llvm::ArrayRef<int64_t>) { return true; }
    static constexpr bool greater(llvm::ArrayRef<int64_t>) { return false; }
    static constexpr bool lessEqual(llvm::ArrayRef<int64_t>) { return true; }
    static constexpr bool less(llvm::ArrayRef<int64_t>) { return false; }
    static constexpr bool equal(llvm::ArrayRef<int64_t>) { return true; }
    static constexpr bool equalNegative(llvm::ArrayRef<int64_t>,
                                        llvm::ArrayRef<int64_t>) {
        return true;
    }
    static constexpr bool lessEqual(llvm::ArrayRef<int64_t>, int64_t x) {
        return 0 <= x;
    }
};

// for non-symbolic constraints
struct LiteralComparator {
    static constexpr size_t getNumConstTerms() { return 1; }
    static inline bool greaterEqual(llvm::ArrayRef<int64_t> x,
                                    llvm::ArrayRef<int64_t> y) {
        return x[0] >= y[0];
    }
    static inline bool greater(llvm::ArrayRef<int64_t> x,
                               llvm::ArrayRef<int64_t> y) {
        return x[0] > y[0];
    }
    static inline bool lessEqual(llvm::ArrayRef<int64_t> x,
                                 llvm::ArrayRef<int64_t> y) {
        return x[0] <= y[0];
    }
    static inline bool less(llvm::ArrayRef<int64_t> x,
                            llvm::ArrayRef<int64_t> y) {
        return x[0] < y[0];
    }
    static inline bool equal(llvm::ArrayRef<int64_t> x,
                             llvm::ArrayRef<int64_t> y) {
        return x[0] == y[0];
    }
    static inline bool greaterEqual(llvm::ArrayRef<int64_t> x) {
        return x[0] >= 0;
    }
    static inline bool greater(llvm::ArrayRef<int64_t> x) { return x[0] > 0; }
    static inline bool lessEqual(llvm::ArrayRef<int64_t> x) {
        return x[0] <= 0;
    }
    static inline bool less(llvm::ArrayRef<int64_t> x) { return x[0] < 0; }
    static inline bool equal(llvm::ArrayRef<int64_t> x) { return x[0] == 0; }
    static inline bool equalNegative(llvm::ArrayRef<int64_t> x,
                                     llvm::ArrayRef<int64_t> y) {
        // this version should return correct results for
        // `std::numeric_limits<int64_t>::min()`
        return (x[0] + y[0]) == 0;
    }
    static inline bool lessEqual(llvm::ArrayRef<int64_t> y, int64_t x) {
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
    inline bool greaterEqual(llvm::MutableArrayRef<int64_t> delta,
                             llvm::ArrayRef<int64_t> x,
                             llvm::ArrayRef<int64_t> y) const {
        const size_t N = getNumConstTerms();
        assert(delta.size() >= N);
        assert(x.size() >= N);
        assert(y.size() >= N);
        for (size_t n = 0; n < N; ++n)
            delta[n] = x[n] - y[n];
        return static_cast<const T *>(this)->greaterEqual(delta);
    }
    inline bool greaterEqual(llvm::ArrayRef<int64_t> x,
                             llvm::ArrayRef<int64_t> y) const {
        llvm::SmallVector<int64_t> delta(getNumConstTerms());
        return greaterEqual(delta, x, y);
    }
    inline bool less(llvm::ArrayRef<int64_t> x,
                     llvm::ArrayRef<int64_t> y) const {
        return greater(y, x);
    }
    inline bool greater(llvm::ArrayRef<int64_t> x,
                        llvm::ArrayRef<int64_t> y) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        assert(N <= y.size());
        llvm::SmallVector<int64_t> delta(N);
        for (size_t n = 0; n < N; ++n)
            delta[n] = x[n] - y[n];
        --delta[0];
        return static_cast<const T *>(this)->greaterEqual(delta);
    }
    inline bool lessEqual(llvm::ArrayRef<int64_t> x,
                          llvm::ArrayRef<int64_t> y) const {
        return static_cast<const T *>(this)->greaterEqual(y, x);
    }
    inline bool equal(llvm::ArrayRef<int64_t> x,
                      llvm::ArrayRef<int64_t> y) const {
        // check cheap trivial first
        if (x == y)
            return true;
        llvm::SmallVector<int64_t> delta(getNumConstTerms());
        return (greaterEqual(delta, x, y) && greaterEqual(delta, y, x));
    }
    inline bool greaterEqual(llvm::ArrayRef<int64_t> x) const {
        return static_cast<const T *>(this)->greaterEqual(x);
    }
    inline bool lessEqual(llvm::SmallVectorImpl<int64_t> &x) const {
        return lessEqual(llvm::MutableArrayRef<int64_t>(x));
    }
    inline bool lessEqual(llvm::MutableArrayRef<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        for (size_t n = 0; n < N; ++n)
            x[n] *= -1;
        bool ret = static_cast<const T *>(this)->greaterEqual(x);
        for (size_t n = 0; n < N; ++n)
            x[n] *= -1;
        return ret;
    }
    inline bool lessEqual(llvm::MutableArrayRef<int64_t> x, int64_t y) const {
        int64_t x0 = x[0];
        x[0] = x0 - y;
        bool ret = lessEqual(x);
        x[0] = x0;
        return ret;
    }
    inline bool less(llvm::MutableArrayRef<int64_t> x) const {
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
    inline bool lessEqual(llvm::ArrayRef<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 16> y{x.begin(), x.begin() + N};
        return lessEqual(llvm::MutableArrayRef<int64_t>(y));
    }
    inline bool less(llvm::ArrayRef<int64_t> x) const {
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 16> y{x.begin(), x.begin() + N};
        return less(llvm::MutableArrayRef<int64_t>(y));
    }
    inline bool greater(llvm::MutableArrayRef<int64_t> x) const {
        int64_t x0 = x[0]--;
        bool ret = static_cast<const T *>(this)->greaterEqual(x);
        x[0] = x0;
        return ret;
    }
    inline bool greater(llvm::ArrayRef<int64_t> x) const {
        // TODO: avoid this needless memcopy and (possible) allocation?
        const size_t N = getNumConstTerms();
        assert(N <= x.size());
        llvm::SmallVector<int64_t, 8> xm{x.begin(), x.begin() + N};
        return greater(llvm::MutableArrayRef<int64_t>(xm));
    }
    inline bool equal(llvm::ArrayRef<int64_t> x) const {
        // check cheap trivial first
        return allZero(x) ||
               (static_cast<const T *>(this)->greaterEqual(x) && lessEqual(x));
    }
    inline bool equalNegative(llvm::ArrayRef<int64_t> x,
                              llvm::ArrayRef<int64_t> y) const {
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
                                  llvm::SmallVector<Polynomial::Monomial>{}};

        return sc;
    }
    static SymbolicComparator construct(llvm::ArrayRef<MPoly> x,
                                        PartiallyOrderedSet poset) {
        SymbolicComparator sc{SymbolicComparator::construct(poset)};
        for (auto &p : x)
            for (auto &t : p)
                if (t.exponent.degree())
                    addTerm(sc.monomials, t.exponent);
        return sc;
    }
    size_t getNumConstTerms() const { return 1 + monomials.size(); }
    MPoly getPoly(llvm::ArrayRef<int64_t> x) const {
        MPoly delta;
        assert(x.size() >= 1 + monomials.size());
        for (size_t i = 0; i < monomials.size(); ++i)
            if (int64_t d = x[i + 1])
                delta.terms.emplace_back(d, monomials[i]);
        if (int64_t d = x[0])
            delta.terms.emplace_back(d);
        return delta;
    }
    bool greaterEqual(llvm::ArrayRef<int64_t> x,
                      llvm::ArrayRef<int64_t> y) const {
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
    bool greaterEqual(llvm::ArrayRef<int64_t> x) const {
        return POSet.knownGreaterEqualZero(getPoly(x));
    }
    std::ostream &printSymbol(std::ostream &os, llvm::ArrayRef<int64_t> x,
                              int64_t mul = 1) const {
        os << mul * x[0];
        for (size_t i = 1; i < x.size(); ++i)
            if (int64_t xi = x[i])
                os << " + " << Polynomial::Term{xi * mul, monomials[i - 1]};
        return os;
    }
};

template <typename T>
concept Comparator = requires(T t, llvm::ArrayRef<int64_t> x, int64_t y) {
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

struct LinearSymbolicComparator : BaseComparator<LinearSymbolicComparator> {
    IntMatrix U;
    IntMatrix V;
    llvm::Optional<llvm::SmallVector<int64_t, 16>> d;
    size_t numRowDiff;
    //llvm::SmallVector<int64_t, 16> sol;
    static LinearSymbolicComparator construct(IntMatrix Ap) {
        // std::cout << "start Test = " << std::endl;
        const auto [numCon, numVar] = Ap.size();
        IntMatrix A(numVar + numCon, 2 * numCon);
        // A = [Ap' 0
        //      S   I]
        for (size_t i = 0; i < numCon; ++i)
            for (size_t j = 0; j < numVar; ++j)
                A(j, i) = Ap(i, j);

        for (size_t j = 0; j < numCon; ++j) {
            A(j + numVar, j) = -1;
            A(j + numVar, j + numCon) = 1;
        }
        // std::cout << "A = " << A << std::endl;
        // We will have query of the form Ax = q;
        auto [H, U] = NormalForm::hermite(std::move(A));
        // auto NS2 = NormalForm::nullSpace(H);
        // std::cout << "H = " << H << std::endl;
        // std::cout << "NS = " << NS2.numRow() << std::endl;
        size_t R = H.numRow();
        size_t numRowPre = R;
        while ((R > 0) && allZero(H.getRow(R - 1)))
            --R;
        H.truncateRows(R);
        size_t numRowDiff = numRowPre - R;
        // std::cout << "H = " << H << std::endl;
        // auto NS = NormalForm::nullSpace(H);
        // std::cout << "NS = " << NS.numRow() << std::endl;
        if (H.isSquare())
            return LinearSymbolicComparator{.U = std::move(U), .V = std::move(H), .d = {}};
        // std::cout << "H = " << H << std::endl;
        // std::cout << "U = " << U << std::endl;
        //std::cout << "U matrix:" << U << std::endl;
        auto Ht = H.transpose();
        // std::cout << "Ht matrix:" << Ht << std::endl;
        auto Vt = IntMatrix::identity(Ht.numRow());
        // Vt * Ht = D
        // std::cout<< "Ht row size = " << Ht.numRow() <<std::endl;
        NormalForm::solveSystem(Ht, Vt);
        // std::cout << "Vt =" << Vt << std::endl;
        // H * V = Diagonal(d)
        // std::cout <<"D = " << Ht << std::endl;
        auto d = Ht.diag();
        printVector(std::cout << "D matrix:", d) << std::endl;
        auto V = Vt.transpose();
        return LinearSymbolicComparator{.U = std::move(U), .V = std::move(V), .d = std::move(d), .numRowDiff = numRowDiff} ;
    };

    bool greaterEqualZero(llvm::ArrayRef<int64_t> query) const {
        std::cout << "----start testing greaterEqualZero-----"<< std::endl;
        auto nVars = query.size();
        auto nEqs = V.numCol() / 2;
        if (!d.hasValue()) {
            auto b = U.view(0, U.numRow(), 0, query.size()) * query;
            // std::cout << "U =" << U << std::endl;
            for (size_t i = V.numRow(); i < b.size(); ++i) {
                if (b[i] != 0)
                    return false;
            }       
            auto H = V;
            // std::cout << "H = \n" << H << std::endl;
            auto oldn = H.numCol();
            H.resizeCols(oldn + 1);
            // std::cout << "V = \n" << V << std::endl;
            // std::cout << "H = \n" << H << std::endl;
            for (size_t i = 0; i < H.numRow(); ++i)
                H(i, oldn) = b[i];
            NormalForm::solveSystem(H);
            // std::cout <<"after solving:" << H << std::endl;
            for (size_t i = nEqs; i < H.numRow(); ++i) {
                if (auto rhs = H(i, oldn))
                    if ((rhs > 0) != (H(i, i) > 0)) {
                        std::cout    << "Wow: " << i << std::endl;
                        return false;
                    }
            }
            return true;
        }
        else{
            // std::cout << "Col rank deficient" << std::endl;
            //IntMatrix tmpU(U.numRow()-1, U.numCol());
            // for (size_t i = 0; i < tmpU.numRow(); ++i)
            //     for (size_t j = 0; j < tmpU.numCol(); ++j)
            //         tmpU(i, j) = U(i, j);
            auto tmpU = U.view(0, U.numRow()-numRowDiff, 0, U.numCol());
            auto b = U.view(0, tmpU.numRow(), 0, query.size()) * query;
            // std::cout << "b size 0= " << b.size() << std::endl;
            IntMatrix J(nEqs, 2 * nEqs);
            for (size_t i = 0; i < nEqs; ++i)
                 J(i, i + nEqs) = 1;
            auto dinv = d.getValue();
            auto Dlcm = dinv[0];
            for (size_t i = 1; i < dinv.size(); ++i){
                Dlcm = lcm(Dlcm, dinv[i]);
            }
            for (size_t i = 0; i < dinv.size(); ++i){
                dinv[i] = Dlcm / dinv[i];
                // std::cout << "d inv i= " << dinv[i] << std::endl;
            }
            // std::cout << "d size = " << dinv.size() << std::endl;
            // std::cout << "b size = " << b.size() << std::endl;
            for (size_t i = 0; i < b.size(); ++i){
                b[i] *= dinv[i];
            }
            // std::cout <<"Dlcm = " << Dlcm << std::endl;
            auto JV1 = matmul(J, V.view(0, V.numRow(), 0, tmpU.numRow()));
            //std::cout <<"JV1 = " << JV1 << std::endl;
            auto c = JV1 * b;
            auto NSdim = V.numRow() - tmpU.numRow();
            IntMatrix expandW(nEqs, NSdim * 2 +1);
            // auto JV2 = matmul(J, V.view(0, V.numRow(), tmpU.numRow(), V.numCol()));
            // std::cout <<"JV2 = " << JV2 << std::endl;
            // std::cout <<"V2 = " << V << std::endl;
            for (size_t i = 0; i < nEqs; ++i)
            {
                expandW(i, 0) = c[i];
                // expandW(i, 0) *= Dlcm;
                for (size_t j = 0; j < NSdim; ++j){
                    auto val = V(i, tmpU.numRow() + j) * Dlcm;
                    //auto val = JV2(i, j) * Dlcm;
                    // should change positive and negative?
                    expandW(i, j + 1) = -val;
                    expandW(i, j + NSdim + 1) = val;
                }
            }
            // std::cout <<"expandW =" << expandW << std::endl;
            IntMatrix Wcouple{0, expandW.numCol()};
            llvm::Optional<Simplex> optS{Simplex::positiveVariables(expandW, Wcouple)};
            //optS.hasValue()
            // std::cout <<"size of dinv: " << dinv.size() << std::endl;
            // std::cout <<"size of b: " << b.size() << std::endl;
            // std::cout <<"has value " << optS.hasValue() << std::endl;
            return optS.hasValue();
        }
        //for low rank deficient case:
        //U: 10 x 10;
        //Vt: 8 x 8;
        //
        // if (D.numRow() > D.numCol()){
        //     auto const numCon = V.numRow()/2;
        //     IntMatrix J(numCon, 2 * numCon);
        //     for (size_t i = 0; i < numCon; ++i)
        //         J(i, i + numCon) = 1;
        //     IntMatrix q(query.size() + numCon,1);
        //     for (size_t i = 0; i < query.size(); ++i)
        //         q[i] = query[i];
        //     size_t NSdim = D.numCol() - D.numRow();
        //     // IntMatrix JV2(numCon, NSdim * 2);
        //     IntMatrix JV1DiUq(numCon, 1);
        //     // std::cout<< "J = " << J << std::endl;
        //     // std::cout<< "V = " << V << std::endl;
        //     auto JV1 = matmul(J, V.view(0, V.numRow(),0,numCon*2-NSdim));
        //     auto JV1Di = matmul(std::move(JV1), D.view(0,numCon*2-NSdim,0,numCon*2));
        //     //std::cout << "col size = " << U.numsCol() << std::endl;
        //     auto b = matmul(U, std::move(q));
        //     // std::cout << "JV1Di = " << JV1Di << std::endl;
        //     // std::cout << "b = " << b << std::endl;
        //     //identify b and JV1Dis size
        //     auto c = matmul(std::move(JV1Di), std::move(b));
        //     IntMatrix expandV2(numCon*2, NSdim * 2 + 1); //extra one dim for simplex
        //     for (size_t i = 0; i < numCon * 2; ++i)
        //     {
        //         expandV2(i, 0) = c(i, 0);
        //         for (size_t j = 0; j < NSdim * 2; ++j){
        //             expandV2(i, j + 1) = V(i, j);
        //             expandV2(i, j + NSdim + 1) = V(i, j);
        //         }
        //     }
        //     IntMatrix B{0, expandV2.numCol()};
        //     llvm::Optional<Simplex> optS{Simplex::positiveVariables(expandV2, B)};
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
