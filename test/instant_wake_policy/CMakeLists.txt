add_executable(InstantWakePolicyTest
  InstantWakePolicyTest.cpp
)

target_include_directories(InstantWakePolicyTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(InstantWakePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(InstantWakePolicyTest)
