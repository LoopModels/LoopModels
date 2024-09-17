cmake_minimum_required(VERSION 3.28.2)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)
option(ENABLE_LLD "Use lld for linking" ON)
option(TEST_LOOPMODELS "Test LoopModels" OFF) # ON FIXME
option(POLYMATHNOEXPLICITSIMDARRAY "No explicit SIMD for Array operations" OFF)
option(USE_MODULES "Use C++ Modules" OFF)

project(LoopModelsTests LANGUAGES CXX)
# --- Import tools ----

include(../extern/Math/cmake/tools.cmake)

# ---- Dependencies ----

include(../extern/Math/cmake/CPM.cmake)

# ---- compile_commands.json ----
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CPMAddPackage("gh:onqtam/doctest@2.4.9")
cpmaddpackage("gh:TheLartians/Format.cmake@1.7.3")
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG 305e5a238b3c8d11266fbafd85520fb6b3184851 # May 24, 2024 main
  FIND_PACKAGE_ARGS NAMES GTest)
FetchContent_MakeAvailable(googletest)

# https://cliutils.gitlab.io/modern-cmake/chapters/projects/submodule.html
# find_package(Git QUIET) if(GIT_FOUND AND EXISTS
# "${PROJECT_SOURCE_DIR}/../.git") # Update submodules as needed
# option(GIT_SUBMODULE "Check submodules during build" ON) if(GIT_SUBMODULE)
# message(STATUS "Submodule update") execute_process( COMMAND ${GIT_EXECUTABLE}
# submodule update --init --recursive --rebase WORKING_DIRECTORY
# ${CMAKE_CURRENT_SOURCE_DIR} RESULT_VARIABLE GIT_SUBMOD_RESULT) if(NOT
# GIT_SUBMOD_RESULT EQUAL "0") message( FATAL_ERROR "git submodule update --init
# --recursive --rebase failed with ${GIT_SUBMOD_RESULT}, please checkout
# submodules" ) endif() endif() endif()
message(
  STATUS
    "Checking for existence of: ${PROJECT_SOURCE_DIR}/../extern/Math/CMakeLists.txt"
)
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/../extern/Math/CMakeLists.txt")
  message(
    FATAL_ERROR
      "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again."
  )
endif()

if((USE_SANITIZER MATCHES "([Uu]ndefined)") OR (USE_SANITIZER MATCHES
                                                "([Aa]ddress);([Uu]ndefined)"))
  set(ENABLE_RTTI
      ON
      CACHE BOOL "Use RTTI")
  set(USE_MI_MALLOC
      OFF
      CACHE BOOL "No mimalloc with sanitizers")
  set(USE_JE_MALLOC
      OFF
      CACHE BOOL "No jemalloc with sanitizers")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(ENABLE_STACK_PROTECTOR
        ON
        CACHE BOOL "Use stack potection")
  endif()
else()
  set(ENABLE_STACK_PROTECTOR
      ON
      CACHE BOOL "Use stack potection")
endif()
# add_subdirectory("${PROJECT_SOURCE_DIR}/../extern/Math" "extern_build/math")

add_subdirectory("${PROJECT_SOURCE_DIR}/../mod" LoopModels)
# if(TEST_LOOPMODELS) if(TEST_INSTALLED_VERSION) find_package(LoopModels
# REQUIRED) else() add_subdirectory(.. LoopModels) endif() endif()

# ---- Create binary ----

# FIXME: re-enable tests file(GLOB tests CONFIGURE_DEPENDS
# ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(
  GLOB
  tests
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/cache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/comparator_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/compat_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/dependence_meanstddev_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/dependence_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/dict_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/index_graph_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/permutation_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/simple_dependence_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/triangular_solve_test.cpp
  # ${CMAKE_CURRENT_SOURCE_DIR}/remarks_test.cpp
)

# list(FILTER tests EXCLUDE REGEX "remarks.*") for remarks test
# set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
# set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC) find_package(LLVM REQUIRED CONFIG)
# string(COMPARE GREATER_EQUAL ${LLVM_PACKAGE_VERSION} 17
# LLVM_VERSION_ACCEPTABLE) if(NOT LLVM_VERSION_ACCEPTABLE) message(FATAL_ERROR
# "Require LLVM >= 17, but found: ${LLVM_PACKAGE_VERSION}") endif() list(APPEND
# CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR}) # include(AddLLVM)
# include(${LLVM_DIR}/AddLLVM.cmake)

add_executable(${PROJECT_NAME} ${tests})
if(USE_MODULES)
  target_sources(
    ${PROJECT_NAME} PRIVATE FILE_SET CXX_MODULES FILES
                            "${CMAKE_CURRENT_SOURCE_DIR}/TestUtilities.cxx")
# else()
  # target_include_directories(${PROJECT_NAME} PRIVATE
  # ${PROJECT_SOURCE_DIR}/../mod)
endif()

if(POLYMATHNOEXPLICITSIMDARRAY)
  target_compile_definitions(${PROJECT_NAME} PUBLIC POLYMATHNOEXPLICITSIMDARRAY)
endif()

