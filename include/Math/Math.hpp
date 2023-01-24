#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.

#include "Math/AxisTypes.hpp"
#include "Math/Indexing.hpp"
#include "Math/Matrix.hpp"
#include "Math/Vector.hpp"
#include "TypePromotion.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <numeric>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
// #ifndef NDEBUG
// #include <memory>
// #include <stacktrace>
// using stacktrace =
//     std::basic_stacktrace<std::allocator<std::stacktrace_entry>>;
// #endif

struct Rational;
namespace LinearAlgebra {

[[gnu::flatten]] inline auto copyto(AbstractVector auto &y,
                                    const AbstractVector auto &x) -> auto & {
  const size_t M = x.size();
  y.extendOrAssertSize(M);
  for (size_t i = 0; i < M; ++i) y[i] = x[i];
  return y;
}
[[gnu::flatten]] inline auto copyto(AbstractMatrixCore auto &A,
                                    const AbstractMatrixCore auto &B)
  -> auto & {
  const Row M = B.numRow();
  const Col N = B.numCol();
  A.extendOrAssertSize(M, N);
  for (size_t r = 0; r < M; ++r)
    for (size_t c = 0; c < N; ++c) A(r, c) = B(r, c);
  return A;
}

[[gnu::flatten]] auto operator==(const AbstractMatrix auto &A,
                                 const AbstractMatrix auto &B) -> bool {
  const Row M = B.numRow();
  const Col N = B.numCol();
  if ((M != A.numRow()) || (N != A.numCol())) return false;
  for (size_t r = 0; r < M; ++r)
    for (size_t c = 0; c < N; ++c)
      if (A(r, c) != B(r, c)) return false;
  return true;
}

struct Add {
  constexpr auto operator()(auto x, auto y) const { return x + y; }
};
struct Sub {
  constexpr auto operator()(auto x) const { return -x; }
  constexpr auto operator()(auto x, auto y) const { return x - y; }
};
struct Mul {
  constexpr auto operator()(auto x, auto y) const { return x * y; }
};
struct Div {
  constexpr auto operator()(auto x, auto y) const { return x / y; }
};

template <typename Op, typename A> struct ElementwiseUnaryOp {
  using eltype = typename A::eltype;
  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  auto operator()(size_t i, size_t j) const { return op(a(i, j)); }

  [[nodiscard]] constexpr auto size() const { return a.size(); }
  [[nodiscard]] constexpr auto numRow() const -> Row { return a.numRow(); }
  [[nodiscard]] constexpr auto numCol() const -> Col { return a.numCol(); }
  [[nodiscard]] constexpr auto view() const { return *this; };
};
template <typename Op, AbstractVector A> struct ElementwiseUnaryOp<Op, A> {
  using eltype = typename A::eltype;
  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  auto operator[](size_t i) const { return op(a[i]); }

  [[nodiscard]] constexpr auto size() const { return a.size(); }
  [[nodiscard]] constexpr auto view() const { return *this; };
};
// scalars broadcast
constexpr auto get(const std::integral auto A, size_t) { return A; }
constexpr auto get(const std::floating_point auto A, size_t) { return A; }
constexpr auto get(const std::integral auto A, size_t, size_t) { return A; }
constexpr auto get(const std::floating_point auto A, size_t, size_t) {
  return A;
}
inline auto get(const AbstractVector auto &A, size_t i) { return A[i]; }
inline auto get(const AbstractMatrix auto &A, size_t i, size_t j) {
  return A(i, j);
}

constexpr auto size(const std::integral auto) -> size_t { return 1; }
constexpr auto size(const std::floating_point auto) -> size_t { return 1; }
constexpr auto size(const AbstractVector auto &x) -> size_t { return x.size(); }

template <typename T>
concept Scalar =
  std::integral<T> || std::floating_point<T> || std::same_as<T, Rational>;

template <typename T>
concept VectorOrScalar = AbstractVector<T> || Scalar<T>;
template <typename T>
concept MatrixOrScalar = AbstractMatrix<T> || Scalar<T>;
template <typename T>
concept TriviallyCopyableVectorOrScalar =
  std::is_trivially_copyable_v<T> && VectorOrScalar<T>;
template <typename T>
concept TriviallyCopyableMatrixOrScalar =
  std::is_trivially_copyable_v<T> && MatrixOrScalar<T>;

template <typename Op, TriviallyCopyableVectorOrScalar A,
          TriviallyCopyableVectorOrScalar B>
struct ElementwiseVectorBinaryOp {
  using eltype = promote_eltype_t<A, B>;
  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  ElementwiseVectorBinaryOp(Op _op, A _a, B _b) : op(_op), a(_a), b(_b) {}
  auto operator[](size_t i) const { return op(get(a, i), get(b, i)); }
  [[nodiscard]] constexpr auto size() const -> size_t {
    if constexpr (AbstractVector<A> && AbstractVector<B>) {
      const size_t N = a.size();
      assert(N == b.size());
      return N;
    } else if constexpr (AbstractVector<A>) {
      return a.size();
    } else { // if constexpr (AbstractVector<B>) {
      return b.size();
    }
  }
  [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
};

template <typename Op, TriviallyCopyableMatrixOrScalar A,
          TriviallyCopyableMatrixOrScalar B>
struct ElementwiseMatrixBinaryOp {
  using eltype = promote_eltype_t<A, B>;
  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  ElementwiseMatrixBinaryOp(Op _op, A _a, B _b) : op(_op), a(_a), b(_b) {}
  auto operator()(size_t i, size_t j) const {
    return op(get(a, i, j), get(b, i, j));
  }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    static_assert(AbstractMatrix<A> || std::integral<A> ||
                    std::floating_point<A>,
                  "Argument A to elementwise binary op is not a matrix.");
    static_assert(AbstractMatrix<B> || std::integral<B> ||
                    std::floating_point<B>,
                  "Argument B to elementwise binary op is not a matrix.");
    if constexpr (AbstractMatrix<A> && AbstractMatrix<B>) {
      const Row N = a.numRow();
      assert(N == b.numRow());
      return N;
    } else if constexpr (AbstractMatrix<A>) {
      return a.numRow();
    } else if constexpr (AbstractMatrix<B>) {
      return b.numRow();
    }
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    static_assert(AbstractMatrix<A> || std::integral<A> ||
                    std::floating_point<A>,
                  "Argument A to elementwise binary op is not a matrix.");
    static_assert(AbstractMatrix<B> || std::integral<B> ||
                    std::floating_point<B>,
                  "Argument B to elementwise binary op is not a matrix.");
    if constexpr (AbstractMatrix<A> && AbstractMatrix<B>) {
      const Col N = a.numCol();
      assert(N == b.numCol());
      return N;
    } else if constexpr (AbstractMatrix<A>) {
      return a.numCol();
    } else if constexpr (AbstractMatrix<B>) {
      return b.numCol();
    }
  }
  [[nodiscard]] constexpr auto size() const -> std::pair<Row, Col> {
    return std::make_pair(numRow(), numCol());
  }
  [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
};

template <typename A> struct Transpose {
  static_assert(AbstractMatrix<A>, "Argument to transpose is not a matrix.");
  static_assert(std::is_trivially_copyable_v<A>,
                "Argument to transpose is not trivially copyable.");

  using eltype = eltype_t<A>;
  [[no_unique_address]] A a;
  auto operator()(size_t i, size_t j) const { return a(j, i); }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return Row{size_t{a.numCol()}};
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    return Col{size_t{a.numRow()}};
  }
  [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
  [[nodiscard]] constexpr auto size() const -> std::pair<Row, Col> {
    return std::make_pair(numRow(), numCol());
  }
  Transpose(A b) : a(b) {}
};
template <typename A> Transpose(A) -> Transpose<A>;
template <AbstractMatrix A, AbstractMatrix B> struct MatMatMul {
  using eltype = promote_eltype_t<A, B>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  auto operator()(size_t i, size_t j) const {
    static_assert(AbstractMatrix<B>, "B should be an AbstractMatrix");
    eltype s = 0;
    for (size_t k = 0; k < size_t(a.numCol()); ++k) s += a(i, k) * b(k, j);
    return s;
  }
  [[nodiscard]] constexpr auto numRow() const -> Row { return a.numRow(); }
  [[nodiscard]] constexpr auto numCol() const -> Col { return b.numCol(); }
  [[nodiscard]] constexpr auto size() const -> std::pair<Row, Col> {
    return std::make_pair(numRow(), numCol());
  }
  [[nodiscard]] constexpr auto view() const { return *this; };
  [[nodiscard]] constexpr auto transpose() const { return Transpose{*this}; };
};
template <AbstractMatrix A, AbstractVector B> struct MatVecMul {
  using eltype = promote_eltype_t<A, B>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  auto operator[](size_t i) const {
    static_assert(AbstractVector<B>, "B should be an AbstractVector");
    eltype s = 0;
    for (size_t k = 0; k < a.numCol(); ++k) s += a(i, k) * b[k];
    return s;
  }
  [[nodiscard]] constexpr auto size() const -> size_t {
    return size_t(a.numRow());
  }
  constexpr auto view() const { return *this; };
};

template <typename T> PtrVector(T *, size_t) -> PtrVector<T>;
template <typename T> MutPtrVector(T *, size_t) -> MutPtrVector<T>;
template <typename T> PtrVector(NotNull<T>, size_t) -> PtrVector<T>;
template <typename T> MutPtrVector(NotNull<T>, size_t) -> MutPtrVector<T>;

//
// Vectors
//

template <typename T> constexpr auto view(llvm::SmallVectorImpl<T> &x) {
  return MutPtrVector<T>{x.data(), x.size()};
}
template <typename T> constexpr auto view(const llvm::SmallVectorImpl<T> &x) {
  return PtrVector<T>{x.data(), x.size()};
}
template <typename T> constexpr auto view(llvm::MutableArrayRef<T> x) {
  return MutPtrVector<T>{x.data(), x.size()};
}
template <typename T> constexpr auto view(llvm::ArrayRef<T> x) {
  return PtrVector<T>{x.data(), x.size()};
}
static_assert(!AbstractMatrix<StridedVector<int64_t>>);

// static_assert(std::is_trivially_copyable_v<MutStridedVector<int64_t>>);
static_assert(std::is_trivially_copyable_v<
              ElementwiseUnaryOp<Sub, StridedVector<int64_t>>>);
static_assert(TriviallyCopyableVectorOrScalar<
              ElementwiseUnaryOp<Sub, StridedVector<int64_t>>>);

auto printVectorImpl(llvm::raw_ostream &os, const AbstractVector auto &a)
  -> llvm::raw_ostream & {
  os << "[ ";
  if (size_t M = a.size()) {
    os << a[0];
    for (size_t m = 1; m < M; m++) os << ", " << a[m];
  }
  os << " ]";
  return os;
}
template <typename T>
auto printVector(llvm::raw_ostream &os, PtrVector<T> a) -> llvm::raw_ostream & {
  return printVectorImpl(os, a);
}
template <typename T>
auto printVector(llvm::raw_ostream &os, StridedVector<T> a)
  -> llvm::raw_ostream & {
  return printVectorImpl(os, a);
}
template <typename T>
auto printVector(llvm::raw_ostream &os, const llvm::SmallVectorImpl<T> &a)
  -> llvm::raw_ostream & {
  return printVector(os, PtrVector<T>{a.data(), a.size()});
}

template <typename T>
inline auto operator<<(llvm::raw_ostream &os, PtrVector<T> const &A)
  -> llvm::raw_ostream & {
  return printVector(os, A);
}
inline auto operator<<(llvm::raw_ostream &os, const AbstractVector auto &A)
  -> llvm::raw_ostream & {
  return printVector(os, A.view());
}

auto allMatch(const AbstractVector auto &x0, const AbstractVector auto &x1)
  -> bool {
  size_t N = x0.size();
  if (N != x1.size()) return false;
  for (size_t n = 0; n < N; ++n)
    if (x0(n) != x1(n)) return false;
  return true;
}

inline void swap(MutPtrMatrix<int64_t> A, Row i, Row j) {
  if (i == j) return;
  Col N = A.numCol();
  assert((i < A.numRow()) && (j < A.numRow()));

  for (size_t n = 0; n < N; ++n) std::swap(A(i, n), A(j, n));
}
inline void swap(MutPtrMatrix<int64_t> A, Col i, Col j) {
  if (i == j) return;
  Row M = A.numRow();
  assert((i < A.numCol()) && (j < A.numCol()));

  for (size_t m = 0; m < M; ++m) std::swap(A(m, i), A(m, j));
}
template <typename T>
inline void swap(llvm::SmallVectorImpl<T> &A, Col i, Col j) {
  std::swap(A[i], A[j]);
}
template <typename T>
inline void swap(llvm::SmallVectorImpl<T> &A, Row i, Row j) {
  std::swap(A[i], A[j]);
}

template <int Bits, class T>
constexpr bool is_uint_v =
  sizeof(T) == (Bits / 8) && std::is_integral_v<T> && !std::is_signed_v<T>;

template <class T>
constexpr auto zeroUpper(T x) -> T
requires is_uint_v<16, T>
{
  return x & 0x00ff;
}
template <class T>
constexpr auto zeroLower(T x) -> T
requires is_uint_v<16, T>
{
  return x & 0xff00;
}
template <class T>
constexpr auto upperHalf(T x) -> T
requires is_uint_v<16, T>
{
  return x >> 8;
}

template <class T>
constexpr auto zeroUpper(T x) -> T
requires is_uint_v<32, T>
{
  return x & 0x0000ffff;
}
template <class T>
constexpr auto zeroLower(T x) -> T
requires is_uint_v<32, T>
{
  return x & 0xffff0000;
}
template <class T>
constexpr auto upperHalf(T x) -> T
requires is_uint_v<32, T>
{
  return x >> 16;
}
template <class T>
constexpr auto zeroUpper(T x) -> T
requires is_uint_v<64, T>
{
  return x & 0x00000000ffffffff;
}
template <class T>
constexpr auto zeroLower(T x) -> T
requires is_uint_v<64, T>
{
  return x & 0xffffffff00000000;
}
template <class T>
constexpr auto upperHalf(T x) -> T
requires is_uint_v<64, T>
{
  return x >> 32;
}

template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

static_assert(std::copy_constructible<PtrMatrix<int64_t>>);
// static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>);
static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>);
static_assert(TriviallyCopyableMatrixOrScalar<PtrMatrix<int64_t>>);
static_assert(TriviallyCopyableMatrixOrScalar<int>);
static_assert(TriviallyCopyable<Mul>);
static_assert(TriviallyCopyableMatrixOrScalar<
              ElementwiseMatrixBinaryOp<Mul, PtrMatrix<int64_t>, int>>);
static_assert(TriviallyCopyableMatrixOrScalar<
              MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>);

template <TriviallyCopyable OP, TriviallyCopyableVectorOrScalar A,
          TriviallyCopyableVectorOrScalar B>
ElementwiseVectorBinaryOp(OP, A, B) -> ElementwiseVectorBinaryOp<OP, A, B>;
template <TriviallyCopyable OP, TriviallyCopyableMatrixOrScalar A,
          TriviallyCopyableMatrixOrScalar B>
ElementwiseMatrixBinaryOp(OP, A, B) -> ElementwiseMatrixBinaryOp<OP, A, B>;

inline constexpr auto view(const Scalar auto &x) { return x; }
inline constexpr auto view(const AbstractVector auto &x) { return x.view(); }
inline constexpr auto view(const AbstractMatrixCore auto &x) {
  return x.view();
}

constexpr auto bin2(std::integral auto x) { return (x * (x - 1)) >> 1; }

template <typename T>
auto printMatrix(llvm::raw_ostream &os, PtrMatrix<T> A) -> llvm::raw_ostream & {
  // llvm::raw_ostream &printMatrix(llvm::raw_ostream &os, T const &A) {
  auto [m, n] = A.size();
  if (!m) return os << "[ ]";
  for (Row i = 0; i < m; i++) {
    if (i) os << "  ";
    else os << "\n[ ";
    if (n) {
      for (Col j = 0; j < n - 1; j++) {
        auto Aij = A(i, j);
        if (Aij >= 0) os << " ";
        os << Aij << " ";
      }
    }
    if (n) {
      auto Aij = A(i, n - 1);
      if (Aij >= 0) os << " ";
      os << Aij;
    }
    if (i != m - 1) os << "\n";
  }
  os << " ]";
  return os;
}

template <typename T> struct SmallSparseMatrix {
  // non-zeros
  [[no_unique_address]] llvm::SmallVector<T> nonZeros{};
  // masks, the upper 8 bits give the number of elements in previous rows
  // the remaining 24 bits are a mask indicating non-zeros within this row
  static constexpr size_t maxElemPerRow = 24;
  [[no_unique_address]] llvm::SmallVector<uint32_t> rows;
  [[no_unique_address]] Col col;
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return Row{rows.size()};
  }
  [[nodiscard]] constexpr auto numCol() const -> Col { return col; }
  SmallSparseMatrix(Row numRows, Col numCols)
    : rows{llvm::SmallVector<uint32_t>(size_t(numRows))}, col{numCols} {
    assert(size_t(col) <= maxElemPerRow);
  }
  auto get(Row i, Col j) const -> T {
    assert(j < col);
    uint32_t r(rows[size_t(i)]);
    uint32_t jshift = uint32_t(1) << size_t(j);
    if (r & (jshift)) {
      // offset from previous rows
      uint32_t prevRowOffset = r >> maxElemPerRow;
      uint32_t rowOffset = std::popcount(r & (jshift - 1));
      return nonZeros[rowOffset + prevRowOffset];
    } else {
      return 0;
    }
  }
  constexpr auto operator()(size_t i, size_t j) const -> T {
    return get(Row{i}, Col{j});
  }
  void insert(T x, Row i, Col j) {
    assert(j < col);
    llvm::errs() << "inserting " << x << " at " << size_t(i) << ", "
                 << size_t(j) << "; rows.size() = " << rows.size() << "\n";
    uint32_t r{rows[size_t(i)]};
    uint32_t jshift = uint32_t(1) << size_t(j);
    // offset from previous rows
    uint32_t prevRowOffset = r >> maxElemPerRow;
    uint32_t rowOffset = std::popcount(r & (jshift - 1));
    size_t k = rowOffset + prevRowOffset;
    if (r & jshift) {
      nonZeros[k] = std::move(x);
    } else {
      nonZeros.insert(nonZeros.begin() + k, std::move(x));
      rows[size_t(i)] = r | jshift;
      for (size_t l = size_t(i) + 1; l < rows.size(); ++l)
        rows[l] += uint32_t(1) << maxElemPerRow;
    }
  }

