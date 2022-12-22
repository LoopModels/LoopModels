#include <array>
#include <cstdio>
#include <cstring>
#include <string>
// #include <gtest/gtest.h>

auto main(int argc, char **argv) -> int {
  if (argc != 3)
    return 1000;
  std::string modulePath = argv[1];
  std::string examplesPath = argv[2];
  // puts(modulePath.c_str());
  // puts(examplesPath.c_str());
  std::string fileRoot = examplesPath + std::string("/triangular_solve.");
  auto cmd =
    std::string(
      "opt -mcpu=skylake-avx512 --disable-output -load-pass-plugin=") +
    modulePath +
    std::string(" -passes='turbo-loop' -pass-remarks-analysis='turbo-loop' ") +
    fileRoot + std::string("ll 2>&1");
  // puts(cmd.c_str());
  std::string txtfile = fileRoot + std::string("txt");

  FILE *fpopt = popen(cmd.c_str(), "r");
  FILE *fptxt = fopen(txtfile.c_str(), "r");
  std::array<char, 128> bufopt;
  std::array<char, 128> buftxt;

  int count = 0;
  while (fgets(bufopt.data(), sizeof(bufopt), fpopt) != nullptr) {
    if (fgets(buftxt.data(), sizeof(buftxt), fptxt) == nullptr)
      return 1001;
    if (int diff = strcmp(bufopt.data(), buftxt.data()))
      return diff;
    ++count;
  }
  if (count < 276)
    return 1002;
  // should be done
  if (fgets(buftxt.data(), sizeof(buftxt), fptxt) != nullptr)
    return 1003;
  if (pclose(fpopt) != 0)
    return 1004;
  if (fclose(fptxt) != 0)
    return 1005;
  return 0;
}

// // NOLINTNEXTLINE(modernize-use-trailing-return-type)
// TEST(Remarks, BasicAssertions) {
//   FILE *fpopt = popen("opt -mcpu=skylake-avx512 --disable-output
//   -load-pass-plugin=./libTurboLoop.so -passes='turbo-loop'
//   -pass-remarks-analysis='turbo-loop' ./examples/triangular_solve.ll", "r");
//   FILE *fptxt = fopen("./examples/triangular_solve.txt", "r");
//   std::array<char, 128> bufopt;
//   std::array<char, 128> buftxt;

//   while (fgets(bufopt.data(), sizeof(bufopt), fpopt) != nullptr) {
//     EXPECT_NE(fgets(buftxt.data(), sizeof(buftxt), fptxt), nullptr);
//     EXPECT_STREQ(buftxt.data(), bufopt.data());
//   }
//   // should be done
//   EXPECT_EQ(fgets(buftxt.data(), sizeof(buftxt), fptxt), nullptr);
//   EXPECT_EQ(fclose(fptxt), 0);
//   EXPECT_EQ(pclose(fpopt), 0);
// }
