#include "../include/Math.hpp"
#include "../include/Show.hpp"
#include "../include/Symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <utility>

TEST(pseudoRemTests, BasicAssertions) {
    // pseudorem
    Polynomial::Uninomial x{1};
    Polynomial::UnivariateTerm<int64_t> y(x);

    Polynomial::UnivariateTerm<int64_t> t0 = 3 * x;
    Polynomial::Univariate<int64_t> t1 = 3 * x - 3;

    Polynomial::Univariate<int64_t> p =
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

    printf("Term<int64_t,Uninomial>(1):\n");
    Polynomial::Term<int64_t, Polynomial::Uninomial> tt(1);
    showln(tt);

    // Polynomial::Univariate<int64_t> pp1 = (p + int64_t(1));
    Polynomial::Univariate<int64_t> pp1 = (p + 1);
    printf("pp1, p + 1:\n");
    showln(pp1);
    // Polynomial::Univariate<int64_t> pp1v2(p);
    // pp1v2 += 1;
    // printf("pp1v2, p + 1:\n");
    // showln(pp1v2);

    Polynomial::Univariate<int64_t> pp2 = (p + 2);
    printf("pp2, p + 2:\n");
    showln(pp2);
    Polynomial::Univariate<int64_t> pp3 = (p + 3);
    printf("pp3, p + 3:\n");
    showln(pp3);
    Polynomial::Univariate<int64_t> ppp12 = pp1 * pp2;
    printf("ppp12, (p+1) * (p+2):\n");
    showln(ppp12);
    Polynomial::Univariate<int64_t> ppp12v2 = (p + 1) * (p + 2);
    printf("ppp12v2, (p+1) * (p+2):\n");
    showln(ppp12v2);
    printf("p = 2x^10 + x^7 + 7x^2 + 5x:\n");
    showln(p);
    printf("pp1, p + 1:\n");
    showln(pp1);
    EXPECT_EQ(ppp12, ppp12v2);

    Polynomial::Univariate<int64_t> pppp = ppp12 * pp3;
    printf("pppp, (p+1) * (p+2) * (p+3):\n");
    showln(pppp);

    Polynomial::Univariate<int64_t> q0 = (p + 1) * (p + 2) * (p + 3);
    printf("q0, (p + 1) * (p + 2) * (p + 3):\n");
    showln(q0);
    std::cout << "pseudorem(q0, p) = " << Polynomial::pseudorem(q0, p)
              << " == 12582912" << std::endl;
    EXPECT_TRUE(Polynomial::pseudorem(q0, p) == 12582912);
    Polynomial::Univariate<int64_t> q1 = (x ^ 7) + 20;
    EXPECT_TRUE(Polynomial::pseudorem(q1, p) == q1);
    Polynomial::Univariate<int64_t> r1 = Polynomial::pseudorem(p, q1);
    printf("r1, should be -40*(x^3) + 7*(x^2) + 5*x - 20:\n");
    showln(r1);
    Polynomial::Univariate<int64_t> r1check =
        (-40 * (x ^ 3) + 7 * (x * x) + 5 * x - 20);
    printf("r1 check:\n");
    showln(r1check);
    EXPECT_TRUE(r1 == (-40 * (x ^ 3) + 7 * (x * x) + 5 * x - 20));
    // EXPECT_TRUE(Polynomial::pseudorem(p, q1) == (-40*(x^3) + 7*(x*x) + 5*x -
    // 20)); EXPECT_TRUE(Polynomial::pseudorem(p, q1) == (-40*(x^3) + 7*(x^2) +
    // 5*x - 20));
    Polynomial::Univariate<int64_t> q2 = (x ^ 6) + 23;
    Polynomial::Univariate<int64_t> r = Polynomial::pseudorem(p, q2);
    printf("r, should be -46*(x^4) + 7*(x^2) - 18*x:\n");
    showln(r);
    EXPECT_TRUE(Polynomial::pseudorem(p, q2) ==
                (-46 * (x ^ 4) + 7 * (x ^ 2) - 18 * x));
}

