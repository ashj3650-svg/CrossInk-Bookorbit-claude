add_executable(BatteryUsageTest
  BatteryUsageTest.cpp
)

target_include_directories(BatteryUsageTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(BatteryUsageTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BatteryUsageTest)
