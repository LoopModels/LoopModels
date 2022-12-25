#pragma once

#include "./Math.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

[[nodiscard]] inline auto operator"" _mat(const char *s, size_t) -> IntMatrix {
  assert(s[0] == '[');
  llvm::SmallVector<int64_t, 64> content;
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
    } else if (c == ']') {
      break;
    }
    size_t sz = 0;
    int64_t ll = std::stoll(s + cur, &sz, 10);
    cur += sz;
    content.push_back(ll);
  }
  size_t numCols = content.size() / numRows;
  assert(content.size() % numRows == 0);
  IntMatrix A(std::move(content), Row{numRows}, Col{numCols});
  return A;
}
