#pragma once

#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

constexpr int64_t cstoll(const char *s, size_t &cur) {
  int64_t res = 0;
  bool neg = false;
  while (s[cur] == ' ') ++cur;
  if (s[cur] == '-') {
    neg = true;
    ++cur;
  }
  while (s[cur] >= '0' && s[cur] <= '9') {
    res = res * 10 + (s[cur] - '0');
    ++cur;
  }
  return neg ? -res : res;
}

[[nodiscard]] constexpr auto operator"" _mat(const char *s, size_t)
  -> LinAlg::DenseMatrix<int64_t, 0> {
  assert(s[0] == '[');
  LinAlg::ManagedArray<int64_t, unsigned, 0> content;
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
    int64_t ll = cstoll(s, cur);
    content.push_back(ll);
  }
  size_t numCols = content.size() / numRows;
  assert(content.size() % numRows == 0);
  LinAlg::DenseMatrix<int64_t, 0> A(std::move(content),
                                    DenseDims{Row{numRows}, Col{numCols}});
  return A;
}
