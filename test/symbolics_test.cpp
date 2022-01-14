#include "../include/math.hpp"
#include "../include/show.hpp"
#include "../include/symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

TEST(pseudoRemTests, BasicAssertions) {
    // pseudorem
    Polynomial::Uninomial x{1};

    Polynomial::UnivariateTerm<intptr_t> y(x);

    Polynomial::UnivariateTerm<intptr_t> t0 = 3 * x;
    Polynomial::Univariate<intptr_t> t1 = 3 * x - 3;

    Polynomial::Univariate<intptr_t> p =
        2 * (x ^ 10) + (x ^ 7) + 7 * (x ^ 2) + 2 * x + 3 * x;

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
    Polynomial::Term<intptr_t, Polynomial::Uninomial> tt(1);
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
    Polynomial::Univariate<intptr_t> ppp12v2 = (p + 1) * (p + 2);
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
    Polynomial::Univariate<intptr_t> q1 = (x ^ 7) + 20;
    EXPECT_TRUE(Polynomial::pseudorem(q1, p) == q1);
    Polynomial::Univariate<intptr_t> r1 = Polynomial::pseudorem(p, q1);
    printf("r1, should be -40*(x^3) + 7*(x^2) + 5*x - 20:\n");
    showln(r1);
    Polynomial::Univariate<intptr_t> r1check =
        (-40 * (x ^ 3) + 7 * (x * x) + 5 * x - 20);
    printf("r1 check:\n");
    showln(r1check);
    EXPECT_TRUE(r1 == (-40 * (x ^ 3) + 7 * (x * x) + 5 * x - 20));
    // EXPECT_TRUE(Polynomial::pseudorem(p, q1) == (-40*(x^3) + 7*(x*x) + 5*x -
    // 20)); EXPECT_TRUE(Polynomial::pseudorem(p, q1) == (-40*(x^3) + 7*(x^2) +
    // 5*x - 20));
    Polynomial::Univariate<intptr_t> q2 = (x ^ 6) + 23;
    Polynomial::Univariate<intptr_t> r = Polynomial::pseudorem(p, q2);
    printf("r, should be -46*(x^4) + 7*(x^2) - 18*x:\n");
    showln(r);
    EXPECT_TRUE(Polynomial::pseudorem(p, q2) ==
                (-46 * (x ^ 4) + 7 * (x ^ 2) - 18 * x));
}

