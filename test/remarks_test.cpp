#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

auto main(int argc, char **argv) -> int {
  if (argc != 3)
    return 1000;
  char *modulePath = argv[1];
  char *examplesPath = argv[2];
  printf("modulePath: %s\n", modulePath);
  printf("examplesPath: %s\n", examplesPath);
  const char *testfile = "triangular_solve";
  std::array<char, 1024> bufopt;
  sprintf(
    bufopt.data(),
    "opt -mcpu=skylake-avx512 --disable-output -load-pass-plugin=%s "
    "-passes=turbo-loop -pass-remarks-analysis=turbo-loop %s/%s.ll 2>&1 | "
    "diff %s/%s.txt -",
    modulePath, examplesPath, testfile, examplesPath, testfile);

  int rc = system(bufopt.data());
  printf("\n\nretcode: %d\n", rc);
  return rc != 0;
}
