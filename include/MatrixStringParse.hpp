#pragma once

#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

[[nodiscard]] constexpr auto operator"" _mat(const char *s, size_t)
  -> Matrix<int64_t, LinearAlgebra::DenseDims> {
  assert(s[0] == '[');
  Buffer<int64_t, 64, unsigned> content;
  size_t cur = 1;
  size_t numRows = 1;
  while (s[cur] != ']') {
    char c = s[cur];
    if (c == ' ') {
      ++cur;
      continue;
    } else if (c == ';') {
      numRows += 1;
      ++cur;
      continue;
    }
    size_t sz = 0;
    int64_t ll = std::stoll(s + cur, &sz, 10);
    cur += sz;
    content.push_back(ll);
  }
  size_t numCols = content.size() / numRows;
  assert(content.size() % numRows == 0);
  Matrix<int64_t, LinearAlgebra::DenseDims> A(std::move(content), Row{numRows},
                                              Col{numCols});
  return A;
}
