cmake_minimum_required(VERSION 3.14)

project(
    LoopModels
    VERSION 0.1
    LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")

find_package(LLVM REQUIRED HINTS "${LLVM_CMAKE_PATH}")
list(APPEND CMAKE_MODULE_PATH ${LLVM_DIR})

include_directories("${LLVM_BINARY_DIR}/include" "${LLVM_INCLUDE_DIR}")
link_directories("${LLVM_LIBRARY_DIR}")

include(FetchContent)

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(benchmark)
