#pragma once

#include "Utilities/Optional.hpp"
#include <Alloc/Arena.hpp>
#include <Alloc/Mallocator.hpp>
#include <Math/Array.hpp>
#include <Math/Constraints.hpp>
#include <Math/EmptyArrays.hpp>
#include <Math/Math.hpp>
#include <Math/MatrixDimensions.hpp>
#include <Math/NormalForm.hpp>
#include <Math/Simplex.hpp>
#include <Math/VectorGreatestCommonDivisor.hpp>
#include <Utilities/Invariant.hpp>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace poly::comparator {
using math::PtrVector, math::MutPtrVector, math::Vector, math::_, math::Row,
  math::Col, math::DensePtrMatrix, math::MutDensePtrMatrix, math::PtrMatrix,
  math::MutPtrMatrix, math::EmptyMatrix, math::begin, math::end,
  math::NormalForm::simplifySystemsImpl, math::NormalForm::solveSystem,
  math::StridedVector, math::vector, math::matrix, math::identity,
  math::Simplex, math::DenseDims, math::DenseMatrix;
using utils::invariant, alloc::Arena, utils::Optional;
// For `== 0` constraints
struct EmptyComparator {
  static constexpr auto getNumConstTerms() -> ptrdiff_t { return 0; }
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
  static constexpr auto getNumConstTerms() -> ptrdiff_t { return 1; }
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
  [[nodiscard]] constexpr auto getNumConstTerms() const -> ptrdiff_t {
    return static_cast<const T *>(this)->getNumConstTermsImpl();
  }
  [[nodiscard]] constexpr auto greaterEqual(MutPtrVector<int64_t> delta,
                                            PtrVector<int64_t> x,
                                            PtrVector<int64_t> y) const
    -> bool {
    const ptrdiff_t N = getNumConstTerms();
    assert(delta.size() >= N);
    assert(x.size() >= N);
    assert(y.size() >= N);
    for (ptrdiff_t n = 0; n < N; ++n) delta[n] = x[n] - y[n];
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
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    assert(N <= y.size());
    Vector<int64_t> delta(N);
    for (ptrdiff_t n = 0; n < N; ++n) delta[n] = x[n] - y[n];
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
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    for (ptrdiff_t n = 0; n < N; ++n) x[n] *= -1;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    for (ptrdiff_t n = 0; n < N; ++n) x[n] *= -1;
    return ret;
  }
  [[nodiscard]] constexpr auto lessEqual(PtrVector<int64_t> x) const -> bool {
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> y{x[_(0, N)]};
    return lessEqual(y);
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
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> z{x[_(0, N)]};
    return lessEqual(z, y);
  }
  [[nodiscard]] constexpr auto less(MutPtrVector<int64_t> x) const -> bool {
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    int64_t x0 = x[0];
    x[0] = -x0 - 1;
    for (ptrdiff_t i = 1; i < N; ++i) x[i] *= -1;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    x[0] = x0;
    for (ptrdiff_t i = 1; i < N; ++i) x[i] *= -1;
    return ret;
  }
  [[nodiscard]] constexpr auto less(PtrVector<int64_t> x) const -> bool {
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> y{x[_(0, N)]};
    return less(y);
  }
  [[nodiscard]] constexpr auto greater(MutPtrVector<int64_t> x) const -> bool {
    int64_t x0 = x[0]--;
    bool ret = static_cast<const T *>(this)->greaterEqual(x);
    x[0] = x0;
    return ret;
  }
  [[nodiscard]] constexpr auto greater(PtrVector<int64_t> x) const -> bool {
    // TODO: avoid this needless memcopy and (possible) allocation?
    const ptrdiff_t N = getNumConstTerms();
    assert(N <= x.size());
    Vector<int64_t> xm{x[_(0, N)]};
    return greater(math::view(xm));
  }

  [[nodiscard]] constexpr auto equal(PtrVector<int64_t> x) const -> bool {
    // check cheap trivial first
    return allZero(x) ||
           (static_cast<const T *>(this)->greaterEqual(x) && lessEqual(x));
  }
  [[nodiscard]] constexpr auto equalNegative(PtrVector<int64_t> x,
                                             PtrVector<int64_t> y) const
    -> bool {
    const ptrdiff_t N = getNumConstTerms();
    assert(x.size() >= N);
    assert(y.size() >= N);
    if (x[_(0, N)] == y[_(0, N)]) return true;
    Vector<int64_t> delta{x[_(0, N)] - y[_(0, N)]};
    return equal(delta);
  }
};

