#include <array>
#include <cstdio>
#include <cstring>
#include <string>

auto main(int argc, char **argv) -> int {
  if (argc != 3)
    return 1000;
  std::string modulePath = argv[1];
  std::string examplesPath = argv[2];
  printf("modulePath: %s\n", modulePath.c_str());
  printf("examplesPath: %s\n", examplesPath.c_str());
  std::string fileRoot = examplesPath + std::string("/triangular_solve.");
  auto cmd =
    std::string(
      "opt -mcpu=skylake-avx512 --disable-output -load-pass-plugin=") +
    modulePath +
    std::string(" -passes='turbo-loop' -pass-remarks-analysis='turbo-loop' ") +
    fileRoot + std::string("ll 2>&1");
  printf("cmd: %s", cmd.c_str());
  std::string txtfile = fileRoot + std::string("txt");

  FILE *fpopt = popen(cmd.c_str(), "r");
  FILE *fptxt = fopen(txtfile.c_str(), "r");
  std::array<char, 128> bufopt;
  std::array<char, 128> buftxt;

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
