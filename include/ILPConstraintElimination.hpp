#pragma once
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "NormalForm.hpp"
#include "lp_data/HConst.h"
#include "llvm/ADT/SmallVector.h"
#include <Highs.h>
#include <cstddef>
#include <cstdint>

// use ILP solver for eliminating redundant constraints

bool constraintIsRedundant(IntMatrix auto &A,
                           llvm::SmallVectorImpl<intptr_t> &b,
                           IntMatrix auto &E,
                           llvm::SmallVectorImpl<intptr_t> &q, const size_t C) {

    auto [numRow, numColA] = A.size();
    size_t numColE = q.size();
    HighsModel model;
    model.lp_.sense_ = ObjSense::kMaximize;
    model.lp_.offset_ = 0;
    model.lp_.col_cost_.reserve(numRow);
    for (auto &a : A.getCol(C)) {
        model.lp_.col_cost_.push_back(a);
    }
    model.lp_.num_col_ = numRow;
    // col_lower_ <= x <= col_upper_
    // row_lower_ <= A*x <= row_upper_
    // We want to be reasonable efficient in partioning constraints into
    // former vs latter.
    //
    model.lp_.a_matrix_.format_ = MatrixFormat::kRowwise;

    model.lp_.col_lower_.resize(numRow, -1e30);
    model.lp_.col_upper_.resize(numRow, 1e30);
    size_t numCol = 0;
    llvm::SmallVector<uint8_t, 64> isvar(numRow);
    intptr_t target = b[C] + 1;
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
            assert(model.lp_.a_matrix_.index_.size());
            model.lp_.a_matrix_.start_.push_back(
                model.lp_.a_matrix_.index_.size());
            model.lp_.row_lower_.push_back(-1e30);
            model.lp_.row_upper_.push_back(b[c] + (c == C));
            ++numCol;
        } else if (n) {
            // add to col_lower_
            isvar[lastV] |= 1;
            if (c == C) {
                // we're minimizing instead
                // model.lp_.sense_ = ObjSense::kMinimize;
                model.lp_.col_lower_[lastV] = -b[c] - 1;
            } else {
                model.lp_.col_lower_[lastV] = -b[c];
            }
        } else {
            // add to col_upper_
            isvar[lastV] |= 2;
            model.lp_.col_upper_[lastV] = b[c] + (c == C);
        }
    }
    for (size_t c = 0; c < numColE; ++c) {
        for (size_t v = 0; v < numRow; ++v) {
            if (intptr_t Evc = E(v, c)) {
                model.lp_.a_matrix_.index_.push_back(v);
                model.lp_.a_matrix_.value_.push_back(Evc);
            }
        }
        assert(model.lp_.a_matrix_.index_.size());
        model.lp_.a_matrix_.start_.push_back(model.lp_.a_matrix_.index_.size());
        intptr_t qc = q[c];
        model.lp_.row_lower_.push_back(qc);
        model.lp_.row_upper_.push_back(qc);
    }
    model.lp_.num_row_ = numCol + numColE;

    model.lp_.integrality_.resize(numRow);
    for (size_t v = 0; v < numRow; v++) {
        model.lp_.integrality_[v] = HighsVarType::kInteger;
    }
    printVector(std::cout << "value= ", model.lp_.a_matrix_.value_)
        << std::endl;
    printVector(std::cout << "index= ", model.lp_.a_matrix_.index_)
        << std::endl;
    printVector(std::cout << "start= ", model.lp_.a_matrix_.start_)
        << std::endl;
    printVector(std::cout << "cost= ", model.lp_.col_cost_) << std::endl;
    std::cout << "target = " << target << std::endl;
    Highs highs;
    HighsStatus return_status = highs.passModel(model);
    assert(return_status == HighsStatus::kOk);

    // const HighsLp &lp = highs.getLp();

    return_status = highs.run();
    assert(return_status == HighsStatus::kOk);

    const HighsModelStatus &model_status = highs.getModelStatus();
    std::cout << "Objective function value: "
              << highs.getInfo().objective_function_value << std::endl;
    assert(model_status == HighsModelStatus::kOptimal);

    return highs.getInfo().objective_function_value != target;
}

void pruneBounds(IntMatrix auto &A, llvm::SmallVectorImpl<intptr_t> &b,
                 IntMatrix auto &E, llvm::SmallVectorImpl<intptr_t> &q) {
    NormalForm::simplifyEqualityConstraints(E, q);
    for (size_t c = A.numCol(); c > 0;) {
        if (constraintIsRedundant(A, b, E, q, --c)) {
            A.eraseCol(c);
            b.erase(b.begin() + c);
        }
    }
}
