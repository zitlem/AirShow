find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(test_pipeline
  test_pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/pipeline/MediaPipeline.cpp
)

target_include_directories(test_pipeline PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_pipeline PRIVATE
  GTest::gtest_main
  GTest::gmock
  PkgConfig::GST
)

# Register test filters matching VALIDATION.md per-task verification map
gtest_discover_tests(test_pipeline
  TEST_SUFFIX ""
  PROPERTIES TIMEOUT 30
)

add_executable(test_discovery
  test_discovery.cpp
)

target_include_directories(test_discovery PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_discovery PRIVATE
  GTest::gtest_main
  GTest::gmock
)

gtest_discover_tests(test_discovery
  TEST_SUFFIX ""
  PROPERTIES TIMEOUT 30
)
