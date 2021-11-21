cmake_minimum_required(VERSION 3.14)

project(
    LoopModels
    VERSION 0.1
    LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -glldb")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -Wall -Wextra -Wpedantic -glldb")

include(FetchContent)

FetchContent_Declare(
    fmtlib
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 5.3.0)
FetchContent_MakeAvailable(fmtlib)

add_subdirectory(src)
add_subdirectory(test)
