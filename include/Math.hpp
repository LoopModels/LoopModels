#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.

#include "./TypePromotion.hpp"
#include "./Utilities.hpp"
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

inline auto allZero(const auto &x) -> bool {
  return std::all_of(x.begin(), x.end(), [](auto a) { return a == 0; });
  // return std::ranges::all_of(x, [](auto x) { return x == 0; });
}
inline auto allGEZero(const auto &x) -> bool {
  return std::all_of(x.begin(), x.end(), [](auto a) { return a >= 0; });
  // return std::ranges::all_of(x, [](auto x) { return x >= 0; });
}
inline auto allLEZero(const auto &x) -> bool {
  return std::all_of(x.begin(), x.end(), [](auto a) { return a <= 0; });
  // return std::ranges::all_of(x, [](auto x) { return x <= 0; });
}

inline auto countNonZero(const auto &x) -> size_t {
  return std::count_if(x.begin(), x.end(), [](auto a) { return a != 0; });
  // return std::ranges::count_if(x, [](auto x) { return x != 0; });
}

template <typename T>
concept AbstractVector =
  HasEltype<T> && requires(T t, size_t i) {
                    { t[i] } -> std::convertible_to<eltype_t<T>>;
                    { t.size() } -> std::convertible_to<size_t>;
                    { t.view() };
                    // {
                    //     std::remove_reference_t<T>::canResize
                    //     } -> std::same_as<const bool &>;
                    // {t.extendOrAssertSize(i)};
                  };

enum class AxisType {
  Row,
  Column,
  RowStride,
};
inline auto operator<<(llvm::raw_ostream &os, AxisType x)
  -> llvm::raw_ostream & {
  switch (x) {
  case AxisType::Row:
    return os << "Row";
  case AxisType::Column:
    return os << "Column";
  case AxisType::RowStride:
    return os << "RowStride";
  }
  llvm_unreachable("Unknown AxisType");
  return os;
}

// strong typing
template <AxisType T> struct AxisInt {
  using V = size_t;
  [[no_unique_address]] V value{0};
  // [[no_unique_address]] unsigned int value{0};
  constexpr AxisInt() = default;
  constexpr AxisInt(V v) : value(v) {}
  explicit constexpr operator size_t() const { return value; }
  explicit constexpr operator ptrdiff_t() const { return value; }
  explicit constexpr operator unsigned() const { return value; }
  explicit constexpr operator bool() const { return value; }

  constexpr auto operator+(V i) const -> AxisInt<T> { return value + i; }
  constexpr auto operator-(V i) const -> AxisInt<T> { return value - i; }
  constexpr auto operator*(V i) const -> AxisInt<T> { return value * i; }
  constexpr auto operator/(V i) const -> AxisInt<T> { return value / i; }
  constexpr auto operator%(V i) const -> AxisInt<T> { return value % i; }
  constexpr auto operator==(V i) const -> bool { return value == i; }
  constexpr auto operator!=(V i) const -> bool { return value != i; }
  constexpr auto operator<(V i) const -> bool { return value < i; }
  constexpr auto operator<=(V i) const -> bool { return value <= i; }
  constexpr auto operator>(V i) const -> bool { return value > i; }
  constexpr auto operator>=(V i) const -> bool { return value >= i; }
  constexpr auto operator++() -> AxisInt<T> & {
    ++value;
    return *this;
  }
  constexpr auto operator++(int) -> AxisInt<T> { return value++; }
  constexpr auto operator--() -> AxisInt<T> & {
    --value;
    return *this;
  }
  constexpr auto operator--(int) -> AxisInt<T> { return value--; }
  constexpr auto operator+=(AxisInt<T> i) -> AxisInt<T> & {
    value += V(i);
    return *this;
  }
  constexpr auto operator+=(V i) -> AxisInt<T> & {
    value += i;
    return *this;
  }
  constexpr auto operator-=(AxisInt<T> i) -> AxisInt<T> & {
    value -= V(i);
    return *this;
  }
  constexpr auto operator-=(V i) -> AxisInt<T> & {
    value -= i;
    return *this;
  }
  constexpr auto operator*=(AxisInt<T> i) -> AxisInt<T> & {
    value *= V(i);
    return *this;
  }
  constexpr auto operator*=(V i) -> AxisInt<T> & {
    value *= i;
    return *this;
  }
  constexpr auto operator/=(AxisInt<T> i) -> AxisInt<T> & {
    value /= V(i);
    return *this;
  }
  constexpr auto operator/=(V i) -> AxisInt<T> & {
    value /= i;
    return *this;
  }
  constexpr auto operator%=(AxisInt<T> i) -> AxisInt<T> & {
    value %= V(i);
    return *this;
  }
  constexpr auto operator%=(V i) -> AxisInt<T> & {
    value %= i;
    return *this;
  }
  constexpr auto operator*() const -> V { return value; }

  friend inline auto operator<<(llvm::raw_ostream &os, AxisInt<T> x)
    -> llvm::raw_ostream & {
    return os << T << "{" << *x << "}";
  }
};
template <typename T, AxisType W>
constexpr auto operator+(T *p, AxisInt<W> y) -> T * {
  return p + *y;
}
template <typename T, AxisType W>
constexpr auto operator-(T *p, AxisInt<W> y) -> T * {
  return p - *y;
}

template <AxisType T>
constexpr auto operator+(AxisInt<T> x, AxisInt<T> y) -> AxisInt<T> {
  return (*x) + (*y);
}
template <AxisType T>
constexpr auto operator-(AxisInt<T> x, AxisInt<T> y) -> AxisInt<T> {
  return (*x) - (*y);
}
template <AxisType T>
constexpr auto operator*(AxisInt<T> x, AxisInt<T> y) -> AxisInt<T> {
  return (*x) * (*y);
}
template <AxisType T>
constexpr auto operator/(AxisInt<T> x, AxisInt<T> y) -> AxisInt<T> {
  return (*x) / (*y);
}
template <AxisType T>
constexpr auto operator%(AxisInt<T> x, AxisInt<T> y) -> AxisInt<T> {
  return (*x) % (*y);
}
template <AxisType S, AxisType T>
constexpr auto operator==(AxisInt<S> x, AxisInt<T> y) -> bool {
  return *x == *y;
}
template <AxisType S, AxisType T>
constexpr auto operator!=(AxisInt<S> x, AxisInt<T> y) -> bool {
  return *x != *y;
}
template <AxisType T>
constexpr auto operator<(AxisInt<T> x, AxisInt<T> y) -> bool {
  return *x < *y;
}
template <AxisType T>
constexpr auto operator<=(AxisInt<T> x, AxisInt<T> y) -> bool {
  return *x <= *y;
}
template <AxisType T>
constexpr auto operator>(AxisInt<T> x, AxisInt<T> y) -> bool {
  return *x > *y;
}
template <AxisType T>
constexpr auto operator>=(AxisInt<T> x, AxisInt<T> y) -> bool {
  return *x >= *y;
}
using Col = AxisInt<AxisType::Column>;
using Row = AxisInt<AxisType::Row>;
using RowStride = AxisInt<AxisType::RowStride>;
using CarInd = std::pair<Row, Col>;

constexpr auto operator*(RowStride x, Row y) -> size_t { return (*x) * (*y); }
constexpr auto operator>=(RowStride x, Col u) -> bool { return (*x) >= (*u); }
constexpr auto operator<(RowStride x, Col u) -> bool { return (*x) < (*u); }

static_assert(sizeof(Row) == sizeof(size_t));
static_assert(sizeof(Col) == sizeof(size_t));
static_assert(sizeof(RowStride) == sizeof(size_t));
constexpr auto operator*(Row r, Col c) -> Row::V { return *r * *c; }

constexpr auto operator<(size_t x, Row y) -> bool { return x < size_t(y); }
constexpr auto operator<(size_t x, Col y) -> bool { return x < size_t(y); }
constexpr auto operator>(size_t x, Row y) -> bool { return x > size_t(y); }
constexpr auto operator>(size_t x, Col y) -> bool { return x > size_t(y); }

constexpr auto operator+(size_t x, Col y) -> Col { return Col{x + size_t(y)}; }
constexpr auto operator-(size_t x, Col y) -> Col { return Col{x - size_t(y)}; }
constexpr auto operator*(size_t x, Col y) -> Col { return Col{x * size_t(y)}; }
constexpr auto operator+(size_t x, Row y) -> Row { return Row{x + size_t(y)}; }
constexpr auto operator-(size_t x, Row y) -> Row { return Row{x - size_t(y)}; }
constexpr auto operator*(size_t x, Row y) -> Row { return Row{x * size_t(y)}; }
constexpr auto operator+(size_t x, RowStride y) -> RowStride {
  return RowStride{x + size_t(y)};
}
constexpr auto operator-(size_t x, RowStride y) -> RowStride {
  return RowStride{x - size_t(y)};
}
constexpr auto operator*(size_t x, RowStride y) -> RowStride {
  return RowStride{x * size_t(y)};
}

constexpr auto max(Col N, RowStride X) -> RowStride {
  return RowStride{std::max(size_t(N), size_t(X))};
}
constexpr auto min(Col N, Col X) -> Col {
  return Col{std::max(Col::V(N), Col::V(X))};
}
constexpr auto min(Row N, Col X) -> size_t {
  return std::min(size_t(N), size_t(X));
}

template <typename T>
concept RowOrCol = std::same_as<T, Row> || std::same_as<T, Col>;