template <typename T>
concept Comparator = requires(T t, PtrVector<int64_t> x, int64_t y) {
  { t.getNumConstTerms() } -> std::convertible_to<ptrdiff_t>;
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
  [[no_unique_address]] ptrdiff_t numVar{0};
  [[no_unique_address]] ptrdiff_t numEquations{0};
  using ThisT = BaseSymbolicComparator<T>;
  using BaseT = BaseComparator<ThisT>;
  using BaseT::greaterEqual;
  [[nodiscard]] constexpr auto getNumConstTermsImpl() const -> ptrdiff_t {
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
  [[nodiscard]] constexpr auto getV() const {
    return static_cast<const T *>(this)->getVImpl();
  }
  [[nodiscard]] constexpr auto getU() const {
    return static_cast<const T *>(this)->getUImpl();
  }
  [[nodiscard]] constexpr auto getD() const -> PtrVector<int64_t> {
    return static_cast<const T *>(this)->getDImpl();
  }
  constexpr auto getV(Row<> r, Col<> c) -> MutDensePtrMatrix<int64_t> {
    return static_cast<T *>(this)->getVImpl(r, c);
  }
  constexpr auto getU(Row<> r, Col<> c) -> MutDensePtrMatrix<int64_t> {
    return static_cast<T *>(this)->getUImpl(r, c);
  }
  constexpr auto getD(Row<> n) -> MutPtrVector<int64_t> {
    return static_cast<T *>(this)->getDImpl(n);
  }
  constexpr void setURank(Row<> r) { static_cast<T *>(this)->setURankImpl(r); }
  [[nodiscard]] constexpr auto getURank() const -> ptrdiff_t {
    return static_cast<const T *>(this)->getURankImpl();
  }

  constexpr void initNonNegative(math::Alloc<int64_t> auto alloc,
                                 PtrMatrix<int64_t> A, EmptyMatrix<int64_t>,
                                 ptrdiff_t numNonNegative) {
    initNonNegative(alloc, A, numNonNegative);
  }
  constexpr void initNonNegative(math::Alloc<int64_t> auto alloc,
                                 PtrMatrix<int64_t> A,
                                 ptrdiff_t numNonNegative) {
    // we have an additional numNonNegative x numNonNegative identity matrix
    // as the lower right block of `A`.
    // numConExplicit has +1 to indicate positive.
    // I.e., first variable (probably const offsets) is positive.
    const ptrdiff_t numConExplicit = ptrdiff_t(A.numRow()) + 1;
    const ptrdiff_t numConTotal = numConExplicit + numNonNegative;
    numVar = ptrdiff_t(A.numCol());
    Row rowV = Row<>{numVar + numConTotal};
    Col colV = Col<>{2 * numConTotal};
    /// B.size() == (A.numCol() + A.numRow() + 1 + numNonNegative) x
    ///             (2 * (A.numRow() + 1 + numNonNegative))
    ///
    auto B = getV(rowV, colV);
    std::fill_n(B.begin(), ptrdiff_t(B.numRow()) * ptrdiff_t(B.numCol()), 0);
    B[0, 0] = 1;
    // B = [ A_0 A_1
    //        0   I  ]
    // V = [B' 0
    //      S   I]
    // V = [A_0'  0  0
    //      A_1'  I  0
    //      S_0  S_1 I]
    B[_(begin, numVar), _(1, numConExplicit)] << A.t();
    for (ptrdiff_t j = 0; j < numNonNegative; ++j)
      B[j + numVar - numNonNegative, numConExplicit + j] = 1;
    for (ptrdiff_t j = 0; j < numConTotal; ++j) {
      B[j + numVar, j] = -1;
      B[j + numVar, j + numConTotal] = 1;
    }
    numEquations = numConTotal;
    initCore(alloc);
  }
  constexpr void initNonNegative(math::Alloc<int64_t> auto alloc,
                                 PtrMatrix<int64_t> A, PtrMatrix<int64_t> E,
                                 ptrdiff_t numNonNegative) {
    // we have an additional numNonNegative x numNonNegative identity matrix
    // as the lower right block of `A`.
    const ptrdiff_t numInEqConExplicit = ptrdiff_t(A.numRow()) + 1;
    const ptrdiff_t numInEqConTotal = numInEqConExplicit + numNonNegative;
    const ptrdiff_t numEqCon = ptrdiff_t(E.numRow());
    numVar = ptrdiff_t(A.numCol());
    Row rowV = Row<>{numVar + numInEqConTotal};
    Col colV = Col<>{2 * numInEqConTotal + numEqCon};
    auto B = getV(rowV, colV);
    std::fill_n(B.begin(), ptrdiff_t(B.numRow()) * ptrdiff_t(B.numCol()), 0);
    B[0, 0] = 1;
    // B is `A` augmented with the implicit non-negative constraints
    // B = [ A_0 A_1
    //        0   I  ]
    // V = [B' E' 0
    //      S  0  I]
    // V = [A_0'  0  E_0' 0
    //      A_1'  I  E_1' 0
    //      S_0  S_1  0   I]
    numEquations = numInEqConTotal + numEqCon;
    B[_(begin, numVar), _(1, numInEqConExplicit)] << A.t();
    B[_(begin, numVar), _(numInEqConTotal, numInEqConTotal + numEqCon)]
      << E.t();
    if (numNonNegative)
      B[_(numVar - numNonNegative, numVar),
        _(numInEqConExplicit, numInEqConExplicit + numNonNegative)]
          .diag()
        << 1;
    for (ptrdiff_t j = 0; j < numInEqConTotal; ++j) {
      B[j + numVar, j] = -1;
      B[j + numVar, j + numEquations] = 1;
    }
    initCore(alloc);
  }

  [[nodiscard]] static constexpr auto
  memoryNeededNonNegative(PtrMatrix<int64_t> A, EmptyMatrix<int64_t>,
                          ptrdiff_t numNonNegative) -> ptrdiff_t {
    return memoryNeededImpl(A.numRow(), A.numCol(), Row<>{0}, ++numNonNegative);
  }
  [[nodiscard]] inline static constexpr auto
  memoryNeededImpl(Row<> Ar, Col<> Ac, Row<> Er, ptrdiff_t numPos)
    -> ptrdiff_t {
    // alternative:
    ptrdiff_t numInEqConTotal = ptrdiff_t(Ar) + numPos;
    ptrdiff_t colV = (numInEqConTotal << 1) + ptrdiff_t(Er);
    ptrdiff_t rowV = ptrdiff_t(Ac) + numInEqConTotal;
    return rowV * rowV + std::max(rowV, colV) * colV + colV;
  }
  [[nodiscard]] static constexpr auto
  memoryNeededNonNegative(PtrMatrix<int64_t> A, ptrdiff_t numNonNegative)
    -> ptrdiff_t {
    return memoryNeededImpl(A.numRow(), A.numCol(), Row<>{0}, ++numNonNegative);
  }
  [[nodiscard]] static constexpr auto
  memoryNeededNonNegative(PtrMatrix<int64_t> A, PtrMatrix<int64_t> E,
                          ptrdiff_t numNonNegative) -> ptrdiff_t {
    return memoryNeededImpl(A.numRow(), A.numCol(), E.numRow(),
                            ++numNonNegative);
  }
  [[nodiscard]] static constexpr auto memoryNeeded(PtrMatrix<int64_t> A,
                                                   EmptyMatrix<int64_t>,
                                                   bool pos0) -> ptrdiff_t {
    return memoryNeededImpl(A.numRow(), A.numCol(), Row<>{0}, pos0);
  }
  [[nodiscard]] static constexpr auto memoryNeeded(PtrMatrix<int64_t> A,
                                                   bool pos0) -> ptrdiff_t {
    return memoryNeededImpl(A.numRow(), A.numCol(), Row<>{0}, pos0);
  }
  [[nodiscard]] static constexpr auto memoryNeeded(PtrMatrix<int64_t> A,
                                                   PtrMatrix<int64_t> E,
                                                   bool pos0) -> ptrdiff_t {
    return memoryNeededImpl(A.numRow(), A.numCol(), E.numRow(), pos0);
  }
  constexpr void init(math::Alloc<int64_t> auto alloc, PtrMatrix<int64_t> A,
                      bool pos0) {
    const ptrdiff_t numCon = ptrdiff_t(A.numRow()) + pos0;
    numVar = ptrdiff_t(A.numCol());
    Row<> rowV = {numVar + numCon};
    Col<> colV = {2 * numCon};
    auto B = getV(rowV, colV);
    std::fill_n(B.begin(), ptrdiff_t(B.numRow()) * ptrdiff_t(B.numCol()), 0);
    B[0, 0] = pos0;
    // V = [A' 0
    //      S  I]
    B[_(begin, numVar), _(pos0, numCon)] << A.t();
    for (ptrdiff_t j = 0; j < numCon; ++j) {
      B[j + numVar, j] = -1;
      B[j + numVar, j + numCon] = 1;
    }
    numEquations = numCon;
    initCore(alloc);
  }
  constexpr void init(math::Alloc<int64_t> auto alloc, PtrMatrix<int64_t> A,
                      EmptyMatrix<int64_t>, bool pos0) {
    init(alloc, A, pos0);
  }
  constexpr void init(math::Alloc<int64_t> auto alloc, PtrMatrix<int64_t> A,
                      PtrMatrix<int64_t> E, bool pos0) {
    const ptrdiff_t numInEqCon = ptrdiff_t(A.numRow()) + pos0;
    numVar = ptrdiff_t(A.numCol());
    const ptrdiff_t numEqCon = ptrdiff_t(E.numRow());
    Row rowV = Row<>{numVar + numInEqCon};
    Col colV = Col<>{2 * numInEqCon + numEqCon};
    auto B = getV(rowV, colV);
    B << 0;
    // V = [A' E' 0
    //      S  0  I]
    B[0, 0] = pos0;
    B[_(begin, numVar), _(pos0, numInEqCon)] << A.t();
    // A(_, _(pos0, end)).t();
    B[_(begin, numVar), _(numInEqCon, numInEqCon + numEqCon)] << E.t();

    numEquations = numInEqCon + numEqCon;
    for (ptrdiff_t j = 0; j < numInEqCon; ++j) {
      B[j + numVar, j] = -1;
      B[j + numVar, j + numEquations] = 1;
    }
    initCore(alloc);
  }
  // sets U, V, and d.
  // needs to also set their size, which is only determined here.
  constexpr void initCore(math::Alloc<int64_t> auto alloc) {
    // numVar + numInEq x 2*numInEq + numEq
    MutPtrMatrix<int64_t> B = getV();
    Row R = B.numRow();
    MutPtrMatrix<int64_t> U = getU(); // numVar + numInEq x numVar + numInEq
    U.diag() << 1;
    // We will have query of the form Ax = q;
    simplifySystemsImpl({B, U});
    while ((R) && allZero(B[ptrdiff_t(R) - 1, _])) --R;
    setURank(R);
    ptrdiff_t numColB = ptrdiff_t(B.numCol());
    // upper bounded by numVar + numInEq x numVar + numInEq
    // if V is square, it is full rank and there is 1 solution
    // if V has fewer rows, there are infinitely many solutions
    if (R == numColB) return;
    invariant(R < numColB);
    // H (aliasing V and A) copied
    // R = B.numRow() < B.numCol()
    auto Vt{identity<int64_t>(alloc, numColB)};
    // Ht.numRow() > Ht.numCol() = R
    // (2*numInEq + numEq) x R
    auto Ht = matrix<int64_t>(alloc, Row<>{numColB}, Col<>{ptrdiff_t(R)});
    Ht << B[_(0, R), _].t();
    solveSystem(Ht, Vt);
    // upper bounded by numVar + numInEq
    // rows/cols, but of rank R
    // smaller based on rank
    getD(R) << Ht.diag(); // d.size() == R
    // upper bounded by 2*numInEq + numEq x 2*numInEq + numEq
    getV() << Vt.t();
  }

  // Note that this is only valid when the comparator was constructed
  // with index `0` referring to >= 0 constants (i.e., the default).
  [[nodiscard]] constexpr auto isEmpty(Arena<> alloc) const -> bool {
    auto V = getV();
    auto U = getU();
    auto d = getD();
    StridedVector<int64_t> b{U[_, 0]};
    if (d.empty()) {
      if (!allZero(b[_(V.numRow(), end)])) return false;
      Col oldn = V.numCol();
      auto H{matrix<int64_t>(&alloc, V.numRow(), ++auto{oldn})};
      // IntMatrix H{V.numRow(), oldn + 1};
      H[_, _(0, oldn)] << V;
      H[_, oldn] << -b;
      solveSystem(H);
      for (ptrdiff_t i = numEquations; i < H.numRow(); ++i)
        if ((H[i, oldn] > 0) != (H[i, i] > 0)) return false;
      return true;
    }
    // Column rank deficient case
    Row numSlack = Row<>{ptrdiff_t(V.numRow()) - numEquations};
    // Vector<int64_t> dinv = d; // copy
    // We represent D martix as a vector, and multiply the lcm to the
    // linear equation to avoid store D^(-1) as rational type
    int64_t lcmD = lcm(d);
    auto b2{vector<int64_t>(&alloc, d.size())};
    b2 << -b * lcmD / d;
    // Vector<int64_t> b2 = -b * Dlcm / d;
    ptrdiff_t numRowTrunc = ptrdiff_t(U.numRow());
    auto c{vector<int64_t>(&alloc, ptrdiff_t(V.numRow()) - numEquations)};
    c << b2 * V[_(numEquations, end), _(begin, numRowTrunc)].t();
    // Vector<int64_t> c = V(_(numEquations, end), _(begin, numRowTrunc)) *
    // b2;
    ptrdiff_t dimNS = ptrdiff_t(V.numCol()) - numRowTrunc;
    // expand W stores [c -JV2 JV2]
    //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
    // where y2 = y2+ - y2-
    auto expandW{matrix<int64_t>(&alloc, numSlack, Col<>{dimNS * 2 + 1})};
    for (ptrdiff_t i = 0; i < numSlack; ++i) {
      expandW[i, 0] = c[i];
      // expandW(i, 0) *= Dlcm;
      for (ptrdiff_t j = 0; j < dimNS; ++j) {
        auto val = V[i + numEquations, numRowTrunc + j] * lcmD;
        expandW[i, j + 1] = -val;
        expandW[i, dimNS + 1 + j] = val;
      }
    }
    return Simplex::positiveVariables(&alloc, expandW).hasValue();
  }
  [[nodiscard]] constexpr auto isEmpty() const -> bool {
    alloc::OwningArena<> alloc;
    return isEmpty(alloc);
  }
  [[nodiscard]] constexpr auto greaterEqual(PtrVector<int64_t> query) const
    -> bool {
    alloc::OwningArena<> alloc;
    return greaterEqual(alloc, query);
  }
  [[nodiscard]] constexpr auto greaterEqualFullRank(Arena<> *alloc,
                                                    PtrVector<int64_t> b) const
    -> bool {
    auto V = getV();
    if (!allZero(b[_(V.numRow(), end)])) return false;
    auto H = matrix<int64_t>(alloc, V.numRow(), ++auto{V.numCol()});
    Col oldn = V.numCol();
    H[_, _(0, oldn)] << V;
    // H.numRow() == b.size(), because we're only here if dimD == 0,
    // in which case V.numRow() == U.numRow() == b.size()
    H[_, oldn] << b;
    solveSystem(H);
    for (ptrdiff_t i = numEquations; i < H.numRow(); ++i)
      if ((H[i, oldn] > 0) != (H[i, i] > 0)) return false;
    return true;
  }
  [[nodiscard]] constexpr auto
  greaterEqualRankDeficient(Arena<> *alloc, MutPtrVector<int64_t> b) const
    -> bool {
    auto V = getV();
    auto d = getD();
    Row numSlack = Row<>{ptrdiff_t(V.numRow()) - numEquations};
    auto dinv = vector<int64_t>(alloc, d.size());
    dinv << d; // copy
    // We represent D martix as a vector, and multiply the lcm to the
    // linear equation to avoid store D^(-1) as rational type
    int64_t lcmD = lcm(dinv);
    for (ptrdiff_t i = 0; i < dinv.size(); ++i) {
      auto x = lcmD / dinv[i];
      dinv[i] = x;
      b[i] *= x;
    }
    ptrdiff_t numRowTrunc = getURank();
    auto c = vector<int64_t>(alloc, ptrdiff_t(V.numRow()) - numEquations);
    c << b * V[_(numEquations, end), _(begin, numRowTrunc)].t();
    auto dimNS = ptrdiff_t(V.numCol()) - numRowTrunc;
    // expand W stores [c -JV2 JV2]
    //  we use simplex to solve [-JV2 JV2][y2+ y2-]' <= JV1D^(-1)Uq
    // where y2 = y2+ - y2-
    auto expandW = matrix<int64_t>(alloc, numSlack, Col<>{dimNS * 2 + 1});
    for (ptrdiff_t i = 0; i < numSlack; ++i) {
      expandW[i, 0] = c[i];
      // expandW(i, 0) *= Dlcm;
      for (ptrdiff_t j = 0; j < dimNS;) {
        auto val = V[i + numEquations, numRowTrunc + j++] * lcmD;
        expandW[i, j] = -val;
        expandW[i, dimNS + j] = val;
      }
    }
    Optional<Simplex *> optS{Simplex::positiveVariables(alloc, expandW)};
    return optS.hasValue();
  }
  [[nodiscard]] constexpr auto greaterEqual(Arena<> alloc,
                                            PtrVector<int64_t> query) const
    -> bool {
    auto U = getU();
    auto b = vector<int64_t>(&alloc, ptrdiff_t(U.numRow()));
    b << query * U[_, _(begin, query.size())].t();
    return getD().size() ? greaterEqualRankDeficient(&alloc, b)
                         : greaterEqualFullRank(&alloc, b);
  }
};
struct LinearSymbolicComparator
  : public BaseSymbolicComparator<LinearSymbolicComparator> {
  using Base = BaseSymbolicComparator<LinearSymbolicComparator>;
  using Base::init;
  using Matrix = math::ManagedArray<int64_t, DenseDims<>>;
  [[no_unique_address]] Matrix U;
  [[no_unique_address]] Matrix V;
  [[no_unique_address]] Vector<int64_t> d;
  constexpr auto getUImpl() -> MutDensePtrMatrix<int64_t> { return U; }
  constexpr auto getVImpl() -> MutDensePtrMatrix<int64_t> { return V; }
  constexpr auto getDImpl() -> MutPtrVector<int64_t> { return d; }
  [[nodiscard]] constexpr auto getUImpl() const -> DensePtrMatrix<int64_t> {
    return U;
  }
  [[nodiscard]] constexpr auto getVImpl() const -> DensePtrMatrix<int64_t> {
    return V;
  }
  [[nodiscard]] constexpr auto getDImpl() const -> PtrVector<int64_t> {
    return d;
  }

  constexpr void setURankImpl(Row<> r) {
    V.truncate(r);
    U.truncate(r);
  }
  // void setURankImpl(Row r) {
  //   U.truncate(r);
  // }
  // void setUColImpl(Col c) { colU = unsigned(c); }
  // void setVDimImpl(ptrdiff_t x) { dimV = unsigned(x); }
  // void setDDimImpl(ptrdiff_t x) { dimD = unsigned(x); }
  [[nodiscard]] constexpr auto getURankImpl() const -> ptrdiff_t {
    return ptrdiff_t(U.numRow());
  }
  constexpr auto getUImpl(Row<> r, Col<> c) -> MutDensePtrMatrix<int64_t> {
    U.resizeForOverwrite(r, c);
    return U;
  }
  constexpr auto getVImpl(Row<> r, Col<> c) -> MutDensePtrMatrix<int64_t> {
    V.setSize(r, c);
    U.setSize(r, Col<>{ptrdiff_t(r)});
    return V;
  }
  constexpr auto getDImpl(Row<> N) -> MutPtrVector<int64_t> {
    d.resizeForOverwrite(ptrdiff_t(N));
    V.resizeForOverwrite(Row<>{ptrdiff_t{V.numCol()}});
    return d;
  }
  static constexpr auto construct(PtrMatrix<int64_t> Ap, EmptyMatrix<int64_t>,
                                  bool pos0) -> LinearSymbolicComparator {
    return construct(Ap, pos0);
  };
  static constexpr auto construct(PtrMatrix<int64_t> Ap, bool pos0)
    -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    alloc::Mallocator<int64_t> alloc{};
    cmp.init(alloc, Ap, pos0);
    return cmp;
  };
  static constexpr auto construct(PtrMatrix<int64_t> Ap, PtrMatrix<int64_t> Ep,
                                  bool pos0) -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    alloc::Mallocator<int64_t> alloc{};
    cmp.init(alloc, Ap, Ep, pos0);
    return cmp;
  };
  static constexpr auto constructNonNeg(PtrMatrix<int64_t> Ap,
                                        EmptyMatrix<int64_t>,
                                        ptrdiff_t numNonNeg)
    -> LinearSymbolicComparator {
    return constructNonNeg(Ap, numNonNeg);
  };
  static constexpr auto constructNonNeg(PtrMatrix<int64_t> Ap,
                                        ptrdiff_t numNonNeg)
    -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    alloc::Mallocator<int64_t> alloc{};
    cmp.initNonNegative(alloc, Ap, numNonNeg);
    return cmp;
  };
  static constexpr auto constructNonNeg(PtrMatrix<int64_t> Ap,
                                        PtrMatrix<int64_t> Ep,
                                        ptrdiff_t numNonNeg)
    -> LinearSymbolicComparator {
    LinearSymbolicComparator cmp;
    alloc::Mallocator<int64_t> alloc{};
    cmp.initNonNegative(alloc, Ap, Ep, numNonNeg);
    return cmp;
  };
};
struct PtrSymbolicComparator
  : public BaseSymbolicComparator<PtrSymbolicComparator> {
  using Base = BaseSymbolicComparator<PtrSymbolicComparator>;
  using Base::init;
  int64_t *mem;
  ptrdiff_t rankU{0};
  ptrdiff_t colU{0};
  ptrdiff_t dimV{0};
  ptrdiff_t dimD{0};

  constexpr void setURankImpl(Row<> r) { rankU = ptrdiff_t(r); }
  [[nodiscard]] constexpr auto getURankImpl() const -> ptrdiff_t {
    return rankU;
  }
  // void setUColImpl(Col c) { colU = unsigned(c); }
  // void setVDimImpl(ptrdiff_t d) { dimV = unsigned(d); }
  // void setDDimImpl(ptrdiff_t d) { dimD = int(d); }

  // R x numVar + numInEq
  // [[nodiscard]] constexpr auto colU() const -> unsigned {
  //   return numVar + numInEq;
  // }
  // [[nodiscard]] constexpr auto dimV() const -> unsigned {
  //   return 2 * numInEq + numEq;
  // }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  constexpr auto getUImpl() -> MutDensePtrMatrix<int64_t> {
    return {mem, DenseDims<>{{rankU}, {colU}}};
  }
  // A = V
  // H = A
  // H.truncate(Row());
  // size is H.numCol() * H.numCol()
  // offset by (numVar + numInEq)*(numVar + numInEq)
  constexpr auto getVImpl() -> MutDensePtrMatrix<int64_t> {
    return {getUImpl().end(), DenseDims<>{numVRows(), Col<>{dimV}}};
  }
  // size D
  constexpr auto getDImpl() -> MutPtrVector<int64_t> {
    // d = Ht.diag()
    return {getVImpl().end(), dimD};
  }
  [[nodiscard]] constexpr auto getUImpl() const -> DensePtrMatrix<int64_t> {
    return {mem, DenseDims<>{Row<>{rankU}, Col<>{colU}}};
  }
  [[nodiscard]] constexpr auto getVImpl() const -> DensePtrMatrix<int64_t> {
    return {mem + ptrdiff_t(rankU) * colU,
            DenseDims<>{numVRows(), Col<>{dimV}}};
  }
  [[nodiscard]] constexpr auto getDImpl() const -> PtrVector<int64_t> {
    return {mem + ptrdiff_t(rankU) * colU + ptrdiff_t(numVRows()) * dimV, dimD};
  }
  // constexpr auto getUImpl(Row r, Col c) -> MutPtrMatrix<int64_t> {}
  constexpr auto getVImpl(Row<> r, Col<> c) -> MutDensePtrMatrix<int64_t> {
    colU = rankU = ptrdiff_t(r);
    dimV = ptrdiff_t(c);
    getUImpl() << 0;
    dimD = 0;
    return getVImpl();
  }
  constexpr auto getDImpl(Row<> r) -> MutPtrVector<int64_t> {
    dimD = ptrdiff_t(r);
    invariant(dimD > 0);
    return getDImpl();
  }
  static constexpr auto construct(Arena<> *alloc, PtrMatrix<int64_t> Ap,
                                  EmptyMatrix<int64_t>, bool pos0)
    -> PtrSymbolicComparator {
    return construct(alloc, Ap, pos0);
  };
  static constexpr auto construct(Arena<> *alloc, PtrMatrix<int64_t> Ap,
                                  bool pos0) -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(alloc->allocate<int64_t>(memoryNeeded(Ap, pos0)));
    cmp.init(alloc, Ap, pos0);
    return cmp;
  };
  static constexpr auto construct(Arena<> *alloc, PtrMatrix<int64_t> Ap,
                                  PtrMatrix<int64_t> Ep, bool pos0)
    -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(
      alloc->allocate<int64_t>(memoryNeeded(Ap, Ep, pos0)));
    cmp.init(alloc, Ap, Ep, pos0);
    return cmp;
  };
  static constexpr auto constructNonNeg(Arena<> *alloc, PtrMatrix<int64_t> Ap,
                                        EmptyMatrix<int64_t>,
                                        ptrdiff_t numNonNeg)
    -> PtrSymbolicComparator {
    return constructNonNeg(alloc, Ap, numNonNeg);
  };
  static constexpr auto constructNonNeg(Arena<> *alloc, PtrMatrix<int64_t> Ap,
                                        ptrdiff_t numNonNeg)
    -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(
      alloc->allocate<int64_t>(memoryNeededNonNegative(Ap, numNonNeg)));
    cmp.initNonNegative(alloc, Ap, numNonNeg);
    return cmp;
  };
  static constexpr auto constructNonNeg(Arena<> *alloc, PtrMatrix<int64_t> Ap,
                                        PtrMatrix<int64_t> Ep,
                                        ptrdiff_t numNonNeg)
    -> PtrSymbolicComparator {
    PtrSymbolicComparator cmp(
      alloc->allocate<int64_t>(memoryNeededNonNegative(Ap, Ep, numNonNeg)));
    cmp.initNonNegative(alloc, Ap, Ep, numNonNeg);
    return cmp;
  };

