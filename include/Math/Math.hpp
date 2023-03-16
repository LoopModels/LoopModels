#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.

#include "Math/Array.hpp"
#include "Math/AxisTypes.hpp"
#include "Math/Indexing.hpp"
#include "Math/Matrix.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/Rational.hpp"
#include "TypePromotion.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <charconv>
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
#include <string_view>
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
  using value_type = typename A::value_type;
  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  auto operator()(size_t i, size_t j) const { return op(a(i, j)); }

  [[nodiscard]] constexpr auto size() const { return a.size(); }
  [[nodiscard]] constexpr auto dim() const { return a.dim(); }
  [[nodiscard]] constexpr auto numRow() const -> Row { return a.numRow(); }
  [[nodiscard]] constexpr auto numCol() const -> Col { return a.numCol(); }
  [[nodiscard]] constexpr auto view() const { return *this; };
};
template <typename Op, AbstractVector A> struct ElementwiseUnaryOp<Op, A> {
  using value_type = typename A::value_type;
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
  using value_type = promote_eltype_t<A, B>;
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
  using value_type = promote_eltype_t<A, B>;
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
  [[nodiscard]] constexpr auto size() const -> CartesianIndex<Row, Col> {
    return {numRow(), numCol()};
  }
  [[nodiscard]] constexpr auto dim() const -> DenseDims {
    return {numRow(), numCol()};
  }
  [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
};

template <AbstractMatrix A, AbstractMatrix B> struct MatMatMul {
  using value_type = promote_eltype_t<A, B>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  constexpr auto operator()(size_t i, size_t j) const -> value_type {
    static_assert(AbstractMatrix<B>, "B should be an AbstractMatrix");
    value_type s = 0;
    for (size_t k = 0; k < size_t(a.numCol()); ++k) s += a(i, k) * b(k, j);
    return s;
  }
  [[nodiscard]] constexpr auto numRow() const -> Row { return a.numRow(); }
  [[nodiscard]] constexpr auto numCol() const -> Col { return b.numCol(); }
  [[nodiscard]] constexpr auto size() const -> CartesianIndex<Row, Col> {
    invariant(size_t(a.numCol()) == size_t(b.numRow()));
    return {numRow(), numCol()};
  }
  [[nodiscard]] constexpr auto dim() const -> DenseDims {
    invariant(size_t(a.numCol()) == size_t(b.numRow()));
    return {numRow(), numCol()};
  }
  [[nodiscard]] constexpr auto view() const { return *this; };
  [[nodiscard]] constexpr auto transpose() const { return Transpose{*this}; };
};
template <AbstractMatrix A, AbstractVector B> struct MatVecMul {
  using value_type = promote_eltype_t<A, B>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  constexpr auto operator[](size_t i) const -> value_type {
    static_assert(AbstractVector<B>, "B should be an AbstractVector");
    value_type s = 0;
    for (size_t k = 0; k < a.numCol(); ++k) s += a(i, k) * b[k];
    return s;
  }
  [[nodiscard]] constexpr auto size() const -> size_t {
    return size_t(a.numRow());
  }
  constexpr auto view() const { return *this; };
};

//
// Vectors
//

template <class T, class S> constexpr auto view(const Array<T, S> &x) {
  return x;
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

inline auto printVectorImpl(llvm::raw_ostream &os, const AbstractVector auto &a)
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
inline auto printVector(llvm::raw_ostream &os, PtrVector<T> a)
  -> llvm::raw_ostream & {
  return printVectorImpl(os, a);
}
template <typename T>
inline auto printVector(llvm::raw_ostream &os, StridedVector<T> a)
  -> llvm::raw_ostream & {
  return printVectorImpl(os, a);
}
template <typename T>
inline auto printVector(llvm::raw_ostream &os,
                        const llvm::SmallVectorImpl<T> &a)
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

constexpr auto allMatch(const AbstractVector auto &x0,
                        const AbstractVector auto &x1) -> bool {
  size_t N = x0.size();
  if (N != x1.size()) return false;
  for (size_t n = 0; n < N; ++n)
    if (x0(n) != x1(n)) return false;
  return true;
}

constexpr void swap(MutPtrMatrix<int64_t> A, Row i, Row j) {
  if (i == j) return;
  Col N = A.numCol();
  invariant((i < A.numRow()) && (j < A.numRow()));
  for (Col n = 0; n < N; ++n) std::swap(A(i, n), A(j, n));
}
constexpr void swap(MutPtrMatrix<int64_t> A, Col i, Col j) {
  if (i == j) return;
  Row M = A.numRow();
  invariant((i < A.numCol()) && (j < A.numCol()));
  for (Row m = 0; m < M; ++m) std::swap(A(m, i), A(m, j));
}
template <typename T>
constexpr void swap(llvm::SmallVectorImpl<T> &A, Col i, Col j) {
  std::swap(A[i], A[j]);
}
template <typename T>
constexpr void swap(llvm::SmallVectorImpl<T> &A, Row i, Row j) {
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
static_assert(
  AbstractMatrix<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>);

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

template <std::integral T> struct MaxPow10 {
  static constexpr T value = (sizeof(T) == 1)   ? 3
                             : (sizeof(T) == 2) ? 5
                             : (sizeof(T) == 4)
                               ? 10
                               : (std::signed_integral<T> ? 19 : 20);
};
template <std::unsigned_integral T> constexpr auto countDigits(T x) {
  std::array<T, MaxPow10<T>::value + 1> powers;
  powers[0] = 0;
  powers[1] = 10;
  for (size_t i = 2; i < powers.size(); i++) powers[i] = powers[i - 1] * 10;
  std::array<T, sizeof(T) * 8 + 1> bits;
  if constexpr (sizeof(T) == 8) {
    bits = {1,  1,  1,  1,  2,  2,  2,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,
            6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9,  10, 10, 10, 10,
            11, 11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15, 15, 15, 16,
            16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 19, 20};
  } else if constexpr (sizeof(T) == 4) {
    bits = {1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4,  5,  5, 5,
            6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10};
  } else if constexpr (sizeof(T) == 2) {
    bits = {1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5};
  } else if constexpr (sizeof(T) == 1) {
    bits = {1, 1, 1, 1, 2, 2, 2, 3, 3};
  }
  T digits = bits[8 * sizeof(T) - std::countl_zero(x)];
  return digits - (x < powers[digits - 1]);
}
template <std::signed_integral T> constexpr auto countDigits(T x) {
  using U = std::make_unsigned_t<T>;
  if (x == std::numeric_limits<T>::min()) return U{sizeof(T) == 8 ? 20 : 11};
  return countDigits<U>(U(std::abs(x))) + U{x < 0};
}

constexpr auto countDigits(Rational x) -> size_t {
  size_t num = countDigits(x.numerator);
  return (x.denominator == 1) ? num : num + countDigits(x.denominator) + 2;
}

/// Returns the number of digits of the largest number in the matrix.
template <std::integral T>
constexpr auto getMaxDigits(PtrMatrix<T> A) -> Vector<T> {
  size_t M = size_t(A.numRow());
  size_t N = size_t(A.numCol());
  Vector<T> maxDigits{unsigned(N), T{}};
  invariant(size_t(maxDigits.size()), N);
  // first, we find the digits with the maximum value per column
  for (Row i = 0; i < M; i++) {
    for (size_t j = 0; j < N; j++) {
      // negative numbers need one more digit
      // first, we find the maximum value per column,
      // dividing positive numbers by -10
      T Aij = A(i, j);
      if constexpr (std::signed_integral<T>)
        maxDigits[j] = std::min(maxDigits[j], Aij > 0 ? Aij / -10 : Aij);
      else maxDigits[j] = std::max(maxDigits[j], Aij);
    }
  }
  // then, we count the digits of the maximum value per column
  for (size_t j = 0; j < maxDigits.size(); j++)
    maxDigits[j] = countDigits(maxDigits[j]);
  return maxDigits;
}
/// \brief Returns the maximum number of digits per column of a matrix.
constexpr auto getMaxDigits(PtrMatrix<Rational> A) -> Vector<size_t> {
  size_t M = size_t(A.numRow());
  size_t N = size_t(A.numCol());
  Vector<size_t> maxDigits{unsigned(N), 0};
  invariant(size_t(maxDigits.size()), N);
  // this is slow, because we count the digits of every element
  // we could optimize this by reducing the number of calls to countDigits
  for (Row i = 0; i < M; i++) {
    for (size_t j = 0; j < N; j++) {
      size_t c = countDigits(A(i, j));
      maxDigits[j] = std::max(maxDigits[j], c);
    }
  }
  return maxDigits;
}

template <typename T>
inline auto printMatrix(llvm::raw_ostream &os, PtrMatrix<T> A)
  -> llvm::raw_ostream & {
  // llvm::raw_ostream &printMatrix(llvm::raw_ostream &os, T const &A) {
  auto [M, N] = A.size();
  if ((!M) || (!N)) return os << "[ ]";
  // first, we determine the number of digits needed per column
  auto maxDigits{getMaxDigits(A)};
  using U = decltype(countDigits(std::declval<T>()));
  for (Row i = 0; i < M; i++) {
    if (i) os << "  ";
    else os << "\n[ ";
    for (size_t j = 0; j < N; j++) {
      auto Aij = A(i, j);
      for (U k = 0; k < U(maxDigits[j]) - countDigits(Aij); k++) os << " ";
      os << Aij;
      if (j != N - 1) os << " ";
      else if (i != M - 1) os << "\n";
    }
  }
  return os << " ]";
}
// We mirror `A` with a matrix of integers indicating sizes, and a vectors of
// chars. We fill the matrix with the number of digits of each element, and the
// vector with the characters of each element.
// We could use a vector of vectors of chars to avoid needing to copy memory on
// reallocation, but this would yield more complicated management.
// We should also generally be able to avoid allocations.
// We can use a Vector with a lot of initial capacity, and then resize based on
// a conservative estimate of the number of chars per elements.
inline auto printMatrix(llvm::raw_ostream &os, PtrMatrix<double> A)
  -> llvm::raw_ostream & {
  // llvm::raw_ostream &printMatrix(llvm::raw_ostream &os, T const &A) {
  auto [M, N] = A.size();
  if ((!M) || (!N)) return os << "[ ]";
  // first, we determine the number of digits needed per column
  Vector<char, 512> digits;
  digits.resizeForOverwrite(512);
  // we can't have more than 255 digits
  DenseMatrix<uint8_t> numDigits{DenseDims{M, N}};
  char *ptr = digits.begin();
  char *pEnd = digits.end();
  for (size_t m = 0; m < M; m++) {
    for (size_t n = 0; n < N; n++) {
      auto Aij = A(m, n);
      while (true) {
        auto [p, ec] = std::to_chars(ptr, pEnd, Aij);
        if (ec == std::errc()) [[likely]] {
          numDigits(m, n) = std::distance(ptr, p);
          ptr = p;
          break;
        }
        // we need more space
        size_t elemSoFar = m * size_t(N) + n;
        size_t charSoFar = std::distance(digits.begin(), ptr);
        // cld
        size_t charPerElem = (charSoFar + elemSoFar - 1) / elemSoFar;
        size_t newCapacity = (1 + charPerElem) * M * N; // +1 for good measure
        digits.resize(newCapacity);
        ptr = digits.begin() + charSoFar;
        pEnd = digits.end();
      }
    }
  }
  Vector<uint8_t> maxDigits;
  maxDigits.resizeForOverwrite(N);
  maxDigits << numDigits(0, _);
  for (size_t m = 0; m < M; m++)
    for (size_t n = 0; n < N; n++)
      maxDigits[n] = std::max(maxDigits[n], numDigits(m, n));

  ptr = digits.begin();
  // we will allocate 512 bytes at a time
  for (Row i = 0; i < M; i++) {
    if (i) os << "  ";
    else os << "\n[ ";
    for (size_t j = 0; j < N; j++) {
      size_t nD = numDigits(i, j);
      for (size_t k = 0; k < maxDigits[j] - nD; k++) os << " ";
      os << std::string_view(ptr, nD);
      if (j != N - 1) os << " ";
      else if (i != M - 1) os << "\n";
      ptr += nD;
    }
  }
  return os << " ]";
}

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
  Matrix<std::remove_const_t<typename T::value_type>> B{A};
  return printMatrix(os, PtrMatrix<typename T::value_type>(B));
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
static_assert(AbstractMatrix<Array<int64_t, SquareDims>>);
static_assert(AbstractMatrix<ManagedArray<int64_t, SquareDims>>);

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
  typename V::value_type s = 0;
  for (size_t i = 0; i < b.size(); ++i) s += a.a(i) * b(i);
  return s;
}

static_assert(
  AbstractVector<decltype(-std::declval<StridedVector<int64_t>>())>);
static_assert(
  AbstractVector<decltype(-std::declval<StridedVector<int64_t>>() * 0)>);
// static_assert(std::ranges::range<StridedVector<int64_t>>);

static_assert(AbstractVector<Vector<int64_t>>);
static_assert(AbstractVector<const Vector<int64_t>>);
static_assert(AbstractVector<Vector<int64_t> &>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(AbstractMatrix<IntMatrix &>);

static_assert(std::copyable<ManagedArray<int64_t, StridedDims>>);
static_assert(std::copyable<ManagedArray<int64_t, DenseDims>>);
static_assert(std::copyable<ManagedArray<int64_t, SquareDims>>);

template <typename T, typename I> struct SliceView {
  using value_type = T;
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
// inline auto operator<<(std::ostream &os, const AbstractVector auto &x)
//   -> std::ostream & {
//   return adaptOStream(os, x);
// }
// inline auto operator<<(std::ostream &os, const AbstractMatrix auto &x)
//   -> std::ostream & {
//   return adaptOStream(os, x);
// }
// inline auto operator<<(std::ostream &os, const DenseMatrix<int64_t> &x)
//   -> std::ostream & {
//   return adaptOStream(os, x);
// }

} // namespace LinearAlgebra

// exports:
// NOLINTNEXTLINE(bugprone-reserved-identifier)
using LinearAlgebra::_;
using LinearAlgebra::SmallSparseMatrix, LinearAlgebra::StridedVector,
  LinearAlgebra::MutStridedVector, LinearAlgebra::MutSquarePtrMatrix,
  LinearAlgebra::begin, LinearAlgebra::end, LinearAlgebra::swap,
  LinearAlgebra::SquarePtrMatrix, LinearAlgebra::Row, LinearAlgebra::RowStride,
  LinearAlgebra::Col, LinearAlgebra::CarInd, LinearAlgebra::last,
  LinearAlgebra::MutDensePtrMatrix, LinearAlgebra::DensePtrMatrix;
