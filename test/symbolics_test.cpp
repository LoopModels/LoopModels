#include "../include/math.hpp"
#include "../include/symbolics.hpp"
#include "../include/show.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

TEST(SymbolicsTest, BasicAssertions) {
    // pseudorem
    Polynomial::Uninomial x{1};
    
    Polynomial::UnivariateTerm<intptr_t> y(x);

    Polynomial::UnivariateTerm<intptr_t> t0 = 3*x;
    Polynomial::Univariate<intptr_t> t1 = 3*x - 3;

    Polynomial::Univariate<intptr_t> p = 2*(x^10) + (x^7) + 7*(x^2) + 2*x + 3*x;

    printf("t0 = 3*x:\n");
    showln(t0);
    printf("t0 coef:\n");
    showln(t0.coefficient);
    printf("t0 exponent:\n");
    showln(t0.exponent);

    printf("t1 = 3*x - 3:\n");
    showln(t1);

    printf("p = 2x^10 + x^7 + 7x^2 + 5x:\n");
    showln(p);
    printf("p, num terms:\n");
    showln(p.terms.size());

    printf("Term<intptr_t,Uninomial>(1):\n");
    Polynomial::Term<intptr_t,Polynomial::Uninomial> tt(1);
    showln(tt);
    
    // Polynomial::Univariate<intptr_t> pp1 = (p + intptr_t(1));
    Polynomial::Univariate<intptr_t> pp1 = (p + 1);
    printf("pp1, p + 1:\n");
    showln(pp1);
    // Polynomial::Univariate<intptr_t> pp1v2(p);
    // pp1v2 += 1;
    // printf("pp1v2, p + 1:\n");
    // showln(pp1v2);
    
    Polynomial::Univariate<intptr_t> pp2 = (p + 2);
    printf("pp2, p + 2:\n");
    showln(pp2);
    Polynomial::Univariate<intptr_t> pp3 = (p + 3);
    printf("pp3, p + 3:\n");
    showln(pp3);
    Polynomial::Univariate<intptr_t> ppp12 = pp1 * pp2;
    printf("ppp12, (p+1) * (p+2):\n");
    showln(ppp12);
    Polynomial::Univariate<intptr_t> ppp12v2 = (p+1) * (p+2);
    printf("ppp12v2, (p+1) * (p+2):\n");
    showln(ppp12v2);
    printf("p = 2x^10 + x^7 + 7x^2 + 5x:\n");
    showln(p);
    printf("pp1, p + 1:\n");
    showln(pp1);
    EXPECT_EQ(ppp12, ppp12v2);

    Polynomial::Univariate<intptr_t> pppp = ppp12 * pp3;
    printf("pppp, (p+1) * (p+2) * (p+3):\n");
    showln(pppp);
    
    
    Polynomial::Univariate<intptr_t> q0 = (p + 1) * (p + 2) * (p + 3);
    printf("q0, (p + 1) * (p + 2) * (p + 3):\n");
    showln(q0);
    EXPECT_TRUE(Polynomial::pseudorem(q0, p) == 12582912);
    Polynomial::Univariate<intptr_t> q1 = (x^7) + 20;
    EXPECT_TRUE(Polynomial::pseudorem(q1, p) == q1);
    Polynomial::Univariate<intptr_t> r1 = Polynomial::pseudorem(p, q1);
    printf("r1, should be -40*(x^3) + 7*(x^2) + 5*x - 20:\n");
    showln(r1);
    Polynomial::Univariate<intptr_t> r1check =  (-40*(x^3) + 7*(x*x) + 5*x - 20);
    printf("r1 check:\n");
    showln(r1check);
    EXPECT_TRUE(r1 == (-40*(x^3) + 7*(x*x) + 5*x - 20));
    // EXPECT_TRUE(Polynomial::pseudorem(p, q1) == (-40*(x^3) + 7*(x*x) + 5*x - 20));
    // EXPECT_TRUE(Polynomial::pseudorem(p, q1) == (-40*(x^3) + 7*(x^2) + 5*x - 20));
    Polynomial::Univariate<intptr_t> q2 = (x^6) + 23;
    Polynomial::Univariate<intptr_t> r = Polynomial::pseudorem(p, q2);
    printf("r, should be -46*(x^4) + 7*(x^2) - 18*x:\n");
    showln(r);
    EXPECT_TRUE(Polynomial::pseudorem(p, q2) == (-46*(x^4) + 7*(x^2) - 18*x));
}

