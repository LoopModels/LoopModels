cmake_minimum_required(VERSION 3.28.2)
option(ENABLE_STACK_PROTECTOR "Use stack protection" OFF)
option(USE_MODULES "Use C++ Modules" OFF)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.29.0")
  cmake_policy(SET CMP0167 NEW) # new find boost
endif()

project(
  LoopModelsModules
  VERSION 1.0
  LANGUAGES C CXX)
# LANGUAGES CXX)

# Dependencies; Boost:
find_package(Boost 1.81.0 REQUIRED COMPONENTS headers)
# LLVM
set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)
find_package(LLVM REQUIRED CONFIG)
string(COMPARE GREATER_EQUAL ${LLVM_PACKAGE_VERSION} 17 LLVM_VERSION_ACCEPTABLE)
if(NOT LLVM_VERSION_ACCEPTABLE)
  message(FATAL_ERROR "Require LLVM >= 17, but found: ${LLVM_PACKAGE_VERSION}")
endif()
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
# include(AddLLVM)
include(${LLVM_DIR}/AddLLVM.cmake)

file(GLOB_RECURSE modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx")

add_subdirectory("${PROJECT_SOURCE_DIR}/../extern/Math" "extern_build/math")

if(USE_MODULES)
  add_library(${PROJECT_NAME} OBJECT)
  target_sources(${PROJECT_NAME} PUBLIC FILE_SET CXX_MODULES FILES ${modules})
  target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-exceptions)
  target_compile_definitions(${PROJECT_NAME} PUBLIC USE_MODULE)
  if(NOT ENABLE_RTTI)
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-rtti)
  endif()
  if(ENABLE_STACK_PROTECTOR)
    target_compile_options(${PROJECT_NAME} PRIVATE -fstack-protector-strong)
  endif()
  target_include_directories(${PROJECT_NAME} SYSTEM
                             PRIVATE ${LLVM_INCLUDE_DIRS})
  target_link_libraries(${PROJECT_NAME} LLVM Boost::headers Math)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(
      ${PROJECT_NAME} PRIVATE -ferror-limit=8 -fcolor-diagnostics -ftime-trace)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(
      ${PROJECT_NAME}
      PRIVATE -fmax-errors=8 -fconcepts-diagnostics-depth=4
              -fno-semantic-interposition -fdiagnostics-color=always
              -Wno-comma-subscript -Wno-psabi)
  endif()

else()
  add_library(${PROJECT_NAME} INTERFACE ${modules})
  target_include_directories(
    ${PROJECT_NAME}
    INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
              $<INSTALL_INTERFACE:${PROJECT_NAME}-${PROJECT_VERSION}>)
  target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)
  target_compile_options(${PROJECT_NAME} INTERFACE -fno-exceptions)
  if(NOT ENABLE_RTTI)
    target_compile_options(${PROJECT_NAME} INTERFACE -fno-rtti)
  endif()
  if(ENABLE_STACK_PROTECTOR)
    target_compile_options(${PROJECT_NAME} INTERFACE -fstack-protector-strong)
  endif()
  target_include_directories(${PROJECT_NAME} SYSTEM
                             INTERFACE ${LLVM_INCLUDE_DIRS})
  target_link_libraries(${PROJECT_NAME} INTERFACE LLVM Boost::headers Math)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)

message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")

include(GNUInstallDirs)

if(USE_MODULES)
  install(
    TARGETS ${PROJECT_NAME}
    EXPORT loopmodels-modules
    FILE_SET CXX_MODULES
    # There's currently no convention for this location
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/loopmodels/mod
    FILE_SET HEADERS
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} # Same as default, could be omitted
    INCLUDES
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(
    EXPORT loopmodels-modules
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/loopmodels
    CXX_MODULES_DIRECTORY .)
else()
  install(
    TARGETS ${PROJECT_NAME}
    EXPORT loopmodels-modules
    FILE_SET HEADERS
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} # Same as default, could be omitted
    INCLUDES
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT math-targets DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/math)
endif()
