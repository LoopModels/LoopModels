#pragma once

#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

[[nodiscard]] constexpr auto operator"" _mat(const char *s, size_t)
  -> LinAlg::DenseMatrix<int64_t> {
  assert(s[0] == '[');
  LinAlg::ManagedArray<int64_t, unsigned, 64> content;
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
  LinAlg::DenseMatrix<int64_t> A(std::move(content),
                                        DenseDims{Row{numRows}, Col{numCols}});
  return A;
}
