include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest
  GIT_TAG 9d21db9e0a60a1ea61ec19331c9bc0dd33e907b1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()
# add_executable(
#  perm_test
#  perm_test.cpp
#  )

add_executable(
  poset_test
  poset_test.cpp
)
add_executable(
  compat_test
  compat_test.cpp
)
add_executable(
  ir_test
  ir_test.cpp
)
add_executable(
  bitset_test
  bitset_test.cpp
)
add_executable(
  symbolics_test
  symbolics_test.cpp
)
target_link_libraries(
  poset_test
  gtest_main
  LLVM
)
target_link_libraries(
  compat_test
  gtest_main
  LLVM
)
# target_link_libraries(
#   perm_test
#   gtest_main
#   LLVM
# )
target_link_libraries(
  ir_test
  gtest_main
  LLVM
)
target_link_libraries(
  bitset_test
  gtest_main
  LLVM
)
target_link_libraries(
  symbolics_test
  gtest_main
  LLVM
)
add_executable(
  unimodularization_test
  unimodularization_test.cpp
)
target_link_libraries(
  unimodularization_test
  gtest_main
  LLVM
)

add_executable(
  linear_diophantine_test
  linear_diophantine_test.cpp
)
target_link_libraries(
  linear_diophantine_test
  gtest_main
  LLVM
)

add_executable(
  normal_form_test
  normal_form_test.cpp
)
target_link_libraries(
  normal_form_test
  gtest_main
  LLVM
)

add_executable(
  linear_algebra_test
  linear_algebra_test.cpp
)
target_link_libraries(
  linear_algebra_test
  gtest_main
  LLVM
)

add_executable(
  orthogonalize_test
  orthogonalize_test.cpp
)
target_link_libraries(
  orthogonalize_test
  gtest_main
  LLVM
)

add_executable(
  dependence_test
  dependence_test.cpp
)
target_link_libraries(
  dependence_test
  gtest_main
  LLVM
)

add_executable(
  dependence_test2
  dependence_test2.cpp
)
target_link_libraries(
  dependence_test2
  gtest_main
  LLVM
)

add_executable(
  edge_detection_test
  edge_detection_test.cpp
)
target_link_libraries(
  edge_detection_test
  gtest_main
  LLVM
)

#add_executable(
#  highs_test
#  highs_test.cpp
#)
#target_link_libraries(
#  highs_test
#  gtest_main
#  LLVM
#  libhighs
#)

include(GoogleTest)
gtest_discover_tests(compat_test)
# gtest_discover_tests(perm_test)
gtest_discover_tests(ir_test)
gtest_discover_tests(bitset_test)
gtest_discover_tests(unimodularization_test)
gtest_discover_tests(linear_diophantine_test)
gtest_discover_tests(normal_form_test)
gtest_discover_tests(linear_algebra_test)
gtest_discover_tests(orthogonalize_test)
gtest_discover_tests(dependence_test)
gtest_discover_tests(edge_detection_test)
#gtest_discover_tests(highs_test)