TEST(MonomialTests, BasicAssertions) {
    Polynomial::Monomial x = Polynomial::Monomial(Polynomial::ID{0});
    Polynomial::Monomial y = Polynomial::Monomial(Polynomial::ID{1});
    Polynomial::Monomial z = Polynomial::Monomial(Polynomial::ID{2});

    Polynomial::Monomial xxyz = x*x*y*z;
    Polynomial::Monomial xyzz = x*y*z*z;
    EXPECT_TRUE(gcd(xxyz,xyzz) == x*y*z);
    EXPECT_TRUE(gcd(x*y,z).prodIDs.size() == 0);

    Polynomial::Monomial d;
    EXPECT_FALSE(tryDiv(d, xxyz, z));
    EXPECT_TRUE(d == x*x*y);
    EXPECT_TRUE(tryDiv(d, xxyz, xyzz));
}

TEST(MultivariateMonomialTests, BasicAssertions) {
    Polynomial::Monomial x = Polynomial::Monomial(Polynomial::ID{0});
    Polynomial::Monomial y = Polynomial::Monomial(Polynomial::ID{1});
    Polynomial::Monomial z = Polynomial::Monomial(Polynomial::ID{2});
    typedef Polynomial::Multivariate<int64_t, Polynomial::Monomial>
        MultivariatePolynomial;
    MultivariatePolynomial xp1z = x * z + z;
    MultivariatePolynomial c0v2 = 10 * xp1z;

    // MultivariatePolynomial c0 = 10*(x*z + z);
    MultivariatePolynomial c0 = 10 * (x * z + x);
    MultivariatePolynomial c1 = 2 * ((x ^ 2) + z);
    MultivariatePolynomial c2 = 2 * (2 - z);
    MultivariatePolynomial c3 = 20 * (x * (z ^ 2));

    int64_t e0 = 0;
    int64_t e1 = 5;
    int64_t e2 = 7;
    int64_t e3 = 10;

    showln(x);
    showln(y);
    showln(z);
    MultivariatePolynomial p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    printf("Polynomial p:\n");
    showln(p);
    for (auto it = p.begin(); it != p.end(); ++it) {
        printf("prodIDs:\n");
        showln((it->monomial()).prodIDs);
    }
    printf("\n");

    Polynomial::Univariate<MultivariatePolynomial> ppy =
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

    MultivariatePolynomial a = x * y + y;
    MultivariatePolynomial b = y * z + y;
    printf("gcd(a,b) == M:\n");
    EXPECT_TRUE(Polynomial::gcd(a, b) == MultivariatePolynomial(y));
    printf("GCD: ");
    showln(gcd(a, b)); // we have N + 2? aka z + 1???
    printf("y:  ");
    showln(y);
    printf("Multivariate(y):  ");
    showln(MultivariatePolynomial(y));

    // MultivariatePolynomial q = p * (p + 1);
    MultivariatePolynomial q = p * (p + 1) * (p + 2) * (p + 3);
    // MultivariatePolynomial q = p * (p + 1) * (p + 2); //* (p +
    // 3);

    printf("q:\n");
    std::cout << q << std::endl;
    printf("p:\n");
    std::cout << p << std::endl;
    printf("gcd(p, q):\n");
    std::cout << gcd(p, q) << std::endl;
    // showln(gcd(p, q));
    
    EXPECT_TRUE(Polynomial::gcd(p, q) == p);
    EXPECT_TRUE(Polynomial::gcd(p + 1, q) == p + 1);
    EXPECT_TRUE(Polynomial::gcd(p + 2, q) == p + 2);
    EXPECT_TRUE(Polynomial::gcd(p + 3, q) == p + 3);

    MultivariatePolynomial k = (y ^ 2) + 1;
    EXPECT_TRUE(Polynomial::gcd(x * k, z * k) == k);
    EXPECT_TRUE(Polynomial::gcd(z * k, x * k) == k);
    EXPECT_TRUE(Polynomial::gcd(x * k, (z + 1) * k) == k);
    EXPECT_TRUE(Polynomial::gcd((z + 1) * k, x * k) == k);
    EXPECT_TRUE(Polynomial::gcd(x * k, p * k) == k);
    EXPECT_TRUE(Polynomial::gcd(p * k, x * k) == k);

    Polynomial::Term<int64_t, Polynomial::Monomial> twoxy = 2 * (x * y);
    MultivariatePolynomial twoxyplusx = (2 * x) * y + x;
    EXPECT_TRUE(Polynomial::gcd(twoxy, twoxyplusx) == x);
    EXPECT_TRUE(Polynomial::gcd(twoxyplusx, twoxy) == x);

    MultivariatePolynomial c = x * y + y;
    MultivariatePolynomial d = -1 * c;
    std::cout << "gcd(c,d): " << gcd(c, d) << "\ngcd(d,c): " << gcd(d, c)
              << "\n; c: " << c << std::endl;
    EXPECT_TRUE(Polynomial::gcd(c, d) == (-1 * c));
    EXPECT_TRUE(Polynomial::gcd(d, c) == c);

    MultivariatePolynomial ps = (x ^ 2) - (y ^ 2);
    MultivariatePolynomial qs = x + y;

    Polynomial::divExact(ps, gcd(ps, qs));
    EXPECT_TRUE(ps == (x - y));

    std::cout << "sizeof(uint8_t): " << sizeof(uint8_t) << std::endl;
    std::cout << "sizeof(uint16_t): " << sizeof(uint16_t) << std::endl;
    std::cout << "sizeof(uint32_t): " << sizeof(uint32_t) << std::endl;
    std::cout << "sizeof(uint64_t): " << sizeof(uint64_t) << std::endl;

    std::cout << "sizeof(SmallVector<uint8_t,0>): "
              << sizeof(llvm::SmallVector<uint8_t, 0>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,0>): "
              << sizeof(llvm::SmallVector<uint16_t, 0>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,0>): "
              << sizeof(llvm::SmallVector<uint32_t, 0>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,0>): "
              << sizeof(llvm::SmallVector<uint64_t, 0>) << std::endl;

    std::cout << "sizeof(SmallVector<uint8_t,1>): "
              << sizeof(llvm::SmallVector<uint8_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,1>): "
              << sizeof(llvm::SmallVector<uint16_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,1>): "
              << sizeof(llvm::SmallVector<uint32_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,1>): "
              << sizeof(llvm::SmallVector<uint64_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,2>): "
              << sizeof(llvm::SmallVector<uint8_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,2>): "
              << sizeof(llvm::SmallVector<uint16_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,2>): "
              << sizeof(llvm::SmallVector<uint32_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,2>): "
              << sizeof(llvm::SmallVector<uint64_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,2>): "
              << sizeof(llvm::SmallVector<uint8_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,2>): "
              << sizeof(llvm::SmallVector<uint16_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,2>): "
              << sizeof(llvm::SmallVector<uint32_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,2>): "
              << sizeof(llvm::SmallVector<uint64_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,4>): "
              << sizeof(llvm::SmallVector<uint8_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,4>): "
              << sizeof(llvm::SmallVector<uint16_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,4>): "
              << sizeof(llvm::SmallVector<uint32_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,4>): "
              << sizeof(llvm::SmallVector<uint64_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,8>): "
              << sizeof(llvm::SmallVector<uint8_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,8>): "
              << sizeof(llvm::SmallVector<uint16_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,8>): "
              << sizeof(llvm::SmallVector<uint32_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,8>): "
              << sizeof(llvm::SmallVector<uint64_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,16>): "
              << sizeof(llvm::SmallVector<uint8_t, 16>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,16>): "
              << sizeof(llvm::SmallVector<uint16_t, 16>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,16>): "
              << sizeof(llvm::SmallVector<uint32_t, 16>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,16>): "
              << sizeof(llvm::SmallVector<uint64_t, 16>) << std::endl;

    std::cout << "sizeof(Polynomial::Monomial): "
              << sizeof(Polynomial::Monomial) << std::endl;
    std::cout
        << "sizeof(Polynomial::Multivariate<size_t,Polynomial::Monomoial>): "
        << sizeof(MultivariatePolynomial) << std::endl;
}

