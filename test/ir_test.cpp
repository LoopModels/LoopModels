#include "../include/ir.hpp"
#include "../include/math.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <llvm/ADT/ArrayRef.h>
#include <utility>

TEST(IRTest, BasicAssertions) {
    EXPECT_EQ(3, 3);
    Const a = Const{.NumType = Const::Int64, .i64 = 0};
    std::cout << a << std::endl;
    a = Const{.NumType = Const::Float64, .d = 2.3};
    std::cout << a << std::endl;
    a = Const{.NumType = Const::Float32, .f = 3.4f};
    std::cout << a << std::endl;
    // pretty print
    // We'll build an ArrayRef
    // i_2 (Induction Variable) +
    // 2 M_0 i_8 (Memory) +
    // (3 M_0 M_1) i_18 (Term) +
    // (5 + 7 M_0) i_3 (Induction Variable) +
    // (11 + 13 (M_0 M_2) + 17 (M_0 M_1 M_2)) i_0 (Induction Variable)
    // llvm::SmallVector<std::pair<size_t, SourceType>> inds(
    //     {std::make_pair(2, SourceType::LoopInductionVariable),
    //      std::make_pair(8, SourceType::Memory), std::make_pair(18, SourceType::LoopInductionVariable),
    //      std::make_pair(3, SourceType::LoopInductionVariable),
    //      std::make_pair(0, SourceType::LoopInductionVariable)});

    
    // std::vector<Int> coef_memory({1, 2, 3, 5, 7, 11, 13, 17});
    // std::vector<size_t> coef_offsets({0, 1, 2, 3, 5, 8});
    // VoV<Int> coef = VoV<Int>(toVector(coef_memory), toVector(coef_offsets));
    // llvm::SmallVector<size_t> pvc_memory({0, 0, 1, 0, 0, 2, 0, 1, 2});
    // std::vector<size_t> innerOffsets({0, 0, 0, 1, 0, 2, 0, 0, 1, 0, 0, 2, 5});
    // printf("innOff len: %d\n", innerOffsets.size());
    // std::vector<size_t> outerOffsets({0, 2, 4, 6, 9, 13});
    // std::cout << toVector(llvm::ArrayRef<size_t>(innerOffsets)) << std::endl;
    // size_t raw[16];
    // Vector<size_t, 0> memBuffer(raw, outerOffsets.size());
    // llvm::SmallVector<size_t> memBuffer().resize(outerOffsets.size());
    // VoVoV<size_t> pvc =
    //     VoVoV<size_t>(&pvc_memory.front(), toVector(innerOffsets),
    //                   toVector(outerOffsets), memBuffer);
    
    llvm::SmallVector<std::pair<Polynomial::Multivariate<intptr_t>, Source>,2> inds;
    inds.emplace_back(Polynomial::Multivariate<intptr_t>(1), Source(2, SourceType::LoopInductionVariable));
    inds.emplace_back(Polynomial::MultivariateTerm<intptr_t>(2, Polynomial::MonomialID(0)), Source(8, SourceType::Memory));
    inds.emplace_back(Polynomial::MultivariateTerm<intptr_t>(3, Polynomial::MonomialID(0, 1)), Source(18, SourceType::Term));
    Polynomial::Multivariate<intptr_t> p3(5);
    p3.add_term(Polynomial::MultivariateTerm<intptr_t>(7, Polynomial::MonomialID(0)));
    inds.emplace_back(p3, Source(3, SourceType::LoopInductionVariable));
    Polynomial::Multivariate<intptr_t> p4(11);
    p4.add_term(Polynomial::MultivariateTerm<intptr_t>(13, Polynomial::MonomialID(0,3)));
    p4.add_term(Polynomial::MultivariateTerm<intptr_t>(17, Polynomial::MonomialID(0,1,2)));
    p4.add_term(Polynomial::MultivariateTerm<intptr_t>(11, Polynomial::MonomialID(0,0,2)));
    inds.emplace_back(p4, Source(0, SourceType::LoopInductionVariable));
        
    ArrayRef ar = ArrayRef{.arrayID = 10, .inds = inds};
    std::cout << ar << std::endl;
    std::cout << "sizeof(TermBundle): " << sizeof(TermBundle) << std::endl;
}
