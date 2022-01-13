set(CMAKE_CXX_STANDARD 20)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.6.1)
FetchContent_MakeAvailable(benchmark)

add_executable(
  polynomial_benchmark
  polynomial_benchmark.cpp
)
target_link_libraries(
  polynomial_benchmark
  benchmark::benchmark
  LLVM
)

