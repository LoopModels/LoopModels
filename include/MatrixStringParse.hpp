#pragma once

#include "./Math.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <string>

// #undef HWY_TARGET_INCLUDE
// #define HWY_TARGET_INCLUDE "./MatrixStringParse.hpp"
// #include <hwy/foreach_target.h>
// #include <hwy/highway.h>
// #if defined(MATRIXSTRINGPARSER_H) == defined(MATRIXSTRINGPARSER_H)
// #ifdef MATRIXSTRINGPARSER_H
// #undef MATRIXSTRINGPARSER_H 
// #else
// #define MATRIXSTRINGPARSER_H
// #endif

// namespace project {  // optional
namespace HWY_NAMESPACE {

IntMatrix stringToIntMatrix(const std::string &s) {
    assert(s.starts_with('['));
    assert(s.ends_with(']'));
    llvm::SmallVector<int64_t, 64> content;
    size_t cur = 1;
    size_t numRows = 1;
    while (cur < s.length()) {
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
        long long ll = std::stoll(s.c_str() + cur, &sz, 10);
        cur += sz;
        content.push_back(ll);
    }
    size_t numCols = content.size() / numRows;
    assert(content.size() % numRows == 0);
    IntMatrix A(std::move(content), numRows, numCols);
    return A;
}

}  // namespace HWY_NAMESPACE
// }  // namespace project - optional