message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
if(NOT USE_MODULES)
  target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE
    <boost/unordered/unordered_flat_map.hpp>
    <boost/unordered/unordered_flat_set.hpp>
    <llvm/ADT/ArrayRef.h>
    <llvm/ADT/SmallVector.h>
    <llvm/Analysis/LoopInfo.h>
    <llvm/Analysis/ScalarEvolution.h>
    <llvm/Analysis/ScalarEvolutionExpressions.h>
    <llvm/Analysis/TargetLibraryInfo.h>
    <llvm/Analysis/TargetTransformInfo.h>
    <llvm/CodeGen/BasicTTIImpl.h>
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
    "../extern/Math/mod/Alloc/Arena.cxx"
    "../extern/Math/mod/Math/Array.cxx"
    "../extern/Math/mod/Math/ManagedArray.cxx"
    "../extern/Math/mod/Containers/Pair.cxx"
    "../extern/Math/mod/Containers/Tuple.cxx"
    "../extern/Math/mod/Containers/UnrolledList.cxx"
    "../extern/Math/mod/Math/Comparisons.cxx"
    "../extern/Math/mod/Math/Constructors.cxx"
    "../extern/Math/mod/Math/GreatestCommonDivisor.cxx"
    "../extern/Math/mod/Math/LinearAlgebra.cxx"
    "../extern/Math/mod/Math/NormalForm.cxx"
    "../extern/Math/mod/Math/SOA.cxx"
    "../extern/Math/mod/Math/Simplex.cxx"
    "../extern/Math/mod/Math/StaticArrays.cxx"
    "../extern/Math/mod/Utilities/Invariant.cxx"
    "../extern/Math/mod/Utilities/ListRanges.cxx"
    "../extern/Math/mod/Utilities/Valid.cxx"
    <algorithm>
    <cassert>
    <cmath>
    <concepts>
    <cstddef>
    <cstdint>
    <limits>
    <ranges>
    <utility>
    <concepts>
    <gtest/gtest.h>
    <iostream>
    <iterator>
    <limits>
    <numeric>
    <optional>
    <random>
    <ranges>
    <string>
    <type_traits>
    <utility>)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE GTest::gtest_main
                                              LoopModelsModules)
set(CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)
set_target_properties(
  ${PROJECT_NAME} PROPERTIES ENVIRONMENT
                             WORKING_DIRECTORY=${PROJECT_BINARY_DIR})

target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions)

if((USE_SANITIZER MATCHES "([Uu]ndefined)") OR (USE_SANITIZER MATCHES
                                                "([Aa]ddress);([Uu]ndefined)"))
  # we use don't rtti, which is incompatible with the vptr check:
  # https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-omit-frame-pointer
                                                 -fsanitize-trap=all)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-rtti)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(
    ${PROJECT_NAME} PRIVATE -ferror-limit=8 -fcolor-diagnostics
                            -fstack-protector-strong -ftime-trace)
  add_compile_options(-stdlib=libc++)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(
    ${PROJECT_NAME}
    PRIVATE -fmax-errors=8 -fconcepts-diagnostics-depth=4
            -fno-semantic-interposition -fdiagnostics-color=always -Wno-psabi)
  if(NOT ((USE_SANITIZER MATCHES "([Aa]ddress)")
          OR (USE_SANITIZER MATCHES "([Aa]ddress);([Uu]ndefined)")))
    target_compile_options(${PROJECT_NAME} PRIVATE -fstack-protector-strong)
    # target_compile_options(LoopModels PRIVATE -fstack-protector-strong)
  endif()
endif()
# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES
                                              "GNU")
    # -Werror?
    if(TEST_LOOPMODELS)
      target_compile_options(LoopModels PUBLIC -Wall -Wpedantic -Wextra
                                               -Wshadow)
    endif()
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wpedantic -Wextra
                                                  -Wshadow)
  elseif(MSVC and TEST_LOOPMODELS)
    target_compile_options(LoopModels PUBLIC /W4 /WX)
  endif()
endif()
# target_compile_options(LoopModels PRIVATE -D_GLIBCXX_DEBUG)
# target_compile_options(${PROJECT_NAME} PRIVATE -D_GLIBCXX_DEBUG)

if(TEST_LOOPMODELS)
  target_compile_options(LoopModels PRIVATE -D_GLIBCXX_ASSERTIONS)
endif()
target_compile_options(${PROJECT_NAME} PRIVATE -D_GLIBCXX_ASSERTIONS)

# When upgrading to cmake 3.29, we can use CMAKE_LINKER_TYPE
# https://cmake.org/cmake/help/latest/variable/CMAKE_LINKER_TYPE.html
if(ENABLE_LLD)
  target_link_options(${PROJECT_NAME} PRIVATE -fuse-ld=lld)
  if(TEST_LOOPMODELS)
    target_link_options(LoopModels PRIVATE -fuse-ld=lld)
  endif()
endif()
# ---- code coverage ----

message(STATUS "ENABLE_TEST_COVERAGE: ${ENABLE_TEST_COVERAGE}")
if(ENABLE_TEST_COVERAGE)
  if(TEST_LOOPMODELS)
    target_compile_options(LoopModels PUBLIC -O0 -g --coverage)
    target_link_options(LoopModels PUBLIC --coverage)
  endif()
  target_compile_options(${PROJECT_NAME} PUBLIC -O0 -g --coverage)
  target_link_options(${PROJECT_NAME} PUBLIC --coverage)
  add_custom_target(
    clean_coverage
    COMMAND rm -f ${PROJECT_BINARY_DIR}/CMakeFiles/LoopModelsTests.dir/*.gcda
    COMMAND
      rm -f
      ${PROJECT_BINARY_DIR}/_deps/loopmodels-build/CMakeFiles/LoopModels.dir/lib/*.gcda
    COMMENT "Clearing out old coverage data.")
  add_dependencies(${PROJECT_NAME} clean_coverage)
  message(STATUS "Test coverage enabled.")
endif()

# ---- Add Unit Tests ----

enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake
# tests. For other testing frameworks add the tests target instead:
# add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})

# ---- Add opt tests ----
# find_program(OPT NAME opt PATHS LLVM_BINARY_DIR)
