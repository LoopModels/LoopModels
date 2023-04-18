#include <array>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

// sample command to generate a test file, run from test dir
// clang-format off
// LD_PRELOAD=/usr/lib64/libasan.so.8 LSAN_OPTIONS='suppressions=../../test/leak_warning_suppressions.txt' opt -mcpu=skylake-avx512 --disable-output --load-pass-plugin=/home/chriselrod/Documents/progwork/cxx/LoopModels/buildgcc/test/_deps/loopmodels-build/libLoopModels.so -passes=turbo-loop -pass-remarks-analysis=turbo-loop ~/Documents/progwork/cxx/LoopModels/test/examples/triangular_solve.ll 2>&1 | head -n300 > ../../test/examples/triangular_solve.txt
// clang-format on

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(Remarks, BasicAssertions) {
  const char *testfile = "triangular_solve";
  std::array<char, 1024> bufopt;
  // if using asan
  // sprintf(bufopt.data(),
  //         "opt -mcpu=skylake-avx512 --disable-output "
  //         "-load-pass-plugin=_deps/loopmodels-build/libLoopModels.so "
  //         "-passes=turbo-loop -pass-remarks-analysis=turbo-loop "
  //         "../../test/examples/%s.ll 2>&1 | head -n300 | diff "
  //         "../../test/examples/%s.txt -",
  //         testfile, testfile);
  sprintf(
    bufopt.data(),
    "LD_PRELOAD=/usr/lib64/libasan.so.8 opt -mcpu=skylake-avx512 "
    "--disable-output "
    "-load-pass-plugin=_deps/loopmodels-build/libLoopModels.so "
    "-passes=turbo-loop -pass-remarks-analysis=turbo-loop "
    "../../test/examples/%s.ll 2>&1 | sdiff -l - ../../test/examples/%s.txt",
    testfile, testfile);

  int rc = system(bufopt.data());
  EXPECT_EQ(rc, 0);
}