template <typename T>
concept AbstractMatrixCore =
  HasEltype<T> && requires(T t, size_t i) {
                    { t(i, i) } -> std::convertible_to<eltype_t<T>>;
                    { t.numRow() } -> std::same_as<Row>;
                    { t.numCol() } -> std::same_as<Col>;
                    { t.size() } -> std::same_as<std::pair<Row, Col>>;
                    // {
                    //     std::remove_reference_t<T>::canResize
                    //     } -> std::same_as<const bool &>;
                    // {t.extendOrAssertSize(i, i)};
                  };
template <typename T>
concept AbstractMatrix = AbstractMatrixCore<T> && requires(T t, size_t i) {
                                                    {
                                                      t.view()
                                                      } -> AbstractMatrixCore;
                                                  };
template <typename T>
concept AbstractRowMajorMatrix =
  AbstractMatrix<T> && requires(T t) {
                         { t.rowStride() } -> std::same_as<RowStride>;
                       };

inline auto copyto(AbstractVector auto &y, const AbstractVector auto &x)
  -> auto & {
  const size_t M = x.size();
  y.extendOrAssertSize(M);
  for (size_t i = 0; i < M; ++i) y[i] = x[i];
  return y;
}
inline auto copyto(AbstractMatrixCore auto &A, const AbstractMatrixCore auto &B)
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
};
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

static inline constexpr struct Begin {
  friend inline auto operator<<(llvm::raw_ostream &os, Begin)
    -> llvm::raw_ostream & {
    return os << 0;
  }
} begin;
static inline constexpr struct End {
  friend inline auto operator<<(llvm::raw_ostream &os, End)
    -> llvm::raw_ostream & {
    return os << "end";
  }
} end;
struct OffsetBegin {
  [[no_unique_address]] size_t offset;
  friend inline auto operator<<(llvm::raw_ostream &os, OffsetBegin r)
    -> llvm::raw_ostream & {
    return os << r.offset;
  }
};
// FIXME: we currently lose strong typing of Row and Col when using relative
// indexing; we should preserve it, perhaps within the OffsetBegin row/struct,
// making them templated?
template <typename T>
concept ScalarValueIndex =
  std::integral<T> || std::same_as<T, Row> || std::same_as<T, Col>;

constexpr auto operator+(ScalarValueIndex auto x, Begin) -> OffsetBegin {
  return OffsetBegin{size_t(x)};
}
constexpr auto operator+(Begin, ScalarValueIndex auto x) -> OffsetBegin {
  return OffsetBegin{size_t(x)};
}
constexpr auto operator+(ScalarValueIndex auto x, OffsetBegin y)
  -> OffsetBegin {
  return OffsetBegin{size_t(x) + y.offset};
}
inline auto operator+(OffsetBegin y, ScalarValueIndex auto x) -> OffsetBegin {
  return OffsetBegin{size_t(x) + y.offset};
}
struct OffsetEnd {
  [[no_unique_address]] size_t offset;
  friend inline auto operator<<(llvm::raw_ostream &os, OffsetEnd r)
    -> llvm::raw_ostream & {
    return os << "end - " << r.offset;
  }
};
constexpr auto operator-(End, ScalarValueIndex auto x) -> OffsetEnd {
  return OffsetEnd{size_t(x)};
}
constexpr auto operator-(OffsetEnd y, ScalarValueIndex auto x) -> OffsetEnd {
  return OffsetEnd{y.offset + size_t(x)};
}
constexpr auto operator+(OffsetEnd y, ScalarValueIndex auto x) -> OffsetEnd {
  return OffsetEnd{y.offset - size_t(x)};
}

template <typename T>
concept RelativeOffset = std::same_as<T, End> || std::same_as<T, OffsetEnd> ||
                         std::same_as<T, Begin> || std::same_as<T, OffsetBegin>;

template <typename B, typename E> struct Range {
  [[no_unique_address]] B b;
  [[no_unique_address]] E e;
};
template <std::integral B, std::integral E> struct Range<B, E> {
  [[no_unique_address]] B b;
  [[no_unique_address]] E e;
  // wrapper that allows dereferencing
  struct Iterator {
    [[no_unique_address]] B i;
    constexpr auto operator==(E other) -> bool { return i == other; }
    auto operator++() -> Iterator & {
      ++i;
      return *this;
    }
    auto operator++(int) -> Iterator { return Iterator{i++}; }
    auto operator--() -> Iterator & {
      --i;
      return *this;
    }
    auto operator--(int) -> Iterator { return Iterator{i--}; }
    auto operator*() -> B { return i; }
  };
  [[nodiscard]] constexpr auto begin() const -> Iterator { return Iterator{b}; }
  [[nodiscard]] constexpr auto end() const -> E { return e; }
  [[nodiscard]] constexpr auto rbegin() const -> Iterator {
    return std::reverse_iterator{end()};
  }
  [[nodiscard]] constexpr auto rend() const -> E {
    return std::reverse_iterator{begin()};
  }
  [[nodiscard]] constexpr auto size() const { return e - b; }
  friend inline auto operator<<(llvm::raw_ostream &os, Range<B, E> r)
    -> llvm::raw_ostream & {
    return os << "[" << r.b << ":" << r.e << ")";
  }
  template <std::integral BB, std::integral EE>
  constexpr operator Range<BB, EE>() const {
    return Range<BB, EE>{static_cast<BB>(b), static_cast<EE>(e)};
  }
};

template <typename T> struct StandardizeRangeBound {
  using type = T;
};
template <RowOrCol T> struct StandardizeRangeBound<T> {
  using type = size_t;
};
template <std::unsigned_integral T> struct StandardizeRangeBound<T> {
  using type = size_t;
};
template <std::signed_integral T> struct StandardizeRangeBound<T> {
  using type = ptrdiff_t;
};
template <typename T>
using StandardizeRangeBound_t = typename StandardizeRangeBound<T>::type;

constexpr auto standardizeRangeBound(auto x) { return x; }
constexpr auto standardizeRangeBound(RowOrCol auto x) { return size_t(x); }

constexpr auto standardizeRangeBound(std::unsigned_integral auto x) {
  return size_t(x);
}
constexpr auto standardizeRangeBound(std::signed_integral auto x) {
  return ptrdiff_t(x);
}

template <typename B, typename E>
Range(B b, E e) -> Range<decltype(standardizeRangeBound(b)),
                         decltype(standardizeRangeBound(e))>;

static inline constexpr struct Colon {
  [[nodiscard]] inline constexpr auto operator()(auto B, auto E) const {
    return Range{standardizeRangeBound(B), standardizeRangeBound(E)};
  }
} _; // NOLINT(bugprone-reserved-identifier)

#ifndef NDEBUG
static inline void checkIndex(size_t X, size_t x) { assert(x < X); }
inline void checkIndex(size_t X, End) { assert(X > 0); }
inline void checkIndex(size_t X, Begin) { assert(X > 0); }
inline void checkIndex(size_t X, OffsetEnd x) { assert(x.offset < X); }
inline void checkIndex(size_t X, OffsetBegin x) { assert(x.offset < X); }
template <typename B> inline void checkIndex(size_t X, Range<B, size_t> x) {
  assert(x.e <= X);
}
template <typename B, typename E> inline void checkIndex(size_t, Range<B, E>) {}
inline void checkIndex(size_t, Colon) {}
#endif

constexpr auto canonicalize(size_t e, size_t) -> size_t { return e; }
constexpr auto canonicalize(Begin, size_t) -> size_t { return 0; }
constexpr auto canonicalize(OffsetBegin b, size_t) -> size_t {
  return b.offset;
}
constexpr auto canonicalize(End, size_t M) -> size_t { return M - 1; }
constexpr auto canonicalize(OffsetEnd e, size_t M) -> size_t {
  return M - 1 - e.offset;
}

constexpr auto canonicalizeForRange(size_t e, size_t) -> size_t { return e; }
constexpr auto canonicalizeForRange(Begin, size_t) -> size_t { return 0; }
constexpr auto canonicalizeForRange(OffsetBegin b, size_t) -> size_t {
  return b.offset;
}
constexpr auto canonicalizeForRange(End, size_t M) -> size_t { return M; }
constexpr auto canonicalizeForRange(OffsetEnd e, size_t M) -> size_t {
  return M - e.offset;
}

// Union type
template <typename T>
concept ScalarRelativeIndex =
  std::same_as<T, End> || std::same_as<T, Begin> ||
  std::same_as<T, OffsetBegin> || std::same_as<T, OffsetEnd>;

template <typename T>
concept ScalarIndex = std::integral<T> || ScalarRelativeIndex<T>;

template <typename B, typename E>
constexpr auto canonicalizeRange(Range<B, E> r, size_t M)
  -> Range<size_t, size_t> {
  return Range<size_t, size_t>{canonicalizeForRange(r.b, M),
                               canonicalizeForRange(r.e, M)};
}
constexpr auto canonicalizeRange(Colon, size_t M) -> Range<size_t, size_t> {
  return Range<size_t, size_t>{0, M};
}

template <typename B, typename E>
constexpr auto operator+(Range<B, E> r, size_t x) {
  return _(r.b + x, r.e + x);
}
template <typename B, typename E>
constexpr auto operator-(Range<B, E> r, size_t x) {
  return _(r.b - x, r.e - x);
}

