cmake_minimum_required(VERSION 3.20)

project(LoopModelsTests LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(ENABLE_TEST_EXCEPTIONS "Enable test exceptions" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

# ---- compile_commands.json ----
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CPMAddPackage("gh:onqtam/doctest@2.4.9")
CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")
CPMFindPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  GIT_TAG release-1.12.1
  VERSION 1.12.1
  OPTIONS "INSTALL_GTEST OFF" "BUILD_GMOCK OFF" "gtest_hide_internal_symbols ON"
)

if(TEST_INSTALLED_VERSION)
  find_package(LoopModels REQUIRED)
else()
  CPMAddPackage(NAME LoopModels SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

file(GLOB tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
# list(FILTER tests EXCLUDE REGEX "remarks.*") for remarks test
find_package(LLVM 15 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
# include(AddLLVM)
include(${LLVM_DIR}/AddLLVM.cmake)

add_executable(${PROJECT_NAME} ${tests})

message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
add_dependencies(${PROJECT_NAME} LoopModels)
target_include_directories(
  ${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../include ${LLVM_INCLUDE_DIRS}
)
target_precompile_headers(
  ${PROJECT_NAME}
  PRIVATE
  <cstddef>
  <algorithm>
  <bit>
  <cassert>
  <cmath>
  <concepts>
  <cstddef>
  <cstdint>
  <gtest/gtest.h>
  <iterator>
  <limits>
  <llvm/ADT/ArrayRef.h>
  <llvm/ADT/Optional.h>
  <llvm/ADT/SmallVector.h>
  <llvm/Support/raw_os_ostream.h>
  <llvm/Support/raw_ostream.h>
  <numeric>
  <optional>
  <ostream>
  <random>
  <ranges>
  <string>
  <tuple>
  <type_traits>
  <utility>
)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE GTest::gtest_main LLVM)
set(CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(
  ${PROJECT_NAME} PROPERTIES ENVIRONMENT WORKING_DIRECTORY=${PROJECT_BINARY_DIR}
)

if(ENABLE_TEST_EXCEPTIONS)
  target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions)
endif()

if(NOT ((USE_SANITIZER MATCHES "([Uu]ndefined)") OR (USE_SANITIZER MATCHES
                                                     "([Aa]ddress);([Uu]ndefined)"))
)
  # we use rtti, which is incompatible with the vptr check:
  # https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-rtti)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(${PROJECT_NAME} PRIVATE -ferror-limit=8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE -fmax-errors=8 -fconcepts-diagnostics-depth=4)
endif()
# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(LoopModels PUBLIC -Wall -Wpedantic -Wextra -Wshadow)
    # target_compile_options(LoopModels PUBLIC -Wall -Wpedantic -Wextra -Werror)
  elseif(MSVC)
    target_compile_options(LoopModels PUBLIC /W4 /WX)
    # target_compile_definitions(${PROJECT_NAME} PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
  endif()
endif()

# ---- code coverage ----

message(STATUS "ENABLE_TEST_COVERAGE: ${ENABLE_TEST_COVERAGE}")
if(ENABLE_TEST_COVERAGE)
  target_compile_options(LoopModels PUBLIC -O0 -g --coverage)
  target_link_options(LoopModels PUBLIC --coverage)
  target_compile_options(${PROJECT_NAME} PUBLIC -O0 -g --coverage)
  target_link_options(${PROJECT_NAME} PUBLIC --coverage)
  add_custom_target(
    clean_coverage
    COMMAND rm -f ${PROJECT_BINARY_DIR}/CMakeFiles/LoopModelsTests.dir/*.gcda
    COMMAND rm -f ${PROJECT_BINARY_DIR}/_deps/loopmodels-build/CMakeFiles/LoopModels.dir/lib/*.gcda
    COMMENT "Clearing out old coverage data."
  )
  add_dependencies(${PROJECT_NAME} clean_coverage)
  message(STATUS "Test coverage enabled.")
endif()

# ---- Add Unit Tests ----

enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME ${PROJECT_NAME} COMMAND
# ${PROJECT_NAME})

# include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})

# ---- Add opt tests ----
# find_program(OPT NAME opt PATHS LLVM_BINARY_DIR)
