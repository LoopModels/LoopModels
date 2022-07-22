#pragma once

#include "./Math.hpp"
#include <cassert>
#include <cstddef>

IntMatrix stringToIntMatrix(const std::string &s) {
    assert(s.starts_with('['));
    assert(s.ends_with(']'));
    size_t numRows = 1, numCols = 1;
    for (auto &c : s)
	numRows += c == ';';
    size_t rowEnd = numRows == 1 ? s.size()-1 : s.find(';');
    auto row = s.substr(1, rowEnd);
    IntMatrix A(numRows, numCols);
    return A;
}
