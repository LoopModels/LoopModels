cmake_minimum_required(VERSION 3.23)

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
  OPTIONS "BENCHMARK_ENABLE_TESTING Off" "BENCHMARK_ENABLE_LIBPFM On" "BENCHMARK_ENABLE_WERROR Off"
          "BENCHMARK_ENABLE_EXCEPTIONS Off" SYSTEM TRUE
)
if(benchmark_ADDED)
  # enable c++11 to avoid compilation errors
  set_target_properties(benchmark PROPERTIES CXX_STANDARD 11)
endif()

CPMAddPackage(
  NAME unordered_dense
  GITHUB_REPOSITORY chriselrod/unordered_dense
  GIT_TAG fnoexceptions SYSTEM TRUE
)

# file(GLOB_RECURSE headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)
file(GLOB benchmarks CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

find_package(LLVM 16 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(${LLVM_DIR}/AddLLVM.cmake)
# message(STATUS "headers: ${headers}") add_executable(${PROJECT_NAME} ${headers} ${benchmarks})
add_executable(${PROJECT_NAME} ${benchmarks})

message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(
  ${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../include ${PROJECT_SOURCE_DIR}/include
)
find_package(OpenMP)
target_link_libraries(
  ${PROJECT_NAME} PRIVATE benchmark::benchmark LLVM unordered_dense::unordered_dense
                          OpenMP::OpenMP_CXX
)

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR (CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM"))
  target_compile_options(${PROJECT_NAME} PRIVATE -ferror-limit=2 -fcolor-diagnostics)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(
    ${PROJECT_NAME} PRIVATE -fmax-errors=2 -fconcepts-diagnostics-depth=4
                            -fno-semantic-interposition -fdiagnostics-color=always -fverbose-asm
  )
endif()

if(ENABLE_NATIVE_COMPILATION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
    target_compile_options(${PROJECT_NAME} PRIVATE -xhost -qopt-zmm-usage=high)
  else()
    target_compile_options(${PROJECT_NAME} PRIVATE -march=native) # -fno-unroll-loops)
    check_cxx_compiler_flag("-mprefer-vector-width=512" VEC512)
    if(VEC512)
      target_compile_options(${PROJECT_NAME} PRIVATE -mprefer-vector-width=512)
    endif()
  endif()
endif()
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES CXX_STANDARD 20
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
)
set_target_properties(
  ${PROJECT_NAME} PROPERTIES ENVIRONMENT WORKING_DIRECTORY=${PROJECT_BINARY_DIR}
)

target_compile_options(
  ${PROJECT_NAME}
  PRIVATE -fno-exceptions
          -fno-rtti
          -fstrict-aliasing
          -fno-plt
          -fstrict-overflow
          -fomit-frame-pointer
          -fno-signed-zeros
          -fassociative-math
          -ffinite-math-only
          -fno-trapping-math
          -Wall
          -Wshadow
          -Wextra
          -save-temps
)
if(CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
  target_compile_options(${PROJECT_NAME} PRIVATE -fiopenmp)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -fopenmp)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
  target_compile_options(${PROJECT_NAME} PRIVATE -masm=intel)
endif()
