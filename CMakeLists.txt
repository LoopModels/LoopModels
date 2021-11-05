cmake_minimum_required(VERSION 3.14)

project(
    LoopModels
    VERSION 0.1
    LANGUAGES CXX)

include(FetchContent)

# Only do these if this is the main project, and not if it is included through add_subdirectory
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # GoogleTest requires at least C++11
    set(CMAKE_CXX_STANDARD 11)

    # Optionally set things like CMAKE_CXX_STANDARD, CMAKE_POSITION_INDEPENDENT_CODE here

    # Let's ensure -std=c++xx instead of -std=g++xx
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Let's nicely support folders in IDEs
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Testing only available if this is the main app
    # Note this needs to be done in the main CMakeLists
    # since it calls enable_testing, which must be in the
    # main CMakeLists.
    include(CTest)

    # Docs only available if this is the main app
    find_package(Doxygen)
    if(Doxygen_FOUND)
        add_subdirectory(docs)
    else()
        message(STATUS "Doxygen not found, not building docs")
    endif()
endif()

include(FetchContent)

FetchContent_Declare(
    fmtlib
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 5.3.0)
FetchContent_MakeAvailable(fmtlib)

add_subdirectory(src)
add_subdirectory(test)