TEST(PseudoRemTests, BasicAssertions) {
    Polynomial::Monomial x = Polynomial::MonomialID(0);
    Polynomial::Monomial y = Polynomial::MonomialID(1);
    Polynomial::Monomial z = Polynomial::MonomialID(2);
    Polynomial::Multivariate<intptr_t> xp1z = x * z + z;
    Polynomial::Multivariate<intptr_t> c0v2 = 10 * xp1z;

    // Polynomial::Multivariate<intptr_t> c0 = 10*(x*z + z);
    Polynomial::Multivariate<intptr_t> c0 = 10 * (x * z + x);
    Polynomial::Multivariate<intptr_t> c1 = 2 * ((x ^ 2) + z);
    Polynomial::Multivariate<intptr_t> c2 = 2 * (2 - z);
    Polynomial::Multivariate<intptr_t> c3 = 20 * (x * (z ^ 2));

    intptr_t e0 = 0;
    intptr_t e1 = 5;
    intptr_t e2 = 7;
    intptr_t e3 = 10;

    showln(x);
    showln(y);
    showln(z);
    Polynomial::Multivariate<intptr_t> p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    printf("Polynomial p:\n");
    showln(p);
    for (auto it = p.begin(); it != p.end(); ++it) {
        printf("prodIDs:\n");
        showln((it->monomial()).prodIDs);
    }
    printf("\n");

    Polynomial::Univariate<Polynomial::Multivariate<intptr_t>> ppy =
        Polynomial::multivariateToUnivariate(p, 1);
    printf("Number of terms in p: %d \n", int(ppy.terms.size()));
    printf("c3:\n");
    std::cout << c3 << std::endl;
    printf("coef 0:\n");
    showln(ppy.terms[0].coefficient);

    printf("c2:\n");
    std::cout << c2 << std::endl;
    printf("coef 1:\n");
    showln(ppy.terms[1].coefficient);

    printf("c1:\n");
    // showln(c1);
    std::cout << c1 << std::endl;
    printf("coef 2:\n");
    showln(ppy.terms[2].coefficient);

    printf("c0:\n");
    std::cout << c0 << std::endl;
    // showln(c0);
    printf("coef 3:\n");
    showln(ppy.terms[3].coefficient);

    EXPECT_TRUE(ppy.terms[0].coefficient == c3);
    EXPECT_TRUE(ppy.terms[1].coefficient == c2);
    EXPECT_TRUE(ppy.terms[2].coefficient == c1);
    EXPECT_TRUE(ppy.terms[3].coefficient == c0);

    EXPECT_EQ(ppy.terms[0].exponent.exponent, e3);
    EXPECT_EQ(ppy.terms[1].exponent.exponent, e2);
    EXPECT_EQ(ppy.terms[2].exponent.exponent, e1);
    EXPECT_EQ(ppy.terms[3].exponent.exponent, e0);

    Polynomial::Multivariate<intptr_t> a = x * y + y;
    Polynomial::Multivariate<intptr_t> b = y * z + y;
    printf("gcd(a,b) == M:\n");
    EXPECT_TRUE(Polynomial::gcd(a, b) == Polynomial::Multivariate<intptr_t>(y));
    printf("GCD: ");
    showln(gcd(a, b)); // we have N + 2? aka z + 1???
    printf("y:  ");
    showln(y);
    printf("Multivariate(y):  ");
    showln(Polynomial::Multivariate<intptr_t>(y));

    // Polynomial::Multivariate<intptr_t> q = p * (p + 1);
    Polynomial::Multivariate<intptr_t> q = p * (p + 1) * (p + 2) * (p + 3);
    // Polynomial::Multivariate<intptr_t> q = p * (p + 1) * (p + 2); //* (p +
    // 3);
    printf("q:\n");
    std::cout << q << std::endl;
    printf("p:\n");
    std::cout << p << std::endl;
    printf("gcd(p, q):\n");
    std::cout << gcd(p, q) << std::endl;
    // showln(gcd(p, q));
    /*
    printf("p+1:\n");
    showln(p+1);
    printf("gcd(p+1, q):\n");
    showln(gcd(p+1, q));
    printf("p+2:\n");
    showln(p+2);
    printf("gcd(p+2, q):\n");
    showln(gcd(p+2, q));
    printf("p+3:\n");
    showln(p+3);
    printf("gcd(p+3, q):\n");
    showln(gcd(p+3, q));
    */

    EXPECT_TRUE(Polynomial::gcd(p, q) == p);
    EXPECT_TRUE(Polynomial::gcd(p + 1, q) == p + 1);
    EXPECT_TRUE(Polynomial::gcd(p + 2, q) == p + 2);
    EXPECT_TRUE(Polynomial::gcd(p + 3, q) == p + 3);

    Polynomial::Multivariate<intptr_t> k = (y ^ 2) + 1;
    EXPECT_TRUE(Polynomial::gcd(x * k, z * k) == k);
    EXPECT_TRUE(Polynomial::gcd(z * k, x * k) == k);
    EXPECT_TRUE(Polynomial::gcd(x * k, (z + 1) * k) == k);
    EXPECT_TRUE(Polynomial::gcd((z + 1) * k, x * k) == k);
    EXPECT_TRUE(Polynomial::gcd(x * k, p * k) == k);
    EXPECT_TRUE(Polynomial::gcd(p * k, x * k) == k);

    Polynomial::Term<intptr_t, Polynomial::Monomial> twoxy = 2 * (x * y);
    Polynomial::Multivariate<intptr_t> twoxyplusx = (2 * x) * y + x;
    EXPECT_TRUE(Polynomial::gcd(twoxy, twoxyplusx) == x);
    EXPECT_TRUE(Polynomial::gcd(twoxyplusx, twoxy) == x);

    Polynomial::Multivariate<intptr_t> c = x * y + y;
    Polynomial::Multivariate<intptr_t> d = -1 * c;
    EXPECT_TRUE(Polynomial::gcd(c, d) == c);
    EXPECT_TRUE(Polynomial::gcd(d, c) == c);

    Polynomial::Multivariate<intptr_t> ps = (x^2) - (y^2);
    Polynomial::Multivariate<intptr_t> qs = x + y;

    EXPECT_TRUE(Polynomial::divExact(ps, gcd(ps, qs)) == (x - y));
}
