set(CMAKE_CXX_STANDARD 20)

find_package(benchmark REQUIRED)

add_executable(
  polynomial_benchmark
  polynomial_benchmark.cpp
)
target_link_libraries(
  polynomial_benchmark
  benchmark::benchmark
  LLVM
)