template <typename T> struct PtrVector {
  static_assert(!std::is_const_v<T>, "const T is redundant");
  using eltype = T;
  [[no_unique_address]] NotNull<const T> mem;
  [[no_unique_address]] const size_t N;
  auto operator==(AbstractVector auto &x) -> bool {
    if (N != x.size()) return false;
    for (size_t n = 0; n < N; ++n)
      if (mem[n] != x(n)) return false;
    return true;
  }
  [[nodiscard]] constexpr auto front() const -> const T & { return mem[0]; }
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) const
    -> const T & {
#ifndef NDEBUG
    checkIndex(size_t(N), i);
#endif
    return mem[canonicalize(i, N)];
  }
  [[gnu::flatten]] constexpr auto operator[](Range<size_t, size_t> i) const
    -> PtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= N);
    return PtrVector<T>{mem + i.b, i.e - i.b};
  }
  template <typename F, typename L>
  [[gnu::flatten]] constexpr auto operator[](Range<F, L> i) const
    -> PtrVector<T> {
    return (*this)[canonicalizeRange(i, N)];
  }
  [[gnu::returns_nonnull]] [[nodiscard]] constexpr auto begin() const
    -> const T * {
    return mem;
  }
  [[gnu::returns_nonnull]] [[nodiscard]] constexpr auto end() const
    -> const T * {
    return mem + N;
  }
  [[nodiscard]] constexpr auto rbegin() const {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() const {
    return std::reverse_iterator(begin());
  }
  [[nodiscard]] constexpr auto size() const -> size_t { return N; }
  constexpr operator llvm::ArrayRef<T>() const {
    return llvm::ArrayRef<T>{mem, N};
  }
  // llvm::ArrayRef<T> arrayref() const { return llvm::ArrayRef<T>(ptr, M); }
  auto operator==(const PtrVector<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
  }
  auto operator==(const llvm::ArrayRef<std::remove_const_t<T>> x) const
    -> bool {
    return llvm::ArrayRef<std::remove_const_t<T>>(*this) == x;
  }
  [[nodiscard]] constexpr auto view() const -> PtrVector<T> { return *this; };
#ifndef NDEBUG
  void extendOrAssertSize(size_t M) const { assert(M == N); }
#else
  // two defs to avoid unused parameter compiler warning in release builds
  static constexpr void extendOrAssertSize(size_t) {}
#endif
  constexpr PtrVector(NotNull<const T> pt, size_t NN) : mem(pt), N(NN) {}
  PtrVector(llvm::ArrayRef<T> x) : mem(x.data()), N(x.size()) {}
};
template <typename T> struct MutPtrVector {
  static_assert(!std::is_const_v<T>, "T shouldn't be const");
  using eltype = T;
  // using eltype = std::remove_const_t<T>;
  [[no_unique_address]] NotNull<T> mem;
  [[no_unique_address]] const size_t N;
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) -> T & {
#ifndef NDEBUG
    checkIndex(size_t(N), i);
#endif
    return mem[canonicalize(i, N)];
  }
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) const
    -> const T & {
#ifndef NDEBUG
    checkIndex(size_t(N), i);
#endif
    return mem[canonicalize(i, N)];
  }
  [[nodiscard]] auto front() -> T & {
    assert(N > 0);
    return mem[0];
  }
  [[nodiscard]] auto back() -> T & {
    assert(N > 0);
    return mem[N - 1];
  }
  [[nodiscard]] auto front() const -> const T & {
    assert(N > 0);
    return mem[0];
  }
  [[nodiscard]] auto back() const -> const T & {
    assert(N > 0);
    return mem[N - 1];
  }
  [[nodiscard]] constexpr auto isEmpty() const -> bool { return N == 0; }
  // copy constructor
  constexpr MutPtrVector() = default;
  constexpr MutPtrVector(const MutPtrVector<T> &x) = default;
  constexpr MutPtrVector(llvm::MutableArrayRef<T> x)
    : mem(x.data()), N(x.size()) {}
  constexpr MutPtrVector(T *pt, size_t NN) : mem(pt), N(NN) {}
  constexpr auto operator[](Range<size_t, size_t> i) -> MutPtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= N);
    return MutPtrVector<T>{mem + i.b, i.e - i.b};
  }
  constexpr auto operator[](Range<size_t, size_t> i) const -> PtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= N);
    return PtrVector<T>{mem + i.b, i.e - i.b};
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) -> MutPtrVector<T> {
    return (*this)[canonicalizeRange(i, N)];
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) const -> PtrVector<T> {
    return (*this)[canonicalizeRange(i, N)];
  }
  [[gnu::returns_nonnull]] constexpr auto begin() -> T * { return mem; }
  [[gnu::returns_nonnull]] constexpr auto end() -> T * { return mem + N; }
  [[gnu::returns_nonnull]] [[nodiscard]] constexpr auto begin() const
    -> const T * {
    return mem;
  }
  [[gnu::returns_nonnull]] [[nodiscard]] constexpr auto end() const
    -> const T * {
    return mem + N;
  }
  [[nodiscard]] constexpr auto size() const -> size_t { return N; }
  constexpr operator PtrVector<T>() const { return PtrVector<T>{mem, N}; }
  constexpr operator llvm::ArrayRef<T>() const {
    return llvm::ArrayRef<T>{mem, N};
  }
  constexpr operator llvm::MutableArrayRef<T>() {
    return llvm::MutableArrayRef<T>{mem, N};
  }
  // llvm::ArrayRef<T> arrayref() const { return llvm::ArrayRef<T>(ptr, M); }
  auto operator==(const MutPtrVector<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
  }
  auto operator==(const PtrVector<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
  }
  auto operator==(const llvm::ArrayRef<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == x;
  }
  [[nodiscard]] constexpr auto view() const -> PtrVector<T> { return *this; };
  [[gnu::flatten]] auto operator=(PtrVector<T> x) -> MutPtrVector<T> {
    return copyto(*this, x);
  }
  [[gnu::flatten]] auto operator=(MutPtrVector<T> x) -> MutPtrVector<T> {
    return copyto(*this, x);
  }
  [[gnu::flatten]] auto operator=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    return copyto(*this, x);
  }
  [[gnu::flatten]] auto operator=(std::integral auto x) -> MutPtrVector<T> {
    for (auto &&y : *this) y = x;
    return *this;
  }
  [[gnu::flatten]] auto operator+=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(N == x.size());
    for (size_t i = 0; i < N; ++i) mem[i] += x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(N == x.size());
    for (size_t i = 0; i < N; ++i) mem[i] -= x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(N == x.size());
    for (size_t i = 0; i < N; ++i) mem[i] *= x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(N == x.size());
    for (size_t i = 0; i < N; ++i) mem[i] /= x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator+=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < N; ++i) mem[i] += x;
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < N; ++i) mem[i] -= x;
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < N; ++i) mem[i] *= x;
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < N; ++i) mem[i] /= x;
    return *this;
  }
#ifndef NDEBUG
  void extendOrAssertSize(size_t M) const { assert(M == N); }
#else
  static constexpr void extendOrAssertSize(size_t) {}
#endif
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

template <typename T> struct Vector {
  using eltype = T;
  [[no_unique_address]] llvm::SmallVector<T, 16> data;

  Vector(int N) : data(llvm::SmallVector<T>(N)){};
  Vector(size_t N = 0) : data(llvm::SmallVector<T>(N)){};
  Vector(llvm::SmallVector<T> A) : data(std::move(A)){};

  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) -> T & {
    return data[canonicalize(i, data.size())];
  }
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) const
    -> const T & {
    return data[canonicalize(i, data.size())];
  }
  [[gnu::flatten]] constexpr auto operator[](Range<size_t, size_t> i)
    -> MutPtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= data.size());
    return MutPtrVector<T>{data.data() + i.b, i.e - i.b};
  }
  [[gnu::flatten]] constexpr auto operator[](Range<size_t, size_t> i) const
    -> PtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= data.size());
    return PtrVector<T>{data.data() + i.b, i.e - i.b};
  }
  template <typename F, typename L>
  [[gnu::flatten]] constexpr auto operator[](Range<F, L> i) -> MutPtrVector<T> {
    return (*this)[canonicalizeRange(i, data.size())];
  }
  template <typename F, typename L>
  [[gnu::flatten]] constexpr auto operator[](Range<F, L> i) const
    -> PtrVector<T> {
    return (*this)[canonicalizeRange(i, data.size())];
  }
  [[gnu::flatten]] constexpr auto operator[](size_t i) -> T & {
    return data[i];
  }
  [[gnu::flatten]] constexpr auto operator[](size_t i) const -> const T & {
    return data[i];
  }
  // bool operator==(Vector<T, 0> x0) const { return allMatch(*this, x0); }
  constexpr auto begin() { return data.begin(); }
  constexpr auto end() { return data.end(); }
  [[nodiscard]] constexpr auto begin() const { return data.begin(); }
  [[nodiscard]] constexpr auto end() const { return data.end(); }
  [[nodiscard]] constexpr auto size() const -> size_t { return data.size(); }
  [[nodiscard]] constexpr auto view() const -> PtrVector<T> {
    return PtrVector<T>{data.data(), data.size()};
  };
  template <typename A> void push_back(A &&x) {
    data.push_back(std::forward<A>(x));
  }
  template <typename... A> void emplace_back(A &&...x) {
    data.emplace_back(std::forward<A>(x)...);
  }
  Vector(const AbstractVector auto &x) : data(llvm::SmallVector<T>{}) {
    const size_t N = x.size();
    data.resize_for_overwrite(N);
    for (size_t n = 0; n < N; ++n) data[n] = x[n];
  }
  void resize(size_t N) { data.resize(N); }
  void resizeForOverwrite(size_t N) { data.resize_for_overwrite(N); }

  constexpr operator MutPtrVector<T>() {
    return MutPtrVector<T>{data.data(), data.size()};
  }
  constexpr operator PtrVector<T>() const {
    return PtrVector<T>{data.data(), data.size()};
  }
  constexpr operator llvm::MutableArrayRef<T>() {
    return llvm::MutableArrayRef<T>{data.data(), data.size()};
  }
  constexpr operator llvm::ArrayRef<T>() const {
    return llvm::ArrayRef<T>{data.data(), data.size()};
  }
  // MutPtrVector<T> operator=(AbstractVector auto &x) {
  auto operator=(const T &x) -> Vector<T> & {
    MutPtrVector<T> y{*this};
    y = x;
    return *this;
  }
  auto operator=(AbstractVector auto &x) -> Vector<T> & {
    MutPtrVector<T> y{*this};
    y = x;
    return *this;
  }
  auto operator+=(AbstractVector auto &x) -> Vector<T> & {
    MutPtrVector<T> y{*this};
    y += x;
    return *this;
  }
  auto operator-=(AbstractVector auto &x) -> Vector<T> & {
    MutPtrVector<T> y{*this};
    y -= x;
    return *this;
  }
  auto operator*=(AbstractVector auto &x) -> Vector<T> & {
    MutPtrVector<T> y{*this};
    y *= x;
    return *this;
  }
  auto operator/=(AbstractVector auto &x) -> Vector<T> & {
    MutPtrVector<T> y{*this};
    y /= x;
    return *this;
  }
  auto operator+=(const std::integral auto x) -> Vector<T> & {
    for (auto &&y : data) y += x;
    return *this;
  }
  auto operator-=(const std::integral auto x) -> Vector<T> & {
    for (auto &&y : data) y -= x;
    return *this;
  }
  auto operator*=(const std::integral auto x) -> Vector<T> & {
    for (auto &&y : data) y *= x;
    return *this;
  }
  auto operator/=(const std::integral auto x) -> Vector<T> & {
    for (auto &&y : data) y /= x;
    return *this;
  }
  template <typename... Ts> Vector(Ts... inputs) : data{inputs...} {}
  void clear() { data.clear(); }
