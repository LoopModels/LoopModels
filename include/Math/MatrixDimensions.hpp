#pragma once

#include "Math/AxisTypes.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace LinearAlgebra {
template <class R, class C> struct CartesianIndex {
  R row;
  C col;
  explicit constexpr operator Row() const { return row; }
  explicit constexpr operator Col() const { return col; }
  [[nodiscard]] constexpr auto operator==(const CartesianIndex &other) const
    -> bool {
    return (row == other.row) && (col == other.col);
  }
};
template <class R, class C> CartesianIndex(R r, C c) -> CartesianIndex<R, C>;

#ifndef NDEBUG
template <std::integral I>
constexpr auto checkedMul(std::integral auto a, std::integral auto b) -> I {
  I result;
  bool overflow = __builtin_mul_overflow(a, b, &result);
  assert(!overflow && "overflow");
  return result;
}
#else
template <std::integral I>
constexpr auto checkedMul(std::integral auto a, std::integral auto b) -> I {
  return a * b;
}
#endif
struct SquareDims;
struct DenseDims;
struct StridedDims {
  unsigned int M{};
  unsigned int N{};
  unsigned int strideM{};
  constexpr StridedDims() = default;
  constexpr StridedDims(Row m, Col n) : M(m), N(n), strideM(n) {}
  constexpr StridedDims(Row m, Col n, RowStride x) : M(m), N(n), strideM(x) {}
  constexpr StridedDims(CartesianIndex<Row, Col> ind)
    : M(unsigned(ind.row)), N(unsigned(ind.col)), strideM(unsigned(ind.col)) {}
  constexpr explicit operator uint32_t() const {
    return checkedMul<uint32_t>(M, strideM);
  }
  constexpr explicit operator uint64_t() const {
    return checkedMul<uint64_t>(M, strideM);
  }
  constexpr auto operator=(const DenseDims &D) -> StridedDims &;
  constexpr auto operator=(const SquareDims &D) -> StridedDims &;
  constexpr operator CarInd() const { return {M, N}; }
  constexpr explicit operator Row() const { return M; }
  constexpr explicit operator Col() const { return N; }
  constexpr explicit operator RowStride() const { return strideM; }
  [[nodiscard]] constexpr auto operator==(const StridedDims &D) const -> bool {
    return (M == D.M) && (N == D.N) && (strideM == D.strideM);
  }
  [[nodiscard]] constexpr auto truncate(Row r) const -> StridedDims {
    assert((r <= Row{M}) && "truncate cannot add rows.");
    return {unsigned(r), N, strideM};
  }
  [[nodiscard]] constexpr auto truncate(Col c) const -> StridedDims {
    assert((c <= Col{M}) && "truncate cannot add columns.");
    return {M, unsigned(c), strideM};
  }
  constexpr auto set(Row r) -> StridedDims & {
    M = unsigned(r);
    return *this;
  }
  constexpr auto set(Col c) -> StridedDims & {
    N = unsigned(c);
    strideM = std::max(strideM, N);
    return *this;
  }
  [[nodiscard]] constexpr auto similar(Row r) -> StridedDims {
    return {unsigned(r), N, strideM};
  }
  [[nodiscard]] constexpr auto similar(Col c) -> StridedDims {
    return {M, unsigned(c), strideM};
  }
};
/// Dimensions with a capacity
// struct CapDims : StridedDims {
//   unsigned int rowCapacity;
// };
struct DenseDims {
  unsigned int M{};
  unsigned int N{};
  constexpr explicit operator uint32_t() const {
    return checkedMul<uint32_t>(M, N);
  }
  constexpr explicit operator uint64_t() const {
    return checkedMul<uint64_t>(M, N);
  }
  constexpr DenseDims() = default;
  constexpr DenseDims(Row m, Col n) : M(unsigned(m)), N(unsigned(n)) {}
  constexpr explicit DenseDims(StridedDims d) : M(d.M), N(d.N) {}
  constexpr DenseDims(CartesianIndex<Row, Col> ind)
    : M(unsigned(ind.row)), N(unsigned(ind.col)) {}
  constexpr operator StridedDims() const { return {M, N, N}; }
  constexpr operator CarInd() const { return {M, N}; }
  constexpr auto operator=(const SquareDims &D) -> DenseDims &;
  constexpr explicit operator Row() const { return M; }
  constexpr explicit operator Col() const { return N; }
  constexpr explicit operator RowStride() const { return N; }
  [[nodiscard]] constexpr auto truncate(Row r) const -> DenseDims {
    assert((r <= Row{M}) && "truncate cannot add rows.");
    return {unsigned(r), N};
  }
  [[nodiscard]] constexpr auto truncate(Col c) const -> StridedDims {
    assert((c <= Col{M}) && "truncate cannot add columns.");
    return {M, c, N};
  }
  constexpr auto set(Row r) -> DenseDims & {
    M = unsigned(r);
    return *this;
  }
  constexpr auto set(Col c) -> DenseDims & {
    N = unsigned(c);
    return *this;
  }
  [[nodiscard]] constexpr auto similar(Row r) -> DenseDims {
    return {unsigned(r), N};
  }
  [[nodiscard]] constexpr auto similar(Col c) -> DenseDims {
    return {M, unsigned(c)};
  }
};
struct SquareDims {
  unsigned int M{};
  constexpr explicit operator uint32_t() const {
    return checkedMul<uint32_t>(M, M);
  }
  constexpr explicit operator uint64_t() const {
    return checkedMul<uint64_t>(M, M);
  }
  constexpr SquareDims() = default;
  constexpr SquareDims(unsigned int d) : M{d} {}
  constexpr SquareDims(Row d) : M{unsigned(d)} {}
  constexpr SquareDims(Col d) : M{unsigned(d)} {}
  constexpr operator StridedDims() const { return {M, M, M}; }
  constexpr operator DenseDims() const { return {M, M}; }
  constexpr operator CarInd() const { return {M, M}; }
  constexpr explicit operator Row() const { return M; }
  constexpr explicit operator Col() const { return M; }
  constexpr explicit operator RowStride() const { return M; }
  [[nodiscard]] constexpr auto truncate(Row r) const -> DenseDims {
    assert((r <= Row{M}) && "truncate cannot add rows.");
    return {unsigned(r), M};
  }
  [[nodiscard]] constexpr auto truncate(Col c) const -> StridedDims {
    assert((c <= Col{M}) && "truncate cannot add columns.");
    return {M, unsigned(c), M};
  }
  [[nodiscard]] constexpr auto similar(Row r) -> DenseDims {
    return {unsigned(r), M};
  }
  [[nodiscard]] constexpr auto similar(Col c) -> DenseDims {
    return {M, unsigned(c)};
  }
};
// [[nodiscard]] constexpr auto capacity(std::integral auto c) { return c; }
// [[nodiscard]] constexpr auto capacity(auto c) -> unsigned int { return c; }
// [[nodiscard]] constexpr auto capacity(CapDims c) -> unsigned int {
//   return c.rowCapacity * c.strideM;
// }

