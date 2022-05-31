set(CMAKE_CXX_STANDARD 20)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG 6d50251d8e6ed6b7f6eb1e12c558d2808fb4ddaa)
FetchContent_MakeAvailable(benchmark)
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=restrict --save-temps")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --save-temps=obj")
endif()


add_executable(
  polynomial_benchmark
  polynomial_benchmark.cpp
)
target_link_libraries(
  polynomial_benchmark
  benchmark::benchmark
  LLVM
)

add_executable(
  constraint_pruning_benchmark
  constraint_pruning_benchmark.cpp
)
target_link_libraries(
  constraint_pruning_benchmark
  benchmark::benchmark
  LLVM
)