#ifndef NDEBUG
  void extendOrAssertSize(size_t N) const { assert(N == data.size()); }
#else
  constexpr void extendOrAssertSize(size_t) const {}
#endif
  void extendOrAssertSize(size_t N) {
    if (N != data.size()) data.resize_for_overwrite(N);
  }
  auto operator==(const Vector<T> &x) const -> bool {
    return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
  }
  void pushBack(T x) { data.push_back(std::move(x)); }
};

static_assert(std::copyable<Vector<intptr_t>>);
static_assert(AbstractVector<Vector<int64_t>>);
static_assert(!AbstractVector<int64_t>);

template <typename T> struct StridedVector {
  static_assert(!std::is_const_v<T>, "const T is redundant");
  using eltype = T;
  [[no_unique_address]] const T *d;
  [[no_unique_address]] size_t N;
  [[no_unique_address]] RowStride x;
  struct StridedIterator {
    using value_type = const T;
    [[no_unique_address]] const T *d;
    [[no_unique_address]] size_t xx;
    constexpr auto operator++() -> StridedIterator & {
      d += xx;
      return *this;
    }
    constexpr auto operator--() -> StridedIterator & {
      d -= xx;
      return *this;
    }
    constexpr auto operator++(int) {
      auto tmp = *this;
      d += xx;
      return tmp;
    }
    constexpr auto operator--(int) {
      auto tmp = *this;
      d -= xx;
      return tmp;
    }
    auto operator[](ptrdiff_t y) const -> const T & { return d[y * xx]; }
    auto operator-(StridedIterator y) const -> ptrdiff_t {
      return (d - y.d) / xx;
    }
    auto operator+(ptrdiff_t y) const -> StridedIterator {
      return {d + y * xx, xx};
    }
    auto operator-(ptrdiff_t y) const -> StridedIterator {
      return {d + y * xx, xx};
    }
    auto operator+=(ptrdiff_t y) -> StridedIterator & {
      d += y * xx;
      return *this;
    }
    auto operator-=(ptrdiff_t y) -> StridedIterator & {
      d -= y * xx;
      return *this;
    }
    constexpr auto operator*() const -> const T & { return *d; }
    constexpr auto operator->() const -> const T * { return d; }
    constexpr auto operator==(const StridedIterator y) const -> bool {
      return d == y.d;
    }
    constexpr auto operator!=(const StridedIterator y) const -> bool {
      return d != y.d;
    }
    constexpr auto operator>(const StridedIterator y) const -> bool {
      return d > y.d;
    }
    constexpr auto operator<(const StridedIterator y) const -> bool {
      return d < y.d;
    }
    constexpr auto operator>=(const StridedIterator y) const -> bool {
      return d >= y.d;
    }
    constexpr auto operator<=(const StridedIterator y) const -> bool {
      return d <= y.d;
    }
    friend auto operator+(ptrdiff_t y,
                          typename StridedVector<T>::StridedIterator a) ->
      typename StridedVector<T>::StridedIterator {
      return {a.d + y * a.xx, a.xx};
    }
  };
  [[nodiscard]] constexpr auto begin() const {
    return StridedIterator{d, size_t(x)};
  }
  [[nodiscard]] constexpr auto end() const {
    return StridedIterator{d + x * N, size_t(x)};
  }
  [[nodiscard]] constexpr auto rbegin() const {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() const {
    return std::reverse_iterator(begin());
  }
  // [[nodiscard]] constexpr auto begin() const {
  //   return std::ranges::stride_view{llvm::ArrayRef<T>{d, N}, x};
  // }
  constexpr auto operator[](size_t i) const -> const T & {
    return d[size_t(x * i)];
  }

  constexpr auto operator[](Range<size_t, size_t> i) const -> StridedVector<T> {
    return StridedVector<T>{.d = d + x * i.b, .N = i.e - i.b, .x = x};
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) const -> StridedVector<T> {
    return (*this)[canonicalizeRange(i, N)];
  }

  [[nodiscard]] constexpr auto size() const -> size_t { return N; }
  auto operator==(StridedVector<T> a) const -> bool {
    if (size() != a.size()) return false;
    for (size_t i = 0; i < size(); ++i)
      if ((*this)[i] != a[i]) return false;
    return true;
  }
  [[nodiscard]] constexpr auto view() const -> StridedVector<T> {
    return *this;
  }
#ifndef NDEBUG
  void extendOrAssertSize(size_t M) const { assert(N == M); }
#else
  static constexpr void extendOrAssertSize(size_t) {}
#endif
};
template <typename T> struct MutStridedVector {
  static_assert(!std::is_const_v<T>, "T should not be const");
  using eltype = T;
  [[no_unique_address]] T *const d;
  [[no_unique_address]] const size_t N;
  [[no_unique_address]] RowStride x;
  struct StridedIterator {
    using value_type = T;

    [[no_unique_address]] T *d;
    [[no_unique_address]] size_t xx;
    constexpr auto operator++() -> StridedIterator & {
      d += xx;
      return *this;
    }
    constexpr auto operator--() -> StridedIterator & {
      d -= xx;
      return *this;
    }
    constexpr auto operator++(int) {
      auto tmp = *this;
      d += xx;
      return tmp;
    }
    constexpr auto operator--(int) {
      auto tmp = *this;
      d -= xx;
      return tmp;
    }
    auto operator[](ptrdiff_t y) const -> T & { return d[y * xx]; }
    auto operator-(StridedIterator y) const -> ptrdiff_t {
      return (d - y.d) / xx;
    }
    auto operator+(ptrdiff_t y) const -> StridedIterator {
      return {d + y * xx, xx};
    }
    auto operator-(ptrdiff_t y) const -> StridedIterator {
      return {d + y * xx, xx};
    }
    auto operator+=(ptrdiff_t y) -> StridedIterator & {
      d += y * xx;
      return *this;
    }
    auto operator-=(ptrdiff_t y) -> StridedIterator & {
      d -= y * xx;
      return *this;
    }
    constexpr auto operator->() const -> T * { return d; }
    constexpr auto operator*() const -> T & { return *d; }
    // constexpr auto operator->() -> T * { return d; }
    // constexpr auto operator*() -> T & { return *d; }
    // constexpr auto operator->() const -> const T * { return d; }
    // constexpr auto operator*() const -> const T & { return *d; }
    constexpr auto operator==(const StridedIterator y) const -> bool {
      return d == y.d;
    }
    constexpr auto operator!=(const StridedIterator y) const -> bool {
      return d != y.d;
    }
    constexpr auto operator>(const StridedIterator y) const -> bool {
      return d > y.d;
    }
    constexpr auto operator<(const StridedIterator y) const -> bool {
      return d < y.d;
    }
    constexpr auto operator>=(const StridedIterator y) const -> bool {
      return d >= y.d;
    }
    constexpr auto operator<=(const StridedIterator y) const -> bool {
      return d <= y.d;
    }
    friend auto operator+(ptrdiff_t y,
                          typename MutStridedVector<T>::StridedIterator a) ->
      typename MutStridedVector<T>::StridedIterator {
      return {a.d + y * a.xx, a.xx};
    }
  };
  // FIXME: if `x` == 0, then it will not iterate!
  constexpr auto begin() { return StridedIterator{d, size_t(x)}; }
  constexpr auto end() { return StridedIterator{d + x * N, size_t(x)}; }
  [[nodiscard]] constexpr auto begin() const {
    return StridedIterator{d, size_t(x)};
  }
  [[nodiscard]] constexpr auto end() const {
    return StridedIterator{d + x * N, size_t(x)};
  }
  constexpr auto rbegin() { return std::reverse_iterator(end()); }
  constexpr auto rend() { return std::reverse_iterator(begin()); }
  [[nodiscard]] constexpr auto rbegin() const {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() const {
    return std::reverse_iterator(begin());
  }
  constexpr auto operator[](size_t i) -> T & { return d[size_t(x * i)]; }
  constexpr auto operator[](size_t i) const -> const T & {
    return d[size_t(x * i)];
  }
  constexpr auto operator[](Range<size_t, size_t> i) -> MutStridedVector<T> {
    return MutStridedVector<T>{.d = d + x * i.b, .N = i.e - i.b, .x = x};
  }
  constexpr auto operator[](Range<size_t, size_t> i) const -> StridedVector<T> {
    return StridedVector<T>{.d = d + x * i.b, .N = i.e - i.b, .x = x};
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) -> MutStridedVector<T> {
    return (*this)[canonicalizeRange(i, N)];
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) const -> StridedVector<T> {
    return (*this)[canonicalizeRange(i, N)];
  }

  [[nodiscard]] constexpr auto size() const -> size_t { return N; }
  // bool operator==(StridedVector<T> x) const {
  //     if (size() != x.size())
  //         return false;
  //     for (size_t i = 0; i < size(); ++i) {
  //         if ((*this)[i] != x[i])
  //             return false;
  //     }
  //     return true;
  // }
  constexpr operator StridedVector<T>() {
    const T *const p = d;
    return StridedVector<T>{.d = p, .N = N, .x = x};
  }
  [[nodiscard]] constexpr auto view() const -> StridedVector<T> {
    return StridedVector<T>{.d = d, .N = N, .x = x};
  }
  auto operator=(const T &y) -> MutStridedVector<T> & {
    for (size_t i = 0; i < N; ++i) d[size_t(x * i)] = y;
    return *this;
  }
  auto operator=(const AbstractVector auto &a) -> MutStridedVector<T> & {
    return copyto(*this, a);
  }
  auto operator=(const MutStridedVector<T> &a) -> MutStridedVector<T> & {
    if (this == &a) return *this;
    return copyto(*this, a);
  }
  auto operator+=(T a) -> MutStridedVector<T> & {
    MutStridedVector<T> &self = *this;
    for (size_t i = 0; i < N; ++i) self[i] += a;
    return self;
  }
  auto operator+=(const AbstractVector auto &a) -> MutStridedVector<T> & {
    const size_t M = a.size();
    MutStridedVector<T> &self = *this;
    assert(M == N);
    for (size_t i = 0; i < M; ++i) self[i] += a[i];
    return self;
  }
  auto operator-=(const AbstractVector auto &a) -> MutStridedVector<T> & {
    const size_t M = a.size();
    MutStridedVector<T> &self = *this;
    assert(M == N);
    for (size_t i = 0; i < M; ++i) self[i] -= a[i];
    return self;
  }
  auto operator*=(const AbstractVector auto &a) -> MutStridedVector<T> & {
    const size_t M = a.size();
    MutStridedVector<T> &self = *this;
    assert(M == N);
    for (size_t i = 0; i < M; ++i) self[i] *= a[i];
    return self;
  }
  auto operator/=(const AbstractVector auto &a) -> MutStridedVector<T> & {
    const size_t M = a.size();
    MutStridedVector<T> &self = *this;
    assert(M == N);
    for (size_t i = 0; i < M; ++i) self[i] /= a[i];
    return self;
  }
#ifndef NDEBUG
  void extendOrAssertSize(size_t M) const { assert(N == M); }
#else
  static constexpr void extendOrAssertSize(size_t) {}
#endif
};
static_assert(
  std::weakly_incrementable<StridedVector<int64_t>::StridedIterator>);
static_assert(
  std::input_or_output_iterator<StridedVector<int64_t>::StridedIterator>);

static_assert(std::indirectly_readable<StridedVector<int64_t>::StridedIterator>,
              "failed indirectly readable");
static_assert(
  std::indirectly_readable<MutStridedVector<int64_t>::StridedIterator>,
  "failed indirectly readable");
static_assert(
  std::output_iterator<MutStridedVector<int64_t>::StridedIterator, int>,
  "failed output iterator");
static_assert(std::forward_iterator<StridedVector<int64_t>::StridedIterator>,
              "failed forward iterator");
static_assert(std::input_iterator<StridedVector<int64_t>::StridedIterator>,
              "failed input iterator");
static_assert(
  std::bidirectional_iterator<StridedVector<int64_t>::StridedIterator>,
  "failed bidirectional iterator");

static_assert(std::totally_ordered<StridedVector<int64_t>::StridedIterator>,
              "failed random access iterator");
static_assert(
  std::random_access_iterator<StridedVector<int64_t>::StridedIterator>,
  "failed random access iterator");

static_assert(AbstractVector<StridedVector<int64_t>>);
static_assert(!AbstractMatrix<StridedVector<int64_t>>);
static_assert(std::is_trivially_copyable_v<StridedVector<int64_t>>);
// static_assert(std::is_trivially_copyable_v<MutStridedVector<int64_t>>);
static_assert(std::is_trivially_copyable_v<
              ElementwiseUnaryOp<Sub, StridedVector<int64_t>>>);
static_assert(TriviallyCopyableVectorOrScalar<
              ElementwiseUnaryOp<Sub, StridedVector<int64_t>>>);

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

template <typename T> struct PtrMatrix;
template <typename T> struct MutPtrMatrix;

template <typename T>
concept ScalarRowIndex = ScalarIndex<T> || std::same_as<T, Row>;
template <typename T>
concept ScalarColIndex = ScalarIndex<T> || std::same_as<T, Col>;

constexpr auto unwrapRow(Row x) -> size_t { return size_t(x); }
constexpr auto unwrapCol(Col x) -> size_t { return size_t(x); }
constexpr auto unwrapRow(auto x) { return x; }
constexpr auto unwrapCol(auto x) { return x; }

template <typename T>
constexpr inline auto matrixGet(NotNull<T> ptr, Row M, Col N, RowStride X,
                                const ScalarRowIndex auto mm,
                                const ScalarColIndex auto nn) -> T & {
  auto m = unwrapRow(mm);
  auto n = unwrapCol(nn);
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  return *(ptr +
           size_t(canonicalize(n, size_t(N)) + X * canonicalize(m, size_t(M))));
}
template <typename T>
constexpr inline auto matrixGet(NotNull<const T> ptr, Row M, Col N, RowStride X,
                                const ScalarRowIndex auto mm,
                                const ScalarColIndex auto nn) -> const T & {
  auto m = unwrapRow(mm);
  auto n = unwrapCol(nn);
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  return *(ptr +
           size_t(canonicalize(n, size_t(N)) + X * canonicalize(m, size_t(M))));
}

template <typename T>
concept AbstractSlice = requires(T t, size_t M) {
                          {
                            canonicalizeRange(t, M)
                            } -> std::same_as<Range<size_t, size_t>>;
                        };

template <typename T>
inline constexpr auto matrixGet(NotNull<const T> ptr, Row M, Col N, RowStride X,
                                const AbstractSlice auto m,
                                const AbstractSlice auto n) -> PtrMatrix<T> {
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  Range<size_t, size_t> mr = canonicalizeRange(m, size_t(M));
  Range<size_t, size_t> nr = canonicalizeRange(n, size_t(N));
  return PtrMatrix<T>{ptr + size_t(nr.b + X * mr.b), mr.e - mr.b, nr.e - nr.b,
                      X};
}
template <typename T>
inline constexpr auto matrixGet(NotNull<T> ptr, Row M, Col N, RowStride X,
                                const AbstractSlice auto m,
                                const AbstractSlice auto n) -> MutPtrMatrix<T> {
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  Range<size_t, size_t> mr = canonicalizeRange(m, size_t(M));
  Range<size_t, size_t> nr = canonicalizeRange(n, size_t(N));
  return MutPtrMatrix<T>{ptr + size_t(nr.b + X * mr.b), Row{mr.e - mr.b},
                         Col{nr.e - nr.b}, X};
}

template <typename T>
inline constexpr auto matrixGet(NotNull<const T> ptr, Row M, Col N, RowStride X,
                                const ScalarRowIndex auto mm,
                                const AbstractSlice auto n) -> PtrVector<T> {
  auto m = unwrapRow(mm);
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  size_t mi = canonicalize(m, size_t(M));
  Range<size_t, size_t> nr = canonicalizeRange(n, size_t(N));
  return PtrVector<T>{ptr + size_t(nr.b + X * mi), nr.e - nr.b};
}
template <typename T>
inline constexpr auto matrixGet(NotNull<T> ptr, Row M, Col N, RowStride X,
                                const ScalarRowIndex auto mm,
                                const AbstractSlice auto n) -> MutPtrVector<T> {
  auto m = unwrapRow(mm);
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  size_t mi = canonicalize(m, size_t(M));
  Range<size_t, size_t> nr = canonicalizeRange(n, size_t(N));
  return MutPtrVector<T>{ptr + size_t(nr.b + X * mi), nr.e - nr.b};
}

template <typename T>
inline constexpr auto matrixGet(NotNull<const T> ptr, Row M, Col N, RowStride X,
                                const AbstractSlice auto m,
                                const ScalarColIndex auto nn)
  -> StridedVector<T> {
  auto n = unwrapCol(nn);
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  Range<size_t, size_t> mr = canonicalizeRange(m, size_t(M));
  size_t ni = canonicalize(n, size_t(N));
  return StridedVector<T>{ptr + size_t(ni + X * mr.b), mr.e - mr.b, X};
}
template <typename T>
inline constexpr auto matrixGet(NotNull<T> ptr, Row M, Col N, RowStride X,
                                const AbstractSlice auto m,
                                const ScalarColIndex auto nn)
  -> MutStridedVector<T> {
  auto n = unwrapCol(nn);
#ifndef NDEBUG
  checkIndex(size_t(M), m);
  checkIndex(size_t(N), n);
#endif
  Range<size_t, size_t> mr = canonicalizeRange(m, size_t(M));
  size_t ni = canonicalize(n, size_t(N));
  return MutStridedVector<T>{ptr + size_t(ni + X * mr.b), mr.e - mr.b, X};
}

constexpr auto isSquare(const AbstractMatrix auto &A) -> bool {
  return A.numRow() == A.numCol();
}

template <typename T>
constexpr auto diag(MutPtrMatrix<T> A) -> MutStridedVector<T> {
  return MutStridedVector<T>{A.data(), A.minRowCol(), A.rowStride() + 1};
}
template <typename T> constexpr auto diag(PtrMatrix<T> A) -> StridedVector<T> {
  return StridedVector<T>{A.data(), A.minRowCol(), A.rowStride() + 1};
}
template <typename T>
constexpr auto antiDiag(MutPtrMatrix<T> A) -> MutStridedVector<T> {
  return MutStridedVector<T>{A.data() + size_t(A.numCol()) - 1, A.minRowCol(),
                             (A.rowStride() - 1)};
}
template <typename T>
constexpr auto antiDiag(PtrMatrix<T> A) -> StridedVector<T> {
  return StridedVector<T>{A.data() + size_t(A.numCol()) - 1, A.minRowCol(),
                          (A.rowStride() - 1)};
}

/// A CRTP type defining const methods for matrices.
template <typename T, typename A> struct ConstMatrixCore {
  [[nodiscard]] constexpr auto data() const -> NotNull<const T> {
    return static_cast<const A *>(this)->data();
  }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return static_cast<const A *>(this)->numRow();
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    return static_cast<const A *>(this)->numCol();
  }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return static_cast<const A *>(this)->rowStride();
  }

  [[gnu::flatten]] constexpr auto operator()(auto m, auto n) const
    -> decltype(auto) {
    return matrixGet(data(), numRow(), numCol(), rowStride(), m, n);
  }
  [[nodiscard]] constexpr auto size() const -> std::pair<Row, Col> {
    return std::make_pair(numRow(), numCol());
  }
  [[nodiscard]] constexpr auto diag() const {
    return LinearAlgebra::diag(PtrMatrix<T>(*this));
  }
  [[nodiscard]] constexpr auto antiDiag() const {
    return LinearAlgebra::antiDiag(PtrMatrix<T>(*this));
  }
  [[nodiscard]] constexpr auto minRowCol() const -> size_t {
    return std::min(size_t(numRow()), size_t(numCol()));
  }
  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(numRow()) == size_t(numCol());
  }
  [[nodiscard]] constexpr auto checkSquare() const -> Optional<size_t> {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return {};
    return N;
  }
  constexpr operator PtrMatrix<T>() const {
    const T *ptr = data();
    return PtrMatrix<T>(ptr, numRow(), numCol(), rowStride());
  }
  [[nodiscard]] constexpr auto view() const -> PtrMatrix<T> {
    const T *ptr = data();
    return PtrMatrix<T>(ptr, numRow(), numCol(), rowStride());
  }
  [[nodiscard]] constexpr auto transpose() const -> Transpose<PtrMatrix<T>> {
    return Transpose<PtrMatrix<eltype_t<A>>>{view()};
  }
  [[nodiscard]] auto isExchangeMatrix() const -> bool {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return false;
    A &M = *static_cast<A *>(this);
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j)
        if (A(i, j) != (i + j == N - 1)) return false;
    }
  }
  [[nodiscard]] auto isDiagonal() const -> bool {
    for (Row r = 0; r < numRow(); ++r)
      for (Col c = 0; c < numCol(); ++c)
        if (r != c && (*this)(r, c) != 0) return false;
    return true;
  }
};
template <typename T, typename A> struct MutMatrixCore : ConstMatrixCore<T, A> {
  using CMC = ConstMatrixCore<T, A>;
  using CMC::data, CMC::numRow, CMC::numCol, CMC::rowStride,
    CMC::operator(), CMC::size, CMC::diag, CMC::antiDiag,
    CMC::operator ::LinearAlgebra::PtrMatrix<T>, CMC::view, CMC::transpose,
    CMC::isSquare, CMC::minRowCol;

  constexpr auto data() -> NotNull<T> { return static_cast<A *>(this)->data(); }

  [[gnu::flatten]] constexpr auto operator()(auto m, auto n) -> decltype(auto) {
    return matrixGet(data(), numRow(), numCol(), rowStride(), m, n);
  }
  constexpr auto diag() {
    return LinearAlgebra::diag(MutPtrMatrix<T>(*static_cast<A *>(this)));
  }
  constexpr auto antiDiag() {
    return LinearAlgebra::antiDiag(MutPtrMatrix<T>(*static_cast<A *>(this)));
  }
  constexpr operator MutPtrMatrix<T>() {
    return MutPtrMatrix<T>{data(), numRow(), numCol(), rowStride()};
  }
  [[nodiscard]] constexpr auto view() -> MutPtrMatrix<T> {
    T *ptr = data();
    return MutPtrMatrix<T>{ptr, numRow(), numCol(), rowStride()};
  }
};

