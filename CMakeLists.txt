cmake_minimum_required(VERSION 3.14)

project(
    LoopModels
    VERSION 0.1
    LANGUAGES CXX)

enable_language(C)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

list(APPEND CMAKE_MODULE_PATH ${LLVM_DIR})

message(STATUS "LLVM Binary DIR: ${LLVM_BINARY_DIR}")
message(STATUS "LLVM Library DIR: ${LLVM_LIBRARY_DIR}")

include_directories("${LLVM_BINARY_DIR}/include" "${LLVM_INCLUDE_DIR}")
link_directories("${LLVM_LIBRARY_DIR}")

include(FetchContent)

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(benchmark)
