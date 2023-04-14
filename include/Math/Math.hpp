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
namespace LinAlg {

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
  constexpr auto operator()(size_t i, size_t j) const { return op(a(i, j)); }

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
  constexpr auto operator[](size_t i) const { return op(a[i]); }

  [[nodiscard]] constexpr auto size() const { return a.size(); }
  [[nodiscard]] constexpr auto view() const { return *this; };
};
// scalars broadcast
constexpr auto get(const auto &A, size_t) { return A; }
constexpr auto get(const auto &A, size_t, size_t) { return A; }
constexpr auto get(const AbstractVector auto &A, size_t i) { return A[i]; }
constexpr auto get(const AbstractMatrix auto &A, size_t i, size_t j) {
  return A(i, j);
}

constexpr auto size(const std::integral auto) -> size_t { return 1; }
constexpr auto size(const std::floating_point auto) -> size_t { return 1; }
constexpr auto size(const AbstractVector auto &x) -> size_t { return x.size(); }

static_assert(ElementOf<int, DenseMatrix<int64_t>>);
static_assert(ElementOf<int64_t, DenseMatrix<int64_t>>);
static_assert(ElementOf<int64_t, DenseMatrix<double>>);
static_assert(!ElementOf<DenseMatrix<double>, DenseMatrix<double>>);

template <typename T>
concept Trivial =
  std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>;

template <typename T>
concept HasConcreteSize = requires(T) {
  std::is_same_v<typename std::remove_reference_t<T>::concrete, std::true_type>;
};

static_assert(HasConcreteSize<DenseMatrix<int64_t>>);
static_assert(!HasConcreteSize<int64_t>);
static_assert(!HasConcreteSize<UniformScaling<std::true_type>>);
// template <typename T>
// using is_concrete_t =
//   std::conditional_t<HasConcreteSize<T>, std::true_type, std::false_type>;
template <typename T, typename U>
using is_concrete_t =
  std::conditional_t<HasConcreteSize<T> || HasConcreteSize<U>, std::true_type,
                     std::false_type>;