template <typename T> struct SmallSparseMatrix;
template <typename T> struct PtrMatrix : ConstMatrixCore<T, PtrMatrix<T>> {
  using ConstMatrixCore<T, PtrMatrix<T>>::size,
    ConstMatrixCore<T, PtrMatrix<T>>::diag,
    ConstMatrixCore<T, PtrMatrix<T>>::antiDiag,
    ConstMatrixCore<T, PtrMatrix<T>>::operator();

  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "const T is redundant");

  [[no_unique_address]] NotNull<const T> mem;
  [[no_unique_address]] unsigned int M, N, X;
  // [[no_unique_address]] Row M;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;

  [[nodiscard]] constexpr auto data() const -> NotNull<const T> { return mem; }
  [[nodiscard]] constexpr auto _numRow() const -> unsigned { return M; }
  [[nodiscard]] constexpr auto _numCol() const -> unsigned { return N; }
  [[nodiscard]] constexpr auto _rowStride() const -> unsigned { return X; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return X; }
  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(M) == size_t(N);
  }
  [[nodiscard]] constexpr inline auto view() const -> PtrMatrix<T> {
    return *this;
  };
  [[nodiscard]] constexpr auto transpose() const -> Transpose<PtrMatrix<T>> {
    return Transpose<PtrMatrix<T>>{*this};
  }
  constexpr PtrMatrix(const T *const pt, const Row MM, const Col NN,
                      const RowStride XX)
    : mem(pt), M(MM), N(NN), X(XX) {}
