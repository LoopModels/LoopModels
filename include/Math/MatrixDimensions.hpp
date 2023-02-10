#pragma once

#include "Math/AxisTypes.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace LinearAlgebra {

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
  constexpr DenseDims(Row m, Col n) : M(m), N(n) {}
  constexpr explicit DenseDims(StridedDims d) : M(d.M), N(d.N) {}
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

template <class R, class C> struct CartesianIndex {
  R row;
  C col;
  explicit constexpr operator Row() const { return row; }
  explicit constexpr operator Col() const { return col; }
};
template <class R, class C> CartesianIndex(R r, C c) -> CartesianIndex<R, C>;

// Concept for aligning array dimensions with indices.
template <class I, class D>
concept Index = (std::integral<D> && std::integral<I>) ||
                (MatrixDimension<D> && requires(I i) {
                                         { i.row };
                                         { i.col };
                                       });

} // namespace LinearAlgebra

using LinearAlgebra::StridedDims, LinearAlgebra::DenseDims,
  LinearAlgebra::SquareDims;