TEST(PackedMultivariateMonomialTests, BasicAssertions) {
    Polynomial::PackedMonomial x =
        Polynomial::PackedMonomial(Polynomial::ID{0});
    showln(x);
    Polynomial::PackedMonomial y =
        Polynomial::PackedMonomial(Polynomial::ID{1});
    showln(y);
    Polynomial::PackedMonomial z =
        Polynomial::PackedMonomial(Polynomial::ID{2});
    showln(z);
    EXPECT_EQ(x.degree(), 1);
    EXPECT_EQ(y.degree(), 1);
    EXPECT_EQ(z.degree(), 1);
    x.calcDegree();
    EXPECT_EQ(x.degree(), 1);
    y.calcDegree();
    EXPECT_EQ(y.degree(), 1);
    z.calcDegree();
    EXPECT_EQ(z.degree(), 1);
    typedef Polynomial::Multivariate<int64_t,
                                     Polynomial::PackedMonomial<15, 7>>
        MultivariatePolynomial;
    MultivariatePolynomial xp1z = x * z + z;
    MultivariatePolynomial c0v2 = 10 * xp1z;

    // MultivariatePolynomial c0 = 10*(x*z + z);
    MultivariatePolynomial c0 = 10 * (x * z + x);
    MultivariatePolynomial c1 = 2 * ((x ^ 2) + z);
    MultivariatePolynomial c2 = 2 * (2 - z);
    MultivariatePolynomial c3 = 20 * (x * (z ^ 2));

    int64_t e0 = 0;
    int64_t e1 = 5;
    int64_t e2 = 7;
    int64_t e3 = 10;

    MultivariatePolynomial p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    printf("Polynomial p:\n");
    showln(p);
    printf("\n");

    Polynomial::Univariate<MultivariatePolynomial> ppy =
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

    MultivariatePolynomial a = x * y + y;
    MultivariatePolynomial b = y * z + y;
    printf("gcd(a,b) == M:\n");
    EXPECT_TRUE(Polynomial::gcd(a, b) == MultivariatePolynomial(y));
    printf("GCD: ");
    showln(gcd(a, b)); // we have N + 2? aka z + 1???
    printf("y:  ");
    showln(y);
    printf("Multivariate(y):  ");
    showln(MultivariatePolynomial(y));

    // MultivariatePolynomial q = p * (p + 1);
    MultivariatePolynomial q = p * (p + 1) * (p + 2) * (p + 3);
    // MultivariatePolynomial q = p * (p + 1) * (p + 2); //* (p +
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

    MultivariatePolynomial k = (y ^ 2) + 1;
    EXPECT_TRUE(Polynomial::gcd(x * k, z * k) == k);
    EXPECT_TRUE(Polynomial::gcd(z * k, x * k) == k);
    EXPECT_TRUE(Polynomial::gcd(x * k, (z + 1) * k) == k);
    EXPECT_TRUE(Polynomial::gcd((z + 1) * k, x * k) == k);
    EXPECT_TRUE(Polynomial::gcd(x * k, p * k) == k);
    EXPECT_TRUE(Polynomial::gcd(p * k, x * k) == k);

    Polynomial::Term<int64_t, Polynomial::PackedMonomial<15, 7>> twoxy =
        2 * (x * y);
    MultivariatePolynomial twoxyplusx = (2 * x) * y + x;
    EXPECT_TRUE(Polynomial::gcd(twoxy, twoxyplusx) == x);
    EXPECT_TRUE(Polynomial::gcd(twoxyplusx, twoxy) == x);

    MultivariatePolynomial c = x * y + y;
    MultivariatePolynomial d = -1 * c;
    EXPECT_TRUE(Polynomial::gcd(c, d) == (-1 * c));
    EXPECT_TRUE(Polynomial::gcd(d, c) == c);

    MultivariatePolynomial ps = (x ^ 2) - (y ^ 2);
    MultivariatePolynomial qs = x + y;

    Polynomial::divExact(ps, gcd(ps, qs));
    EXPECT_TRUE(ps == (x - y));

    std::cout << "sizeof(uint8_t): " << sizeof(uint8_t) << std::endl;
    std::cout << "sizeof(uint16_t): " << sizeof(uint16_t) << std::endl;
    std::cout << "sizeof(uint32_t): " << sizeof(uint32_t) << std::endl;
    std::cout << "sizeof(uint64_t): " << sizeof(uint64_t) << std::endl;

    std::cout << "sizeof(SmallVector<uint8_t,0>): "
              << sizeof(llvm::SmallVector<uint8_t, 0>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,0>): "
              << sizeof(llvm::SmallVector<uint16_t, 0>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,0>): "
              << sizeof(llvm::SmallVector<uint32_t, 0>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,0>): "
              << sizeof(llvm::SmallVector<uint64_t, 0>) << std::endl;

    std::cout << "sizeof(SmallVector<uint8_t,1>): "
              << sizeof(llvm::SmallVector<uint8_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,1>): "
              << sizeof(llvm::SmallVector<uint16_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,1>): "
              << sizeof(llvm::SmallVector<uint32_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,1>): "
              << sizeof(llvm::SmallVector<uint64_t, 1>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,2>): "
              << sizeof(llvm::SmallVector<uint8_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,2>): "
              << sizeof(llvm::SmallVector<uint16_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,2>): "
              << sizeof(llvm::SmallVector<uint32_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,2>): "
              << sizeof(llvm::SmallVector<uint64_t, 2>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,2>): "
              << sizeof(llvm::SmallVector<uint8_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,2>): "
              << sizeof(llvm::SmallVector<uint16_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,2>): "
              << sizeof(llvm::SmallVector<uint32_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,2>): "
              << sizeof(llvm::SmallVector<uint64_t, 3>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,4>): "
              << sizeof(llvm::SmallVector<uint8_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,4>): "
              << sizeof(llvm::SmallVector<uint16_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,4>): "
              << sizeof(llvm::SmallVector<uint32_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,4>): "
              << sizeof(llvm::SmallVector<uint64_t, 4>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,8>): "
              << sizeof(llvm::SmallVector<uint8_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,8>): "
              << sizeof(llvm::SmallVector<uint16_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,8>): "
              << sizeof(llvm::SmallVector<uint32_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,8>): "
              << sizeof(llvm::SmallVector<uint64_t, 8>) << std::endl;
    std::cout << "sizeof(SmallVector<uint8_t,16>): "
              << sizeof(llvm::SmallVector<uint8_t, 16>) << std::endl;
    std::cout << "sizeof(SmallVector<uint16_t,16>): "
              << sizeof(llvm::SmallVector<uint16_t, 16>) << std::endl;
    std::cout << "sizeof(SmallVector<uint32_t,16>): "
              << sizeof(llvm::SmallVector<uint32_t, 16>) << std::endl;
    std::cout << "sizeof(SmallVector<uint64_t,16>): "
              << sizeof(llvm::SmallVector<uint64_t, 16>) << std::endl;

    std::cout << "sizeof(Polynomial::Monomial): "
              << sizeof(Polynomial::Monomial) << std::endl;
    std::cout << "sizeof(Polynomial::Multivariate<size_t,Polynomial::"
                 "PackedMonomial<15,7>>): "
              << sizeof(MultivariatePolynomial) << std::endl;
}