  struct Reference {
    [[no_unique_address]] SmallSparseMatrix<T> *A;
    [[no_unique_address]] size_t i, j;
    operator T() const { return A->get(Row{i}, Col{j}); }
    void operator=(T x) {
      A->insert(std::move(x), Row{i}, Col{j});
      return;
    }
  };
  auto operator()(size_t i, size_t j) -> Reference {
    return Reference{this, i, j};
  }
  operator Matrix<T>() {
    Matrix<T> A(Row{numRow()}, Col{numCol()});
    assert(numRow() == A.numRow());
    assert(numCol() == A.numCol());
    size_t k = 0;
    for (size_t i = 0; i < numRow(); ++i) {
      uint32_t m = rows[i] & 0x00ffffff;
      size_t j = 0;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        j += tz;
        A(i, j++) = nonZeros[k++];
      }
    }
    assert(k == nonZeros.size());
    return A;
  }
};

template <typename T>
inline auto operator<<(llvm::raw_ostream &os, SmallSparseMatrix<T> const &A)
  -> llvm::raw_ostream & {
  size_t k = 0;
  os << "[ ";
  for (size_t i = 0; i < A.numRow(); ++i) {
    if (i) os << "  ";
    uint32_t m = A.rows[i] & 0x00ffffff;
    size_t j = 0;
    while (m) {
      if (j) os << " ";
      uint32_t tz = std::countr_zero(m);
      m >>= (tz + 1);
      j += (tz + 1);
      while (tz--) os << " 0 ";
      const T &x = A.nonZeros[k++];
      if (x >= 0) os << " ";
      os << x;
    }
    for (; j < A.numCol(); ++j) os << "  0";
    os << "\n";
  }
  os << " ]";
  assert(k == A.nonZeros.size());
  return os;
}
template <typename T>
inline auto operator<<(llvm::raw_ostream &os, PtrMatrix<T> A)
  -> llvm::raw_ostream & {
  return printMatrix(os, A);
}
template <AbstractMatrix T>
inline auto operator<<(llvm::raw_ostream &os, const T &A)
  -> llvm::raw_ostream & {
  Matrix<std::remove_const_t<typename T::eltype>> B{A};
  return printMatrix(os, PtrMatrix<typename T::eltype>(B));
}