private:
  [[nodiscard]] constexpr auto numVRows() const -> Row<> {
    return {ptrdiff_t(dimD ? dimV : rankU)};
  }

  constexpr PtrSymbolicComparator(int64_t *p) : mem(p) {}
};

static_assert(Comparator<PtrSymbolicComparator>);
static_assert(Comparator<LinearSymbolicComparator>);

constexpr void moveEqualities(DenseMatrix<int64_t> &, EmptyMatrix<int64_t>,
                              const Comparator auto &) {}
constexpr void moveEqualities(DenseMatrix<int64_t> &A, math::IntMatrix<> &E,
                              const Comparator auto &C) {
  const ptrdiff_t numVar = ptrdiff_t(E.numCol());
  assert(A.numCol() == numVar);
  if (A.numRow() <= 1) return;
  for (ptrdiff_t o = ptrdiff_t(A.numRow()) - 1; o > 0;) {
    for (ptrdiff_t i = o--; i < A.numRow(); ++i) {
      bool isNeg = true;
      for (ptrdiff_t v = 0; v < numVar; ++v) {
        if (A[i, v] != -A[o, v]) {
          isNeg = false;
          break;
        }
      }
      if (isNeg && C.equalNegative(A[i, _], A[o, _])) {
        ptrdiff_t e = ptrdiff_t(E.numRow());
        E.resize(Row<>{e + 1}, Col<>{numVar});
        for (ptrdiff_t v = 0; v < numVar; ++v) E[e, v] = A[i, v];
        eraseConstraint(A, i, o);
        break;
      }
    }
  }
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linear(alloc::Mallocator<int64_t>, PtrMatrix<int64_t> A,
                      EmptyMatrix<int64_t>, bool pos0) {
  return LinearSymbolicComparator::construct(A, pos0);
}
constexpr auto linear(Arena<> *alloc, PtrMatrix<int64_t> A,
                      EmptyMatrix<int64_t>, bool pos0) {
  return PtrSymbolicComparator::construct(alloc, A, pos0);
}
// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linear(alloc::Mallocator<int64_t>, PtrMatrix<int64_t> A,
                      PtrMatrix<int64_t> E, bool pos0) {
  return LinearSymbolicComparator::construct(A, E, pos0);
}
constexpr auto linear(Arena<> *alloc, PtrMatrix<int64_t> A,
                      PtrMatrix<int64_t> E, bool pos0) {
  return PtrSymbolicComparator::construct(alloc, A, E, pos0);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linearNonNegative(alloc::Mallocator<int64_t>,
                                 PtrMatrix<int64_t> A, EmptyMatrix<int64_t>,
                                 ptrdiff_t numNonNeg) {
  return LinearSymbolicComparator::constructNonNeg(A, numNonNeg);
}
constexpr auto linearNonNegative(Arena<> *alloc, PtrMatrix<int64_t> A,
                                 EmptyMatrix<int64_t>, ptrdiff_t numNonNeg) {
  return PtrSymbolicComparator::constructNonNeg(alloc, A, numNonNeg);
}
// NOLINTNEXTLINE(performance-unnecessary-value-param)
constexpr auto linearNonNegative(alloc::Mallocator<int64_t>,
                                 PtrMatrix<int64_t> A, PtrMatrix<int64_t> E,
                                 ptrdiff_t numNonNeg) {
  return LinearSymbolicComparator::constructNonNeg(A, E, numNonNeg);
}
constexpr auto linearNonNegative(Arena<> *alloc, PtrMatrix<int64_t> A,
                                 PtrMatrix<int64_t> E, ptrdiff_t numNonNeg) {
  return PtrSymbolicComparator::constructNonNeg(alloc, A, E, numNonNeg);
}

} // namespace poly::comparator
