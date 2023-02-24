#pragma once

#include "Math/Array.hpp"
#include "Math/Constraints.hpp"
#include "Math/EmptyArrays.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/NormalForm.hpp"
#include "Math/Simplex.hpp"
#include "Math/VectorGreatestCommonDivisor.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Invariant.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/AllocatorBase.h>
#include <memory>

namespace comparator {
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
  static constexpr auto greaterEqual(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] >= y[0];
  }
  static constexpr auto greater(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] > y[0];
  }
  static constexpr auto lessEqual(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] <= y[0];
  }
  static constexpr auto less(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] < y[0];
  }
  static constexpr auto equal(PtrVector<int64_t> x, PtrVector<int64_t> y)
    -> bool {
    return x[0] == y[0];
  }
  static constexpr auto greaterEqual(PtrVector<int64_t> x) -> bool {
    return x[0] >= 0;
  }
  static constexpr auto greater(PtrVector<int64_t> x) -> bool {
    return x[0] > 0;
  }
  static constexpr auto lessEqual(PtrVector<int64_t> x) -> bool {
    return x[0] <= 0;
  }
  static constexpr auto less(PtrVector<int64_t> x) -> bool { return x[0] < 0; }
  static constexpr auto equal(PtrVector<int64_t> x) -> bool {
    return x[0] == 0;
  }
  static constexpr auto equalNegative(PtrVector<int64_t> x,
                                      PtrVector<int64_t> y) -> bool {
    // this version should return correct results for
    // `std::numeric_limits<int64_t>::min()`
    return (x[0] + y[0]) == 0;
  }
  static constexpr auto lessEqual(PtrVector<int64_t> y, int64_t x) -> bool {
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
  [[nodiscard]] constexpr auto getNumConstTerms() const -> size_t {
    return static_cast<const T *>(this)->getNumConstTermsImpl();
  }
  [[nodiscard]] constexpr auto greaterEqual(MutPtrVector<int64_t> delta,
                                            PtrVector<int64_t> x,
                                            PtrVector<int64_t> y) const
    -> bool {
    const size_t N = getNumConstTerms();
    assert(delta.size() >= N);
    assert(x.size() >= N);
    assert(y.size() >= N);
    for (size_t n = 0; n < N; ++n) delta[n] = x[n] - y[n];
    return static_cast<const T *>(this)->greaterEqual(delta);
  }
  [[nodiscard]] constexpr auto greaterEqual(PtrVector<int64_t> x,
                                            PtrVector<int64_t> y) const
    -> bool {
    Vector<int64_t> delta(getNumConstTerms());
    return greaterEqual(delta, x, y);
  }
  [[nodiscard]] constexpr auto less(PtrVector<int64_t> x,
                                    PtrVector<int64_t> y) const -> bool {
    return greater(y, x);
  }
  [[nodiscard]] constexpr auto greater(PtrVector<int64_t> x,
                                       PtrVector<int64_t> y) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    assert(N <= y.size());
    Vector<int64_t> delta(N);
    for (size_t n = 0; n < N; ++n) delta[n] = x[n] - y[n];
    --delta[0];
    return static_cast<const T *>(this)->greaterEqual(delta);
  }
  [[nodiscard]] constexpr auto lessEqual(PtrVector<int64_t> x,
                                         PtrVector<int64_t> y) const -> bool {
    return static_cast<const T *>(this)->greaterEqual(y, x);
  }
  [[nodiscard]] constexpr auto equal(PtrVector<int64_t> x,
                                     PtrVector<int64_t> y) const -> bool {
    // check cheap trivial first
    if (x == y) return true;
    Vector<int64_t> delta(getNumConstTerms());
    return (greaterEqual(delta, x, y) && greaterEqual(delta, y, x));
  }
  [[nodiscard]] constexpr auto greaterEqual(PtrVector<int64_t> x) const
    -> bool {
    return static_cast<const T *>(this)->greaterEqual(x);
  }
  [[nodiscard]] constexpr auto lessEqual(MutPtrVector<int64_t> x) const
    -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    for (size_t n = 0; n < N; ++n) x[n] *= -1;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    for (size_t n = 0; n < N; ++n) x[n] *= -1;
    return ret;
  }
  [[nodiscard]] constexpr auto lessEqual(PtrVector<int64_t> x) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> y{x[_(0, N)]};
    return lessEqual(LinearAlgebra::view(y));
  }
  [[nodiscard]] constexpr auto lessEqual(MutPtrVector<int64_t> x,
                                         int64_t y) const -> bool {
    int64_t x0 = x[0];
    x[0] = x0 - y;
    bool ret = lessEqual(x);
    x[0] = x0;
    return ret;
  }
  [[nodiscard]] constexpr auto lessEqual(PtrVector<int64_t> x, int64_t y) const
    -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> z{x[_(0, N)]};
    return lessEqual(z, y);
  }
  [[nodiscard]] constexpr auto less(MutPtrVector<int64_t> x) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    int64_t x0 = x[0];
    x[0] = -x0 - 1;
    for (size_t i = 1; i < N; ++i) x[i] *= -1;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    x[0] = x0;
    for (size_t i = 1; i < N; ++i) x[i] *= -1;
    return ret;
  }
  [[nodiscard]] constexpr auto less(PtrVector<int64_t> x) const -> bool {
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> y{x[_(0, N)]};
    return less(LinearAlgebra::view(y));
  }
  [[nodiscard]] constexpr auto greater(MutPtrVector<int64_t> x) const -> bool {
    int64_t x0 = x[0]--;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    x[0] = x0;
    return ret;
  }
  [[nodiscard]] constexpr auto greater(PtrVector<int64_t> x) const -> bool {
    // TODO: avoid this needless memcopy and (possible) allocation?
    const size_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> xm{x[_(0, N)]};
    return greater(LinearAlgebra::view(xm));
  }
  constexpr auto greater(Vector<int64_t> &x) const -> bool {
    return greater(MutPtrVector<int64_t>(x));
  }
  constexpr auto less(Vector<int64_t> &x) const -> bool {
    return less(x.view());
  }
  constexpr auto lessEqual(Vector<int64_t> &x) const -> bool {
    return lessEqual(x.view());
  }
  constexpr auto lessEqual(Vector<int64_t> &x, int64_t y) const -> bool {
    return lessEqual(x.view(), y);
  }

  [[nodiscard]] constexpr auto equal(PtrVector<int64_t> x) const -> bool {
    // check cheap trivial first
    return allZero(x) ||
           (static_cast<const T *>(this)->greaterEqual(x) && lessEqual(x));
  }
  [[nodiscard]] constexpr auto equalNegative(PtrVector<int64_t> x,
                                             PtrVector<int64_t> y) const
    -> bool {
    const size_t N = getNumConstTerms();
    assert(x.size() >= N);
    assert(y.size() >= N);
    if (x[_(0, N)] == y[_(0, N)]) return true;
    Vector<int64_t> delta{x[_(0, N)] - y[_(0, N)]};
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

template <typename T>
struct BaseSymbolicComparator : BaseComparator<BaseSymbolicComparator<T>> {
  [[no_unique_address]] unsigned int numVar{0};
  [[no_unique_address]] unsigned int numEquations{0};
  using ThisT = BaseSymbolicComparator<T>;
  using BaseT = BaseComparator<ThisT>;
  using BaseT::greaterEqual;
  [[nodiscard]] constexpr auto getNumConstTermsImpl() const -> size_t {
    return numVar;
  }

  constexpr auto getV() -> MutDensePtrMatrix<int64_t> {
    return static_cast<T *>(this)->getVImpl();
  }
  constexpr auto getU() -> MutDensePtrMatrix<int64_t> {
    return static_cast<T *>(this)->getUImpl();
  }
  constexpr auto getD() -> MutPtrVector<int64_t> {
    return static_cast<T *>(this)->getDImpl();
  }
  constexpr auto getV(Row r, Col c) -> MutDensePtrMatrix<int64_t> {
    return static_cast<T *>(this)->getVImpl(r, c);
  }
  constexpr auto getU(Row r, Col c) -> MutDensePtrMatrix<int64_t> {
    return static_cast<T *>(this)->getUImpl(r, c);
  }
  constexpr auto getD(Row n) -> MutPtrVector<int64_t> {
    return static_cast<T *>(this)->getDImpl(n);
  }
  constexpr void setURank(Row r) { static_cast<T *>(this)->setURankImpl(r); }

  template <typename Allocator>
  constexpr void initNonNegative(Allocator alloc, PtrMatrix<int64_t> A,
                                 EmptyMatrix<int64_t>, size_t numNonNegative) {
    initNonNegative(alloc, A, numNonNegative);
  }
  template <typename Allocator>
  constexpr void initNonNegative(Allocator alloc, PtrMatrix<int64_t> A,
                                 size_t numNonNegative) {
    // we have an additional numNonNegative x numNonNegative identity matrix
    // as the lower right block of `A`.
    const size_t numConExplicit = size_t(A.numRow()) + 1;
    const size_t numConTotal = numConExplicit + numNonNegative;
    numVar = size_t(A.numCol());
    Row rowV = Row{numVar + numConTotal};
    Col colV = Col{2 * numConTotal};
    auto &&B = getV(rowV, colV);
    B(0, 0) = 1;
    // B = [ A_0 A_1
    //        0   I  ]
    // V = [B' 0
    //      S   I]
    // V = [A_0'  0  0
    //      A_1'  I  0
    //      S_0  S_1 I]
    B(_(begin, numVar), _(1, numConExplicit)) << A.transpose();
    for (size_t j = 0; j < numNonNegative; ++j)
      B(j + numVar - numNonNegative, numConExplicit + j) = 1;
    for (size_t j = 0; j < numConTotal; ++j) {
      B(j + numVar, j) = -1;
      B(j + numVar, j + numConTotal) = 1;
    }
    numEquations = numConTotal;
    initCore(alloc);
  }
  template <typename Allocator>
  constexpr void initNonNegative(Allocator alloc, PtrMatrix<int64_t> A,
                                 PtrMatrix<int64_t> E, size_t numNonNegative) {
    // we have an additional numNonNegative x numNonNegative identity matrix
    // as the lower right block of `A`.
    const size_t numInEqConExplicit = size_t(A.numRow()) + 1;
    const size_t numInEqConTotal = numInEqConExplicit + numNonNegative;
    const size_t numEqCon = size_t(E.numRow());
    numVar = size_t(A.numCol());
    Row rowV = Row{numVar + numInEqConTotal};
    Col colV = Col{2 * numInEqConTotal + numEqCon};
    auto &&B = getV(rowV, colV);
    B(0, 0) = 1;
    // B is `A` augmented with the implicit non-negative constraints
    // B = [ A_0 A_1
    //        0   I  ]
    // V = [B' E' 0
    //      S  0  I]
    // V = [A_0'  0  E_0' 0
    //      A_1'  I  E_1' 0
    //      S_0  S_1  0   I]
    numEquations = numInEqConTotal + numEqCon;
    B(_(begin, numVar), _(1, numInEqConExplicit)) << A.transpose();
    B(_(begin, numVar), _(numInEqConTotal, numInEqConTotal + numEqCon))
      << E.transpose();
    if (numNonNegative)
      B(_(numVar - numNonNegative, numVar),
        _(numInEqConExplicit, numInEqConExplicit + numNonNegative))
          .diag()
        << 1;
    for (size_t j = 0; j < numInEqConTotal; ++j) {
      B(j + numVar, j) = -1;
      B(j + numVar, j + numEquations) = 1;
    }
    initCore(alloc);
  }
  [[nodiscard]] static constexpr auto
  memoryNeededNonNegative(PtrMatrix<int64_t> A, EmptyMatrix<int64_t>,
                          size_t numNonNegative) -> size_t {
    return memoryNeededNonNegative(A, numNonNegative);
  }
  [[nodiscard]] static constexpr auto
  memoryNeededNonNegative(PtrMatrix<int64_t> A, size_t numNonNegative)
    -> size_t {
    size_t numConTotal = size_t(A.numRow()) + 1 + numNonNegative;
    size_t rowV = size_t(A.numCol()) + numConTotal;
    return (2 * numConTotal + rowV + 1) * rowV;
  }
  [[nodiscard]] static constexpr auto
  memoryNeededNonNegative(PtrMatrix<int64_t> A, PtrMatrix<int64_t> E,
                          size_t numNonNegative) -> size_t {
    size_t numInEqConTotal = size_t(A.numRow()) + 1 + numNonNegative;
    size_t rowV = size_t(A.numCol()) + numInEqConTotal;
    return (2 * numInEqConTotal + size_t(E.numRow()) + rowV + 1) * rowV;
  }
  template <class Allocator>
  constexpr void init(Allocator alloc, PtrMatrix<int64_t> A, bool pos0) {
    const size_t numCon = size_t(A.numRow()) + pos0;
    numVar = size_t(A.numCol());
    Row rowV = numVar + numCon;
    Col colV = 2 * numCon;
    auto &&B = getV(rowV, colV);
    B(0, 0) = pos0;
    // V = [A' 0
    //      S  I]
    B(_(begin, numVar), _(pos0, numCon)) << A.transpose();
    for (size_t j = 0; j < numCon; ++j) {
      B(j + numVar, j) = -1;
      B(j + numVar, j + numCon) = 1;
    }
    numEquations = numCon;
    initCore(alloc);
  }
  template <class Allocator>
  constexpr void init(Allocator alloc, PtrMatrix<int64_t> A,
                      EmptyMatrix<int64_t>, bool pos0) {
    init(alloc, A, pos0);
  }
  [[nodiscard]] static constexpr auto memoryNeeded(PtrMatrix<int64_t> A,
                                                   EmptyMatrix<int64_t>,
                                                   bool pos0) -> size_t {
    return memoryNeeded(A, pos0);
  }
  [[nodiscard]] static constexpr auto memoryNeeded(PtrMatrix<int64_t> A,
                                                   bool pos0) -> size_t {
    size_t numInEqCon = size_t(A.numRow()) + pos0;
    size_t rowV = size_t(A.numCol()) + numInEqCon;
    return (2 * numInEqCon + rowV + 1) * rowV;
  }
  [[nodiscard]] static constexpr auto memoryNeeded(PtrMatrix<int64_t> A,
                                                   PtrMatrix<int64_t> E,
                                                   bool pos0) -> size_t {
    size_t numInEqCon = size_t(A.numRow()) + pos0;
    size_t rowV = size_t(A.numCol()) + numInEqCon;
    return (2 * numInEqCon + size_t(E.numRow()) + rowV + 1) * rowV;
  }
  template <typename Allocator>
  constexpr void init(Allocator alloc, PtrMatrix<int64_t> A,
                      PtrMatrix<int64_t> E, bool pos0) {
    const size_t numInEqCon = size_t(A.numRow()) + pos0;
    numVar = size_t(A.numCol());
    const size_t numEqCon = size_t(E.numRow());
    Row rowV = Row{numVar + numInEqCon};
    Col colV = Col{2 * numInEqCon + numEqCon};
    auto &&B = getV(rowV, colV);
    // V = [A' E' 0
    //      S  0  I]
    B(0, 0) = pos0;
    B(_(begin, numVar), _(pos0, numInEqCon)) << A.transpose();
    // A(_, _(pos0, end)).transpose();
    B(_(begin, numVar), _(numInEqCon, numInEqCon + numEqCon)) << E.transpose();

    numEquations = numInEqCon + numEqCon;
    for (size_t j = 0; j < numInEqCon; ++j) {
      B(j + numVar, j) = -1;
      B(j + numVar, j + numEquations) = 1;
    }
    initCore(alloc);
  }
  // sets U, V, and d.
  // needs to also set their size, which is only determined here.
  template <typename Allocator> constexpr void initCore(Allocator alloc) {
    // numVar + numInEq x 2*numInEq + numEq
    MutPtrMatrix<int64_t> B = getV();
    Row R = B.numRow();
    MutPtrMatrix<int64_t> U = getU(); // numVar + numInEq x numVar + numInEq
    U.diag() << 1;
    // We will have query of the form Ax = q;
    NormalForm::simplifySystemsImpl({B, U});
    while ((R) && allZero(B(R - 1, _))) --R;
    setURank(R);
    size_t numColB = size_t(B.numCol());
    // upper bounded by numVar + numInEq x numVar + numInEq
    // if V is square, it is full rank and there is 1 solution
    // if V has fewer rows, there are infinitely many solutions
    if (R == numColB) return;
    // H (aliasing V and A) copied
    // R = B.numRow() < B.numCol()
    auto Vt{identity(alloc, numColB)};
    // Ht.numRow() > Ht.numCol() = R
    // (2*numInEq + numEq) x R
    auto Ht = matrix(alloc, numColB, size_t(R));
    Ht << B(_(0, R), _).transpose();
    NormalForm::solveSystem(Ht, Vt);
    // upper bounded by numVar + numInEq
    // rows/cols, but of rank R
    // smaller based on rank
    getD(R) << Ht.diag(); // d.size() == R
    // upper bounded by 2*numInEq + numEq x 2*numInEq + numEq
    // fewer cols based on rank?
    getV() << Vt.transpose();
  }

  // Note that this is only valid when the comparator was constructed
  // with index `0` referring to >= 0 constants (i.e., the default).
  constexpr auto isEmpty() -> bool {
    auto &&V = getV();
    auto &&U = getU();
    auto &&d = getD();
    StridedVector<int64_t> b{U(_, 0)};
    if (d.size() == 0) {
      if (!allZero(b[_(V.numRow(), end)])) return false;
      Col oldn = V.numCol();
      IntMatrix H{V.numRow(), oldn + 1};
      H(_, _(0, oldn)) = V;
      H(_, oldn) << -b;
      NormalForm::solveSystem(H);
      for (size_t i = numEquations; i < H.numRow(); ++i)
        if (auto rhs = H(i, oldn))
          if ((rhs > 0) != (H(i, i) > 0)) return false;
      return true;
    }
    // Column rank deficient case
    else {
      Row numSlack = V.numRow() - numEquations;
      // Vector<int64_t> dinv = d; // copy
      // We represent D martix as a vector, and multiply the lcm to the
      // linear equation to avoid store D^(-1) as rational type
      int64_t Dlcm = lcm(d);
      Vector<int64_t> b2 = -b * Dlcm / d;
      size_t numRowTrunc = size_t(U.numRow());
      Vector<int64_t> c = V(_(numEquations, end), _(begin, numRowTrunc)) * b2;
      auto NSdim = V.numCol() - numRowTrunc;
      // expand W stores [c -JV2 JV2]
      //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
      // where y2 = y2+ - y2-
      IntMatrix expandW(numSlack, NSdim * 2 + 1);
      for (size_t i = 0; i < numSlack; ++i) {
        expandW(i, 0) = c[i];
        // expandW(i, 0) *= Dlcm;
        for (size_t j = 0; j < NSdim; ++j) {
          auto val = V(i + numEquations, numRowTrunc + j) * Dlcm;
          expandW(i, j + 1) = -val;
          expandW(i, NSdim + 1 + j) = val;
        }
      }
      std::optional<Simplex> optS{Simplex::positiveVariables(expandW)};
      return optS.has_value();
    }
    return true;
  }
  [[nodiscard]] constexpr auto greaterEqual(PtrVector<int64_t> query) const
    -> bool {
    auto &&V = getV();
    auto &&U = getU();
    auto &&d = getD();
    Vector<int64_t> b = U(_, _(begin, query.size())) * query;
    // Full column rank case
    if (d.size() == 0) {
      if (!allZero(b[_(V.numRow(), end)])) return false;
      auto H = V;
      Col oldn = H.numCol();
      H.resize(oldn + 1);
      for (size_t i = 0; i < H.numRow(); ++i) H(i, oldn) = b[i];
      NormalForm::solveSystem(H);
      for (size_t i = numEquations; i < H.numRow(); ++i)
        if (auto rhs = H(i, oldn))
          if ((rhs > 0) != (H(i, i) > 0)) return false;
      return true;
    } else {
      // Column rank deficient case
      Row numSlack = V.numRow() - numEquations;
      Vector<int64_t> dinv = d; // copy
      // We represent D martix as a vector, and multiply the lcm to the
      // linear equation to avoid store D^(-1) as rational type
      int64_t Dlcm = lcm(dinv);
      for (size_t i = 0; i < dinv.size(); ++i) {
        auto x = Dlcm / dinv[i];
        dinv[i] = x;
        b[i] *= x;
      }
      size_t numRowTrunc = size_t(U.numRow());
      Vector<int64_t> c = V(_(numEquations, end), _(begin, numRowTrunc)) * b;
      auto NSdim = V.numCol() - numRowTrunc;
      // expand W stores [c -JV2 JV2]
      //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
      // where y2 = y2+ - y2-
      IntMatrix expandW(numSlack, NSdim * 2 + 1);
      for (size_t i = 0; i < numSlack; ++i) {
        expandW(i, 0) = c[i];
        // expandW(i, 0) *= Dlcm;
        for (size_t j = 0; j < NSdim; ++j) {
          auto val = V(i + numEquations, numRowTrunc + j) * Dlcm;
          expandW(i, j + 1) = -val;
          expandW(i, NSdim + 1 + j) = val;
        }
      }
      std::optional<Simplex> optS{Simplex::positiveVariables(expandW)};
      return optS.has_value();
    }
  }
};
struct LinearSymbolicComparator
  : public BaseSymbolicComparator<LinearSymbolicComparator> {
  using Base = BaseSymbolicComparator<LinearSymbolicComparator>;
  using Base::init;
  using Matrix = LinearAlgebra::ManagedArray<int64_t, DenseDims>;
  [[no_unique_address]] Matrix U;
  [[no_unique_address]] Matrix V;
  [[no_unique_address]] Vector<int64_t> d;
  constexpr auto getUImpl() -> Matrix & { return U; }
  constexpr auto getVImpl() -> Matrix & { return V; }
  constexpr auto getDImpl() -> Vector<int64_t> & { return d; }

  constexpr void setURankImpl(Row r) {
    V.truncate(r);
    U.truncate(r);
  }
  // void setURankImpl(Row r) {
  //   U.truncate(r);
  // }
  // void setUColImpl(Col c) { colU = unsigned(c); }
  // void setVDimImpl(size_t x) { dimV = unsigned(x); }
  // void setDDimImpl(size_t x) { dimD = unsigned(x); }

  constexpr auto getUImpl(Row r, Col c) -> MutDensePtrMatrix<int64_t> {
    U.resizeForOverwrite(r, c);
    return U;
  }
  constexpr auto getVImpl(Row r, Col c) -> MutDensePtrMatrix<int64_t> {
    V.setSize(r, c);
    U.setSize(r, Col{size_t(r)});
    return V;
  }
  constexpr auto getDImpl(Row N) -> MutPtrVector<int64_t> {
    d.resizeForOverwrite(size_t(N));
    return d;
  }
  static constexpr auto construct(PtrMatrix<int64_t> Ap, EmptyMatrix<int64_t>,
                                  bool pos0) -> LinearSymbolicComparator {
    return construct(Ap, pos0);
  };
  static constexpr auto construct(PtrMatrix<int64_t> Ap, bool pos0)
    -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    cmp.init(std::allocator<int64_t>{}, Ap, pos0);
    return cmp;
  };
  static constexpr auto construct(PtrMatrix<int64_t> Ap, PtrMatrix<int64_t> Ep,
                                  bool pos0) -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    cmp.init(std::allocator<int64_t>{}, Ap, Ep, pos0);
    return cmp;
  };
  static constexpr auto constructNonNeg(PtrMatrix<int64_t> Ap,
                                        EmptyMatrix<int64_t>, size_t numNonNeg)
    -> LinearSymbolicComparator {
    return constructNonNeg(Ap, numNonNeg);
  };
  static constexpr auto constructNonNeg(PtrMatrix<int64_t> Ap, size_t numNonNeg)
    -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    cmp.initNonNegative(std::allocator<int64_t>{}, Ap, numNonNeg);
    return cmp;
  };
  static constexpr auto constructNonNeg(PtrMatrix<int64_t> Ap,
                                        PtrMatrix<int64_t> Ep, size_t numNonNeg)
    -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    cmp.initNonNegative(std::allocator<int64_t>{}, Ap, Ep, numNonNeg);
    return cmp;
  };
};
struct PtrSymbolicComparator
  : public BaseSymbolicComparator<PtrSymbolicComparator> {
  using Base = BaseSymbolicComparator<PtrSymbolicComparator>;
  using Base::init;
  int64_t *mem;
  // unsigned int numVar;
  // unsigned int numInEq;
  // unsigned int numEq;
  unsigned int rankU{0};
  unsigned int colU{0};
  unsigned int dimV{0};
  unsigned int dimD{0};

  constexpr void setURankImpl(Row r) { rankU = unsigned(r); }
  // void setUColImpl(Col c) { colU = unsigned(c); }
  // void setVDimImpl(size_t d) { dimV = unsigned(d); }
  // void setDDimImpl(size_t d) { dimD = int(d); }

  // R x numVar + numInEq
  // [[nodiscard]] constexpr auto colU() const -> unsigned {
  //   return numVar + numInEq;
  // }
  // [[nodiscard]] constexpr auto dimV() const -> unsigned {
  //   return 2 * numInEq + numEq;
  // }
  constexpr auto getUImpl() -> MutDensePtrMatrix<int64_t> {
    return {mem, DenseDims{rankU, colU}};
  }
  // offset by (numVar + numInEq)*(numVar + numInEq)
  constexpr auto getVImpl() -> MutDensePtrMatrix<int64_t> {
    // A = V
    // H = A
    // H.truncate(Row());
    // size is H.numCol() * H.numCol()
    return {getUImpl().end(), DenseDims{dimD ? dimV : rankU, dimV}};
  }
  // size D
  constexpr auto getDImpl() -> MutPtrVector<int64_t> {
    // d = Ht.diag()
    return {getVImpl().end(), dimD};
  }
  // constexpr auto getUImpl(Row r, Col c) -> MutPtrMatrix<int64_t> {}
  constexpr auto getVImpl(Row r, Col c) -> MutDensePtrMatrix<int64_t> {
    rankU = unsigned(r);
    colU = rankU;
    dimV = unsigned(c);
    getUImpl() << 0;
    dimD = 0;
    return getVImpl();
  }
  constexpr auto getDImpl(Row r) -> MutPtrVector<int64_t> {
    dimD = unsigned(r);
    invariant(dimD > 0);
    return getDImpl();
  }
  static constexpr auto construct(WBumpAlloc<int64_t> alloc,
                                  PtrMatrix<int64_t> Ap, EmptyMatrix<int64_t>,
                                  bool pos0) -> PtrSymbolicComparator {
    return construct(alloc, Ap, pos0);
  };
  static constexpr auto construct(WBumpAlloc<int64_t> alloc,
                                  PtrMatrix<int64_t> Ap, bool pos0)
    -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(alloc.allocate(memoryNeeded(Ap, pos0)));
    cmp.init(WBumpAlloc<int64_t>(alloc), Ap, pos0);
    return cmp;
  };
  static constexpr auto construct(WBumpAlloc<int64_t> alloc,
                                  PtrMatrix<int64_t> Ap, PtrMatrix<int64_t> Ep,
                                  bool pos0) -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(alloc.allocate(memoryNeeded(Ap, Ep, pos0)));
    cmp.init(alloc, Ap, Ep, pos0);
    return cmp;
  };
  static constexpr auto constructNonNeg(WBumpAlloc<int64_t> alloc,
                                        PtrMatrix<int64_t> Ap,
                                        EmptyMatrix<int64_t>, size_t numNonNeg)
    -> PtrSymbolicComparator {
    return constructNonNeg(alloc, Ap, numNonNeg);
  };
  static constexpr auto constructNonNeg(WBumpAlloc<int64_t> alloc,
                                        PtrMatrix<int64_t> Ap, size_t numNonNeg)
    -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(
      alloc.allocate(memoryNeededNonNegative(Ap, numNonNeg)));
    cmp.initNonNegative(alloc, Ap, numNonNeg);
    return cmp;
  };
  static constexpr auto constructNonNeg(WBumpAlloc<int64_t> alloc,
                                        PtrMatrix<int64_t> Ap,
                                        PtrMatrix<int64_t> Ep, size_t numNonNeg)
    -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(
      alloc.allocate(memoryNeededNonNegative(Ap, Ep, numNonNeg)));
    cmp.initNonNegative(alloc, Ap, Ep, numNonNeg);
    return cmp;
  };