constexpr auto operator-(const AbstractVector auto &a) {
  auto AA{a.view()};
  return ElementwiseUnaryOp<Sub, decltype(AA)>{.op = Sub{}, .a = AA};
}
constexpr auto operator-(const AbstractMatrix auto &a) {
  auto AA{a.view()};
  return ElementwiseUnaryOp<Sub, decltype(AA)>{.op = Sub{}, .a = AA};
}
static_assert(AbstractMatrix<ElementwiseUnaryOp<Sub, PtrMatrix<int64_t>>>);
static_assert(AbstractMatrix<SquareMatrix<int64_t>>);

constexpr auto operator+(const AbstractMatrix auto &a, const auto &b) {
  return ElementwiseMatrixBinaryOp(Add{}, view(a), view(b));
}
constexpr auto operator+(const AbstractVector auto &a, const auto &b) {
  return ElementwiseVectorBinaryOp(Add{}, view(a), view(b));
}
constexpr auto operator+(Scalar auto a, const AbstractMatrix auto &b) {
  return ElementwiseMatrixBinaryOp(Add{}, view(a), view(b));
}
constexpr auto operator+(Scalar auto a, const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Add{}, view(a), view(b));
}
constexpr auto operator-(const AbstractMatrix auto &a, const auto &b) {
  return ElementwiseMatrixBinaryOp(Sub{}, view(a), view(b));
}
constexpr auto operator-(const AbstractVector auto &a, const auto &b) {
  return ElementwiseVectorBinaryOp(Sub{}, view(a), view(b));
}
constexpr auto operator-(Scalar auto a, const AbstractMatrix auto &b) {
  return ElementwiseMatrixBinaryOp(Sub{}, view(a), view(b));
}
constexpr auto operator-(Scalar auto a, const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Sub{}, view(a), view(b));
}
constexpr auto operator/(const AbstractMatrix auto &a, const auto &b) {
  return ElementwiseMatrixBinaryOp(Div{}, view(a), view(b));
}
constexpr auto operator/(const AbstractVector auto &a, const auto &b) {
  return ElementwiseVectorBinaryOp(Div{}, view(a), view(b));
}
constexpr auto operator/(Scalar auto a, const AbstractMatrix auto &b) {
  return ElementwiseMatrixBinaryOp(Div{}, view(a), view(b));
}
constexpr auto operator/(Scalar auto a, const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Div{}, view(a), view(b));
}
constexpr auto operator*(const AbstractMatrix auto &a,
                         const AbstractMatrix auto &b) {
  auto AA{a.view()};
  auto BB{b.view()};
  assert(size_t(AA.numCol()) == size_t(BB.numRow()));
  return MatMatMul<decltype(AA), decltype(BB)>{.a = AA, .b = BB};
}
constexpr auto operator*(const AbstractMatrix auto &a,
                         const AbstractVector auto &b) {
  auto AA{a.view()};
  auto BB{b.view()};
  assert(size_t(AA.numCol()) == BB.size());
  return MatVecMul<decltype(AA), decltype(BB)>{.a = AA, .b = BB};
}
constexpr auto operator*(const AbstractMatrix auto &a, std::integral auto b) {
  return ElementwiseMatrixBinaryOp(Mul{}, view(a), view(b));
}
constexpr auto operator*(const AbstractVector auto &a,
                         const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Mul{}, view(a), view(b));
}
constexpr auto operator*(const AbstractVector auto &a, std::integral auto b) {
  return ElementwiseVectorBinaryOp(Mul{}, view(a), view(b));
}
constexpr auto operator*(Scalar auto a, const AbstractMatrix auto &b) {
  return ElementwiseMatrixBinaryOp(Mul{}, view(a), view(b));
}
constexpr auto operator*(Scalar auto a, const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Mul{}, view(a), view(b));
}

