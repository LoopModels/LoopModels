#pragma once
#include "Math/AxisTypes.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Utilities/Invariant.hpp"
#include "Utilities/Iterators.hpp"
#include <cstddef>

namespace LinearAlgebra {

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
constexpr auto operator+(OffsetBegin y, ScalarValueIndex auto x)
  -> OffsetBegin {
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
static constexpr inline OffsetEnd last{1};
template <typename T>
concept RelativeOffset = std::same_as<T, End> || std::same_as<T, OffsetEnd> ||
                         std::same_as<T, Begin> || std::same_as<T, OffsetBegin>;

// Union type
template <typename T>
concept ScalarRelativeIndex =
  std::same_as<T, End> || std::same_as<T, Begin> ||
  std::same_as<T, OffsetBegin> || std::same_as<T, OffsetEnd>;

template <typename T>
concept ScalarIndex = std::integral<T> || ScalarRelativeIndex<T>;

static inline constexpr struct Colon {
  [[nodiscard]] inline constexpr auto operator()(auto B, auto E) const {
    return Range{standardizeRangeBound(B), standardizeRangeBound(E)};
  }
} _; // NOLINT(bugprone-reserved-identifier)
constexpr auto canonicalize(size_t e, size_t) -> size_t { return e; }
constexpr auto canonicalize(Begin, size_t) -> size_t { return 0; }
constexpr auto canonicalize(OffsetBegin b, size_t) -> size_t {
  return b.offset;
}
constexpr auto canonicalize(End, size_t M) -> size_t { return M; }
constexpr auto canonicalize(OffsetEnd e, size_t M) -> size_t {
  return M - e.offset;
}
template <typename B, typename E>
constexpr auto canonicalizeRange(Range<B, E> r, size_t M)
  -> Range<size_t, size_t> {
  return Range<size_t, size_t>{canonicalize(r.b, M), canonicalize(r.e, M)};
}
constexpr auto canonicalizeRange(Colon, size_t M) -> Range<size_t, size_t> {
  return Range<size_t, size_t>{0, M};
}

template <typename T>
concept ScalarRowIndex = ScalarIndex<T> || std::same_as<T, Row>;
template <typename T>
concept ScalarColIndex = ScalarIndex<T> || std::same_as<T, Col>;

template <typename T>
concept AbstractSlice = requires(T t, size_t M) {
                          {
                            canonicalizeRange(t, M)
                            } -> std::same_as<Range<size_t, size_t>>;
                        };
static_assert(AbstractSlice<Range<size_t, size_t>>);
static_assert(AbstractSlice<Colon>);

[[nodiscard]] inline constexpr auto calcOffset(size_t len, size_t i) -> size_t {
  invariant(i < len);
  return i;
}
[[nodiscard]] inline constexpr auto calcOffset(size_t len, Col i) -> size_t {
  invariant(*i < len);
  return *i;
}
[[nodiscard]] inline constexpr auto calcOffset(size_t len, Row i) -> size_t {
  invariant(*i < len);
  return *i;
}
[[nodiscard]] inline constexpr auto calcOffset(size_t, Begin) -> size_t {
  return 0;
}
[[nodiscard]] inline constexpr auto calcOffset(size_t len, OffsetBegin i)
  -> size_t {
  return calcOffset(len, i.offset);
}
[[nodiscard]] inline constexpr auto calcOffset(size_t len, OffsetEnd i)
  -> size_t {
  invariant(i.offset <= len);
  return len - i.offset;
}
[[nodiscard]] inline constexpr auto calcRangeOffset(size_t len, size_t i)
  -> size_t {
  invariant(i <= len);
  return i;
}
[[nodiscard]] inline constexpr auto calcRangeOffset(size_t len, Col i)
  -> size_t {
  invariant(*i <= len);
  return *i;
}
[[nodiscard]] inline constexpr auto calcRangeOffset(size_t len, Row i)
  -> size_t {
  invariant(*i <= len);
  return *i;
}
[[nodiscard]] inline constexpr auto calcRangeOffset(size_t, Begin) -> size_t {
  return 0;
}
[[nodiscard]] inline constexpr auto calcRangeOffset(size_t len, OffsetBegin i)
  -> size_t {
  return calcRangeOffset(len, i.offset);
}
[[nodiscard]] inline constexpr auto calcRangeOffset(size_t len, OffsetEnd i)
  -> size_t {
  invariant(i.offset <= len);
  return len - i.offset;
}
// note that we don't check i.b < len because we want to allow
// empty ranges, and r.b <= r.e <= len is checked in calcNewDim.
template <class B, class E>
constexpr auto calcOffset(size_t len, Range<B, E> i) -> size_t {
  return calcRangeOffset(len, i.b);
}
constexpr auto calcOffset(size_t, Colon) -> size_t { return 0; }

template <class R, class C>
[[nodiscard]] inline constexpr auto calcOffset(StridedDims d,
                                               CartesianIndex<R, C> i)
  -> size_t {
  return size_t(RowStride{d} * calcOffset(size_t(Row{d}), i.row) +
                calcOffset(size_t(Col{d}), i.col));
}

struct StridedRange {
  [[no_unique_address]] unsigned len;
  [[no_unique_address]] unsigned stride;
  constexpr operator unsigned() const { return len; }
};
template <class I> constexpr auto calcOffset(StridedRange d, I i) -> size_t {
  return d.stride * calcOffset(d.len, i);
};

// Concept for aligning array dimensions with indices.
template <class I, class D>
concept Index = ((std::integral<D> || std::same_as<D, StridedRange>) &&
                 (ScalarIndex<I> || AbstractSlice<I>)) ||
                (MatrixDimension<D> && requires(I i) {
                                         { i.row };
                                         { i.col };
                                       });

struct Empty {};

constexpr auto calcNewDim(size_t, ScalarIndex auto) -> Empty { return {}; };
constexpr auto calcNewDim(size_t len, Range<size_t, size_t> r) {
  invariant(r.e <= len);
  invariant(r.b <= r.e);
  return r.e - r.b;
};
template <class B, class E>
constexpr auto calcNewDim(size_t len, Range<B, E> r) {
  return calcNewDim(len, canonicalizeRange(r, len));
};
template <ScalarValueIndex R, ScalarValueIndex C>
constexpr auto calcNewDim(StridedDims, CartesianIndex<R, C>) -> Empty {
  return {};
}
constexpr auto calcNewDim(std::integral auto len, Colon) { return len; };

template <AbstractSlice B, ScalarColIndex C>
constexpr auto calcNewDim(StridedDims d, CartesianIndex<B, C> i) {
  unsigned rowDims = unsigned(calcNewDim(size_t(Row{d}), i.row));
  return StridedRange{rowDims, unsigned(RowStride{d})};
}

template <ScalarRowIndex R, AbstractSlice C>
constexpr auto calcNewDim(StridedDims d, CartesianIndex<R, C> i) {
  return calcNewDim(size_t(Col{d}), i.col);
}

template <AbstractSlice B, AbstractSlice C>
constexpr auto calcNewDim(StridedDims d, CartesianIndex<B, C> i) {
  auto rowDims = calcNewDim(size_t(Row{d}), i.row);
  auto colDims = calcNewDim(size_t(Col{d}), i.col);
  return StridedDims{Row{rowDims}, Col{colDims}, RowStride{d}};
}
template <AbstractSlice B>
constexpr auto calcNewDim(DenseDims d, CartesianIndex<B, Colon> i) {
  auto rowDims = calcNewDim(size_t(Row{d}), i.row);
  return DenseDims{Row{rowDims}, Col{d}};
}

} // namespace LinearAlgebra
