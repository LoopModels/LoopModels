cmake_minimum_required(VERSION 3.14)
include(CheckCXXCompilerFlag)
project(
    LoopModels
    VERSION 0.1
    LANGUAGES CXX)


enable_language(C)
enable_language(CXX)
set (CMAKE_CXX_STANDARD 20)
# target_compile_options(${name} PRIVATE )
#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -ggdb")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -ggdb")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-semantic-interposition")
endif()
check_cxx_compiler_flag("-mprefer-vector-width=512" VEC512)
if ("${VEC512}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mprefer-vector-width=512")
endif()
find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(NOT ${LLVM_ENABLE_RTTI})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -DCXXOPTS_NO_RTTI")
endif()

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})


message(STATUS "LLVM Binary DIR: ${LLVM_BINARY_DIR}")
message(STATUS "LLVM Library DIR: ${LLVM_LIBRARY_DIR}")

option (FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options (-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options (-fcolor-diagnostics)
    endif ()
endif ()
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=restrict")
endif()


include_directories("${LLVM_BINARY_DIR}/include" "${LLVM_INCLUDE_DIR}")
link_directories("${LLVM_LIBRARY_DIR}")

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(benchmark)
