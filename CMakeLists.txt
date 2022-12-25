# Based on ModernCppStarter
# https://github.com/TheLartians/ModernCppStarter

# 3.20 minimum for C++23
cmake_minimum_required(VERSION 3.20)

project(
  LoopModels
  VERSION 0.1
  LANGUAGES C CXX
)
# TODO: disable C when this is fixed: https://github.com/llvm/llvm-project/issues/53950

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
# Here, we are preferring CPMFindPackage, as relying on system
# libraries can save a lot of compile time.
# ---- LLVM ----

# CPMFindPackage(
#   NAME LLVM
#   GIT_TAG llvmorg-15.0.6
#   GITHUB_REPOSITORY llvm/llvm-project
#   OPTIONS "LLVM_USE_SPLIT_DWARF ON" "LLVM_BUILD_LLVM_DYLIB ON" "LLVM_ENABLE_PROJECTS clang;clang-tools-extra;lld;lldb;polly;pstl" "LLVM_ENABLE_RUNTIMES all" "LLVM_ENABLE_RTTI OFF" "BUILD_SHARED_LIBS OFF" "LLVM_CCACHE_BUILD ON"  "LLVM_OPTIMIZED_TABLEGEN ON" "LLVM_ENABLE_LTO ON" "LLVM_ENABLE_Z3_SOLVER ON"
# )
find_package(LLVM 15 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
# include(AddLLVM)
include(${LLVM_DIR}/AddLLVM.cmake)
# llvm/cmake/modules/AddLLVM.cmake 
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Found LLVM_CMAKE_DIR ${LLVM_CMAKE_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# # "LLVM_TARGETS_TO_BUILD X86;AArch64"
# # ---- google/benchmark ----
# CPMFindPackage(
#   NAME benchmark
#   GITHUB_REPOSITORY google/benchmark
#   VERSION 1.7.1
#   OPTIONS "BENCHMARK_ENABLE_TESTING Off"
# )
# if(benchmark_ADDED)
#   # enable c++11 to avoid compilation errors
#   set_target_properties(benchmark PROPERTIES CXX_STANDARD 11)
# endif()


# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.cpp")

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} MODULE ${headers} ${sources})
set(CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_compile_options(${PROJECT_NAME} PRIVATE -fno-rtti -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-semantic-interposition)
  add_compile_options (-fdiagnostics-color=always)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options (-fcolor-diagnostics)
endif ()

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE LLVM)

target_include_directories(${PROJECT_NAME} PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
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
