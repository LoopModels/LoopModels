# Based on ModernCppStarter https://github.com/TheLartians/ModernCppStarter

# 3.25 for SYSTEM support
cmake_minimum_required(VERSION 3.25)

project(
  LoopModels
  VERSION 0.1
  LANGUAGES C CXX
)
# TODO: disable C when this is fixed: https://github.com/llvm/llvm-project/issues/53950
set(OMIT_FRAME_POINTER
    "ReleaseOnly"
    CACHE STRING "Omit frame pointers (ON/OFF/ReleaseOnly)"
)
set_property(CACHE OMIT_FRAME_POINTER PROPERTY STRINGS ON OFF ReleaseOnly)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- compile_commands.json ----
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.8.0")

# ---- Dependencies ----
# Here, we are preferring CPMFindPackage, as relying on system libraries can save a lot of compile
# time.
# ---- LLVM ----

# CPMFindPackage( NAME LLVM GIT_TAG llvmorg-15.0.6 GITHUB_REPOSITORY llvm/llvm-project OPTIONS
# "LLVM_USE_SPLIT_DWARF ON" "LLVM_BUILD_LLVM_DYLIB ON" "LLVM_ENABLE_PROJECTS
# clang;clang-tools-extra;lld;lldb;polly;pstl" "LLVM_ENABLE_RUNTIMES all" "LLVM_ENABLE_RTTI OFF"
# "BUILD_SHARED_LIBS OFF" "LLVM_CCACHE_BUILD ON"  "LLVM_OPTIMIZED_TABLEGEN ON" "LLVM_ENABLE_LTO ON"
# "LLVM_ENABLE_Z3_SOLVER ON" )
find_package(LLVM 16 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
# include(AddLLVM)
include(${LLVM_DIR}/AddLLVM.cmake)
# llvm/cmake/modules/AddLLVM.cmake
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Found LLVM_CMAKE_DIR ${LLVM_CMAKE_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# # "LLVM_TARGETS_TO_BUILD X86;AArch64" # ---- google/benchmark ---- CPMFindPackage( NAME benchmark
# GITHUB_REPOSITORY google/benchmark VERSION 1.7.1 OPTIONS "BENCHMARK_ENABLE_TESTING Off" )
# if(benchmark_ADDED) # enable c++11 to avoid compilation errors set_target_properties(benchmark
# PROPERTIES CXX_STANDARD 11) endif()

# message(STATUS "ENABLE_TEST_COVERAGE: ${ENABLE_TEST_COVERAGE}") if(ENABLE_TEST_COVERAGE)
# add_compile_options(${PROJECT_NAME} PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
# target_link_options(${PROJECT_NAME} PUBLIC -fprofile-arcs -ftest-coverage) message(STATUS "Lib
# coverage enabled.") endif()

CPMAddPackage(
  NAME unordered_dense
  GITHUB_REPOSITORY chriselrod/unordered_dense
  GIT_TAG fnoexceptions
)

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.cpp)

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} MODULE ${headers} ${sources})
set(CXX_STANDARD_REQUIRED ON)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES CXX_STANDARD 20
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
)
target_compile_options(
  ${PROJECT_NAME} PRIVATE -fno-exceptions -fno-rtti -fstrict-aliasing -fno-plt -fstrict-overflow
)
if(OMIT_FRAME_POINTER STREQUAL "ReleaseOnly")
  target_compile_options(
    ${PROJECT_NAME} PRIVATE "$<IF:$<CONFIG:Release>,-fomit-frame-pointer,-fno-omit-frame-pointer>"
  )
elseif(OMIT_FRAME_POINTER)
  target_compile_options(${PROJECT_NAME} PRIVATE -fomit-frame-pointer)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-omit-frame-pointer)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(
    ${PROJECT_NAME} PRIVATE -fno-semantic-interposition -fdiagnostics-color=always
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(${PROJECT_NAME} PRIVATE -fcolor-diagnostics)
endif()

# MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

FetchContent_Declare(
  Math
  GIT_REPOSITORY git@github.com:LoopModels/Math.git
  GIT_TAG origin/main
)
FetchContent_MakeAvailable(Math)

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE LLVM unordered_dense::unordered_dense Math)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(
  ${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)
target_precompile_headers(
  ${PROJECT_NAME}
  PRIVATE
  <algorithm>
  <cassert>
  <cmath>
  <cstddef>
  <cstdint>
  <limits>
  <llvm/ADT/ArrayRef.h>
  <llvm/ADT/SmallPtrSet.h>
  <llvm/ADT/SmallVector.h>
  <llvm/Analysis/AssumptionCache.h>
  <llvm/Analysis/Delinearization.h>
  <llvm/Analysis/LoopInfo.h>
  <llvm/Analysis/OptimizationRemarkEmitter.h>
  <llvm/Analysis/ScalarEvolution.h>
  <llvm/Analysis/ScalarEvolutionExpressions.h>
  <llvm/Analysis/TargetLibraryInfo.h>
  <llvm/Analysis/TargetTransformInfo.h>
  <llvm/IR/BasicBlock.h>
  <llvm/IR/CFG.h>
  <llvm/IR/Constant.h>
  <llvm/IR/Constants.h>
  <llvm/IR/DataLayout.h>
  <llvm/IR/Dominators.h>
  <llvm/IR/IRBuilder.h>
  <llvm/IR/InstrTypes.h>
  <llvm/IR/Instruction.h>
  <llvm/IR/Instructions.h>
  <llvm/IR/PassManager.h>
  <llvm/IR/Type.h>
  <llvm/IR/Value.h>
  <llvm/Support/Casting.h>
  <llvm/Support/Debug.h>
  <llvm/Support/raw_ostream.h>
  <llvm/Transforms/Utils/LoopUtils.h>
  <llvm/Transforms/Utils/ScalarEvolutionExpander.h>
  <ranges>
  <utility>
)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
  NAME ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION}
  NAMESPACE ${PROJECT_NAME}
  BINARY_DIR ${PROJECT_BINARY_DIR}
  INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
  INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
  VERSION_HEADER "${VERSION_HEADER_LOCATION}"
  COMPATIBILITY SameMinorVersion
  DEPENDENCIES "LLVM 15.0.6"
)
