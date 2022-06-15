#include "../include/ArrayReference.hpp"
#include "../include/Math.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <llvm/ADT/ArrayRef.h>
#include <utility>

TEST(IRTest, BasicAssertions) {
    // Const a = Const{.NumType = Const::Int64, .i64 = 0};
    // std::cout << a << std::endl;
    // a = Const{.NumType = Const::Float64, .d = 2.3};
    // std::cout << a << std::endl;
    // a = Const{.NumType = Const::Float32, .f = 3.4f};
    // std::cout << a << std::endl;
    // pretty print
    // We'll build an ArrayRef
    // i_2 (Induction Variable) +
    // 2 M_0 i_8 (Memory) +
    // (3 M_0 M_1) i_18 (Term) +
    // (5 + 7 M_0) i_3 (Induction Variable) +
    // (11 + 13 (M_0 M_2) + 17 (M_0 M_1 M_2)) i_0 (Induction Variable)
    // llvm::SmallVector<std::pair<size_t, VarType>> inds(
    //     {std::make_pair(2, VarType::LoopInductionVariable),
    //      std::make_pair(8, VarType::Memory), std::make_pair(18,
    //      VarType::LoopInductionVariable), std::make_pair(3,
    //      VarType::LoopInductionVariable), std::make_pair(0,
    //      VarType::LoopInductionVariable)});

    // std::vector<int64_t> coef_memory({1, 2, 3, 5, 7, 11, 13, 17});
    // std::vector<size_t> coef_offsets({0, 1, 2, 3, 5, 8});
    // VoV<int64_t> coef = VoV<int64_t>(toVector(coef_memory), toVector(coef_offsets));
    // llvm::SmallVector<size_t> pvc_memory({0, 0, 1, 0, 0, 2, 0, 1, 2});
    // std::vector<size_t> innerOffsets({0, 0, 0, 1, 0, 2, 0, 0, 1, 0, 0, 2,
    // 5}); printf("innOff len: %d\n", innerOffsets.size()); std::vector<size_t>
    // outerOffsets({0, 2, 4, 6, 9, 13}); std::cout <<
    // toVector(llvm::ArrayRef<size_t>(innerOffsets)) << std::endl; size_t
    // raw[16]; Vector<size_t, 0> memBuffer(raw, outerOffsets.size());
    // llvm::SmallVector<size_t> memBuffer().resize(outerOffsets.size());
    // VoVoV<size_t> pvc =
    //     VoVoV<size_t>(&pvc_memory.front(), toVector(innerOffsets),
    //                   toVector(outerOffsets), memBuffer);

    llvm::SmallVector<
        std::pair<Polynomial::Multivariate<int64_t, Polynomial::Monomial>,
                  VarID>,
        2>
        inds;
    inds.emplace_back(
        Polynomial::Multivariate<int64_t, Polynomial::Monomial>(1),
        VarID(2, VarType::LoopInductionVariable));
    inds.emplace_back(
        Polynomial::MultivariateTerm<int64_t, Polynomial::Monomial>(
            2, Polynomial::Monomial(Polynomial::ID{0})),
        VarID(8, VarType::Memory));
    inds.emplace_back(
        Polynomial::MultivariateTerm<int64_t, Polynomial::Monomial>(
            3, Polynomial::Monomial(Polynomial::ID{0}, Polynomial::ID{1})),
        VarID(18, VarType::Term));
    Polynomial::Multivariate<int64_t, Polynomial::Monomial> p3(5);
    p3.addTerm(Polynomial::MultivariateTerm<int64_t, Polynomial::Monomial>(
        7, Polynomial::Monomial(Polynomial::ID{0})));
    inds.emplace_back(p3, VarID(3, VarType::LoopInductionVariable));
    Polynomial::Multivariate<int64_t, Polynomial::Monomial> p4(11);
    p4.addTerm(Polynomial::MultivariateTerm<int64_t, Polynomial::Monomial>(
        13, Polynomial::Monomial(Polynomial::ID{0}, Polynomial::ID{3})));
    p4.addTerm(Polynomial::MultivariateTerm<int64_t, Polynomial::Monomial>(
        17, Polynomial::Monomial(Polynomial::ID{0}, Polynomial::ID{1},
                                 Polynomial::ID{2})));
    p4.addTerm(Polynomial::MultivariateTerm<int64_t, Polynomial::Monomial>(
        11, Polynomial::Monomial(Polynomial::ID{0}, Polynomial::ID{0},
                                 Polynomial::ID{2})));
    inds.emplace_back(p4, VarID(0, VarType::LoopInductionVariable));

    // ArrayReferenceFlat ar{.arrayID = 10, .inds = inds};
    // std::cout << ar << std::endl;
    // std::cout << "sizeof(TermBundle): " << sizeof(TermBundle) << std::endl;
}