#ifndef NDEBUG
  void extendOrAssertSize(Row MM, Col NN) const {
    assert(MM == M);
    assert(NN == N);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
};
static_assert(std::same_as<PtrMatrix<int64_t>::eltype, int64_t>);
static_assert(HasEltype<PtrMatrix<int64_t>>);
static_assert(
  std::same_as<
    PtrMatrix<int64_t>::eltype,
    std::decay_t<decltype(std::declval<PtrMatrix<int64_t>>()(0, 0))>>);

template <typename T> struct MutPtrMatrix : MutMatrixCore<T, MutPtrMatrix<T>> {
  using MutMatrixCore<T, MutPtrMatrix<T>>::size,
    MutMatrixCore<T, MutPtrMatrix<T>>::diag,
    MutMatrixCore<T, MutPtrMatrix<T>>::antiDiag,
    MutMatrixCore<T, MutPtrMatrix<T>>::operator();

  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "MutPtrMatrix should never have const T");
  [[no_unique_address]] T *const mem;
  [[no_unique_address]] unsigned int M, N, X;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;

  [[nodiscard]] constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return X; }
  [[nodiscard]] constexpr auto _numRow() const -> unsigned { return M; }
  [[nodiscard]] constexpr auto _numCol() const -> unsigned { return N; }
  [[nodiscard]] constexpr auto _rowStride() const -> unsigned { return X; }
  [[nodiscard]] constexpr auto view() const -> PtrMatrix<T> {
    return PtrMatrix<T>(data(), _numRow(), _numCol(), _rowStride());
  };
  constexpr operator PtrMatrix<T>() const {
    return PtrMatrix<T>(data(), _numRow(), _numCol(), _rowStride());
  }

  [[gnu::flatten]] auto operator=(const SmallSparseMatrix<T> &A)
    -> MutPtrMatrix<T> {
    assert(numRow() == A.numRow());
    assert(numCol() == A.numCol());
    size_t k = 0;
    for (size_t i = 0; i < M; ++i) {
      uint32_t m = A.rows[i] & 0x00ffffff;
      size_t j = 0;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        j += tz;
        mem[X * i + (j++)] = A.nonZeros[k++];
      }
    }
    assert(k == A.nonZeros.size());
    return *this;
  }
  [[gnu::flatten]] auto operator=(MutPtrMatrix<T> A) -> MutPtrMatrix<T> {
    return copyto(*this, PtrMatrix<T>(A));
  }
  // rule of 5 requires...
  constexpr MutPtrMatrix(const MutPtrMatrix<T> &A) = default;
  constexpr MutPtrMatrix(T *pt, Row MM, Col NN)
    : mem(pt), M(MM), N(NN), X(NN){};
  constexpr MutPtrMatrix(T *pt, Row MM, Col NN, RowStride XX)
    : mem(pt), M(MM), N(NN), X(XX){};
  template <typename ARM>
  constexpr MutPtrMatrix(ARM &A)
    : mem(A.data()), M(A.numRow()), N(A.numCol()), X(A.rowStride()) {}

  [[gnu::flatten]] auto operator=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    return copyto(*this, B);
  }
  [[gnu::flatten]] auto operator=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) (*this)(r, c) = b;
    return *this;
  }
  [[gnu::flatten]] auto operator+=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) (*this)(r, c) += B(r, c);
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) (*this)(r, c) -= B(r, c);
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) (*this)(r, c) *= b;
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) (*this)(r, c) /= b;
    return *this;
  }
  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(M) == size_t(N);
  }
  [[nodiscard]] constexpr auto transpose() const -> Transpose<PtrMatrix<T>> {
    return Transpose<PtrMatrix<T>>{view()};
  }
