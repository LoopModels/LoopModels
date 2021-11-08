# GoogleTest requires at least C++11
set(CMAKE_CXX_STANDARD 11)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_executable(
  perm_test
  perm_test.cpp
)
add_executable(
  compat_test
  compat_test.cpp
)
add_executable(
  ir_test
  ir_test.cpp
)
target_link_libraries(
  compat_test
  gtest_main
)
target_link_libraries(
  perm_test
  gtest_main
)
target_link_libraries(
  ir_test
  gtest_main
)

include(GoogleTest)
gtest_discover_tests(compat_test)
gtest_discover_tests(perm_test)
gtest_discover_tests(ir_test)