constexpr auto StridedDims::operator=(const DenseDims &D) -> StridedDims & {
  M = D.M;
  N = D.N;
  strideM = N;
  return *this;
}
constexpr auto StridedDims::operator=(const SquareDims &D) -> StridedDims & {
  M = D.M;
  N = M;
  strideM = M;
  return *this;
}
constexpr auto DenseDims::operator=(const SquareDims &D) -> DenseDims & {
  M = D.M;
  N = M;
  return *this;
}
template <typename D>
concept MatrixDimension = requires(D d) {
                            { d } -> std::convertible_to<StridedDims>;
                          };
static_assert(MatrixDimension<SquareDims>);
static_assert(MatrixDimension<DenseDims>);
static_assert(MatrixDimension<StridedDims>);

template <class T>
concept DenseLayout = std::integral<T> || std::is_same_v<T, DenseDims> ||
                      std::is_same_v<T, SquareDims>;

template <std::integral T> constexpr auto dimension(Row r, Col c) -> T {
  return T(r);
}
template <MatrixDimension T> constexpr auto dimension(Row r, Col c) -> T {
  return DenseDims(r, c);
}

} // namespace LinearAlgebra

using LinearAlgebra::StridedDims, LinearAlgebra::DenseDims,
  LinearAlgebra::SquareDims;
