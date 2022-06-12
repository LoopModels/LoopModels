#include "../include/LinearDiophantine.hpp"
#include "../include/Math.hpp"
#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <random>

TEST(LinearDiophantineTest, BasicAssertions) {
    {
        std::vector perm{2, 3, 4};
        do {
            int64_t x = perm[0], y = perm[1], z = perm[2];
            auto opts = linearDiophantine(1, std::make_tuple(x, y, z));
            EXPECT_TRUE(opts.hasValue());
            if (opts.hasValue()) {
                auto [a, b, c] = opts.getValue();
                EXPECT_EQ(1, a * x + b * y + c * z);
                // std::cout << "sols = [ " << a << ", " << b << ", " << c
                // << " ]\n";
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
    }
    {
        std::vector perm{2, 3, 4, 5};
        do {
            int64_t w = perm[0], x = perm[1], y = perm[2], z = perm[3];
            auto opts = linearDiophantine(1, std::make_tuple(w, x, y, z));
            EXPECT_TRUE(opts.hasValue());
            if (opts.hasValue()) {
                auto [a, b, c, d] = opts.getValue();
                EXPECT_EQ(1, a * w + b * x + c * y + d * z);
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
    }
    {
        std::vector perm{2, 3, 4, 5, 6};
        do {
            int64_t w = perm[0], x = perm[1], y = perm[2], z = perm[3],
                     u = perm[4];
            auto opts = linearDiophantine(1, std::make_tuple(w, x, y, z, u));
            EXPECT_TRUE(opts.hasValue());
            if (opts.hasValue()) {
                auto [a, b, c, d, e] = opts.getValue();
                EXPECT_EQ(1, a * w + b * x + c * y + d * z + u * e);
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-100, 100);
    size_t solvedOneCounter = 0;
    size_t numIters = 100000;
    for (size_t n = 0; n < numIters; ++n) {
        int64_t a0 = distrib(gen);
        int64_t a1 = distrib(gen);
        int64_t a2 = distrib(gen);
        int64_t a3 = distrib(gen);
        int64_t a4 = distrib(gen);
        int64_t a5 = distrib(gen);
        int64_t a6 = distrib(gen);
        auto t = std::make_tuple(a0, a1, a2, a3, a4, a5, a6);

        int64_t b0 = distrib(gen);
        int64_t b1 = distrib(gen);
        int64_t b2 = distrib(gen);
        int64_t b3 = distrib(gen);
        int64_t b4 = distrib(gen);
        int64_t b5 = distrib(gen);
        int64_t b6 = distrib(gen);
        int64_t d =
            a0 * b0 + a1 * b1 + a2 * b2 + a3 * b3 + a4 * b4 + a5 * b5 + a6 * b6;
        auto opt = linearDiophantine(d, t);
        EXPECT_TRUE(opt.hasValue());
        if (opt.hasValue()) {
            auto [x0, x1, x2, x3, x4, x5, x6] = opt.getValue();
            EXPECT_EQ(d, a0 * x0 + a1 * x1 + a2 * x2 + a3 * x3 + a4 * x4 +
                             a5 * x5 + a6 * x6);
        }
        opt = linearDiophantine(1, t);
        if (opt.hasValue()) {
            ++solvedOneCounter;
            auto [x0, x1, x2, x3, x4, x5, x6] = opt.getValue();
            EXPECT_EQ(1, a0 * x0 + a1 * x1 + a2 * x2 + a3 * x3 + a4 * x4 +
                             a5 * x5 + a6 * x6);
        }
        auto opt1 = linearDiophantine(d * a0, std::make_tuple(a0));
        EXPECT_TRUE(opt1.hasValue());
        if (opt1.hasValue()) {
            if (a0) {
                EXPECT_EQ(std::get<0>(opt1.getValue()), d);
            } else {
                EXPECT_EQ(std::get<0>(opt1.getValue()), 0);
            }
        }
        if (std::abs(a0) > 1) {
            // guaranteed coprime
            auto opt1 = linearDiophantine(a0 + 1, std::make_tuple(a0));
            EXPECT_FALSE(opt1.hasValue());
        }
    }
    std::cout << "solved: " << solvedOneCounter << " / " << numIters
              << std::endl;
}
