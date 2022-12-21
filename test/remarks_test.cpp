#include <cstdio>
#include <gtest/gtest.h>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(Remarks, BasicAssertions) {
  FILE *fpopt = popen("opt -mcpu=skylake-avx512 --disable-output -load-pass-plugin=./libTurboLoop.so -passes='turbo-loop' -pass-remarks-analysis='turbo-loop' ./examples/triangular_solve.ll", "r");
  FILE *fptxt = fopen("./examples/triangular_solve.txt", "r");
  std::array<char, 128> bufopt;
  std::array<char, 128> buftxt;
  
  while (fgets(bufopt.data(), sizeof(bufopt), fpopt) != nullptr) {
    EXPECT_NE(fgets(buftxt.data(), sizeof(buftxt), fptxt), nullptr);
    EXPECT_STREQ(buftxt.data(), bufopt.data());
  }
  // should be done
  EXPECT_EQ(fgets(buftxt.data(), sizeof(buftxt), fptxt), nullptr);
  EXPECT_EQ(fclose(fptxt), 0);
  EXPECT_EQ(pclose(fpopt), 0);
}
