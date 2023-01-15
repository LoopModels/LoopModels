#pragma once
#include "Math/Math.hpp"
#include "./NormalForm.hpp"
#include <cstddef>
#include <cstdint>

// function unimod_hnf(A)
//     H, U = Matrix.(hnf_with_transform(MatrixSpace(ZZ, size(A')...)(A')))
//     (isdiag(H) && all(isone, @views H[diagind(H)])) || return nothing
//     [A; Int.(inv(U' .// 1))[size(A, 1)+1:end, :]]
// end

// if `A` can be unimodularized, returns the inverse of the unimodularized `A`
[[nodiscard]] inline auto unimodularize(IntMatrix A)
  -> std::optional<SquareMatrix<int64_t>> {
  std::optional<std::pair<IntMatrix, SquareMatrix<int64_t>>> OHNF =
    NormalForm::hermite(std::move(A));
  if (!OHNF.has_value()) return {};
  auto &[H, U] = *OHNF;
  for (size_t m = 0; m < H.numCol(); ++m)
    if (H(m, m) != 1) return {};
  return std::move(U);
}
