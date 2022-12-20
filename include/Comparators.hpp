#pragma once

#include "./Constraints.hpp"
#include "./EmptyArrays.hpp"
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
  static constexpr auto getNumConstTerms() -> size_t { return 0; }
  static constexpr auto greaterEqual(PtrVector<int64_t>, PtrVector<int64_t>)
    -> bool {
    return true;
  }
  static constexpr auto greater(PtrVector<int64_t>, PtrVector<int64_t>)
    -> bool {
    return false;
  }
  static constexpr auto lessEqual(PtrVector<int64_t>, PtrVector<int64_t>)
    -> bool {
    return true;
  }
  static constexpr auto less(PtrVector<int64_t>, PtrVector<int64_t>) -> bool {
    return false;
  }
  static constexpr auto equal(PtrVector<int64_t>, PtrVector<int64_t>) -> bool {
    return true;
  }
  static constexpr auto greaterEqual(PtrVector<int64_t>) -> bool {
    return true;
  }
  static constexpr auto greater(PtrVector<int64_t>) -> bool { return false; }
  static constexpr auto lessEqual(PtrVector<int64_t>) -> bool { return true; }
  static constexpr auto less(PtrVector<int64_t>) -> bool { return false; }
  static constexpr auto equal(PtrVector<int64_t>) -> bool { return true; }
  static constexpr auto equalNegative(PtrVector<int64_t>, PtrVector<int64_t>)
    -> bool {
    return true;
  }
  static constexpr auto lessEqual(PtrVector<int64_t>, int64_t x) -> bool {
    return 0 <= x;
  }
};

