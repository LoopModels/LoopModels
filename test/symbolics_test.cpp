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

    Polynomial::UnivariateTerm<intptr_t> t = 2*x;

    Polynomial::Univariate<intptr_t> p = 2*(x^10) + (x^7) + 7*(x^2) + 2*x + 3*x;

    printf("t:\n");
    showln(t);
    printf("t coef:\n");
    showln(t.coefficient);
    printf("t exponent:\n");
    showln(t.exponent);
    printf("p:\n");
    showln(p);
    printf("p, num terms:\n");
    showln(p.terms.size());


}