// constexpr auto operator*(AbstractMatrix auto &A, AbstractVector auto &x) {
//     auto AA{A.view()};
//     auto xx{x.view()};
//     return MatMul<decltype(AA), decltype(xx)>{.a = AA, .b = xx};
// }
static_assert(
  AbstractMatrix<ElementwiseMatrixBinaryOp<Mul, PtrMatrix<int64_t>, int>>,
  "ElementwiseBinaryOp isa AbstractMatrix failed");

static_assert(
  !AbstractVector<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>,
  "MatMul should not be an AbstractVector!");
static_assert(AbstractMatrix<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>,
              "MatMul is not an AbstractMatrix!");
static_assert(AbstractMatrix<Transpose<PtrMatrix<int64_t>>>);

template <AbstractVector V>
constexpr auto operator*(const Transpose<V> &a, const AbstractVector auto &b) {
  typename V::eltype s = 0;
  for (size_t i = 0; i < b.size(); ++i) s += a.a(i) * b(i);
  return s;
}

static_assert(
  AbstractVector<decltype(-std::declval<StridedVector<int64_t>>())>);
static_assert(
  AbstractVector<decltype(-std::declval<StridedVector<int64_t>>() * 0)>);
static_assert(std::ranges::range<StridedVector<int64_t>>);