// for non-symbolic constraints
struct LiteralComparator {
  static constexpr auto getNumConstTerms() -> size_t { return 1; }
  static inline auto greaterEqual(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] >= y[0];
  }
  static inline auto greater(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] > y[0];
  }
  static inline auto lessEqual(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] <= y[0];
  }
  static inline auto less(PtrVector<int64_t> x, PtrVector<int64_t> y) -> bool {
    return x[0] < y[0];
  }
  static inline auto equal(PtrVector<int64_t> x, PtrVector<int64_t> y) -> bool {
    return x[0] == y[0];
  }
  static inline auto greaterEqual(PtrVector<int64_t> x) -> bool {
    return x[0] >= 0;
  }
  static inline auto greater(PtrVector<int64_t> x) -> bool { return x[0] > 0; }
  static inline auto lessEqual(PtrVector<int64_t> x) -> bool {
    return x[0] <= 0;
  }
  static inline auto less(PtrVector<int64_t> x) -> bool { return x[0] < 0; }
  static inline auto equal(PtrVector<int64_t> x) -> bool { return x[0] == 0; }
  static inline auto equalNegative(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    // this version should return correct results for
    // `std::numeric_limits<int64_t>::min()`
    return (x[0] + y[0]) == 0;
  }
  static inline auto lessEqual(PtrVector<int64_t> y, int64_t x) -> bool {
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
  [[nodiscard]] inline auto getNumConstTerms() const -> size_t {
    return static_cast<const T *>(this)->getNumConstTermsImpl();
  }
  [[nodiscard]] inline auto greaterEqual(MutPtrVector<int64_t> delta,
                                         PtrVector<int64_t> x,
                                         PtrVector<int64_t> y) const -> bool {
    const size_t N = getNumConstTerms();
    assert(delta.size() >= N);
    assert(x.size() >= N);
    assert(y.size() >= N);
    for (size_t n = 0; n < N; ++n)
      delta[n] = x[n] - y[n];
    return static_cast<const T *>(this)->greaterEqual(delta);
  }
  [[nodiscard]] inline auto greaterEqual(PtrVector<int64_t> x,
                                         PtrVector<int64_t> y) const -> bool {
    llvm::SmallVector<int64_t> delta(getNumConstTerms());
    return greaterEqual(delta, x, y);
  }
  [[nodiscard]] inline auto less(PtrVector<int64_t> x,
                                 PtrVector<int64_t> y) const -> bool {
    return greater(y, x);
  }
  [[nodiscard]] inline auto greater(PtrVector<int64_t> x,
                                    PtrVector<int64_t> y) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    assert(N <= y.size());
    llvm::SmallVector<int64_t> delta(N);
    for (size_t n = 0; n < N; ++n)
      delta[n] = x[n] - y[n];
    --delta[0];
    return static_cast<const T *>(this)->greaterEqual(delta);
  }
  [[nodiscard]] inline auto lessEqual(PtrVector<int64_t> x,
                                      PtrVector<int64_t> y) const -> bool {
    return static_cast<const T *>(this)->greaterEqual(y, x);
  }
  [[nodiscard]] inline auto equal(PtrVector<int64_t> x,
                                  PtrVector<int64_t> y) const -> bool {
    // check cheap trivial first
    if (x == y)
      return true;
    llvm::SmallVector<int64_t> delta(getNumConstTerms());
    return (greaterEqual(delta, x, y) && greaterEqual(delta, y, x));
  }
  [[nodiscard]] inline auto greaterEqual(PtrVector<int64_t> x) const -> bool {
    return static_cast<const T *>(this)->greaterEqual(x);
  }
  inline auto lessEqual(llvm::SmallVectorImpl<int64_t> &x) const -> bool {
    return lessEqual(LinearAlgebra::view(x));
  }
  [[nodiscard]] inline auto lessEqual(MutPtrVector<int64_t> x) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    for (size_t n = 0; n < N; ++n)
      x[n] *= -1;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    for (size_t n = 0; n < N; ++n)
      x[n] *= -1;
    return ret;
  }
  [[nodiscard]] inline auto lessEqual(PtrVector<int64_t> x) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    llvm::SmallVector<int64_t, 16> y{x.begin(), x.begin() + N};
    return lessEqual(LinearAlgebra::view(y));
  }
  [[nodiscard]] inline auto lessEqual(MutPtrVector<int64_t> x, int64_t y) const
    -> bool {
    int64_t x0 = x[0];
    x[0] = x0 - y;
    bool ret = lessEqual(x);
    x[0] = x0;
    return ret;
  }
  [[nodiscard]] inline auto lessEqual(PtrVector<int64_t> x, int64_t y) const
    -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    llvm::SmallVector<int64_t, 16> z{x.begin(), x.begin() + N};
    return lessEqual(z, y);
  }
  [[nodiscard]] inline auto less(MutPtrVector<int64_t> x) const -> bool {
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
  [[nodiscard]] inline auto less(PtrVector<int64_t> x) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    llvm::SmallVector<int64_t, 16> y{x.begin(), x.begin() + N};
    return less(LinearAlgebra::view(y));
  }
  [[nodiscard]] inline auto greater(MutPtrVector<int64_t> x) const -> bool {
    int64_t x0 = x[0]--;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    x[0] = x0;
    return ret;
  }
  [[nodiscard]] inline auto greater(PtrVector<int64_t> x) const -> bool {
    // TODO: avoid this needless memcopy and (possible) allocation?
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    llvm::SmallVector<int64_t, 8> xm{x.begin(), x.begin() + N};
    return greater(LinearAlgebra::view(xm));
  }
  inline auto greater(Vector<int64_t> &x) const -> bool {
    return greater(MutPtrVector<int64_t>(x));
  }
  inline auto less(Vector<int64_t> &x) const -> bool { return less(x.view()); }
  inline auto lessEqual(Vector<int64_t> &x) const -> bool {
    return lessEqual(x.view());
  }
  inline auto lessEqual(Vector<int64_t> &x, int64_t y) const -> bool {
    return lessEqual(x.view(), y);
  }

  [[nodiscard]] inline auto equal(PtrVector<int64_t> x) const -> bool {
    // check cheap trivial first
    return allZero(x) ||
           (static_cast<const T *>(this)->greaterEqual(x) && lessEqual(x));
  }
  [[nodiscard]] inline auto equalNegative(PtrVector<int64_t> x,
                                          PtrVector<int64_t> y) const -> bool {
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
  [[no_unique_address]] IntMatrix U;
  [[no_unique_address]] IntMatrix V;
  [[no_unique_address]] Vector<int64_t> d;
  [[no_unique_address]] size_t numVar;
  [[no_unique_address]] size_t numEquations;
  using BaseComparator<LinearSymbolicComparator>::greaterEqual;
  [[nodiscard]] auto getNumConstTermsImpl() const -> size_t { return numVar; }
  void init(PtrMatrix<int64_t> A, EmptyMatrix<int64_t> = EmptyMatrix<int64_t>{},
            bool pos0 = true) {
    const size_t numCon = size_t(A.numRow()) + pos0;
    numVar = size_t(A.numCol());
    V.resizeForOverwrite(Row{numVar + numCon}, Col{2 * numCon});
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
    const size_t numConExplicit = size_t(A.numRow()) + 1;
    const size_t numConTotal = numConExplicit + numNonNegative;
    numVar = size_t(A.numCol());
    V.resizeForOverwrite(Row{numVar + numConTotal}, Col{2 * numConTotal});
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
    const size_t numInEqConExplicit = size_t(A.numRow()) + 1;
    const size_t numInEqConTotal = numInEqConExplicit + numNonNegative;
    const size_t numEqCon = size_t(E.numRow());
    numVar = size_t(A.numCol());
    V.resizeForOverwrite(Row{numVar + numInEqConTotal},
                         Col{2 * numInEqConTotal + numEqCon});
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
    const size_t numInEqCon = size_t(A.numRow()) + pos0;
    numVar = size_t(A.numCol());
    const size_t numEqCon = size_t(E.numRow());
    V.resizeForOverwrite(Row{numVar + numInEqCon},
                         Col{2 * numInEqCon + numEqCon});
    V = 0;
    // V = [A' E' 0
    //      S  0  I]
    V(0, 0) = pos0;
    V(_(begin, numVar), _(pos0, numInEqCon)) = A.transpose();
    // A(_, _(pos0, end)).transpose();
    V(_(begin, numVar), _(numInEqCon, numInEqCon + numEqCon)) = E.transpose();

    numEquations = numInEqCon + numEqCon;
    for (size_t j = 0; j < numInEqCon; ++j) {
      V(j + numVar, j) = -1;
      V(j + numVar, j + numEquations) = 1;
    }
    initCore();
  }
  void initCore() {
    auto &A = V;
    size_t R = size_t(V.numRow());
    U.resizeForOverwrite(Row{R}, Col{R});
    U = 0;
    for (size_t i = 0; i < R; ++i)
      U(i, i) = 1;
    // We will have query of the form Ax = q;
    NormalForm::simplifySystemImpl(NormalForm::solvePair(A, U));
    auto &H = A;
    while ((R) && allZero(H(R - 1, _)))
      --R;
    H.truncate(Row{R});
    U.truncate(Row{R});
    // numRowTrunc = R;
    if (H.isSquare()) {
      d.clear();
      return;
    }
    IntMatrix Ht = H.transpose();
    auto Vt = IntMatrix::identity(size_t(Ht.numRow()));
    NormalForm::solveSystem(Ht, Vt);
    d = Ht.diag();
    V = Vt.transpose();
  }

  static auto construct(PtrMatrix<int64_t> Ap,
                        EmptyMatrix<int64_t> = EmptyMatrix<int64_t>{},
                        bool pos0 = true) -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    cmp.init(Ap, EmptyMatrix<int64_t>{}, pos0);
    return cmp;
  };
  static auto construct(PtrMatrix<int64_t> Ap, bool pos0)
    -> LinearSymbolicComparator {
    return construct(Ap, EmptyMatrix<int64_t>{}, pos0);
  };
  static auto construct(PtrMatrix<int64_t> Ap, PtrMatrix<int64_t> Ep,
                        bool pos0 = true) -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    cmp.init(Ap, Ep, pos0);
    return cmp;
  };
  // Note that this is only valid when the comparator was constructed
  // with index `0` referring to >= 0 constants (i.e., the default).
  auto isEmpty() -> bool {
    StridedVector<int64_t> b{StridedVector<int64_t>(U(_, 0))};
    if (d.size() == 0) {
      for (size_t i = size_t(V.numRow()); i < b.size(); ++i)
        if (b(i))
          return false;
      auto H = V;
      Col oldn = H.numCol();
      H.resize(oldn + 1);
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
      size_t numSlack = size_t(V.numRow()) - numEquations;
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
      size_t numRowTrunc = size_t(U.numRow());
      Vector<int64_t> c = V(_(numEquations, end), _(begin, numRowTrunc)) * b2;
      auto NSdim = V.numCol() - numRowTrunc;
      // expand W stores [c -JV2 JV2]
      //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
      // where y2 = y2+ - y2-
      IntMatrix expandW(Row{numSlack}, NSdim * 2 + 1);
      for (size_t i = 0; i < numSlack; ++i) {
        expandW(i, 0) = c(i);
        // expandW(i, 0) *= Dlcm;
        for (size_t j = 0; j < NSdim; ++j) {
          auto val = V(i + numEquations, numRowTrunc + j) * Dlcm;
          expandW(i, j + 1) = -val;
          expandW(i, NSdim + 1 + j) = val;
        }
      }
      IntMatrix Wcouple{Row{0}, Col{expandW.numCol()}};
      std::optional<Simplex> optS{Simplex::positiveVariables(expandW, Wcouple)};
      return optS.has_value();
    }
    return true;
  }
  [[nodiscard]] auto greaterEqual(PtrVector<int64_t> query) const -> bool {
    Vector<int64_t> b = U(_, _(begin, query.size())) * query;
    // Full column rank case
    if (d.size() == 0) {
      for (size_t i = size_t(V.numRow()); i < b.size(); ++i)
        if (b(i))
          return false;
      auto H = V;
      auto oldn = H.numCol();
      H.resize(oldn + 1);
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
      size_t numSlack = size_t(V.numRow()) - numEquations;
      Vector<int64_t> dinv = d; // copy
      auto Dlcm = dinv[0];
      // We represent D martix as a vector, and multiply the lcm to the
      // linear equation to avoid store D^(-1) as rational type
      for (size_t i = 1; i < dinv.size(); ++i)
        Dlcm = lcm(Dlcm, dinv(i));
      for (size_t i = 0; i < dinv.size(); ++i)
        dinv(i) = Dlcm / dinv(i);
      b *= dinv;
      size_t numRowTrunc = size_t(U.numRow());
      Vector<int64_t> c = V(_(numEquations, end), _(begin, numRowTrunc)) * b;
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
          expandW(i, NSdim + 1 + j) = val;
        }
      }
      IntMatrix Wcouple{0, expandW.numCol()};
      std::optional<Simplex> optS{Simplex::positiveVariables(expandW, Wcouple)};
      return optS.has_value();
    }
  }
};

static_assert(Comparator<LinearSymbolicComparator>);

static constexpr void moveEqualities(IntMatrix &, EmptyMatrix<int64_t> &,
                                     const Comparator auto &) {}
static inline void moveEqualities(IntMatrix &A, IntMatrix &E,
                                  const Comparator auto &C) {
  const size_t numVar = size_t(E.numCol());
  assert(A.numCol() == numVar);
  if (A.numRow() <= 1)
    return;
  for (size_t o = size_t(A.numRow()) - 1; o > 0;) {
    for (size_t i = o--; i < A.numRow(); ++i) {
      bool isNeg = true;
      for (size_t v = 0; v < numVar; ++v) {
        if (A(i, v) != -A(o, v)) {
          isNeg = false;
          break;
        }
      }
      if (isNeg && C.equalNegative(A(i, _), A(o, _))) {
        size_t e = size_t(E.numRow());
        E.resize(e + 1, numVar);
        for (size_t v = 0; v < numVar; ++v)
          E(e, v) = A(i, v);
        eraseConstraint(A, i, o);
        break;
      }
    }
  }
}
