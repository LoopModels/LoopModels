#include <array>
#include <cstdio>
#include <cstring>

auto main(int argc, char **argv) -> int {
  if (argc != 3)
    return 1000;
  char *modulePath = argv[1];
  char *examplesPath = argv[2];
  printf("modulePath: %s\n", modulePath);
  printf("examplesPath: %s\n", examplesPath);
  const char *testfile = "triangular_solve";
  std::array<char, 512> bufopt;
  sprintf(
    bufopt.data(),
    "opt -mcpu=skylake-avx512 --disable-output -load-pass-plugin=%s "
    "-passes='turbo-loop' -pass-remarks-analysis='turbo-loop' %s/%s.ll 2>&1",
    modulePath, examplesPath, testfile);

  printf("cmd: %s", bufopt.data());
  FILE *fpopt = popen(bufopt.data(), "r");

  sprintf(bufopt.data(), "%s/%s.txt", examplesPath, testfile);
  FILE *fptxt = fopen(bufopt.data(), "r");

  std::array<char, 512> buftxt;
  int count = 0;
  int failed = -1;
  while (fgets(bufopt.data(), sizeof(bufopt), fpopt) != nullptr) {
    if (fgets(buftxt.data(), sizeof(buftxt), fptxt) == nullptr)
      return 1001;
    if (int diff = strcmp(bufopt.data(), buftxt.data())) {
      printf("line %d differed at %d\ntxt: %s\nopt:\n%s\n", count, diff,
             buftxt.data(), bufopt.data());
      failed = count;
      break;
    }
    ++count;
  }
  if (failed >= 0) {
    while (fgets(bufopt.data(), sizeof(bufopt), fpopt) != nullptr)
      puts(bufopt.data());
    return ++failed;
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
