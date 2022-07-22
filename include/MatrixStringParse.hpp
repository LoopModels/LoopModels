#pragma once

#include "./Math.hpp"
#include <cassert>
#include <cstddef>
#include <string>

IntMatrix stringToIntMatrix(const std::string &s) {
    assert(s.starts_with('['));
    assert(s.ends_with(']'));
    std::cout << "s = \n" << s << std::endl;
    size_t numRows = 1, numCols = 1;
    for (auto &c : s)
        numRows += c == ';';
    size_t rowEnd = numRows == 1 ? s.size() - 1 : s.find(';', 1);
    std::basic_string<char> row = s.substr(1, rowEnd);
    for (auto &c : row)
        numCols += c == ' ';
    std::cout << "numRows = " << numRows << "; numCols = " << numCols << std::endl;
    IntMatrix A(numRows, numCols);
    std::string::size_type n, p=1;
    for (size_t r = 0; r < numRows; ++r){
	auto rowTerminator = (r+1) == numRows ? ']' : ';';
	for (size_t c = 0; c < numCols; ++c){
	    n = s.find((c+1==numCols) ? rowTerminator : ' ', p);
	    std::cout << "p = " << p << "; n = " << n << std::endl;
	    auto subStr = s.substr(p, n);
	    std::cout << "subStr = " << subStr<< std::endl;
	    auto Arc = std::stoll(subStr);
	    std::cout << "Arc = " << Arc << std::endl;
	    A(r,c) = Arc;
	    p = n;
	}
    }
    return A;
}
