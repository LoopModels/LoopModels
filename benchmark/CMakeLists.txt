cmake_minimum_required(VERSION 3.20)

project(LoopModelsBenchmarks LANGUAGES C CXX)

option(ENABLE_NATIVE_COMPILATION "Compile with -march=native" ON)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

# ---- compile_commands.json ----
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

CPMAddPackage(
  NAME benchmark
  GITHUB_REPOSITORY google/benchmark
  VERSION 1.7.1
  OPTIONS "BENCHMARK_ENABLE_TESTING Off" "BENCHMARK_ENABLE_LIBPFM On"
)
if(benchmark_ADDED)
  # enable c++11 to avoid compilation errors
  set_target_properties(benchmark PROPERTIES CXX_STANDARD 11)
endif()

file(GLOB benchmarks CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

find_package(LLVM 15 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(${LLVM_DIR}/AddLLVM.cmake)

add_executable(${PROJECT_NAME} ${benchmarks})

message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")

target_link_libraries(${PROJECT_NAME} PRIVATE benchmark LLVM)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(${PROJECT_NAME} PRIVATE -ferror-limit=2)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE -fmax-errors=2 -fconcepts-diagnostics-depth=4)
endif()


if (ENABLE_NATIVE_COMPILATION)
  target_compile_options(${PROJECT_NAME} PRIVATE -march=native)
  check_cxx_compiler_flag("-mprefer-vector-width=512" VEC512)
  if (VEC512)
    target_compile_options(${PROJECT_NAME} PRIVATE -mprefer-vector-width=512)
  endif()
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(
  ${PROJECT_NAME} PROPERTIES ENVIRONMENT WORKING_DIRECTORY=${PROJECT_BINARY_DIR}
)

target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions -fno-rtti -fstrict-aliasing -Wall -Wshadow -Wextra)