#ifndef NDEBUG
  void extendOrAssertSize(Row MM, Col NN) const {
    assert(numRow() == MM);
    assert(numCol() == NN);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
};
template <typename T> constexpr auto ptrVector(T *p, size_t M) {
  if constexpr (std::is_const_v<T>)
    return PtrVector<std::remove_const_t<T>>{p, M};
  else return MutPtrVector<T>{p, M};
}

template <typename T> PtrMatrix(T *, size_t, size_t) -> PtrMatrix<T>;
template <typename T> MutPtrMatrix(T *, size_t, size_t) -> MutPtrMatrix<T>;
template <typename T> PtrMatrix(T *, size_t, size_t, size_t) -> PtrMatrix<T>;
template <typename T>
MutPtrMatrix(T *, size_t, size_t, size_t) -> MutPtrMatrix<T>;

template <AbstractRowMajorMatrix T> PtrMatrix(T &A) -> PtrMatrix<eltype_t<T>>;
template <AbstractRowMajorMatrix T>
MutPtrMatrix(T &A) -> MutPtrMatrix<eltype_t<T>>;

static_assert(sizeof(PtrMatrix<int64_t>) <=
              4 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(MutPtrMatrix<int64_t>) <=
              4 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(std::is_trivially_copyable_v<Row>);
static_assert(std::is_trivially_copyable_v<Col>);
static_assert(std::is_trivially_copyable_v<RowStride>);
static_assert(std::is_trivially_copyable_v<const Row>);
static_assert(std::is_trivially_copyable_v<const Col>);
static_assert(std::is_trivially_copyable_v<const RowStride>);
static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> is not trivially copyable!");
static_assert(std::is_trivially_copyable_v<PtrVector<int64_t>>,
              "PtrVector<int64_t,0> is not trivially copyable!");
// static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>,
//               "MutPtrMatrix<int64_t> is not trivially copyable!");

static_assert(!AbstractVector<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");

static_assert(AbstractMatrix<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(
  std::same_as<std::remove_reference_t<decltype(MutPtrMatrix<int64_t>(
                 nullptr, Row{0}, Col{0})(size_t(0), size_t(0)))>,
               int64_t>);

static_assert(AbstractMatrix<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");

static_assert(AbstractVector<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");
static_assert(AbstractVector<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");

static_assert(AbstractVector<Vector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");

static_assert(!AbstractMatrix<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");

static_assert(
  AbstractMatrix<ElementwiseMatrixBinaryOp<Mul, PtrMatrix<int64_t>, int>>,
  "ElementwiseBinaryOp isa AbstractMatrix failed");

static_assert(
  !AbstractVector<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>,
  "MatMul should not be an AbstractVector!");
static_assert(AbstractMatrix<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>,
              "MatMul is not an AbstractMatrix!");

template <typename T>
concept IntVector = requires(T t, int64_t y) {
                      { t.size() } -> std::convertible_to<size_t>;
                      { t[y] } -> std::convertible_to<int64_t>;
                    };

//
// Matrix
//
template <typename T, size_t M = 0, size_t N = 0, size_t S = 64>
struct Matrix : MutMatrixCore<T, Matrix<T, M, N, S>> {
  using Base = MutMatrixCore<T, Matrix<T, M, N, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>;
  // using eltype = std::remove_cv_t<T>;
  using eltype = std::remove_reference_t<T>;
  // static_assert(M * N == S,
  //               "if specifying non-zero M and N, we should have M*N == S");
  T mem[S]; // NOLINT(*-avoid-c-arrays)
  static constexpr auto numRow() -> Row { return Row{M}; }
  static constexpr auto numCol() -> Col { return Col{N}; }
  static constexpr auto rowStride() -> RowStride { return RowStride{N}; }

  [[nodiscard]] constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }

  static constexpr auto getConstCol() -> size_t { return N; }
};

template <typename T, size_t M, size_t S>
struct Matrix<T, M, 0, S> : MutMatrixCore<T, Matrix<T, M, 0, S>> {
  using Base = MutMatrixCore<T, Matrix<T, M, 0, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  [[no_unique_address]] llvm::SmallVector<T, S> mem;
  [[no_unique_address]] size_t N, X;

  Matrix(size_t n) : mem(llvm::SmallVector<T, S>(M * n)), N(n), X(n){};

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{N}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{X};
  }

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }
  void resizeForOverwrite(Col NN, RowStride XX) {
    N = size_t(NN);
    X = size_t(XX);
    mem.resize_for_overwrite(XX * M);
  }
  void resizeForOverwrite(Col NN) { resizeForOverwrite(NN, NN); }
};
template <typename T, size_t N, size_t S>
struct Matrix<T, 0, N, S> : MutMatrixCore<T, Matrix<T, 0, N, S>> {
  using Base = MutMatrixCore<T, Matrix<T, 0, N, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  [[no_unique_address]] llvm::SmallVector<T, S> mem;
  [[no_unique_address]] size_t M;

  Matrix(size_t m) : mem(llvm::SmallVector<T, S>(m * N)), M(m){};

  [[nodiscard]] constexpr inline auto numRow() const -> Row { return Row{M}; }
  static constexpr auto numCol() -> Col { return Col{N}; }
  static constexpr auto rowStride() -> RowStride { return RowStride{N}; }
  static constexpr auto getConstCol() -> size_t { return N; }

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }
};

template <typename T>
struct SquarePtrMatrix : ConstMatrixCore<T, SquarePtrMatrix<T>> {
  using Base = ConstMatrixCore<T, SquarePtrMatrix<T>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "const T is redundant");
  [[no_unique_address]] const T *const mem;
  [[no_unique_address]] const size_t M;
  constexpr SquarePtrMatrix(const T *const pt, size_t MM) : mem(pt), M(MM){};

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{M}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{M};
  }
  constexpr auto data() -> const T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
};

template <typename T>
struct MutSquarePtrMatrix : MutMatrixCore<T, MutSquarePtrMatrix<T>> {
  using Base = MutMatrixCore<T, MutSquarePtrMatrix<T>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "T should not be const");
  [[no_unique_address]] T *const mem;
  [[no_unique_address]] const size_t M;

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{M}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{M};
  }
  constexpr MutSquarePtrMatrix(T *pt, size_t MM) : mem(pt), M(MM){};
  constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
  constexpr operator SquarePtrMatrix<T>() const {
    return SquarePtrMatrix<T>{mem, M};
  }
  [[gnu::flatten]] auto operator=(const AbstractMatrix auto &B)
    -> MutSquarePtrMatrix<T> {
    return copyto(*this, B);
  }
};

template <typename T, unsigned STORAGE = 8>
struct SquareMatrix : MutMatrixCore<T, SquareMatrix<T, STORAGE>> {
  using Base = MutMatrixCore<T, SquareMatrix<T, STORAGE>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  static constexpr unsigned TOTALSTORAGE = STORAGE * STORAGE;
  [[no_unique_address]] llvm::SmallVector<T, TOTALSTORAGE> mem;
  [[no_unique_address]] size_t M;

  SquareMatrix(size_t m)
    : mem(llvm::SmallVector<T, TOTALSTORAGE>(m * m)), M(m){};

  SquareMatrix(AbstractMatrix auto A) {
    M = size_t(A.numRow());
    mem.resize_for_overwrite(M * M);
    copyto(*this, A);
  }

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{M}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{M};
  }

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }

  constexpr auto begin() -> T * { return data(); }
  constexpr auto end() -> T * { return data() + M * M; }
  [[nodiscard]] constexpr auto begin() const -> const T * { return data(); }
  [[nodiscard]] constexpr auto end() const -> const T * {
    return data() + M * M;
  }
  auto operator[](size_t i) -> T & { return mem[i]; }
  auto operator[](size_t i) const -> const T & { return mem[i]; }

  static auto identity(size_t N) -> SquareMatrix<T, STORAGE> {
    SquareMatrix<T, STORAGE> A(N);
    for (size_t r = 0; r < N; ++r) A(r, r) = 1;
    return A;
  }
  inline static auto identity(Row N) -> SquareMatrix<T, STORAGE> {
    return identity(size_t(N));
  }
  inline static auto identity(Col N) -> SquareMatrix<T, STORAGE> {
    return identity(size_t(N));
  }
  constexpr operator MutSquarePtrMatrix<T>() {
    return MutSquarePtrMatrix<T>(mem.data(), size_t(M));
  }
  constexpr operator SquarePtrMatrix<T>() const {
    return SquarePtrMatrix<T>(mem.data(), M);
  }
