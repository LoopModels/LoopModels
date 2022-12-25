#include <array>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(Remarks, BasicAssertions) {
  const char *testfile = "triangular_solve";
  std::array<char, 1024> bufopt;
  sprintf(bufopt.data(),
          "opt -mcpu=skylake-avx512 --disable-output "
          "-load-pass-plugin=_deps/loopmodels-build/libLoopModels.so "
          "-passes=turbo-loop -pass-remarks-analysis=turbo-loop "
          "../../test/examples/%s.ll 2>&1 | diff ../../test/examples/%s.txt -",
          testfile, testfile);

  int rc = system(bufopt.data());
  EXPECT_EQ(rc, 0);
}