private:
  constexpr PtrSymbolicComparator(int64_t *p) : mem(p) {}
};

static_assert(Comparator<PtrSymbolicComparator>);
static_assert(Comparator<LinearSymbolicComparator>);

constexpr void moveEqualities(IntMatrix &, EmptyMatrix<int64_t>,
                              const Comparator auto &) {}
constexpr void moveEqualities(IntMatrix &A, IntMatrix &E,
                              const Comparator auto &C) {
  const size_t numVar = size_t(E.numCol());
  assert(A.numCol() == numVar);
  if (A.numRow() <= 1) return;
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
        for (size_t v = 0; v < numVar; ++v) E(e, v) = A(i, v);
        eraseConstraint(A, i, o);
        break;
      }
    }
  }
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linear(std::allocator<int64_t>, PtrMatrix<int64_t> A,
                      EmptyMatrix<int64_t>, bool pos0) {
  return LinearSymbolicComparator::construct(A, pos0);
}
constexpr auto linear(WBumpAlloc<int64_t> alloc, PtrMatrix<int64_t> A,
                      EmptyMatrix<int64_t>, bool pos0) {
  return PtrSymbolicComparator::construct(alloc, A, pos0);
}
// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linear(std::allocator<int64_t>, PtrMatrix<int64_t> A,
                      PtrMatrix<int64_t> E, bool pos0) {
  return LinearSymbolicComparator::construct(A, E, pos0);
}
constexpr auto linear(WBumpAlloc<int64_t> alloc, PtrMatrix<int64_t> A,
                      PtrMatrix<int64_t> E, bool pos0) {
  return PtrSymbolicComparator::construct(alloc, A, E, pos0);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linearNonNegative(std::allocator<int64_t>, PtrMatrix<int64_t> A,
                                 EmptyMatrix<int64_t>, size_t numNonNeg) {
  return LinearSymbolicComparator::constructNonNeg(A, numNonNeg);
}
constexpr auto linearNonNegative(WBumpAlloc<int64_t> alloc,
                                 PtrMatrix<int64_t> A, EmptyMatrix<int64_t>,
                                 size_t numNonNeg) {
  return PtrSymbolicComparator::constructNonNeg(alloc, A, numNonNeg);
}
// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linearNonNegative(std::allocator<int64_t>, PtrMatrix<int64_t> A,
                                 PtrMatrix<int64_t> E, size_t numNonNeg) {
  return LinearSymbolicComparator::constructNonNeg(A, E, numNonNeg);
}
constexpr auto linearNonNegative(WBumpAlloc<int64_t> alloc,
                                 PtrMatrix<int64_t> A, PtrMatrix<int64_t> E,
                                 size_t numNonNeg) {
  return PtrSymbolicComparator::constructNonNeg(alloc, A, E, numNonNeg);
}

} // namespace comparator