#ifndef NDEBUG
  void extendOrAssertSize(Row R, Col C) {
    assert(R == C && "Matrix must be square");
    M = size_t(R);
    mem.resize_for_overwrite(M * M);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
};

template <typename T, size_t S>
struct Matrix<T, 0, 0, S> : MutMatrixCore<T, Matrix<T, 0, 0, S>> {
  using Base = MutMatrixCore<T, Matrix<T, 0, 0, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  [[no_unique_address]] llvm::SmallVector<T, S> mem;
  [[no_unique_address]] unsigned int M = 0, N = 0, X = 0;
  // [[no_unique_address]] Row M;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }
  Matrix(llvm::SmallVector<T, S> content, Row MM, Col NN)
    : mem(std::move(content)), M(MM), N(NN), X(RowStride(*NN)){};

  Matrix(Row MM, Col NN)
    : mem(llvm::SmallVector<T, S>(MM * NN)), M(MM), N(NN), X(RowStride(*NN)){};

  Matrix() = default;
  Matrix(SquareMatrix<T> &&A) : mem(std::move(A.mem)), M(A.M), N(A.M), X(A.M){};
  Matrix(const SquareMatrix<T> &A)
    : mem(A.begin(), A.end()), M(A.M), N(A.M), X(A.M){};
  Matrix(const AbstractMatrix auto &A)
    : mem(llvm::SmallVector<T>{}), M(A.numRow()), N(A.numCol()), X(A.numCol()) {
    mem.resize_for_overwrite(M * N);
    for (size_t m = 0; m < M; ++m)
      for (size_t n = 0; n < N; ++n) mem[size_t(X * m + n)] = A(m, n);
  }
  constexpr auto begin() { return mem.begin(); }
  constexpr auto end() { return mem.begin() + rowStride() * M; }
  [[nodiscard]] constexpr auto begin() const { return mem.begin(); }
  [[nodiscard]] constexpr auto end() const {
    return mem.begin() + rowStride() * M;
  }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return X; }
  [[nodiscard]] constexpr auto _numRow() const -> unsigned { return M; }
  [[nodiscard]] constexpr auto _numCol() const -> unsigned { return N; }
  [[nodiscard]] constexpr auto _rowStride() const -> unsigned { return X; }

  static auto uninitialized(Row MM, Col NN) -> Matrix<T, 0, 0, S> {
    Matrix<T, 0, 0, S> A(0, 0);
    A.M = MM;
    A.X = A.N = NN;
    A.mem.resize_for_overwrite(MM * NN);
    return A;
  }
  static auto identity(size_t MM) -> Matrix<T, 0, 0, S> {
    Matrix<T, 0, 0, S> A(MM, MM);
    for (size_t i = 0; i < MM; ++i) A(i, i) = 1;
    return A;
  }
  inline static auto identity(Row N) -> Matrix<T, 0, 0, S> {
    return identity(size_t(N));
  }
  inline static auto identity(Col N) -> Matrix<T, 0, 0, S> {
    return identity(size_t(N));
  }
  void clear() {
    M = N = X = 0;
    mem.clear();
  }

  void resize(Row MM, Col NN, RowStride XX) {
    mem.resize(XX * MM);
    size_t minMMM = std::min(size_t(M), size_t(MM));
    if ((XX > X) && M && N)
      // need to copy
      for (size_t m = minMMM - 1; m > 0; --m)
        for (auto n = size_t(N); n-- > 0;)
          mem[size_t(XX * m + n)] = mem[size_t(X * m + n)];
    // zero
    for (size_t m = 0; m < minMMM; ++m)
      for (auto n = size_t(N); n < size_t(NN); ++n) mem[size_t(XX * m + n)] = 0;
    for (size_t m = minMMM; m < size_t(MM); ++m)
      for (size_t n = 0; n < size_t(NN); ++n) mem[size_t(XX * m + n)] = 0;
    X = *XX;
    M = *MM;
    N = *NN;
  }
  void insertZero(Col i) {
    Col NN = N + 1;
    auto XX = RowStride(std::max(size_t(X), size_t(NN)));
    mem.resize(XX * M);
    size_t nLower = (XX > X) ? size_t(0) : size_t(i);
    if (M && N)
      // need to copy
      for (auto m = size_t(M); m-- > 0;)
        for (auto n = size_t(N); n-- > nLower;)
          mem[size_t(XX * m + n) + (n >= size_t(i))] = mem[size_t(X * m + n)];
    // zero
    for (size_t m = 0; m < M; ++m) mem[size_t(XX * m) + size_t(i)] = 0;
    X = *XX;
    N = *NN;
  }
  void resize(Row MM, Col NN) { resize(MM, NN, max(NN, X)); }
  void reserve(Row MM, Col NN) { reserve(MM, max(NN, X)); }
  void reserve(Row MM, RowStride NN) { mem.reserve(NN * MM); }
  void clearReserve(Row MM, Col NN) { clearReserve(MM, RowStride(*NN)); }
  void clearReserve(Row MM, RowStride XX) {
    clear();
    mem.reserve(XX * MM);
  }
  void resizeForOverwrite(Row MM, Col NN, RowStride XX) {
    assert(XX >= NN);
    M = *MM;
    N = *NN;
    X = *XX;
    if (X * M > mem.size()) mem.resize_for_overwrite(X * M);
  }
  void resizeForOverwrite(Row MM, Col NN) {
    M = *MM;
    N = X = *NN;
    if (X * M > mem.size()) mem.resize_for_overwrite(X * M);
  }

  void resize(Row MM) {
    Row Mold = M;
    M = *MM;
    if (rowStride() * M > mem.size()) mem.resize(X * M);
    if (M > Mold) (*this)(_(Mold, M), _) = 0;
  }
  void resizeForOverwrite(Row MM) {
    if (rowStride() * MM > mem.size()) mem.resize_for_overwrite(X * M);
    M = *MM;
  }
  void resize(Col NN) { resize(M, NN); }
  void resizeForOverwrite(Col NN) {
    if (X < NN) {
      X = *NN;
      mem.resize_for_overwrite(X * M);
    }
    N = *NN;
  }
  void extendOrAssertSize(Row R, Col C) { resizeForOverwrite(R, C); }
  void erase(Col i) {
    assert(i < N);
    for (size_t m = 0; m < M; ++m)
      for (auto n = size_t(i); n < N - 1; ++n)
        (*this)(m, n) = (*this)(m, n + 1);
    --N;
  }
  void erase(Row i) {
    assert(i < M);
    auto it = mem.begin() + X * i;
    mem.erase(it, it + X);
    --M;
  }
  constexpr void truncate(Col NN) {
    assert(NN <= N);
    N = *NN;
  }
  constexpr void truncate(Row MM) {
    assert(MM <= M);
    M = *MM;
  }
  [[gnu::flatten]] auto operator=(T x) -> Matrix<T, 0, 0, S> & {
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) (*this)(r, c) = x;
    return *this;
  }
  void moveLast(Col j) {
    if (j == N) return;
    for (size_t m = 0; m < M; ++m) {
      auto x = (*this)(m, j);
      for (auto n = size_t(j); n < N - 1;) {
        size_t o = n++;
        (*this)(m, o) = (*this)(m, n);
      }
      (*this)(m, N - 1) = x;
    }
  }
  [[nodiscard]] auto deleteCol(size_t c) const -> Matrix<T, 0, 0, S> {
    Matrix<T, 0, 0, S> A(M, N - 1);
    for (size_t m = 0; m < M; ++m) {
      A(m, _(0, c)) = (*this)(m, _(0, c));
      A(m, _(c, LinearAlgebra::end)) = (*this)(m, _(c + 1, LinearAlgebra::end));
    }
    return A;
  }
  [[gnu::flatten]] auto operator+=(const AbstractMatrix auto &A) {
    MutPtrMatrix(*this) += A;
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const AbstractMatrix auto &A) {
    MutPtrMatrix(*this) -= A;
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const AbstractMatrix auto &A) {
    MutPtrMatrix(*this) *= A;
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const AbstractMatrix auto &A) {
    MutPtrMatrix(*this) /= A;
    return *this;
  }
};
using IntMatrix = Matrix<int64_t>;
static_assert(std::same_as<IntMatrix::eltype, int64_t>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(AbstractMatrix<Transpose<PtrMatrix<int64_t>>>);
static_assert(std::same_as<eltype_t<Matrix<int64_t>>, int64_t>);

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
  LinearAlgebra::MutSquarePtrMatrix, LinearAlgebra::Range, LinearAlgebra::begin,
  LinearAlgebra::end, LinearAlgebra::swap, LinearAlgebra::SquarePtrMatrix,
  LinearAlgebra::Row, LinearAlgebra::RowStride, LinearAlgebra::Col,
  LinearAlgebra::CarInd;