template <typename Op, Trivial A, Trivial B> struct ElementwiseVectorBinaryOp {
  using value_type = promote_eltype_t<A, B>;
  using concrete = is_concrete_t<A, B>;

  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  constexpr ElementwiseVectorBinaryOp(Op _op, A _a, B _b)
    : op(_op), a(_a), b(_b) {}
  constexpr auto operator[](size_t i) const { return op(get(a, i), get(b, i)); }
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
template <typename Op, Trivial A, Trivial B> struct ElementwiseMatrixBinaryOp {
  using value_type = promote_eltype_t<A, B>;
  using concrete = is_concrete_t<A, B>;
  [[no_unique_address]] Op op;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  constexpr ElementwiseMatrixBinaryOp(Op _op, A _a, B _b)
    : op(_op), a(_a), b(_b) {}
  constexpr auto operator()(size_t i, size_t j) const {
    return op(get(a, i, j), get(b, i, j));
  }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    // static_assert(AbstractMatrix<A> || std::integral<A> ||
    //                 std::floating_point<A>,
    //               "Argument A to elementwise binary op is not a matrix.");
    // static_assert(AbstractMatrix<B> || std::integral<B> ||
    //                 std::floating_point<B>,
    //               "Argument B to elementwise binary op is not a matrix.");
    if constexpr (AbstractMatrix<A> && AbstractMatrix<B>) {
      if constexpr (HasConcreteSize<A>) {
        if constexpr (HasConcreteSize<B>) {
          const Row N = a.numRow();
          invariant(N, b.numRow());
          return N;
        } else {
          return a.numRow();
        }
      } else if constexpr (HasConcreteSize<B>) {
        return b.numRow();
      } else {
        return 0;
      }
    } else if constexpr (AbstractMatrix<A>) {
      return a.numRow();
    } else if constexpr (AbstractMatrix<B>) {
      return b.numRow();
    }
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    // static_assert(AbstractMatrix<A> || std::integral<A> ||
    //                 std::floating_point<A>,
    //               "Argument A to elementwise binary op is not a matrix.");
    // static_assert(AbstractMatrix<B> || std::integral<B> ||
    //                 std::floating_point<B>,
    //               "Argument B to elementwise binary op is not a matrix.");
    if constexpr (AbstractMatrix<A> && AbstractMatrix<B>) {
      if constexpr (HasConcreteSize<A>) {
        if constexpr (HasConcreteSize<B>) {
          const Col N = a.numCol();
          invariant(N, b.numCol());
          return N;
        } else {
          return a.numCol();
        }
      } else if constexpr (HasConcreteSize<B>) {
        return b.numCol();
      } else {
        return 0;
      }
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
  using concrete = is_concrete_t<A, B>;
  [[no_unique_address]] A a;
  [[no_unique_address]] B b;
  constexpr auto operator()(size_t i, size_t j) const -> value_type {
    static_assert(AbstractMatrix<B>, "B should be an AbstractMatrix");
    value_type s{};
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
  using concrete = is_concrete_t<A, B>;
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
static_assert(Trivial<ElementwiseUnaryOp<Sub, StridedVector<int64_t>>>);

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
static_assert(Trivial<PtrMatrix<int64_t>>);
static_assert(Trivial<int>);
static_assert(TriviallyCopyable<Mul>);
static_assert(Trivial<ElementwiseMatrixBinaryOp<Mul, PtrMatrix<int64_t>, int>>);
static_assert(Trivial<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>);

template <TriviallyCopyable OP, Trivial A, Trivial B>
ElementwiseVectorBinaryOp(OP, A, B) -> ElementwiseVectorBinaryOp<OP, A, B>;
template <TriviallyCopyable OP, Trivial A, Trivial B>
ElementwiseMatrixBinaryOp(OP, A, B) -> ElementwiseMatrixBinaryOp<OP, A, B>;

inline constexpr auto view(const Trivial auto &x) { return x; }
inline constexpr auto view(const auto &x) { return x.view(); }

constexpr auto bin2(std::integral auto x) { return (x * (x - 1)) >> 1; }

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
constexpr auto operator*(const AbstractVector auto &a,
                         const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Mul{}, view(a), view(b));
}

template <AbstractVector M, ElementOf<M> S>
constexpr auto operator+(S a, const M &b) {
  return ElementwiseVectorBinaryOp(Add{}, view(a), view(b));
}
template <AbstractVector M, ElementOf<M> S>
constexpr auto operator+(const M &b, S a) {
  return ElementwiseVectorBinaryOp(Add{}, view(b), view(a));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator+(S a, const M &b) {
  return ElementwiseMatrixBinaryOp(Add{}, view(a), view(b));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator+(const M &b, S a) {
  return ElementwiseMatrixBinaryOp(Add{}, view(b), view(a));
}

template <AbstractVector M, ElementOf<M> S>
constexpr auto operator-(S a, const M &b) {
  return ElementwiseVectorBinaryOp(Sub{}, view(a), view(b));
}
template <AbstractVector M, ElementOf<M> S>
constexpr auto operator-(const M &b, S a) {
  return ElementwiseVectorBinaryOp(Sub{}, view(b), view(a));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator-(S a, const M &b) {
  return ElementwiseMatrixBinaryOp(Sub{}, view(a), view(b));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator-(const M &b, S a) {
  return ElementwiseMatrixBinaryOp(Sub{}, view(b), view(a));
}

template <AbstractVector M, ElementOf<M> S>
constexpr auto operator*(S a, const M &b) {
  return ElementwiseVectorBinaryOp(Mul{}, view(a), view(b));
}
template <AbstractVector M, ElementOf<M> S>
constexpr auto operator*(const M &b, S a) {
  return ElementwiseVectorBinaryOp(Mul{}, view(b), view(a));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator*(S a, const M &b) {
  return ElementwiseMatrixBinaryOp(Mul{}, view(a), view(b));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator*(const M &b, S a) {
  return ElementwiseMatrixBinaryOp(Mul{}, view(b), view(a));
}

template <AbstractVector M, ElementOf<M> S>
constexpr auto operator/(S a, const M &b) {
  return ElementwiseVectorBinaryOp(Div{}, view(a), view(b));
}
template <AbstractVector M, ElementOf<M> S>
constexpr auto operator/(const M &b, S a) {
  return ElementwiseVectorBinaryOp(Div{}, view(b), view(a));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator/(S a, const M &b) {
  return ElementwiseMatrixBinaryOp(Div{}, view(a), view(b));
}
template <AbstractMatrix M, ElementOf<M> S>
constexpr auto operator/(const M &b, S a) {
  return ElementwiseMatrixBinaryOp(Div{}, view(b), view(a));
}

constexpr auto operator+(const AbstractVector auto &a,
                         const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Add{}, view(a), view(b));
}
constexpr auto operator+(const AbstractMatrix auto &a,
                         const AbstractMatrix auto &b) {
  return ElementwiseMatrixBinaryOp(Add{}, view(a), view(b));
}
constexpr auto operator-(const AbstractVector auto &a,
                         const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Sub{}, view(a), view(b));
}
constexpr auto operator-(const AbstractMatrix auto &a,
                         const AbstractMatrix auto &b) {
  return ElementwiseMatrixBinaryOp(Sub{}, view(a), view(b));
}

constexpr auto operator/(const AbstractVector auto &a,
                         const AbstractVector auto &b) {
  return ElementwiseVectorBinaryOp(Div{}, view(a), view(b));
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

template <AbstractVector B> constexpr auto norm2(const B &A) {
  using T = typename B::value_type;
  T s = 0;
  for (size_t j = 0; j < A.numCol(); ++j) s += A(j) * A(j);
  return s;
}
template <AbstractMatrix B> constexpr auto norm2(const B &A) {
  using T = typename B::value_type;
  T s = 0;
  for (size_t i = 0; i < A.numRow(); ++i)
    for (size_t j = 0; j < A.numCol(); ++j) s += A(i, j) * A(i, j);
  return s;
}

// template <class T, size_t N> struct Zip {
//   std::array<T, N> a;
//   constexpr Zip(std::initializer_list<T> b) : a(b) {}
//   constexpr auto operator[](size_t i) -> T & { return a[i]; }
//   constexpr auto operator[](size_t i) const -> const T & { return a[i]; }
//   constexpr auto size() const -> size_t { return N; }
//   constexpr auto begin() -> typename std::array<T, N>::iterator {
//     return a.begin();
//   }
//   constexpr auto end() -> typename std::array<T, N>::iterator {
//     return a.end();
//   }
//   constexpr auto begin() const -> typename std::array<T, N>::const_iterator {
//     return a.begin();
//   }
//   constexpr auto end() const -> typename std::array<T, N>::const_iterator {
//     return a.end();
//   }

// };

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

} // namespace LinAlg

// exports:
// NOLINTNEXTLINE(bugprone-reserved-identifier)
using LinAlg::_;
using LinAlg::SmallSparseMatrix, LinAlg::StridedVector,
  LinAlg::MutStridedVector, LinAlg::MutSquarePtrMatrix, LinAlg::begin,
  LinAlg::end, LinAlg::SquarePtrMatrix, LinAlg::Row, LinAlg::RowStride,
  LinAlg::Col, LinAlg::CarInd, LinAlg::last, LinAlg::MutDensePtrMatrix,
  LinAlg::DensePtrMatrix, LinAlg::DenseMatrix;