static_assert(AbstractVector<Vector<int64_t>>);
static_assert(AbstractVector<const Vector<int64_t>>);
static_assert(AbstractVector<Vector<int64_t> &>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(AbstractMatrix<IntMatrix &>);

static_assert(std::copyable<Matrix<int64_t, 4, 4>>);
static_assert(std::copyable<Matrix<int64_t, 4, 0>>);
static_assert(std::copyable<Matrix<int64_t, 0, 4>>);
static_assert(std::copyable<Matrix<int64_t, 0, 0>>);
static_assert(std::copyable<SquareMatrix<int64_t>>);

template <typename T>
concept DerivedMatrix =
  requires(T t, const T ct) {
    {
      t.data()
      } -> std::convertible_to<typename std::add_pointer_t<
        typename std::add_const_t<typename T::eltype>>>;
    {
      ct.data()
      } -> std::same_as<typename std::add_pointer_t<
        typename std::add_const_t<typename T::eltype>>>;
    { t.numRow() } -> std::convertible_to<Row>;
    { t.numCol() } -> std::convertible_to<Col>;
    { t.rowStride() } -> std::convertible_to<RowStride>;
  };
static_assert(DerivedMatrix<Matrix<int64_t, 4, 4>>);
static_assert(DerivedMatrix<Matrix<int64_t, 4, 0>>);
static_assert(DerivedMatrix<Matrix<int64_t, 0, 4>>);
static_assert(DerivedMatrix<Matrix<int64_t, 0, 0>>);
static_assert(DerivedMatrix<IntMatrix>);
static_assert(DerivedMatrix<IntMatrix>);
static_assert(DerivedMatrix<IntMatrix>);

static_assert(std::is_same_v<SquareMatrix<int64_t>::eltype, int64_t>);
static_assert(std::is_same_v<IntMatrix::eltype, int64_t>);

template <typename T, typename I> struct SliceView {
  using eltype = T;
  [[no_unique_address]] MutPtrVector<T> a;
  [[no_unique_address]] llvm::ArrayRef<I> i;
  struct Iterator {
    [[no_unique_address]] MutPtrVector<T> a;
    [[no_unique_address]] llvm::ArrayRef<I> i;
    [[no_unique_address]] size_t j;
    auto operator==(const Iterator &k) const -> bool { return j == k.j; }
    auto operator++() -> Iterator & {
      ++j;
      return *this;
    }
    auto operator*() -> T & { return a[i[j]]; }
    auto operator*() const -> const T & { return a[i[j]]; }
    auto operator->() -> T * { return &a[i[j]]; }
    auto operator->() const -> const T * { return &a[i[j]]; }
  };
  constexpr auto begin() -> Iterator { return Iterator{a, i, 0}; }
  constexpr auto end() -> Iterator { return Iterator{a, i, i.size()}; }
  auto operator[](size_t j) -> T & { return a[i[j]]; }
  auto operator[](size_t j) const -> const T & { return a[i[j]]; }
  [[nodiscard]] constexpr auto size() const -> size_t { return i.size(); }
  constexpr auto view() -> SliceView<T, I> { return *this; }
};

static_assert(AbstractVector<SliceView<int64_t, unsigned>>);

inline auto adaptOStream(std::ostream &os, const auto &x) -> std::ostream & {
  llvm::raw_os_ostream(os) << x;
  return os;
}
inline auto operator<<(std::ostream &os, const AbstractVector auto &x)
  -> std::ostream & {
  return adaptOStream(os, x);
}
inline auto operator<<(std::ostream &os, const AbstractMatrix auto &x)
  -> std::ostream & {
  return adaptOStream(os, x);
}

} // namespace LinearAlgebra

// exports:
// NOLINTNEXTLINE(bugprone-reserved-identifier)
using LinearAlgebra::_;
using LinearAlgebra::AbstractVector, LinearAlgebra::AbstractMatrix,
  LinearAlgebra::PtrVector, LinearAlgebra::MutPtrVector, LinearAlgebra::Vector,
  LinearAlgebra::Matrix, LinearAlgebra::SquareMatrix, LinearAlgebra::IntMatrix,
  LinearAlgebra::PtrMatrix, LinearAlgebra::MutPtrMatrix, LinearAlgebra::AxisInt,
  LinearAlgebra::AxisInt, LinearAlgebra::SmallSparseMatrix,
  LinearAlgebra::StridedVector, LinearAlgebra::MutStridedVector,
  LinearAlgebra::MutSquarePtrMatrix, LinearAlgebra::begin, LinearAlgebra::end,
  LinearAlgebra::swap, LinearAlgebra::SquarePtrMatrix, LinearAlgebra::Row,
  LinearAlgebra::RowStride, LinearAlgebra::Col, LinearAlgebra::CarInd,
  LinearAlgebra::last, LinearAlgebra::DenseMutPtrMatrix, LinearAlgebra::matrix,
  LinearAlgebra::identity;
