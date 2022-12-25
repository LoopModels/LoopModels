cmake_minimum_required(VERSION 3.20)

project(LoopModelsTests LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

# CPMAddPackage("gh:onqtam/doctest@2.4.9")
CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")
CPMFindPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  GIT_TAG release-1.12.1
  VERSION 1.10.0
  OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt"
)

if(TEST_INSTALLED_VERSION)
  find_package(LoopModels REQUIRED)
else()
  CPMAddPackage(NAME LoopModels SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

file(GLOB tests CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
# list(FILTER tests EXCLUDE REGEX "remarks.*")
# for remarks test
find_package(LLVM 15 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
# include(AddLLVM)
include(${LLVM_DIR}/AddLLVM.cmake)



add_executable(${PROJECT_NAME} ${tests})
# target_link_libraries(${PROJECT_NAME} doctest::doctest Greeter::Greeter)
message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
add_dependencies(${PROJECT_NAME} LoopModels)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../include)
target_include_directories(${PROJECT_NAME} PRIVATE ${LLVM_INCLUDE_DIRS})
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE gtest_main LLVM)
set(CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES 
  ENVIRONMENT
  WORKING_DIRECTORY=${PROJECT_BINARY_DIR}
)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(LoopModels PUBLIC -Wall -Wpedantic -Wextra)
    # target_compile_options(LoopModels PUBLIC -Wall -Wpedantic -Wextra -Werror)
  elseif(MSVC)
    target_compile_options(LoopModels PUBLIC /W4 /WX)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
  endif()
endif()

# ---- Add Unit Tests ----

enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME ${PROJECT_NAME} COMMAND
# ${PROJECT_NAME})

# include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(LoopModels PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(LoopModels PUBLIC -fprofile-arcs -ftest-coverage)
endif()

