#pragma once
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "lp_data/HConst.h"
#include "llvm/ADT/SmallVector.h"
#include <Highs.h>
#include <cstddef>
#include <cstdint>

// use ILP solver for eliminating redundant constraints

bool checkRedundantConstraint(IntMatrix auto &A,
                              llvm::SmallVectorImpl<intptr_t> &b,
                              IntMatrix auto &E,
                              llvm::SmallVectorImpl<intptr_t> &q,
                              const size_t C) {

    auto [numRow, numColA] = A.size();
    size_t numColE = q.size();
    HighsModel model;
    model.lp_.sense_ = ObjSense::kMaximize;
    model.lp_.offset_ = b[C];
    model.lp_.col_cost_ = A.getCol(C);
    model.lp_.num_col_ = numRow;
    // col_lower_ <= x <= col_upper_
    // row_lower_ <= A*x <= row_upper_
    // We want to be reasonable efficient in partioning constraints into
    // former vs latter.
    //
    model.lp_.a_matrix_.format_ = MatrixFormat::kRowwise;

    model.lp_.col_lower_.resize(numRow, -1e30);
    model.lp_.col_upper_.resize(numRow, 1e30);
    model.lp_.a_matrix_.start_.push_back(0);
    //
    llvm::SmallVector<uint8_t, 64> isvar(numRow);
    for (size_t c = 0; c < numColA; ++c) {
        size_t n = 0, p = 0, nz = 0;
        intptr_t lastV = -1;
        for (size_t v = 0; v < numRow; ++v) {
            intptr_t Avc = A(v, c);
            nz += (Avc != 0);
            p += (Avc == 1);
            n += (Avc == -1);
            lastV = Avc ? v : lastV;
        }
        if ((nz != 1) || ((n + p) != 1) || (isvar[lastV] & (n + 2 * p))) {
            // add to row_
            for (size_t v = 0; v < numRow; ++v) {
                if (intptr_t Avc = A(v, c)) {
                    model.lp_.a_matrix_.index_.push_back(v);
                    model.lp_.a_matrix_.value_.push_back(Avc);
                }
            }
            model.lp_.a_matrix_.start_.push_back(
                model.lp_.a_matrix_.index_.size());
	    model.lp_.row_lower_.push_back(-1e30);
	    model.lp_.row_upper_.push_back(b[c]);
        } else if (n) {
            // add to col_lower_
            isvar[lastV] |= 1;
            model.lp_.col_lower_[lastV] = -b[c];
        } else {
            // add to col_upper_
            isvar[lastV] |= 2;
            model.lp_.col_upper_[lastV] = b[c];
        }
    }
    model.lp_.num_row_ = numColA + numColE;
    
    model.lp_.integrality_.resize(numRow);
    for (size_t v = 0; v < numRow; v++) {
        model.lp_.integrality_[v] = HighsVarType::kInteger;
    }

    Highs highs;
    HighsStatus return_status;

    const HighsLp &lp = highs.getLp();
}
