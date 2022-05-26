#pragma once
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include "NormalForm.hpp"
#include "lp_data/HConst.h"
#include "llvm/ADT/SmallVector.h"
#include <Highs.h>
#include <cmath>
#include <cstddef>
#include <cstdint>

// use ILP solver for eliminating redundant constraints

bool constraintIsRedundant(IntMatrix auto &A,
                           llvm::SmallVectorImpl<intptr_t> &b,
                           IntMatrix auto &E,
                           llvm::SmallVectorImpl<intptr_t> &q, const size_t C) {

    Highs highs;
    auto [numVar, numColA] = A.size();
    size_t numColE = q.size();
    HighsModel model;
    model.lp_.sense_ = ObjSense::kMaximize;
    model.lp_.offset_ = 0;
    model.lp_.col_cost_.reserve(numVar);
    for (auto &a : A.getCol(C)) {
        model.lp_.col_cost_.push_back(a);
    }
    model.lp_.num_col_ = numVar;
    // col_lower_ <= x <= col_upper_
    // row_lower_ <= A*x <= row_upper_
    // We want to be reasonable efficient in partioning constraints into
    // former vs latter.
    //
    model.lp_.a_matrix_.format_ = MatrixFormat::kRowwise;

    model.lp_.col_lower_.resize(numVar, -highs.getInfinity());
    model.lp_.col_upper_.resize(numVar, highs.getInfinity());
    size_t numCol = 0;
    llvm::SmallVector<uint8_t, 64> isvar(numVar);
    intptr_t target = b[C] + 1;
    for (size_t c = 0; c < numColA; ++c) {
        size_t n = 0, p = 0, nz = 0;
        intptr_t lastV = -1;
        for (size_t v = 0; v < numVar; ++v) {
            intptr_t Avc = A(v, c);
            nz += (Avc != 0);
            p += (Avc == 1);
            n += (Avc == -1);
            lastV = Avc ? v : lastV;
        }
        if ((nz != 1) || ((n + p) != 1) || (isvar[lastV] & (n + 2 * p))) {
            // add to row_
            for (size_t v = 0; v < numVar; ++v) {
                if (intptr_t Avc = A(v, c)) {
                    model.lp_.a_matrix_.index_.push_back(v);
                    model.lp_.a_matrix_.value_.push_back(Avc);
                }
            }
            assert(model.lp_.a_matrix_.index_.size());
            model.lp_.a_matrix_.start_.push_back(
                model.lp_.a_matrix_.index_.size());
            model.lp_.row_lower_.push_back(-highs.getInfinity());
            model.lp_.row_upper_.push_back(b[c] + (c == C));
            ++numCol;
        } else if (n) {
            // add to col_lower_
            isvar[lastV] |= 1;
            model.lp_.col_lower_[lastV] = -b[c] - (c == C);
            // if (c == C) {
            //     // we're minimizing instead
            //     // model.lp_.sense_ = ObjSense::kMinimize;
            //     model.lp_.col_lower_[lastV] = -b[c] - 1;
            // } else {
            //     model.lp_.col_lower_[lastV] = -b[c];
            // }
        } else {
            // add to col_upper_
            isvar[lastV] |= 2;
            model.lp_.col_upper_[lastV] = b[c] + (c == C);
        }
    }
    for (size_t c = 0; c < numColE; ++c) {
        for (size_t v = 0; v < numVar; ++v) {
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

    model.lp_.integrality_.resize(numVar, HighsVarType::kInteger);
    printVector(std::cout << "value= ", model.lp_.a_matrix_.value_)
        << std::endl;
    printVector(std::cout << "index= ", model.lp_.a_matrix_.index_)
        << std::endl;
    printVector(std::cout << "start= ", model.lp_.a_matrix_.start_)
        << std::endl;
    printVector(std::cout << "cost= ", model.lp_.col_cost_) << std::endl;
    printVector(std::cout << "Var lb = ", model.lp_.col_lower_) << std::endl;
    printVector(std::cout << "Var ub = ", model.lp_.col_upper_) << std::endl;
    printVector(std::cout << "Constraint lb = ", model.lp_.row_lower_)
        << std::endl;
    printVector(std::cout << "Constraint ub = ", model.lp_.row_upper_)
        << std::endl;
    std::cout << "target = " << target << std::endl;
    HighsStatus return_status = highs.passModel(std::move(model));
    assert(return_status == HighsStatus::kOk);

    // const HighsLp &lp = highs.getLp();
    // highs.changeColsIntegrality();
    return_status = highs.run();
    assert(return_status == HighsStatus::kOk);

    const HighsModelStatus &model_status = highs.getModelStatus();
    std::cout << "Objective function value: "
              << highs.getInfo().objective_function_value << std::endl;
    std::cout << "Model status: " << highs.modelStatusToString(model_status)
              << std::endl;
    assert(model_status == HighsModelStatus::kOptimal);

    double obj = highs.getInfo().objective_function_value;
    bool redundant = !std::isnan(obj) && (obj != target);
    std::cout << "highs.getInfo().objective_function_value = "
              << highs.getInfo().objective_function_value
              << "; target = " << target << "; neq = " << redundant
              << std::endl;
    ;
    return redundant;
}

void pruneBounds(IntMatrix auto &A, llvm::SmallVectorImpl<intptr_t> &b,
                 IntMatrix auto &E, llvm::SmallVectorImpl<intptr_t> &q) {
    NormalForm::simplifyEqualityConstraints(E, q);
    for (size_t c = A.numCol(); c > 0;) {
        if (constraintIsRedundant(A, b, E, q, --c)) {
            std::cout << "dropping constraint c = " << c << std::endl;
            A.eraseCol(c);
            b.erase(b.begin() + c);
        }
    }
}

void fourierMotzkin(IntMatrix auto &Anew, llvm::SmallVectorImpl<intptr_t> &bnew,
                    IntMatrix auto &Enew, llvm::SmallVectorImpl<intptr_t> &qnew,
                    IntMatrix auto &A, llvm::SmallVectorImpl<intptr_t> &b,
                    IntMatrix auto &E, llvm::SmallVectorImpl<intptr_t> &q,
                    size_t i) {

    const auto [numRow, numColA] = A.size();
    const size_t numColE = E.numCol();
    size_t countNeg = 0, countPos = 0, countEq = 0;
    for (size_t j = 0; j < numColA; ++j) {
        countNeg += (A(i, j) < 0);
        countPos += (A(i, j) > 0);
    }
    for (size_t j = 0; j < numColE; ++j) {
        countEq += (E(i, j) != 0);
    }
    size_t newColA = numColA - countNeg - countPos +
                     (countNeg + countEq) * (countPos + countEq) -
                     countEq * countEq;

    Anew.resize(numRow, newColA);
    bnew.resize(newColA);
    size_t newColE = numColE - countEq + countEq * countEq;
    Enew.resize(numRow, newColE);
    qnew.resize(newColE);

    size_t a = 0;
    for (size_t j = 0; j < numColA; ++j) {
        if (intptr_t Aij = A(i, j)) {
            for (size_t k = 0; k < j; ++k) {
                if ((A(i, k) == 0) || ((Aij > 0) == (A(i, k) > 0)))
                    continue;
                if (IntegerPolyhedra::setBounds(Anew.getCol(a), bnew[a],
                                                A.getCol(j), b[j], A.getCol(k),
                                                b[k], i) &&
                    IntegerPolyhedra::uniqueConstraint(Anew, bnew, a)) {
                    ++a;
                }
            }
            for (size_t k = 0; k < numColE; ++k) {
                if (E(i, k) == 0)
                    continue;
                if ((E(i, k) > 0) == (Aij > 0)) {
                    for (size_t v = 0; v < numRow; ++v) {
                        E(v, k) *= -1;
                    }
                    q[k] *= -1;
                }
                if (IntegerPolyhedra::setBounds(Anew.getCol(a), bnew[a],
                                                A.getCol(j), b[j], E.getCol(k),
                                                q[k], i) &&
                    IntegerPolyhedra::uniqueConstraint(Anew, bnew, a)) {
                    ++a;
                }
            }
        } else {
            for (size_t v = 0; v < numRow; ++v) {
                Anew(v, a) = A(v, j);
            }
            bnew[a] = b[j];
            if (IntegerPolyhedra::uniqueConstraint(Anew, bnew, a)) {
                ++a;
            }
        }
    }
    Anew.resize(numRow, a);
    bnew.resize(a);
    size_t e = 0;
    for (size_t j = 0; j < numColE; ++j) {
        if (intptr_t Eij = E(i, j)) {
            for (size_t k = 0; k < j; ++k) {
                if (k == j)
                    continue;
                if (intptr_t Eik = E(i, k)) {
                    intptr_t g = std::gcd(Eij, Eik);
                    intptr_t Ejg = Eij / g;
                    intptr_t Ekg = Eik / g;
                    for (size_t v = 0; v < numRow; ++v) {
                        Enew(v, e) = Ejg * E(v, k) - Ekg * E(v, j);
                    }
                    qnew[e] = Ejg * q[k] - Ekg * q[j];
                    ++e;
                }
            }
        } else {
            for (size_t v = 0; v < numRow; ++v) {
                Enew(v, e) = E(v, j);
            }
            qnew[e] = q[j];
            ++e;
        }
    }
    Enew.resize(numRow, e);
    qnew.resize(e);
}
