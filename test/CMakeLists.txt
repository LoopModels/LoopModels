include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

## add_executable(
##  perm_test
##  perm_test.cpp
##  )
#
#add_executable(
#  poset_test
#  poset_test.cpp
#)
#add_executable(
#  compat_test
#  compat_test.cpp
#)
#add_executable(
#  ir_test
#  ir_test.cpp
#)
#add_executable(
#  bitset_test
#  bitset_test.cpp
#)
#add_executable(
#  symbolics_test
#  symbolics_test.cpp
#)
#target_link_libraries(
#  poset_test
#  gtest_main
#  LLVM
#)
#target_link_libraries(
#  compat_test
#  gtest_main
#  LLVM
#)
## target_link_libraries(
##   perm_test
##   gtest_main
##   LLVM
## )
#target_link_libraries(
#  ir_test
#  gtest_main
#  LLVM
#)
#target_link_libraries(
#  bitset_test
#  gtest_main
#  LLVM
#)
#target_link_libraries(
#  symbolics_test
#  gtest_main
#  LLVM
#)
add_executable(
  unimodularization_test
  unimodularization_test.cpp
)
target_link_libraries(
  unimodularization_test
  gtest_main
  LLVM
)

include(GoogleTest)
#gtest_discover_tests(compat_test)
## gtest_discover_tests(perm_test)
#gtest_discover_tests(ir_test)
#gtest_discover_tests(bitset_test)
gtest_discover_tests(unimodularization_test)
