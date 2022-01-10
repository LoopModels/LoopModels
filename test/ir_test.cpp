#include "../include/ir.hpp"
#include "../include/math.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

TEST(IRTest, BasicAssertions) {
    EXPECT_EQ(3, 3);
    Const a = Const{.type = Int64, .bits = 0};
    showln(a);
    a = Const{.type = Float64, .bits = 0x4002666666666666};
    showln(a);
    a = Const{.type = Float32, .bits = 0x0000000040133333};
    showln(a);
    // pretty print
    // showln(ArrayRef ar);
    // We'll build an ArrayRef
    // i_2 (Induction Variable) +
    // 2 M_0 i_8 (Memory) +
    // (3 M_0 M_1) i_18 (Term) +
    // (5 + 7 M_0) i_3 (Induction Variable) +
    // (11 + 13 (M_0 M_2) + 17 (M_0 M_1 M_2)) i_0 (Induction Variable)
    // std::vector<std::pair<size_t, SourceType>> inds(
    //     {std::make_pair(2, SourceType::LoopInductionVariable),
    //      std::make_pair(8, SourceType::Memory), std::make_pair(18, SourceType::LoopInductionVariable),
    //      std::make_pair(3, SourceType::LoopInductionVariable),
    //      std::make_pair(0, SourceType::LoopInductionVariable)});

    
    std::vector<Int> coef_memory({1, 2, 3, 5, 7, 11, 13, 17});
    std::vector<size_t> coef_offsets({0, 1, 2, 3, 5, 8});
    VoV<Int> coef = VoV<Int>(toVector(coef_memory), toVector(coef_offsets));
    std::vector<size_t> pvc_memory({0, 0, 1, 0, 0, 2, 0, 1, 2});
    std::vector<size_t> innerOffsets({0, 0, 0, 1, 0, 2, 0, 0, 1, 0, 0, 2, 5});
    // printf("innOff len: %d\n", innerOffsets.size());
    std::vector<size_t> outerOffsets({0, 2, 4, 6, 9, 13});
    showln(toVector(innerOffsets));
    size_t raw[16];
    Vector<size_t, 0> memBuffer(raw, outerOffsets.size());
    // std::vector<size_t> memBuffer().resize(outerOffsets.size());
    VoVoV<size_t> pvc =
        VoVoV<size_t>(&pvc_memory.front(), toVector(innerOffsets),
                      toVector(outerOffsets), memBuffer);

    std::vector<std::pair<Polynomial::Multivariate<intptr_t>, Source>> inds;
    inds.emplace_back(Polynomial::Multivariate<intptr_t>(1), Source(2, SourceType::LoopInductionVariable));
    inds.emplace_back(Polynomial::MultivariateTerm<intptr_t>(2, Polynomial::Monomial(std::vector<uint_fast32_t>{0})), Source(8, SourceType::Memory));
    inds.emplace_back(Polynomial::MultivariateTerm<intptr_t>(3, Polynomial::Monomial(std::vector<uint_fast32_t>{0, 1})), Source(18, SourceType::Term));
    Polynomial::Multivariate<intptr_t> p3(5);
    p3.add_term(Polynomial::MultivariateTerm<intptr_t>(7, Polynomial::Monomial(std::vector<uint_fast32_t>{0})));
    inds.emplace_back(p3, Source(3, SourceType::LoopInductionVariable));
    Polynomial::Multivariate<intptr_t> p4(11);
    p4.add_term(Polynomial::MultivariateTerm<intptr_t>(13, Polynomial::Monomial(std::vector<uint_fast32_t>{0,3})));
    p4.add_term(Polynomial::MultivariateTerm<intptr_t>(17, Polynomial::Monomial(std::vector<uint_fast32_t>{0,1,2})));
    p4.add_term(Polynomial::MultivariateTerm<intptr_t>(11, Polynomial::Monomial(std::vector<uint_fast32_t>{0,0,2})));
    inds.emplace_back(p4, Source(0, SourceType::LoopInductionVariable));
        
    ArrayRef ar = ArrayRef{.arrayId = 10, .inds = inds};
    showln(ar);
}
