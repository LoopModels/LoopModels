#pragma once

#include "Math/AxisTypes.hpp"
#include <cstddef>
#include <cstdint>

namespace LinearAlgebra {

struct SquareDims;
struct DenseDims;
struct StridedDims {
  unsigned int M{};
  unsigned int N{};
  unsigned int strideM{};
  constexpr StridedDims() = default;
  constexpr StridedDims(Row M, Col N) : M(M), N(N), strideM(N) {}
  constexpr StridedDims(Row M, Col N, RowStride X) : M(M), N(N), strideM(X) {}
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(strideM) == size_t(M * strideM) && "overflow");
    return M * strideM;
  }
  constexpr auto operator=(const DenseDims &D) -> StridedDims &;
  constexpr auto operator=(const SquareDims &D) -> StridedDims &;
  constexpr operator CarInd() const { return {M, N}; }
  constexpr operator Row() const { return M; }
  constexpr operator Col() const { return N; }
  constexpr operator RowStride() const { return strideM; }
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
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(N) == size_t(M * N) && "overflow");
    return M * N;
  }
  constexpr DenseDims() = default;
  constexpr DenseDims(Row M, Col N) : M(M), N(N) {}
  constexpr operator StridedDims() const { return {M, N, N}; }
  constexpr operator CarInd() const { return {M, N}; }
  constexpr auto operator=(const SquareDims &D) -> DenseDims &;
  constexpr operator Row() const { return M; }
  constexpr operator Col() const { return N; }
  constexpr operator RowStride() const { return N; }
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
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(M) == size_t(M * M) && "overflow");
    return M * M;
  }
  constexpr SquareDims() = default;
  constexpr operator StridedDims() const { return {M, M, M}; }
  constexpr operator DenseDims() const { return {M, M}; }
  constexpr operator CarInd() const { return {M, M}; }
  constexpr operator Row() const { return M; }
  constexpr operator Col() const { return M; }
  constexpr operator RowStride() const { return M; }
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

} // namespace LinearAlgebra
