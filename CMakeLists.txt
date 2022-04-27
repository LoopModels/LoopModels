cmake_minimum_required(VERSION 3.14)

project(
    LoopModels
    VERSION 0.1
    LANGUAGES CXX)


enable_language(C)
enable_language(CXX)
set (CMAKE_CXX_STANDARD 20)
#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -ggdb")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -ggdb")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -ggdb")

# Colored output?
# https://medium.com/@alasher/colored-c-compiler-output-with-ninja-clang-gcc-10bfe7f2b949
# Not aware of a license agreement on medium content, but was clearly shared with the intent
# people use it, so I'm assuming I can use it. If not, sue me.
# TODO: get an implementation without potential licensing problems.
# I sent a linked in message asking about MIT.


find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(NOT ${LLVM_ENABLE_RTTI})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})


list(APPEND CMAKE_MODULE_PATH ${LLVM_DIR})
include(AddLLVM)

message(STATUS "LLVM Binary DIR: ${LLVM_BINARY_DIR}")
message(STATUS "LLVM Library DIR: ${LLVM_LIBRARY_DIR}")

include(FetchContent)

FetchContent_Declare(
  highsextern
  GIT_REPOSITORY https://github.com/chriselrod/HiGHS.git
  GIT_TAG b2de0801387bf0e79702feabafcf316abfb1b4eb
)
FetchContent_GetProperties(highsextern)
set(highsextern_CMAKE_BUILD_TYPE Release)
# FetchContent_MakeAvailable(HIGHSEXTERN)
if(NOT highsextern_POPULATED)
  FetchContent_Populate(highsextern)
  message(STATUS "HiGHS source dir: ${highsextern_SOURCE_DIR}")
  message(STATUS "HiGHS binary dir: ${highsextern_BINARY_DIR}")
  add_subdirectory(
    ${highsextern_SOURCE_DIR}
    ${highsextern_BINARY_DIR}
  )
  #   EXCLUDE_FROM_ALL)

  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${highsextern_SOURCE_DIR})
endif()
include_directories("${highsextern_SOURCE_DIR}/src")
include_directories("${highsextern_BINARY_DIR}")


option (FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options (-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options (-fcolor-diagnostics)
    endif ()
endif ()


include_directories("${LLVM_BINARY_DIR}/include" "${LLVM_INCLUDE_DIR}")
link_directories("${LLVM_LIBRARY_DIR}")

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(benchmark)
